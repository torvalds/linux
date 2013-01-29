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

#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/can/platform/sja1000.h>

#include <asm/mach/arch.h>

#include "common.h"
#include "devices-imx27.h"
#include "hardware.h"
#include "iomux-mx27.h"

static const int pcm970_pins[] __initconst = {
	/* SDHC */
	PB4_PF_SD2_D0,
	PB5_PF_SD2_D1,
	PB6_PF_SD2_D2,
	PB7_PF_SD2_D3,
	PB8_PF_SD2_CMD,
	PB9_PF_SD2_CLK,
	/* display */
	PA5_PF_LSCLK,
	PA6_PF_LD0,
	PA7_PF_LD1,
	PA8_PF_LD2,
	PA9_PF_LD3,
	PA10_PF_LD4,
	PA11_PF_LD5,
	PA12_PF_LD6,
	PA13_PF_LD7,
	PA14_PF_LD8,
	PA15_PF_LD9,
	PA16_PF_LD10,
	PA17_PF_LD11,
	PA18_PF_LD12,
	PA19_PF_LD13,
	PA20_PF_LD14,
	PA21_PF_LD15,
	PA22_PF_LD16,
	PA23_PF_LD17,
	PA24_PF_REV,
	PA25_PF_CLS,
	PA26_PF_PS,
	PA27_PF_SPL_SPR,
	PA28_PF_HSYNC,
	PA29_PF_VSYNC,
	PA30_PF_CONTRAST,
	PA31_PF_OE_ACD,
	/*
	 * it seems the data line misses a pullup, so we must enable
	 * the internal pullup as a local workaround
	 */
	PD17_PF_I2C_DATA | GPIO_PUEN,
	PD18_PF_I2C_CLK,
	/* Camera */
	PB10_PF_CSI_D0,
	PB11_PF_CSI_D1,
	PB12_PF_CSI_D2,
	PB13_PF_CSI_D3,
	PB14_PF_CSI_D4,
	PB15_PF_CSI_MCLK,
	PB16_PF_CSI_PIXCLK,
	PB17_PF_CSI_D5,
	PB18_PF_CSI_D6,
	PB19_PF_CSI_D7,
	PB20_PF_CSI_VSYNC,
	PB21_PF_CSI_HSYNC,
};

static int pcm970_sdhc2_get_ro(struct device *dev)
{
	return gpio_get_value(GPIO_PORTC + 28);
}

static int pcm970_sdhc2_init(struct device *dev, irq_handler_t detect_irq, void *data)
{
	int ret;

	ret = request_irq(gpio_to_irq(IMX_GPIO_NR(3, 29)), detect_irq,
			  IRQF_TRIGGER_FALLING, "imx-mmc-detect", data);
	if (ret)
		return ret;

	ret = gpio_request(GPIO_PORTC + 28, "imx-mmc-ro");
	if (ret) {
		free_irq(gpio_to_irq(IMX_GPIO_NR(3, 29)), data);
		return ret;
	}

	gpio_direction_input(GPIO_PORTC + 28);

	return 0;
}

static void pcm970_sdhc2_exit(struct device *dev, void *data)
{
	free_irq(gpio_to_irq(IMX_GPIO_NR(3, 29)), data);
	gpio_free(GPIO_PORTC + 28);
}

static const struct imxmmc_platform_data sdhc_pdata __initconst = {
	.get_ro = pcm970_sdhc2_get_ro,
	.init = pcm970_sdhc2_init,
	.exit = pcm970_sdhc2_exit,
};

static struct imx_fb_videomode pcm970_modes[] = {
	{
		.mode = {
			.name		= "Sharp-LQ035Q7",
			.refresh	= 60,
			.xres		= 240,
			.yres		= 320,
			.pixclock	= 188679, /* in ps (5.3MHz) */
			.hsync_len	= 7,
			.left_margin	= 5,
			.right_margin	= 16,
			.vsync_len	= 1,
			.upper_margin	= 7,
			.lower_margin	= 9,
		},
		/*
		 * - HSYNC active high
		 * - VSYNC active high
		 * - clk notenabled while idle
		 * - clock not inverted
		 * - data not inverted
		 * - data enable low active
		 * - enable sharp mode
		 */
		.pcr		= 0xF00080C0,
		.bpp		= 16,
	}, {
		.mode = {
			.name		= "TX090",
			.refresh	= 60,
			.xres		= 240,
			.yres		= 320,
			.pixclock	= 38255,
			.left_margin	= 144,
			.right_margin	= 0,
			.upper_margin	= 7,
			.lower_margin	= 40,
			.hsync_len	= 96,
			.vsync_len	= 1,
		},
		/*
		 * - HSYNC active low (1 << 22)
		 * - VSYNC active low (1 << 23)
		 * - clk notenabled while idle
		 * - clock not inverted
		 * - data not inverted
		 * - data enable low active
		 * - enable sharp mode
		 */
		.pcr = 0xF0008080 | (1<<22) | (1<<23) | (1<<19),
		.bpp = 32,
	},
};

static const struct imx_fb_platform_data pcm038_fb_data __initconst = {
	.mode = pcm970_modes,
	.num_modes = ARRAY_SIZE(pcm970_modes),

	.pwmr		= 0x00A903FF,
	.lscr1		= 0x00120300,
	.dmacr		= 0x00020010,
};

static struct resource pcm970_sja1000_resources[] = {
	{
		.start   = MX27_CS4_BASE_ADDR,
		.end     = MX27_CS4_BASE_ADDR + 0x100 - 1,
		.flags   = IORESOURCE_MEM,
	}, {
		/* irq number is run-time assigned */
		.flags   = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
};

static struct sja1000_platform_data pcm970_sja1000_platform_data = {
	.osc_freq	= 16000000,
	.ocr		= OCR_TX1_PULLDOWN | OCR_TX0_PUSHPULL,
	.cdr		= CDR_CBP,
};

static struct platform_device pcm970_sja1000 = {
	.name = "sja1000_platform",
	.dev = {
		.platform_data = &pcm970_sja1000_platform_data,
	},
	.resource = pcm970_sja1000_resources,
	.num_resources = ARRAY_SIZE(pcm970_sja1000_resources),
};

/*
 * system init for baseboard usage. Will be called by pcm038 init.
 *
 * Add platform devices present on this baseboard and init
 * them from CPU side as far as required to use them later on
 */
void __init pcm970_baseboard_init(void)
{
	mxc_gpio_setup_multiple_pins(pcm970_pins, ARRAY_SIZE(pcm970_pins),
			"PCM970");

	imx27_add_imx_fb(&pcm038_fb_data);
	mxc_gpio_mode(GPIO_PORTC | 28 | GPIO_GPIO | GPIO_IN);
	imx27_add_mxc_mmc(1, &sdhc_pdata);
	pcm970_sja1000_resources[1].start = gpio_to_irq(IMX_GPIO_NR(5, 19));
	pcm970_sja1000_resources[1].end = gpio_to_irq(IMX_GPIO_NR(5, 19));
	platform_device_register(&pcm970_sja1000);
}
