// SPDX-License-Identifier: GPL-2.0
/* windows.c: Routines to deal with register window management
 *            at the C-code level.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/smp.h>

#include <asm/cacheflush.h>
#include <linux/uaccess.h>

#include "kernel.h"

/* Do save's until all user register windows are out of the cpu. */
void flush_user_windows(void)
{
	register int ctr asm("g5");

	ctr = 0;
	__asm__ __volatile__(
		"\n1:\n\t"
		"ld	[%%g6 + %2], %%g4\n\t"
		"orcc	%%g0, %%g4, %%g0\n\t"
		"add	%0, 1, %0\n\t"
		"bne	1b\n\t"
		" save	%%sp, -64, %%sp\n"
		"2:\n\t"
		"subcc	%0, 1, %0\n\t"
		"bne	2b\n\t"
		" restore %%g0, %%g0, %%g0\n"
	: "=&r" (ctr)
	: "0" (ctr),
	  "i" ((const unsigned long)TI_UWINMASK)
	: "g4", "cc");
}

static inline void shift_window_buffer(int first_win, int last_win, struct thread_info *tp)
{
	int i;

	for(i = first_win; i < last_win; i++) {
		tp->rwbuf_stkptrs[i] = tp->rwbuf_stkptrs[i+1];
		memcpy(&tp->reg_window[i], &tp->reg_window[i+1], sizeof(struct reg_window32));
	}
}

/* Place as many of the user's current register windows 
 * on the stack that we can.  Even if the %sp is unaligned
 * we still copy the window there, the only case that we don't
 * succeed is if the %sp points to a bum mapping altogether.
 * setup_frame() and do_sigreturn() use this before shifting
 * the user stack around.  Future instruction and hardware
 * bug workaround routines will need this functionality as
 * well.
 */
void synchronize_user_stack(void)
{
	struct thread_info *tp = current_thread_info();
	int window;

	flush_user_windows();
	if(!tp->w_saved)
		return;

	/* Ok, there is some dirty work to do. */
	for(window = tp->w_saved - 1; window >= 0; window--) {
		unsigned long sp = tp->rwbuf_stkptrs[window];

		/* Ok, let it rip. */
		if (copy_to_user((char __user *) sp, &tp->reg_window[window],
				 sizeof(struct reg_window32)))
			continue;

		shift_window_buffer(window, tp->w_saved - 1, tp);
		tp->w_saved--;
	}
}

#if 0
/* An optimization. */
static inline void copy_aligned_window(void *dest, const void *src)
{
	__asm__ __volatile__("ldd [%1], %%g2\n\t"
			     "ldd [%1 + 0x8], %%g4\n\t"
			     "std %%g2, [%0]\n\t"
			     "std %%g4, [%0 + 0x8]\n\t"
			     "ldd [%1 + 0x10], %%g2\n\t"
			     "ldd [%1 + 0x18], %%g4\n\t"
			     "std %%g2, [%0 + 0x10]\n\t"
			     "std %%g4, [%0 + 0x18]\n\t"
			     "ldd [%1 + 0x20], %%g2\n\t"
			     "ldd [%1 + 0x28], %%g4\n\t"
			     "std %%g2, [%0 + 0x20]\n\t"
			     "std %%g4, [%0 + 0x28]\n\t"
			     "ldd [%1 + 0x30], %%g2\n\t"
			     "ldd [%1 + 0x38], %%g4\n\t"
			     "std %%g2, [%0 + 0x30]\n\t"
			     "std %%g4, [%0 + 0x38]\n\t" : :
			     "r" (dest), "r" (src) :
			     "g2", "g3", "g4", "g5");
}
#endif

/* Try to push the windows in a threads window buffer to the
 * user stack.  Unaligned %sp's are not allowed here.
 */

void try_to_clear_window_buffer(struct pt_regs *regs, int who)
{
	struct thread_info *tp = current_thread_info();
	int window;

	flush_user_windows();
	for(window = 0; window < tp->w_saved; window++) {
		unsigned long sp = tp->rwbuf_stkptrs[window];

		if ((sp & 7) ||
		    copy_to_user((char __user *) sp, &tp->reg_window[window],
				 sizeof(struct reg_window32))) {
			force_exit_sig(SIGILL);
			return;
		}
	}
	tp->w_saved = 0;
}
