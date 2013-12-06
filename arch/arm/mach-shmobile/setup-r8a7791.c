/*
 * r8a7791 processor support
 *
 * Copyright (C) 2013  Renesas Electronics Corporation
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
#include <linux/platform_data/gpio-rcar.h>
#include <linux/platform_data/irq-renesas-irqc.h>
#include <linux/serial_sci.h>
#include <linux/sh_timer.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7791.h>
#include <mach/rcar-gen2.h>
#include <asm/mach/arch.h>

static const struct resource pfc_resources[] __initconst = {
	DEFINE_RES_MEM(0xe6060000, 0x250),
};

#define r8a7791_register_pfc()						\
	platform_device_register_simple("pfc-r8a7791", -1, pfc_resources, \
					ARRAY_SIZE(pfc_resources))

#define R8A7791_GPIO(idx, base, nr)					\
static const struct resource r8a7791_gpio##idx##_resources[] __initconst = { \
	DEFINE_RES_MEM((base), 0x50),					\
	DEFINE_RES_IRQ(gic_spi(4 + (idx))),				\
};									\
									\
static const struct gpio_rcar_config					\
r8a7791_gpio##idx##_platform_data __initconst = {			\
	.gpio_base	= 32 * (idx),					\
	.irq_base	= 0,						\
	.number_of_pins	= (nr),						\
	.pctl_name	= "pfc-r8a7791",				\
	.has_both_edge_trigger = 1,					\
};									\

R8A7791_GPIO(0, 0xe6050000, 32);
R8A7791_GPIO(1, 0xe6051000, 32);
R8A7791_GPIO(2, 0xe6052000, 32);
R8A7791_GPIO(3, 0xe6053000, 32);
R8A7791_GPIO(4, 0xe6054000, 32);
R8A7791_GPIO(5, 0xe6055000, 32);
R8A7791_GPIO(6, 0xe6055400, 32);
R8A7791_GPIO(7, 0xe6055800, 26);

#define r8a7791_register_gpio(idx)					\
	platform_device_register_resndata(&platform_bus, "gpio_rcar", idx, \
		r8a7791_gpio##idx##_resources,				\
		ARRAY_SIZE(r8a7791_gpio##idx##_resources),		\
		&r8a7791_gpio##idx##_platform_data,			\
		sizeof(r8a7791_gpio##idx##_platform_data))

void __init r8a7791_pinmux_init(void)
{
	r8a7791_register_pfc();
	r8a7791_register_gpio(0);
	r8a7791_register_gpio(1);
	r8a7791_register_gpio(2);
	r8a7791_register_gpio(3);
	r8a7791_register_gpio(4);
	r8a7791_register_gpio(5);
	r8a7791_register_gpio(6);
	r8a7791_register_gpio(7);
}

#define __R8A7791_SCIF(scif_type, algo, index, baseaddr, irq)		\
static struct plat_sci_port scif##index##_platform_data = {		\
	.type		= scif_type,					\
	.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,		\
	.scbrr_algo_id	= algo,						\
	.scscr		= SCSCR_RE | SCSCR_TE,				\
};									\
									\
static struct resource scif##index##_resources[] = {			\
	DEFINE_RES_MEM(baseaddr, 0x100),				\
	DEFINE_RES_IRQ(irq),						\
}

#define R8A7791_SCIF(index, baseaddr, irq)				\
	__R8A7791_SCIF(PORT_SCIF, SCBRR_ALGO_2, index, baseaddr, irq)

#define R8A7791_SCIFA(index, baseaddr, irq)				\
	__R8A7791_SCIF(PORT_SCIFA, SCBRR_ALGO_4, index, baseaddr, irq)

#define R8A7791_SCIFB(index, baseaddr, irq)				\
	__R8A7791_SCIF(PORT_SCIFB, SCBRR_ALGO_4, index, baseaddr, irq)

R8A7791_SCIFA(0,  0xe6c40000, gic_spi(144)); /* SCIFA0 */
R8A7791_SCIFA(1,  0xe6c50000, gic_spi(145)); /* SCIFA1 */
R8A7791_SCIFB(2,  0xe6c20000, gic_spi(148)); /* SCIFB0 */
R8A7791_SCIFB(3,  0xe6c30000, gic_spi(149)); /* SCIFB1 */
R8A7791_SCIFB(4,  0xe6ce0000, gic_spi(150)); /* SCIFB2 */
R8A7791_SCIFA(5,  0xe6c60000, gic_spi(151)); /* SCIFA2 */
R8A7791_SCIF(6,   0xe6e60000, gic_spi(152)); /* SCIF0 */
R8A7791_SCIF(7,   0xe6e68000, gic_spi(153)); /* SCIF1 */
R8A7791_SCIF(8,   0xe6e58000, gic_spi(22)); /* SCIF2 */
R8A7791_SCIF(9,   0xe6ea8000, gic_spi(23)); /* SCIF3 */
R8A7791_SCIF(10,  0xe6ee0000, gic_spi(24)); /* SCIF4 */
R8A7791_SCIF(11,  0xe6ee8000, gic_spi(25)); /* SCIF5 */
R8A7791_SCIFA(12, 0xe6c70000, gic_spi(29)); /* SCIFA3 */
R8A7791_SCIFA(13, 0xe6c78000, gic_spi(30)); /* SCIFA4 */
R8A7791_SCIFA(14, 0xe6c80000, gic_spi(31)); /* SCIFA5 */

#define r8a7791_register_scif(index)					       \
	platform_device_register_resndata(&platform_bus, "sh-sci", index,      \
					  scif##index##_resources,	       \
					  ARRAY_SIZE(scif##index##_resources), \
					  &scif##index##_platform_data,	       \
					  sizeof(scif##index##_platform_data))

static const struct sh_timer_config cmt00_platform_data __initconst = {
	.name = "CMT00",
	.timer_bit = 0,
	.clockevent_rating = 80,
};

static const struct resource cmt00_resources[] __initconst = {
	DEFINE_RES_MEM(0xffca0510, 0x0c),
	DEFINE_RES_MEM(0xffca0500, 0x04),
	DEFINE_RES_IRQ(gic_spi(142)), /* CMT0_0 */
};

#define r8a7791_register_cmt(idx)					\
	platform_device_register_resndata(&platform_bus, "sh_cmt",	\
					  idx, cmt##idx##_resources,	\
					  ARRAY_SIZE(cmt##idx##_resources), \
					  &cmt##idx##_platform_data,	\
					  sizeof(struct sh_timer_config))

static struct renesas_irqc_config irqc0_data = {
	.irq_base = irq_pin(0), /* IRQ0 -> IRQ9 */
};

static struct resource irqc0_resources[] = {
	DEFINE_RES_MEM(0xe61c0000, 0x200), /* IRQC Event Detector Block_0 */
	DEFINE_RES_IRQ(gic_spi(0)), /* IRQ0 */
	DEFINE_RES_IRQ(gic_spi(1)), /* IRQ1 */
	DEFINE_RES_IRQ(gic_spi(2)), /* IRQ2 */
	DEFINE_RES_IRQ(gic_spi(3)), /* IRQ3 */
	DEFINE_RES_IRQ(gic_spi(12)), /* IRQ4 */
	DEFINE_RES_IRQ(gic_spi(13)), /* IRQ5 */
	DEFINE_RES_IRQ(gic_spi(14)), /* IRQ6 */
	DEFINE_RES_IRQ(gic_spi(15)), /* IRQ7 */
	DEFINE_RES_IRQ(gic_spi(16)), /* IRQ8 */
	DEFINE_RES_IRQ(gic_spi(17)), /* IRQ9 */
};

#define r8a7791_register_irqc(idx)					\
	platform_device_register_resndata(&platform_bus, "renesas_irqc", \
					  idx, irqc##idx##_resources,	\
					  ARRAY_SIZE(irqc##idx##_resources), \
					  &irqc##idx##_data,		\
					  sizeof(struct renesas_irqc_config))

static const struct resource thermal_resources[] __initconst = {
	DEFINE_RES_MEM(0xe61f0000, 0x14),
	DEFINE_RES_MEM(0xe61f0100, 0x38),
	DEFINE_RES_IRQ(gic_spi(69)),
};

#define r8a7791_register_thermal()					\
	platform_device_register_simple("rcar_thermal", -1,		\
					thermal_resources,		\
					ARRAY_SIZE(thermal_resources))

void __init r8a7791_add_dt_devices(void)
{
	r8a7791_register_scif(0);
	r8a7791_register_scif(1);
	r8a7791_register_scif(2);
	r8a7791_register_scif(3);
	r8a7791_register_scif(4);
	r8a7791_register_scif(5);
	r8a7791_register_scif(6);
	r8a7791_register_scif(7);
	r8a7791_register_scif(8);
	r8a7791_register_scif(9);
	r8a7791_register_scif(10);
	r8a7791_register_scif(11);
	r8a7791_register_scif(12);
	r8a7791_register_scif(13);
	r8a7791_register_scif(14);
	r8a7791_register_cmt(00);
}

void __init r8a7791_add_standard_devices(void)
{
	r8a7791_add_dt_devices();
	r8a7791_register_irqc(0);
	r8a7791_register_thermal();
}

void __init r8a7791_init_early(void)
{
#ifndef CONFIG_ARM_ARCH_TIMER
	shmobile_setup_delay(1300, 2, 4); /* Cortex-A15 @ 1300MHz */
#endif
}

#ifdef CONFIG_USE_OF
static const char *r8a7791_boards_compat_dt[] __initdata = {
	"renesas,r8a7791",
	NULL,
};

DT_MACHINE_START(R8A7791_DT, "Generic R8A7791 (Flattened Device Tree)")
	.smp		= smp_ops(r8a7791_smp_ops),
	.init_early	= r8a7791_init_early,
	.init_time	= rcar_gen2_timer_init,
	.dt_compat	= r8a7791_boards_compat_dt,
MACHINE_END
#endif /* CONFIG_USE_OF */
