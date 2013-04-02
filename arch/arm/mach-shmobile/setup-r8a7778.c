/*
 * r8a7778 processor support
 *
 * Copyright (C) 2013  Renesas Solutions Corp.
 * Copyright (C) 2013  Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
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
#include <linux/io.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/irqchip.h>
#include <linux/serial_sci.h>
#include <linux/sh_timer.h>
#include <mach/irqs.h>
#include <mach/r8a7778.h>
#include <mach/common.h>
#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>

/* SCIF */
#define SCIF_INFO(baseaddr, irq)				\
{								\
	.mapbase	= baseaddr,				\
	.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,	\
	.scscr		= SCSCR_RE | SCSCR_TE | SCSCR_CKE1,	\
	.scbrr_algo_id	= SCBRR_ALGO_2,				\
	.type		= PORT_SCIF,				\
	.irqs		= SCIx_IRQ_MUXED(irq),			\
}

static struct plat_sci_port scif_platform_data[] = {
	SCIF_INFO(0xffe40000, gic_iid(0x66)),
	SCIF_INFO(0xffe41000, gic_iid(0x67)),
	SCIF_INFO(0xffe42000, gic_iid(0x68)),
	SCIF_INFO(0xffe43000, gic_iid(0x69)),
	SCIF_INFO(0xffe44000, gic_iid(0x6a)),
	SCIF_INFO(0xffe45000, gic_iid(0x6b)),
};

/* TMU */
static struct resource sh_tmu0_resources[] = {
	DEFINE_RES_MEM(0xffd80008, 12),
	DEFINE_RES_IRQ(gic_iid(0x40)),
};

static struct sh_timer_config sh_tmu0_platform_data = {
	.name			= "TMU00",
	.channel_offset		= 0x4,
	.timer_bit		= 0,
	.clockevent_rating	= 200,
};

static struct resource sh_tmu1_resources[] = {
	DEFINE_RES_MEM(0xffd80014, 12),
	DEFINE_RES_IRQ(gic_iid(0x41)),
};

static struct sh_timer_config sh_tmu1_platform_data = {
	.name			= "TMU01",
	.channel_offset		= 0x10,
	.timer_bit		= 1,
	.clocksource_rating	= 200,
};

#define PLATFORM_INFO(n, i)					\
{								\
	.parent		= &platform_bus,			\
	.name		= #n,					\
	.id		= i,					\
	.res		= n ## i ## _resources,			\
	.num_res	= ARRAY_SIZE(n ## i ##_resources),	\
	.data		= &n ## i ##_platform_data,		\
	.size_data	= sizeof(n ## i ## _platform_data),	\
}

struct platform_device_info platform_devinfo[] = {
	PLATFORM_INFO(sh_tmu, 0),
	PLATFORM_INFO(sh_tmu, 1),
};

void __init r8a7778_add_standard_devices(void)
{
	int i;

#ifdef CONFIG_CACHE_L2X0
	void __iomem *base = ioremap_nocache(0xf0100000, 0x1000);
	if (base) {
		/*
		 * Early BRESP enable, Shared attribute override enable, 64K*16way
		 * don't call iounmap(base)
		 */
		l2x0_init(base, 0x40470000, 0x82000fff);
	}
#endif

	for (i = 0; i < ARRAY_SIZE(scif_platform_data); i++)
		platform_device_register_data(&platform_bus, "sh-sci", i,
					      &scif_platform_data[i],
					      sizeof(struct plat_sci_port));

	for (i = 0; i < ARRAY_SIZE(platform_devinfo); i++)
		platform_device_register_full(&platform_devinfo[i]);
}

#define INT2SMSKCR0	0x82288 /* 0xfe782288 */
#define INT2SMSKCR1	0x8228c /* 0xfe78228c */

#define INT2NTSR0	0x00018 /* 0xfe700018 */
#define INT2NTSR1	0x0002c /* 0xfe70002c */
static void __init r8a7778_init_irq_common(void)
{
	void __iomem *base = ioremap_nocache(0xfe700000, 0x00100000);

	BUG_ON(!base);

	/* route all interrupts to ARM */
	__raw_writel(0x73ffffff, base + INT2NTSR0);
	__raw_writel(0xffffffff, base + INT2NTSR1);

	/* unmask all known interrupts in INTCS2 */
	__raw_writel(0x08330773, base + INT2SMSKCR0);
	__raw_writel(0x00311110, base + INT2SMSKCR1);

	iounmap(base);
}

void __init r8a7778_init_irq(void)
{
	void __iomem *gic_dist_base;
	void __iomem *gic_cpu_base;

	gic_dist_base = ioremap_nocache(0xfe438000, PAGE_SIZE);
	gic_cpu_base  = ioremap_nocache(0xfe430000, PAGE_SIZE);
	BUG_ON(!gic_dist_base || !gic_cpu_base);

	/* use GIC to handle interrupts */
	gic_init(0, 29, gic_dist_base, gic_cpu_base);

	r8a7778_init_irq_common();
}

void __init r8a7778_init_delay(void)
{
	shmobile_setup_delay(800, 1, 3); /* Cortex-A9 @ 800MHz */
}

#ifdef CONFIG_USE_OF
void __init r8a7778_init_irq_dt(void)
{
	irqchip_init();
	r8a7778_init_irq_common();
}

static const struct of_dev_auxdata r8a7778_auxdata_lookup[] __initconst = {
	{},
};

void __init r8a7778_add_standard_devices_dt(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
			     r8a7778_auxdata_lookup, NULL);
}

static const char *r8a7778_compat_dt[] __initdata = {
	"renesas,r8a7778",
	NULL,
};

DT_MACHINE_START(R8A7778_DT, "Generic R8A7778 (Flattened Device Tree)")
	.init_early	= r8a7778_init_delay,
	.init_irq	= r8a7778_init_irq_dt,
	.init_machine	= r8a7778_add_standard_devices_dt,
	.init_time	= shmobile_timer_init,
	.dt_compat	= r8a7778_compat_dt,
MACHINE_END

#endif /* CONFIG_USE_OF */
