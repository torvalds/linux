#ifndef _ASM_IA64_PERCPU_H
#define _ASM_IA64_PERCPU_H

/*
 * Copyright (C) 2002-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#ifdef __ASSEMBLY__
# define THIS_CPU(var)	(var)  /* use this to mark accesses to per-CPU variables... */
#else /* !__ASSEMBLY__ */


#include <linux/threads.h>

#ifdef CONFIG_SMP

#ifdef HAVE_MODEL_SMALL_ATTRIBUTE
# define PER_CPU_ATTRIBUTES	__attribute__((__model__ (__small__)))
#endif

#define __my_cpu_offset	__ia64_per_cpu_var(local_per_cpu_offset)

extern void *per_cpu_init(void);

#else /* ! SMP */

#define per_cpu_init()				(__phys_per_cpu_start)

#endif	/* SMP */

#define PER_CPU_BASE_SECTION ".data..percpu"

/*
 * Be extremely careful when taking the address of this variable!  Due to virtual
 * remapping, it is different from the canonical address returned by this_cpu_ptr(&var)!
 * On the positive side, using __ia64_per_cpu_var() instead of this_cpu_ptr() is slightly
 * more efficient.
 */
#define __ia64_per_cpu_var(var) (*({					\
	__verify_pcpu_ptr(&(var));					\
	((typeof(var) __kernel __force *)&(var));			\
}))

#include <asm-generic/percpu.h>

/* Equal to __per_cpu_offset[smp_processor_id()], but faster to access: */
DECLARE_PER_CPU(unsigned long, local_per_cpu_offset);

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_IA64_PERCPU_H */
