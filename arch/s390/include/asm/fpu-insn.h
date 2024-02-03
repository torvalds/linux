/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Floating Point and Vector Instructions
 *
 */

#ifndef __ASM_S390_FPU_INSN_H
#define __ASM_S390_FPU_INSN_H

#include <asm/fpu-insn-asm.h>

#ifndef __ASSEMBLY__

#include <linux/instrumented.h>
#include <asm/asm-extable.h>

asm(".include \"asm/fpu-insn-asm.h\"\n");

/*
 * Various small helper functions, which can and should be used within
 * kernel fpu code sections. Each function represents only one floating
 * point or vector instruction (except for helper functions which require
 * exception handling).
 *
 * This allows to use floating point and vector instructions like C
 * functions, which has the advantage that all supporting code, like
 * e.g. loops, can be written in easy to read C code.
 *
 * Each of the helper functions provides support for code instrumentation,
 * like e.g. KASAN. Therefore instrumentation is also covered automatically
 * when using these functions.
 *
 * In order to ensure that code generated with the helper functions stays
 * within kernel fpu sections, which are guarded with kernel_fpu_begin()
 * and kernel_fpu_end() calls, each function has a mandatory "memory"
 * barrier.
 */

static __always_inline void fpu_ld(unsigned short fpr, freg_t *reg)
{
	instrument_read(reg, sizeof(*reg));
	asm volatile("ld	 %[fpr],%[reg]\n"
		     :
		     : [fpr] "I" (fpr), [reg] "Q" (reg->ui)
		     : "memory");
}

static __always_inline void fpu_lfpc(unsigned int *fpc)
{
	instrument_read(fpc, sizeof(*fpc));
	asm volatile("lfpc	%[fpc]"
		     :
		     : [fpc] "Q" (*fpc)
		     : "memory");
}

/**
 * fpu_lfpc_safe - Load floating point control register safely.
 * @fpc: new value for floating point control register
 *
 * Load floating point control register. This may lead to an exception,
 * since a saved value may have been modified by user space (ptrace,
 * signal return, kvm registers) to an invalid value. In such a case
 * set the floating point control register to zero.
 */
static inline void fpu_lfpc_safe(unsigned int *fpc)
{
	u32 tmp;

	instrument_read(fpc, sizeof(*fpc));
	asm volatile("\n"
		"0:	lfpc	%[fpc]\n"
		"1:	nopr	%%r7\n"
		".pushsection .fixup, \"ax\"\n"
		"2:	lghi	%[tmp],0\n"
		"	sfpc	%[tmp]\n"
		"	jg	1b\n"
		".popsection\n"
		EX_TABLE(1b, 2b)
		: [tmp] "=d" (tmp)
		: [fpc] "Q" (*fpc)
		: "memory");
}

static __always_inline void fpu_std(unsigned short fpr, freg_t *reg)
{
	instrument_write(reg, sizeof(*reg));
	asm volatile("std	 %[fpr],%[reg]\n"
		     : [reg] "=Q" (reg->ui)
		     : [fpr] "I" (fpr)
		     : "memory");
}

static __always_inline void fpu_sfpc(unsigned int fpc)
{
	asm volatile("sfpc	%[fpc]"
		     :
		     : [fpc] "d" (fpc)
		     : "memory");
}

static __always_inline void fpu_stfpc(unsigned int *fpc)
{
	instrument_write(fpc, sizeof(*fpc));
	asm volatile("stfpc	%[fpc]"
		     : [fpc] "=Q" (*fpc)
		     :
		     : "memory");
}

#ifdef CONFIG_CC_IS_CLANG

#define fpu_vlm(_v1, _v3, _vxrs) do {					\
	unsigned int size = ((_v3) - (_v1) + 1) * sizeof(__vector128);	\
	struct {							\
		__vector128 _v[(_v3) - (_v1) + 1];			\
	} *_v = (void *)(_vxrs);					\
									\
	instrument_read(_v, size);					\
	asm volatile("\n"						\
		"	la	1,%[vxrs]\n"				\
		"	VLM	%[v1],%[v3],0,1\n"			\
		:							\
		: [vxrs] "R" (*_v),					\
		  [v1] "I" (_v1), [v3] "I" (_v3)			\
		: "memory", "1");					\
} while (0)

#else /* CONFIG_CC_IS_CLANG */

#define fpu_vlm(_v1, _v3, _vxrs) do {					\
	unsigned int size = ((_v3) - (_v1) + 1) * sizeof(__vector128);	\
	struct {							\
		__vector128 _v[(_v3) - (_v1) + 1];			\
	} *_v = (void *)(_vxrs);					\
									\
	instrument_read(_v, size);					\
	asm volatile("VLM	%[v1],%[v3],%O[vxrs],%R[vxrs]\n"	\
		     :							\
		     : [vxrs] "Q" (*_v),				\
		       [v1] "I" (_v1), [v3] "I" (_v3)			\
		     : "memory");					\
} while (0)

#endif /* CONFIG_CC_IS_CLANG */

#ifdef CONFIG_CC_IS_CLANG

#define fpu_vstm(_v1, _v3, _vxrs) do {					\
	unsigned int size = ((_v3) - (_v1) + 1) * sizeof(__vector128);	\
	struct {							\
		__vector128 _v[(_v3) - (_v1) + 1];			\
	} *_v = (void *)(_vxrs);					\
									\
	instrument_write(_v, size);					\
	asm volatile("\n"						\
		"	la	1,%[vxrs]\n"				\
		"	VSTM	%[v1],%[v3],0,1\n"			\
		: [vxrs] "=R" (*_v)					\
		: [v1] "I" (_v1), [v3] "I" (_v3)			\
		: "memory", "1");					\
} while (0)

#else /* CONFIG_CC_IS_CLANG */

#define fpu_vstm(_v1, _v3, _vxrs) do {					\
	unsigned int size = ((_v3) - (_v1) + 1) * sizeof(__vector128);	\
	struct {							\
		__vector128 _v[(_v3) - (_v1) + 1];			\
	} *_v = (void *)(_vxrs);					\
									\
	instrument_write(_v, size);					\
	asm volatile("VSTM	%[v1],%[v3],%O[vxrs],%R[vxrs]\n"	\
		     : [vxrs] "=Q" (*_v)				\
		     : [v1] "I" (_v1), [v3] "I" (_v3)			\
		     : "memory");					\
} while (0)

#endif /* CONFIG_CC_IS_CLANG */

#endif /* __ASSEMBLY__ */
#endif	/* __ASM_S390_FPU_INSN_H */
