/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2010 - 2011 Intel Corporation. All rights reserved.
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
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2010 - 2011 Intel Corporation. All rights reserved.
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
#include <net/net_namespace.h>
#include <linux/netdevice.h>
#include <net/cfg80211.h>
#include <net/mac80211.h>
#include <net/netlink.h>

#include "iwl-dev.h"
#include "iwl-core.h"
#include "iwl-debug.h"
#include "iwl-fh.h"
#include "iwl-io.h"
#include "iwl-agn.h"
#include "iwl-testmode.h"
#include "iwl-trans.h"

/* The TLVs used in the gnl message policy between the kernel module and
 * user space application. iwl_testmode_gnl_msg_policy is to be carried
 * through the NL80211_CMD_TESTMODE channel regulated by nl80211.
 * See iwl-testmode.h
 */
static
struct nla_policy iwl_testmode_gnl_msg_policy[IWL_TM_ATTR_MAX] = {
	[IWL_TM_ATTR_COMMAND] = { .type = NLA_U32, },

	[IWL_TM_ATTR_UCODE_CMD_ID] = { .type = NLA_U8, },
	[IWL_TM_ATTR_UCODE_CMD_DATA] = { .type = NLA_UNSPEC, },

	[IWL_TM_ATTR_REG_OFFSET] = { .type = NLA_U32, },
	[IWL_TM_ATTR_REG_VALUE8] = { .type = NLA_U8, },
	[IWL_TM_ATTR_REG_VALUE32] = { .type = NLA_U32, },

	[IWL_TM_ATTR_SYNC_RSP] = { .type = NLA_UNSPEC, },
	[IWL_TM_ATTR_UCODE_RX_PKT] = { .type = NLA_UNSPEC, },

	[IWL_TM_ATTR_EEPROM] = { .type = NLA_UNSPEC, },

	[IWL_TM_ATTR_TRACE_ADDR] = { .type = NLA_UNSPEC, },
	[IWL_TM_ATTR_TRACE_DUMP] = { .type = NLA_UNSPEC, },
	[IWL_TM_ATTR_TRACE_SIZE] = { .type = NLA_U32, },

	[IWL_TM_ATTR_FIXRATE] = { .type = NLA_U32, },

	[IWL_TM_ATTR_UCODE_OWNER] = { .type = NLA_U8, },
};

/*
 * See the struct iwl_rx_packet in iwl-commands.h for the format of the
 * received events from the device
 */
static inline int get_event_length(struct iwl_rx_mem_buffer *rxb)
{
	struct iwl_rx_packet *pkt = rxb_addr(rxb);
	if (pkt)
		return le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK;
	else
		return 0;
}


/*
 * This function multicasts the spontaneous messages from the device to the
 * user space. It is invoked whenever there is a received messages
 * from the device. This function is called within the ISR of the rx handlers
 * in iwlagn driver.
 *
 * The parsing of the message content is left to the user space application,
 * The message content is treated as unattacked raw data and is encapsulated
 * with IWL_TM_ATTR_UCODE_RX_PKT multicasting to the user space.
 *
 * @priv: the instance of iwlwifi device
 * @rxb: pointer to rx data content received by the ISR
 *
 * See the message policies and TLVs in iwl_testmode_gnl_msg_policy[].
 * For the messages multicasting to the user application, the mandatory
 * TLV fields are :
 *	IWL_TM_ATTR_COMMAND must be IWL_TM_CMD_DEV2APP_UCODE_RX_PKT
 *	IWL_TM_ATTR_UCODE_RX_PKT for carrying the message content
 */

static void iwl_testmode_ucode_rx_pkt(struct iwl_priv *priv,
				struct iwl_rx_mem_buffer *rxb)
{
	struct ieee80211_hw *hw = priv->hw;
	struct sk_buff *skb;
	void *data;
	int length;

	data = (void *)rxb_addr(rxb);
	length = get_event_length(rxb);

	if (!data || length == 0)
		return;

	skb = cfg80211_testmode_alloc_event_skb(hw->wiphy, 20 + length,
								GFP_ATOMIC);
	if (skb == NULL) {
		IWL_DEBUG_INFO(priv,
			 "Run out of memory for messages to user space ?\n");
		return;
	}
	NLA_PUT_U32(skb, IWL_TM_ATTR_COMMAND, IWL_TM_CMD_DEV2APP_UCODE_RX_PKT);
	NLA_PUT(skb, IWL_TM_ATTR_UCODE_RX_PKT, length, data);
	cfg80211_testmode_event(skb, GFP_ATOMIC);
	return;

nla_put_failure:
	kfree_skb(skb);
	IWL_DEBUG_INFO(priv, "Ouch, overran buffer, check allocation!\n");
}

void iwl_testmode_init(struct iwl_priv *priv)
{
	priv->pre_rx_handler = iwl_testmode_ucode_rx_pkt;
	priv->testmode_trace.trace_enabled = false;
}

static void iwl_trace_cleanup(struct iwl_priv *priv)
{
	if (priv->testmode_trace.trace_enabled) {
		if (priv->testmode_trace.cpu_addr &&
		    priv->testmode_trace.dma_addr)
			dma_free_coherent(priv->bus->dev,
					priv->testmode_trace.total_size,
					priv->testmode_trace.cpu_addr,
					priv->testmode_trace.dma_addr);
		priv->testmode_trace.trace_enabled = false;
		priv->testmode_trace.cpu_addr = NULL;
		priv->testmode_trace.trace_addr = NULL;
		priv->testmode_trace.dma_addr = 0;
		priv->testmode_trace.buff_size = 0;
		priv->testmode_trace.total_size = 0;
	}
}


void iwl_testmode_cleanup(struct iwl_priv *priv)
{
	iwl_trace_cleanup(priv);
}

/*
 * This function handles the user application commands to the ucode.
 *
 * It retrieves the mandatory fields IWL_TM_ATTR_UCODE_CMD_ID and
 * IWL_TM_ATTR_UCODE_CMD_DATA and calls to the handler to send the
 * host command to the ucode.
 *
 * If any mandatory field is missing, -ENOMSG is replied to the user space
 * application; otherwise, the actual execution result of the host command to
 * ucode is replied.
 *
 * @hw: ieee80211_hw object that represents the device
 * @tb: gnl message fields from the user space
 */
static int iwl_testmode_ucode(struct ieee80211_hw *hw, struct nlattr **tb)
{
	struct iwl_priv *priv = hw->priv;
	struct iwl_host_cmd cmd;

	memset(&cmd, 0, sizeof(struct iwl_host_cmd));

	if (!tb[IWL_TM_ATTR_UCODE_CMD_ID] ||
	    !tb[IWL_TM_ATTR_UCODE_CMD_DATA]) {
		IWL_DEBUG_INFO(priv,
			"Error finding ucode command mandatory fields\n");
		return -ENOMSG;
	}

	cmd.flags = CMD_ON_DEMAND;
	cmd.id = nla_get_u8(tb[IWL_TM_ATTR_UCODE_CMD_ID]);
	cmd.data[0] = nla_data(tb[IWL_TM_ATTR_UCODE_CMD_DATA]);
	cmd.len[0] = nla_len(tb[IWL_TM_ATTR_UCODE_CMD_DATA]);
	cmd.dataflags[0] = IWL_HCMD_DFL_NOCOPY;
	IWL_INFO(priv, "testmode ucode command ID 0x%x, flags 0x%x,"
				" len %d\n", cmd.id, cmd.flags, cmd.len[0]);
	/* ok, let's submit the command to ucode */
	return trans_send_cmd(&priv->trans, &cmd);
}


/*
 * This function handles the user application commands for register access.
 *
 * It retrieves command ID carried with IWL_TM_ATTR_COMMAND and calls to the
 * handlers respectively.
 *
 * If it's an unknown commdn ID, -ENOSYS is returned; or -ENOMSG if the
 * mandatory fields(IWL_TM_ATTR_REG_OFFSET,IWL_TM_ATTR_REG_VALUE32,
 * IWL_TM_ATTR_REG_VALUE8) are missing; Otherwise 0 is replied indicating
 * the success of the command execution.
 *
 * If IWL_TM_ATTR_COMMAND is IWL_TM_CMD_APP2DEV_REG_READ32, the register read
 * value is returned with IWL_TM_ATTR_REG_VALUE32.
 *
 * @hw: ieee80211_hw object that represents the device
 * @tb: gnl message fields from the user space
 */
static int iwl_testmode_reg(struct ieee80211_hw *hw, struct nlattr **tb)
{
	struct iwl_priv *priv = hw->priv;
	u32 ofs, val32;
	u8 val8;
	struct sk_buff *skb;
	int status = 0;

	if (!tb[IWL_TM_ATTR_REG_OFFSET]) {
		IWL_DEBUG_INFO(priv, "Error finding register offset\n");
		return -ENOMSG;
	}
	ofs = nla_get_u32(tb[IWL_TM_ATTR_REG_OFFSET]);
	IWL_INFO(priv, "testmode register access command offset 0x%x\n", ofs);

	switch (nla_get_u32(tb[IWL_TM_ATTR_COMMAND])) {
	case IWL_TM_CMD_APP2DEV_REG_READ32:
		val32 = iwl_read32(priv, ofs);
		IWL_INFO(priv, "32bit value to read 0x%x\n", val32);

		skb = cfg80211_testmode_alloc_reply_skb(hw->wiphy, 20);
		if (!skb) {
			IWL_DEBUG_INFO(priv, "Error allocating memory\n");
			return -ENOMEM;
		}
		NLA_PUT_U32(skb, IWL_TM_ATTR_REG_VALUE32, val32);
		status = cfg80211_testmode_reply(skb);
		if (status < 0)
			IWL_DEBUG_INFO(priv,
				       "Error sending msg : %d\n", status);
		break;
	case IWL_TM_CMD_APP2DEV_REG_WRITE32:
		if (!tb[IWL_TM_ATTR_REG_VALUE32]) {
			IWL_DEBUG_INFO(priv,
				       "Error finding value to write\n");
			return -ENOMSG;
		} else {
			val32 = nla_get_u32(tb[IWL_TM_ATTR_REG_VALUE32]);
			IWL_INFO(priv, "32bit value to write 0x%x\n", val32);
			iwl_write32(priv, ofs, val32);
		}
		break;
	case IWL_TM_CMD_APP2DEV_REG_WRITE8:
		if (!tb[IWL_TM_ATTR_REG_VALUE8]) {
			IWL_DEBUG_INFO(priv, "Error finding value to write\n");
			return -ENOMSG;
		} else {
			val8 = nla_get_u8(tb[IWL_TM_ATTR_REG_VALUE8]);
			IWL_INFO(priv, "8bit value to write 0x%x\n", val8);
			iwl_write8(priv, ofs, val8);
		}
		break;
	default:
		IWL_DEBUG_INFO(priv, "Unknown testmode register command ID\n");
		return -ENOSYS;
	}

	return status;

nla_put_failure:
	kfree_skb(skb);
	return -EMSGSIZE;
}


static int iwl_testmode_cfg_init_calib(struct iwl_priv *priv)
{
	struct iwl_notification_wait calib_wait;
	int ret;

	iwlagn_init_notification_wait(priv, &calib_wait,
				      CALIBRATION_COMPLETE_NOTIFICATION,
				      NULL, NULL);
	ret = iwlagn_init_alive_start(priv);
	if (ret) {
		IWL_DEBUG_INFO(priv,
			"Error configuring init calibration: %d\n", ret);
		goto cfg_init_calib_error;
	}

	ret = iwlagn_wait_notification(priv, &calib_wait, 2 * HZ);
	if (ret)
		IWL_DEBUG_INFO(priv, "Error detecting"
			" CALIBRATION_COMPLETE_NOTIFICATION: %d\n", ret);
	return ret;

cfg_init_calib_error:
	iwlagn_remove_notification(priv, &calib_wait);
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
	struct iwl_priv *priv = hw->priv;
	struct sk_buff *skb;
	unsigned char *rsp_data_ptr = NULL;
	int status = 0, rsp_data_len = 0;

	switch (nla_get_u32(tb[IWL_TM_ATTR_COMMAND])) {
	case IWL_TM_CMD_APP2DEV_GET_DEVICENAME:
		rsp_data_ptr = (unsigned char *)priv->cfg->name;
		rsp_data_len = strlen(priv->cfg->name);
		skb = cfg80211_testmode_alloc_reply_skb(hw->wiphy,
							rsp_data_len + 20);
		if (!skb) {
			IWL_DEBUG_INFO(priv,
				       "Error allocating memory\n");
			return -ENOMEM;
		}
		NLA_PUT_U32(skb, IWL_TM_ATTR_COMMAND,
			    IWL_TM_CMD_DEV2APP_SYNC_RSP);
		NLA_PUT(skb, IWL_TM_ATTR_SYNC_RSP,
			rsp_data_len, rsp_data_ptr);
		status = cfg80211_testmode_reply(skb);
		if (status < 0)
			IWL_DEBUG_INFO(priv, "Error sending msg : %d\n",
				       status);
		break;

	case IWL_TM_CMD_APP2DEV_LOAD_INIT_FW:
		status = iwlagn_load_ucode_wait_alive(priv, &priv->ucode_init,
						      IWL_UCODE_INIT);
		if (status)
			IWL_DEBUG_INFO(priv,
				"Error loading init ucode: %d\n", status);
		break;

	case IWL_TM_CMD_APP2DEV_CFG_INIT_CALIB:
		iwl_testmode_cfg_init_calib(priv);
		trans_stop_device(&priv->trans);
		break;

	case IWL_TM_CMD_APP2DEV_LOAD_RUNTIME_FW:
		status = iwlagn_load_ucode_wait_alive(priv,
					   &priv->ucode_rt,
					   IWL_UCODE_REGULAR);
		if (status) {
			IWL_DEBUG_INFO(priv,
				"Error loading runtime ucode: %d\n", status);
			break;
		}
		status = iwl_alive_start(priv);
		if (status)
			IWL_DEBUG_INFO(priv,
				"Error starting the device: %d\n", status);
		break;

	case IWL_TM_CMD_APP2DEV_GET_EEPROM:
		if (priv->eeprom) {
			skb = cfg80211_testmode_alloc_reply_skb(hw->wiphy,
				priv->cfg->base_params->eeprom_size + 20);
			if (!skb) {
				IWL_DEBUG_INFO(priv,
				       "Error allocating memory\n");
				return -ENOMEM;
			}
			NLA_PUT_U32(skb, IWL_TM_ATTR_COMMAND,
				IWL_TM_CMD_DEV2APP_EEPROM_RSP);
			NLA_PUT(skb, IWL_TM_ATTR_EEPROM,
				priv->cfg->base_params->eeprom_size,
				priv->eeprom);
			status = cfg80211_testmode_reply(skb);
			if (status < 0)
				IWL_DEBUG_INFO(priv,
					       "Error sending msg : %d\n",
					       status);
		} else
			return -EFAULT;
		break;

	case IWL_TM_CMD_APP2DEV_FIXRATE_REQ:
		if (!tb[IWL_TM_ATTR_FIXRATE]) {
			IWL_DEBUG_INFO(priv,
				       "Error finding fixrate setting\n");
			return -ENOMSG;
		}
		priv->tm_fixed_rate = nla_get_u32(tb[IWL_TM_ATTR_FIXRATE]);
		break;

	default:
		IWL_DEBUG_INFO(priv, "Unknown testmode driver command ID\n");
		return -ENOSYS;
	}
	return status;

nla_put_failure:
	kfree_skb(skb);
	return -EMSGSIZE;
}


/*
 * This function handles the user application commands for uCode trace
 *
 * It retrieves command ID carried with IWL_TM_ATTR_COMMAND and calls to the
 * handlers respectively.
 *
 * If it's an unknown commdn ID, -ENOSYS is replied; otherwise, the returned
 * value of the actual command execution is replied to the user application.
 *
 * @hw: ieee80211_hw object that represents the device
 * @tb: gnl message fields from the user space
 */
static int iwl_testmode_trace(struct ieee80211_hw *hw, struct nlattr **tb)
{
	struct iwl_priv *priv = hw->priv;
	struct sk_buff *skb;
	int status = 0;
	struct device *dev = priv->bus->dev;

	switch (nla_get_u32(tb[IWL_TM_ATTR_COMMAND])) {
	case IWL_TM_CMD_APP2DEV_BEGIN_TRACE:
		if (priv->testmode_trace.trace_enabled)
			return -EBUSY;

		if (!tb[IWL_TM_ATTR_TRACE_SIZE])
			priv->testmode_trace.buff_size = TRACE_BUFF_SIZE_DEF;
		else
			priv->testmode_trace.buff_size =
				nla_get_u32(tb[IWL_TM_ATTR_TRACE_SIZE]);
		if (!priv->testmode_trace.buff_size)
			return -EINVAL;
		if (priv->testmode_trace.buff_size < TRACE_BUFF_SIZE_MIN ||
		    priv->testmode_trace.buff_size > TRACE_BUFF_SIZE_MAX)
			return -EINVAL;

		priv->testmode_trace.total_size =
			priv->testmode_trace.buff_size + TRACE_BUFF_PADD;
		priv->testmode_trace.cpu_addr =
			dma_alloc_coherent(dev,
					   priv->testmode_trace.total_size,
					   &priv->testmode_trace.dma_addr,
					   GFP_KERNEL);
		if (!priv->testmode_trace.cpu_addr)
			return -ENOMEM;
		priv->testmode_trace.trace_enabled = true;
		priv->testmode_trace.trace_addr = (u8 *)PTR_ALIGN(
			priv->testmode_trace.cpu_addr, 0x100);
		memset(priv->testmode_trace.trace_addr, 0x03B,
			priv->testmode_trace.buff_size);
		skb = cfg80211_testmode_alloc_reply_skb(hw->wiphy,
			sizeof(priv->testmode_trace.dma_addr) + 20);
		if (!skb) {
			IWL_DEBUG_INFO(priv,
				"Error allocating memory\n");
			iwl_trace_cleanup(priv);
			return -ENOMEM;
		}
		NLA_PUT(skb, IWL_TM_ATTR_TRACE_ADDR,
			sizeof(priv->testmode_trace.dma_addr),
			(u64 *)&priv->testmode_trace.dma_addr);
		status = cfg80211_testmode_reply(skb);
		if (status < 0) {
			IWL_DEBUG_INFO(priv,
				       "Error sending msg : %d\n",
				       status);
		}
		priv->testmode_trace.num_chunks =
			DIV_ROUND_UP(priv->testmode_trace.buff_size,
				     TRACE_CHUNK_SIZE);
		break;

	case IWL_TM_CMD_APP2DEV_END_TRACE:
		iwl_trace_cleanup(priv);
		break;
	default:
		IWL_DEBUG_INFO(priv, "Unknown testmode mem command ID\n");
		return -ENOSYS;
	}
	return status;

nla_put_failure:
	kfree_skb(skb);
	if (nla_get_u32(tb[IWL_TM_ATTR_COMMAND]) ==
	    IWL_TM_CMD_APP2DEV_BEGIN_TRACE)
		iwl_trace_cleanup(priv);
	return -EMSGSIZE;
}

static int iwl_testmode_trace_dump(struct ieee80211_hw *hw, struct nlattr **tb,
				   struct sk_buff *skb,
				   struct netlink_callback *cb)
{
	struct iwl_priv *priv = hw->priv;
	int idx, length;

	if (priv->testmode_trace.trace_enabled &&
	    priv->testmode_trace.trace_addr) {
		idx = cb->args[4];
		if (idx >= priv->testmode_trace.num_chunks)
			return -ENOENT;
		length = TRACE_CHUNK_SIZE;
		if (((idx + 1) == priv->testmode_trace.num_chunks) &&
		    (priv->testmode_trace.buff_size % TRACE_CHUNK_SIZE))
			length = priv->testmode_trace.buff_size %
				TRACE_CHUNK_SIZE;

		NLA_PUT(skb, IWL_TM_ATTR_TRACE_DUMP, length,
			priv->testmode_trace.trace_addr +
			(TRACE_CHUNK_SIZE * idx));
		idx++;
		cb->args[4] = idx;
		return 0;
	} else
		return -EFAULT;

 nla_put_failure:
	return -ENOBUFS;
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
	struct iwl_priv *priv = hw->priv;
	u8 owner;

	if (!tb[IWL_TM_ATTR_UCODE_OWNER]) {
		IWL_DEBUG_INFO(priv, "Error finding ucode owner\n");
		return -ENOMSG;
	}

	owner = nla_get_u8(tb[IWL_TM_ATTR_UCODE_OWNER]);
	if ((owner == IWL_OWNERSHIP_DRIVER) || (owner == IWL_OWNERSHIP_TM))
		priv->ucode_owner = owner;
	else {
		IWL_DEBUG_INFO(priv, "Invalid owner\n");
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
int iwl_testmode_cmd(struct ieee80211_hw *hw, void *data, int len)
{
	struct nlattr *tb[IWL_TM_ATTR_MAX];
	struct iwl_priv *priv = hw->priv;
	int result;

	result = nla_parse(tb, IWL_TM_ATTR_MAX - 1, data, len,
			iwl_testmode_gnl_msg_policy);
	if (result != 0) {
		IWL_DEBUG_INFO(priv,
			       "Error parsing the gnl message : %d\n", result);
		return result;
	}

	/* IWL_TM_ATTR_COMMAND is absolutely mandatory */
	if (!tb[IWL_TM_ATTR_COMMAND]) {
		IWL_DEBUG_INFO(priv, "Error finding testmode command type\n");
		return -ENOMSG;
	}
	/* in case multiple accesses to the device happens */
	mutex_lock(&priv->mutex);

	switch (nla_get_u32(tb[IWL_TM_ATTR_COMMAND])) {
	case IWL_TM_CMD_APP2DEV_UCODE:
		IWL_DEBUG_INFO(priv, "testmode cmd to uCode\n");
		result = iwl_testmode_ucode(hw, tb);
		break;
	case IWL_TM_CMD_APP2DEV_REG_READ32:
	case IWL_TM_CMD_APP2DEV_REG_WRITE32:
	case IWL_TM_CMD_APP2DEV_REG_WRITE8:
		IWL_DEBUG_INFO(priv, "testmode cmd to register\n");
		result = iwl_testmode_reg(hw, tb);
		break;
	case IWL_TM_CMD_APP2DEV_GET_DEVICENAME:
	case IWL_TM_CMD_APP2DEV_LOAD_INIT_FW:
	case IWL_TM_CMD_APP2DEV_CFG_INIT_CALIB:
	case IWL_TM_CMD_APP2DEV_LOAD_RUNTIME_FW:
	case IWL_TM_CMD_APP2DEV_GET_EEPROM:
	case IWL_TM_CMD_APP2DEV_FIXRATE_REQ:
		IWL_DEBUG_INFO(priv, "testmode cmd to driver\n");
		result = iwl_testmode_driver(hw, tb);
		break;

	case IWL_TM_CMD_APP2DEV_BEGIN_TRACE:
	case IWL_TM_CMD_APP2DEV_END_TRACE:
	case IWL_TM_CMD_APP2DEV_READ_TRACE:
		IWL_DEBUG_INFO(priv, "testmode uCode trace cmd to driver\n");
		result = iwl_testmode_trace(hw, tb);
		break;

	case IWL_TM_CMD_APP2DEV_OWNERSHIP:
		IWL_DEBUG_INFO(priv, "testmode change uCode ownership\n");
		result = iwl_testmode_ownership(hw, tb);
		break;

	default:
		IWL_DEBUG_INFO(priv, "Unknown testmode command\n");
		result = -ENOSYS;
		break;
	}

	mutex_unlock(&priv->mutex);
	return result;
}

int iwl_testmode_dump(struct ieee80211_hw *hw, struct sk_buff *skb,
		      struct netlink_callback *cb,
		      void *data, int len)
{
	struct nlattr *tb[IWL_TM_ATTR_MAX];
	struct iwl_priv *priv = hw->priv;
	int result;
	u32 cmd;

	if (cb->args[3]) {
		/* offset by 1 since commands start at 0 */
		cmd = cb->args[3] - 1;
	} else {
		result = nla_parse(tb, IWL_TM_ATTR_MAX - 1, data, len,
				iwl_testmode_gnl_msg_policy);
		if (result) {
			IWL_DEBUG_INFO(priv,
			       "Error parsing the gnl message : %d\n", result);
			return result;
		}

		/* IWL_TM_ATTR_COMMAND is absolutely mandatory */
		if (!tb[IWL_TM_ATTR_COMMAND]) {
			IWL_DEBUG_INFO(priv,
				"Error finding testmode command type\n");
			return -ENOMSG;
		}
		cmd = nla_get_u32(tb[IWL_TM_ATTR_COMMAND]);
		cb->args[3] = cmd + 1;
	}

	/* in case multiple accesses to the device happens */
	mutex_lock(&priv->mutex);
	switch (cmd) {
	case IWL_TM_CMD_APP2DEV_READ_TRACE:
		IWL_DEBUG_INFO(priv, "uCode trace cmd to driver\n");
		result = iwl_testmode_trace_dump(hw, tb, skb, cb);
		break;
	default:
		result = -EINVAL;
		break;
	}

	mutex_unlock(&priv->mutex);
	return result;
}
