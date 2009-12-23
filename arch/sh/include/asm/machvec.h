/*
 * include/asm-sh/machvec.h
 *
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 */

#ifndef _ASM_SH_MACHVEC_H
#define _ASM_SH_MACHVEC_H

#include <linux/types.h>
#include <linux/time.h>
#include <generated/machtypes.h>

struct sh_machine_vector {
	void (*mv_setup)(char **cmdline_p);
	const char *mv_name;
	int mv_nr_irqs;

	u8 (*mv_inb)(unsigned long);
	u16 (*mv_inw)(unsigned long);
	u32 (*mv_inl)(unsigned long);
	void (*mv_outb)(u8, unsigned long);
	void (*mv_outw)(u16, unsigned long);
	void (*mv_outl)(u32, unsigned long);

	u8 (*mv_inb_p)(unsigned long);
	u16 (*mv_inw_p)(unsigned long);
	u32 (*mv_inl_p)(unsigned long);
	void (*mv_outb_p)(u8, unsigned long);
	void (*mv_outw_p)(u16, unsigned long);
	void (*mv_outl_p)(u32, unsigned long);

	void (*mv_insb)(unsigned long, void *dst, unsigned long count);
	void (*mv_insw)(unsigned long, void *dst, unsigned long count);
	void (*mv_insl)(unsigned long, void *dst, unsigned long count);
	void (*mv_outsb)(unsigned long, const void *src, unsigned long count);
	void (*mv_outsw)(unsigned long, const void *src, unsigned long count);
	void (*mv_outsl)(unsigned long, const void *src, unsigned long count);

	int (*mv_irq_demux)(int irq);

	void (*mv_init_irq)(void);

	void __iomem *(*mv_ioport_map)(unsigned long port, unsigned int size);
	void (*mv_ioport_unmap)(void __iomem *);

	int (*mv_clk_init)(void);
	int (*mv_mode_pins)(void);
};

extern struct sh_machine_vector sh_mv;

#define get_system_type()	sh_mv.mv_name

#define __initmv \
	__used __section(.machvec.init)

#endif /* _ASM_SH_MACHVEC_H */
