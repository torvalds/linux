#ifndef _ASM_X86_SIGCONTEXT32_H
#define _ASM_X86_SIGCONTEXT32_H

/* Signal context definitions for compat 32-bit programs: */

#include <asm/sigcontext.h>

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
       __u32				fpstate;	/* Pointer to 'struct _fpstate_32' */
       __u32				oldmask;
       __u32				cr2;
};

#endif /* _ASM_X86_SIGCONTEXT32_H */
