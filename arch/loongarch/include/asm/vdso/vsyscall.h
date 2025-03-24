/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_VSYSCALL_H
#define __ASM_VDSO_VSYSCALL_H

#ifndef __ASSEMBLY__

#include <vdso/datapage.h>

extern struct vdso_data *vdso_data;
extern struct vdso_rng_data *vdso_rng_data;

static __always_inline
struct vdso_data *__loongarch_get_k_vdso_data(void)
{
	return vdso_data;
}
#define __arch_get_k_vdso_data __loongarch_get_k_vdso_data

static __always_inline
struct vdso_rng_data *__loongarch_get_k_vdso_rng_data(void)
{
	return vdso_rng_data;
}
#define __arch_get_k_vdso_rng_data __loongarch_get_k_vdso_rng_data

/* The asm-generic header needs to be included after the definitions above */
#include <asm-generic/vdso/vsyscall.h>

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_VSYSCALL_H */
