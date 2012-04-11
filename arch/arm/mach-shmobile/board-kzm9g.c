/*
 * KZM-A9-GT board support
 *
 * Copyright (C) 2012	Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
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
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/smsc911x.h>
#include <mach/irqs.h>
#include <mach/sh73a0.h>
#include <mach/common.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/hardware/gic.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>

/* SMSC 9221 */
static struct resource smsc9221_resources[] = {
	[0] = {
		.start	= 0x10000000, /* CS4 */
		.end	= 0x100000ff,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= intcs_evt2irq(0x260), /* IRQ3 */
		.flags	= IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config smsc9221_platdata = {
	.flags		= SMSC911X_USE_32BIT | SMSC911X_SAVE_MAC_ADDRESS,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
};

static struct platform_device smsc_device = {
	.name		= "smsc911x",
	.dev  = {
		.platform_data = &smsc9221_platdata,
	},
	.resource	= smsc9221_resources,
	.num_resources	= ARRAY_SIZE(smsc9221_resources),
};

static struct platform_device *kzm_devices[] __initdata = {
	&smsc_device,
};

static void __init kzm_init(void)
{
	sh73a0_pinmux_init();

	/* enable SCIFA4 */
	gpio_request(GPIO_FN_SCIFA4_TXD, NULL);
	gpio_request(GPIO_FN_SCIFA4_RXD, NULL);
	gpio_request(GPIO_FN_SCIFA4_RTS_, NULL);
	gpio_request(GPIO_FN_SCIFA4_CTS_, NULL);

	/* CS4 for SMSC/USB */
	gpio_request(GPIO_FN_CS4_, NULL); /* CS4 */

	/* SMSC */
	gpio_request(GPIO_PORT224, NULL); /* IRQ3 */
	gpio_direction_input(GPIO_PORT224);

#ifdef CONFIG_CACHE_L2X0
	/* Early BRESP enable, Shared attribute override enable, 64K*8way */
	l2x0_init(IOMEM(0xf0100000), 0x40460000, 0x82000fff);
#endif

	sh73a0_add_standard_devices();
	platform_add_devices(kzm_devices, ARRAY_SIZE(kzm_devices));
}

MACHINE_START(KZM9G, "kzm9g")
	.map_io		= sh73a0_map_io,
	.init_early	= sh73a0_add_early_devices,
	.nr_irqs	= NR_IRQS_LEGACY,
	.init_irq	= sh73a0_init_irq,
	.handle_irq	= gic_handle_irq,
	.init_machine	= kzm_init,
	.timer		= &shmobile_timer,
MACHINE_END
