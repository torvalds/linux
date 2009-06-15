/*
 *  Copyright (C) 2000 Deep Blue Solutions Ltd
 *  Copyright (C) 2002 Shane Nay (shane@minirl.com)
 *  Copyright 2006-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/physmap.h>
#include <linux/gpio.h>
#include <mach/common.h>
#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>
#include <mach/imx-uart.h>
#include <mach/imxfb.h>
#include <mach/iomux.h>
#include <mach/mxc_nand.h>
#include <mach/mmc.h>
#include <mach/board-mx21ads.h>

#include "devices.h"

static unsigned int mx21ads_pins[] = {

	/* CS8900A */
	(GPIO_PORTE | GPIO_GPIO | GPIO_IN | 11),

	/* UART1 */
	PE12_PF_UART1_TXD,
	PE13_PF_UART1_RXD,
	PE14_PF_UART1_CTS,
	PE15_PF_UART1_RTS,

	/* UART3 (IrDA) - only TXD and RXD */
	PE8_PF_UART3_TXD,
	PE9_PF_UART3_RXD,

	/* UART4 */
	PB26_AF_UART4_RTS,
	PB28_AF_UART4_TXD,
	PB29_AF_UART4_CTS,
	PB31_AF_UART4_RXD,

	/* LCDC */
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
	PA24_PF_REV,     /* Sharp panel dedicated signal */
	PA25_PF_CLS,     /* Sharp panel dedicated signal */
	PA26_PF_PS,      /* Sharp panel dedicated signal */
	PA27_PF_SPL_SPR, /* Sharp panel dedicated signal */
	PA28_PF_HSYNC,
	PA29_PF_VSYNC,
	PA30_PF_CONTRAST,
	PA31_PF_OE_ACD,

	/* MMC/SDHC */
	PE18_PF_SD1_D0,
	PE19_PF_SD1_D1,
	PE20_PF_SD1_D2,
	PE21_PF_SD1_D3,
	PE22_PF_SD1_CMD,
	PE23_PF_SD1_CLK,

	/* NFC */
	PF0_PF_NRFB,
	PF1_PF_NFCE,
	PF2_PF_NFWP,
	PF3_PF_NFCLE,
	PF4_PF_NFALE,
	PF5_PF_NFRE,
	PF6_PF_NFWE,
	PF7_PF_NFIO0,
	PF8_PF_NFIO1,
	PF9_PF_NFIO2,
	PF10_PF_NFIO3,
	PF11_PF_NFIO4,
	PF12_PF_NFIO5,
	PF13_PF_NFIO6,
	PF14_PF_NFIO7,
};

/* ADS's NOR flash: 2x AM29BDS128HE9VKI on 32-bit bus */
static struct physmap_flash_data mx21ads_flash_data = {
	.width = 4,
};

static struct resource mx21ads_flash_resource = {
	.start = CS0_BASE_ADDR,
	.end = CS0_BASE_ADDR + 0x02000000 - 1,
	.flags = IORESOURCE_MEM,
};

static struct platform_device mx21ads_nor_mtd_device = {
	.name = "physmap-flash",
	.id = 0,
	.dev = {
		.platform_data = &mx21ads_flash_data,
	},
	.num_resources = 1,
	.resource = &mx21ads_flash_resource,
};

static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct imxuart_platform_data uart_norts_pdata = {
};


static int mx21ads_fb_init(struct platform_device *pdev)
{
	u16 tmp;

	tmp = __raw_readw(MX21ADS_IO_REG);
	tmp |= MX21ADS_IO_LCDON;
	__raw_writew(tmp, MX21ADS_IO_REG);
	return 0;
}

static void mx21ads_fb_exit(struct platform_device *pdev)
{
	u16 tmp;

	tmp = __raw_readw(MX21ADS_IO_REG);
	tmp &= ~MX21ADS_IO_LCDON;
	__raw_writew(tmp, MX21ADS_IO_REG);
}

/*
 * Connected is a portrait Sharp-QVGA display
 * of type: LQ035Q7DB02
 */
static struct imx_fb_platform_data mx21ads_fb_data = {
	.pixclock       = 188679, /* in ps */
	.xres           = 240,
	.yres           = 320,

	.bpp            = 16,
	.hsync_len      = 2,
	.left_margin    = 6,
	.right_margin   = 16,

	.vsync_len      = 1,
	.upper_margin   = 8,
	.lower_margin   = 10,
	.fixed_screen_cpu = 0,

	.pcr            = 0xFB108BC7,
	.pwmr           = 0x00A901ff,
	.lscr1          = 0x00120300,
	.dmacr          = 0x00020008,

	.init = mx21ads_fb_init,
	.exit = mx21ads_fb_exit,
};

static int mx21ads_sdhc_get_ro(struct device *dev)
{
	return (__raw_readw(MX21ADS_IO_REG) & MX21ADS_IO_SD_WP) ? 1 : 0;
}

static int mx21ads_sdhc_init(struct device *dev, irq_handler_t detect_irq,
	void *data)
{
	int ret;

	ret = request_irq(IRQ_GPIOD(25), detect_irq,
		IRQF_TRIGGER_FALLING, "mmc-detect", data);
	if (ret)
		goto out;
	return 0;
out:
	return ret;
}

static void mx21ads_sdhc_exit(struct device *dev, void *data)
{
	free_irq(IRQ_GPIOD(25), data);
}

static struct imxmmc_platform_data mx21ads_sdhc_pdata = {
	.ocr_avail = MMC_VDD_29_30 | MMC_VDD_30_31, /* 3.0V */
	.get_ro = mx21ads_sdhc_get_ro,
	.init = mx21ads_sdhc_init,
	.exit = mx21ads_sdhc_exit,
};

static struct mxc_nand_platform_data mx21ads_nand_board_info = {
	.width = 1,
	.hw_ecc = 1,
};

static struct map_desc mx21ads_io_desc[] __initdata = {
	/*
	 * Memory-mapped I/O on MX21ADS Base board:
	 *   - CS8900A Ethernet controller
	 *   - ST16C2552CJ UART
	 *   - CPU and Base board version
	 *   - Base board I/O register
	 */
	{
		.virtual = MX21ADS_MMIO_BASE_ADDR,
		.pfn = __phys_to_pfn(CS1_BASE_ADDR),
		.length = MX21ADS_MMIO_SIZE,
		.type = MT_DEVICE,
	},
};

static void __init mx21ads_map_io(void)
{
	mx21_map_io();
	iotable_init(mx21ads_io_desc, ARRAY_SIZE(mx21ads_io_desc));
}

static struct platform_device *platform_devices[] __initdata = {
	&mx21ads_nor_mtd_device,
};

static void __init mx21ads_board_init(void)
{
	mxc_gpio_setup_multiple_pins(mx21ads_pins, ARRAY_SIZE(mx21ads_pins),
			"mx21ads");

	mxc_register_device(&mxc_uart_device0, &uart_pdata);
	mxc_register_device(&mxc_uart_device2, &uart_norts_pdata);
	mxc_register_device(&mxc_uart_device3, &uart_pdata);
	mxc_register_device(&mxc_fb_device, &mx21ads_fb_data);
	mxc_register_device(&mxc_sdhc_device0, &mx21ads_sdhc_pdata);
	mxc_register_device(&mxc_nand_device, &mx21ads_nand_board_info);

	platform_add_devices(platform_devices, ARRAY_SIZE(platform_devices));
}

static void __init mx21ads_timer_init(void)
{
	mx21_clocks_init(32768, 26000000);
}

static struct sys_timer mx21ads_timer = {
	.init	= mx21ads_timer_init,
};

MACHINE_START(MX21ADS, "Freescale i.MX21ADS")
	/* maintainer: Freescale Semiconductor, Inc. */
	.phys_io        = AIPI_BASE_ADDR,
	.io_pg_offst    = ((AIPI_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params    = PHYS_OFFSET + 0x100,
	.map_io         = mx21ads_map_io,
	.init_irq       = mxc_init_irq,
	.init_machine   = mx21ads_board_init,
	.timer          = &mx21ads_timer,
MACHINE_END
