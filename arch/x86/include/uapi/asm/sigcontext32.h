#ifndef _ASM_X86_SIGCONTEXT32_H
#define _ASM_X86_SIGCONTEXT32_H

/* Signal context definitions for compat 32-bit programs: */

#include <linux/types.h>

#include <asm/sigcontext.h>

/* 10-byte legacy floating point register: */
struct _fpreg {
	unsigned short			significand[4];
	unsigned short			exponent;
};

/* 16-byte floating point register: */
struct _fpxreg {
	unsigned short			significand[4];
	unsigned short			exponent;
	unsigned short			padding[3];
};

/* 16-byte XMM vector register: */
struct _xmmreg {
	__u32	element[4];
};

#define X86_FXSR_MAGIC			0x0000

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
	unsigned short			status;
	unsigned short			magic;		/* 0xffff: regular FPU data only */
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
       unsigned short			gs, __gsh;
       unsigned short			fs, __fsh;
       unsigned short			es, __esh;
       unsigned short			ds, __dsh;
       unsigned int			di;
       unsigned int			si;
       unsigned int			bp;
       unsigned int			sp;
       unsigned int			bx;
       unsigned int			dx;
       unsigned int			cx;
       unsigned int			ax;
       unsigned int			trapno;
       unsigned int			err;
       unsigned int			ip;
       unsigned short			cs, __csh;
       unsigned int			flags;
       unsigned int			sp_at_signal;
       unsigned short			ss, __ssh;
       unsigned int			fpstate;	/* Pointer to 'struct _fpstate_ia32' */
       unsigned int			oldmask;
       unsigned int			cr2;
};

#endif /* _ASM_X86_SIGCONTEXT32_H */
