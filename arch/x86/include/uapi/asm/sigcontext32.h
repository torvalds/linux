#ifndef _ASM_X86_SIGCONTEXT32_H
#define _ASM_X86_SIGCONTEXT32_H

/* Signal context definitions for compat 32-bit programs: */

#include <asm/sigcontext.h>

/* FXSAVE frame: FSAVE frame with extensions */
struct _fpstate_ia32 {
	/* Regular FPU environment: */
	__u32				cw;
	__u32				sw;
	__u32				tag;		/* Not compatible with the 64-bit frame */
	__u32				ipoff;
	__u32				cssel;
	__u32				dataoff;
	__u32				datasel;
	struct _fpreg			_st[8];
	__u16				status;
	__u16				magic;		/* 0xffff: regular FPU data only */
							/* 0x0000: FXSR data */

	/* Extended FXSR FPU environment: */
	__u32				_fxsr_env[6];
	__u32				mxcsr;
	__u32				reserved;
	struct _fpxreg			_fxsr_st[8];
	struct _xmmreg			_xmm[8];	/* The first  8 XMM registers */
	__u32				padding[44];	/* The second 8 XMM registers plus padding */
	union {
		__u32			padding2[12];
		/* Might encode xstate extensions, see asm/sigcontext.h: */
		struct _fpx_sw_bytes	sw_reserved;
	};
};

/* 32-bit compat sigcontext: */
struct sigcontext_ia32 {
       __u16				gs, __gsh;
       __u16				fs, __fsh;
       __u16				es, __esh;
       __u16				ds, __dsh;
       __u32				di;
       __u32				si;
       __u32				bp;
       __u32				sp;
       __u32				bx;
       __u32				dx;
       __u32				cx;
       __u32				ax;
       __u32				trapno;
       __u32				err;
       __u32				ip;
       __u16				cs, __csh;
       __u32				flags;
       __u32				sp_at_signal;
       __u16				ss, __ssh;
       __u32				fpstate;	/* Pointer to 'struct _fpstate_ia32' */
       __u32				oldmask;
       __u32				cr2;
};

#endif /* _ASM_X86_SIGCONTEXT32_H */
