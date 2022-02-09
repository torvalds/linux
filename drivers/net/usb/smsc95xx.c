// SPDX-License-Identifier: GPL-2.0-or-later
 /***************************************************************************
 *
 * Copyright (C) 2007-2008 SMSC
 *
 *****************************************************************************/

#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/bitrev.h>
#include <linux/crc16.h>
#include <linux/crc32.h>
#include <linux/usb/usbnet.h>
#include <linux/slab.h>
#include <linux/of_net.h>
#include <linux/mdio.h>
#include <linux/phy.h>
#include <net/selftests.h>

#include "smsc95xx.h"

#define SMSC_CHIPNAME			"smsc95xx"
#define SMSC_DRIVER_VERSION		"2.0.0"
#define HS_USB_PKT_SIZE			(512)
#define FS_USB_PKT_SIZE			(64)
#define DEFAULT_HS_BURST_CAP_SIZE	(16 * 1024 + 5 * HS_USB_PKT_SIZE)
#define DEFAULT_FS_BURST_CAP_SIZE	(6 * 1024 + 33 * FS_USB_PKT_SIZE)
#define DEFAULT_BULK_IN_DELAY		(0x00002000)
#define MAX_SINGLE_PACKET_SIZE		(2048)
#define LAN95XX_EEPROM_MAGIC		(0x9500)
#define EEPROM_MAC_OFFSET		(0x01)
#define DEFAULT_TX_CSUM_ENABLE		(true)
#define DEFAULT_RX_CSUM_ENABLE		(true)
#define SMSC95XX_INTERNAL_PHY_ID	(1)
#define SMSC95XX_TX_OVERHEAD		(8)
#define SMSC95XX_TX_OVERHEAD_CSUM	(12)
#define SUPPORTED_WAKE			(WAKE_PHY | WAKE_UCAST | WAKE_BCAST | \
					 WAKE_MCAST | WAKE_ARP | WAKE_MAGIC)

#define FEATURE_8_WAKEUP_FILTERS	(0x01)
#define FEATURE_PHY_NLP_CROSSOVER	(0x02)
#define FEATURE_REMOTE_WAKEUP		(0x04)

#define SUSPEND_SUSPEND0		(0x01)
#define SUSPEND_SUSPEND1		(0x02)
#define SUSPEND_SUSPEND2		(0x04)
#define SUSPEND_SUSPEND3		(0x08)
#define SUSPEND_ALLMODES		(SUSPEND_SUSPEND0 | SUSPEND_SUSPEND1 | \
					 SUSPEND_SUSPEND2 | SUSPEND_SUSPEND3)

struct smsc95xx_priv {
	u32 mac_cr;
	u32 hash_hi;
	u32 hash_lo;
	u32 wolopts;
	spinlock_t mac_cr_lock;
	u8 features;
	u8 suspend_flags;
	struct mii_bus *mdiobus;
	struct phy_device *phydev;
};

static bool turbo_mode = true;
module_param(turbo_mode, bool, 0644);
MODULE_PARM_DESC(turbo_mode, "Enable multiple frames per Rx transaction");

static int __must_check __smsc95xx_read_reg(struct usbnet *dev, u32 index,
					    u32 *data, int in_pm)
{
	u32 buf;
	int ret;
	int (*fn)(struct usbnet *, u8, u8, u16, u16, void *, u16);

	BUG_ON(!dev);

	if (!in_pm)
		fn = usbnet_read_cmd;
	else
		fn = usbnet_read_cmd_nopm;

	ret = fn(dev, USB_VENDOR_REQUEST_READ_REGISTER, USB_DIR_IN
		 | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		 0, index, &buf, 4);
	if (unlikely(ret < 0)) {
		netdev_warn(dev->net, "Failed to read reg index 0x%08x: %d\n",
			    index, ret);
		return ret;
	}

	le32_to_cpus(&buf);
	*data = buf;

	return ret;
}

static int __must_check __smsc95xx_write_reg(struct usbnet *dev, u32 index,
					     u32 data, int in_pm)
{
	u32 buf;
	int ret;
	int (*fn)(struct usbnet *, u8, u8, u16, u16, const void *, u16);

	BUG_ON(!dev);

	if (!in_pm)
		fn = usbnet_write_cmd;
	else
		fn = usbnet_write_cmd_nopm;

	buf = data;
	cpu_to_le32s(&buf);

	ret = fn(dev, USB_VENDOR_REQUEST_WRITE_REGISTER, USB_DIR_OUT
		 | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
		 0, index, &buf, 4);
	if (unlikely(ret < 0))
		netdev_warn(dev->net, "Failed to write reg index 0x%08x: %d\n",
			    index, ret);

	return ret;
}

static int __must_check smsc95xx_read_reg_nopm(struct usbnet *dev, u32 index,
					       u32 *data)
{
	return __smsc95xx_read_reg(dev, index, data, 1);
}

static int __must_check smsc95xx_write_reg_nopm(struct usbnet *dev, u32 index,
						u32 data)
{
	return __smsc95xx_write_reg(dev, index, data, 1);
}

static int __must_check smsc95xx_read_reg(struct usbnet *dev, u32 index,
					  u32 *data)
{
	return __smsc95xx_read_reg(dev, index, data, 0);
}

static int __must_check smsc95xx_write_reg(struct usbnet *dev, u32 index,
					   u32 data)
{
	return __smsc95xx_write_reg(dev, index, data, 0);
}

/* Loop until the read is completed with timeout
 * called with phy_mutex held */
static int __must_check __smsc95xx_phy_wait_not_busy(struct usbnet *dev,
						     int in_pm)
{
	unsigned long start_time = jiffies;
	u32 val;
	int ret;

	do {
		ret = __smsc95xx_read_reg(dev, MII_ADDR, &val, in_pm);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading MII_ACCESS\n");
			return ret;
		}

		if (!(val & MII_BUSY_))
			return 0;
	} while (!time_after(jiffies, start_time + HZ));

	return -EIO;
}

static u32 mii_address_cmd(int phy_id, int idx, u16 op)
{
	return (phy_id & 0x1f) << 11 | (idx & 0x1f) << 6 | op;
}

static int __smsc95xx_mdio_read(struct usbnet *dev, int phy_id, int idx,
				int in_pm)
{
	u32 val, addr;
	int ret;

	mutex_lock(&dev->phy_mutex);

	/* confirm MII not busy */
	ret = __smsc95xx_phy_wait_not_busy(dev, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "%s: MII is busy\n", __func__);
		goto done;
	}

	/* set the address, index & direction (read from PHY) */
	addr = mii_address_cmd(phy_id, idx, MII_READ_ | MII_BUSY_);
	ret = __smsc95xx_write_reg(dev, MII_ADDR, addr, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing MII_ADDR\n");
		goto done;
	}

	ret = __smsc95xx_phy_wait_not_busy(dev, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "Timed out reading MII reg %02X\n", idx);
		goto done;
	}

	ret = __smsc95xx_read_reg(dev, MII_DATA, &val, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "Error reading MII_DATA\n");
		goto done;
	}

	ret = (u16)(val & 0xFFFF);

done:
	mutex_unlock(&dev->phy_mutex);
	return ret;
}

static void __smsc95xx_mdio_write(struct usbnet *dev, int phy_id,
				  int idx, int regval, int in_pm)
{
	u32 val, addr;
	int ret;

	mutex_lock(&dev->phy_mutex);

	/* confirm MII not busy */
	ret = __smsc95xx_phy_wait_not_busy(dev, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "%s: MII is busy\n", __func__);
		goto done;
	}

	val = regval;
	ret = __smsc95xx_write_reg(dev, MII_DATA, val, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing MII_DATA\n");
		goto done;
	}

	/* set the address, index & direction (write to PHY) */
	addr = mii_address_cmd(phy_id, idx, MII_WRITE_ | MII_BUSY_);
	ret = __smsc95xx_write_reg(dev, MII_ADDR, addr, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing MII_ADDR\n");
		goto done;
	}

	ret = __smsc95xx_phy_wait_not_busy(dev, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "Timed out writing MII reg %02X\n", idx);
		goto done;
	}

done:
	mutex_unlock(&dev->phy_mutex);
}

static int smsc95xx_mdio_read_nopm(struct usbnet *dev, int idx)
{
	struct smsc95xx_priv *pdata = dev->driver_priv;

	return __smsc95xx_mdio_read(dev, pdata->phydev->mdio.addr, idx, 1);
}

static void smsc95xx_mdio_write_nopm(struct usbnet *dev, int idx, int regval)
{
	struct smsc95xx_priv *pdata = dev->driver_priv;

	__smsc95xx_mdio_write(dev, pdata->phydev->mdio.addr, idx, regval, 1);
}

static int smsc95xx_mdiobus_read(struct mii_bus *bus, int phy_id, int idx)
{
	struct usbnet *dev = bus->priv;

	return __smsc95xx_mdio_read(dev, phy_id, idx, 0);
}

static int smsc95xx_mdiobus_write(struct mii_bus *bus, int phy_id, int idx,
				  u16 regval)
{
	struct usbnet *dev = bus->priv;

	__smsc95xx_mdio_write(dev, phy_id, idx, regval, 0);
	return 0;
}

static int __must_check smsc95xx_wait_eeprom(struct usbnet *dev)
{
	unsigned long start_time = jiffies;
	u32 val;
	int ret;

	do {
		ret = smsc95xx_read_reg(dev, E2P_CMD, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading E2P_CMD\n");
			return ret;
		}

		if (!(val & E2P_CMD_BUSY_) || (val & E2P_CMD_TIMEOUT_))
			break;
		udelay(40);
	} while (!time_after(jiffies, start_time + HZ));

	if (val & (E2P_CMD_TIMEOUT_ | E2P_CMD_BUSY_)) {
		netdev_warn(dev->net, "EEPROM read operation timeout\n");
		return -EIO;
	}

	return 0;
}

static int __must_check smsc95xx_eeprom_confirm_not_busy(struct usbnet *dev)
{
	unsigned long start_time = jiffies;
	u32 val;
	int ret;

	do {
		ret = smsc95xx_read_reg(dev, E2P_CMD, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading E2P_CMD\n");
			return ret;
		}

		if (!(val & E2P_CMD_BUSY_))
			return 0;

		udelay(40);
	} while (!time_after(jiffies, start_time + HZ));

	netdev_warn(dev->net, "EEPROM is busy\n");
	return -EIO;
}

static int smsc95xx_read_eeprom(struct usbnet *dev, u32 offset, u32 length,
				u8 *data)
{
	u32 val;
	int i, ret;

	BUG_ON(!dev);
	BUG_ON(!data);

	ret = smsc95xx_eeprom_confirm_not_busy(dev);
	if (ret)
		return ret;

	for (i = 0; i < length; i++) {
		val = E2P_CMD_BUSY_ | E2P_CMD_READ_ | (offset & E2P_CMD_ADDR_);
		ret = smsc95xx_write_reg(dev, E2P_CMD, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing E2P_CMD\n");
			return ret;
		}

		ret = smsc95xx_wait_eeprom(dev);
		if (ret < 0)
			return ret;

		ret = smsc95xx_read_reg(dev, E2P_DATA, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading E2P_DATA\n");
			return ret;
		}

		data[i] = val & 0xFF;
		offset++;
	}

	return 0;
}

static int smsc95xx_write_eeprom(struct usbnet *dev, u32 offset, u32 length,
				 u8 *data)
{
	u32 val;
	int i, ret;

	BUG_ON(!dev);
	BUG_ON(!data);

	ret = smsc95xx_eeprom_confirm_not_busy(dev);
	if (ret)
		return ret;

	/* Issue write/erase enable command */
	val = E2P_CMD_BUSY_ | E2P_CMD_EWEN_;
	ret = smsc95xx_write_reg(dev, E2P_CMD, val);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing E2P_DATA\n");
		return ret;
	}

	ret = smsc95xx_wait_eeprom(dev);
	if (ret < 0)
		return ret;

	for (i = 0; i < length; i++) {

		/* Fill data register */
		val = data[i];
		ret = smsc95xx_write_reg(dev, E2P_DATA, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing E2P_DATA\n");
			return ret;
		}

		/* Send "write" command */
		val = E2P_CMD_BUSY_ | E2P_CMD_WRITE_ | (offset & E2P_CMD_ADDR_);
		ret = smsc95xx_write_reg(dev, E2P_CMD, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing E2P_CMD\n");
			return ret;
		}

		ret = smsc95xx_wait_eeprom(dev);
		if (ret < 0)
			return ret;

		offset++;
	}

	return 0;
}

static int __must_check smsc95xx_write_reg_async(struct usbnet *dev, u16 index,
						 u32 data)
{
	const u16 size = 4;
	u32 buf;
	int ret;

	buf = data;
	cpu_to_le32s(&buf);

	ret = usbnet_write_cmd_async(dev, USB_VENDOR_REQUEST_WRITE_REGISTER,
				     USB_DIR_OUT | USB_TYPE_VENDOR |
				     USB_RECIP_DEVICE,
				     0, index, &buf, size);
	if (ret < 0)
		netdev_warn(dev->net, "Error write async cmd, sts=%d\n",
			    ret);
	return ret;
}

/* returns hash bit number for given MAC address
 * example:
 * 01 00 5E 00 00 01 -> returns bit number 31 */
static unsigned int smsc95xx_hash(char addr[ETH_ALEN])
{
	return (ether_crc(ETH_ALEN, addr) >> 26) & 0x3f;
}

static void smsc95xx_set_multicast(struct net_device *netdev)
{
	struct usbnet *dev = netdev_priv(netdev);
	struct smsc95xx_priv *pdata = dev->driver_priv;
	unsigned long flags;
	int ret;

	pdata->hash_hi = 0;
	pdata->hash_lo = 0;

	spin_lock_irqsave(&pdata->mac_cr_lock, flags);

	if (dev->net->flags & IFF_PROMISC) {
		netif_dbg(dev, drv, dev->net, "promiscuous mode enabled\n");
		pdata->mac_cr |= MAC_CR_PRMS_;
		pdata->mac_cr &= ~(MAC_CR_MCPAS_ | MAC_CR_HPFILT_);
	} else if (dev->net->flags & IFF_ALLMULTI) {
		netif_dbg(dev, drv, dev->net, "receive all multicast enabled\n");
		pdata->mac_cr |= MAC_CR_MCPAS_;
		pdata->mac_cr &= ~(MAC_CR_PRMS_ | MAC_CR_HPFILT_);
	} else if (!netdev_mc_empty(dev->net)) {
		struct netdev_hw_addr *ha;

		pdata->mac_cr |= MAC_CR_HPFILT_;
		pdata->mac_cr &= ~(MAC_CR_PRMS_ | MAC_CR_MCPAS_);

		netdev_for_each_mc_addr(ha, netdev) {
			u32 bitnum = smsc95xx_hash(ha->addr);
			u32 mask = 0x01 << (bitnum & 0x1F);
			if (bitnum & 0x20)
				pdata->hash_hi |= mask;
			else
				pdata->hash_lo |= mask;
		}

		netif_dbg(dev, drv, dev->net, "HASHH=0x%08X, HASHL=0x%08X\n",
				   pdata->hash_hi, pdata->hash_lo);
	} else {
		netif_dbg(dev, drv, dev->net, "receive own packets only\n");
		pdata->mac_cr &=
			~(MAC_CR_PRMS_ | MAC_CR_MCPAS_ | MAC_CR_HPFILT_);
	}

	spin_unlock_irqrestore(&pdata->mac_cr_lock, flags);

	/* Initiate async writes, as we can't wait for completion here */
	ret = smsc95xx_write_reg_async(dev, HASHH, pdata->hash_hi);
	if (ret < 0)
		netdev_warn(dev->net, "failed to initiate async write to HASHH\n");

	ret = smsc95xx_write_reg_async(dev, HASHL, pdata->hash_lo);
	if (ret < 0)
		netdev_warn(dev->net, "failed to initiate async write to HASHL\n");

	ret = smsc95xx_write_reg_async(dev, MAC_CR, pdata->mac_cr);
	if (ret < 0)
		netdev_warn(dev->net, "failed to initiate async write to MAC_CR\n");
}

static int smsc95xx_phy_update_flowcontrol(struct usbnet *dev)
{
	u32 flow = 0, afc_cfg;
	struct smsc95xx_priv *pdata = dev->driver_priv;
	bool tx_pause, rx_pause;

	int ret = smsc95xx_read_reg(dev, AFC_CFG, &afc_cfg);
	if (ret < 0)
		return ret;

	if (pdata->phydev->duplex == DUPLEX_FULL) {
		phy_get_pause(pdata->phydev, &tx_pause, &rx_pause);

		if (rx_pause)
			flow = 0xFFFF0002;

		if (tx_pause) {
			afc_cfg |= 0xF;
			flow |= 0xFFFF0000;
		} else {
			afc_cfg &= ~0xF;
		}

		netif_dbg(dev, link, dev->net, "rx pause %s, tx pause %s\n",
			  rx_pause ? "enabled" : "disabled",
			  tx_pause ? "enabled" : "disabled");
	} else {
		netif_dbg(dev, link, dev->net, "half duplex\n");
		afc_cfg |= 0xF;
	}

	ret = smsc95xx_write_reg(dev, FLOW, flow);
	if (ret < 0)
		return ret;

	return smsc95xx_write_reg(dev, AFC_CFG, afc_cfg);
}

static int smsc95xx_link_reset(struct usbnet *dev)
{
	struct smsc95xx_priv *pdata = dev->driver_priv;
	unsigned long flags;
	int ret;

	ret = smsc95xx_write_reg(dev, INT_STS, INT_STS_CLEAR_ALL_);
	if (ret < 0)
		return ret;

	spin_lock_irqsave(&pdata->mac_cr_lock, flags);
	if (pdata->phydev->duplex != DUPLEX_FULL) {
		pdata->mac_cr &= ~MAC_CR_FDPX_;
		pdata->mac_cr |= MAC_CR_RCVOWN_;
	} else {
		pdata->mac_cr &= ~MAC_CR_RCVOWN_;
		pdata->mac_cr |= MAC_CR_FDPX_;
	}
	spin_unlock_irqrestore(&pdata->mac_cr_lock, flags);

	ret = smsc95xx_write_reg(dev, MAC_CR, pdata->mac_cr);
	if (ret < 0)
		return ret;

	ret = smsc95xx_phy_update_flowcontrol(dev);
	if (ret < 0)
		netdev_warn(dev->net, "Error updating PHY flow control\n");

	return ret;
}

static void smsc95xx_status(struct usbnet *dev, struct urb *urb)
{
	u32 intdata;

	if (urb->actual_length != 4) {
		netdev_warn(dev->net, "unexpected urb length %d\n",
			    urb->actual_length);
		return;
	}

	intdata = get_unaligned_le32(urb->transfer_buffer);
	netif_dbg(dev, link, dev->net, "intdata: 0x%08X\n", intdata);

	if (intdata & INT_ENP_PHY_INT_)
		usbnet_defer_kevent(dev, EVENT_LINK_RESET);
	else
		netdev_warn(dev->net, "unexpected interrupt, intdata=0x%08X\n",
			    intdata);
}

/* Enable or disable Tx & Rx checksum offload engines */
static int smsc95xx_set_features(struct net_device *netdev,
	netdev_features_t features)
{
	struct usbnet *dev = netdev_priv(netdev);
	u32 read_buf;
	int ret;

	ret = smsc95xx_read_reg(dev, COE_CR, &read_buf);
	if (ret < 0)
		return ret;

	if (features & NETIF_F_IP_CSUM)
		read_buf |= Tx_COE_EN_;
	else
		read_buf &= ~Tx_COE_EN_;

	if (features & NETIF_F_RXCSUM)
		read_buf |= Rx_COE_EN_;
	else
		read_buf &= ~Rx_COE_EN_;

	ret = smsc95xx_write_reg(dev, COE_CR, read_buf);
	if (ret < 0)
		return ret;

	netif_dbg(dev, hw, dev->net, "COE_CR = 0x%08x\n", read_buf);
	return 0;
}

static int smsc95xx_ethtool_get_eeprom_len(struct net_device *net)
{
	return MAX_EEPROM_SIZE;
}

static int smsc95xx_ethtool_get_eeprom(struct net_device *netdev,
				       struct ethtool_eeprom *ee, u8 *data)
{
	struct usbnet *dev = netdev_priv(netdev);

	ee->magic = LAN95XX_EEPROM_MAGIC;

	return smsc95xx_read_eeprom(dev, ee->offset, ee->len, data);
}

static int smsc95xx_ethtool_set_eeprom(struct net_device *netdev,
				       struct ethtool_eeprom *ee, u8 *data)
{
	struct usbnet *dev = netdev_priv(netdev);

	if (ee->magic != LAN95XX_EEPROM_MAGIC) {
		netdev_warn(dev->net, "EEPROM: magic value mismatch, magic = 0x%x\n",
			    ee->magic);
		return -EINVAL;
	}

	return smsc95xx_write_eeprom(dev, ee->offset, ee->len, data);
}

static int smsc95xx_ethtool_getregslen(struct net_device *netdev)
{
	/* all smsc95xx registers */
	return COE_CR - ID_REV + sizeof(u32);
}

static void
smsc95xx_ethtool_getregs(struct net_device *netdev, struct ethtool_regs *regs,
			 void *buf)
{
	struct usbnet *dev = netdev_priv(netdev);
	unsigned int i, j;
	int retval;
	u32 *data = buf;

	retval = smsc95xx_read_reg(dev, ID_REV, &regs->version);
	if (retval < 0) {
		netdev_warn(netdev, "REGS: cannot read ID_REV\n");
		return;
	}

	for (i = ID_REV, j = 0; i <= COE_CR; i += (sizeof(u32)), j++) {
		retval = smsc95xx_read_reg(dev, i, &data[j]);
		if (retval < 0) {
			netdev_warn(netdev, "REGS: cannot read reg[%x]\n", i);
			return;
		}
	}
}

static void smsc95xx_ethtool_get_wol(struct net_device *net,
				     struct ethtool_wolinfo *wolinfo)
{
	struct usbnet *dev = netdev_priv(net);
	struct smsc95xx_priv *pdata = dev->driver_priv;

	wolinfo->supported = SUPPORTED_WAKE;
	wolinfo->wolopts = pdata->wolopts;
}

static int smsc95xx_ethtool_set_wol(struct net_device *net,
				    struct ethtool_wolinfo *wolinfo)
{
	struct usbnet *dev = netdev_priv(net);
	struct smsc95xx_priv *pdata = dev->driver_priv;
	int ret;

	if (wolinfo->wolopts & ~SUPPORTED_WAKE)
		return -EINVAL;

	pdata->wolopts = wolinfo->wolopts & SUPPORTED_WAKE;

	ret = device_set_wakeup_enable(&dev->udev->dev, pdata->wolopts);
	if (ret < 0)
		netdev_warn(dev->net, "device_set_wakeup_enable error %d\n", ret);

	return ret;
}

static u32 smsc95xx_get_link(struct net_device *net)
{
	phy_read_status(net->phydev);
	return net->phydev->link;
}

static void smsc95xx_ethtool_get_strings(struct net_device *netdev, u32 sset,
					u8 *data)
{
	switch (sset) {
	case ETH_SS_TEST:
		net_selftest_get_strings(data);
		break;
	}
}

static int smsc95xx_ethtool_get_sset_count(struct net_device *ndev, int sset)
{
	switch (sset) {
	case ETH_SS_TEST:
		return net_selftest_get_count();
	default:
		return -EOPNOTSUPP;
	}
}

static const struct ethtool_ops smsc95xx_ethtool_ops = {
	.get_link	= smsc95xx_get_link,
	.nway_reset	= phy_ethtool_nway_reset,
	.get_drvinfo	= usbnet_get_drvinfo,
	.get_msglevel	= usbnet_get_msglevel,
	.set_msglevel	= usbnet_set_msglevel,
	.get_eeprom_len	= smsc95xx_ethtool_get_eeprom_len,
	.get_eeprom	= smsc95xx_ethtool_get_eeprom,
	.set_eeprom	= smsc95xx_ethtool_set_eeprom,
	.get_regs_len	= smsc95xx_ethtool_getregslen,
	.get_regs	= smsc95xx_ethtool_getregs,
	.get_wol	= smsc95xx_ethtool_get_wol,
	.set_wol	= smsc95xx_ethtool_set_wol,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
	.get_ts_info	= ethtool_op_get_ts_info,
	.self_test	= net_selftest,
	.get_strings	= smsc95xx_ethtool_get_strings,
	.get_sset_count	= smsc95xx_ethtool_get_sset_count,
};

static int smsc95xx_ioctl(struct net_device *netdev, struct ifreq *rq, int cmd)
{
	if (!netif_running(netdev))
		return -EINVAL;

	return phy_mii_ioctl(netdev->phydev, rq, cmd);
}

static void smsc95xx_init_mac_address(struct usbnet *dev)
{
	u8 addr[ETH_ALEN];

	/* maybe the boot loader passed the MAC address in devicetree */
	if (!platform_get_ethdev_address(&dev->udev->dev, dev->net)) {
		if (is_valid_ether_addr(dev->net->dev_addr)) {
			/* device tree values are valid so use them */
			netif_dbg(dev, ifup, dev->net, "MAC address read from the device tree\n");
			return;
		}
	}

	/* try reading mac address from EEPROM */
	if (smsc95xx_read_eeprom(dev, EEPROM_MAC_OFFSET, ETH_ALEN, addr) == 0) {
		eth_hw_addr_set(dev->net, addr);
		if (is_valid_ether_addr(dev->net->dev_addr)) {
			/* eeprom values are valid so use them */
			netif_dbg(dev, ifup, dev->net, "MAC address read from EEPROM\n");
			return;
		}
	}

	/* no useful static MAC address found. generate a random one */
	eth_hw_addr_random(dev->net);
	netif_dbg(dev, ifup, dev->net, "MAC address set to eth_random_addr\n");
}

static int smsc95xx_set_mac_address(struct usbnet *dev)
{
	u32 addr_lo = dev->net->dev_addr[0] | dev->net->dev_addr[1] << 8 |
		dev->net->dev_addr[2] << 16 | dev->net->dev_addr[3] << 24;
	u32 addr_hi = dev->net->dev_addr[4] | dev->net->dev_addr[5] << 8;
	int ret;

	ret = smsc95xx_write_reg(dev, ADDRL, addr_lo);
	if (ret < 0)
		return ret;

	return smsc95xx_write_reg(dev, ADDRH, addr_hi);
}

/* starts the TX path */
static int smsc95xx_start_tx_path(struct usbnet *dev)
{
	struct smsc95xx_priv *pdata = dev->driver_priv;
	unsigned long flags;
	int ret;

	/* Enable Tx at MAC */
	spin_lock_irqsave(&pdata->mac_cr_lock, flags);
	pdata->mac_cr |= MAC_CR_TXEN_;
	spin_unlock_irqrestore(&pdata->mac_cr_lock, flags);

	ret = smsc95xx_write_reg(dev, MAC_CR, pdata->mac_cr);
	if (ret < 0)
		return ret;

	/* Enable Tx at SCSRs */
	return smsc95xx_write_reg(dev, TX_CFG, TX_CFG_ON_);
}

/* Starts the Receive path */
static int smsc95xx_start_rx_path(struct usbnet *dev, int in_pm)
{
	struct smsc95xx_priv *pdata = dev->driver_priv;
	unsigned long flags;

	spin_lock_irqsave(&pdata->mac_cr_lock, flags);
	pdata->mac_cr |= MAC_CR_RXEN_;
	spin_unlock_irqrestore(&pdata->mac_cr_lock, flags);

	return __smsc95xx_write_reg(dev, MAC_CR, pdata->mac_cr, in_pm);
}

static int smsc95xx_reset(struct usbnet *dev)
{
	struct smsc95xx_priv *pdata = dev->driver_priv;
	u32 read_buf, write_buf, burst_cap;
	int ret = 0, timeout;

	netif_dbg(dev, ifup, dev->net, "entering smsc95xx_reset\n");

	ret = smsc95xx_write_reg(dev, HW_CFG, HW_CFG_LRST_);
	if (ret < 0)
		return ret;

	timeout = 0;
	do {
		msleep(10);
		ret = smsc95xx_read_reg(dev, HW_CFG, &read_buf);
		if (ret < 0)
			return ret;
		timeout++;
	} while ((read_buf & HW_CFG_LRST_) && (timeout < 100));

	if (timeout >= 100) {
		netdev_warn(dev->net, "timeout waiting for completion of Lite Reset\n");
		return ret;
	}

	ret = smsc95xx_write_reg(dev, PM_CTRL, PM_CTL_PHY_RST_);
	if (ret < 0)
		return ret;

	timeout = 0;
	do {
		msleep(10);
		ret = smsc95xx_read_reg(dev, PM_CTRL, &read_buf);
		if (ret < 0)
			return ret;
		timeout++;
	} while ((read_buf & PM_CTL_PHY_RST_) && (timeout < 100));

	if (timeout >= 100) {
		netdev_warn(dev->net, "timeout waiting for PHY Reset\n");
		return ret;
	}

	ret = smsc95xx_set_mac_address(dev);
	if (ret < 0)
		return ret;

	netif_dbg(dev, ifup, dev->net, "MAC Address: %pM\n",
		  dev->net->dev_addr);

	ret = smsc95xx_read_reg(dev, HW_CFG, &read_buf);
	if (ret < 0)
		return ret;

	netif_dbg(dev, ifup, dev->net, "Read Value from HW_CFG : 0x%08x\n",
		  read_buf);

	read_buf |= HW_CFG_BIR_;

	ret = smsc95xx_write_reg(dev, HW_CFG, read_buf);
	if (ret < 0)
		return ret;

	ret = smsc95xx_read_reg(dev, HW_CFG, &read_buf);
	if (ret < 0)
		return ret;

	netif_dbg(dev, ifup, dev->net,
		  "Read Value from HW_CFG after writing HW_CFG_BIR_: 0x%08x\n",
		  read_buf);

	if (!turbo_mode) {
		burst_cap = 0;
		dev->rx_urb_size = MAX_SINGLE_PACKET_SIZE;
	} else if (dev->udev->speed == USB_SPEED_HIGH) {
		burst_cap = DEFAULT_HS_BURST_CAP_SIZE / HS_USB_PKT_SIZE;
		dev->rx_urb_size = DEFAULT_HS_BURST_CAP_SIZE;
	} else {
		burst_cap = DEFAULT_FS_BURST_CAP_SIZE / FS_USB_PKT_SIZE;
		dev->rx_urb_size = DEFAULT_FS_BURST_CAP_SIZE;
	}

	netif_dbg(dev, ifup, dev->net, "rx_urb_size=%ld\n",
		  (ulong)dev->rx_urb_size);

	ret = smsc95xx_write_reg(dev, BURST_CAP, burst_cap);
	if (ret < 0)
		return ret;

	ret = smsc95xx_read_reg(dev, BURST_CAP, &read_buf);
	if (ret < 0)
		return ret;

	netif_dbg(dev, ifup, dev->net,
		  "Read Value from BURST_CAP after writing: 0x%08x\n",
		  read_buf);

	ret = smsc95xx_write_reg(dev, BULK_IN_DLY, DEFAULT_BULK_IN_DELAY);
	if (ret < 0)
		return ret;

	ret = smsc95xx_read_reg(dev, BULK_IN_DLY, &read_buf);
	if (ret < 0)
		return ret;

	netif_dbg(dev, ifup, dev->net,
		  "Read Value from BULK_IN_DLY after writing: 0x%08x\n",
		  read_buf);

	ret = smsc95xx_read_reg(dev, HW_CFG, &read_buf);
	if (ret < 0)
		return ret;

	netif_dbg(dev, ifup, dev->net, "Read Value from HW_CFG: 0x%08x\n",
		  read_buf);

	if (turbo_mode)
		read_buf |= (HW_CFG_MEF_ | HW_CFG_BCE_);

	read_buf &= ~HW_CFG_RXDOFF_;

	/* set Rx data offset=2, Make IP header aligns on word boundary. */
	read_buf |= NET_IP_ALIGN << 9;

	ret = smsc95xx_write_reg(dev, HW_CFG, read_buf);
	if (ret < 0)
		return ret;

	ret = smsc95xx_read_reg(dev, HW_CFG, &read_buf);
	if (ret < 0)
		return ret;

	netif_dbg(dev, ifup, dev->net,
		  "Read Value from HW_CFG after writing: 0x%08x\n", read_buf);

	ret = smsc95xx_write_reg(dev, INT_STS, INT_STS_CLEAR_ALL_);
	if (ret < 0)
		return ret;

	ret = smsc95xx_read_reg(dev, ID_REV, &read_buf);
	if (ret < 0)
		return ret;
	netif_dbg(dev, ifup, dev->net, "ID_REV = 0x%08x\n", read_buf);

	/* Configure GPIO pins as LED outputs */
	write_buf = LED_GPIO_CFG_SPD_LED | LED_GPIO_CFG_LNK_LED |
		LED_GPIO_CFG_FDX_LED;
	ret = smsc95xx_write_reg(dev, LED_GPIO_CFG, write_buf);
	if (ret < 0)
		return ret;

	/* Init Tx */
	ret = smsc95xx_write_reg(dev, FLOW, 0);
	if (ret < 0)
		return ret;

	ret = smsc95xx_write_reg(dev, AFC_CFG, AFC_CFG_DEFAULT);
	if (ret < 0)
		return ret;

	/* Don't need mac_cr_lock during initialisation */
	ret = smsc95xx_read_reg(dev, MAC_CR, &pdata->mac_cr);
	if (ret < 0)
		return ret;

	/* Init Rx */
	/* Set Vlan */
	ret = smsc95xx_write_reg(dev, VLAN1, (u32)ETH_P_8021Q);
	if (ret < 0)
		return ret;

	/* Enable or disable checksum offload engines */
	ret = smsc95xx_set_features(dev->net, dev->net->features);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to set checksum offload features\n");
		return ret;
	}

	smsc95xx_set_multicast(dev->net);

	ret = smsc95xx_read_reg(dev, INT_EP_CTL, &read_buf);
	if (ret < 0)
		return ret;

	/* enable PHY interrupts */
	read_buf |= INT_EP_CTL_PHY_INT_;

	ret = smsc95xx_write_reg(dev, INT_EP_CTL, read_buf);
	if (ret < 0)
		return ret;

	ret = smsc95xx_start_tx_path(dev);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to start TX path\n");
		return ret;
	}

	ret = smsc95xx_start_rx_path(dev, 0);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to start RX path\n");
		return ret;
	}

	netif_dbg(dev, ifup, dev->net, "smsc95xx_reset, return 0\n");
	return 0;
}

static const struct net_device_ops smsc95xx_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
	.ndo_start_xmit		= usbnet_start_xmit,
	.ndo_tx_timeout		= usbnet_tx_timeout,
	.ndo_change_mtu		= usbnet_change_mtu,
	.ndo_get_stats64	= dev_get_tstats64,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_eth_ioctl		= smsc95xx_ioctl,
	.ndo_set_rx_mode	= smsc95xx_set_multicast,
	.ndo_set_features	= smsc95xx_set_features,
};

static void smsc95xx_handle_link_change(struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);

	phy_print_status(net->phydev);
	usbnet_defer_kevent(dev, EVENT_LINK_CHANGE);
}

static int smsc95xx_bind(struct usbnet *dev, struct usb_interface *intf)
{
	struct smsc95xx_priv *pdata;
	bool is_internal_phy;
	u32 val;
	int ret;

	printk(KERN_INFO SMSC_CHIPNAME " v" SMSC_DRIVER_VERSION "\n");

	ret = usbnet_get_endpoints(dev, intf);
	if (ret < 0) {
		netdev_warn(dev->net, "usbnet_get_endpoints failed: %d\n", ret);
		return ret;
	}

	pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	dev->driver_priv = pdata;

	spin_lock_init(&pdata->mac_cr_lock);

	/* LAN95xx devices do not alter the computed checksum of 0 to 0xffff.
	 * RFC 2460, ipv6 UDP calculated checksum yields a result of zero must
	 * be changed to 0xffff. RFC 768, ipv4 UDP computed checksum is zero,
	 * it is transmitted as all ones. The zero transmitted checksum means
	 * transmitter generated no checksum. Hence, enable csum offload only
	 * for ipv4 packets.
	 */
	if (DEFAULT_TX_CSUM_ENABLE)
		dev->net->features |= NETIF_F_IP_CSUM;
	if (DEFAULT_RX_CSUM_ENABLE)
		dev->net->features |= NETIF_F_RXCSUM;

	dev->net->hw_features = NETIF_F_IP_CSUM | NETIF_F_RXCSUM;
	set_bit(EVENT_NO_IP_ALIGN, &dev->flags);

	smsc95xx_init_mac_address(dev);

	/* Init all registers */
	ret = smsc95xx_reset(dev);
	if (ret)
		goto free_pdata;

	pdata->mdiobus = mdiobus_alloc();
	if (!pdata->mdiobus) {
		ret = -ENOMEM;
		goto free_pdata;
	}

	ret = smsc95xx_read_reg(dev, HW_CFG, &val);
	if (ret < 0)
		goto free_mdio;

	is_internal_phy = !(val & HW_CFG_PSEL_);
	if (is_internal_phy)
		pdata->mdiobus->phy_mask = ~(1u << SMSC95XX_INTERNAL_PHY_ID);

	pdata->mdiobus->priv = dev;
	pdata->mdiobus->read = smsc95xx_mdiobus_read;
	pdata->mdiobus->write = smsc95xx_mdiobus_write;
	pdata->mdiobus->name = "smsc95xx-mdiobus";
	pdata->mdiobus->parent = &dev->udev->dev;

	snprintf(pdata->mdiobus->id, ARRAY_SIZE(pdata->mdiobus->id),
		 "usb-%03d:%03d", dev->udev->bus->busnum, dev->udev->devnum);

	ret = mdiobus_register(pdata->mdiobus);
	if (ret) {
		netdev_err(dev->net, "Could not register MDIO bus\n");
		goto free_mdio;
	}

	pdata->phydev = phy_find_first(pdata->mdiobus);
	if (!pdata->phydev) {
		netdev_err(dev->net, "no PHY found\n");
		ret = -ENODEV;
		goto unregister_mdio;
	}

	pdata->phydev->is_internal = is_internal_phy;

	/* detect device revision as different features may be available */
	ret = smsc95xx_read_reg(dev, ID_REV, &val);
	if (ret < 0)
		goto unregister_mdio;

	val >>= 16;
	if ((val == ID_REV_CHIP_ID_9500A_) || (val == ID_REV_CHIP_ID_9530_) ||
	    (val == ID_REV_CHIP_ID_89530_) || (val == ID_REV_CHIP_ID_9730_))
		pdata->features = (FEATURE_8_WAKEUP_FILTERS |
			FEATURE_PHY_NLP_CROSSOVER |
			FEATURE_REMOTE_WAKEUP);
	else if (val == ID_REV_CHIP_ID_9512_)
		pdata->features = FEATURE_8_WAKEUP_FILTERS;

	dev->net->netdev_ops = &smsc95xx_netdev_ops;
	dev->net->ethtool_ops = &smsc95xx_ethtool_ops;
	dev->net->flags |= IFF_MULTICAST;
	dev->net->hard_header_len += SMSC95XX_TX_OVERHEAD_CSUM;
	dev->net->min_mtu = ETH_MIN_MTU;
	dev->net->max_mtu = ETH_DATA_LEN;
	dev->hard_mtu = dev->net->mtu + dev->net->hard_header_len;

	ret = phy_connect_direct(dev->net, pdata->phydev,
				 &smsc95xx_handle_link_change,
				 PHY_INTERFACE_MODE_MII);
	if (ret) {
		netdev_err(dev->net, "can't attach PHY to %s\n", pdata->mdiobus->id);
		goto unregister_mdio;
	}

	phy_attached_info(dev->net->phydev);

	return 0;

unregister_mdio:
	mdiobus_unregister(pdata->mdiobus);

free_mdio:
	mdiobus_free(pdata->mdiobus);

free_pdata:
	kfree(pdata);
	return ret;
}

static void smsc95xx_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	struct smsc95xx_priv *pdata = dev->driver_priv;

	phy_disconnect(dev->net->phydev);
	mdiobus_unregister(pdata->mdiobus);
	mdiobus_free(pdata->mdiobus);
	netif_dbg(dev, ifdown, dev->net, "free pdata\n");
	kfree(pdata);
}

static int smsc95xx_start_phy(struct usbnet *dev)
{
	phy_start(dev->net->phydev);

	return 0;
}

static int smsc95xx_stop(struct usbnet *dev)
{
	if (dev->net->phydev)
		phy_stop(dev->net->phydev);

	return 0;
}

static u32 smsc_crc(const u8 *buffer, size_t len, int filter)
{
	u32 crc = bitrev16(crc16(0xFFFF, buffer, len));
	return crc << ((filter % 2) * 16);
}

static int smsc95xx_enable_phy_wakeup_interrupts(struct usbnet *dev, u16 mask)
{
	int ret;

	netdev_dbg(dev->net, "enabling PHY wakeup interrupts\n");

	/* read to clear */
	ret = smsc95xx_mdio_read_nopm(dev, PHY_INT_SRC);
	if (ret < 0)
		return ret;

	/* enable interrupt source */
	ret = smsc95xx_mdio_read_nopm(dev, PHY_INT_MASK);
	if (ret < 0)
		return ret;

	ret |= mask;

	smsc95xx_mdio_write_nopm(dev, PHY_INT_MASK, ret);

	return 0;
}

static int smsc95xx_link_ok_nopm(struct usbnet *dev)
{
	int ret;

	/* first, a dummy read, needed to latch some MII phys */
	ret = smsc95xx_mdio_read_nopm(dev, MII_BMSR);
	if (ret < 0)
		return ret;

	ret = smsc95xx_mdio_read_nopm(dev, MII_BMSR);
	if (ret < 0)
		return ret;

	return !!(ret & BMSR_LSTATUS);
}

static int smsc95xx_enter_suspend0(struct usbnet *dev)
{
	struct smsc95xx_priv *pdata = dev->driver_priv;
	u32 val;
	int ret;

	ret = smsc95xx_read_reg_nopm(dev, PM_CTRL, &val);
	if (ret < 0)
		return ret;

	val &= (~(PM_CTL_SUS_MODE_ | PM_CTL_WUPS_ | PM_CTL_PHY_RST_));
	val |= PM_CTL_SUS_MODE_0;

	ret = smsc95xx_write_reg_nopm(dev, PM_CTRL, val);
	if (ret < 0)
		return ret;

	/* clear wol status */
	val &= ~PM_CTL_WUPS_;
	val |= PM_CTL_WUPS_WOL_;

	/* enable energy detection */
	if (pdata->wolopts & WAKE_PHY)
		val |= PM_CTL_WUPS_ED_;

	ret = smsc95xx_write_reg_nopm(dev, PM_CTRL, val);
	if (ret < 0)
		return ret;

	/* read back PM_CTRL */
	ret = smsc95xx_read_reg_nopm(dev, PM_CTRL, &val);
	if (ret < 0)
		return ret;

	pdata->suspend_flags |= SUSPEND_SUSPEND0;

	return 0;
}

static int smsc95xx_enter_suspend1(struct usbnet *dev)
{
	struct smsc95xx_priv *pdata = dev->driver_priv;
	u32 val;
	int ret;

	/* reconfigure link pulse detection timing for
	 * compatibility with non-standard link partners
	 */
	if (pdata->features & FEATURE_PHY_NLP_CROSSOVER)
		smsc95xx_mdio_write_nopm(dev, PHY_EDPD_CONFIG,
					 PHY_EDPD_CONFIG_DEFAULT);

	/* enable energy detect power-down mode */
	ret = smsc95xx_mdio_read_nopm(dev, PHY_MODE_CTRL_STS);
	if (ret < 0)
		return ret;

	ret |= MODE_CTRL_STS_EDPWRDOWN_;

	smsc95xx_mdio_write_nopm(dev, PHY_MODE_CTRL_STS, ret);

	/* enter SUSPEND1 mode */
	ret = smsc95xx_read_reg_nopm(dev, PM_CTRL, &val);
	if (ret < 0)
		return ret;

	val &= ~(PM_CTL_SUS_MODE_ | PM_CTL_WUPS_ | PM_CTL_PHY_RST_);
	val |= PM_CTL_SUS_MODE_1;

	ret = smsc95xx_write_reg_nopm(dev, PM_CTRL, val);
	if (ret < 0)
		return ret;

	/* clear wol status, enable energy detection */
	val &= ~PM_CTL_WUPS_;
	val |= (PM_CTL_WUPS_ED_ | PM_CTL_ED_EN_);

	ret = smsc95xx_write_reg_nopm(dev, PM_CTRL, val);
	if (ret < 0)
		return ret;

	pdata->suspend_flags |= SUSPEND_SUSPEND1;

	return 0;
}

static int smsc95xx_enter_suspend2(struct usbnet *dev)
{
	struct smsc95xx_priv *pdata = dev->driver_priv;
	u32 val;
	int ret;

	ret = smsc95xx_read_reg_nopm(dev, PM_CTRL, &val);
	if (ret < 0)
		return ret;

	val &= ~(PM_CTL_SUS_MODE_ | PM_CTL_WUPS_ | PM_CTL_PHY_RST_);
	val |= PM_CTL_SUS_MODE_2;

	ret = smsc95xx_write_reg_nopm(dev, PM_CTRL, val);
	if (ret < 0)
		return ret;

	pdata->suspend_flags |= SUSPEND_SUSPEND2;

	return 0;
}

static int smsc95xx_enter_suspend3(struct usbnet *dev)
{
	struct smsc95xx_priv *pdata = dev->driver_priv;
	u32 val;
	int ret;

	ret = smsc95xx_read_reg_nopm(dev, RX_FIFO_INF, &val);
	if (ret < 0)
		return ret;

	if (val & RX_FIFO_INF_USED_) {
		netdev_info(dev->net, "rx fifo not empty in autosuspend\n");
		return -EBUSY;
	}

	ret = smsc95xx_read_reg_nopm(dev, PM_CTRL, &val);
	if (ret < 0)
		return ret;

	val &= ~(PM_CTL_SUS_MODE_ | PM_CTL_WUPS_ | PM_CTL_PHY_RST_);
	val |= PM_CTL_SUS_MODE_3 | PM_CTL_RES_CLR_WKP_STS;

	ret = smsc95xx_write_reg_nopm(dev, PM_CTRL, val);
	if (ret < 0)
		return ret;

	/* clear wol status */
	val &= ~PM_CTL_WUPS_;
	val |= PM_CTL_WUPS_WOL_;

	ret = smsc95xx_write_reg_nopm(dev, PM_CTRL, val);
	if (ret < 0)
		return ret;

	pdata->suspend_flags |= SUSPEND_SUSPEND3;

	return 0;
}

static int smsc95xx_autosuspend(struct usbnet *dev, u32 link_up)
{
	struct smsc95xx_priv *pdata = dev->driver_priv;
	int ret;

	if (!netif_running(dev->net)) {
		/* interface is ifconfig down so fully power down hw */
		netdev_dbg(dev->net, "autosuspend entering SUSPEND2\n");
		return smsc95xx_enter_suspend2(dev);
	}

	if (!link_up) {
		/* link is down so enter EDPD mode, but only if device can
		 * reliably resume from it.  This check should be redundant
		 * as current FEATURE_REMOTE_WAKEUP parts also support
		 * FEATURE_PHY_NLP_CROSSOVER but it's included for clarity */
		if (!(pdata->features & FEATURE_PHY_NLP_CROSSOVER)) {
			netdev_warn(dev->net, "EDPD not supported\n");
			return -EBUSY;
		}

		netdev_dbg(dev->net, "autosuspend entering SUSPEND1\n");

		/* enable PHY wakeup events for if cable is attached */
		ret = smsc95xx_enable_phy_wakeup_interrupts(dev,
			PHY_INT_MASK_ANEG_COMP_);
		if (ret < 0) {
			netdev_warn(dev->net, "error enabling PHY wakeup ints\n");
			return ret;
		}

		netdev_info(dev->net, "entering SUSPEND1 mode\n");
		return smsc95xx_enter_suspend1(dev);
	}

	/* enable PHY wakeup events so we remote wakeup if cable is pulled */
	ret = smsc95xx_enable_phy_wakeup_interrupts(dev,
		PHY_INT_MASK_LINK_DOWN_);
	if (ret < 0) {
		netdev_warn(dev->net, "error enabling PHY wakeup ints\n");
		return ret;
	}

	netdev_dbg(dev->net, "autosuspend entering SUSPEND3\n");
	return smsc95xx_enter_suspend3(dev);
}

static int smsc95xx_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usbnet *dev = usb_get_intfdata(intf);
	struct smsc95xx_priv *pdata = dev->driver_priv;
	u32 val, link_up;
	int ret;

	ret = usbnet_suspend(intf, message);
	if (ret < 0) {
		netdev_warn(dev->net, "usbnet_suspend error\n");
		return ret;
	}

	if (pdata->suspend_flags) {
		netdev_warn(dev->net, "error during last resume\n");
		pdata->suspend_flags = 0;
	}

	/* determine if link is up using only _nopm functions */
	link_up = smsc95xx_link_ok_nopm(dev);

	if (message.event == PM_EVENT_AUTO_SUSPEND &&
	    (pdata->features & FEATURE_REMOTE_WAKEUP)) {
		ret = smsc95xx_autosuspend(dev, link_up);
		goto done;
	}

	/* if we get this far we're not autosuspending */
	/* if no wol options set, or if link is down and we're not waking on
	 * PHY activity, enter lowest power SUSPEND2 mode
	 */
	if (!(pdata->wolopts & SUPPORTED_WAKE) ||
		!(link_up || (pdata->wolopts & WAKE_PHY))) {
		netdev_info(dev->net, "entering SUSPEND2 mode\n");

		/* disable energy detect (link up) & wake up events */
		ret = smsc95xx_read_reg_nopm(dev, WUCSR, &val);
		if (ret < 0)
			goto done;

		val &= ~(WUCSR_MPEN_ | WUCSR_WAKE_EN_);

		ret = smsc95xx_write_reg_nopm(dev, WUCSR, val);
		if (ret < 0)
			goto done;

		ret = smsc95xx_read_reg_nopm(dev, PM_CTRL, &val);
		if (ret < 0)
			goto done;

		val &= ~(PM_CTL_ED_EN_ | PM_CTL_WOL_EN_);

		ret = smsc95xx_write_reg_nopm(dev, PM_CTRL, val);
		if (ret < 0)
			goto done;

		ret = smsc95xx_enter_suspend2(dev);
		goto done;
	}

	if (pdata->wolopts & WAKE_PHY) {
		ret = smsc95xx_enable_phy_wakeup_interrupts(dev,
			(PHY_INT_MASK_ANEG_COMP_ | PHY_INT_MASK_LINK_DOWN_));
		if (ret < 0) {
			netdev_warn(dev->net, "error enabling PHY wakeup ints\n");
			goto done;
		}

		/* if link is down then configure EDPD and enter SUSPEND1,
		 * otherwise enter SUSPEND0 below
		 */
		if (!link_up) {
			netdev_info(dev->net, "entering SUSPEND1 mode\n");
			ret = smsc95xx_enter_suspend1(dev);
			goto done;
		}
	}

	if (pdata->wolopts & (WAKE_BCAST | WAKE_MCAST | WAKE_ARP | WAKE_UCAST)) {
		u32 *filter_mask = kcalloc(32, sizeof(u32), GFP_KERNEL);
		u32 command[2];
		u32 offset[2];
		u32 crc[4];
		int wuff_filter_count =
			(pdata->features & FEATURE_8_WAKEUP_FILTERS) ?
			LAN9500A_WUFF_NUM : LAN9500_WUFF_NUM;
		int i, filter = 0;

		if (!filter_mask) {
			netdev_warn(dev->net, "Unable to allocate filter_mask\n");
			ret = -ENOMEM;
			goto done;
		}

		memset(command, 0, sizeof(command));
		memset(offset, 0, sizeof(offset));
		memset(crc, 0, sizeof(crc));

		if (pdata->wolopts & WAKE_BCAST) {
			const u8 bcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
			netdev_info(dev->net, "enabling broadcast detection\n");
			filter_mask[filter * 4] = 0x003F;
			filter_mask[filter * 4 + 1] = 0x00;
			filter_mask[filter * 4 + 2] = 0x00;
			filter_mask[filter * 4 + 3] = 0x00;
			command[filter/4] |= 0x05UL << ((filter % 4) * 8);
			offset[filter/4] |= 0x00 << ((filter % 4) * 8);
			crc[filter/2] |= smsc_crc(bcast, 6, filter);
			filter++;
		}

		if (pdata->wolopts & WAKE_MCAST) {
			const u8 mcast[] = {0x01, 0x00, 0x5E};
			netdev_info(dev->net, "enabling multicast detection\n");
			filter_mask[filter * 4] = 0x0007;
			filter_mask[filter * 4 + 1] = 0x00;
			filter_mask[filter * 4 + 2] = 0x00;
			filter_mask[filter * 4 + 3] = 0x00;
			command[filter/4] |= 0x09UL << ((filter % 4) * 8);
			offset[filter/4] |= 0x00  << ((filter % 4) * 8);
			crc[filter/2] |= smsc_crc(mcast, 3, filter);
			filter++;
		}

		if (pdata->wolopts & WAKE_ARP) {
			const u8 arp[] = {0x08, 0x06};
			netdev_info(dev->net, "enabling ARP detection\n");
			filter_mask[filter * 4] = 0x0003;
			filter_mask[filter * 4 + 1] = 0x00;
			filter_mask[filter * 4 + 2] = 0x00;
			filter_mask[filter * 4 + 3] = 0x00;
			command[filter/4] |= 0x05UL << ((filter % 4) * 8);
			offset[filter/4] |= 0x0C << ((filter % 4) * 8);
			crc[filter/2] |= smsc_crc(arp, 2, filter);
			filter++;
		}

		if (pdata->wolopts & WAKE_UCAST) {
			netdev_info(dev->net, "enabling unicast detection\n");
			filter_mask[filter * 4] = 0x003F;
			filter_mask[filter * 4 + 1] = 0x00;
			filter_mask[filter * 4 + 2] = 0x00;
			filter_mask[filter * 4 + 3] = 0x00;
			command[filter/4] |= 0x01UL << ((filter % 4) * 8);
			offset[filter/4] |= 0x00 << ((filter % 4) * 8);
			crc[filter/2] |= smsc_crc(dev->net->dev_addr, ETH_ALEN, filter);
			filter++;
		}

		for (i = 0; i < (wuff_filter_count * 4); i++) {
			ret = smsc95xx_write_reg_nopm(dev, WUFF, filter_mask[i]);
			if (ret < 0) {
				kfree(filter_mask);
				goto done;
			}
		}
		kfree(filter_mask);

		for (i = 0; i < (wuff_filter_count / 4); i++) {
			ret = smsc95xx_write_reg_nopm(dev, WUFF, command[i]);
			if (ret < 0)
				goto done;
		}

		for (i = 0; i < (wuff_filter_count / 4); i++) {
			ret = smsc95xx_write_reg_nopm(dev, WUFF, offset[i]);
			if (ret < 0)
				goto done;
		}

		for (i = 0; i < (wuff_filter_count / 2); i++) {
			ret = smsc95xx_write_reg_nopm(dev, WUFF, crc[i]);
			if (ret < 0)
				goto done;
		}

		/* clear any pending pattern match packet status */
		ret = smsc95xx_read_reg_nopm(dev, WUCSR, &val);
		if (ret < 0)
			goto done;

		val |= WUCSR_WUFR_;

		ret = smsc95xx_write_reg_nopm(dev, WUCSR, val);
		if (ret < 0)
			goto done;
	}

	if (pdata->wolopts & WAKE_MAGIC) {
		/* clear any pending magic packet status */
		ret = smsc95xx_read_reg_nopm(dev, WUCSR, &val);
		if (ret < 0)
			goto done;

		val |= WUCSR_MPR_;

		ret = smsc95xx_write_reg_nopm(dev, WUCSR, val);
		if (ret < 0)
			goto done;
	}

	/* enable/disable wakeup sources */
	ret = smsc95xx_read_reg_nopm(dev, WUCSR, &val);
	if (ret < 0)
		goto done;

	if (pdata->wolopts & (WAKE_BCAST | WAKE_MCAST | WAKE_ARP | WAKE_UCAST)) {
		netdev_info(dev->net, "enabling pattern match wakeup\n");
		val |= WUCSR_WAKE_EN_;
	} else {
		netdev_info(dev->net, "disabling pattern match wakeup\n");
		val &= ~WUCSR_WAKE_EN_;
	}

	if (pdata->wolopts & WAKE_MAGIC) {
		netdev_info(dev->net, "enabling magic packet wakeup\n");
		val |= WUCSR_MPEN_;
	} else {
		netdev_info(dev->net, "disabling magic packet wakeup\n");
		val &= ~WUCSR_MPEN_;
	}

	ret = smsc95xx_write_reg_nopm(dev, WUCSR, val);
	if (ret < 0)
		goto done;

	/* enable wol wakeup source */
	ret = smsc95xx_read_reg_nopm(dev, PM_CTRL, &val);
	if (ret < 0)
		goto done;

	val |= PM_CTL_WOL_EN_;

	/* phy energy detect wakeup source */
	if (pdata->wolopts & WAKE_PHY)
		val |= PM_CTL_ED_EN_;

	ret = smsc95xx_write_reg_nopm(dev, PM_CTRL, val);
	if (ret < 0)
		goto done;

	/* enable receiver to enable frame reception */
	smsc95xx_start_rx_path(dev, 1);

	/* some wol options are enabled, so enter SUSPEND0 */
	netdev_info(dev->net, "entering SUSPEND0 mode\n");
	ret = smsc95xx_enter_suspend0(dev);

done:
	/*
	 * TODO: resume() might need to handle the suspend failure
	 * in system sleep
	 */
	if (ret && PMSG_IS_AUTO(message))
		usbnet_resume(intf);

	return ret;
}

static int smsc95xx_resume(struct usb_interface *intf)
{
	struct usbnet *dev = usb_get_intfdata(intf);
	struct smsc95xx_priv *pdata;
	u8 suspend_flags;
	int ret;
	u32 val;

	BUG_ON(!dev);
	pdata = dev->driver_priv;
	suspend_flags = pdata->suspend_flags;

	netdev_dbg(dev->net, "resume suspend_flags=0x%02x\n", suspend_flags);

	/* do this first to ensure it's cleared even in error case */
	pdata->suspend_flags = 0;

	if (suspend_flags & SUSPEND_ALLMODES) {
		/* clear wake-up sources */
		ret = smsc95xx_read_reg_nopm(dev, WUCSR, &val);
		if (ret < 0)
			return ret;

		val &= ~(WUCSR_WAKE_EN_ | WUCSR_MPEN_);

		ret = smsc95xx_write_reg_nopm(dev, WUCSR, val);
		if (ret < 0)
			return ret;

		/* clear wake-up status */
		ret = smsc95xx_read_reg_nopm(dev, PM_CTRL, &val);
		if (ret < 0)
			return ret;

		val &= ~PM_CTL_WOL_EN_;
		val |= PM_CTL_WUPS_;

		ret = smsc95xx_write_reg_nopm(dev, PM_CTRL, val);
		if (ret < 0)
			return ret;
	}

	ret = usbnet_resume(intf);
	if (ret < 0)
		netdev_warn(dev->net, "usbnet_resume error\n");

	phy_init_hw(pdata->phydev);
	return ret;
}

static int smsc95xx_reset_resume(struct usb_interface *intf)
{
	struct usbnet *dev = usb_get_intfdata(intf);
	int ret;

	ret = smsc95xx_reset(dev);
	if (ret < 0)
		return ret;

	return smsc95xx_resume(intf);
}

static void smsc95xx_rx_csum_offload(struct sk_buff *skb)
{
	skb->csum = *(u16 *)(skb_tail_pointer(skb) - 2);
	skb->ip_summed = CHECKSUM_COMPLETE;
	skb_trim(skb, skb->len - 2);
}

static int smsc95xx_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	/* This check is no longer done by usbnet */
	if (skb->len < dev->net->hard_header_len)
		return 0;

	while (skb->len > 0) {
		u32 header, align_count;
		struct sk_buff *ax_skb;
		unsigned char *packet;
		u16 size;

		header = get_unaligned_le32(skb->data);
		skb_pull(skb, 4 + NET_IP_ALIGN);
		packet = skb->data;

		/* get the packet length */
		size = (u16)((header & RX_STS_FL_) >> 16);
		align_count = (4 - ((size + NET_IP_ALIGN) % 4)) % 4;

		if (unlikely(header & RX_STS_ES_)) {
			netif_dbg(dev, rx_err, dev->net,
				  "Error header=0x%08x\n", header);
			dev->net->stats.rx_errors++;
			dev->net->stats.rx_dropped++;

			if (header & RX_STS_CRC_) {
				dev->net->stats.rx_crc_errors++;
			} else {
				if (header & (RX_STS_TL_ | RX_STS_RF_))
					dev->net->stats.rx_frame_errors++;

				if ((header & RX_STS_LE_) &&
					(!(header & RX_STS_FT_)))
					dev->net->stats.rx_length_errors++;
			}
		} else {
			/* ETH_FRAME_LEN + 4(CRC) + 2(COE) + 4(Vlan) */
			if (unlikely(size > (ETH_FRAME_LEN + 12))) {
				netif_dbg(dev, rx_err, dev->net,
					  "size err header=0x%08x\n", header);
				return 0;
			}

			/* last frame in this batch */
			if (skb->len == size) {
				if (dev->net->features & NETIF_F_RXCSUM)
					smsc95xx_rx_csum_offload(skb);
				skb_trim(skb, skb->len - 4); /* remove fcs */
				skb->truesize = size + sizeof(struct sk_buff);

				return 1;
			}

			ax_skb = skb_clone(skb, GFP_ATOMIC);
			if (unlikely(!ax_skb)) {
				netdev_warn(dev->net, "Error allocating skb\n");
				return 0;
			}

			ax_skb->len = size;
			ax_skb->data = packet;
			skb_set_tail_pointer(ax_skb, size);

			if (dev->net->features & NETIF_F_RXCSUM)
				smsc95xx_rx_csum_offload(ax_skb);
			skb_trim(ax_skb, ax_skb->len - 4); /* remove fcs */
			ax_skb->truesize = size + sizeof(struct sk_buff);

			usbnet_skb_return(dev, ax_skb);
		}

		skb_pull(skb, size);

		/* padding bytes before the next frame starts */
		if (skb->len)
			skb_pull(skb, align_count);
	}

	return 1;
}

static u32 smsc95xx_calc_csum_preamble(struct sk_buff *skb)
{
	u16 low_16 = (u16)skb_checksum_start_offset(skb);
	u16 high_16 = low_16 + skb->csum_offset;
	return (high_16 << 16) | low_16;
}

/* The TX CSUM won't work if the checksum lies in the last 4 bytes of the
 * transmission. This is fairly unlikely, only seems to trigger with some
 * short TCP ACK packets sent.
 *
 * Note, this calculation should probably check for the alignment of the
 * data as well, but a straight check for csum being in the last four bytes
 * of the packet should be ok for now.
 */
static bool smsc95xx_can_tx_checksum(struct sk_buff *skb)
{
       unsigned int len = skb->len - skb_checksum_start_offset(skb);

       if (skb->len <= 45)
	       return false;
       return skb->csum_offset < (len - (4 + 1));
}

static struct sk_buff *smsc95xx_tx_fixup(struct usbnet *dev,
					 struct sk_buff *skb, gfp_t flags)
{
	bool csum = skb->ip_summed == CHECKSUM_PARTIAL;
	int overhead = csum ? SMSC95XX_TX_OVERHEAD_CSUM : SMSC95XX_TX_OVERHEAD;
	u32 tx_cmd_a, tx_cmd_b;
	void *ptr;

	/* We do not advertise SG, so skbs should be already linearized */
	BUG_ON(skb_shinfo(skb)->nr_frags);

	/* Make writable and expand header space by overhead if required */
	if (skb_cow_head(skb, overhead)) {
		/* Must deallocate here as returning NULL to indicate error
		 * means the skb won't be deallocated in the caller.
		 */
		dev_kfree_skb_any(skb);
		return NULL;
	}

	tx_cmd_b = (u32)skb->len;
	tx_cmd_a = tx_cmd_b | TX_CMD_A_FIRST_SEG_ | TX_CMD_A_LAST_SEG_;

	if (csum) {
		if (!smsc95xx_can_tx_checksum(skb)) {
			/* workaround - hardware tx checksum does not work
			 * properly with extremely small packets */
			long csstart = skb_checksum_start_offset(skb);
			__wsum calc = csum_partial(skb->data + csstart,
				skb->len - csstart, 0);
			*((__sum16 *)(skb->data + csstart
				+ skb->csum_offset)) = csum_fold(calc);

			csum = false;
		} else {
			u32 csum_preamble = smsc95xx_calc_csum_preamble(skb);
			ptr = skb_push(skb, 4);
			put_unaligned_le32(csum_preamble, ptr);

			tx_cmd_a += 4;
			tx_cmd_b += 4;
			tx_cmd_b |= TX_CMD_B_CSUM_ENABLE;
		}
	}

	ptr = skb_push(skb, 8);
	put_unaligned_le32(tx_cmd_a, ptr);
	put_unaligned_le32(tx_cmd_b, ptr+4);

	return skb;
}

static int smsc95xx_manage_power(struct usbnet *dev, int on)
{
	struct smsc95xx_priv *pdata = dev->driver_priv;

	dev->intf->needs_remote_wakeup = on;

	if (pdata->features & FEATURE_REMOTE_WAKEUP)
		return 0;

	/* this chip revision isn't capable of remote wakeup */
	netdev_info(dev->net, "hardware isn't capable of remote wakeup\n");

	if (on)
		usb_autopm_get_interface_no_resume(dev->intf);
	else
		usb_autopm_put_interface(dev->intf);

	return 0;
}

static const struct driver_info smsc95xx_info = {
	.description	= "smsc95xx USB 2.0 Ethernet",
	.bind		= smsc95xx_bind,
	.unbind		= smsc95xx_unbind,
	.link_reset	= smsc95xx_link_reset,
	.reset		= smsc95xx_reset,
	.check_connect	= smsc95xx_start_phy,
	.stop		= smsc95xx_stop,
	.rx_fixup	= smsc95xx_rx_fixup,
	.tx_fixup	= smsc95xx_tx_fixup,
	.status		= smsc95xx_status,
	.manage_power	= smsc95xx_manage_power,
	.flags		= FLAG_ETHER | FLAG_SEND_ZLP | FLAG_LINK_INTR,
};

static const struct usb_device_id products[] = {
	{
		/* SMSC9500 USB Ethernet Device */
		USB_DEVICE(0x0424, 0x9500),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC9505 USB Ethernet Device */
		USB_DEVICE(0x0424, 0x9505),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC9500A USB Ethernet Device */
		USB_DEVICE(0x0424, 0x9E00),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC9505A USB Ethernet Device */
		USB_DEVICE(0x0424, 0x9E01),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC9512/9514 USB Hub & Ethernet Device */
		USB_DEVICE(0x0424, 0xec00),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC9500 USB Ethernet Device (SAL10) */
		USB_DEVICE(0x0424, 0x9900),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC9505 USB Ethernet Device (SAL10) */
		USB_DEVICE(0x0424, 0x9901),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC9500A USB Ethernet Device (SAL10) */
		USB_DEVICE(0x0424, 0x9902),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC9505A USB Ethernet Device (SAL10) */
		USB_DEVICE(0x0424, 0x9903),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC9512/9514 USB Hub & Ethernet Device (SAL10) */
		USB_DEVICE(0x0424, 0x9904),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC9500A USB Ethernet Device (HAL) */
		USB_DEVICE(0x0424, 0x9905),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC9505A USB Ethernet Device (HAL) */
		USB_DEVICE(0x0424, 0x9906),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC9500 USB Ethernet Device (Alternate ID) */
		USB_DEVICE(0x0424, 0x9907),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC9500A USB Ethernet Device (Alternate ID) */
		USB_DEVICE(0x0424, 0x9908),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC9512/9514 USB Hub & Ethernet Device (Alternate ID) */
		USB_DEVICE(0x0424, 0x9909),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC LAN9530 USB Ethernet Device */
		USB_DEVICE(0x0424, 0x9530),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC LAN9730 USB Ethernet Device */
		USB_DEVICE(0x0424, 0x9730),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{
		/* SMSC LAN89530 USB Ethernet Device */
		USB_DEVICE(0x0424, 0x9E08),
		.driver_info = (unsigned long) &smsc95xx_info,
	},
	{ },		/* END */
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver smsc95xx_driver = {
	.name		= "smsc95xx",
	.id_table	= products,
	.probe		= usbnet_probe,
	.suspend	= smsc95xx_suspend,
	.resume		= smsc95xx_resume,
	.reset_resume	= smsc95xx_reset_resume,
	.disconnect	= usbnet_disconnect,
	.disable_hub_initiated_lpm = 1,
	.supports_autosuspend = 1,
};

module_usb_driver(smsc95xx_driver);

MODULE_AUTHOR("Nancy Lin");
MODULE_AUTHOR("Steve Glendinning <steve.glendinning@shawell.net>");
MODULE_DESCRIPTION("SMSC95XX USB 2.0 Ethernet Devices");
MODULE_LICENSE("GPL");
