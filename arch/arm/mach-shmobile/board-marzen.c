/*
 * marzen board support
 *
 * Copyright (C) 2011, 2013  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 * Copyright (C) 2013  Cogent Embedded, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/dma-mapping.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_data/camera-rcar.h>
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_data/usb-rcar-phy.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/smsc911x.h>
#include <linux/spi/spi.h>
#include <linux/spi/sh_hspi.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/mfd/tmio.h>

#include <media/soc_camera.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/traps.h>

#include "common.h"
#include "irqs.h"
#include "r8a7779.h"

/* Fixed 3.3V regulator to be used by SDHI0 */
static struct regulator_consumer_supply fixed3v3_power_consumers[] = {
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.0"),
	REGULATOR_SUPPLY("vqmmc", "sh_mobile_sdhi.0"),
};

/* Dummy supplies, where voltage doesn't matter */
static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x"),
};

/* USB PHY */
static struct resource usb_phy_resources[] = {
	[0] = {
		.start		= 0xffe70800,
		.end		= 0xffe70900 - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct rcar_phy_platform_data usb_phy_platform_data;

static struct platform_device usb_phy = {
	.name		= "rcar_usb_phy",
	.id		= -1,
	.dev  = {
		.platform_data = &usb_phy_platform_data,
	},
	.resource	= usb_phy_resources,
	.num_resources	= ARRAY_SIZE(usb_phy_resources),
};

/* SMSC LAN89218 */
static struct resource smsc911x_resources[] = {
	[0] = {
		.start		= 0x18000000, /* ExCS0 */
		.end		= 0x180000ff, /* A1->A7 */
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= irq_pin(1), /* IRQ 1 */
		.flags		= IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config smsc911x_platdata = {
	.flags		= SMSC911X_USE_32BIT, /* 32-bit SW on 16-bit HW bus */
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
};

static struct platform_device eth_device = {
	.name		= "smsc911x",
	.id		= -1,
	.dev  = {
		.platform_data = &smsc911x_platdata,
	},
	.resource	= smsc911x_resources,
	.num_resources	= ARRAY_SIZE(smsc911x_resources),
};

static struct resource sdhi0_resources[] = {
	[0] = {
		.name	= "sdhi0",
		.start	= 0xffe4c000,
		.end	= 0xffe4c0ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= gic_iid(0x88),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct sh_mobile_sdhi_info sdhi0_platform_data = {
	.dma_slave_tx = HPBDMA_SLAVE_SDHI0_TX,
	.dma_slave_rx = HPBDMA_SLAVE_SDHI0_RX,
	.tmio_flags = TMIO_MMC_WRPROTECT_DISABLE | TMIO_MMC_HAS_IDLE_WAIT,
	.tmio_caps = MMC_CAP_SD_HIGHSPEED,
};

static struct platform_device sdhi0_device = {
	.name = "sh_mobile_sdhi",
	.num_resources = ARRAY_SIZE(sdhi0_resources),
	.resource = sdhi0_resources,
	.id = 0,
	.dev = {
		.platform_data = &sdhi0_platform_data,
	}
};

/* Thermal */
static struct resource thermal_resources[] = {
	[0] = {
		.start		= 0xFFC48000,
		.end		= 0xFFC48038 - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device thermal_device = {
	.name		= "rcar_thermal",
	.resource	= thermal_resources,
	.num_resources	= ARRAY_SIZE(thermal_resources),
};

/* HSPI */
static struct resource hspi_resources[] = {
	[0] = {
		.start		= 0xFFFC7000,
		.end		= 0xFFFC7018 - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device hspi_device = {
	.name	= "sh-hspi",
	.id	= 0,
	.resource	= hspi_resources,
	.num_resources	= ARRAY_SIZE(hspi_resources),
};

/* LEDS */
static struct gpio_led marzen_leds[] = {
	{
		.name		= "led2",
		.gpio		= RCAR_GP_PIN(4, 29),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "led3",
		.gpio		= RCAR_GP_PIN(4, 30),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	}, {
		.name		= "led4",
		.gpio		= RCAR_GP_PIN(4, 31),
		.default_state	= LEDS_GPIO_DEFSTATE_ON,
	},
};

static struct gpio_led_platform_data marzen_leds_pdata = {
	.leds		= marzen_leds,
	.num_leds	= ARRAY_SIZE(marzen_leds),
};

static struct platform_device leds_device = {
	.name	= "leds-gpio",
	.id	= 0,
	.dev	= {
		.platform_data  = &marzen_leds_pdata,
	},
};

/* VIN */
static struct rcar_vin_platform_data vin_platform_data __initdata = {
	.flags	= RCAR_VIN_BT656,
};

#define MARZEN_VIN(idx)						\
static struct resource vin##idx##_resources[] __initdata = {	\
	DEFINE_RES_MEM(0xffc50000 + 0x1000 * (idx), 0x1000),	\
	DEFINE_RES_IRQ(gic_iid(0x5f + (idx))),			\
};								\
								\
static struct platform_device_info vin##idx##_info __initdata = { \
	.name		= "r8a7779-vin",			\
	.id		= idx,					\
	.res		= vin##idx##_resources,			\
	.num_res	= ARRAY_SIZE(vin##idx##_resources),	\
	.dma_mask	= DMA_BIT_MASK(32),			\
	.data		= &vin_platform_data,			\
	.size_data	= sizeof(vin_platform_data),		\
}
MARZEN_VIN(1);
MARZEN_VIN(3);

#define MARZEN_CAMERA(idx)					\
static struct i2c_board_info camera##idx##_info = {		\
	I2C_BOARD_INFO("adv7180", 0x20 + (idx)),		\
};								\
								\
static struct soc_camera_link iclink##idx##_adv7180 = {		\
	.bus_id		= 1 + 2 * (idx),			\
	.i2c_adapter_id	= 0,					\
	.board_info	= &camera##idx##_info,			\
};								\
								\
static struct platform_device camera##idx##_device = {		\
	.name	= "soc-camera-pdrv",				\
	.id	= idx,						\
	.dev	= {						\
		.platform_data	= &iclink##idx##_adv7180,	\
	},							\
};

MARZEN_CAMERA(0);
MARZEN_CAMERA(1);

static struct platform_device *marzen_devices[] __initdata = {
	&eth_device,
	&sdhi0_device,
	&thermal_device,
	&hspi_device,
	&leds_device,
	&usb_phy,
	&camera0_device,
	&camera1_device,
};

static const struct pinctrl_map marzen_pinctrl_map[] = {
	/* DU (CN10: ARGB0, CN13: LVDS) */
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7779", "pfc-r8a7779",
				  "du0_rgb888", "du0"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7779", "pfc-r8a7779",
				  "du0_sync_1", "du0"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7779", "pfc-r8a7779",
				  "du0_clk_out_0", "du0"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7779", "pfc-r8a7779",
				  "du1_rgb666", "du1"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7779", "pfc-r8a7779",
				  "du1_sync_1", "du1"),
	PIN_MAP_MUX_GROUP_DEFAULT("rcar-du-r8a7779", "pfc-r8a7779",
				  "du1_clk_out", "du1"),
	/* HSPI0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-hspi.0", "pfc-r8a7779",
				  "hspi0", "hspi0"),
	/* SCIF2 (CN18: DEBUG0) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.2", "pfc-r8a7779",
				  "scif2_data_c", "scif2"),
	/* SCIF4 (CN19: DEBUG1) */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.4", "pfc-r8a7779",
				  "scif4_data", "scif4"),
	/* SDHI0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7779",
				  "sdhi0_data4", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7779",
				  "sdhi0_ctrl", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a7779",
				  "sdhi0_cd", "sdhi0"),
	/* SMSC */
	PIN_MAP_MUX_GROUP_DEFAULT("smsc911x", "pfc-r8a7779",
				  "intc_irq1_b", "intc"),
	PIN_MAP_MUX_GROUP_DEFAULT("smsc911x", "pfc-r8a7779",
				  "lbsc_ex_cs0", "lbsc"),
	/* USB0 */
	PIN_MAP_MUX_GROUP_DEFAULT("ehci-platform.0", "pfc-r8a7779",
				  "usb0", "usb0"),
	/* USB1 */
	PIN_MAP_MUX_GROUP_DEFAULT("ehci-platform.0", "pfc-r8a7779",
				  "usb1", "usb1"),
	/* USB2 */
	PIN_MAP_MUX_GROUP_DEFAULT("ehci-platform.1", "pfc-r8a7779",
				  "usb2", "usb2"),
	/* VIN1 */
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7779-vin.1", "pfc-r8a7779",
				  "vin1_clk", "vin1"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7779-vin.1", "pfc-r8a7779",
				  "vin1_data8", "vin1"),
	/* VIN3 */
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7779-vin.3", "pfc-r8a7779",
				  "vin3_clk", "vin3"),
	PIN_MAP_MUX_GROUP_DEFAULT("r8a7779-vin.3", "pfc-r8a7779",
				  "vin3_data8", "vin3"),
};

static void __init marzen_init(void)
{
	regulator_register_always_on(0, "fixed-3.3V", fixed3v3_power_consumers,
				ARRAY_SIZE(fixed3v3_power_consumers), 3300000);
	regulator_register_fixed(1, dummy_supplies,
				ARRAY_SIZE(dummy_supplies));

	pinctrl_register_mappings(marzen_pinctrl_map,
				  ARRAY_SIZE(marzen_pinctrl_map));
	r8a7779_pinmux_init();
	r8a7779_init_irq_extpin(1); /* IRQ1 as individual interrupt */

	r8a7779_add_standard_devices();
	platform_device_register_full(&vin1_info);
	platform_device_register_full(&vin3_info);
	platform_add_devices(marzen_devices, ARRAY_SIZE(marzen_devices));
}

static const char *marzen_boards_compat_dt[] __initdata = {
        "renesas,marzen",
        NULL,
};

DT_MACHINE_START(MARZEN, "marzen")
	.smp		= smp_ops(r8a7779_smp_ops),
	.map_io		= r8a7779_map_io,
	.init_early	= r8a7779_add_early_devices,
	.init_irq	= r8a7779_init_irq_dt,
	.init_machine	= marzen_init,
	.init_late	= r8a7779_init_late,
	.dt_compat	= marzen_boards_compat_dt,
	.init_time	= r8a7779_earlytimer_init,
MACHINE_END
