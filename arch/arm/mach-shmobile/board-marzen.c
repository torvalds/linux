/*
 * marzen board support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
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
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/dma-mapping.h>
#include <linux/smsc911x.h>
#include <mach/hardware.h>
#include <mach/r8a7779.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/hardware/gic.h>
#include <asm/traps.h>

/* SMSC LAN89218 */
static struct resource smsc911x_resources[] = {
	[0] = {
		.start		= 0x18000000, /* ExCS0 */
		.end		= 0x180000ff, /* A1->A7 */
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= gic_spi(28), /* IRQ 1 */
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
	.id		= 0,
	.dev  = {
		.platform_data = &smsc911x_platdata,
	},
	.resource	= smsc911x_resources,
	.num_resources	= ARRAY_SIZE(smsc911x_resources),
};

static struct platform_device *marzen_devices[] __initdata = {
	&eth_device,
};

static struct map_desc marzen_io_desc[] __initdata = {
	/* 2M entity map for 0xf0000000 (MPCORE) */
	{
		.virtual	= 0xf0000000,
		.pfn		= __phys_to_pfn(0xf0000000),
		.length		= SZ_2M,
		.type		= MT_DEVICE_NONSHARED
	},
	/* 16M entity map for 0xfexxxxxx (DMAC-S/HPBREG/INTC2/LRAM/DBSC) */
	{
		.virtual	= 0xfe000000,
		.pfn		= __phys_to_pfn(0xfe000000),
		.length		= SZ_16M,
		.type		= MT_DEVICE_NONSHARED
	},
};

static void __init marzen_map_io(void)
{
	iotable_init(marzen_io_desc, ARRAY_SIZE(marzen_io_desc));
}

static void __init marzen_init_early(void)
{
	r8a7779_add_early_devices();

	/* Early serial console setup is not included here due to
	 * memory map collisions. The SCIF serial ports in r8a7779
	 * are difficult to entity map 1:1 due to collision with the
	 * virtual memory range used by the coherent DMA code on ARM.
	 *
	 * Anyone wanting to debug early can remove UPF_IOREMAP from
	 * the sh-sci serial console platform data, adjust mapbase
	 * to a static M:N virt:phys mapping that needs to be added to
	 * the mappings passed with iotable_init() above.
	 *
	 * Then add a call to shmobile_setup_console() from this function.
	 *
	 * As a final step pass earlyprint=sh-sci.2,115200 on the kernel
	 * command line.
	 */
}

static void __init marzen_init(void)
{
	r8a7779_pinmux_init();

	/* SCIF2 (CN18: DEBUG0) */
	gpio_request(GPIO_FN_TX2_C, NULL);
	gpio_request(GPIO_FN_RX2_C, NULL);

	/* SCIF4 (CN19: DEBUG1) */
	gpio_request(GPIO_FN_TX4, NULL);
	gpio_request(GPIO_FN_RX4, NULL);

	/* LAN89218 */
	gpio_request(GPIO_FN_EX_CS0, NULL); /* nCS */
	gpio_request(GPIO_FN_IRQ1_B, NULL); /* IRQ + PME */

	r8a7779_add_standard_devices();
	platform_add_devices(marzen_devices, ARRAY_SIZE(marzen_devices));
}

static void __init marzen_timer_init(void)
{
	r8a7779_clock_init();
	shmobile_timer.init();
	return;
}

struct sys_timer marzen_timer = {
	.init	= marzen_timer_init,
};

MACHINE_START(MARZEN, "marzen")
	.map_io		= marzen_map_io,
	.init_early	= marzen_init_early,
	.nr_irqs	= NR_IRQS_LEGACY,
	.init_irq	= r8a7779_init_irq,
	.handle_irq	= gic_handle_irq,
	.init_machine	= marzen_init,
	.timer		= &marzen_timer,
MACHINE_END
