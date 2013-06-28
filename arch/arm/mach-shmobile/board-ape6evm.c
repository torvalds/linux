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
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/pinctrl/machine.h>
#include <linux/platform_device.h>
#include <linux/regulator/fixed.h>
#include <linux/regulator/machine.h>
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

static const struct pinctrl_map ape6evm_pinctrl_map[] = {
	/* SCIFA0 console */
	PIN_MAP_MUX_GROUP_DEFAULT("sh-sci.0", "pfc-r8a73a4",
				  "scifa0_data", "scifa0"),
	/* SMSC */
	PIN_MAP_MUX_GROUP_DEFAULT("smsc911x", "pfc-r8a73a4",
				  "irqc_irq40", "irqc"),
};

static void __init ape6evm_add_standard_devices(void)
{
	r8a73a4_clock_init();
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
}

static const char *ape6evm_boards_compat_dt[] __initdata = {
	"renesas,ape6evm",
	NULL,
};

DT_MACHINE_START(APE6EVM_DT, "ape6evm")
	.init_irq	= irqchip_init,
	.init_time	= shmobile_timer_init,
	.init_machine	= ape6evm_add_standard_devices,
	.dt_compat	= ape6evm_boards_compat_dt,
MACHINE_END
