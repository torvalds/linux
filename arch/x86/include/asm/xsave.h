#ifndef __ASM_X86_XSAVE_H
#define __ASM_X86_XSAVE_H

#include <linux/types.h>
#include <asm/processor.h>
#include <asm/i387.h>

#define XSTATE_FP	0x1
#define XSTATE_SSE	0x2

#define XSTATE_FPSSE	(XSTATE_FP | XSTATE_SSE)

#define FXSAVE_SIZE	512

/*
 * These are the features that the OS can handle currently.
 */
#define XCNTXT_MASK	(XSTATE_FP | XSTATE_SSE)

#ifdef CONFIG_X86_64
#define REX_PREFIX	"0x48, "
#else
#define REX_PREFIX
#endif

extern unsigned int xstate_size;
extern u64 pcntxt_mask;
extern struct xsave_struct *init_xstate_buf;

extern void xsave_cntxt_init(void);
extern void xsave_init(void);
extern int init_fpu(struct task_struct *child);
extern int check_for_xstate(struct i387_fxsave_struct __user *buf,
			    void __user *fpstate,
			    struct _fpx_sw_bytes *sw);

static inline int xrstor_checking(struct xsave_struct *fx)
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
	__asm__ __volatile__("1: .byte " REX_PREFIX "0x0f,0xae,0x27\n"
			     "2:\n"
			     ".section .fixup,\"ax\"\n"
			     "3:  movl $-1,%[err]\n"
			     "    jmp  2b\n"
			     ".previous\n"
			     ".section __ex_table,\"a\"\n"
			     _ASM_ALIGN "\n"
			     _ASM_PTR "1b,3b\n"
			     ".previous"
			     : [err] "=r" (err)
			     : "D" (buf), "a" (-1), "d" (-1), "0" (0)
			     : "memory");
	if (unlikely(err) && __clear_user(buf, xstate_size))
		err = -EFAULT;
	/* No need to clear here because the caller clears USED_MATH */
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
			     ".section __ex_table,\"a\"\n"
			     _ASM_ALIGN "\n"
			     _ASM_PTR "1b,3b\n"
			     ".previous"
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

static inline void xsave(struct task_struct *tsk)
{
	/* This, however, we can work around by forcing the compiler to select
	   an addressing mode that doesn't require extended registers. */
	__asm__ __volatile__(".byte " REX_PREFIX "0x0f,0xae,0x27"
			     : : "D" (&(tsk->thread.xstate->xsave)),
				 "a" (-1), "d"(-1) : "memory");
}
#endif
