/*
 * r8a73a4 processor support
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
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/platform_data/irq-renesas-irqc.h>
#include <linux/serial_sci.h>
#include <linux/sh_timer.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a73a4.h>
#include <asm/mach/arch.h>

static const struct resource pfc_resources[] = {
	DEFINE_RES_MEM(0xe6050000, 0x9000),
};

void __init r8a73a4_pinmux_init(void)
{
	platform_device_register_simple("pfc-r8a73a4", -1, pfc_resources,
					ARRAY_SIZE(pfc_resources));
}

#define SCIF_COMMON(scif_type, baseaddr, irq)			\
	.type		= scif_type,				\
	.mapbase	= baseaddr,				\
	.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,	\
	.scbrr_algo_id	= SCBRR_ALGO_4,				\
	.irqs		= SCIx_IRQ_MUXED(irq)

#define SCIFA_DATA(index, baseaddr, irq)		\
[index] = {						\
	SCIF_COMMON(PORT_SCIFA, baseaddr, irq),		\
	.scscr = SCSCR_RE | SCSCR_TE | SCSCR_CKE0,	\
}

#define SCIFB_DATA(index, baseaddr, irq)	\
[index] = {					\
	SCIF_COMMON(PORT_SCIFB, baseaddr, irq),	\
	.scscr = SCSCR_RE | SCSCR_TE,		\
}

enum { SCIFA0, SCIFA1, SCIFB0, SCIFB1, SCIFB2, SCIFB3 };

static const struct plat_sci_port scif[] = {
	SCIFA_DATA(SCIFA0, 0xe6c40000, gic_spi(144)), /* SCIFA0 */
	SCIFA_DATA(SCIFA1, 0xe6c50000, gic_spi(145)), /* SCIFA1 */
	SCIFB_DATA(SCIFB0, 0xe6c20000, gic_spi(148)), /* SCIFB0 */
	SCIFB_DATA(SCIFB1, 0xe6c30000, gic_spi(149)), /* SCIFB1 */
	SCIFB_DATA(SCIFB2, 0xe6ce0000, gic_spi(150)), /* SCIFB2 */
	SCIFB_DATA(SCIFB3, 0xe6cf0000, gic_spi(151)), /* SCIFB3 */
};

static inline void r8a73a4_register_scif(int idx)
{
	platform_device_register_data(&platform_bus, "sh-sci", idx, &scif[idx],
				      sizeof(struct plat_sci_port));
}

static const struct renesas_irqc_config irqc0_data = {
	.irq_base = irq_pin(0), /* IRQ0 -> IRQ31 */
};

static const struct resource irqc0_resources[] = {
	DEFINE_RES_MEM(0xe61c0000, 0x200), /* IRQC Event Detector Block_0 */
	DEFINE_RES_IRQ(gic_spi(0)), /* IRQ0 */
	DEFINE_RES_IRQ(gic_spi(1)), /* IRQ1 */
	DEFINE_RES_IRQ(gic_spi(2)), /* IRQ2 */
	DEFINE_RES_IRQ(gic_spi(3)), /* IRQ3 */
	DEFINE_RES_IRQ(gic_spi(4)), /* IRQ4 */
	DEFINE_RES_IRQ(gic_spi(5)), /* IRQ5 */
	DEFINE_RES_IRQ(gic_spi(6)), /* IRQ6 */
	DEFINE_RES_IRQ(gic_spi(7)), /* IRQ7 */
	DEFINE_RES_IRQ(gic_spi(8)), /* IRQ8 */
	DEFINE_RES_IRQ(gic_spi(9)), /* IRQ9 */
	DEFINE_RES_IRQ(gic_spi(10)), /* IRQ10 */
	DEFINE_RES_IRQ(gic_spi(11)), /* IRQ11 */
	DEFINE_RES_IRQ(gic_spi(12)), /* IRQ12 */
	DEFINE_RES_IRQ(gic_spi(13)), /* IRQ13 */
	DEFINE_RES_IRQ(gic_spi(14)), /* IRQ14 */
	DEFINE_RES_IRQ(gic_spi(15)), /* IRQ15 */
	DEFINE_RES_IRQ(gic_spi(16)), /* IRQ16 */
	DEFINE_RES_IRQ(gic_spi(17)), /* IRQ17 */
	DEFINE_RES_IRQ(gic_spi(18)), /* IRQ18 */
	DEFINE_RES_IRQ(gic_spi(19)), /* IRQ19 */
	DEFINE_RES_IRQ(gic_spi(20)), /* IRQ20 */
	DEFINE_RES_IRQ(gic_spi(21)), /* IRQ21 */
	DEFINE_RES_IRQ(gic_spi(22)), /* IRQ22 */
	DEFINE_RES_IRQ(gic_spi(23)), /* IRQ23 */
	DEFINE_RES_IRQ(gic_spi(24)), /* IRQ24 */
	DEFINE_RES_IRQ(gic_spi(25)), /* IRQ25 */
	DEFINE_RES_IRQ(gic_spi(26)), /* IRQ26 */
	DEFINE_RES_IRQ(gic_spi(27)), /* IRQ27 */
	DEFINE_RES_IRQ(gic_spi(28)), /* IRQ28 */
	DEFINE_RES_IRQ(gic_spi(29)), /* IRQ29 */
	DEFINE_RES_IRQ(gic_spi(30)), /* IRQ30 */
	DEFINE_RES_IRQ(gic_spi(31)), /* IRQ31 */
};

static const struct renesas_irqc_config irqc1_data = {
	.irq_base = irq_pin(32), /* IRQ32 -> IRQ57 */
};

static const struct resource irqc1_resources[] = {
	DEFINE_RES_MEM(0xe61c0200, 0x200), /* IRQC Event Detector Block_1 */
	DEFINE_RES_IRQ(gic_spi(32)), /* IRQ32 */
	DEFINE_RES_IRQ(gic_spi(33)), /* IRQ33 */
	DEFINE_RES_IRQ(gic_spi(34)), /* IRQ34 */
	DEFINE_RES_IRQ(gic_spi(35)), /* IRQ35 */
	DEFINE_RES_IRQ(gic_spi(36)), /* IRQ36 */
	DEFINE_RES_IRQ(gic_spi(37)), /* IRQ37 */
	DEFINE_RES_IRQ(gic_spi(38)), /* IRQ38 */
	DEFINE_RES_IRQ(gic_spi(39)), /* IRQ39 */
	DEFINE_RES_IRQ(gic_spi(40)), /* IRQ40 */
	DEFINE_RES_IRQ(gic_spi(41)), /* IRQ41 */
	DEFINE_RES_IRQ(gic_spi(42)), /* IRQ42 */
	DEFINE_RES_IRQ(gic_spi(43)), /* IRQ43 */
	DEFINE_RES_IRQ(gic_spi(44)), /* IRQ44 */
	DEFINE_RES_IRQ(gic_spi(45)), /* IRQ45 */
	DEFINE_RES_IRQ(gic_spi(46)), /* IRQ46 */
	DEFINE_RES_IRQ(gic_spi(47)), /* IRQ47 */
	DEFINE_RES_IRQ(gic_spi(48)), /* IRQ48 */
	DEFINE_RES_IRQ(gic_spi(49)), /* IRQ49 */
	DEFINE_RES_IRQ(gic_spi(50)), /* IRQ50 */
	DEFINE_RES_IRQ(gic_spi(51)), /* IRQ51 */
	DEFINE_RES_IRQ(gic_spi(52)), /* IRQ52 */
	DEFINE_RES_IRQ(gic_spi(53)), /* IRQ53 */
	DEFINE_RES_IRQ(gic_spi(54)), /* IRQ54 */
	DEFINE_RES_IRQ(gic_spi(55)), /* IRQ55 */
	DEFINE_RES_IRQ(gic_spi(56)), /* IRQ56 */
	DEFINE_RES_IRQ(gic_spi(57)), /* IRQ57 */
};

#define r8a73a4_register_irqc(idx)					\
	platform_device_register_resndata(&platform_bus, "renesas_irqc", \
					  idx, irqc##idx##_resources,	\
					  ARRAY_SIZE(irqc##idx##_resources), \
					  &irqc##idx##_data,		\
					  sizeof(struct renesas_irqc_config))

/* Thermal0 -> Thermal2 */
static const struct resource thermal0_resources[] = {
	DEFINE_RES_MEM(0xe61f0000, 0x14),
	DEFINE_RES_MEM(0xe61f0100, 0x38),
	DEFINE_RES_MEM(0xe61f0200, 0x38),
	DEFINE_RES_MEM(0xe61f0300, 0x38),
	DEFINE_RES_IRQ(gic_spi(69)),
};

#define r8a73a4_register_thermal()					\
	platform_device_register_simple("rcar_thermal", -1,		\
					thermal0_resources,		\
					ARRAY_SIZE(thermal0_resources))

static struct sh_timer_config cmt10_platform_data = {
	.name = "CMT10",
	.timer_bit = 0,
	.clockevent_rating = 80,
};

static struct resource cmt10_resources[] = {
	DEFINE_RES_MEM(0xe6130010, 0x0c),
	DEFINE_RES_MEM(0xe6130000, 0x04),
	DEFINE_RES_IRQ(gic_spi(120)), /* CMT1_0 */
};

#define r8a7790_register_cmt(idx)					\
	platform_device_register_resndata(&platform_bus, "sh_cmt",	\
					  idx, cmt##idx##_resources,	\
					  ARRAY_SIZE(cmt##idx##_resources), \
					  &cmt##idx##_platform_data,	\
					  sizeof(struct sh_timer_config))

void __init r8a73a4_add_dt_devices(void)
{
	r8a73a4_register_scif(SCIFA0);
	r8a73a4_register_scif(SCIFA1);
	r8a73a4_register_scif(SCIFB0);
	r8a73a4_register_scif(SCIFB1);
	r8a73a4_register_scif(SCIFB2);
	r8a73a4_register_scif(SCIFB3);
	r8a7790_register_cmt(10);
}

void __init r8a73a4_add_standard_devices(void)
{
	r8a73a4_add_dt_devices();
	r8a73a4_register_irqc(0);
	r8a73a4_register_irqc(1);
	r8a73a4_register_thermal();
}

void __init r8a73a4_init_delay(void)
{
#ifndef CONFIG_ARM_ARCH_TIMER
	shmobile_setup_delay(1500, 2, 4); /* Cortex-A15 @ 1500MHz */
#endif
}

#ifdef CONFIG_USE_OF

static const char *r8a73a4_boards_compat_dt[] __initdata = {
	"renesas,r8a73a4",
	NULL,
};

DT_MACHINE_START(R8A73A4_DT, "Generic R8A73A4 (Flattened Device Tree)")
	.init_early	= r8a73a4_init_delay,
	.dt_compat	= r8a73a4_boards_compat_dt,
MACHINE_END
#endif /* CONFIG_USE_OF */
