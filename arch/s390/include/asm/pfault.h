/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Copyright IBM Corp. 1999, 2023
 */
#ifndef _ASM_S390_PFAULT_H
#define _ASM_S390_PFAULT_H

#include <linux/errno.h>

int __pfault_init(void);
void __pfault_fini(void);

static inline int pfault_init(void)
{
	if (IS_ENABLED(CONFIG_PFAULT))
		return __pfault_init();
	return -EOPNOTSUPP;
}

static inline void pfault_fini(void)
{
	if (IS_ENABLED(CONFIG_PFAULT))
		__pfault_fini();
}

#endif /* _ASM_S390_PFAULT_H */
