#ifndef _ASM_X86_PERCPU_H
#define _ASM_X86_PERCPU_H

#ifdef CONFIG_X86_64
#define __percpu_seg		gs
#define __percpu_mov_op		movq
#else
#define __percpu_seg		fs
#define __percpu_mov_op		movl
#endif

#ifdef __ASSEMBLY__

/*
 * PER_CPU finds an address of a per-cpu variable.
 *
 * Args:
 *    var - variable name
 *    reg - 32bit register
 *
 * The resulting address is stored in the "reg" argument.
 *
 * Example:
 *    PER_CPU(cpu_gdt_descr, %ebx)
 */
#ifdef CONFIG_SMP
#define PER_CPU(var, reg)						\
	__percpu_mov_op %__percpu_seg:per_cpu__this_cpu_off, reg;	\
	lea per_cpu__##var(reg), reg
#define PER_CPU_VAR(var)	%__percpu_seg:per_cpu__##var
#else /* ! SMP */
#define PER_CPU(var, reg)						\
	__percpu_mov_op $per_cpu__##var, reg
#define PER_CPU_VAR(var)	per_cpu__##var
#endif	/* SMP */

#ifdef CONFIG_X86_64_SMP
#define INIT_PER_CPU_VAR(var)  init_per_cpu__##var
#else
#define INIT_PER_CPU_VAR(var)  per_cpu__##var
#endif

#else /* ...!ASSEMBLY */

#include <linux/kernel.h>
#include <linux/stringify.h>

#ifdef CONFIG_SMP
#define __percpu_arg(x)		"%%"__stringify(__percpu_seg)":%P" #x
#define __my_cpu_offset		percpu_read(this_cpu_off)
#else
#define __percpu_arg(x)		"%P" #x
#endif

/*
 * Initialized pointers to per-cpu variables needed for the boot
 * processor need to use these macros to get the proper address
 * offset from __per_cpu_load on SMP.
 *
 * There also must be an entry in vmlinux_64.lds.S
 */
#define DECLARE_INIT_PER_CPU(var) \
       extern typeof(per_cpu_var(var)) init_per_cpu_var(var)

#ifdef CONFIG_X86_64_SMP
#define init_per_cpu_var(var)  init_per_cpu__##var
#else
#define init_per_cpu_var(var)  per_cpu_var(var)
#endif

/* For arch-specific code, we can use direct single-insn ops (they
 * don't give an lvalue though). */
extern void __bad_percpu_size(void);

#define percpu_to_op(op, var, val)			\
do {							\
	typedef typeof(var) T__;			\
	if (0) {					\
		T__ tmp__;				\
		tmp__ = (val);				\
	}						\
	switch (sizeof(var)) {				\
	case 1:						\
		asm(op "b %1,"__percpu_arg(0)		\
		    : "+m" (var)			\
		    : "qi" ((T__)(val)));		\
		break;					\
	case 2:						\
		asm(op "w %1,"__percpu_arg(0)		\
		    : "+m" (var)			\
		    : "ri" ((T__)(val)));		\
		break;					\
	case 4:						\
		asm(op "l %1,"__percpu_arg(0)		\
		    : "+m" (var)			\
		    : "ri" ((T__)(val)));		\
		break;					\
	case 8:						\
		asm(op "q %1,"__percpu_arg(0)		\
		    : "+m" (var)			\
		    : "re" ((T__)(val)));		\
		break;					\
	default: __bad_percpu_size();			\
	}						\
} while (0)

#define percpu_from_op(op, var, constraint)		\
({							\
	typeof(var) ret__;				\
	switch (sizeof(var)) {				\
	case 1:						\
		asm(op "b "__percpu_arg(1)",%0"		\
		    : "=q" (ret__)			\
		    : constraint);			\
		break;					\
	case 2:						\
		asm(op "w "__percpu_arg(1)",%0"		\
		    : "=r" (ret__)			\
		    : constraint);			\
		break;					\
	case 4:						\
		asm(op "l "__percpu_arg(1)",%0"		\
		    : "=r" (ret__)			\
		    : constraint);			\
		break;					\
	case 8:						\
		asm(op "q "__percpu_arg(1)",%0"		\
		    : "=r" (ret__)			\
		    : constraint);			\
		break;					\
	default: __bad_percpu_size();			\
	}						\
	ret__;						\
})

/*
 * percpu_read() makes gcc load the percpu variable every time it is
 * accessed while percpu_read_stable() allows the value to be cached.
 * percpu_read_stable() is more efficient and can be used if its value
 * is guaranteed to be valid across cpus.  The current users include
 * get_current() and get_thread_info() both of which are actually
 * per-thread variables implemented as per-cpu variables and thus
 * stable for the duration of the respective task.
 */
#define percpu_read(var)	percpu_from_op("mov", per_cpu__##var,	\
					       "m" (per_cpu__##var))
#define percpu_read_stable(var)	percpu_from_op("mov", per_cpu__##var,	\
					       "p" (&per_cpu__##var))
#define percpu_write(var, val)	percpu_to_op("mov", per_cpu__##var, val)
#define percpu_add(var, val)	percpu_to_op("add", per_cpu__##var, val)
#define percpu_sub(var, val)	percpu_to_op("sub", per_cpu__##var, val)
#define percpu_and(var, val)	percpu_to_op("and", per_cpu__##var, val)
#define percpu_or(var, val)	percpu_to_op("or", per_cpu__##var, val)
#define percpu_xor(var, val)	percpu_to_op("xor", per_cpu__##var, val)

/* This is not atomic against other CPUs -- CPU preemption needs to be off */
#define x86_test_and_clear_bit_percpu(bit, var)				\
({									\
	int old__;							\
	asm volatile("btr %2,"__percpu_arg(1)"\n\tsbbl %0,%0"		\
		     : "=r" (old__), "+m" (per_cpu__##var)		\
		     : "dIr" (bit));					\
	old__;								\
})

#include <asm-generic/percpu.h>

/* We can use this directly for local CPU (faster). */
DECLARE_PER_CPU(unsigned long, this_cpu_off);

#endif /* !__ASSEMBLY__ */

#ifdef CONFIG_SMP

/*
 * Define the "EARLY_PER_CPU" macros.  These are used for some per_cpu
 * variables that are initialized and accessed before there are per_cpu
 * areas allocated.
 */

#define	DEFINE_EARLY_PER_CPU(_type, _name, _initvalue)			\
	DEFINE_PER_CPU(_type, _name) = _initvalue;			\
	__typeof__(_type) _name##_early_map[NR_CPUS] __initdata =	\
				{ [0 ... NR_CPUS-1] = _initvalue };	\
	__typeof__(_type) *_name##_early_ptr __refdata = _name##_early_map

#define EXPORT_EARLY_PER_CPU_SYMBOL(_name)			\
	EXPORT_PER_CPU_SYMBOL(_name)

#define DECLARE_EARLY_PER_CPU(_type, _name)			\
	DECLARE_PER_CPU(_type, _name);				\
	extern __typeof__(_type) *_name##_early_ptr;		\
	extern __typeof__(_type)  _name##_early_map[]

#define	early_per_cpu_ptr(_name) (_name##_early_ptr)
#define	early_per_cpu_map(_name, _idx) (_name##_early_map[_idx])
#define	early_per_cpu(_name, _cpu) 				\
	*(early_per_cpu_ptr(_name) ?				\
		&early_per_cpu_ptr(_name)[_cpu] :		\
		&per_cpu(_name, _cpu))

#else	/* !CONFIG_SMP */
#define	DEFINE_EARLY_PER_CPU(_type, _name, _initvalue)		\
	DEFINE_PER_CPU(_type, _name) = _initvalue

#define EXPORT_EARLY_PER_CPU_SYMBOL(_name)			\
	EXPORT_PER_CPU_SYMBOL(_name)

#define DECLARE_EARLY_PER_CPU(_type, _name)			\
	DECLARE_PER_CPU(_type, _name)

#define	early_per_cpu(_name, _cpu) per_cpu(_name, _cpu)
#define	early_per_cpu_ptr(_name) NULL
/* no early_per_cpu_map() */

#endif	/* !CONFIG_SMP */

#endif /* _ASM_X86_PERCPU_H */
