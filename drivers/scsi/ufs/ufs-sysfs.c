// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Western Digital Corporation

#include <linux/err.h>
#include <linux/string.h>
#include <linux/bitfield.h>
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
	case UIC_LINK_BROKEN_STATE:	return "BROKEN";
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

	return sprintf(buf, "%d\n", hba->rpm_lvl);
}

static ssize_t rpm_lvl_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return ufs_sysfs_pm_lvl_store(dev, attr, buf, count, true);
}

static ssize_t rpm_target_dev_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", ufschd_ufs_dev_pwr_mode_to_string(
			ufs_pm_lvl_states[hba->rpm_lvl].dev_state));
}

static ssize_t rpm_target_link_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", ufschd_uic_link_state_to_string(
			ufs_pm_lvl_states[hba->rpm_lvl].link_state));
}

static ssize_t spm_lvl_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", hba->spm_lvl);
}

static ssize_t spm_lvl_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return ufs_sysfs_pm_lvl_store(dev, attr, buf, count, false);
}

static ssize_t spm_target_dev_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", ufschd_ufs_dev_pwr_mode_to_string(
				ufs_pm_lvl_states[hba->spm_lvl].dev_state));
}

static ssize_t spm_target_link_state_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", ufschd_uic_link_state_to_string(
				ufs_pm_lvl_states[hba->spm_lvl].link_state));
}

/* Convert Auto-Hibernate Idle Timer register value to microseconds */
static int ufshcd_ahit_to_us(u32 ahit)
{
	int timer = FIELD_GET(UFSHCI_AHIBERN8_TIMER_MASK, ahit);
	int scale = FIELD_GET(UFSHCI_AHIBERN8_SCALE_MASK, ahit);

	for (; scale > 0; --scale)
		timer *= UFSHCI_AHIBERN8_SCALE_FACTOR;

	return timer;
}

/* Convert microseconds to Auto-Hibernate Idle Timer register value */
static u32 ufshcd_us_to_ahit(unsigned int timer)
{
	unsigned int scale;

	for (scale = 0; timer > UFSHCI_AHIBERN8_TIMER_MASK; ++scale)
		timer /= UFSHCI_AHIBERN8_SCALE_FACTOR;

	return FIELD_PREP(UFSHCI_AHIBERN8_TIMER_MASK, timer) |
	       FIELD_PREP(UFSHCI_AHIBERN8_SCALE_MASK, scale);
}

static ssize_t auto_hibern8_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	u32 ahit;
	struct ufs_hba *hba = dev_get_drvdata(dev);

	if (!ufshcd_is_auto_hibern8_supported(hba))
		return -EOPNOTSUPP;

	pm_runtime_get_sync(hba->dev);
	ufshcd_hold(hba, false);
	ahit = ufshcd_readl(hba, REG_AUTO_HIBERNATE_IDLE_TIMER);
	ufshcd_release(hba);
	pm_runtime_put_sync(hba->dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", ufshcd_ahit_to_us(ahit));
}

static ssize_t auto_hibern8_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct ufs_hba *hba = dev_get_drvdata(dev);
	unsigned int timer;

	if (!ufshcd_is_auto_hibern8_supported(hba))
		return -EOPNOTSUPP;

	if (kstrtouint(buf, 0, &timer))
		return -EINVAL;

	if (timer > UFSHCI_AHIBERN8_MAX)
		return -EINVAL;

	ufshcd_auto_hibern8_update(hba, ufshcd_us_to_ahit(timer));

	return count;
}

static DEVICE_ATTR_RW(rpm_lvl);
static DEVICE_ATTR_RO(rpm_target_dev_state);
static DEVICE_ATTR_RO(rpm_target_link_state);
static DEVICE_ATTR_RW(spm_lvl);
static DEVICE_ATTR_RO(spm_target_dev_state);
static DEVICE_ATTR_RO(spm_target_link_state);
static DEVICE_ATTR_RW(auto_hibern8);

static struct attribute *ufs_sysfs_ufshcd_attrs[] = {
	&dev_attr_rpm_lvl.attr,
	&dev_attr_rpm_target_dev_state.attr,
	&dev_attr_rpm_target_link_state.attr,
	&dev_attr_spm_lvl.attr,
	&dev_attr_spm_target_dev_state.attr,
	&dev_attr_spm_target_link_state.attr,
	&dev_attr_auto_hibern8.attr,
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

	pm_runtime_get_sync(hba->dev);
	ret = ufshcd_read_desc_param(hba, desc_id, desc_index,
				param_offset, desc_buf, param_size);
	pm_runtime_put_sync(hba->dev);
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
UFS_DEVICE_DESC_PARAM(ext_feature_sup, _EXT_UFS_FEATURE_SUP, 4);
UFS_DEVICE_DESC_PARAM(wb_presv_us_en, _WB_PRESRV_USRSPC_EN, 1);
UFS_DEVICE_DESC_PARAM(wb_type, _WB_TYPE, 1);
UFS_DEVICE_DESC_PARAM(wb_shared_alloc_units, _WB_SHARED_ALLOC_UNITS, 4);

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
	&dev_attr_ext_feature_sup.attr,
	&dev_attr_wb_presv_us_en.attr,
	&dev_attr_wb_type.attr,
	&dev_attr_wb_shared_alloc_units.attr,
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
UFS_GEOMETRY_DESC_PARAM(wb_max_alloc_units, _WB_MAX_ALLOC_UNITS, 4);
UFS_GEOMETRY_DESC_PARAM(wb_max_wb_luns, _WB_MAX_WB_LUNS, 1);
UFS_GEOMETRY_DESC_PARAM(wb_buff_cap_adj, _WB_BUFF_CAP_ADJ, 1);
UFS_GEOMETRY_DESC_PARAM(wb_sup_red_type, _WB_SUP_RED_TYPE, 1);
UFS_GEOMETRY_DESC_PARAM(wb_sup_wb_type, _WB_SUP_WB_TYPE, 1);


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
	&dev_attr_wb_max_alloc_units.attr,
	&dev_attr_wb_max_wb_luns.attr,
	&dev_attr_wb_buff_cap_adj.attr,
	&dev_attr_wb_sup_red_type.attr,
	&dev_attr_wb_sup_wb_type.attr,
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
									\
	desc_buf = kzalloc(QUERY_DESC_MAX_SIZE, GFP_ATOMIC);		\
	if (!desc_buf)                                                  \
		return -ENOMEM;                                         \
	pm_runtime_get_sync(hba->dev);					\
	ret = ufshcd_query_descriptor_retry(hba,			\
		UPIU_QUERY_OPCODE_READ_DESC, QUERY_DESC_IDN_DEVICE,	\
		0, 0, desc_buf, &desc_len);				\
	if (ret) {							\
		ret = -EINVAL;						\
		goto out;						\
	}								\
	index = desc_buf[DEVICE_DESC_PARAM##_pname];			\
	kfree(desc_buf);						\
	desc_buf = NULL;						\
	ret = ufshcd_read_string_desc(hba, index, &desc_buf,		\
				      SD_ASCII_STD);			\
	if (ret < 0)							\
		goto out;						\
	ret = snprintf(buf, PAGE_SIZE, "%s\n", desc_buf);		\
out:									\
	pm_runtime_put_sync(hba->dev);					\
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

static inline bool ufshcd_is_wb_flags(enum flag_idn idn)
{
	return ((idn >= QUERY_FLAG_IDN_WB_EN) &&
		(idn <= QUERY_FLAG_IDN_WB_BUFF_FLUSH_DURING_HIBERN8));
}

#define UFS_FLAG(_name, _uname)						\
static ssize_t _name##_show(struct device *dev,				\
	struct device_attribute *attr, char *buf)			\
{									\
	bool flag;							\
	u8 index = 0;							\
	int ret;							\
	struct ufs_hba *hba = dev_get_drvdata(dev);			\
	if (ufshcd_is_wb_flags(QUERY_FLAG_IDN##_uname))			\
		index = ufshcd_wb_get_query_index(hba);			\
	pm_runtime_get_sync(hba->dev);					\
	ret = ufshcd_query_flag(hba, UPIU_QUERY_OPCODE_READ_FLAG,	\
		QUERY_FLAG_IDN##_uname, index, &flag);			\
	pm_runtime_put_sync(hba->dev);					\
	if (ret)							\
		return -EINVAL;						\
	return sprintf(buf, "%s\n", flag ? "true" : "false"); \
}									\
static DEVICE_ATTR_RO(_name)

UFS_FLAG(device_init, _FDEVICEINIT);
UFS_FLAG(permanent_wpe, _PERMANENT_WPE);
UFS_FLAG(power_on_wpe, _PWR_ON_WPE);
UFS_FLAG(bkops_enable, _BKOPS_EN);
UFS_FLAG(life_span_mode_enable, _LIFE_SPAN_MODE_ENABLE);
UFS_FLAG(phy_resource_removal, _FPHYRESOURCEREMOVAL);
UFS_FLAG(busy_rtc, _BUSY_RTC);
UFS_FLAG(disable_fw_update, _PERMANENTLY_DISABLE_FW_UPDATE);
UFS_FLAG(wb_enable, _WB_EN);
UFS_FLAG(wb_flush_en, _WB_BUFF_FLUSH_EN);
UFS_FLAG(wb_flush_during_h8, _WB_BUFF_FLUSH_DURING_HIBERN8);

static struct attribute *ufs_sysfs_device_flags[] = {
	&dev_attr_device_init.attr,
	&dev_attr_permanent_wpe.attr,
	&dev_attr_power_on_wpe.attr,
	&dev_attr_bkops_enable.attr,
	&dev_attr_life_span_mode_enable.attr,
	&dev_attr_phy_resource_removal.attr,
	&dev_attr_busy_rtc.attr,
	&dev_attr_disable_fw_update.attr,
	&dev_attr_wb_enable.attr,
	&dev_attr_wb_flush_en.attr,
	&dev_attr_wb_flush_during_h8.attr,
	NULL,
};

static const struct attribute_group ufs_sysfs_flags_group = {
	.name = "flags",
	.attrs = ufs_sysfs_device_flags,
};

static inline bool ufshcd_is_wb_attrs(enum attr_idn idn)
{
	return ((idn >= QUERY_ATTR_IDN_WB_FLUSH_STATUS) &&
		(idn <= QUERY_ATTR_IDN_CURR_WB_BUFF_SIZE));
}

#define UFS_ATTRIBUTE(_name, _uname)					\
static ssize_t _name##_show(struct device *dev,				\
	struct device_attribute *attr, char *buf)			\
{									\
	struct ufs_hba *hba = dev_get_drvdata(dev);			\
	u32 value;							\
	int ret;							\
	u8 index = 0;							\
	if (ufshcd_is_wb_attrs(QUERY_ATTR_IDN##_uname))			\
		index = ufshcd_wb_get_query_index(hba);			\
	pm_runtime_get_sync(hba->dev);					\
	ret = ufshcd_query_attr(hba, UPIU_QUERY_OPCODE_READ_ATTR,	\
		QUERY_ATTR_IDN##_uname, index, 0, &value);		\
	pm_runtime_put_sync(hba->dev);					\
	if (ret)							\
		return -EINVAL;						\
	return sprintf(buf, "0x%08X\n", value);				\
}									\
static DEVICE_ATTR_RO(_name)

UFS_ATTRIBUTE(boot_lun_enabled, _BOOT_LU_EN);
UFS_ATTRIBUTE(current_power_mode, _POWER_MODE);
UFS_ATTRIBUTE(active_icc_level, _ACTIVE_ICC_LVL);
UFS_ATTRIBUTE(ooo_data_enabled, _OOO_DATA_EN);
UFS_ATTRIBUTE(bkops_status, _BKOPS_STATUS);
UFS_ATTRIBUTE(purge_status, _PURGE_STATUS);
UFS_ATTRIBUTE(max_data_in_size, _MAX_DATA_IN);
UFS_ATTRIBUTE(max_data_out_size, _MAX_DATA_OUT);
UFS_ATTRIBUTE(reference_clock_frequency, _REF_CLK_FREQ);
UFS_ATTRIBUTE(configuration_descriptor_lock, _CONF_DESC_LOCK);
UFS_ATTRIBUTE(max_number_of_rtt, _MAX_NUM_OF_RTT);
UFS_ATTRIBUTE(exception_event_control, _EE_CONTROL);
UFS_ATTRIBUTE(exception_event_status, _EE_STATUS);
UFS_ATTRIBUTE(ffu_status, _FFU_STATUS);
UFS_ATTRIBUTE(psa_state, _PSA_STATE);
UFS_ATTRIBUTE(psa_data_size, _PSA_DATA_SIZE);
UFS_ATTRIBUTE(wb_flush_status, _WB_FLUSH_STATUS);
UFS_ATTRIBUTE(wb_avail_buf, _AVAIL_WB_BUFF_SIZE);
UFS_ATTRIBUTE(wb_life_time_est, _WB_BUFF_LIFE_TIME_EST);
UFS_ATTRIBUTE(wb_cur_buf, _CURR_WB_BUFF_SIZE);


static struct attribute *ufs_sysfs_attributes[] = {
	&dev_attr_boot_lun_enabled.attr,
	&dev_attr_current_power_mode.attr,
	&dev_attr_active_icc_level.attr,
	&dev_attr_ooo_data_enabled.attr,
	&dev_attr_bkops_status.attr,
	&dev_attr_purge_status.attr,
	&dev_attr_max_data_in_size.attr,
	&dev_attr_max_data_out_size.attr,
	&dev_attr_reference_clock_frequency.attr,
	&dev_attr_configuration_descriptor_lock.attr,
	&dev_attr_max_number_of_rtt.attr,
	&dev_attr_exception_event_control.attr,
	&dev_attr_exception_event_status.attr,
	&dev_attr_ffu_status.attr,
	&dev_attr_psa_state.attr,
	&dev_attr_psa_data_size.attr,
	&dev_attr_wb_flush_status.attr,
	&dev_attr_wb_avail_buf.attr,
	&dev_attr_wb_life_time_est.attr,
	&dev_attr_wb_cur_buf.attr,
	NULL,
};

static const struct attribute_group ufs_sysfs_attributes_group = {
	.name = "attributes",
	.attrs = ufs_sysfs_attributes,
};

static const struct attribute_group *ufs_sysfs_groups[] = {
	&ufs_sysfs_default_group,
	&ufs_sysfs_device_descriptor_group,
	&ufs_sysfs_interconnect_descriptor_group,
	&ufs_sysfs_geometry_descriptor_group,
	&ufs_sysfs_health_descriptor_group,
	&ufs_sysfs_power_descriptor_group,
	&ufs_sysfs_string_descriptors_group,
	&ufs_sysfs_flags_group,
	&ufs_sysfs_attributes_group,
	NULL,
};

#define UFS_LUN_DESC_PARAM(_pname, _puname, _duname, _size)		\
static ssize_t _pname##_show(struct device *dev,			\
	struct device_attribute *attr, char *buf)			\
{									\
	struct scsi_device *sdev = to_scsi_device(dev);			\
	struct ufs_hba *hba = shost_priv(sdev->host);			\
	u8 lun = ufshcd_scsi_to_upiu_lun(sdev->lun);			\
	if (!ufs_is_valid_unit_desc_lun(&hba->dev_info, lun))		\
		return -EINVAL;						\
	return ufs_sysfs_read_desc_param(hba, QUERY_DESC_IDN_##_duname,	\
		lun, _duname##_DESC_PARAM##_puname, buf, _size);	\
}									\
static DEVICE_ATTR_RO(_pname)

#define UFS_UNIT_DESC_PARAM(_name, _uname, _size)			\
	UFS_LUN_DESC_PARAM(_name, _uname, UNIT, _size)

UFS_UNIT_DESC_PARAM(boot_lun_id, _BOOT_LUN_ID, 1);
UFS_UNIT_DESC_PARAM(lun_write_protect, _LU_WR_PROTECT, 1);
UFS_UNIT_DESC_PARAM(lun_queue_depth, _LU_Q_DEPTH, 1);
UFS_UNIT_DESC_PARAM(psa_sensitive, _PSA_SENSITIVE, 1);
UFS_UNIT_DESC_PARAM(lun_memory_type, _MEM_TYPE, 1);
UFS_UNIT_DESC_PARAM(data_reliability, _DATA_RELIABILITY, 1);
UFS_UNIT_DESC_PARAM(logical_block_size, _LOGICAL_BLK_SIZE, 1);
UFS_UNIT_DESC_PARAM(logical_block_count, _LOGICAL_BLK_COUNT, 8);
UFS_UNIT_DESC_PARAM(erase_block_size, _ERASE_BLK_SIZE, 4);
UFS_UNIT_DESC_PARAM(provisioning_type, _PROVISIONING_TYPE, 1);
UFS_UNIT_DESC_PARAM(physical_memory_resourse_count, _PHY_MEM_RSRC_CNT, 8);
UFS_UNIT_DESC_PARAM(context_capabilities, _CTX_CAPABILITIES, 2);
UFS_UNIT_DESC_PARAM(large_unit_granularity, _LARGE_UNIT_SIZE_M1, 1);
UFS_UNIT_DESC_PARAM(wb_buf_alloc_units, _WB_BUF_ALLOC_UNITS, 4);


static struct attribute *ufs_sysfs_unit_descriptor[] = {
	&dev_attr_boot_lun_id.attr,
	&dev_attr_lun_write_protect.attr,
	&dev_attr_lun_queue_depth.attr,
	&dev_attr_psa_sensitive.attr,
	&dev_attr_lun_memory_type.attr,
	&dev_attr_data_reliability.attr,
	&dev_attr_logical_block_size.attr,
	&dev_attr_logical_block_count.attr,
	&dev_attr_erase_block_size.attr,
	&dev_attr_provisioning_type.attr,
	&dev_attr_physical_memory_resourse_count.attr,
	&dev_attr_context_capabilities.attr,
	&dev_attr_large_unit_granularity.attr,
	&dev_attr_wb_buf_alloc_units.attr,
	NULL,
};

const struct attribute_group ufs_sysfs_unit_descriptor_group = {
	.name = "unit_descriptor",
	.attrs = ufs_sysfs_unit_descriptor,
};

static ssize_t dyn_cap_needed_attribute_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	u32 value;
	struct scsi_device *sdev = to_scsi_device(dev);
	struct ufs_hba *hba = shost_priv(sdev->host);
	u8 lun = ufshcd_scsi_to_upiu_lun(sdev->lun);
	int ret;

	pm_runtime_get_sync(hba->dev);
	ret = ufshcd_query_attr(hba, UPIU_QUERY_OPCODE_READ_ATTR,
		QUERY_ATTR_IDN_DYN_CAP_NEEDED, lun, 0, &value);
	pm_runtime_put_sync(hba->dev);
	if (ret)
		return -EINVAL;
	return sprintf(buf, "0x%08X\n", value);
}
static DEVICE_ATTR_RO(dyn_cap_needed_attribute);

static struct attribute *ufs_sysfs_lun_attributes[] = {
	&dev_attr_dyn_cap_needed_attribute.attr,
	NULL,
};

const struct attribute_group ufs_sysfs_lun_attributes_group = {
	.attrs = ufs_sysfs_lun_attributes,
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
