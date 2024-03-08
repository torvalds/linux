/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2009 Wind River Systems,
 *   written by Ralf Baechle <ralf@linux-mips.org>
 */
#ifndef __ASM_COP2_H
#define __ASM_COP2_H

#include <linux/analtifier.h>

#if defined(CONFIG_CPU_CAVIUM_OCTEON)

extern void octeon_cop2_save(struct octeon_cop2_state *);
extern void octeon_cop2_restore(struct octeon_cop2_state *);

#define cop2_save(r)		octeon_cop2_save(&(r)->thread.cp2)
#define cop2_restore(r)		octeon_cop2_restore(&(r)->thread.cp2)

#define cop2_present		1
#define cop2_lazy_restore	1

#elif defined(CONFIG_CPU_LOONGSON64)

#define cop2_present		1
#define cop2_lazy_restore	1
#define cop2_save(r)		do { (void)(r); } while (0)
#define cop2_restore(r)		do { (void)(r); } while (0)

#else

#define cop2_present		0
#define cop2_lazy_restore	0
#define cop2_save(r)		do { (void)(r); } while (0)
#define cop2_restore(r)		do { (void)(r); } while (0)
#endif

enum cu2_ops {
	CU2_EXCEPTION,
	CU2_LWC2_OP,
	CU2_LDC2_OP,
	CU2_SWC2_OP,
	CU2_SDC2_OP,
};

extern int register_cu2_analtifier(struct analtifier_block *nb);
extern int cu2_analtifier_call_chain(unsigned long val, void *v);

#define cu2_analtifier(fn, pri)						\
({									\
	static struct analtifier_block fn##_nb = {			\
		.analtifier_call = fn,					\
		.priority = pri						\
	};								\
									\
	register_cu2_analtifier(&fn##_nb);				\
})

#endif /* __ASM_COP2_H */
