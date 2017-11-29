/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _ASM_METAG_SIGCONTEXT_H
#define _ASM_METAG_SIGCONTEXT_H

#include <asm/ptrace.h>

/*
 * In a sigcontext structure we need to store the active state of the
 * user process so that it does not get trashed when we call the signal
 * handler. That not really the same as a user context that we are
 * going to store on syscall etc.
 */
struct sigcontext {
	struct user_gp_regs regs;	/* needs to be first */

	/*
	 * Catch registers describing a memory fault.
	 * If USER_GP_REGS_STATUS_CATCH_BIT is set in regs.status then catch
	 * buffers have been saved and will be replayed on sigreturn.
	 * Clear that bit to discard the catch state instead of replaying it.
	 */
	struct user_cb_regs cb;

	/*
	 * Read pipeline state. This will get restored on sigreturn.
	 */
	struct user_rp_state rp;

	unsigned long oldmask;
};

#endif
