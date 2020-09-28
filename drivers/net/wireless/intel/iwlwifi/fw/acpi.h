/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2017        Intel Deutschland GmbH
 * Copyright(c) 2018 - 2020        Intel Corporation
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
 * Copyright(c) 2018 - 2020       Intel Corporation
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
#ifndef __iwl_fw_acpi__
#define __iwl_fw_acpi__

#include <linux/acpi.h>
#include "fw/api/commands.h"
#include "fw/api/power.h"
#include "fw/api/phy.h"
#include "fw/api/nvm-reg.h"
#include "fw/img.h"
#include "iwl-trans.h"


#define ACPI_WRDS_METHOD	"WRDS"
#define ACPI_EWRD_METHOD	"EWRD"
#define ACPI_WGDS_METHOD	"WGDS"
#define ACPI_WRDD_METHOD	"WRDD"
#define ACPI_SPLC_METHOD	"SPLC"
#define ACPI_ECKV_METHOD	"ECKV"
#define ACPI_PPAG_METHOD	"PPAG"
#define ACPI_WTAS_METHOD	"WTAS"

#define ACPI_WIFI_DOMAIN	(0x07)

#define ACPI_SAR_TABLE_SIZE		10
#define ACPI_SAR_PROFILE_NUM		4

#define ACPI_GEO_TABLE_SIZE		6
#define ACPI_NUM_GEO_PROFILES		3
#define ACPI_GEO_PER_CHAIN_SIZE		3

#define ACPI_SAR_NUM_CHAIN_LIMITS	2
#define ACPI_SAR_NUM_SUB_BANDS		5
#define ACPI_SAR_NUM_TABLES		1

#define ACPI_WRDS_WIFI_DATA_SIZE	(ACPI_SAR_TABLE_SIZE + 2)
#define ACPI_EWRD_WIFI_DATA_SIZE	((ACPI_SAR_PROFILE_NUM - 1) * \
					 ACPI_SAR_TABLE_SIZE + 3)
#define ACPI_WGDS_WIFI_DATA_SIZE	19
#define ACPI_WRDD_WIFI_DATA_SIZE	2
#define ACPI_SPLC_WIFI_DATA_SIZE	2
#define ACPI_ECKV_WIFI_DATA_SIZE	2

/*
 * 1 type, 1 enabled, 1 black list size, 16 black list array
 */
#define APCI_WTAS_BLACK_LIST_MAX	16
#define ACPI_WTAS_WIFI_DATA_SIZE	(3 + APCI_WTAS_BLACK_LIST_MAX)

#define ACPI_WGDS_NUM_BANDS		2
#define ACPI_WGDS_TABLE_SIZE		3

#define ACPI_PPAG_WIFI_DATA_SIZE	((IWL_NUM_CHAIN_LIMITS * \
					IWL_NUM_SUB_BANDS) + 3)
#define ACPI_PPAG_WIFI_DATA_SIZE_V2	((IWL_NUM_CHAIN_LIMITS * \
					IWL_NUM_SUB_BANDS_V2) + 3)

/* PPAG gain value bounds in 1/8 dBm */
#define ACPI_PPAG_MIN_LB -16
#define ACPI_PPAG_MAX_LB 24
#define ACPI_PPAG_MIN_HB -16
#define ACPI_PPAG_MAX_HB 40

struct iwl_sar_profile {
	bool enabled;
	u8 table[ACPI_SAR_TABLE_SIZE];
};

struct iwl_geo_profile {
	u8 values[ACPI_GEO_TABLE_SIZE];
};

enum iwl_dsm_funcs_rev_0 {
	DSM_FUNC_QUERY = 0,
	DSM_FUNC_DISABLE_SRD = 1,
	DSM_FUNC_ENABLE_INDONESIA_5G2 = 2,
};

enum iwl_dsm_values_srd {
	DSM_VALUE_SRD_ACTIVE,
	DSM_VALUE_SRD_PASSIVE,
	DSM_VALUE_SRD_DISABLE,
	DSM_VALUE_SRD_MAX
};

enum iwl_dsm_values_indonesia {
	DSM_VALUE_INDONESIA_DISABLE,
	DSM_VALUE_INDONESIA_ENABLE,
	DSM_VALUE_INDONESIA_RESERVED,
	DSM_VALUE_INDONESIA_MAX
};

#ifdef CONFIG_ACPI

struct iwl_fw_runtime;

void *iwl_acpi_get_object(struct device *dev, acpi_string method);

int iwl_acpi_get_dsm_u8(struct device *dev, int rev, int func);

union acpi_object *iwl_acpi_get_wifi_pkg(struct device *dev,
					 union acpi_object *data,
					 int data_size, int *tbl_rev);

/**
 * iwl_acpi_get_mcc - read MCC from ACPI, if available
 *
 * @dev: the struct device
 * @mcc: output buffer (3 bytes) that will get the MCC
 *
 * This function tries to read the current MCC from ACPI if available.
 */
int iwl_acpi_get_mcc(struct device *dev, char *mcc);

u64 iwl_acpi_get_pwr_limit(struct device *dev);

/*
 * iwl_acpi_get_eckv - read external clock validation from ACPI, if available
 *
 * @dev: the struct device
 * @extl_clk: output var (2 bytes) that will get the clk indication.
 *
 * This function tries to read the external clock indication
 * from ACPI if available.
 */
int iwl_acpi_get_eckv(struct device *dev, u32 *extl_clk);

int iwl_sar_select_profile(struct iwl_fw_runtime *fwrt,
			   __le16 *per_chain, u32 n_tables, u32 n_subbands,
			   int prof_a, int prof_b);

int iwl_sar_get_wrds_table(struct iwl_fw_runtime *fwrt);

int iwl_sar_get_ewrd_table(struct iwl_fw_runtime *fwrt);

int iwl_sar_get_wgds_table(struct iwl_fw_runtime *fwrt);

bool iwl_sar_geo_support(struct iwl_fw_runtime *fwrt);

int iwl_sar_geo_init(struct iwl_fw_runtime *fwrt,
		     struct iwl_per_chain_offset_group *table);

int iwl_acpi_get_tas(struct iwl_fw_runtime *fwrt, __le32 *black_list_array,
		     int *black_list_size);

#else /* CONFIG_ACPI */

static inline void *iwl_acpi_get_object(struct device *dev, acpi_string method)
{
	return ERR_PTR(-ENOENT);
}

static inline void *iwl_acpi_get_dsm_object(struct device *dev, int rev,
					    int func, union acpi_object *args)
{
	return ERR_PTR(-ENOENT);
}

static inline int iwl_acpi_get_dsm_u8(struct device *dev, int rev, int func)
{
	return -ENOENT;
}

static inline union acpi_object *iwl_acpi_get_wifi_pkg(struct device *dev,
						       union acpi_object *data,
						       int data_size,
						       int *tbl_rev)
{
	return ERR_PTR(-ENOENT);
}

static inline int iwl_acpi_get_mcc(struct device *dev, char *mcc)
{
	return -ENOENT;
}

static inline u64 iwl_acpi_get_pwr_limit(struct device *dev)
{
	return 0;
}

static inline int iwl_acpi_get_eckv(struct device *dev, u32 *extl_clk)
{
	return -ENOENT;
}

static inline int iwl_sar_select_profile(struct iwl_fw_runtime *fwrt,
			   __le16 *per_chain, u32 n_tables, u32 n_subbands,
			   int prof_a, int prof_b)
{
	return -ENOENT;
}

static inline int iwl_sar_get_wrds_table(struct iwl_fw_runtime *fwrt)
{
	return -ENOENT;
}

static inline int iwl_sar_get_ewrd_table(struct iwl_fw_runtime *fwrt)
{
	return -ENOENT;
}

static inline int iwl_sar_get_wgds_table(struct iwl_fw_runtime *fwrt)
{
	return -ENOENT;
}

static inline bool iwl_sar_geo_support(struct iwl_fw_runtime *fwrt)
{
	return false;
}

static inline int iwl_sar_geo_init(struct iwl_fw_runtime *fwrt,
				   struct iwl_per_chain_offset_group *table)
{
	return -ENOENT;
}

static inline int iwl_acpi_get_tas(struct iwl_fw_runtime *fwrt,
				   __le32 *black_list_array,
				   int *black_list_size)
{
	return -ENOENT;
}
#endif /* CONFIG_ACPI */
#endif /* __iwl_fw_acpi__ */
