/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_VDSO_CSKY_VSYSCALL_H
#define __ASM_VDSO_CSKY_VSYSCALL_H

#ifndef __ASSEMBLY__

#include <vdso/datapage.h>

extern struct vdso_data *vdso_data;

static __always_inline struct vdso_data *__csky_get_k_vdso_data(void)
{
	return vdso_data;
}
#define __arch_get_k_vdso_data __csky_get_k_vdso_data

#include <asm-generic/vdso/vsyscall.h>

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_CSKY_VSYSCALL_H */
