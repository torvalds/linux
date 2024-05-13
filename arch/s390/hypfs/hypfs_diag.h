/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Hypervisor filesystem for Linux on s390. Diag 204 and 224
 *    implementation.
 *
 *    Copyright IBM Corp. 2006, 2008
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#ifndef _S390_HYPFS_DIAG_H_
#define _S390_HYPFS_DIAG_H_

#include <asm/diag.h>

enum diag204_format diag204_get_info_type(void);
void *diag204_get_buffer(enum diag204_format fmt, int *pages);
int diag204_store(void *buf, int pages);

int __hypfs_diag_fs_init(void);
void __hypfs_diag_fs_exit(void);

static inline int hypfs_diag_fs_init(void)
{
	if (IS_ENABLED(CONFIG_S390_HYPFS_FS))
		return __hypfs_diag_fs_init();
	return 0;
}

static inline void hypfs_diag_fs_exit(void)
{
	if (IS_ENABLED(CONFIG_S390_HYPFS_FS))
		__hypfs_diag_fs_exit();
}

#endif /* _S390_HYPFS_DIAG_H_ */
