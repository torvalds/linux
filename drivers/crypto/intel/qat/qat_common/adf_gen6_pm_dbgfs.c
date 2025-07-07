// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2025 Intel Corporation */
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/string_helpers.h>

#include "adf_admin.h"
#include "adf_common_drv.h"
#include "adf_gen6_pm.h"
#include "adf_pm_dbgfs_utils.h"
#include "icp_qat_fw_init_admin.h"

#define PM_INFO_REGSET_ENTRY(_reg_, _field_) \
	PM_INFO_REGSET_ENTRY_MASK(_reg_, _field_, ADF_GEN6_PM_##_field_##_MASK)

static struct pm_status_row pm_fuse_rows[] = {
	PM_INFO_REGSET_ENTRY(fusectl0, ENABLE_PM),
	PM_INFO_REGSET_ENTRY(fusectl0, ENABLE_PM_IDLE),
	PM_INFO_REGSET_ENTRY(fusectl0, ENABLE_DEEP_PM_IDLE),
};

static struct pm_status_row pm_info_rows[] = {
	PM_INFO_REGSET_ENTRY(pm.status, CPM_PM_STATE),
	PM_INFO_REGSET_ENTRY(pm.fw_init, IDLE_ENABLE),
	PM_INFO_REGSET_ENTRY(pm.fw_init, IDLE_FILTER),
};

static struct pm_status_row pm_ssm_rows[] = {
	PM_INFO_REGSET_ENTRY(ssm.pm_enable, SSM_PM_ENABLE),
	PM_INFO_REGSET_ENTRY(ssm.pm_domain_status, DOMAIN_POWERED_UP),
};

static struct pm_status_row pm_csrs_rows[] = {
	PM_INFO_REGSET_ENTRY32(pm.fw_init, CPM_PM_FW_INIT),
	PM_INFO_REGSET_ENTRY32(pm.status, CPM_PM_STATUS),
};

static_assert(sizeof(struct icp_qat_fw_init_admin_pm_info) < PAGE_SIZE);

static ssize_t adf_gen6_print_pm_status(struct adf_accel_dev *accel_dev,
					char __user *buf, size_t count,
					loff_t *pos)
{
	void __iomem *pmisc = adf_get_pmisc_base(accel_dev);
	struct icp_qat_fw_init_admin_pm_info *pm_info;
	dma_addr_t p_state_addr;
	u32 *pm_info_regs;
	size_t len = 0;
	char *pm_kv;
	u32 val;
	int ret;

	pm_info = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!pm_info)
		return -ENOMEM;

	pm_kv = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!pm_kv) {
		kfree(pm_info);
		return -ENOMEM;
	}

	p_state_addr = dma_map_single(&GET_DEV(accel_dev), pm_info, PAGE_SIZE,
				      DMA_FROM_DEVICE);
	ret = dma_mapping_error(&GET_DEV(accel_dev), p_state_addr);
	if (ret)
		goto out_free;

	/* Query power management information from QAT FW */
	ret = adf_get_pm_info(accel_dev, p_state_addr, PAGE_SIZE);
	dma_unmap_single(&GET_DEV(accel_dev), p_state_addr, PAGE_SIZE,
			 DMA_FROM_DEVICE);
	if (ret)
		goto out_free;

	pm_info_regs = (u32 *)pm_info;

	/* Fuse control register */
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len,
			 "----------- PM Fuse info ---------\n");
	len += adf_pm_scnprint_table_lower_keys(&pm_kv[len], pm_fuse_rows,
						pm_info_regs, PAGE_SIZE - len,
						ARRAY_SIZE(pm_fuse_rows));

	/* Power management */
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len,
			 "----------- PM Info --------------\n");

	len += adf_pm_scnprint_table_lower_keys(&pm_kv[len], pm_info_rows,
						pm_info_regs, PAGE_SIZE - len,
						ARRAY_SIZE(pm_info_rows));
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len, "pm_mode: ACTIVE\n");

	/* Shared Slice Module */
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len,
			 "----------- SSM_PM Info ----------\n");
	len += adf_pm_scnprint_table_lower_keys(&pm_kv[len], pm_ssm_rows,
						pm_info_regs, PAGE_SIZE - len,
						ARRAY_SIZE(pm_ssm_rows));

	/* Control status register content */
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len,
			 "----------- HW PM CSRs -----------\n");
	len += adf_pm_scnprint_table_upper_keys(&pm_kv[len], pm_csrs_rows,
						pm_info_regs, PAGE_SIZE - len,
						ARRAY_SIZE(pm_csrs_rows));

	val = ADF_CSR_RD(pmisc, ADF_GEN6_PM_INTERRUPT);
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len, "CPM_PM_INTERRUPT: %#x\n", val);
	ret = simple_read_from_buffer(buf, count, pos, pm_kv, len);

out_free:
	kfree(pm_info);
	kfree(pm_kv);

	return ret;
}

void adf_gen6_init_dev_pm_data(struct adf_accel_dev *accel_dev)
{
	accel_dev->power_management.print_pm_status = adf_gen6_print_pm_status;
	accel_dev->power_management.present = true;
}
EXPORT_SYMBOL_GPL(adf_gen6_init_dev_pm_data);
