/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_VSYSCALL_H
#define __ASM_VDSO_VSYSCALL_H

#ifndef __ASSEMBLY__

#include <linux/timekeeper_internal.h>
#include <vdso/datapage.h>
#include <asm/cacheflush.h>

extern struct vdso_data *vdso_data;
extern bool cntvct_ok;

/*
 * Update the vDSO data page to keep in sync with kernel timekeeping.
 */
static __always_inline
struct vdso_data *__arm_get_k_vdso_data(void)
{
	return vdso_data;
}
#define __arch_get_k_vdso_data __arm_get_k_vdso_data

static __always_inline
bool __arm_update_vdso_data(void)
{
	return cntvct_ok;
}
#define __arch_update_vdso_data __arm_update_vdso_data

static __always_inline
void __arm_sync_vdso_data(struct vdso_data *vdata)
{
	flush_dcache_page(virt_to_page(vdata));
}
#define __arch_sync_vdso_data __arm_sync_vdso_data

/* The asm-generic header needs to be included after the definitions above */
#include <asm-generic/vdso/vsyscall.h>

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_VSYSCALL_H */
