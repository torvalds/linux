// SPDX-License-Identifier: GPL-2.0-or-later
 /***************************************************************************
 *
 * Copyright (C) 2007-2010 SMSC
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
#include "smsc75xx.h"

#define SMSC_CHIPNAME			"smsc75xx"
#define SMSC_DRIVER_VERSION		"1.0.0"
#define HS_USB_PKT_SIZE			(512)
#define FS_USB_PKT_SIZE			(64)
#define DEFAULT_HS_BURST_CAP_SIZE	(16 * 1024 + 5 * HS_USB_PKT_SIZE)
#define DEFAULT_FS_BURST_CAP_SIZE	(6 * 1024 + 33 * FS_USB_PKT_SIZE)
#define DEFAULT_BULK_IN_DELAY		(0x00002000)
#define MAX_SINGLE_PACKET_SIZE		(9000)
#define LAN75XX_EEPROM_MAGIC		(0x7500)
#define EEPROM_MAC_OFFSET		(0x01)
#define DEFAULT_TX_CSUM_ENABLE		(true)
#define DEFAULT_RX_CSUM_ENABLE		(true)
#define SMSC75XX_INTERNAL_PHY_ID	(1)
#define SMSC75XX_TX_OVERHEAD		(8)
#define MAX_RX_FIFO_SIZE		(20 * 1024)
#define MAX_TX_FIFO_SIZE		(12 * 1024)
#define USB_VENDOR_ID_SMSC		(0x0424)
#define USB_PRODUCT_ID_LAN7500		(0x7500)
#define USB_PRODUCT_ID_LAN7505		(0x7505)
#define RXW_PADDING			2
#define SUPPORTED_WAKE			(WAKE_PHY | WAKE_UCAST | WAKE_BCAST | \
					 WAKE_MCAST | WAKE_ARP | WAKE_MAGIC)

#define SUSPEND_SUSPEND0		(0x01)
#define SUSPEND_SUSPEND1		(0x02)
#define SUSPEND_SUSPEND2		(0x04)
#define SUSPEND_SUSPEND3		(0x08)
#define SUSPEND_ALLMODES		(SUSPEND_SUSPEND0 | SUSPEND_SUSPEND1 | \
					 SUSPEND_SUSPEND2 | SUSPEND_SUSPEND3)

struct smsc75xx_priv {
	struct usbnet *dev;
	u32 rfe_ctl;
	u32 wolopts;
	u32 multicast_hash_table[DP_SEL_VHF_HASH_LEN];
	struct mutex dataport_mutex;
	spinlock_t rfe_ctl_lock;
	struct work_struct set_multicast;
	u8 suspend_flags;
};

struct usb_context {
	struct usb_ctrlrequest req;
	struct usbnet *dev;
};

static bool turbo_mode = true;
module_param(turbo_mode, bool, 0644);
MODULE_PARM_DESC(turbo_mode, "Enable multiple frames per Rx transaction");

static int smsc75xx_link_ok_nopm(struct usbnet *dev);
static int smsc75xx_phy_gig_workaround(struct usbnet *dev);

static int __must_check __smsc75xx_read_reg(struct usbnet *dev, u32 index,
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

static int __must_check __smsc75xx_write_reg(struct usbnet *dev, u32 index,
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

static int __must_check smsc75xx_read_reg_nopm(struct usbnet *dev, u32 index,
					       u32 *data)
{
	return __smsc75xx_read_reg(dev, index, data, 1);
}

static int __must_check smsc75xx_write_reg_nopm(struct usbnet *dev, u32 index,
						u32 data)
{
	return __smsc75xx_write_reg(dev, index, data, 1);
}

static int __must_check smsc75xx_read_reg(struct usbnet *dev, u32 index,
					  u32 *data)
{
	return __smsc75xx_read_reg(dev, index, data, 0);
}

static int __must_check smsc75xx_write_reg(struct usbnet *dev, u32 index,
					   u32 data)
{
	return __smsc75xx_write_reg(dev, index, data, 0);
}

/* Loop until the read is completed with timeout
 * called with phy_mutex held */
static __must_check int __smsc75xx_phy_wait_not_busy(struct usbnet *dev,
						     int in_pm)
{
	unsigned long start_time = jiffies;
	u32 val;
	int ret;

	do {
		ret = __smsc75xx_read_reg(dev, MII_ACCESS, &val, in_pm);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading MII_ACCESS\n");
			return ret;
		}

		if (!(val & MII_ACCESS_BUSY))
			return 0;
	} while (!time_after(jiffies, start_time + HZ));

	return -EIO;
}

static int __smsc75xx_mdio_read(struct net_device *netdev, int phy_id, int idx,
				int in_pm)
{
	struct usbnet *dev = netdev_priv(netdev);
	u32 val, addr;
	int ret;

	mutex_lock(&dev->phy_mutex);

	/* confirm MII not busy */
	ret = __smsc75xx_phy_wait_not_busy(dev, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "MII is busy in smsc75xx_mdio_read\n");
		goto done;
	}

	/* set the address, index & direction (read from PHY) */
	phy_id &= dev->mii.phy_id_mask;
	idx &= dev->mii.reg_num_mask;
	addr = ((phy_id << MII_ACCESS_PHY_ADDR_SHIFT) & MII_ACCESS_PHY_ADDR)
		| ((idx << MII_ACCESS_REG_ADDR_SHIFT) & MII_ACCESS_REG_ADDR)
		| MII_ACCESS_READ | MII_ACCESS_BUSY;
	ret = __smsc75xx_write_reg(dev, MII_ACCESS, addr, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing MII_ACCESS\n");
		goto done;
	}

	ret = __smsc75xx_phy_wait_not_busy(dev, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "Timed out reading MII reg %02X\n", idx);
		goto done;
	}

	ret = __smsc75xx_read_reg(dev, MII_DATA, &val, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "Error reading MII_DATA\n");
		goto done;
	}

	ret = (u16)(val & 0xFFFF);

done:
	mutex_unlock(&dev->phy_mutex);
	return ret;
}

static void __smsc75xx_mdio_write(struct net_device *netdev, int phy_id,
				  int idx, int regval, int in_pm)
{
	struct usbnet *dev = netdev_priv(netdev);
	u32 val, addr;
	int ret;

	mutex_lock(&dev->phy_mutex);

	/* confirm MII not busy */
	ret = __smsc75xx_phy_wait_not_busy(dev, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "MII is busy in smsc75xx_mdio_write\n");
		goto done;
	}

	val = regval;
	ret = __smsc75xx_write_reg(dev, MII_DATA, val, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing MII_DATA\n");
		goto done;
	}

	/* set the address, index & direction (write to PHY) */
	phy_id &= dev->mii.phy_id_mask;
	idx &= dev->mii.reg_num_mask;
	addr = ((phy_id << MII_ACCESS_PHY_ADDR_SHIFT) & MII_ACCESS_PHY_ADDR)
		| ((idx << MII_ACCESS_REG_ADDR_SHIFT) & MII_ACCESS_REG_ADDR)
		| MII_ACCESS_WRITE | MII_ACCESS_BUSY;
	ret = __smsc75xx_write_reg(dev, MII_ACCESS, addr, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing MII_ACCESS\n");
		goto done;
	}

	ret = __smsc75xx_phy_wait_not_busy(dev, in_pm);
	if (ret < 0) {
		netdev_warn(dev->net, "Timed out writing MII reg %02X\n", idx);
		goto done;
	}

done:
	mutex_unlock(&dev->phy_mutex);
}

static int smsc75xx_mdio_read_nopm(struct net_device *netdev, int phy_id,
				   int idx)
{
	return __smsc75xx_mdio_read(netdev, phy_id, idx, 1);
}

static void smsc75xx_mdio_write_nopm(struct net_device *netdev, int phy_id,
				     int idx, int regval)
{
	__smsc75xx_mdio_write(netdev, phy_id, idx, regval, 1);
}

static int smsc75xx_mdio_read(struct net_device *netdev, int phy_id, int idx)
{
	return __smsc75xx_mdio_read(netdev, phy_id, idx, 0);
}

static void smsc75xx_mdio_write(struct net_device *netdev, int phy_id, int idx,
				int regval)
{
	__smsc75xx_mdio_write(netdev, phy_id, idx, regval, 0);
}

static int smsc75xx_wait_eeprom(struct usbnet *dev)
{
	unsigned long start_time = jiffies;
	u32 val;
	int ret;

	do {
		ret = smsc75xx_read_reg(dev, E2P_CMD, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading E2P_CMD\n");
			return ret;
		}

		if (!(val & E2P_CMD_BUSY) || (val & E2P_CMD_TIMEOUT))
			break;
		udelay(40);
	} while (!time_after(jiffies, start_time + HZ));

	if (val & (E2P_CMD_TIMEOUT | E2P_CMD_BUSY)) {
		netdev_warn(dev->net, "EEPROM read operation timeout\n");
		return -EIO;
	}

	return 0;
}

static int smsc75xx_eeprom_confirm_not_busy(struct usbnet *dev)
{
	unsigned long start_time = jiffies;
	u32 val;
	int ret;

	do {
		ret = smsc75xx_read_reg(dev, E2P_CMD, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading E2P_CMD\n");
			return ret;
		}

		if (!(val & E2P_CMD_BUSY))
			return 0;

		udelay(40);
	} while (!time_after(jiffies, start_time + HZ));

	netdev_warn(dev->net, "EEPROM is busy\n");
	return -EIO;
}

static int smsc75xx_read_eeprom(struct usbnet *dev, u32 offset, u32 length,
				u8 *data)
{
	u32 val;
	int i, ret;

	BUG_ON(!dev);
	BUG_ON(!data);

	ret = smsc75xx_eeprom_confirm_not_busy(dev);
	if (ret)
		return ret;

	for (i = 0; i < length; i++) {
		val = E2P_CMD_BUSY | E2P_CMD_READ | (offset & E2P_CMD_ADDR);
		ret = smsc75xx_write_reg(dev, E2P_CMD, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing E2P_CMD\n");
			return ret;
		}

		ret = smsc75xx_wait_eeprom(dev);
		if (ret < 0)
			return ret;

		ret = smsc75xx_read_reg(dev, E2P_DATA, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading E2P_DATA\n");
			return ret;
		}

		data[i] = val & 0xFF;
		offset++;
	}

	return 0;
}

static int smsc75xx_write_eeprom(struct usbnet *dev, u32 offset, u32 length,
				 u8 *data)
{
	u32 val;
	int i, ret;

	BUG_ON(!dev);
	BUG_ON(!data);

	ret = smsc75xx_eeprom_confirm_not_busy(dev);
	if (ret)
		return ret;

	/* Issue write/erase enable command */
	val = E2P_CMD_BUSY | E2P_CMD_EWEN;
	ret = smsc75xx_write_reg(dev, E2P_CMD, val);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing E2P_CMD\n");
		return ret;
	}

	ret = smsc75xx_wait_eeprom(dev);
	if (ret < 0)
		return ret;

	for (i = 0; i < length; i++) {

		/* Fill data register */
		val = data[i];
		ret = smsc75xx_write_reg(dev, E2P_DATA, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing E2P_DATA\n");
			return ret;
		}

		/* Send "write" command */
		val = E2P_CMD_BUSY | E2P_CMD_WRITE | (offset & E2P_CMD_ADDR);
		ret = smsc75xx_write_reg(dev, E2P_CMD, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing E2P_CMD\n");
			return ret;
		}

		ret = smsc75xx_wait_eeprom(dev);
		if (ret < 0)
			return ret;

		offset++;
	}

	return 0;
}

static int smsc75xx_dataport_wait_not_busy(struct usbnet *dev)
{
	int i, ret;

	for (i = 0; i < 100; i++) {
		u32 dp_sel;
		ret = smsc75xx_read_reg(dev, DP_SEL, &dp_sel);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading DP_SEL\n");
			return ret;
		}

		if (dp_sel & DP_SEL_DPRDY)
			return 0;

		udelay(40);
	}

	netdev_warn(dev->net, "smsc75xx_dataport_wait_not_busy timed out\n");

	return -EIO;
}

static int smsc75xx_dataport_write(struct usbnet *dev, u32 ram_select, u32 addr,
				   u32 length, u32 *buf)
{
	struct smsc75xx_priv *pdata = (struct smsc75xx_priv *)(dev->data[0]);
	u32 dp_sel;
	int i, ret;

	mutex_lock(&pdata->dataport_mutex);

	ret = smsc75xx_dataport_wait_not_busy(dev);
	if (ret < 0) {
		netdev_warn(dev->net, "smsc75xx_dataport_write busy on entry\n");
		goto done;
	}

	ret = smsc75xx_read_reg(dev, DP_SEL, &dp_sel);
	if (ret < 0) {
		netdev_warn(dev->net, "Error reading DP_SEL\n");
		goto done;
	}

	dp_sel &= ~DP_SEL_RSEL;
	dp_sel |= ram_select;
	ret = smsc75xx_write_reg(dev, DP_SEL, dp_sel);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing DP_SEL\n");
		goto done;
	}

	for (i = 0; i < length; i++) {
		ret = smsc75xx_write_reg(dev, DP_ADDR, addr + i);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing DP_ADDR\n");
			goto done;
		}

		ret = smsc75xx_write_reg(dev, DP_DATA, buf[i]);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing DP_DATA\n");
			goto done;
		}

		ret = smsc75xx_write_reg(dev, DP_CMD, DP_CMD_WRITE);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing DP_CMD\n");
			goto done;
		}

		ret = smsc75xx_dataport_wait_not_busy(dev);
		if (ret < 0) {
			netdev_warn(dev->net, "smsc75xx_dataport_write timeout\n");
			goto done;
		}
	}

done:
	mutex_unlock(&pdata->dataport_mutex);
	return ret;
}

/* returns hash bit number for given MAC address */
static u32 smsc75xx_hash(char addr[ETH_ALEN])
{
	return (ether_crc(ETH_ALEN, addr) >> 23) & 0x1ff;
}

static void smsc75xx_deferred_multicast_write(struct work_struct *param)
{
	struct smsc75xx_priv *pdata =
		container_of(param, struct smsc75xx_priv, set_multicast);
	struct usbnet *dev = pdata->dev;
	int ret;

	netif_dbg(dev, drv, dev->net, "deferred multicast write 0x%08x\n",
		  pdata->rfe_ctl);

	smsc75xx_dataport_write(dev, DP_SEL_VHF, DP_SEL_VHF_VLAN_LEN,
		DP_SEL_VHF_HASH_LEN, pdata->multicast_hash_table);

	ret = smsc75xx_write_reg(dev, RFE_CTL, pdata->rfe_ctl);
	if (ret < 0)
		netdev_warn(dev->net, "Error writing RFE_CRL\n");
}

static void smsc75xx_set_multicast(struct net_device *netdev)
{
	struct usbnet *dev = netdev_priv(netdev);
	struct smsc75xx_priv *pdata = (struct smsc75xx_priv *)(dev->data[0]);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&pdata->rfe_ctl_lock, flags);

	pdata->rfe_ctl &=
		~(RFE_CTL_AU | RFE_CTL_AM | RFE_CTL_DPF | RFE_CTL_MHF);
	pdata->rfe_ctl |= RFE_CTL_AB;

	for (i = 0; i < DP_SEL_VHF_HASH_LEN; i++)
		pdata->multicast_hash_table[i] = 0;

	if (dev->net->flags & IFF_PROMISC) {
		netif_dbg(dev, drv, dev->net, "promiscuous mode enabled\n");
		pdata->rfe_ctl |= RFE_CTL_AM | RFE_CTL_AU;
	} else if (dev->net->flags & IFF_ALLMULTI) {
		netif_dbg(dev, drv, dev->net, "receive all multicast enabled\n");
		pdata->rfe_ctl |= RFE_CTL_AM | RFE_CTL_DPF;
	} else if (!netdev_mc_empty(dev->net)) {
		struct netdev_hw_addr *ha;

		netif_dbg(dev, drv, dev->net, "receive multicast hash filter\n");

		pdata->rfe_ctl |= RFE_CTL_MHF | RFE_CTL_DPF;

		netdev_for_each_mc_addr(ha, netdev) {
			u32 bitnum = smsc75xx_hash(ha->addr);
			pdata->multicast_hash_table[bitnum / 32] |=
				(1 << (bitnum % 32));
		}
	} else {
		netif_dbg(dev, drv, dev->net, "receive own packets only\n");
		pdata->rfe_ctl |= RFE_CTL_DPF;
	}

	spin_unlock_irqrestore(&pdata->rfe_ctl_lock, flags);

	/* defer register writes to a sleepable context */
	schedule_work(&pdata->set_multicast);
}

static int smsc75xx_update_flowcontrol(struct usbnet *dev, u8 duplex,
					    u16 lcladv, u16 rmtadv)
{
	u32 flow = 0, fct_flow = 0;
	int ret;

	if (duplex == DUPLEX_FULL) {
		u8 cap = mii_resolve_flowctrl_fdx(lcladv, rmtadv);

		if (cap & FLOW_CTRL_TX) {
			flow = (FLOW_TX_FCEN | 0xFFFF);
			/* set fct_flow thresholds to 20% and 80% */
			fct_flow = (8 << 8) | 32;
		}

		if (cap & FLOW_CTRL_RX)
			flow |= FLOW_RX_FCEN;

		netif_dbg(dev, link, dev->net, "rx pause %s, tx pause %s\n",
			  (cap & FLOW_CTRL_RX ? "enabled" : "disabled"),
			  (cap & FLOW_CTRL_TX ? "enabled" : "disabled"));
	} else {
		netif_dbg(dev, link, dev->net, "half duplex\n");
	}

	ret = smsc75xx_write_reg(dev, FLOW, flow);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing FLOW\n");
		return ret;
	}

	ret = smsc75xx_write_reg(dev, FCT_FLOW, fct_flow);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing FCT_FLOW\n");
		return ret;
	}

	return 0;
}

static int smsc75xx_link_reset(struct usbnet *dev)
{
	struct mii_if_info *mii = &dev->mii;
	struct ethtool_cmd ecmd = { .cmd = ETHTOOL_GSET };
	u16 lcladv, rmtadv;
	int ret;

	/* write to clear phy interrupt status */
	smsc75xx_mdio_write(dev->net, mii->phy_id, PHY_INT_SRC,
		PHY_INT_SRC_CLEAR_ALL);

	ret = smsc75xx_write_reg(dev, INT_STS, INT_STS_CLEAR_ALL);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing INT_STS\n");
		return ret;
	}

	mii_check_media(mii, 1, 1);
	mii_ethtool_gset(&dev->mii, &ecmd);
	lcladv = smsc75xx_mdio_read(dev->net, mii->phy_id, MII_ADVERTISE);
	rmtadv = smsc75xx_mdio_read(dev->net, mii->phy_id, MII_LPA);

	netif_dbg(dev, link, dev->net, "speed: %u duplex: %d lcladv: %04x rmtadv: %04x\n",
		  ethtool_cmd_speed(&ecmd), ecmd.duplex, lcladv, rmtadv);

	return smsc75xx_update_flowcontrol(dev, ecmd.duplex, lcladv, rmtadv);
}

static void smsc75xx_status(struct usbnet *dev, struct urb *urb)
{
	u32 intdata;

	if (urb->actual_length != 4) {
		netdev_warn(dev->net, "unexpected urb length %d\n",
			    urb->actual_length);
		return;
	}

	memcpy(&intdata, urb->transfer_buffer, 4);
	le32_to_cpus(&intdata);

	netif_dbg(dev, link, dev->net, "intdata: 0x%08X\n", intdata);

	if (intdata & INT_ENP_PHY_INT)
		usbnet_defer_kevent(dev, EVENT_LINK_RESET);
	else
		netdev_warn(dev->net, "unexpected interrupt, intdata=0x%08X\n",
			    intdata);
}

static int smsc75xx_ethtool_get_eeprom_len(struct net_device *net)
{
	return MAX_EEPROM_SIZE;
}

static int smsc75xx_ethtool_get_eeprom(struct net_device *netdev,
				       struct ethtool_eeprom *ee, u8 *data)
{
	struct usbnet *dev = netdev_priv(netdev);

	ee->magic = LAN75XX_EEPROM_MAGIC;

	return smsc75xx_read_eeprom(dev, ee->offset, ee->len, data);
}

static int smsc75xx_ethtool_set_eeprom(struct net_device *netdev,
				       struct ethtool_eeprom *ee, u8 *data)
{
	struct usbnet *dev = netdev_priv(netdev);

	if (ee->magic != LAN75XX_EEPROM_MAGIC) {
		netdev_warn(dev->net, "EEPROM: magic value mismatch: 0x%x\n",
			    ee->magic);
		return -EINVAL;
	}

	return smsc75xx_write_eeprom(dev, ee->offset, ee->len, data);
}

static void smsc75xx_ethtool_get_wol(struct net_device *net,
				     struct ethtool_wolinfo *wolinfo)
{
	struct usbnet *dev = netdev_priv(net);
	struct smsc75xx_priv *pdata = (struct smsc75xx_priv *)(dev->data[0]);

	wolinfo->supported = SUPPORTED_WAKE;
	wolinfo->wolopts = pdata->wolopts;
}

static int smsc75xx_ethtool_set_wol(struct net_device *net,
				    struct ethtool_wolinfo *wolinfo)
{
	struct usbnet *dev = netdev_priv(net);
	struct smsc75xx_priv *pdata = (struct smsc75xx_priv *)(dev->data[0]);
	int ret;

	if (wolinfo->wolopts & ~SUPPORTED_WAKE)
		return -EINVAL;

	pdata->wolopts = wolinfo->wolopts & SUPPORTED_WAKE;

	ret = device_set_wakeup_enable(&dev->udev->dev, pdata->wolopts);
	if (ret < 0)
		netdev_warn(dev->net, "device_set_wakeup_enable error %d\n", ret);

	return ret;
}

static const struct ethtool_ops smsc75xx_ethtool_ops = {
	.get_link	= usbnet_get_link,
	.nway_reset	= usbnet_nway_reset,
	.get_drvinfo	= usbnet_get_drvinfo,
	.get_msglevel	= usbnet_get_msglevel,
	.set_msglevel	= usbnet_set_msglevel,
	.get_eeprom_len	= smsc75xx_ethtool_get_eeprom_len,
	.get_eeprom	= smsc75xx_ethtool_get_eeprom,
	.set_eeprom	= smsc75xx_ethtool_set_eeprom,
	.get_wol	= smsc75xx_ethtool_get_wol,
	.set_wol	= smsc75xx_ethtool_set_wol,
	.get_link_ksettings	= usbnet_get_link_ksettings,
	.set_link_ksettings	= usbnet_set_link_ksettings,
};

static int smsc75xx_ioctl(struct net_device *netdev, struct ifreq *rq, int cmd)
{
	struct usbnet *dev = netdev_priv(netdev);

	if (!netif_running(netdev))
		return -EINVAL;

	return generic_mii_ioctl(&dev->mii, if_mii(rq), cmd, NULL);
}

static void smsc75xx_init_mac_address(struct usbnet *dev)
{
	const u8 *mac_addr;

	/* maybe the boot loader passed the MAC address in devicetree */
	mac_addr = of_get_mac_address(dev->udev->dev.of_node);
	if (!IS_ERR(mac_addr)) {
		ether_addr_copy(dev->net->dev_addr, mac_addr);
		return;
	}

	/* try reading mac address from EEPROM */
	if (smsc75xx_read_eeprom(dev, EEPROM_MAC_OFFSET, ETH_ALEN,
			dev->net->dev_addr) == 0) {
		if (is_valid_ether_addr(dev->net->dev_addr)) {
			/* eeprom values are valid so use them */
			netif_dbg(dev, ifup, dev->net,
				  "MAC address read from EEPROM\n");
			return;
		}
	}

	/* no useful static MAC address found. generate a random one */
	eth_hw_addr_random(dev->net);
	netif_dbg(dev, ifup, dev->net, "MAC address set to eth_random_addr\n");
}

static int smsc75xx_set_mac_address(struct usbnet *dev)
{
	u32 addr_lo = dev->net->dev_addr[0] | dev->net->dev_addr[1] << 8 |
		dev->net->dev_addr[2] << 16 | dev->net->dev_addr[3] << 24;
	u32 addr_hi = dev->net->dev_addr[4] | dev->net->dev_addr[5] << 8;

	int ret = smsc75xx_write_reg(dev, RX_ADDRH, addr_hi);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write RX_ADDRH: %d\n", ret);
		return ret;
	}

	ret = smsc75xx_write_reg(dev, RX_ADDRL, addr_lo);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write RX_ADDRL: %d\n", ret);
		return ret;
	}

	addr_hi |= ADDR_FILTX_FB_VALID;
	ret = smsc75xx_write_reg(dev, ADDR_FILTX, addr_hi);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write ADDR_FILTX: %d\n", ret);
		return ret;
	}

	ret = smsc75xx_write_reg(dev, ADDR_FILTX + 4, addr_lo);
	if (ret < 0)
		netdev_warn(dev->net, "Failed to write ADDR_FILTX+4: %d\n", ret);

	return ret;
}

static int smsc75xx_phy_initialize(struct usbnet *dev)
{
	int bmcr, ret, timeout = 0;

	/* Initialize MII structure */
	dev->mii.dev = dev->net;
	dev->mii.mdio_read = smsc75xx_mdio_read;
	dev->mii.mdio_write = smsc75xx_mdio_write;
	dev->mii.phy_id_mask = 0x1f;
	dev->mii.reg_num_mask = 0x1f;
	dev->mii.supports_gmii = 1;
	dev->mii.phy_id = SMSC75XX_INTERNAL_PHY_ID;

	/* reset phy and wait for reset to complete */
	smsc75xx_mdio_write(dev->net, dev->mii.phy_id, MII_BMCR, BMCR_RESET);

	do {
		msleep(10);
		bmcr = smsc75xx_mdio_read(dev->net, dev->mii.phy_id, MII_BMCR);
		if (bmcr < 0) {
			netdev_warn(dev->net, "Error reading MII_BMCR\n");
			return bmcr;
		}
		timeout++;
	} while ((bmcr & BMCR_RESET) && (timeout < 100));

	if (timeout >= 100) {
		netdev_warn(dev->net, "timeout on PHY Reset\n");
		return -EIO;
	}

	/* phy workaround for gig link */
	smsc75xx_phy_gig_workaround(dev);

	smsc75xx_mdio_write(dev->net, dev->mii.phy_id, MII_ADVERTISE,
		ADVERTISE_ALL | ADVERTISE_CSMA | ADVERTISE_PAUSE_CAP |
		ADVERTISE_PAUSE_ASYM);
	smsc75xx_mdio_write(dev->net, dev->mii.phy_id, MII_CTRL1000,
		ADVERTISE_1000FULL);

	/* read and write to clear phy interrupt status */
	ret = smsc75xx_mdio_read(dev->net, dev->mii.phy_id, PHY_INT_SRC);
	if (ret < 0) {
		netdev_warn(dev->net, "Error reading PHY_INT_SRC\n");
		return ret;
	}

	smsc75xx_mdio_write(dev->net, dev->mii.phy_id, PHY_INT_SRC, 0xffff);

	smsc75xx_mdio_write(dev->net, dev->mii.phy_id, PHY_INT_MASK,
		PHY_INT_MASK_DEFAULT);
	mii_nway_restart(&dev->mii);

	netif_dbg(dev, ifup, dev->net, "phy initialised successfully\n");
	return 0;
}

static int smsc75xx_set_rx_max_frame_length(struct usbnet *dev, int size)
{
	int ret = 0;
	u32 buf;
	bool rxenabled;

	ret = smsc75xx_read_reg(dev, MAC_RX, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read MAC_RX: %d\n", ret);
		return ret;
	}

	rxenabled = ((buf & MAC_RX_RXEN) != 0);

	if (rxenabled) {
		buf &= ~MAC_RX_RXEN;
		ret = smsc75xx_write_reg(dev, MAC_RX, buf);
		if (ret < 0) {
			netdev_warn(dev->net, "Failed to write MAC_RX: %d\n", ret);
			return ret;
		}
	}

	/* add 4 to size for FCS */
	buf &= ~MAC_RX_MAX_SIZE;
	buf |= (((size + 4) << MAC_RX_MAX_SIZE_SHIFT) & MAC_RX_MAX_SIZE);

	ret = smsc75xx_write_reg(dev, MAC_RX, buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write MAC_RX: %d\n", ret);
		return ret;
	}

	if (rxenabled) {
		buf |= MAC_RX_RXEN;
		ret = smsc75xx_write_reg(dev, MAC_RX, buf);
		if (ret < 0) {
			netdev_warn(dev->net, "Failed to write MAC_RX: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int smsc75xx_change_mtu(struct net_device *netdev, int new_mtu)
{
	struct usbnet *dev = netdev_priv(netdev);
	int ret;

	ret = smsc75xx_set_rx_max_frame_length(dev, new_mtu + ETH_HLEN);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to set mac rx frame length\n");
		return ret;
	}

	return usbnet_change_mtu(netdev, new_mtu);
}

/* Enable or disable Rx checksum offload engine */
static int smsc75xx_set_features(struct net_device *netdev,
	netdev_features_t features)
{
	struct usbnet *dev = netdev_priv(netdev);
	struct smsc75xx_priv *pdata = (struct smsc75xx_priv *)(dev->data[0]);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&pdata->rfe_ctl_lock, flags);

	if (features & NETIF_F_RXCSUM)
		pdata->rfe_ctl |= RFE_CTL_TCPUDP_CKM | RFE_CTL_IP_CKM;
	else
		pdata->rfe_ctl &= ~(RFE_CTL_TCPUDP_CKM | RFE_CTL_IP_CKM);

	spin_unlock_irqrestore(&pdata->rfe_ctl_lock, flags);
	/* it's racing here! */

	ret = smsc75xx_write_reg(dev, RFE_CTL, pdata->rfe_ctl);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing RFE_CTL\n");
		return ret;
	}
	return 0;
}

static int smsc75xx_wait_ready(struct usbnet *dev, int in_pm)
{
	int timeout = 0;

	do {
		u32 buf;
		int ret;

		ret = __smsc75xx_read_reg(dev, PMT_CTL, &buf, in_pm);

		if (ret < 0) {
			netdev_warn(dev->net, "Failed to read PMT_CTL: %d\n", ret);
			return ret;
		}

		if (buf & PMT_CTL_DEV_RDY)
			return 0;

		msleep(10);
		timeout++;
	} while (timeout < 100);

	netdev_warn(dev->net, "timeout waiting for device ready\n");
	return -EIO;
}

static int smsc75xx_phy_gig_workaround(struct usbnet *dev)
{
	struct mii_if_info *mii = &dev->mii;
	int ret = 0, timeout = 0;
	u32 buf, link_up = 0;

	/* Set the phy in Gig loopback */
	smsc75xx_mdio_write(dev->net, mii->phy_id, MII_BMCR, 0x4040);

	/* Wait for the link up */
	do {
		link_up = smsc75xx_link_ok_nopm(dev);
		usleep_range(10000, 20000);
		timeout++;
	} while ((!link_up) && (timeout < 1000));

	if (timeout >= 1000) {
		netdev_warn(dev->net, "Timeout waiting for PHY link up\n");
		return -EIO;
	}

	/* phy reset */
	ret = smsc75xx_read_reg(dev, PMT_CTL, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read PMT_CTL: %d\n", ret);
		return ret;
	}

	buf |= PMT_CTL_PHY_RST;

	ret = smsc75xx_write_reg(dev, PMT_CTL, buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write PMT_CTL: %d\n", ret);
		return ret;
	}

	timeout = 0;
	do {
		usleep_range(10000, 20000);
		ret = smsc75xx_read_reg(dev, PMT_CTL, &buf);
		if (ret < 0) {
			netdev_warn(dev->net, "Failed to read PMT_CTL: %d\n",
				    ret);
			return ret;
		}
		timeout++;
	} while ((buf & PMT_CTL_PHY_RST) && (timeout < 100));

	if (timeout >= 100) {
		netdev_warn(dev->net, "timeout waiting for PHY Reset\n");
		return -EIO;
	}

	return 0;
}

static int smsc75xx_reset(struct usbnet *dev)
{
	struct smsc75xx_priv *pdata = (struct smsc75xx_priv *)(dev->data[0]);
	u32 buf;
	int ret = 0, timeout;

	netif_dbg(dev, ifup, dev->net, "entering smsc75xx_reset\n");

	ret = smsc75xx_wait_ready(dev, 0);
	if (ret < 0) {
		netdev_warn(dev->net, "device not ready in smsc75xx_reset\n");
		return ret;
	}

	ret = smsc75xx_read_reg(dev, HW_CFG, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read HW_CFG: %d\n", ret);
		return ret;
	}

	buf |= HW_CFG_LRST;

	ret = smsc75xx_write_reg(dev, HW_CFG, buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write HW_CFG: %d\n", ret);
		return ret;
	}

	timeout = 0;
	do {
		msleep(10);
		ret = smsc75xx_read_reg(dev, HW_CFG, &buf);
		if (ret < 0) {
			netdev_warn(dev->net, "Failed to read HW_CFG: %d\n", ret);
			return ret;
		}
		timeout++;
	} while ((buf & HW_CFG_LRST) && (timeout < 100));

	if (timeout >= 100) {
		netdev_warn(dev->net, "timeout on completion of Lite Reset\n");
		return -EIO;
	}

	netif_dbg(dev, ifup, dev->net, "Lite reset complete, resetting PHY\n");

	ret = smsc75xx_read_reg(dev, PMT_CTL, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read PMT_CTL: %d\n", ret);
		return ret;
	}

	buf |= PMT_CTL_PHY_RST;

	ret = smsc75xx_write_reg(dev, PMT_CTL, buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write PMT_CTL: %d\n", ret);
		return ret;
	}

	timeout = 0;
	do {
		msleep(10);
		ret = smsc75xx_read_reg(dev, PMT_CTL, &buf);
		if (ret < 0) {
			netdev_warn(dev->net, "Failed to read PMT_CTL: %d\n", ret);
			return ret;
		}
		timeout++;
	} while ((buf & PMT_CTL_PHY_RST) && (timeout < 100));

	if (timeout >= 100) {
		netdev_warn(dev->net, "timeout waiting for PHY Reset\n");
		return -EIO;
	}

	netif_dbg(dev, ifup, dev->net, "PHY reset complete\n");

	ret = smsc75xx_set_mac_address(dev);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to set mac address\n");
		return ret;
	}

	netif_dbg(dev, ifup, dev->net, "MAC Address: %pM\n",
		  dev->net->dev_addr);

	ret = smsc75xx_read_reg(dev, HW_CFG, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read HW_CFG: %d\n", ret);
		return ret;
	}

	netif_dbg(dev, ifup, dev->net, "Read Value from HW_CFG : 0x%08x\n",
		  buf);

	buf |= HW_CFG_BIR;

	ret = smsc75xx_write_reg(dev, HW_CFG, buf);
	if (ret < 0) {
		netdev_warn(dev->net,  "Failed to write HW_CFG: %d\n", ret);
		return ret;
	}

	ret = smsc75xx_read_reg(dev, HW_CFG, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read HW_CFG: %d\n", ret);
		return ret;
	}

	netif_dbg(dev, ifup, dev->net, "Read Value from HW_CFG after writing HW_CFG_BIR: 0x%08x\n",
		  buf);

	if (!turbo_mode) {
		buf = 0;
		dev->rx_urb_size = MAX_SINGLE_PACKET_SIZE;
	} else if (dev->udev->speed == USB_SPEED_HIGH) {
		buf = DEFAULT_HS_BURST_CAP_SIZE / HS_USB_PKT_SIZE;
		dev->rx_urb_size = DEFAULT_HS_BURST_CAP_SIZE;
	} else {
		buf = DEFAULT_FS_BURST_CAP_SIZE / FS_USB_PKT_SIZE;
		dev->rx_urb_size = DEFAULT_FS_BURST_CAP_SIZE;
	}

	netif_dbg(dev, ifup, dev->net, "rx_urb_size=%ld\n",
		  (ulong)dev->rx_urb_size);

	ret = smsc75xx_write_reg(dev, BURST_CAP, buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write BURST_CAP: %d\n", ret);
		return ret;
	}

	ret = smsc75xx_read_reg(dev, BURST_CAP, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read BURST_CAP: %d\n", ret);
		return ret;
	}

	netif_dbg(dev, ifup, dev->net,
		  "Read Value from BURST_CAP after writing: 0x%08x\n", buf);

	ret = smsc75xx_write_reg(dev, BULK_IN_DLY, DEFAULT_BULK_IN_DELAY);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write BULK_IN_DLY: %d\n", ret);
		return ret;
	}

	ret = smsc75xx_read_reg(dev, BULK_IN_DLY, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read BULK_IN_DLY: %d\n", ret);
		return ret;
	}

	netif_dbg(dev, ifup, dev->net,
		  "Read Value from BULK_IN_DLY after writing: 0x%08x\n", buf);

	if (turbo_mode) {
		ret = smsc75xx_read_reg(dev, HW_CFG, &buf);
		if (ret < 0) {
			netdev_warn(dev->net, "Failed to read HW_CFG: %d\n", ret);
			return ret;
		}

		netif_dbg(dev, ifup, dev->net, "HW_CFG: 0x%08x\n", buf);

		buf |= (HW_CFG_MEF | HW_CFG_BCE);

		ret = smsc75xx_write_reg(dev, HW_CFG, buf);
		if (ret < 0) {
			netdev_warn(dev->net, "Failed to write HW_CFG: %d\n", ret);
			return ret;
		}

		ret = smsc75xx_read_reg(dev, HW_CFG, &buf);
		if (ret < 0) {
			netdev_warn(dev->net, "Failed to read HW_CFG: %d\n", ret);
			return ret;
		}

		netif_dbg(dev, ifup, dev->net, "HW_CFG: 0x%08x\n", buf);
	}

	/* set FIFO sizes */
	buf = (MAX_RX_FIFO_SIZE - 512) / 512;
	ret = smsc75xx_write_reg(dev, FCT_RX_FIFO_END, buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write FCT_RX_FIFO_END: %d\n", ret);
		return ret;
	}

	netif_dbg(dev, ifup, dev->net, "FCT_RX_FIFO_END set to 0x%08x\n", buf);

	buf = (MAX_TX_FIFO_SIZE - 512) / 512;
	ret = smsc75xx_write_reg(dev, FCT_TX_FIFO_END, buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write FCT_TX_FIFO_END: %d\n", ret);
		return ret;
	}

	netif_dbg(dev, ifup, dev->net, "FCT_TX_FIFO_END set to 0x%08x\n", buf);

	ret = smsc75xx_write_reg(dev, INT_STS, INT_STS_CLEAR_ALL);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write INT_STS: %d\n", ret);
		return ret;
	}

	ret = smsc75xx_read_reg(dev, ID_REV, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read ID_REV: %d\n", ret);
		return ret;
	}

	netif_dbg(dev, ifup, dev->net, "ID_REV = 0x%08x\n", buf);

	ret = smsc75xx_read_reg(dev, E2P_CMD, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read E2P_CMD: %d\n", ret);
		return ret;
	}

	/* only set default GPIO/LED settings if no EEPROM is detected */
	if (!(buf & E2P_CMD_LOADED)) {
		ret = smsc75xx_read_reg(dev, LED_GPIO_CFG, &buf);
		if (ret < 0) {
			netdev_warn(dev->net, "Failed to read LED_GPIO_CFG: %d\n", ret);
			return ret;
		}

		buf &= ~(LED_GPIO_CFG_LED2_FUN_SEL | LED_GPIO_CFG_LED10_FUN_SEL);
		buf |= LED_GPIO_CFG_LEDGPIO_EN | LED_GPIO_CFG_LED2_FUN_SEL;

		ret = smsc75xx_write_reg(dev, LED_GPIO_CFG, buf);
		if (ret < 0) {
			netdev_warn(dev->net, "Failed to write LED_GPIO_CFG: %d\n", ret);
			return ret;
		}
	}

	ret = smsc75xx_write_reg(dev, FLOW, 0);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write FLOW: %d\n", ret);
		return ret;
	}

	ret = smsc75xx_write_reg(dev, FCT_FLOW, 0);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write FCT_FLOW: %d\n", ret);
		return ret;
	}

	/* Don't need rfe_ctl_lock during initialisation */
	ret = smsc75xx_read_reg(dev, RFE_CTL, &pdata->rfe_ctl);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read RFE_CTL: %d\n", ret);
		return ret;
	}

	pdata->rfe_ctl |= RFE_CTL_AB | RFE_CTL_DPF;

	ret = smsc75xx_write_reg(dev, RFE_CTL, pdata->rfe_ctl);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write RFE_CTL: %d\n", ret);
		return ret;
	}

	ret = smsc75xx_read_reg(dev, RFE_CTL, &pdata->rfe_ctl);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read RFE_CTL: %d\n", ret);
		return ret;
	}

	netif_dbg(dev, ifup, dev->net, "RFE_CTL set to 0x%08x\n",
		  pdata->rfe_ctl);

	/* Enable or disable checksum offload engines */
	smsc75xx_set_features(dev->net, dev->net->features);

	smsc75xx_set_multicast(dev->net);

	ret = smsc75xx_phy_initialize(dev);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to initialize PHY: %d\n", ret);
		return ret;
	}

	ret = smsc75xx_read_reg(dev, INT_EP_CTL, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read INT_EP_CTL: %d\n", ret);
		return ret;
	}

	/* enable PHY interrupts */
	buf |= INT_ENP_PHY_INT;

	ret = smsc75xx_write_reg(dev, INT_EP_CTL, buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write INT_EP_CTL: %d\n", ret);
		return ret;
	}

	/* allow mac to detect speed and duplex from phy */
	ret = smsc75xx_read_reg(dev, MAC_CR, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read MAC_CR: %d\n", ret);
		return ret;
	}

	buf |= (MAC_CR_ADD | MAC_CR_ASD);
	ret = smsc75xx_write_reg(dev, MAC_CR, buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write MAC_CR: %d\n", ret);
		return ret;
	}

	ret = smsc75xx_read_reg(dev, MAC_TX, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read MAC_TX: %d\n", ret);
		return ret;
	}

	buf |= MAC_TX_TXEN;

	ret = smsc75xx_write_reg(dev, MAC_TX, buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write MAC_TX: %d\n", ret);
		return ret;
	}

	netif_dbg(dev, ifup, dev->net, "MAC_TX set to 0x%08x\n", buf);

	ret = smsc75xx_read_reg(dev, FCT_TX_CTL, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read FCT_TX_CTL: %d\n", ret);
		return ret;
	}

	buf |= FCT_TX_CTL_EN;

	ret = smsc75xx_write_reg(dev, FCT_TX_CTL, buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write FCT_TX_CTL: %d\n", ret);
		return ret;
	}

	netif_dbg(dev, ifup, dev->net, "FCT_TX_CTL set to 0x%08x\n", buf);

	ret = smsc75xx_set_rx_max_frame_length(dev, dev->net->mtu + ETH_HLEN);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to set max rx frame length\n");
		return ret;
	}

	ret = smsc75xx_read_reg(dev, MAC_RX, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read MAC_RX: %d\n", ret);
		return ret;
	}

	buf |= MAC_RX_RXEN;

	ret = smsc75xx_write_reg(dev, MAC_RX, buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write MAC_RX: %d\n", ret);
		return ret;
	}

	netif_dbg(dev, ifup, dev->net, "MAC_RX set to 0x%08x\n", buf);

	ret = smsc75xx_read_reg(dev, FCT_RX_CTL, &buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read FCT_RX_CTL: %d\n", ret);
		return ret;
	}

	buf |= FCT_RX_CTL_EN;

	ret = smsc75xx_write_reg(dev, FCT_RX_CTL, buf);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write FCT_RX_CTL: %d\n", ret);
		return ret;
	}

	netif_dbg(dev, ifup, dev->net, "FCT_RX_CTL set to 0x%08x\n", buf);

	netif_dbg(dev, ifup, dev->net, "smsc75xx_reset, return 0\n");
	return 0;
}

static const struct net_device_ops smsc75xx_netdev_ops = {
	.ndo_open		= usbnet_open,
	.ndo_stop		= usbnet_stop,
	.ndo_start_xmit		= usbnet_start_xmit,
	.ndo_tx_timeout		= usbnet_tx_timeout,
	.ndo_get_stats64	= usbnet_get_stats64,
	.ndo_change_mtu		= smsc75xx_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_do_ioctl 		= smsc75xx_ioctl,
	.ndo_set_rx_mode	= smsc75xx_set_multicast,
	.ndo_set_features	= smsc75xx_set_features,
};

static int smsc75xx_bind(struct usbnet *dev, struct usb_interface *intf)
{
	struct smsc75xx_priv *pdata = NULL;
	int ret;

	printk(KERN_INFO SMSC_CHIPNAME " v" SMSC_DRIVER_VERSION "\n");

	ret = usbnet_get_endpoints(dev, intf);
	if (ret < 0) {
		netdev_warn(dev->net, "usbnet_get_endpoints failed: %d\n", ret);
		return ret;
	}

	dev->data[0] = (unsigned long)kzalloc(sizeof(struct smsc75xx_priv),
					      GFP_KERNEL);

	pdata = (struct smsc75xx_priv *)(dev->data[0]);
	if (!pdata)
		return -ENOMEM;

	pdata->dev = dev;

	spin_lock_init(&pdata->rfe_ctl_lock);
	mutex_init(&pdata->dataport_mutex);

	INIT_WORK(&pdata->set_multicast, smsc75xx_deferred_multicast_write);

	if (DEFAULT_TX_CSUM_ENABLE)
		dev->net->features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;

	if (DEFAULT_RX_CSUM_ENABLE)
		dev->net->features |= NETIF_F_RXCSUM;

	dev->net->hw_features = NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM |
				NETIF_F_RXCSUM;

	ret = smsc75xx_wait_ready(dev, 0);
	if (ret < 0) {
		netdev_warn(dev->net, "device not ready in smsc75xx_bind\n");
		return ret;
	}

	smsc75xx_init_mac_address(dev);

	/* Init all registers */
	ret = smsc75xx_reset(dev);
	if (ret < 0) {
		netdev_warn(dev->net, "smsc75xx_reset error %d\n", ret);
		return ret;
	}

	dev->net->netdev_ops = &smsc75xx_netdev_ops;
	dev->net->ethtool_ops = &smsc75xx_ethtool_ops;
	dev->net->flags |= IFF_MULTICAST;
	dev->net->hard_header_len += SMSC75XX_TX_OVERHEAD;
	dev->hard_mtu = dev->net->mtu + dev->net->hard_header_len;
	dev->net->max_mtu = MAX_SINGLE_PACKET_SIZE;
	return 0;
}

static void smsc75xx_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	struct smsc75xx_priv *pdata = (struct smsc75xx_priv *)(dev->data[0]);
	if (pdata) {
		cancel_work_sync(&pdata->set_multicast);
		netif_dbg(dev, ifdown, dev->net, "free pdata\n");
		kfree(pdata);
		pdata = NULL;
		dev->data[0] = 0;
	}
}

static u16 smsc_crc(const u8 *buffer, size_t len)
{
	return bitrev16(crc16(0xFFFF, buffer, len));
}

static int smsc75xx_write_wuff(struct usbnet *dev, int filter, u32 wuf_cfg,
			       u32 wuf_mask1)
{
	int cfg_base = WUF_CFGX + filter * 4;
	int mask_base = WUF_MASKX + filter * 16;
	int ret;

	ret = smsc75xx_write_reg(dev, cfg_base, wuf_cfg);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing WUF_CFGX\n");
		return ret;
	}

	ret = smsc75xx_write_reg(dev, mask_base, wuf_mask1);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing WUF_MASKX\n");
		return ret;
	}

	ret = smsc75xx_write_reg(dev, mask_base + 4, 0);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing WUF_MASKX\n");
		return ret;
	}

	ret = smsc75xx_write_reg(dev, mask_base + 8, 0);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing WUF_MASKX\n");
		return ret;
	}

	ret = smsc75xx_write_reg(dev, mask_base + 12, 0);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing WUF_MASKX\n");
		return ret;
	}

	return 0;
}

static int smsc75xx_enter_suspend0(struct usbnet *dev)
{
	struct smsc75xx_priv *pdata = (struct smsc75xx_priv *)(dev->data[0]);
	u32 val;
	int ret;

	ret = smsc75xx_read_reg_nopm(dev, PMT_CTL, &val);
	if (ret < 0) {
		netdev_warn(dev->net, "Error reading PMT_CTL\n");
		return ret;
	}

	val &= (~(PMT_CTL_SUS_MODE | PMT_CTL_PHY_RST));
	val |= PMT_CTL_SUS_MODE_0 | PMT_CTL_WOL_EN | PMT_CTL_WUPS;

	ret = smsc75xx_write_reg_nopm(dev, PMT_CTL, val);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing PMT_CTL\n");
		return ret;
	}

	pdata->suspend_flags |= SUSPEND_SUSPEND0;

	return 0;
}

static int smsc75xx_enter_suspend1(struct usbnet *dev)
{
	struct smsc75xx_priv *pdata = (struct smsc75xx_priv *)(dev->data[0]);
	u32 val;
	int ret;

	ret = smsc75xx_read_reg_nopm(dev, PMT_CTL, &val);
	if (ret < 0) {
		netdev_warn(dev->net, "Error reading PMT_CTL\n");
		return ret;
	}

	val &= ~(PMT_CTL_SUS_MODE | PMT_CTL_WUPS | PMT_CTL_PHY_RST);
	val |= PMT_CTL_SUS_MODE_1;

	ret = smsc75xx_write_reg_nopm(dev, PMT_CTL, val);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing PMT_CTL\n");
		return ret;
	}

	/* clear wol status, enable energy detection */
	val &= ~PMT_CTL_WUPS;
	val |= (PMT_CTL_WUPS_ED | PMT_CTL_ED_EN);

	ret = smsc75xx_write_reg_nopm(dev, PMT_CTL, val);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing PMT_CTL\n");
		return ret;
	}

	pdata->suspend_flags |= SUSPEND_SUSPEND1;

	return 0;
}

static int smsc75xx_enter_suspend2(struct usbnet *dev)
{
	struct smsc75xx_priv *pdata = (struct smsc75xx_priv *)(dev->data[0]);
	u32 val;
	int ret;

	ret = smsc75xx_read_reg_nopm(dev, PMT_CTL, &val);
	if (ret < 0) {
		netdev_warn(dev->net, "Error reading PMT_CTL\n");
		return ret;
	}

	val &= ~(PMT_CTL_SUS_MODE | PMT_CTL_WUPS | PMT_CTL_PHY_RST);
	val |= PMT_CTL_SUS_MODE_2;

	ret = smsc75xx_write_reg_nopm(dev, PMT_CTL, val);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing PMT_CTL\n");
		return ret;
	}

	pdata->suspend_flags |= SUSPEND_SUSPEND2;

	return 0;
}

static int smsc75xx_enter_suspend3(struct usbnet *dev)
{
	struct smsc75xx_priv *pdata = (struct smsc75xx_priv *)(dev->data[0]);
	u32 val;
	int ret;

	ret = smsc75xx_read_reg_nopm(dev, FCT_RX_CTL, &val);
	if (ret < 0) {
		netdev_warn(dev->net, "Error reading FCT_RX_CTL\n");
		return ret;
	}

	if (val & FCT_RX_CTL_RXUSED) {
		netdev_dbg(dev->net, "rx fifo not empty in autosuspend\n");
		return -EBUSY;
	}

	ret = smsc75xx_read_reg_nopm(dev, PMT_CTL, &val);
	if (ret < 0) {
		netdev_warn(dev->net, "Error reading PMT_CTL\n");
		return ret;
	}

	val &= ~(PMT_CTL_SUS_MODE | PMT_CTL_WUPS | PMT_CTL_PHY_RST);
	val |= PMT_CTL_SUS_MODE_3 | PMT_CTL_RES_CLR_WKP_EN;

	ret = smsc75xx_write_reg_nopm(dev, PMT_CTL, val);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing PMT_CTL\n");
		return ret;
	}

	/* clear wol status */
	val &= ~PMT_CTL_WUPS;
	val |= PMT_CTL_WUPS_WOL;

	ret = smsc75xx_write_reg_nopm(dev, PMT_CTL, val);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing PMT_CTL\n");
		return ret;
	}

	pdata->suspend_flags |= SUSPEND_SUSPEND3;

	return 0;
}

static int smsc75xx_enable_phy_wakeup_interrupts(struct usbnet *dev, u16 mask)
{
	struct mii_if_info *mii = &dev->mii;
	int ret;

	netdev_dbg(dev->net, "enabling PHY wakeup interrupts\n");

	/* read to clear */
	ret = smsc75xx_mdio_read_nopm(dev->net, mii->phy_id, PHY_INT_SRC);
	if (ret < 0) {
		netdev_warn(dev->net, "Error reading PHY_INT_SRC\n");
		return ret;
	}

	/* enable interrupt source */
	ret = smsc75xx_mdio_read_nopm(dev->net, mii->phy_id, PHY_INT_MASK);
	if (ret < 0) {
		netdev_warn(dev->net, "Error reading PHY_INT_MASK\n");
		return ret;
	}

	ret |= mask;

	smsc75xx_mdio_write_nopm(dev->net, mii->phy_id, PHY_INT_MASK, ret);

	return 0;
}

static int smsc75xx_link_ok_nopm(struct usbnet *dev)
{
	struct mii_if_info *mii = &dev->mii;
	int ret;

	/* first, a dummy read, needed to latch some MII phys */
	ret = smsc75xx_mdio_read_nopm(dev->net, mii->phy_id, MII_BMSR);
	if (ret < 0) {
		netdev_warn(dev->net, "Error reading MII_BMSR\n");
		return ret;
	}

	ret = smsc75xx_mdio_read_nopm(dev->net, mii->phy_id, MII_BMSR);
	if (ret < 0) {
		netdev_warn(dev->net, "Error reading MII_BMSR\n");
		return ret;
	}

	return !!(ret & BMSR_LSTATUS);
}

static int smsc75xx_autosuspend(struct usbnet *dev, u32 link_up)
{
	int ret;

	if (!netif_running(dev->net)) {
		/* interface is ifconfig down so fully power down hw */
		netdev_dbg(dev->net, "autosuspend entering SUSPEND2\n");
		return smsc75xx_enter_suspend2(dev);
	}

	if (!link_up) {
		/* link is down so enter EDPD mode */
		netdev_dbg(dev->net, "autosuspend entering SUSPEND1\n");

		/* enable PHY wakeup events for if cable is attached */
		ret = smsc75xx_enable_phy_wakeup_interrupts(dev,
			PHY_INT_MASK_ANEG_COMP);
		if (ret < 0) {
			netdev_warn(dev->net, "error enabling PHY wakeup ints\n");
			return ret;
		}

		netdev_info(dev->net, "entering SUSPEND1 mode\n");
		return smsc75xx_enter_suspend1(dev);
	}

	/* enable PHY wakeup events so we remote wakeup if cable is pulled */
	ret = smsc75xx_enable_phy_wakeup_interrupts(dev,
		PHY_INT_MASK_LINK_DOWN);
	if (ret < 0) {
		netdev_warn(dev->net, "error enabling PHY wakeup ints\n");
		return ret;
	}

	netdev_dbg(dev->net, "autosuspend entering SUSPEND3\n");
	return smsc75xx_enter_suspend3(dev);
}

static int smsc75xx_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usbnet *dev = usb_get_intfdata(intf);
	struct smsc75xx_priv *pdata = (struct smsc75xx_priv *)(dev->data[0]);
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
	link_up = smsc75xx_link_ok_nopm(dev);

	if (message.event == PM_EVENT_AUTO_SUSPEND) {
		ret = smsc75xx_autosuspend(dev, link_up);
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
		ret = smsc75xx_read_reg_nopm(dev, WUCSR, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading WUCSR\n");
			goto done;
		}

		val &= ~(WUCSR_MPEN | WUCSR_WUEN);

		ret = smsc75xx_write_reg_nopm(dev, WUCSR, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing WUCSR\n");
			goto done;
		}

		ret = smsc75xx_read_reg_nopm(dev, PMT_CTL, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading PMT_CTL\n");
			goto done;
		}

		val &= ~(PMT_CTL_ED_EN | PMT_CTL_WOL_EN);

		ret = smsc75xx_write_reg_nopm(dev, PMT_CTL, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing PMT_CTL\n");
			goto done;
		}

		ret = smsc75xx_enter_suspend2(dev);
		goto done;
	}

	if (pdata->wolopts & WAKE_PHY) {
		ret = smsc75xx_enable_phy_wakeup_interrupts(dev,
			(PHY_INT_MASK_ANEG_COMP | PHY_INT_MASK_LINK_DOWN));
		if (ret < 0) {
			netdev_warn(dev->net, "error enabling PHY wakeup ints\n");
			goto done;
		}

		/* if link is down then configure EDPD and enter SUSPEND1,
		 * otherwise enter SUSPEND0 below
		 */
		if (!link_up) {
			struct mii_if_info *mii = &dev->mii;
			netdev_info(dev->net, "entering SUSPEND1 mode\n");

			/* enable energy detect power-down mode */
			ret = smsc75xx_mdio_read_nopm(dev->net, mii->phy_id,
				PHY_MODE_CTRL_STS);
			if (ret < 0) {
				netdev_warn(dev->net, "Error reading PHY_MODE_CTRL_STS\n");
				goto done;
			}

			ret |= MODE_CTRL_STS_EDPWRDOWN;

			smsc75xx_mdio_write_nopm(dev->net, mii->phy_id,
				PHY_MODE_CTRL_STS, ret);

			/* enter SUSPEND1 mode */
			ret = smsc75xx_enter_suspend1(dev);
			goto done;
		}
	}

	if (pdata->wolopts & (WAKE_MCAST | WAKE_ARP)) {
		int i, filter = 0;

		/* disable all filters */
		for (i = 0; i < WUF_NUM; i++) {
			ret = smsc75xx_write_reg_nopm(dev, WUF_CFGX + i * 4, 0);
			if (ret < 0) {
				netdev_warn(dev->net, "Error writing WUF_CFGX\n");
				goto done;
			}
		}

		if (pdata->wolopts & WAKE_MCAST) {
			const u8 mcast[] = {0x01, 0x00, 0x5E};
			netdev_info(dev->net, "enabling multicast detection\n");

			val = WUF_CFGX_EN | WUF_CFGX_ATYPE_MULTICAST
				| smsc_crc(mcast, 3);
			ret = smsc75xx_write_wuff(dev, filter++, val, 0x0007);
			if (ret < 0) {
				netdev_warn(dev->net, "Error writing wakeup filter\n");
				goto done;
			}
		}

		if (pdata->wolopts & WAKE_ARP) {
			const u8 arp[] = {0x08, 0x06};
			netdev_info(dev->net, "enabling ARP detection\n");

			val = WUF_CFGX_EN | WUF_CFGX_ATYPE_ALL | (0x0C << 16)
				| smsc_crc(arp, 2);
			ret = smsc75xx_write_wuff(dev, filter++, val, 0x0003);
			if (ret < 0) {
				netdev_warn(dev->net, "Error writing wakeup filter\n");
				goto done;
			}
		}

		/* clear any pending pattern match packet status */
		ret = smsc75xx_read_reg_nopm(dev, WUCSR, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading WUCSR\n");
			goto done;
		}

		val |= WUCSR_WUFR;

		ret = smsc75xx_write_reg_nopm(dev, WUCSR, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing WUCSR\n");
			goto done;
		}

		netdev_info(dev->net, "enabling packet match detection\n");
		ret = smsc75xx_read_reg_nopm(dev, WUCSR, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading WUCSR\n");
			goto done;
		}

		val |= WUCSR_WUEN;

		ret = smsc75xx_write_reg_nopm(dev, WUCSR, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing WUCSR\n");
			goto done;
		}
	} else {
		netdev_info(dev->net, "disabling packet match detection\n");
		ret = smsc75xx_read_reg_nopm(dev, WUCSR, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading WUCSR\n");
			goto done;
		}

		val &= ~WUCSR_WUEN;

		ret = smsc75xx_write_reg_nopm(dev, WUCSR, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing WUCSR\n");
			goto done;
		}
	}

	/* disable magic, bcast & unicast wakeup sources */
	ret = smsc75xx_read_reg_nopm(dev, WUCSR, &val);
	if (ret < 0) {
		netdev_warn(dev->net, "Error reading WUCSR\n");
		goto done;
	}

	val &= ~(WUCSR_MPEN | WUCSR_BCST_EN | WUCSR_PFDA_EN);

	ret = smsc75xx_write_reg_nopm(dev, WUCSR, val);
	if (ret < 0) {
		netdev_warn(dev->net, "Error writing WUCSR\n");
		goto done;
	}

	if (pdata->wolopts & WAKE_PHY) {
		netdev_info(dev->net, "enabling PHY wakeup\n");

		ret = smsc75xx_read_reg_nopm(dev, PMT_CTL, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading PMT_CTL\n");
			goto done;
		}

		/* clear wol status, enable energy detection */
		val &= ~PMT_CTL_WUPS;
		val |= (PMT_CTL_WUPS_ED | PMT_CTL_ED_EN);

		ret = smsc75xx_write_reg_nopm(dev, PMT_CTL, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing PMT_CTL\n");
			goto done;
		}
	}

	if (pdata->wolopts & WAKE_MAGIC) {
		netdev_info(dev->net, "enabling magic packet wakeup\n");
		ret = smsc75xx_read_reg_nopm(dev, WUCSR, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading WUCSR\n");
			goto done;
		}

		/* clear any pending magic packet status */
		val |= WUCSR_MPR | WUCSR_MPEN;

		ret = smsc75xx_write_reg_nopm(dev, WUCSR, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing WUCSR\n");
			goto done;
		}
	}

	if (pdata->wolopts & WAKE_BCAST) {
		netdev_info(dev->net, "enabling broadcast detection\n");
		ret = smsc75xx_read_reg_nopm(dev, WUCSR, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading WUCSR\n");
			goto done;
		}

		val |= WUCSR_BCAST_FR | WUCSR_BCST_EN;

		ret = smsc75xx_write_reg_nopm(dev, WUCSR, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing WUCSR\n");
			goto done;
		}
	}

	if (pdata->wolopts & WAKE_UCAST) {
		netdev_info(dev->net, "enabling unicast detection\n");
		ret = smsc75xx_read_reg_nopm(dev, WUCSR, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading WUCSR\n");
			goto done;
		}

		val |= WUCSR_WUFR | WUCSR_PFDA_EN;

		ret = smsc75xx_write_reg_nopm(dev, WUCSR, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing WUCSR\n");
			goto done;
		}
	}

	/* enable receiver to enable frame reception */
	ret = smsc75xx_read_reg_nopm(dev, MAC_RX, &val);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to read MAC_RX: %d\n", ret);
		goto done;
	}

	val |= MAC_RX_RXEN;

	ret = smsc75xx_write_reg_nopm(dev, MAC_RX, val);
	if (ret < 0) {
		netdev_warn(dev->net, "Failed to write MAC_RX: %d\n", ret);
		goto done;
	}

	/* some wol options are enabled, so enter SUSPEND0 */
	netdev_info(dev->net, "entering SUSPEND0 mode\n");
	ret = smsc75xx_enter_suspend0(dev);

done:
	/*
	 * TODO: resume() might need to handle the suspend failure
	 * in system sleep
	 */
	if (ret && PMSG_IS_AUTO(message))
		usbnet_resume(intf);
	return ret;
}

static int smsc75xx_resume(struct usb_interface *intf)
{
	struct usbnet *dev = usb_get_intfdata(intf);
	struct smsc75xx_priv *pdata = (struct smsc75xx_priv *)(dev->data[0]);
	u8 suspend_flags = pdata->suspend_flags;
	int ret;
	u32 val;

	netdev_dbg(dev->net, "resume suspend_flags=0x%02x\n", suspend_flags);

	/* do this first to ensure it's cleared even in error case */
	pdata->suspend_flags = 0;

	if (suspend_flags & SUSPEND_ALLMODES) {
		/* Disable wakeup sources */
		ret = smsc75xx_read_reg_nopm(dev, WUCSR, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading WUCSR\n");
			return ret;
		}

		val &= ~(WUCSR_WUEN | WUCSR_MPEN | WUCSR_PFDA_EN
			| WUCSR_BCST_EN);

		ret = smsc75xx_write_reg_nopm(dev, WUCSR, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing WUCSR\n");
			return ret;
		}

		/* clear wake-up status */
		ret = smsc75xx_read_reg_nopm(dev, PMT_CTL, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading PMT_CTL\n");
			return ret;
		}

		val &= ~PMT_CTL_WOL_EN;
		val |= PMT_CTL_WUPS;

		ret = smsc75xx_write_reg_nopm(dev, PMT_CTL, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing PMT_CTL\n");
			return ret;
		}
	}

	if (suspend_flags & SUSPEND_SUSPEND2) {
		netdev_info(dev->net, "resuming from SUSPEND2\n");

		ret = smsc75xx_read_reg_nopm(dev, PMT_CTL, &val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error reading PMT_CTL\n");
			return ret;
		}

		val |= PMT_CTL_PHY_PWRUP;

		ret = smsc75xx_write_reg_nopm(dev, PMT_CTL, val);
		if (ret < 0) {
			netdev_warn(dev->net, "Error writing PMT_CTL\n");
			return ret;
		}
	}

	ret = smsc75xx_wait_ready(dev, 1);
	if (ret < 0) {
		netdev_warn(dev->net, "device not ready in smsc75xx_resume\n");
		return ret;
	}

	return usbnet_resume(intf);
}

static void smsc75xx_rx_csum_offload(struct usbnet *dev, struct sk_buff *skb,
				     u32 rx_cmd_a, u32 rx_cmd_b)
{
	if (!(dev->net->features & NETIF_F_RXCSUM) ||
	    unlikely(rx_cmd_a & RX_CMD_A_LCSM)) {
		skb->ip_summed = CHECKSUM_NONE;
	} else {
		skb->csum = ntohs((u16)(rx_cmd_b >> RX_CMD_B_CSUM_SHIFT));
		skb->ip_summed = CHECKSUM_COMPLETE;
	}
}

static int smsc75xx_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	/* This check is no longer done by usbnet */
	if (skb->len < dev->net->hard_header_len)
		return 0;

	while (skb->len > 0) {
		u32 rx_cmd_a, rx_cmd_b, align_count, size;
		struct sk_buff *ax_skb;
		unsigned char *packet;

		memcpy(&rx_cmd_a, skb->data, sizeof(rx_cmd_a));
		le32_to_cpus(&rx_cmd_a);
		skb_pull(skb, 4);

		memcpy(&rx_cmd_b, skb->data, sizeof(rx_cmd_b));
		le32_to_cpus(&rx_cmd_b);
		skb_pull(skb, 4 + RXW_PADDING);

		packet = skb->data;

		/* get the packet length */
		size = (rx_cmd_a & RX_CMD_A_LEN) - RXW_PADDING;
		align_count = (4 - ((size + RXW_PADDING) % 4)) % 4;

		if (unlikely(rx_cmd_a & RX_CMD_A_RED)) {
			netif_dbg(dev, rx_err, dev->net,
				  "Error rx_cmd_a=0x%08x\n", rx_cmd_a);
			dev->net->stats.rx_errors++;
			dev->net->stats.rx_dropped++;

			if (rx_cmd_a & RX_CMD_A_FCS)
				dev->net->stats.rx_crc_errors++;
			else if (rx_cmd_a & (RX_CMD_A_LONG | RX_CMD_A_RUNT))
				dev->net->stats.rx_frame_errors++;
		} else {
			/* MAX_SINGLE_PACKET_SIZE + 4(CRC) + 2(COE) + 4(Vlan) */
			if (unlikely(size > (MAX_SINGLE_PACKET_SIZE + ETH_HLEN + 12))) {
				netif_dbg(dev, rx_err, dev->net,
					  "size err rx_cmd_a=0x%08x\n",
					  rx_cmd_a);
				return 0;
			}

			/* last frame in this batch */
			if (skb->len == size) {
				smsc75xx_rx_csum_offload(dev, skb, rx_cmd_a,
					rx_cmd_b);

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

			smsc75xx_rx_csum_offload(dev, ax_skb, rx_cmd_a,
				rx_cmd_b);

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

static struct sk_buff *smsc75xx_tx_fixup(struct usbnet *dev,
					 struct sk_buff *skb, gfp_t flags)
{
	u32 tx_cmd_a, tx_cmd_b;

	if (skb_cow_head(skb, SMSC75XX_TX_OVERHEAD)) {
		dev_kfree_skb_any(skb);
		return NULL;
	}

	tx_cmd_a = (u32)(skb->len & TX_CMD_A_LEN) | TX_CMD_A_FCS;

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		tx_cmd_a |= TX_CMD_A_IPE | TX_CMD_A_TPE;

	if (skb_is_gso(skb)) {
		u16 mss = max(skb_shinfo(skb)->gso_size, TX_MSS_MIN);
		tx_cmd_b = (mss << TX_CMD_B_MSS_SHIFT) & TX_CMD_B_MSS;

		tx_cmd_a |= TX_CMD_A_LSO;
	} else {
		tx_cmd_b = 0;
	}

	skb_push(skb, 4);
	cpu_to_le32s(&tx_cmd_b);
	memcpy(skb->data, &tx_cmd_b, 4);

	skb_push(skb, 4);
	cpu_to_le32s(&tx_cmd_a);
	memcpy(skb->data, &tx_cmd_a, 4);

	return skb;
}

static int smsc75xx_manage_power(struct usbnet *dev, int on)
{
	dev->intf->needs_remote_wakeup = on;
	return 0;
}

static const struct driver_info smsc75xx_info = {
	.description	= "smsc75xx USB 2.0 Gigabit Ethernet",
	.bind		= smsc75xx_bind,
	.unbind		= smsc75xx_unbind,
	.link_reset	= smsc75xx_link_reset,
	.reset		= smsc75xx_reset,
	.rx_fixup	= smsc75xx_rx_fixup,
	.tx_fixup	= smsc75xx_tx_fixup,
	.status		= smsc75xx_status,
	.manage_power	= smsc75xx_manage_power,
	.flags		= FLAG_ETHER | FLAG_SEND_ZLP | FLAG_LINK_INTR,
};

static const struct usb_device_id products[] = {
	{
		/* SMSC7500 USB Gigabit Ethernet Device */
		USB_DEVICE(USB_VENDOR_ID_SMSC, USB_PRODUCT_ID_LAN7500),
		.driver_info = (unsigned long) &smsc75xx_info,
	},
	{
		/* SMSC7500 USB Gigabit Ethernet Device */
		USB_DEVICE(USB_VENDOR_ID_SMSC, USB_PRODUCT_ID_LAN7505),
		.driver_info = (unsigned long) &smsc75xx_info,
	},
	{ },		/* END */
};
MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver smsc75xx_driver = {
	.name		= SMSC_CHIPNAME,
	.id_table	= products,
	.probe		= usbnet_probe,
	.suspend	= smsc75xx_suspend,
	.resume		= smsc75xx_resume,
	.reset_resume	= smsc75xx_resume,
	.disconnect	= usbnet_disconnect,
	.disable_hub_initiated_lpm = 1,
	.supports_autosuspend = 1,
};

module_usb_driver(smsc75xx_driver);

MODULE_AUTHOR("Nancy Lin");
MODULE_AUTHOR("Steve Glendinning <steve.glendinning@shawell.net>");
MODULE_DESCRIPTION("SMSC75XX USB 2.0 Gigabit Ethernet Devices");
MODULE_LICENSE("GPL");
