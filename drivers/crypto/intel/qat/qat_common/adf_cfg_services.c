// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */

#include <linux/array_size.h>
#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/pci.h>
#include <linux/string.h>
#include "adf_cfg.h"
#include "adf_cfg_common.h"
#include "adf_cfg_services.h"
#include "adf_cfg_strings.h"

static const char *const adf_cfg_services[] = {
	[SVC_ASYM] = ADF_CFG_ASYM,
	[SVC_SYM] = ADF_CFG_SYM,
	[SVC_DC] = ADF_CFG_DC,
	[SVC_DCC] = ADF_CFG_DCC,
	[SVC_DECOMP] = ADF_CFG_DECOMP,
};

/*
 * Ensure that the size of the array matches the number of services,
 * SVC_COUNT, that is used to size the bitmap.
 */
static_assert(ARRAY_SIZE(adf_cfg_services) == SVC_COUNT);

/*
 * Ensure that the maximum number of concurrent services that can be
 * enabled on a device is less than or equal to the number of total
 * supported services.
 */
static_assert(ARRAY_SIZE(adf_cfg_services) >= MAX_NUM_CONCURR_SVC);

/*
 * Ensure that the number of services fit a single unsigned long, as each
 * service is represented by a bit in the mask.
 */
static_assert(BITS_PER_LONG >= SVC_COUNT);

/*
 * Ensure that size of the concatenation of all service strings is smaller
 * than the size of the buffer that will contain them.
 */
static_assert(sizeof(ADF_CFG_SYM ADF_SERVICES_DELIMITER
		     ADF_CFG_ASYM ADF_SERVICES_DELIMITER
		     ADF_CFG_DC ADF_SERVICES_DELIMITER
		     ADF_CFG_DECOMP ADF_SERVICES_DELIMITER
		     ADF_CFG_DCC) < ADF_CFG_MAX_VAL_LEN_IN_BYTES);

static int adf_service_string_to_mask(struct adf_accel_dev *accel_dev, const char *buf,
				      size_t len, unsigned long *out_mask)
{
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	char services[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = { };
	unsigned long mask = 0;
	char *substr, *token;
	int id, num_svc = 0;

	if (len > ADF_CFG_MAX_VAL_LEN_IN_BYTES - 1)
		return -EINVAL;

	strscpy(services, buf, ADF_CFG_MAX_VAL_LEN_IN_BYTES);
	substr = services;

	while ((token = strsep(&substr, ADF_SERVICES_DELIMITER))) {
		id = sysfs_match_string(adf_cfg_services, token);
		if (id < 0)
			return id;

		if (test_and_set_bit(id, &mask))
			return -EINVAL;

		if (num_svc++ == MAX_NUM_CONCURR_SVC)
			return -EINVAL;
	}

	if (hw_data->services_supported && !hw_data->services_supported(mask))
		return -EINVAL;

	*out_mask = mask;

	return 0;
}

static int adf_service_mask_to_string(unsigned long mask, char *buf, size_t len)
{
	int offset = 0;
	int bit;

	if (len < ADF_CFG_MAX_VAL_LEN_IN_BYTES)
		return -ENOSPC;

	for_each_set_bit(bit, &mask, SVC_COUNT) {
		if (offset)
			offset += scnprintf(buf + offset, len - offset,
					    ADF_SERVICES_DELIMITER);

		offset += scnprintf(buf + offset, len - offset, "%s",
				    adf_cfg_services[bit]);
	}

	return 0;
}

int adf_parse_service_string(struct adf_accel_dev *accel_dev, const char *in,
			     size_t in_len, char *out, size_t out_len)
{
	unsigned long mask;
	int ret;

	ret = adf_service_string_to_mask(accel_dev, in, in_len, &mask);
	if (ret)
		return ret;

	if (!mask)
		return -EINVAL;

	return adf_service_mask_to_string(mask, out, out_len);
}

int adf_get_service_mask(struct adf_accel_dev *accel_dev, unsigned long *mask)
{
	char services[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = { };
	size_t len;
	int ret;

	ret = adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC,
				      ADF_SERVICES_ENABLED, services);
	if (ret) {
		dev_err(&GET_DEV(accel_dev), "%s param not found\n",
			ADF_SERVICES_ENABLED);
		return ret;
	}

	len = strnlen(services, ADF_CFG_MAX_VAL_LEN_IN_BYTES);
	ret = adf_service_string_to_mask(accel_dev, services, len, mask);
	if (ret)
		dev_err(&GET_DEV(accel_dev), "Invalid value of %s param: %s\n",
			ADF_SERVICES_ENABLED, services);

	return ret;
}
EXPORT_SYMBOL_GPL(adf_get_service_mask);

int adf_get_service_enabled(struct adf_accel_dev *accel_dev)
{
	unsigned long mask;
	int ret;

	ret = adf_get_service_mask(accel_dev, &mask);
	if (ret)
		return ret;

	if (test_bit(SVC_SYM, &mask) && test_bit(SVC_ASYM, &mask))
		return SVC_SYM_ASYM;

	if (test_bit(SVC_SYM, &mask) && test_bit(SVC_DC, &mask))
		return SVC_SYM_DC;

	if (test_bit(SVC_ASYM, &mask) && test_bit(SVC_DC, &mask))
		return SVC_ASYM_DC;

	if (test_bit(SVC_SYM, &mask))
		return SVC_SYM;

	if (test_bit(SVC_ASYM, &mask))
		return SVC_ASYM;

	if (test_bit(SVC_DC, &mask))
		return SVC_DC;

	if (test_bit(SVC_DECOMP, &mask))
		return SVC_DECOMP;

	if (test_bit(SVC_DCC, &mask))
		return SVC_DCC;

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(adf_get_service_enabled);

enum adf_cfg_service_type adf_srv_to_cfg_svc_type(enum adf_base_services svc)
{
	switch (svc) {
	case SVC_ASYM:
		return ASYM;
	case SVC_SYM:
		return SYM;
	case SVC_DC:
		return COMP;
	case SVC_DECOMP:
		return DECOMP;
	default:
		return UNUSED;
	}
}

bool adf_is_service_enabled(struct adf_accel_dev *accel_dev, enum adf_base_services svc)
{
	enum adf_cfg_service_type arb_srv = adf_srv_to_cfg_svc_type(svc);
	struct adf_hw_device_data *hw_data = GET_HW_DATA(accel_dev);
	u8 rps_per_bundle = hw_data->num_banks_per_vf;
	int i;

	for (i = 0; i < rps_per_bundle; i++) {
		if (GET_SRV_TYPE(accel_dev, i) == arb_srv)
			return true;
	}

	return false;
}
