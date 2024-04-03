/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _UFS_FAULT_INJECTION_H
#define _UFS_FAULT_INJECTION_H

#include <linux/kconfig.h>
#include <linux/types.h>

#ifdef CONFIG_SCSI_UFS_FAULT_INJECTION
void ufs_fault_inject_hba_init(struct ufs_hba *hba);
bool ufs_trigger_eh(struct ufs_hba *hba);
bool ufs_fail_completion(struct ufs_hba *hba);
#else
static inline void ufs_fault_inject_hba_init(struct ufs_hba *hba)
{
}

static inline bool ufs_trigger_eh(struct ufs_hba *hba)
{
	return false;
}

static inline bool ufs_fail_completion(struct ufs_hba *hba)
{
	return false;
}
#endif

#endif /* _UFS_FAULT_INJECTION_H */
