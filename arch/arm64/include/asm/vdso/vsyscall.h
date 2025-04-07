/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_VSYSCALL_H
#define __ASM_VDSO_VSYSCALL_H

#ifndef __ASSEMBLY__

#include <vdso/datapage.h>

#define VDSO_PRECISION_MASK	~(0xFF00ULL<<48)


/*
 * Update the vDSO data page to keep in sync with kernel timekeeping.
 */
static __always_inline
void __arm64_update_vsyscall(struct vdso_time_data *vdata)
{
	vdata->clock_data[CS_HRES_COARSE].mask	= VDSO_PRECISION_MASK;
	vdata->clock_data[CS_RAW].mask		= VDSO_PRECISION_MASK;
}
#define __arch_update_vsyscall __arm64_update_vsyscall

/* The asm-generic header needs to be included after the definitions above */
#include <asm-generic/vdso/vsyscall.h>

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_VSYSCALL_H */
