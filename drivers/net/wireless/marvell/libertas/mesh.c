// SPDX-License-Identifier: GPL-2.0
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/hardirq.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/kthread.h>
#include <linux/kfifo.h>
#include <net/cfg80211.h>

#include "mesh.h"
#include "decl.h"
#include "cmd.h"


static int lbs_add_mesh(struct lbs_private *priv);

/***************************************************************************
 * Mesh command handling
 */

static int lbs_mesh_access(struct lbs_private *priv, uint16_t cmd_action,
		    struct cmd_ds_mesh_access *cmd)
{
	int ret;

	cmd->hdr.command = cpu_to_le16(CMD_MESH_ACCESS);
	cmd->hdr.size = cpu_to_le16(sizeof(*cmd));
	cmd->hdr.result = 0;

	cmd->action = cpu_to_le16(cmd_action);

	ret = lbs_cmd_with_response(priv, CMD_MESH_ACCESS, cmd);

	return ret;
}

static int __lbs_mesh_config_send(struct lbs_private *priv,
				  struct cmd_ds_mesh_config *cmd,
				  uint16_t action, uint16_t type)
{
	int ret;
	u16 command = CMD_MESH_CONFIG_OLD;

	/*
	 * Command id is 0xac for v10 FW along with mesh interface
	 * id in bits 14-13-12.
	 */
	if (priv->mesh_tlv == TLV_TYPE_MESH_ID)
		command = CMD_MESH_CONFIG |
			  (MESH_IFACE_ID << MESH_IFACE_BIT_OFFSET);

	cmd->hdr.command = cpu_to_le16(command);
	cmd->hdr.size = cpu_to_le16(sizeof(struct cmd_ds_mesh_config));
	cmd->hdr.result = 0;

	cmd->type = cpu_to_le16(type);
	cmd->action = cpu_to_le16(action);

	ret = lbs_cmd_with_response(priv, command, cmd);

	return ret;
}

/* This function is the CMD_MESH_CONFIG legacy function.  It only handles the
 * START and STOP actions.  The extended actions supported by CMD_MESH_CONFIG
 * are all handled by preparing a struct cmd_ds_mesh_config and passing it to
 * lbs_mesh_config_send.
 */
static int lbs_mesh_config(struct lbs_private *priv, uint16_t action,
		uint16_t chan)
{
	struct wireless_dev *mesh_wdev;
	struct cmd_ds_mesh_config cmd;
	struct mrvl_meshie *ie;

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = cpu_to_le16(chan);
	ie = (struct mrvl_meshie *)cmd.data;

	switch (action) {
	case CMD_ACT_MESH_CONFIG_START:
		ie->id = WLAN_EID_VENDOR_SPECIFIC;
		ie->val.oui[0] = 0x00;
		ie->val.oui[1] = 0x50;
		ie->val.oui[2] = 0x43;
		ie->val.type = MARVELL_MESH_IE_TYPE;
		ie->val.subtype = MARVELL_MESH_IE_SUBTYPE;
		ie->val.version = MARVELL_MESH_IE_VERSION;
		ie->val.active_protocol_id = MARVELL_MESH_PROTO_ID_HWMP;
		ie->val.active_metric_id = MARVELL_MESH_METRIC_ID;
		ie->val.mesh_capability = MARVELL_MESH_CAPABILITY;

		if (priv->mesh_dev) {
			mesh_wdev = priv->mesh_dev->ieee80211_ptr;
			ie->val.mesh_id_len = mesh_wdev->u.mesh.id_up_len;
			memcpy(ie->val.mesh_id, mesh_wdev->u.mesh.id,
						mesh_wdev->u.mesh.id_up_len);
		}

		ie->len = sizeof(struct mrvl_meshie_val) -
			IEEE80211_MAX_SSID_LEN + ie->val.mesh_id_len;

		cmd.length = cpu_to_le16(sizeof(struct mrvl_meshie_val));
		break;
	case CMD_ACT_MESH_CONFIG_STOP:
		break;
	default:
		return -1;
	}
	lbs_deb_cmd("mesh config action %d type %x channel %d SSID %*pE\n",
		    action, priv->mesh_tlv, chan, ie->val.mesh_id_len,
		    ie->val.mesh_id);

	return __lbs_mesh_config_send(priv, &cmd, action, priv->mesh_tlv);
}

int lbs_mesh_set_channel(struct lbs_private *priv, u8 channel)
{
	priv->mesh_channel = channel;
	return lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_START, channel);
}

static uint16_t lbs_mesh_get_channel(struct lbs_private *priv)
{
	return priv->mesh_channel ?: 1;
}

/***************************************************************************
 * Mesh sysfs support
 */

/*
 * Attributes exported through sysfs
 */

/**
 * anycast_mask_show - Get function for sysfs attribute anycast_mask
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer where data will be returned
 */
static ssize_t anycast_mask_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct cmd_ds_mesh_access mesh_access;
	int ret;

	memset(&mesh_access, 0, sizeof(mesh_access));

	ret = lbs_mesh_access(priv, CMD_ACT_MESH_GET_ANYCAST, &mesh_access);
	if (ret)
		return ret;

	return sysfs_emit(buf, "0x%X\n", le32_to_cpu(mesh_access.data[0]));
}

/**
 * anycast_mask_store - Set function for sysfs attribute anycast_mask
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer that contains new attribute value
 * @count: size of buffer
 */
static ssize_t anycast_mask_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct cmd_ds_mesh_access mesh_access;
	uint32_t datum;
	int ret;

	ret = kstrtouint(buf, 16, &datum);
	if (ret)
		return ret;

	memset(&mesh_access, 0, sizeof(mesh_access));
	mesh_access.data[0] = cpu_to_le32(datum);

	ret = lbs_mesh_access(priv, CMD_ACT_MESH_SET_ANYCAST, &mesh_access);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * prb_rsp_limit_show - Get function for sysfs attribute prb_rsp_limit
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer where data will be returned
 */
static ssize_t prb_rsp_limit_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct cmd_ds_mesh_access mesh_access;
	int ret;
	u32 retry_limit;

	memset(&mesh_access, 0, sizeof(mesh_access));
	mesh_access.data[0] = cpu_to_le32(CMD_ACT_GET);

	ret = lbs_mesh_access(priv, CMD_ACT_MESH_SET_GET_PRB_RSP_LIMIT,
			&mesh_access);
	if (ret)
		return ret;

	retry_limit = le32_to_cpu(mesh_access.data[1]);
	return sysfs_emit(buf, "%d\n", retry_limit);
}

/**
 * prb_rsp_limit_store - Set function for sysfs attribute prb_rsp_limit
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer that contains new attribute value
 * @count: size of buffer
 */
static ssize_t prb_rsp_limit_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct cmd_ds_mesh_access mesh_access;
	int ret;
	unsigned long retry_limit;

	ret = kstrtoul(buf, 10, &retry_limit);
	if (ret)
		return ret;
	if (retry_limit > 15)
		return -ENOTSUPP;

	memset(&mesh_access, 0, sizeof(mesh_access));
	mesh_access.data[0] = cpu_to_le32(CMD_ACT_SET);
	mesh_access.data[1] = cpu_to_le32(retry_limit);

	ret = lbs_mesh_access(priv, CMD_ACT_MESH_SET_GET_PRB_RSP_LIMIT,
			&mesh_access);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * lbs_mesh_show - Get function for sysfs attribute mesh
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer where data will be returned
 */
static ssize_t lbs_mesh_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	return sysfs_emit(buf, "0x%X\n", !!priv->mesh_dev);
}

/**
 * lbs_mesh_store - Set function for sysfs attribute mesh
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer that contains new attribute value
 * @count: size of buffer
 */
static ssize_t lbs_mesh_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	int ret, enable;

	ret = kstrtoint(buf, 16, &enable);
	if (ret)
		return ret;

	enable = !!enable;
	if (enable == !!priv->mesh_dev)
		return count;

	if (enable)
		lbs_add_mesh(priv);
	else
		lbs_remove_mesh(priv);

	return count;
}

/*
 * lbs_mesh attribute to be exported per ethX interface
 * through sysfs (/sys/class/net/ethX/lbs_mesh)
 */
static DEVICE_ATTR_RW(lbs_mesh);

/*
 * anycast_mask attribute to be exported per mshX interface
 * through sysfs (/sys/class/net/mshX/anycast_mask)
 */
static DEVICE_ATTR_RW(anycast_mask);

/*
 * prb_rsp_limit attribute to be exported per mshX interface
 * through sysfs (/sys/class/net/mshX/prb_rsp_limit)
 */
static DEVICE_ATTR_RW(prb_rsp_limit);

static struct attribute *lbs_mesh_sysfs_entries[] = {
	&dev_attr_anycast_mask.attr,
	&dev_attr_prb_rsp_limit.attr,
	NULL,
};

static const struct attribute_group lbs_mesh_attr_group = {
	.attrs = lbs_mesh_sysfs_entries,
};

/***************************************************************************
 * Initializing and starting, stopping mesh
 */

/*
 * Check mesh FW version and appropriately send the mesh start
 * command
 */
void lbs_init_mesh(struct lbs_private *priv)
{
	/* Determine mesh_fw_ver from fwrelease and fwcapinfo */
	/* 5.0.16p0 9.0.0.p0 is known to NOT support any mesh */
	/* 5.110.22 have mesh command with 0xa3 command id */
	/* 10.0.0.p0 FW brings in mesh config command with different id */
	/* Check FW version MSB and initialize mesh_fw_ver */
	if (MRVL_FW_MAJOR_REV(priv->fwrelease) == MRVL_FW_V5) {
		/* Enable mesh, if supported, and work out which TLV it uses.
		   0x100 + 291 is an unofficial value used in 5.110.20.pXX
		   0x100 + 37 is the official value used in 5.110.21.pXX
		   but we check them in that order because 20.pXX doesn't
		   give an error -- it just silently fails. */

		/* 5.110.20.pXX firmware will fail the command if the channel
		   doesn't match the existing channel. But only if the TLV
		   is correct. If the channel is wrong, _BOTH_ versions will
		   give an error to 0x100+291, and allow 0x100+37 to succeed.
		   It's just that 5.110.20.pXX will not have done anything
		   useful */

		priv->mesh_tlv = TLV_TYPE_OLD_MESH_ID;
		if (lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_START, 1)) {
			priv->mesh_tlv = TLV_TYPE_MESH_ID;
			if (lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_START, 1))
				priv->mesh_tlv = 0;
		}
	} else
	if ((MRVL_FW_MAJOR_REV(priv->fwrelease) >= MRVL_FW_V10) &&
		(priv->fwcapinfo & MESH_CAPINFO_ENABLE_MASK)) {
		/* 10.0.0.pXX new firmwares should succeed with TLV
		 * 0x100+37; Do not invoke command with old TLV.
		 */
		priv->mesh_tlv = TLV_TYPE_MESH_ID;
		if (lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_START, 1))
			priv->mesh_tlv = 0;
	}

	/* Stop meshing until interface is brought up */
	lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_STOP, 1);
}

void lbs_start_mesh(struct lbs_private *priv)
{
	lbs_add_mesh(priv);

	if (device_create_file(&priv->dev->dev, &dev_attr_lbs_mesh))
		netdev_err(priv->dev, "cannot register lbs_mesh attribute\n");
}

int lbs_deinit_mesh(struct lbs_private *priv)
{
	struct net_device *dev = priv->dev;
	int ret = 0;

	if (priv->mesh_tlv) {
		device_remove_file(&dev->dev, &dev_attr_lbs_mesh);
		ret = 1;
	}

	return ret;
}


/**
 * lbs_mesh_stop - close the mshX interface
 *
 * @dev:	A pointer to &net_device structure
 * returns:	0
 */
static int lbs_mesh_stop(struct net_device *dev)
{
	struct lbs_private *priv = dev->ml_priv;

	lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_STOP,
		lbs_mesh_get_channel(priv));

	spin_lock_irq(&priv->driver_lock);

	netif_stop_queue(dev);
	netif_carrier_off(dev);

	spin_unlock_irq(&priv->driver_lock);

	lbs_update_mcast(priv);
	if (!lbs_iface_active(priv))
		lbs_stop_iface(priv);

	return 0;
}

/**
 * lbs_mesh_dev_open - open the mshX interface
 *
 * @dev:	A pointer to &net_device structure
 * returns:	0 or -EBUSY if monitor mode active
 */
static int lbs_mesh_dev_open(struct net_device *dev)
{
	struct lbs_private *priv = dev->ml_priv;
	int ret = 0;

	if (!priv->iface_running) {
		ret = lbs_start_iface(priv);
		if (ret)
			goto out;
	}

	spin_lock_irq(&priv->driver_lock);

	if (priv->wdev->iftype == NL80211_IFTYPE_MONITOR) {
		ret = -EBUSY;
		spin_unlock_irq(&priv->driver_lock);
		goto out;
	}

	netif_carrier_on(dev);

	if (!priv->tx_pending_len)
		netif_wake_queue(dev);

	spin_unlock_irq(&priv->driver_lock);

	ret = lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_START,
		lbs_mesh_get_channel(priv));

out:
	return ret;
}

static const struct net_device_ops mesh_netdev_ops = {
	.ndo_open		= lbs_mesh_dev_open,
	.ndo_stop 		= lbs_mesh_stop,
	.ndo_start_xmit		= lbs_hard_start_xmit,
	.ndo_set_mac_address	= lbs_set_mac_address,
	.ndo_set_rx_mode	= lbs_set_multicast_list,
};

/**
 * lbs_add_mesh - add mshX interface
 *
 * @priv:	A pointer to the &struct lbs_private structure
 * returns:	0 if successful, -X otherwise
 */
static int lbs_add_mesh(struct lbs_private *priv)
{
	struct net_device *mesh_dev = NULL;
	struct wireless_dev *mesh_wdev;
	int ret = 0;

	/* Allocate a virtual mesh device */
	mesh_wdev = kzalloc_obj(struct wireless_dev);
	if (!mesh_wdev) {
		lbs_deb_mesh("init mshX wireless device failed\n");
		ret = -ENOMEM;
		goto done;
	}

	mesh_dev = alloc_netdev(0, "msh%d", NET_NAME_UNKNOWN, ether_setup);
	if (!mesh_dev) {
		lbs_deb_mesh("init mshX device failed\n");
		ret = -ENOMEM;
		goto err_free_wdev;
	}

	mesh_wdev->iftype = NL80211_IFTYPE_MESH_POINT;
	mesh_wdev->wiphy = priv->wdev->wiphy;

	if (priv->mesh_tlv) {
		sprintf(mesh_wdev->u.mesh.id, "mesh");
		mesh_wdev->u.mesh.id_up_len = 4;
	}

	mesh_wdev->netdev = mesh_dev;

	mesh_dev->ml_priv = priv;
	mesh_dev->ieee80211_ptr = mesh_wdev;
	priv->mesh_dev = mesh_dev;

	mesh_dev->netdev_ops = &mesh_netdev_ops;
	mesh_dev->ethtool_ops = &lbs_ethtool_ops;
	eth_hw_addr_inherit(mesh_dev, priv->dev);

	SET_NETDEV_DEV(priv->mesh_dev, priv->dev->dev.parent);

	mesh_dev->flags |= IFF_BROADCAST | IFF_MULTICAST;
	mesh_dev->sysfs_groups[0] = &lbs_mesh_attr_group;

	/* Register virtual mesh interface */
	ret = register_netdev(mesh_dev);
	if (ret) {
		pr_err("cannot register mshX virtual interface\n");
		goto err_free_netdev;
	}

	/* Everything successful */
	ret = 0;
	goto done;

err_free_netdev:
	free_netdev(mesh_dev);

err_free_wdev:
	kfree(mesh_wdev);

done:
	return ret;
}

void lbs_remove_mesh(struct lbs_private *priv)
{
	struct net_device *mesh_dev;

	mesh_dev = priv->mesh_dev;
	if (!mesh_dev)
		return;

	netif_stop_queue(mesh_dev);
	netif_carrier_off(mesh_dev);
	unregister_netdev(mesh_dev);
	priv->mesh_dev = NULL;
	kfree(mesh_dev->ieee80211_ptr);
	free_netdev(mesh_dev);
}


/***************************************************************************
 * Sending and receiving
 */
struct net_device *lbs_mesh_set_dev(struct lbs_private *priv,
	struct net_device *dev, struct rxpd *rxpd)
{
	if (priv->mesh_dev) {
		if (priv->mesh_tlv == TLV_TYPE_OLD_MESH_ID) {
			if (rxpd->rx_control & RxPD_MESH_FRAME)
				dev = priv->mesh_dev;
		} else if (priv->mesh_tlv == TLV_TYPE_MESH_ID) {
			if (rxpd->u.bss.bss_num == MESH_IFACE_ID)
				dev = priv->mesh_dev;
		}
	}
	return dev;
}


void lbs_mesh_set_txpd(struct lbs_private *priv,
	struct net_device *dev, struct txpd *txpd)
{
	if (dev == priv->mesh_dev) {
		if (priv->mesh_tlv == TLV_TYPE_OLD_MESH_ID)
			txpd->tx_control |= cpu_to_le32(TxPD_MESH_FRAME);
		else if (priv->mesh_tlv == TLV_TYPE_MESH_ID)
			txpd->u.bss.bss_num = MESH_IFACE_ID;
	}
}


/***************************************************************************
 * Ethtool related
 */

static const char mesh_stat_strings[MESH_STATS_NUM][ETH_GSTRING_LEN] = {
	"drop_duplicate_bcast",
	"drop_ttl_zero",
	"drop_no_fwd_route",
	"drop_no_buffers",
	"fwded_unicast_cnt",
	"fwded_bcast_cnt",
	"drop_blind_table",
	"tx_failed_cnt"
};

void lbs_mesh_ethtool_get_stats(struct net_device *dev,
	struct ethtool_stats *stats, uint64_t *data)
{
	struct lbs_private *priv = dev->ml_priv;
	struct cmd_ds_mesh_access mesh_access;
	int ret;

	/* Get Mesh Statistics */
	ret = lbs_mesh_access(priv, CMD_ACT_MESH_GET_STATS, &mesh_access);

	if (ret) {
		memset(data, 0, MESH_STATS_NUM*(sizeof(uint64_t)));
		return;
	}

	priv->mstats.fwd_drop_rbt = le32_to_cpu(mesh_access.data[0]);
	priv->mstats.fwd_drop_ttl = le32_to_cpu(mesh_access.data[1]);
	priv->mstats.fwd_drop_noroute = le32_to_cpu(mesh_access.data[2]);
	priv->mstats.fwd_drop_nobuf = le32_to_cpu(mesh_access.data[3]);
	priv->mstats.fwd_unicast_cnt = le32_to_cpu(mesh_access.data[4]);
	priv->mstats.fwd_bcast_cnt = le32_to_cpu(mesh_access.data[5]);
	priv->mstats.drop_blind = le32_to_cpu(mesh_access.data[6]);
	priv->mstats.tx_failed_cnt = le32_to_cpu(mesh_access.data[7]);

	data[0] = priv->mstats.fwd_drop_rbt;
	data[1] = priv->mstats.fwd_drop_ttl;
	data[2] = priv->mstats.fwd_drop_noroute;
	data[3] = priv->mstats.fwd_drop_nobuf;
	data[4] = priv->mstats.fwd_unicast_cnt;
	data[5] = priv->mstats.fwd_bcast_cnt;
	data[6] = priv->mstats.drop_blind;
	data[7] = priv->mstats.tx_failed_cnt;
}

int lbs_mesh_ethtool_get_sset_count(struct net_device *dev, int sset)
{
	struct lbs_private *priv = dev->ml_priv;

	if (sset == ETH_SS_STATS && dev == priv->mesh_dev)
		return MESH_STATS_NUM;

	return -EOPNOTSUPP;
}

void lbs_mesh_ethtool_get_strings(struct net_device *dev,
	uint32_t stringset, uint8_t *s)
{
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(s, mesh_stat_strings, sizeof(mesh_stat_strings));
		break;
	}
}
