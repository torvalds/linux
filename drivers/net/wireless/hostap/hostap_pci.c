#define PRISM2_PCI

/* Host AP driver's support for Intersil Prism2.5 PCI cards is based on
 * driver patches from Reyk Floeter <reyk@vantronix.net> and
 * Andy Warner <andyw@pobox.com> */

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


static char *dev_info = "hostap_pci";


MODULE_AUTHOR("Jouni Malinen");
MODULE_DESCRIPTION("Support for Intersil Prism2.5-based 802.11 wireless LAN "
		   "PCI cards.");
MODULE_SUPPORTED_DEVICE("Intersil Prism2.5-based WLAN PCI cards");
MODULE_LICENSE("GPL");


/* struct local_info::hw_priv */
struct hostap_pci_priv {
	void __iomem *mem_start;
};


/* FIX: do we need mb/wmb/rmb with memory operations? */


static struct pci_device_id prism2_pci_id_table[] __devinitdata = {
	/* Intersil Prism3 ISL3872 11Mb/s WLAN Controller */
	{ 0x1260, 0x3872, PCI_ANY_ID, PCI_ANY_ID },
	/* Intersil Prism2.5 ISL3874 11Mb/s WLAN Controller */
	{ 0x1260, 0x3873, PCI_ANY_ID, PCI_ANY_ID },
	/* Samsung MagicLAN SWL-2210P */
	{ 0x167d, 0xa000, PCI_ANY_ID, PCI_ANY_ID },
	{ 0 }
};


#ifdef PRISM2_IO_DEBUG

static inline void hfa384x_outb_debug(struct net_device *dev, int a, u8 v)
{
	struct hostap_interface *iface;
	struct hostap_pci_priv *hw_priv;
	local_info_t *local;
	unsigned long flags;

	iface = netdev_priv(dev);
	local = iface->local;
	hw_priv = local->hw_priv;

	spin_lock_irqsave(&local->lock, flags);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_OUTB, a, v);
	writeb(v, hw_priv->mem_start + a);
	spin_unlock_irqrestore(&local->lock, flags);
}

static inline u8 hfa384x_inb_debug(struct net_device *dev, int a)
{
	struct hostap_interface *iface;
	struct hostap_pci_priv *hw_priv;
	local_info_t *local;
	unsigned long flags;
	u8 v;

	iface = netdev_priv(dev);
	local = iface->local;
	hw_priv = local->hw_priv;

	spin_lock_irqsave(&local->lock, flags);
	v = readb(hw_priv->mem_start + a);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_INB, a, v);
	spin_unlock_irqrestore(&local->lock, flags);
	return v;
}

static inline void hfa384x_outw_debug(struct net_device *dev, int a, u16 v)
{
	struct hostap_interface *iface;
	struct hostap_pci_priv *hw_priv;
	local_info_t *local;
	unsigned long flags;

	iface = netdev_priv(dev);
	local = iface->local;
	hw_priv = local->hw_priv;

	spin_lock_irqsave(&local->lock, flags);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_OUTW, a, v);
	writew(v, hw_priv->mem_start + a);
	spin_unlock_irqrestore(&local->lock, flags);
}

static inline u16 hfa384x_inw_debug(struct net_device *dev, int a)
{
	struct hostap_interface *iface;
	struct hostap_pci_priv *hw_priv;
	local_info_t *local;
	unsigned long flags;
	u16 v;

	iface = netdev_priv(dev);
	local = iface->local;
	hw_priv = local->hw_priv;

	spin_lock_irqsave(&local->lock, flags);
	v = readw(hw_priv->mem_start + a);
	prism2_io_debug_add(dev, PRISM2_IO_DEBUG_CMD_INW, a, v);
	spin_unlock_irqrestore(&local->lock, flags);
	return v;
}

#define HFA384X_OUTB(v,a) hfa384x_outb_debug(dev, (a), (v))
#define HFA384X_INB(a) hfa384x_inb_debug(dev, (a))
#define HFA384X_OUTW(v,a) hfa384x_outw_debug(dev, (a), (v))
#define HFA384X_INW(a) hfa384x_inw_debug(dev, (a))
#define HFA384X_OUTW_DATA(v,a) hfa384x_outw_debug(dev, (a), cpu_to_le16((v)))
#define HFA384X_INW_DATA(a) (u16) le16_to_cpu(hfa384x_inw_debug(dev, (a)))

#else /* PRISM2_IO_DEBUG */

static inline void hfa384x_outb(struct net_device *dev, int a, u8 v)
{
	struct hostap_interface *iface;
	struct hostap_pci_priv *hw_priv;
	iface = netdev_priv(dev);
	hw_priv = iface->local->hw_priv;
	writeb(v, hw_priv->mem_start + a);
}

static inline u8 hfa384x_inb(struct net_device *dev, int a)
{
	struct hostap_interface *iface;
	struct hostap_pci_priv *hw_priv;
	iface = netdev_priv(dev);
	hw_priv = iface->local->hw_priv;
	return readb(hw_priv->mem_start + a);
}

static inline void hfa384x_outw(struct net_device *dev, int a, u16 v)
{
	struct hostap_interface *iface;
	struct hostap_pci_priv *hw_priv;
	iface = netdev_priv(dev);
	hw_priv = iface->local->hw_priv;
	writew(v, hw_priv->mem_start + a);
}

static inline u16 hfa384x_inw(struct net_device *dev, int a)
{
	struct hostap_interface *iface;
	struct hostap_pci_priv *hw_priv;
	iface = netdev_priv(dev);
	hw_priv = iface->local->hw_priv;
	return readw(hw_priv->mem_start + a);
}

#define HFA384X_OUTB(v,a) hfa384x_outb(dev, (a), (v))
#define HFA384X_INB(a) hfa384x_inb(dev, (a))
#define HFA384X_OUTW(v,a) hfa384x_outw(dev, (a), (v))
#define HFA384X_INW(a) hfa384x_inw(dev, (a))
#define HFA384X_OUTW_DATA(v,a) hfa384x_outw(dev, (a), cpu_to_le16((v)))
#define HFA384X_INW_DATA(a) (u16) le16_to_cpu(hfa384x_inw(dev, (a)))

#endif /* PRISM2_IO_DEBUG */


static int hfa384x_from_bap(struct net_device *dev, u16 bap, void *buf,
			    int len)
{
	u16 d_off;
	u16 *pos;

	d_off = (bap == 1) ? HFA384X_DATA1_OFF : HFA384X_DATA0_OFF;
	pos = (u16 *) buf;

	for ( ; len > 1; len -= 2)
		*pos++ = HFA384X_INW_DATA(d_off);

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

	for ( ; len > 1; len -= 2)
		HFA384X_OUTW_DATA(*pos++, d_off);

	if (len & 1)
		HFA384X_OUTB(*((char *) pos), d_off);

	return 0;
}


/* FIX: This might change at some point.. */
#include "hostap_hw.c"

static void prism2_pci_cor_sreset(local_info_t *local)
{
	struct net_device *dev = local->dev;
	u16 reg;

	reg = HFA384X_INB(HFA384X_PCICOR_OFF);
	printk(KERN_DEBUG "%s: Original COR value: 0x%0x\n", dev->name, reg);

	/* linux-wlan-ng uses extremely long hold and settle times for
	 * COR sreset. A comment in the driver code mentions that the long
	 * delays appear to be necessary. However, at least IBM 22P6901 seems
	 * to work fine with shorter delays.
	 *
	 * Longer delays can be configured by uncommenting following line: */
/* #define PRISM2_PCI_USE_LONG_DELAYS */

#ifdef PRISM2_PCI_USE_LONG_DELAYS
	int i;

	HFA384X_OUTW(reg | 0x0080, HFA384X_PCICOR_OFF);
	mdelay(250);

	HFA384X_OUTW(reg & ~0x0080, HFA384X_PCICOR_OFF);
	mdelay(500);

	/* Wait for f/w to complete initialization (CMD:BUSY == 0) */
	i = 2000000 / 10;
	while ((HFA384X_INW(HFA384X_CMD_OFF) & HFA384X_CMD_BUSY) && --i)
		udelay(10);

#else /* PRISM2_PCI_USE_LONG_DELAYS */

	HFA384X_OUTW(reg | 0x0080, HFA384X_PCICOR_OFF);
	mdelay(2);
	HFA384X_OUTW(reg & ~0x0080, HFA384X_PCICOR_OFF);
	mdelay(2);

#endif /* PRISM2_PCI_USE_LONG_DELAYS */

	if (HFA384X_INW(HFA384X_CMD_OFF) & HFA384X_CMD_BUSY) {
		printk(KERN_DEBUG "%s: COR sreset timeout\n", dev->name);
	}
}


static void prism2_pci_genesis_reset(local_info_t *local, int hcr)
{
	struct net_device *dev = local->dev;

	HFA384X_OUTW(0x00C5, HFA384X_PCICOR_OFF);
	mdelay(10);
	HFA384X_OUTW(hcr, HFA384X_PCIHCR_OFF);
	mdelay(10);
	HFA384X_OUTW(0x0045, HFA384X_PCICOR_OFF);
	mdelay(10);
}


static struct prism2_helper_functions prism2_pci_funcs =
{
	.card_present	= NULL,
	.cor_sreset	= prism2_pci_cor_sreset,
	.genesis_reset	= prism2_pci_genesis_reset,
	.hw_type	= HOSTAP_HW_PCI,
};


static int prism2_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	unsigned long phymem;
	void __iomem *mem = NULL;
	local_info_t *local = NULL;
	struct net_device *dev = NULL;
	static int cards_found /* = 0 */;
	int irq_registered = 0;
	struct hostap_interface *iface;
	struct hostap_pci_priv *hw_priv;

	hw_priv = kzalloc(sizeof(*hw_priv), GFP_KERNEL);
	if (hw_priv == NULL)
		return -ENOMEM;

	if (pci_enable_device(pdev))
		goto err_out_free;

	phymem = pci_resource_start(pdev, 0);

	if (!request_mem_region(phymem, pci_resource_len(pdev, 0), "Prism2")) {
		printk(KERN_ERR "prism2: Cannot reserve PCI memory region\n");
		goto err_out_disable;
	}

	mem = ioremap(phymem, pci_resource_len(pdev, 0));
	if (mem == NULL) {
		printk(KERN_ERR "prism2: Cannot remap PCI memory region\n") ;
		goto fail;
	}

	dev = prism2_init_local_data(&prism2_pci_funcs, cards_found,
				     &pdev->dev);
	if (dev == NULL)
		goto fail;
	iface = netdev_priv(dev);
	local = iface->local;
	local->hw_priv = hw_priv;
	cards_found++;

        dev->irq = pdev->irq;
        hw_priv->mem_start = mem;

	prism2_pci_cor_sreset(local);

	pci_set_drvdata(pdev, dev);

	if (request_irq(dev->irq, prism2_interrupt, IRQF_SHARED, dev->name,
			dev)) {
		printk(KERN_WARNING "%s: request_irq failed\n", dev->name);
		goto fail;
	} else
		irq_registered = 1;

	if (!local->pri_only && prism2_hw_config(dev, 1)) {
		printk(KERN_DEBUG "%s: hardware initialization failed\n",
		       dev_info);
		goto fail;
	}

	printk(KERN_INFO "%s: Intersil Prism2.5 PCI: "
	       "mem=0x%lx, irq=%d\n", dev->name, phymem, dev->irq);

	return hostap_hw_ready(dev);

 fail:
	if (irq_registered && dev)
		free_irq(dev->irq, dev);

	if (mem)
		iounmap(mem);

	release_mem_region(phymem, pci_resource_len(pdev, 0));

 err_out_disable:
	pci_disable_device(pdev);
	prism2_free_local_data(dev);

 err_out_free:
	kfree(hw_priv);

	return -ENODEV;
}


static void prism2_pci_remove(struct pci_dev *pdev)
{
	struct net_device *dev;
	struct hostap_interface *iface;
	void __iomem *mem_start;
	struct hostap_pci_priv *hw_priv;

	dev = pci_get_drvdata(pdev);
	iface = netdev_priv(dev);
	hw_priv = iface->local->hw_priv;

	/* Reset the hardware, and ensure interrupts are disabled. */
	prism2_pci_cor_sreset(iface->local);
	hfa384x_disable_interrupts(dev);

	if (dev->irq)
		free_irq(dev->irq, dev);

	mem_start = hw_priv->mem_start;
	prism2_free_local_data(dev);
	kfree(hw_priv);

	iounmap(mem_start);

	release_mem_region(pci_resource_start(pdev, 0),
			   pci_resource_len(pdev, 0));
	pci_disable_device(pdev);
}


#ifdef CONFIG_PM
static int prism2_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (netif_running(dev)) {
		netif_stop_queue(dev);
		netif_device_detach(dev);
	}
	prism2_suspend(dev);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int prism2_pci_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	int err;

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR "%s: pci_enable_device failed on resume\n",
		       dev->name);
		return err;
	}
	pci_restore_state(pdev);
	prism2_hw_config(dev, 0);
	if (netif_running(dev)) {
		netif_device_attach(dev);
		netif_start_queue(dev);
	}

	return 0;
}
#endif /* CONFIG_PM */


MODULE_DEVICE_TABLE(pci, prism2_pci_id_table);

static struct pci_driver prism2_pci_driver = {
	.name		= "hostap_pci",
	.id_table	= prism2_pci_id_table,
	.probe		= prism2_pci_probe,
	.remove		= prism2_pci_remove,
#ifdef CONFIG_PM
	.suspend	= prism2_pci_suspend,
	.resume		= prism2_pci_resume,
#endif /* CONFIG_PM */
};


static int __init init_prism2_pci(void)
{
	return pci_register_driver(&prism2_pci_driver);
}


static void __exit exit_prism2_pci(void)
{
	pci_unregister_driver(&prism2_pci_driver);
}


module_init(init_prism2_pci);
module_exit(exit_prism2_pci);
