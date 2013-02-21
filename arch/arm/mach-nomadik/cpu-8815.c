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
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/dma-mapping.h>
#include <linux/irqchip/arm-vic.h>
#include <linux/platform_data/clk-nomadik.h>
#include <linux/platform_data/pinctrl-nomadik.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
#include <asm/mach/map.h>

#include <asm/cacheflush.h>
#include <asm/hardware/cache-l2x0.h>

#include "cpu-8815.h"

/* The 8815 has 4 GPIO blocks, let's register them immediately */
static resource_size_t __initdata cpu8815_gpio_base[] = {
	NOMADIK_GPIO0_BASE,
	NOMADIK_GPIO1_BASE,
	NOMADIK_GPIO2_BASE,
	NOMADIK_GPIO3_BASE,
};

static struct platform_device *
cpu8815_add_gpio(int id, resource_size_t addr, int irq,
		 struct nmk_gpio_platform_data *pdata)
{
	struct resource resources[] = {
		{
			.start	= addr,
			.end	= addr + 127,
			.flags	= IORESOURCE_MEM,
		},
		{
			.start	= irq,
			.end	= irq,
			.flags	= IORESOURCE_IRQ,
		}
	};

	return platform_device_register_resndata(NULL, "gpio", id,
				resources, ARRAY_SIZE(resources),
				pdata, sizeof(*pdata));
}

void cpu8815_add_gpios(resource_size_t *base, int num, int irq,
		       struct nmk_gpio_platform_data *pdata)
{
	int first = 0;
	int i;

	for (i = 0; i < num; i++, first += 32, irq++) {
		pdata->first_gpio = first;
		pdata->first_irq = NOMADIK_GPIO_TO_IRQ(first);
		pdata->num_gpio = 32;

		cpu8815_add_gpio(i, base[i], irq, pdata);
	}
}

static inline void
cpu8815_add_pinctrl(struct device *parent, const char *name)
{
	struct platform_device_info pdevinfo = {
		.parent = parent,
		.name = name,
		.id = -1,
	};

	platform_device_register_full(&pdevinfo);
}

static int __init cpu8815_init(void)
{
	struct nmk_gpio_platform_data pdata = {
		/* No custom data yet */
	};

	cpu8815_add_gpios(cpu8815_gpio_base, ARRAY_SIZE(cpu8815_gpio_base),
			  IRQ_GPIO0, &pdata);
	cpu8815_add_pinctrl(NULL, "pinctrl-stn8815");
	amba_apb_device_add(NULL, "rng", NOMADIK_RNG_BASE, SZ_4K, 0, 0, NULL, 0);
	amba_apb_device_add(NULL, "rtc-pl031", NOMADIK_RTC_BASE, SZ_4K, IRQ_RTC_RTT, 0, NULL, 0);
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

	/*
	 * Init clocks here so that they are available for system timer
	 * initialization.
	 */
	nomadik_clk_init();
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

void cpu8815_restart(char mode, const char *cmd)
{
	void __iomem *src_rstsr = io_p2v(NOMADIK_SRC_BASE + 0x18);

	/* FIXME: use egpio when implemented */

	/* Write anything to Reset status register */
	writel(1, src_rstsr);
}
