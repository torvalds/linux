/*
 * APE6EVM board support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/tmio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mmcif.h>
#include <linux/mmc/sh_mobile_sdhi.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_device.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
#include <linux/sh_clk.h>
#include <linux/smsc911x.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a73a4.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

/* LEDS */
static struct gpio_led ape6evm_leds[] = {
	{
		.name		= "gnss-en",
		.gpio		= 28,
		.default_state	= LEDS_GPIO_DEFSTATE_OFF,
	}, {
		.name		= "nfc-nrst",
		.gpio		= 126,
		.default_state	= LEDS_GPIO_DEFSTATE_OFF,
	}, {
		.name		= "gnss-nrst",
		.gpio		= 132,
		.default_state	= LEDS_GPIO_DEFSTATE_OFF,
	}, {
		.name		= "bt-wakeup",
		.gpio		= 232,
		.default_state	= LEDS_GPIO_DEFSTATE_OFF,
	}, {
		.name		= "strobe",
		.gpio		= 250,
		.default_state	= LEDS_GPIO_DEFSTATE_OFF,
	}, {
		.name		= "bbresetout",
		.gpio		= 288,
		.default_state	= LEDS_GPIO_DEFSTATE_OFF,
	},
};

static __initdata struct gpio_led_platform_data ape6evm_leds_pdata = {
	.leds		= ape6evm_leds,
	.num_leds	= ARRAY_SIZE(ape6evm_leds),
};

/* GPIO KEY */
#define GPIO_KEY(c, g, d, ...) \
	{ .code = c, .gpio = g, .desc = d, .active_low = 1 }

static struct gpio_keys_button gpio_buttons[] = {
	GPIO_KEY(KEY_0,			324,	"S16"),
	GPIO_KEY(KEY_MENU,		325,	"S17"),
	GPIO_KEY(KEY_HOME,		326,	"S18"),
	GPIO_KEY(KEY_BACK,		327,	"S19"),
	GPIO_KEY(KEY_VOLUMEUP,		328,	"S20"),
	GPIO_KEY(KEY_VOLUMEDOWN,	329,	"S21"),
};

static struct gpio_keys_platform_data ape6evm_keys_pdata __initdata = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

/* Dummy supplies, where voltage doesn't matter */
static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x"),
};

/* SMSC LAN9220 */
static const struct resource lan9220_res[] __initconst = {
	DEFINE_RES_MEM(0x08000000, 0x1000),
	{
		.start	= irq_pin(40), /* IRQ40 */
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_HIGH,
	},
};

static const struct smsc911x_platform_config lan9220_data __initconst = {
	.flags		= SMSC911X_USE_32BIT,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_HIGH,
};

/*
 * On APE6EVM power is supplied to MMCIF by a tps80032 regulator. For now we
 * model a VDD supply to MMCIF, using a fixed 3.3V regulator. Also use the
 * static power supply for SDHI0 and SDHI1, whereas SDHI0's VccQ is also
 * supplied by the same tps80032 regulator and thus can also be adjusted
 * dynamically.
 */
static struct regulator_consumer_supply fixed3v3_power_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mmcif.0"),
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.0"),
	REGULATOR_SUPPLY("vmmc", "sh_mobile_sdhi.1"),
};

/* MMCIF */
static const struct sh_mmcif_plat_data mmcif0_pdata __initconst = {
	.caps		= MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE,
};

static const struct resource mmcif0_resources[] __initconst = {
	DEFINE_RES_MEM_NAMED(0xee200000, 0x100, "MMCIF0"),
	DEFINE_RES_IRQ(gic_spi(169)),
};

/* SDHI0 */
static const struct sh_mobile_sdhi_info sdhi0_pdata __initconst = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_WRPROTECT_DISABLE,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ,
};

static const struct resource sdhi0_resources[] __initconst = {
	DEFINE_RES_MEM_NAMED(0xee100000, 0x100, "SDHI0"),
	DEFINE_RES_IRQ(gic_spi(165)),
};

/* SDHI1 */
static const struct sh_mobile_sdhi_info sdhi1_pdata __initconst = {
	.tmio_flags	= TMIO_MMC_HAS_IDLE_WAIT | TMIO_MMC_WRPROTECT_DISABLE,
	.tmio_caps	= MMC_CAP_SD_HIGHSPEED | MMC_CAP_SDIO_IRQ |
			  MMC_CAP_NEEDS_POLL,
};

static const struct resource sdhi1_resources[] __initconst = {
	DEFINE_RES_MEM_NAMED(0xee120000, 0x100, "SDHI1"),
	DEFINE_RES_IRQ(gic_spi(166)),
};

static const struct pinctrl_map ape6evm_pinctrl_map[] __initconst = {
	/* SCIFA0 console */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.0", "pfc-r8a73a4",
				  "scifa0_data", "scifa0"),
	/* SMSC */
	PIN_MAP_MUX_GROUP_DEFAULT("smsc911x", "pfc-r8a73a4",
				  "irqc_irq40", "irqc"),
	/* MMCIF0 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.0", "pfc-r8a73a4",
				  "mmc0_data8", "mmc0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mmcif.0", "pfc-r8a73a4",
				  "mmc0_ctrl", "mmc0"),
	/* SDHI0: uSD: no WP */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a73a4",
				  "sdhi0_data4", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a73a4",
				  "sdhi0_ctrl", "sdhi0"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.0", "pfc-r8a73a4",
				  "sdhi0_cd", "sdhi0"),
	/* SDHI1 */
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-r8a73a4",
				  "sdhi1_data4", "sdhi1"),
	PIN_MAP_MUX_GROUP_DEFAULT("sh_mobile_sdhi.1", "pfc-r8a73a4",
				  "sdhi1_ctrl", "sdhi1"),
};

static void __init ape6evm_add_standard_devices(void)
{

	struct clk *parent;
	struct clk *mp;

	r8a73a4_clock_init();

	/* MP clock parent = extal2 */
	parent      = clk_get(NULL, "extal2");
	mp          = clk_get(NULL, "mp");
	BUG_ON(IS_ERR(parent) || IS_ERR(mp));

	clk_set_parent(mp, parent);
	clk_put(parent);
	clk_put(mp);

	pinctrl_register_mappings(ape6evm_pinctrl_map,
				  ARRAY_SIZE(ape6evm_pinctrl_map));
	r8a73a4_pinmux_init();
	r8a73a4_add_standard_devices();

	/* LAN9220 ethernet */
	gpio_request_one(270, GPIOF_OUT_INIT_HIGH, NULL); /* smsc9220 RESET */

	regulator_register_fixed(0, dummy_supplies, ARRAY_SIZE(dummy_supplies));

	platform_device_register_resndata(&platform_bus, "smsc911x", -1,
					  lan9220_res, ARRAY_SIZE(lan9220_res),
					  &lan9220_data, sizeof(lan9220_data));
	regulator_register_always_on(1, "fixed-3.3V", fixed3v3_power_consumers,
				     ARRAY_SIZE(fixed3v3_power_consumers), 3300000);
	platform_device_register_resndata(&platform_bus, "sh_mmcif", 0,
					  mmcif0_resources, ARRAY_SIZE(mmcif0_resources),
					  &mmcif0_pdata, sizeof(mmcif0_pdata));
	platform_device_register_resndata(&platform_bus, "sh_mobile_sdhi", 0,
					  sdhi0_resources, ARRAY_SIZE(sdhi0_resources),
					  &sdhi0_pdata, sizeof(sdhi0_pdata));
	platform_device_register_resndata(&platform_bus, "sh_mobile_sdhi", 1,
					  sdhi1_resources, ARRAY_SIZE(sdhi1_resources),
					  &sdhi1_pdata, sizeof(sdhi1_pdata));
	platform_device_register_data(&platform_bus, "gpio-keys", -1,
				      &ape6evm_keys_pdata,
				      sizeof(ape6evm_keys_pdata));
	platform_device_register_data(&platform_bus, "leds-gpio", -1,
				      &ape6evm_leds_pdata,
				      sizeof(ape6evm_leds_pdata));
}

static const char *ape6evm_boards_compat_dt[] __initdata = {
	"renesas,ape6evm",
	NULL,
};

DT_MACHINE_START(APE6EVM_DT, "ape6evm")
	.init_early	= r8a73a4_init_delay,
	.init_machine	= ape6evm_add_standard_devices,
	.dt_compat	= ape6evm_boards_compat_dt,
MACHINE_END
