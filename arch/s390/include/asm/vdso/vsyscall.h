/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_VSYSCALL_H
#define __ASM_VDSO_VSYSCALL_H

#define __VDSO_RND_DATA_OFFSET	768

#ifndef __ASSEMBLY__

#include <linux/hrtimer.h>
#include <vdso/datapage.h>
#include <asm/vdso.h>

enum vvar_pages {
	VVAR_DATA_PAGE_OFFSET,
	VVAR_TIMENS_PAGE_OFFSET,
	VVAR_NR_PAGES
};

static __always_inline struct vdso_data *__s390_get_k_vdso_data(void)
{
	return vdso_data;
}
#define __arch_get_k_vdso_data __s390_get_k_vdso_data

static __always_inline struct vdso_rng_data *__s390_get_k_vdso_rnd_data(void)
{
	return (void *)vdso_data + __VDSO_RND_DATA_OFFSET;
}
#define __arch_get_k_vdso_rng_data __s390_get_k_vdso_rnd_data

/* The asm-generic header needs to be included after the definitions above */
#include <asm-generic/vdso/vsyscall.h>

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_VSYSCALL_H */
