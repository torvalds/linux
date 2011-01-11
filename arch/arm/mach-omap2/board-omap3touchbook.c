/*
 * linux/arch/arm/mach-omap2/board-omap3touchbook.c
 *
 * Copyright (C) 2009 Always Innovating
 *
 * Modified from mach-omap2/board-omap3beagleboard.c
 *
 * Initial code: Grégoire Gentil, Tim Yamin
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
#include <linux/mmc/host.h>

#include <plat/mcspi.h>
#include <linux/spi/spi.h>

#include <linux/spi/ads7846.h>

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

#include "mux.h"
#include "hsmmc.h"
#include "timer-gp.h"

#include <asm/setup.h>

#define NAND_BLOCK_SIZE		SZ_128K

#define OMAP3_AC_GPIO		136
#define OMAP3_TS_GPIO		162
#define TB_BL_PWM_TIMER		9
#define TB_KILL_POWER_GPIO	168

static unsigned long touchbook_revision;

static struct mtd_partition omap3touchbook_nand_partitions[] = {
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

static struct omap_nand_platform_data omap3touchbook_nand_data = {
	.options	= NAND_BUSWIDTH_16,
	.parts		= omap3touchbook_nand_partitions,
	.nr_parts	= ARRAY_SIZE(omap3touchbook_nand_partitions),
	.dma_channel	= -1,		/* disable DMA in OMAP NAND driver */
	.nand_setup	= NULL,
	.dev_ready	= NULL,
};

#include "sdram-micron-mt46h32m32lf-6.h"

static struct omap2_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA,
		.gpio_wp	= 29,
	},
	{}	/* Terminator */
};

static struct platform_device omap3_touchbook_lcd_device = {
	.name		= "omap3touchbook_lcd",
	.id		= -1,
};

static struct omap_lcd_config omap3_touchbook_lcd_config __initdata = {
	.ctrl_name	= "internal",
};

static struct regulator_consumer_supply touchbook_vmmc1_supply = {
	.supply			= "vmmc",
};

static struct regulator_consumer_supply touchbook_vsim_supply = {
	.supply			= "vmmc_aux",
};

static struct gpio_led gpio_leds[];

static int touchbook_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	if (system_rev >= 0x20 && system_rev <= 0x34301000) {
		omap_mux_init_gpio(23, OMAP_PIN_INPUT);
		mmc[0].gpio_wp = 23;
	} else {
		omap_mux_init_gpio(29, OMAP_PIN_INPUT);
	}
	/* gpio + 0 is "mmc0_cd" (input/IRQ) */
	mmc[0].gpio_cd = gpio + 0;
	omap2_hsmmc_init(mmc);

	/* link regulators to MMC adapters */
	touchbook_vmmc1_supply.dev = mmc[0].dev;
	touchbook_vsim_supply.dev = mmc[0].dev;

	/* REVISIT: need ehci-omap hooks for external VBUS
	 * power switch and overcurrent detect
	 */

	gpio_request(gpio + 1, "EHCI_nOC");
	gpio_direction_input(gpio + 1);

	/* TWL4030_GPIO_MAX + 0 == ledA, EHCI nEN_USB_PWR (out, active low) */
	gpio_request(gpio + TWL4030_GPIO_MAX, "nEN_USB_PWR");
	gpio_direction_output(gpio + TWL4030_GPIO_MAX, 0);

	/* TWL4030_GPIO_MAX + 1 == ledB, PMU_STAT (out, active low LED) */
	gpio_leds[2].gpio = gpio + TWL4030_GPIO_MAX + 1;

	return 0;
}

static struct twl4030_gpio_platform_data touchbook_gpio_data = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
	.use_leds	= true,
	.pullups	= BIT(1),
	.pulldowns	= BIT(2) | BIT(6) | BIT(7) | BIT(8) | BIT(13)
				| BIT(15) | BIT(16) | BIT(17),
	.setup		= touchbook_twl_gpio_setup,
};

static struct regulator_consumer_supply touchbook_vdac_supply = {
	.supply		= "vdac",
	.dev		= &omap3_touchbook_lcd_device.dev,
};

static struct regulator_consumer_supply touchbook_vdvi_supply = {
	.supply		= "vdvi",
	.dev		= &omap3_touchbook_lcd_device.dev,
};

/* VMMC1 for MMC1 pins CMD, CLK, DAT0..DAT3 (20 mA, plus card == max 220 mA) */
static struct regulator_init_data touchbook_vmmc1 = {
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
	.consumer_supplies	= &touchbook_vmmc1_supply,
};

/* VSIM for MMC1 pins DAT4..DAT7 (2 mA, plus card == max 50 mA) */
static struct regulator_init_data touchbook_vsim = {
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
	.consumer_supplies	= &touchbook_vsim_supply,
};

/* VDAC for DSS driving S-Video (8 mA unloaded, max 65 mA) */
static struct regulator_init_data touchbook_vdac = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &touchbook_vdac_supply,
};

/* VPLL2 for digital video outputs */
static struct regulator_init_data touchbook_vpll2 = {
	.constraints = {
		.name			= "VDVI",
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &touchbook_vdvi_supply,
};

static struct twl4030_usb_data touchbook_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static struct twl4030_codec_audio_data touchbook_audio_data = {
	.audio_mclk = 26000000,
};

static struct twl4030_codec_data touchbook_codec_data = {
	.audio_mclk = 26000000,
	.audio = &touchbook_audio_data,
};

static struct twl4030_platform_data touchbook_twldata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.usb		= &touchbook_usb_data,
	.gpio		= &touchbook_gpio_data,
	.codec		= &touchbook_codec_data,
	.vmmc1		= &touchbook_vmmc1,
	.vsim		= &touchbook_vsim,
	.vdac		= &touchbook_vdac,
	.vpll2		= &touchbook_vpll2,
};

static struct i2c_board_info __initdata touchbook_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags = I2C_CLIENT_WAKE,
		.irq = INT_34XX_SYS_NIRQ,
		.platform_data = &touchbook_twldata,
	},
};

static struct i2c_board_info __initdata touchBook_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("bq27200", 0x55),
	},
};

static int __init omap3_touchbook_i2c_init(void)
{
	/* Standard TouchBook bus */
	omap_register_i2c_bus(1, 2600, touchbook_i2c_boardinfo,
			ARRAY_SIZE(touchbook_i2c_boardinfo));

	/* Additional TouchBook bus */
	omap_register_i2c_bus(3, 100, touchBook_i2c_boardinfo,
			ARRAY_SIZE(touchBook_i2c_boardinfo));

	return 0;
}

static void __init omap3_ads7846_init(void)
{
	if (gpio_request(OMAP3_TS_GPIO, "ads7846_pen_down")) {
		printk(KERN_ERR "Failed to request GPIO %d for "
				"ads7846 pen down IRQ\n", OMAP3_TS_GPIO);
		return;
	}

	gpio_direction_input(OMAP3_TS_GPIO);
	gpio_set_debounce(OMAP3_TS_GPIO, 310);
}

static struct ads7846_platform_data ads7846_config = {
	.x_min			= 100,
	.y_min			= 265,
	.x_max			= 3950,
	.y_max			= 3750,
	.x_plate_ohms		= 40,
	.pressure_max		= 255,
	.debounce_max		= 10,
	.debounce_tol		= 5,
	.debounce_rep		= 1,
	.gpio_pendown		= OMAP3_TS_GPIO,
	.keep_vref_on		= 1,
};

static struct omap2_mcspi_device_config ads7846_mcspi_config = {
	.turbo_mode	= 0,
	.single_channel	= 1,	/* 0: slave, 1: master */
};

static struct spi_board_info omap3_ads7846_spi_board_info[] __initdata = {
	{
		.modalias		= "ads7846",
		.bus_num		= 4,
		.chip_select		= 0,
		.max_speed_hz		= 1500000,
		.controller_data	= &ads7846_mcspi_config,
		.irq			= OMAP_GPIO_IRQ(OMAP3_TS_GPIO),
		.platform_data		= &ads7846_config,
	}
};

static struct gpio_led gpio_leds[] = {
	{
		.name			= "touchbook::usr0",
		.default_trigger	= "heartbeat",
		.gpio			= 150,
	},
	{
		.name			= "touchbook::usr1",
		.default_trigger	= "mmc0",
		.gpio			= 149,
	},
	{
		.name			= "touchbook::pmu_stat",
		.gpio			= -EINVAL,	/* gets replaced */
		.active_low		= true,
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
		.gpio			= 7,
		.desc			= "user",
		.wakeup			= 1,
	},
	{
		.code			= KEY_POWER,
		.gpio			= 183,
		.desc			= "power",
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

static struct omap_board_config_kernel omap3_touchbook_config[] __initdata = {
	{ OMAP_TAG_LCD,		&omap3_touchbook_lcd_config },
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif

static void __init omap3_touchbook_init_irq(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);
	omap_board_config = omap3_touchbook_config;
	omap_board_config_size = ARRAY_SIZE(omap3_touchbook_config);
	omap2_init_common_infrastructure();
	omap2_init_common_devices(mt46h32m32lf6_sdrc_params,
				  mt46h32m32lf6_sdrc_params);
	omap_init_irq();
#ifdef CONFIG_OMAP_32K_TIMER
	omap2_gp_clockevent_set_gptimer(12);
#endif
}

static struct platform_device *omap3_touchbook_devices[] __initdata = {
	&omap3_touchbook_lcd_device,
	&leds_gpio,
	&keys_gpio,
};

static void __init omap3touchbook_flash_init(void)
{
	u8 cs = 0;
	u8 nandcs = GPMC_CS_NUM + 1;

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
		omap3touchbook_nand_data.cs = nandcs;

		printk(KERN_INFO "Registering NAND on CS%d\n", nandcs);
		if (gpmc_nand_init(&omap3touchbook_nand_data) < 0)
			printk(KERN_ERR "Unable to register NAND device\n");
	}
}

static const struct ehci_hcd_omap_platform_data ehci_pdata __initconst = {

	.port_mode[0] = EHCI_HCD_OMAP_MODE_PHY,
	.port_mode[1] = EHCI_HCD_OMAP_MODE_PHY,
	.port_mode[2] = EHCI_HCD_OMAP_MODE_UNKNOWN,

	.phy_reset  = true,
	.reset_gpio_port[0]  = -EINVAL,
	.reset_gpio_port[1]  = 147,
	.reset_gpio_port[2]  = -EINVAL
};

static void omap3_touchbook_poweroff(void)
{
	int r;

	r = gpio_request(TB_KILL_POWER_GPIO, "DVI reset");
	if (r < 0) {
		printk(KERN_ERR "Unable to get kill power GPIO\n");
		return;
	}

	gpio_direction_output(TB_KILL_POWER_GPIO, 0);
}

static int __init early_touchbook_revision(char *p)
{
	if (!p)
		return 0;

	return strict_strtoul(p, 10, &touchbook_revision);
}
early_param("tbr", early_touchbook_revision);

static struct omap_musb_board_data musb_board_data = {
	.interface_type		= MUSB_INTERFACE_ULPI,
	.mode			= MUSB_OTG,
	.power			= 100,
};

static void __init omap3_touchbook_init(void)
{
	pm_power_off = omap3_touchbook_poweroff;

	omap3_touchbook_i2c_init();
	platform_add_devices(omap3_touchbook_devices,
			ARRAY_SIZE(omap3_touchbook_devices));
	omap_serial_init();

	omap_mux_init_gpio(170, OMAP_PIN_INPUT);
	gpio_request(176, "DVI_nPD");
	/* REVISIT leave DVI powered down until it's needed ... */
	gpio_direction_output(176, true);

	/* Touchscreen and accelerometer */
	spi_register_board_info(omap3_ads7846_spi_board_info,
				ARRAY_SIZE(omap3_ads7846_spi_board_info));
	omap3_ads7846_init();
	usb_musb_init(&musb_board_data);
	usb_ehci_init(&ehci_pdata);
	omap3touchbook_flash_init();

	/* Ensure SDRC pins are mux'd for self-refresh */
	omap_mux_init_signal("sdrc_cke0", OMAP_PIN_OUTPUT);
	omap_mux_init_signal("sdrc_cke1", OMAP_PIN_OUTPUT);
}

MACHINE_START(TOUCHBOOK, "OMAP3 touchbook Board")
	/* Maintainer: Gregoire Gentil - http://www.alwaysinnovating.com */
	.boot_params	= 0x80000100,
	.map_io		= omap3_map_io,
	.reserve	= omap_reserve,
	.init_irq	= omap3_touchbook_init_irq,
	.init_machine	= omap3_touchbook_init,
	.timer		= &omap_timer,
MACHINE_END
