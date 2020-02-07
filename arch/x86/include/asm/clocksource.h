/* SPDX-License-Identifier: GPL-2.0 */
/* x86-specific clocksource additions */

#ifndef _ASM_X86_CLOCKSOURCE_H
#define _ASM_X86_CLOCKSOURCE_H

#define VDSO_ARCH_CLOCKMODES	\
	VDSO_CLOCKMODE_TSC,	\
	VDSO_CLOCKMODE_PVCLOCK,	\
	VDSO_CLOCKMODE_HVCLOCK

extern unsigned int vclocks_used;

static inline bool vclock_was_used(int vclock)
{
	return READ_ONCE(vclocks_used) & (1U << vclock);
}

static inline void vclocks_set_used(unsigned int which)
{
	WRITE_ONCE(vclocks_used, READ_ONCE(vclocks_used) | (1 << which));
}

#endif /* _ASM_X86_CLOCKSOURCE_H */
