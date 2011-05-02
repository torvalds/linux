#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/kthread.h>
#include <linux/kfifo.h>
#include <net/cfg80211.h>

#include "mesh.h"
#include "decl.h"
#include "cmd.h"


/***************************************************************************
 * Mesh sysfs support
 */

/*
 * Attributes exported through sysfs
 */

/**
 * lbs_anycast_get - Get function for sysfs attribute anycast_mask
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer where data will be returned
 */
static ssize_t lbs_anycast_get(struct device *dev,
		struct device_attribute *attr, char * buf)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct cmd_ds_mesh_access mesh_access;
	int ret;

	memset(&mesh_access, 0, sizeof(mesh_access));

	ret = lbs_mesh_access(priv, CMD_ACT_MESH_GET_ANYCAST, &mesh_access);
	if (ret)
		return ret;

	return snprintf(buf, 12, "0x%X\n", le32_to_cpu(mesh_access.data[0]));
}

/**
 * lbs_anycast_set - Set function for sysfs attribute anycast_mask
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer that contains new attribute value
 * @count: size of buffer
 */
static ssize_t lbs_anycast_set(struct device *dev,
		struct device_attribute *attr, const char * buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct cmd_ds_mesh_access mesh_access;
	uint32_t datum;
	int ret;

	memset(&mesh_access, 0, sizeof(mesh_access));
	sscanf(buf, "%x", &datum);
	mesh_access.data[0] = cpu_to_le32(datum);

	ret = lbs_mesh_access(priv, CMD_ACT_MESH_SET_ANYCAST, &mesh_access);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * lbs_prb_rsp_limit_get - Get function for sysfs attribute prb_rsp_limit
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer where data will be returned
 */
static ssize_t lbs_prb_rsp_limit_get(struct device *dev,
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
	return snprintf(buf, 10, "%d\n", retry_limit);
}

/**
 * lbs_prb_rsp_limit_set - Set function for sysfs attribute prb_rsp_limit
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer that contains new attribute value
 * @count: size of buffer
 */
static ssize_t lbs_prb_rsp_limit_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct cmd_ds_mesh_access mesh_access;
	int ret;
	unsigned long retry_limit;

	memset(&mesh_access, 0, sizeof(mesh_access));
	mesh_access.data[0] = cpu_to_le32(CMD_ACT_SET);

	if (!strict_strtoul(buf, 10, &retry_limit))
		return -ENOTSUPP;
	if (retry_limit > 15)
		return -ENOTSUPP;

	mesh_access.data[1] = cpu_to_le32(retry_limit);

	ret = lbs_mesh_access(priv, CMD_ACT_MESH_SET_GET_PRB_RSP_LIMIT,
			&mesh_access);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * lbs_mesh_get - Get function for sysfs attribute mesh
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer where data will be returned
 */
static ssize_t lbs_mesh_get(struct device *dev,
		struct device_attribute *attr, char * buf)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	return snprintf(buf, 5, "0x%X\n", !!priv->mesh_dev);
}

/**
 * lbs_mesh_set - Set function for sysfs attribute mesh
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer that contains new attribute value
 * @count: size of buffer
 */
static ssize_t lbs_mesh_set(struct device *dev,
		struct device_attribute *attr, const char * buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	int enable;
	int ret, action = CMD_ACT_MESH_CONFIG_STOP;

	sscanf(buf, "%x", &enable);
	enable = !!enable;
	if (enable == !!priv->mesh_dev)
		return count;
	if (enable)
		action = CMD_ACT_MESH_CONFIG_START;
	ret = lbs_mesh_config(priv, action, priv->channel);
	if (ret)
		return ret;

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
static DEVICE_ATTR(lbs_mesh, 0644, lbs_mesh_get, lbs_mesh_set);

/*
 * anycast_mask attribute to be exported per mshX interface
 * through sysfs (/sys/class/net/mshX/anycast_mask)
 */
static DEVICE_ATTR(anycast_mask, 0644, lbs_anycast_get, lbs_anycast_set);

/*
 * prb_rsp_limit attribute to be exported per mshX interface
 * through sysfs (/sys/class/net/mshX/prb_rsp_limit)
 */
static DEVICE_ATTR(prb_rsp_limit, 0644, lbs_prb_rsp_limit_get,
		lbs_prb_rsp_limit_set);

static struct attribute *lbs_mesh_sysfs_entries[] = {
	&dev_attr_anycast_mask.attr,
	&dev_attr_prb_rsp_limit.attr,
	NULL,
};

static struct attribute_group lbs_mesh_attr_group = {
	.attrs = lbs_mesh_sysfs_entries,
};



/***************************************************************************
 * Initializing and starting, stopping mesh
 */

/*
 * Check mesh FW version and appropriately send the mesh start
 * command
 */
int lbs_init_mesh(struct lbs_private *priv)
{
	struct net_device *dev = priv->dev;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_MESH);

	priv->mesh_connect_status = LBS_DISCONNECTED;

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
		if (lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_START,
				    priv->channel)) {
			priv->mesh_tlv = TLV_TYPE_MESH_ID;
			if (lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_START,
					    priv->channel))
				priv->mesh_tlv = 0;
		}
	} else
	if ((MRVL_FW_MAJOR_REV(priv->fwrelease) >= MRVL_FW_V10) &&
		(priv->fwcapinfo & MESH_CAPINFO_ENABLE_MASK)) {
		/* 10.0.0.pXX new firmwares should succeed with TLV
		 * 0x100+37; Do not invoke command with old TLV.
		 */
		priv->mesh_tlv = TLV_TYPE_MESH_ID;
		if (lbs_mesh_config(priv, CMD_ACT_MESH_CONFIG_START,
				    priv->channel))
			priv->mesh_tlv = 0;
	}


	if (priv->mesh_tlv) {
		sprintf(priv->mesh_ssid, "mesh");
		priv->mesh_ssid_len = 4;

		lbs_add_mesh(priv);

		if (device_create_file(&dev->dev, &dev_attr_lbs_mesh))
			netdev_err(dev, "cannot register lbs_mesh attribute\n");

		ret = 1;
	}

	lbs_deb_leave_args(LBS_DEB_MESH, "ret %d", ret);
	return ret;
}


int lbs_deinit_mesh(struct lbs_private *priv)
{
	struct net_device *dev = priv->dev;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_MESH);

	if (priv->mesh_tlv) {
		device_remove_file(&dev->dev, &dev_attr_lbs_mesh);
		ret = 1;
	}

	lbs_deb_leave_args(LBS_DEB_MESH, "ret %d", ret);
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

	lbs_deb_enter(LBS_DEB_MESH);
	spin_lock_irq(&priv->driver_lock);

	priv->mesh_open = 0;
	priv->mesh_connect_status = LBS_DISCONNECTED;

	netif_stop_queue(dev);
	netif_carrier_off(dev);

	spin_unlock_irq(&priv->driver_lock);

	schedule_work(&priv->mcast_work);

	lbs_deb_leave(LBS_DEB_MESH);
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

	lbs_deb_enter(LBS_DEB_NET);

	spin_lock_irq(&priv->driver_lock);

	if (priv->wdev->iftype == NL80211_IFTYPE_MONITOR) {
		ret = -EBUSY;
		goto out;
	}

	priv->mesh_open = 1;
	priv->mesh_connect_status = LBS_CONNECTED;
	netif_carrier_on(dev);

	if (!priv->tx_pending_len)
		netif_wake_queue(dev);
 out:

	spin_unlock_irq(&priv->driver_lock);
	lbs_deb_leave_args(LBS_DEB_NET, "ret %d", ret);
	return ret;
}

static const struct net_device_ops mesh_netdev_ops = {
	.ndo_open		= lbs_mesh_dev_open,
	.ndo_stop 		= lbs_mesh_stop,
	.ndo_start_xmit		= lbs_hard_start_xmit,
	.ndo_set_mac_address	= lbs_set_mac_address,
	.ndo_set_multicast_list = lbs_set_multicast_list,
};

/**
 * lbs_add_mesh - add mshX interface
 *
 * @priv:	A pointer to the &struct lbs_private structure
 * returns:	0 if successful, -X otherwise
 */
int lbs_add_mesh(struct lbs_private *priv)
{
	struct net_device *mesh_dev = NULL;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_MESH);

	/* Allocate a virtual mesh device */
	mesh_dev = alloc_netdev(0, "msh%d", ether_setup);
	if (!mesh_dev) {
		lbs_deb_mesh("init mshX device failed\n");
		ret = -ENOMEM;
		goto done;
	}
	mesh_dev->ml_priv = priv;
	priv->mesh_dev = mesh_dev;

	mesh_dev->netdev_ops = &mesh_netdev_ops;
	mesh_dev->ethtool_ops = &lbs_ethtool_ops;
	memcpy(mesh_dev->dev_addr, priv->dev->dev_addr, ETH_ALEN);

	SET_NETDEV_DEV(priv->mesh_dev, priv->dev->dev.parent);

	mesh_dev->flags |= IFF_BROADCAST | IFF_MULTICAST;
	/* Register virtual mesh interface */
	ret = register_netdev(mesh_dev);
	if (ret) {
		pr_err("cannot register mshX virtual interface\n");
		goto err_free;
	}

	ret = sysfs_create_group(&(mesh_dev->dev.kobj), &lbs_mesh_attr_group);
	if (ret)
		goto err_unregister;

	lbs_persist_config_init(mesh_dev);

	/* Everything successful */
	ret = 0;
	goto done;

err_unregister:
	unregister_netdev(mesh_dev);

err_free:
	free_netdev(mesh_dev);

done:
	lbs_deb_leave_args(LBS_DEB_MESH, "ret %d", ret);
	return ret;
}

void lbs_remove_mesh(struct lbs_private *priv)
{
	struct net_device *mesh_dev;

	mesh_dev = priv->mesh_dev;
	if (!mesh_dev)
		return;

	lbs_deb_enter(LBS_DEB_MESH);
	netif_stop_queue(mesh_dev);
	netif_carrier_off(mesh_dev);
	sysfs_remove_group(&(mesh_dev->dev.kobj), &lbs_mesh_attr_group);
	lbs_persist_config_remove(mesh_dev);
	unregister_netdev(mesh_dev);
	priv->mesh_dev = NULL;
	free_netdev(mesh_dev);
	lbs_deb_leave(LBS_DEB_MESH);
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
 * Mesh command handling
 */

/**
 * lbs_mesh_bt_add_del - Add or delete Mesh Blinding Table entries
 *
 * @priv:	A pointer to &struct lbs_private structure
 * @add:	TRUE to add the entry, FALSE to delete it
 * @addr1:	Destination address to blind or unblind
 *
 * returns:	0 on success, error on failure
 */
int lbs_mesh_bt_add_del(struct lbs_private *priv, bool add, u8 *addr1)
{
	struct cmd_ds_bt_access cmd;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	BUG_ON(addr1 == NULL);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	memcpy(cmd.addr1, addr1, ETH_ALEN);
	if (add) {
		cmd.action = cpu_to_le16(CMD_ACT_BT_ACCESS_ADD);
		lbs_deb_hex(LBS_DEB_MESH, "BT_ADD: blinded MAC addr",
			addr1, ETH_ALEN);
	} else {
		cmd.action = cpu_to_le16(CMD_ACT_BT_ACCESS_DEL);
		lbs_deb_hex(LBS_DEB_MESH, "BT_DEL: blinded MAC addr",
			addr1, ETH_ALEN);
	}

	ret = lbs_cmd_with_response(priv, CMD_BT_ACCESS, &cmd);

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

/**
 * lbs_mesh_bt_reset - Reset/clear the mesh blinding table
 *
 * @priv:	A pointer to &struct lbs_private structure
 *
 * returns:	0 on success, error on failure
 */
int lbs_mesh_bt_reset(struct lbs_private *priv)
{
	struct cmd_ds_bt_access cmd;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_BT_ACCESS_RESET);

	ret = lbs_cmd_with_response(priv, CMD_BT_ACCESS, &cmd);

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

/**
 * lbs_mesh_bt_get_inverted - Gets the inverted status of the mesh
 * blinding table
 *
 * Normally the firmware "blinds" or ignores traffic from mesh nodes in the
 * table, but an inverted table allows *only* traffic from nodes listed in
 * the table.
 *
 * @priv:	A pointer to &struct lbs_private structure
 * @inverted:  	On success, TRUE if the blinding table is inverted,
 *		FALSE if it is not inverted
 *
 * returns:	0 on success, error on failure
 */
int lbs_mesh_bt_get_inverted(struct lbs_private *priv, bool *inverted)
{
	struct cmd_ds_bt_access cmd;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	BUG_ON(inverted == NULL);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_BT_ACCESS_GET_INVERT);

	ret = lbs_cmd_with_response(priv, CMD_BT_ACCESS, &cmd);
	if (ret == 0)
		*inverted = !!cmd.id;

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

/**
 * lbs_mesh_bt_set_inverted - Sets the inverted status of the mesh
 * blinding table
 *
 * Normally the firmware "blinds" or ignores traffic from mesh nodes in the
 * table, but an inverted table allows *only* traffic from nodes listed in
 * the table.
 *
 * @priv:	A pointer to &struct lbs_private structure
 * @inverted:	TRUE to invert the blinding table (only traffic from
 *		listed nodes allowed), FALSE to return it
 *		to normal state (listed nodes ignored)
 *
 * returns:	0 on success, error on failure
 */
int lbs_mesh_bt_set_inverted(struct lbs_private *priv, bool inverted)
{
	struct cmd_ds_bt_access cmd;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_BT_ACCESS_SET_INVERT);
	cmd.id = cpu_to_le32(!!inverted);

	ret = lbs_cmd_with_response(priv, CMD_BT_ACCESS, &cmd);

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

/**
 * lbs_mesh_bt_get_entry - List an entry in the mesh blinding table
 *
 * @priv:	A pointer to &struct lbs_private structure
 * @id:		The ID of the entry to list
 * @addr1:	MAC address associated with the table entry
 *
 * returns: 	   	0 on success, error on failure
 */
int lbs_mesh_bt_get_entry(struct lbs_private *priv, u32 id, u8 *addr1)
{
	struct cmd_ds_bt_access cmd;
	int ret = 0;

	lbs_deb_enter(LBS_DEB_CMD);

	BUG_ON(addr1 == NULL);

	memset(&cmd, 0, sizeof(cmd));
	cmd.hdr.size = cpu_to_le16(sizeof(cmd));
	cmd.action = cpu_to_le16(CMD_ACT_BT_ACCESS_SET_INVERT);
	cmd.id = cpu_to_le32(id);

	ret = lbs_cmd_with_response(priv, CMD_BT_ACCESS, &cmd);
	if (ret == 0)
		memcpy(addr1, cmd.addr1, sizeof(cmd.addr1));

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return ret;
}

/**
 * lbs_cmd_fwt_access - Access the mesh forwarding table
 *
 * @priv:	A pointer to &struct lbs_private structure
 * @cmd_action:	The forwarding table action to perform
 * @cmd:	The pre-filled FWT_ACCESS command
 *
 * returns:	0 on success and 'cmd' will be filled with the
 *		firmware's response
 */
int lbs_cmd_fwt_access(struct lbs_private *priv, u16 cmd_action,
			struct cmd_ds_fwt_access *cmd)
{
	int ret;

	lbs_deb_enter_args(LBS_DEB_CMD, "action %d", cmd_action);

	cmd->hdr.command = cpu_to_le16(CMD_FWT_ACCESS);
	cmd->hdr.size = cpu_to_le16(sizeof(struct cmd_ds_fwt_access));
	cmd->hdr.result = 0;
	cmd->action = cpu_to_le16(cmd_action);

	ret = lbs_cmd_with_response(priv, CMD_FWT_ACCESS, cmd);

	lbs_deb_leave_args(LBS_DEB_CMD, "ret %d", ret);
	return 0;
}

int lbs_mesh_access(struct lbs_private *priv, uint16_t cmd_action,
		    struct cmd_ds_mesh_access *cmd)
{
	int ret;

	lbs_deb_enter_args(LBS_DEB_CMD, "action %d", cmd_action);

	cmd->hdr.command = cpu_to_le16(CMD_MESH_ACCESS);
	cmd->hdr.size = cpu_to_le16(sizeof(*cmd));
	cmd->hdr.result = 0;

	cmd->action = cpu_to_le16(cmd_action);

	ret = lbs_cmd_with_response(priv, CMD_MESH_ACCESS, cmd);

	lbs_deb_leave(LBS_DEB_CMD);
	return ret;
}

static int __lbs_mesh_config_send(struct lbs_private *priv,
				  struct cmd_ds_mesh_config *cmd,
				  uint16_t action, uint16_t type)
{
	int ret;
	u16 command = CMD_MESH_CONFIG_OLD;

	lbs_deb_enter(LBS_DEB_CMD);

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

	lbs_deb_leave(LBS_DEB_CMD);
	return ret;
}

int lbs_mesh_config_send(struct lbs_private *priv,
			 struct cmd_ds_mesh_config *cmd,
			 uint16_t action, uint16_t type)
{
	int ret;

	if (!(priv->fwcapinfo & FW_CAPINFO_PERSISTENT_CONFIG))
		return -EOPNOTSUPP;

	ret = __lbs_mesh_config_send(priv, cmd, action, type);
	return ret;
}

/* This function is the CMD_MESH_CONFIG legacy function.  It only handles the
 * START and STOP actions.  The extended actions supported by CMD_MESH_CONFIG
 * are all handled by preparing a struct cmd_ds_mesh_config and passing it to
 * lbs_mesh_config_send.
 */
int lbs_mesh_config(struct lbs_private *priv, uint16_t action, uint16_t chan)
{
	struct cmd_ds_mesh_config cmd;
	struct mrvl_meshie *ie;
	DECLARE_SSID_BUF(ssid);

	memset(&cmd, 0, sizeof(cmd));
	cmd.channel = cpu_to_le16(chan);
	ie = (struct mrvl_meshie *)cmd.data;

	switch (action) {
	case CMD_ACT_MESH_CONFIG_START:
		ie->id = WLAN_EID_GENERIC;
		ie->val.oui[0] = 0x00;
		ie->val.oui[1] = 0x50;
		ie->val.oui[2] = 0x43;
		ie->val.type = MARVELL_MESH_IE_TYPE;
		ie->val.subtype = MARVELL_MESH_IE_SUBTYPE;
		ie->val.version = MARVELL_MESH_IE_VERSION;
		ie->val.active_protocol_id = MARVELL_MESH_PROTO_ID_HWMP;
		ie->val.active_metric_id = MARVELL_MESH_METRIC_ID;
		ie->val.mesh_capability = MARVELL_MESH_CAPABILITY;
		ie->val.mesh_id_len = priv->mesh_ssid_len;
		memcpy(ie->val.mesh_id, priv->mesh_ssid, priv->mesh_ssid_len);
		ie->len = sizeof(struct mrvl_meshie_val) -
			IEEE80211_MAX_SSID_LEN + priv->mesh_ssid_len;
		cmd.length = cpu_to_le16(sizeof(struct mrvl_meshie_val));
		break;
	case CMD_ACT_MESH_CONFIG_STOP:
		break;
	default:
		return -1;
	}
	lbs_deb_cmd("mesh config action %d type %x channel %d SSID %s\n",
		    action, priv->mesh_tlv, chan,
		    print_ssid(ssid, priv->mesh_ssid, priv->mesh_ssid_len));

	return __lbs_mesh_config_send(priv, &cmd, action, priv->mesh_tlv);
}



/***************************************************************************
 * Persistent configuration support
 */

static int mesh_get_default_parameters(struct device *dev,
				       struct mrvl_mesh_defaults *defs)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct cmd_ds_mesh_config cmd;
	int ret;

	memset(&cmd, 0, sizeof(struct cmd_ds_mesh_config));
	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_GET,
				   CMD_TYPE_MESH_GET_DEFAULTS);

	if (ret)
		return -EOPNOTSUPP;

	memcpy(defs, &cmd.data[0], sizeof(struct mrvl_mesh_defaults));

	return 0;
}

/**
 * bootflag_get - Get function for sysfs attribute bootflag
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer where data will be returned
 */
static ssize_t bootflag_get(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct mrvl_mesh_defaults defs;
	int ret;

	ret = mesh_get_default_parameters(dev, &defs);

	if (ret)
		return ret;

	return snprintf(buf, 12, "%d\n", le32_to_cpu(defs.bootflag));
}

/**
 * bootflag_set - Set function for sysfs attribute bootflag
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer that contains new attribute value
 * @count: size of buffer
 */
static ssize_t bootflag_set(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct cmd_ds_mesh_config cmd;
	uint32_t datum;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	ret = sscanf(buf, "%d", &datum);
	if ((ret != 1) || (datum > 1))
		return -EINVAL;

	*((__le32 *)&cmd.data[0]) = cpu_to_le32(!!datum);
	cmd.length = cpu_to_le16(sizeof(uint32_t));
	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_SET,
				   CMD_TYPE_MESH_SET_BOOTFLAG);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * boottime_get - Get function for sysfs attribute boottime
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer where data will be returned
 */
static ssize_t boottime_get(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct mrvl_mesh_defaults defs;
	int ret;

	ret = mesh_get_default_parameters(dev, &defs);

	if (ret)
		return ret;

	return snprintf(buf, 12, "%d\n", defs.boottime);
}

/**
 * boottime_set - Set function for sysfs attribute boottime
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer that contains new attribute value
 * @count: size of buffer
 */
static ssize_t boottime_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct cmd_ds_mesh_config cmd;
	uint32_t datum;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	ret = sscanf(buf, "%d", &datum);
	if ((ret != 1) || (datum > 255))
		return -EINVAL;

	/* A too small boot time will result in the device booting into
	 * standalone (no-host) mode before the host can take control of it,
	 * so the change will be hard to revert.  This may be a desired
	 * feature (e.g to configure a very fast boot time for devices that
	 * will not be attached to a host), but dangerous.  So I'm enforcing a
	 * lower limit of 20 seconds:  remove and recompile the driver if this
	 * does not work for you.
	 */
	datum = (datum < 20) ? 20 : datum;
	cmd.data[0] = datum;
	cmd.length = cpu_to_le16(sizeof(uint8_t));
	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_SET,
				   CMD_TYPE_MESH_SET_BOOTTIME);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * channel_get - Get function for sysfs attribute channel
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer where data will be returned
 */
static ssize_t channel_get(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct mrvl_mesh_defaults defs;
	int ret;

	ret = mesh_get_default_parameters(dev, &defs);

	if (ret)
		return ret;

	return snprintf(buf, 12, "%d\n", le16_to_cpu(defs.channel));
}

/**
 * channel_set - Set function for sysfs attribute channel
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer that contains new attribute value
 * @count: size of buffer
 */
static ssize_t channel_set(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	struct cmd_ds_mesh_config cmd;
	uint32_t datum;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	ret = sscanf(buf, "%d", &datum);
	if (ret != 1 || datum < 1 || datum > 11)
		return -EINVAL;

	*((__le16 *)&cmd.data[0]) = cpu_to_le16(datum);
	cmd.length = cpu_to_le16(sizeof(uint16_t));
	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_SET,
				   CMD_TYPE_MESH_SET_DEF_CHANNEL);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * mesh_id_get - Get function for sysfs attribute mesh_id
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer where data will be returned
 */
static ssize_t mesh_id_get(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct mrvl_mesh_defaults defs;
	int ret;

	ret = mesh_get_default_parameters(dev, &defs);

	if (ret)
		return ret;

	if (defs.meshie.val.mesh_id_len > IEEE80211_MAX_SSID_LEN) {
		dev_err(dev, "inconsistent mesh ID length\n");
		defs.meshie.val.mesh_id_len = IEEE80211_MAX_SSID_LEN;
	}

	memcpy(buf, defs.meshie.val.mesh_id, defs.meshie.val.mesh_id_len);
	buf[defs.meshie.val.mesh_id_len] = '\n';
	buf[defs.meshie.val.mesh_id_len + 1] = '\0';

	return defs.meshie.val.mesh_id_len + 1;
}

/**
 * mesh_id_set - Set function for sysfs attribute mesh_id
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer that contains new attribute value
 * @count: size of buffer
 */
static ssize_t mesh_id_set(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct cmd_ds_mesh_config cmd;
	struct mrvl_mesh_defaults defs;
	struct mrvl_meshie *ie;
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	int len;
	int ret;

	if (count < 2 || count > IEEE80211_MAX_SSID_LEN + 1)
		return -EINVAL;

	memset(&cmd, 0, sizeof(struct cmd_ds_mesh_config));
	ie = (struct mrvl_meshie *) &cmd.data[0];

	/* fetch all other Information Element parameters */
	ret = mesh_get_default_parameters(dev, &defs);

	cmd.length = cpu_to_le16(sizeof(struct mrvl_meshie));

	/* transfer IE elements */
	memcpy(ie, &defs.meshie, sizeof(struct mrvl_meshie));

	len = count - 1;
	memcpy(ie->val.mesh_id, buf, len);
	/* SSID len */
	ie->val.mesh_id_len = len;
	/* IE len */
	ie->len = sizeof(struct mrvl_meshie_val) - IEEE80211_MAX_SSID_LEN + len;

	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_SET,
				   CMD_TYPE_MESH_SET_MESH_IE);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * protocol_id_get - Get function for sysfs attribute protocol_id
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer where data will be returned
 */
static ssize_t protocol_id_get(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct mrvl_mesh_defaults defs;
	int ret;

	ret = mesh_get_default_parameters(dev, &defs);

	if (ret)
		return ret;

	return snprintf(buf, 5, "%d\n", defs.meshie.val.active_protocol_id);
}

/**
 * protocol_id_set - Set function for sysfs attribute protocol_id
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer that contains new attribute value
 * @count: size of buffer
 */
static ssize_t protocol_id_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cmd_ds_mesh_config cmd;
	struct mrvl_mesh_defaults defs;
	struct mrvl_meshie *ie;
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	uint32_t datum;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	ret = sscanf(buf, "%d", &datum);
	if ((ret != 1) || (datum > 255))
		return -EINVAL;

	/* fetch all other Information Element parameters */
	ret = mesh_get_default_parameters(dev, &defs);

	cmd.length = cpu_to_le16(sizeof(struct mrvl_meshie));

	/* transfer IE elements */
	ie = (struct mrvl_meshie *) &cmd.data[0];
	memcpy(ie, &defs.meshie, sizeof(struct mrvl_meshie));
	/* update protocol id */
	ie->val.active_protocol_id = datum;

	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_SET,
				   CMD_TYPE_MESH_SET_MESH_IE);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * metric_id_get - Get function for sysfs attribute metric_id
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer where data will be returned
 */
static ssize_t metric_id_get(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mrvl_mesh_defaults defs;
	int ret;

	ret = mesh_get_default_parameters(dev, &defs);

	if (ret)
		return ret;

	return snprintf(buf, 5, "%d\n", defs.meshie.val.active_metric_id);
}

/**
 * metric_id_set - Set function for sysfs attribute metric_id
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer that contains new attribute value
 * @count: size of buffer
 */
static ssize_t metric_id_set(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct cmd_ds_mesh_config cmd;
	struct mrvl_mesh_defaults defs;
	struct mrvl_meshie *ie;
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	uint32_t datum;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	ret = sscanf(buf, "%d", &datum);
	if ((ret != 1) || (datum > 255))
		return -EINVAL;

	/* fetch all other Information Element parameters */
	ret = mesh_get_default_parameters(dev, &defs);

	cmd.length = cpu_to_le16(sizeof(struct mrvl_meshie));

	/* transfer IE elements */
	ie = (struct mrvl_meshie *) &cmd.data[0];
	memcpy(ie, &defs.meshie, sizeof(struct mrvl_meshie));
	/* update metric id */
	ie->val.active_metric_id = datum;

	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_SET,
				   CMD_TYPE_MESH_SET_MESH_IE);
	if (ret)
		return ret;

	return strlen(buf);
}

/**
 * capability_get - Get function for sysfs attribute capability
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer where data will be returned
 */
static ssize_t capability_get(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mrvl_mesh_defaults defs;
	int ret;

	ret = mesh_get_default_parameters(dev, &defs);

	if (ret)
		return ret;

	return snprintf(buf, 5, "%d\n", defs.meshie.val.mesh_capability);
}

/**
 * capability_set - Set function for sysfs attribute capability
 * @dev: the &struct device
 * @attr: device attributes
 * @buf: buffer that contains new attribute value
 * @count: size of buffer
 */
static ssize_t capability_set(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct cmd_ds_mesh_config cmd;
	struct mrvl_mesh_defaults defs;
	struct mrvl_meshie *ie;
	struct lbs_private *priv = to_net_dev(dev)->ml_priv;
	uint32_t datum;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	ret = sscanf(buf, "%d", &datum);
	if ((ret != 1) || (datum > 255))
		return -EINVAL;

	/* fetch all other Information Element parameters */
	ret = mesh_get_default_parameters(dev, &defs);

	cmd.length = cpu_to_le16(sizeof(struct mrvl_meshie));

	/* transfer IE elements */
	ie = (struct mrvl_meshie *) &cmd.data[0];
	memcpy(ie, &defs.meshie, sizeof(struct mrvl_meshie));
	/* update value */
	ie->val.mesh_capability = datum;

	ret = lbs_mesh_config_send(priv, &cmd, CMD_ACT_MESH_CONFIG_SET,
				   CMD_TYPE_MESH_SET_MESH_IE);
	if (ret)
		return ret;

	return strlen(buf);
}


static DEVICE_ATTR(bootflag, 0644, bootflag_get, bootflag_set);
static DEVICE_ATTR(boottime, 0644, boottime_get, boottime_set);
static DEVICE_ATTR(channel, 0644, channel_get, channel_set);
static DEVICE_ATTR(mesh_id, 0644, mesh_id_get, mesh_id_set);
static DEVICE_ATTR(protocol_id, 0644, protocol_id_get, protocol_id_set);
static DEVICE_ATTR(metric_id, 0644, metric_id_get, metric_id_set);
static DEVICE_ATTR(capability, 0644, capability_get, capability_set);

static struct attribute *boot_opts_attrs[] = {
	&dev_attr_bootflag.attr,
	&dev_attr_boottime.attr,
	&dev_attr_channel.attr,
	NULL
};

static struct attribute_group boot_opts_group = {
	.name = "boot_options",
	.attrs = boot_opts_attrs,
};

static struct attribute *mesh_ie_attrs[] = {
	&dev_attr_mesh_id.attr,
	&dev_attr_protocol_id.attr,
	&dev_attr_metric_id.attr,
	&dev_attr_capability.attr,
	NULL
};

static struct attribute_group mesh_ie_group = {
	.name = "mesh_ie",
	.attrs = mesh_ie_attrs,
};

void lbs_persist_config_init(struct net_device *dev)
{
	int ret;
	ret = sysfs_create_group(&(dev->dev.kobj), &boot_opts_group);
	ret = sysfs_create_group(&(dev->dev.kobj), &mesh_ie_group);
}

void lbs_persist_config_remove(struct net_device *dev)
{
	sysfs_remove_group(&(dev->dev.kobj), &boot_opts_group);
	sysfs_remove_group(&(dev->dev.kobj), &mesh_ie_group);
}



/***************************************************************************
 * Ethtool related
 */

static const char *mesh_stat_strings[] = {
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

	lbs_deb_enter(LBS_DEB_ETHTOOL);

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

	lbs_deb_enter(LBS_DEB_ETHTOOL);
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
	int i;

	lbs_deb_enter(LBS_DEB_ETHTOOL);

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < MESH_STATS_NUM; i++) {
			memcpy(s + i * ETH_GSTRING_LEN,
					mesh_stat_strings[i],
					ETH_GSTRING_LEN);
		}
		break;
	}
	lbs_deb_enter(LBS_DEB_ETHTOOL);
}
