/*
 * linux/arch/sh/kernel/mach_hs7751rvoip.c
 *
 * Minor tweak of mach_se.c file to reference hs7751rvoip-specific items.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Machine vector for the Renesas Technology sales HS7751RVoIP
 */

#include <linux/config.h>
#include <linux/init.h>

#include <asm/machvec.h>
#include <asm/rtc.h>
#include <asm/irq.h>
#include <asm/hs7751rvoip/io.h>

extern void init_hs7751rvoip_IRQ(void);
extern void *hs7751rvoip_ioremap(unsigned long, unsigned long);

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_hs7751rvoip __initmv = {
	.mv_nr_irqs		= 72,

	.mv_inb			= hs7751rvoip_inb,
	.mv_inw			= hs7751rvoip_inw,
	.mv_inl			= hs7751rvoip_inl,
	.mv_outb		= hs7751rvoip_outb,
	.mv_outw		= hs7751rvoip_outw,
	.mv_outl		= hs7751rvoip_outl,

	.mv_inb_p		= hs7751rvoip_inb_p,
	.mv_inw_p		= hs7751rvoip_inw,
	.mv_inl_p		= hs7751rvoip_inl,
	.mv_outb_p		= hs7751rvoip_outb_p,
	.mv_outw_p		= hs7751rvoip_outw,
	.mv_outl_p		= hs7751rvoip_outl,

	.mv_insb		= hs7751rvoip_insb,
	.mv_insw		= hs7751rvoip_insw,
	.mv_insl		= hs7751rvoip_insl,
	.mv_outsb		= hs7751rvoip_outsb,
	.mv_outsw		= hs7751rvoip_outsw,
	.mv_outsl		= hs7751rvoip_outsl,

	.mv_ioremap		= hs7751rvoip_ioremap,
	.mv_isa_port2addr	= hs7751rvoip_isa_port2addr,
	.mv_init_irq		= init_hs7751rvoip_IRQ,
};
ALIAS_MV(hs7751rvoip)
