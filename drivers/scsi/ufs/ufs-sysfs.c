// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Western Digital Corporation

#include <linux/err.h>
#include <linux/string.h>
#include <asm/unaligned.h>

#include "ufs.h"
#include "ufs-sysfs.h"

static const char *ufschd_uic_link_state_to_string(
			enum uic_link_state state)
{
	switch (state) {
	case UIC_LINK_OFF_STATE:	return "OFF";
	case UIC_LINK_ACTIVE_STATE:	return "ACTIVE";
	case UIC_LINK_HIBERN8_STATE:	return "HIBERN8";
	default:			return "UNKNOWN";
	}
}

static const char *ufschd_ufs_dev_pwr_mode_to_string(
			enum ufs_dev_pwr_mode state)
{
	switch (state) {
	case UFS_ACTIVE_PWR_MODE:	return "ACTIVE";
	case UFS_SLEEP_PWR_MODE:	return "SLEEP";
	case UFS_POWERDOWN_PWR_MODE:	return "POWERDOWN";
	default:			return "UNKNOWN";
	}
}

static inline ssize_t ufs_sysfs_pm_lvl_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count,
					     bool rpm)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	unsigned long flags, value;

	if (kstrtoul(buf, 0, &value))
		return -EINVAL;

	if (value >= UFS_PM_LVL_MAX)
		return -EINVAL;

	spin_lock_irqsave(hba->host->host_lock, flags);
	if (rpm)
		hba->rpm_lvl = value;
	else
		hba->spm_lvl = value;
	spin_unlock_irqrestore(hba->host->host_lock, flags);
	return count;
}

static ssize_t rpm_lvl_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	int curr_len;
	u8 lvl;

	curr_len = snprintf(buf, PAGE_SIZE,
			    "\nCurrent Runtime PM level [%d] => dev_state [%s] link_state [%s]\n",
			    hba->rpm_lvl,
			    ufschd_ufs_dev_pwr_mode_to_string(
				ufs_pm_lvl_states[hba->rpm_lvl].dev_state),
			    ufschd_uic_link_state_to_string(
				ufs_pm_lvl_states[hba->rpm_lvl].link_state));

	curr_len += snprintf((buf + curr_len), (PAGE_SIZE - curr_len),
			     "\nAll available Runtime PM levels info:\n");
	for (lvl = UFS_PM_LVL_0; lvl < UFS_PM_LVL_MAX; lvl++)
		curr_len += snprintf((buf + curr_len), (PAGE_SIZE - curr_len),
				     "\tRuntime PM level [%d] => dev_state [%s] link_state [%s]\n",
				    lvl,
				    ufschd_ufs_dev_pwr_mode_to_string(
					ufs_pm_lvl_states[lvl].dev_state),
				    ufschd_uic_link_state_to_string(
					ufs_pm_lvl_states[lvl].link_state));

	return curr_len;
}

static ssize_t rpm_lvl_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return ufs_sysfs_pm_lvl_store(dev, attr, buf, count, true);
}

static ssize_t spm_lvl_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	int curr_len;
	u8 lvl;

	curr_len = snprintf(buf, PAGE_SIZE,
			    "\nCurrent System PM level [%d] => dev_state [%s] link_state [%s]\n",
			    hba->spm_lvl,
			    ufschd_ufs_dev_pwr_mode_to_string(
				ufs_pm_lvl_states[hba->spm_lvl].dev_state),
			    ufschd_uic_link_state_to_string(
				ufs_pm_lvl_states[hba->spm_lvl].link_state));

	curr_len += snprintf((buf + curr_len), (PAGE_SIZE - curr_len),
			     "\nAll available System PM levels info:\n");
	for (lvl = UFS_PM_LVL_0; lvl < UFS_PM_LVL_MAX; lvl++)
		curr_len += snprintf((buf + curr_len), (PAGE_SIZE - curr_len),
				     "\tSystem PM level [%d] => dev_state [%s] link_state [%s]\n",
				    lvl,
				    ufschd_ufs_dev_pwr_mode_to_string(
					ufs_pm_lvl_states[lvl].dev_state),
				    ufschd_uic_link_state_to_string(
					ufs_pm_lvl_states[lvl].link_state));

	return curr_len;
}

static ssize_t spm_lvl_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return ufs_sysfs_pm_lvl_store(dev, attr, buf, count, false);
}

static DEVICE_ATTR_RW(rpm_lvl);
static DEVICE_ATTR_RW(spm_lvl);

static struct attribute *ufs_sysfs_ufshcd_attrs[] = {
	&dev_attr_rpm_lvl.attr,
	&dev_attr_spm_lvl.attr,
	NULL
};

static const struct attribute_group ufs_sysfs_default_group = {
	.attrs = ufs_sysfs_ufshcd_attrs,
};

static ssize_t ufs_sysfs_read_desc_param(struct ufs_hba *hba,
				  enum desc_idn desc_id,
				  u8 desc_index,
				  u8 param_offset,
				  u8 *sysfs_buf,
				  u8 param_size)
{
	u8 desc_buf[8] = {0};
	int ret;

	if (param_size > 8)
		return -EINVAL;

	ret = ufshcd_read_desc_param(hba, desc_id, desc_index,
				param_offset, desc_buf, param_size);
	if (ret)
		return -EINVAL;
	switch (param_size) {
	case 1:
		ret = sprintf(sysfs_buf, "0x%02X\n", *desc_buf);
		break;
	case 2:
		ret = sprintf(sysfs_buf, "0x%04X\n",
			get_unaligned_be16(desc_buf));
		break;
	case 4:
		ret = sprintf(sysfs_buf, "0x%08X\n",
			get_unaligned_be32(desc_buf));
		break;
	case 8:
		ret = sprintf(sysfs_buf, "0x%016llX\n",
			get_unaligned_be64(desc_buf));
		break;
	}

	return ret;
}

#define UFS_DESC_PARAM(_name, _puname, _duname, _size)			\
static ssize_t _name##_show(struct device *dev,				\
	struct device_attribute *attr, char *buf)			\
{									\
	struct ufs_hba *hba = dev_get_drvdata(dev);			\
	return ufs_sysfs_read_desc_param(hba, QUERY_DESC_IDN_##_duname,	\
		0, _duname##_DESC_PARAM##_puname, buf, _size);		\
}									\
static DEVICE_ATTR_RO(_name)

#define UFS_DEVICE_DESC_PARAM(_name, _uname, _size)			\
	UFS_DESC_PARAM(_name, _uname, DEVICE, _size)

UFS_DEVICE_DESC_PARAM(device_type, _DEVICE_TYPE, 1);
UFS_DEVICE_DESC_PARAM(device_class, _DEVICE_CLASS, 1);
UFS_DEVICE_DESC_PARAM(device_sub_class, _DEVICE_SUB_CLASS, 1);
UFS_DEVICE_DESC_PARAM(protocol, _PRTCL, 1);
UFS_DEVICE_DESC_PARAM(number_of_luns, _NUM_LU, 1);
UFS_DEVICE_DESC_PARAM(number_of_wluns, _NUM_WLU, 1);
UFS_DEVICE_DESC_PARAM(boot_enable, _BOOT_ENBL, 1);
UFS_DEVICE_DESC_PARAM(descriptor_access_enable, _DESC_ACCSS_ENBL, 1);
UFS_DEVICE_DESC_PARAM(initial_power_mode, _INIT_PWR_MODE, 1);
UFS_DEVICE_DESC_PARAM(high_priority_lun, _HIGH_PR_LUN, 1);
UFS_DEVICE_DESC_PARAM(secure_removal_type, _SEC_RMV_TYPE, 1);
UFS_DEVICE_DESC_PARAM(support_security_lun, _SEC_LU, 1);
UFS_DEVICE_DESC_PARAM(bkops_termination_latency, _BKOP_TERM_LT, 1);
UFS_DEVICE_DESC_PARAM(initial_active_icc_level, _ACTVE_ICC_LVL, 1);
UFS_DEVICE_DESC_PARAM(specification_version, _SPEC_VER, 2);
UFS_DEVICE_DESC_PARAM(manufacturing_date, _MANF_DATE, 2);
UFS_DEVICE_DESC_PARAM(manufacturer_id, _MANF_ID, 2);
UFS_DEVICE_DESC_PARAM(rtt_capability, _RTT_CAP, 1);
UFS_DEVICE_DESC_PARAM(rtc_update, _FRQ_RTC, 2);
UFS_DEVICE_DESC_PARAM(ufs_features, _UFS_FEAT, 1);
UFS_DEVICE_DESC_PARAM(ffu_timeout, _FFU_TMT, 1);
UFS_DEVICE_DESC_PARAM(queue_depth, _Q_DPTH, 1);
UFS_DEVICE_DESC_PARAM(device_version, _DEV_VER, 2);
UFS_DEVICE_DESC_PARAM(number_of_secure_wpa, _NUM_SEC_WPA, 1);
UFS_DEVICE_DESC_PARAM(psa_max_data_size, _PSA_MAX_DATA, 4);
UFS_DEVICE_DESC_PARAM(psa_state_timeout, _PSA_TMT, 1);

static struct attribute *ufs_sysfs_device_descriptor[] = {
	&dev_attr_device_type.attr,
	&dev_attr_device_class.attr,
	&dev_attr_device_sub_class.attr,
	&dev_attr_protocol.attr,
	&dev_attr_number_of_luns.attr,
	&dev_attr_number_of_wluns.attr,
	&dev_attr_boot_enable.attr,
	&dev_attr_descriptor_access_enable.attr,
	&dev_attr_initial_power_mode.attr,
	&dev_attr_high_priority_lun.attr,
	&dev_attr_secure_removal_type.attr,
	&dev_attr_support_security_lun.attr,
	&dev_attr_bkops_termination_latency.attr,
	&dev_attr_initial_active_icc_level.attr,
	&dev_attr_specification_version.attr,
	&dev_attr_manufacturing_date.attr,
	&dev_attr_manufacturer_id.attr,
	&dev_attr_rtt_capability.attr,
	&dev_attr_rtc_update.attr,
	&dev_attr_ufs_features.attr,
	&dev_attr_ffu_timeout.attr,
	&dev_attr_queue_depth.attr,
	&dev_attr_device_version.attr,
	&dev_attr_number_of_secure_wpa.attr,
	&dev_attr_psa_max_data_size.attr,
	&dev_attr_psa_state_timeout.attr,
	NULL,
};

static const struct attribute_group ufs_sysfs_device_descriptor_group = {
	.name = "device_descriptor",
	.attrs = ufs_sysfs_device_descriptor,
};

#define UFS_INTERCONNECT_DESC_PARAM(_name, _uname, _size)		\
	UFS_DESC_PARAM(_name, _uname, INTERCONNECT, _size)

UFS_INTERCONNECT_DESC_PARAM(unipro_version, _UNIPRO_VER, 2);
UFS_INTERCONNECT_DESC_PARAM(mphy_version, _MPHY_VER, 2);

static struct attribute *ufs_sysfs_interconnect_descriptor[] = {
	&dev_attr_unipro_version.attr,
	&dev_attr_mphy_version.attr,
	NULL,
};

static const struct attribute_group ufs_sysfs_interconnect_descriptor_group = {
	.name = "interconnect_descriptor",
	.attrs = ufs_sysfs_interconnect_descriptor,
};

#define UFS_GEOMETRY_DESC_PARAM(_name, _uname, _size)			\
	UFS_DESC_PARAM(_name, _uname, GEOMETRY, _size)

UFS_GEOMETRY_DESC_PARAM(raw_device_capacity, _DEV_CAP, 8);
UFS_GEOMETRY_DESC_PARAM(max_number_of_luns, _MAX_NUM_LUN, 1);
UFS_GEOMETRY_DESC_PARAM(segment_size, _SEG_SIZE, 4);
UFS_GEOMETRY_DESC_PARAM(allocation_unit_size, _ALLOC_UNIT_SIZE, 1);
UFS_GEOMETRY_DESC_PARAM(min_addressable_block_size, _MIN_BLK_SIZE, 1);
UFS_GEOMETRY_DESC_PARAM(optimal_read_block_size, _OPT_RD_BLK_SIZE, 1);
UFS_GEOMETRY_DESC_PARAM(optimal_write_block_size, _OPT_WR_BLK_SIZE, 1);
UFS_GEOMETRY_DESC_PARAM(max_in_buffer_size, _MAX_IN_BUF_SIZE, 1);
UFS_GEOMETRY_DESC_PARAM(max_out_buffer_size, _MAX_OUT_BUF_SIZE, 1);
UFS_GEOMETRY_DESC_PARAM(rpmb_rw_size, _RPMB_RW_SIZE, 1);
UFS_GEOMETRY_DESC_PARAM(dyn_capacity_resource_policy, _DYN_CAP_RSRC_PLC, 1);
UFS_GEOMETRY_DESC_PARAM(data_ordering, _DATA_ORDER, 1);
UFS_GEOMETRY_DESC_PARAM(max_number_of_contexts, _MAX_NUM_CTX, 1);
UFS_GEOMETRY_DESC_PARAM(sys_data_tag_unit_size, _TAG_UNIT_SIZE, 1);
UFS_GEOMETRY_DESC_PARAM(sys_data_tag_resource_size, _TAG_RSRC_SIZE, 1);
UFS_GEOMETRY_DESC_PARAM(secure_removal_types, _SEC_RM_TYPES, 1);
UFS_GEOMETRY_DESC_PARAM(memory_types, _MEM_TYPES, 2);
UFS_GEOMETRY_DESC_PARAM(sys_code_memory_max_alloc_units,
	_SCM_MAX_NUM_UNITS, 4);
UFS_GEOMETRY_DESC_PARAM(sys_code_memory_capacity_adjustment_factor,
	_SCM_CAP_ADJ_FCTR, 2);
UFS_GEOMETRY_DESC_PARAM(non_persist_memory_max_alloc_units,
	_NPM_MAX_NUM_UNITS, 4);
UFS_GEOMETRY_DESC_PARAM(non_persist_memory_capacity_adjustment_factor,
	_NPM_CAP_ADJ_FCTR, 2);
UFS_GEOMETRY_DESC_PARAM(enh1_memory_max_alloc_units,
	_ENM1_MAX_NUM_UNITS, 4);
UFS_GEOMETRY_DESC_PARAM(enh1_memory_capacity_adjustment_factor,
	_ENM1_CAP_ADJ_FCTR, 2);
UFS_GEOMETRY_DESC_PARAM(enh2_memory_max_alloc_units,
	_ENM2_MAX_NUM_UNITS, 4);
UFS_GEOMETRY_DESC_PARAM(enh2_memory_capacity_adjustment_factor,
	_ENM2_CAP_ADJ_FCTR, 2);
UFS_GEOMETRY_DESC_PARAM(enh3_memory_max_alloc_units,
	_ENM3_MAX_NUM_UNITS, 4);
UFS_GEOMETRY_DESC_PARAM(enh3_memory_capacity_adjustment_factor,
	_ENM3_CAP_ADJ_FCTR, 2);
UFS_GEOMETRY_DESC_PARAM(enh4_memory_max_alloc_units,
	_ENM4_MAX_NUM_UNITS, 4);
UFS_GEOMETRY_DESC_PARAM(enh4_memory_capacity_adjustment_factor,
	_ENM4_CAP_ADJ_FCTR, 2);

static struct attribute *ufs_sysfs_geometry_descriptor[] = {
	&dev_attr_raw_device_capacity.attr,
	&dev_attr_max_number_of_luns.attr,
	&dev_attr_segment_size.attr,
	&dev_attr_allocation_unit_size.attr,
	&dev_attr_min_addressable_block_size.attr,
	&dev_attr_optimal_read_block_size.attr,
	&dev_attr_optimal_write_block_size.attr,
	&dev_attr_max_in_buffer_size.attr,
	&dev_attr_max_out_buffer_size.attr,
	&dev_attr_rpmb_rw_size.attr,
	&dev_attr_dyn_capacity_resource_policy.attr,
	&dev_attr_data_ordering.attr,
	&dev_attr_max_number_of_contexts.attr,
	&dev_attr_sys_data_tag_unit_size.attr,
	&dev_attr_sys_data_tag_resource_size.attr,
	&dev_attr_secure_removal_types.attr,
	&dev_attr_memory_types.attr,
	&dev_attr_sys_code_memory_max_alloc_units.attr,
	&dev_attr_sys_code_memory_capacity_adjustment_factor.attr,
	&dev_attr_non_persist_memory_max_alloc_units.attr,
	&dev_attr_non_persist_memory_capacity_adjustment_factor.attr,
	&dev_attr_enh1_memory_max_alloc_units.attr,
	&dev_attr_enh1_memory_capacity_adjustment_factor.attr,
	&dev_attr_enh2_memory_max_alloc_units.attr,
	&dev_attr_enh2_memory_capacity_adjustment_factor.attr,
	&dev_attr_enh3_memory_max_alloc_units.attr,
	&dev_attr_enh3_memory_capacity_adjustment_factor.attr,
	&dev_attr_enh4_memory_max_alloc_units.attr,
	&dev_attr_enh4_memory_capacity_adjustment_factor.attr,
	NULL,
};

static const struct attribute_group ufs_sysfs_geometry_descriptor_group = {
	.name = "geometry_descriptor",
	.attrs = ufs_sysfs_geometry_descriptor,
};

#define UFS_HEALTH_DESC_PARAM(_name, _uname, _size)			\
	UFS_DESC_PARAM(_name, _uname, HEALTH, _size)

UFS_HEALTH_DESC_PARAM(eol_info, _EOL_INFO, 1);
UFS_HEALTH_DESC_PARAM(life_time_estimation_a, _LIFE_TIME_EST_A, 1);
UFS_HEALTH_DESC_PARAM(life_time_estimation_b, _LIFE_TIME_EST_B, 1);

static struct attribute *ufs_sysfs_health_descriptor[] = {
	&dev_attr_eol_info.attr,
	&dev_attr_life_time_estimation_a.attr,
	&dev_attr_life_time_estimation_b.attr,
	NULL,
};

static const struct attribute_group ufs_sysfs_health_descriptor_group = {
	.name = "health_descriptor",
	.attrs = ufs_sysfs_health_descriptor,
};

#define UFS_POWER_DESC_PARAM(_name, _uname, _index)			\
static ssize_t _name##_index##_show(struct device *dev,			\
	struct device_attribute *attr, char *buf)			\
{									\
	struct ufs_hba *hba = dev_get_drvdata(dev);			\
	return ufs_sysfs_read_desc_param(hba, QUERY_DESC_IDN_POWER, 0,	\
		PWR_DESC##_uname##_0 + _index * 2, buf, 2);		\
}									\
static DEVICE_ATTR_RO(_name##_index)

UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 0);
UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 1);
UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 2);
UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 3);
UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 4);
UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 5);
UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 6);
UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 7);
UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 8);
UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 9);
UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 10);
UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 11);
UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 12);
UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 13);
UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 14);
UFS_POWER_DESC_PARAM(active_icc_levels_vcc, _ACTIVE_LVLS_VCC, 15);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 0);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 1);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 2);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 3);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 4);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 5);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 6);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 7);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 8);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 9);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 10);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 11);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 12);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 13);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 14);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq, _ACTIVE_LVLS_VCCQ, 15);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 0);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 1);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 2);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 3);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 4);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 5);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 6);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 7);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 8);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 9);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 10);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 11);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 12);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 13);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 14);
UFS_POWER_DESC_PARAM(active_icc_levels_vccq2, _ACTIVE_LVLS_VCCQ2, 15);

static struct attribute *ufs_sysfs_power_descriptor[] = {
	&dev_attr_active_icc_levels_vcc0.attr,
	&dev_attr_active_icc_levels_vcc1.attr,
	&dev_attr_active_icc_levels_vcc2.attr,
	&dev_attr_active_icc_levels_vcc3.attr,
	&dev_attr_active_icc_levels_vcc4.attr,
	&dev_attr_active_icc_levels_vcc5.attr,
	&dev_attr_active_icc_levels_vcc6.attr,
	&dev_attr_active_icc_levels_vcc7.attr,
	&dev_attr_active_icc_levels_vcc8.attr,
	&dev_attr_active_icc_levels_vcc9.attr,
	&dev_attr_active_icc_levels_vcc10.attr,
	&dev_attr_active_icc_levels_vcc11.attr,
	&dev_attr_active_icc_levels_vcc12.attr,
	&dev_attr_active_icc_levels_vcc13.attr,
	&dev_attr_active_icc_levels_vcc14.attr,
	&dev_attr_active_icc_levels_vcc15.attr,
	&dev_attr_active_icc_levels_vccq0.attr,
	&dev_attr_active_icc_levels_vccq1.attr,
	&dev_attr_active_icc_levels_vccq2.attr,
	&dev_attr_active_icc_levels_vccq3.attr,
	&dev_attr_active_icc_levels_vccq4.attr,
	&dev_attr_active_icc_levels_vccq5.attr,
	&dev_attr_active_icc_levels_vccq6.attr,
	&dev_attr_active_icc_levels_vccq7.attr,
	&dev_attr_active_icc_levels_vccq8.attr,
	&dev_attr_active_icc_levels_vccq9.attr,
	&dev_attr_active_icc_levels_vccq10.attr,
	&dev_attr_active_icc_levels_vccq11.attr,
	&dev_attr_active_icc_levels_vccq12.attr,
	&dev_attr_active_icc_levels_vccq13.attr,
	&dev_attr_active_icc_levels_vccq14.attr,
	&dev_attr_active_icc_levels_vccq15.attr,
	&dev_attr_active_icc_levels_vccq20.attr,
	&dev_attr_active_icc_levels_vccq21.attr,
	&dev_attr_active_icc_levels_vccq22.attr,
	&dev_attr_active_icc_levels_vccq23.attr,
	&dev_attr_active_icc_levels_vccq24.attr,
	&dev_attr_active_icc_levels_vccq25.attr,
	&dev_attr_active_icc_levels_vccq26.attr,
	&dev_attr_active_icc_levels_vccq27.attr,
	&dev_attr_active_icc_levels_vccq28.attr,
	&dev_attr_active_icc_levels_vccq29.attr,
	&dev_attr_active_icc_levels_vccq210.attr,
	&dev_attr_active_icc_levels_vccq211.attr,
	&dev_attr_active_icc_levels_vccq212.attr,
	&dev_attr_active_icc_levels_vccq213.attr,
	&dev_attr_active_icc_levels_vccq214.attr,
	&dev_attr_active_icc_levels_vccq215.attr,
	NULL,
};

static const struct attribute_group ufs_sysfs_power_descriptor_group = {
	.name = "power_descriptor",
	.attrs = ufs_sysfs_power_descriptor,
};

#define UFS_STRING_DESCRIPTOR(_name, _pname)				\
static ssize_t _name##_show(struct device *dev,				\
	struct device_attribute *attr, char *buf)			\
{									\
	u8 index;							\
	struct ufs_hba *hba = dev_get_drvdata(dev);			\
	int ret;							\
	int desc_len = QUERY_DESC_MAX_SIZE;				\
	u8 *desc_buf;							\
	desc_buf = kzalloc(QUERY_DESC_MAX_SIZE, GFP_ATOMIC);		\
	if (!desc_buf)							\
		return -ENOMEM;						\
	ret = ufshcd_query_descriptor_retry(hba,			\
		UPIU_QUERY_OPCODE_READ_DESC, QUERY_DESC_IDN_DEVICE,	\
		0, 0, desc_buf, &desc_len);				\
	if (ret) {							\
		ret = -EINVAL;						\
		goto out;						\
	}								\
	index = desc_buf[DEVICE_DESC_PARAM##_pname];			\
	memset(desc_buf, 0, QUERY_DESC_MAX_SIZE);			\
	if (ufshcd_read_string_desc(hba, index, desc_buf,		\
		QUERY_DESC_MAX_SIZE, true)) {				\
		ret = -EINVAL;						\
		goto out;						\
	}								\
	ret = snprintf(buf, PAGE_SIZE, "%s\n",				\
		desc_buf + QUERY_DESC_HDR_SIZE);			\
out:									\
	kfree(desc_buf);						\
	return ret;							\
}									\
static DEVICE_ATTR_RO(_name)

UFS_STRING_DESCRIPTOR(manufacturer_name, _MANF_NAME);
UFS_STRING_DESCRIPTOR(product_name, _PRDCT_NAME);
UFS_STRING_DESCRIPTOR(oem_id, _OEM_ID);
UFS_STRING_DESCRIPTOR(serial_number, _SN);
UFS_STRING_DESCRIPTOR(product_revision, _PRDCT_REV);

static struct attribute *ufs_sysfs_string_descriptors[] = {
	&dev_attr_manufacturer_name.attr,
	&dev_attr_product_name.attr,
	&dev_attr_oem_id.attr,
	&dev_attr_serial_number.attr,
	&dev_attr_product_revision.attr,
	NULL,
};

static const struct attribute_group ufs_sysfs_string_descriptors_group = {
	.name = "string_descriptors",
	.attrs = ufs_sysfs_string_descriptors,
};


static const struct attribute_group *ufs_sysfs_groups[] = {
	&ufs_sysfs_default_group,
	&ufs_sysfs_device_descriptor_group,
	&ufs_sysfs_interconnect_descriptor_group,
	&ufs_sysfs_geometry_descriptor_group,
	&ufs_sysfs_health_descriptor_group,
	&ufs_sysfs_power_descriptor_group,
	&ufs_sysfs_string_descriptors_group,
	NULL,
};

void ufs_sysfs_add_nodes(struct device *dev)
{
	int ret;

	ret = sysfs_create_groups(&dev->kobj, ufs_sysfs_groups);
	if (ret)
		dev_err(dev,
			"%s: sysfs groups creation failed (err = %d)\n",
			__func__, ret);
}

void ufs_sysfs_remove_nodes(struct device *dev)
{
	sysfs_remove_groups(&dev->kobj, ufs_sysfs_groups);
}
