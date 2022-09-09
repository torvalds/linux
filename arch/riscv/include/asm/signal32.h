/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __ASM_SIGNAL32_H
#define __ASM_SIGNAL32_H

#if IS_ENABLED(CONFIG_COMPAT)
int compat_setup_rt_frame(struct ksignal *ksig, sigset_t *set,
			  struct pt_regs *regs);
#else
static inline
int compat_setup_rt_frame(struct ksignal *ksig, sigset_t *set,
			  struct pt_regs *regs)
{
	return -1;
}
#endif

#endif
