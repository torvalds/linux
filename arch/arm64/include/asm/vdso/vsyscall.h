/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_VSYSCALL_H
#define __ASM_VDSO_VSYSCALL_H

#ifndef __ASSEMBLY__

#include <linux/timekeeper_internal.h>
#include <vdso/datapage.h>

#define VDSO_PRECISION_MASK	~(0xFF00ULL<<48)

extern struct vdso_data *vdso_data;

/*
 * Update the vDSO data page to keep in sync with kernel timekeeping.
 */
static __always_inline
struct vdso_data *__arm64_get_k_vdso_data(void)
{
	return vdso_data;
}
#define __arch_get_k_vdso_data __arm64_get_k_vdso_data

static __always_inline
int __arm64_get_clock_mode(struct timekeeper *tk)
{
	u32 use_syscall = !tk->tkr_mono.clock->archdata.vdso_direct;

	return use_syscall;
}
#define __arch_get_clock_mode __arm64_get_clock_mode

static __always_inline
void __arm64_update_vsyscall(struct vdso_data *vdata, struct timekeeper *tk)
{
	vdata[CS_HRES_COARSE].mask	= VDSO_PRECISION_MASK;
	vdata[CS_RAW].mask		= VDSO_PRECISION_MASK;
}
#define __arch_update_vsyscall __arm64_update_vsyscall

/* The asm-generic header needs to be included after the definitions above */
#include <asm-generic/vdso/vsyscall.h>

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_VSYSCALL_H */
