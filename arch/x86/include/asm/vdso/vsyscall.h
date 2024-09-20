/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_VSYSCALL_H
#define __ASM_VDSO_VSYSCALL_H

#ifndef __ASSEMBLY__

#include <linux/timekeeper_internal.h>
#include <vdso/datapage.h>
#include <asm/vgtod.h>
#include <asm/vvar.h>

DEFINE_VVAR(struct vdso_data, _vdso_data);
DEFINE_VVAR_SINGLE(struct vdso_rng_data, _vdso_rng_data);

/*
 * Update the vDSO data page to keep in sync with kernel timekeeping.
 */
static __always_inline
struct vdso_data *__x86_get_k_vdso_data(void)
{
	return _vdso_data;
}
#define __arch_get_k_vdso_data __x86_get_k_vdso_data

/* The asm-generic header needs to be included after the definitions above */
#include <asm-generic/vdso/vsyscall.h>

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_VSYSCALL_H */
