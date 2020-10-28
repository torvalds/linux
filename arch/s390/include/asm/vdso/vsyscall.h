/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_VSYSCALL_H
#define __ASM_VDSO_VSYSCALL_H

#ifndef __ASSEMBLY__

#include <linux/hrtimer.h>
#include <linux/timekeeper_internal.h>
#include <vdso/datapage.h>
#include <asm/vdso.h>
/*
 * Update the vDSO data page to keep in sync with kernel timekeeping.
 */

static __always_inline struct vdso_data *__s390_get_k_vdso_data(void)
{
	return vdso_data;
}
#define __arch_get_k_vdso_data __s390_get_k_vdso_data

/* The asm-generic header needs to be included after the definitions above */
#include <asm-generic/vdso/vsyscall.h>

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_VSYSCALL_H */
