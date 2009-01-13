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

#else /* ...!ASSEMBLY */

#include <linux/stringify.h>

#ifdef CONFIG_SMP
#define __percpu_seg_str	"%%"__stringify(__percpu_seg)":"
#define __my_cpu_offset		x86_read_percpu(this_cpu_off)
#else
#define __percpu_seg_str
#endif

#include <asm-generic/percpu.h>

/* We can use this directly for local CPU (faster). */
DECLARE_PER_CPU(unsigned long, this_cpu_off);

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
		asm(op "b %1,"__percpu_seg_str"%0"	\
		    : "+m" (var)			\
		    : "ri" ((T__)val));			\
		break;					\
	case 2:						\
		asm(op "w %1,"__percpu_seg_str"%0"	\
		    : "+m" (var)			\
		    : "ri" ((T__)val));			\
		break;					\
	case 4:						\
		asm(op "l %1,"__percpu_seg_str"%0"	\
		    : "+m" (var)			\
		    : "ri" ((T__)val));			\
		break;					\
	case 8:						\
		asm(op "q %1,"__percpu_seg_str"%0"	\
		    : "+m" (var)			\
		    : "r" ((T__)val));			\
		break;					\
	default: __bad_percpu_size();			\
	}						\
} while (0)

#define percpu_from_op(op, var)				\
({							\
	typeof(var) ret__;				\
	switch (sizeof(var)) {				\
	case 1:						\
		asm(op "b "__percpu_seg_str"%1,%0"	\
		    : "=r" (ret__)			\
		    : "m" (var));			\
		break;					\
	case 2:						\
		asm(op "w "__percpu_seg_str"%1,%0"	\
		    : "=r" (ret__)			\
		    : "m" (var));			\
		break;					\
	case 4:						\
		asm(op "l "__percpu_seg_str"%1,%0"	\
		    : "=r" (ret__)			\
		    : "m" (var));			\
		break;					\
	case 8:						\
		asm(op "q "__percpu_seg_str"%1,%0"	\
		    : "=r" (ret__)			\
		    : "m" (var));			\
		break;					\
	default: __bad_percpu_size();			\
	}						\
	ret__;						\
})

#define x86_read_percpu(var) percpu_from_op("mov", per_cpu__##var)
#define x86_write_percpu(var, val) percpu_to_op("mov", per_cpu__##var, val)
#define x86_add_percpu(var, val) percpu_to_op("add", per_cpu__##var, val)
#define x86_sub_percpu(var, val) percpu_to_op("sub", per_cpu__##var, val)
#define x86_or_percpu(var, val) percpu_to_op("or", per_cpu__##var, val)

#ifdef CONFIG_X86_64
extern void load_pda_offset(int cpu);
#else
static inline void load_pda_offset(int cpu) { }
#endif

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
