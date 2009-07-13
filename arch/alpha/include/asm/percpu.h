#ifndef __ALPHA_PERCPU_H
#define __ALPHA_PERCPU_H

#include <linux/compiler.h>
#include <linux/threads.h>
#include <linux/percpu-defs.h>

/*
 * Determine the real variable name from the name visible in the
 * kernel sources.
 */
#define per_cpu_var(var) per_cpu__##var

#ifdef CONFIG_SMP

/*
 * per_cpu_offset() is the offset that has to be added to a
 * percpu variable to get to the instance for a certain processor.
 */
extern unsigned long __per_cpu_offset[NR_CPUS];

#define per_cpu_offset(x) (__per_cpu_offset[x])

#define __my_cpu_offset per_cpu_offset(raw_smp_processor_id())
#ifdef CONFIG_DEBUG_PREEMPT
#define my_cpu_offset per_cpu_offset(smp_processor_id())
#else
#define my_cpu_offset __my_cpu_offset
#endif

#ifndef MODULE
#define SHIFT_PERCPU_PTR(var, offset) RELOC_HIDE(&per_cpu_var(var), (offset))
#define PER_CPU_DEF_ATTRIBUTES
#else
/*
 * To calculate addresses of locally defined variables, GCC uses 32-bit
 * displacement from the GP. Which doesn't work for per cpu variables in
 * modules, as an offset to the kernel per cpu area is way above 4G.
 *
 * This forces allocation of a GOT entry for per cpu variable using
 * ldq instruction with a 'literal' relocation.
 */
#define SHIFT_PERCPU_PTR(var, offset) ({		\
	extern int simple_identifier_##var(void);	\
	unsigned long __ptr, tmp_gp;			\
	asm (  "br	%1, 1f		  	      \n\
	1:	ldgp	%1, 0(%1)	    	      \n\
		ldq %0, per_cpu__" #var"(%1)\t!literal"		\
		: "=&r"(__ptr), "=&r"(tmp_gp));		\
	(typeof(&per_cpu_var(var)))(__ptr + (offset)); })

#define PER_CPU_DEF_ATTRIBUTES	__used

#endif /* MODULE */

/*
 * A percpu variable may point to a discarded regions. The following are
 * established ways to produce a usable pointer from the percpu variable
 * offset.
 */
#define per_cpu(var, cpu) \
	(*SHIFT_PERCPU_PTR(var, per_cpu_offset(cpu)))
#define __get_cpu_var(var) \
	(*SHIFT_PERCPU_PTR(var, my_cpu_offset))
#define __raw_get_cpu_var(var) \
	(*SHIFT_PERCPU_PTR(var, __my_cpu_offset))

#else /* ! SMP */

#define per_cpu(var, cpu)		(*((void)(cpu), &per_cpu_var(var)))
#define __get_cpu_var(var)		per_cpu_var(var)
#define __raw_get_cpu_var(var)		per_cpu_var(var)

#define PER_CPU_DEF_ATTRIBUTES

#endif /* SMP */

#ifdef CONFIG_SMP
#define PER_CPU_BASE_SECTION ".data.percpu"
#else
#define PER_CPU_BASE_SECTION ".data"
#endif

#ifdef CONFIG_SMP

#ifdef MODULE
#define PER_CPU_SHARED_ALIGNED_SECTION ""
#else
#define PER_CPU_SHARED_ALIGNED_SECTION ".shared_aligned"
#endif
#define PER_CPU_FIRST_SECTION ".first"

#else

#define PER_CPU_SHARED_ALIGNED_SECTION ""
#define PER_CPU_FIRST_SECTION ""

#endif

#define PER_CPU_ATTRIBUTES

#endif /* __ALPHA_PERCPU_H */
