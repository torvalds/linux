/* arch/arm/mach-exynos/grande-jack.c
 *
 * Copyright (C) 2012 Samsung Electronics Co, Ltd
 *
 * Based on mach-exynos/mach-grande.c
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

static void sec_set_jack_micbias(bool on)
{
	gpio_set_value(GPIO_MIC_BIAS_EN, on);
}

static struct sec_jack_platform_data sec_jack_data = {
	.set_micbias_state = sec_set_jack_micbias,
};

static struct platform_device sec_device_jack = {
	.name = "sec_jack",
	.id = 1,		/* will be used also for gpio_event id */
	.dev.platform_data = &sec_jack_data,
};
void __init grande_jack_init(void)
{
	/* Ear Microphone BIAS */
	int err;
	err = gpio_request(GPIO_MIC_BIAS_EN, "EAR MIC");
	if (err) {
		pr_err(KERN_ERR "GPIO_MIC_BIAS_EN GPIO set error!\n");
		return;
	}
	gpio_direction_output(GPIO_MIC_BIAS_EN, 1);
	gpio_set_value(GPIO_MIC_BIAS_EN, 0);
	gpio_free(GPIO_MIC_BIAS_EN);

	platform_device_register(&sec_device_jack);
}

