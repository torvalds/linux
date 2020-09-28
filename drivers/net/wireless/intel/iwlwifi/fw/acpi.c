/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017        Intel Deutschland GmbH
 * Copyright (C) 2019 - 2020 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2017        Intel Deutschland GmbH
 * Copyright (C) 2019 - 2020 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#include <linux/uuid.h>
#include "iwl-drv.h"
#include "iwl-debug.h"
#include "acpi.h"
#include "fw/runtime.h"

static const guid_t intel_wifi_guid = GUID_INIT(0xF21202BF, 0x8F78, 0x4DC6,
						0xA5, 0xB3, 0x1F, 0x73,
						0x8E, 0x28, 0x5A, 0xDE);

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

void *iwl_acpi_get_object(struct device *dev, acpi_string method)
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
IWL_EXPORT_SYMBOL(iwl_acpi_get_object);

/**
* Generic function for evaluating a method defined in the device specific
* method (DSM) interface. The returned acpi object must be freed by calling
* function.
*/
static void *iwl_acpi_get_dsm_object(struct device *dev, int rev, int func,
				     union acpi_object *args)
{
	union acpi_object *obj;

	obj = acpi_evaluate_dsm(ACPI_HANDLE(dev), &intel_wifi_guid, rev, func,
				args);
	if (!obj) {
		IWL_DEBUG_DEV_RADIO(dev,
				    "ACPI: DSM method invocation failed (rev: %d, func:%d)\n",
				    rev, func);
		return ERR_PTR(-ENOENT);
	}
	return obj;
}

/**
 * Evaluate a DSM with no arguments and a single u8 return value (inside a
 * buffer object), verify and return that value.
 */
int iwl_acpi_get_dsm_u8(struct device *dev, int rev, int func)
{
	union acpi_object *obj;
	int ret;

	obj = iwl_acpi_get_dsm_object(dev, rev, func, NULL);
	if (IS_ERR(obj))
		return -ENOENT;

	if (obj->type != ACPI_TYPE_BUFFER) {
		IWL_DEBUG_DEV_RADIO(dev,
				    "ACPI: DSM method did not return a valid object, type=%d\n",
				    obj->type);
		ret = -EINVAL;
		goto out;
	}

	if (obj->buffer.length != sizeof(u8)) {
		IWL_DEBUG_DEV_RADIO(dev,
				    "ACPI: DSM method returned invalid buffer, length=%d\n",
				    obj->buffer.length);
		ret = -EINVAL;
		goto out;
	}

	ret = obj->buffer.pointer[0];
	IWL_DEBUG_DEV_RADIO(dev,
			    "ACPI: DSM method evaluated: func=%d, ret=%d\n",
			    func, ret);
out:
	ACPI_FREE(obj);
	return ret;
}
IWL_EXPORT_SYMBOL(iwl_acpi_get_dsm_u8);

union acpi_object *iwl_acpi_get_wifi_pkg(struct device *dev,
					 union acpi_object *data,
					 int data_size, int *tbl_rev)
{
	int i;
	union acpi_object *wifi_pkg;

	/*
	 * We need at least one entry in the wifi package that
	 * describes the domain, and one more entry, otherwise there's
	 * no point in reading it.
	 */
	if (WARN_ON_ONCE(data_size < 2))
		return ERR_PTR(-EINVAL);

	/*
	 * We need at least two packages, one for the revision and one
	 * for the data itself.  Also check that the revision is valid
	 * (i.e. it is an integer smaller than 2, as we currently support only
	 * 2 revisions).
	 */
	if (data->type != ACPI_TYPE_PACKAGE ||
	    data->package.count < 2 ||
	    data->package.elements[0].type != ACPI_TYPE_INTEGER ||
	    data->package.elements[0].integer.value > 1) {
		IWL_DEBUG_DEV_RADIO(dev, "Unsupported packages structure\n");
		return ERR_PTR(-EINVAL);
	}

	*tbl_rev = data->package.elements[0].integer.value;

	/* loop through all the packages to find the one for WiFi */
	for (i = 1; i < data->package.count; i++) {
		union acpi_object *domain;

		wifi_pkg = &data->package.elements[i];

		/* skip entries that are not a package with the right size */
		if (wifi_pkg->type != ACPI_TYPE_PACKAGE ||
		    wifi_pkg->package.count != data_size)
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
IWL_EXPORT_SYMBOL(iwl_acpi_get_wifi_pkg);

int iwl_acpi_get_tas(struct iwl_fw_runtime *fwrt,
		     __le32 *black_list_array,
		     int *black_list_size)
{
	union acpi_object *wifi_pkg, *data;
	int ret, tbl_rev, i;
	bool enabled;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_WTAS_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_WTAS_WIFI_DATA_SIZE,
					 &tbl_rev);
	if (IS_ERR(wifi_pkg)) {
		ret = PTR_ERR(wifi_pkg);
		goto out_free;
	}

	if (wifi_pkg->package.elements[0].type != ACPI_TYPE_INTEGER ||
	    tbl_rev != 0) {
		ret = -EINVAL;
		goto out_free;
	}

	enabled = !!wifi_pkg->package.elements[0].integer.value;

	if (!enabled) {
		*black_list_size = -1;
		IWL_DEBUG_RADIO(fwrt, "TAS not enabled\n");
		ret = 0;
		goto out_free;
	}

	if (wifi_pkg->package.elements[1].type != ACPI_TYPE_INTEGER ||
	    wifi_pkg->package.elements[1].integer.value >
	    APCI_WTAS_BLACK_LIST_MAX) {
		IWL_DEBUG_RADIO(fwrt, "TAS invalid array size %llu\n",
				wifi_pkg->package.elements[1].integer.value);
		ret = -EINVAL;
		goto out_free;
	}
	*black_list_size = wifi_pkg->package.elements[1].integer.value;

	IWL_DEBUG_RADIO(fwrt, "TAS array size %d\n", *black_list_size);
	if (*black_list_size > APCI_WTAS_BLACK_LIST_MAX) {
		IWL_DEBUG_RADIO(fwrt, "TAS invalid array size value %u\n",
				*black_list_size);
		ret = -EINVAL;
		goto out_free;
	}

	for (i = 0; i < *black_list_size; i++) {
		u32 country;

		if (wifi_pkg->package.elements[2 + i].type !=
		    ACPI_TYPE_INTEGER) {
			IWL_DEBUG_RADIO(fwrt,
					"TAS invalid array elem %d\n", 2 + i);
			ret = -EINVAL;
			goto out_free;
		}

		country = wifi_pkg->package.elements[2 + i].integer.value;
		black_list_array[i] = cpu_to_le32(country);
		IWL_DEBUG_RADIO(fwrt, "TAS black list country %d\n", country);
	}

	ret = 0;
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
			       bool enabled)
{
	int i;

	profile->enabled = enabled;

	for (i = 0; i < ACPI_SAR_TABLE_SIZE; i++) {
		if (table[i].type != ACPI_TYPE_INTEGER ||
		    table[i].integer.value > U8_MAX)
			return -EINVAL;

		profile->table[i] = table[i].integer.value;
	}

	return 0;
}

int iwl_sar_select_profile(struct iwl_fw_runtime *fwrt,
			   __le16 per_chain[][IWL_NUM_SUB_BANDS],
			   int prof_a, int prof_b)
{
	int i, j, idx;
	int profs[ACPI_SAR_NUM_CHAIN_LIMITS] = { prof_a, prof_b };

	BUILD_BUG_ON(ACPI_SAR_NUM_CHAIN_LIMITS < 2);
	BUILD_BUG_ON(ACPI_SAR_NUM_CHAIN_LIMITS * ACPI_SAR_NUM_SUB_BANDS !=
		     ACPI_SAR_TABLE_SIZE);

	for (i = 0; i < ACPI_SAR_NUM_CHAIN_LIMITS; i++) {
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
		for (j = 0; j < ACPI_SAR_NUM_SUB_BANDS; j++) {
			idx = (i * ACPI_SAR_NUM_SUB_BANDS) + j;
			per_chain[i][j] = cpu_to_le16(prof->table[idx]);
			IWL_DEBUG_RADIO(fwrt, "    Band[%d] = %d * .125dBm\n",
					j, prof->table[idx]);
		}
	}

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_sar_select_profile);

int iwl_sar_get_wrds_table(struct iwl_fw_runtime *fwrt)
{
	union acpi_object *wifi_pkg, *table, *data;
	bool enabled;
	int ret, tbl_rev;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_WRDS_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_WRDS_WIFI_DATA_SIZE, &tbl_rev);
	if (IS_ERR(wifi_pkg) || tbl_rev != 0) {
		ret = PTR_ERR(wifi_pkg);
		goto out_free;
	}

	if (wifi_pkg->package.elements[1].type != ACPI_TYPE_INTEGER) {
		ret = -EINVAL;
		goto out_free;
	}

	enabled = !!(wifi_pkg->package.elements[1].integer.value);

	/* position of the actual table */
	table = &wifi_pkg->package.elements[2];

	/* The profile from WRDS is officially profile 1, but goes
	 * into sar_profiles[0] (because we don't have a profile 0).
	 */
	ret = iwl_sar_set_profile(table, &fwrt->sar_profiles[0], enabled);
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

	data = iwl_acpi_get_object(fwrt->dev, ACPI_EWRD_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_EWRD_WIFI_DATA_SIZE, &tbl_rev);
	if (IS_ERR(wifi_pkg) || tbl_rev != 0) {
		ret = PTR_ERR(wifi_pkg);
		goto out_free;
	}

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
					  &fwrt->sar_profiles[i + 1],
					  enabled);
		if (ret < 0)
			break;

		/* go to the next table */
		pos += ACPI_SAR_TABLE_SIZE;
	}

out_free:
	kfree(data);
	return ret;
}
IWL_EXPORT_SYMBOL(iwl_sar_get_ewrd_table);

int iwl_sar_get_wgds_table(struct iwl_fw_runtime *fwrt)
{
	union acpi_object *wifi_pkg, *data;
	int i, j, ret, tbl_rev;
	int idx = 1;

	data = iwl_acpi_get_object(fwrt->dev, ACPI_WGDS_METHOD);
	if (IS_ERR(data))
		return PTR_ERR(data);

	wifi_pkg = iwl_acpi_get_wifi_pkg(fwrt->dev, data,
					 ACPI_WGDS_WIFI_DATA_SIZE, &tbl_rev);
	if (IS_ERR(wifi_pkg) || tbl_rev > 1) {
		ret = PTR_ERR(wifi_pkg);
		goto out_free;
	}

	fwrt->geo_rev = tbl_rev;
	for (i = 0; i < ACPI_NUM_GEO_PROFILES; i++) {
		for (j = 0; j < ACPI_GEO_TABLE_SIZE; j++) {
			union acpi_object *entry;

			entry = &wifi_pkg->package.elements[idx++];
			if (entry->type != ACPI_TYPE_INTEGER ||
			    entry->integer.value > U8_MAX) {
				ret = -EINVAL;
				goto out_free;
			}

			fwrt->geo_profiles[i].values[j] = entry->integer.value;
		}
	}
	ret = 0;
out_free:
	kfree(data);
	return ret;
}
IWL_EXPORT_SYMBOL(iwl_sar_get_wgds_table);

bool iwl_sar_geo_support(struct iwl_fw_runtime *fwrt)
{
	/*
	 * The GEO_TX_POWER_LIMIT command is not supported on earlier
	 * firmware versions.  Unfortunately, we don't have a TLV API
	 * flag to rely on, so rely on the major version which is in
	 * the first byte of ucode_ver.  This was implemented
	 * initially on version 38 and then backported to 17.  It was
	 * also backported to 29, but only for 7265D devices.  The
	 * intention was to have it in 36 as well, but not all 8000
	 * family got this feature enabled.  The 8000 family is the
	 * only one using version 36, so skip this version entirely.
	 */
	return IWL_UCODE_SERIAL(fwrt->fw->ucode_ver) >= 38 ||
	       IWL_UCODE_SERIAL(fwrt->fw->ucode_ver) == 17 ||
	       (IWL_UCODE_SERIAL(fwrt->fw->ucode_ver) == 29 &&
		((fwrt->trans->hw_rev & CSR_HW_REV_TYPE_MSK) ==
		 CSR_HW_REV_TYPE_7265D));
}
IWL_EXPORT_SYMBOL(iwl_sar_geo_support);

int iwl_sar_geo_init(struct iwl_fw_runtime *fwrt,
		     struct iwl_per_chain_offset_group *table)
{
	int ret, i, j;

	if (!iwl_sar_geo_support(fwrt))
		return -EOPNOTSUPP;

	ret = iwl_sar_get_wgds_table(fwrt);
	if (ret < 0) {
		IWL_DEBUG_RADIO(fwrt,
				"Geo SAR BIOS table invalid or unavailable. (%d)\n",
				ret);
		/* we don't fail if the table is not available */
		return -ENOENT;
	}

	BUILD_BUG_ON(ACPI_NUM_GEO_PROFILES * ACPI_WGDS_NUM_BANDS *
		     ACPI_WGDS_TABLE_SIZE + 1 !=  ACPI_WGDS_WIFI_DATA_SIZE);

	BUILD_BUG_ON(ACPI_NUM_GEO_PROFILES > IWL_NUM_GEO_PROFILES);

	for (i = 0; i < ACPI_NUM_GEO_PROFILES; i++) {
		struct iwl_per_chain_offset *chain =
			(struct iwl_per_chain_offset *)&table[i];

		for (j = 0; j < ACPI_WGDS_NUM_BANDS; j++) {
			u8 *value;

			value = &fwrt->geo_profiles[i].values[j *
				ACPI_GEO_PER_CHAIN_SIZE];
			chain[j].max_tx_power = cpu_to_le16(value[0]);
			chain[j].chain_a = value[1];
			chain[j].chain_b = value[2];
			IWL_DEBUG_RADIO(fwrt,
					"SAR geographic profile[%d] Band[%d]: chain A = %d chain B = %d max_tx_power = %d\n",
					i, j, value[1], value[2], value[0]);
		}
	}

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_sar_geo_init);
