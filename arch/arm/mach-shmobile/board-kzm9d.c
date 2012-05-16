/*
 * kzm9d board support
 *
 * Copyright (C) 2012  Renesas Solutions Corp.
 * Copyright (C) 2012  Magnus Damm
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

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/smsc911x.h>
#include <mach/common.h>
#include <mach/emev2.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/hardware/gic.h>

/* Ether */
static struct resource smsc911x_resources[] = {
	[0] = {
		.start	= 0x20000000,
		.end	= 0x2000ffff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= EMEV2_GPIO_IRQ(1),
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_HIGH,
	},
};

static struct smsc911x_platform_config smsc911x_platdata = {
	.flags		= SMSC911X_USE_32BIT,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_HIGH,
};

static struct platform_device smsc91x_device = {
	.name	= "smsc911x",
	.id	= 0,
	.dev	= {
		  .platform_data = &smsc911x_platdata,
		},
	.num_resources	= ARRAY_SIZE(smsc911x_resources),
	.resource	= smsc911x_resources,
};

static struct platform_device *kzm9d_devices[] __initdata = {
	&smsc91x_device,
};

void __init kzm9d_add_standard_devices(void)
{
	emev2_add_standard_devices();

	platform_add_devices(kzm9d_devices, ARRAY_SIZE(kzm9d_devices));
}

MACHINE_START(KZM9D, "kzm9d")
	.map_io		= emev2_map_io,
	.init_early	= emev2_add_early_devices,
	.nr_irqs	= NR_IRQS_LEGACY,
	.init_irq	= emev2_init_irq,
	.handle_irq	= gic_handle_irq,
	.init_machine	= kzm9d_add_standard_devices,
	.timer		= &shmobile_timer,
MACHINE_END
