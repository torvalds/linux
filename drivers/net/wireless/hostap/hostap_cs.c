#define PRISM2_PCCARD

#include <linux/module.h>
#include <linux/if.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include <asm/io.h>

#include "hostap_wlan.h"


static char *dev_info = "hostap_cs";

MODULE_AUTHOR("Jouni Malinen");
MODULE_DESCRIPTION("Support for Intersil Prism2-based 802.11 wireless LAN "
		   "cards (PC Card).");
MODULE_SUPPORTED_DEVICE("Intersil Prism2-based WLAN cards (PC Card)");
MODULE_LICENSE("GPL");


static int ignore_cis_vcc;
module_param(ignore_cis_vcc, int, 0444);
MODULE_PARM_DESC(ignore_cis_vcc, "Ignore broken CIS VCC entry");


/* struct local_info::hw_priv */
struct hostap_cs_priv {
	struct pcmcia_device *link;
	int sandisk_connectplus;
};


#ifdef PRISM2_IO_DEBUG

static inline void hfa384x_outb_debug(struct net_device *dev, int a, u8 v)
{
	struct hostap_interface *iface;
	local_info_t *local;
	unsigned long flags;

	iface = netdev_priv(dev);
	local = iface->local;
	spin_lock_irqsave(&local->lock, flags);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_OUTB, a, v);
	outb(v, dev->base_addr + a);
	spin_unlock_irqrestore(&local->lock, flags);
}

static inline u8 hfa384x_inb_debug(struct net_device *dev, int a)
{
	struct hostap_interface *iface;
	local_info_t *local;
	unsigned long flags;
	u8 v;

	iface = netdev_priv(dev);
	local = iface->local;
	spin_lock_irqsave(&local->lock, flags);
	v = inb(dev->base_addr + a);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_INB, a, v);
	spin_unlock_irqrestore(&local->lock, flags);
	return v;
}

static inline void hfa384x_outw_debug(struct net_device *dev, int a, u16 v)
{
	struct hostap_interface *iface;
	local_info_t *local;
	unsigned long flags;

	iface = netdev_priv(dev);
	local = iface->local;
	spin_lock_irqsave(&local->lock, flags);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_OUTW, a, v);
	outw(v, dev->base_addr + a);
	spin_unlock_irqrestore(&local->lock, flags);
}

static inline u16 hfa384x_inw_debug(struct net_device *dev, int a)
{
	struct hostap_interface *iface;
	local_info_t *local;
	unsigned long flags;
	u16 v;

	iface = netdev_priv(dev);
	local = iface->local;
	spin_lock_irqsave(&local->lock, flags);
	v = inw(dev->base_addr + a);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_INW, a, v);
	spin_unlock_irqrestore(&local->lock, flags);
	return v;
}

static inline void hfa384x_outsw_debug(struct net_device *dev, int a,
				       u8 *buf, int wc)
{
	struct hostap_interface *iface;
	local_info_t *local;
	unsigned long flags;

	iface = netdev_priv(dev);
	local = iface->local;
	spin_lock_irqsave(&local->lock, flags);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_OUTSW, a, wc);
	outsw(dev->base_addr + a, buf, wc);
	spin_unlock_irqrestore(&local->lock, flags);
}

static inline void hfa384x_insw_debug(struct net_device *dev, int a,
				      u8 *buf, int wc)
{
	struct hostap_interface *iface;
	local_info_t *local;
	unsigned long flags;

	iface = netdev_priv(dev);
	local = iface->local;
	spin_lock_irqsave(&local->lock, flags);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_INSW, a, wc);
	insw(dev->base_addr + a, buf, wc);
	spin_unlock_irqrestore(&local->lock, flags);
}

#define HFA384X_OUTB(v,a) hfa384x_outb_debug(dev, (a), (v))
#define HFA384X_INB(a) hfa384x_inb_debug(dev, (a))
#define HFA384X_OUTW(v,a) hfa384x_outw_debug(dev, (a), (v))
#define HFA384X_INW(a) hfa384x_inw_debug(dev, (a))
#define HFA384X_OUTSW(a, buf, wc) hfa384x_outsw_debug(dev, (a), (buf), (wc))
#define HFA384X_INSW(a, buf, wc) hfa384x_insw_debug(dev, (a), (buf), (wc))

#else /* PRISM2_IO_DEBUG */

#define HFA384X_OUTB(v,a) outb((v), dev->base_addr + (a))
#define HFA384X_INB(a) inb(dev->base_addr + (a))
#define HFA384X_OUTW(v,a) outw((v), dev->base_addr + (a))
#define HFA384X_INW(a) inw(dev->base_addr + (a))
#define HFA384X_INSW(a, buf, wc) insw(dev->base_addr + (a), buf, wc)
#define HFA384X_OUTSW(a, buf, wc) outsw(dev->base_addr + (a), buf, wc)

#endif /* PRISM2_IO_DEBUG */


static int hfa384x_from_bap(struct net_device *dev, u16 bap, void *buf,
			    int len)
{
	u16 d_off;
	u16 *pos;

	d_off = (bap == 1) ? HFA384X_DATA1_OFF : HFA384X_DATA0_OFF;
	pos = (u16 *) buf;

	if (len / 2)
		HFA384X_INSW(d_off, buf, len / 2);
	pos += len / 2;

	if (len & 1)
		*((char *) pos) = HFA384X_INB(d_off);

	return 0;
}


static int hfa384x_to_bap(struct net_device *dev, u16 bap, void *buf, int len)
{
	u16 d_off;
	u16 *pos;

	d_off = (bap == 1) ? HFA384X_DATA1_OFF : HFA384X_DATA0_OFF;
	pos = (u16 *) buf;

	if (len / 2)
		HFA384X_OUTSW(d_off, buf, len / 2);
	pos += len / 2;

	if (len & 1)
		HFA384X_OUTB(*((char *) pos), d_off);

	return 0;
}


/* FIX: This might change at some point.. */
#include "hostap_hw.c"



static void prism2_detach(struct pcmcia_device *p_dev);
static void prism2_release(u_long arg);
static int prism2_config(struct pcmcia_device *link);


static int prism2_pccard_card_present(local_info_t *local)
{
	struct hostap_cs_priv *hw_priv = local->hw_priv;
	if (hw_priv != NULL && hw_priv->link != NULL && pcmcia_dev_present(hw_priv->link))
		return 1;
	return 0;
}


/*
 * SanDisk CompactFlash WLAN Flashcard - Product Manual v1.0
 * Document No. 20-10-00058, January 2004
 * http://www.sandisk.com/pdf/industrial/ProdManualCFWLANv1.0.pdf
 */
#define SANDISK_WLAN_ACTIVATION_OFF 0x40
#define SANDISK_HCR_OFF 0x42


static void sandisk_set_iobase(local_info_t *local)
{
	int res;
	struct hostap_cs_priv *hw_priv = local->hw_priv;

	res = pcmcia_write_config_byte(hw_priv->link, 0x10,
				hw_priv->link->resource[0]->start & 0x00ff);
	if (res != 0) {
		printk(KERN_DEBUG "Prism3 SanDisk - failed to set I/O base 0 -"
		       " res=%d\n", res);
	}
	udelay(10);

	res = pcmcia_write_config_byte(hw_priv->link, 0x12,
				(hw_priv->link->resource[0]->start >> 8) & 0x00ff);
	if (res != 0) {
		printk(KERN_DEBUG "Prism3 SanDisk - failed to set I/O base 1 -"
		       " res=%d\n", res);
	}
}


static void sandisk_write_hcr(local_info_t *local, int hcr)
{
	struct net_device *dev = local->dev;
	int i;

	HFA384X_OUTB(0x80, SANDISK_WLAN_ACTIVATION_OFF);
	udelay(50);
	for (i = 0; i < 10; i++) {
		HFA384X_OUTB(hcr, SANDISK_HCR_OFF);
	}
	udelay(55);
	HFA384X_OUTB(0x45, SANDISK_WLAN_ACTIVATION_OFF);
}


static int sandisk_enable_wireless(struct net_device *dev)
{
	int res, ret = 0;
	struct hostap_interface *iface = netdev_priv(dev);
	local_info_t *local = iface->local;
	struct hostap_cs_priv *hw_priv = local->hw_priv;

	if (resource_size(hw_priv->link->resource[0]) < 0x42) {
		/* Not enough ports to be SanDisk multi-function card */
		ret = -ENODEV;
		goto done;
	}

	if (hw_priv->link->manf_id != 0xd601 || hw_priv->link->card_id != 0x0101) {
		/* No SanDisk manfid found */
		ret = -ENODEV;
		goto done;
	}

	if (hw_priv->link->socket->functions < 2) {
		/* No multi-function links found */
		ret = -ENODEV;
		goto done;
	}

	printk(KERN_DEBUG "%s: Multi-function SanDisk ConnectPlus detected"
	       " - using vendor-specific initialization\n", dev->name);
	hw_priv->sandisk_connectplus = 1;

	res = pcmcia_write_config_byte(hw_priv->link, CISREG_COR,
				COR_SOFT_RESET);
	if (res != 0) {
		printk(KERN_DEBUG "%s: SanDisk - COR sreset failed (%d)\n",
		       dev->name, res);
		goto done;
	}
	mdelay(5);

	/*
	 * Do not enable interrupts here to avoid some bogus events. Interrupts
	 * will be enabled during the first cor_sreset call.
	 */
	res = pcmcia_write_config_byte(hw_priv->link, CISREG_COR,
				(COR_LEVEL_REQ | 0x8 | COR_ADDR_DECODE |
					COR_FUNC_ENA));
	if (res != 0) {
		printk(KERN_DEBUG "%s: SanDisk - COR sreset failed (%d)\n",
		       dev->name, res);
		goto done;
	}
	mdelay(5);

	sandisk_set_iobase(local);

	HFA384X_OUTB(0xc5, SANDISK_WLAN_ACTIVATION_OFF);
	udelay(10);
	HFA384X_OUTB(0x4b, SANDISK_WLAN_ACTIVATION_OFF);
	udelay(10);

done:
	return ret;
}


static void prism2_pccard_cor_sreset(local_info_t *local)
{
	int res;
	u8 val;
	struct hostap_cs_priv *hw_priv = local->hw_priv;

	if (!prism2_pccard_card_present(local))
	       return;

	res = pcmcia_read_config_byte(hw_priv->link, CISREG_COR, &val);
	if (res != 0) {
		printk(KERN_DEBUG "prism2_pccard_cor_sreset failed 1 (%d)\n",
		       res);
		return;
	}
	printk(KERN_DEBUG "prism2_pccard_cor_sreset: original COR %02x\n",
		val);

	val |= COR_SOFT_RESET;
	res = pcmcia_write_config_byte(hw_priv->link, CISREG_COR, val);
	if (res != 0) {
		printk(KERN_DEBUG "prism2_pccard_cor_sreset failed 2 (%d)\n",
		       res);
		return;
	}

	mdelay(hw_priv->sandisk_connectplus ? 5 : 2);

	val &= ~COR_SOFT_RESET;
	if (hw_priv->sandisk_connectplus)
		val |= COR_IREQ_ENA;
	res = pcmcia_write_config_byte(hw_priv->link, CISREG_COR, val);
	if (res != 0) {
		printk(KERN_DEBUG "prism2_pccard_cor_sreset failed 3 (%d)\n",
		       res);
		return;
	}

	mdelay(hw_priv->sandisk_connectplus ? 5 : 2);

	if (hw_priv->sandisk_connectplus)
		sandisk_set_iobase(local);
}


static void prism2_pccard_genesis_reset(local_info_t *local, int hcr)
{
	int res;
	u8 old_cor;
	struct hostap_cs_priv *hw_priv = local->hw_priv;

	if (!prism2_pccard_card_present(local))
	       return;

	if (hw_priv->sandisk_connectplus) {
		sandisk_write_hcr(local, hcr);
		return;
	}

	res = pcmcia_read_config_byte(hw_priv->link, CISREG_COR, &old_cor);
	if (res != 0) {
		printk(KERN_DEBUG "prism2_pccard_genesis_sreset failed 1 "
		       "(%d)\n", res);
		return;
	}
	printk(KERN_DEBUG "prism2_pccard_genesis_sreset: original COR %02x\n",
		old_cor);

	res = pcmcia_write_config_byte(hw_priv->link, CISREG_COR,
				old_cor | COR_SOFT_RESET);
	if (res != 0) {
		printk(KERN_DEBUG "prism2_pccard_genesis_sreset failed 2 "
		       "(%d)\n", res);
		return;
	}

	mdelay(10);

	/* Setup Genesis mode */
	res = pcmcia_write_config_byte(hw_priv->link, CISREG_CCSR, hcr);
	if (res != 0) {
		printk(KERN_DEBUG "prism2_pccard_genesis_sreset failed 3 "
		       "(%d)\n", res);
		return;
	}
	mdelay(10);

	res = pcmcia_write_config_byte(hw_priv->link, CISREG_COR,
				old_cor & ~COR_SOFT_RESET);
	if (res != 0) {
		printk(KERN_DEBUG "prism2_pccard_genesis_sreset failed 4 "
		       "(%d)\n", res);
		return;
	}

	mdelay(10);
}


static struct prism2_helper_functions prism2_pccard_funcs =
{
	.card_present	= prism2_pccard_card_present,
	.cor_sreset	= prism2_pccard_cor_sreset,
	.genesis_reset	= prism2_pccard_genesis_reset,
	.hw_type	= HOSTAP_HW_PCCARD,
};


/* allocate local data and register with CardServices
 * initialize dev_link structure, but do not configure the card yet */
static int hostap_cs_probe(struct pcmcia_device *p_dev)
{
	int ret;

	PDEBUG(DEBUG_HW, "%s: setting Vcc=33 (constant)\n", dev_info);

	ret = prism2_config(p_dev);
	if (ret) {
		PDEBUG(DEBUG_EXTRA, "prism2_config() failed\n");
	}

	return ret;
}


static void prism2_detach(struct pcmcia_device *link)
{
	PDEBUG(DEBUG_FLOW, "prism2_detach\n");

	prism2_release((u_long)link);

	/* release net devices */
	if (link->priv) {
		struct hostap_cs_priv *hw_priv;
		struct net_device *dev;
		struct hostap_interface *iface;
		dev = link->priv;
		iface = netdev_priv(dev);
		hw_priv = iface->local->hw_priv;
		prism2_free_local_data(dev);
		kfree(hw_priv);
	}
}


static int prism2_config_check(struct pcmcia_device *p_dev, void *priv_data)
{
	if (p_dev->config_index == 0)
		return -EINVAL;

	return pcmcia_request_io(p_dev);
}

static int prism2_config(struct pcmcia_device *link)
{
	struct net_device *dev;
	struct hostap_interface *iface;
	local_info_t *local;
	int ret = 1;
	struct hostap_cs_priv *hw_priv;
	unsigned long flags;

	PDEBUG(DEBUG_FLOW, "prism2_config()\n");

	hw_priv = kzalloc(sizeof(*hw_priv), GFP_KERNEL);
	if (hw_priv == NULL) {
		ret = -ENOMEM;
		goto failed;
	}

	/* Look for an appropriate configuration table entry in the CIS */
	link->config_flags |= CONF_AUTO_SET_VPP | CONF_AUTO_AUDIO |
		CONF_AUTO_CHECK_VCC | CONF_AUTO_SET_IO | CONF_ENABLE_IRQ;
	if (ignore_cis_vcc)
		link->config_flags &= ~CONF_AUTO_CHECK_VCC;
	ret = pcmcia_loop_config(link, prism2_config_check, NULL);
	if (ret) {
		if (!ignore_cis_vcc)
			printk(KERN_ERR "GetNextTuple(): No matching "
			       "CIS configuration.  Maybe you need the "
			       "ignore_cis_vcc=1 parameter.\n");
		goto failed;
	}

	/* Need to allocate net_device before requesting IRQ handler */
	dev = prism2_init_local_data(&prism2_pccard_funcs, 0,
				     &link->dev);
	if (dev == NULL)
		goto failed;
	link->priv = dev;

	iface = netdev_priv(dev);
	local = iface->local;
	local->hw_priv = hw_priv;
	hw_priv->link = link;

	/*
	 * We enable IRQ here, but IRQ handler will not proceed
	 * until dev->base_addr is set below. This protect us from
	 * receive interrupts when driver is not initialized.
	 */
	ret = pcmcia_request_irq(link, prism2_interrupt);
	if (ret)
		goto failed;

	ret = pcmcia_enable_device(link);
	if (ret)
		goto failed;

	spin_lock_irqsave(&local->irq_init_lock, flags);
	dev->irq = link->irq;
	dev->base_addr = link->resource[0]->start;
	spin_unlock_irqrestore(&local->irq_init_lock, flags);

	local->shutdown = 0;

	sandisk_enable_wireless(dev);

	ret = prism2_hw_config(dev, 1);
	if (!ret)
		ret = hostap_hw_ready(dev);

	return ret;

 failed:
	kfree(hw_priv);
	prism2_release((u_long)link);
	return ret;
}


static void prism2_release(u_long arg)
{
	struct pcmcia_device *link = (struct pcmcia_device *)arg;

	PDEBUG(DEBUG_FLOW, "prism2_release\n");

	if (link->priv) {
		struct net_device *dev = link->priv;
		struct hostap_interface *iface;

		iface = netdev_priv(dev);
		prism2_hw_shutdown(dev, 0);
		iface->local->shutdown = 1;
	}

	pcmcia_disable_device(link);
	PDEBUG(DEBUG_FLOW, "release - done\n");
}

static int hostap_cs_suspend(struct pcmcia_device *link)
{
	struct net_device *dev = (struct net_device *) link->priv;
	int dev_open = 0;
	struct hostap_interface *iface = NULL;

	if (!dev)
		return -ENODEV;

	iface = netdev_priv(dev);

	PDEBUG(DEBUG_EXTRA, "%s: CS_EVENT_PM_SUSPEND\n", dev_info);
	if (iface && iface->local)
		dev_open = iface->local->num_dev_open > 0;
	if (dev_open) {
		netif_stop_queue(dev);
		netif_device_detach(dev);
	}
	prism2_suspend(dev);

	return 0;
}

static int hostap_cs_resume(struct pcmcia_device *link)
{
	struct net_device *dev = (struct net_device *) link->priv;
	int dev_open = 0;
	struct hostap_interface *iface = NULL;

	if (!dev)
		return -ENODEV;

	iface = netdev_priv(dev);

	PDEBUG(DEBUG_EXTRA, "%s: CS_EVENT_PM_RESUME\n", dev_info);

	if (iface && iface->local)
		dev_open = iface->local->num_dev_open > 0;

	prism2_hw_shutdown(dev, 1);
	prism2_hw_config(dev, dev_open ? 0 : 1);
	if (dev_open) {
		netif_device_attach(dev);
		netif_start_queue(dev);
	}

	return 0;
}

static const struct pcmcia_device_id hostap_cs_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x000b, 0x7100),
	PCMCIA_DEVICE_MANF_CARD(0x000b, 0x7300),
	PCMCIA_DEVICE_MANF_CARD(0x0101, 0x0777),
	PCMCIA_DEVICE_MANF_CARD(0x0126, 0x8000),
	PCMCIA_DEVICE_MANF_CARD(0x0138, 0x0002),
	PCMCIA_DEVICE_MANF_CARD(0x01bf, 0x3301),
	PCMCIA_DEVICE_MANF_CARD(0x0250, 0x0002),
	PCMCIA_DEVICE_MANF_CARD(0x026f, 0x030b),
	PCMCIA_DEVICE_MANF_CARD(0x0274, 0x1612),
	PCMCIA_DEVICE_MANF_CARD(0x0274, 0x1613),
	PCMCIA_DEVICE_MANF_CARD(0x028a, 0x0002),
	PCMCIA_DEVICE_MANF_CARD(0x02aa, 0x0002),
	PCMCIA_DEVICE_MANF_CARD(0x02d2, 0x0001),
	PCMCIA_DEVICE_MANF_CARD(0x50c2, 0x0001),
	PCMCIA_DEVICE_MANF_CARD(0x50c2, 0x7300),
/*	PCMCIA_DEVICE_MANF_CARD(0xc00f, 0x0000),    conflict with pcnet_cs */
	PCMCIA_DEVICE_MANF_CARD(0xc250, 0x0002),
	PCMCIA_DEVICE_MANF_CARD(0xd601, 0x0002),
	PCMCIA_DEVICE_MANF_CARD(0xd601, 0x0005),
	PCMCIA_DEVICE_MANF_CARD(0xd601, 0x0010),
	PCMCIA_DEVICE_MANF_CARD(0x0126, 0x0002),
	PCMCIA_DEVICE_MANF_CARD_PROD_ID1(0xd601, 0x0005, "ADLINK 345 CF",
					 0x2d858104),
	PCMCIA_DEVICE_MANF_CARD_PROD_ID1(0x0156, 0x0002, "INTERSIL",
					 0x74c5e40d),
	PCMCIA_DEVICE_MANF_CARD_PROD_ID1(0x0156, 0x0002, "Intersil",
					 0x4b801a17),
	PCMCIA_DEVICE_MANF_CARD_PROD_ID3(0x0156, 0x0002, "Version 01.02",
					 0x4b74baa0),
	PCMCIA_MFC_DEVICE_PROD_ID12(0, "SanDisk", "ConnectPlus",
				    0x7a954bd9, 0x74be00c6),
	PCMCIA_DEVICE_PROD_ID123(
		"Addtron", "AWP-100 Wireless PCMCIA", "Version 01.02",
		0xe6ec52ce, 0x08649af2, 0x4b74baa0),
	PCMCIA_DEVICE_PROD_ID123(
		"Canon", "Wireless LAN CF Card K30225", "Version 01.00",
		0x96ef6fe2, 0x263fcbab, 0xa57adb8c),
	PCMCIA_DEVICE_PROD_ID123(
		"D", "Link DWL-650 11Mbps WLAN Card", "Version 01.02",
		0x71b18589, 0xb6f1b0ab, 0x4b74baa0),
	PCMCIA_DEVICE_PROD_ID123(
		"Instant Wireless ", " Network PC CARD", "Version 01.02",
		0x11d901af, 0x6e9bd926, 0x4b74baa0),
	PCMCIA_DEVICE_PROD_ID123(
		"SMC", "SMC2632W", "Version 01.02",
		0xc4f8b18b, 0x474a1f2a, 0x4b74baa0),
	PCMCIA_DEVICE_PROD_ID12("BUFFALO", "WLI-CF-S11G", 
				0x2decece3, 0x82067c18),
	PCMCIA_DEVICE_PROD_ID12("Compaq", "WL200_11Mbps_Wireless_PCI_Card",
				0x54f7c49c, 0x15a75e5b),
	PCMCIA_DEVICE_PROD_ID12("INTERSIL", "HFA384x/IEEE",
				0x74c5e40d, 0xdb472a18),
	PCMCIA_DEVICE_PROD_ID12("Linksys", "Wireless CompactFlash Card",
				0x0733cc81, 0x0c52f395),
	PCMCIA_DEVICE_PROD_ID12(
		"ZoomAir 11Mbps High", "Rate wireless Networking",
		0x273fe3db, 0x32a1eaee),
	PCMCIA_DEVICE_PROD_ID123(
		"Pretec", "CompactWLAN Card 802.11b", "2.5",
		0x1cadd3e5, 0xe697636c, 0x7a5bfcf1),
	PCMCIA_DEVICE_PROD_ID123(
		"U.S. Robotics", "IEEE 802.11b PC-CARD", "Version 01.02",
		0xc7b8df9d, 0x1700d087, 0x4b74baa0),
	PCMCIA_DEVICE_PROD_ID123(
		"Allied Telesyn", "AT-WCL452 Wireless PCMCIA Radio",
		"Ver. 1.00",
		0x5cd01705, 0x4271660f, 0x9d08ee12),
	PCMCIA_DEVICE_PROD_ID123(
		"Wireless LAN" , "11Mbps PC Card", "Version 01.02",
		0x4b8870ff, 0x70e946d1, 0x4b74baa0),
	PCMCIA_DEVICE_PROD_ID3("HFA3863", 0x355cb092),
	PCMCIA_DEVICE_PROD_ID3("ISL37100P", 0x630d52b2),
	PCMCIA_DEVICE_PROD_ID3("ISL37101P-10", 0xdd97a26b),
	PCMCIA_DEVICE_PROD_ID3("ISL37300P", 0xc9049a39),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, hostap_cs_ids);


static struct pcmcia_driver hostap_driver = {
	.name		= "hostap_cs",
	.probe		= hostap_cs_probe,
	.remove		= prism2_detach,
	.owner		= THIS_MODULE,
	.id_table	= hostap_cs_ids,
	.suspend	= hostap_cs_suspend,
	.resume		= hostap_cs_resume,
};
module_pcmcia_driver(hostap_driver);
