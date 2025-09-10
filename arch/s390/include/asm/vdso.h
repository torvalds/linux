/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __S390_VDSO_H__
#define __S390_VDSO_H__

#include <vdso/datapage.h>

#ifndef __ASSEMBLER__

int vdso_getcpu_init(void);

#endif /* __ASSEMBLER__ */

#define __VDSO_PAGES	4

#define VDSO_VERSION_STRING	LINUX_2.6.29

#endif /* __S390_VDSO_H__ */
