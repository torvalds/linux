/*
 * Copyright (C) 2012 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __ASM_SIGNAL32_H
#define __ASM_SIGNAL32_H

#ifdef __KERNEL__
#ifdef CONFIG_COMPAT
#include <linux/compat.h>

#define AARCH32_KERN_SIGRET_CODE_OFFSET	0x500

extern const compat_ulong_t aarch32_sigret_code[6];

int compat_setup_frame(int usig, struct k_sigaction *ka, sigset_t *set,
		       struct pt_regs *regs);
int compat_setup_rt_frame(int usig, struct k_sigaction *ka, siginfo_t *info,
			  sigset_t *set, struct pt_regs *regs);

void compat_setup_restart_syscall(struct pt_regs *regs);
#else

static inline int compat_setup_frame(int usid, struct k_sigaction *ka,
				     sigset_t *set, struct pt_regs *regs)
{
	return -ENOSYS;
}

static inline int compat_setup_rt_frame(int usig, struct k_sigaction *ka,
					siginfo_t *info, sigset_t *set,
					struct pt_regs *regs)
{
	return -ENOSYS;
}

static inline void compat_setup_restart_syscall(struct pt_regs *regs)
{
}
#endif /* CONFIG_COMPAT */
#endif /* __KERNEL__ */
#endif /* __ASM_SIGNAL32_H */
