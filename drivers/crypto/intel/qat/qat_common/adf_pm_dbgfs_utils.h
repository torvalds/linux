/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright(c) 2025 Intel Corporation */
#ifndef ADF_PM_DBGFS_UTILS_H_
#define ADF_PM_DBGFS_UTILS_H_

#include <linux/stddef.h>
#include <linux/stringify.h>
#include <linux/types.h>
#include "icp_qat_fw_init_admin.h"

#define PM_INFO_MEMBER_OFF(member)	\
	(offsetof(struct icp_qat_fw_init_admin_pm_info, member) / sizeof(u32))

#define PM_INFO_REGSET_ENTRY_MASK(_reg_, _field_, _mask_)	\
{								\
	.reg_offset = PM_INFO_MEMBER_OFF(_reg_),		\
	.key = __stringify(_field_),				\
	.field_mask = _mask_,					\
}

#define PM_INFO_REGSET_ENTRY32(_reg_, _field_)	\
	PM_INFO_REGSET_ENTRY_MASK(_reg_, _field_, GENMASK(31, 0))

struct pm_status_row {
	int reg_offset;
	u32 field_mask;
	const char *key;
};

int adf_pm_scnprint_table_upper_keys(char *buff, const struct pm_status_row *table,
				     u32 *pm_info_regs, size_t buff_size, int table_len);

int adf_pm_scnprint_table_lower_keys(char *buff, const struct pm_status_row *table,
				     u32 *pm_info_regs, size_t buff_size, int table_len);

#endif /* ADF_PM_DBGFS_UTILS_H_ */
