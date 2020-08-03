// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 */

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/platform_device.h>

#include "hns_dsaf_main.h"
#include "hns_dsaf_misc.h"
#include "hns_dsaf_rcb.h"

#define MAC_EN_FLAG_V		0xada0328

static const u16 mac_phy_to_speed[] = {
	[PHY_INTERFACE_MODE_MII] = MAC_SPEED_100,
	[PHY_INTERFACE_MODE_GMII] = MAC_SPEED_1000,
	[PHY_INTERFACE_MODE_SGMII] = MAC_SPEED_1000,
	[PHY_INTERFACE_MODE_TBI] = MAC_SPEED_1000,
	[PHY_INTERFACE_MODE_RMII] = MAC_SPEED_100,
	[PHY_INTERFACE_MODE_RGMII] = MAC_SPEED_1000,
	[PHY_INTERFACE_MODE_RGMII_ID] = MAC_SPEED_1000,
	[PHY_INTERFACE_MODE_RGMII_RXID]	= MAC_SPEED_1000,
	[PHY_INTERFACE_MODE_RGMII_TXID]	= MAC_SPEED_1000,
	[PHY_INTERFACE_MODE_RTBI] = MAC_SPEED_1000,
	[PHY_INTERFACE_MODE_XGMII] = MAC_SPEED_10000
};

static const enum mac_mode g_mac_mode_100[] = {
	[PHY_INTERFACE_MODE_MII]	= MAC_MODE_MII_100,
	[PHY_INTERFACE_MODE_RMII]   = MAC_MODE_RMII_100
};

static const enum mac_mode g_mac_mode_1000[] = {
	[PHY_INTERFACE_MODE_GMII]   = MAC_MODE_GMII_1000,
	[PHY_INTERFACE_MODE_SGMII]  = MAC_MODE_SGMII_1000,
	[PHY_INTERFACE_MODE_TBI]	= MAC_MODE_TBI_1000,
	[PHY_INTERFACE_MODE_RGMII]  = MAC_MODE_RGMII_1000,
	[PHY_INTERFACE_MODE_RGMII_ID]   = MAC_MODE_RGMII_1000,
	[PHY_INTERFACE_MODE_RGMII_RXID] = MAC_MODE_RGMII_1000,
	[PHY_INTERFACE_MODE_RGMII_TXID] = MAC_MODE_RGMII_1000,
	[PHY_INTERFACE_MODE_RTBI]   = MAC_MODE_RTBI_1000
};

static enum mac_mode hns_get_enet_interface(const struct hns_mac_cb *mac_cb)
{
	switch (mac_cb->max_speed) {
	case MAC_SPEED_100:
		return g_mac_mode_100[mac_cb->phy_if];
	case MAC_SPEED_1000:
		return g_mac_mode_1000[mac_cb->phy_if];
	case MAC_SPEED_10000:
		return MAC_MODE_XGMII_10000;
	default:
		return MAC_MODE_MII_100;
	}
}

void hns_mac_get_link_status(struct hns_mac_cb *mac_cb, u32 *link_status)
{
	struct mac_driver *mac_ctrl_drv;
	int ret, sfp_prsnt;

	mac_ctrl_drv = hns_mac_get_drv(mac_cb);

	if (mac_ctrl_drv->get_link_status)
		mac_ctrl_drv->get_link_status(mac_ctrl_drv, link_status);
	else
		*link_status = 0;

	if (mac_cb->media_type == HNAE_MEDIA_TYPE_FIBER) {
		ret = mac_cb->dsaf_dev->misc_op->get_sfp_prsnt(mac_cb,
							       &sfp_prsnt);
		if (!ret)
			*link_status = *link_status && sfp_prsnt;
	}

	mac_cb->link = *link_status;
}

int hns_mac_get_port_info(struct hns_mac_cb *mac_cb,
			  u8 *auto_neg, u16 *speed, u8 *duplex)
{
	struct mac_driver *mac_ctrl_drv;
	struct mac_info    info;

	mac_ctrl_drv = hns_mac_get_drv(mac_cb);

	if (!mac_ctrl_drv->get_info)
		return -ENODEV;

	mac_ctrl_drv->get_info(mac_ctrl_drv, &info);
	if (auto_neg)
		*auto_neg = info.auto_neg;
	if (speed)
		*speed = info.speed;
	if (duplex)
		*duplex = info.duplex;

	return 0;
}

/**
 *hns_mac_is_adjust_link - check is need change mac speed and duplex register
 *@mac_cb: mac device
 *@speed: phy device speed
 *@duplex:phy device duplex
 *
 */
bool hns_mac_need_adjust_link(struct hns_mac_cb *mac_cb, int speed, int duplex)
{
	struct mac_driver *mac_ctrl_drv;

	mac_ctrl_drv = (struct mac_driver *)(mac_cb->priv.mac);

	if (mac_ctrl_drv->need_adjust_link)
		return mac_ctrl_drv->need_adjust_link(mac_ctrl_drv,
			(enum mac_speed)speed, duplex);
	else
		return true;
}

void hns_mac_adjust_link(struct hns_mac_cb *mac_cb, int speed, int duplex)
{
	int ret;
	struct mac_driver *mac_ctrl_drv;

	mac_ctrl_drv = (struct mac_driver *)(mac_cb->priv.mac);

	mac_cb->speed = speed;
	mac_cb->half_duplex = !duplex;

	if (mac_ctrl_drv->adjust_link) {
		ret = mac_ctrl_drv->adjust_link(mac_ctrl_drv,
			(enum mac_speed)speed, duplex);
		if (ret) {
			dev_err(mac_cb->dev,
				"adjust_link failed, %s mac%d ret = %#x!\n",
				mac_cb->dsaf_dev->ae_dev.name,
				mac_cb->mac_id, ret);
			return;
		}
	}
}

/**
 *hns_mac_get_inner_port_num - get mac table inner port number
 *@mac_cb: mac device
 *@vmid: vm id
 *@port_num:port number
 *
 */
int hns_mac_get_inner_port_num(struct hns_mac_cb *mac_cb, u8 vmid, u8 *port_num)
{
	int q_num_per_vf, vf_num_per_port;
	int vm_queue_id;
	u8 tmp_port;

	if (mac_cb->dsaf_dev->dsaf_mode <= DSAF_MODE_ENABLE) {
		if (mac_cb->mac_id != DSAF_MAX_PORT_NUM) {
			dev_err(mac_cb->dev,
				"input invalid, %s mac%d vmid%d !\n",
				mac_cb->dsaf_dev->ae_dev.name,
				mac_cb->mac_id, vmid);
			return -EINVAL;
		}
	} else if (mac_cb->dsaf_dev->dsaf_mode < DSAF_MODE_MAX) {
		if (mac_cb->mac_id >= DSAF_MAX_PORT_NUM) {
			dev_err(mac_cb->dev,
				"input invalid, %s mac%d vmid%d!\n",
				mac_cb->dsaf_dev->ae_dev.name,
				mac_cb->mac_id, vmid);
			return -EINVAL;
		}
	} else {
		dev_err(mac_cb->dev, "dsaf mode invalid, %s mac%d!\n",
			mac_cb->dsaf_dev->ae_dev.name, mac_cb->mac_id);
		return -EINVAL;
	}

	if (vmid >= mac_cb->dsaf_dev->rcb_common[0]->max_vfn) {
		dev_err(mac_cb->dev, "input invalid, %s mac%d vmid%d !\n",
			mac_cb->dsaf_dev->ae_dev.name, mac_cb->mac_id, vmid);
		return -EINVAL;
	}

	q_num_per_vf = mac_cb->dsaf_dev->rcb_common[0]->max_q_per_vf;
	vf_num_per_port = mac_cb->dsaf_dev->rcb_common[0]->max_vfn;

	vm_queue_id = vmid * q_num_per_vf +
			vf_num_per_port * q_num_per_vf * mac_cb->mac_id;

	switch (mac_cb->dsaf_dev->dsaf_mode) {
	case DSAF_MODE_ENABLE_FIX:
		tmp_port = 0;
		break;
	case DSAF_MODE_DISABLE_FIX:
		tmp_port = 0;
		break;
	case DSAF_MODE_ENABLE_0VM:
	case DSAF_MODE_ENABLE_8VM:
	case DSAF_MODE_ENABLE_16VM:
	case DSAF_MODE_ENABLE_32VM:
	case DSAF_MODE_ENABLE_128VM:
	case DSAF_MODE_DISABLE_2PORT_8VM:
	case DSAF_MODE_DISABLE_2PORT_16VM:
	case DSAF_MODE_DISABLE_2PORT_64VM:
	case DSAF_MODE_DISABLE_6PORT_0VM:
	case DSAF_MODE_DISABLE_6PORT_2VM:
	case DSAF_MODE_DISABLE_6PORT_4VM:
	case DSAF_MODE_DISABLE_6PORT_16VM:
		tmp_port = vm_queue_id;
		break;
	default:
		dev_err(mac_cb->dev, "dsaf mode invalid, %s mac%d!\n",
			mac_cb->dsaf_dev->ae_dev.name, mac_cb->mac_id);
		return -EINVAL;
	}
	tmp_port += DSAF_BASE_INNER_PORT_NUM;

	*port_num = tmp_port;

	return 0;
}

/**
 *hns_mac_change_vf_addr - change vf mac address
 *@mac_cb: mac device
 *@vmid: vmid
 *@addr:mac address
 */
int hns_mac_change_vf_addr(struct hns_mac_cb *mac_cb,
			   u32 vmid, char *addr)
{
	int ret;
	struct mac_driver *mac_ctrl_drv = hns_mac_get_drv(mac_cb);
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;
	struct dsaf_drv_mac_single_dest_entry mac_entry;
	struct mac_entry_idx *old_entry;

	old_entry = &mac_cb->addr_entry_idx[vmid];
	if (!HNS_DSAF_IS_DEBUG(dsaf_dev)) {
		memcpy(mac_entry.addr, addr, sizeof(mac_entry.addr));
		mac_entry.in_vlan_id = old_entry->vlan_id;
		mac_entry.in_port_num = mac_cb->mac_id;
		ret = hns_mac_get_inner_port_num(mac_cb, (u8)vmid,
						 &mac_entry.port_num);
		if (ret)
			return ret;

		if ((old_entry->valid != 0) &&
		    (memcmp(old_entry->addr,
		    addr, sizeof(mac_entry.addr)) != 0)) {
			ret = hns_dsaf_del_mac_entry(dsaf_dev,
						     old_entry->vlan_id,
						     mac_cb->mac_id,
						     old_entry->addr);
			if (ret)
				return ret;
		}

		ret = hns_dsaf_set_mac_uc_entry(dsaf_dev, &mac_entry);
		if (ret)
			return ret;
	}

	if ((mac_ctrl_drv->set_mac_addr) && (vmid == 0))
		mac_ctrl_drv->set_mac_addr(mac_cb->priv.mac, addr);

	memcpy(old_entry->addr, addr, sizeof(old_entry->addr));
	old_entry->valid = 1;
	return 0;
}

int hns_mac_add_uc_addr(struct hns_mac_cb *mac_cb, u8 vf_id,
			const unsigned char *addr)
{
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;
	struct dsaf_drv_mac_single_dest_entry mac_entry;
	int ret;

	if (HNS_DSAF_IS_DEBUG(dsaf_dev))
		return -ENOSPC;

	memset(&mac_entry, 0, sizeof(mac_entry));
	memcpy(mac_entry.addr, addr, sizeof(mac_entry.addr));
	mac_entry.in_port_num = mac_cb->mac_id;
	ret = hns_mac_get_inner_port_num(mac_cb, vf_id, &mac_entry.port_num);
	if (ret)
		return ret;

	return hns_dsaf_set_mac_uc_entry(dsaf_dev, &mac_entry);
}

int hns_mac_rm_uc_addr(struct hns_mac_cb *mac_cb, u8 vf_id,
		       const unsigned char *addr)
{
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;
	struct dsaf_drv_mac_single_dest_entry mac_entry;
	int ret;

	if (HNS_DSAF_IS_DEBUG(dsaf_dev))
		return -ENOSPC;

	memset(&mac_entry, 0, sizeof(mac_entry));
	memcpy(mac_entry.addr, addr, sizeof(mac_entry.addr));
	mac_entry.in_port_num = mac_cb->mac_id;
	ret = hns_mac_get_inner_port_num(mac_cb, vf_id, &mac_entry.port_num);
	if (ret)
		return ret;

	return hns_dsaf_rm_mac_addr(dsaf_dev, &mac_entry);
}

int hns_mac_set_multi(struct hns_mac_cb *mac_cb,
		      u32 port_num, char *addr, bool enable)
{
	int ret;
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;
	struct dsaf_drv_mac_single_dest_entry mac_entry;

	if (!HNS_DSAF_IS_DEBUG(dsaf_dev) && addr) {
		memcpy(mac_entry.addr, addr, sizeof(mac_entry.addr));
		mac_entry.in_vlan_id = 0;/*vlan_id;*/
		mac_entry.in_port_num = mac_cb->mac_id;
		mac_entry.port_num = port_num;

		if (!enable)
			ret = hns_dsaf_del_mac_mc_port(dsaf_dev, &mac_entry);
		else
			ret = hns_dsaf_add_mac_mc_port(dsaf_dev, &mac_entry);
		if (ret) {
			dev_err(dsaf_dev->dev,
				"set mac mc port failed, %s mac%d ret = %#x!\n",
				mac_cb->dsaf_dev->ae_dev.name,
				mac_cb->mac_id, ret);
			return ret;
		}
	}

	return 0;
}

int hns_mac_clr_multicast(struct hns_mac_cb *mac_cb, int vfn)
{
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;
	u8 port_num;
	int ret = hns_mac_get_inner_port_num(mac_cb, vfn, &port_num);

	if (ret)
		return ret;

	return hns_dsaf_clr_mac_mc_port(dsaf_dev, mac_cb->mac_id, port_num);
}

static void hns_mac_param_get(struct mac_params *param,
			      struct hns_mac_cb *mac_cb)
{
	param->vaddr = mac_cb->vaddr;
	param->mac_mode = hns_get_enet_interface(mac_cb);
	ether_addr_copy(param->addr, mac_cb->addr_entry_idx[0].addr);
	param->mac_id = mac_cb->mac_id;
	param->dev = mac_cb->dev;
}

/**
 *hns_mac_queue_config_bc_en - set broadcast rx&tx enable
 *@mac_cb: mac device
 *@queue: queue number
 *@en:enable
 *retuen 0 - success , negative --fail
 */
static int hns_mac_port_config_bc_en(struct hns_mac_cb *mac_cb,
				     u32 port_num, u16 vlan_id, bool enable)
{
	int ret;
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;
	struct dsaf_drv_mac_single_dest_entry mac_entry;

	/* directy return ok in debug network mode */
	if (mac_cb->mac_type == HNAE_PORT_DEBUG)
		return 0;

	if (!HNS_DSAF_IS_DEBUG(dsaf_dev)) {
		eth_broadcast_addr(mac_entry.addr);
		mac_entry.in_vlan_id = vlan_id;
		mac_entry.in_port_num = mac_cb->mac_id;
		mac_entry.port_num = port_num;

		if (!enable)
			ret = hns_dsaf_del_mac_mc_port(dsaf_dev, &mac_entry);
		else
			ret = hns_dsaf_add_mac_mc_port(dsaf_dev, &mac_entry);
		return ret;
	}

	return 0;
}

/**
 *hns_mac_vm_config_bc_en - set broadcast rx&tx enable
 *@mac_cb: mac device
 *@vmid: vm id
 *@en:enable
 *retuen 0 - success , negative --fail
 */
int hns_mac_vm_config_bc_en(struct hns_mac_cb *mac_cb, u32 vmid, bool enable)
{
	int ret;
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;
	u8 port_num;
	struct mac_entry_idx *uc_mac_entry;
	struct dsaf_drv_mac_single_dest_entry mac_entry;

	if (mac_cb->mac_type == HNAE_PORT_DEBUG)
		return 0;

	uc_mac_entry = &mac_cb->addr_entry_idx[vmid];

	if (!HNS_DSAF_IS_DEBUG(dsaf_dev))  {
		eth_broadcast_addr(mac_entry.addr);
		mac_entry.in_vlan_id = uc_mac_entry->vlan_id;
		mac_entry.in_port_num = mac_cb->mac_id;
		ret = hns_mac_get_inner_port_num(mac_cb, vmid, &port_num);
		if (ret)
			return ret;
		mac_entry.port_num = port_num;

		if (!enable)
			ret = hns_dsaf_del_mac_mc_port(dsaf_dev, &mac_entry);
		else
			ret = hns_dsaf_add_mac_mc_port(dsaf_dev, &mac_entry);
		return ret;
	}

	return 0;
}

int hns_mac_wait_fifo_clean(struct hns_mac_cb *mac_cb)
{
	struct mac_driver *drv = hns_mac_get_drv(mac_cb);

	if (drv->wait_fifo_clean)
		return drv->wait_fifo_clean(drv);

	return 0;
}

void hns_mac_reset(struct hns_mac_cb *mac_cb)
{
	struct mac_driver *drv = hns_mac_get_drv(mac_cb);
	bool is_ver1 = AE_IS_VER1(mac_cb->dsaf_dev->dsaf_ver);

	drv->mac_init(drv);

	if (drv->config_max_frame_length)
		drv->config_max_frame_length(drv, mac_cb->max_frm);

	if (drv->set_tx_auto_pause_frames)
		drv->set_tx_auto_pause_frames(drv, mac_cb->tx_pause_frm_time);

	if (drv->set_an_mode)
		drv->set_an_mode(drv, 1);

	if (drv->mac_pausefrm_cfg) {
		if (mac_cb->mac_type == HNAE_PORT_DEBUG)
			drv->mac_pausefrm_cfg(drv, !is_ver1, !is_ver1);
		else /* mac rx must disable, dsaf pfc close instead of it*/
			drv->mac_pausefrm_cfg(drv, 0, 1);
	}
}

int hns_mac_set_mtu(struct hns_mac_cb *mac_cb, u32 new_mtu, u32 buf_size)
{
	struct mac_driver *drv = hns_mac_get_drv(mac_cb);
	u32 new_frm = new_mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;

	if (new_frm > HNS_RCB_RING_MAX_BD_PER_PKT * buf_size)
		return -EINVAL;

	if (!drv->config_max_frame_length)
		return -ECHILD;

	/* adjust max frame to be at least the size of a standard frame */
	if (new_frm < (ETH_FRAME_LEN + ETH_FCS_LEN + VLAN_HLEN))
		new_frm = (ETH_FRAME_LEN + ETH_FCS_LEN + VLAN_HLEN);

	drv->config_max_frame_length(drv, new_frm);

	mac_cb->max_frm = new_frm;

	return 0;
}

void hns_mac_start(struct hns_mac_cb *mac_cb)
{
	struct mac_driver *mac_drv = hns_mac_get_drv(mac_cb);

	/* for virt */
	if (mac_drv->mac_en_flg == MAC_EN_FLAG_V) {
		/*plus 1 when the virtual mac has been enabled */
		mac_drv->virt_dev_num += 1;
		return;
	}

	if (mac_drv->mac_enable) {
		mac_drv->mac_enable(mac_cb->priv.mac, MAC_COMM_MODE_RX_AND_TX);
		mac_drv->mac_en_flg = MAC_EN_FLAG_V;
	}
}

void hns_mac_stop(struct hns_mac_cb *mac_cb)
{
	struct mac_driver *mac_ctrl_drv = hns_mac_get_drv(mac_cb);

	/*modified for virtualization */
	if (mac_ctrl_drv->virt_dev_num > 0) {
		mac_ctrl_drv->virt_dev_num -= 1;
		if (mac_ctrl_drv->virt_dev_num > 0)
			return;
	}

	if (mac_ctrl_drv->mac_disable)
		mac_ctrl_drv->mac_disable(mac_cb->priv.mac,
			MAC_COMM_MODE_RX_AND_TX);

	mac_ctrl_drv->mac_en_flg = 0;
	mac_cb->link = 0;
	mac_cb->dsaf_dev->misc_op->cpld_reset_led(mac_cb);
}

/**
 * hns_mac_get_autoneg - get auto autonegotiation
 * @mac_cb: mac control block
 * @enable: enable or not
 * retuen 0 - success , negative --fail
 */
void hns_mac_get_autoneg(struct hns_mac_cb *mac_cb, u32 *auto_neg)
{
	struct mac_driver *mac_ctrl_drv = hns_mac_get_drv(mac_cb);

	if (mac_ctrl_drv->autoneg_stat)
		mac_ctrl_drv->autoneg_stat(mac_ctrl_drv, auto_neg);
	else
		*auto_neg = 0;
}

/**
 * hns_mac_get_pauseparam - set rx & tx pause parameter
 * @mac_cb: mac control block
 * @rx_en: rx enable status
 * @tx_en: tx enable status
 * retuen 0 - success , negative --fail
 */
void hns_mac_get_pauseparam(struct hns_mac_cb *mac_cb, u32 *rx_en, u32 *tx_en)
{
	struct mac_driver *mac_ctrl_drv = hns_mac_get_drv(mac_cb);

	if (mac_ctrl_drv->get_pause_enable) {
		mac_ctrl_drv->get_pause_enable(mac_ctrl_drv, rx_en, tx_en);
	} else {
		*rx_en = 0;
		*tx_en = 0;
	}
}

/**
 * hns_mac_set_autoneg - set auto autonegotiation
 * @mac_cb: mac control block
 * @enable: enable or not
 * retuen 0 - success , negative --fail
 */
int hns_mac_set_autoneg(struct hns_mac_cb *mac_cb, u8 enable)
{
	struct mac_driver *mac_ctrl_drv = hns_mac_get_drv(mac_cb);

	if (mac_cb->phy_if == PHY_INTERFACE_MODE_XGMII && enable) {
		dev_err(mac_cb->dev, "enabling autoneg is not allowed!\n");
		return -ENOTSUPP;
	}

	if (mac_ctrl_drv->set_an_mode)
		mac_ctrl_drv->set_an_mode(mac_ctrl_drv, enable);

	return 0;
}

/**
 * hns_mac_set_autoneg - set rx & tx pause parameter
 * @mac_cb: mac control block
 * @rx_en: rx enable or not
 * @tx_en: tx enable or not
 * return 0 - success , negative --fail
 */
int hns_mac_set_pauseparam(struct hns_mac_cb *mac_cb, u32 rx_en, u32 tx_en)
{
	struct mac_driver *mac_ctrl_drv = hns_mac_get_drv(mac_cb);
	bool is_ver1 = AE_IS_VER1(mac_cb->dsaf_dev->dsaf_ver);

	if (mac_cb->mac_type == HNAE_PORT_DEBUG) {
		if (is_ver1 && (tx_en || rx_en)) {
			dev_err(mac_cb->dev, "macv1 can't enable tx/rx_pause!\n");
			return -EINVAL;
		}
	}

	if (mac_ctrl_drv->mac_pausefrm_cfg)
		mac_ctrl_drv->mac_pausefrm_cfg(mac_ctrl_drv, rx_en, tx_en);

	return 0;
}

/**
 * hns_mac_init_ex - mac init
 * @mac_cb: mac control block
 * retuen 0 - success , negative --fail
 */
static int hns_mac_init_ex(struct hns_mac_cb *mac_cb)
{
	int ret;
	struct mac_params param;
	struct mac_driver *drv;

	hns_dsaf_fix_mac_mode(mac_cb);

	memset(&param, 0, sizeof(struct mac_params));
	hns_mac_param_get(&param, mac_cb);

	if (MAC_SPEED_FROM_MODE(param.mac_mode) < MAC_SPEED_10000)
		drv = (struct mac_driver *)hns_gmac_config(mac_cb, &param);
	else
		drv = (struct mac_driver *)hns_xgmac_config(mac_cb, &param);

	if (!drv)
		return -ENOMEM;

	mac_cb->priv.mac = (void *)drv;
	hns_mac_reset(mac_cb);

	hns_mac_adjust_link(mac_cb, mac_cb->speed, !mac_cb->half_duplex);

	ret = hns_mac_port_config_bc_en(mac_cb, mac_cb->mac_id, 0, true);
	if (ret)
		goto free_mac_drv;

	return 0;

free_mac_drv:
	drv->mac_free(mac_cb->priv.mac);
	mac_cb->priv.mac = NULL;

	return ret;
}

static int
hns_mac_phy_parse_addr(struct device *dev, struct fwnode_handle *fwnode)
{
	u32 addr;
	int ret;

	ret = fwnode_property_read_u32(fwnode, "phy-addr", &addr);
	if (ret) {
		dev_err(dev, "has invalid PHY address ret:%d\n", ret);
		return ret;
	}

	if (addr >= PHY_MAX_ADDR) {
		dev_err(dev, "PHY address %i is too large\n", addr);
		return -EINVAL;
	}

	return addr;
}

static int
hns_mac_register_phydev(struct mii_bus *mdio, struct hns_mac_cb *mac_cb,
			u32 addr)
{
	struct phy_device *phy;
	const char *phy_type;
	bool is_c45;
	int rc;

	rc = fwnode_property_read_string(mac_cb->fw_port,
					 "phy-mode", &phy_type);
	if (rc < 0)
		return rc;

	if (!strcmp(phy_type, phy_modes(PHY_INTERFACE_MODE_XGMII)))
		is_c45 = true;
	else if (!strcmp(phy_type, phy_modes(PHY_INTERFACE_MODE_SGMII)))
		is_c45 = false;
	else
		return -ENODATA;

	phy = get_phy_device(mdio, addr, is_c45);
	if (!phy || IS_ERR(phy))
		return -EIO;

	phy->irq = mdio->irq[addr];

	/* All data is now stored in the phy struct;
	 * register it
	 */
	rc = phy_device_register(phy);
	if (rc) {
		phy_device_free(phy);
		dev_err(&mdio->dev, "registered phy fail at address %i\n",
			addr);
		return -ENODEV;
	}

	mac_cb->phy_dev = phy;

	dev_dbg(&mdio->dev, "registered phy at address %i\n", addr);

	return 0;
}

static int hns_mac_register_phy(struct hns_mac_cb *mac_cb)
{
	struct fwnode_reference_args args;
	struct platform_device *pdev;
	struct mii_bus *mii_bus;
	int rc;
	int addr;

	/* Loop over the child nodes and register a phy_device for each one */
	if (!to_acpi_device_node(mac_cb->fw_port))
		return -ENODEV;

	rc = acpi_node_get_property_reference(
			mac_cb->fw_port, "mdio-node", 0, &args);
	if (rc)
		return rc;
	if (!is_acpi_device_node(args.fwnode))
		return -EINVAL;

	addr = hns_mac_phy_parse_addr(mac_cb->dev, mac_cb->fw_port);
	if (addr < 0)
		return addr;

	/* dev address in adev */
	pdev = hns_dsaf_find_platform_device(args.fwnode);
	if (!pdev) {
		dev_err(mac_cb->dev, "mac%d mdio pdev is NULL\n",
			mac_cb->mac_id);
		return  -EINVAL;
	}

	mii_bus = platform_get_drvdata(pdev);
	if (!mii_bus) {
		dev_err(mac_cb->dev,
			"mac%d mdio is NULL, dsaf will probe again later\n",
			mac_cb->mac_id);
		return -EPROBE_DEFER;
	}

	rc = hns_mac_register_phydev(mii_bus, mac_cb, addr);
	if (!rc)
		dev_dbg(mac_cb->dev, "mac%d register phy addr:%d\n",
			mac_cb->mac_id, addr);

	return rc;
}

static void hns_mac_remove_phydev(struct hns_mac_cb *mac_cb)
{
	if (!to_acpi_device_node(mac_cb->fw_port) || !mac_cb->phy_dev)
		return;

	phy_device_remove(mac_cb->phy_dev);
	phy_device_free(mac_cb->phy_dev);

	mac_cb->phy_dev = NULL;
}

#define MAC_MEDIA_TYPE_MAX_LEN		16

static const struct {
	enum hnae_media_type value;
	const char *name;
} media_type_defs[] = {
	{HNAE_MEDIA_TYPE_UNKNOWN,	"unknown" },
	{HNAE_MEDIA_TYPE_FIBER,		"fiber" },
	{HNAE_MEDIA_TYPE_COPPER,	"copper" },
	{HNAE_MEDIA_TYPE_BACKPLANE,	"backplane" },
};

/**
 *hns_mac_get_info  - get mac information from device node
 *@mac_cb: mac device
 *@np:device node
 * return: 0 --success, negative --fail
 */
static int hns_mac_get_info(struct hns_mac_cb *mac_cb)
{
	struct device_node *np;
	struct regmap *syscon;
	struct of_phandle_args cpld_args;
	const char *media_type;
	u32 i;
	u32 ret;

	mac_cb->link = false;
	mac_cb->half_duplex = false;
	mac_cb->media_type = HNAE_MEDIA_TYPE_UNKNOWN;
	mac_cb->speed = mac_phy_to_speed[mac_cb->phy_if];
	mac_cb->max_speed = mac_cb->speed;

	if (mac_cb->phy_if == PHY_INTERFACE_MODE_SGMII) {
		mac_cb->if_support = MAC_GMAC_SUPPORTED;
		mac_cb->if_support |= SUPPORTED_1000baseT_Full;
	} else if (mac_cb->phy_if == PHY_INTERFACE_MODE_XGMII) {
		mac_cb->if_support = SUPPORTED_10000baseR_FEC;
		mac_cb->if_support |= SUPPORTED_10000baseKR_Full;
	}

	mac_cb->max_frm = MAC_DEFAULT_MTU;
	mac_cb->tx_pause_frm_time = MAC_DEFAULT_PAUSE_TIME;
	mac_cb->port_rst_off = mac_cb->mac_id;
	mac_cb->port_mode_off = 0;

	/* if the dsaf node doesn't contain a port subnode, get phy-handle
	 * from dsaf node
	 */
	if (!mac_cb->fw_port) {
		np = of_parse_phandle(mac_cb->dev->of_node, "phy-handle",
				      mac_cb->mac_id);
		mac_cb->phy_dev = of_phy_find_device(np);
		if (mac_cb->phy_dev) {
			/* refcount is held by of_phy_find_device()
			 * if the phy_dev is found
			 */
			put_device(&mac_cb->phy_dev->mdio.dev);

			dev_dbg(mac_cb->dev, "mac%d phy_node: %pOFn\n",
				mac_cb->mac_id, np);
		}
		of_node_put(np);

		return 0;
	}

	if (is_of_node(mac_cb->fw_port)) {
		/* parse property from port subnode in dsaf */
		np = of_parse_phandle(to_of_node(mac_cb->fw_port),
				      "phy-handle", 0);
		mac_cb->phy_dev = of_phy_find_device(np);
		if (mac_cb->phy_dev) {
			/* refcount is held by of_phy_find_device()
			 * if the phy_dev is found
			 */
			put_device(&mac_cb->phy_dev->mdio.dev);
			dev_dbg(mac_cb->dev, "mac%d phy_node: %pOFn\n",
				mac_cb->mac_id, np);
		}
		of_node_put(np);

		np = of_parse_phandle(to_of_node(mac_cb->fw_port),
				      "serdes-syscon", 0);
		syscon = syscon_node_to_regmap(np);
		of_node_put(np);
		if (IS_ERR_OR_NULL(syscon)) {
			dev_err(mac_cb->dev, "serdes-syscon is needed!\n");
			return -EINVAL;
		}
		mac_cb->serdes_ctrl = syscon;

		ret = fwnode_property_read_u32(mac_cb->fw_port,
					       "port-rst-offset",
					       &mac_cb->port_rst_off);
		if (ret) {
			dev_dbg(mac_cb->dev,
				"mac%d port-rst-offset not found, use default value.\n",
				mac_cb->mac_id);
		}

		ret = fwnode_property_read_u32(mac_cb->fw_port,
					       "port-mode-offset",
					       &mac_cb->port_mode_off);
		if (ret) {
			dev_dbg(mac_cb->dev,
				"mac%d port-mode-offset not found, use default value.\n",
				mac_cb->mac_id);
		}

		ret = of_parse_phandle_with_fixed_args(
			to_of_node(mac_cb->fw_port), "cpld-syscon", 1, 0,
			&cpld_args);
		if (ret) {
			dev_dbg(mac_cb->dev, "mac%d no cpld-syscon found.\n",
				mac_cb->mac_id);
			mac_cb->cpld_ctrl = NULL;
		} else {
			syscon = syscon_node_to_regmap(cpld_args.np);
			if (IS_ERR_OR_NULL(syscon)) {
				dev_dbg(mac_cb->dev, "no cpld-syscon found!\n");
				mac_cb->cpld_ctrl = NULL;
			} else {
				mac_cb->cpld_ctrl = syscon;
				mac_cb->cpld_ctrl_reg = cpld_args.args[0];
			}
		}
	} else if (is_acpi_node(mac_cb->fw_port)) {
		ret = hns_mac_register_phy(mac_cb);
		/*
		 * Mac can work well if there is phy or not.If the port don't
		 * connect with phy, the return value will be ignored. Only
		 * when there is phy but can't find mdio bus, the return value
		 * will be handled.
		 */
		if (ret == -EPROBE_DEFER)
			return ret;
	} else {
		dev_err(mac_cb->dev, "mac%d cannot find phy node\n",
			mac_cb->mac_id);
	}

	if (!fwnode_property_read_string(mac_cb->fw_port, "media-type",
					 &media_type)) {
		for (i = 0; i < ARRAY_SIZE(media_type_defs); i++) {
			if (!strncmp(media_type_defs[i].name, media_type,
				     MAC_MEDIA_TYPE_MAX_LEN)) {
				mac_cb->media_type = media_type_defs[i].value;
				break;
			}
		}
	}

	if (fwnode_property_read_u8_array(mac_cb->fw_port, "mc-mac-mask",
					  mac_cb->mc_mask, ETH_ALEN)) {
		dev_warn(mac_cb->dev,
			 "no mc-mac-mask property, set to default value.\n");
		eth_broadcast_addr(mac_cb->mc_mask);
	}

	return 0;
}

/**
 * hns_mac_get_mode - get mac mode
 * @phy_if: phy interface
 * retuen 0 - gmac, 1 - xgmac , negative --fail
 */
static int hns_mac_get_mode(phy_interface_t phy_if)
{
	switch (phy_if) {
	case PHY_INTERFACE_MODE_SGMII:
		return MAC_GMAC_IDX;
	case PHY_INTERFACE_MODE_XGMII:
		return MAC_XGMAC_IDX;
	default:
		return -EINVAL;
	}
}

static u8 __iomem *
hns_mac_get_vaddr(struct dsaf_device *dsaf_dev,
		  struct hns_mac_cb *mac_cb, u32 mac_mode_idx)
{
	u8 __iomem *base = dsaf_dev->io_base;
	int mac_id = mac_cb->mac_id;

	if (mac_cb->mac_type == HNAE_PORT_SERVICE)
		return base + 0x40000 + mac_id * 0x4000 -
				mac_mode_idx * 0x20000;
	else
		return dsaf_dev->ppe_base + 0x1000;
}

/**
 * hns_mac_get_cfg - get mac cfg from dtb or acpi table
 * @dsaf_dev: dsa fabric device struct pointer
 * @mac_cb: mac control block
 * return 0 - success , negative --fail
 */
static int
hns_mac_get_cfg(struct dsaf_device *dsaf_dev, struct hns_mac_cb *mac_cb)
{
	int ret;
	u32 mac_mode_idx;

	mac_cb->dsaf_dev = dsaf_dev;
	mac_cb->dev = dsaf_dev->dev;

	mac_cb->sys_ctl_vaddr =	dsaf_dev->sc_base;
	mac_cb->serdes_vaddr = dsaf_dev->sds_base;

	mac_cb->sfp_prsnt = 0;
	mac_cb->txpkt_for_led = 0;
	mac_cb->rxpkt_for_led = 0;

	if (!HNS_DSAF_IS_DEBUG(dsaf_dev))
		mac_cb->mac_type = HNAE_PORT_SERVICE;
	else
		mac_cb->mac_type = HNAE_PORT_DEBUG;

	mac_cb->phy_if = dsaf_dev->misc_op->get_phy_if(mac_cb);

	ret = hns_mac_get_mode(mac_cb->phy_if);
	if (ret < 0) {
		dev_err(dsaf_dev->dev,
			"hns_mac_get_mode failed, mac%d ret = %#x!\n",
			mac_cb->mac_id, ret);
		return ret;
	}
	mac_mode_idx = (u32)ret;

	ret  = hns_mac_get_info(mac_cb);
	if (ret)
		return ret;

	mac_cb->dsaf_dev->misc_op->cpld_reset_led(mac_cb);
	mac_cb->vaddr = hns_mac_get_vaddr(dsaf_dev, mac_cb, mac_mode_idx);

	return 0;
}

static int hns_mac_get_max_port_num(struct dsaf_device *dsaf_dev)
{
	if (HNS_DSAF_IS_DEBUG(dsaf_dev))
		return 1;
	else
		return  DSAF_MAX_PORT_NUM;
}

void hns_mac_enable(struct hns_mac_cb *mac_cb, enum mac_commom_mode mode)
{
	struct mac_driver *mac_ctrl_drv = hns_mac_get_drv(mac_cb);

	mac_ctrl_drv->mac_enable(mac_cb->priv.mac, mode);
}

void hns_mac_disable(struct hns_mac_cb *mac_cb, enum mac_commom_mode mode)
{
	struct mac_driver *mac_ctrl_drv = hns_mac_get_drv(mac_cb);

	mac_ctrl_drv->mac_disable(mac_cb->priv.mac, mode);
}

/**
 * hns_mac_init - init mac
 * @dsaf_dev: dsa fabric device struct pointer
 * return 0 - success , negative --fail
 */
int hns_mac_init(struct dsaf_device *dsaf_dev)
{
	bool found = false;
	int ret;
	u32 port_id;
	int max_port_num = hns_mac_get_max_port_num(dsaf_dev);
	struct hns_mac_cb *mac_cb;
	struct fwnode_handle *child;

	device_for_each_child_node(dsaf_dev->dev, child) {
		ret = fwnode_property_read_u32(child, "reg", &port_id);
		if (ret) {
			dev_err(dsaf_dev->dev,
				"get reg fail, ret=%d!\n", ret);
			return ret;
		}
		if (port_id >= max_port_num) {
			dev_err(dsaf_dev->dev,
				"reg(%u) out of range!\n", port_id);
			return -EINVAL;
		}
		mac_cb = devm_kzalloc(dsaf_dev->dev, sizeof(*mac_cb),
				      GFP_KERNEL);
		if (!mac_cb)
			return -ENOMEM;
		mac_cb->fw_port = child;
		mac_cb->mac_id = (u8)port_id;
		dsaf_dev->mac_cb[port_id] = mac_cb;
		found = true;
	}

	/* if don't get any port subnode from dsaf node
	 * will init all port then, this is compatible with the old dts
	 */
	if (!found) {
		for (port_id = 0; port_id < max_port_num; port_id++) {
			mac_cb = devm_kzalloc(dsaf_dev->dev, sizeof(*mac_cb),
					      GFP_KERNEL);
			if (!mac_cb)
				return -ENOMEM;

			mac_cb->mac_id = port_id;
			dsaf_dev->mac_cb[port_id] = mac_cb;
		}
	}

	/* init mac_cb for all port */
	for (port_id = 0; port_id < max_port_num; port_id++) {
		mac_cb = dsaf_dev->mac_cb[port_id];
		if (!mac_cb)
			continue;

		ret = hns_mac_get_cfg(dsaf_dev, mac_cb);
		if (ret)
			return ret;

		ret = hns_mac_init_ex(mac_cb);
		if (ret)
			return ret;
	}

	return 0;
}

void hns_mac_uninit(struct dsaf_device *dsaf_dev)
{
	int i;
	int max_port_num = hns_mac_get_max_port_num(dsaf_dev);

	for (i = 0; i < max_port_num; i++) {
		if (!dsaf_dev->mac_cb[i])
			continue;

		dsaf_dev->misc_op->cpld_reset_led(dsaf_dev->mac_cb[i]);
		hns_mac_remove_phydev(dsaf_dev->mac_cb[i]);
		dsaf_dev->mac_cb[i] = NULL;
	}
}

int hns_mac_config_mac_loopback(struct hns_mac_cb *mac_cb,
				enum hnae_loop loop, int en)
{
	int ret;
	struct mac_driver *drv = hns_mac_get_drv(mac_cb);

	if (drv->config_loopback)
		ret = drv->config_loopback(drv, loop, en);
	else
		ret = -ENOTSUPP;

	return ret;
}

void hns_mac_update_stats(struct hns_mac_cb *mac_cb)
{
	struct mac_driver *mac_ctrl_drv = hns_mac_get_drv(mac_cb);

	mac_ctrl_drv->update_stats(mac_ctrl_drv);
}

void hns_mac_get_stats(struct hns_mac_cb *mac_cb, u64 *data)
{
	struct mac_driver *mac_ctrl_drv = hns_mac_get_drv(mac_cb);

	mac_ctrl_drv->get_ethtool_stats(mac_ctrl_drv, data);
}

void hns_mac_get_strings(struct hns_mac_cb *mac_cb,
			 int stringset, u8 *data)
{
	struct mac_driver *mac_ctrl_drv = hns_mac_get_drv(mac_cb);

	mac_ctrl_drv->get_strings(stringset, data);
}

int hns_mac_get_sset_count(struct hns_mac_cb *mac_cb, int stringset)
{
	struct mac_driver *mac_ctrl_drv = hns_mac_get_drv(mac_cb);

	return mac_ctrl_drv->get_sset_count(stringset);
}

void hns_mac_set_promisc(struct hns_mac_cb *mac_cb, u8 en)
{
	struct mac_driver *mac_ctrl_drv = hns_mac_get_drv(mac_cb);

	hns_dsaf_set_promisc_tcam(mac_cb->dsaf_dev, mac_cb->mac_id, !!en);

	if (mac_ctrl_drv->set_promiscuous)
		mac_ctrl_drv->set_promiscuous(mac_ctrl_drv, en);
}

int hns_mac_get_regs_count(struct hns_mac_cb *mac_cb)
{
	struct mac_driver *mac_ctrl_drv = hns_mac_get_drv(mac_cb);

	return mac_ctrl_drv->get_regs_count();
}

void hns_mac_get_regs(struct hns_mac_cb *mac_cb, void *data)
{
	struct mac_driver *mac_ctrl_drv = hns_mac_get_drv(mac_cb);

	mac_ctrl_drv->get_regs(mac_ctrl_drv, data);
}

void hns_set_led_opt(struct hns_mac_cb *mac_cb)
{
	int nic_data = 0;
	int txpkts, rxpkts;

	txpkts = mac_cb->txpkt_for_led - mac_cb->hw_stats.tx_good_pkts;
	rxpkts = mac_cb->rxpkt_for_led - mac_cb->hw_stats.rx_good_pkts;
	if (txpkts || rxpkts)
		nic_data = 1;
	else
		nic_data = 0;
	mac_cb->txpkt_for_led = mac_cb->hw_stats.tx_good_pkts;
	mac_cb->rxpkt_for_led = mac_cb->hw_stats.rx_good_pkts;
	mac_cb->dsaf_dev->misc_op->cpld_set_led(mac_cb, (int)mac_cb->link,
			 mac_cb->speed, nic_data);
}

int hns_cpld_led_set_id(struct hns_mac_cb *mac_cb,
			enum hnae_led_state status)
{
	if (!mac_cb)
		return 0;

	return mac_cb->dsaf_dev->misc_op->cpld_set_led_id(mac_cb, status);
}
