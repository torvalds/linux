/* arch/arm/mach-exynos/gc1-jack.c
 *
 * Copyright (C) 2012 Samsung Electronics Co, Ltd
 *
 * Based on mach-exynos/mach-midas.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <mach/gpio-midas.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/sec_jack.h>
#include <mach/gc1-jack.h>

static void sec_set_jack_micbias(bool on)
{
	if ((system_rev == 3) || (system_rev == 4) || (system_rev == 5))
		set_wm1811_micbias2(on);
	else
		gpio_set_value(GPIO_EAR_MIC_BIAS_EN, on);
}

/* FIXME: these values are for GD as hw rev 6 */
static struct sec_jack_zone sec_jack_zones[] = {
	{
		/* adc == 0, unstable zone, default to 3pole if it stays
		 * in this range for 300ms (15ms delays, 20 samples)
		 */
		.adc_high = 0,
		.delay_ms = 15,
		.check_count = 20,
		.jack_type = SEC_HEADSET_3POLE,
	},
	{
		/* 0 < adc <= 1200, unstable zone, default to 3pole if it stays
		 * in this range for 300ms (15ms delays, 20 samples)
		 */
		.adc_high = 1200,
		.delay_ms = 10,
		.check_count = 80,
		.jack_type = SEC_HEADSET_3POLE,
	},
	{
		/* 1200 < adc <= 2100, unstable zone, default to 3pole if it
		 * stays in this range for 800ms (10ms delays, 80 samples)
		 */
		.adc_high = 2100,
		.delay_ms = 10,
		.check_count = 10,
		.jack_type = SEC_HEADSET_3POLE,
	},
	{
		/* 2100 < adc <= 4100, 4 pole zone, default to 4pole if it
		 * stays in this range for 100ms (10ms delays, 10 samples)
		 */
		.adc_high = 4100,
		.delay_ms = 10,
		.check_count = 15,
		.jack_type = SEC_HEADSET_4POLE,
	},
	{
		/* adc > 4100, unstable zone, default to 3pole if it stays
		 * in this range for two seconds (10ms delays, 200 samples)
		 */
		.adc_high = 0x7fffffff,
		.delay_ms = 10,
		.check_count = 200,
		.jack_type = SEC_HEADSET_3POLE,
	},
};

/* To support 3-buttons earjack */
static struct sec_jack_buttons_zone sec_jack_buttons_zones[] = {
	{
		/* 0 <= adc <=350, stable zone */
		.code = KEY_MEDIA,
		.adc_low = 0,
		.adc_high = 350,
	},
	{
		/* 351 <= adc <= 920, stable zone */
		.code = KEY_VOLUMEUP,
		.adc_low = 351,
		.adc_high = 920,
	},
	{
		/* 921 <= adc <= 1700, stable zone */
		.code = KEY_VOLUMEDOWN,
		.adc_low = 921,
		.adc_high = 1700,
	},
};
/* END FIXME: these values is for GD as hw rev 6 */

static struct sec_jack_platform_data sec_jack_data = {
	.set_micbias_state = sec_set_jack_micbias,
	.zones = sec_jack_zones,
	.num_zones = ARRAY_SIZE(sec_jack_zones),
	.buttons_zones = sec_jack_buttons_zones,
	.num_buttons_zones = ARRAY_SIZE(sec_jack_buttons_zones),
	.det_gpio = GPIO_DET_35,
	.send_end_gpio = GPIO_EAR_SEND_END,
};

static struct platform_device sec_device_jack = {
	.name = "sec_jack",
	.id = 1,		/* will be used also for gpio_event id */
	.dev.platform_data = &sec_jack_data,
};
void __init gc1_jack_init(void)
{

	/* Ear Microphone BIAS */
	int err;
	err = gpio_request(GPIO_EAR_MIC_BIAS_EN, "EAR MIC");
	if (err) {
		pr_err(KERN_ERR "GPIO_EAR_MIC_BIAS_EN GPIO set error!\n");
		return;
	}
	gpio_direction_output(GPIO_EAR_MIC_BIAS_EN, 1);
	gpio_set_value(GPIO_EAR_MIC_BIAS_EN, 0);
	gpio_free(GPIO_EAR_MIC_BIAS_EN);

	platform_device_register(&sec_device_jack);
}
