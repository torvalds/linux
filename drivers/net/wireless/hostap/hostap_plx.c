#define PRISM2_PLX

/* Host AP driver's support for PC Cards on PCI adapters using PLX9052 is
 * based on:
 * - Host AP driver patch from james@madingley.org
 * - linux-wlan-ng driver, Copyright (C) AbsoluteValue Systems, Inc.
 */


#include <linux/module.h>
#include <linux/init.h>
#include <linux/if.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>

#include <linux/ioport.h>
#include <linux/pci.h>
#include <asm/io.h>

#include "hostap_wlan.h"


static char *dev_info = "hostap_plx";


MODULE_AUTHOR("Jouni Malinen");
MODULE_DESCRIPTION("Support for Intersil Prism2-based 802.11 wireless LAN "
		   "cards (PLX).");
MODULE_SUPPORTED_DEVICE("Intersil Prism2-based WLAN cards (PLX)");
MODULE_LICENSE("GPL");


static int ignore_cis;
module_param(ignore_cis, int, 0444);
MODULE_PARM_DESC(ignore_cis, "Do not verify manfid information in CIS");


/* struct local_info::hw_priv */
struct hostap_plx_priv {
	void __iomem *attr_mem;
	unsigned int cor_offset;
};


#define PLX_MIN_ATTR_LEN 512	/* at least 2 x 256 is needed for CIS */
#define COR_SRESET       0x80
#define COR_LEVLREQ      0x40
#define COR_ENABLE_FUNC  0x01
/* PCI Configuration Registers */
#define PLX_PCIIPR       0x3d   /* PCI Interrupt Pin */
/* Local Configuration Registers */
#define PLX_INTCSR       0x4c   /* Interrupt Control/Status Register */
#define PLX_INTCSR_PCI_INTEN BIT(6) /* PCI Interrupt Enable */
#define PLX_CNTRL        0x50
#define PLX_CNTRL_SERIAL_EEPROM_PRESENT BIT(28)


#define PLXDEV(vendor,dev,str) { vendor, dev, PCI_ANY_ID, PCI_ANY_ID }

static struct pci_device_id prism2_plx_id_table[] __devinitdata = {
	PLXDEV(0x10b7, 0x7770, "3Com AirConnect PCI 777A"),
	PLXDEV(0x111a, 0x1023, "Siemens SpeedStream SS1023"),
	PLXDEV(0x126c, 0x8030, "Nortel emobility"),
	PLXDEV(0x1562, 0x0001, "Symbol LA-4123"),
	PLXDEV(0x1385, 0x4100, "Netgear MA301"),
	PLXDEV(0x15e8, 0x0130, "National Datacomm NCP130 (PLX9052)"),
	PLXDEV(0x15e8, 0x0131, "National Datacomm NCP130 (TMD7160)"),
	PLXDEV(0x1638, 0x1100, "Eumitcom WL11000"),
	PLXDEV(0x16ab, 0x1100, "Global Sun Tech GL24110P"),
	PLXDEV(0x16ab, 0x1101, "Global Sun Tech GL24110P (?)"),
	PLXDEV(0x16ab, 0x1102, "Linksys WPC11 with WDT11"),
	PLXDEV(0x16ab, 0x1103, "Longshine 8031"),
	PLXDEV(0x16ec, 0x3685, "US Robotics USR2415"),
	PLXDEV(0xec80, 0xec00, "Belkin F5D6000"),
	{ 0 }
};


/* Array of known Prism2/2.5 PC Card manufactured ids. If your card's manfid
 * is not listed here, you will need to add it here to get the driver
 * initialized. */
static struct prism2_plx_manfid {
	u16 manfid1, manfid2;
} prism2_plx_known_manfids[] = {
	{ 0x000b, 0x7110 } /* D-Link DWL-650 Rev. P1 */,
	{ 0x000b, 0x7300 } /* Philips 802.11b WLAN PCMCIA */,
	{ 0x0101, 0x0777 } /* 3Com AirConnect PCI 777A */,
	{ 0x0126, 0x8000 } /* Proxim RangeLAN */,
	{ 0x0138, 0x0002 } /* Compaq WL100 */,
	{ 0x0156, 0x0002 } /* Intersil Prism II Ref. Design (and others) */,
	{ 0x026f, 0x030b } /* Buffalo WLI-CF-S11G */,
	{ 0x0274, 0x1612 } /* Linksys WPC11 Ver 2.5 */,
	{ 0x0274, 0x1613 } /* Linksys WPC11 Ver 3 */,
	{ 0x028a, 0x0002 } /* D-Link DRC-650 */,
	{ 0x0250, 0x0002 } /* Samsung SWL2000-N */,
	{ 0xc250, 0x0002 } /* EMTAC A2424i */,
	{ 0xd601, 0x0002 } /* Z-Com XI300 */,
	{ 0xd601, 0x0005 } /* Zcomax XI-325H 200mW */,
	{ 0, 0}
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


static void prism2_plx_cor_sreset(local_info_t *local)
{
	unsigned char corsave;
	struct hostap_plx_priv *hw_priv = local->hw_priv;

	printk(KERN_DEBUG "%s: Doing reset via direct COR access.\n",
	       dev_info);

	/* Set sreset bit of COR and clear it after hold time */

	if (hw_priv->attr_mem == NULL) {
		/* TMD7160 - COR at card's first I/O addr */
		corsave = inb(hw_priv->cor_offset);
		outb(corsave | COR_SRESET, hw_priv->cor_offset);
		mdelay(2);
		outb(corsave & ~COR_SRESET, hw_priv->cor_offset);
		mdelay(2);
	} else {
		/* PLX9052 */
		corsave = readb(hw_priv->attr_mem + hw_priv->cor_offset);
		writeb(corsave | COR_SRESET,
		       hw_priv->attr_mem + hw_priv->cor_offset);
		mdelay(2);
		writeb(corsave & ~COR_SRESET,
		       hw_priv->attr_mem + hw_priv->cor_offset);
		mdelay(2);
	}
}


static void prism2_plx_genesis_reset(local_info_t *local, int hcr)
{
	unsigned char corsave;
	struct hostap_plx_priv *hw_priv = local->hw_priv;

	if (hw_priv->attr_mem == NULL) {
		/* TMD7160 - COR at card's first I/O addr */
		corsave = inb(hw_priv->cor_offset);
		outb(corsave | COR_SRESET, hw_priv->cor_offset);
		mdelay(10);
		outb(hcr, hw_priv->cor_offset + 2);
		mdelay(10);
		outb(corsave & ~COR_SRESET, hw_priv->cor_offset);
		mdelay(10);
	} else {
		/* PLX9052 */
		corsave = readb(hw_priv->attr_mem + hw_priv->cor_offset);
		writeb(corsave | COR_SRESET,
		       hw_priv->attr_mem + hw_priv->cor_offset);
		mdelay(10);
		writeb(hcr, hw_priv->attr_mem + hw_priv->cor_offset + 2);
		mdelay(10);
		writeb(corsave & ~COR_SRESET,
		       hw_priv->attr_mem + hw_priv->cor_offset);
		mdelay(10);
	}
}


static struct prism2_helper_functions prism2_plx_funcs =
{
	.card_present	= NULL,
	.cor_sreset	= prism2_plx_cor_sreset,
	.genesis_reset	= prism2_plx_genesis_reset,
	.hw_type	= HOSTAP_HW_PLX,
};


static int prism2_plx_check_cis(void __iomem *attr_mem, int attr_len,
				unsigned int *cor_offset,
				unsigned int *cor_index)
{
#define CISTPL_CONFIG 0x1A
#define CISTPL_MANFID 0x20
#define CISTPL_END 0xFF
#define CIS_MAX_LEN 256
	u8 *cis;
	int i, pos;
	unsigned int rmsz, rasz, manfid1, manfid2;
	struct prism2_plx_manfid *manfid;

	cis = kmalloc(CIS_MAX_LEN, GFP_KERNEL);
	if (cis == NULL)
		return -ENOMEM;

	/* read CIS; it is in even offsets in the beginning of attr_mem */
	for (i = 0; i < CIS_MAX_LEN; i++)
		cis[i] = readb(attr_mem + 2 * i);
	printk(KERN_DEBUG "%s: CIS: %02x %02x %02x %02x %02x %02x ...\n",
	       dev_info, cis[0], cis[1], cis[2], cis[3], cis[4], cis[5]);

	/* set reasonable defaults for Prism2 cards just in case CIS parsing
	 * fails */
	*cor_offset = 0x3e0;
	*cor_index = 0x01;
	manfid1 = manfid2 = 0;

	pos = 0;
	while (pos < CIS_MAX_LEN - 1 && cis[pos] != CISTPL_END) {
		if (pos + 2 + cis[pos + 1] > CIS_MAX_LEN)
			goto cis_error;

		switch (cis[pos]) {
		case CISTPL_CONFIG:
			if (cis[pos + 1] < 2)
				goto cis_error;
			rmsz = (cis[pos + 2] & 0x3c) >> 2;
			rasz = cis[pos + 2] & 0x03;
			if (4 + rasz + rmsz > cis[pos + 1])
				goto cis_error;
			*cor_index = cis[pos + 3] & 0x3F;
			*cor_offset = 0;
			for (i = 0; i <= rasz; i++)
				*cor_offset += cis[pos + 4 + i] << (8 * i);
			printk(KERN_DEBUG "%s: cor_index=0x%x "
			       "cor_offset=0x%x\n", dev_info,
			       *cor_index, *cor_offset);
			if (*cor_offset > attr_len) {
				printk(KERN_ERR "%s: COR offset not within "
				       "attr_mem\n", dev_info);
				kfree(cis);
				return -1;
			}
			break;

		case CISTPL_MANFID:
			if (cis[pos + 1] < 4)
				goto cis_error;
			manfid1 = cis[pos + 2] + (cis[pos + 3] << 8);
			manfid2 = cis[pos + 4] + (cis[pos + 5] << 8);
			printk(KERN_DEBUG "%s: manfid=0x%04x, 0x%04x\n",
			       dev_info, manfid1, manfid2);
			break;
		}

		pos += cis[pos + 1] + 2;
	}

	if (pos >= CIS_MAX_LEN || cis[pos] != CISTPL_END)
		goto cis_error;

	for (manfid = prism2_plx_known_manfids; manfid->manfid1 != 0; manfid++)
		if (manfid1 == manfid->manfid1 && manfid2 == manfid->manfid2) {
			kfree(cis);
			return 0;
		}

	printk(KERN_INFO "%s: unknown manfid 0x%04x, 0x%04x - assuming this is"
	       " not supported card\n", dev_info, manfid1, manfid2);
	goto fail;

 cis_error:
	printk(KERN_WARNING "%s: invalid CIS data\n", dev_info);

 fail:
	kfree(cis);
	if (ignore_cis) {
		printk(KERN_INFO "%s: ignore_cis parameter set - ignoring "
		       "errors during CIS verification\n", dev_info);
		return 0;
	}
	return -1;
}


static int prism2_plx_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	unsigned int pccard_ioaddr, plx_ioaddr;
	unsigned long pccard_attr_mem;
	unsigned int pccard_attr_len;
	void __iomem *attr_mem = NULL;
	unsigned int cor_offset, cor_index;
	u32 reg;
	local_info_t *local = NULL;
	struct net_device *dev = NULL;
	struct hostap_interface *iface;
	static int cards_found /* = 0 */;
	int irq_registered = 0;
	int tmd7160;
	struct hostap_plx_priv *hw_priv;

	hw_priv = kzalloc(sizeof(*hw_priv), GFP_KERNEL);
	if (hw_priv == NULL)
		return -ENOMEM;

	if (pci_enable_device(pdev))
		goto err_out_free;

	/* National Datacomm NCP130 based on TMD7160, not PLX9052. */
	tmd7160 = (pdev->vendor == 0x15e8) && (pdev->device == 0x0131);

	plx_ioaddr = pci_resource_start(pdev, 1);
	pccard_ioaddr = pci_resource_start(pdev, tmd7160 ? 2 : 3);

	if (tmd7160) {
		/* TMD7160 */
		attr_mem = NULL; /* no access to PC Card attribute memory */

		printk(KERN_INFO "TMD7160 PCI/PCMCIA adapter: io=0x%x, "
		       "irq=%d, pccard_io=0x%x\n",
		       plx_ioaddr, pdev->irq, pccard_ioaddr);

		cor_offset = plx_ioaddr;
		cor_index = 0x04;

		outb(cor_index | COR_LEVLREQ | COR_ENABLE_FUNC, plx_ioaddr);
		mdelay(1);
		reg = inb(plx_ioaddr);
		if (reg != (cor_index | COR_LEVLREQ | COR_ENABLE_FUNC)) {
			printk(KERN_ERR "%s: Error setting COR (expected="
			       "0x%02x, was=0x%02x)\n", dev_info,
			       cor_index | COR_LEVLREQ | COR_ENABLE_FUNC, reg);
			goto fail;
		}
	} else {
		/* PLX9052 */
		pccard_attr_mem = pci_resource_start(pdev, 2);
		pccard_attr_len = pci_resource_len(pdev, 2);
		if (pccard_attr_len < PLX_MIN_ATTR_LEN)
			goto fail;


		attr_mem = ioremap(pccard_attr_mem, pccard_attr_len);
		if (attr_mem == NULL) {
			printk(KERN_ERR "%s: cannot remap attr_mem\n",
			       dev_info);
			goto fail;
		}

		printk(KERN_INFO "PLX9052 PCI/PCMCIA adapter: "
		       "mem=0x%lx, plx_io=0x%x, irq=%d, pccard_io=0x%x\n",
		       pccard_attr_mem, plx_ioaddr, pdev->irq, pccard_ioaddr);

		if (prism2_plx_check_cis(attr_mem, pccard_attr_len,
					 &cor_offset, &cor_index)) {
			printk(KERN_INFO "Unknown PC Card CIS - not a "
			       "Prism2/2.5 card?\n");
			goto fail;
		}

		printk(KERN_DEBUG "Prism2/2.5 PC Card detected in PLX9052 "
		       "adapter\n");

		/* Write COR to enable PC Card */
		writeb(cor_index | COR_LEVLREQ | COR_ENABLE_FUNC,
		       attr_mem + cor_offset);

		/* Enable PCI interrupts if they are not already enabled */
		reg = inl(plx_ioaddr + PLX_INTCSR);
		printk(KERN_DEBUG "PLX_INTCSR=0x%x\n", reg);
		if (!(reg & PLX_INTCSR_PCI_INTEN)) {
			outl(reg | PLX_INTCSR_PCI_INTEN,
			     plx_ioaddr + PLX_INTCSR);
			if (!(inl(plx_ioaddr + PLX_INTCSR) &
			      PLX_INTCSR_PCI_INTEN)) {
				printk(KERN_WARNING "%s: Could not enable "
				       "Local Interrupts\n", dev_info);
				goto fail;
			}
		}

		reg = inl(plx_ioaddr + PLX_CNTRL);
		printk(KERN_DEBUG "PLX_CNTRL=0x%x (Serial EEPROM "
		       "present=%d)\n",
		       reg, (reg & PLX_CNTRL_SERIAL_EEPROM_PRESENT) != 0);
		/* should set PLX_PCIIPR to 0x01 (INTA#) if Serial EEPROM is
		 * not present; but are there really such cards in use(?) */
	}

	dev = prism2_init_local_data(&prism2_plx_funcs, cards_found,
				     &pdev->dev);
	if (dev == NULL)
		goto fail;
	iface = netdev_priv(dev);
	local = iface->local;
	local->hw_priv = hw_priv;
	cards_found++;

	dev->irq = pdev->irq;
	dev->base_addr = pccard_ioaddr;
	hw_priv->attr_mem = attr_mem;
	hw_priv->cor_offset = cor_offset;

	pci_set_drvdata(pdev, dev);

	if (request_irq(dev->irq, prism2_interrupt, IRQF_SHARED, dev->name,
			dev)) {
		printk(KERN_WARNING "%s: request_irq failed\n", dev->name);
		goto fail;
	} else
		irq_registered = 1;

	if (prism2_hw_config(dev, 1)) {
		printk(KERN_DEBUG "%s: hardware initialization failed\n",
		       dev_info);
		goto fail;
	}

	return hostap_hw_ready(dev);

 fail:
	if (irq_registered && dev)
		free_irq(dev->irq, dev);

	if (attr_mem)
		iounmap(attr_mem);

	pci_disable_device(pdev);
	prism2_free_local_data(dev);

 err_out_free:
	kfree(hw_priv);

	return -ENODEV;
}


static void prism2_plx_remove(struct pci_dev *pdev)
{
	struct net_device *dev;
	struct hostap_interface *iface;
	struct hostap_plx_priv *hw_priv;

	dev = pci_get_drvdata(pdev);
	iface = netdev_priv(dev);
	hw_priv = iface->local->hw_priv;

	/* Reset the hardware, and ensure interrupts are disabled. */
	prism2_plx_cor_sreset(iface->local);
	hfa384x_disable_interrupts(dev);

	if (hw_priv->attr_mem)
		iounmap(hw_priv->attr_mem);
	if (dev->irq)
		free_irq(dev->irq, dev);

	prism2_free_local_data(dev);
	kfree(hw_priv);
	pci_disable_device(pdev);
}


MODULE_DEVICE_TABLE(pci, prism2_plx_id_table);

static struct pci_driver prism2_plx_driver = {
	.name		= "hostap_plx",
	.id_table	= prism2_plx_id_table,
	.probe		= prism2_plx_probe,
	.remove		= prism2_plx_remove,
};


static int __init init_prism2_plx(void)
{
	return pci_register_driver(&prism2_plx_driver);
}


static void __exit exit_prism2_plx(void)
{
	pci_unregister_driver(&prism2_plx_driver);
}


module_init(init_prism2_plx);
module_exit(exit_prism2_plx);
