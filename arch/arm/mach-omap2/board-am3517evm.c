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
#include <linux/i2c/pca953x.h>
#include <linux/can/platform/ti_hecc.h>
#include <linux/davinci_emac.h>

#include <mach/hardware.h>
#include <mach/am35xx.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <plat/board.h>
#include <plat/common.h>
#include <plat/usb.h>
#include <plat/display.h>
#include <plat/panel-generic-dpi.h>

#include "mux.h"
#include "control.h"

#define AM35XX_EVM_MDIO_FREQUENCY	(1000000)

static struct mdio_platform_data am3517_evm_mdio_pdata = {
	.bus_freq	= AM35XX_EVM_MDIO_FREQUENCY,
};

static struct resource am3517_mdio_resources[] = {
	{
		.start  = AM35XX_IPSS_EMAC_BASE + AM35XX_EMAC_MDIO_OFFSET,
		.end    = AM35XX_IPSS_EMAC_BASE + AM35XX_EMAC_MDIO_OFFSET +
			  SZ_4K - 1,
		.flags  = IORESOURCE_MEM,
	},
};

static struct platform_device am3517_mdio_device = {
	.name		= "davinci_mdio",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(am3517_mdio_resources),
	.resource	= am3517_mdio_resources,
	.dev.platform_data = &am3517_evm_mdio_pdata,
};

static struct emac_platform_data am3517_evm_emac_pdata = {
	.rmii_en	= 1,
};

static struct resource am3517_emac_resources[] = {
	{
		.start  = AM35XX_IPSS_EMAC_BASE,
		.end    = AM35XX_IPSS_EMAC_BASE + 0x2FFFF,
		.flags  = IORESOURCE_MEM,
	},
	{
		.start  = INT_35XX_EMAC_C0_RXTHRESH_IRQ,
		.end    = INT_35XX_EMAC_C0_RXTHRESH_IRQ,
		.flags  = IORESOURCE_IRQ,
	},
	{
		.start  = INT_35XX_EMAC_C0_RX_PULSE_IRQ,
		.end    = INT_35XX_EMAC_C0_RX_PULSE_IRQ,
		.flags  = IORESOURCE_IRQ,
	},
	{
		.start  = INT_35XX_EMAC_C0_TX_PULSE_IRQ,
		.end    = INT_35XX_EMAC_C0_TX_PULSE_IRQ,
		.flags  = IORESOURCE_IRQ,
	},
	{
		.start  = INT_35XX_EMAC_C0_MISC_PULSE_IRQ,
		.end    = INT_35XX_EMAC_C0_MISC_PULSE_IRQ,
		.flags  = IORESOURCE_IRQ,
	},
};

static struct platform_device am3517_emac_device = {
	.name		= "davinci_emac",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(am3517_emac_resources),
	.resource	= am3517_emac_resources,
};

static void am3517_enable_ethernet_int(void)
{
	u32 regval;

	regval = omap_ctrl_readl(AM35XX_CONTROL_LVL_INTR_CLEAR);
	regval = (regval | AM35XX_CPGMAC_C0_RX_PULSE_CLR |
		AM35XX_CPGMAC_C0_TX_PULSE_CLR |
		AM35XX_CPGMAC_C0_MISC_PULSE_CLR |
		AM35XX_CPGMAC_C0_RX_THRESH_CLR);
	omap_ctrl_writel(regval, AM35XX_CONTROL_LVL_INTR_CLEAR);
	regval = omap_ctrl_readl(AM35XX_CONTROL_LVL_INTR_CLEAR);
}

static void am3517_disable_ethernet_int(void)
{
	u32 regval;

	regval = omap_ctrl_readl(AM35XX_CONTROL_LVL_INTR_CLEAR);
	regval = (regval | AM35XX_CPGMAC_C0_RX_PULSE_CLR |
		AM35XX_CPGMAC_C0_TX_PULSE_CLR);
	omap_ctrl_writel(regval, AM35XX_CONTROL_LVL_INTR_CLEAR);
	regval = omap_ctrl_readl(AM35XX_CONTROL_LVL_INTR_CLEAR);
}

static void am3517_evm_ethernet_init(struct emac_platform_data *pdata)
{
	unsigned int regval;

	pdata->ctrl_reg_offset		= AM35XX_EMAC_CNTRL_OFFSET;
	pdata->ctrl_mod_reg_offset	= AM35XX_EMAC_CNTRL_MOD_OFFSET;
	pdata->ctrl_ram_offset		= AM35XX_EMAC_CNTRL_RAM_OFFSET;
	pdata->ctrl_ram_size		= AM35XX_EMAC_CNTRL_RAM_SIZE;
	pdata->version			= EMAC_VERSION_2;
	pdata->hw_ram_addr		= AM35XX_EMAC_HW_RAM_ADDR;
	pdata->interrupt_enable		= am3517_enable_ethernet_int;
	pdata->interrupt_disable	= am3517_disable_ethernet_int;
	am3517_emac_device.dev.platform_data	= pdata;
	platform_device_register(&am3517_emac_device);
	platform_device_register(&am3517_mdio_device);
	clk_add_alias(NULL, dev_name(&am3517_mdio_device.dev),
		      NULL, &am3517_emac_device.dev);

	regval = omap_ctrl_readl(AM35XX_CONTROL_IP_SW_RESET);
	regval = regval & (~(AM35XX_CPGMACSS_SW_RST));
	omap_ctrl_writel(regval, AM35XX_CONTROL_IP_SW_RESET);
	regval = omap_ctrl_readl(AM35XX_CONTROL_IP_SW_RESET);

	return ;
}



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
	r = gpio_request(GPIO_RTCS35390A_IRQ, "rtcs35390a-irq");
	if (r < 0) {
		printk(KERN_WARNING "failed to request GPIO#%d\n",
				GPIO_RTCS35390A_IRQ);
		return;
	}
	r = gpio_direction_input(GPIO_RTCS35390A_IRQ);
	if (r < 0) {
		printk(KERN_WARNING "GPIO#%d cannot be configured as input\n",
				GPIO_RTCS35390A_IRQ);
		gpio_free(GPIO_RTCS35390A_IRQ);
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

static int lcd_enabled;
static int dvi_enabled;

#if defined(CONFIG_PANEL_SHARP_LQ043T1DG01) || \
		defined(CONFIG_PANEL_SHARP_LQ043T1DG01_MODULE)
static void __init am3517_evm_display_init(void)
{
	int r;

	omap_mux_init_gpio(LCD_PANEL_PWR, OMAP_PIN_INPUT_PULLUP);
	omap_mux_init_gpio(LCD_PANEL_BKLIGHT_PWR, OMAP_PIN_INPUT_PULLDOWN);
	omap_mux_init_gpio(LCD_PANEL_PWM, OMAP_PIN_INPUT_PULLDOWN);
	/*
	 * Enable GPIO 182 = LCD Backlight Power
	 */
	r = gpio_request(LCD_PANEL_BKLIGHT_PWR, "lcd_backlight_pwr");
	if (r) {
		printk(KERN_ERR "failed to get lcd_backlight_pwr\n");
		return;
	}
	gpio_direction_output(LCD_PANEL_BKLIGHT_PWR, 1);
	/*
	 * Enable GPIO 181 = LCD Panel PWM
	 */
	r = gpio_request(LCD_PANEL_PWM, "lcd_pwm");
	if (r) {
		printk(KERN_ERR "failed to get lcd_pwm\n");
		goto err_1;
	}
	gpio_direction_output(LCD_PANEL_PWM, 1);
	/*
	 * Enable GPIO 176 = LCD Panel Power enable pin
	 */
	r = gpio_request(LCD_PANEL_PWR, "lcd_panel_pwr");
	if (r) {
		printk(KERN_ERR "failed to get lcd_panel_pwr\n");
		goto err_2;
	}
	gpio_direction_output(LCD_PANEL_PWR, 1);

	printk(KERN_INFO "Display initialized successfully\n");
	return;

err_2:
	gpio_free(LCD_PANEL_PWM);
err_1:
	gpio_free(LCD_PANEL_BKLIGHT_PWR);
}
#else
static void __init am3517_evm_display_init(void) {}
#endif

static int am3517_evm_panel_enable_lcd(struct omap_dss_device *dssdev)
{
	if (dvi_enabled) {
		printk(KERN_ERR "cannot enable LCD, DVI is enabled\n");
		return -EINVAL;
	}
	gpio_set_value(LCD_PANEL_PWR, 1);
	lcd_enabled = 1;

	return 0;
}

static void am3517_evm_panel_disable_lcd(struct omap_dss_device *dssdev)
{
	gpio_set_value(LCD_PANEL_PWR, 0);
	lcd_enabled = 0;
}

static struct panel_generic_dpi_data lcd_panel = {
	.name			= "sharp_lq",
	.platform_enable	= am3517_evm_panel_enable_lcd,
	.platform_disable	= am3517_evm_panel_disable_lcd,
};

static struct omap_dss_device am3517_evm_lcd_device = {
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.name			= "lcd",
	.driver_name		= "generic_dpi_panel",
	.data			= &lcd_panel,
	.phy.dpi.data_lines 	= 16,
};

static int am3517_evm_panel_enable_tv(struct omap_dss_device *dssdev)
{
	return 0;
}

static void am3517_evm_panel_disable_tv(struct omap_dss_device *dssdev)
{
}

static struct omap_dss_device am3517_evm_tv_device = {
	.type 			= OMAP_DISPLAY_TYPE_VENC,
	.name 			= "tv",
	.driver_name		= "venc",
	.phy.venc.type		= OMAP_DSS_VENC_TYPE_SVIDEO,
	.platform_enable	= am3517_evm_panel_enable_tv,
	.platform_disable	= am3517_evm_panel_disable_tv,
};

static int am3517_evm_panel_enable_dvi(struct omap_dss_device *dssdev)
{
	if (lcd_enabled) {
		printk(KERN_ERR "cannot enable DVI, LCD is enabled\n");
		return -EINVAL;
	}
	dvi_enabled = 1;

	return 0;
}

static void am3517_evm_panel_disable_dvi(struct omap_dss_device *dssdev)
{
	dvi_enabled = 0;
}

static struct panel_generic_dpi_data dvi_panel = {
	.name			= "generic",
	.platform_enable	= am3517_evm_panel_enable_dvi,
	.platform_disable	= am3517_evm_panel_disable_dvi,
};

static struct omap_dss_device am3517_evm_dvi_device = {
	.type			= OMAP_DISPLAY_TYPE_DPI,
	.name			= "dvi",
	.driver_name		= "generic_dpi_panel",
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

static struct platform_device am3517_evm_dss_device = {
	.name		= "omapdss",
	.id		= -1,
	.dev		= {
		.platform_data	= &am3517_evm_dss_data,
	},
};

/*
 * Board initialization
 */
static struct omap_board_config_kernel am3517_evm_config[] __initdata = {
};

static struct platform_device *am3517_evm_devices[] __initdata = {
	&am3517_evm_dss_device,
};

static void __init am3517_evm_init_irq(void)
{
	omap_board_config = am3517_evm_config;
	omap_board_config_size = ARRAY_SIZE(am3517_evm_config);
	omap2_init_common_infrastructure();
	omap2_init_common_devices(NULL, NULL);
	omap_init_irq();
}

static struct omap_musb_board_data musb_board_data = {
	.interface_type         = MUSB_INTERFACE_ULPI,
	.mode                   = MUSB_OTG,
	.power                  = 500,
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

static const struct ehci_hcd_omap_platform_data ehci_pdata __initconst = {
	.port_mode[0] = EHCI_HCD_OMAP_MODE_PHY,
#if defined(CONFIG_PANEL_SHARP_LQ043T1DG01) || \
		defined(CONFIG_PANEL_SHARP_LQ043T1DG01_MODULE)
	.port_mode[1] = EHCI_HCD_OMAP_MODE_UNKNOWN,
#else
	.port_mode[1] = EHCI_HCD_OMAP_MODE_PHY,
#endif
	.port_mode[2] = EHCI_HCD_OMAP_MODE_UNKNOWN,

	.phy_reset  = true,
	.reset_gpio_port[0]  = 57,
	.reset_gpio_port[1]  = -EINVAL,
	.reset_gpio_port[2]  = -EINVAL
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
		.start	= INT_35XX_HECC0_IRQ,
		.end	= INT_35XX_HECC0_IRQ,
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

static void __init am3517_evm_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);

	am3517_evm_i2c_init();
	platform_add_devices(am3517_evm_devices,
				ARRAY_SIZE(am3517_evm_devices));

	omap_serial_init();

	/* Configure GPIO for EHCI port */
	omap_mux_init_gpio(57, OMAP_PIN_OUTPUT);
	usb_ehci_init(&ehci_pdata);
	am3517_evm_hecc_init(&am3517_evm_hecc_pdata);
	/* DSS */
	am3517_evm_display_init();

	/* RTC - S35390A */
	am3517_evm_rtc_init();

	i2c_register_board_info(1, am3517evm_i2c1_boardinfo,
				ARRAY_SIZE(am3517evm_i2c1_boardinfo));
	/*Ethernet*/
	am3517_evm_ethernet_init(&am3517_evm_emac_pdata);

	/* MUSB */
	am3517_evm_musb_init();
}

MACHINE_START(OMAP3517EVM, "OMAP3517/AM3517 EVM")
	.boot_params	= 0x80000100,
	.map_io		= omap3_map_io,
	.reserve	= omap_reserve,
	.init_irq	= am3517_evm_init_irq,
	.init_machine	= am3517_evm_init,
	.timer		= &omap_timer,
MACHINE_END
