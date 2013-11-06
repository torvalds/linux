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

#define SCIF_DATA(index, baseaddr, irq)					\
[index] = {								\
	.type		= PORT_SCIF,					\
	.regtype	= SCIx_SH2_SCIF_FIFODATA_REGTYPE,		\
	.flags		= UPF_BOOT_AUTOCONF | UPF_IOREMAP,		\
	.scbrr_algo_id	= SCBRR_ALGO_2,					\
	.scscr		= SCSCR_RIE | SCSCR_TIE | SCSCR_RE | SCSCR_TE |	\
			  SCSCR_REIE,					\
	.mapbase	= baseaddr,					\
	.irqs		= { irq + 1, irq + 2, irq + 3, irq },		\
}

enum { SCIF0, SCIF1, SCIF2, SCIF3, SCIF4, SCIF5, SCIF6, SCIF7 };

static const struct plat_sci_port scif[] __initconst = {
	SCIF_DATA(SCIF0, 0xe8007000, gic_iid(221)), /* SCIF0 */
	SCIF_DATA(SCIF1, 0xe8007800, gic_iid(225)), /* SCIF1 */
	SCIF_DATA(SCIF2, 0xe8008000, gic_iid(229)), /* SCIF2 */
	SCIF_DATA(SCIF3, 0xe8008800, gic_iid(233)), /* SCIF3 */
	SCIF_DATA(SCIF4, 0xe8009000, gic_iid(237)), /* SCIF4 */
	SCIF_DATA(SCIF5, 0xe8009800, gic_iid(241)), /* SCIF5 */
	SCIF_DATA(SCIF6, 0xe800a000, gic_iid(245)), /* SCIF6 */
	SCIF_DATA(SCIF7, 0xe800a800, gic_iid(249)), /* SCIF7 */
};

static inline void r7s72100_register_scif(int idx)
{
	platform_device_register_data(&platform_bus, "sh-sci", idx, &scif[idx],
				      sizeof(struct plat_sci_port));
}


static struct sh_timer_config mtu2_0_platform_data __initdata = {
	.name = "MTU2_0",
	.timer_bit = 0,
	.channel_offset = -0x80,
	.clockevent_rating = 200,
};

static struct resource mtu2_0_resources[] __initdata = {
	DEFINE_RES_MEM(0xfcff0300, 0x27),
	DEFINE_RES_IRQ(gic_iid(139)), /* MTU2 TGI0A */
};

#define r7s72100_register_mtu2(idx)					\
	platform_device_register_resndata(&platform_bus, "sh_mtu2",	\
					  idx, mtu2_##idx##_resources,	\
					  ARRAY_SIZE(mtu2_##idx##_resources), \
					  &mtu2_##idx##_platform_data,	\
					  sizeof(struct sh_timer_config))

void __init r7s72100_add_dt_devices(void)
{
	r7s72100_register_scif(SCIF0);
	r7s72100_register_scif(SCIF1);
	r7s72100_register_scif(SCIF2);
	r7s72100_register_scif(SCIF3);
	r7s72100_register_scif(SCIF4);
	r7s72100_register_scif(SCIF5);
	r7s72100_register_scif(SCIF6);
	r7s72100_register_scif(SCIF7);
	r7s72100_register_mtu2(0);
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
