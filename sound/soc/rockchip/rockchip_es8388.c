/*
 * Rockchip machine ASoC driver for boards using a ES8388 CODEC.
 *
 * Copyright (c) 2016, Collabora Ltd.
 *
 * Authors: Sjoerd Simons <sjoerd.simons@collabora.com>,
 *	    Romain Perier <romain.perier@collabora.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "rockchip_i2s.h"

#define DRV_NAME "rockchip-snd-es8388"

struct rk_es8388_drvdata {
	int gpio_hp_en;
	int gpio_hp_det;
};

static int rk_es8388_hp_power(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *k, int event)
{
	struct rk_es8388_drvdata *machine = snd_soc_card_get_drvdata(w->dapm->card);

	if (!gpio_is_valid(machine->gpio_hp_en))
		return 0;

	gpio_set_value_cansleep(machine->gpio_hp_en,
				SND_SOC_DAPM_EVENT_ON(event));

	return 0;
}

static struct snd_soc_jack headphone_jack;
static struct snd_soc_jack_pin headphone_jack_pins[] = {
	{
		.pin = "Headphone",
		.mask = SND_JACK_HEADPHONE
	},
};

static const struct snd_soc_dapm_widget rk_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", rk_es8388_hp_power),
};

static const struct snd_soc_dapm_route rk_audio_map[] = {
	{"Headphone", NULL, "LOUT2"},
	{"Headphone", NULL, "ROUT2"},
};

static const struct snd_kcontrol_new rk_mc_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
};

static int rk_hw_params(struct snd_pcm_substream *substream,
			struct snd_pcm_hw_params *params)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int mclk;

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 64000:
	case 96000:
		mclk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		mclk = 11289600;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk,
				     SND_SOC_CLOCK_OUT);

	if (ret && ret != -ENOTSUPP) {
		dev_err(codec_dai->dev, "Can't set codec clock %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk,
				     SND_SOC_CLOCK_IN);
	if (ret && ret != -ENOTSUPP) {
		dev_err(codec_dai->dev, "Can't set codec clock %d\n", ret);
		return ret;
	}

	return 0;
}

static struct snd_soc_jack_gpio rk_hp_jack_gpio = {
	.name = "Headphone detection",
	.report = SND_JACK_HEADPHONE,
	.debounce_time = 150
};

static int rk_init(struct snd_soc_pcm_runtime *runtime)
{
	struct rk_es8388_drvdata *machine = snd_soc_card_get_drvdata(runtime->card);

	/* Enable Headset Jack detection */
	if (gpio_is_valid(machine->gpio_hp_det)) {
		snd_soc_card_jack_new(runtime->card, "Headphone Jack",
				      SND_JACK_HEADPHONE, &headphone_jack,
				      headphone_jack_pins,
				      ARRAY_SIZE(headphone_jack_pins));
		rk_hp_jack_gpio.gpio = machine->gpio_hp_det;
		snd_soc_jack_add_gpios(&headphone_jack, 1, &rk_hp_jack_gpio);
	}

	return 0;
}

static struct snd_soc_ops rk_ops = {
	.hw_params = rk_hw_params,
};

static struct snd_soc_dai_link rk_dailink = {
	.name = "ES8388",
	.stream_name = "Audio",
	.codec_dai_name = "es8328-hifi-analog",
	.init = rk_init,
	.ops = &rk_ops,
	/* Set es8388 as slave */
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBS_CFS,
};

static struct snd_soc_card snd_soc_card_rk = {
	.name = "I2S-ES8388",
	.dai_link = &rk_dailink,
	.num_links = 1,
	.num_aux_devs = 0,
	.dapm_widgets = rk_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rk_dapm_widgets),
	.dapm_routes = rk_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rk_audio_map),
	.controls = rk_mc_controls,
	.num_controls = ARRAY_SIZE(rk_mc_controls),
};

static int snd_rk_mc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct snd_soc_card *card = &snd_soc_card_rk;
	struct device_node *np = pdev->dev.of_node;
	struct rk_es8388_drvdata *machine;

	machine = devm_kzalloc(&pdev->dev, sizeof(struct rk_es8388_drvdata),
			       GFP_KERNEL);

	if (!machine)
		return -ENOMEM;

	card->dev = &pdev->dev;

	machine->gpio_hp_det = of_get_named_gpio(np,
		"rockchip,hp-det-gpios", 0);
	if (machine->gpio_hp_det == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	machine->gpio_hp_en = of_get_named_gpio(np,
		"rockchip,hp-en-gpios", 0);
	if (machine->gpio_hp_en == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	if (gpio_is_valid(machine->gpio_hp_en)) {
		ret = devm_gpio_request_one(&pdev->dev, machine->gpio_hp_en,
					    GPIOF_OUT_INIT_LOW, "hp_en");
		if (ret) {
			dev_err(card->dev, "cannot get hp_en gpio\n");
			return ret;
		}
	}

	ret = snd_soc_of_parse_card_name(card, "rockchip,model");
	if (ret) {
		dev_err(card->dev, "SoC parse card name failed %d\n", ret);
		return ret;
	}

	rk_dailink.codec_of_node = of_parse_phandle(np, "rockchip,audio-codec",
						    0);
	if (!rk_dailink.codec_of_node) {
		dev_err(&pdev->dev,
			"Property 'rockchip,audio-codec' missing or invalid\n");
		return -EINVAL;
	}

	rk_dailink.cpu_of_node = of_parse_phandle(np, "rockchip,i2s-controller",
						  0);
	if (!rk_dailink.cpu_of_node) {
		dev_err(&pdev->dev,
			"Property 'rockchip,i2s-controller' missing or invalid\n");
		return -EINVAL;
	}

	rk_dailink.platform_of_node = rk_dailink.cpu_of_node;
	snd_soc_card_set_drvdata(card, machine);

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(&pdev->dev,
			"Soc register card failed %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, card);

	return ret;
}

static const struct of_device_id rockchip_es8388_of_match[] = {
	{ .compatible = "rockchip,rockchip-audio-es8388", },
	{},
};

MODULE_DEVICE_TABLE(of, rockchip_es8388_of_match);

static struct platform_driver snd_rk_es8388_driver = {
	.probe = snd_rk_mc_probe,
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = rockchip_es8388_of_match,
	},
};

module_platform_driver(snd_rk_es8388_driver);

MODULE_AUTHOR("Sjoerd Simons");
MODULE_DESCRIPTION("Rockchip es8388 machine ASoC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
