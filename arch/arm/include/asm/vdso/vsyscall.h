/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_VSYSCALL_H
#define __ASM_VDSO_VSYSCALL_H

#ifndef __ASSEMBLY__

#include <vdso/datapage.h>
#include <asm/cacheflush.h>

static __always_inline
void __arch_sync_vdso_time_data(struct vdso_time_data *vdata)
{
	flush_dcache_page(virt_to_page(vdata));
}
#define __arch_sync_vdso_time_data __arch_sync_vdso_time_data

/* The asm-generic header needs to be included after the definitions above */
#include <asm-generic/vdso/vsyscall.h>

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_VDSO_VSYSCALL_H */
