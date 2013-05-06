/*
 *  linux/arch/arm/mach-mmp/aspenite.c
 *
 *  Support for the Marvell PXA168-based Aspenite and Zylonite2
 *  Development Platform.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/smc91x.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>
#include <linux/interrupt.h>
#include <linux/platform_data/mv_usb.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/addr-map.h>
#include <mach/mfp-pxa168.h>
#include <mach/pxa168.h>
#include <mach/irqs.h>
#include <video/pxa168fb.h>
#include <linux/input.h>
#include <linux/platform_data/keypad-pxa27x.h>

#include "common.h"

static unsigned long common_pin_config[] __initdata = {
	/* Data Flash Interface */
	GPIO0_DFI_D15,
	GPIO1_DFI_D14,
	GPIO2_DFI_D13,
	GPIO3_DFI_D12,
	GPIO4_DFI_D11,
	GPIO5_DFI_D10,
	GPIO6_DFI_D9,
	GPIO7_DFI_D8,
	GPIO8_DFI_D7,
	GPIO9_DFI_D6,
	GPIO10_DFI_D5,
	GPIO11_DFI_D4,
	GPIO12_DFI_D3,
	GPIO13_DFI_D2,
	GPIO14_DFI_D1,
	GPIO15_DFI_D0,

	/* Static Memory Controller */
	GPIO18_SMC_nCS0,
	GPIO34_SMC_nCS1,
	GPIO23_SMC_nLUA,
	GPIO25_SMC_nLLA,
	GPIO28_SMC_RDY,
	GPIO29_SMC_SCLK,
	GPIO35_SMC_BE1,
	GPIO36_SMC_BE2,
	GPIO27_GPIO,	/* Ethernet IRQ */

	/* UART1 */
	GPIO107_UART1_RXD,
	GPIO108_UART1_TXD,

	/* SSP1 */
	GPIO113_I2S_MCLK,
	GPIO114_I2S_FRM,
	GPIO115_I2S_BCLK,
	GPIO116_I2S_RXD,
	GPIO117_I2S_TXD,

	/* LCD */
	GPIO56_LCD_FCLK_RD,
	GPIO57_LCD_LCLK_A0,
	GPIO58_LCD_PCLK_WR,
	GPIO59_LCD_DENA_BIAS,
	GPIO60_LCD_DD0,
	GPIO61_LCD_DD1,
	GPIO62_LCD_DD2,
	GPIO63_LCD_DD3,
	GPIO64_LCD_DD4,
	GPIO65_LCD_DD5,
	GPIO66_LCD_DD6,
	GPIO67_LCD_DD7,
	GPIO68_LCD_DD8,
	GPIO69_LCD_DD9,
	GPIO70_LCD_DD10,
	GPIO71_LCD_DD11,
	GPIO72_LCD_DD12,
	GPIO73_LCD_DD13,
	GPIO74_LCD_DD14,
	GPIO75_LCD_DD15,
	GPIO76_LCD_DD16,
	GPIO77_LCD_DD17,
	GPIO78_LCD_DD18,
	GPIO79_LCD_DD19,
	GPIO80_LCD_DD20,
	GPIO81_LCD_DD21,
	GPIO82_LCD_DD22,
	GPIO83_LCD_DD23,

	/* Keypad */
	GPIO109_KP_MKIN1,
	GPIO110_KP_MKIN0,
	GPIO111_KP_MKOUT7,
	GPIO112_KP_MKOUT6,
	GPIO121_KP_MKIN4,
};

static struct smc91x_platdata smc91x_info = {
	.flags	= SMC91X_USE_16BIT | SMC91X_NOWAIT,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= SMC_CS1_PHYS_BASE + 0x300,
		.end	= SMC_CS1_PHYS_BASE + 0xfffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= MMP_GPIO_TO_IRQ(27),
		.end	= MMP_GPIO_TO_IRQ(27),
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE,
	}
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.dev		= {
		.platform_data = &smc91x_info,
	},
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static struct mtd_partition aspenite_nand_partitions[] = {
	{
		.name		= "bootloader",
		.offset		= 0,
		.size		= SZ_1M,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "reserved",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_128K,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "reserved",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_8M,
		.mask_flags	= MTD_WRITEABLE,
	}, {
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= (SZ_2M + SZ_1M),
		.mask_flags	= 0,
	}, {
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_32M + SZ_16M,
		.mask_flags	= 0,
	}
};

static struct pxa3xx_nand_platform_data aspenite_nand_info = {
	.enable_arbiter	= 1,
	.num_cs = 1,
	.parts[0]	= aspenite_nand_partitions,
	.nr_parts[0]	= ARRAY_SIZE(aspenite_nand_partitions),
};

static struct i2c_board_info aspenite_i2c_info[] __initdata = {
	{ I2C_BOARD_INFO("wm8753", 0x1b), },
};

static struct fb_videomode video_modes[] = {
	[0] = {
		.pixclock	= 30120,
		.refresh	= 60,
		.xres		= 800,
		.yres		= 480,
		.hsync_len	= 1,
		.left_margin	= 215,
		.right_margin	= 40,
		.vsync_len	= 1,
		.upper_margin	= 34,
		.lower_margin	= 10,
		.sync		= FB_SYNC_VERT_HIGH_ACT | FB_SYNC_HOR_HIGH_ACT,
	},
};

struct pxa168fb_mach_info aspenite_lcd_info = {
	.id			= "Graphic Frame",
	.modes			= video_modes,
	.num_modes		= ARRAY_SIZE(video_modes),
	.pix_fmt		= PIX_FMT_RGB565,
	.io_pin_allocation_mode = PIN_MODE_DUMB_24,
	.dumb_mode		= DUMB_MODE_RGB888,
	.active			= 1,
	.panel_rbswap		= 0,
	.invert_pixclock	= 0,
};

static const unsigned int aspenite_matrix_key_map[] = {
	KEY(0, 6, KEY_UP),	/* SW 4 */
	KEY(0, 7, KEY_DOWN),	/* SW 5 */
	KEY(1, 6, KEY_LEFT),	/* SW 6 */
	KEY(1, 7, KEY_RIGHT),	/* SW 7 */
	KEY(4, 6, KEY_ENTER),	/* SW 8 */
	KEY(4, 7, KEY_ESC),	/* SW 9 */
};

static struct matrix_keymap_data aspenite_matrix_keymap_data = {
	.keymap			= aspenite_matrix_key_map,
	.keymap_size		= ARRAY_SIZE(aspenite_matrix_key_map),
};

static struct pxa27x_keypad_platform_data aspenite_keypad_info __initdata = {
	.matrix_key_rows	= 5,
	.matrix_key_cols	= 8,
	.matrix_keymap_data	= &aspenite_matrix_keymap_data,
	.debounce_interval	= 30,
};

#if defined(CONFIG_USB_EHCI_MV)
static char *pxa168_sph_clock_name[] = {
	[0] = "PXA168-USBCLK",
};

static struct mv_usb_platform_data pxa168_sph_pdata = {
	.clknum         = 1,
	.clkname        = pxa168_sph_clock_name,
	.mode           = MV_USB_MODE_HOST,
	.phy_init	= pxa_usb_phy_init,
	.phy_deinit	= pxa_usb_phy_deinit,
	.set_vbus	= NULL,
};
#endif

static void __init common_init(void)
{
	mfp_config(ARRAY_AND_SIZE(common_pin_config));

	/* on-chip devices */
	pxa168_add_uart(1);
	pxa168_add_twsi(1, NULL, ARRAY_AND_SIZE(aspenite_i2c_info));
	pxa168_add_ssp(1);
	pxa168_add_nand(&aspenite_nand_info);
	pxa168_add_fb(&aspenite_lcd_info);
	pxa168_add_keypad(&aspenite_keypad_info);
	platform_device_register(&pxa168_device_gpio);

	/* off-chip devices */
	platform_device_register(&smc91x_device);

#if defined(CONFIG_USB_EHCI_MV)
	pxa168_add_usb_host(&pxa168_sph_pdata);
#endif
}

MACHINE_START(ASPENITE, "PXA168-based Aspenite Development Platform")
	.map_io		= mmp_map_io,
	.nr_irqs	= MMP_NR_IRQS,
	.init_irq       = pxa168_init_irq,
	.init_time	= pxa168_timer_init,
	.init_machine   = common_init,
	.restart	= pxa168_restart,
MACHINE_END

MACHINE_START(ZYLONITE2, "PXA168-based Zylonite2 Development Platform")
	.map_io		= mmp_map_io,
	.nr_irqs	= MMP_NR_IRQS,
	.init_irq       = pxa168_init_irq,
	.init_time	= pxa168_timer_init,
	.init_machine   = common_init,
	.restart	= pxa168_restart,
MACHINE_END
