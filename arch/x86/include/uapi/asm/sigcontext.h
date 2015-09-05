#ifndef _UAPI_ASM_X86_SIGCONTEXT_H
#define _UAPI_ASM_X86_SIGCONTEXT_H

/*
 * Linux signal context definitions. The sigcontext includes a complex hierarchy of CPU
 * and FPU state, available to user-space (on the stack) when a signal handler is
 * executed.
 *
 * As over the years this ABI grew from its very simple roots towards supporting more and
 * more CPU state organically, some of the details (which were rather clever hacks back
 * in the days) became a bit quirky by today.
 *
 * The current ABI includes flexible provisions for future extensions, so we won't have
 * to grow new quirks for quite some time. Promise!
 */

#include <linux/compiler.h>
#include <linux/types.h>

#define FP_XSTATE_MAGIC1		0x46505853U
#define FP_XSTATE_MAGIC2		0x46505845U
#define FP_XSTATE_MAGIC2_SIZE		sizeof(FP_XSTATE_MAGIC2)

/*
 * Bytes 464..511 in the current 512-byte layout of the FXSAVE/FXRSTOR frame
 * are reserved for SW usage. On CPUs supporting XSAVE/XRSTOR, these bytes
 * are used to extend the fpstate pointer in the sigcontext, which now
 * includes the extended state information along with fpstate information.
 *
 * If sw_reserved.magic1 == FP_XSTATE_MAGIC1 then there's a sw_reserved.extended_size
 * bytes large extended context area present. (The last 32-bit word of this extended
 * area (at the fpstate+extended_size-FP_XSTATE_MAGIC2_SIZE address) is set to
 * FP_XSTATE_MAGIC2 so that you can sanity check your size calculations.)
 *
 * This extended area typically grows with newer CPUs that have larger and larger
 * XSAVE areas.
 */
struct _fpx_sw_bytes {
	/* If set to FP_XSTATE_MAGIC1 then this is an xstate context. 0 if a legacy frame. */
	__u32				magic1;

	/*
	 * Total size of the fpstate area:
	 *
	 *  - if magic1 == 0 then it's sizeof(struct _fpstate)
	 *  - if magic1 == FP_XSTATE_MAGIC1 then it's sizeof(struct _xstate) plus extensions (if any)
	 */
	__u32				extended_size;

	/*
	 * Feature bit mask (including FP/SSE/extended state) that is present
	 * in the memory layout:
	 */
	__u64				xfeatures;

	/*
	 * Actual XSAVE state size, based on the xfeatures saved in the layout.
	 * 'extended_size' is greater than 'xstate_size':
	 */
	__u32				xstate_size;

	/* For future use: */
	__u32				padding[7];
};

#ifdef __i386__
/*
 * As documented in the iBCS2 standard:
 *
 * The first part of "struct _fpstate" is just the normal i387
 * hardware setup, the extra "status" word is used to save the
 * coprocessor status word before entering the handler.
 *
 * The FPU state data structure has had to grow to accommodate the
 * extended FPU state required by the Streaming SIMD Extensions.
 * There is no documented standard to accomplish this at the moment.
 */

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

/* 16-byte XMM register: */
struct _xmmreg {
	unsigned long			element[4];
};

#define X86_FXSR_MAGIC			0x0000

struct _fpstate {
	/* Legacy FPU environment: */
	unsigned long			cw;
	unsigned long			sw;
	unsigned long			tag;
	unsigned long			ipoff;
	unsigned long			cssel;
	unsigned long			dataoff;
	unsigned long			datasel;
	struct _fpreg			_st[8];
	unsigned short			status;
	unsigned short			magic;		/* 0xffff: regular FPU data only */
							/* 0x0000: FXSR FPU data */

	/* FXSR FPU environment */
	unsigned long			_fxsr_env[6];	/* FXSR FPU env is ignored */
	unsigned long			mxcsr;
	unsigned long			reserved;
	struct _fpxreg			_fxsr_st[8];	/* FXSR FPU reg data is ignored */
	struct _xmmreg			_xmm[8];	/* First 8 XMM registers */
	unsigned long			padding1[44];	/* Second 8 XMM registers plus padding */

	union {
		unsigned long		padding2[12];
		struct _fpx_sw_bytes	sw_reserved;	/* Potential extended state is encoded here */
	};
};

# ifndef __KERNEL__
/*
 * User-space might still rely on the old definition:
 */
struct sigcontext {
	unsigned short			gs, __gsh;
	unsigned short			fs, __fsh;
	unsigned short			es, __esh;
	unsigned short			ds, __dsh;
	unsigned long			edi;
	unsigned long			esi;
	unsigned long			ebp;
	unsigned long			esp;
	unsigned long			ebx;
	unsigned long			edx;
	unsigned long			ecx;
	unsigned long			eax;
	unsigned long			trapno;
	unsigned long			err;
	unsigned long			eip;
	unsigned short			cs, __csh;
	unsigned long			eflags;
	unsigned long			esp_at_signal;
	unsigned short			ss, __ssh;
	struct _fpstate __user		*fpstate;
	unsigned long			oldmask;
	unsigned long			cr2;
};
# endif /* !__KERNEL__ */

#else /* __x86_64__: */

/*
 * The FXSAVE frame.
 *
 * Note1: If sw_reserved.magic1 == FP_XSTATE_MAGIC1 then the structure is
 *        larger: 'struct _xstate'. Note that 'struct _xstate' embedds
 *        'struct _fpstate' so that you can always assume the _fpstate portion
 *        exists so that you can check the magic value.
 *
 * Note2: Reserved fields may someday contain valuable data. Always save/restore
 *        them when you change signal frames.
 */
struct _fpstate {
	__u16				cwd;
	__u16				swd;
	/* Note this is not the same as the 32-bit/x87/FSAVE twd: */
	__u16				twd;
	__u16				fop;
	__u64				rip;
	__u64				rdp;
	__u32				mxcsr;
	__u32				mxcsr_mask;
	__u32				st_space[32];	/*  8x  FP registers, 16 bytes each */
	__u32				xmm_space[64];	/* 16x XMM registers, 16 bytes each */
	__u32				reserved2[12];
	union {
		__u32			reserved3[12];
		struct _fpx_sw_bytes	sw_reserved;	/* Potential extended state is encoded here */
	};
};

# ifndef __KERNEL__
/*
 * User-space might still rely on the old definition:
 */
struct sigcontext {
	__u64				r8;
	__u64				r9;
	__u64				r10;
	__u64				r11;
	__u64				r12;
	__u64				r13;
	__u64				r14;
	__u64				r15;
	__u64				rdi;
	__u64				rsi;
	__u64				rbp;
	__u64				rbx;
	__u64				rdx;
	__u64				rax;
	__u64				rcx;
	__u64				rsp;
	__u64				rip;
	__u64				eflags;		/* RFLAGS */
	__u16				cs;
	__u16				gs;
	__u16				fs;
	__u16				__pad0;
	__u64				err;
	__u64				trapno;
	__u64				oldmask;
	__u64				cr2;
	struct _fpstate __user		*fpstate;	/* Zero when no FPU context */
#  ifdef __ILP32__
	__u32				__fpstate_pad;
#  endif
	__u64				reserved1[8];
};
# endif /* !__KERNEL__ */

#endif /* __x86_64__ */

struct _header {
	__u64				xfeatures;
	__u64				reserved1[2];
	__u64				reserved2[5];
};

struct _ymmh_state {
	/* 16x YMM registers, 16 bytes each: */
	__u32				ymmh_space[64];
};

/*
 * Extended state pointed to by sigcontext::fpstate.
 *
 * In addition to the fpstate, information encoded in _xstate::xstate_hdr
 * indicates the presence of other extended state information supported
 * by the CPU and kernel:
 */
struct _xstate {
	struct _fpstate			fpstate;
	struct _header			xstate_hdr;
	struct _ymmh_state		ymmh;
	/* New processor state extensions go here: */
};

#endif /* _UAPI_ASM_X86_SIGCONTEXT_H */
