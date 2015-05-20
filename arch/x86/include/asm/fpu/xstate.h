#ifndef __ASM_X86_XSAVE_H
#define __ASM_X86_XSAVE_H

#include <linux/types.h>
#include <asm/processor.h>
#include <linux/uaccess.h>

/* Bit 63 of XCR0 is reserved for future expansion */
#define XSTATE_EXTEND_MASK	(~(XSTATE_FPSSE | (1ULL << 63)))

#define XSTATE_CPUID		0x0000000d

#define FXSAVE_SIZE	512

#define XSAVE_HDR_SIZE	    64
#define XSAVE_HDR_OFFSET    FXSAVE_SIZE

#define XSAVE_YMM_SIZE	    256
#define XSAVE_YMM_OFFSET    (XSAVE_HDR_SIZE + XSAVE_HDR_OFFSET)

/* Supported features which support lazy state saving */
#define XSTATE_LAZY	(XSTATE_FP | XSTATE_SSE | XSTATE_YMM		      \
			| XSTATE_OPMASK | XSTATE_ZMM_Hi256 | XSTATE_Hi16_ZMM)

/* Supported features which require eager state saving */
#define XSTATE_EAGER	(XSTATE_BNDREGS | XSTATE_BNDCSR)

/* All currently supported features */
#define XCNTXT_MASK	(XSTATE_LAZY | XSTATE_EAGER)

#ifdef CONFIG_X86_64
#define REX_PREFIX	"0x48, "
#else
#define REX_PREFIX
#endif

extern unsigned int xstate_size;
extern u64 xfeatures_mask;
extern u64 xstate_fx_sw_bytes[USER_XSTATE_FX_SW_WORDS];

extern void update_regset_xstate_info(unsigned int size, u64 xstate_mask);

/* These macros all use (%edi)/(%rdi) as the single memory argument. */
#define XSAVE		".byte " REX_PREFIX "0x0f,0xae,0x27"
#define XSAVEOPT	".byte " REX_PREFIX "0x0f,0xae,0x37"
#define XSAVES		".byte " REX_PREFIX "0x0f,0xc7,0x2f"
#define XRSTOR		".byte " REX_PREFIX "0x0f,0xae,0x2f"
#define XRSTORS		".byte " REX_PREFIX "0x0f,0xc7,0x1f"

/* xstate instruction fault handler: */
#define xstate_fault(__err)		\
					\
	".section .fixup,\"ax\"\n"	\
					\
	"3:  movl $-1,%[err]\n"		\
	"    jmp  2b\n"			\
					\
	".previous\n"			\
					\
	_ASM_EXTABLE(1b, 3b)		\
	: [err] "=r" (__err)

/*
 * This function is called only during boot time when x86 caps are not set
 * up and alternative can not be used yet.
 */
static inline int copy_xregs_to_kernel_booting(struct xregs_state *fx)
{
	u64 mask = -1;
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err = 0;

	WARN_ON(system_state != SYSTEM_BOOTING);

	if (boot_cpu_has(X86_FEATURE_XSAVES))
		asm volatile("1:"XSAVES"\n\t"
			"2:\n\t"
			     xstate_fault(err)
			: "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
			:   "memory");
	else
		asm volatile("1:"XSAVE"\n\t"
			"2:\n\t"
			     xstate_fault(err)
			: "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
			:   "memory");
	return err;
}

/*
 * This function is called only during boot time when x86 caps are not set
 * up and alternative can not be used yet.
 */
static inline int copy_kernel_to_xregs_booting(struct xregs_state *fx, u64 mask)
{
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err = 0;

	WARN_ON(system_state != SYSTEM_BOOTING);

	if (boot_cpu_has(X86_FEATURE_XSAVES))
		asm volatile("1:"XRSTORS"\n\t"
			"2:\n\t"
			     xstate_fault(err)
			: "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
			:   "memory");
	else
		asm volatile("1:"XRSTOR"\n\t"
			"2:\n\t"
			     xstate_fault(err)
			: "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
			:   "memory");
	return err;
}

/*
 * Save processor xstate to xsave area.
 */
static inline int copy_xregs_to_kernel(struct xregs_state *fx)
{
	u64 mask = -1;
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err = 0;

	WARN_ON(!alternatives_patched);

	/*
	 * If xsaves is enabled, xsaves replaces xsaveopt because
	 * it supports compact format and supervisor states in addition to
	 * modified optimization in xsaveopt.
	 *
	 * Otherwise, if xsaveopt is enabled, xsaveopt replaces xsave
	 * because xsaveopt supports modified optimization which is not
	 * supported by xsave.
	 *
	 * If none of xsaves and xsaveopt is enabled, use xsave.
	 */
	alternative_input_2(
		"1:"XSAVE,
		XSAVEOPT,
		X86_FEATURE_XSAVEOPT,
		XSAVES,
		X86_FEATURE_XSAVES,
		[fx] "D" (fx), "a" (lmask), "d" (hmask) :
		"memory");
	asm volatile("2:\n\t"
		     xstate_fault(err)
		     : "0" (0)
		     : "memory");

	return err;
}

/*
 * Restore processor xstate from xsave area.
 */
static inline int copy_kernel_to_xregs(struct xregs_state *fx, u64 mask)
{
	int err = 0;
	u32 lmask = mask;
	u32 hmask = mask >> 32;

	/*
	 * Use xrstors to restore context if it is enabled. xrstors supports
	 * compacted format of xsave area which is not supported by xrstor.
	 */
	alternative_input(
		"1: " XRSTOR,
		XRSTORS,
		X86_FEATURE_XSAVES,
		"D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
		: "memory");

	asm volatile("2:\n"
		     xstate_fault(err)
		     : "0" (0)
		     : "memory");

	return err;
}

/*
 * Save xstate to user space xsave area.
 *
 * We don't use modified optimization because xrstor/xrstors might track
 * a different application.
 *
 * We don't use compacted format xsave area for
 * backward compatibility for old applications which don't understand
 * compacted format of xsave area.
 */
static inline int copy_xregs_to_user(struct xregs_state __user *buf)
{
	int err;

	/*
	 * Clear the xsave header first, so that reserved fields are
	 * initialized to zero.
	 */
	err = __clear_user(&buf->header, sizeof(buf->header));
	if (unlikely(err))
		return -EFAULT;

	__asm__ __volatile__(ASM_STAC "\n"
			     "1:"XSAVE"\n"
			     "2: " ASM_CLAC "\n"
			     xstate_fault(err)
			     : "D" (buf), "a" (-1), "d" (-1), "0" (0)
			     : "memory");
	return err;
}

/*
 * Restore xstate from user space xsave area.
 */
static inline int copy_user_to_xregs(struct xregs_state __user *buf, u64 mask)
{
	int err = 0;
	struct xregs_state *xstate = ((__force struct xregs_state *)buf);
	u32 lmask = mask;
	u32 hmask = mask >> 32;

	__asm__ __volatile__(ASM_STAC "\n"
			     "1:"XRSTOR"\n"
			     "2: " ASM_CLAC "\n"
			     xstate_fault(err)
			     : "D" (xstate), "a" (lmask), "d" (hmask), "0" (0)
			     : "memory");	/* memory required? */
	return err;
}

void *get_xsave_addr(struct xregs_state *xsave, int xstate);

#endif
