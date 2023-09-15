// SPDX-License-Identifier: GPL-2.0
/* Marvell Octeon EP (EndPoint) Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */
#include <linux/string.h>
#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <linux/wait.h>

#include "octep_config.h"
#include "octep_main.h"
#include "octep_ctrl_net.h"

/* Control plane version */
#define OCTEP_CP_VERSION_CURRENT	OCTEP_CP_VERSION(1, 0, 0)

static const u32 req_hdr_sz = sizeof(union octep_ctrl_net_req_hdr);
static const u32 mtu_sz = sizeof(struct octep_ctrl_net_h2f_req_cmd_mtu);
static const u32 mac_sz = sizeof(struct octep_ctrl_net_h2f_req_cmd_mac);
static const u32 state_sz = sizeof(struct octep_ctrl_net_h2f_req_cmd_state);
static const u32 link_info_sz = sizeof(struct octep_ctrl_net_link_info);
static atomic_t ctrl_net_msg_id;

/* Control plane version in which OCTEP_CTRL_NET_H2F_CMD was added */
static const u32 octep_ctrl_net_h2f_cmd_versions[OCTEP_CTRL_NET_H2F_CMD_MAX] = {
	[OCTEP_CTRL_NET_H2F_CMD_INVALID ... OCTEP_CTRL_NET_H2F_CMD_LINK_INFO] =
	 OCTEP_CP_VERSION(1, 0, 0)
};

/* Control plane version in which OCTEP_CTRL_NET_F2H_CMD was added */
static const u32 octep_ctrl_net_f2h_cmd_versions[OCTEP_CTRL_NET_F2H_CMD_MAX] = {
	[OCTEP_CTRL_NET_F2H_CMD_INVALID ... OCTEP_CTRL_NET_F2H_CMD_LINK_STATUS] =
	 OCTEP_CP_VERSION(1, 0, 0)
};

static void init_send_req(struct octep_ctrl_mbox_msg *msg, void *buf,
			  u16 sz, int vfid)
{
	msg->hdr.s.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_REQ;
	msg->hdr.s.msg_id = atomic_inc_return(&ctrl_net_msg_id) &
			    GENMASK(sizeof(msg->hdr.s.msg_id) * BITS_PER_BYTE, 0);
	msg->hdr.s.sz = req_hdr_sz + sz;
	msg->sg_num = 1;
	msg->sg_list[0].msg = buf;
	msg->sg_list[0].sz = msg->hdr.s.sz;
	if (vfid != OCTEP_CTRL_NET_INVALID_VFID) {
		msg->hdr.s.is_vf = 1;
		msg->hdr.s.vf_idx = vfid;
	}
}

static int octep_send_mbox_req(struct octep_device *oct,
			       struct octep_ctrl_net_wait_data *d,
			       bool wait_for_response)
{
	int err, ret, cmd;

	/* check if firmware is compatible for this request */
	cmd = d->data.req.hdr.s.cmd;
	if (octep_ctrl_net_h2f_cmd_versions[cmd] > oct->ctrl_mbox.max_fw_version ||
	    octep_ctrl_net_h2f_cmd_versions[cmd] < oct->ctrl_mbox.min_fw_version)
		return -EOPNOTSUPP;

	err = octep_ctrl_mbox_send(&oct->ctrl_mbox, &d->msg);
	if (err < 0)
		return err;

	if (!wait_for_response)
		return 0;

	d->done = 0;
	INIT_LIST_HEAD(&d->list);
	list_add_tail(&d->list, &oct->ctrl_req_wait_list);
	ret = wait_event_interruptible_timeout(oct->ctrl_req_wait_q,
					       (d->done != 0),
					       msecs_to_jiffies(500));
	list_del(&d->list);
	if (ret == 0 || ret == 1)
		return -EAGAIN;

	/**
	 * (ret == 0)  cond = false && timeout, return 0
	 * (ret < 0) interrupted by signal, return 0
	 * (ret == 1) cond = true && timeout, return 1
	 * (ret >= 1) cond = true && !timeout, return 1
	 */

	if (d->data.resp.hdr.s.reply != OCTEP_CTRL_NET_REPLY_OK)
		return -EAGAIN;

	return 0;
}

int octep_ctrl_net_init(struct octep_device *oct)
{
	struct octep_ctrl_mbox *ctrl_mbox;
	struct pci_dev *pdev = oct->pdev;
	int ret;

	init_waitqueue_head(&oct->ctrl_req_wait_q);
	INIT_LIST_HEAD(&oct->ctrl_req_wait_list);

	/* Initialize control mbox */
	ctrl_mbox = &oct->ctrl_mbox;
	ctrl_mbox->version = OCTEP_CP_VERSION_CURRENT;
	ctrl_mbox->barmem = CFG_GET_CTRL_MBOX_MEM_ADDR(oct->conf);
	ret = octep_ctrl_mbox_init(ctrl_mbox);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize control mbox\n");
		return ret;
	}
	dev_info(&pdev->dev, "Control plane versions host: %llx, firmware: %x:%x\n",
		 ctrl_mbox->version, ctrl_mbox->min_fw_version,
		 ctrl_mbox->max_fw_version);
	oct->ctrl_mbox_ifstats_offset = ctrl_mbox->barmem_sz;

	return 0;
}

int octep_ctrl_net_get_link_status(struct octep_device *oct, int vfid)
{
	struct octep_ctrl_net_wait_data d = {0};
	struct octep_ctrl_net_h2f_req *req = &d.data.req;
	int err;

	init_send_req(&d.msg, (void *)req, state_sz, vfid);
	req->hdr.s.cmd = OCTEP_CTRL_NET_H2F_CMD_LINK_STATUS;
	req->link.cmd = OCTEP_CTRL_NET_CMD_GET;
	err = octep_send_mbox_req(oct, &d, true);
	if (err < 0)
		return err;

	return d.data.resp.link.state;
}

int octep_ctrl_net_set_link_status(struct octep_device *oct, int vfid, bool up,
				   bool wait_for_response)
{
	struct octep_ctrl_net_wait_data d = {0};
	struct octep_ctrl_net_h2f_req *req = &d.data.req;

	init_send_req(&d.msg, req, state_sz, vfid);
	req->hdr.s.cmd = OCTEP_CTRL_NET_H2F_CMD_LINK_STATUS;
	req->link.cmd = OCTEP_CTRL_NET_CMD_SET;
	req->link.state = (up) ? OCTEP_CTRL_NET_STATE_UP :
				OCTEP_CTRL_NET_STATE_DOWN;

	return octep_send_mbox_req(oct, &d, wait_for_response);
}

int octep_ctrl_net_set_rx_state(struct octep_device *oct, int vfid, bool up,
				bool wait_for_response)
{
	struct octep_ctrl_net_wait_data d = {0};
	struct octep_ctrl_net_h2f_req *req = &d.data.req;

	init_send_req(&d.msg, req, state_sz, vfid);
	req->hdr.s.cmd = OCTEP_CTRL_NET_H2F_CMD_RX_STATE;
	req->link.cmd = OCTEP_CTRL_NET_CMD_SET;
	req->link.state = (up) ? OCTEP_CTRL_NET_STATE_UP :
				OCTEP_CTRL_NET_STATE_DOWN;

	return octep_send_mbox_req(oct, &d, wait_for_response);
}

int octep_ctrl_net_get_mac_addr(struct octep_device *oct, int vfid, u8 *addr)
{
	struct octep_ctrl_net_wait_data d = {0};
	struct octep_ctrl_net_h2f_req *req = &d.data.req;
	int err;

	init_send_req(&d.msg, req, mac_sz, vfid);
	req->hdr.s.cmd = OCTEP_CTRL_NET_H2F_CMD_MAC;
	req->link.cmd = OCTEP_CTRL_NET_CMD_GET;
	err = octep_send_mbox_req(oct, &d, true);
	if (err < 0)
		return err;

	memcpy(addr, d.data.resp.mac.addr, ETH_ALEN);

	return 0;
}

int octep_ctrl_net_set_mac_addr(struct octep_device *oct, int vfid, u8 *addr,
				bool wait_for_response)
{
	struct octep_ctrl_net_wait_data d = {0};
	struct octep_ctrl_net_h2f_req *req = &d.data.req;

	init_send_req(&d.msg, req, mac_sz, vfid);
	req->hdr.s.cmd = OCTEP_CTRL_NET_H2F_CMD_MAC;
	req->mac.cmd = OCTEP_CTRL_NET_CMD_SET;
	memcpy(&req->mac.addr, addr, ETH_ALEN);

	return octep_send_mbox_req(oct, &d, wait_for_response);
}

int octep_ctrl_net_set_mtu(struct octep_device *oct, int vfid, int mtu,
			   bool wait_for_response)
{
	struct octep_ctrl_net_wait_data d = {0};
	struct octep_ctrl_net_h2f_req *req = &d.data.req;

	init_send_req(&d.msg, req, mtu_sz, vfid);
	req->hdr.s.cmd = OCTEP_CTRL_NET_H2F_CMD_MTU;
	req->mtu.cmd = OCTEP_CTRL_NET_CMD_SET;
	req->mtu.val = mtu;

	return octep_send_mbox_req(oct, &d, wait_for_response);
}

int octep_ctrl_net_get_if_stats(struct octep_device *oct, int vfid,
				struct octep_iface_rx_stats *rx_stats,
				struct octep_iface_tx_stats *tx_stats)
{
	struct octep_ctrl_net_wait_data d = {0};
	struct octep_ctrl_net_h2f_req *req = &d.data.req;
	struct octep_ctrl_net_h2f_resp *resp;
	int err;

	init_send_req(&d.msg, req, 0, vfid);
	req->hdr.s.cmd = OCTEP_CTRL_NET_H2F_CMD_GET_IF_STATS;
	err = octep_send_mbox_req(oct, &d, true);
	if (err < 0)
		return err;

	resp = &d.data.resp;
	memcpy(rx_stats, &resp->if_stats.rx_stats, sizeof(struct octep_iface_rx_stats));
	memcpy(tx_stats, &resp->if_stats.tx_stats, sizeof(struct octep_iface_tx_stats));
	return 0;
}

int octep_ctrl_net_get_link_info(struct octep_device *oct, int vfid,
				 struct octep_iface_link_info *link_info)
{
	struct octep_ctrl_net_wait_data d = {0};
	struct octep_ctrl_net_h2f_req *req = &d.data.req;
	struct octep_ctrl_net_h2f_resp *resp;
	int err;

	init_send_req(&d.msg, req, link_info_sz, vfid);
	req->hdr.s.cmd = OCTEP_CTRL_NET_H2F_CMD_LINK_INFO;
	req->link_info.cmd = OCTEP_CTRL_NET_CMD_GET;
	err = octep_send_mbox_req(oct, &d, true);
	if (err < 0)
		return err;

	resp = &d.data.resp;
	link_info->supported_modes = resp->link_info.supported_modes;
	link_info->advertised_modes = resp->link_info.advertised_modes;
	link_info->autoneg = resp->link_info.autoneg;
	link_info->pause = resp->link_info.pause;
	link_info->speed = resp->link_info.speed;

	return 0;
}

int octep_ctrl_net_set_link_info(struct octep_device *oct, int vfid,
				 struct octep_iface_link_info *link_info,
				 bool wait_for_response)
{
	struct octep_ctrl_net_wait_data d = {0};
	struct octep_ctrl_net_h2f_req *req = &d.data.req;

	init_send_req(&d.msg, req, link_info_sz, vfid);
	req->hdr.s.cmd = OCTEP_CTRL_NET_H2F_CMD_LINK_INFO;
	req->link_info.cmd = OCTEP_CTRL_NET_CMD_SET;
	req->link_info.info.advertised_modes = link_info->advertised_modes;
	req->link_info.info.autoneg = link_info->autoneg;
	req->link_info.info.pause = link_info->pause;
	req->link_info.info.speed = link_info->speed;

	return octep_send_mbox_req(oct, &d, wait_for_response);
}

static void process_mbox_resp(struct octep_device *oct,
			      struct octep_ctrl_mbox_msg *msg)
{
	struct octep_ctrl_net_wait_data *pos, *n;

	list_for_each_entry_safe(pos, n, &oct->ctrl_req_wait_list, list) {
		if (pos->msg.hdr.s.msg_id == msg->hdr.s.msg_id) {
			memcpy(&pos->data.resp,
			       msg->sg_list[0].msg,
			       msg->hdr.s.sz);
			pos->done = 1;
			wake_up_interruptible_all(&oct->ctrl_req_wait_q);
			break;
		}
	}
}

static int process_mbox_notify(struct octep_device *oct,
			       struct octep_ctrl_mbox_msg *msg)
{
	struct net_device *netdev = oct->netdev;
	struct octep_ctrl_net_f2h_req *req;
	int cmd;

	req = (struct octep_ctrl_net_f2h_req *)msg->sg_list[0].msg;
	cmd = req->hdr.s.cmd;

	/* check if we support this command */
	if (octep_ctrl_net_f2h_cmd_versions[cmd] > OCTEP_CP_VERSION_CURRENT ||
	    octep_ctrl_net_f2h_cmd_versions[cmd] < OCTEP_CP_VERSION_CURRENT)
		return -EOPNOTSUPP;

	switch (cmd) {
	case OCTEP_CTRL_NET_F2H_CMD_LINK_STATUS:
		if (netif_running(netdev)) {
			if (req->link.state) {
				dev_info(&oct->pdev->dev, "netif_carrier_on\n");
				netif_carrier_on(netdev);
			} else {
				dev_info(&oct->pdev->dev, "netif_carrier_off\n");
				netif_carrier_off(netdev);
			}
		}
		break;
	default:
		pr_info("Unknown mbox req : %u\n", req->hdr.s.cmd);
		break;
	}

	return 0;
}

void octep_ctrl_net_recv_fw_messages(struct octep_device *oct)
{
	static u16 msg_sz = sizeof(union octep_ctrl_net_max_data);
	union octep_ctrl_net_max_data data = {0};
	struct octep_ctrl_mbox_msg msg = {0};
	int ret;

	msg.hdr.s.sz = msg_sz;
	msg.sg_num = 1;
	msg.sg_list[0].sz = msg_sz;
	msg.sg_list[0].msg = &data;
	while (true) {
		/* mbox will overwrite msg.hdr.s.sz so initialize it */
		msg.hdr.s.sz = msg_sz;
		ret = octep_ctrl_mbox_recv(&oct->ctrl_mbox, (struct octep_ctrl_mbox_msg *)&msg);
		if (ret < 0)
			break;

		if (msg.hdr.s.flags & OCTEP_CTRL_MBOX_MSG_HDR_FLAG_RESP)
			process_mbox_resp(oct, &msg);
		else if (msg.hdr.s.flags & OCTEP_CTRL_MBOX_MSG_HDR_FLAG_NOTIFY)
			process_mbox_notify(oct, &msg);
	}
}

int octep_ctrl_net_uninit(struct octep_device *oct)
{
	struct octep_ctrl_net_wait_data *pos, *n;

	list_for_each_entry_safe(pos, n, &oct->ctrl_req_wait_list, list)
		pos->done = 1;

	wake_up_interruptible_all(&oct->ctrl_req_wait_q);

	octep_ctrl_mbox_uninit(&oct->ctrl_mbox);

	return 0;
}
