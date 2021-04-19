/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __S390_VDSO_H__
#define __S390_VDSO_H__

#include <vdso/datapage.h>

/* Default link address for the vDSO */
#define VDSO64_LBASE	0

#define __VVAR_PAGES	2

#define VDSO_VERSION_STRING	LINUX_2.6.29

#ifndef __ASSEMBLY__

extern struct vdso_data *vdso_data;

int vdso_getcpu_init(void);

#endif /* __ASSEMBLY__ */
#endif /* __S390_VDSO_H__ */
