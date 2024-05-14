/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __S390_VDSO_H__
#define __S390_VDSO_H__

#include <vdso/datapage.h>

#ifndef __ASSEMBLY__

#include <generated/vdso64-offsets.h>
#ifdef CONFIG_COMPAT
#include <generated/vdso32-offsets.h>
#endif

#define VDSO64_SYMBOL(tsk, name) ((tsk)->mm->context.vdso_base + (vdso64_offset_##name))
#ifdef CONFIG_COMPAT
#define VDSO32_SYMBOL(tsk, name) ((tsk)->mm->context.vdso_base + (vdso32_offset_##name))
#else
#define VDSO32_SYMBOL(tsk, name) (-1UL)
#endif

extern struct vdso_data *vdso_data;

int vdso_getcpu_init(void);

#endif /* __ASSEMBLY__ */

/* Default link address for the vDSO */
#define VDSO_LBASE	0

#define __VVAR_PAGES	2

#define VDSO_VERSION_STRING	LINUX_2.6.29

#endif /* __S390_VDSO_H__ */
