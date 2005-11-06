/*
 * linux/arch/sh/kernel/mach_rts7751r2d.c
 *
 * Minor tweak of mach_se.c file to reference rts7751r2d-specific items.
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * Machine vector for the Renesas Technology sales RTS7751R2D
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/types.h>

#include <asm/machvec.h>
#include <asm/rtc.h>
#include <asm/irq.h>
#include <asm/rts7751r2d/io.h>

extern void heartbeat_rts7751r2d(void);
extern void init_rts7751r2d_IRQ(void);
extern void *rts7751r2d_ioremap(unsigned long, unsigned long);
extern int rts7751r2d_irq_demux(int irq);

extern void *voyagergx_consistent_alloc(struct device *, size_t, dma_addr_t *, gfp_t);
extern int voyagergx_consistent_free(struct device *, size_t, void *, dma_addr_t);

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_rts7751r2d __initmv = {
	.mv_nr_irqs		= 72,

	.mv_inb			= rts7751r2d_inb,
	.mv_inw			= rts7751r2d_inw,
	.mv_inl			= rts7751r2d_inl,
	.mv_outb		= rts7751r2d_outb,
	.mv_outw		= rts7751r2d_outw,
	.mv_outl		= rts7751r2d_outl,

	.mv_inb_p		= rts7751r2d_inb_p,
	.mv_inw_p		= rts7751r2d_inw,
	.mv_inl_p		= rts7751r2d_inl,
	.mv_outb_p		= rts7751r2d_outb_p,
	.mv_outw_p		= rts7751r2d_outw,
	.mv_outl_p		= rts7751r2d_outl,

	.mv_insb		= rts7751r2d_insb,
	.mv_insw		= rts7751r2d_insw,
	.mv_insl		= rts7751r2d_insl,
	.mv_outsb		= rts7751r2d_outsb,
	.mv_outsw		= rts7751r2d_outsw,
	.mv_outsl		= rts7751r2d_outsl,

	.mv_ioremap		= rts7751r2d_ioremap,
	.mv_isa_port2addr	= rts7751r2d_isa_port2addr,
	.mv_init_irq		= init_rts7751r2d_IRQ,
#ifdef CONFIG_HEARTBEAT
	.mv_heartbeat		= heartbeat_rts7751r2d,
#endif
	.mv_irq_demux		= rts7751r2d_irq_demux,

#ifdef CONFIG_USB_OHCI_HCD
	.mv_consistent_alloc	= voyagergx_consistent_alloc,
	.mv_consistent_free	= voyagergx_consistent_free,
#endif
};
ALIAS_MV(rts7751r2d)
