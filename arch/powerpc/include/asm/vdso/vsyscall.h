/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_VDSO_VSYSCALL_H
#define _ASM_POWERPC_VDSO_VSYSCALL_H

#ifndef __ASSEMBLY__

#include <asm/vdso_datapage.h>

static __always_inline
struct vdso_data *__arch_get_k_vdso_data(void)
{
	return vdso_data->data;
}
#define __arch_get_k_vdso_data __arch_get_k_vdso_data

static __always_inline
struct vdso_rng_data *__arch_get_k_vdso_rng_data(void)
{
	return &vdso_data->rng_data;
}

/* The asm-generic header needs to be included after the definitions above */
#include <asm-generic/vdso/vsyscall.h>

#endif /* !__ASSEMBLY__ */

#endif /* _ASM_POWERPC_VDSO_VSYSCALL_H */
