/*
 * Copyright STMicroelectronics, 2007.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/gpio.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <asm/mach/map.h>
#include <asm/hardware/vic.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>

/* The 8815 has 4 GPIO blocks, let's register them immediately */
static struct nmk_gpio_platform_data cpu8815_gpio[] = {
	{
		.name = "GPIO-0-31",
		.first_gpio = 0,
		.first_irq = NOMADIK_GPIO_TO_IRQ(0),
		.parent_irq = IRQ_GPIO0,
	}, {
		.name = "GPIO-32-63",
		.first_gpio = 32,
		.first_irq = NOMADIK_GPIO_TO_IRQ(32),
		.parent_irq = IRQ_GPIO1,
	}, {
		.name = "GPIO-64-95",
		.first_gpio = 64,
		.first_irq = NOMADIK_GPIO_TO_IRQ(64),
		.parent_irq = IRQ_GPIO2,
	}, {
		.name = "GPIO-96-127", /* 124..127 not routed to pin */
		.first_gpio = 96,
		.first_irq = NOMADIK_GPIO_TO_IRQ(96),
		.parent_irq = IRQ_GPIO3,
	}
};

#define __MEM_4K_RESOURCE(x) \
	.res = {.start = (x), .end = (x) + SZ_4K - 1, .flags = IORESOURCE_MEM}

static struct amba_device cpu8815_amba_gpio[] = {
	{
		.dev = {
			.init_name = "gpio0",
			.platform_data = cpu8815_gpio + 0,
		},
		__MEM_4K_RESOURCE(NOMADIK_GPIO0_BASE),
	}, {
		.dev = {
			.init_name = "gpio1",
			.platform_data = cpu8815_gpio + 1,
		},
		__MEM_4K_RESOURCE(NOMADIK_GPIO1_BASE),
	}, {
		.dev = {
			.init_name = "gpio2",
			.platform_data = cpu8815_gpio + 2,
		},
		__MEM_4K_RESOURCE(NOMADIK_GPIO2_BASE),
	}, {
		.dev = {
			.init_name = "gpio3",
			.platform_data = cpu8815_gpio + 3,
		},
		__MEM_4K_RESOURCE(NOMADIK_GPIO3_BASE),
	},
};

static struct amba_device cpu8815_amba_rng = {
	.dev = {
		.init_name = "rng",
	},
	__MEM_4K_RESOURCE(NOMADIK_RNG_BASE),
};

static struct amba_device *amba_devs[] __initdata = {
	cpu8815_amba_gpio + 0,
	cpu8815_amba_gpio + 1,
	cpu8815_amba_gpio + 2,
	cpu8815_amba_gpio + 3,
	&cpu8815_amba_rng
};

static int __init cpu8815_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(amba_devs); i++)
		amba_device_register(amba_devs[i], &iomem_resource);
	return 0;
}
arch_initcall(cpu8815_init);

/* All SoC devices live in the same area (see hardware.h) */
static struct map_desc nomadik_io_desc[] __initdata = {
	{
		.virtual =	NOMADIK_IO_VIRTUAL,
		.pfn =		__phys_to_pfn(NOMADIK_IO_PHYSICAL),
		.length =	NOMADIK_IO_SIZE,
		.type = 	MT_DEVICE,
	}
	/* static ram and secured ram may be added later */
};

void __init cpu8815_map_io(void)
{
	iotable_init(nomadik_io_desc, ARRAY_SIZE(nomadik_io_desc));
}

void __init cpu8815_init_irq(void)
{
	/* This modified VIC cell has two register blocks, at 0 and 0x20 */
	vic_init(io_p2v(NOMADIK_IC_BASE + 0x00), IRQ_VIC_START +  0, ~0, 0);
	vic_init(io_p2v(NOMADIK_IC_BASE + 0x20), IRQ_VIC_START + 32, ~0, 0);
}

/*
 * This function is called from the board init ("init_machine").
 */
 void __init cpu8815_platform_init(void)
{
#ifdef CONFIG_CACHE_L2X0
	/* At full speed latency must be >=2, so 0x249 in low bits */
	l2x0_init(io_p2v(NOMADIK_L2CC_BASE), 0x00730249, 0xfe000fff);
#endif
	 return;
}
