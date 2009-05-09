/*
 * Copyright (C) 2008 Juergen Beisert (kernel@pengutronix.de)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/irq.h>

#include <asm/mach/arch.h>

#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/mmc.h>
#include <mach/imxfb.h>
#include <mach/iomux.h>

#include "devices.h"

static int pcm970_sdhc2_get_ro(struct device *dev)
{
	return gpio_get_value(GPIO_PORTC + 28);
}

static int pcm970_sdhc2_pins[] = {
	PB4_PF_SD2_D0,
	PB5_PF_SD2_D1,
	PB6_PF_SD2_D2,
	PB7_PF_SD2_D3,
	PB8_PF_SD2_CMD,
	PB9_PF_SD2_CLK,
};

static int pcm970_sdhc2_init(struct device *dev, irq_handler_t detect_irq, void *data)
{
	int ret;

	ret = mxc_gpio_setup_multiple_pins(pcm970_sdhc2_pins,
		ARRAY_SIZE(pcm970_sdhc2_pins), "sdhc2");
	if(ret)
		return ret;

	ret = request_irq(IRQ_GPIOC(29), detect_irq, 0,
				"imx-mmc-detect", data);
	if (ret)
		goto out_release_gpio;

	set_irq_type(IRQ_GPIOC(29), IRQF_TRIGGER_FALLING);

	ret = gpio_request(GPIO_PORTC + 28, "imx-mmc-ro");
	if (ret)
		goto out_release_gpio;

	mxc_gpio_mode((GPIO_PORTC | 28) | GPIO_GPIO | GPIO_IN);
	gpio_direction_input(GPIO_PORTC + 28);

	return 0;

out_release_gpio:
	mxc_gpio_release_multiple_pins(pcm970_sdhc2_pins,
			ARRAY_SIZE(pcm970_sdhc2_pins));
	return ret;
}

static void pcm970_sdhc2_exit(struct device *dev, void *data)
{
	free_irq(IRQ_GPIOC(29), data);
	gpio_free(GPIO_PORTC + 28);
	mxc_gpio_release_multiple_pins(pcm970_sdhc2_pins,
			ARRAY_SIZE(pcm970_sdhc2_pins));
}

static struct imxmmc_platform_data sdhc_pdata = {
	.get_ro = pcm970_sdhc2_get_ro,
	.init = pcm970_sdhc2_init,
	.exit = pcm970_sdhc2_exit,
};

static int mxc_fb_pins[] = {
	PA5_PF_LSCLK,	PA6_PF_LD0,	PA7_PF_LD1,	PA8_PF_LD2,
	PA9_PF_LD3,	PA10_PF_LD4,	PA11_PF_LD5,	PA12_PF_LD6,
	PA13_PF_LD7,	PA14_PF_LD8,	PA15_PF_LD9,	PA16_PF_LD10,
	PA17_PF_LD11,	PA18_PF_LD12,	PA19_PF_LD13,	PA20_PF_LD14,
	PA21_PF_LD15,	PA22_PF_LD16,	PA23_PF_LD17,	PA24_PF_REV,
	PA25_PF_CLS,	PA26_PF_PS,	PA27_PF_SPL_SPR, PA28_PF_HSYNC,
	PA29_PF_VSYNC,	PA30_PF_CONTRAST, PA31_PF_OE_ACD
};

static int pcm038_fb_init(struct platform_device *pdev)
{
	return mxc_gpio_setup_multiple_pins(mxc_fb_pins,
			ARRAY_SIZE(mxc_fb_pins), "FB");
}

static int pcm038_fb_exit(struct platform_device *pdev)
{
	mxc_gpio_release_multiple_pins(mxc_fb_pins, ARRAY_SIZE(mxc_fb_pins));

	return 0;
}

/*
 * Connected is a portrait Sharp-QVGA display
 * of type: LQ035Q7DH06
 */
static struct imx_fb_platform_data pcm038_fb_data = {
	.pixclock	= 188679, /* in ps (5.3MHz) */
	.xres		= 240,
	.yres		= 320,

	.bpp		= 16,
	.hsync_len	= 7,
	.left_margin	= 5,
	.right_margin	= 16,

	.vsync_len	= 1,
	.upper_margin	= 7,
	.lower_margin	= 9,
	.fixed_screen_cpu = 0,

	/*
	 * - HSYNC active high
	 * - VSYNC active high
	 * - clk notenabled while idle
	 * - clock not inverted
	 * - data not inverted
	 * - data enable low active
	 * - enable sharp mode
	 */
	.pcr		= 0xFA0080C0,
	.pwmr		= 0x00A903FF,
	.lscr1		= 0x00120300,
	.dmacr		= 0x00020010,

	.init = pcm038_fb_init,
	.exit = pcm038_fb_exit,
};

/*
 * system init for baseboard usage. Will be called by pcm038 init.
 *
 * Add platform devices present on this baseboard and init
 * them from CPU side as far as required to use them later on
 */
void __init pcm970_baseboard_init(void)
{
	mxc_register_device(&mxc_fb_device, &pcm038_fb_data);
	mxc_register_device(&mxc_sdhc_device1, &sdhc_pdata);
}
