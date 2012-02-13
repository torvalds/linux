/*
 * Davicom DM9620 USB 2.0 10/100Mbps ethernet devices
 *
 * Peter Korsgaard <jacmet@sunsite.dk>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 * V1.0 - ftp fail fixed
 * V1.1 - model name checking, & ether plug function enhancement [0x4f, 0x20]
 * V1.2 - init tx/rx checksum
 *      - fix dm_write_shared_word(), bug fix
 *      - fix 10 Mbps link at power saving mode fail  
 * V1.3 - Support kernel 2.6.31
 * V1.4 - Support eeprom write of ethtool 
 *        Support DM9685
 *        Transmit Check Sum Control by Optopn (Source Code Default: Disable)
 *        Recieve Drop Check Sum Error Packet Disable as chip default
 * V1.5 - Support RK2818 (Debug the Register Function) 
 */

//#define DEBUG

#define RK2818


#include <linux/module.h>
//#include <linux/kernel.h> // new v1.3
#include <linux/sched.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/usb.h>
#include <linux/crc32.h>
#include <linux/usb/usbnet.h>
#include <linux/ctype.h>
#include <linux/skbuff.h>   
#include <linux/version.h> // new v1.3

/* datasheet:
 http://www.davicom.com.tw
*/

/* control requests */
#define DM_READ_REGS	0x00
#define DM_WRITE_REGS	0x01
#define DM_READ_MEMS	0x02
#define DM_WRITE_REG	0x03
#define DM_WRITE_MEMS	0x05
#define DM_WRITE_MEM	0x07

/* registers */
#define DM_NET_CTRL	0x00
#define DM_RX_CTRL	0x05
#define DM_SHARED_CTRL	0x0b
#define DM_SHARED_ADDR	0x0c
#define DM_SHARED_DATA	0x0d	/* low + high */
#define DM_EE_PHY_L	0x0d
#define DM_EE_PHY_H	0x0e
#define DM_WAKEUP_CTRL  0x0f
#define DM_PHY_ADDR	0x10	/* 6 bytes */
#define DM_MCAST_ADDR	0x16	/* 8 bytes */
#define DM_GPR_CTRL	0x1e
#define DM_GPR_DATA	0x1f
#define DM_XPHY_CTRL	0x2e
#define DM_TX_CRC_CTRL	0x31
#define DM_RX_CRC_CTRL	0x32 
#define DM_SMIREG       0x91
#define USB_CTRL	0xf4
#define PHY_SPEC_CFG	20

#define MD96XX_EEPROM_MAGIC	0x9620
#define DM_MAX_MCAST	64
#define DM_MCAST_SIZE	8
#define DM_EEPROM_LEN	256
#define DM_TX_OVERHEAD	2	/* 2 byte header */
#define DM_RX_OVERHEAD_9601	7	/* 3 byte header + 4 byte crc tail */
#define DM_RX_OVERHEAD		8	/* 4 byte header + 4 byte crc tail */
#define DM_TIMEOUT	1000
#define DM_MODE9620     0x80
#define DM_TX_CS_EN	0        /* Transmit Check Sum Control */

struct dm96xx_priv {
	int	flag_fail_count;
	u8     mode_9620;	
};     


static int dm_read(struct usbnet *dev, u8 reg, u16 length, void *data)
{
	devdbg(dev, "dm_read() reg=0x%02x length=%d", reg, length);
	return usb_control_msg(dev->udev,
			       usb_rcvctrlpipe(dev->udev, 0),
			       DM_READ_REGS,
			       USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			       0, reg, data, length, USB_CTRL_SET_TIMEOUT); //USB_CTRL_SET_TIMEOUT V.S. USB_CTRL_GET_TIMEOUT
}

static int dm_read_reg(struct usbnet *dev, u8 reg, u8 *value)
{
//	return dm_read(dev, reg, 1, value);

	//__le16 w;
	//int ret = dm_read(dev, reg, 2, &w);  // usb_submit_urb
	//*value= (u8)(w & 0xff);
	//return ret;
	u16 *tmpwPtr;
	int ret;
	tmpwPtr= kmalloc (2, GFP_ATOMIC);
	if (!tmpwPtr)
	{
		printk("+++++++++++ JJ5 dm_read_reg() Error: can not kmalloc!\n"); //usbnet_suspend (intf, message);
		return 0; 
	}
	
	ret = dm_read(dev, reg, 2, tmpwPtr);  // usb_submit_urb v.s. usb_control_msg
	*value= (u8)(*tmpwPtr & 0xff);
	
	kfree (tmpwPtr);
	return ret;
}

static int dm_write(struct usbnet *dev, u8 reg, u16 length, void *data)
{
	devdbg(dev, "dm_write() reg=0x%02x, length=%d", reg, length);
	return usb_control_msg(dev->udev,
			       usb_sndctrlpipe(dev->udev, 0),
			       DM_WRITE_REGS,
			       USB_DIR_OUT | USB_TYPE_VENDOR |USB_RECIP_DEVICE,
			       0, reg, data, length, USB_CTRL_SET_TIMEOUT);
}

static int dm_write_reg(struct usbnet *dev, u8 reg, u8 value)
{
	devdbg(dev, "dm_write_reg() reg=0x%02x, value=0x%02x", reg, value);
	return usb_control_msg(dev->udev,
			       usb_sndctrlpipe(dev->udev, 0),
			       DM_WRITE_REG,
			       USB_DIR_OUT | USB_TYPE_VENDOR |USB_RECIP_DEVICE,
			       value, reg, NULL, 0, USB_CTRL_SET_TIMEOUT);
}

static void dm_write_async_callback(struct urb *urb)
{
	struct usb_ctrlrequest *req = (struct usb_ctrlrequest *)urb->context;

	if (urb->status < 0)
		printk(KERN_DEBUG "dm_write_async_callback() failed with %d\n",
		       urb->status);

	kfree(req);
	usb_free_urb(urb);
}

static void dm_write_async_helper(struct usbnet *dev, u8 reg, u8 value,
				  u16 length, void *data)
{
	struct usb_ctrlrequest *req;
	struct urb *urb;
	int status;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		deverr(dev, "Error allocating URB in dm_write_async_helper!");
		return;
	}

	req = kmalloc(sizeof(struct usb_ctrlrequest), GFP_ATOMIC);
	if (!req) {
		deverr(dev, "Failed to allocate memory for control request");
		usb_free_urb(urb);
		return;
	}

	req->bRequestType = USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE;
	req->bRequest = length ? DM_WRITE_REGS : DM_WRITE_REG;
	req->wValue = cpu_to_le16(value);
	req->wIndex = cpu_to_le16(reg);
	req->wLength = cpu_to_le16(length);

	usb_fill_control_urb(urb, dev->udev,
			     usb_sndctrlpipe(dev->udev, 0),
			     (void *)req, data, length,
			     dm_write_async_callback, req);

	status = usb_submit_urb(urb, GFP_ATOMIC);
	if (status < 0) {
		deverr(dev, "Error submitting the control message: status=%d",
		       status);
		kfree(req);
		usb_free_urb(urb);
	}
}

static void dm_write_async(struct usbnet *dev, u8 reg, u16 length, void *data)
{
	devdbg(dev, "dm_write_async() reg=0x%02x length=%d", reg, length);

	dm_write_async_helper(dev, reg, 0, length, data);
}

static void dm_write_reg_async(struct usbnet *dev, u8 reg, u8 value)
{
	devdbg(dev, "dm_write_reg_async() reg=0x%02x value=0x%02x",
	       reg, value);

	dm_write_async_helper(dev, reg, value, 0, NULL);
}

static int dm_read_shared_word(struct usbnet *dev, int phy, u8 reg, __le16 *value)
{
	int ret, i;

	mutex_lock(&dev->phy_mutex);

	dm_write_reg(dev, DM_SHARED_ADDR, phy ? (reg | 0x40) : reg);
	dm_write_reg(dev, DM_SHARED_CTRL, phy ? 0xc : 0x4);

	for (i = 0; i < DM_TIMEOUT; i++) {
		u8 tmp;

		udelay(1);
		ret = dm_read_reg(dev, DM_SHARED_CTRL, &tmp);
		if (ret < 0)
			goto out;

		/* ready */
		if ((tmp & 1) == 0)
			break;
	}

	if (i == DM_TIMEOUT) {
		deverr(dev, "%s read timed out!", phy ? "phy" : "eeprom");
		ret = -EIO;
		goto out;
	}

	dm_write_reg(dev, DM_SHARED_CTRL, 0x0);
	ret = dm_read(dev, DM_SHARED_DATA, 2, value);

	devdbg(dev, "read shared %d 0x%02x returned 0x%04x, %d",
	       phy, reg, *value, ret);

 out:
	mutex_unlock(&dev->phy_mutex);
	return ret;
}

static int dm_write_shared_word(struct usbnet *dev, int phy, u8 reg, __le16 value)
{
	int ret, i;

	mutex_lock(&dev->phy_mutex);

	ret = dm_write(dev, DM_SHARED_DATA, 2, &value);
	if (ret < 0)
		goto out;

	dm_write_reg(dev, DM_SHARED_ADDR, phy ? (reg | 0x40) : reg);
	//dm_write_reg(dev, DM_SHARED_CTRL, phy ? 0x0a : 0x12);
	if (!phy) dm_write_reg(dev, DM_SHARED_CTRL, 0x10);
	dm_write_reg(dev, DM_SHARED_CTRL, phy ? 0x0a : 0x12);
	dm_write_reg(dev, DM_SHARED_CTRL, 0x10);

	for (i = 0; i < DM_TIMEOUT; i++) {
		u8 tmp;

		udelay(1);
		ret = dm_read_reg(dev, DM_SHARED_CTRL, &tmp);
		if (ret < 0)
			goto out;

		/* ready */
		if ((tmp & 1) == 0)
			break;
	}

	if (i == DM_TIMEOUT) {
		deverr(dev, "%s write timed out!", phy ? "phy" : "eeprom");
		ret = -EIO;
		goto out;
	}

	dm_write_reg(dev, DM_SHARED_CTRL, 0x0);

out:
	mutex_unlock(&dev->phy_mutex);
	return ret;
}

static int dm_write_eeprom_word(struct usbnet *dev, int phy, u8 offset, u8 value)
{
	int ret, i;
	u8  reg,dloc;
	__le16 eeword;

	//devwarn(dev, " offset =0x%x value = 0x%x ", offset,value);
	
	/* hank: from offset to determin eeprom word register location,reg */
	reg = (offset >> 1)&0xff;

	/* hank:  high/low byte by odd/even of offset  */
	dloc = (offset & 0x01)? DM_EE_PHY_H:DM_EE_PHY_L;

	/* retrieve high and low byte from the corresponding reg*/
	ret=dm_read_shared_word(dev,0,reg,&eeword);
	//devwarn(dev, " reg =0x%x dloc = 0x%x eeword = 0x%4x", reg,dloc,eeword);
 
	mutex_lock(&dev->phy_mutex);
	/* hank: write data to eeprom high/low byte reg */
	 dm_write(dev, (offset & 0x01)? DM_EE_PHY_H:DM_EE_PHY_L, 1, &value);

	/* load the unaffected word to value */
	(offset & 0x01)? (value = eeword << 8):(value = eeword >> 8);

	/* write the not modified 8 bits back to its origional high/low byte reg */ 
	dm_write(dev, (offset & 0x01)? DM_EE_PHY_L:DM_EE_PHY_H, 1, &value);
	if (ret < 0)
		goto out;

	/* hank : write word location to reg 0x0c  */
	ret = dm_write_reg(dev, DM_SHARED_ADDR, reg);
		
	if (!phy) dm_write_reg(dev, DM_SHARED_CTRL, 0x10);
	dm_write_reg(dev, DM_SHARED_CTRL, 0x12);
	dm_write_reg(dev, DM_SHARED_CTRL, 0x10);

	for (i = 0; i < DM_TIMEOUT; i++) {
		u8 tmp;

		udelay(1);
		ret = dm_read_reg(dev, DM_SHARED_CTRL, &tmp);
		if (ret < 0)
			goto out;

		/* ready */
		if ((tmp & 1) == 0)
			break;
	}

	if (i == DM_TIMEOUT) {
		deverr(dev, "%s write timed out!", phy ? "phy" : "eeprom");
		ret = -EIO;
		goto out;
	}

	//dm_write_reg(dev, DM_SHARED_CTRL, 0x0);

out:
	mutex_unlock(&dev->phy_mutex);
	return ret;
}
static int dm_read_eeprom_word(struct usbnet *dev, u8 offset, void *value)
{
	return dm_read_shared_word(dev, 0, offset, value);
}


static int dm9620_set_eeprom(struct net_device *net,struct ethtool_eeprom *eeprom, u8 *data)
{
	struct usbnet *dev = netdev_priv(net);
	
	devwarn(dev, "EEPROM: magic value, magic = 0x%x offset =0x%x data = 0x%x ",eeprom->magic, eeprom->offset,*data);
	if (eeprom->magic != MD96XX_EEPROM_MAGIC) {
		devwarn(dev, "EEPROM: magic value mismatch, magic = 0x%x",
			eeprom->magic);
		return -EINVAL;
	}

		if(dm_write_eeprom_word(dev, 0, eeprom->offset, *data) < 0)  
		return -EINVAL;
	
	return 0;
}

static int dm9620_get_eeprom_len(struct net_device *dev)
{
	return DM_EEPROM_LEN;
}

static int dm9620_get_eeprom(struct net_device *net,
			     struct ethtool_eeprom *eeprom, u8 * data)
{
	struct usbnet *dev = netdev_priv(net);
	__le16 *ebuf = (__le16 *) data;
	int i;

	/* access is 16bit */
	if ((eeprom->offset % 2) || (eeprom->len % 2))
		return -EINVAL;

	for (i = 0; i < eeprom->len / 2; i++) {
		if (dm_read_eeprom_word(dev, eeprom->offset / 2 + i,
					&ebuf[i]) < 0)
			return -EINVAL;
	}
	return 0;
}

static int dm9620_mdio_read(struct net_device *netdev, int phy_id, int loc)
{
	struct usbnet *dev = netdev_priv(netdev);

	__le16 res;

	if (phy_id) {
		devdbg(dev, "Only internal phy supported");
		return 0;
	}

	dm_read_shared_word(dev, 1, loc, &res);

	devdbg(dev,
	       "dm9620_mdio_read() phy_id=0x%02x, loc=0x%02x, returns=0x%04x",
	       phy_id, loc, le16_to_cpu(res));

	return le16_to_cpu(res);
}

static void dm9620_mdio_write(struct net_device *netdev, int phy_id, int loc,
			      int val)
{
	struct usbnet *dev = netdev_priv(netdev);
	__le16 res = cpu_to_le16(val);
	int mdio_val;

	if (phy_id) {
		devdbg(dev, "Only internal phy supported");
		return;
	}

	devdbg(dev,"dm9620_mdio_write() phy_id=0x%02x, loc=0x%02x, val=0x%04x",
	       phy_id, loc, val);

	dm_write_shared_word(dev, 1, loc, res);
	mdelay(1);
	mdio_val = dm9620_mdio_read(netdev, phy_id, loc);

}

static void dm9620_get_drvinfo(struct net_device *net,
			       struct ethtool_drvinfo *info)
{
	/* Inherit standard device info */
	usbnet_get_drvinfo(net, info);
	info->eedump_len = DM_EEPROM_LEN;
}

static u32 dm9620_get_link(struct net_device *net)
{
	struct usbnet *dev = netdev_priv(net);

	return mii_link_ok(&dev->mii);
}

static int dm9620_ioctl(struct net_device *net, struct ifreq *rq, int cmd)
{
	struct usbnet *dev = netdev_priv(net);

	return generic_mii_ioctl(&dev->mii, if_mii(rq), cmd, NULL);
}


#define DM_LINKEN  (1<<5)
#define DM_MAGICEN (1<<3)
#define DM_LINKST  (1<<2)
#define DM_MAGICST (1<<0)

static void
dm9620_get_wol(struct net_device *net, struct ethtool_wolinfo *wolinfo)
{
	struct usbnet *dev = netdev_priv(net);
	u8 opt;

	if (dm_read_reg(dev, DM_WAKEUP_CTRL, &opt) < 0) {
		wolinfo->supported = 0;
		wolinfo->wolopts = 0;
		return;
	}
	wolinfo->supported = WAKE_PHY | WAKE_MAGIC;
	wolinfo->wolopts = 0;

	if (opt & DM_LINKEN)
		wolinfo->wolopts |= WAKE_PHY;
	if (opt & DM_MAGICEN)
		wolinfo->wolopts |= WAKE_MAGIC;
}


static int
dm9620_set_wol(struct net_device *net, struct ethtool_wolinfo *wolinfo)
{
	struct usbnet *dev = netdev_priv(net);
	u8 opt = 0;

	if (wolinfo->wolopts & WAKE_PHY)
		opt |= DM_LINKEN;
	if (wolinfo->wolopts & WAKE_MAGIC)
		opt |= DM_MAGICEN;

	dm_write_reg(dev, DM_NET_CTRL, 0x48);  // enable WAKEEN 
	
	dm_write_reg(dev, 0x92, 0x3f); //keep clock on Hank Jun 30
	
	return dm_write_reg(dev, DM_WAKEUP_CTRL, opt);
}

static struct ethtool_ops dm9620_ethtool_ops = {
	.get_drvinfo	= dm9620_get_drvinfo,
	.get_link	= dm9620_get_link,
	.get_msglevel	= usbnet_get_msglevel,
	.set_msglevel	= usbnet_set_msglevel,
	.get_eeprom_len	= dm9620_get_eeprom_len,
	.get_eeprom	= dm9620_get_eeprom,
	.set_eeprom	= dm9620_set_eeprom,
	.get_settings	= usbnet_get_settings,
	.set_settings	= usbnet_set_settings,
	.nway_reset	= usbnet_nway_reset,
	.get_wol	= dm9620_get_wol,
	.set_wol	= dm9620_set_wol,
};

static void dm9620_set_multicast(struct net_device *net)
{
        struct usbnet *dev = netdev_priv(net);
        /* We use the 20 byte dev->data for our 8 byte filter buffer
         * to avoid allocating memory that is tricky to free later */
        u8 *hashes = (u8 *) & dev->data;
        u8 rx_ctl = 0x31;

        memset(hashes, 0x00, DM_MCAST_SIZE);
        hashes[DM_MCAST_SIZE - 1] |= 0x80;      /* broadcast address */

        if (net->flags & IFF_PROMISC) {
                rx_ctl |= 0x02;
        } else if (net->flags & IFF_ALLMULTI ||
                   netdev_mc_count(net) > DM_MAX_MCAST) {
                rx_ctl |= 0x04;
        } else if (!netdev_mc_empty(net)) {
                struct netdev_hw_addr *ha;

                netdev_for_each_mc_addr(ha, net) {
                        u32 crc = ether_crc(ETH_ALEN, ha->addr) >> 26;
                        hashes[crc >> 3] |= 1 << (crc & 0x7);
                }
        }

        dm_write_async(dev, DM_MCAST_ADDR, DM_MCAST_SIZE, hashes);
        dm_write_reg_async(dev, DM_RX_CTRL, rx_ctl);
}

 
 static void __dm9620_set_mac_address(struct usbnet *dev)
 {
         dm_write_async(dev, DM_PHY_ADDR, ETH_ALEN, dev->net->dev_addr);
 }
 
 static int dm9620_set_mac_address(struct net_device *net, void *p)
 {
         struct sockaddr *addr = p;
         struct usbnet *dev = netdev_priv(net);
	 int i;
 
#if 1
	 printk("[dm96] Set mac addr %pM\n", addr->sa_data);  // %x:%x:...
	 printk("[dm96] ");
	 for (i=0; i<net->addr_len; i++)
	 printk("[%02x] ", addr->sa_data[i]);
	 printk("\n");
 #endif
 
         if (!is_valid_ether_addr(addr->sa_data)) {
                 dev_err(&net->dev, "not setting invalid mac address %pM\n",
                                                                 addr->sa_data);
                 return -EINVAL;
         }
 
         memcpy(net->dev_addr, addr->sa_data, net->addr_len);
         __dm9620_set_mac_address(dev);
 
         return 0;
 }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31) 

static const struct net_device_ops vm_netdev_ops= { // new kernel 2.6.31  (20091217JJ)

            .ndo_open               = usbnet_open,  
            .ndo_stop               = usbnet_stop,  
            .ndo_start_xmit         = usbnet_start_xmit, 
            .ndo_tx_timeout         = usbnet_tx_timeout, 
            .ndo_change_mtu         = usbnet_change_mtu, 
            .ndo_validate_addr      = eth_validate_addr, 
	    .ndo_do_ioctl	    = dm9620_ioctl,   
	    .ndo_set_multicast_list = dm9620_set_multicast,   
            .ndo_set_mac_address    = dm9620_set_mac_address,  
};
#endif

static int dm9620_bind(struct usbnet *dev, struct usb_interface *intf)
{
	int ret,mdio_val,i;
	struct dm96xx_priv* priv;
	u8 temp;
	u8 tmp;

	ret = usbnet_get_endpoints(dev, intf);
	if (ret)
		goto out;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31) 
	dev->net->netdev_ops = &vm_netdev_ops; // new kernel 2.6.31  (20091217JJ)
	dev->net->ethtool_ops = &dm9620_ethtool_ops;
#else
	dev->net->do_ioctl = dm9620_ioctl;  
	dev->net->set_multicast_list = dm9620_set_multicast;
	dev->net->ethtool_ops = &dm9620_ethtool_ops;
#endif
	dev->net->hard_header_len += DM_TX_OVERHEAD;
	dev->hard_mtu = dev->net->mtu + dev->net->hard_header_len;
#ifdef RK2818   //harris 2010.12.27
	dev->rx_urb_size = 8912; // ftp fail fixed
#else
	dev->rx_urb_size = 2048;//dev->net->mtu + ETH_HLEN + DM_RX_OVERHEAD+1; // ftp fail fixed
#endif

	dev->mii.dev = dev->net;
	dev->mii.mdio_read = dm9620_mdio_read;
	dev->mii.mdio_write = dm9620_mdio_write;
	dev->mii.phy_id_mask = 0x1f;
	dev->mii.reg_num_mask = 0x1f;

//JJ1
	if ( (ret= dm_read_reg(dev, 0x29, &tmp)) >=0)
		printk("++++++[dm962]+++++ dm_read_reg() 0x29 0x%02x\n",tmp);
	else
		printk("++++++[dm962]+++++ dm_read_reg() 0x29 fail-func-return %d\n", ret);
		
	if ( (ret= dm_read_reg(dev, 0x28, &tmp)) >=0)
		printk("++++++[dm962]+++++ dm_read_reg() 0x28 0x%02x\n",tmp);
	else
		printk("++++++[dm962]+++++ dm_read_reg() 0x28 fail-func-return %d\n", ret);
		
	if ( (ret= dm_read_reg(dev, 0x2b, &tmp)) >=0)
		printk("++++++[dm962]+++++ dm_read_reg() 0x2b 0x%02x\n",tmp);
	else
		printk("++++++[dm962]+++++ dm_read_reg() 0x2b fail-func-return %d\n", ret);
	if ( (ret= dm_read_reg(dev, 0x2a, &tmp)) >=0)
		printk("++++++[dm962]+++++ dm_read_reg() 0x2a 0x%02x\n",tmp);
	else
		printk("++++++[dm962]+++++ dm_read_reg() 0x2a fail-func-return %d\n", ret);
		
//JJ3
	if ( (ret= dm_read_reg(dev, 0xF2, &tmp)) >=0)
		printk("++++++[dm962]+++++ dm_read_reg() 0xF2 0x%02x\n",tmp);
	else
		printk("++++++[dm962]+++++ dm_read_reg() 0xF2 fail-func-return %d\n", ret);
		
		printk("++++++[dm962]+++++  [Analysis.2] 0xF2, D[7] %d %s\n", tmp>>7, (tmp&(1<<7))? "Err: RX Unexpected condition": "OK" );
		printk("++++++[dm962]+++++  [Analysis.2] 0xF2, D[6] %d %s\n", (tmp>>6)&1, (tmp&(1<<6))? "Err: Host Suspend condition": "OK" );
		printk("++++++[dm962]+++++  [Analysis.2] 0xF2, D[5] %d %s\n", (tmp>>5)&1, (tmp&(1<<5))? "EP1: Data Ready": "EP1: Empty" );
		printk("++++++[dm962]+++++  [Analysis.2] 0xF2, D[3] %d %s\n", (tmp>>3)&1, (tmp&(1<<3))? "Err: Bulk out condition": "OK" );
		
		printk("++++++[dm962]+++++  [Analysis.2] 0xF2, D[2] %d %s\n", (tmp>>2)&1, (tmp&(1<<2))? "Err: TX Buffer full": "OK" );
		printk("++++++[dm962]+++++  [Analysis.2] 0xF2, D[1] %d %s\n", (tmp>>1)&1, (tmp&(1<<1))? "Warn: TX buffer Almost full": "OK" );
		printk("++++++[dm962]+++++  [Analysis.2] 0xF2, D[0] %d %s\n", (tmp>>0)&1, (tmp&(1<<0))? "Status: TX buffer has pkts": "Status: TX buffer 0 pkts" );

	/* reset */
	dm_write_reg(dev, DM_XPHY_CTRL, 0); // dm9622/dm9685, bit[5](EXTERNAL), clear-it, add clk & limit to internal PHY
	dm_write_reg(dev, DM_NET_CTRL, 1);
	udelay(20);
	
	

	/* Add V1.1, Enable auto link while plug in RJ45, Hank July 20, 2009*/
	dm_write_reg(dev, USB_CTRL, 0x20); 
	/* read MAC */
	if (dm_read(dev, DM_PHY_ADDR, ETH_ALEN, dev->net->dev_addr) < 0) {
		printk(KERN_ERR "Error reading MAC address\n");
		ret = -ENODEV;
		goto out;
	}

#if 1
	 printk("[dm96] Chk mac addr %pM\n", dev->net->dev_addr);  // %x:%x...
	 printk("[dm96] ");
	 for (i=0; i<ETH_ALEN; i++)
	 printk("[%02x] ", dev->net->dev_addr[i]);
	 printk("\n");
#endif

	/* read SMI mode register */
        priv = dev->driver_priv = kmalloc(sizeof(struct dm96xx_priv), GFP_ATOMIC);
	if (!priv) {
		deverr(dev, "Failed to allocate memory for dm96xx_priv");
		ret = -ENOMEM;
		goto out;
	}
	
        /* work-around for 9620 mode */
	printk("[dm96] Fixme: work around for 9620 mode\n");
	printk("[dm96] Add tx_fixup() debug...\n");
	dm_write_reg(dev, DM_MCAST_ADDR, 0);     // clear data bus to 0s
	dm_read_reg(dev, DM_MCAST_ADDR, &temp);  // clear data bus to 0s
	ret = dm_read_reg(dev, DM_SMIREG, &temp);   // Must clear data bus before we can read the 'MODE9620' bit

	priv->flag_fail_count= 0;
	if (ret<0) {
		printk(KERN_ERR "[dm96] Error read SMI register\n");
	}
	else priv->mode_9620 = temp & DM_MODE9620;

	printk(KERN_WARNING "[dm96] 9620 Mode = %d\n", priv->mode_9620);
	
	/* power up phy */
	dm_write_reg(dev, DM_GPR_CTRL, 1);
	dm_write_reg(dev, DM_GPR_DATA, 0);

	/* Init tx/rx checksum */
#if 	DM_TX_CS_EN
	dm_write_reg(dev, DM_TX_CRC_CTRL, 7);
#endif 
	dm_write_reg(dev, DM_RX_CRC_CTRL, 2);

	/* receive broadcast packets */
	dm9620_set_multicast(dev->net);
	dm9620_mdio_write(dev->net, dev->mii.phy_id, MII_BMCR, BMCR_RESET);

	/* Hank add, work for comapubility issue (10M Power control) */	
	
	dm9620_mdio_write(dev->net, dev->mii.phy_id, PHY_SPEC_CFG, 0x800);
	//printk("[dm96] dm962++ write phy[20]= 0x800\n");
	mdio_val = dm9620_mdio_read(dev->net, dev->mii.phy_id, PHY_SPEC_CFG);
	//printk("[dm96] dm962++ read  phy[20]= %x\n",mdio_val );
	
	dm9620_mdio_write(dev->net, dev->mii.phy_id, MII_ADVERTISE,
			  ADVERTISE_ALL | ADVERTISE_CSMA | ADVERTISE_PAUSE_CAP);
	mii_nway_restart(&dev->mii); 
	
out:
	return ret;
}

void dm9620_unbind(struct usbnet *dev, struct usb_interface *intf)
{
struct dm96xx_priv* priv= dev->driver_priv;
//u8 opt=0;
//int i;
	printk("dm9620_unbind():\n");

	printk("flag_fail_count  %lu\n", (long unsigned int)priv->flag_fail_count);
	kfree(dev->driver_priv); // displayed dev->.. above, then can free dev 

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31) 
	printk("rx_length_errors %lu\n",dev->net->stats.rx_length_errors);
	printk("rx_over_errors   %lu\n",dev->net->stats.rx_over_errors  );
	printk("rx_crc_errors    %lu\n",dev->net->stats.rx_crc_errors   );
	printk("rx_frame_errors  %lu\n",dev->net->stats.rx_frame_errors );
	printk("rx_fifo_errors   %lu\n",dev->net->stats.rx_fifo_errors  );
	printk("rx_missed_errors %lu\n",dev->net->stats.rx_missed_errors);	
#else
	printk("rx_length_errors %lu\n",dev->stats.rx_length_errors);
	printk("rx_over_errors   %lu\n",dev->stats.rx_over_errors  );
	printk("rx_crc_errors    %lu\n",dev->stats.rx_crc_errors   );
	printk("rx_frame_errors  %lu\n",dev->stats.rx_frame_errors );
	printk("rx_fifo_errors   %lu\n",dev->stats.rx_fifo_errors  );
	printk("rx_missed_errors %lu\n",dev->stats.rx_missed_errors);	
#endif

// check if dm9620 receive magic packet
//i=dm_read_reg(dev, DM_WAKEUP_CTRL, &opt);
//	printk("rx_magic_packet   %lu\n",i);

}

static int dm9620_rx_fixup(struct usbnet *dev, struct sk_buff *skb)
{
	u8 status;
	int len;
	struct dm96xx_priv* priv = (struct dm96xx_priv *)dev->driver_priv;

	/* 9620 format:
	   b0: rx status
	   b1: packet length (incl crc) low
	   b2: packet length (incl crc) high
	   b3..n-4: packet data
	   bn-3..bn: ethernet crc
	 */

	/* 9620 format:
	   one additional byte then 9620 : 
	   rx_flag in the first pos
	 */

	if (unlikely(skb->len < DM_RX_OVERHEAD_9601)) {   // 20090623
		dev_err(&dev->udev->dev, "unexpected tiny rx frame\n");
		return 0;
	}

	if (priv->mode_9620) {
		/* mode 9620 */

		if (unlikely(skb->len < DM_RX_OVERHEAD)) {  // 20090623
			dev_err(&dev->udev->dev, "unexpected tiny rx frame\n");
			return 0;
		}

	//.	struct dm96xx_priv* priv= dev->driver_priv;
	//	if (skb->data[0]!=0x01)
	//		priv->flag_fail_count++;

		status = skb->data[1];
		len = (skb->data[2] | (skb->data[3] << 8)) - 4;
		
		if (unlikely(status & 0xbf)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31) 
			if (status & 0x01) dev->net->stats.rx_fifo_errors++;
			if (status & 0x02) dev->net->stats.rx_crc_errors++;
			if (status & 0x04) dev->net->stats.rx_frame_errors++;
			if (status & 0x20) dev->net->stats.rx_missed_errors++;
			if (status & 0x90) dev->net->stats.rx_length_errors++;
#else
			if (status & 0x01) dev->stats.rx_fifo_errors++;
			if (status & 0x02) dev->stats.rx_crc_errors++;
			if (status & 0x04) dev->stats.rx_frame_errors++;
			if (status & 0x20) dev->stats.rx_missed_errors++;
			if (status & 0x90) dev->stats.rx_length_errors++;
#endif
			return 0;
		}

		skb_pull(skb, 4);
		skb_trim(skb, len);

	}
	else { /* mode 9620 (original driver code) */
		status = skb->data[0];
		len = (skb->data[1] | (skb->data[2] << 8)) - 4;
		
		if (unlikely(status & 0xbf)) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31) 
			if (status & 0x01) dev->net->stats.rx_fifo_errors++;
			if (status & 0x02) dev->net->stats.rx_crc_errors++;
			if (status & 0x04) dev->net->stats.rx_frame_errors++;
			if (status & 0x20) dev->net->stats.rx_missed_errors++;
			if (status & 0x90) dev->net->stats.rx_length_errors++;
#else
			if (status & 0x01) dev->stats.rx_fifo_errors++;
			if (status & 0x02) dev->stats.rx_crc_errors++;
			if (status & 0x04) dev->stats.rx_frame_errors++;
			if (status & 0x20) dev->stats.rx_missed_errors++;
			if (status & 0x90) dev->stats.rx_length_errors++;
#endif
			return 0;
		}

		skb_pull(skb, 3);
		skb_trim(skb, len);
	}

	return 1;
} // 'priv'

static struct sk_buff *dm9620_tx_fixup(struct usbnet *dev, struct sk_buff *skb,
				       gfp_t flags)
{
	int len;

	/* format:
	   b0: packet length low
	   b1: packet length high
	   b3..n: packet data
	*/

	len = skb->len;

	if (skb_headroom(skb) < DM_TX_OVERHEAD) {
		struct sk_buff *skb2;
		skb2 = skb_copy_expand(skb, DM_TX_OVERHEAD, 0, flags);
		dev_kfree_skb_any(skb);
		skb = skb2;
		if (!skb)
			return NULL;
	}

	__skb_push(skb, DM_TX_OVERHEAD);

	/* usbnet adds padding if length is a multiple of packet size
	   if so, adjust length value in header */
	if ((skb->len % dev->maxpacket) == 0)
		len++;

	skb->data[0] = len;
	skb->data[1] = len >> 8;

	/* hank, recalcute checksum of TCP */

	
	return skb;
} // 'kb'

static void dm9620_status(struct usbnet *dev, struct urb *urb)
{
	int link;
	u8 *buf;

	/* format:
	   b0: net status
	   b1: tx status 1
	   b2: tx status 2
	   b3: rx status
	   b4: rx overflow
	   b5: rx count
	   b6: tx count
	   b7: gpr
	*/

	if (urb->actual_length < 8)
		return;

	buf = urb->transfer_buffer;

	link = !!(buf[0] & 0x40);
	if (netif_carrier_ok(dev->net) != link) {
		if (link) {
			netif_carrier_on(dev->net);
			usbnet_defer_kevent (dev, EVENT_LINK_RESET);
		}
		else
			netif_carrier_off(dev->net);
		devdbg(dev, "Link Status is: %d", link);
	}
}

static int dm9620_link_reset(struct usbnet *dev)
{
	struct ethtool_cmd ecmd;
	mii_check_media(&dev->mii, 1, 1);
	mii_ethtool_gset(&dev->mii, &ecmd);
	/* hank add*/
dm9620_mdio_write(dev->net, dev->mii.phy_id, PHY_SPEC_CFG, 0x800);
	devdbg(dev, "link_reset() speed: %d duplex: %d",
	       ecmd.speed, ecmd.duplex);
	
	return 0;
}

static const struct driver_info dm9620_info = {
	.description	= "Davicom DM9620 USB Ethernet",
	.flags		= FLAG_ETHER,
	.bind		= dm9620_bind,
	.rx_fixup	= dm9620_rx_fixup,
	.tx_fixup	= dm9620_tx_fixup,
	.status		= dm9620_status,
#ifdef RK2818  //harris 2010.12.27

#else
	.link_reset	= dm9620_link_reset,
	.reset		= dm9620_link_reset,
#endif
	.unbind     = dm9620_unbind,
};

static const struct usb_device_id products[] = {
	{
	 USB_DEVICE(0x07aa, 0x9601),	/* Corega FEther USB-TXC */
	 .driver_info = (unsigned long)&dm9620_info,
	 },
	{
	 USB_DEVICE(0x0a46, 0x9601),	/* Davicom USB-100 */
	 .driver_info = (unsigned long)&dm9620_info,
	 },
	{
	 USB_DEVICE(0x0a46, 0x6688),	/* ZT6688 USB NIC */
	 .driver_info = (unsigned long)&dm9620_info,
	 },
	{
	 USB_DEVICE(0x0a46, 0x0268),	/* ShanTou ST268 USB NIC */
	 .driver_info = (unsigned long)&dm9620_info,
	 },
	{
	 USB_DEVICE(0x0a46, 0x8515),	/* ADMtek ADM8515 USB NIC */
	 .driver_info = (unsigned long)&dm9620_info,
	 },
	{
	USB_DEVICE(0x0a47, 0x9601),	/* Hirose USB-100 */
	.driver_info = (unsigned long)&dm9620_info,
	 },
        {
        USB_DEVICE(0x0a46, 0x9620),     /* Davicom 9620 */
        .driver_info = (unsigned long)&dm9620_info,
         },
        {
        USB_DEVICE(0x0a46, 0x9621),     /* Davicom 9621 */
        .driver_info = (unsigned long)&dm9620_info,
         },
        {
        USB_DEVICE(0x0a46, 0x9622),     /* Davicom 9622 */
        .driver_info = (unsigned long)&dm9620_info,
         },
        {
	USB_DEVICE(0x0fe6, 0x8101),     /* Davicom 9601 USB to Fast Ethernet Adapter */
        .driver_info = (unsigned long)&dm9620_info,
         },
	{},			// END
};

MODULE_DEVICE_TABLE(usb, products);

static struct usb_driver dm9620_driver = {
//	.name = "dm9601",
	.name = "dm9620",
	.id_table = products,
	.probe = usbnet_probe,
	.disconnect = usbnet_disconnect,
	.suspend = usbnet_suspend,
	.resume = usbnet_resume,
};




static int __init dm9620_init(void)
{
	return usb_register(&dm9620_driver);
}

static void __exit dm9620_exit(void)
{
	usb_deregister(&dm9620_driver);
}

module_init(dm9620_init);
module_exit(dm9620_exit);

MODULE_AUTHOR("Peter Korsgaard <jacmet@sunsite.dk>");
MODULE_DESCRIPTION("Davicom DM9620 USB 2.0 ethernet devices");
MODULE_LICENSE("GPL");

