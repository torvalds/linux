#ifndef __ASM_X86_XSAVE_H
#define __ASM_X86_XSAVE_H

#include <linux/types.h>
#include <asm/processor.h>

#define XSTATE_CPUID		0x0000000d

#define XSTATE_FP	0x1
#define XSTATE_SSE	0x2
#define XSTATE_YMM	0x4

#define XSTATE_FPSSE	(XSTATE_FP | XSTATE_SSE)

#define FXSAVE_SIZE	512

#define XSAVE_HDR_SIZE	    64
#define XSAVE_HDR_OFFSET    FXSAVE_SIZE

#define XSAVE_YMM_SIZE	    256
#define XSAVE_YMM_OFFSET    (XSAVE_HDR_SIZE + XSAVE_HDR_OFFSET)

/*
 * These are the features that the OS can handle currently.
 */
#define XCNTXT_MASK	(XSTATE_FP | XSTATE_SSE | XSTATE_YMM)

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

static inline int fpu_xrstor_checking(struct xsave_struct *fx)
{
	int err;

	asm volatile("1: .byte " REX_PREFIX "0x0f,0xae,0x2f\n\t"
		     "2:\n"
		     ".section .fixup,\"ax\"\n"
		     "3:  movl $-1,%[err]\n"
		     "    jmp  2b\n"
		     ".previous\n"
		     _ASM_EXTABLE(1b, 3b)
		     : [err] "=r" (err)
		     : "D" (fx), "m" (*fx), "a" (-1), "d" (-1), "0" (0)
		     : "memory");

	return err;
}

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

	__asm__ __volatile__("1: .byte " REX_PREFIX "0x0f,0xae,0x27\n"
			     "2:\n"
			     ".section .fixup,\"ax\"\n"
			     "3:  movl $-1,%[err]\n"
			     "    jmp  2b\n"
			     ".previous\n"
			     _ASM_EXTABLE(1b,3b)
			     : [err] "=r" (err)
			     : "D" (buf), "a" (-1), "d" (-1), "0" (0)
			     : "memory");
	return err;
}

static inline int xrestore_user(struct xsave_struct __user *buf, u64 mask)
{
	int err;
	struct xsave_struct *xstate = ((__force struct xsave_struct *)buf);
	u32 lmask = mask;
	u32 hmask = mask >> 32;

	__asm__ __volatile__("1: .byte " REX_PREFIX "0x0f,0xae,0x2f\n"
			     "2:\n"
			     ".section .fixup,\"ax\"\n"
			     "3:  movl $-1,%[err]\n"
			     "    jmp  2b\n"
			     ".previous\n"
			     _ASM_EXTABLE(1b,3b)
			     : [err] "=r" (err)
			     : "D" (xstate), "a" (lmask), "d" (hmask), "0" (0)
			     : "memory");	/* memory required? */
	return err;
}

static inline void xrstor_state(struct xsave_struct *fx, u64 mask)
{
	u32 lmask = mask;
	u32 hmask = mask >> 32;

	asm volatile(".byte " REX_PREFIX "0x0f,0xae,0x2f\n\t"
		     : : "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
		     :   "memory");
}

static inline void xsave_state(struct xsave_struct *fx, u64 mask)
{
	u32 lmask = mask;
	u32 hmask = mask >> 32;

	asm volatile(".byte " REX_PREFIX "0x0f,0xae,0x27\n\t"
		     : : "D" (fx), "m" (*fx), "a" (lmask), "d" (hmask)
		     :   "memory");
}

static inline void fpu_xsave(struct fpu *fpu)
{
	/* This, however, we can work around by forcing the compiler to select
	   an addressing mode that doesn't require extended registers. */
	alternative_input(
		".byte " REX_PREFIX "0x0f,0xae,0x27",
		".byte " REX_PREFIX "0x0f,0xae,0x37",
		X86_FEATURE_XSAVEOPT,
		[fx] "D" (&fpu->state->xsave), "a" (-1), "d" (-1) :
		"memory");
}
#endif
