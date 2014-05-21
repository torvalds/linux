/*
 * r7s72100 processor support
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
#include <linux/serial_sci.h>
#include <linux/sh_timer.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r7s72100.h>
#include <asm/mach/arch.h>

#define R7S72100_SCIF(index, baseaddr, irq)				\
static const struct plat_sci_port scif##index##_platform_data = {	\
	.type		= PORT_SCIF,					\
	.regtype	= SCIx_SH2_SCIF_FIFODATA_REGTYPE,		\
	.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,		\
	.scscr		= SCSCR_RIE | SCSCR_TIE | SCSCR_RE | SCSCR_TE |	\
			  SCSCR_REIE,					\
};									\
									\
static struct resource scif##index##_resources[] = {			\
	DEFINE_RES_MEM(baseaddr, 0x100),				\
	DEFINE_RES_IRQ(irq + 1),					\
	DEFINE_RES_IRQ(irq + 2),					\
	DEFINE_RES_IRQ(irq + 3),					\
	DEFINE_RES_IRQ(irq),						\
}									\

R7S72100_SCIF(0, 0xe8007000, gic_iid(221));
R7S72100_SCIF(1, 0xe8007800, gic_iid(225));
R7S72100_SCIF(2, 0xe8008000, gic_iid(229));
R7S72100_SCIF(3, 0xe8008800, gic_iid(233));
R7S72100_SCIF(4, 0xe8009000, gic_iid(237));
R7S72100_SCIF(5, 0xe8009800, gic_iid(241));
R7S72100_SCIF(6, 0xe800a000, gic_iid(245));
R7S72100_SCIF(7, 0xe800a800, gic_iid(249));

#define r7s72100_register_scif(index)					       \
	platform_device_register_resndata(&platform_bus, "sh-sci", index,      \
					  scif##index##_resources,	       \
					  ARRAY_SIZE(scif##index##_resources), \
					  &scif##index##_platform_data,	       \
					  sizeof(scif##index##_platform_data))


static struct resource mtu2_resources[] __initdata = {
	DEFINE_RES_MEM(0xfcff0000, 0x400),
	DEFINE_RES_IRQ_NAMED(gic_iid(139), "tgi0a"),
};

#define r7s72100_register_mtu2()					\
	platform_device_register_resndata(&platform_bus, "sh-mtu2",	\
					  -1, mtu2_resources,		\
					  ARRAY_SIZE(mtu2_resources),	\
					  NULL, 0)

void __init r7s72100_add_dt_devices(void)
{
	r7s72100_register_scif(0);
	r7s72100_register_scif(1);
	r7s72100_register_scif(2);
	r7s72100_register_scif(3);
	r7s72100_register_scif(4);
	r7s72100_register_scif(5);
	r7s72100_register_scif(6);
	r7s72100_register_scif(7);
	r7s72100_register_mtu2();
}

void __init r7s72100_init_early(void)
{
	shmobile_setup_delay(400, 1, 3); /* Cortex-A9 @ 400MHz */
}

#ifdef CONFIG_USE_OF
static const char *r7s72100_boards_compat_dt[] __initdata = {
	"renesas,r7s72100",
	NULL,
};

DT_MACHINE_START(R7S72100_DT, "Generic R7S72100 (Flattened Device Tree)")
	.init_early	= r7s72100_init_early,
	.dt_compat	= r7s72100_boards_compat_dt,
MACHINE_END
#endif /* CONFIG_USE_OF */
