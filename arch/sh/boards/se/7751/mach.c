/*
 * linux/arch/sh/kernel/mach_7751se.c
 *
 * Minor tweak of mach_se.c file to reference 7751se-specific items.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Machine vector for the Hitachi 7751 SolutionEngine
 */

#include <linux/config.h>
#include <linux/init.h>

#include <asm/machvec.h>
#include <asm/rtc.h>
#include <asm/machvec_init.h>

#include <asm/se7751/io.h>

void heartbeat_7751se(void);
void init_7751se_IRQ(void);

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_7751se __initmv = {
	.mv_nr_irqs		= 72,

	.mv_inb			= sh7751se_inb,
	.mv_inw			= sh7751se_inw,
	.mv_inl			= sh7751se_inl,
	.mv_outb		= sh7751se_outb,
	.mv_outw		= sh7751se_outw,
	.mv_outl		= sh7751se_outl,

	.mv_inb_p		= sh7751se_inb_p,
	.mv_inw_p		= sh7751se_inw,
	.mv_inl_p		= sh7751se_inl,
	.mv_outb_p		= sh7751se_outb_p,
	.mv_outw_p		= sh7751se_outw,
	.mv_outl_p		= sh7751se_outl,

	.mv_insl		= sh7751se_insl,
	.mv_outsl		= sh7751se_outsl,

	.mv_isa_port2addr	= sh7751se_isa_port2addr,

	.mv_init_irq		= init_7751se_IRQ,
#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat		= heartbeat_7751se,
#endif
};
ALIAS_MV(7751se)
