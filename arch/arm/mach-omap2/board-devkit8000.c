/*
 * board-devkit8000.c - TimLL Devkit8000
 *
 * Copyright (C) 2009 Kim Botherway
 * Copyright (C) 2010 Thomas Weber
 *
 * Modified from mach-omap2/board-omap3beagle.c
 *
 * Initial code: Syed Mohammed Khasim
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>

#include <linux/regulator/machine.h>
#include <linux/i2c/twl.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <plat/board.h>
#include <plat/common.h>
#include <plat/gpmc.h>
#include <plat/nand.h>
#include <plat/usb.h>
#include <plat/timer-gp.h>
#include <plat/display.h>

#include <plat/mcspi.h>
#include <linux/input/matrix_keypad.h>
#include <linux/spi/spi.h>
#include <linux/spi/ads7846.h>
#include <linux/usb/otg.h>
#include <linux/dm9000.h>
#include <linux/interrupt.h>

#include "sdram-micron-mt46h32m32lf-6.h"

#include "mux.h"
#include "hsmmc.h"

#define GPMC_CS0_BASE  0x60
#define GPMC_CS_SIZE   0x30

#define NAND_BLOCK_SIZE		SZ_128K

#define OMAP_DM9000_GPIO_IRQ	25
#define OMAP3_DEVKIT_TS_GPIO	27

static struct mtd_partition devkit8000_nand_partitions[] = {
	/* All the partition sizes are listed in terms of NAND block size */
	{
		.name		= "X-Loader",
		.offset		= 0,
		.size		= 4 * NAND_BLOCK_SIZE,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "U-Boot",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x80000 */
		.size		= 15 * NAND_BLOCK_SIZE,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "U-Boot Env",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x260000 */
		.size		= 1 * NAND_BLOCK_SIZE,
	},
	{
		.name		= "Kernel",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x280000 */
		.size		= 32 * NAND_BLOCK_SIZE,
	},
	{
		.name		= "File System",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x680000 */
		.size		= MTDPART_SIZ_FULL,
	},
};

static struct omap_nand_platform_data devkit8000_nand_data = {
	.options	= NAND_BUSWIDTH_16,
	.parts		= devkit8000_nand_partitions,
	.nr_parts	= ARRAY_SIZE(devkit8000_nand_partitions),
	.dma_channel	= -1,		/* disable DMA in OMAP NAND driver */
};

static struct resource devkit8000_nand_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device devkit8000_nand_device = {
	.name		= "omap2-nand",
	.id		= -1,
	.dev		= {
		.platform_data	= &devkit8000_nand_data,
	},
	.num_resources	= 1,
	.resource	= &devkit8000_nand_resource,
};

static struct omap2_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		.wires		= 8,
		.gpio_wp	= 29,
	},
	{}	/* Terminator */
};
static struct omap_board_config_kernel devkit8000_config[] __initdata = {
};

static int devkit8000_panel_enable_lcd(struct omap_dss_device *dssdev)
{
	twl_i2c_write_u8(TWL4030_MODULE_GPIO, 0x80, REG_GPIODATADIR1);
	twl_i2c_write_u8(TWL4030_MODULE_LED, 0x0, 0x0);

	return 0;
}

static void devkit8000_panel_disable_lcd(struct omap_dss_device *dssdev)
{
}
static int devkit8000_panel_enable_dvi(struct omap_dss_device *dssdev)
{
	return 0;
}

static void devkit8000_panel_disable_dvi(struct omap_dss_device *dssdev)
{
}

static int devkit8000_panel_enable_tv(struct omap_dss_device *dssdev)
{

	return 0;
}

static void devkit8000_panel_disable_tv(struct omap_dss_device *dssdev)
{
}


static struct regulator_consumer_supply devkit8000_vmmc1_supply = {
	.supply			= "vmmc",
};

static struct regulator_consumer_supply devkit8000_vsim_supply = {
	.supply			= "vmmc_aux",
};


static struct omap_dss_device devkit8000_lcd_device = {
	.name                   = "lcd",
	.driver_name            = "innolux_at_panel",
	.type                   = OMAP_DISPLAY_TYPE_DPI,
	.phy.dpi.data_lines     = 24,
	.platform_enable        = devkit8000_panel_enable_lcd,
	.platform_disable       = devkit8000_panel_disable_lcd,
};
static struct omap_dss_device devkit8000_dvi_device = {
	.name                   = "dvi",
	.driver_name            = "generic_panel",
	.type                   = OMAP_DISPLAY_TYPE_DPI,
	.phy.dpi.data_lines     = 24,
	.platform_enable        = devkit8000_panel_enable_dvi,
	.platform_disable       = devkit8000_panel_disable_dvi,
};

static struct omap_dss_device devkit8000_tv_device = {
	.name                   = "tv",
	.driver_name            = "venc",
	.type                   = OMAP_DISPLAY_TYPE_VENC,
	.phy.venc.type          = OMAP_DSS_VENC_TYPE_SVIDEO,
	.platform_enable        = devkit8000_panel_enable_tv,
	.platform_disable       = devkit8000_panel_disable_tv,
};


static struct omap_dss_device *devkit8000_dss_devices[] = {
	&devkit8000_lcd_device,
	&devkit8000_dvi_device,
	&devkit8000_tv_device,
};

static struct omap_dss_board_info devkit8000_dss_data = {
	.num_devices = ARRAY_SIZE(devkit8000_dss_devices),
	.devices = devkit8000_dss_devices,
	.default_device = &devkit8000_lcd_device,
};

static struct platform_device devkit8000_dss_device = {
	.name		= "omapdss",
	.id		= -1,
	.dev		= {
		.platform_data = &devkit8000_dss_data,
	},
};

static struct regulator_consumer_supply devkit8000_vdda_dac_supply = {
	.supply = "vdda_dac",
	.dev	= &devkit8000_dss_device.dev,
};

static int board_keymap[] = {
	KEY(0, 0, KEY_1),
	KEY(1, 0, KEY_2),
	KEY(2, 0, KEY_3),
	KEY(0, 1, KEY_4),
	KEY(1, 1, KEY_5),
	KEY(2, 1, KEY_6),
	KEY(3, 1, KEY_F5),
	KEY(0, 2, KEY_7),
	KEY(1, 2, KEY_8),
	KEY(2, 2, KEY_9),
	KEY(3, 2, KEY_F6),
	KEY(0, 3, KEY_F7),
	KEY(1, 3, KEY_0),
	KEY(2, 3, KEY_F8),
	PERSISTENT_KEY(4, 5),
	KEY(4, 4, KEY_VOLUMEUP),
	KEY(5, 5, KEY_VOLUMEDOWN),
	0
};

static struct matrix_keymap_data board_map_data = {
	.keymap			= board_keymap,
	.keymap_size		= ARRAY_SIZE(board_keymap),
};

static struct twl4030_keypad_data devkit8000_kp_data = {
	.keymap_data	= &board_map_data,
	.rows		= 6,
	.cols		= 6,
	.rep		= 1,
};

static struct gpio_led gpio_leds[];

static int devkit8000_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	omap_mux_init_gpio(29, OMAP_PIN_INPUT);
	/* gpio + 0 is "mmc0_cd" (input/IRQ) */
	mmc[0].gpio_cd = gpio + 0;
	omap2_hsmmc_init(mmc);

	/* link regulators to MMC adapters */
	devkit8000_vmmc1_supply.dev = mmc[0].dev;
	devkit8000_vsim_supply.dev = mmc[0].dev;

	/* REVISIT: need ehci-omap hooks for external VBUS
	 * power switch and overcurrent detect
	 */

	gpio_request(gpio + 1, "EHCI_nOC");
	gpio_direction_input(gpio + 1);

	/* TWL4030_GPIO_MAX + 0 == ledA, EHCI nEN_USB_PWR (out, active low) */
	gpio_request(gpio + TWL4030_GPIO_MAX, "nEN_USB_PWR");
	gpio_direction_output(gpio + TWL4030_GPIO_MAX, 1);

	/* TWL4030_GPIO_MAX + 1 == ledB, PMU_STAT (out, active low LED) */
	gpio_leds[2].gpio = gpio + TWL4030_GPIO_MAX + 1;

	return 0;
}

static struct twl4030_gpio_platform_data devkit8000_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
	.use_leds	= true,
	.pullups	= BIT(1),
	.pulldowns	= BIT(2) | BIT(6) | BIT(7) | BIT(8) | BIT(13)
				| BIT(15) | BIT(16) | BIT(17),
	.setup		= devkit8000_twl_gpio_setup,
};

static struct regulator_consumer_supply devkit8000_vpll2_supplies[] = {
	{
	.supply		= "vdvi",
	.dev		= &devkit8000_lcd_device.dev,
	},
	{
	.supply		= "vdss_dsi",
	.dev		= &devkit8000_dss_device.dev,
	}
};

/* VMMC1 for MMC1 pins CMD, CLK, DAT0..DAT3 (20 mA, plus card == max 220 mA) */
static struct regulator_init_data devkit8000_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &devkit8000_vmmc1_supply,
};

/* VSIM for MMC1 pins DAT4..DAT7 (2 mA, plus card == max 50 mA) */
static struct regulator_init_data devkit8000_vsim = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 3000000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &devkit8000_vsim_supply,
};

/* VDAC for DSS driving S-Video (8 mA unloaded, max 65 mA) */
static struct regulator_init_data devkit8000_vdac = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &devkit8000_vdda_dac_supply,
};

/* VPLL2 for digital video outputs */
static struct regulator_init_data devkit8000_vpll2 = {
	.constraints = {
		.name			= "VDVI",
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(devkit8000_vpll2_supplies),
	.consumer_supplies	= devkit8000_vpll2_supplies,
};

static struct twl4030_usb_data devkit8000_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static struct twl4030_codec_audio_data devkit8000_audio_data = {
	.audio_mclk = 26000000,
};

static struct twl4030_codec_data devkit8000_codec_data = {
	.audio_mclk = 26000000,
	.audio = &devkit8000_audio_data,
};

static struct twl4030_platform_data devkit8000_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.usb		= &devkit8000_usb_data,
	.gpio		= &devkit8000_gpio_data,
	.codec		= &devkit8000_codec_data,
	.vmmc1		= &devkit8000_vmmc1,
	.vsim		= &devkit8000_vsim,
	.vdac		= &devkit8000_vdac,
	.vpll2		= &devkit8000_vpll2,
	.keypad		= &devkit8000_kp_data,
};

static struct i2c_board_info __initdata devkit8000_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_34XX_SYS_NIRQ,
		.platform_data = &devkit8000_twldata,
	},
};

static int __init devkit8000_i2c_init(void)
{
	omap_register_i2c_bus(1, 2600, devkit8000_i2c_boardinfo,
			ARRAY_SIZE(devkit8000_i2c_boardinfo));
	/* Bus 3 is attached to the DVI port where devices like the pico DLP
	 * projector don't work reliably with 400kHz */
	omap_register_i2c_bus(3, 400, NULL, 0);
	return 0;
}

static struct gpio_led gpio_leds[] = {
	{
		.name			= "led1",
		.default_trigger	= "heartbeat",
		.gpio			= 186,
		.active_low		= true,
	},
	{
		.name			= "led2",
		.default_trigger	= "mmc0",
		.gpio			= 163,
		.active_low		= true,
	},
	{
		.name			= "ledB",
		.default_trigger	= "none",
		.gpio			= 153,
		.active_low             = true,
	},
	{
		.name			= "led3",
		.default_trigger	= "none",
		.gpio			= 164,
		.active_low             = true,
	},
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_led_info,
	},
};

static struct gpio_keys_button gpio_buttons[] = {
	{
		.code			= BTN_EXTRA,
		.gpio			= 26,
		.desc			= "user",
		.wakeup			= 1,
	},
};

static struct gpio_keys_platform_data gpio_key_info = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

static struct platform_device keys_gpio = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_key_info,
	},
};


static void __init devkit8000_init_irq(void)
{
	omap_board_config = devkit8000_config;
	omap_board_config_size = ARRAY_SIZE(devkit8000_config);
	omap2_init_common_hw(mt46h32m32lf6_sdrc_params,
			     mt46h32m32lf6_sdrc_params);
	omap_init_irq();
#ifdef CONFIG_OMAP_32K_TIMER
	omap2_gp_clockevent_set_gptimer(12);
#endif
	omap_gpio_init();
}

static void __init devkit8000_ads7846_init(void)
{
	int gpio = OMAP3_DEVKIT_TS_GPIO;
	int ret;

	ret = gpio_request(gpio, "ads7846_pen_down");
	if (ret < 0) {
		printk(KERN_ERR "Failed to request GPIO %d for "
				"ads7846 pen down IRQ\n", gpio);
		return;
	}

	gpio_direction_input(gpio);
}

static int ads7846_get_pendown_state(void)
{
	return !gpio_get_value(OMAP3_DEVKIT_TS_GPIO);
}

static struct ads7846_platform_data ads7846_config = {
	.x_max                  = 0x0fff,
	.y_max                  = 0x0fff,
	.x_plate_ohms           = 180,
	.pressure_max           = 255,
	.debounce_max           = 10,
	.debounce_tol           = 5,
	.debounce_rep           = 1,
	.get_pendown_state	= ads7846_get_pendown_state,
	.keep_vref_on		= 1,
	.settle_delay_usecs     = 150,
};

static struct omap2_mcspi_device_config ads7846_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,	/* 0: slave, 1: master */
};

static struct spi_board_info devkit8000_spi_board_info[] __initdata = {
	{
		.modalias		= "ads7846",
		.bus_num		= 2,
		.chip_select		= 0,
		.max_speed_hz		= 1500000,
		.controller_data	= &ads7846_mcspi_config,
		.irq			= OMAP_GPIO_IRQ(OMAP3_DEVKIT_TS_GPIO),
		.platform_data		= &ads7846_config,
	}
};

#define OMAP_DM9000_BASE	0x2c000000

static struct resource omap_dm9000_resources[] = {
	[0] = {
		.start		= OMAP_DM9000_BASE,
		.end		= (OMAP_DM9000_BASE + 0x4 - 1),
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= (OMAP_DM9000_BASE + 0x400),
		.end		= (OMAP_DM9000_BASE + 0x400 + 0x4 - 1),
		.flags		= IORESOURCE_MEM,
	},
	[2] = {
		.start		= OMAP_GPIO_IRQ(OMAP_DM9000_GPIO_IRQ),
		.flags		= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct dm9000_plat_data omap_dm9000_platdata = {
	.flags = DM9000_PLATF_16BITONLY,
};

static struct platform_device omap_dm9000_dev = {
	.name = "dm9000",
	.id = -1,
	.num_resources	= ARRAY_SIZE(omap_dm9000_resources),
	.resource	= omap_dm9000_resources,
	.dev = {
		.platform_data = &omap_dm9000_platdata,
	},
};

static void __init omap_dm9000_init(void)
{
	if (gpio_request(OMAP_DM9000_GPIO_IRQ, "dm9000 irq") < 0) {
		printk(KERN_ERR "Failed to request GPIO%d for dm9000 IRQ\n",
			OMAP_DM9000_GPIO_IRQ);
		return;
		}

	gpio_direction_input(OMAP_DM9000_GPIO_IRQ);
}

static struct platform_device *devkit8000_devices[] __initdata = {
	&devkit8000_dss_device,
	&leds_gpio,
	&keys_gpio,
	&omap_dm9000_dev,
};

static void __init devkit8000_flash_init(void)
{
	u8 cs = 0;
	u8 nandcs = GPMC_CS_NUM + 1;

	u32 gpmc_base_add = OMAP34XX_GPMC_VIRT;

	/* find out the chip-select on which NAND exists */
	while (cs < GPMC_CS_NUM) {
		u32 ret = 0;
		ret = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG1);

		if ((ret & 0xC00) == 0x800) {
			printk(KERN_INFO "Found NAND on CS%d\n", cs);
			if (nandcs > GPMC_CS_NUM)
				nandcs = cs;
		}
		cs++;
	}

	if (nandcs > GPMC_CS_NUM) {
		printk(KERN_INFO "NAND: Unable to find configuration "
				 "in GPMC\n ");
		return;
	}

	if (nandcs < GPMC_CS_NUM) {
		devkit8000_nand_data.cs = nandcs;
		devkit8000_nand_data.gpmc_cs_baseaddr = (void *)
			(gpmc_base_add + GPMC_CS0_BASE + nandcs * GPMC_CS_SIZE);
		devkit8000_nand_data.gpmc_baseaddr = (void *)
			(gpmc_base_add);

		printk(KERN_INFO "Registering NAND on CS%d\n", nandcs);
		if (platform_device_register(&devkit8000_nand_device) < 0)
			printk(KERN_ERR "Unable to register NAND device\n");
	}
}

static struct omap_musb_board_data musb_board_data = {
	.interface_type		= MUSB_INTERFACE_ULPI,
	.mode			= MUSB_OTG,
	.power			= 100,
};

static struct ehci_hcd_omap_platform_data ehci_pdata __initconst = {

	.port_mode[0] = EHCI_HCD_OMAP_MODE_PHY,
	.port_mode[1] = EHCI_HCD_OMAP_MODE_PHY,
	.port_mode[2] = EHCI_HCD_OMAP_MODE_UNKNOWN,

	.phy_reset  = true,
	.reset_gpio_port[0]  = -EINVAL,
	.reset_gpio_port[1]  = 147,
	.reset_gpio_port[2]  = -EINVAL
};

static void __init devkit8000_init(void)
{
	devkit8000_i2c_init();
	platform_add_devices(devkit8000_devices,
			ARRAY_SIZE(devkit8000_devices));
	omap_board_config = devkit8000_config;
	omap_board_config_size = ARRAY_SIZE(devkit8000_config);

	spi_register_board_info(devkit8000_spi_board_info,
	ARRAY_SIZE(devkit8000_spi_board_info));

	omap_serial_init();

	omap_dm9000_init();

	devkit8000_ads7846_init();

	omap_mux_init_gpio(170, OMAP_PIN_INPUT);

	gpio_request(170, "DVI_nPD");
	/* REVISIT leave DVI powered down until it's needed ... */
	gpio_direction_output(170, true);

	usb_musb_init(&musb_board_data);
	usb_ehci_init(&ehci_pdata);
	devkit8000_flash_init();

	/* Ensure SDRC pins are mux'd for self-refresh */
	omap_mux_init_signal("sdr_cke0", OMAP_PIN_OUTPUT);
	omap_mux_init_signal("sdr_cke1", OMAP_PIN_OUTPUT);
}

static void __init devkit8000_map_io(void)
{
	omap2_set_globals_343x();
	omap34xx_map_common_io();
}

MACHINE_START(DEVKIT8000, "OMAP3 Devkit8000")
	.phys_io	= 0x48000000,
	.io_pg_offst	= ((0xd8000000) >> 18) & 0xfffc,
	.boot_params	= 0x80000100,
	.map_io		= devkit8000_map_io,
	.init_irq	= devkit8000_init_irq,
	.init_machine	= devkit8000_init,
	.timer		= &omap_timer,
MACHINE_END
