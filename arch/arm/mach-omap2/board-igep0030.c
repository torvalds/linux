/*
 * Copyright (C) 2010 - ISEE 2007 SL
 *
 * Modified from mach-omap2/board-generic.c
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
#include <linux/gpio.h>
#include <linux/interrupt.h>

#include <linux/regulator/machine.h>
#include <linux/i2c/twl.h>
#include <linux/mmc/host.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <plat/board.h>
#include <plat/common.h>
#include <plat/gpmc.h>
#include <plat/usb.h>
#include <plat/onenand.h>

#include "mux.h"
#include "hsmmc.h"
#include "sdram-numonyx-m65kxxxxam.h"

#define IGEP3_GPIO_LED0_GREEN	54
#define IGEP3_GPIO_LED0_RED	53
#define IGEP3_GPIO_LED1_RED	16

#define IGEP3_GPIO_WIFI_NPD	138
#define IGEP3_GPIO_WIFI_NRESET	139
#define IGEP3_GPIO_BT_NRESET	137

#define IGEP3_GPIO_USBH_NRESET  183


#if defined(CONFIG_MTD_ONENAND_OMAP2) || \
	defined(CONFIG_MTD_ONENAND_OMAP2_MODULE)

#define ONENAND_MAP             0x20000000

/*
 * x2 Flash built-in COMBO POP MEMORY
 * Since the device is equipped with two DataRAMs, and two-plane NAND
 * Flash memory array, these two component enables simultaneous program
 * of 4KiB. Plane1 has only even blocks such as block0, block2, block4
 * while Plane2 has only odd blocks such as block1, block3, block5.
 * So MTD regards it as 4KiB page size and 256KiB block size 64*(2*2048)
 */

static struct mtd_partition igep3_onenand_partitions[] = {
	{
		.name           = "X-Loader",
		.offset         = 0,
		.size           = 2 * (64*(2*2048))
	},
	{
		.name           = "U-Boot",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 6 * (64*(2*2048)),
	},
	{
		.name           = "Environment",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 2 * (64*(2*2048)),
	},
	{
		.name           = "Kernel",
		.offset         = MTDPART_OFS_APPEND,
		.size           = 12 * (64*(2*2048)),
	},
	{
		.name           = "File System",
		.offset         = MTDPART_OFS_APPEND,
		.size           = MTDPART_SIZ_FULL,
	},
};

static struct omap_onenand_platform_data igep3_onenand_pdata = {
	.parts = igep3_onenand_partitions,
	.nr_parts = ARRAY_SIZE(igep3_onenand_partitions),
	.onenand_setup = NULL,
	.dma_channel	= -1,	/* disable DMA in OMAP OneNAND driver */
};

static struct platform_device igep3_onenand_device = {
	.name		= "omap2-onenand",
	.id		= -1,
	.dev = {
		.platform_data = &igep3_onenand_pdata,
	},
};

static void __init igep3_flash_init(void)
{
	u8 cs = 0;
	u8 onenandcs = GPMC_CS_NUM + 1;

	for (cs = 0; cs < GPMC_CS_NUM; cs++) {
		u32 ret;
		ret = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG1);

		/* Check if NAND/oneNAND is configured */
		if ((ret & 0xC00) == 0x800)
			/* NAND found */
			pr_err("IGEP3: Unsupported NAND found\n");
		else {
			ret = gpmc_cs_read_reg(cs, GPMC_CS_CONFIG7);

			if ((ret & 0x3F) == (ONENAND_MAP >> 24))
				/* OneNAND found */
				onenandcs = cs;
		}
	}

	if (onenandcs > GPMC_CS_NUM) {
		pr_err("IGEP3: Unable to find configuration in GPMC\n");
		return;
	}

	igep3_onenand_pdata.cs = onenandcs;

	if (platform_device_register(&igep3_onenand_device) < 0)
		pr_err("IGEP3: Unable to register OneNAND device\n");
}

#else
static void __init igep3_flash_init(void) {}
#endif

static struct regulator_consumer_supply igep3_vmmc1_supply = {
	.supply		= "vmmc",
};

/* VMMC1 for OMAP VDD_MMC1 (i/o) and MMC1 card */
static struct regulator_init_data igep3_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies  = 1,
	.consumer_supplies      = &igep3_vmmc1_supply,
};

static struct omap2_hsmmc_info mmc[] = {
	[0] = {
		.mmc		= 1,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
	},
#if defined(CONFIG_LIBERTAS_SDIO) || defined(CONFIG_LIBERTAS_SDIO_MODULE)
	[1] = {
		.mmc		= 2,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_cd	= -EINVAL,
		.gpio_wp	= -EINVAL,
	},
#endif
	{}      /* Terminator */
};

#if defined(CONFIG_LEDS_GPIO) || defined(CONFIG_LEDS_GPIO_MODULE)
#include <linux/leds.h>

static struct gpio_led igep3_gpio_leds[] = {
	[0] = {
		.name			= "gpio-led:red:d0",
		.gpio			= IGEP3_GPIO_LED0_RED,
		.default_trigger	= "default-off"
	},
	[1] = {
		.name			= "gpio-led:green:d0",
		.gpio			= IGEP3_GPIO_LED0_GREEN,
		.default_trigger	= "default-off",
	},
	[2] = {
		.name			= "gpio-led:red:d1",
		.gpio			= IGEP3_GPIO_LED1_RED,
		.default_trigger	= "default-off",
	},
	[3] = {
		.name			= "gpio-led:green:d1",
		.default_trigger	= "heartbeat",
		.gpio			= -EINVAL, /* gets replaced */
	},
};

static struct gpio_led_platform_data igep3_led_pdata = {
	.leds           = igep3_gpio_leds,
	.num_leds       = ARRAY_SIZE(igep3_gpio_leds),
};

static struct platform_device igep3_led_device = {
	 .name   = "leds-gpio",
	 .id     = -1,
	 .dev    = {
		 .platform_data = &igep3_led_pdata,
	},
};

static void __init igep3_leds_init(void)
{
	platform_device_register(&igep3_led_device);
}

#else
static inline void igep3_leds_init(void)
{
	if ((gpio_request(IGEP3_GPIO_LED0_RED, "gpio-led:red:d0") == 0) &&
	    (gpio_direction_output(IGEP3_GPIO_LED0_RED, 1) == 0)) {
		gpio_export(IGEP3_GPIO_LED0_RED, 0);
		gpio_set_value(IGEP3_GPIO_LED0_RED, 1);
	} else
		pr_warning("IGEP3: Could not obtain gpio GPIO_LED0_RED\n");

	if ((gpio_request(IGEP3_GPIO_LED0_GREEN, "gpio-led:green:d0") == 0) &&
	    (gpio_direction_output(IGEP3_GPIO_LED0_GREEN, 1) == 0)) {
		gpio_export(IGEP3_GPIO_LED0_GREEN, 0);
		gpio_set_value(IGEP3_GPIO_LED0_GREEN, 1);
	} else
		pr_warning("IGEP3: Could not obtain gpio GPIO_LED0_GREEN\n");

	if ((gpio_request(IGEP3_GPIO_LED1_RED, "gpio-led:red:d1") == 0) &&
		(gpio_direction_output(IGEP3_GPIO_LED1_RED, 1) == 0)) {
		gpio_export(IGEP3_GPIO_LED1_RED, 0);
		gpio_set_value(IGEP3_GPIO_LED1_RED, 1);
	} else
		pr_warning("IGEP3: Could not obtain gpio GPIO_LED1_RED\n");
}
#endif

static int igep3_twl4030_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	/* gpio + 0 is "mmc0_cd" (input/IRQ) */
	mmc[0].gpio_cd = gpio + 0;
	omap2_hsmmc_init(mmc);

	/*
	 * link regulators to MMC adapters ... we "know" the
	 * regulators will be set up only *after* we return.
	 */
	igep3_vmmc1_supply.dev = mmc[0].dev;

	/* TWL4030_GPIO_MAX + 1 == ledB (out, active low LED) */
#if !defined(CONFIG_LEDS_GPIO) && !defined(CONFIG_LEDS_GPIO_MODULE)
	if ((gpio_request(gpio+TWL4030_GPIO_MAX+1, "gpio-led:green:d1") == 0)
	    && (gpio_direction_output(gpio + TWL4030_GPIO_MAX + 1, 1) == 0)) {
		gpio_export(gpio + TWL4030_GPIO_MAX + 1, 0);
		gpio_set_value(gpio + TWL4030_GPIO_MAX + 1, 0);
	} else
		pr_warning("IGEP3: Could not obtain gpio GPIO_LED1_GREEN\n");
#else
	igep3_gpio_leds[3].gpio = gpio + TWL4030_GPIO_MAX + 1;
#endif

	return 0;
};

static struct twl4030_gpio_platform_data igep3_twl4030_gpio_pdata = {
	.gpio_base	= OMAP_MAX_GPIO_LINES,
	.irq_base	= TWL4030_GPIO_IRQ_BASE,
	.irq_end	= TWL4030_GPIO_IRQ_END,
	.use_leds	= true,
	.setup		= igep3_twl4030_gpio_setup,
};

static struct twl4030_usb_data igep3_twl4030_usb_data = {
	.usb_mode	= T2_USB_MODE_ULPI,
};

static void __init igep3_init_irq(void)
{
	omap2_init_common_infrastructure();
	omap2_init_common_devices(m65kxxxxam_sdrc_params,
				  m65kxxxxam_sdrc_params);
	omap_init_irq();
}

static struct twl4030_platform_data igep3_twl4030_pdata = {
	.irq_base	= TWL4030_IRQ_BASE,
	.irq_end	= TWL4030_IRQ_END,

	/* platform_data for children goes here */
	.usb		= &igep3_twl4030_usb_data,
	.gpio		= &igep3_twl4030_gpio_pdata,
	.vmmc1		= &igep3_vmmc1,
};

static struct i2c_board_info __initdata igep3_i2c_boardinfo[] = {
	{
		I2C_BOARD_INFO("twl4030", 0x48),
		.flags		= I2C_CLIENT_WAKE,
		.irq		= INT_34XX_SYS_NIRQ,
		.platform_data	= &igep3_twl4030_pdata,
	},
};

static int __init igep3_i2c_init(void)
{
	omap_register_i2c_bus(1, 2600, igep3_i2c_boardinfo,
			ARRAY_SIZE(igep3_i2c_boardinfo));

	return 0;
}

static struct omap_musb_board_data musb_board_data = {
	.interface_type	= MUSB_INTERFACE_ULPI,
	.mode		= MUSB_OTG,
	.power		= 100,
};

#if defined(CONFIG_LIBERTAS_SDIO) || defined(CONFIG_LIBERTAS_SDIO_MODULE)

static void __init igep3_wifi_bt_init(void)
{
	/* Configure MUX values for W-LAN + Bluetooth GPIO's */
	omap_mux_init_gpio(IGEP3_GPIO_WIFI_NPD, OMAP_PIN_OUTPUT);
	omap_mux_init_gpio(IGEP3_GPIO_WIFI_NRESET, OMAP_PIN_OUTPUT);
	omap_mux_init_gpio(IGEP3_GPIO_BT_NRESET, OMAP_PIN_OUTPUT);

	/* Set GPIO's for  W-LAN + Bluetooth combo module */
	if ((gpio_request(IGEP3_GPIO_WIFI_NPD, "GPIO_WIFI_NPD") == 0) &&
	    (gpio_direction_output(IGEP3_GPIO_WIFI_NPD, 1) == 0)) {
		gpio_export(IGEP3_GPIO_WIFI_NPD, 0);
	} else
		pr_warning("IGEP3: Could not obtain gpio GPIO_WIFI_NPD\n");

	if ((gpio_request(IGEP3_GPIO_WIFI_NRESET, "GPIO_WIFI_NRESET") == 0) &&
	    (gpio_direction_output(IGEP3_GPIO_WIFI_NRESET, 1) == 0)) {
		gpio_export(IGEP3_GPIO_WIFI_NRESET, 0);
		gpio_set_value(IGEP3_GPIO_WIFI_NRESET, 0);
		udelay(10);
		gpio_set_value(IGEP3_GPIO_WIFI_NRESET, 1);
	} else
		pr_warning("IGEP3: Could not obtain gpio GPIO_WIFI_NRESET\n");

	if ((gpio_request(IGEP3_GPIO_BT_NRESET, "GPIO_BT_NRESET") == 0) &&
	    (gpio_direction_output(IGEP3_GPIO_BT_NRESET, 1) == 0)) {
		gpio_export(IGEP3_GPIO_BT_NRESET, 0);
	} else
		pr_warning("IGEP3: Could not obtain gpio GPIO_BT_NRESET\n");
}
#else
void __init igep3_wifi_bt_init(void) {}
#endif

static const struct ehci_hcd_omap_platform_data ehci_pdata __initconst = {
	.port_mode[0] = EHCI_HCD_OMAP_MODE_UNKNOWN,
	.port_mode[1] = EHCI_HCD_OMAP_MODE_PHY,
	.port_mode[2] = EHCI_HCD_OMAP_MODE_UNKNOWN,

	.phy_reset = true,
	.reset_gpio_port[0] = -EINVAL,
	.reset_gpio_port[1] = IGEP3_GPIO_USBH_NRESET,
	.reset_gpio_port[2] = -EINVAL,
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {
	OMAP3_MUX(I2C2_SDA, OMAP_MUX_MODE4 | OMAP_PIN_OUTPUT),
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif

static void __init igep3_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);

	/* Register I2C busses and drivers */
	igep3_i2c_init();

	omap_serial_init();
	usb_musb_init(&musb_board_data);
	usb_ehci_init(&ehci_pdata);

	igep3_flash_init();
	igep3_leds_init();

	/*
	 * WLAN-BT combo module from MuRata wich has a Marvell WLAN
	 * (88W8686) + CSR Bluetooth chipset. Uses SDIO interface.
	 */
	igep3_wifi_bt_init();

}

MACHINE_START(IGEP0030, "IGEP OMAP3 module")
	.boot_params	= 0x80000100,
	.map_io		= omap3_map_io,
	.init_irq	= igep3_init_irq,
	.init_machine	= igep3_init,
	.timer		= &omap_timer,
MACHINE_END
