/*
 * arch/arm/mach-shmobile/board-ag5evm.c
 *
 * Copyright (C) 2010  Takashi Yoshii <yoshii.takashi.zj@renesas.com>
 * Copyright (C) 2009  Yoshihiro Shimoda <shimoda.yoshihiro@renesas.com>
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
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/serial_sci.h>
#include <linux/smsc911x.h>
#include <linux/gpio.h>
#include <mach/hardware.h>
#include <mach/sh73a0.h>
#include <mach/common.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/hardware/gic.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/traps.h>

static struct resource smsc9220_resources[] = {
	[0] = {
		.start		= 0x14000000,
		.end		= 0x14000000 + SZ_64K - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		.start		= gic_spi(33), /* PINT1 */
		.flags		= IORESOURCE_IRQ,
	},
};

static struct smsc911x_platform_config smsc9220_platdata = {
	.flags		= SMSC911X_USE_32BIT | SMSC911X_SAVE_MAC_ADDRESS,
	.phy_interface	= PHY_INTERFACE_MODE_MII,
	.irq_polarity	= SMSC911X_IRQ_POLARITY_ACTIVE_LOW,
	.irq_type	= SMSC911X_IRQ_TYPE_PUSH_PULL,
};

static struct platform_device eth_device = {
	.name		= "smsc911x",
	.id		= 0,
	.dev  = {
		.platform_data = &smsc9220_platdata,
	},
	.resource	= smsc9220_resources,
	.num_resources	= ARRAY_SIZE(smsc9220_resources),
};

static struct platform_device *ag5evm_devices[] __initdata = {
	&eth_device,
};

static struct map_desc ag5evm_io_desc[] __initdata = {
	/* create a 1:1 entity map for 0xe6xxxxxx
	 * used by CPGA, INTC and PFC.
	 */
	{
		.virtual	= 0xe6000000,
		.pfn		= __phys_to_pfn(0xe6000000),
		.length		= 256 << 20,
		.type		= MT_DEVICE_NONSHARED
	},
};

static void __init ag5evm_map_io(void)
{
	iotable_init(ag5evm_io_desc, ARRAY_SIZE(ag5evm_io_desc));

	/* setup early devices and console here as well */
	sh73a0_add_early_devices();
	shmobile_setup_console();
}

#define PINTC_ADDR	0xe6900000
#define PINTER0A	(PINTC_ADDR + 0xa0)
#define PINTCR0A	(PINTC_ADDR + 0xb0)

void __init ag5evm_init_irq(void)
{
	/* setup PINT: enable PINTA2 as active low */
	__raw_writel(__raw_readl(PINTER0A) | (1<<29), PINTER0A);
	__raw_writew(__raw_readw(PINTCR0A) | (2<<10), PINTCR0A);

	gic_dist_init(0, __io(0xf0001000), 29);
	gic_cpu_init(0, __io(0xf0000100));
}

#define PORT144CR 0xe6052090
#define PORT145CR 0xe6052091

#define PORT154CR 0xe605209a
#define PORT155CR 0xe605209b
#define PORT156CR 0xe605209c
#define PORT157CR 0xe605209d

#define PORTR159_128DR 0xe6056004

static void __init ag5evm_init(void)
{
	sh73a0_pinmux_init();

	/* enable SCIFA2 */
	gpio_request(GPIO_FN_SCIFA2_TXD1, NULL);
	gpio_request(GPIO_FN_SCIFA2_RXD1, NULL);
	gpio_request(GPIO_FN_SCIFA2_RTS1_, NULL);
	gpio_request(GPIO_FN_SCIFA2_CTS1_, NULL);

	/* enable SMSC911X */
	gpio_request(GPIO_PORT144, NULL); /* PINTA2 */
	gpio_direction_input(GPIO_PORT144);
	gpio_request(GPIO_PORT145, NULL); /* RESET */
	gpio_direction_output(GPIO_PORT145, 1);

#ifdef CONFIG_CACHE_L2X0
	/* Shared attribute override enable, 64K*8way */
	l2x0_init(__io(0xf0100000), 0x00460000, 0xc2000fff);
#endif
	sh73a0_add_standard_devices();
	platform_add_devices(ag5evm_devices, ARRAY_SIZE(ag5evm_devices));
}

static void __init ag5evm_timer_init(void)
{
	sh73a0_clock_init();
	shmobile_timer.init();
	return;
}

struct sys_timer ag5evm_timer = {
	.init	= ag5evm_timer_init,
};

MACHINE_START(AG5EVM, "ag5evm")
	.map_io		= ag5evm_map_io,
	.init_irq	= ag5evm_init_irq,
	.init_machine	= ag5evm_init,
	.timer		= &ag5evm_timer,
MACHINE_END
