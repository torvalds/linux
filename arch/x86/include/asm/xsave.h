#ifndef __ASM_X86_XSAVE_H
#define __ASM_X86_XSAVE_H

#include <linux/types.h>
#include <asm/processor.h>

#define XSTATE_CPUID		0x0000000d

#define XSTATE_FP		0x1
#define XSTATE_SSE		0x2
#define XSTATE_YMM		0x4
#define XSTATE_BNDREGS		0x8
#define XSTATE_BNDCSR		0x10
#define XSTATE_OPMASK		0x20
#define XSTATE_ZMM_Hi256	0x40
#define XSTATE_Hi16_ZMM		0x80

#define XSTATE_FPSSE	(XSTATE_FP | XSTATE_SSE)
/* Bit 63 of XCR0 is reserved for future expansion */
#define XSTATE_EXTEND_MASK	(~(XSTATE_FPSSE | (1ULL << 63)))

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
extern u64 pcntxt_mask;
extern u64 xstate_fx_sw_bytes[USER_XSTATE_FX_SW_WORDS];
extern struct xsave_struct *init_xstate_buf;

extern void xsave_init(void);
extern void update_regset_xstate_info(unsigned int size, u64 xstate_mask);
extern int init_fpu(struct task_struct *child);

/* These macros all use (%edi)/(%rdi) as the single memory argument. */
#define XSAVE		".byte " REX_PREFIX "0x0f,0xae,0x27"
#define XSAVEOPT	".byte " REX_PREFIX "0x0f,0xae,0x37"
#define XSAVES		".byte " REX_PREFIX "0x0f,0xc7,0x2f"
#define XRSTOR		".byte " REX_PREFIX "0x0f,0xae,0x2f"
#define XRSTORS		".byte " REX_PREFIX "0x0f,0xc7,0x1f"

#define xstate_fault	".section .fixup,\"ax\"\n"	\
			"3:  movl $-1,%[err]\n"		\
			"    jmp  2b\n"			\
			".previous\n"			\
			_ASM_EXTABLE(1b, 3b)		\
			: [err] "=r" (err)

/*
 * This function is called only during boot time when x86 caps are not set
 * up and alternative can not be used yet.
 */
static int xsave_state_booting(struct xsave_struct *fx, u64 mask)
{
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err = 0;

	WARN_ON(system_state != SYSTEM_BOOTING);

	if (boot_cpu_has(X86_FEATURE_XSAVES))
		asm volatile("1:"XSAVES"\n\t"
			"2:\n\t"
			: : "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
			:   "memory");
	else
		asm volatile("1:"XSAVE"\n\t"
			"2:\n\t"
			: : "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
			:   "memory");

	asm volatile(xstate_fault
		     : "0" (0)
		     : "memory");

	return err;
}

/*
 * This function is called only during boot time when x86 caps are not set
 * up and alternative can not be used yet.
 */
static inline int xrstor_state_booting(struct xsave_struct *fx, u64 mask)
{
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err = 0;

	WARN_ON(system_state != SYSTEM_BOOTING);

	if (boot_cpu_has(X86_FEATURE_XSAVES))
		asm volatile("1:"XRSTORS"\n\t"
			"2:\n\t"
			: : "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
			:   "memory");
	else
		asm volatile("1:"XRSTOR"\n\t"
			"2:\n\t"
			: : "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
			:   "memory");

	asm volatile(xstate_fault
		     : "0" (0)
		     : "memory");

	return err;
}

/*
 * Save processor xstate to xsave area.
 */
static inline int xsave_state(struct xsave_struct *fx, u64 mask)
{
	u32 lmask = mask;
	u32 hmask = mask >> 32;
	int err = 0;

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
		"1:"XSAVEOPT,
		X86_FEATURE_XSAVEOPT,
		"1:"XSAVES,
		X86_FEATURE_XSAVES,
		[fx] "D" (fx), "a" (lmask), "d" (hmask) :
		"memory");
	asm volatile("2:\n\t"
		     xstate_fault
		     : "0" (0)
		     : "memory");

	return err;
}

/*
 * Restore processor xstate from xsave area.
 */
static inline int xrstor_state(struct xsave_struct *fx, u64 mask)
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
		"1: " XRSTORS,
		X86_FEATURE_XSAVES,
		"D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
		: "memory");

	asm volatile("2:\n"
		     xstate_fault
		     : "0" (0)
		     : "memory");

	return err;
}

/*
 * Save xstate context for old process during context switch.
 */
static inline void fpu_xsave(struct fpu *fpu)
{
	xsave_state(&fpu->state->xsave, -1);
}

/*
 * Restore xstate context for new process during context switch.
 */
static inline int fpu_xrstor_checking(struct xsave_struct *fx)
{
	return xrstor_state(fx, -1);
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
static inline int xsave_user(struct xsave_struct __user *buf)
{
	int err;

	/*
	 * Clear the xsave header first, so that reserved fields are
	 * initialized to zero.
	 */
	err = __clear_user(&buf->xsave_hdr, sizeof(buf->xsave_hdr));
	if (unlikely(err))
		return -EFAULT;

	__asm__ __volatile__(ASM_STAC "\n"
			     "1:"XSAVE"\n"
			     "2: " ASM_CLAC "\n"
			     xstate_fault
			     : "D" (buf), "a" (-1), "d" (-1), "0" (0)
			     : "memory");
	return err;
}

/*
 * Restore xstate from user space xsave area.
 */
static inline int xrestore_user(struct xsave_struct __user *buf, u64 mask)
{
	int err = 0;
	struct xsave_struct *xstate = ((__force struct xsave_struct *)buf);
	u32 lmask = mask;
	u32 hmask = mask >> 32;

	__asm__ __volatile__(ASM_STAC "\n"
			     "1:"XRSTOR"\n"
			     "2: " ASM_CLAC "\n"
			     xstate_fault
			     : "D" (xstate), "a" (lmask), "d" (hmask), "0" (0)
			     : "memory");	/* memory required? */
	return err;
}

void *get_xsave_addr(struct xsave_struct *xsave, int xstate);
void setup_xstate_comp(void);

#endif
