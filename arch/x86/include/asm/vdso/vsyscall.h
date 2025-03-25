/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_VSYSCALL_H
#define __ASM_VDSO_VSYSCALL_H

#define __VDSO_RND_DATA_OFFSET  640
#define __VVAR_PAGES	4

#define VDSO_NR_VCLOCK_PAGES	2
#define VDSO_PAGE_PVCLOCK_OFFSET	0
#define VDSO_PAGE_HVCLOCK_OFFSET	1

#ifndef __ASSEMBLER__

#include <vdso/datapage.h>
#include <asm/vgtod.h>

extern struct vdso_data *vdso_data;

/*
 * Update the vDSO data page to keep in sync with kernel timekeeping.
 */
static __always_inline
struct vdso_data *__x86_get_k_vdso_data(void)
{
	return vdso_data;
}
#define __arch_get_k_vdso_data __x86_get_k_vdso_data

static __always_inline
struct vdso_rng_data *__x86_get_k_vdso_rng_data(void)
{
	return (void *)vdso_data + __VDSO_RND_DATA_OFFSET;
}
#define __arch_get_k_vdso_rng_data __x86_get_k_vdso_rng_data

/* The asm-generic header needs to be included after the definitions above */
#include <asm-generic/vdso/vsyscall.h>

#endif /* !__ASSEMBLER__ */

#endif /* __ASM_VDSO_VSYSCALL_H */
