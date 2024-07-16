/* SPDX-License-Identifier: GPL-2.0
 *
 * include/asm-sh/machvec.h
 *
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 */

#ifndef _ASM_SH_MACHVEC_H
#define _ASM_SH_MACHVEC_H

#include <linux/types.h>
#include <linux/time.h>
#include <generated/machtypes.h>

struct sh_machine_vector {
	void (*mv_setup)(char **cmdline_p);
	const char *mv_name;

	int (*mv_irq_demux)(int irq);
	void (*mv_init_irq)(void);

#ifdef CONFIG_HAS_IOPORT_MAP
	void __iomem *(*mv_ioport_map)(unsigned long port, unsigned int size);
	void (*mv_ioport_unmap)(void __iomem *);
#endif

	int (*mv_clk_init)(void);
	int (*mv_mode_pins)(void);

	void (*mv_mem_init)(void);
	void (*mv_mem_reserve)(void);
};

extern struct sh_machine_vector sh_mv;

#define get_system_type()	sh_mv.mv_name

#define __initmv \
	__used __section(".machvec.init")

#endif /* _ASM_SH_MACHVEC_H */
