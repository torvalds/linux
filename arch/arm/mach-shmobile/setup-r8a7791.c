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
#include <linux/serial_sci.h>
#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/r8a7791.h>
#include <asm/mach/arch.h>

#define SCIF_COMMON(scif_type, baseaddr, irq)			\
	.type		= scif_type,				\
	.mapbase	= baseaddr,				\
	.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,	\
	.irqs		= SCIx_IRQ_MUXED(irq)

#define SCIFA_DATA(index, baseaddr, irq)		\
[index] = {						\
	SCIF_COMMON(PORT_SCIFA, baseaddr, irq),		\
	.scbrr_algo_id	= SCBRR_ALGO_4,			\
	.scscr = SCSCR_RE | SCSCR_TE,	\
}

#define SCIFB_DATA(index, baseaddr, irq)	\
[index] = {					\
	SCIF_COMMON(PORT_SCIFB, baseaddr, irq),	\
	.scbrr_algo_id	= SCBRR_ALGO_4,		\
	.scscr = SCSCR_RE | SCSCR_TE,		\
}

#define SCIF_DATA(index, baseaddr, irq)		\
[index] = {						\
	SCIF_COMMON(PORT_SCIF, baseaddr, irq),		\
	.scbrr_algo_id	= SCBRR_ALGO_2,			\
	.scscr = SCSCR_RE | SCSCR_TE,	\
}

#define HSCIF_DATA(index, baseaddr, irq)		\
[index] = {						\
	SCIF_COMMON(PORT_HSCIF, baseaddr, irq),		\
	.scbrr_algo_id	= SCBRR_ALGO_6,			\
	.scscr = SCSCR_RE | SCSCR_TE,	\
}

enum { SCIFA0, SCIFA1, SCIFB0, SCIFB1, SCIFB2, SCIFA2, SCIF0, SCIF1,
	SCIF2, SCIF3, SCIF4, SCIF5, SCIFA3, SCIFA4, SCIFA5 };

static const struct plat_sci_port scif[] __initconst = {
	SCIFA_DATA(SCIFA0, 0xe6c40000, gic_spi(144)), /* SCIFA0 */
	SCIFA_DATA(SCIFA1, 0xe6c50000, gic_spi(145)), /* SCIFA1 */
	SCIFB_DATA(SCIFB0, 0xe6c20000, gic_spi(148)), /* SCIFB0 */
	SCIFB_DATA(SCIFB1, 0xe6c30000, gic_spi(149)), /* SCIFB1 */
	SCIFB_DATA(SCIFB2, 0xe6ce0000, gic_spi(150)), /* SCIFB2 */
	SCIFA_DATA(SCIFA2, 0xe6c60000, gic_spi(151)), /* SCIFA2 */
	SCIF_DATA(SCIF0, 0xe6e60000, gic_spi(152)), /* SCIF0 */
	SCIF_DATA(SCIF1, 0xe6e68000, gic_spi(153)), /* SCIF1 */
	SCIF_DATA(SCIF2, 0xe6e58000, gic_spi(22)), /* SCIF2 */
	SCIF_DATA(SCIF3, 0xe6ea8000, gic_spi(23)), /* SCIF3 */
	SCIF_DATA(SCIF4, 0xe6ee0000, gic_spi(24)), /* SCIF4 */
	SCIF_DATA(SCIF5, 0xe6ee8000, gic_spi(25)), /* SCIF5 */
	SCIFA_DATA(SCIFA3, 0xe6c70000, gic_spi(29)), /* SCIFA3 */
	SCIFA_DATA(SCIFA4, 0xe6c78000, gic_spi(30)), /* SCIFA4 */
	SCIFA_DATA(SCIFA5, 0xe6c80000, gic_spi(31)), /* SCIFA5 */
};

static inline void r8a7791_register_scif(int idx)
{
	platform_device_register_data(&platform_bus, "sh-sci", idx, &scif[idx],
				      sizeof(struct plat_sci_port));
}

void __init r8a7791_add_dt_devices(void)
{
	r8a7791_register_scif(SCIFA0);
	r8a7791_register_scif(SCIFA1);
	r8a7791_register_scif(SCIFB0);
	r8a7791_register_scif(SCIFB1);
	r8a7791_register_scif(SCIFB2);
	r8a7791_register_scif(SCIFA2);
	r8a7791_register_scif(SCIF0);
	r8a7791_register_scif(SCIF1);
	r8a7791_register_scif(SCIF2);
	r8a7791_register_scif(SCIF3);
	r8a7791_register_scif(SCIF4);
	r8a7791_register_scif(SCIF5);
	r8a7791_register_scif(SCIFA3);
	r8a7791_register_scif(SCIFA4);
	r8a7791_register_scif(SCIFA5);
}

#ifdef CONFIG_USE_OF
static const char *r8a7791_boards_compat_dt[] __initdata = {
	"renesas,r8a7791",
	NULL,
};

DT_MACHINE_START(R8A7791_DT, "Generic R8A7791 (Flattened Device Tree)")
	.dt_compat	= r8a7791_boards_compat_dt,
MACHINE_END
#endif /* CONFIG_USE_OF */
