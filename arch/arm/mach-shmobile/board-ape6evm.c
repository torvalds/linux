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
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sh_mmcif.h>
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

/* Dummy supplies, where voltage doesn't matter */
static struct regulator_consumer_supply dummy_supplies[] = {
	REGULATOR_SUPPLY("vddvario", "smsc911x"),
	REGULATOR_SUPPLY("vdd33a", "smsc911x"),
};

/* SMSC LAN9220 */
static const struct resource lan9220_res[] = {
	DEFINE_RES_MEM(0x08000000, 0x1000),
	{
		.start	= irq_pin(40), /* IRQ40 */
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_HIGH,
	},
};

static const struct smsc911x_platform_config lan9220_data = {
	.flags		= SMSC911X_USE_32BIT,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_HIGH,
};

/*
 * On APE6EVM power is supplied to MMCIF by a tps80032 regulator. For now we
 * model a VDD supply to MMCIF, using a fixed 3.3V regulator.
 */
static struct regulator_consumer_supply fixed3v3_power_consumers[] =
{
	REGULATOR_SUPPLY("vmmc", "sh_mmcif.0"),
};

/* MMCIF */
static struct sh_mmcif_plat_data mmcif0_pdata = {
	.caps		= MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE,
};

static struct resource mmcif0_resources[] = {
	DEFINE_RES_MEM_NAMED(0xee200000, 0x100, "MMCIF0"),
	DEFINE_RES_IRQ(gic_spi(169)),
};

static const struct pinctrl_map ape6evm_pinctrl_map[] = {
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
}

static const char *ape6evm_boards_compat_dt[] __initdata = {
	"renesas,ape6evm",
	NULL,
};

DT_MACHINE_START(APE6EVM_DT, "ape6evm")
	.init_early	= r8a73a4_init_delay,
	.init_time	= shmobile_timer_init,
	.init_machine	= ape6evm_add_standard_devices,
	.dt_compat	= ape6evm_boards_compat_dt,
MACHINE_END
