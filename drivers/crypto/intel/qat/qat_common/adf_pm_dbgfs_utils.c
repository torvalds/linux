// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2025 Intel Corporation */
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/sprintf.h>
#include <linux/string_helpers.h>

#include "adf_pm_dbgfs_utils.h"

#define PM_INFO_MAX_KEY_LEN	21

static int pm_scnprint_table(char *buff, const struct pm_status_row *table,
			     u32 *pm_info_regs, size_t buff_size, int table_len,
			     bool lowercase)
{
	char key[PM_INFO_MAX_KEY_LEN];
	int wr = 0;
	int i;

	for (i = 0; i < table_len; i++) {
		if (lowercase)
			string_lower(key, table[i].key);
		else
			string_upper(key, table[i].key);

		wr += scnprintf(&buff[wr], buff_size - wr, "%s: %#x\n", key,
				field_get(table[i].field_mask,
					  pm_info_regs[table[i].reg_offset]));
	}

	return wr;
}

int adf_pm_scnprint_table_upper_keys(char *buff, const struct pm_status_row *table,
				     u32 *pm_info_regs, size_t buff_size, int table_len)
{
	return pm_scnprint_table(buff, table, pm_info_regs, buff_size,
				 table_len, false);
}

int adf_pm_scnprint_table_lower_keys(char *buff, const struct pm_status_row *table,
				     u32 *pm_info_regs, size_t buff_size, int table_len)
{
	return pm_scnprint_table(buff, table, pm_info_regs, buff_size,
				 table_len, true);
}
