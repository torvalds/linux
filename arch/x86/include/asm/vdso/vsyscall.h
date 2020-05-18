/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_VSYSCALL_H
#define __ASM_VDSO_VSYSCALL_H

#ifndef __ASSEMBLY__

#include <linux/hrtimer.h>
#include <linux/timekeeper_internal.h>
#include <vdso/datapage.h>
#include <asm/vgtod.h>
#include <asm/vvar.h>

int vclocks_used __read_mostly;

DEFINE_VVAR(struct vdso_data, _vdso_data);
/*
 * Update the vDSO data page to keep in sync with kernel timekeeping.
 */
static __always_inline
struct vdso_data *__x86_get_k_vdso_data(void)
{
	return _vdso_data;
}
#define __arch_get_k_vdso_data __x86_get_k_vdso_data

static __always_inline
int __x86_get_clock_mode(struct timekeeper *tk)
{
	int vclock_mode = tk->tkr_mono.clock->archdata.vclock_mode;

	/* Mark the new vclock used. */
	BUILD_BUG_ON(VCLOCK_MAX >= 32);
	WRITE_ONCE(vclocks_used, READ_ONCE(vclocks_used) | (1 << vclock_mode));

	return vclock_mode;
}
#define __arch_get_clock_mode __x86_get_clock_mode

/* The asm-generic header needs to be included after the definitions above */
#include <asm-generic/vdso/vsyscall.h>

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_VSYSCALL_H */
