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

#include "octep_config.h"
#include "octep_main.h"
#include "octep_ctrl_net.h"

int octep_get_link_status(struct octep_device *oct)
{
	struct octep_ctrl_net_h2f_req req = {};
	struct octep_ctrl_net_h2f_resp *resp;
	struct octep_ctrl_mbox_msg msg = {};
	int err;

	req.hdr.cmd = OCTEP_CTRL_NET_H2F_CMD_LINK_STATUS;
	req.link.cmd = OCTEP_CTRL_NET_CMD_GET;

	msg.hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_REQ;
	msg.hdr.sizew = OCTEP_CTRL_NET_H2F_STATE_REQ_SZW;
	msg.msg = &req;
	err = octep_ctrl_mbox_send(&oct->ctrl_mbox, &msg);
	if (err)
		return err;

	resp = (struct octep_ctrl_net_h2f_resp *)&req;
	return resp->link.state;
}

void octep_set_link_status(struct octep_device *oct, bool up)
{
	struct octep_ctrl_net_h2f_req req = {};
	struct octep_ctrl_mbox_msg msg = {};

	req.hdr.cmd = OCTEP_CTRL_NET_H2F_CMD_LINK_STATUS;
	req.link.cmd = OCTEP_CTRL_NET_CMD_SET;
	req.link.state = (up) ? OCTEP_CTRL_NET_STATE_UP : OCTEP_CTRL_NET_STATE_DOWN;

	msg.hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_REQ;
	msg.hdr.sizew = OCTEP_CTRL_NET_H2F_STATE_REQ_SZW;
	msg.msg = &req;
	octep_ctrl_mbox_send(&oct->ctrl_mbox, &msg);
}

void octep_set_rx_state(struct octep_device *oct, bool up)
{
	struct octep_ctrl_net_h2f_req req = {};
	struct octep_ctrl_mbox_msg msg = {};

	req.hdr.cmd = OCTEP_CTRL_NET_H2F_CMD_RX_STATE;
	req.link.cmd = OCTEP_CTRL_NET_CMD_SET;
	req.link.state = (up) ? OCTEP_CTRL_NET_STATE_UP : OCTEP_CTRL_NET_STATE_DOWN;

	msg.hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_REQ;
	msg.hdr.sizew = OCTEP_CTRL_NET_H2F_STATE_REQ_SZW;
	msg.msg = &req;
	octep_ctrl_mbox_send(&oct->ctrl_mbox, &msg);
}

int octep_get_mac_addr(struct octep_device *oct, u8 *addr)
{
	struct octep_ctrl_net_h2f_req req = {};
	struct octep_ctrl_net_h2f_resp *resp;
	struct octep_ctrl_mbox_msg msg = {};
	int err;

	req.hdr.cmd = OCTEP_CTRL_NET_H2F_CMD_MAC;
	req.link.cmd = OCTEP_CTRL_NET_CMD_GET;

	msg.hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_REQ;
	msg.hdr.sizew = OCTEP_CTRL_NET_H2F_MAC_REQ_SZW;
	msg.msg = &req;
	err = octep_ctrl_mbox_send(&oct->ctrl_mbox, &msg);
	if (err)
		return err;

	resp = (struct octep_ctrl_net_h2f_resp *)&req;
	memcpy(addr, resp->mac.addr, ETH_ALEN);

	return err;
}

int octep_set_mac_addr(struct octep_device *oct, u8 *addr)
{
	struct octep_ctrl_net_h2f_req req = {};
	struct octep_ctrl_mbox_msg msg = {};

	req.hdr.cmd = OCTEP_CTRL_NET_H2F_CMD_MAC;
	req.mac.cmd = OCTEP_CTRL_NET_CMD_SET;
	memcpy(&req.mac.addr, addr, ETH_ALEN);

	msg.hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_REQ;
	msg.hdr.sizew = OCTEP_CTRL_NET_H2F_MAC_REQ_SZW;
	msg.msg = &req;

	return octep_ctrl_mbox_send(&oct->ctrl_mbox, &msg);
}

int octep_set_mtu(struct octep_device *oct, int mtu)
{
	struct octep_ctrl_net_h2f_req req = {};
	struct octep_ctrl_mbox_msg msg = {};

	req.hdr.cmd = OCTEP_CTRL_NET_H2F_CMD_MTU;
	req.mtu.cmd = OCTEP_CTRL_NET_CMD_SET;
	req.mtu.val = mtu;

	msg.hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_REQ;
	msg.hdr.sizew = OCTEP_CTRL_NET_H2F_MTU_REQ_SZW;
	msg.msg = &req;

	return octep_ctrl_mbox_send(&oct->ctrl_mbox, &msg);
}

int octep_get_if_stats(struct octep_device *oct)
{
	void __iomem *iface_rx_stats;
	void __iomem *iface_tx_stats;
	struct octep_ctrl_net_h2f_req req = {};
	struct octep_ctrl_mbox_msg msg = {};
	int err;

	req.hdr.cmd = OCTEP_CTRL_NET_H2F_CMD_GET_IF_STATS;
	req.mac.cmd = OCTEP_CTRL_NET_CMD_GET;
	req.get_stats.offset = oct->ctrl_mbox_ifstats_offset;

	msg.hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_REQ;
	msg.hdr.sizew = OCTEP_CTRL_NET_H2F_GET_STATS_REQ_SZW;
	msg.msg = &req;
	err = octep_ctrl_mbox_send(&oct->ctrl_mbox, &msg);
	if (err)
		return err;

	iface_rx_stats = oct->ctrl_mbox.barmem + oct->ctrl_mbox_ifstats_offset;
	iface_tx_stats = oct->ctrl_mbox.barmem + oct->ctrl_mbox_ifstats_offset +
			 sizeof(struct octep_iface_rx_stats);
	memcpy_fromio(&oct->iface_rx_stats, iface_rx_stats, sizeof(struct octep_iface_rx_stats));
	memcpy_fromio(&oct->iface_tx_stats, iface_tx_stats, sizeof(struct octep_iface_tx_stats));

	return err;
}

int octep_get_link_info(struct octep_device *oct)
{
	struct octep_ctrl_net_h2f_req req = {};
	struct octep_ctrl_net_h2f_resp *resp;
	struct octep_ctrl_mbox_msg msg = {};
	int err;

	req.hdr.cmd = OCTEP_CTRL_NET_H2F_CMD_LINK_INFO;
	req.mac.cmd = OCTEP_CTRL_NET_CMD_GET;

	msg.hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_REQ;
	msg.hdr.sizew = OCTEP_CTRL_NET_H2F_LINK_INFO_REQ_SZW;
	msg.msg = &req;
	err = octep_ctrl_mbox_send(&oct->ctrl_mbox, &msg);
	if (err)
		return err;

	resp = (struct octep_ctrl_net_h2f_resp *)&req;
	oct->link_info.supported_modes = resp->link_info.supported_modes;
	oct->link_info.advertised_modes = resp->link_info.advertised_modes;
	oct->link_info.autoneg = resp->link_info.autoneg;
	oct->link_info.pause = resp->link_info.pause;
	oct->link_info.speed = resp->link_info.speed;

	return err;
}

int octep_set_link_info(struct octep_device *oct, struct octep_iface_link_info *link_info)
{
	struct octep_ctrl_net_h2f_req req = {};
	struct octep_ctrl_mbox_msg msg = {};

	req.hdr.cmd = OCTEP_CTRL_NET_H2F_CMD_LINK_INFO;
	req.link_info.cmd = OCTEP_CTRL_NET_CMD_SET;
	req.link_info.info.advertised_modes = link_info->advertised_modes;
	req.link_info.info.autoneg = link_info->autoneg;
	req.link_info.info.pause = link_info->pause;
	req.link_info.info.speed = link_info->speed;

	msg.hdr.flags = OCTEP_CTRL_MBOX_MSG_HDR_FLAG_REQ;
	msg.hdr.sizew = OCTEP_CTRL_NET_H2F_LINK_INFO_REQ_SZW;
	msg.msg = &req;

	return octep_ctrl_mbox_send(&oct->ctrl_mbox, &msg);
}
