/*
 * linux/arch/arm/mach-omap2/board-am3517evm.c
 *
 * Copyright (C) 2009 Texas Instruments Incorporated
 * Author: Ranjith Lohithakshan <ranjithl@ti.com>
 *
 * Based on mach-omap2/board-omap3evm.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as  published by the
 * Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/platform_data/pca953x.h>
#include <linux/can/platform/ti_hecc.h>
#include <linux/davinci_emac.h>
#include <linux/mmc/host.h>
#include <linux/usb/musb.h>
#include <linux/platform_data/gpio-omap.h>

#include "am35xx.h"
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"
#include <video/omapdss.h>
#include <video/omap-panel-data.h>

#include "am35xx-emac.h"
#include "mux.h"
#include "control.h"
#include "hsmmc.h"

#define LCD_PANEL_PWR		176
#define LCD_PANEL_BKLIGHT_PWR	182
#define LCD_PANEL_PWM		181

static struct i2c_board_info __initdata am3517evm_i2c1_boardinfo[] = {
	{
		I2C_BOARD_INFO("s35390a", 0x30),
	},
};

/*
 * RTC - S35390A
 */
#define GPIO_RTCS35390A_IRQ	55

static void __init am3517_evm_rtc_init(void)
{
	int r;

	omap_mux_init_gpio(GPIO_RTCS35390A_IRQ, OMAP_PIN_INPUT_PULLUP);

	r = gpio_request_one(GPIO_RTCS35390A_IRQ, GPIOF_IN, "rtcs35390a-irq");
	if (r < 0) {
		printk(KERN_WARNING "failed to request GPIO#%d\n",
				GPIO_RTCS35390A_IRQ);
		return;
	}

	am3517evm_i2c1_boardinfo[0].irq = gpio_to_irq(GPIO_RTCS35390A_IRQ);
}

/*
 * I2C GPIO Expander - TCA6416
 */

/* Mounted on Base-Board */
static struct pca953x_platform_data am3517evm_gpio_expander_info_0 = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
};
static struct i2c_board_info __initdata am3517evm_i2c2_boardinfo[] = {
	{
		I2C_BOARD_INFO("tlv320aic23", 0x1A),
	},
	{
		I2C_BOARD_INFO("tca6416", 0x21),
		.platform_data = &am3517evm_gpio_expander_info_0,
	},
};

/* Mounted on UI Card */
static struct pca953x_platform_data am3517evm_ui_gpio_expander_info_1 = {
	.gpio_base	= OMAP_MAX_GPIO_LINES + 16,
};
static struct pca953x_platform_data am3517evm_ui_gpio_expander_info_2 = {
	.gpio_base	= OMAP_MAX_GPIO_LINES + 32,
};
static struct i2c_board_info __initdata am3517evm_i2c3_boardinfo[] = {
	{
		I2C_BOARD_INFO("tca6416", 0x20),
		.platform_data = &am3517evm_ui_gpio_expander_info_1,
	},
	{
		I2C_BOARD_INFO("tca6416", 0x21),
		.platform_data = &am3517evm_ui_gpio_expander_info_2,
	},
};

static int __init am3517_evm_i2c_init(void)
{
	omap_register_i2c_bus(1, 400, NULL, 0);
	omap_register_i2c_bus(2, 400, am3517evm_i2c2_boardinfo,
			ARRAY_SIZE(am3517evm_i2c2_boardinfo));
	omap_register_i2c_bus(3, 400, am3517evm_i2c3_boardinfo,
			ARRAY_SIZE(am3517evm_i2c3_boardinfo));

	return 0;
}

static struct panel_generic_dpi_data lcd_panel = {
	.name			= "sharp_lq",
	.num_gpios		= 3,
	.gpios			= {
		LCD_PANEL_PWR,
		LCD_PANEL_BKLIGHT_PWR,
		LCD_PANEL_PWM,
	},
};

static struct omap_dss_device am3517_evm_lcd_device = {
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.name			= "lcd",
	.driver_name		= "generic_dpi_panel",
	.data			= &lcd_panel,
	.phy.dpi.data_lines 	= 16,
};

static struct omap_dss_device am3517_evm_tv_device = {
	.type 			= OMAP_DISPLAY_TYPE_VENC,
	.name 			= "tv",
	.driver_name		= "venc",
	.phy.venc.type		= OMAP_DSS_VENC_TYPE_SVIDEO,
};

static struct tfp410_platform_data dvi_panel = {
	.power_down_gpio	= -1,
	.i2c_bus_num		= -1,
};

static struct omap_dss_device am3517_evm_dvi_device = {
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.name			= "dvi",
	.driver_name		= "tfp410",
	.data			= &dvi_panel,
	.phy.dpi.data_lines	= 24,
};

static struct omap_dss_device *am3517_evm_dss_devices[] = {
	&am3517_evm_lcd_device,
	&am3517_evm_tv_device,
	&am3517_evm_dvi_device,
};

static struct omap_dss_board_info am3517_evm_dss_data = {
	.num_devices	= ARRAY_SIZE(am3517_evm_dss_devices),
	.devices	= am3517_evm_dss_devices,
	.default_device	= &am3517_evm_lcd_device,
};

/*
 * Board initialization
 */

static struct omap_musb_board_data musb_board_data = {
	.interface_type         = MUSB_INTERFACE_ULPI,
	.mode                   = MUSB_OTG,
	.power                  = 500,
	.set_phy_power		= am35x_musb_phy_power,
	.clear_irq		= am35x_musb_clear_irq,
	.set_mode		= am35x_set_mode,
	.reset			= am35x_musb_reset,
};

static __init void am3517_evm_musb_init(void)
{
	u32 devconf2;

	/*
	 * Set up USB clock/mode in the DEVCONF2 register.
	 */
	devconf2 = omap_ctrl_readl(AM35XX_CONTROL_DEVCONF2);

	/* USB2.0 PHY reference clock is 13 MHz */
	devconf2 &= ~(CONF2_REFFREQ | CONF2_OTGMODE | CONF2_PHY_GPIOMODE);
	devconf2 |=  CONF2_REFFREQ_13MHZ | CONF2_SESENDEN | CONF2_VBDTCTEN
			| CONF2_DATPOL;

	omap_ctrl_writel(devconf2, AM35XX_CONTROL_DEVCONF2);

	usb_musb_init(&musb_board_data);
}

static __init void am3517_evm_mcbsp1_init(void)
{
	u32 devconf0;

	/* McBSP1 CLKR/FSR signal to be connected to CLKX/FSX pin */
	devconf0 = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0);
	devconf0 |=  OMAP2_MCBSP1_CLKR_MASK | OMAP2_MCBSP1_FSR_MASK;
	omap_ctrl_writel(devconf0, OMAP2_CONTROL_DEVCONF0);
}

static struct usbhs_phy_data phy_data[] __initdata = {
	{
		.port = 1,
		.reset_gpio = 57,
		.vcc_gpio = -EINVAL,
	},
};

static struct usbhs_omap_platform_data usbhs_bdata __initdata = {
	.port_mode[0] = OMAP_EHCI_PORT_MODE_PHY,
#if defined(CONFIG_PANEL_SHARP_LQ043T1DG01) || \
		defined(CONFIG_PANEL_SHARP_LQ043T1DG01_MODULE)
	.port_mode[1] = OMAP_USBHS_PORT_MODE_UNUSED,
#else
	.port_mode[1] = OMAP_EHCI_PORT_MODE_PHY,
#endif
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	/* USB OTG DRVVBUS offset = 0x212 */
	OMAP3_MUX(SAD2D_MCAD23, OMAP_MUX_MODE0 | OMAP_PIN_INPUT_PULLDOWN),
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif


static struct resource am3517_hecc_resources[] = {
	{
		.start	= AM35XX_IPSS_HECC_BASE,
		.end	= AM35XX_IPSS_HECC_BASE + 0x3FFF,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= 24 + OMAP_INTC_START,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device am3517_hecc_device = {
	.name		= "ti_hecc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(am3517_hecc_resources),
	.resource	= am3517_hecc_resources,
};

static struct ti_hecc_platform_data am3517_evm_hecc_pdata = {
	.scc_hecc_offset	= AM35XX_HECC_SCC_HECC_OFFSET,
	.scc_ram_offset		= AM35XX_HECC_SCC_RAM_OFFSET,
	.hecc_ram_offset	= AM35XX_HECC_RAM_OFFSET,
	.mbx_offset		= AM35XX_HECC_MBOX_OFFSET,
	.int_line		= AM35XX_HECC_INT_LINE,
	.version		= AM35XX_HECC_VERSION,
};

static void am3517_evm_hecc_init(struct ti_hecc_platform_data *pdata)
{
	am3517_hecc_device.dev.platform_data = pdata;
	platform_device_register(&am3517_hecc_device);
}

static struct omap2_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_cd	= 127,
		.gpio_wp	= 126,
	},
	{
		.mmc		= 2,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_cd	= 128,
		.gpio_wp	= 129,
	},
	{}      /* Terminator */
};

static void __init am3517_evm_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);

	am3517_evm_i2c_init();
	omap_display_init(&am3517_evm_dss_data);
	omap_serial_init();
	omap_sdrc_init(NULL, NULL);

	/* Configure GPIO for EHCI port */
	omap_mux_init_gpio(57, OMAP_PIN_OUTPUT);

	usbhs_init_phys(phy_data, ARRAY_SIZE(phy_data));
	usbhs_init(&usbhs_bdata);
	am3517_evm_hecc_init(&am3517_evm_hecc_pdata);

	/* RTC - S35390A */
	am3517_evm_rtc_init();

	i2c_register_board_info(1, am3517evm_i2c1_boardinfo,
				ARRAY_SIZE(am3517evm_i2c1_boardinfo));
	/*Ethernet*/
	am35xx_emac_init(AM35XX_DEFAULT_MDIO_FREQUENCY, 1);

	/* MUSB */
	am3517_evm_musb_init();

	/* McBSP1 */
	am3517_evm_mcbsp1_init();

	/* MMC init function */
	omap_hsmmc_init(mmc);
}

MACHINE_START(OMAP3517EVM, "OMAP3517/AM3517 EVM")
	.atag_offset	= 0x100,
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= am35xx_init_early,
	.init_irq	= omap3_init_irq,
	.handle_irq	= omap3_intc_handle_irq,
	.init_machine	= am3517_evm_init,
	.init_late	= am35xx_init_late,
	.init_time	= omap3_sync32k_timer_init,
	.restart	= omap3xxx_restart,
MACHINE_END
