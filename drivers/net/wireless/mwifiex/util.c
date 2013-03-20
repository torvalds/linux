/*
 * Marvell Wireless LAN device driver: utility functions
 *
 * Copyright (C) 2011, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#include "decl.h"
#include "ioctl.h"
#include "util.h"
#include "fw.h"
#include "main.h"
#include "wmm.h"
#include "11n.h"

/*
 * Firmware initialization complete callback handler.
 *
 * This function wakes up the function waiting on the init
 * wait queue for the firmware initialization to complete.
 */
int mwifiex_init_fw_complete(struct mwifiex_adapter *adapter)
{

	adapter->init_wait_q_woken = true;
	wake_up_interruptible(&adapter->init_wait_q);
	return 0;
}

/*
 * Firmware shutdown complete callback handler.
 *
 * This function sets the hardware status to not ready and wakes up
 * the function waiting on the init wait queue for the firmware
 * shutdown to complete.
 */
int mwifiex_shutdown_fw_complete(struct mwifiex_adapter *adapter)
{
	adapter->hw_status = MWIFIEX_HW_STATUS_NOT_READY;
	adapter->init_wait_q_woken = true;
	wake_up_interruptible(&adapter->init_wait_q);
	return 0;
}

/*
 * This function sends init/shutdown command
 * to firmware.
 */
int mwifiex_init_shutdown_fw(struct mwifiex_private *priv,
			     u32 func_init_shutdown)
{
	u16 cmd;

	if (func_init_shutdown == MWIFIEX_FUNC_INIT) {
		cmd = HostCmd_CMD_FUNC_INIT;
	} else if (func_init_shutdown == MWIFIEX_FUNC_SHUTDOWN) {
		cmd = HostCmd_CMD_FUNC_SHUTDOWN;
	} else {
		dev_err(priv->adapter->dev, "unsupported parameter\n");
		return -1;
	}

	return mwifiex_send_cmd_sync(priv, cmd, HostCmd_ACT_GEN_SET, 0, NULL);
}
EXPORT_SYMBOL_GPL(mwifiex_init_shutdown_fw);

/*
 * IOCTL request handler to set/get debug information.
 *
 * This function collates/sets the information from/to different driver
 * structures.
 */
int mwifiex_get_debug_info(struct mwifiex_private *priv,
			   struct mwifiex_debug_info *info)
{
	struct mwifiex_adapter *adapter = priv->adapter;

	if (info) {
		memcpy(info->packets_out,
		       priv->wmm.packets_out,
		       sizeof(priv->wmm.packets_out));
		info->curr_tx_buf_size = (u32) adapter->curr_tx_buf_size;
		info->tx_buf_size = (u32) adapter->tx_buf_size;
		info->rx_tbl_num = mwifiex_get_rx_reorder_tbl(priv,
							      info->rx_tbl);
		info->tx_tbl_num = mwifiex_get_tx_ba_stream_tbl(priv,
								info->tx_tbl);
		info->ps_mode = adapter->ps_mode;
		info->ps_state = adapter->ps_state;
		info->is_deep_sleep = adapter->is_deep_sleep;
		info->pm_wakeup_card_req = adapter->pm_wakeup_card_req;
		info->pm_wakeup_fw_try = adapter->pm_wakeup_fw_try;
		info->is_hs_configured = adapter->is_hs_configured;
		info->hs_activated = adapter->hs_activated;
		info->num_cmd_host_to_card_failure
				= adapter->dbg.num_cmd_host_to_card_failure;
		info->num_cmd_sleep_cfm_host_to_card_failure
			= adapter->dbg.num_cmd_sleep_cfm_host_to_card_failure;
		info->num_tx_host_to_card_failure
				= adapter->dbg.num_tx_host_to_card_failure;
		info->num_event_deauth = adapter->dbg.num_event_deauth;
		info->num_event_disassoc = adapter->dbg.num_event_disassoc;
		info->num_event_link_lost = adapter->dbg.num_event_link_lost;
		info->num_cmd_deauth = adapter->dbg.num_cmd_deauth;
		info->num_cmd_assoc_success =
					adapter->dbg.num_cmd_assoc_success;
		info->num_cmd_assoc_failure =
					adapter->dbg.num_cmd_assoc_failure;
		info->num_tx_timeout = adapter->dbg.num_tx_timeout;
		info->num_cmd_timeout = adapter->dbg.num_cmd_timeout;
		info->timeout_cmd_id = adapter->dbg.timeout_cmd_id;
		info->timeout_cmd_act = adapter->dbg.timeout_cmd_act;
		memcpy(info->last_cmd_id, adapter->dbg.last_cmd_id,
		       sizeof(adapter->dbg.last_cmd_id));
		memcpy(info->last_cmd_act, adapter->dbg.last_cmd_act,
		       sizeof(adapter->dbg.last_cmd_act));
		info->last_cmd_index = adapter->dbg.last_cmd_index;
		memcpy(info->last_cmd_resp_id, adapter->dbg.last_cmd_resp_id,
		       sizeof(adapter->dbg.last_cmd_resp_id));
		info->last_cmd_resp_index = adapter->dbg.last_cmd_resp_index;
		memcpy(info->last_event, adapter->dbg.last_event,
		       sizeof(adapter->dbg.last_event));
		info->last_event_index = adapter->dbg.last_event_index;
		info->data_sent = adapter->data_sent;
		info->cmd_sent = adapter->cmd_sent;
		info->cmd_resp_received = adapter->cmd_resp_received;
	}

	return 0;
}

/*
 * This function processes the received management packet and send it
 * to the kernel.
 */
int
mwifiex_process_mgmt_packet(struct mwifiex_private *priv,
			    struct sk_buff *skb)
{
	struct rxpd *rx_pd;
	u16 pkt_len;

	if (!skb)
		return -1;

	rx_pd = (struct rxpd *)skb->data;

	skb_pull(skb, le16_to_cpu(rx_pd->rx_pkt_offset));
	skb_pull(skb, sizeof(pkt_len));

	pkt_len = le16_to_cpu(rx_pd->rx_pkt_length);

	/* Remove address4 */
	memmove(skb->data + sizeof(struct ieee80211_hdr_3addr),
		skb->data + sizeof(struct ieee80211_hdr),
		pkt_len - sizeof(struct ieee80211_hdr));

	pkt_len -= ETH_ALEN + sizeof(pkt_len);
	rx_pd->rx_pkt_length = cpu_to_le16(pkt_len);

	cfg80211_rx_mgmt(priv->wdev, priv->roc_cfg.chan.center_freq,
			 CAL_RSSI(rx_pd->snr, rx_pd->nf),
			 skb->data, pkt_len, GFP_ATOMIC);

	return 0;
}

/*
 * This function processes the received packet before sending it to the
 * kernel.
 *
 * It extracts the SKB from the received buffer and sends it to kernel.
 * In case the received buffer does not contain the data in SKB format,
 * the function creates a blank SKB, fills it with the data from the
 * received buffer and then sends this new SKB to the kernel.
 */
int mwifiex_recv_packet(struct mwifiex_private *priv, struct sk_buff *skb)
{
	if (!skb)
		return -1;

	skb->dev = priv->netdev;
	skb->protocol = eth_type_trans(skb, priv->netdev);
	skb->ip_summed = CHECKSUM_NONE;

	/* This is required only in case of 11n and USB/PCIE as we alloc
	 * a buffer of 4K only if its 11N (to be able to receive 4K
	 * AMSDU packets). In case of SD we allocate buffers based
	 * on the size of packet and hence this is not needed.
	 *
	 * Modifying the truesize here as our allocation for each
	 * skb is 4K but we only receive 2K packets and this cause
	 * the kernel to start dropping packets in case where
	 * application has allocated buffer based on 2K size i.e.
	 * if there a 64K packet received (in IP fragments and
	 * application allocates 64K to receive this packet but
	 * this packet would almost double up because we allocate
	 * each 1.5K fragment in 4K and pass it up. As soon as the
	 * 64K limit hits kernel will start to drop rest of the
	 * fragments. Currently we fail the Filesndl-ht.scr script
	 * for UDP, hence this fix
	 */
	if ((priv->adapter->iface_type == MWIFIEX_USB ||
	     priv->adapter->iface_type == MWIFIEX_PCIE) &&
	    (skb->truesize > MWIFIEX_RX_DATA_BUF_SIZE))
		skb->truesize += (skb->len - MWIFIEX_RX_DATA_BUF_SIZE);

	priv->stats.rx_bytes += skb->len;
	priv->stats.rx_packets++;
	if (in_interrupt())
		netif_rx(skb);
	else
		netif_rx_ni(skb);

	return 0;
}

/*
 * IOCTL completion callback handler.
 *
 * This function is called when a pending IOCTL is completed.
 *
 * If work queue support is enabled, the function wakes up the
 * corresponding waiting function. Otherwise, it processes the
 * IOCTL response and frees the response buffer.
 */
int mwifiex_complete_cmd(struct mwifiex_adapter *adapter,
			 struct cmd_ctrl_node *cmd_node)
{
	atomic_dec(&adapter->cmd_pending);
	dev_dbg(adapter->dev, "cmd completed: status=%d\n",
		adapter->cmd_wait_q.status);

	*(cmd_node->condition) = true;

	if (adapter->cmd_wait_q.status == -ETIMEDOUT)
		dev_err(adapter->dev, "cmd timeout\n");
	else
		wake_up_interruptible(&adapter->cmd_wait_q.wait);

	return 0;
}
