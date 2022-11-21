/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _UFS_FAULT_INJECTION_H
#define _UFS_FAULT_INJECTION_H

#include <linux/kconfig.h>
#include <linux/types.h>

#ifdef CONFIG_SCSI_UFS_FAULT_INJECTION
bool ufs_trigger_eh(void);
bool ufs_fail_completion(void);
#else
static inline bool ufs_trigger_eh(void)
{
	return false;
}

static inline bool ufs_fail_completion(void)
{
	return false;
}
#endif

#endif /* _UFS_FAULT_INJECTION_H */
