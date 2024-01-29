// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>

#include "octep_config.h"
#include "octep_main.h"
#include "octep_pfvf_mbox.h"
#include "octep_ctrl_net.h"

/* When a new command is implemented, the below table should be updated
 * with new command and it's version info.
 */
static u32 pfvf_cmd_versions[OCTEP_PFVF_MBOX_CMD_MAX] = {
	[0 ... OCTEP_PFVF_MBOX_CMD_DEV_REMOVE] = OCTEP_PFVF_MBOX_VERSION_V1,
	[OCTEP_PFVF_MBOX_CMD_GET_FW_INFO ... OCTEP_PFVF_MBOX_NOTIF_LINK_STATUS] =
		OCTEP_PFVF_MBOX_VERSION_V2
};

static void octep_pfvf_validate_version(struct octep_device *oct,  u32 vf_id,
					union octep_pfvf_mbox_word cmd,
					union octep_pfvf_mbox_word *rsp)
{
	u32 vf_version = (u32)cmd.s_version.version;

	dev_dbg(&oct->pdev->dev, "VF id:%d VF version:%d PF version:%d\n",
		vf_id, vf_version, OCTEP_PFVF_MBOX_VERSION_CURRENT);
	if (vf_version < OCTEP_PFVF_MBOX_VERSION_CURRENT)
		rsp->s_version.version = vf_version;
	else
		rsp->s_version.version = OCTEP_PFVF_MBOX_VERSION_CURRENT;

	oct->vf_info[vf_id].mbox_version = rsp->s_version.version;
	dev_dbg(&oct->pdev->dev, "VF id:%d negotiated VF version:%d\n",
		vf_id, oct->vf_info[vf_id].mbox_version);

	rsp->s_version.type = OCTEP_PFVF_MBOX_TYPE_RSP_ACK;
}

static void octep_pfvf_get_link_status(struct octep_device *oct, u32 vf_id,
				       union octep_pfvf_mbox_word cmd,
				       union octep_pfvf_mbox_word *rsp)
{
	int status;

	status = octep_ctrl_net_get_link_status(oct, vf_id);
	if (status < 0) {
		rsp->s_link_status.type = OCTEP_PFVF_MBOX_TYPE_RSP_NACK;
		dev_err(&oct->pdev->dev, "Get VF link status failed via host control Mbox\n");
		return;
	}
	rsp->s_link_status.type = OCTEP_PFVF_MBOX_TYPE_RSP_ACK;
	rsp->s_link_status.status = status;
}

static void octep_pfvf_set_link_status(struct octep_device *oct, u32 vf_id,
				       union octep_pfvf_mbox_word cmd,
				       union octep_pfvf_mbox_word *rsp)
{
	int err;

	err = octep_ctrl_net_set_link_status(oct, vf_id, cmd.s_link_status.status, true);
	if (err) {
		rsp->s_link_status.type = OCTEP_PFVF_MBOX_TYPE_RSP_NACK;
		dev_err(&oct->pdev->dev, "Set VF link status failed via host control Mbox\n");
		return;
	}
	rsp->s_link_status.type = OCTEP_PFVF_MBOX_TYPE_RSP_ACK;
}

static void octep_pfvf_set_rx_state(struct octep_device *oct, u32 vf_id,
				    union octep_pfvf_mbox_word cmd,
				    union octep_pfvf_mbox_word *rsp)
{
	int err;

	err = octep_ctrl_net_set_rx_state(oct, vf_id, cmd.s_link_state.state, true);
	if (err) {
		rsp->s_link_state.type = OCTEP_PFVF_MBOX_TYPE_RSP_NACK;
		dev_err(&oct->pdev->dev, "Set VF Rx link state failed via host control Mbox\n");
		return;
	}
	rsp->s_link_state.type = OCTEP_PFVF_MBOX_TYPE_RSP_ACK;
}

static int octep_send_notification(struct octep_device *oct, u32 vf_id,
				   union octep_pfvf_mbox_word cmd)
{
	u32 max_rings_per_vf, vf_mbox_queue;
	struct octep_mbox *mbox;

	/* check if VF PF Mailbox is compatible for this notification */
	if (pfvf_cmd_versions[cmd.s.opcode] > oct->vf_info[vf_id].mbox_version) {
		dev_dbg(&oct->pdev->dev, "VF Mbox doesn't support Notification:%d on VF ver:%d\n",
			cmd.s.opcode, oct->vf_info[vf_id].mbox_version);
		return -EOPNOTSUPP;
	}

	max_rings_per_vf = CFG_GET_MAX_RPVF(oct->conf);
	vf_mbox_queue = vf_id * max_rings_per_vf;
	if (!oct->mbox[vf_mbox_queue]) {
		dev_err(&oct->pdev->dev, "Notif obtained for bad mbox vf %d\n", vf_id);
		return -EINVAL;
	}
	mbox = oct->mbox[vf_mbox_queue];

	mutex_lock(&mbox->lock);
	writeq(cmd.u64, mbox->pf_vf_data_reg);
	mutex_unlock(&mbox->lock);

	return 0;
}

static void octep_pfvf_set_mtu(struct octep_device *oct, u32 vf_id,
			       union octep_pfvf_mbox_word cmd,
			       union octep_pfvf_mbox_word *rsp)
{
	int err;

	err = octep_ctrl_net_set_mtu(oct, vf_id, cmd.s_set_mtu.mtu, true);
	if (err) {
		rsp->s_set_mtu.type = OCTEP_PFVF_MBOX_TYPE_RSP_NACK;
		dev_err(&oct->pdev->dev, "Set VF MTU failed via host control Mbox\n");
		return;
	}
	rsp->s_set_mtu.type = OCTEP_PFVF_MBOX_TYPE_RSP_ACK;
}

static void octep_pfvf_get_mtu(struct octep_device *oct, u32 vf_id,
			       union octep_pfvf_mbox_word cmd,
			       union octep_pfvf_mbox_word *rsp)
{
	int max_rx_pktlen = oct->netdev->max_mtu + (ETH_HLEN + ETH_FCS_LEN);

	rsp->s_set_mtu.type = OCTEP_PFVF_MBOX_TYPE_RSP_ACK;
	rsp->s_get_mtu.mtu = max_rx_pktlen;
}

static void octep_pfvf_set_mac_addr(struct octep_device *oct,  u32 vf_id,
				    union octep_pfvf_mbox_word cmd,
				    union octep_pfvf_mbox_word *rsp)
{
	int err;

	err = octep_ctrl_net_set_mac_addr(oct, vf_id, cmd.s_set_mac.mac_addr, true);
	if (err) {
		rsp->s_set_mac.type = OCTEP_PFVF_MBOX_TYPE_RSP_NACK;
		dev_err(&oct->pdev->dev, "Set VF MAC address failed via host control Mbox\n");
		return;
	}
	rsp->s_set_mac.type = OCTEP_PFVF_MBOX_TYPE_RSP_ACK;
}

static void octep_pfvf_get_mac_addr(struct octep_device *oct,  u32 vf_id,
				    union octep_pfvf_mbox_word cmd,
				    union octep_pfvf_mbox_word *rsp)
{
	int err;

	err = octep_ctrl_net_get_mac_addr(oct, vf_id, rsp->s_set_mac.mac_addr);
	if (err) {
		rsp->s_set_mac.type = OCTEP_PFVF_MBOX_TYPE_RSP_NACK;
		dev_err(&oct->pdev->dev, "Get VF MAC address failed via host control Mbox\n");
		return;
	}
	rsp->s_set_mac.type = OCTEP_PFVF_MBOX_TYPE_RSP_ACK;
}

static void octep_pfvf_dev_remove(struct octep_device *oct,  u32 vf_id,
				  union octep_pfvf_mbox_word cmd,
				  union octep_pfvf_mbox_word *rsp)
{
	int err;

	err = octep_ctrl_net_dev_remove(oct, vf_id);
	if (err) {
		rsp->s.type = OCTEP_PFVF_MBOX_TYPE_RSP_NACK;
		dev_err(&oct->pdev->dev, "Failed to acknowledge fw of vf %d removal\n",
			vf_id);
		return;
	}
	rsp->s.type = OCTEP_PFVF_MBOX_TYPE_RSP_ACK;
}

static void octep_pfvf_get_fw_info(struct octep_device *oct,  u32 vf_id,
				   union octep_pfvf_mbox_word cmd,
				   union octep_pfvf_mbox_word *rsp)
{
	struct octep_fw_info fw_info;
	int err;

	err = octep_ctrl_net_get_info(oct, vf_id, &fw_info);
	if (err) {
		rsp->s_fw_info.type = OCTEP_PFVF_MBOX_TYPE_RSP_NACK;
		dev_err(&oct->pdev->dev, "Get VF info failed via host control Mbox\n");
		return;
	}

	rsp->s_fw_info.pkind = fw_info.pkind;
	rsp->s_fw_info.fsz = fw_info.fsz;
	rsp->s_fw_info.rx_ol_flags = fw_info.rx_ol_flags;
	rsp->s_fw_info.tx_ol_flags = fw_info.tx_ol_flags;

	rsp->s_fw_info.type = OCTEP_PFVF_MBOX_TYPE_RSP_ACK;
}

static void octep_pfvf_set_offloads(struct octep_device *oct, u32 vf_id,
				    union octep_pfvf_mbox_word cmd,
				    union octep_pfvf_mbox_word *rsp)
{
	struct octep_ctrl_net_offloads offloads = {
		.rx_offloads = cmd.s_offloads.rx_ol_flags,
		.tx_offloads = cmd.s_offloads.tx_ol_flags
	};
	int err;

	err = octep_ctrl_net_set_offloads(oct, vf_id, &offloads, true);
	if (err) {
		rsp->s_offloads.type = OCTEP_PFVF_MBOX_TYPE_RSP_NACK;
		dev_err(&oct->pdev->dev, "Set VF offloads failed via host control Mbox\n");
		return;
	}
	rsp->s_offloads.type = OCTEP_PFVF_MBOX_TYPE_RSP_ACK;
}

int octep_setup_pfvf_mbox(struct octep_device *oct)
{
	int i = 0, num_vfs = 0, rings_per_vf = 0;
	int ring = 0;

	num_vfs = oct->conf->sriov_cfg.active_vfs;
	rings_per_vf = oct->conf->sriov_cfg.max_rings_per_vf;

	for (i = 0; i < num_vfs; i++) {
		ring  = rings_per_vf * i;
		oct->mbox[ring] = vzalloc(sizeof(*oct->mbox[ring]));

		if (!oct->mbox[ring])
			goto free_mbox;

		memset(oct->mbox[ring], 0, sizeof(struct octep_mbox));
		memset(&oct->vf_info[i], 0, sizeof(struct octep_pfvf_info));
		mutex_init(&oct->mbox[ring]->lock);
		INIT_WORK(&oct->mbox[ring]->wk.work, octep_pfvf_mbox_work);
		oct->mbox[ring]->wk.ctxptr = oct->mbox[ring];
		oct->mbox[ring]->oct = oct;
		oct->mbox[ring]->vf_id = i;
		oct->hw_ops.setup_mbox_regs(oct, ring);
	}
	return 0;

free_mbox:
	while (i) {
		i--;
		ring  = rings_per_vf * i;
		cancel_work_sync(&oct->mbox[ring]->wk.work);
		mutex_destroy(&oct->mbox[ring]->lock);
		vfree(oct->mbox[ring]);
		oct->mbox[ring] = NULL;
	}
	return -ENOMEM;
}

void octep_delete_pfvf_mbox(struct octep_device *oct)
{
	int rings_per_vf = oct->conf->sriov_cfg.max_rings_per_vf;
	int num_vfs = oct->conf->sriov_cfg.active_vfs;
	int i = 0, ring = 0, vf_srn = 0;

	for (i = 0; i < num_vfs; i++) {
		ring  = vf_srn + rings_per_vf * i;
		if (!oct->mbox[ring])
			continue;

		if (work_pending(&oct->mbox[ring]->wk.work))
			cancel_work_sync(&oct->mbox[ring]->wk.work);

		mutex_destroy(&oct->mbox[ring]->lock);
		vfree(oct->mbox[ring]);
		oct->mbox[ring] = NULL;
	}
}

static void octep_pfvf_pf_get_data(struct octep_device *oct,
				   struct octep_mbox *mbox, int vf_id,
				   union octep_pfvf_mbox_word cmd,
				   union octep_pfvf_mbox_word *rsp)
{
	int length = 0;
	int i = 0;
	int err;
	struct octep_iface_link_info link_info;
	struct octep_iface_rx_stats rx_stats;
	struct octep_iface_tx_stats tx_stats;

	rsp->s_data.type = OCTEP_PFVF_MBOX_TYPE_RSP_ACK;

	if (cmd.s_data.frag != OCTEP_PFVF_MBOX_MORE_FRAG_FLAG) {
		mbox->config_data_index = 0;
		memset(mbox->config_data, 0, MAX_VF_PF_MBOX_DATA_SIZE);
		/* Based on the OPCODE CMD the PF driver
		 * specific API should be called to fetch
		 * the requested data
		 */
		switch (cmd.s.opcode) {
		case OCTEP_PFVF_MBOX_CMD_GET_LINK_INFO:
			memset(&link_info, 0, sizeof(link_info));
			err = octep_ctrl_net_get_link_info(oct, vf_id, &link_info);
			if (!err) {
				mbox->message_len = sizeof(link_info);
				*((int32_t *)rsp->s_data.data) = mbox->message_len;
				memcpy(mbox->config_data, (u8 *)&link_info, sizeof(link_info));
			} else {
				rsp->s_data.type = OCTEP_PFVF_MBOX_TYPE_RSP_NACK;
				return;
			}
			break;
		case OCTEP_PFVF_MBOX_CMD_GET_STATS:
			memset(&rx_stats, 0, sizeof(rx_stats));
			memset(&tx_stats, 0, sizeof(tx_stats));
			err = octep_ctrl_net_get_if_stats(oct, vf_id, &rx_stats, &tx_stats);
			if (!err) {
				mbox->message_len = sizeof(rx_stats) + sizeof(tx_stats);
				*((int32_t *)rsp->s_data.data) = mbox->message_len;
				memcpy(mbox->config_data, (u8 *)&rx_stats, sizeof(rx_stats));
				memcpy(mbox->config_data + sizeof(rx_stats), (u8 *)&tx_stats,
				       sizeof(tx_stats));

			} else {
				rsp->s_data.type = OCTEP_PFVF_MBOX_TYPE_RSP_NACK;
				return;
			}
			break;
		}
		*((int32_t *)rsp->s_data.data) = mbox->message_len;
		return;
	}

	if (mbox->message_len > OCTEP_PFVF_MBOX_MAX_DATA_SIZE)
		length = OCTEP_PFVF_MBOX_MAX_DATA_SIZE;
	else
		length = mbox->message_len;

	mbox->message_len -= length;

	for (i = 0; i < length; i++) {
		rsp->s_data.data[i] =
			mbox->config_data[mbox->config_data_index];
		mbox->config_data_index++;
	}
}

void octep_pfvf_notify(struct octep_device *oct, struct octep_ctrl_mbox_msg *msg)
{
	union octep_pfvf_mbox_word notif = { 0 };
	struct octep_ctrl_net_f2h_req *req;

	req = (struct octep_ctrl_net_f2h_req *)msg->sg_list[0].msg;
	switch (req->hdr.s.cmd) {
	case OCTEP_CTRL_NET_F2H_CMD_LINK_STATUS:
		notif.s_link_status.opcode = OCTEP_PFVF_MBOX_NOTIF_LINK_STATUS;
		notif.s_link_status.status = req->link.state;
		break;
	default:
		pr_info("Unknown mbox notif for vf: %u\n",
			req->hdr.s.cmd);
		return;
	}

	notif.s.type = OCTEP_PFVF_MBOX_TYPE_CMD;
	octep_send_notification(oct, msg->hdr.s.vf_idx, notif);
}

void octep_pfvf_mbox_work(struct work_struct *work)
{
	struct octep_pfvf_mbox_wk *wk = container_of(work, struct octep_pfvf_mbox_wk, work);
	union octep_pfvf_mbox_word cmd = { 0 };
	union octep_pfvf_mbox_word rsp = { 0 };
	struct octep_mbox *mbox = NULL;
	struct octep_device *oct = NULL;
	int vf_id;

	mbox = (struct octep_mbox *)wk->ctxptr;
	oct = (struct octep_device *)mbox->oct;
	vf_id = mbox->vf_id;

	mutex_lock(&mbox->lock);
	cmd.u64 = readq(mbox->vf_pf_data_reg);
	rsp.u64 = 0;

	switch (cmd.s.opcode) {
	case OCTEP_PFVF_MBOX_CMD_VERSION:
		octep_pfvf_validate_version(oct, vf_id, cmd, &rsp);
		break;
	case OCTEP_PFVF_MBOX_CMD_GET_LINK_STATUS:
		octep_pfvf_get_link_status(oct, vf_id, cmd, &rsp);
		break;
	case OCTEP_PFVF_MBOX_CMD_SET_LINK_STATUS:
		octep_pfvf_set_link_status(oct, vf_id, cmd, &rsp);
		break;
	case OCTEP_PFVF_MBOX_CMD_SET_RX_STATE:
		octep_pfvf_set_rx_state(oct, vf_id, cmd, &rsp);
		break;
	case OCTEP_PFVF_MBOX_CMD_SET_MTU:
		octep_pfvf_set_mtu(oct, vf_id, cmd, &rsp);
		break;
	case OCTEP_PFVF_MBOX_CMD_SET_MAC_ADDR:
		octep_pfvf_set_mac_addr(oct, vf_id, cmd, &rsp);
		break;
	case OCTEP_PFVF_MBOX_CMD_GET_MAC_ADDR:
		octep_pfvf_get_mac_addr(oct, vf_id, cmd, &rsp);
		break;
	case OCTEP_PFVF_MBOX_CMD_GET_LINK_INFO:
	case OCTEP_PFVF_MBOX_CMD_GET_STATS:
		octep_pfvf_pf_get_data(oct, mbox, vf_id, cmd, &rsp);
		break;
	case OCTEP_PFVF_MBOX_CMD_GET_MTU:
		octep_pfvf_get_mtu(oct, vf_id, cmd, &rsp);
		break;
	case OCTEP_PFVF_MBOX_CMD_DEV_REMOVE:
		octep_pfvf_dev_remove(oct, vf_id, cmd, &rsp);
		break;
	case OCTEP_PFVF_MBOX_CMD_GET_FW_INFO:
		octep_pfvf_get_fw_info(oct, vf_id, cmd, &rsp);
		break;
	case OCTEP_PFVF_MBOX_CMD_SET_OFFLOADS:
		octep_pfvf_set_offloads(oct, vf_id, cmd, &rsp);
		break;
	default:
		dev_err(&oct->pdev->dev, "PF-VF mailbox: invalid opcode %d\n", cmd.s.opcode);
		rsp.s.type = OCTEP_PFVF_MBOX_TYPE_RSP_NACK;
		break;
	}
	writeq(rsp.u64, mbox->vf_pf_data_reg);
	mutex_unlock(&mbox->lock);
}
