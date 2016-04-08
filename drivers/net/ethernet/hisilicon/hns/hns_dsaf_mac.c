/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/phy_fixed.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "hns_dsaf_misc.h"
#include "hns_dsaf_main.h"
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

static enum mac_mode hns_mac_dev_to_enet_if(const struct hns_mac_cb *mac_cb)
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

int hns_mac_get_sfp_prsnt(struct hns_mac_cb *mac_cb, int *sfp_prsnt)
{
	if (!mac_cb->cpld_vaddr)
		return -ENODEV;

	*sfp_prsnt = !dsaf_read_b((u8 *)mac_cb->cpld_vaddr
					+ MAC_SFP_PORT_OFFSET);

	return 0;
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

	ret = hns_mac_get_sfp_prsnt(mac_cb, &sfp_prsnt);
	if (!ret)
		*link_status = *link_status && sfp_prsnt;

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

void hns_mac_adjust_link(struct hns_mac_cb *mac_cb, int speed, int duplex)
{
	int ret;
	struct mac_driver *mac_ctrl_drv;

	mac_ctrl_drv = (struct mac_driver *)(mac_cb->priv.mac);

	mac_cb->speed = speed;
	mac_cb->half_duplex = !duplex;
	mac_ctrl_drv->mac_mode = hns_mac_dev_to_enet_if(mac_cb);

	if (mac_ctrl_drv->adjust_link) {
		ret = mac_ctrl_drv->adjust_link(mac_ctrl_drv,
			(enum mac_speed)speed, duplex);
		if (ret) {
			dev_err(mac_cb->dev,
				"adjust_link failed,%s mac%d ret = %#x!\n",
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
static int hns_mac_get_inner_port_num(struct hns_mac_cb *mac_cb,
				      u8 vmid, u8 *port_num)
{
	u8 tmp_port;
	u32 comm_idx;

	if (mac_cb->dsaf_dev->dsaf_mode <= DSAF_MODE_ENABLE) {
		if (mac_cb->mac_id != DSAF_MAX_PORT_NUM_PER_CHIP) {
			dev_err(mac_cb->dev,
				"input invalid,%s mac%d vmid%d !\n",
				mac_cb->dsaf_dev->ae_dev.name,
				mac_cb->mac_id, vmid);
			return -EINVAL;
		}
	} else if (mac_cb->dsaf_dev->dsaf_mode < DSAF_MODE_MAX) {
		if (mac_cb->mac_id >= DSAF_MAX_PORT_NUM_PER_CHIP) {
			dev_err(mac_cb->dev,
				"input invalid,%s mac%d vmid%d!\n",
				mac_cb->dsaf_dev->ae_dev.name,
				mac_cb->mac_id, vmid);
			return -EINVAL;
		}
	} else {
		dev_err(mac_cb->dev, "dsaf mode invalid,%s mac%d!\n",
			mac_cb->dsaf_dev->ae_dev.name, mac_cb->mac_id);
		return -EINVAL;
	}

	comm_idx = hns_dsaf_get_comm_idx_by_port(mac_cb->mac_id);

	if (vmid >= mac_cb->dsaf_dev->rcb_common[comm_idx]->max_vfn) {
		dev_err(mac_cb->dev, "input invalid,%s mac%d vmid%d !\n",
			mac_cb->dsaf_dev->ae_dev.name, mac_cb->mac_id, vmid);
		return -EINVAL;
	}

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
		tmp_port = vmid;
		break;
	default:
		dev_err(mac_cb->dev, "dsaf mode invalid,%s mac%d!\n",
			mac_cb->dsaf_dev->ae_dev.name, mac_cb->mac_id);
		return -EINVAL;
	}
	tmp_port += DSAF_BASE_INNER_PORT_NUM;

	*port_num = tmp_port;

	return 0;
}

/**
 *hns_mac_get_inner_port_num - change vf mac address
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
	if (dsaf_dev) {
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

int hns_mac_set_multi(struct hns_mac_cb *mac_cb,
		      u32 port_num, char *addr, bool enable)
{
	int ret;
	struct dsaf_device *dsaf_dev = mac_cb->dsaf_dev;
	struct dsaf_drv_mac_single_dest_entry mac_entry;

	if (dsaf_dev && addr) {
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
				"set mac mc port failed,%s mac%d ret = %#x!\n",
				mac_cb->dsaf_dev->ae_dev.name,
				mac_cb->mac_id, ret);
			return ret;
		}
	}

	return 0;
}

/**
 *hns_mac_del_mac - delete mac address into dsaf table,can't delete the same
 *                  address twice
 *@net_dev: net device
 *@vfn :   vf lan
 *@mac : mac address
 *return status
 */
int hns_mac_del_mac(struct hns_mac_cb *mac_cb, u32 vfn, char *mac)
{
	struct mac_entry_idx *old_mac;
	struct dsaf_device *dsaf_dev;
	u32 ret;

	dsaf_dev = mac_cb->dsaf_dev;

	if (vfn < DSAF_MAX_VM_NUM) {
		old_mac = &mac_cb->addr_entry_idx[vfn];
	} else {
		dev_err(mac_cb->dev,
			"vf queue is too large,%s mac%d queue = %#x!\n",
			mac_cb->dsaf_dev->ae_dev.name, mac_cb->mac_id, vfn);
		return -EINVAL;
	}

	if (dsaf_dev) {
		ret = hns_dsaf_del_mac_entry(dsaf_dev, old_mac->vlan_id,
					     mac_cb->mac_id, old_mac->addr);
		if (ret)
			return ret;

		if (memcmp(old_mac->addr, mac, sizeof(old_mac->addr)) == 0)
			old_mac->valid = 0;
	}

	return 0;
}

static void hns_mac_param_get(struct mac_params *param,
			      struct hns_mac_cb *mac_cb)
{
	param->vaddr = (void *)mac_cb->vaddr;
	param->mac_mode = hns_get_enet_interface(mac_cb);
	memcpy(param->addr, mac_cb->addr_entry_idx[0].addr,
	       MAC_NUM_OCTETS_PER_ADDR);
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
	u8 addr[MAC_NUM_OCTETS_PER_ADDR]
		= {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct dsaf_drv_mac_single_dest_entry mac_entry;

	/* directy return ok in debug network mode */
	if (mac_cb->mac_type == HNAE_PORT_DEBUG)
		return 0;

	if (dsaf_dev) {
		memcpy(mac_entry.addr, addr, sizeof(mac_entry.addr));
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
	u8 addr[MAC_NUM_OCTETS_PER_ADDR]
		= {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	struct mac_entry_idx *uc_mac_entry;
	struct dsaf_drv_mac_single_dest_entry mac_entry;

	if (mac_cb->mac_type == HNAE_PORT_DEBUG)
		return 0;

	uc_mac_entry = &mac_cb->addr_entry_idx[vmid];

	if (dsaf_dev)  {
		memcpy(mac_entry.addr, addr, sizeof(mac_entry.addr));
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

void hns_mac_reset(struct hns_mac_cb *mac_cb)
{
	struct mac_driver *drv;

	drv = hns_mac_get_drv(mac_cb);

	drv->mac_init(drv);

	if (drv->config_max_frame_length)
		drv->config_max_frame_length(drv, mac_cb->max_frm);

	if (drv->set_tx_auto_pause_frames)
		drv->set_tx_auto_pause_frames(drv, mac_cb->tx_pause_frm_time);

	if (drv->set_an_mode)
		drv->set_an_mode(drv, 1);

	if (drv->mac_pausefrm_cfg) {
		if (mac_cb->mac_type == HNAE_PORT_DEBUG)
			drv->mac_pausefrm_cfg(drv, 0, 0);
		else /* mac rx must disable, dsaf pfc close instead of it*/
			drv->mac_pausefrm_cfg(drv, 0, 1);
	}
}

int hns_mac_set_mtu(struct hns_mac_cb *mac_cb, u32 new_mtu)
{
	struct mac_driver *drv = hns_mac_get_drv(mac_cb);
	u32 buf_size = mac_cb->dsaf_dev->buf_size;
	u32 new_frm = new_mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;
	u32 max_frm = AE_IS_VER1(mac_cb->dsaf_dev->dsaf_ver) ?
			MAC_MAX_MTU : MAC_MAX_MTU_V2;

	if (mac_cb->mac_type == HNAE_PORT_DEBUG)
		max_frm = MAC_MAX_MTU_DBG;

	if ((new_mtu < MAC_MIN_MTU) || (new_frm > max_frm) ||
	    (new_frm > HNS_RCB_RING_MAX_BD_PER_PKT * buf_size))
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
	cpld_led_reset(mac_cb);
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

	/* Due to the chip defect, the service mac's rx pause CAN'T be enabled.
	 * We set the rx pause frm always be true (1), because DSAF deals with
	 * the rx pause frm instead of service mac. After all, we still support
	 * rx pause frm.
	 */
	if (mac_cb->mac_type == HNAE_PORT_SERVICE)
		*rx_en = 1;
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
		dev_err(mac_cb->dev, "enable autoneg is not allowed!");
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

	if (mac_cb->mac_type == HNAE_PORT_SERVICE) {
		if (!rx_en) {
			dev_err(mac_cb->dev, "disable rx_pause is not allowed!");
			return -EINVAL;
		}
	} else if (mac_cb->mac_type == HNAE_PORT_DEBUG) {
		if (tx_en || rx_en) {
			dev_err(mac_cb->dev, "enable tx_pause or enable rx_pause are not allowed!");
			return -EINVAL;
		}
	} else {
		dev_err(mac_cb->dev, "Unsupport this operation!");
		return -EINVAL;
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

/**
 *mac_free_dev  - get mac information from device node
 *@mac_cb: mac device
 *@np:device node
 *@mac_mode_idx:mac mode index
 */
static void hns_mac_get_info(struct hns_mac_cb *mac_cb,
			     struct device_node *np, u32 mac_mode_idx)
{
	mac_cb->link = false;
	mac_cb->half_duplex = false;
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

	/* Get the rest of the PHY information */
	mac_cb->phy_node = of_parse_phandle(np, "phy-handle", mac_cb->mac_id);
	if (mac_cb->phy_node)
		dev_dbg(mac_cb->dev, "mac%d phy_node: %s\n",
			mac_cb->mac_id, mac_cb->phy_node->name);
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

u8 __iomem *hns_mac_get_vaddr(struct dsaf_device *dsaf_dev,
			      struct hns_mac_cb *mac_cb, u32 mac_mode_idx)
{
	u8 __iomem *base = dsaf_dev->io_base;
	int mac_id = mac_cb->mac_id;

	if (mac_cb->mac_type == HNAE_PORT_SERVICE)
		return base + 0x40000 + mac_id * 0x4000 -
				mac_mode_idx * 0x20000;
	else
		return mac_cb->serdes_vaddr + 0x1000
			+ (mac_id - DSAF_SERVICE_PORT_NUM_PER_DSAF) * 0x100000;
}

/**
 * hns_mac_get_cfg - get mac cfg from dtb or acpi table
 * @dsaf_dev: dsa fabric device struct pointer
 * @mac_idx: mac index
 * retuen 0 - success , negative --fail
 */
int hns_mac_get_cfg(struct dsaf_device *dsaf_dev, int mac_idx)
{
	int ret;
	u32 mac_mode_idx;
	struct hns_mac_cb *mac_cb = &dsaf_dev->mac_cb[mac_idx];

	mac_cb->dsaf_dev = dsaf_dev;
	mac_cb->dev = dsaf_dev->dev;
	mac_cb->mac_id = mac_idx;

	mac_cb->sys_ctl_vaddr =	dsaf_dev->sc_base;
	mac_cb->serdes_vaddr = dsaf_dev->sds_base;

	if (dsaf_dev->cpld_base &&
	    mac_idx < DSAF_SERVICE_PORT_NUM_PER_DSAF) {
		mac_cb->cpld_vaddr = dsaf_dev->cpld_base +
			mac_cb->mac_id * CPLD_ADDR_PORT_OFFSET;
		cpld_led_reset(mac_cb);
	}
	mac_cb->sfp_prsnt = 0;
	mac_cb->txpkt_for_led = 0;
	mac_cb->rxpkt_for_led = 0;

	if (mac_idx < DSAF_SERVICE_PORT_NUM_PER_DSAF)
		mac_cb->mac_type = HNAE_PORT_SERVICE;
	else
		mac_cb->mac_type = HNAE_PORT_DEBUG;

	mac_cb->phy_if = hns_mac_get_phy_if(mac_cb);

	ret = hns_mac_get_mode(mac_cb->phy_if);
	if (ret < 0) {
		dev_err(dsaf_dev->dev,
			"hns_mac_get_mode failed,mac%d ret = %#x!\n",
			mac_cb->mac_id, ret);
		return ret;
	}
	mac_mode_idx = (u32)ret;

	hns_mac_get_info(mac_cb, mac_cb->dev->of_node, mac_mode_idx);

	mac_cb->vaddr = hns_mac_get_vaddr(dsaf_dev, mac_cb, mac_mode_idx);

	return 0;
}

/**
 * hns_mac_init - init mac
 * @dsaf_dev: dsa fabric device struct pointer
 * retuen 0 - success , negative --fail
 */
int hns_mac_init(struct dsaf_device *dsaf_dev)
{
	int i;
	int ret;
	size_t size;
	struct hns_mac_cb *mac_cb;

	size = sizeof(struct hns_mac_cb) * DSAF_MAX_PORT_NUM_PER_CHIP;
	dsaf_dev->mac_cb = devm_kzalloc(dsaf_dev->dev, size, GFP_KERNEL);
	if (!dsaf_dev->mac_cb)
		return -ENOMEM;

	for (i = 0; i < DSAF_MAX_PORT_NUM_PER_CHIP; i++) {
		ret = hns_mac_get_cfg(dsaf_dev, i);
		if (ret)
			goto free_mac_cb;

		mac_cb = &dsaf_dev->mac_cb[i];
		ret = hns_mac_init_ex(mac_cb);
		if (ret)
			goto free_mac_cb;
	}

	return 0;

free_mac_cb:
	dsaf_dev->mac_cb = NULL;

	return ret;
}

void hns_mac_uninit(struct dsaf_device *dsaf_dev)
{
	cpld_led_reset(dsaf_dev->mac_cb);
	dsaf_dev->mac_cb = NULL;
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
	hns_cpld_set_led(mac_cb, (int)mac_cb->link,
			 mac_cb->speed, nic_data);
}

int hns_cpld_led_set_id(struct hns_mac_cb *mac_cb,
			enum hnae_led_state status)
{
	if (!mac_cb || !mac_cb->cpld_vaddr)
		return 0;

	return cpld_set_led_id(mac_cb, status);
}
