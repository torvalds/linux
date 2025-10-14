/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Floating Point and Vector Instructions
 *
 */

#ifndef __ASM_S390_FPU_INSN_H
#define __ASM_S390_FPU_INSN_H

#include <asm/fpu-insn-asm.h>

#ifndef __ASSEMBLER__

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

static __always_inline void fpu_cefbr(u8 f1, s32 val)
{
	asm volatile("cefbr	%[f1],%[val]"
		     :
		     : [f1] "I" (f1), [val] "d" (val)
		     : "memory");
}

static __always_inline unsigned long fpu_cgebr(u8 f2, u8 mode)
{
	unsigned long val;

	asm volatile("cgebr	%[val],%[mode],%[f2]"
		     : [val] "=d" (val)
		     : [f2] "I" (f2), [mode] "I" (mode)
		     : "memory");
	return val;
}

static __always_inline void fpu_debr(u8 f1, u8 f2)
{
	asm volatile("debr	%[f1],%[f2]"
		     :
		     : [f1] "I" (f1), [f2] "I" (f2)
		     : "memory");
}

static __always_inline void fpu_ld(unsigned short fpr, freg_t *reg)
{
	instrument_read(reg, sizeof(*reg));
	asm volatile("ld	 %[fpr],%[reg]"
		     :
		     : [fpr] "I" (fpr), [reg] "Q" (reg->ui)
		     : "memory");
}

static __always_inline void fpu_ldgr(u8 f1, u32 val)
{
	asm volatile("ldgr	%[f1],%[val]"
		     :
		     : [f1] "I" (f1), [val] "d" (val)
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
	instrument_read(fpc, sizeof(*fpc));
	asm_inline volatile(
		"	lfpc	%[fpc]\n"
		"0:	nopr	%%r7\n"
		EX_TABLE_FPC(0b, 0b)
		:
		: [fpc] "Q" (*fpc)
		: "memory");
}

static __always_inline void fpu_std(unsigned short fpr, freg_t *reg)
{
	instrument_write(reg, sizeof(*reg));
	asm volatile("std	 %[fpr],%[reg]"
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

static __always_inline void fpu_vab(u8 v1, u8 v2, u8 v3)
{
	asm volatile("VAB	%[v1],%[v2],%[v3]"
		     :
		     : [v1] "I" (v1), [v2] "I" (v2), [v3] "I" (v3)
		     : "memory");
}

static __always_inline void fpu_vcksm(u8 v1, u8 v2, u8 v3)
{
	asm volatile("VCKSM	%[v1],%[v2],%[v3]"
		     :
		     : [v1] "I" (v1), [v2] "I" (v2), [v3] "I" (v3)
		     : "memory");
}

static __always_inline void fpu_vesravb(u8 v1, u8 v2, u8 v3)
{
	asm volatile("VESRAVB	%[v1],%[v2],%[v3]"
		     :
		     : [v1] "I" (v1), [v2] "I" (v2), [v3] "I" (v3)
		     : "memory");
}

static __always_inline void fpu_vgfmag(u8 v1, u8 v2, u8 v3, u8 v4)
{
	asm volatile("VGFMAG	%[v1],%[v2],%[v3],%[v4]"
		     :
		     : [v1] "I" (v1), [v2] "I" (v2), [v3] "I" (v3), [v4] "I" (v4)
		     : "memory");
}

static __always_inline void fpu_vgfmg(u8 v1, u8 v2, u8 v3)
{
	asm volatile("VGFMG	%[v1],%[v2],%[v3]"
		     :
		     : [v1] "I" (v1), [v2] "I" (v2), [v3] "I" (v3)
		     : "memory");
}

#ifdef CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS

static __always_inline void fpu_vl(u8 v1, const void *vxr)
{
	instrument_read(vxr, sizeof(__vector128));
	asm volatile("VL	%[v1],%O[vxr],,%R[vxr]"
		     :
		     : [vxr] "Q" (*(__vector128 *)vxr),
		       [v1] "I" (v1)
		     : "memory");
}

#else /* CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS */

static __always_inline void fpu_vl(u8 v1, const void *vxr)
{
	instrument_read(vxr, sizeof(__vector128));
	asm volatile(
		"	la	1,%[vxr]\n"
		"	VL	%[v1],0,,1"
		:
		: [vxr] "R" (*(__vector128 *)vxr),
		  [v1] "I" (v1)
		: "memory", "1");
}

#endif /* CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS */

static __always_inline void fpu_vleib(u8 v, s16 val, u8 index)
{
	asm volatile("VLEIB	%[v],%[val],%[index]"
		     :
		     : [v] "I" (v), [val] "K" (val), [index] "I" (index)
		     : "memory");
}

static __always_inline void fpu_vleig(u8 v, s16 val, u8 index)
{
	asm volatile("VLEIG	%[v],%[val],%[index]"
		     :
		     : [v] "I" (v), [val] "K" (val), [index] "I" (index)
		     : "memory");
}

static __always_inline u64 fpu_vlgvf(u8 v, u16 index)
{
	u64 val;

	asm volatile("VLGVF	%[val],%[v],%[index]"
		     : [val] "=d" (val)
		     : [v] "I" (v), [index] "L" (index)
		     : "memory");
	return val;
}

#ifdef CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS

static __always_inline void fpu_vll(u8 v1, u32 index, const void *vxr)
{
	unsigned int size;

	size = min(index + 1, sizeof(__vector128));
	instrument_read(vxr, size);
	asm volatile("VLL	%[v1],%[index],%O[vxr],%R[vxr]"
		     :
		     : [vxr] "Q" (*(u8 *)vxr),
		       [index] "d" (index),
		       [v1] "I" (v1)
		     : "memory");
}

#else /* CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS */

static __always_inline void fpu_vll(u8 v1, u32 index, const void *vxr)
{
	unsigned int size;

	size = min(index + 1, sizeof(__vector128));
	instrument_read(vxr, size);
	asm volatile(
		"	la	1,%[vxr]\n"
		"	VLL	%[v1],%[index],0,1"
		:
		: [vxr] "R" (*(u8 *)vxr),
		  [index] "d" (index),
		  [v1] "I" (v1)
		: "memory", "1");
}

#endif /* CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS */

#ifdef CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS

#define fpu_vlm(_v1, _v3, _vxrs)					\
({									\
	unsigned int size = ((_v3) - (_v1) + 1) * sizeof(__vector128);	\
	struct {							\
		__vector128 _v[(_v3) - (_v1) + 1];			\
	} *_v = (void *)(_vxrs);					\
									\
	instrument_read(_v, size);					\
	asm volatile("VLM	%[v1],%[v3],%O[vxrs],%R[vxrs]"		\
		     :							\
		     : [vxrs] "Q" (*_v),				\
		       [v1] "I" (_v1), [v3] "I" (_v3)			\
		     : "memory");					\
	(_v3) - (_v1) + 1;						\
})

#else /* CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS */

#define fpu_vlm(_v1, _v3, _vxrs)					\
({									\
	unsigned int size = ((_v3) - (_v1) + 1) * sizeof(__vector128);	\
	struct {							\
		__vector128 _v[(_v3) - (_v1) + 1];			\
	} *_v = (void *)(_vxrs);					\
									\
	instrument_read(_v, size);					\
	asm volatile(							\
		"	la	1,%[vxrs]\n"				\
		"	VLM	%[v1],%[v3],0,1"			\
		:							\
		: [vxrs] "R" (*_v),					\
		  [v1] "I" (_v1), [v3] "I" (_v3)			\
		: "memory", "1");					\
	(_v3) - (_v1) + 1;						\
})

#endif /* CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS */

static __always_inline void fpu_vlr(u8 v1, u8 v2)
{
	asm volatile("VLR	%[v1],%[v2]"
		     :
		     : [v1] "I" (v1), [v2] "I" (v2)
		     : "memory");
}

static __always_inline void fpu_vlvgf(u8 v, u32 val, u16 index)
{
	asm volatile("VLVGF	%[v],%[val],%[index]"
		     :
		     : [v] "I" (v), [val] "d" (val), [index] "L" (index)
		     : "memory");
}

static __always_inline void fpu_vn(u8 v1, u8 v2, u8 v3)
{
	asm volatile("VN	%[v1],%[v2],%[v3]"
		     :
		     : [v1] "I" (v1), [v2] "I" (v2), [v3] "I" (v3)
		     : "memory");
}

static __always_inline void fpu_vperm(u8 v1, u8 v2, u8 v3, u8 v4)
{
	asm volatile("VPERM	%[v1],%[v2],%[v3],%[v4]"
		     :
		     : [v1] "I" (v1), [v2] "I" (v2), [v3] "I" (v3), [v4] "I" (v4)
		     : "memory");
}

static __always_inline void fpu_vrepib(u8 v1, s16 i2)
{
	asm volatile("VREPIB	%[v1],%[i2]"
		     :
		     : [v1] "I" (v1), [i2] "K" (i2)
		     : "memory");
}

static __always_inline void fpu_vsrlb(u8 v1, u8 v2, u8 v3)
{
	asm volatile("VSRLB	%[v1],%[v2],%[v3]"
		     :
		     : [v1] "I" (v1), [v2] "I" (v2), [v3] "I" (v3)
		     : "memory");
}

#ifdef CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS

static __always_inline void fpu_vst(u8 v1, const void *vxr)
{
	instrument_write(vxr, sizeof(__vector128));
	asm volatile("VST	%[v1],%O[vxr],,%R[vxr]"
		     : [vxr] "=Q" (*(__vector128 *)vxr)
		     : [v1] "I" (v1)
		     : "memory");
}

#else /* CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS */

static __always_inline void fpu_vst(u8 v1, const void *vxr)
{
	instrument_write(vxr, sizeof(__vector128));
	asm volatile(
		"	la	1,%[vxr]\n"
		"	VST	%[v1],0,,1"
		: [vxr] "=R" (*(__vector128 *)vxr)
		: [v1] "I" (v1)
		: "memory", "1");
}

#endif /* CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS */

#ifdef CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS

static __always_inline void fpu_vstl(u8 v1, u32 index, const void *vxr)
{
	unsigned int size;

	size = min(index + 1, sizeof(__vector128));
	instrument_write(vxr, size);
	asm volatile("VSTL	%[v1],%[index],%O[vxr],%R[vxr]"
		     : [vxr] "=Q" (*(u8 *)vxr)
		     : [index] "d" (index), [v1] "I" (v1)
		     : "memory");
}

#else /* CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS */

static __always_inline void fpu_vstl(u8 v1, u32 index, const void *vxr)
{
	unsigned int size;

	size = min(index + 1, sizeof(__vector128));
	instrument_write(vxr, size);
	asm volatile(
		"	la	1,%[vxr]\n"
		"	VSTL	%[v1],%[index],0,1"
		: [vxr] "=R" (*(u8 *)vxr)
		: [index] "d" (index), [v1] "I" (v1)
		: "memory", "1");
}

#endif /* CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS */

#ifdef CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS

#define fpu_vstm(_v1, _v3, _vxrs)					\
({									\
	unsigned int size = ((_v3) - (_v1) + 1) * sizeof(__vector128);	\
	struct {							\
		__vector128 _v[(_v3) - (_v1) + 1];			\
	} *_v = (void *)(_vxrs);					\
									\
	instrument_write(_v, size);					\
	asm volatile("VSTM	%[v1],%[v3],%O[vxrs],%R[vxrs]"		\
		     : [vxrs] "=Q" (*_v)				\
		     : [v1] "I" (_v1), [v3] "I" (_v3)			\
		     : "memory");					\
	(_v3) - (_v1) + 1;						\
})

#else /* CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS */

#define fpu_vstm(_v1, _v3, _vxrs)					\
({									\
	unsigned int size = ((_v3) - (_v1) + 1) * sizeof(__vector128);	\
	struct {							\
		__vector128 _v[(_v3) - (_v1) + 1];			\
	} *_v = (void *)(_vxrs);					\
									\
	instrument_write(_v, size);					\
	asm volatile(							\
		"	la	1,%[vxrs]\n"				\
		"	VSTM	%[v1],%[v3],0,1"			\
		: [vxrs] "=R" (*_v)					\
		: [v1] "I" (_v1), [v3] "I" (_v3)			\
		: "memory", "1");					\
	(_v3) - (_v1) + 1;						\
})

#endif /* CONFIG_CC_HAS_ASM_AOR_FORMAT_FLAGS */

static __always_inline void fpu_vupllf(u8 v1, u8 v2)
{
	asm volatile("VUPLLF	%[v1],%[v2]"
		     :
		     : [v1] "I" (v1), [v2] "I" (v2)
		     : "memory");
}

static __always_inline void fpu_vx(u8 v1, u8 v2, u8 v3)
{
	asm volatile("VX	%[v1],%[v2],%[v3]"
		     :
		     : [v1] "I" (v1), [v2] "I" (v2), [v3] "I" (v3)
		     : "memory");
}

static __always_inline void fpu_vzero(u8 v)
{
	asm volatile("VZERO	%[v]"
		     :
		     : [v] "I" (v)
		     : "memory");
}

#endif /* __ASSEMBLER__ */
#endif	/* __ASM_S390_FPU_INSN_H */
