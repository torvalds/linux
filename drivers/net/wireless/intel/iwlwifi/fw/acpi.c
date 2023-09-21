// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2017 Intel Deutschland GmbH
 * Copyright (C) 2019-2023 Intel Corporation
 */
#include <linux/uuid.h>
#include <linux/dmi.h>
#include "iwl-drv.h"
#include "iwl-debug.h"
#include "acpi.h"
#include "fw/runtime.h"

const guid_t iwl_guid = GUID_INIT(0xF21202BF, 0x8F78, 0x4DC6,
				  0xA5, 0xB3, 0x1F, 0x73,
				  0x8E, 0x28, 0x5A, 0xDE);
IWL_EXPORT_SYMBOL(iwl_guid);

const guid_t iwl_rfi_guid = GUID_INIT(0x7266172C, 0x220B, 0x4B29,
				      0x81, 0x4F, 0x75, 0xE4,
				      0xDD, 0x26, 0xB5, 0xFD);
IWL_EXPORT_SYMBOL(iwl_rfi_guid);

static const struct dmi_system_id dmi_ppag_approved_list[] = {
	{ .ident = "HP",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
		},
	},
	{ .ident = "SAMSUNG",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD"),
		},
	},
	{ .ident = "MSFT",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Microsoft Corporation"),
		},
	},
	{ .ident = "ASUS",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		},
	},
	{ .ident = "GOOGLE-HP",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_BOARD_VENDOR, "HP"),
		},
	},
	{ .ident = "GOOGLE-ASUS",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTek COMPUTER INC."),
		},
	},
	{ .ident = "GOOGLE-SAMSUNG",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Google"),
			DMI_MATCH(DMI_BOARD_VENDOR, "SAMSUNG ELECTRONICS CO., LTD"),
		},
	},
	{ .ident = "DELL",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		},
	},
	{ .ident = "DELL",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Alienware"),
		},
	},
	{ .ident = "RAZER",
	  .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Razer"),
		},
	},
	{}
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
static void *iwl_acpi_get_dsm_object(struct device *dev, int rev, int func,
				     union acpi_object *args,
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
	int ret = 0;

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

		if (WARN_ON_ONCE(expected_size > sizeof(le_value)))
			return -EINVAL;

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
			    "ACPI: DSM method evaluated: func=%d, ret=%d\n",
			    func, ret);
out:
	ACPI_FREE(obj);
	return ret;
}

/*
 * Evaluate a DSM with no arguments and a u8 return value,
 */
int iwl_acpi_get_dsm_u8(struct device *dev, int rev, int func,
			const guid_t *guid, u8 *value)
{
	int ret;
	u64 val;

	ret = iwl_acpi_get_dsm_integer(dev, rev, func,
				       guid, &val, sizeof(u8));

	if (ret < 0)
		return ret;

	/* cast val (u64) to be u8 */
	*value = (u8)val;
	return 0;
}
IWL_EXPORT_SYMBOL(iwl_acpi_get_dsm_u8);

/*
 * Evaluate a DSM with no arguments and a u32 return value,
 */
int iwl_acpi_get_dsm_u32(struct device *dev, int rev, int func,
			 const guid_t *guid, u32 *value)
{
	int ret;
	u64 val;

	ret = iwl_acpi_get_dsm_integer(dev, rev, func,
				       guid, &val, sizeof(u32));

	if (ret < 0)
		return ret;

	/* cast val (u64) to be u32 */
	*value = (u32)val;
	return 0;
}
IWL_EXPORT_SYMBOL(iwl_acpi_get_dsm_u32);

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


int iwl_acpi_get_tas(struct iwl_fw_runtime *fwrt,
		     union iwl_tas_config_cmd *cmd, int fw_ver)
{
	union acpi_object *wifi_pkg, *data;
	int ret, tbl_rev, i, block_list_size, enabled;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_WTAS_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	/* try to read wtas table revision 1 or revision 0*/
	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_WTAS_WIFI_DATA_SIZE,
					 &tbl_rev);
	if (IS_ERR(wifi_pkg)) {
		ret = PTR_ERR(wifi_pkg);
		goto out_free;
	}

	if (tbl_rev == 1 && wifi_pkg->package.elements[1].type ==
		ACPI_TYPE_INTEGER) {
		u32 tas_selection =
			(u32)wifi_pkg->package.elements[1].integer.value;
		u16 override_iec =
			(tas_selection & ACPI_WTAS_OVERRIDE_IEC_MSK) >> ACPI_WTAS_OVERRIDE_IEC_POS;
		u16 enabled_iec = (tas_selection & ACPI_WTAS_ENABLE_IEC_MSK) >>
			ACPI_WTAS_ENABLE_IEC_POS;
		u8 usa_tas_uhb = (tas_selection & ACPI_WTAS_USA_UHB_MSK) >> ACPI_WTAS_USA_UHB_POS;


		enabled = tas_selection & ACPI_WTAS_ENABLED_MSK;
		if (fw_ver <= 3) {
			cmd->v3.override_tas_iec = cpu_to_le16(override_iec);
			cmd->v3.enable_tas_iec = cpu_to_le16(enabled_iec);
		} else {
			cmd->v4.usa_tas_uhb_allowed = usa_tas_uhb;
			cmd->v4.override_tas_iec = (u8)override_iec;
			cmd->v4.enable_tas_iec = (u8)enabled_iec;
		}

	} else if (tbl_rev == 0 &&
		wifi_pkg->package.elements[1].type == ACPI_TYPE_INTEGER) {
		enabled = !!wifi_pkg->package.elements[1].integer.value;
	} else {
		ret = -EINVAL;
		goto out_free;
	}

	if (!enabled) {
		IWL_DEBUG_RADIO(fwrt, "TAS not enabled\n");
		ret = 0;
		goto out_free;
	}

	IWL_DEBUG_RADIO(fwrt, "Reading TAS table revision %d\n", tbl_rev);
	if (wifi_pkg->package.elements[2].type != ACPI_TYPE_INTEGER ||
	    wifi_pkg->package.elements[2].integer.value >
	    APCI_WTAS_BLACK_LIST_MAX) {
		IWL_DEBUG_RADIO(fwrt, "TAS invalid array size %llu\n",
				wifi_pkg->package.elements[2].integer.value);
		ret = -EINVAL;
		goto out_free;
	}
	block_list_size = wifi_pkg->package.elements[2].integer.value;
	cmd->v4.block_list_size = cpu_to_le32(block_list_size);

	IWL_DEBUG_RADIO(fwrt, "TAS array size %u\n", block_list_size);
	if (block_list_size > APCI_WTAS_BLACK_LIST_MAX) {
		IWL_DEBUG_RADIO(fwrt, "TAS invalid array size value %u\n",
				block_list_size);
		ret = -EINVAL;
		goto out_free;
	}

	for (i = 0; i < block_list_size; i++) {
		u32 country;

		if (wifi_pkg->package.elements[3 + i].type !=
		    ACPI_TYPE_INTEGER) {
			IWL_DEBUG_RADIO(fwrt,
					"TAS invalid array elem %d\n", 3 + i);
			ret = -EINVAL;
			goto out_free;
		}

		country = wifi_pkg->package.elements[3 + i].integer.value;
		cmd->v4.block_list_array[i] = cpu_to_le32(country);
		IWL_DEBUG_RADIO(fwrt, "TAS block list country %d\n", country);
	}

	ret = 1;
out_free:
	kfree(data);
	return ret;
}
IWL_EXPORT_SYMBOL(iwl_acpi_get_tas);

int iwl_acpi_get_mcc(struct device *dev, char *mcc)
{
	union acpi_object *wifi_pkg, *data;
	u32 mcc_val;
	int ret, tbl_rev;

	data = iwl_acpi_get_object(dev, ACPI_WRDD_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	wifi_pkg = iwl_acpi_get_wifi_pkg(dev, data, ACPI_WRDD_WIFI_DATA_SIZE,
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

	mcc[0] = (mcc_val >> 8) & 0xff;
	mcc[1] = mcc_val & 0xff;
	mcc[2] = '\0';

	ret = 0;
out_free:
	kfree(data);
	return ret;
}
IWL_EXPORT_SYMBOL(iwl_acpi_get_mcc);

u64 iwl_acpi_get_pwr_limit(struct device *dev)
{
	union acpi_object *data, *wifi_pkg;
	u64 dflt_pwr_limit;
	int tbl_rev;

	data = iwl_acpi_get_object(dev, ACPI_SPLC_METHOD);
	if (IS_ERR(data)) {
		dflt_pwr_limit = 0;
		goto out;
	}

	wifi_pkg = iwl_acpi_get_wifi_pkg(dev, data,
					 ACPI_SPLC_WIFI_DATA_SIZE, &tbl_rev);
	if (IS_ERR(wifi_pkg) || tbl_rev != 0 ||
	    wifi_pkg->package.elements[1].integer.value != ACPI_TYPE_INTEGER) {
		dflt_pwr_limit = 0;
		goto out_free;
	}

	dflt_pwr_limit = wifi_pkg->package.elements[1].integer.value;
out_free:
	kfree(data);
out:
	return dflt_pwr_limit;
}
IWL_EXPORT_SYMBOL(iwl_acpi_get_pwr_limit);

int iwl_acpi_get_eckv(struct device *dev, u32 *extl_clk)
{
	union acpi_object *wifi_pkg, *data;
	int ret, tbl_rev;

	data = iwl_acpi_get_object(dev, ACPI_ECKV_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	wifi_pkg = iwl_acpi_get_wifi_pkg(dev, data, ACPI_ECKV_WIFI_DATA_SIZE,
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
IWL_EXPORT_SYMBOL(iwl_acpi_get_eckv);

static int iwl_sar_set_profile(union acpi_object *table,
			       struct iwl_sar_profile *profile,
			       bool enabled, u8 num_chains, u8 num_sub_bands)
{
	int i, j, idx = 0;

	/*
	 * The table from ACPI is flat, but we store it in a
	 * structured array.
	 */
	for (i = 0; i < ACPI_SAR_NUM_CHAINS_REV2; i++) {
		for (j = 0; j < ACPI_SAR_NUM_SUB_BANDS_REV2; j++) {
			/* if we don't have the values, use the default */
			if (i >= num_chains || j >= num_sub_bands) {
				profile->chains[i].subbands[j] = 0;
			} else {
				if (table[idx].type != ACPI_TYPE_INTEGER ||
				    table[idx].integer.value > U8_MAX)
					return -EINVAL;

				profile->chains[i].subbands[j] =
					table[idx].integer.value;

				idx++;
			}
		}
	}

	/* Only if all values were valid can the profile be enabled */
	profile->enabled = enabled;

	return 0;
}

static int iwl_sar_fill_table(struct iwl_fw_runtime *fwrt,
			      __le16 *per_chain, u32 n_subbands,
			      int prof_a, int prof_b)
{
	int profs[ACPI_SAR_NUM_CHAINS_REV0] = { prof_a, prof_b };
	int i, j;

	for (i = 0; i < ACPI_SAR_NUM_CHAINS_REV0; i++) {
		struct iwl_sar_profile *prof;

		/* don't allow SAR to be disabled (profile 0 means disable) */
		if (profs[i] == 0)
			return -EPERM;

		/* we are off by one, so allow up to ACPI_SAR_PROFILE_NUM */
		if (profs[i] > ACPI_SAR_PROFILE_NUM)
			return -EINVAL;

		/* profiles go from 1 to 4, so decrement to access the array */
		prof = &fwrt->sar_profiles[profs[i] - 1];

		/* if the profile is disabled, do nothing */
		if (!prof->enabled) {
			IWL_DEBUG_RADIO(fwrt, "SAR profile %d is disabled.\n",
					profs[i]);
			/*
			 * if one of the profiles is disabled, we
			 * ignore all of them and return 1 to
			 * differentiate disabled from other failures.
			 */
			return 1;
		}

		IWL_DEBUG_INFO(fwrt,
			       "SAR EWRD: chain %d profile index %d\n",
			       i, profs[i]);
		IWL_DEBUG_RADIO(fwrt, "  Chain[%d]:\n", i);
		for (j = 0; j < n_subbands; j++) {
			per_chain[i * n_subbands + j] =
				cpu_to_le16(prof->chains[i].subbands[j]);
			IWL_DEBUG_RADIO(fwrt, "    Band[%d] = %d * .125dBm\n",
					j, prof->chains[i].subbands[j]);
		}
	}

	return 0;
}

int iwl_sar_select_profile(struct iwl_fw_runtime *fwrt,
			   __le16 *per_chain, u32 n_tables, u32 n_subbands,
			   int prof_a, int prof_b)
{
	int i, ret = 0;

	for (i = 0; i < n_tables; i++) {
		ret = iwl_sar_fill_table(fwrt,
			 &per_chain[i * n_subbands * ACPI_SAR_NUM_CHAINS_REV0],
			 n_subbands, prof_a, prof_b);
		if (ret)
			break;
	}

	return ret;
}
IWL_EXPORT_SYMBOL(iwl_sar_select_profile);

int iwl_sar_get_wrds_table(struct iwl_fw_runtime *fwrt)
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
			ret = PTR_ERR(wifi_pkg);
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
			ret = PTR_ERR(wifi_pkg);
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
			ret = PTR_ERR(wifi_pkg);
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
	ret = iwl_sar_set_profile(table, &fwrt->sar_profiles[0],
				  flags & IWL_SAR_ENABLE_MSK,
				  num_chains, num_sub_bands);
out_free:
	kfree(data);
	return ret;
}
IWL_EXPORT_SYMBOL(iwl_sar_get_wrds_table);

int iwl_sar_get_ewrd_table(struct iwl_fw_runtime *fwrt)
{
	union acpi_object *wifi_pkg, *data;
	bool enabled;
	int i, n_profiles, tbl_rev, pos;
	int ret = 0;
	u8 num_chains, num_sub_bands;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_EWRD_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	/* start by trying to read revision 2 */
	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_EWRD_WIFI_DATA_SIZE_REV2,
					 &tbl_rev);
	if (!IS_ERR(wifi_pkg)) {
		if (tbl_rev != 2) {
			ret = PTR_ERR(wifi_pkg);
			goto out_free;
		}

		num_chains = ACPI_SAR_NUM_CHAINS_REV2;
		num_sub_bands = ACPI_SAR_NUM_SUB_BANDS_REV2;

		goto read_table;
	}

	/* then try revision 1 */
	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_EWRD_WIFI_DATA_SIZE_REV1,
					 &tbl_rev);
	if (!IS_ERR(wifi_pkg)) {
		if (tbl_rev != 1) {
			ret = PTR_ERR(wifi_pkg);
			goto out_free;
		}

		num_chains = ACPI_SAR_NUM_CHAINS_REV1;
		num_sub_bands = ACPI_SAR_NUM_SUB_BANDS_REV1;

		goto read_table;
	}

	/* then finally revision 0 */
	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_EWRD_WIFI_DATA_SIZE_REV0,
					 &tbl_rev);
	if (!IS_ERR(wifi_pkg)) {
		if (tbl_rev != 0) {
			ret = PTR_ERR(wifi_pkg);
			goto out_free;
		}

		num_chains = ACPI_SAR_NUM_CHAINS_REV0;
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
	if (n_profiles <= 0 || n_profiles >= ACPI_SAR_PROFILE_NUM) {
		ret = -EINVAL;
		goto out_free;
	}

	/* the tables start at element 3 */
	pos = 3;

	for (i = 0; i < n_profiles; i++) {
		/* The EWRD profiles officially go from 2 to 4, but we
		 * save them in sar_profiles[1-3] (because we don't
		 * have profile 0).  So in the array we start from 1.
		 */
		ret = iwl_sar_set_profile(&wifi_pkg->package.elements[pos],
					  &fwrt->sar_profiles[i + 1], enabled,
					  num_chains, num_sub_bands);
		if (ret < 0)
			break;

		/* go to the next table */
		pos += num_chains * num_sub_bands;
	}

out_free:
	kfree(data);
	return ret;
}
IWL_EXPORT_SYMBOL(iwl_sar_get_ewrd_table);

int iwl_sar_get_wgds_table(struct iwl_fw_runtime *fwrt)
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
			.min_profiles = 3,
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
				    entry->integer.value > num_profiles) {
					ret = -EINVAL;
					goto out_free;
				}
				num_profiles = entry->integer.value;

				/*
				 * this also validates >= min_profiles since we
				 * otherwise wouldn't have gotten the data when
				 * looking up in ACPI
				 */
				if (wifi_pkg->package.count !=
				    hdr_size + profile_size * num_profiles) {
					ret = -EINVAL;
					goto out_free;
				}
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
		for (j = 0; j < ACPI_GEO_NUM_BANDS_REV2; j++) {
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

			for (k = 0; k < ACPI_GEO_NUM_CHAINS; k++) {
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
IWL_EXPORT_SYMBOL(iwl_sar_get_wgds_table);

bool iwl_sar_geo_support(struct iwl_fw_runtime *fwrt)
{
	/*
	 * The PER_CHAIN_LIMIT_OFFSET_CMD command is not supported on
	 * earlier firmware versions.  Unfortunately, we don't have a
	 * TLV API flag to rely on, so rely on the major version which
	 * is in the first byte of ucode_ver.  This was implemented
	 * initially on version 38 and then backported to 17.  It was
	 * also backported to 29, but only for 7265D devices.  The
	 * intention was to have it in 36 as well, but not all 8000
	 * family got this feature enabled.  The 8000 family is the
	 * only one using version 36, so skip this version entirely.
	 */
	return IWL_UCODE_SERIAL(fwrt->fw->ucode_ver) >= 38 ||
		(IWL_UCODE_SERIAL(fwrt->fw->ucode_ver) == 17 &&
		 fwrt->trans->hw_rev != CSR_HW_REV_TYPE_3160) ||
		(IWL_UCODE_SERIAL(fwrt->fw->ucode_ver) == 29 &&
		 ((fwrt->trans->hw_rev & CSR_HW_REV_TYPE_MSK) ==
		  CSR_HW_REV_TYPE_7265D));
}
IWL_EXPORT_SYMBOL(iwl_sar_geo_support);

int iwl_sar_geo_init(struct iwl_fw_runtime *fwrt,
		     struct iwl_per_chain_offset *table,
		     u32 n_bands, u32 n_profiles)
{
	int i, j;

	if (!fwrt->geo_enabled)
		return -ENODATA;

	if (!iwl_sar_geo_support(fwrt))
		return -EOPNOTSUPP;

	for (i = 0; i < n_profiles; i++) {
		for (j = 0; j < n_bands; j++) {
			struct iwl_per_chain_offset *chain =
				&table[i * n_bands + j];

			chain->max_tx_power =
				cpu_to_le16(fwrt->geo_profiles[i].bands[j].max);
			chain->chain_a = fwrt->geo_profiles[i].bands[j].chains[0];
			chain->chain_b = fwrt->geo_profiles[i].bands[j].chains[1];
			IWL_DEBUG_RADIO(fwrt,
					"SAR geographic profile[%d] Band[%d]: chain A = %d chain B = %d max_tx_power = %d\n",
					i, j,
					fwrt->geo_profiles[i].bands[j].chains[0],
					fwrt->geo_profiles[i].bands[j].chains[1],
					fwrt->geo_profiles[i].bands[j].max);
		}
	}

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_sar_geo_init);

__le32 iwl_acpi_get_lari_config_bitmap(struct iwl_fw_runtime *fwrt)
{
	int ret;
	u8 value;
	u32 val;
	__le32 config_bitmap = 0;

	/*
	 ** Evaluate func 'DSM_FUNC_ENABLE_INDONESIA_5G2'
	 */
	ret = iwl_acpi_get_dsm_u8(fwrt->dev, 0,
				  DSM_FUNC_ENABLE_INDONESIA_5G2,
				  &iwl_guid, &value);

	if (!ret && value == DSM_VALUE_INDONESIA_ENABLE)
		config_bitmap |=
			cpu_to_le32(LARI_CONFIG_ENABLE_5G2_IN_INDONESIA_MSK);

	/*
	 ** Evaluate func 'DSM_FUNC_DISABLE_SRD'
	 */
	ret = iwl_acpi_get_dsm_u8(fwrt->dev, 0,
				  DSM_FUNC_DISABLE_SRD,
				  &iwl_guid, &value);
	if (!ret) {
		if (value == DSM_VALUE_SRD_PASSIVE)
			config_bitmap |=
				cpu_to_le32(LARI_CONFIG_CHANGE_ETSI_TO_PASSIVE_MSK);
		else if (value == DSM_VALUE_SRD_DISABLE)
			config_bitmap |=
				cpu_to_le32(LARI_CONFIG_CHANGE_ETSI_TO_DISABLED_MSK);
	}

	if (fw_has_capa(&fwrt->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_CHINA_22_REG_SUPPORT)) {
		/*
		 ** Evaluate func 'DSM_FUNC_REGULATORY_CONFIG'
		 */
		ret = iwl_acpi_get_dsm_u32(fwrt->dev, 0,
					   DSM_FUNC_REGULATORY_CONFIG,
					   &iwl_guid, &val);
		/*
		 * China 2022 enable if the BIOS object does not exist or
		 * if it is enabled in BIOS.
		 */
		if (ret < 0 || val & DSM_MASK_CHINA_22_REG)
			config_bitmap |=
				cpu_to_le32(LARI_CONFIG_ENABLE_CHINA_22_REG_SUPPORT_MSK);
	}

	return config_bitmap;
}
IWL_EXPORT_SYMBOL(iwl_acpi_get_lari_config_bitmap);

int iwl_acpi_get_ppag_table(struct iwl_fw_runtime *fwrt)
{
	union acpi_object *wifi_pkg, *data, *flags;
	int i, j, ret, tbl_rev, num_sub_bands = 0;
	int idx = 2;
	u8 cmd_ver;

	fwrt->ppag_flags = 0;
	fwrt->ppag_table_valid = false;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_PPAG_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	/* try to read ppag table rev 2 or 1 (both have the same data size) */
	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
				ACPI_PPAG_WIFI_DATA_SIZE_V2, &tbl_rev);

	if (!IS_ERR(wifi_pkg)) {
		if (tbl_rev == 1 || tbl_rev == 2) {
			num_sub_bands = IWL_NUM_SUB_BANDS_V2;
			IWL_DEBUG_RADIO(fwrt,
					"Reading PPAG table v2 (tbl_rev=%d)\n",
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

read_table:
	fwrt->ppag_ver = tbl_rev;
	flags = &wifi_pkg->package.elements[1];

	if (flags->type != ACPI_TYPE_INTEGER) {
		ret = -EINVAL;
		goto out_free;
	}

	fwrt->ppag_flags = flags->integer.value & ACPI_PPAG_MASK;
	cmd_ver = iwl_fw_lookup_cmd_ver(fwrt->fw,
					WIDE_ID(PHY_OPS_GROUP,
						PER_PLATFORM_ANT_GAIN_CMD),
					IWL_FW_CMD_VER_UNKNOWN);
	if (cmd_ver == IWL_FW_CMD_VER_UNKNOWN) {
		ret = -EINVAL;
		goto out_free;
	}
	if (!fwrt->ppag_flags && cmd_ver <= 3) {
		ret = 0;
		goto out_free;
	}

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
			/* from ver 4 the fw deals with out of range values */
			if (cmd_ver >= 4)
				continue;
			if ((j == 0 &&
				(fwrt->ppag_chains[i].subbands[j] > ACPI_PPAG_MAX_LB ||
				 fwrt->ppag_chains[i].subbands[j] < ACPI_PPAG_MIN_LB)) ||
				(j != 0 &&
				(fwrt->ppag_chains[i].subbands[j] > ACPI_PPAG_MAX_HB ||
				fwrt->ppag_chains[i].subbands[j] < ACPI_PPAG_MIN_HB))) {
					ret = -EINVAL;
					goto out_free;
				}
		}
	}

	fwrt->ppag_table_valid = true;
	ret = 0;

out_free:
	kfree(data);
	return ret;
}
IWL_EXPORT_SYMBOL(iwl_acpi_get_ppag_table);

int iwl_read_ppag_table(struct iwl_fw_runtime *fwrt, union iwl_ppag_table_cmd *cmd,
			int *cmd_size)
{
        u8 cmd_ver;
        int i, j, num_sub_bands;
        s8 *gain;

	/* many firmware images for JF lie about this */
	if (CSR_HW_RFID_TYPE(fwrt->trans->hw_rf_id) ==
	    CSR_HW_RFID_TYPE(CSR_HW_RF_ID_TYPE_JF))
		return -EOPNOTSUPP;

        if (!fw_has_capa(&fwrt->fw->ucode_capa, IWL_UCODE_TLV_CAPA_SET_PPAG)) {
                IWL_DEBUG_RADIO(fwrt,
                                "PPAG capability not supported by FW, command not sent.\n");
                return -EINVAL;
	}

	cmd_ver = iwl_fw_lookup_cmd_ver(fwrt->fw,
					WIDE_ID(PHY_OPS_GROUP,
						PER_PLATFORM_ANT_GAIN_CMD),
					IWL_FW_CMD_VER_UNKNOWN);
	if (!fwrt->ppag_table_valid || (cmd_ver <= 3 && !fwrt->ppag_flags)) {
		IWL_DEBUG_RADIO(fwrt, "PPAG not enabled, command not sent.\n");
		return -EINVAL;
	}

        /* The 'flags' field is the same in v1 and in v2 so we can just
         * use v1 to access it.
         */
        cmd->v1.flags = cpu_to_le32(fwrt->ppag_flags);

	IWL_DEBUG_RADIO(fwrt, "PPAG cmd ver is %d\n", cmd_ver);
	if (cmd_ver == 1) {
                num_sub_bands = IWL_NUM_SUB_BANDS_V1;
                gain = cmd->v1.gain[0];
                *cmd_size = sizeof(cmd->v1);
                if (fwrt->ppag_ver == 1 || fwrt->ppag_ver == 2) {
			/* in this case FW supports revision 0 */
                        IWL_DEBUG_RADIO(fwrt,
					"PPAG table rev is %d, send truncated table\n",
                                        fwrt->ppag_ver);
		}
	} else if (cmd_ver >= 2 && cmd_ver <= 4) {
                num_sub_bands = IWL_NUM_SUB_BANDS_V2;
                gain = cmd->v2.gain[0];
                *cmd_size = sizeof(cmd->v2);
                if (fwrt->ppag_ver == 0) {
			/* in this case FW supports revisions 1 or 2 */
                        IWL_DEBUG_RADIO(fwrt,
					"PPAG table rev is 0, send padded table\n");
                }
        } else {
                IWL_DEBUG_RADIO(fwrt, "Unsupported PPAG command version\n");
                return -EINVAL;
        }

	/* ppag mode */
	IWL_DEBUG_RADIO(fwrt,
			"PPAG MODE bits were read from bios: %d\n",
			cmd->v1.flags & cpu_to_le32(ACPI_PPAG_MASK));
	if ((cmd_ver == 1 && !fw_has_capa(&fwrt->fw->ucode_capa,
					  IWL_UCODE_TLV_CAPA_PPAG_CHINA_BIOS_SUPPORT)) ||
	    (cmd_ver == 2 && fwrt->ppag_ver == 2)) {
		cmd->v1.flags &= cpu_to_le32(IWL_PPAG_ETSI_MASK);
		IWL_DEBUG_RADIO(fwrt, "masking ppag China bit\n");
	} else {
		IWL_DEBUG_RADIO(fwrt, "isn't masking ppag China bit\n");
	}

	IWL_DEBUG_RADIO(fwrt,
			"PPAG MODE bits going to be sent: %d\n",
			cmd->v1.flags & cpu_to_le32(ACPI_PPAG_MASK));

	for (i = 0; i < IWL_NUM_CHAIN_LIMITS; i++) {
                for (j = 0; j < num_sub_bands; j++) {
                        gain[i * num_sub_bands + j] =
                                fwrt->ppag_chains[i].subbands[j];
                        IWL_DEBUG_RADIO(fwrt,
                                        "PPAG table: chain[%d] band[%d]: gain = %d\n",
                                        i, j, gain[i * num_sub_bands + j]);
                }
        }

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_read_ppag_table);

bool iwl_acpi_is_ppag_approved(struct iwl_fw_runtime *fwrt)
{

	if (!dmi_check_system(dmi_ppag_approved_list)) {
		IWL_DEBUG_RADIO(fwrt,
			"System vendor '%s' is not in the approved list, disabling PPAG.\n",
			dmi_get_system_info(DMI_SYS_VENDOR));
			fwrt->ppag_flags = 0;
			return false;
	}

	return true;
}
IWL_EXPORT_SYMBOL(iwl_acpi_is_ppag_approved);

void iwl_acpi_get_phy_filters(struct iwl_fw_runtime *fwrt,
			      struct iwl_phy_specific_cfg *filters)
{
	struct iwl_phy_specific_cfg tmp = {};
	union acpi_object *wifi_pkg, *data;
	int tbl_rev, i;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_WPFC_METHOD);
	if (IS_ERR(data))
		return;

	/* try to read wtas table revision 1 or revision 0*/
	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_WPFC_WIFI_DATA_SIZE,
					 &tbl_rev);
	if (IS_ERR(wifi_pkg))
		goto out_free;

	if (tbl_rev != 0)
		goto out_free;

	BUILD_BUG_ON(ARRAY_SIZE(filters->filter_cfg_chains) != ACPI_WPFC_WIFI_DATA_SIZE);

	for (i = 0; i < ARRAY_SIZE(filters->filter_cfg_chains); i++) {
		if (wifi_pkg->package.elements[i].type != ACPI_TYPE_INTEGER)
			return;
		tmp.filter_cfg_chains[i] =
			cpu_to_le32(wifi_pkg->package.elements[i].integer.value);
	}

	IWL_DEBUG_RADIO(fwrt, "Loaded WPFC filter config from ACPI\n");
	*filters = tmp;
out_free:
	kfree(data);
}
IWL_EXPORT_SYMBOL(iwl_acpi_get_phy_filters);
