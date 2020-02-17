/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_VSYSCALL_H
#define __ASM_VDSO_VSYSCALL_H

#ifndef __ASSEMBLY__

#include <linux/timekeeper_internal.h>
#include <vdso/datapage.h>
#include <asm/cacheflush.h>

extern struct vdso_data *vdso_data;
extern bool cntvct_ok;

static __always_inline
bool tk_is_cntvct(const struct timekeeper *tk)
{
	if (!IS_ENABLED(CONFIG_ARM_ARCH_TIMER))
		return false;

	if (!tk->tkr_mono.clock->archdata.vdso_direct)
		return false;

	return true;
}

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
int __arm_get_clock_mode(struct timekeeper *tk)
{
	u32 __tk_is_cntvct = tk_is_cntvct(tk);

	return __tk_is_cntvct;
}
#define __arch_get_clock_mode __arm_get_clock_mode

static __always_inline
int __arm_use_vsyscall(struct vdso_data *vdata)
{
	return vdata[CS_HRES_COARSE].clock_mode;
}
#define __arch_use_vsyscall __arm_use_vsyscall

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
