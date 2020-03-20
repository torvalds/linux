/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_CLOCKSOURCE_H
#define __ASM_VDSO_CLOCKSOURCE_H

#define VCLOCK_NONE	0	/* No vDSO clock available.		*/
#define VCLOCK_TSC	1	/* vDSO should use vread_tsc.		*/
#define VCLOCK_PVCLOCK	2	/* vDSO should use vread_pvclock.	*/
#define VCLOCK_HVCLOCK	3	/* vDSO should use vread_hvclock.	*/
#define VCLOCK_MAX	3

#endif /* __ASM_VDSO_CLOCKSOURCE_H */
