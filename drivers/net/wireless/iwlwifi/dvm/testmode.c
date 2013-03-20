/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2010 - 2013 Intel Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2010 - 2013 Intel Corporation. All rights reserved.
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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <net/net_namespace.h>
#include <linux/netdevice.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <net/netlink.h>

#include "iwl-debug.h"
#include "iwl-trans.h"
#include "dev.h"
#include "agn.h"
#include "iwl-test.h"
#include "iwl-testmode.h"

static int iwl_testmode_send_cmd(struct iwl_op_mode *op_mode,
				 struct iwl_host_cmd *cmd)
{
	struct iwl_priv *priv = IWL_OP_MODE_GET_DVM(op_mode);
	return iwl_dvm_send_cmd(priv, cmd);
}

static bool iwl_testmode_valid_hw_addr(u32 addr)
{
	if (iwlagn_hw_valid_rtc_data_addr(addr))
		return true;

	if (IWLAGN_RTC_INST_LOWER_BOUND <= addr &&
	    addr < IWLAGN_RTC_INST_UPPER_BOUND)
		return true;

	return false;
}

static u32 iwl_testmode_get_fw_ver(struct iwl_op_mode *op_mode)
{
	struct iwl_priv *priv = IWL_OP_MODE_GET_DVM(op_mode);
	return priv->fw->ucode_ver;
}

static struct sk_buff*
iwl_testmode_alloc_reply(struct iwl_op_mode *op_mode, int len)
{
	struct iwl_priv *priv = IWL_OP_MODE_GET_DVM(op_mode);
	return cfg80211_testmode_alloc_reply_skb(priv->hw->wiphy, len);
}

static int iwl_testmode_reply(struct iwl_op_mode *op_mode, struct sk_buff *skb)
{
	return cfg80211_testmode_reply(skb);
}

static struct sk_buff *iwl_testmode_alloc_event(struct iwl_op_mode *op_mode,
						int len)
{
	struct iwl_priv *priv = IWL_OP_MODE_GET_DVM(op_mode);
	return cfg80211_testmode_alloc_event_skb(priv->hw->wiphy, len,
						 GFP_ATOMIC);
}

static void iwl_testmode_event(struct iwl_op_mode *op_mode, struct sk_buff *skb)
{
	return cfg80211_testmode_event(skb, GFP_ATOMIC);
}

static struct iwl_test_ops tst_ops = {
	.send_cmd = iwl_testmode_send_cmd,
	.valid_hw_addr = iwl_testmode_valid_hw_addr,
	.get_fw_ver = iwl_testmode_get_fw_ver,
	.alloc_reply = iwl_testmode_alloc_reply,
	.reply = iwl_testmode_reply,
	.alloc_event = iwl_testmode_alloc_event,
	.event = iwl_testmode_event,
};

void iwl_testmode_init(struct iwl_priv *priv)
{
	iwl_test_init(&priv->tst, priv->trans, &tst_ops);
}

void iwl_testmode_free(struct iwl_priv *priv)
{
	iwl_test_free(&priv->tst);
}

static int iwl_testmode_cfg_init_calib(struct iwl_priv *priv)
{
	struct iwl_notification_wait calib_wait;
	static const u8 calib_complete[] = {
		CALIBRATION_COMPLETE_NOTIFICATION
	};
	int ret;

	iwl_init_notification_wait(&priv->notif_wait, &calib_wait,
				   calib_complete, ARRAY_SIZE(calib_complete),
				   NULL, NULL);
	ret = iwl_init_alive_start(priv);
	if (ret) {
		IWL_ERR(priv, "Fail init calibration: %d\n", ret);
		goto cfg_init_calib_error;
	}

	ret = iwl_wait_notification(&priv->notif_wait, &calib_wait, 2 * HZ);
	if (ret)
		IWL_ERR(priv, "Error detecting"
			" CALIBRATION_COMPLETE_NOTIFICATION: %d\n", ret);
	return ret;

cfg_init_calib_error:
	iwl_remove_notification(&priv->notif_wait, &calib_wait);
	return ret;
}

/*
 * This function handles the user application commands for driver.
 *
 * It retrieves command ID carried with IWL_TM_ATTR_COMMAND and calls to the
 * handlers respectively.
 *
 * If it's an unknown commdn ID, -ENOSYS is replied; otherwise, the returned
 * value of the actual command execution is replied to the user application.
 *
 * If there's any message responding to the user space, IWL_TM_ATTR_SYNC_RSP
 * is used for carry the message while IWL_TM_ATTR_COMMAND must set to
 * IWL_TM_CMD_DEV2APP_SYNC_RSP.
 *
 * @hw: ieee80211_hw object that represents the device
 * @tb: gnl message fields from the user space
 */
static int iwl_testmode_driver(struct ieee80211_hw *hw, struct nlattr **tb)
{
	struct iwl_priv *priv = IWL_MAC80211_GET_DVM(hw);
	struct iwl_trans *trans = priv->trans;
	struct sk_buff *skb;
	unsigned char *rsp_data_ptr = NULL;
	int status = 0, rsp_data_len = 0;
	u32 inst_size = 0, data_size = 0;
	const struct fw_img *img;

	switch (nla_get_u32(tb[IWL_TM_ATTR_COMMAND])) {
	case IWL_TM_CMD_APP2DEV_GET_DEVICENAME:
		rsp_data_ptr = (unsigned char *)priv->cfg->name;
		rsp_data_len = strlen(priv->cfg->name);
		skb = cfg80211_testmode_alloc_reply_skb(hw->wiphy,
							rsp_data_len + 20);
		if (!skb) {
			IWL_ERR(priv, "Memory allocation fail\n");
			return -ENOMEM;
		}
		if (nla_put_u32(skb, IWL_TM_ATTR_COMMAND,
				IWL_TM_CMD_DEV2APP_SYNC_RSP) ||
		    nla_put(skb, IWL_TM_ATTR_SYNC_RSP,
			    rsp_data_len, rsp_data_ptr))
			goto nla_put_failure;
		status = cfg80211_testmode_reply(skb);
		if (status < 0)
			IWL_ERR(priv, "Error sending msg : %d\n", status);
		break;

	case IWL_TM_CMD_APP2DEV_LOAD_INIT_FW:
		status = iwl_load_ucode_wait_alive(priv, IWL_UCODE_INIT);
		if (status)
			IWL_ERR(priv, "Error loading init ucode: %d\n", status);
		break;

	case IWL_TM_CMD_APP2DEV_CFG_INIT_CALIB:
		iwl_testmode_cfg_init_calib(priv);
		priv->ucode_loaded = false;
		iwl_trans_stop_device(trans);
		break;

	case IWL_TM_CMD_APP2DEV_LOAD_RUNTIME_FW:
		status = iwl_load_ucode_wait_alive(priv, IWL_UCODE_REGULAR);
		if (status) {
			IWL_ERR(priv,
				"Error loading runtime ucode: %d\n", status);
			break;
		}
		status = iwl_alive_start(priv);
		if (status)
			IWL_ERR(priv,
				"Error starting the device: %d\n", status);
		break;

	case IWL_TM_CMD_APP2DEV_LOAD_WOWLAN_FW:
		iwl_scan_cancel_timeout(priv, 200);
		priv->ucode_loaded = false;
		iwl_trans_stop_device(trans);
		status = iwl_load_ucode_wait_alive(priv, IWL_UCODE_WOWLAN);
		if (status) {
			IWL_ERR(priv,
				"Error loading WOWLAN ucode: %d\n", status);
			break;
		}
		status = iwl_alive_start(priv);
		if (status)
			IWL_ERR(priv,
				"Error starting the device: %d\n", status);
		break;

	case IWL_TM_CMD_APP2DEV_GET_EEPROM:
		if (priv->eeprom_blob) {
			skb = cfg80211_testmode_alloc_reply_skb(hw->wiphy,
				priv->eeprom_blob_size + 20);
			if (!skb) {
				IWL_ERR(priv, "Memory allocation fail\n");
				return -ENOMEM;
			}
			if (nla_put_u32(skb, IWL_TM_ATTR_COMMAND,
					IWL_TM_CMD_DEV2APP_EEPROM_RSP) ||
			    nla_put(skb, IWL_TM_ATTR_EEPROM,
				    priv->eeprom_blob_size,
				    priv->eeprom_blob))
				goto nla_put_failure;
			status = cfg80211_testmode_reply(skb);
			if (status < 0)
				IWL_ERR(priv, "Error sending msg : %d\n",
					status);
		} else
			return -ENODATA;
		break;

	case IWL_TM_CMD_APP2DEV_FIXRATE_REQ:
		if (!tb[IWL_TM_ATTR_FIXRATE]) {
			IWL_ERR(priv, "Missing fixrate setting\n");
			return -ENOMSG;
		}
		priv->tm_fixed_rate = nla_get_u32(tb[IWL_TM_ATTR_FIXRATE]);
		break;

	case IWL_TM_CMD_APP2DEV_GET_FW_INFO:
		skb = cfg80211_testmode_alloc_reply_skb(hw->wiphy, 20 + 8);
		if (!skb) {
			IWL_ERR(priv, "Memory allocation fail\n");
			return -ENOMEM;
		}
		if (!priv->ucode_loaded) {
			IWL_ERR(priv, "No uCode has not been loaded\n");
			return -EINVAL;
		} else {
			img = &priv->fw->img[priv->cur_ucode];
			inst_size = img->sec[IWL_UCODE_SECTION_INST].len;
			data_size = img->sec[IWL_UCODE_SECTION_DATA].len;
		}
		if (nla_put_u32(skb, IWL_TM_ATTR_FW_TYPE, priv->cur_ucode) ||
		    nla_put_u32(skb, IWL_TM_ATTR_FW_INST_SIZE, inst_size) ||
		    nla_put_u32(skb, IWL_TM_ATTR_FW_DATA_SIZE, data_size))
			goto nla_put_failure;
		status = cfg80211_testmode_reply(skb);
		if (status < 0)
			IWL_ERR(priv, "Error sending msg : %d\n", status);
		break;

	default:
		IWL_ERR(priv, "Unknown testmode driver command ID\n");
		return -ENOSYS;
	}
	return status;

nla_put_failure:
	kfree_skb(skb);
	return -EMSGSIZE;
}

/*
 * This function handles the user application switch ucode ownership.
 *
 * It retrieves the mandatory fields IWL_TM_ATTR_UCODE_OWNER and
 * decide who the current owner of the uCode
 *
 * If the current owner is OWNERSHIP_TM, then the only host command
 * can deliver to uCode is from testmode, all the other host commands
 * will dropped.
 *
 * default driver is the owner of uCode in normal operational mode
 *
 * @hw: ieee80211_hw object that represents the device
 * @tb: gnl message fields from the user space
 */
static int iwl_testmode_ownership(struct ieee80211_hw *hw, struct nlattr **tb)
{
	struct iwl_priv *priv = IWL_MAC80211_GET_DVM(hw);
	u8 owner;

	if (!tb[IWL_TM_ATTR_UCODE_OWNER]) {
		IWL_ERR(priv, "Missing ucode owner\n");
		return -ENOMSG;
	}

	owner = nla_get_u8(tb[IWL_TM_ATTR_UCODE_OWNER]);
	if (owner == IWL_OWNERSHIP_DRIVER) {
		priv->ucode_owner = owner;
		iwl_test_enable_notifications(&priv->tst, false);
	} else if (owner == IWL_OWNERSHIP_TM) {
		priv->ucode_owner = owner;
		iwl_test_enable_notifications(&priv->tst, true);
	} else {
		IWL_ERR(priv, "Invalid owner\n");
		return -EINVAL;
	}
	return 0;
}

/* The testmode gnl message handler that takes the gnl message from the
 * user space and parses it per the policy iwl_testmode_gnl_msg_policy, then
 * invoke the corresponding handlers.
 *
 * This function is invoked when there is user space application sending
 * gnl message through the testmode tunnel NL80211_CMD_TESTMODE regulated
 * by nl80211.
 *
 * It retrieves the mandatory field, IWL_TM_ATTR_COMMAND, before
 * dispatching it to the corresponding handler.
 *
 * If IWL_TM_ATTR_COMMAND is missing, -ENOMSG is replied to user application;
 * -ENOSYS is replied to the user application if the command is unknown;
 * Otherwise, the command is dispatched to the respective handler.
 *
 * @hw: ieee80211_hw object that represents the device
 * @data: pointer to user space message
 * @len: length in byte of @data
 */
int iwlagn_mac_testmode_cmd(struct ieee80211_hw *hw, void *data, int len)
{
	struct nlattr *tb[IWL_TM_ATTR_MAX];
	struct iwl_priv *priv = IWL_MAC80211_GET_DVM(hw);
	int result;

	result = iwl_test_parse(&priv->tst, tb, data, len);
	if (result)
		return result;

	/* in case multiple accesses to the device happens */
	mutex_lock(&priv->mutex);
	switch (nla_get_u32(tb[IWL_TM_ATTR_COMMAND])) {
	case IWL_TM_CMD_APP2DEV_UCODE:
	case IWL_TM_CMD_APP2DEV_DIRECT_REG_READ32:
	case IWL_TM_CMD_APP2DEV_DIRECT_REG_WRITE32:
	case IWL_TM_CMD_APP2DEV_DIRECT_REG_WRITE8:
	case IWL_TM_CMD_APP2DEV_BEGIN_TRACE:
	case IWL_TM_CMD_APP2DEV_END_TRACE:
	case IWL_TM_CMD_APP2DEV_INDIRECT_BUFFER_READ:
	case IWL_TM_CMD_APP2DEV_NOTIFICATIONS:
	case IWL_TM_CMD_APP2DEV_GET_FW_VERSION:
	case IWL_TM_CMD_APP2DEV_GET_DEVICE_ID:
	case IWL_TM_CMD_APP2DEV_INDIRECT_BUFFER_WRITE:
		result = iwl_test_handle_cmd(&priv->tst, tb);
		break;

	case IWL_TM_CMD_APP2DEV_GET_DEVICENAME:
	case IWL_TM_CMD_APP2DEV_LOAD_INIT_FW:
	case IWL_TM_CMD_APP2DEV_CFG_INIT_CALIB:
	case IWL_TM_CMD_APP2DEV_LOAD_RUNTIME_FW:
	case IWL_TM_CMD_APP2DEV_GET_EEPROM:
	case IWL_TM_CMD_APP2DEV_FIXRATE_REQ:
	case IWL_TM_CMD_APP2DEV_LOAD_WOWLAN_FW:
	case IWL_TM_CMD_APP2DEV_GET_FW_INFO:
		IWL_DEBUG_INFO(priv, "testmode cmd to driver\n");
		result = iwl_testmode_driver(hw, tb);
		break;

	case IWL_TM_CMD_APP2DEV_OWNERSHIP:
		IWL_DEBUG_INFO(priv, "testmode change uCode ownership\n");
		result = iwl_testmode_ownership(hw, tb);
		break;

	default:
		IWL_ERR(priv, "Unknown testmode command\n");
		result = -ENOSYS;
		break;
	}
	mutex_unlock(&priv->mutex);

	if (result)
		IWL_ERR(priv, "Test cmd failed result=%d\n", result);
	return result;
}

int iwlagn_mac_testmode_dump(struct ieee80211_hw *hw, struct sk_buff *skb,
		      struct netlink_callback *cb,
		      void *data, int len)
{
	struct iwl_priv *priv = IWL_MAC80211_GET_DVM(hw);
	int result;
	u32 cmd;

	if (cb->args[3]) {
		/* offset by 1 since commands start at 0 */
		cmd = cb->args[3] - 1;
	} else {
		struct nlattr *tb[IWL_TM_ATTR_MAX];

		result = iwl_test_parse(&priv->tst, tb, data, len);
		if (result)
			return result;

		cmd = nla_get_u32(tb[IWL_TM_ATTR_COMMAND]);
		cb->args[3] = cmd + 1;
	}

	/* in case multiple accesses to the device happens */
	mutex_lock(&priv->mutex);
	result = iwl_test_dump(&priv->tst, cmd, skb, cb);
	mutex_unlock(&priv->mutex);
	return result;
}
