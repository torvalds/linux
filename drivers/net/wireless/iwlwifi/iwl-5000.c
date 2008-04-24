/******************************************************************************
 *
 * Copyright(c) 2007-2008 Intel Corporation. All rights reserved.
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
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
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
#include "iwl-4965.h"
#include "iwl-core.h"
#include "iwl-io.h"
#include "iwl-helpers.h"
#include "iwl-5000-hw.h"

#define IWL5000_UCODE_API  "-1"

static struct iwl_hcmd_ops iwl5000_hcmd = {
};

static struct iwl_hcmd_utils_ops iwl5000_hcmd_utils = {
};

static struct iwl_lib_ops iwl5000_lib = {
	.eeprom_ops = {
		.verify_signature  = iwlcore_eeprom_verify_signature,
		.acquire_semaphore = iwlcore_eeprom_acquire_semaphore,
		.release_semaphore = iwlcore_eeprom_release_semaphore,
	},
};

static struct iwl_ops iwl5000_ops = {
	.lib = &iwl5000_lib,
	.hcmd = &iwl5000_hcmd,
	.utils = &iwl5000_hcmd_utils,
};

static struct iwl_mod_params iwl50_mod_params = {
	.num_of_queues = IWL50_NUM_QUEUES,
	.enable_qos = 1,
	.amsdu_size_8K = 1,
	/* the rest are 0 by default */
};


struct iwl_cfg iwl5300_agn_cfg = {
	.name = "5300AGN",
	.fw_name = "iwlwifi-5000" IWL5000_UCODE_API ".ucode",
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl5000_ops,
	.mod_params = &iwl50_mod_params,
};

struct iwl_cfg iwl5100_agn_cfg = {
	.name = "5100AGN",
	.fw_name = "iwlwifi-5000" IWL5000_UCODE_API ".ucode",
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl5000_ops,
	.mod_params = &iwl50_mod_params,
};

struct iwl_cfg iwl5350_agn_cfg = {
	.name = "5350AGN",
	.fw_name = "iwlwifi-5000" IWL5000_UCODE_API ".ucode",
	.sku = IWL_SKU_A|IWL_SKU_G|IWL_SKU_N,
	.ops = &iwl5000_ops,
	.mod_params = &iwl50_mod_params,
};

module_param_named(disable50, iwl50_mod_params.disable, int, 0444);
MODULE_PARM_DESC(disable50,
		  "manually disable the 50XX radio (default 0 [radio on])");
module_param_named(swcrypto50, iwl50_mod_params.sw_crypto, bool, 0444);
MODULE_PARM_DESC(swcrypto50,
		  "using software crypto engine (default 0 [hardware])\n");
module_param_named(debug50, iwl50_mod_params.debug, int, 0444);
MODULE_PARM_DESC(debug50, "50XX debug output mask");
module_param_named(queues_num50, iwl50_mod_params.num_of_queues, int, 0444);
MODULE_PARM_DESC(queues_num50, "number of hw queues in 50xx series");
module_param_named(qos_enable50, iwl50_mod_params.enable_qos, int, 0444);
MODULE_PARM_DESC(qos_enable50, "enable all 50XX QoS functionality");
module_param_named(amsdu_size_8K50, iwl50_mod_params.amsdu_size_8K, int, 0444);
MODULE_PARM_DESC(amsdu_size_8K50, "enable 8K amsdu size in 50XX series");


