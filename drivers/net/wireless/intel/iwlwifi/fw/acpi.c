// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2017 Intel Deutschland GmbH
 * Copyright (C) 2019-2025 Intel Corporation
 */
#include <linux/uuid.h>
#include "iwl-drv.h"
#include "iwl-debug.h"
#include "acpi.h"
#include "fw/runtime.h"

const guid_t iwl_guid = GUID_INIT(0xF21202BF, 0x8F78, 0x4DC6,
				  0xA5, 0xB3, 0x1F, 0x73,
				  0x8E, 0x28, 0x5A, 0xDE);

static const size_t acpi_dsm_size[DSM_FUNC_NUM_FUNCS] = {
	[DSM_FUNC_QUERY] =			sizeof(u32),
	[DSM_FUNC_DISABLE_SRD] =		sizeof(u8),
	[DSM_FUNC_ENABLE_INDONESIA_5G2] =	sizeof(u8),
	[DSM_FUNC_ENABLE_6E] =			sizeof(u32),
	[DSM_FUNC_REGULATORY_CONFIG] =		sizeof(u32),
	/* Not supported in driver */
	[5] =					(size_t)0,
	[DSM_FUNC_11AX_ENABLEMENT] =		sizeof(u32),
	[DSM_FUNC_ENABLE_UNII4_CHAN] =		sizeof(u32),
	[DSM_FUNC_ACTIVATE_CHANNEL] =		sizeof(u32),
	[DSM_FUNC_FORCE_DISABLE_CHANNELS] =	sizeof(u32),
	[DSM_FUNC_ENERGY_DETECTION_THRESHOLD] =	sizeof(u32),
	[DSM_FUNC_RFI_CONFIG] =			sizeof(u32),
	[DSM_FUNC_ENABLE_11BE] =		sizeof(u32),
};

static int iwl_acpi_get_handle(struct device *dev, acpi_string method,
			       acpi_handle *ret_handle)
{
	acpi_handle root_handle;
	acpi_status status;

	root_handle = ACPI_HANDLE(dev);
	if (!root_handle) {
		IWL_DEBUG_DEV_RADIO(dev,
				    "ACPI: Could not retrieve root port handle\n");
		return -ENOENT;
	}

	status = acpi_get_handle(root_handle, method, ret_handle);
	if (ACPI_FAILURE(status)) {
		IWL_DEBUG_DEV_RADIO(dev,
				    "ACPI: %s method not found\n", method);
		return -ENOENT;
	}
	return 0;
}

static void *iwl_acpi_get_object(struct device *dev, acpi_string method)
{
	struct acpi_buffer buf = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_handle handle;
	acpi_status status;
	int ret;

	ret = iwl_acpi_get_handle(dev, method, &handle);
	if (ret)
		return ERR_PTR(-ENOENT);

	/* Call the method with no arguments */
	status = acpi_evaluate_object(handle, NULL, NULL, &buf);
	if (ACPI_FAILURE(status)) {
		IWL_DEBUG_DEV_RADIO(dev,
				    "ACPI: %s method invocation failed (status: 0x%x)\n",
				    method, status);
		return ERR_PTR(-ENOENT);
	}
	return buf.pointer;
}

/*
 * Generic function for evaluating a method defined in the device specific
 * method (DSM) interface. The returned acpi object must be freed by calling
 * function.
 */
union acpi_object *iwl_acpi_get_dsm_object(struct device *dev, int rev,
					   int func, union acpi_object *args,
					   const guid_t *guid)
{
	union acpi_object *obj;

	obj = acpi_evaluate_dsm(ACPI_HANDLE(dev), guid, rev, func,
				args);
	if (!obj) {
		IWL_DEBUG_DEV_RADIO(dev,
				    "ACPI: DSM method invocation failed (rev: %d, func:%d)\n",
				    rev, func);
		return ERR_PTR(-ENOENT);
	}
	return obj;
}

/*
 * Generic function to evaluate a DSM with no arguments
 * and an integer return value,
 * (as an integer object or inside a buffer object),
 * verify and assign the value in the "value" parameter.
 * return 0 in success and the appropriate errno otherwise.
 */
static int iwl_acpi_get_dsm_integer(struct device *dev, int rev, int func,
				    const guid_t *guid, u64 *value,
				    size_t expected_size)
{
	union acpi_object *obj;
	int ret;

	obj = iwl_acpi_get_dsm_object(dev, rev, func, NULL, guid);
	if (IS_ERR(obj)) {
		IWL_DEBUG_DEV_RADIO(dev,
				    "Failed to get  DSM object. func= %d\n",
				    func);
		return -ENOENT;
	}

	if (obj->type == ACPI_TYPE_INTEGER) {
		*value = obj->integer.value;
	} else if (obj->type == ACPI_TYPE_BUFFER) {
		__le64 le_value = 0;

		if (WARN_ON_ONCE(expected_size > sizeof(le_value))) {
			ret = -EINVAL;
			goto out;
		}

		/* if the buffer size doesn't match the expected size */
		if (obj->buffer.length != expected_size)
			IWL_DEBUG_DEV_RADIO(dev,
					    "ACPI: DSM invalid buffer size, padding or truncating (%d)\n",
					    obj->buffer.length);

		 /* assuming LE from Intel BIOS spec */
		memcpy(&le_value, obj->buffer.pointer,
		       min_t(size_t, expected_size, (size_t)obj->buffer.length));
		*value = le64_to_cpu(le_value);
	} else {
		IWL_DEBUG_DEV_RADIO(dev,
				    "ACPI: DSM method did not return a valid object, type=%d\n",
				    obj->type);
		ret = -EINVAL;
		goto out;
	}

	IWL_DEBUG_DEV_RADIO(dev,
			    "ACPI: DSM method evaluated: func=%d, value=%lld\n",
			    func, *value);
	ret = 0;
out:
	ACPI_FREE(obj);
	return ret;
}

/*
 * This function receives a DSM function number, calculates its expected size
 * according to Intel BIOS spec, and fills in the value in a 32-bit field.
 * In case the expected size is smaller than 32-bit, padding will be added.
 */
int iwl_acpi_get_dsm(struct iwl_fw_runtime *fwrt,
		     enum iwl_dsm_funcs func, u32 *value)
{
	size_t expected_size;
	u64 tmp;
	int ret;

	BUILD_BUG_ON(ARRAY_SIZE(acpi_dsm_size) != DSM_FUNC_NUM_FUNCS);

	if (WARN_ON(func >= ARRAY_SIZE(acpi_dsm_size)))
		return -EINVAL;

	expected_size = acpi_dsm_size[func];

	/* Currently all ACPI DSMs are either 8-bit or 32-bit */
	if (expected_size != sizeof(u8) && expected_size != sizeof(u32))
		return -EOPNOTSUPP;

	ret = iwl_acpi_get_dsm_integer(fwrt->dev, ACPI_DSM_REV, func,
				       &iwl_guid, &tmp, expected_size);
	if (ret)
		return ret;

	if ((expected_size == sizeof(u8) && tmp != (u8)tmp) ||
	    (expected_size == sizeof(u32) && tmp != (u32)tmp))
		IWL_DEBUG_RADIO(fwrt,
				"DSM value overflows the expected size, truncating\n");
	*value = (u32)tmp;

	return 0;
}

static union acpi_object *
iwl_acpi_get_wifi_pkg_range(struct device *dev,
			    union acpi_object *data,
			    int min_data_size,
			    int max_data_size,
			    int *tbl_rev)
{
	int i;
	union acpi_object *wifi_pkg;

	/*
	 * We need at least one entry in the wifi package that
	 * describes the domain, and one more entry, otherwise there's
	 * no point in reading it.
	 */
	if (WARN_ON_ONCE(min_data_size < 2 || min_data_size > max_data_size))
		return ERR_PTR(-EINVAL);

	/*
	 * We need at least two packages, one for the revision and one
	 * for the data itself.  Also check that the revision is valid
	 * (i.e. it is an integer (each caller has to check by itself
	 * if the returned revision is supported)).
	 */
	if (data->type != ACPI_TYPE_PACKAGE ||
	    data->package.count < 2 ||
	    data->package.elements[0].type != ACPI_TYPE_INTEGER) {
		IWL_DEBUG_DEV_RADIO(dev, "Invalid packages structure\n");
		return ERR_PTR(-EINVAL);
	}

	*tbl_rev = data->package.elements[0].integer.value;

	/* loop through all the packages to find the one for WiFi */
	for (i = 1; i < data->package.count; i++) {
		union acpi_object *domain;

		wifi_pkg = &data->package.elements[i];

		/* skip entries that are not a package with the right size */
		if (wifi_pkg->type != ACPI_TYPE_PACKAGE ||
		    wifi_pkg->package.count < min_data_size ||
		    wifi_pkg->package.count > max_data_size)
			continue;

		domain = &wifi_pkg->package.elements[0];
		if (domain->type == ACPI_TYPE_INTEGER &&
		    domain->integer.value == ACPI_WIFI_DOMAIN)
			goto found;
	}

	return ERR_PTR(-ENOENT);

found:
	return wifi_pkg;
}

static union acpi_object *
iwl_acpi_get_wifi_pkg(struct device *dev,
		      union acpi_object *data,
		      int data_size, int *tbl_rev)
{
	return iwl_acpi_get_wifi_pkg_range(dev, data, data_size, data_size,
					   tbl_rev);
}

int iwl_acpi_get_tas_table(struct iwl_fw_runtime *fwrt,
			   struct iwl_tas_data *tas_data)
{
	union acpi_object *wifi_pkg, *data;
	int ret, tbl_rev, block_list_size, enabled;
	u32 tas_selection;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_WTAS_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	/* try to read wtas table */
	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_WTAS_WIFI_DATA_SIZE,
					 &tbl_rev);
	if (IS_ERR(wifi_pkg)) {
		ret = PTR_ERR(wifi_pkg);
		goto out_free;
	}

	if (tbl_rev < 0 || tbl_rev > 2 ||
	    wifi_pkg->package.elements[1].type != ACPI_TYPE_INTEGER) {
		ret = -EINVAL;
		goto out_free;
	}

	tas_selection = (u32)wifi_pkg->package.elements[1].integer.value;
	enabled = tas_selection & IWL_WTAS_ENABLED_MSK;

	IWL_DEBUG_RADIO(fwrt, "TAS selection as read from BIOS: 0x%x\n",
			tas_selection);
	tas_data->table_source = BIOS_SOURCE_ACPI;
	tas_data->table_revision = tbl_rev;
	tas_data->tas_selection = tas_selection;

	IWL_DEBUG_RADIO(fwrt, "TAS %s enabled\n",
			enabled ? "is" : "not");

	IWL_DEBUG_RADIO(fwrt, "Reading TAS table revision %d\n", tbl_rev);
	if (wifi_pkg->package.elements[2].type != ACPI_TYPE_INTEGER ||
	    wifi_pkg->package.elements[2].integer.value >
	    IWL_WTAS_BLACK_LIST_MAX) {
		IWL_DEBUG_RADIO(fwrt, "TAS invalid array size %llu\n",
				wifi_pkg->package.elements[2].integer.value);
		ret = -EINVAL;
		goto out_free;
	}

	block_list_size = wifi_pkg->package.elements[2].integer.value;
	tas_data->block_list_size = block_list_size;

	IWL_DEBUG_RADIO(fwrt, "TAS array size %u\n", block_list_size);

	for (int i = 0; i < block_list_size; i++) {
		u16 country;

		if (wifi_pkg->package.elements[3 + i].type !=
		    ACPI_TYPE_INTEGER) {
			IWL_DEBUG_RADIO(fwrt,
					"TAS invalid array elem %d\n", 3 + i);
			ret = -EINVAL;
			goto out_free;
		}

		country = wifi_pkg->package.elements[3 + i].integer.value;
		tas_data->block_list_array[i] = country;
		IWL_DEBUG_RADIO(fwrt, "TAS block list country %d\n", country);
	}

	ret = enabled;
out_free:
	kfree(data);
	return ret;
}

int iwl_acpi_get_mcc(struct iwl_fw_runtime *fwrt, char *mcc)
{
	union acpi_object *wifi_pkg, *data;
	u32 mcc_val;
	int ret, tbl_rev;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_WRDD_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_WRDD_WIFI_DATA_SIZE,
					 &tbl_rev);
	if (IS_ERR(wifi_pkg)) {
		ret = PTR_ERR(wifi_pkg);
		goto out_free;
	}

	if (wifi_pkg->package.elements[1].type != ACPI_TYPE_INTEGER ||
	    tbl_rev != 0) {
		ret = -EINVAL;
		goto out_free;
	}

	mcc_val = wifi_pkg->package.elements[1].integer.value;
	if (mcc_val != BIOS_MCC_CHINA) {
		ret = -EINVAL;
		IWL_DEBUG_RADIO(fwrt, "ACPI WRDD is supported only for CN\n");
		goto out_free;
	}

	mcc[0] = (mcc_val >> 8) & 0xff;
	mcc[1] = mcc_val & 0xff;
	mcc[2] = '\0';

	ret = 0;
out_free:
	kfree(data);
	return ret;
}

int iwl_acpi_get_pwr_limit(struct iwl_fw_runtime *fwrt, u64 *dflt_pwr_limit)
{
	union acpi_object *data, *wifi_pkg;
	int tbl_rev, ret = -EINVAL;

	*dflt_pwr_limit = 0;
	data = iwl_acpi_get_object(fwrt->dev, ACPI_SPLC_METHOD);
	if (IS_ERR(data))
		goto out;

	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_SPLC_WIFI_DATA_SIZE, &tbl_rev);
	if (IS_ERR(wifi_pkg) || tbl_rev != 0 ||
	    wifi_pkg->package.elements[1].integer.value != ACPI_TYPE_INTEGER)
		goto out_free;

	*dflt_pwr_limit = wifi_pkg->package.elements[1].integer.value;
	ret = 0;
out_free:
	kfree(data);
out:
	return ret;
}

int iwl_acpi_get_eckv(struct iwl_fw_runtime *fwrt, u32 *extl_clk)
{
	union acpi_object *wifi_pkg, *data;
	int ret, tbl_rev;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_ECKV_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_ECKV_WIFI_DATA_SIZE,
					 &tbl_rev);
	if (IS_ERR(wifi_pkg)) {
		ret = PTR_ERR(wifi_pkg);
		goto out_free;
	}

	if (wifi_pkg->package.elements[1].type != ACPI_TYPE_INTEGER ||
	    tbl_rev != 0) {
		ret = -EINVAL;
		goto out_free;
	}

	*extl_clk = wifi_pkg->package.elements[1].integer.value;

	ret = 0;

out_free:
	kfree(data);
	return ret;
}

static int
iwl_acpi_parse_chains_table(union acpi_object *table,
			    struct iwl_sar_profile_chain *chains,
			    u8 num_chains, u8 num_sub_bands)
{
	for (u8 chain = 0; chain < num_chains; chain++) {
		for (u8 subband = 0; subband < BIOS_SAR_MAX_SUB_BANDS_NUM;
		     subband++) {
			/* if we don't have the values, use the default */
			if (subband >= num_sub_bands) {
				chains[chain].subbands[subband] = 0;
			} else if (table->type != ACPI_TYPE_INTEGER ||
				   table->integer.value > U8_MAX) {
				return -EINVAL;
			} else {
				chains[chain].subbands[subband] =
					table->integer.value;
				table++;
			}
		}
	}

	return 0;
}

int iwl_acpi_get_wrds_table(struct iwl_fw_runtime *fwrt)
{
	union acpi_object *wifi_pkg, *table, *data;
	int ret, tbl_rev;
	u32 flags;
	u8 num_chains, num_sub_bands;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_WRDS_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	/* start by trying to read revision 2 */
	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_WRDS_WIFI_DATA_SIZE_REV2,
					 &tbl_rev);
	if (!IS_ERR(wifi_pkg)) {
		if (tbl_rev != 2) {
			ret = -EINVAL;
			goto out_free;
		}

		num_chains = ACPI_SAR_NUM_CHAINS_REV2;
		num_sub_bands = ACPI_SAR_NUM_SUB_BANDS_REV2;

		goto read_table;
	}

	/* then try revision 1 */
	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_WRDS_WIFI_DATA_SIZE_REV1,
					 &tbl_rev);
	if (!IS_ERR(wifi_pkg)) {
		if (tbl_rev != 1) {
			ret = -EINVAL;
			goto out_free;
		}

		num_chains = ACPI_SAR_NUM_CHAINS_REV1;
		num_sub_bands = ACPI_SAR_NUM_SUB_BANDS_REV1;

		goto read_table;
	}

	/* then finally revision 0 */
	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_WRDS_WIFI_DATA_SIZE_REV0,
					 &tbl_rev);
	if (!IS_ERR(wifi_pkg)) {
		if (tbl_rev != 0) {
			ret = -EINVAL;
			goto out_free;
		}

		num_chains = ACPI_SAR_NUM_CHAINS_REV0;
		num_sub_bands = ACPI_SAR_NUM_SUB_BANDS_REV0;

		goto read_table;
	}

	ret = PTR_ERR(wifi_pkg);
	goto out_free;

read_table:
	if (wifi_pkg->package.elements[1].type != ACPI_TYPE_INTEGER) {
		ret = -EINVAL;
		goto out_free;
	}

	IWL_DEBUG_RADIO(fwrt, "Reading WRDS tbl_rev=%d\n", tbl_rev);

	flags = wifi_pkg->package.elements[1].integer.value;
	fwrt->reduced_power_flags = flags >> IWL_REDUCE_POWER_FLAGS_POS;

	/* position of the actual table */
	table = &wifi_pkg->package.elements[2];

	/* The profile from WRDS is officially profile 1, but goes
	 * into sar_profiles[0] (because we don't have a profile 0).
	 */
	ret = iwl_acpi_parse_chains_table(table, fwrt->sar_profiles[0].chains,
					  num_chains, num_sub_bands);
	if (!ret && flags & IWL_SAR_ENABLE_MSK)
		fwrt->sar_profiles[0].enabled = true;

out_free:
	kfree(data);
	return ret;
}

int iwl_acpi_get_ewrd_table(struct iwl_fw_runtime *fwrt)
{
	union acpi_object *wifi_pkg, *data;
	bool enabled;
	int i, n_profiles, tbl_rev, pos;
	int ret = 0;
	u8 num_sub_bands;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_EWRD_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	/* start by trying to read revision 2 */
	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_EWRD_WIFI_DATA_SIZE_REV2,
					 &tbl_rev);
	if (!IS_ERR(wifi_pkg)) {
		if (tbl_rev != 2) {
			ret = -EINVAL;
			goto out_free;
		}

		num_sub_bands = ACPI_SAR_NUM_SUB_BANDS_REV2;

		goto read_table;
	}

	/* then try revision 1 */
	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_EWRD_WIFI_DATA_SIZE_REV1,
					 &tbl_rev);
	if (!IS_ERR(wifi_pkg)) {
		if (tbl_rev != 1) {
			ret = -EINVAL;
			goto out_free;
		}

		num_sub_bands = ACPI_SAR_NUM_SUB_BANDS_REV1;

		goto read_table;
	}

	/* then finally revision 0 */
	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_EWRD_WIFI_DATA_SIZE_REV0,
					 &tbl_rev);
	if (!IS_ERR(wifi_pkg)) {
		if (tbl_rev != 0) {
			ret = -EINVAL;
			goto out_free;
		}

		num_sub_bands = ACPI_SAR_NUM_SUB_BANDS_REV0;

		goto read_table;
	}

	ret = PTR_ERR(wifi_pkg);
	goto out_free;

read_table:
	if (wifi_pkg->package.elements[1].type != ACPI_TYPE_INTEGER ||
	    wifi_pkg->package.elements[2].type != ACPI_TYPE_INTEGER) {
		ret = -EINVAL;
		goto out_free;
	}

	enabled = !!(wifi_pkg->package.elements[1].integer.value);
	n_profiles = wifi_pkg->package.elements[2].integer.value;

	/*
	 * Check the validity of n_profiles.  The EWRD profiles start
	 * from index 1, so the maximum value allowed here is
	 * ACPI_SAR_PROFILES_NUM - 1.
	 */
	if (n_profiles >= BIOS_SAR_MAX_PROFILE_NUM) {
		ret = -EINVAL;
		goto out_free;
	}

	/* the tables start at element 3 */
	pos = 3;

	BUILD_BUG_ON(ACPI_SAR_NUM_CHAINS_REV0 != ACPI_SAR_NUM_CHAINS_REV1);
	BUILD_BUG_ON(ACPI_SAR_NUM_CHAINS_REV2 != 2 * ACPI_SAR_NUM_CHAINS_REV0);

	/* parse non-cdb chains for all profiles */
	for (i = 0; i < n_profiles; i++) {
		union acpi_object *table = &wifi_pkg->package.elements[pos];

		/* The EWRD profiles officially go from 2 to 4, but we
		 * save them in sar_profiles[1-3] (because we don't
		 * have profile 0).  So in the array we start from 1.
		 */
		ret = iwl_acpi_parse_chains_table(table,
						  fwrt->sar_profiles[i + 1].chains,
						  ACPI_SAR_NUM_CHAINS_REV0,
						  num_sub_bands);
		if (ret < 0)
			goto out_free;

		/* go to the next table */
		pos += ACPI_SAR_NUM_CHAINS_REV0 * num_sub_bands;
	}

	/* non-cdb table revisions */
	if (tbl_rev < 2)
		goto set_enabled;

	/* parse cdb chains for all profiles */
	for (i = 0; i < n_profiles; i++) {
		struct iwl_sar_profile_chain *chains;
		union acpi_object *table;

		table = &wifi_pkg->package.elements[pos];
		chains = &fwrt->sar_profiles[i + 1].chains[ACPI_SAR_NUM_CHAINS_REV0];
		ret = iwl_acpi_parse_chains_table(table,
						  chains,
						  ACPI_SAR_NUM_CHAINS_REV0,
						  num_sub_bands);
		if (ret < 0)
			goto out_free;

		/* go to the next table */
		pos += ACPI_SAR_NUM_CHAINS_REV0 * num_sub_bands;
	}

set_enabled:
	for (i = 0; i < n_profiles; i++)
		fwrt->sar_profiles[i + 1].enabled = enabled;

out_free:
	kfree(data);
	return ret;
}

int iwl_acpi_get_wgds_table(struct iwl_fw_runtime *fwrt)
{
	union acpi_object *wifi_pkg, *data;
	int i, j, k, ret, tbl_rev;
	u8 num_bands, num_profiles;
	static const struct {
		u8 revisions;
		u8 bands;
		u8 profiles;
		u8 min_profiles;
	} rev_data[] = {
		{
			.revisions = BIT(3),
			.bands = ACPI_GEO_NUM_BANDS_REV2,
			.profiles = ACPI_NUM_GEO_PROFILES_REV3,
			.min_profiles = BIOS_GEO_MIN_PROFILE_NUM,
		},
		{
			.revisions = BIT(2),
			.bands = ACPI_GEO_NUM_BANDS_REV2,
			.profiles = ACPI_NUM_GEO_PROFILES,
		},
		{
			.revisions = BIT(0) | BIT(1),
			.bands = ACPI_GEO_NUM_BANDS_REV0,
			.profiles = ACPI_NUM_GEO_PROFILES,
		},
	};
	int idx;
	/* start from one to skip the domain */
	int entry_idx = 1;

	BUILD_BUG_ON(ACPI_NUM_GEO_PROFILES_REV3 != IWL_NUM_GEO_PROFILES_V3);
	BUILD_BUG_ON(ACPI_NUM_GEO_PROFILES != IWL_NUM_GEO_PROFILES);

	data = iwl_acpi_get_object(fwrt->dev, ACPI_WGDS_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	/* read the highest revision we understand first */
	for (idx = 0; idx < ARRAY_SIZE(rev_data); idx++) {
		/* min_profiles != 0 requires num_profiles header */
		u32 hdr_size = 1 + !!rev_data[idx].min_profiles;
		u32 profile_size = ACPI_GEO_PER_CHAIN_SIZE *
				   rev_data[idx].bands;
		u32 max_size = hdr_size + profile_size * rev_data[idx].profiles;
		u32 min_size;

		if (!rev_data[idx].min_profiles)
			min_size = max_size;
		else
			min_size = hdr_size +
				   profile_size * rev_data[idx].min_profiles;

		wifi_pkg = iwl_acpi_get_wifi_pkg_range(fwrt->dev, data,
						       min_size, max_size,
						       &tbl_rev);
		if (!IS_ERR(wifi_pkg)) {
			if (!(BIT(tbl_rev) & rev_data[idx].revisions))
				continue;

			num_bands = rev_data[idx].bands;
			num_profiles = rev_data[idx].profiles;

			if (rev_data[idx].min_profiles) {
				/* read header that says # of profiles */
				union acpi_object *entry;

				entry = &wifi_pkg->package.elements[entry_idx];
				entry_idx++;
				if (entry->type != ACPI_TYPE_INTEGER ||
				    entry->integer.value > num_profiles ||
				    entry->integer.value <
					rev_data[idx].min_profiles) {
					ret = -EINVAL;
					goto out_free;
				}

				/*
				 * Check to see if we received package count
				 * same as max # of profiles
				 */
				if (wifi_pkg->package.count !=
				    hdr_size + profile_size * num_profiles) {
					ret = -EINVAL;
					goto out_free;
				}

				/* Number of valid profiles */
				num_profiles = entry->integer.value;
			}
			goto read_table;
		}
	}

	if (idx < ARRAY_SIZE(rev_data))
		ret = PTR_ERR(wifi_pkg);
	else
		ret = -ENOENT;
	goto out_free;

read_table:
	fwrt->geo_rev = tbl_rev;
	for (i = 0; i < num_profiles; i++) {
		for (j = 0; j < BIOS_GEO_MAX_NUM_BANDS; j++) {
			union acpi_object *entry;

			/*
			 * num_bands is either 2 or 3, if it's only 2 then
			 * fill the third band (6 GHz) with the values from
			 * 5 GHz (second band)
			 */
			if (j >= num_bands) {
				fwrt->geo_profiles[i].bands[j].max =
					fwrt->geo_profiles[i].bands[1].max;
			} else {
				entry = &wifi_pkg->package.elements[entry_idx];
				entry_idx++;
				if (entry->type != ACPI_TYPE_INTEGER ||
				    entry->integer.value > U8_MAX) {
					ret = -EINVAL;
					goto out_free;
				}

				fwrt->geo_profiles[i].bands[j].max =
					entry->integer.value;
			}

			for (k = 0; k < BIOS_GEO_NUM_CHAINS; k++) {
				/* same here as above */
				if (j >= num_bands) {
					fwrt->geo_profiles[i].bands[j].chains[k] =
						fwrt->geo_profiles[i].bands[1].chains[k];
				} else {
					entry = &wifi_pkg->package.elements[entry_idx];
					entry_idx++;
					if (entry->type != ACPI_TYPE_INTEGER ||
					    entry->integer.value > U8_MAX) {
						ret = -EINVAL;
						goto out_free;
					}

					fwrt->geo_profiles[i].bands[j].chains[k] =
						entry->integer.value;
				}
			}
		}
	}

	fwrt->geo_num_profiles = num_profiles;
	fwrt->geo_enabled = true;
	ret = 0;
out_free:
	kfree(data);
	return ret;
}

int iwl_acpi_get_ppag_table(struct iwl_fw_runtime *fwrt)
{
	union acpi_object *wifi_pkg, *data, *flags;
	int i, j, ret, tbl_rev, num_sub_bands = 0;
	int idx = 2;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_PPAG_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	/* try to read ppag table rev 3, 2 or 1 (all have the same data size) */
	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
				ACPI_PPAG_WIFI_DATA_SIZE_V2, &tbl_rev);

	if (!IS_ERR(wifi_pkg)) {
		if (tbl_rev >= 1 && tbl_rev <= 3) {
			num_sub_bands = IWL_NUM_SUB_BANDS_V2;
			IWL_DEBUG_RADIO(fwrt,
					"Reading PPAG table (tbl_rev=%d)\n",
					tbl_rev);
			goto read_table;
		} else {
			ret = -EINVAL;
			goto out_free;
		}
	}

	/* try to read ppag table revision 0 */
	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
			ACPI_PPAG_WIFI_DATA_SIZE_V1, &tbl_rev);

	if (!IS_ERR(wifi_pkg)) {
		if (tbl_rev != 0) {
			ret = -EINVAL;
			goto out_free;
		}
		num_sub_bands = IWL_NUM_SUB_BANDS_V1;
		IWL_DEBUG_RADIO(fwrt, "Reading PPAG table v1 (tbl_rev=0)\n");
		goto read_table;
	}

	ret = PTR_ERR(wifi_pkg);
	goto out_free;

read_table:
	fwrt->ppag_bios_rev = tbl_rev;
	flags = &wifi_pkg->package.elements[1];

	if (flags->type != ACPI_TYPE_INTEGER) {
		ret = -EINVAL;
		goto out_free;
	}

	fwrt->ppag_flags = iwl_bios_get_ppag_flags(flags->integer.value,
						   fwrt->ppag_bios_rev);

	/*
	 * read, verify gain values and save them into the PPAG table.
	 * first sub-band (j=0) corresponds to Low-Band (2.4GHz), and the
	 * following sub-bands to High-Band (5GHz).
	 */
	for (i = 0; i < IWL_NUM_CHAIN_LIMITS; i++) {
		for (j = 0; j < num_sub_bands; j++) {
			union acpi_object *ent;

			ent = &wifi_pkg->package.elements[idx++];
			if (ent->type != ACPI_TYPE_INTEGER) {
				ret = -EINVAL;
				goto out_free;
			}

			fwrt->ppag_chains[i].subbands[j] = ent->integer.value;
		}
	}

	ret = 0;

out_free:
	kfree(data);
	return ret;
}

int iwl_acpi_get_phy_filters(struct iwl_fw_runtime *fwrt)
{
	struct iwl_phy_specific_cfg *filters = &fwrt->phy_filters;
	struct iwl_phy_specific_cfg tmp = {};
	union acpi_object *wifi_pkg, *data __free(kfree);
	int tbl_rev, i;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_WPFC_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_WPFC_WIFI_DATA_SIZE,
					 &tbl_rev);
	if (IS_ERR(wifi_pkg))
		return PTR_ERR(wifi_pkg);

	if (tbl_rev != 0)
		return -EINVAL;

	BUILD_BUG_ON(ARRAY_SIZE(filters->filter_cfg_chains) !=
		     ACPI_WPFC_WIFI_DATA_SIZE - 1);

	for (i = 0; i < ARRAY_SIZE(filters->filter_cfg_chains); i++) {
		if (wifi_pkg->package.elements[i + 1].type != ACPI_TYPE_INTEGER)
			return -EINVAL;
		tmp.filter_cfg_chains[i] =
			cpu_to_le32(wifi_pkg->package.elements[i + 1].integer.value);
	}

	IWL_DEBUG_RADIO(fwrt, "Loaded WPFC filter config from ACPI\n");
	*filters = tmp;
	return 0;
}
IWL_EXPORT_SYMBOL(iwl_acpi_get_phy_filters);

void iwl_acpi_get_guid_lock_status(struct iwl_fw_runtime *fwrt)
{
	union acpi_object *wifi_pkg, *data;
	int tbl_rev;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_GLAI_METHOD);
	if (IS_ERR(data))
		return;

	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_GLAI_WIFI_DATA_SIZE,
					 &tbl_rev);
	if (IS_ERR(wifi_pkg))
		goto out_free;

	if (tbl_rev != 0) {
		IWL_DEBUG_RADIO(fwrt, "Invalid GLAI revision: %d\n", tbl_rev);
		goto out_free;
	}

	if (wifi_pkg->package.elements[1].type != ACPI_TYPE_INTEGER ||
	    wifi_pkg->package.elements[1].integer.value > ACPI_GLAI_MAX_STATUS)
		goto out_free;

	fwrt->uefi_tables_lock_status =
		wifi_pkg->package.elements[1].integer.value;

	IWL_DEBUG_RADIO(fwrt,
			"Loaded UEFI WIFI GUID lock status: %d from ACPI\n",
			fwrt->uefi_tables_lock_status);
out_free:
	kfree(data);
}
IWL_EXPORT_SYMBOL(iwl_acpi_get_guid_lock_status);

int iwl_acpi_get_wbem(struct iwl_fw_runtime *fwrt, u32 *value)
{
	union acpi_object *wifi_pkg, *data;
	int ret = -ENOENT;
	int tbl_rev;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_WBEM_METHOD);
	if (IS_ERR(data))
		return ret;

	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_WBEM_WIFI_DATA_SIZE,
					 &tbl_rev);
	if (IS_ERR(wifi_pkg))
		goto out_free;

	if (tbl_rev != IWL_ACPI_WBEM_REVISION) {
		IWL_DEBUG_RADIO(fwrt, "Unsupported ACPI WBEM revision:%d\n",
				tbl_rev);
		goto out_free;
	}

	if (wifi_pkg->package.elements[1].type != ACPI_TYPE_INTEGER)
		goto out_free;

	*value = wifi_pkg->package.elements[1].integer.value &
		 IWL_ACPI_WBEM_REV0_MASK;
	IWL_DEBUG_RADIO(fwrt, "Loaded WBEM config from ACPI\n");
	ret = 0;
out_free:
	kfree(data);
	return ret;
}

int iwl_acpi_get_dsbr(struct iwl_fw_runtime *fwrt, u32 *value)
{
	union acpi_object *wifi_pkg, *data;
	int ret = -ENOENT;
	int tbl_rev;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_DSBR_METHOD);
	if (IS_ERR(data))
		return ret;

	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_DSBR_WIFI_DATA_SIZE,
					 &tbl_rev);
	if (IS_ERR(wifi_pkg))
		goto out_free;

	if (tbl_rev != ACPI_DSBR_WIFI_DATA_REV) {
		IWL_DEBUG_RADIO(fwrt, "Unsupported ACPI DSBR revision:%d\n",
				tbl_rev);
		goto out_free;
	}

	if (wifi_pkg->package.elements[1].type != ACPI_TYPE_INTEGER)
		goto out_free;

	*value = wifi_pkg->package.elements[1].integer.value;
	IWL_DEBUG_RADIO(fwrt, "Loaded DSBR config from ACPI value: 0x%x\n",
			*value);
	ret = 0;
out_free:
	kfree(data);
	return ret;
}
