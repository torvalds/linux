/*
 * armadillo5x0.c
 *
 * Copyright 2009 Alberto Panizzo <maramaopercheseimorto@gmail.com>
 * updates in http://alberdroid.blogspot.com/
 *
 * Based on Atmark Techno, Inc. armadillo 500 BSP 2008
 * Based on mx31ads.c and pcm037.c Great Work!
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/smsc911x.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mtd/physmap.h>
#include <linux/io.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/i2c.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/memory.h>
#include <asm/mach/map.h>

#include <mach/common.h>
#include <mach/imx-uart.h>
#include <mach/iomux-mx3.h>
#include <mach/board-armadillo5x0.h>
#include <mach/mmc.h>
#include <mach/ipu.h>
#include <mach/mx3fb.h>
#include <mach/mxc_nand.h>

#include "devices.h"
#include "crm_regs.h"

static int armadillo5x0_pins[] = {
	/* UART1 */
	MX31_PIN_CTS1__CTS1,
	MX31_PIN_RTS1__RTS1,
	MX31_PIN_TXD1__TXD1,
	MX31_PIN_RXD1__RXD1,
	/* UART2 */
	MX31_PIN_CTS2__CTS2,
	MX31_PIN_RTS2__RTS2,
	MX31_PIN_TXD2__TXD2,
	MX31_PIN_RXD2__RXD2,
	/* LAN9118_IRQ */
	IOMUX_MODE(MX31_PIN_GPIO1_0, IOMUX_CONFIG_GPIO),
	/* SDHC1 */
	MX31_PIN_SD1_DATA3__SD1_DATA3,
	MX31_PIN_SD1_DATA2__SD1_DATA2,
	MX31_PIN_SD1_DATA1__SD1_DATA1,
	MX31_PIN_SD1_DATA0__SD1_DATA0,
	MX31_PIN_SD1_CLK__SD1_CLK,
	MX31_PIN_SD1_CMD__SD1_CMD,
	/* Framebuffer */
	MX31_PIN_LD0__LD0,
	MX31_PIN_LD1__LD1,
	MX31_PIN_LD2__LD2,
	MX31_PIN_LD3__LD3,
	MX31_PIN_LD4__LD4,
	MX31_PIN_LD5__LD5,
	MX31_PIN_LD6__LD6,
	MX31_PIN_LD7__LD7,
	MX31_PIN_LD8__LD8,
	MX31_PIN_LD9__LD9,
	MX31_PIN_LD10__LD10,
	MX31_PIN_LD11__LD11,
	MX31_PIN_LD12__LD12,
	MX31_PIN_LD13__LD13,
	MX31_PIN_LD14__LD14,
	MX31_PIN_LD15__LD15,
	MX31_PIN_LD16__LD16,
	MX31_PIN_LD17__LD17,
	MX31_PIN_VSYNC3__VSYNC3,
	MX31_PIN_HSYNC__HSYNC,
	MX31_PIN_FPSHIFT__FPSHIFT,
	MX31_PIN_DRDY0__DRDY0,
	IOMUX_MODE(MX31_PIN_LCS1, IOMUX_CONFIG_GPIO), /*ADV7125_PSAVE*/
	/* I2C2 */
	MX31_PIN_CSPI2_MOSI__SCL,
	MX31_PIN_CSPI2_MISO__SDA,
};

/* RTC over I2C*/
#define ARMADILLO5X0_RTC_GPIO	IOMUX_TO_GPIO(MX31_PIN_SRXD4)

static struct i2c_board_info armadillo5x0_i2c_rtc = {
	I2C_BOARD_INFO("s35390a", 0x30),
};

/* GPIO BUTTONS */
static struct gpio_keys_button armadillo5x0_buttons[] = {
	{
		.code		= KEY_ENTER, /*28*/
		.gpio		= IOMUX_TO_GPIO(MX31_PIN_SCLK0),
		.active_low	= 1,
		.desc		= "menu",
		.wakeup		= 1,
	}, {
		.code		= KEY_BACK, /*158*/
		.gpio		= IOMUX_TO_GPIO(MX31_PIN_SRST0),
		.active_low	= 1,
		.desc		= "back",
		.wakeup		= 1,
	}
};

static struct gpio_keys_platform_data armadillo5x0_button_data = {
	.buttons	= armadillo5x0_buttons,
	.nbuttons	= ARRAY_SIZE(armadillo5x0_buttons),
};

static struct platform_device armadillo5x0_button_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.num_resources	= 0,
	.dev		= {
		.platform_data	= &armadillo5x0_button_data,
	}
};

/*
 * NAND Flash
 */
static struct mxc_nand_platform_data armadillo5x0_nand_flash_pdata = {
	.width		= 1,
	.hw_ecc		= 1,
};

/*
 * MTD NOR Flash
 */
static struct mtd_partition armadillo5x0_nor_flash_partitions[] = {
	{
		.name		= "nor.bootloader",
		.offset		= 0x00000000,
		.size		= 4*32*1024,
	}, {
		.name		= "nor.kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 16*128*1024,
	}, {
		.name		= "nor.userland",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 110*128*1024,
	}, {
		.name		= "nor.config",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 1*128*1024,
	},
};

static struct physmap_flash_data armadillo5x0_nor_flash_pdata = {
	.width		= 2,
	.parts		= armadillo5x0_nor_flash_partitions,
	.nr_parts	= ARRAY_SIZE(armadillo5x0_nor_flash_partitions),
};

static struct resource armadillo5x0_nor_flash_resource = {
	.flags		= IORESOURCE_MEM,
	.start		= CS0_BASE_ADDR,
	.end		= CS0_BASE_ADDR + SZ_64M - 1,
};

static struct platform_device armadillo5x0_nor_flash = {
	.name			= "physmap-flash",
	.id			= -1,
	.num_resources		= 1,
	.resource		= &armadillo5x0_nor_flash_resource,
};

/*
 * FB support
 */
static const struct fb_videomode fb_modedb[] = {
	{	/* 640x480 @ 60 Hz */
		.name		= "CRT-VGA",
		.refresh	= 60,
		.xres		= 640,
		.yres		= 480,
		.pixclock	= 39721,
		.left_margin	= 35,
		.right_margin	= 115,
		.upper_margin	= 43,
		.lower_margin	= 1,
		.hsync_len	= 10,
		.vsync_len	= 1,
		.sync		= FB_SYNC_OE_ACT_HIGH,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	}, {/* 800x600 @ 56 Hz */
		.name		= "CRT-SVGA",
		.refresh	= 56,
		.xres		= 800,
		.yres		= 600,
		.pixclock	= 30000,
		.left_margin	= 30,
		.right_margin	= 108,
		.upper_margin	= 13,
		.lower_margin	= 10,
		.hsync_len	= 10,
		.vsync_len	= 1,
		.sync		= FB_SYNC_OE_ACT_HIGH | FB_SYNC_HOR_HIGH_ACT |
				  FB_SYNC_VERT_HIGH_ACT,
		.vmode		= FB_VMODE_NONINTERLACED,
		.flag		= 0,
	},
};

static struct ipu_platform_data mx3_ipu_data = {
	.irq_base = MXC_IPU_IRQ_START,
};

static struct mx3fb_platform_data mx3fb_pdata = {
	.dma_dev	= &mx3_ipu.dev,
	.name		= "CRT-VGA",
	.mode		= fb_modedb,
	.num_modes	= ARRAY_SIZE(fb_modedb),
};

/*
 * SDHC 1
 * MMC support
 */
static int armadillo5x0_sdhc1_get_ro(struct device *dev)
{
	return gpio_get_value(IOMUX_TO_GPIO(MX31_PIN_ATA_RESET_B));
}

static int armadillo5x0_sdhc1_init(struct device *dev,
				   irq_handler_t detect_irq, void *data)
{
	int ret;
	int gpio_det, gpio_wp;

	gpio_det = IOMUX_TO_GPIO(MX31_PIN_ATA_DMACK);
	gpio_wp = IOMUX_TO_GPIO(MX31_PIN_ATA_RESET_B);

	ret = gpio_request(gpio_det, "sdhc-card-detect");
	if (ret)
		return ret;

	gpio_direction_input(gpio_det);

	ret = gpio_request(gpio_wp, "sdhc-write-protect");
	if (ret)
		goto err_gpio_free;

	gpio_direction_input(gpio_wp);

	/* When supported the trigger type have to be BOTH */
	ret = request_irq(IOMUX_TO_IRQ(MX31_PIN_ATA_DMACK), detect_irq,
			  IRQF_DISABLED | IRQF_TRIGGER_FALLING,
			  "sdhc-detect", data);

	if (ret)
		goto err_gpio_free_2;

	return 0;

err_gpio_free_2:
	gpio_free(gpio_wp);

err_gpio_free:
	gpio_free(gpio_det);

	return ret;

}

static void armadillo5x0_sdhc1_exit(struct device *dev, void *data)
{
	free_irq(IOMUX_TO_IRQ(MX31_PIN_ATA_DMACK), data);
	gpio_free(IOMUX_TO_GPIO(MX31_PIN_ATA_DMACK));
	gpio_free(IOMUX_TO_GPIO(MX31_PIN_ATA_RESET_B));
}

static struct imxmmc_platform_data sdhc_pdata = {
	.get_ro = armadillo5x0_sdhc1_get_ro,
	.init = armadillo5x0_sdhc1_init,
	.exit = armadillo5x0_sdhc1_exit,
};

/*
 * SMSC 9118
 * Network support
 */
static struct resource armadillo5x0_smc911x_resources[] = {
	{
		.start	= CS3_BASE_ADDR,
		.end	= CS3_BASE_ADDR + SZ_32M - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= IOMUX_TO_IRQ(MX31_PIN_GPIO1_0),
		.end	= IOMUX_TO_IRQ(MX31_PIN_GPIO1_0),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_LOWLEVEL,
	},
};

static struct smsc911x_platform_config smsc911x_info = {
	.flags		= SMSC911X_USE_16BIT,
	.irq_polarity   = SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type       = SMSC911X_IRQ_TYPE_PUSH_PULL,
};

static struct platform_device armadillo5x0_smc911x_device = {
	.name           = "smsc911x",
	.id             = -1,
	.num_resources  = ARRAY_SIZE(armadillo5x0_smc911x_resources),
	.resource       = armadillo5x0_smc911x_resources,
	.dev            = {
		.platform_data = &smsc911x_info,
	},
};

/* UART device data */
static struct imxuart_platform_data uart_pdata = {
	.flags = IMXUART_HAVE_RTSCTS,
};

static struct platform_device *devices[] __initdata = {
	&armadillo5x0_smc911x_device,
	&mxc_i2c_device1,
	&armadillo5x0_button_device,
};

/*
 * Perform board specific initializations
 */
static void __init armadillo5x0_init(void)
{
	mxc_iomux_setup_multiple_pins(armadillo5x0_pins,
			ARRAY_SIZE(armadillo5x0_pins), "armadillo5x0");

	platform_add_devices(devices, ARRAY_SIZE(devices));

	/* Register UART */
	mxc_register_device(&mxc_uart_device0, &uart_pdata);
	mxc_register_device(&mxc_uart_device1, &uart_pdata);

	/* SMSC9118 IRQ pin */
	gpio_direction_input(MX31_PIN_GPIO1_0);

	/* Register SDHC */
	mxc_register_device(&mxcsdhc_device0, &sdhc_pdata);

	/* Register FB */
	mxc_register_device(&mx3_ipu, &mx3_ipu_data);
	mxc_register_device(&mx3_fb, &mx3fb_pdata);

	/* Register NOR Flash */
	mxc_register_device(&armadillo5x0_nor_flash,
			    &armadillo5x0_nor_flash_pdata);

	/* Register NAND Flash */
	mxc_register_device(&mxc_nand_device, &armadillo5x0_nand_flash_pdata);

	/* set NAND page size to 2k if not configured via boot mode pins */
	__raw_writel(__raw_readl(MXC_CCM_RCSR) | (1 << 30), MXC_CCM_RCSR);

	/* RTC */
	/* Get RTC IRQ and register the chip */
	if (gpio_request(ARMADILLO5X0_RTC_GPIO, "rtc") == 0) {
		if (gpio_direction_input(ARMADILLO5X0_RTC_GPIO) == 0)
			armadillo5x0_i2c_rtc.irq = gpio_to_irq(ARMADILLO5X0_RTC_GPIO);
		else
			gpio_free(ARMADILLO5X0_RTC_GPIO);
	}
	if (armadillo5x0_i2c_rtc.irq == 0)
		pr_warning("armadillo5x0_init: failed to get RTC IRQ\n");
	i2c_register_board_info(1, &armadillo5x0_i2c_rtc, 1);
}

static void __init armadillo5x0_timer_init(void)
{
	mx31_clocks_init(26000000);
}

static struct sys_timer armadillo5x0_timer = {
	.init	= armadillo5x0_timer_init,
};

MACHINE_START(ARMADILLO5X0, "Armadillo-500")
	/* Maintainer: Alberto Panizzo  */
	.phys_io	= AIPS1_BASE_ADDR,
	.io_pg_offst	= ((AIPS1_BASE_ADDR_VIRT) >> 18) & 0xfffc,
	.boot_params	= PHYS_OFFSET + 0x00000100,
	.map_io		= mx31_map_io,
	.init_irq	= mx31_init_irq,
	.timer		= &armadillo5x0_timer,
	.init_machine	= armadillo5x0_init,
MACHINE_END
