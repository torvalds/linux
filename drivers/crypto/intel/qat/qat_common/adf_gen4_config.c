// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2023 Intel Corporation */
#include "adf_accel_devices.h"
#include "adf_cfg.h"
#include "adf_cfg_services.h"
#include "adf_cfg_strings.h"
#include "adf_common_drv.h"
#include "adf_gen4_config.h"
#include "adf_heartbeat.h"
#include "adf_transport_access_macros.h"
#include "qat_compression.h"
#include "qat_crypto.h"

static int adf_crypto_dev_config(struct adf_accel_dev *accel_dev)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	int banks = GET_MAX_BANKS(accel_dev);
	int cpus = num_online_cpus();
	unsigned long bank, val;
	int instances;
	int ret;
	int i;

	if (adf_hw_dev_has_crypto(accel_dev))
		instances = min(cpus, banks / 2);
	else
		instances = 0;

	for (i = 0; i < instances; i++) {
		val = i;
		bank = i * 2;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_BANK_NUM, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &bank, ADF_DEC);
		if (ret)
			goto err;

		bank += 1;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_BANK_NUM, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &bank, ADF_DEC);
		if (ret)
			goto err;

		snprintf(key, sizeof(key), ADF_CY "%d" ADF_ETRMGR_CORE_AFFINITY,
			 i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_SIZE, i);
		val = 128;
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 512;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_SIZE, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 0;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_TX, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 0;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_TX, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 1;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_ASYM_RX, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 1;
		snprintf(key, sizeof(key), ADF_CY "%d" ADF_RING_SYM_RX, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = ADF_COALESCING_DEF_TIME;
		snprintf(key, sizeof(key), ADF_ETRMGR_COALESCE_TIMER_FORMAT, i);
		ret = adf_cfg_add_key_value_param(accel_dev, "Accelerator0",
						  key, &val, ADF_DEC);
		if (ret)
			goto err;
	}

	val = i;
	ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, ADF_NUM_CY,
					  &val, ADF_DEC);
	if (ret)
		goto err;

	val = 0;
	ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, ADF_NUM_DC,
					  &val, ADF_DEC);
	if (ret)
		goto err;

	return 0;
err:
	dev_err(&GET_DEV(accel_dev), "Failed to add configuration for crypto\n");
	return ret;
}

static int adf_comp_dev_config(struct adf_accel_dev *accel_dev)
{
	char key[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	int banks = GET_MAX_BANKS(accel_dev);
	int cpus = num_online_cpus();
	unsigned long val;
	int instances;
	int ret;
	int i;

	if (adf_hw_dev_has_compression(accel_dev))
		instances = min(cpus, banks);
	else
		instances = 0;

	for (i = 0; i < instances; i++) {
		val = i;
		snprintf(key, sizeof(key), ADF_DC "%d" ADF_RING_DC_BANK_NUM, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 512;
		snprintf(key, sizeof(key), ADF_DC "%d" ADF_RING_DC_SIZE, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 0;
		snprintf(key, sizeof(key), ADF_DC "%d" ADF_RING_DC_TX, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = 1;
		snprintf(key, sizeof(key), ADF_DC "%d" ADF_RING_DC_RX, i);
		ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC,
						  key, &val, ADF_DEC);
		if (ret)
			goto err;

		val = ADF_COALESCING_DEF_TIME;
		snprintf(key, sizeof(key), ADF_ETRMGR_COALESCE_TIMER_FORMAT, i);
		ret = adf_cfg_add_key_value_param(accel_dev, "Accelerator0",
						  key, &val, ADF_DEC);
		if (ret)
			goto err;
	}

	val = i;
	ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, ADF_NUM_DC,
					  &val, ADF_DEC);
	if (ret)
		goto err;

	val = 0;
	ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, ADF_NUM_CY,
					  &val, ADF_DEC);
	if (ret)
		goto err;

	return 0;
err:
	dev_err(&GET_DEV(accel_dev), "Failed to add configuration for compression\n");
	return ret;
}

static int adf_no_dev_config(struct adf_accel_dev *accel_dev)
{
	unsigned long val;
	int ret;

	val = 0;
	ret = adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, ADF_NUM_DC,
					  &val, ADF_DEC);
	if (ret)
		return ret;

	return adf_cfg_add_key_value_param(accel_dev, ADF_KERNEL_SEC, ADF_NUM_CY,
					  &val, ADF_DEC);
}

/**
 * adf_gen4_dev_config() - create dev config required to create instances
 *
 * @accel_dev: Pointer to acceleration device.
 *
 * Function creates device configuration required to create instances
 *
 * Return: 0 on success, error code otherwise.
 */
int adf_gen4_dev_config(struct adf_accel_dev *accel_dev)
{
	int ret;

	ret = adf_cfg_section_add(accel_dev, ADF_KERNEL_SEC);
	if (ret)
		goto err;

	ret = adf_cfg_section_add(accel_dev, "Accelerator0");
	if (ret)
		goto err;

	switch (adf_get_service_enabled(accel_dev)) {
	case SVC_SYM_ASYM:
		ret = adf_crypto_dev_config(accel_dev);
		break;
	case SVC_DC:
	case SVC_DCC:
		ret = adf_comp_dev_config(accel_dev);
		break;
	default:
		ret = adf_no_dev_config(accel_dev);
		break;
	}

	if (ret)
		goto err;

	set_bit(ADF_STATUS_CONFIGURED, &accel_dev->status);

	return ret;

err:
	dev_err(&GET_DEV(accel_dev), "Failed to configure QAT driver\n");
	return ret;
}
EXPORT_SYMBOL_GPL(adf_gen4_dev_config);

int adf_gen4_cfg_dev_init(struct adf_accel_dev *accel_dev)
{
	const char *config;
	int ret;

	config = accel_dev->accel_id % 2 ? ADF_CFG_DC : ADF_CFG_CY;

	ret = adf_cfg_section_add(accel_dev, ADF_GENERAL_SEC);
	if (ret)
		return ret;

	/* Default configuration is crypto only for even devices
	 * and compression for odd devices
	 */
	ret = adf_cfg_add_key_value_param(accel_dev, ADF_GENERAL_SEC,
					  ADF_SERVICES_ENABLED, config,
					  ADF_STR);
	if (ret)
		return ret;

	adf_heartbeat_save_cfg_param(accel_dev, ADF_CFG_HB_TIMER_MIN_MS);

	return 0;
}
EXPORT_SYMBOL_GPL(adf_gen4_cfg_dev_init);
