// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/string_helpers.h>
#include <linux/stringify.h>

#include "adf_accel_devices.h"
#include "adf_admin.h"
#include "adf_common_drv.h"
#include "adf_gen4_pm.h"
#include "icp_qat_fw_init_admin.h"

/*
 * This is needed because a variable is used to index the mask at
 * pm_scnprint_table(), making it not compile time constant, so the compile
 * asserts from FIELD_GET() or u32_get_bits() won't be fulfilled.
 */
#define field_get(_mask, _reg) (((_reg) & (_mask)) >> (ffs(_mask) - 1))

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

#define PM_INFO_REGSET_ENTRY(_reg_, _field_)	\
	PM_INFO_REGSET_ENTRY_MASK(_reg_, _field_, ADF_GEN4_PM_##_field_##_MASK)

#define PM_INFO_MAX_KEY_LEN	21

struct pm_status_row {
	int reg_offset;
	u32 field_mask;
	const char *key;
};

static struct pm_status_row pm_fuse_rows[] = {
	PM_INFO_REGSET_ENTRY(fusectl0, ENABLE_PM),
	PM_INFO_REGSET_ENTRY(fusectl0, ENABLE_PM_IDLE),
	PM_INFO_REGSET_ENTRY(fusectl0, ENABLE_DEEP_PM_IDLE),
};

static struct pm_status_row pm_info_rows[] = {
	PM_INFO_REGSET_ENTRY(pm.status, CPM_PM_STATE),
	PM_INFO_REGSET_ENTRY(pm.status, PENDING_WP),
	PM_INFO_REGSET_ENTRY(pm.status, CURRENT_WP),
	PM_INFO_REGSET_ENTRY(pm.fw_init, IDLE_ENABLE),
	PM_INFO_REGSET_ENTRY(pm.fw_init, IDLE_FILTER),
	PM_INFO_REGSET_ENTRY(pm.main, MIN_PWR_ACK),
	PM_INFO_REGSET_ENTRY(pm.thread, MIN_PWR_ACK_PENDING),
	PM_INFO_REGSET_ENTRY(pm.main, THR_VALUE),
};

static struct pm_status_row pm_ssm_rows[] = {
	PM_INFO_REGSET_ENTRY(ssm.pm_enable, SSM_PM_ENABLE),
	PM_INFO_REGSET_ENTRY32(ssm.active_constraint, ACTIVE_CONSTRAINT),
	PM_INFO_REGSET_ENTRY(ssm.pm_domain_status, DOMAIN_POWER_GATED),
	PM_INFO_REGSET_ENTRY(ssm.pm_active_status, ATH_ACTIVE_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_active_status, CPH_ACTIVE_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_active_status, PKE_ACTIVE_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_active_status, CPR_ACTIVE_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_active_status, DCPR_ACTIVE_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_active_status, UCS_ACTIVE_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_active_status, XLT_ACTIVE_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_active_status, WAT_ACTIVE_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_active_status, WCP_ACTIVE_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_managed_status, ATH_MANAGED_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_managed_status, CPH_MANAGED_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_managed_status, PKE_MANAGED_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_managed_status, CPR_MANAGED_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_managed_status, DCPR_MANAGED_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_managed_status, UCS_MANAGED_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_managed_status, XLT_MANAGED_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_managed_status, WAT_MANAGED_COUNT),
	PM_INFO_REGSET_ENTRY(ssm.pm_managed_status, WCP_MANAGED_COUNT),
};

static struct pm_status_row pm_log_rows[] = {
	PM_INFO_REGSET_ENTRY32(event_counters.host_msg, HOST_MSG_EVENT_COUNT),
	PM_INFO_REGSET_ENTRY32(event_counters.sys_pm, SYS_PM_EVENT_COUNT),
	PM_INFO_REGSET_ENTRY32(event_counters.local_ssm, SSM_EVENT_COUNT),
	PM_INFO_REGSET_ENTRY32(event_counters.timer, TIMER_EVENT_COUNT),
	PM_INFO_REGSET_ENTRY32(event_counters.unknown, UNKNOWN_EVENT_COUNT),
};

static struct pm_status_row pm_event_rows[ICP_QAT_NUMBER_OF_PM_EVENTS] = {
	PM_INFO_REGSET_ENTRY32(event_log[0], EVENT0),
	PM_INFO_REGSET_ENTRY32(event_log[1], EVENT1),
	PM_INFO_REGSET_ENTRY32(event_log[2], EVENT2),
	PM_INFO_REGSET_ENTRY32(event_log[3], EVENT3),
	PM_INFO_REGSET_ENTRY32(event_log[4], EVENT4),
	PM_INFO_REGSET_ENTRY32(event_log[5], EVENT5),
	PM_INFO_REGSET_ENTRY32(event_log[6], EVENT6),
	PM_INFO_REGSET_ENTRY32(event_log[7], EVENT7),
};

static struct pm_status_row pm_csrs_rows[] = {
	PM_INFO_REGSET_ENTRY32(pm.fw_init, CPM_PM_FW_INIT),
	PM_INFO_REGSET_ENTRY32(pm.status, CPM_PM_STATUS),
	PM_INFO_REGSET_ENTRY32(pm.main, CPM_PM_MASTER_FW),
	PM_INFO_REGSET_ENTRY32(pm.pwrreq, CPM_PM_PWRREQ),
};

static int pm_scnprint_table(char *buff, struct pm_status_row *table,
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

static int pm_scnprint_table_upper_keys(char *buff, struct pm_status_row *table,
					u32 *pm_info_regs, size_t buff_size,
					int table_len)
{
	return pm_scnprint_table(buff, table, pm_info_regs, buff_size,
				 table_len, false);
}

static int pm_scnprint_table_lower_keys(char *buff, struct pm_status_row *table,
					u32 *pm_info_regs, size_t buff_size,
					int table_len)
{
	return pm_scnprint_table(buff, table, pm_info_regs, buff_size,
				 table_len, true);
}

static_assert(sizeof(struct icp_qat_fw_init_admin_pm_info) < PAGE_SIZE);

static ssize_t adf_gen4_print_pm_status(struct adf_accel_dev *accel_dev,
					char __user *buf, size_t count,
					loff_t *pos)
{
	void __iomem *pmisc = adf_get_pmisc_base(accel_dev);
	struct adf_pm *pm = &accel_dev->power_management;
	struct icp_qat_fw_init_admin_pm_info *pm_info;
	dma_addr_t p_state_addr;
	u32 *pm_info_regs;
	char *pm_kv;
	int len = 0;
	u32 val;
	int ret;

	pm_info = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!pm_info)
		return -ENOMEM;

	pm_kv = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!pm_kv) {
		ret = -ENOMEM;
		goto out_free;
	}

	p_state_addr = dma_map_single(&GET_DEV(accel_dev), pm_info, PAGE_SIZE,
				      DMA_FROM_DEVICE);
	ret = dma_mapping_error(&GET_DEV(accel_dev), p_state_addr);
	if (ret)
		goto out_free;

	/* Query PM info from QAT FW */
	ret = adf_get_pm_info(accel_dev, p_state_addr, PAGE_SIZE);
	dma_unmap_single(&GET_DEV(accel_dev), p_state_addr, PAGE_SIZE,
			 DMA_FROM_DEVICE);
	if (ret)
		goto out_free;

	pm_info_regs = (u32 *)pm_info;

	/* Fusectl related */
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len,
			 "----------- PM Fuse info ---------\n");
	len += pm_scnprint_table_lower_keys(&pm_kv[len], pm_fuse_rows,
					    pm_info_regs, PAGE_SIZE - len,
					    ARRAY_SIZE(pm_fuse_rows));
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len, "max_pwrreq: %#x\n",
			 pm_info->max_pwrreq);
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len, "min_pwrreq: %#x\n",
			 pm_info->min_pwrreq);

	/* PM related */
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len,
			 "------------  PM Info ------------\n");
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len, "power_level: %s\n",
			 pm_info->pwr_state == PM_SET_MIN ? "min" : "max");
	len += pm_scnprint_table_lower_keys(&pm_kv[len], pm_info_rows,
					    pm_info_regs, PAGE_SIZE - len,
					    ARRAY_SIZE(pm_info_rows));
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len, "pm_mode: STATIC\n");

	/* SSM related */
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len,
			 "----------- SSM_PM Info ----------\n");
	len += pm_scnprint_table_lower_keys(&pm_kv[len], pm_ssm_rows,
					    pm_info_regs, PAGE_SIZE - len,
					    ARRAY_SIZE(pm_ssm_rows));

	/* Log related */
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len,
			 "------------- PM Log -------------\n");
	len += pm_scnprint_table_lower_keys(&pm_kv[len], pm_log_rows,
					    pm_info_regs, PAGE_SIZE - len,
					    ARRAY_SIZE(pm_log_rows));

	len += pm_scnprint_table_lower_keys(&pm_kv[len], pm_event_rows,
					    pm_info_regs, PAGE_SIZE - len,
					    ARRAY_SIZE(pm_event_rows));

	len += scnprintf(&pm_kv[len], PAGE_SIZE - len, "idle_irq_count: %#x\n",
			 pm->idle_irq_counters);
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len, "fw_irq_count: %#x\n",
			 pm->fw_irq_counters);
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len,
			 "throttle_irq_count: %#x\n", pm->throttle_irq_counters);
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len, "host_ack_count: %#x\n",
			 pm->host_ack_counter);
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len, "host_nack_count: %#x\n",
			 pm->host_nack_counter);

	/* CSRs content */
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len,
			 "----------- HW PM CSRs -----------\n");
	len += pm_scnprint_table_upper_keys(&pm_kv[len], pm_csrs_rows,
					    pm_info_regs, PAGE_SIZE - len,
					    ARRAY_SIZE(pm_csrs_rows));

	val = ADF_CSR_RD(pmisc, ADF_GEN4_PM_HOST_MSG);
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len,
			 "CPM_PM_HOST_MSG: %#x\n", val);
	val = ADF_CSR_RD(pmisc, ADF_GEN4_PM_INTERRUPT);
	len += scnprintf(&pm_kv[len], PAGE_SIZE - len,
			 "CPM_PM_INTERRUPT: %#x\n", val);
	ret = simple_read_from_buffer(buf, count, pos, pm_kv, len);

out_free:
	kfree(pm_info);
	kfree(pm_kv);
	return ret;
}

void adf_gen4_init_dev_pm_data(struct adf_accel_dev *accel_dev)
{
	accel_dev->power_management.print_pm_status = adf_gen4_print_pm_status;
	accel_dev->power_management.present = true;
}
