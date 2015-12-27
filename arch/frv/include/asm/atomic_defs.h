
#include <asm/spr-regs.h>

#ifdef __ATOMIC_LIB__

#ifdef CONFIG_FRV_OUTOFLINE_ATOMIC_OPS

#define ATOMIC_QUALS
#define ATOMIC_EXPORT(x)	EXPORT_SYMBOL(x)

#else /* !OUTOFLINE && LIB */

#define ATOMIC_OP_RETURN(op)
#define ATOMIC_FETCH_OP(op)

#endif /* OUTOFLINE */

#else /* !__ATOMIC_LIB__ */

#define ATOMIC_EXPORT(x)

#ifdef CONFIG_FRV_OUTOFLINE_ATOMIC_OPS

#define ATOMIC_OP_RETURN(op)						\
extern int __atomic_##op##_return(int i, int *v);			\
extern long long __atomic64_##op##_return(long long i, long long *v);

#define ATOMIC_FETCH_OP(op)						\
extern int __atomic32_fetch_##op(int i, int *v);			\
extern long long __atomic64_fetch_##op(long long i, long long *v);

#else /* !OUTOFLINE && !LIB */

#define ATOMIC_QUALS	static inline

#endif /* OUTOFLINE */
#endif /* __ATOMIC_LIB__ */


/*
 * Note on the 64 bit inline asm variants...
 *
 * CSTD is a conditional instruction and needs a constrained memory reference.
 * Normally 'U' provides the correct constraints for conditional instructions
 * and this is used for the 32 bit version, however 'U' does not appear to work
 * for 64 bit values (gcc-4.9)
 *
 * The exact constraint is that conditional instructions cannot deal with an
 * immediate displacement in the memory reference, so what we do is we read the
 * address through a volatile cast into a local variable in order to insure we
 * _have_ to compute the correct address without displacement. This allows us
 * to use the regular 'm' for the memory address.
 *
 * Furthermore, the %Ln operand, which prints the low word register (r+1),
 * really only works for registers, this means we cannot allow immediate values
 * for the 64 bit versions -- like we do for the 32 bit ones.
 *
 */

#ifndef ATOMIC_OP_RETURN
#define ATOMIC_OP_RETURN(op)						\
ATOMIC_QUALS int __atomic_##op##_return(int i, int *v)			\
{									\
	int val;							\
									\
	asm volatile(							\
	    "0:						\n"		\
	    "	orcc		gr0,gr0,gr0,icc3	\n"		\
	    "	ckeq		icc3,cc7		\n"		\
	    "	ld.p		%M0,%1			\n"		\
	    "	orcr		cc7,cc7,cc3		\n"		\
	    "   "#op"%I2	%1,%2,%1		\n"		\
	    "	cst.p		%1,%M0		,cc3,#1	\n"		\
	    "	corcc		gr29,gr29,gr0	,cc3,#1	\n"		\
	    "	beq		icc3,#0,0b		\n"		\
	    : "+U"(*v), "=&r"(val)					\
	    : "NPr"(i)							\
	    : "memory", "cc7", "cc3", "icc3"				\
	    );								\
									\
	return val;							\
}									\
ATOMIC_EXPORT(__atomic_##op##_return);					\
									\
ATOMIC_QUALS long long __atomic64_##op##_return(long long i, long long *v)	\
{									\
	long long *__v = READ_ONCE(v);					\
	long long val;							\
									\
	asm volatile(							\
	    "0:						\n"		\
	    "	orcc		gr0,gr0,gr0,icc3	\n"		\
	    "	ckeq		icc3,cc7		\n"		\
	    "	ldd.p		%M0,%1			\n"		\
	    "	orcr		cc7,cc7,cc3		\n"		\
	    "   "#op"cc		%L1,%L2,%L1,icc0	\n"		\
	    "   "#op"x		%1,%2,%1,icc0		\n"		\
	    "	cstd.p		%1,%M0		,cc3,#1	\n"		\
	    "	corcc		gr29,gr29,gr0	,cc3,#1	\n"		\
	    "	beq		icc3,#0,0b		\n"		\
	    : "+m"(*__v), "=&e"(val)					\
	    : "e"(i)							\
	    : "memory", "cc7", "cc3", "icc0", "icc3"			\
	    );								\
									\
	return val;							\
}									\
ATOMIC_EXPORT(__atomic64_##op##_return);
#endif

#ifndef ATOMIC_FETCH_OP
#define ATOMIC_FETCH_OP(op)						\
ATOMIC_QUALS int __atomic32_fetch_##op(int i, int *v)			\
{									\
	int old, tmp;							\
									\
	asm volatile(							\
		"0:						\n"	\
		"	orcc		gr0,gr0,gr0,icc3	\n"	\
		"	ckeq		icc3,cc7		\n"	\
		"	ld.p		%M0,%1			\n"	\
		"	orcr		cc7,cc7,cc3		\n"	\
		"	"#op"%I3	%1,%3,%2		\n"	\
		"	cst.p		%2,%M0		,cc3,#1	\n"	\
		"	corcc		gr29,gr29,gr0	,cc3,#1	\n"	\
		"	beq		icc3,#0,0b		\n"	\
		: "+U"(*v), "=&r"(old), "=r"(tmp)			\
		: "NPr"(i)						\
		: "memory", "cc7", "cc3", "icc3"			\
		);							\
									\
	return old;							\
}									\
ATOMIC_EXPORT(__atomic32_fetch_##op);					\
									\
ATOMIC_QUALS long long __atomic64_fetch_##op(long long i, long long *v)	\
{									\
	long long *__v = READ_ONCE(v);					\
	long long old, tmp;						\
									\
	asm volatile(							\
		"0:						\n"	\
		"	orcc		gr0,gr0,gr0,icc3	\n"	\
		"	ckeq		icc3,cc7		\n"	\
		"	ldd.p		%M0,%1			\n"	\
		"	orcr		cc7,cc7,cc3		\n"	\
		"	"#op"		%L1,%L3,%L2		\n"	\
		"	"#op"		%1,%3,%2		\n"	\
		"	cstd.p		%2,%M0		,cc3,#1	\n"	\
		"	corcc		gr29,gr29,gr0	,cc3,#1	\n"	\
		"	beq		icc3,#0,0b		\n"	\
		: "+m"(*__v), "=&e"(old), "=e"(tmp)			\
		: "e"(i)						\
		: "memory", "cc7", "cc3", "icc3"			\
		);							\
									\
	return old;							\
}									\
ATOMIC_EXPORT(__atomic64_fetch_##op);
#endif

ATOMIC_FETCH_OP(or)
ATOMIC_FETCH_OP(and)
ATOMIC_FETCH_OP(xor)

ATOMIC_OP_RETURN(add)
ATOMIC_OP_RETURN(sub)

#undef ATOMIC_FETCH_OP
#undef ATOMIC_OP_RETURN
#undef ATOMIC_QUALS
#undef ATOMIC_EXPORT
