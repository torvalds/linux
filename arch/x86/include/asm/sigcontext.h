#ifndef _ASM_X86_SIGCONTEXT_H
#define _ASM_X86_SIGCONTEXT_H

#include <uapi/asm/sigcontext.h>

#ifdef __i386__
struct sigcontext {
	__u16				 gs, __gsh;
	__u16				 fs, __fsh;
	__u16				 es, __esh;
	__u16				 ds, __dsh;
	__u32				 di;
	__u32				 si;
	__u32				 bp;
	__u32				 sp;
	__u32				 bx;
	__u32				 dx;
	__u32				 cx;
	__u32				 ax;
	__u32				 trapno;
	__u32				 err;
	__u32				 ip;
	__u16				 cs, __csh;
	__u32				 flags;
	__u32				 sp_at_signal;
	__u16				 ss, __ssh;

	/*
	 * fpstate is really (struct _fpstate *) or (struct _xstate *)
	 * depending on the FP_XSTATE_MAGIC1 encoded in the SW reserved
	 * bytes of (struct _fpstate) and FP_XSTATE_MAGIC2 present at the end
	 * of extended memory layout. See comments at the definition of
	 * (struct _fpx_sw_bytes)
	 */
	void __user			*fpstate; /* Zero when no FPU/extended context */
	__u32				 oldmask;
	__u32				 cr2;
};
#else /* __x86_64__: */
struct sigcontext {
	__u64				 r8;
	__u64				 r9;
	__u64				 r10;
	__u64				 r11;
	__u64				 r12;
	__u64				 r13;
	__u64				 r14;
	__u64				 r15;
	__u64				 di;
	__u64				 si;
	__u64				 bp;
	__u64				 bx;
	__u64				 dx;
	__u64				 ax;
	__u64				 cx;
	__u64				 sp;
	__u64				 ip;
	__u64				 flags;
	__u16				 cs;
	__u16				 gs;
	__u16				 fs;
	__u16				 __pad0;
	__u64				 err;
	__u64				 trapno;
	__u64				 oldmask;
	__u64				 cr2;

	/*
	 * fpstate is really (struct _fpstate *) or (struct _xstate *)
	 * depending on the FP_XSTATE_MAGIC1 encoded in the SW reserved
	 * bytes of (struct _fpstate) and FP_XSTATE_MAGIC2 present at the end
	 * of extended memory layout. See comments at the definition of
	 * (struct _fpx_sw_bytes)
	 */
	void __user			*fpstate; /* Zero when no FPU/extended context */
	__u64				 reserved1[8];
};
#endif /* !__x86_64__ */
#endif /* _ASM_X86_SIGCONTEXT_H */
