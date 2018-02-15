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

static const struct attribute_group *ufs_sysfs_groups[] = {
	&ufs_sysfs_default_group,
	&ufs_sysfs_device_descriptor_group,
	&ufs_sysfs_interconnect_descriptor_group,
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
