/* SPDX-License-Identifier: GPL-2.0 */
/* x86-specific clocksource additions */

#ifndef _ASM_X86_CLOCKSOURCE_H
#define _ASM_X86_CLOCKSOURCE_H

#define VCLOCK_NONE	0	/* No vDSO clock available.		*/
#define VCLOCK_TSC	1	/* vDSO should use vread_tsc.		*/
#define VCLOCK_PVCLOCK	2	/* vDSO should use vread_pvclock.	*/
#define VCLOCK_HVCLOCK	3	/* vDSO should use vread_hvclock.	*/
#define VCLOCK_MAX	3

struct arch_clocksource_data {
	int vclock_mode;
};

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
