/* 
 *    Copyright (C) 2001 Matthew Wilcox <willy at parisc-linux.org>
 *    Copyright (C) 2003 Carlos O'Donell <carlos at parisc-linux.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef _PARISC64_KERNEL_SIGNAL32_H
#define _PARISC64_KERNEL_SIGNAL32_H

#include <linux/compat.h>

typedef compat_uptr_t compat_sighandler_t;

typedef struct compat_sigaltstack {
        compat_uptr_t ss_sp;
        compat_int_t ss_flags;
        compat_size_t ss_size;
} compat_stack_t;

/* Most things should be clean enough to redefine this at will, if care
   is taken to make libc match.  */

struct compat_sigaction {
        compat_sighandler_t sa_handler;
        compat_uint_t sa_flags;
        compat_sigset_t sa_mask;               /* mask last for extensibility */
};

/* 32-bit ucontext as seen from an 64-bit kernel */
struct compat_ucontext {
        compat_uint_t uc_flags;
        compat_uptr_t uc_link;
        compat_stack_t uc_stack;        /* struct compat_sigaltstack (12 bytes)*/
        /* FIXME: Pad out to get uc_mcontext to start at an 8-byte aligned boundary */
        compat_uint_t pad[1];
        struct compat_sigcontext uc_mcontext;
        compat_sigset_t uc_sigmask;     /* mask last for extensibility */
};

/* ELF32 signal handling */

struct k_sigaction32 {
	struct compat_sigaction sa;
};

typedef struct compat_siginfo {
        int si_signo;
        int si_errno;
        int si_code;

        union {
                int _pad[((128/sizeof(int)) - 3)];

                /* kill() */
                struct {
                        unsigned int _pid;      /* sender's pid */
                        unsigned int _uid;      /* sender's uid */
                } _kill;

                /* POSIX.1b timers */
                struct {
                        compat_timer_t _tid;            /* timer id */
                        int _overrun;           /* overrun count */
                        char _pad[sizeof(unsigned int) - sizeof(int)];
                        compat_sigval_t _sigval;        /* same as below */
                        int _sys_private;       /* not to be passed to user */
                } _timer;

                /* POSIX.1b signals */
                struct {
                        unsigned int _pid;      /* sender's pid */
                        unsigned int _uid;      /* sender's uid */
                        compat_sigval_t _sigval;
                } _rt;

                /* SIGCHLD */
                struct {
                        unsigned int _pid;      /* which child */
                        unsigned int _uid;      /* sender's uid */
                        int _status;            /* exit code */
                        compat_clock_t _utime;
                        compat_clock_t _stime;
                } _sigchld;

                /* SIGILL, SIGFPE, SIGSEGV, SIGBUS */
                struct {
                        unsigned int _addr;     /* faulting insn/memory ref. */
                } _sigfault;

                /* SIGPOLL */
                struct {
                        int _band;      /* POLL_IN, POLL_OUT, POLL_MSG */
                        int _fd;
                } _sigpoll;
        } _sifields;
} compat_siginfo_t;

int copy_siginfo_to_user32 (compat_siginfo_t __user *to, siginfo_t *from);
int copy_siginfo_from_user32 (siginfo_t *to, compat_siginfo_t __user *from);

/* In a deft move of uber-hackery, we decide to carry the top half of all
 * 64-bit registers in a non-portable, non-ABI, hidden structure.
 * Userspace can read the hidden structure if it *wants* but is never
 * guaranteed to be in the same place. In fact the uc_sigmask from the
 * ucontext_t structure may push the hidden register file downards
 */
struct compat_regfile {
        /* Upper half of all the 64-bit registers that were truncated
           on a copy to a 32-bit userspace */
        compat_int_t rf_gr[32];
        compat_int_t rf_iasq[2];
        compat_int_t rf_iaoq[2];
        compat_int_t rf_sar;
};

#define COMPAT_SIGRETURN_TRAMP 4
#define COMPAT_SIGRESTARTBLOCK_TRAMP 5
#define COMPAT_TRAMP_SIZE (COMPAT_SIGRETURN_TRAMP + \
				COMPAT_SIGRESTARTBLOCK_TRAMP)

struct compat_rt_sigframe {
        /* XXX: Must match trampoline size in arch/parisc/kernel/signal.c
                Secondary to that it must protect the ERESTART_RESTARTBLOCK
                trampoline we left on the stack (we were bad and didn't
                change sp so we could run really fast.) */
        compat_uint_t tramp[COMPAT_TRAMP_SIZE];
        compat_siginfo_t info;
        struct compat_ucontext uc;
        /* Hidden location of truncated registers, *must* be last. */
        struct compat_regfile regs;
};

/*
 * The 32-bit ABI wants at least 48 bytes for a function call frame:
 * 16 bytes for arg0-arg3, and 32 bytes for magic (the only part of
 * which Linux/parisc uses is sp-20 for the saved return pointer...)
 * Then, the stack pointer must be rounded to a cache line (64 bytes).
 */
#define SIGFRAME32              64
#define FUNCTIONCALLFRAME32     48
#define PARISC_RT_SIGFRAME_SIZE32 (((sizeof(struct compat_rt_sigframe) + FUNCTIONCALLFRAME32) + SIGFRAME32) & -SIGFRAME32)

void sigset_32to64(sigset_t *s64, compat_sigset_t *s32);
void sigset_64to32(compat_sigset_t *s32, sigset_t *s64);
int do_sigaltstack32 (const compat_stack_t __user *uss32, 
		compat_stack_t __user *uoss32, unsigned long sp);
long restore_sigcontext32(struct compat_sigcontext __user *sc, 
		struct compat_regfile __user *rf,
		struct pt_regs *regs);
long setup_sigcontext32(struct compat_sigcontext __user *sc, 
		struct compat_regfile __user *rf,
		struct pt_regs *regs, int in_syscall);

#endif
