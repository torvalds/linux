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
#define IWL1000_UCODE_API_MAX 2

/* Lowest firmware API version supported */
#define IWL1000_UCODE_API_MIN 1

#define IWL1000_FW_PRE "iwlwifi-1000-"
#define _IWL1000_MODULE_FIRMWARE(api) IWL1000_FW_PRE #api ".ucode"
#define IWL1000_MODULE_FIRMWARE(api) _IWL1000_MODULE_FIRMWARE(api)

struct iwl_cfg iwl1000_bgn_cfg = {
	.name = "1000 Series BGN",
	.fw_name_pre = IWL1000_FW_PRE,
	.ucode_api_max = IWL1000_UCODE_API_MAX,
	.ucode_api_min = IWL1000_UCODE_API_MIN,
	.sku = IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl5000_ops,
	.eeprom_size = IWL_5000_EEPROM_IMG_SIZE,
	.eeprom_ver = EEPROM_5000_EEPROM_VERSION,
	.eeprom_calib_ver = EEPROM_5000_TX_POWER_VERSION,
	.mod_params = &iwl50_mod_params,
	.valid_tx_ant = ANT_A,
	.valid_rx_ant = ANT_AB,
	.need_pll_cfg = true,
};

