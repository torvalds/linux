// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2015 - 2025 Beijing WangXun Technology Co., Ltd. */

#include <linux/etherdevice.h>
#include <linux/pci.h>

#include "wx_type.h"
#include "wx_hw.h"
#include "wx_mbx.h"
#include "wx_vf.h"

static void wx_virt_clr_reg(struct wx *wx)
{
	u32 vfsrrctl, i;

	/* VRSRRCTL default values (BSIZEPACKET = 2048, BSIZEHEADER = 256) */
	vfsrrctl = WX_VXRXDCTL_HDRSZ(wx_hdr_sz(WX_RX_HDR_SIZE));
	vfsrrctl |= WX_VXRXDCTL_BUFSZ(wx_buf_sz(WX_RX_BUF_SIZE));

	/* clear all rxd ctl */
	for (i = 0; i < WX_VF_MAX_RING_NUMS; i++)
		wr32m(wx, WX_VXRXDCTL(i),
		      WX_VXRXDCTL_HDRSZ_MASK | WX_VXRXDCTL_BUFSZ_MASK,
		      vfsrrctl);

	rd32(wx, WX_VXSTATUS);
}

/**
 *  wx_init_hw_vf - virtual function hardware initialization
 *  @wx: pointer to hardware structure
 *
 *  Initialize the mac address
 **/
void wx_init_hw_vf(struct wx *wx)
{
	wx_get_mac_addr_vf(wx, wx->mac.addr);
}
EXPORT_SYMBOL(wx_init_hw_vf);

static int wx_mbx_write_and_read_reply(struct wx *wx, u32 *req_buf,
				       u32 *resp_buf, u16 size)
{
	int ret;

	ret = wx_write_posted_mbx(wx, req_buf, size);
	if (ret)
		return ret;

	return wx_read_posted_mbx(wx, resp_buf, size);
}

/**
 *  wx_reset_hw_vf - Performs hardware reset
 *  @wx: pointer to hardware structure
 *
 *  Resets the hardware by resetting the transmit and receive units, masks and
 *  clears all interrupts.
 *
 *  Return: returns 0 on success, negative error code on failure
 **/
int wx_reset_hw_vf(struct wx *wx)
{
	struct wx_mbx_info *mbx = &wx->mbx;
	u32 msgbuf[4] = {WX_VF_RESET};
	u8 *addr = (u8 *)(&msgbuf[1]);
	u32 b4_buf[16] = {0};
	u32 timeout = 200;
	int ret;
	u32 i;

	/* Call wx stop to disable tx/rx and clear interrupts */
	wx_stop_adapter_vf(wx);

	/* reset the api version */
	wx->vfinfo->vf_api = wx_mbox_api_null;

	/* backup msix vectors */
	if (wx->b4_addr) {
		for (i = 0; i < 16; i++)
			b4_buf[i] = readl(wx->b4_addr + i * 4);
	}

	wr32m(wx, WX_VXCTRL, WX_VXCTRL_RST, WX_VXCTRL_RST);
	rd32(wx, WX_VXSTATUS);

	/* we cannot reset while the RSTI / RSTD bits are asserted */
	while (!wx_check_for_rst_vf(wx) && timeout) {
		timeout--;
		udelay(5);
	}

	/* restore msix vectors */
	if (wx->b4_addr) {
		for (i = 0; i < 16; i++)
			writel(b4_buf[i], wx->b4_addr + i * 4);
	}

	/* amlite: bme */
	if (wx->mac.type == wx_mac_aml || wx->mac.type == wx_mac_aml40)
		wr32(wx, WX_VX_PF_BME, WX_VF_BME_ENABLE);

	if (!timeout)
		return -EBUSY;

	/* Reset VF registers to initial values */
	wx_virt_clr_reg(wx);

	/* mailbox timeout can now become active */
	mbx->timeout = 2000;

	ret = wx_mbx_write_and_read_reply(wx, msgbuf, msgbuf,
					  ARRAY_SIZE(msgbuf));
	if (ret)
		return ret;

	if (msgbuf[0] != (WX_VF_RESET | WX_VT_MSGTYPE_ACK) &&
	    msgbuf[0] != (WX_VF_RESET | WX_VT_MSGTYPE_NACK))
		return -EINVAL;

	if (msgbuf[0] == (WX_VF_RESET | WX_VT_MSGTYPE_ACK))
		ether_addr_copy(wx->mac.perm_addr, addr);

	wx->mac.mc_filter_type = msgbuf[3];

	return 0;
}
EXPORT_SYMBOL(wx_reset_hw_vf);

/**
 *  wx_stop_adapter_vf - Generic stop Tx/Rx units
 *  @wx: pointer to hardware structure
 *
 *  Clears interrupts, disables transmit and receive units.
 **/
void wx_stop_adapter_vf(struct wx *wx)
{
	u32 reg_val;
	u16 i;

	/* Clear interrupt mask to stop from interrupts being generated */
	wr32(wx, WX_VXIMS, WX_VF_IRQ_CLEAR_MASK);

	/* Clear any pending interrupts, flush previous writes */
	wr32(wx, WX_VXICR, U32_MAX);

	/* Disable the transmit unit.  Each queue must be disabled. */
	for (i = 0; i < wx->mac.max_tx_queues; i++)
		wr32(wx, WX_VXTXDCTL(i), WX_VXTXDCTL_FLUSH);

	/* Disable the receive unit by stopping each queue */
	for (i = 0; i < wx->mac.max_rx_queues; i++) {
		reg_val = rd32(wx, WX_VXRXDCTL(i));
		reg_val &= ~WX_VXRXDCTL_ENABLE;
		wr32(wx, WX_VXRXDCTL(i), reg_val);
	}
	/* Clear packet split and pool config */
	wr32(wx, WX_VXMRQC, 0);

	/* flush all queues disables */
	rd32(wx, WX_VXSTATUS);
}
EXPORT_SYMBOL(wx_stop_adapter_vf);

/**
 *  wx_set_rar_vf - set device MAC address
 *  @wx: pointer to hardware structure
 *  @index: Receive address register to write
 *  @addr: Address to put into receive address register
 *  @enable_addr: set flag that address is active
 *
 *  Return: returns 0 on success, negative error code on failure
 **/
int wx_set_rar_vf(struct wx *wx, u32 index, u8 *addr, u32 enable_addr)
{
	u32 msgbuf[3] = {WX_VF_SET_MAC_ADDR};
	u8 *msg_addr = (u8 *)(&msgbuf[1]);
	int ret;

	memcpy(msg_addr, addr, ETH_ALEN);

	ret = wx_mbx_write_and_read_reply(wx, msgbuf, msgbuf,
					  ARRAY_SIZE(msgbuf));
	if (ret)
		return ret;
	msgbuf[0] &= ~WX_VT_MSGTYPE_CTS;

	/* if nacked the address was rejected, use "perm_addr" */
	if (msgbuf[0] == (WX_VF_SET_MAC_ADDR | WX_VT_MSGTYPE_NACK)) {
		wx_get_mac_addr_vf(wx, wx->mac.addr);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(wx_set_rar_vf);

/**
 *  wx_update_mc_addr_list_vf - Update Multicast addresses
 *  @wx: pointer to the HW structure
 *  @netdev: pointer to the net device structure
 *
 *  Updates the Multicast Table Array.
 *
 *  Return: returns 0 on success, negative error code on failure
 **/
int wx_update_mc_addr_list_vf(struct wx *wx, struct net_device *netdev)
{
	u32 msgbuf[WX_VXMAILBOX_SIZE] = {WX_VF_SET_MULTICAST};
	u16 *vector_l = (u16 *)&msgbuf[1];
	struct netdev_hw_addr *ha;
	u32 cnt, i;

	cnt = netdev_mc_count(netdev);
	if (cnt > 28)
		cnt = 28;
	msgbuf[0] |= cnt << WX_VT_MSGINFO_SHIFT;

	i = 0;
	netdev_for_each_mc_addr(ha, netdev) {
		if (i == cnt)
			break;
		if (is_link_local_ether_addr(ha->addr))
			continue;

		vector_l[i++] = wx_mta_vector(wx, ha->addr);
	}

	return wx_write_posted_mbx(wx, msgbuf, ARRAY_SIZE(msgbuf));
}
EXPORT_SYMBOL(wx_update_mc_addr_list_vf);

/**
 *  wx_update_xcast_mode_vf - Update Multicast mode
 *  @wx: pointer to the HW structure
 *  @xcast_mode: new multicast mode
 *
 *  Updates the Multicast Mode of VF.
 *
 *  Return: returns 0 on success, negative error code on failure
 **/
int wx_update_xcast_mode_vf(struct wx *wx, int xcast_mode)
{
	u32 msgbuf[2] = {WX_VF_UPDATE_XCAST_MODE, xcast_mode};
	int ret = 0;

	if (wx->vfinfo->vf_api < wx_mbox_api_13)
		return -EINVAL;

	ret = wx_mbx_write_and_read_reply(wx, msgbuf, msgbuf,
					  ARRAY_SIZE(msgbuf));
	if (ret)
		return ret;

	msgbuf[0] &= ~WX_VT_MSGTYPE_CTS;
	if (msgbuf[0] == (WX_VF_UPDATE_XCAST_MODE | WX_VT_MSGTYPE_NACK))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(wx_update_xcast_mode_vf);

/**
 * wx_get_link_state_vf - Get VF link state from PF
 * @wx: pointer to the HW structure
 * @link_state: link state storage
 *
 * Return: return state of the operation error or success.
 **/
int wx_get_link_state_vf(struct wx *wx, u16 *link_state)
{
	u32 msgbuf[2] = {WX_VF_GET_LINK_STATE};
	int ret;

	ret = wx_mbx_write_and_read_reply(wx, msgbuf, msgbuf,
					  ARRAY_SIZE(msgbuf));
	if (ret)
		return ret;

	if (msgbuf[0] & WX_VT_MSGTYPE_NACK)
		return -EINVAL;

	*link_state = msgbuf[1];

	return 0;
}
EXPORT_SYMBOL(wx_get_link_state_vf);

/**
 *  wx_set_vfta_vf - Set/Unset vlan filter table address
 *  @wx: pointer to the HW structure
 *  @vlan: 12 bit VLAN ID
 *  @vind: unused by VF drivers
 *  @vlan_on: if true then set bit, else clear bit
 *  @vlvf_bypass: boolean flag indicating updating default pool is okay
 *
 *  Turn on/off specified VLAN in the VLAN filter table.
 *
 *  Return: returns 0 on success, negative error code on failure
 **/
int wx_set_vfta_vf(struct wx *wx, u32 vlan, u32 vind, bool vlan_on,
		   bool vlvf_bypass)
{
	u32 msgbuf[2] = {WX_VF_SET_VLAN, vlan};
	bool vlan_offload = false;
	int ret;

	/* Setting the 8 bit field MSG INFO to TRUE indicates "add" */
	msgbuf[0] |= vlan_on << WX_VT_MSGINFO_SHIFT;
	/* if vf vlan offload is disabled, allow to create vlan under pf port vlan */
	msgbuf[0] |= BIT(vlan_offload);

	ret = wx_mbx_write_and_read_reply(wx, msgbuf, msgbuf,
					  ARRAY_SIZE(msgbuf));
	if (ret)
		return ret;

	if (msgbuf[0] & WX_VT_MSGTYPE_ACK)
		return 0;

	return msgbuf[0] & WX_VT_MSGTYPE_NACK;
}
EXPORT_SYMBOL(wx_set_vfta_vf);

void wx_get_mac_addr_vf(struct wx *wx, u8 *mac_addr)
{
	ether_addr_copy(mac_addr, wx->mac.perm_addr);
}
EXPORT_SYMBOL(wx_get_mac_addr_vf);

int wx_get_fw_version_vf(struct wx *wx)
{
	u32 msgbuf[2] = {WX_VF_GET_FW_VERSION};
	int ret;

	ret = wx_mbx_write_and_read_reply(wx, msgbuf, msgbuf,
					  ARRAY_SIZE(msgbuf));
	if (ret)
		return ret;

	if (msgbuf[0] & WX_VT_MSGTYPE_NACK)
		return -EINVAL;
	snprintf(wx->eeprom_id, 32, "0x%08x", msgbuf[1]);

	return 0;
}
EXPORT_SYMBOL(wx_get_fw_version_vf);

int wx_set_uc_addr_vf(struct wx *wx, u32 index, u8 *addr)
{
	u32 msgbuf[3] = {WX_VF_SET_MACVLAN};
	u8 *msg_addr = (u8 *)(&msgbuf[1]);
	int ret;

	/* If index is one then this is the start of a new list and needs
	 * indication to the PF so it can do it's own list management.
	 * If it is zero then that tells the PF to just clear all of
	 * this VF's macvlans and there is no new list.
	 */
	msgbuf[0] |= index << WX_VT_MSGINFO_SHIFT;
	if (addr)
		memcpy(msg_addr, addr, 6);
	ret = wx_mbx_write_and_read_reply(wx, msgbuf, msgbuf,
					  ARRAY_SIZE(msgbuf));
	if (ret)
		return ret;

	msgbuf[0] &= ~WX_VT_MSGTYPE_CTS;

	if (msgbuf[0] == (WX_VF_SET_MACVLAN | WX_VT_MSGTYPE_NACK))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(wx_set_uc_addr_vf);

/**
 *  wx_rlpml_set_vf - Set the maximum receive packet length
 *  @wx: pointer to the HW structure
 *  @max_size: value to assign to max frame size
 *
 *  Return: returns 0 on success, negative error code on failure
 **/
int wx_rlpml_set_vf(struct wx *wx, u16 max_size)
{
	u32 msgbuf[2] = {WX_VF_SET_LPE, max_size};
	int ret;

	ret = wx_mbx_write_and_read_reply(wx, msgbuf, msgbuf,
					  ARRAY_SIZE(msgbuf));
	if (ret)
		return ret;
	if ((msgbuf[0] & WX_VF_SET_LPE) &&
	    (msgbuf[0] & WX_VT_MSGTYPE_NACK))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(wx_rlpml_set_vf);

/**
 *  wx_negotiate_api_version - Negotiate supported API version
 *  @wx: pointer to the HW structure
 *  @api: integer containing requested API version
 *
 *  Return: returns 0 on success, negative error code on failure
 **/
int wx_negotiate_api_version(struct wx *wx, int api)
{
	u32 msgbuf[2] = {WX_VF_API_NEGOTIATE, api};
	int ret;

	ret = wx_mbx_write_and_read_reply(wx, msgbuf, msgbuf,
					  ARRAY_SIZE(msgbuf));
	if (ret)
		return ret;

	msgbuf[0] &= ~WX_VT_MSGTYPE_CTS;

	/* Store value and return 0 on success */
	if (msgbuf[0] == (WX_VF_API_NEGOTIATE | WX_VT_MSGTYPE_NACK))
		return -EINVAL;
	wx->vfinfo->vf_api = api;

	return 0;
}
EXPORT_SYMBOL(wx_negotiate_api_version);

int wx_get_queues_vf(struct wx *wx, u32 *num_tcs, u32 *default_tc)
{
	u32 msgbuf[5] = {WX_VF_GET_QUEUES};
	int ret;

	/* do nothing if API doesn't support wx_get_queues */
	if (wx->vfinfo->vf_api < wx_mbox_api_13)
		return -EINVAL;

	/* Fetch queue configuration from the PF */
	ret = wx_mbx_write_and_read_reply(wx, msgbuf, msgbuf,
					  ARRAY_SIZE(msgbuf));
	if (ret)
		return ret;
	msgbuf[0] &= ~WX_VT_MSGTYPE_CTS;

	/* if we didn't get an ACK there must have been
	 * some sort of mailbox error so we should treat it
	 * as such
	 */
	if (msgbuf[0] != (WX_VF_GET_QUEUES | WX_VT_MSGTYPE_ACK))
		return -EINVAL;
	/* record and validate values from message */
	wx->mac.max_tx_queues = msgbuf[WX_VF_TX_QUEUES];
	if (wx->mac.max_tx_queues == 0 ||
	    wx->mac.max_tx_queues > WX_VF_MAX_TX_QUEUES)
		wx->mac.max_tx_queues = WX_VF_MAX_TX_QUEUES;

	wx->mac.max_rx_queues = msgbuf[WX_VF_RX_QUEUES];
	if (wx->mac.max_rx_queues == 0 ||
	    wx->mac.max_rx_queues > WX_VF_MAX_RX_QUEUES)
		wx->mac.max_rx_queues = WX_VF_MAX_RX_QUEUES;

	*num_tcs = msgbuf[WX_VF_TRANS_VLAN];
	/* in case of unknown state assume we cannot tag frames */
	if (*num_tcs > wx->mac.max_rx_queues)
		*num_tcs = 1;
	*default_tc = msgbuf[WX_VF_DEF_QUEUE];
	/* default to queue 0 on out-of-bounds queue number */
	if (*default_tc >= wx->mac.max_tx_queues)
		*default_tc = 0;

	return 0;
}
EXPORT_SYMBOL(wx_get_queues_vf);

static int wx_get_link_status_from_pf(struct wx *wx, u32 *msgbuf)
{
	u32 links_reg = msgbuf[1];

	if (msgbuf[1] & WX_PF_NOFITY_VF_NET_NOT_RUNNING)
		wx->notify_down = true;
	else
		wx->notify_down = false;

	if (wx->notify_down) {
		wx->link = false;
		wx->speed = SPEED_UNKNOWN;
		return 0;
	}

	wx->link = WX_PFLINK_STATUS(links_reg);
	wx->speed = WX_PFLINK_SPEED(links_reg);

	return 0;
}

static int wx_pf_ping_vf(struct wx *wx, u32 *msgbuf)
{
	if (!(msgbuf[0] & WX_VT_MSGTYPE_CTS))
		/* msg is not CTS, we need to do reset */
		return -EINVAL;

	return 0;
}

static struct wx_link_reg_fields wx_speed_lookup_vf[] = {
	{wx_mac_unknown},
	{wx_mac_sp, SPEED_10000, SPEED_1000, SPEED_100, SPEED_UNKNOWN, SPEED_UNKNOWN},
	{wx_mac_em, SPEED_1000,  SPEED_100, SPEED_10, SPEED_UNKNOWN, SPEED_UNKNOWN},
	{wx_mac_aml, SPEED_40000, SPEED_25000, SPEED_10000, SPEED_1000, SPEED_UNKNOWN},
	{wx_mac_aml40, SPEED_40000, SPEED_25000, SPEED_10000, SPEED_1000, SPEED_UNKNOWN},
};

static void wx_check_physical_link(struct wx *wx)
{
	u32 val, link_val;
	int ret;

	/* get link status from hw status reg
	 * for SFP+ modules and DA cables, it can take up to 500usecs
	 * before the link status is correct
	 */
	if (wx->mac.type == wx_mac_em)
		ret = read_poll_timeout_atomic(rd32, val, val & GENMASK(4, 1),
					       100, 500, false, wx, WX_VXSTATUS);
	else
		ret = read_poll_timeout_atomic(rd32, val, val & BIT(0), 100,
					       500, false, wx, WX_VXSTATUS);
	if (ret) {
		wx->speed = SPEED_UNKNOWN;
		wx->link = false;
		return;
	}

	wx->link = true;
	link_val = WX_VXSTATUS_SPEED(val);

	if (link_val & BIT(0))
		wx->speed = wx_speed_lookup_vf[wx->mac.type].bit0_f;
	else if (link_val & BIT(1))
		wx->speed = wx_speed_lookup_vf[wx->mac.type].bit1_f;
	else if (link_val & BIT(2))
		wx->speed = wx_speed_lookup_vf[wx->mac.type].bit2_f;
	else if (link_val & BIT(3))
		wx->speed = wx_speed_lookup_vf[wx->mac.type].bit3_f;
	else
		wx->speed = SPEED_UNKNOWN;
}

int wx_check_mac_link_vf(struct wx *wx)
{
	struct wx_mbx_info *mbx = &wx->mbx;
	u32 msgbuf[2] = {0};
	int ret = 0;

	if (!mbx->timeout)
		goto out;

	wx_check_for_rst_vf(wx);
	if (!wx_check_for_msg_vf(wx))
		ret = wx_read_mbx_vf(wx, msgbuf, 2);
	if (ret)
		goto out;

	switch (msgbuf[0] & GENMASK(8, 0)) {
	case WX_PF_NOFITY_VF_LINK_STATUS | WX_PF_CONTROL_MSG:
		ret = wx_get_link_status_from_pf(wx, msgbuf);
		goto out;
	case WX_PF_CONTROL_MSG:
		ret = wx_pf_ping_vf(wx, msgbuf);
		goto out;
	case 0:
		if (msgbuf[0] & WX_VT_MSGTYPE_NACK) {
			/* msg is NACK, we must have lost CTS status */
			ret = -EBUSY;
			goto out;
		}
		/* no message, check link status */
		wx_check_physical_link(wx);
		goto out;
	default:
		break;
	}

	if (!(msgbuf[0] & WX_VT_MSGTYPE_CTS)) {
		/* msg is not CTS and is NACK we must have lost CTS status */
		if (msgbuf[0] & WX_VT_MSGTYPE_NACK)
			ret = -EBUSY;
		goto out;
	}

	/* the pf is talking, if we timed out in the past we reinit */
	if (!mbx->timeout) {
		ret = -EBUSY;
		goto out;
	}

out:
	return ret;
}
