/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 1994 - 2000  Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */

#ifndef __SIGNAL_COMMON_H
#define __SIGNAL_COMMON_H

/*
 * handle hardware context
 */
extern int setup_sigcontext(struct pt_regs *, struct sigcontext __user *);
extern int restore_sigcontext(struct pt_regs *, struct sigcontext __user *);

/*
 * Determine which stack to use..
 */
extern void __user *get_sigframe(struct k_sigaction *ka, struct pt_regs *regs,
				 size_t frame_size);
/*
 * install trampoline code to get back from the sig handler
 */
extern int install_sigtramp(unsigned int __user *tramp, unsigned int syscall);

#endif	/* __SIGNAL_COMMON_H */
