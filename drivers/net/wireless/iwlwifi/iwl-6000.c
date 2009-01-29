/******************************************************************************
 *
 * Copyright(c) 2008-2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <net/mac80211.h>
#include <linux/etherdevice.h>
#include <asm/unaligned.h>

#include "iwl-eeprom.h"
#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-sta.h"
#include "iwl-helpers.h"
#include "iwl-5000-hw.h"

/* Highest firmware API version supported */
#define IWL6000_UCODE_API_MAX 2
#define IWL6050_UCODE_API_MAX 2

/* Lowest firmware API version supported */
#define IWL6000_UCODE_API_MIN 1
#define IWL6050_UCODE_API_MIN 1

#define IWL6000_FW_PRE "iwlwifi-6000-"
#define _IWL6000_MODULE_FIRMWARE(api) IWL6000_FW_PRE #api ".ucode"
#define IWL6000_MODULE_FIRMWARE(api) _IWL6000_MODULE_FIRMWARE(api)

#define IWL6050_FW_PRE "iwlwifi-6050-"
#define _IWL6050_MODULE_FIRMWARE(api) IWL6050_FW_PRE #api ".ucode"
#define IWL6050_MODULE_FIRMWARE(api) _IWL6050_MODULE_FIRMWARE(api)

static struct iwl_hcmd_utils_ops iwl6000_hcmd_utils = {
	.get_hcmd_size = iwl5000_get_hcmd_size,
	.build_addsta_hcmd = iwl5000_build_addsta_hcmd,
	.rts_tx_cmd_flag = iwl5000_rts_tx_cmd_flag,
	.calc_rssi = iwl5000_calc_rssi,
};

static struct iwl_ops iwl6000_ops = {
	.lib = &iwl5000_lib,
	.hcmd = &iwl5000_hcmd,
	.utils = &iwl6000_hcmd_utils,
};

struct iwl_cfg iwl6000_2ag_cfg = {
	.name = "6000 Series 2x2 AG",
	.fw_name_pre = IWL6000_FW_PRE,
	.ucode_api_max = IWL6000_UCODE_API_MAX,
	.ucode_api_min = IWL6000_UCODE_API_MIN,
	.sku = IWL_SKU_A|IWL_SKU_G,
	.ops = &iwl6000_ops,
	.eeprom_size = IWL_5000_EEPROM_IMG_SIZE,
	.eeprom_ver = EEPROM_5000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_5000_TX_POWER_VERSION,
	.mod_params = &iwl50_mod_params,
	.valid_tx_ant = ANT_BC,
	.valid_rx_ant = ANT_BC,
	.need_pll_cfg = false,
};

struct iwl_cfg iwl6000_2agn_cfg = {
	.name = "6000 Series 2x2 AGN",
	.fw_name_pre = IWL6000_FW_PRE,
	.ucode_api_max = IWL6000_UCODE_API_MAX,
	.ucode_api_min = IWL6000_UCODE_API_MIN,
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl6000_ops,
	.eeprom_size = IWL_5000_EEPROM_IMG_SIZE,
	.eeprom_ver = EEPROM_5000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_5000_TX_POWER_VERSION,
	.mod_params = &iwl50_mod_params,
	.valid_tx_ant = ANT_BC,
	.valid_rx_ant = ANT_BC,
	.need_pll_cfg = false,
};

struct iwl_cfg iwl6050_2agn_cfg = {
	.name = "6050 Series 2x2 AGN",
	.fw_name_pre = IWL6050_FW_PRE,
	.ucode_api_max = IWL6050_UCODE_API_MAX,
	.ucode_api_min = IWL6050_UCODE_API_MIN,
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl6000_ops,
	.eeprom_size = IWL_5000_EEPROM_IMG_SIZE,
	.eeprom_ver = EEPROM_5000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_5000_TX_POWER_VERSION,
	.mod_params = &iwl50_mod_params,
	.valid_tx_ant = ANT_BC,
	.valid_rx_ant = ANT_BC,
	.need_pll_cfg = false,
};

struct iwl_cfg iwl6000_3agn_cfg = {
	.name = "6000 Series 3x3 AGN",
	.fw_name_pre = IWL6000_FW_PRE,
	.ucode_api_max = IWL6000_UCODE_API_MAX,
	.ucode_api_min = IWL6000_UCODE_API_MIN,
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl6000_ops,
	.eeprom_size = IWL_5000_EEPROM_IMG_SIZE,
	.eeprom_ver = EEPROM_5000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_5000_TX_POWER_VERSION,
	.mod_params = &iwl50_mod_params,
	.valid_tx_ant = ANT_ABC,
	.valid_rx_ant = ANT_ABC,
	.need_pll_cfg = false,
};

struct iwl_cfg iwl6050_3agn_cfg = {
	.name = "6050 Series 3x3 AGN",
	.fw_name_pre = IWL6050_FW_PRE,
	.ucode_api_max = IWL6050_UCODE_API_MAX,
	.ucode_api_min = IWL6050_UCODE_API_MIN,
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl6000_ops,
	.eeprom_size = IWL_5000_EEPROM_IMG_SIZE,
	.eeprom_ver = EEPROM_5000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_5000_TX_POWER_VERSION,
	.mod_params = &iwl50_mod_params,
	.valid_tx_ant = ANT_ABC,
	.valid_rx_ant = ANT_ABC,
	.need_pll_cfg = false,
};

MODULE_FIRMWARE(IWL6000_MODULE_FIRMWARE(IWL6000_UCODE_API_MAX));
MODULE_FIRMWARE(IWL6050_MODULE_FIRMWARE(IWL6050_UCODE_API_MAX));
