/*******************************************************************************
 *
 *  Linux ThunderLAN Driver
 *
 *  tlan.c
 *  by James Banks
 *
 *  (C) 1997-1998 Caldera, Inc.
 *  (C) 1998 James Banks
 *  (C) 1999-2001 Torben Mathiasen
 *  (C) 2002 Samuel Chessman
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU General Public License, incorporated herein by reference.
 *
 ** Useful (if not required) reading:
 *
 *		Texas Instruments, ThunderLAN Programmer's Guide,
 *			TI Literature Number SPWU013A
 *			available in PDF format from www.ti.com
 *		Level One, LXT901 and LXT970 Data Sheets
 *			available in PDF format from www.level1.com
 *		National Semiconductor, DP83840A Data Sheet
 *			available in PDF format from www.national.com
 *		Microchip Technology, 24C01A/02A/04A Data Sheet
 *			available in PDF format from www.microchip.com
 *
 ******************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/hardirq.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/eisa.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/mii.h>

#include "tlan.h"


/* For removing EISA devices */
static	struct net_device	*tlan_eisa_devices;

static	int		tlan_devices_installed;

/* Set speed, duplex and aui settings */
static  int aui[MAX_TLAN_BOARDS];
static  int duplex[MAX_TLAN_BOARDS];
static  int speed[MAX_TLAN_BOARDS];
static  int boards_found;
module_param_array(aui, int, NULL, 0);
module_param_array(duplex, int, NULL, 0);
module_param_array(speed, int, NULL, 0);
MODULE_PARM_DESC(aui, "ThunderLAN use AUI port(s) (0-1)");
MODULE_PARM_DESC(duplex,
		 "ThunderLAN duplex setting(s) (0-default, 1-half, 2-full)");
MODULE_PARM_DESC(speed, "ThunderLAN port speed setting(s) (0,10,100)");

MODULE_AUTHOR("Maintainer: Samuel Chessman <chessman@tux.org>");
MODULE_DESCRIPTION("Driver for TI ThunderLAN based ethernet PCI adapters");
MODULE_LICENSE("GPL");

/* Turn on debugging.
 * See Documentation/networking/device_drivers/ethernet/ti/tlan.rst for details
 */
static  int		debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "ThunderLAN debug mask");

static	const char tlan_signature[] = "TLAN";
static  const char tlan_banner[] = "ThunderLAN driver v1.17\n";
static  int tlan_have_pci;
static  int tlan_have_eisa;

static const char * const media[] = {
	"10BaseT-HD", "10BaseT-FD", "100baseTx-HD",
	"100BaseTx-FD", "100BaseT4", NULL
};

static struct board {
	const char	*device_label;
	u32		flags;
	u16		addr_ofs;
} board_info[] = {
	{ "Compaq Netelligent 10 T PCI UTP", TLAN_ADAPTER_ACTIVITY_LED, 0x83 },
	{ "Compaq Netelligent 10/100 TX PCI UTP",
	  TLAN_ADAPTER_ACTIVITY_LED, 0x83 },
	{ "Compaq Integrated NetFlex-3/P", TLAN_ADAPTER_NONE, 0x83 },
	{ "Compaq NetFlex-3/P",
	  TLAN_ADAPTER_UNMANAGED_PHY | TLAN_ADAPTER_BIT_RATE_PHY, 0x83 },
	{ "Compaq NetFlex-3/P", TLAN_ADAPTER_NONE, 0x83 },
	{ "Compaq Netelligent Integrated 10/100 TX UTP",
	  TLAN_ADAPTER_ACTIVITY_LED, 0x83 },
	{ "Compaq Netelligent Dual 10/100 TX PCI UTP",
	  TLAN_ADAPTER_NONE, 0x83 },
	{ "Compaq Netelligent 10/100 TX Embedded UTP",
	  TLAN_ADAPTER_NONE, 0x83 },
	{ "Olicom OC-2183/2185", TLAN_ADAPTER_USE_INTERN_10, 0x83 },
	{ "Olicom OC-2325", TLAN_ADAPTER_ACTIVITY_LED |
	  TLAN_ADAPTER_UNMANAGED_PHY, 0xf8 },
	{ "Olicom OC-2326", TLAN_ADAPTER_ACTIVITY_LED |
	  TLAN_ADAPTER_USE_INTERN_10, 0xf8 },
	{ "Compaq Netelligent 10/100 TX UTP", TLAN_ADAPTER_ACTIVITY_LED, 0x83 },
	{ "Compaq Netelligent 10 T/2 PCI UTP/coax", TLAN_ADAPTER_NONE, 0x83 },
	{ "Compaq NetFlex-3/E",
	  TLAN_ADAPTER_ACTIVITY_LED |	/* EISA card */
	  TLAN_ADAPTER_UNMANAGED_PHY | TLAN_ADAPTER_BIT_RATE_PHY, 0x83 },
	{ "Compaq NetFlex-3/E",
	  TLAN_ADAPTER_ACTIVITY_LED, 0x83 }, /* EISA card */
};

static const struct pci_device_id tlan_pci_tbl[] = {
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_NETEL10,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_NETEL100,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1 },
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_NETFLEX3I,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2 },
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_THUNDER,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3 },
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_NETFLEX3B,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4 },
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_NETEL100PI,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 5 },
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_NETEL100D,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 6 },
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_NETEL100I,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 7 },
	{ PCI_VENDOR_ID_OLICOM, PCI_DEVICE_ID_OLICOM_OC2183,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 8 },
	{ PCI_VENDOR_ID_OLICOM, PCI_DEVICE_ID_OLICOM_OC2325,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 9 },
	{ PCI_VENDOR_ID_OLICOM, PCI_DEVICE_ID_OLICOM_OC2326,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 10 },
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_NETELLIGENT_10_100_WS_5100,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 11 },
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_NETELLIGENT_10_T2,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 12 },
	{ 0,}
};
MODULE_DEVICE_TABLE(pci, tlan_pci_tbl);

static void	tlan_eisa_probe(void);
static void	tlan_eisa_cleanup(void);
static int      tlan_init(struct net_device *);
static int	tlan_open(struct net_device *dev);
static netdev_tx_t tlan_start_tx(struct sk_buff *, struct net_device *);
static irqreturn_t tlan_handle_interrupt(int, void *);
static int	tlan_close(struct net_device *);
static struct	net_device_stats *tlan_get_stats(struct net_device *);
static void	tlan_set_multicast_list(struct net_device *);
static int	tlan_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int      tlan_probe1(struct pci_dev *pdev, long ioaddr,
			    int irq, int rev, const struct pci_device_id *ent);
static void	tlan_tx_timeout(struct net_device *dev, unsigned int txqueue);
static void	tlan_tx_timeout_work(struct work_struct *work);
static int	tlan_init_one(struct pci_dev *pdev,
			      const struct pci_device_id *ent);

static u32	tlan_handle_tx_eof(struct net_device *, u16);
static u32	tlan_handle_stat_overflow(struct net_device *, u16);
static u32	tlan_handle_rx_eof(struct net_device *, u16);
static u32	tlan_handle_dummy(struct net_device *, u16);
static u32	tlan_handle_tx_eoc(struct net_device *, u16);
static u32	tlan_handle_status_check(struct net_device *, u16);
static u32	tlan_handle_rx_eoc(struct net_device *, u16);

static void	tlan_timer(struct timer_list *t);
static void	tlan_phy_monitor(struct timer_list *t);

static void	tlan_reset_lists(struct net_device *);
static void	tlan_free_lists(struct net_device *);
static void	tlan_print_dio(u16);
static void	tlan_print_list(struct tlan_list *, char *, int);
static void	tlan_read_and_clear_stats(struct net_device *, int);
static void	tlan_reset_adapter(struct net_device *);
static void	tlan_finish_reset(struct net_device *);
static void	tlan_set_mac(struct net_device *, int areg, char *mac);

static void	tlan_phy_print(struct net_device *);
static void	tlan_phy_detect(struct net_device *);
static void	tlan_phy_power_down(struct net_device *);
static void	tlan_phy_power_up(struct net_device *);
static void	tlan_phy_reset(struct net_device *);
static void	tlan_phy_start_link(struct net_device *);
static void	tlan_phy_finish_auto_neg(struct net_device *);

/*
  static int	tlan_phy_nop(struct net_device *);
  static int	tlan_phy_internal_check(struct net_device *);
  static int	tlan_phy_internal_service(struct net_device *);
  static int	tlan_phy_dp83840a_check(struct net_device *);
*/

static bool	tlan_mii_read_reg(struct net_device *, u16, u16, u16 *);
static void	tlan_mii_send_data(u16, u32, unsigned);
static void	tlan_mii_sync(u16);
static void	tlan_mii_write_reg(struct net_device *, u16, u16, u16);

static void	tlan_ee_send_start(u16);
static int	tlan_ee_send_byte(u16, u8, int);
static void	tlan_ee_receive_byte(u16, u8 *, int);
static int	tlan_ee_read_byte(struct net_device *, u8, u8 *);


static inline void
tlan_store_skb(struct tlan_list *tag, struct sk_buff *skb)
{
	unsigned long addr = (unsigned long)skb;
	tag->buffer[9].address = addr;
	tag->buffer[8].address = upper_32_bits(addr);
}

static inline struct sk_buff *
tlan_get_skb(const struct tlan_list *tag)
{
	unsigned long addr;

	addr = tag->buffer[9].address;
	addr |= ((unsigned long) tag->buffer[8].address << 16) << 16;
	return (struct sk_buff *) addr;
}

static u32
(*tlan_int_vector[TLAN_INT_NUMBER_OF_INTS])(struct net_device *, u16) = {
	NULL,
	tlan_handle_tx_eof,
	tlan_handle_stat_overflow,
	tlan_handle_rx_eof,
	tlan_handle_dummy,
	tlan_handle_tx_eoc,
	tlan_handle_status_check,
	tlan_handle_rx_eoc
};

static inline void
tlan_set_timer(struct net_device *dev, u32 ticks, u32 type)
{
	struct tlan_priv *priv = netdev_priv(dev);
	unsigned long flags = 0;

	if (!in_irq())
		spin_lock_irqsave(&priv->lock, flags);
	if (priv->timer.function != NULL &&
	    priv->timer_type != TLAN_TIMER_ACTIVITY) {
		if (!in_irq())
			spin_unlock_irqrestore(&priv->lock, flags);
		return;
	}
	priv->timer.function = tlan_timer;
	if (!in_irq())
		spin_unlock_irqrestore(&priv->lock, flags);

	priv->timer_set_at = jiffies;
	priv->timer_type = type;
	mod_timer(&priv->timer, jiffies + ticks);

}


/*****************************************************************************
******************************************************************************

ThunderLAN driver primary functions

these functions are more or less common to all linux network drivers.

******************************************************************************
*****************************************************************************/





/***************************************************************
 *	tlan_remove_one
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		None
 *
 *	Goes through the TLanDevices list and frees the device
 *	structs and memory associated with each device (lists
 *	and buffers).  It also ureserves the IO port regions
 *	associated with this device.
 *
 **************************************************************/


static void tlan_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct tlan_priv	*priv = netdev_priv(dev);

	unregister_netdev(dev);

	if (priv->dma_storage) {
		dma_free_coherent(&priv->pci_dev->dev, priv->dma_size,
				  priv->dma_storage, priv->dma_storage_dma);
	}

#ifdef CONFIG_PCI
	pci_release_regions(pdev);
#endif

	free_netdev(dev);

	cancel_work_sync(&priv->tlan_tqueue);
}

static void tlan_start(struct net_device *dev)
{
	tlan_reset_lists(dev);
	/* NOTE: It might not be necessary to read the stats before a
	   reset if you don't care what the values are.
	*/
	tlan_read_and_clear_stats(dev, TLAN_IGNORE);
	tlan_reset_adapter(dev);
	netif_wake_queue(dev);
}

static void tlan_stop(struct net_device *dev)
{
	struct tlan_priv *priv = netdev_priv(dev);

	del_timer_sync(&priv->media_timer);
	tlan_read_and_clear_stats(dev, TLAN_RECORD);
	outl(TLAN_HC_AD_RST, dev->base_addr + TLAN_HOST_CMD);
	/* Reset and power down phy */
	tlan_reset_adapter(dev);
	if (priv->timer.function != NULL) {
		del_timer_sync(&priv->timer);
		priv->timer.function = NULL;
	}
}

static int __maybe_unused tlan_suspend(struct device *dev_d)
{
	struct net_device *dev = dev_get_drvdata(dev_d);

	if (netif_running(dev))
		tlan_stop(dev);

	netif_device_detach(dev);

	return 0;
}

static int __maybe_unused tlan_resume(struct device *dev_d)
{
	struct net_device *dev = dev_get_drvdata(dev_d);
	netif_device_attach(dev);

	if (netif_running(dev))
		tlan_start(dev);

	return 0;
}

static SIMPLE_DEV_PM_OPS(tlan_pm_ops, tlan_suspend, tlan_resume);

static struct pci_driver tlan_driver = {
	.name		= "tlan",
	.id_table	= tlan_pci_tbl,
	.probe		= tlan_init_one,
	.remove		= tlan_remove_one,
	.driver.pm	= &tlan_pm_ops,
};

static int __init tlan_probe(void)
{
	int rc = -ENODEV;

	pr_info("%s", tlan_banner);

	TLAN_DBG(TLAN_DEBUG_PROBE, "Starting PCI Probe....\n");

	/* Use new style PCI probing. Now the kernel will
	   do most of this for us */
	rc = pci_register_driver(&tlan_driver);

	if (rc != 0) {
		pr_err("Could not register pci driver\n");
		goto err_out_pci_free;
	}

	TLAN_DBG(TLAN_DEBUG_PROBE, "Starting EISA Probe....\n");
	tlan_eisa_probe();

	pr_info("%d device%s installed, PCI: %d  EISA: %d\n",
		tlan_devices_installed, tlan_devices_installed == 1 ? "" : "s",
		tlan_have_pci, tlan_have_eisa);

	if (tlan_devices_installed == 0) {
		rc = -ENODEV;
		goto  err_out_pci_unreg;
	}
	return 0;

err_out_pci_unreg:
	pci_unregister_driver(&tlan_driver);
err_out_pci_free:
	return rc;
}


static int tlan_init_one(struct pci_dev *pdev,
				   const struct pci_device_id *ent)
{
	return tlan_probe1(pdev, -1, -1, 0, ent);
}


/*
***************************************************************
*	tlan_probe1
*
*	Returns:
*		0 on success, error code on error
*	Parms:
*		none
*
*	The name is lower case to fit in with all the rest of
*	the netcard_probe names.  This function looks for
*	another TLan based adapter, setting it up with the
*	allocated device struct if one is found.
*	tlan_probe has been ported to the new net API and
*	now allocates its own device structure. This function
*	is also used by modules.
*
**************************************************************/

static int tlan_probe1(struct pci_dev *pdev, long ioaddr, int irq, int rev,
		       const struct pci_device_id *ent)
{

	struct net_device  *dev;
	struct tlan_priv  *priv;
	u16		   device_id;
	int		   reg, rc = -ENODEV;

#ifdef CONFIG_PCI
	if (pdev) {
		rc = pci_enable_device(pdev);
		if (rc)
			return rc;

		rc = pci_request_regions(pdev, tlan_signature);
		if (rc) {
			pr_err("Could not reserve IO regions\n");
			goto err_out;
		}
	}
#endif  /*  CONFIG_PCI  */

	dev = alloc_etherdev(sizeof(struct tlan_priv));
	if (dev == NULL) {
		rc = -ENOMEM;
		goto err_out_regions;
	}
	SET_NETDEV_DEV(dev, &pdev->dev);

	priv = netdev_priv(dev);

	priv->pci_dev = pdev;
	priv->dev = dev;

	/* Is this a PCI device? */
	if (pdev) {
		u32		   pci_io_base = 0;

		priv->adapter = &board_info[ent->driver_data];

		rc = dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
		if (rc) {
			pr_err("No suitable PCI mapping available\n");
			goto err_out_free_dev;
		}

		for (reg = 0; reg <= 5; reg++) {
			if (pci_resource_flags(pdev, reg) & IORESOURCE_IO) {
				pci_io_base = pci_resource_start(pdev, reg);
				TLAN_DBG(TLAN_DEBUG_GNRL,
					 "IO mapping is available at %x.\n",
					 pci_io_base);
				break;
			}
		}
		if (!pci_io_base) {
			pr_err("No IO mappings available\n");
			rc = -EIO;
			goto err_out_free_dev;
		}

		dev->base_addr = pci_io_base;
		dev->irq = pdev->irq;
		priv->adapter_rev = pdev->revision;
		pci_set_master(pdev);
		pci_set_drvdata(pdev, dev);

	} else	{     /* EISA card */
		/* This is a hack. We need to know which board structure
		 * is suited for this adapter */
		device_id = inw(ioaddr + EISA_ID2);
		if (device_id == 0x20F1) {
			priv->adapter = &board_info[13]; /* NetFlex-3/E */
			priv->adapter_rev = 23;		/* TLAN 2.3 */
		} else {
			priv->adapter = &board_info[14];
			priv->adapter_rev = 10;		/* TLAN 1.0 */
		}
		dev->base_addr = ioaddr;
		dev->irq = irq;
	}

	/* Kernel parameters */
	if (dev->mem_start) {
		priv->aui    = dev->mem_start & 0x01;
		priv->duplex = ((dev->mem_start & 0x06) == 0x06) ? 0
			: (dev->mem_start & 0x06) >> 1;
		priv->speed  = ((dev->mem_start & 0x18) == 0x18) ? 0
			: (dev->mem_start & 0x18) >> 3;

		if (priv->speed == 0x1)
			priv->speed = TLAN_SPEED_10;
		else if (priv->speed == 0x2)
			priv->speed = TLAN_SPEED_100;

		debug = priv->debug = dev->mem_end;
	} else {
		priv->aui    = aui[boards_found];
		priv->speed  = speed[boards_found];
		priv->duplex = duplex[boards_found];
		priv->debug = debug;
	}

	/* This will be used when we get an adapter error from
	 * within our irq handler */
	INIT_WORK(&priv->tlan_tqueue, tlan_tx_timeout_work);

	spin_lock_init(&priv->lock);

	rc = tlan_init(dev);
	if (rc) {
		pr_err("Could not set up device\n");
		goto err_out_free_dev;
	}

	rc = register_netdev(dev);
	if (rc) {
		pr_err("Could not register device\n");
		goto err_out_uninit;
	}


	tlan_devices_installed++;
	boards_found++;

	/* pdev is NULL if this is an EISA device */
	if (pdev)
		tlan_have_pci++;
	else {
		priv->next_device = tlan_eisa_devices;
		tlan_eisa_devices = dev;
		tlan_have_eisa++;
	}

	netdev_info(dev, "irq=%2d, io=%04x, %s, Rev. %d\n",
		    (int)dev->irq,
		    (int)dev->base_addr,
		    priv->adapter->device_label,
		    priv->adapter_rev);
	return 0;

err_out_uninit:
	dma_free_coherent(&priv->pci_dev->dev, priv->dma_size,
			  priv->dma_storage, priv->dma_storage_dma);
err_out_free_dev:
	free_netdev(dev);
err_out_regions:
#ifdef CONFIG_PCI
	if (pdev)
		pci_release_regions(pdev);
err_out:
#endif
	if (pdev)
		pci_disable_device(pdev);
	return rc;
}


static void tlan_eisa_cleanup(void)
{
	struct net_device *dev;
	struct tlan_priv *priv;

	while (tlan_have_eisa) {
		dev = tlan_eisa_devices;
		priv = netdev_priv(dev);
		if (priv->dma_storage) {
			dma_free_coherent(&priv->pci_dev->dev, priv->dma_size,
					  priv->dma_storage,
					  priv->dma_storage_dma);
		}
		release_region(dev->base_addr, 0x10);
		unregister_netdev(dev);
		tlan_eisa_devices = priv->next_device;
		free_netdev(dev);
		tlan_have_eisa--;
	}
}


static void __exit tlan_exit(void)
{
	pci_unregister_driver(&tlan_driver);

	if (tlan_have_eisa)
		tlan_eisa_cleanup();

}


/* Module loading/unloading */
module_init(tlan_probe);
module_exit(tlan_exit);



/**************************************************************
 *	tlan_eisa_probe
 *
 *	Returns: 0 on success, 1 otherwise
 *
 *	Parms:	 None
 *
 *
 *	This functions probes for EISA devices and calls
 *	TLan_probe1 when one is found.
 *
 *************************************************************/

static void  __init tlan_eisa_probe(void)
{
	long	ioaddr;
	int	irq;
	u16	device_id;

	if (!EISA_bus) {
		TLAN_DBG(TLAN_DEBUG_PROBE, "No EISA bus present\n");
		return;
	}

	/* Loop through all slots of the EISA bus */
	for (ioaddr = 0x1000; ioaddr < 0x9000; ioaddr += 0x1000) {

		TLAN_DBG(TLAN_DEBUG_PROBE, "EISA_ID 0x%4x: 0x%4x\n",
			 (int) ioaddr + 0xc80, inw(ioaddr + EISA_ID));
		TLAN_DBG(TLAN_DEBUG_PROBE, "EISA_ID 0x%4x: 0x%4x\n",
			 (int) ioaddr + 0xc82, inw(ioaddr + EISA_ID2));


		TLAN_DBG(TLAN_DEBUG_PROBE,
			 "Probing for EISA adapter at IO: 0x%4x : ",
			 (int) ioaddr);
		if (request_region(ioaddr, 0x10, tlan_signature) == NULL)
			goto out;

		if (inw(ioaddr + EISA_ID) != 0x110E) {
			release_region(ioaddr, 0x10);
			goto out;
		}

		device_id = inw(ioaddr + EISA_ID2);
		if (device_id !=  0x20F1 && device_id != 0x40F1) {
			release_region(ioaddr, 0x10);
			goto out;
		}

		/* check if adapter is enabled */
		if (inb(ioaddr + EISA_CR) != 0x1) {
			release_region(ioaddr, 0x10);
			goto out2;
		}

		if (debug == 0x10)
			pr_info("Found one\n");


		/* Get irq from board */
		switch (inb(ioaddr + 0xcc0)) {
		case(0x10):
			irq = 5;
			break;
		case(0x20):
			irq = 9;
			break;
		case(0x40):
			irq = 10;
			break;
		case(0x80):
			irq = 11;
			break;
		default:
			goto out;
		}


		/* Setup the newly found eisa adapter */
		tlan_probe1(NULL, ioaddr, irq, 12, NULL);
		continue;

out:
		if (debug == 0x10)
			pr_info("None found\n");
		continue;

out2:
		if (debug == 0x10)
			pr_info("Card found but it is not enabled, skipping\n");
		continue;

	}

}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void tlan_poll(struct net_device *dev)
{
	disable_irq(dev->irq);
	tlan_handle_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

static const struct net_device_ops tlan_netdev_ops = {
	.ndo_open		= tlan_open,
	.ndo_stop		= tlan_close,
	.ndo_start_xmit		= tlan_start_tx,
	.ndo_tx_timeout		= tlan_tx_timeout,
	.ndo_get_stats		= tlan_get_stats,
	.ndo_set_rx_mode	= tlan_set_multicast_list,
	.ndo_do_ioctl		= tlan_ioctl,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	 = tlan_poll,
#endif
};

static void tlan_get_drvinfo(struct net_device *dev,
			     struct ethtool_drvinfo *info)
{
	struct tlan_priv *priv = netdev_priv(dev);

	strlcpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	if (priv->pci_dev)
		strlcpy(info->bus_info, pci_name(priv->pci_dev),
			sizeof(info->bus_info));
	else
		strlcpy(info->bus_info, "EISA",	sizeof(info->bus_info));
}

static int tlan_get_eeprom_len(struct net_device *dev)
{
	return TLAN_EEPROM_SIZE;
}

static int tlan_get_eeprom(struct net_device *dev,
			   struct ethtool_eeprom *eeprom, u8 *data)
{
	int i;

	for (i = 0; i < TLAN_EEPROM_SIZE; i++)
		if (tlan_ee_read_byte(dev, i, &data[i]))
			return -EIO;

	return 0;
}

static const struct ethtool_ops tlan_ethtool_ops = {
	.get_drvinfo	= tlan_get_drvinfo,
	.get_link	= ethtool_op_get_link,
	.get_eeprom_len	= tlan_get_eeprom_len,
	.get_eeprom	= tlan_get_eeprom,
};

/***************************************************************
 *	tlan_init
 *
 *	Returns:
 *		0 on success, error code otherwise.
 *	Parms:
 *		dev	The structure of the device to be
 *			init'ed.
 *
 *	This function completes the initialization of the
 *	device structure and driver.  It reserves the IO
 *	addresses, allocates memory for the lists and bounce
 *	buffers, retrieves the MAC address from the eeprom
 *	and assignes the device's methods.
 *
 **************************************************************/

static int tlan_init(struct net_device *dev)
{
	int		dma_size;
	int		err;
	int		i;
	struct tlan_priv	*priv;

	priv = netdev_priv(dev);

	dma_size = (TLAN_NUM_RX_LISTS + TLAN_NUM_TX_LISTS)
		* (sizeof(struct tlan_list));
	priv->dma_storage = dma_alloc_coherent(&priv->pci_dev->dev, dma_size,
					       &priv->dma_storage_dma, GFP_KERNEL);
	priv->dma_size = dma_size;

	if (priv->dma_storage == NULL) {
		pr_err("Could not allocate lists and buffers for %s\n",
		       dev->name);
		return -ENOMEM;
	}
	priv->rx_list = (struct tlan_list *)
		ALIGN((unsigned long)priv->dma_storage, 8);
	priv->rx_list_dma = ALIGN(priv->dma_storage_dma, 8);
	priv->tx_list = priv->rx_list + TLAN_NUM_RX_LISTS;
	priv->tx_list_dma =
		priv->rx_list_dma + sizeof(struct tlan_list)*TLAN_NUM_RX_LISTS;

	err = 0;
	for (i = 0; i < ETH_ALEN; i++)
		err |= tlan_ee_read_byte(dev,
					 (u8) priv->adapter->addr_ofs + i,
					 (u8 *) &dev->dev_addr[i]);
	if (err) {
		pr_err("%s: Error reading MAC from eeprom: %d\n",
		       dev->name, err);
	}
	/* Olicom OC-2325/OC-2326 have the address byte-swapped */
	if (priv->adapter->addr_ofs == 0xf8) {
		for (i = 0; i < ETH_ALEN; i += 2) {
			char tmp = dev->dev_addr[i];
			dev->dev_addr[i] = dev->dev_addr[i + 1];
			dev->dev_addr[i + 1] = tmp;
		}
	}

	netif_carrier_off(dev);

	/* Device methods */
	dev->netdev_ops = &tlan_netdev_ops;
	dev->ethtool_ops = &tlan_ethtool_ops;
	dev->watchdog_timeo = TX_TIMEOUT;

	return 0;

}




/***************************************************************
 *	tlan_open
 *
 *	Returns:
 *		0 on success, error code otherwise.
 *	Parms:
 *		dev	Structure of device to be opened.
 *
 *	This routine puts the driver and TLAN adapter in a
 *	state where it is ready to send and receive packets.
 *	It allocates the IRQ, resets and brings the adapter
 *	out of reset, and allows interrupts.  It also delays
 *	the startup for autonegotiation or sends a Rx GO
 *	command to the adapter, as appropriate.
 *
 **************************************************************/

static int tlan_open(struct net_device *dev)
{
	struct tlan_priv	*priv = netdev_priv(dev);
	int		err;

	priv->tlan_rev = tlan_dio_read8(dev->base_addr, TLAN_DEF_REVISION);
	err = request_irq(dev->irq, tlan_handle_interrupt, IRQF_SHARED,
			  dev->name, dev);

	if (err) {
		netdev_err(dev, "Cannot open because IRQ %d is already in use\n",
			   dev->irq);
		return err;
	}

	timer_setup(&priv->timer, NULL, 0);
	timer_setup(&priv->media_timer, tlan_phy_monitor, 0);

	tlan_start(dev);

	TLAN_DBG(TLAN_DEBUG_GNRL, "%s: Opened.  TLAN Chip Rev: %x\n",
		 dev->name, priv->tlan_rev);

	return 0;

}



/**************************************************************
 *	tlan_ioctl
 *
 *	Returns:
 *		0 on success, error code otherwise
 *	Params:
 *		dev	structure of device to receive ioctl.
 *
 *		rq	ifreq structure to hold userspace data.
 *
 *		cmd	ioctl command.
 *
 *
 *************************************************************/

static int tlan_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct tlan_priv *priv = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(rq);
	u32 phy   = priv->phy[priv->phy_num];

	if (!priv->phy_online)
		return -EAGAIN;

	switch (cmd) {
	case SIOCGMIIPHY:		/* get address of MII PHY in use. */
		data->phy_id = phy;
		fallthrough;


	case SIOCGMIIREG:		/* read MII PHY register. */
		tlan_mii_read_reg(dev, data->phy_id & 0x1f,
				  data->reg_num & 0x1f, &data->val_out);
		return 0;


	case SIOCSMIIREG:		/* write MII PHY register. */
		tlan_mii_write_reg(dev, data->phy_id & 0x1f,
				   data->reg_num & 0x1f, data->val_in);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}


/***************************************************************
 *	tlan_tx_timeout
 *
 *	Returns: nothing
 *
 *	Params:
 *		dev	structure of device which timed out
 *			during transmit.
 *
 **************************************************************/

static void tlan_tx_timeout(struct net_device *dev, unsigned int txqueue)
{

	TLAN_DBG(TLAN_DEBUG_GNRL, "%s: Transmit timed out.\n", dev->name);

	/* Ok so we timed out, lets see what we can do about it...*/
	tlan_free_lists(dev);
	tlan_reset_lists(dev);
	tlan_read_and_clear_stats(dev, TLAN_IGNORE);
	tlan_reset_adapter(dev);
	netif_trans_update(dev); /* prevent tx timeout */
	netif_wake_queue(dev);

}


/***************************************************************
 *	tlan_tx_timeout_work
 *
 *	Returns: nothing
 *
 *	Params:
 *		work	work item of device which timed out
 *
 **************************************************************/

static void tlan_tx_timeout_work(struct work_struct *work)
{
	struct tlan_priv	*priv =
		container_of(work, struct tlan_priv, tlan_tqueue);

	tlan_tx_timeout(priv->dev, UINT_MAX);
}



/***************************************************************
 *	tlan_start_tx
 *
 *	Returns:
 *		0 on success, non-zero on failure.
 *	Parms:
 *		skb	A pointer to the sk_buff containing the
 *			frame to be sent.
 *		dev	The device to send the data on.
 *
 *	This function adds a frame to the Tx list to be sent
 *	ASAP.  First it	verifies that the adapter is ready and
 *	there is room in the queue.  Then it sets up the next
 *	available list, copies the frame to the	corresponding
 *	buffer.  If the adapter Tx channel is idle, it gives
 *	the adapter a Tx Go command on the list, otherwise it
 *	sets the forward address of the previous list to point
 *	to this one.  Then it frees the sk_buff.
 *
 **************************************************************/

static netdev_tx_t tlan_start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct tlan_priv *priv = netdev_priv(dev);
	dma_addr_t	tail_list_phys;
	struct tlan_list	*tail_list;
	unsigned long	flags;
	unsigned int    txlen;

	if (!priv->phy_online) {
		TLAN_DBG(TLAN_DEBUG_TX, "TRANSMIT:  %s PHY is not ready\n",
			 dev->name);
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (skb_padto(skb, TLAN_MIN_FRAME_SIZE))
		return NETDEV_TX_OK;
	txlen = max(skb->len, (unsigned int)TLAN_MIN_FRAME_SIZE);

	tail_list = priv->tx_list + priv->tx_tail;
	tail_list_phys =
		priv->tx_list_dma + sizeof(struct tlan_list)*priv->tx_tail;

	if (tail_list->c_stat != TLAN_CSTAT_UNUSED) {
		TLAN_DBG(TLAN_DEBUG_TX,
			 "TRANSMIT:  %s is busy (Head=%d Tail=%d)\n",
			 dev->name, priv->tx_head, priv->tx_tail);
		netif_stop_queue(dev);
		priv->tx_busy_count++;
		return NETDEV_TX_BUSY;
	}

	tail_list->forward = 0;

	tail_list->buffer[0].address = dma_map_single(&priv->pci_dev->dev,
						      skb->data, txlen,
						      DMA_TO_DEVICE);
	tlan_store_skb(tail_list, skb);

	tail_list->frame_size = (u16) txlen;
	tail_list->buffer[0].count = TLAN_LAST_BUFFER | (u32) txlen;
	tail_list->buffer[1].count = 0;
	tail_list->buffer[1].address = 0;

	spin_lock_irqsave(&priv->lock, flags);
	tail_list->c_stat = TLAN_CSTAT_READY;
	if (!priv->tx_in_progress) {
		priv->tx_in_progress = 1;
		TLAN_DBG(TLAN_DEBUG_TX,
			 "TRANSMIT:  Starting TX on buffer %d\n",
			 priv->tx_tail);
		outl(tail_list_phys, dev->base_addr + TLAN_CH_PARM);
		outl(TLAN_HC_GO, dev->base_addr + TLAN_HOST_CMD);
	} else {
		TLAN_DBG(TLAN_DEBUG_TX,
			 "TRANSMIT:  Adding buffer %d to TX channel\n",
			 priv->tx_tail);
		if (priv->tx_tail == 0) {
			(priv->tx_list + (TLAN_NUM_TX_LISTS - 1))->forward
				= tail_list_phys;
		} else {
			(priv->tx_list + (priv->tx_tail - 1))->forward
				= tail_list_phys;
		}
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	CIRC_INC(priv->tx_tail, TLAN_NUM_TX_LISTS);

	return NETDEV_TX_OK;

}




/***************************************************************
 *	tlan_handle_interrupt
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		irq	The line on which the interrupt
 *			occurred.
 *		dev_id	A pointer to the device assigned to
 *			this irq line.
 *
 *	This function handles an interrupt generated by its
 *	assigned TLAN adapter.  The function deactivates
 *	interrupts on its adapter, records the type of
 *	interrupt, executes the appropriate subhandler, and
 *	acknowdges the interrupt to the adapter (thus
 *	re-enabling adapter interrupts.
 *
 **************************************************************/

static irqreturn_t tlan_handle_interrupt(int irq, void *dev_id)
{
	struct net_device	*dev = dev_id;
	struct tlan_priv *priv = netdev_priv(dev);
	u16		host_int;
	u16		type;

	spin_lock(&priv->lock);

	host_int = inw(dev->base_addr + TLAN_HOST_INT);
	type = (host_int & TLAN_HI_IT_MASK) >> 2;
	if (type) {
		u32	ack;
		u32	host_cmd;

		outw(host_int, dev->base_addr + TLAN_HOST_INT);
		ack = tlan_int_vector[type](dev, host_int);

		if (ack) {
			host_cmd = TLAN_HC_ACK | ack | (type << 18);
			outl(host_cmd, dev->base_addr + TLAN_HOST_CMD);
		}
	}

	spin_unlock(&priv->lock);

	return IRQ_RETVAL(type);
}




/***************************************************************
 *	tlan_close
 *
 *	Returns:
 *		An error code.
 *	Parms:
 *		dev	The device structure of the device to
 *			close.
 *
 *	This function shuts down the adapter.  It records any
 *	stats, puts the adapter into reset state, deactivates
 *	its time as needed, and	frees the irq it is using.
 *
 **************************************************************/

static int tlan_close(struct net_device *dev)
{
	tlan_stop(dev);

	free_irq(dev->irq, dev);
	tlan_free_lists(dev);
	TLAN_DBG(TLAN_DEBUG_GNRL, "Device %s closed.\n", dev->name);

	return 0;

}




/***************************************************************
 *	tlan_get_stats
 *
 *	Returns:
 *		A pointer to the device's statistics structure.
 *	Parms:
 *		dev	The device structure to return the
 *			stats for.
 *
 *	This function updates the devices statistics by reading
 *	the TLAN chip's onboard registers.  Then it returns the
 *	address of the statistics structure.
 *
 **************************************************************/

static struct net_device_stats *tlan_get_stats(struct net_device *dev)
{
	struct tlan_priv	*priv = netdev_priv(dev);
	int i;

	/* Should only read stats if open ? */
	tlan_read_and_clear_stats(dev, TLAN_RECORD);

	TLAN_DBG(TLAN_DEBUG_RX, "RECEIVE:  %s EOC count = %d\n", dev->name,
		 priv->rx_eoc_count);
	TLAN_DBG(TLAN_DEBUG_TX, "TRANSMIT:  %s Busy count = %d\n", dev->name,
		 priv->tx_busy_count);
	if (debug & TLAN_DEBUG_GNRL) {
		tlan_print_dio(dev->base_addr);
		tlan_phy_print(dev);
	}
	if (debug & TLAN_DEBUG_LIST) {
		for (i = 0; i < TLAN_NUM_RX_LISTS; i++)
			tlan_print_list(priv->rx_list + i, "RX", i);
		for (i = 0; i < TLAN_NUM_TX_LISTS; i++)
			tlan_print_list(priv->tx_list + i, "TX", i);
	}

	return &dev->stats;

}




/***************************************************************
 *	tlan_set_multicast_list
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		dev	The device structure to set the
 *			multicast list for.
 *
 *	This function sets the TLAN adaptor to various receive
 *	modes.  If the IFF_PROMISC flag is set, promiscuous
 *	mode is acitviated.  Otherwise,	promiscuous mode is
 *	turned off.  If the IFF_ALLMULTI flag is set, then
 *	the hash table is set to receive all group addresses.
 *	Otherwise, the first three multicast addresses are
 *	stored in AREG_1-3, and the rest are selected via the
 *	hash table, as necessary.
 *
 **************************************************************/

static void tlan_set_multicast_list(struct net_device *dev)
{
	struct netdev_hw_addr *ha;
	u32			hash1 = 0;
	u32			hash2 = 0;
	int			i;
	u32			offset;
	u8			tmp;

	if (dev->flags & IFF_PROMISC) {
		tmp = tlan_dio_read8(dev->base_addr, TLAN_NET_CMD);
		tlan_dio_write8(dev->base_addr,
				TLAN_NET_CMD, tmp | TLAN_NET_CMD_CAF);
	} else {
		tmp = tlan_dio_read8(dev->base_addr, TLAN_NET_CMD);
		tlan_dio_write8(dev->base_addr,
				TLAN_NET_CMD, tmp & ~TLAN_NET_CMD_CAF);
		if (dev->flags & IFF_ALLMULTI) {
			for (i = 0; i < 3; i++)
				tlan_set_mac(dev, i + 1, NULL);
			tlan_dio_write32(dev->base_addr, TLAN_HASH_1,
					 0xffffffff);
			tlan_dio_write32(dev->base_addr, TLAN_HASH_2,
					 0xffffffff);
		} else {
			i = 0;
			netdev_for_each_mc_addr(ha, dev) {
				if (i < 3) {
					tlan_set_mac(dev, i + 1,
						     (char *) &ha->addr);
				} else {
					offset =
						tlan_hash_func((u8 *)&ha->addr);
					if (offset < 32)
						hash1 |= (1 << offset);
					else
						hash2 |= (1 << (offset - 32));
				}
				i++;
			}
			for ( ; i < 3; i++)
				tlan_set_mac(dev, i + 1, NULL);
			tlan_dio_write32(dev->base_addr, TLAN_HASH_1, hash1);
			tlan_dio_write32(dev->base_addr, TLAN_HASH_2, hash2);
		}
	}

}



/*****************************************************************************
******************************************************************************

ThunderLAN driver interrupt vectors and table

please see chap. 4, "Interrupt Handling" of the "ThunderLAN
Programmer's Guide" for more informations on handling interrupts
generated by TLAN based adapters.

******************************************************************************
*****************************************************************************/




/***************************************************************
 *	tlan_handle_tx_eof
 *
 *	Returns:
 *		1
 *	Parms:
 *		dev		Device assigned the IRQ that was
 *				raised.
 *		host_int	The contents of the HOST_INT
 *				port.
 *
 *	This function handles Tx EOF interrupts which are raised
 *	by the adapter when it has completed sending the
 *	contents of a buffer.  If detemines which list/buffer
 *	was completed and resets it.  If the buffer was the last
 *	in the channel (EOC), then the function checks to see if
 *	another buffer is ready to send, and if so, sends a Tx
 *	Go command.  Finally, the driver activates/continues the
 *	activity LED.
 *
 **************************************************************/

static u32 tlan_handle_tx_eof(struct net_device *dev, u16 host_int)
{
	struct tlan_priv	*priv = netdev_priv(dev);
	int		eoc = 0;
	struct tlan_list	*head_list;
	dma_addr_t	head_list_phys;
	u32		ack = 0;
	u16		tmp_c_stat;

	TLAN_DBG(TLAN_DEBUG_TX,
		 "TRANSMIT:  Handling TX EOF (Head=%d Tail=%d)\n",
		 priv->tx_head, priv->tx_tail);
	head_list = priv->tx_list + priv->tx_head;

	while (((tmp_c_stat = head_list->c_stat) & TLAN_CSTAT_FRM_CMP)
	       && (ack < 255)) {
		struct sk_buff *skb = tlan_get_skb(head_list);

		ack++;
		dma_unmap_single(&priv->pci_dev->dev,
				 head_list->buffer[0].address,
				 max(skb->len, (unsigned int)TLAN_MIN_FRAME_SIZE),
				 DMA_TO_DEVICE);
		dev_kfree_skb_any(skb);
		head_list->buffer[8].address = 0;
		head_list->buffer[9].address = 0;

		if (tmp_c_stat & TLAN_CSTAT_EOC)
			eoc = 1;

		dev->stats.tx_bytes += head_list->frame_size;

		head_list->c_stat = TLAN_CSTAT_UNUSED;
		netif_start_queue(dev);
		CIRC_INC(priv->tx_head, TLAN_NUM_TX_LISTS);
		head_list = priv->tx_list + priv->tx_head;
	}

	if (!ack)
		netdev_info(dev,
			    "Received interrupt for uncompleted TX frame\n");

	if (eoc) {
		TLAN_DBG(TLAN_DEBUG_TX,
			 "TRANSMIT:  handling TX EOC (Head=%d Tail=%d)\n",
			 priv->tx_head, priv->tx_tail);
		head_list = priv->tx_list + priv->tx_head;
		head_list_phys = priv->tx_list_dma
			+ sizeof(struct tlan_list)*priv->tx_head;
		if ((head_list->c_stat & TLAN_CSTAT_READY)
		    == TLAN_CSTAT_READY) {
			outl(head_list_phys, dev->base_addr + TLAN_CH_PARM);
			ack |= TLAN_HC_GO;
		} else {
			priv->tx_in_progress = 0;
		}
	}

	if (priv->adapter->flags & TLAN_ADAPTER_ACTIVITY_LED) {
		tlan_dio_write8(dev->base_addr,
				TLAN_LED_REG, TLAN_LED_LINK | TLAN_LED_ACT);
		if (priv->timer.function == NULL) {
			priv->timer.function = tlan_timer;
			priv->timer.expires = jiffies + TLAN_TIMER_ACT_DELAY;
			priv->timer_set_at = jiffies;
			priv->timer_type = TLAN_TIMER_ACTIVITY;
			add_timer(&priv->timer);
		} else if (priv->timer_type == TLAN_TIMER_ACTIVITY) {
			priv->timer_set_at = jiffies;
		}
	}

	return ack;

}




/***************************************************************
 *	TLan_HandleStatOverflow
 *
 *	Returns:
 *		1
 *	Parms:
 *		dev		Device assigned the IRQ that was
 *				raised.
 *		host_int	The contents of the HOST_INT
 *				port.
 *
 *	This function handles the Statistics Overflow interrupt
 *	which means that one or more of the TLAN statistics
 *	registers has reached 1/2 capacity and needs to be read.
 *
 **************************************************************/

static u32 tlan_handle_stat_overflow(struct net_device *dev, u16 host_int)
{
	tlan_read_and_clear_stats(dev, TLAN_RECORD);

	return 1;

}




/***************************************************************
 *	TLan_HandleRxEOF
 *
 *	Returns:
 *		1
 *	Parms:
 *		dev		Device assigned the IRQ that was
 *				raised.
 *		host_int	The contents of the HOST_INT
 *				port.
 *
 *	This function handles the Rx EOF interrupt which
 *	indicates a frame has been received by the adapter from
 *	the net and the frame has been transferred to memory.
 *	The function determines the bounce buffer the frame has
 *	been loaded into, creates a new sk_buff big enough to
 *	hold the frame, and sends it to protocol stack.  It
 *	then resets the used buffer and appends it to the end
 *	of the list.  If the frame was the last in the Rx
 *	channel (EOC), the function restarts the receive channel
 *	by sending an Rx Go command to the adapter.  Then it
 *	activates/continues the activity LED.
 *
 **************************************************************/

static u32 tlan_handle_rx_eof(struct net_device *dev, u16 host_int)
{
	struct tlan_priv	*priv = netdev_priv(dev);
	u32		ack = 0;
	int		eoc = 0;
	struct tlan_list	*head_list;
	struct sk_buff	*skb;
	struct tlan_list	*tail_list;
	u16		tmp_c_stat;
	dma_addr_t	head_list_phys;

	TLAN_DBG(TLAN_DEBUG_RX, "RECEIVE:  handling RX EOF (Head=%d Tail=%d)\n",
		 priv->rx_head, priv->rx_tail);
	head_list = priv->rx_list + priv->rx_head;
	head_list_phys =
		priv->rx_list_dma + sizeof(struct tlan_list)*priv->rx_head;

	while (((tmp_c_stat = head_list->c_stat) & TLAN_CSTAT_FRM_CMP)
	       && (ack < 255)) {
		dma_addr_t frame_dma = head_list->buffer[0].address;
		u32 frame_size = head_list->frame_size;
		struct sk_buff *new_skb;

		ack++;
		if (tmp_c_stat & TLAN_CSTAT_EOC)
			eoc = 1;

		new_skb = netdev_alloc_skb_ip_align(dev,
						    TLAN_MAX_FRAME_SIZE + 5);
		if (!new_skb)
			goto drop_and_reuse;

		skb = tlan_get_skb(head_list);
		dma_unmap_single(&priv->pci_dev->dev, frame_dma,
				 TLAN_MAX_FRAME_SIZE, DMA_FROM_DEVICE);
		skb_put(skb, frame_size);

		dev->stats.rx_bytes += frame_size;

		skb->protocol = eth_type_trans(skb, dev);
		netif_rx(skb);

		head_list->buffer[0].address =
			dma_map_single(&priv->pci_dev->dev, new_skb->data,
				       TLAN_MAX_FRAME_SIZE, DMA_FROM_DEVICE);

		tlan_store_skb(head_list, new_skb);
drop_and_reuse:
		head_list->forward = 0;
		head_list->c_stat = 0;
		tail_list = priv->rx_list + priv->rx_tail;
		tail_list->forward = head_list_phys;

		CIRC_INC(priv->rx_head, TLAN_NUM_RX_LISTS);
		CIRC_INC(priv->rx_tail, TLAN_NUM_RX_LISTS);
		head_list = priv->rx_list + priv->rx_head;
		head_list_phys = priv->rx_list_dma
			+ sizeof(struct tlan_list)*priv->rx_head;
	}

	if (!ack)
		netdev_info(dev,
			    "Received interrupt for uncompleted RX frame\n");


	if (eoc) {
		TLAN_DBG(TLAN_DEBUG_RX,
			 "RECEIVE:  handling RX EOC (Head=%d Tail=%d)\n",
			 priv->rx_head, priv->rx_tail);
		head_list = priv->rx_list + priv->rx_head;
		head_list_phys = priv->rx_list_dma
			+ sizeof(struct tlan_list)*priv->rx_head;
		outl(head_list_phys, dev->base_addr + TLAN_CH_PARM);
		ack |= TLAN_HC_GO | TLAN_HC_RT;
		priv->rx_eoc_count++;
	}

	if (priv->adapter->flags & TLAN_ADAPTER_ACTIVITY_LED) {
		tlan_dio_write8(dev->base_addr,
				TLAN_LED_REG, TLAN_LED_LINK | TLAN_LED_ACT);
		if (priv->timer.function == NULL)  {
			priv->timer.function = tlan_timer;
			priv->timer.expires = jiffies + TLAN_TIMER_ACT_DELAY;
			priv->timer_set_at = jiffies;
			priv->timer_type = TLAN_TIMER_ACTIVITY;
			add_timer(&priv->timer);
		} else if (priv->timer_type == TLAN_TIMER_ACTIVITY) {
			priv->timer_set_at = jiffies;
		}
	}

	return ack;

}




/***************************************************************
 *	tlan_handle_dummy
 *
 *	Returns:
 *		1
 *	Parms:
 *		dev		Device assigned the IRQ that was
 *				raised.
 *		host_int	The contents of the HOST_INT
 *				port.
 *
 *	This function handles the Dummy interrupt, which is
 *	raised whenever a test interrupt is generated by setting
 *	the Req_Int bit of HOST_CMD to 1.
 *
 **************************************************************/

static u32 tlan_handle_dummy(struct net_device *dev, u16 host_int)
{
	netdev_info(dev, "Test interrupt\n");
	return 1;

}




/***************************************************************
 *	tlan_handle_tx_eoc
 *
 *	Returns:
 *		1
 *	Parms:
 *		dev		Device assigned the IRQ that was
 *				raised.
 *		host_int	The contents of the HOST_INT
 *				port.
 *
 *	This driver is structured to determine EOC occurrences by
 *	reading the CSTAT member of the list structure.  Tx EOC
 *	interrupts are disabled via the DIO INTDIS register.
 *	However, TLAN chips before revision 3.0 didn't have this
 *	functionality, so process EOC events if this is the
 *	case.
 *
 **************************************************************/

static u32 tlan_handle_tx_eoc(struct net_device *dev, u16 host_int)
{
	struct tlan_priv	*priv = netdev_priv(dev);
	struct tlan_list		*head_list;
	dma_addr_t		head_list_phys;
	u32			ack = 1;

	if (priv->tlan_rev < 0x30) {
		TLAN_DBG(TLAN_DEBUG_TX,
			 "TRANSMIT:  handling TX EOC (Head=%d Tail=%d) -- IRQ\n",
			 priv->tx_head, priv->tx_tail);
		head_list = priv->tx_list + priv->tx_head;
		head_list_phys = priv->tx_list_dma
			+ sizeof(struct tlan_list)*priv->tx_head;
		if ((head_list->c_stat & TLAN_CSTAT_READY)
		    == TLAN_CSTAT_READY) {
			netif_stop_queue(dev);
			outl(head_list_phys, dev->base_addr + TLAN_CH_PARM);
			ack |= TLAN_HC_GO;
		} else {
			priv->tx_in_progress = 0;
		}
	}

	return ack;

}




/***************************************************************
 *	tlan_handle_status_check
 *
 *	Returns:
 *		0 if Adapter check, 1 if Network Status check.
 *	Parms:
 *		dev		Device assigned the IRQ that was
 *				raised.
 *		host_int	The contents of the HOST_INT
 *				port.
 *
 *	This function handles Adapter Check/Network Status
 *	interrupts generated by the adapter.  It checks the
 *	vector in the HOST_INT register to determine if it is
 *	an Adapter Check interrupt.  If so, it resets the
 *	adapter.  Otherwise it clears the status registers
 *	and services the PHY.
 *
 **************************************************************/

static u32 tlan_handle_status_check(struct net_device *dev, u16 host_int)
{
	struct tlan_priv	*priv = netdev_priv(dev);
	u32		ack;
	u32		error;
	u8		net_sts;
	u32		phy;
	u16		tlphy_ctl;
	u16		tlphy_sts;

	ack = 1;
	if (host_int & TLAN_HI_IV_MASK) {
		netif_stop_queue(dev);
		error = inl(dev->base_addr + TLAN_CH_PARM);
		netdev_info(dev, "Adaptor Error = 0x%x\n", error);
		tlan_read_and_clear_stats(dev, TLAN_RECORD);
		outl(TLAN_HC_AD_RST, dev->base_addr + TLAN_HOST_CMD);

		schedule_work(&priv->tlan_tqueue);

		netif_wake_queue(dev);
		ack = 0;
	} else {
		TLAN_DBG(TLAN_DEBUG_GNRL, "%s: Status Check\n", dev->name);
		phy = priv->phy[priv->phy_num];

		net_sts = tlan_dio_read8(dev->base_addr, TLAN_NET_STS);
		if (net_sts) {
			tlan_dio_write8(dev->base_addr, TLAN_NET_STS, net_sts);
			TLAN_DBG(TLAN_DEBUG_GNRL, "%s:    Net_Sts = %x\n",
				 dev->name, (unsigned) net_sts);
		}
		if ((net_sts & TLAN_NET_STS_MIRQ) &&  (priv->phy_num == 0)) {
			tlan_mii_read_reg(dev, phy, TLAN_TLPHY_STS, &tlphy_sts);
			tlan_mii_read_reg(dev, phy, TLAN_TLPHY_CTL, &tlphy_ctl);
			if (!(tlphy_sts & TLAN_TS_POLOK) &&
			    !(tlphy_ctl & TLAN_TC_SWAPOL)) {
				tlphy_ctl |= TLAN_TC_SWAPOL;
				tlan_mii_write_reg(dev, phy, TLAN_TLPHY_CTL,
						   tlphy_ctl);
			} else if ((tlphy_sts & TLAN_TS_POLOK) &&
				   (tlphy_ctl & TLAN_TC_SWAPOL)) {
				tlphy_ctl &= ~TLAN_TC_SWAPOL;
				tlan_mii_write_reg(dev, phy, TLAN_TLPHY_CTL,
						   tlphy_ctl);
			}

			if (debug)
				tlan_phy_print(dev);
		}
	}

	return ack;

}




/***************************************************************
 *	tlan_handle_rx_eoc
 *
 *	Returns:
 *		1
 *	Parms:
 *		dev		Device assigned the IRQ that was
 *				raised.
 *		host_int	The contents of the HOST_INT
 *				port.
 *
 *	This driver is structured to determine EOC occurrences by
 *	reading the CSTAT member of the list structure.  Rx EOC
 *	interrupts are disabled via the DIO INTDIS register.
 *	However, TLAN chips before revision 3.0 didn't have this
 *	CSTAT member or a INTDIS register, so if this chip is
 *	pre-3.0, process EOC interrupts normally.
 *
 **************************************************************/

static u32 tlan_handle_rx_eoc(struct net_device *dev, u16 host_int)
{
	struct tlan_priv	*priv = netdev_priv(dev);
	dma_addr_t	head_list_phys;
	u32		ack = 1;

	if (priv->tlan_rev < 0x30) {
		TLAN_DBG(TLAN_DEBUG_RX,
			 "RECEIVE:  Handling RX EOC (head=%d tail=%d) -- IRQ\n",
			 priv->rx_head, priv->rx_tail);
		head_list_phys = priv->rx_list_dma
			+ sizeof(struct tlan_list)*priv->rx_head;
		outl(head_list_phys, dev->base_addr + TLAN_CH_PARM);
		ack |= TLAN_HC_GO | TLAN_HC_RT;
		priv->rx_eoc_count++;
	}

	return ack;

}




/*****************************************************************************
******************************************************************************

ThunderLAN driver timer function

******************************************************************************
*****************************************************************************/


/***************************************************************
 *	tlan_timer
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		data	A value given to add timer when
 *			add_timer was called.
 *
 *	This function handles timed functionality for the
 *	TLAN driver.  The two current timer uses are for
 *	delaying for autonegotionation and driving the ACT LED.
 *	-	Autonegotiation requires being allowed about
 *		2 1/2 seconds before attempting to transmit a
 *		packet.  It would be a very bad thing to hang
 *		the kernel this long, so the driver doesn't
 *		allow transmission 'til after this time, for
 *		certain PHYs.  It would be much nicer if all
 *		PHYs were interrupt-capable like the internal
 *		PHY.
 *	-	The ACT LED, which shows adapter activity, is
 *		driven by the driver, and so must be left on
 *		for a short period to power up the LED so it
 *		can be seen.  This delay can be changed by
 *		changing the TLAN_TIMER_ACT_DELAY in tlan.h,
 *		if desired.  100 ms  produces a slightly
 *		sluggish response.
 *
 **************************************************************/

static void tlan_timer(struct timer_list *t)
{
	struct tlan_priv	*priv = from_timer(priv, t, timer);
	struct net_device	*dev = priv->dev;
	u32		elapsed;
	unsigned long	flags = 0;

	priv->timer.function = NULL;

	switch (priv->timer_type) {
	case TLAN_TIMER_PHY_PDOWN:
		tlan_phy_power_down(dev);
		break;
	case TLAN_TIMER_PHY_PUP:
		tlan_phy_power_up(dev);
		break;
	case TLAN_TIMER_PHY_RESET:
		tlan_phy_reset(dev);
		break;
	case TLAN_TIMER_PHY_START_LINK:
		tlan_phy_start_link(dev);
		break;
	case TLAN_TIMER_PHY_FINISH_AN:
		tlan_phy_finish_auto_neg(dev);
		break;
	case TLAN_TIMER_FINISH_RESET:
		tlan_finish_reset(dev);
		break;
	case TLAN_TIMER_ACTIVITY:
		spin_lock_irqsave(&priv->lock, flags);
		if (priv->timer.function == NULL) {
			elapsed = jiffies - priv->timer_set_at;
			if (elapsed >= TLAN_TIMER_ACT_DELAY) {
				tlan_dio_write8(dev->base_addr,
						TLAN_LED_REG, TLAN_LED_LINK);
			} else  {
				priv->timer.expires = priv->timer_set_at
					+ TLAN_TIMER_ACT_DELAY;
				spin_unlock_irqrestore(&priv->lock, flags);
				add_timer(&priv->timer);
				break;
			}
		}
		spin_unlock_irqrestore(&priv->lock, flags);
		break;
	default:
		break;
	}

}


/*****************************************************************************
******************************************************************************

ThunderLAN driver adapter related routines

******************************************************************************
*****************************************************************************/


/***************************************************************
 *	tlan_reset_lists
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		dev	The device structure with the list
 *			structures to be reset.
 *
 *	This routine sets the variables associated with managing
 *	the TLAN lists to their initial values.
 *
 **************************************************************/

static void tlan_reset_lists(struct net_device *dev)
{
	struct tlan_priv *priv = netdev_priv(dev);
	int		i;
	struct tlan_list	*list;
	dma_addr_t	list_phys;
	struct sk_buff	*skb;

	priv->tx_head = 0;
	priv->tx_tail = 0;
	for (i = 0; i < TLAN_NUM_TX_LISTS; i++) {
		list = priv->tx_list + i;
		list->c_stat = TLAN_CSTAT_UNUSED;
		list->buffer[0].address = 0;
		list->buffer[2].count = 0;
		list->buffer[2].address = 0;
		list->buffer[8].address = 0;
		list->buffer[9].address = 0;
	}

	priv->rx_head = 0;
	priv->rx_tail = TLAN_NUM_RX_LISTS - 1;
	for (i = 0; i < TLAN_NUM_RX_LISTS; i++) {
		list = priv->rx_list + i;
		list_phys = priv->rx_list_dma + sizeof(struct tlan_list)*i;
		list->c_stat = TLAN_CSTAT_READY;
		list->frame_size = TLAN_MAX_FRAME_SIZE;
		list->buffer[0].count = TLAN_MAX_FRAME_SIZE | TLAN_LAST_BUFFER;
		skb = netdev_alloc_skb_ip_align(dev, TLAN_MAX_FRAME_SIZE + 5);
		if (!skb)
			break;

		list->buffer[0].address = dma_map_single(&priv->pci_dev->dev,
							 skb->data,
							 TLAN_MAX_FRAME_SIZE,
							 DMA_FROM_DEVICE);
		tlan_store_skb(list, skb);
		list->buffer[1].count = 0;
		list->buffer[1].address = 0;
		list->forward = list_phys + sizeof(struct tlan_list);
	}

	/* in case ran out of memory early, clear bits */
	while (i < TLAN_NUM_RX_LISTS) {
		tlan_store_skb(priv->rx_list + i, NULL);
		++i;
	}
	list->forward = 0;

}


static void tlan_free_lists(struct net_device *dev)
{
	struct tlan_priv *priv = netdev_priv(dev);
	int		i;
	struct tlan_list	*list;
	struct sk_buff	*skb;

	for (i = 0; i < TLAN_NUM_TX_LISTS; i++) {
		list = priv->tx_list + i;
		skb = tlan_get_skb(list);
		if (skb) {
			dma_unmap_single(&priv->pci_dev->dev,
					 list->buffer[0].address,
					 max(skb->len, (unsigned int)TLAN_MIN_FRAME_SIZE),
					 DMA_TO_DEVICE);
			dev_kfree_skb_any(skb);
			list->buffer[8].address = 0;
			list->buffer[9].address = 0;
		}
	}

	for (i = 0; i < TLAN_NUM_RX_LISTS; i++) {
		list = priv->rx_list + i;
		skb = tlan_get_skb(list);
		if (skb) {
			dma_unmap_single(&priv->pci_dev->dev,
					 list->buffer[0].address,
					 TLAN_MAX_FRAME_SIZE, DMA_FROM_DEVICE);
			dev_kfree_skb_any(skb);
			list->buffer[8].address = 0;
			list->buffer[9].address = 0;
		}
	}
}




/***************************************************************
 *	tlan_print_dio
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		io_base		Base IO port of the device of
 *				which to print DIO registers.
 *
 *	This function prints out all the internal (DIO)
 *	registers of a TLAN chip.
 *
 **************************************************************/

static void tlan_print_dio(u16 io_base)
{
	u32 data0, data1;
	int	i;

	pr_info("Contents of internal registers for io base 0x%04hx\n",
		io_base);
	pr_info("Off.  +0        +4\n");
	for (i = 0; i < 0x4C; i += 8) {
		data0 = tlan_dio_read32(io_base, i);
		data1 = tlan_dio_read32(io_base, i + 0x4);
		pr_info("0x%02x  0x%08x 0x%08x\n", i, data0, data1);
	}

}




/***************************************************************
 *	TLan_PrintList
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		list	A pointer to the struct tlan_list structure to
 *			be printed.
 *		type	A string to designate type of list,
 *			"Rx" or "Tx".
 *		num	The index of the list.
 *
 *	This function prints out the contents of the list
 *	pointed to by the list parameter.
 *
 **************************************************************/

static void tlan_print_list(struct tlan_list *list, char *type, int num)
{
	int i;

	pr_info("%s List %d at %p\n", type, num, list);
	pr_info("   Forward    = 0x%08x\n",  list->forward);
	pr_info("   CSTAT      = 0x%04hx\n", list->c_stat);
	pr_info("   Frame Size = 0x%04hx\n", list->frame_size);
	/* for (i = 0; i < 10; i++) { */
	for (i = 0; i < 2; i++) {
		pr_info("   Buffer[%d].count, addr = 0x%08x, 0x%08x\n",
			i, list->buffer[i].count, list->buffer[i].address);
	}

}




/***************************************************************
 *	tlan_read_and_clear_stats
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		dev	Pointer to device structure of adapter
 *			to which to read stats.
 *		record	Flag indicating whether to add
 *
 *	This functions reads all the internal status registers
 *	of the TLAN chip, which clears them as a side effect.
 *	It then either adds the values to the device's status
 *	struct, or discards them, depending on whether record
 *	is TLAN_RECORD (!=0)  or TLAN_IGNORE (==0).
 *
 **************************************************************/

static void tlan_read_and_clear_stats(struct net_device *dev, int record)
{
	u32		tx_good, tx_under;
	u32		rx_good, rx_over;
	u32		def_tx, crc, code;
	u32		multi_col, single_col;
	u32		excess_col, late_col, loss;

	outw(TLAN_GOOD_TX_FRMS, dev->base_addr + TLAN_DIO_ADR);
	tx_good  = inb(dev->base_addr + TLAN_DIO_DATA);
	tx_good += inb(dev->base_addr + TLAN_DIO_DATA + 1) << 8;
	tx_good += inb(dev->base_addr + TLAN_DIO_DATA + 2) << 16;
	tx_under = inb(dev->base_addr + TLAN_DIO_DATA + 3);

	outw(TLAN_GOOD_RX_FRMS, dev->base_addr + TLAN_DIO_ADR);
	rx_good  = inb(dev->base_addr + TLAN_DIO_DATA);
	rx_good += inb(dev->base_addr + TLAN_DIO_DATA + 1) << 8;
	rx_good += inb(dev->base_addr + TLAN_DIO_DATA + 2) << 16;
	rx_over  = inb(dev->base_addr + TLAN_DIO_DATA + 3);

	outw(TLAN_DEFERRED_TX, dev->base_addr + TLAN_DIO_ADR);
	def_tx  = inb(dev->base_addr + TLAN_DIO_DATA);
	def_tx += inb(dev->base_addr + TLAN_DIO_DATA + 1) << 8;
	crc     = inb(dev->base_addr + TLAN_DIO_DATA + 2);
	code    = inb(dev->base_addr + TLAN_DIO_DATA + 3);

	outw(TLAN_MULTICOL_FRMS, dev->base_addr + TLAN_DIO_ADR);
	multi_col   = inb(dev->base_addr + TLAN_DIO_DATA);
	multi_col  += inb(dev->base_addr + TLAN_DIO_DATA + 1) << 8;
	single_col  = inb(dev->base_addr + TLAN_DIO_DATA + 2);
	single_col += inb(dev->base_addr + TLAN_DIO_DATA + 3) << 8;

	outw(TLAN_EXCESSCOL_FRMS, dev->base_addr + TLAN_DIO_ADR);
	excess_col = inb(dev->base_addr + TLAN_DIO_DATA);
	late_col   = inb(dev->base_addr + TLAN_DIO_DATA + 1);
	loss       = inb(dev->base_addr + TLAN_DIO_DATA + 2);

	if (record) {
		dev->stats.rx_packets += rx_good;
		dev->stats.rx_errors  += rx_over + crc + code;
		dev->stats.tx_packets += tx_good;
		dev->stats.tx_errors  += tx_under + loss;
		dev->stats.collisions += multi_col
			+ single_col + excess_col + late_col;

		dev->stats.rx_over_errors    += rx_over;
		dev->stats.rx_crc_errors     += crc;
		dev->stats.rx_frame_errors   += code;

		dev->stats.tx_aborted_errors += tx_under;
		dev->stats.tx_carrier_errors += loss;
	}

}




/***************************************************************
 *	TLan_Reset
 *
 *	Returns:
 *		0
 *	Parms:
 *		dev	Pointer to device structure of adapter
 *			to be reset.
 *
 *	This function resets the adapter and it's physical
 *	device.  See Chap. 3, pp. 9-10 of the "ThunderLAN
 *	Programmer's Guide" for details.  The routine tries to
 *	implement what is detailed there, though adjustments
 *	have been made.
 *
 **************************************************************/

static void
tlan_reset_adapter(struct net_device *dev)
{
	struct tlan_priv	*priv = netdev_priv(dev);
	int		i;
	u32		addr;
	u32		data;
	u8		data8;

	priv->tlan_full_duplex = false;
	priv->phy_online = 0;
	netif_carrier_off(dev);

/*  1.	Assert reset bit. */

	data = inl(dev->base_addr + TLAN_HOST_CMD);
	data |= TLAN_HC_AD_RST;
	outl(data, dev->base_addr + TLAN_HOST_CMD);

	udelay(1000);

/*  2.	Turn off interrupts. (Probably isn't necessary) */

	data = inl(dev->base_addr + TLAN_HOST_CMD);
	data |= TLAN_HC_INT_OFF;
	outl(data, dev->base_addr + TLAN_HOST_CMD);

/*  3.	Clear AREGs and HASHs. */

	for (i = TLAN_AREG_0; i <= TLAN_HASH_2; i += 4)
		tlan_dio_write32(dev->base_addr, (u16) i, 0);

/*  4.	Setup NetConfig register. */

	data = TLAN_NET_CFG_1FRAG | TLAN_NET_CFG_1CHAN | TLAN_NET_CFG_PHY_EN;
	tlan_dio_write16(dev->base_addr, TLAN_NET_CONFIG, (u16) data);

/*  5.	Load Ld_Tmr and Ld_Thr in HOST_CMD. */

	outl(TLAN_HC_LD_TMR | 0x3f, dev->base_addr + TLAN_HOST_CMD);
	outl(TLAN_HC_LD_THR | 0x9, dev->base_addr + TLAN_HOST_CMD);

/*  6.	Unreset the MII by setting NMRST (in NetSio) to 1. */

	outw(TLAN_NET_SIO, dev->base_addr + TLAN_DIO_ADR);
	addr = dev->base_addr + TLAN_DIO_DATA + TLAN_NET_SIO;
	tlan_set_bit(TLAN_NET_SIO_NMRST, addr);

/*  7.	Setup the remaining registers. */

	if (priv->tlan_rev >= 0x30) {
		data8 = TLAN_ID_TX_EOC | TLAN_ID_RX_EOC;
		tlan_dio_write8(dev->base_addr, TLAN_INT_DIS, data8);
	}
	tlan_phy_detect(dev);
	data = TLAN_NET_CFG_1FRAG | TLAN_NET_CFG_1CHAN;

	if (priv->adapter->flags & TLAN_ADAPTER_BIT_RATE_PHY) {
		data |= TLAN_NET_CFG_BIT;
		if (priv->aui == 1) {
			tlan_dio_write8(dev->base_addr, TLAN_ACOMMIT, 0x0a);
		} else if (priv->duplex == TLAN_DUPLEX_FULL) {
			tlan_dio_write8(dev->base_addr, TLAN_ACOMMIT, 0x00);
			priv->tlan_full_duplex = true;
		} else {
			tlan_dio_write8(dev->base_addr, TLAN_ACOMMIT, 0x08);
		}
	}

	/* don't power down internal PHY if we're going to use it */
	if (priv->phy_num == 0 ||
	   (priv->adapter->flags & TLAN_ADAPTER_USE_INTERN_10))
		data |= TLAN_NET_CFG_PHY_EN;
	tlan_dio_write16(dev->base_addr, TLAN_NET_CONFIG, (u16) data);

	if (priv->adapter->flags & TLAN_ADAPTER_UNMANAGED_PHY)
		tlan_finish_reset(dev);
	else
		tlan_phy_power_down(dev);

}




static void
tlan_finish_reset(struct net_device *dev)
{
	struct tlan_priv	*priv = netdev_priv(dev);
	u8		data;
	u32		phy;
	u8		sio;
	u16		status;
	u16		partner;
	u16		tlphy_ctl;
	u16		tlphy_par;
	u16		tlphy_id1, tlphy_id2;
	int		i;

	phy = priv->phy[priv->phy_num];

	data = TLAN_NET_CMD_NRESET | TLAN_NET_CMD_NWRAP;
	if (priv->tlan_full_duplex)
		data |= TLAN_NET_CMD_DUPLEX;
	tlan_dio_write8(dev->base_addr, TLAN_NET_CMD, data);
	data = TLAN_NET_MASK_MASK4 | TLAN_NET_MASK_MASK5;
	if (priv->phy_num == 0)
		data |= TLAN_NET_MASK_MASK7;
	tlan_dio_write8(dev->base_addr, TLAN_NET_MASK, data);
	tlan_dio_write16(dev->base_addr, TLAN_MAX_RX, ((1536)+7)&~7);
	tlan_mii_read_reg(dev, phy, MII_GEN_ID_HI, &tlphy_id1);
	tlan_mii_read_reg(dev, phy, MII_GEN_ID_LO, &tlphy_id2);

	if ((priv->adapter->flags & TLAN_ADAPTER_UNMANAGED_PHY) ||
	    (priv->aui)) {
		status = MII_GS_LINK;
		netdev_info(dev, "Link forced\n");
	} else {
		tlan_mii_read_reg(dev, phy, MII_GEN_STS, &status);
		udelay(1000);
		tlan_mii_read_reg(dev, phy, MII_GEN_STS, &status);
		if (status & MII_GS_LINK) {
			/* We only support link info on Nat.Sem. PHY's */
			if ((tlphy_id1 == NAT_SEM_ID1) &&
			    (tlphy_id2 == NAT_SEM_ID2)) {
				tlan_mii_read_reg(dev, phy, MII_AN_LPA,
					&partner);
				tlan_mii_read_reg(dev, phy, TLAN_TLPHY_PAR,
					&tlphy_par);

				netdev_info(dev,
					"Link active, %s %uMbps %s-Duplex\n",
					!(tlphy_par & TLAN_PHY_AN_EN_STAT)
					? "forced" : "Autonegotiation enabled,",
					tlphy_par & TLAN_PHY_SPEED_100
					? 100 : 10,
					tlphy_par & TLAN_PHY_DUPLEX_FULL
					? "Full" : "Half");

				if (tlphy_par & TLAN_PHY_AN_EN_STAT) {
					netdev_info(dev, "Partner capability:");
					for (i = 5; i < 10; i++)
						if (partner & (1 << i))
							pr_cont(" %s",
								media[i-5]);
					pr_cont("\n");
				}
			} else
				netdev_info(dev, "Link active\n");
			/* Enabling link beat monitoring */
			priv->media_timer.expires = jiffies + HZ;
			add_timer(&priv->media_timer);
		}
	}

	if (priv->phy_num == 0) {
		tlan_mii_read_reg(dev, phy, TLAN_TLPHY_CTL, &tlphy_ctl);
		tlphy_ctl |= TLAN_TC_INTEN;
		tlan_mii_write_reg(dev, phy, TLAN_TLPHY_CTL, tlphy_ctl);
		sio = tlan_dio_read8(dev->base_addr, TLAN_NET_SIO);
		sio |= TLAN_NET_SIO_MINTEN;
		tlan_dio_write8(dev->base_addr, TLAN_NET_SIO, sio);
	}

	if (status & MII_GS_LINK) {
		tlan_set_mac(dev, 0, dev->dev_addr);
		priv->phy_online = 1;
		outb((TLAN_HC_INT_ON >> 8), dev->base_addr + TLAN_HOST_CMD + 1);
		if (debug >= 1 && debug != TLAN_DEBUG_PROBE)
			outb((TLAN_HC_REQ_INT >> 8),
			     dev->base_addr + TLAN_HOST_CMD + 1);
		outl(priv->rx_list_dma, dev->base_addr + TLAN_CH_PARM);
		outl(TLAN_HC_GO | TLAN_HC_RT, dev->base_addr + TLAN_HOST_CMD);
		tlan_dio_write8(dev->base_addr, TLAN_LED_REG, TLAN_LED_LINK);
		netif_carrier_on(dev);
	} else {
		netdev_info(dev, "Link inactive, will retry in 10 secs...\n");
		tlan_set_timer(dev, (10*HZ), TLAN_TIMER_FINISH_RESET);
		return;
	}
	tlan_set_multicast_list(dev);

}




/***************************************************************
 *	tlan_set_mac
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		dev	Pointer to device structure of adapter
 *			on which to change the AREG.
 *		areg	The AREG to set the address in (0 - 3).
 *		mac	A pointer to an array of chars.  Each
 *			element stores one byte of the address.
 *			IE, it isn't in ascii.
 *
 *	This function transfers a MAC address to one of the
 *	TLAN AREGs (address registers).  The TLAN chip locks
 *	the register on writing to offset 0 and unlocks the
 *	register after writing to offset 5.  If NULL is passed
 *	in mac, then the AREG is filled with 0's.
 *
 **************************************************************/

static void tlan_set_mac(struct net_device *dev, int areg, char *mac)
{
	int i;

	areg *= 6;

	if (mac != NULL) {
		for (i = 0; i < 6; i++)
			tlan_dio_write8(dev->base_addr,
					TLAN_AREG_0 + areg + i, mac[i]);
	} else {
		for (i = 0; i < 6; i++)
			tlan_dio_write8(dev->base_addr,
					TLAN_AREG_0 + areg + i, 0);
	}

}




/*****************************************************************************
******************************************************************************

ThunderLAN driver PHY layer routines

******************************************************************************
*****************************************************************************/



/*********************************************************************
 *	tlan_phy_print
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		dev	A pointer to the device structure of the
 *			TLAN device having the PHYs to be detailed.
 *
 *	This function prints the registers a PHY (aka transceiver).
 *
 ********************************************************************/

static void tlan_phy_print(struct net_device *dev)
{
	struct tlan_priv *priv = netdev_priv(dev);
	u16 i, data0, data1, data2, data3, phy;

	phy = priv->phy[priv->phy_num];

	if (priv->adapter->flags & TLAN_ADAPTER_UNMANAGED_PHY) {
		netdev_info(dev, "Unmanaged PHY\n");
	} else if (phy <= TLAN_PHY_MAX_ADDR) {
		netdev_info(dev, "PHY 0x%02x\n", phy);
		pr_info("   Off.  +0     +1     +2     +3\n");
		for (i = 0; i < 0x20; i += 4) {
			tlan_mii_read_reg(dev, phy, i, &data0);
			tlan_mii_read_reg(dev, phy, i + 1, &data1);
			tlan_mii_read_reg(dev, phy, i + 2, &data2);
			tlan_mii_read_reg(dev, phy, i + 3, &data3);
			pr_info("   0x%02x 0x%04hx 0x%04hx 0x%04hx 0x%04hx\n",
				i, data0, data1, data2, data3);
		}
	} else {
		netdev_info(dev, "Invalid PHY\n");
	}

}




/*********************************************************************
 *	tlan_phy_detect
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		dev	A pointer to the device structure of the adapter
 *			for which the PHY needs determined.
 *
 *	So far I've found that adapters which have external PHYs
 *	may also use the internal PHY for part of the functionality.
 *	(eg, AUI/Thinnet).  This function finds out if this TLAN
 *	chip has an internal PHY, and then finds the first external
 *	PHY (starting from address 0) if it exists).
 *
 ********************************************************************/

static void tlan_phy_detect(struct net_device *dev)
{
	struct tlan_priv *priv = netdev_priv(dev);
	u16		control;
	u16		hi;
	u16		lo;
	u32		phy;

	if (priv->adapter->flags & TLAN_ADAPTER_UNMANAGED_PHY) {
		priv->phy_num = 0xffff;
		return;
	}

	tlan_mii_read_reg(dev, TLAN_PHY_MAX_ADDR, MII_GEN_ID_HI, &hi);

	if (hi != 0xffff)
		priv->phy[0] = TLAN_PHY_MAX_ADDR;
	else
		priv->phy[0] = TLAN_PHY_NONE;

	priv->phy[1] = TLAN_PHY_NONE;
	for (phy = 0; phy <= TLAN_PHY_MAX_ADDR; phy++) {
		tlan_mii_read_reg(dev, phy, MII_GEN_CTL, &control);
		tlan_mii_read_reg(dev, phy, MII_GEN_ID_HI, &hi);
		tlan_mii_read_reg(dev, phy, MII_GEN_ID_LO, &lo);
		if ((control != 0xffff) ||
		    (hi != 0xffff) || (lo != 0xffff)) {
			TLAN_DBG(TLAN_DEBUG_GNRL,
				 "PHY found at %02x %04x %04x %04x\n",
				 phy, control, hi, lo);
			if ((priv->phy[1] == TLAN_PHY_NONE) &&
			    (phy != TLAN_PHY_MAX_ADDR)) {
				priv->phy[1] = phy;
			}
		}
	}

	if (priv->phy[1] != TLAN_PHY_NONE)
		priv->phy_num = 1;
	else if (priv->phy[0] != TLAN_PHY_NONE)
		priv->phy_num = 0;
	else
		netdev_info(dev, "Cannot initialize device, no PHY was found!\n");

}




static void tlan_phy_power_down(struct net_device *dev)
{
	struct tlan_priv	*priv = netdev_priv(dev);
	u16		value;

	TLAN_DBG(TLAN_DEBUG_GNRL, "%s: Powering down PHY(s).\n", dev->name);
	value = MII_GC_PDOWN | MII_GC_LOOPBK | MII_GC_ISOLATE;
	tlan_mii_sync(dev->base_addr);
	tlan_mii_write_reg(dev, priv->phy[priv->phy_num], MII_GEN_CTL, value);
	if ((priv->phy_num == 0) && (priv->phy[1] != TLAN_PHY_NONE)) {
		/* if using internal PHY, the external PHY must be powered on */
		if (priv->adapter->flags & TLAN_ADAPTER_USE_INTERN_10)
			value = MII_GC_ISOLATE; /* just isolate it from MII */
		tlan_mii_sync(dev->base_addr);
		tlan_mii_write_reg(dev, priv->phy[1], MII_GEN_CTL, value);
	}

	/* Wait for 50 ms and powerup
	 * This is abitrary.  It is intended to make sure the
	 * transceiver settles.
	 */
	tlan_set_timer(dev, msecs_to_jiffies(50), TLAN_TIMER_PHY_PUP);

}




static void tlan_phy_power_up(struct net_device *dev)
{
	struct tlan_priv	*priv = netdev_priv(dev);
	u16		value;

	TLAN_DBG(TLAN_DEBUG_GNRL, "%s: Powering up PHY.\n", dev->name);
	tlan_mii_sync(dev->base_addr);
	value = MII_GC_LOOPBK;
	tlan_mii_write_reg(dev, priv->phy[priv->phy_num], MII_GEN_CTL, value);
	tlan_mii_sync(dev->base_addr);
	/* Wait for 500 ms and reset the
	 * transceiver.  The TLAN docs say both 50 ms and
	 * 500 ms, so do the longer, just in case.
	 */
	tlan_set_timer(dev, msecs_to_jiffies(500), TLAN_TIMER_PHY_RESET);

}




static void tlan_phy_reset(struct net_device *dev)
{
	struct tlan_priv	*priv = netdev_priv(dev);
	u16		phy;
	u16		value;
	unsigned long timeout = jiffies + HZ;

	phy = priv->phy[priv->phy_num];

	TLAN_DBG(TLAN_DEBUG_GNRL, "%s: Resetting PHY.\n", dev->name);
	tlan_mii_sync(dev->base_addr);
	value = MII_GC_LOOPBK | MII_GC_RESET;
	tlan_mii_write_reg(dev, phy, MII_GEN_CTL, value);
	do {
		tlan_mii_read_reg(dev, phy, MII_GEN_CTL, &value);
		if (time_after(jiffies, timeout)) {
			netdev_err(dev, "PHY reset timeout\n");
			return;
		}
	} while (value & MII_GC_RESET);

	/* Wait for 500 ms and initialize.
	 * I don't remember why I wait this long.
	 * I've changed this to 50ms, as it seems long enough.
	 */
	tlan_set_timer(dev, msecs_to_jiffies(50), TLAN_TIMER_PHY_START_LINK);

}




static void tlan_phy_start_link(struct net_device *dev)
{
	struct tlan_priv	*priv = netdev_priv(dev);
	u16		ability;
	u16		control;
	u16		data;
	u16		phy;
	u16		status;
	u16		tctl;

	phy = priv->phy[priv->phy_num];
	TLAN_DBG(TLAN_DEBUG_GNRL, "%s: Trying to activate link.\n", dev->name);
	tlan_mii_read_reg(dev, phy, MII_GEN_STS, &status);
	tlan_mii_read_reg(dev, phy, MII_GEN_STS, &ability);

	if ((status & MII_GS_AUTONEG) &&
	    (!priv->aui)) {
		ability = status >> 11;
		if (priv->speed  == TLAN_SPEED_10 &&
		    priv->duplex == TLAN_DUPLEX_HALF) {
			tlan_mii_write_reg(dev, phy, MII_GEN_CTL, 0x0000);
		} else if (priv->speed == TLAN_SPEED_10 &&
			   priv->duplex == TLAN_DUPLEX_FULL) {
			priv->tlan_full_duplex = true;
			tlan_mii_write_reg(dev, phy, MII_GEN_CTL, 0x0100);
		} else if (priv->speed == TLAN_SPEED_100 &&
			   priv->duplex == TLAN_DUPLEX_HALF) {
			tlan_mii_write_reg(dev, phy, MII_GEN_CTL, 0x2000);
		} else if (priv->speed == TLAN_SPEED_100 &&
			   priv->duplex == TLAN_DUPLEX_FULL) {
			priv->tlan_full_duplex = true;
			tlan_mii_write_reg(dev, phy, MII_GEN_CTL, 0x2100);
		} else {

			/* Set Auto-Neg advertisement */
			tlan_mii_write_reg(dev, phy, MII_AN_ADV,
					   (ability << 5) | 1);
			/* Enablee Auto-Neg */
			tlan_mii_write_reg(dev, phy, MII_GEN_CTL, 0x1000);
			/* Restart Auto-Neg */
			tlan_mii_write_reg(dev, phy, MII_GEN_CTL, 0x1200);
			/* Wait for 4 sec for autonegotiation
			 * to complete.  The max spec time is less than this
			 * but the card need additional time to start AN.
			 * .5 sec should be plenty extra.
			 */
			netdev_info(dev, "Starting autonegotiation\n");
			tlan_set_timer(dev, (2*HZ), TLAN_TIMER_PHY_FINISH_AN);
			return;
		}

	}

	if ((priv->aui) && (priv->phy_num != 0)) {
		priv->phy_num = 0;
		data = TLAN_NET_CFG_1FRAG | TLAN_NET_CFG_1CHAN
			| TLAN_NET_CFG_PHY_EN;
		tlan_dio_write16(dev->base_addr, TLAN_NET_CONFIG, data);
		tlan_set_timer(dev, msecs_to_jiffies(40), TLAN_TIMER_PHY_PDOWN);
		return;
	} else if (priv->phy_num == 0) {
		control = 0;
		tlan_mii_read_reg(dev, phy, TLAN_TLPHY_CTL, &tctl);
		if (priv->aui) {
			tctl |= TLAN_TC_AUISEL;
		} else {
			tctl &= ~TLAN_TC_AUISEL;
			if (priv->duplex == TLAN_DUPLEX_FULL) {
				control |= MII_GC_DUPLEX;
				priv->tlan_full_duplex = true;
			}
			if (priv->speed == TLAN_SPEED_100)
				control |= MII_GC_SPEEDSEL;
		}
		tlan_mii_write_reg(dev, phy, MII_GEN_CTL, control);
		tlan_mii_write_reg(dev, phy, TLAN_TLPHY_CTL, tctl);
	}

	/* Wait for 2 sec to give the transceiver time
	 * to establish link.
	 */
	tlan_set_timer(dev, (4*HZ), TLAN_TIMER_FINISH_RESET);

}




static void tlan_phy_finish_auto_neg(struct net_device *dev)
{
	struct tlan_priv	*priv = netdev_priv(dev);
	u16		an_adv;
	u16		an_lpa;
	u16		mode;
	u16		phy;
	u16		status;

	phy = priv->phy[priv->phy_num];

	tlan_mii_read_reg(dev, phy, MII_GEN_STS, &status);
	udelay(1000);
	tlan_mii_read_reg(dev, phy, MII_GEN_STS, &status);

	if (!(status & MII_GS_AUTOCMPLT)) {
		/* Wait for 8 sec to give the process
		 * more time.  Perhaps we should fail after a while.
		 */
		tlan_set_timer(dev, 2 * HZ, TLAN_TIMER_PHY_FINISH_AN);
		return;
	}

	netdev_info(dev, "Autonegotiation complete\n");
	tlan_mii_read_reg(dev, phy, MII_AN_ADV, &an_adv);
	tlan_mii_read_reg(dev, phy, MII_AN_LPA, &an_lpa);
	mode = an_adv & an_lpa & 0x03E0;
	if (mode & 0x0100)
		priv->tlan_full_duplex = true;
	else if (!(mode & 0x0080) && (mode & 0x0040))
		priv->tlan_full_duplex = true;

	/* switch to internal PHY for 10 Mbps */
	if ((!(mode & 0x0180)) &&
	    (priv->adapter->flags & TLAN_ADAPTER_USE_INTERN_10) &&
	    (priv->phy_num != 0)) {
		priv->phy_num = 0;
		tlan_set_timer(dev, msecs_to_jiffies(400), TLAN_TIMER_PHY_PDOWN);
		return;
	}

	if (priv->phy_num == 0) {
		if ((priv->duplex == TLAN_DUPLEX_FULL) ||
		    (an_adv & an_lpa & 0x0040)) {
			tlan_mii_write_reg(dev, phy, MII_GEN_CTL,
					   MII_GC_AUTOENB | MII_GC_DUPLEX);
			netdev_info(dev, "Starting internal PHY with FULL-DUPLEX\n");
		} else {
			tlan_mii_write_reg(dev, phy, MII_GEN_CTL,
					   MII_GC_AUTOENB);
			netdev_info(dev, "Starting internal PHY with HALF-DUPLEX\n");
		}
	}

	/* Wait for 100 ms.  No reason in partiticular.
	 */
	tlan_set_timer(dev, msecs_to_jiffies(100), TLAN_TIMER_FINISH_RESET);

}


/*********************************************************************
 *
 *     tlan_phy_monitor
 *
 *     Returns:
 *	      None
 *
 *     Params:
 *	      data	     The device structure of this device.
 *
 *
 *     This function monitors PHY condition by reading the status
 *     register via the MII bus, controls LINK LED and notifies the
 *     kernel about link state.
 *
 *******************************************************************/

static void tlan_phy_monitor(struct timer_list *t)
{
	struct tlan_priv *priv = from_timer(priv, t, media_timer);
	struct net_device *dev = priv->dev;
	u16     phy;
	u16     phy_status;

	phy = priv->phy[priv->phy_num];

	/* Get PHY status register */
	tlan_mii_read_reg(dev, phy, MII_GEN_STS, &phy_status);

	/* Check if link has been lost */
	if (!(phy_status & MII_GS_LINK)) {
		if (netif_carrier_ok(dev)) {
			printk(KERN_DEBUG "TLAN: %s has lost link\n",
			       dev->name);
			tlan_dio_write8(dev->base_addr, TLAN_LED_REG, 0);
			netif_carrier_off(dev);
			if (priv->adapter->flags & TLAN_ADAPTER_USE_INTERN_10) {
				/* power down internal PHY */
				u16 data = MII_GC_PDOWN | MII_GC_LOOPBK |
					   MII_GC_ISOLATE;

				tlan_mii_sync(dev->base_addr);
				tlan_mii_write_reg(dev, priv->phy[0],
						   MII_GEN_CTL, data);
				/* set to external PHY */
				priv->phy_num = 1;
				/* restart autonegotiation */
				tlan_set_timer(dev, msecs_to_jiffies(400),
					       TLAN_TIMER_PHY_PDOWN);
				return;
			}
		}
	}

	/* Link restablished? */
	if ((phy_status & MII_GS_LINK) && !netif_carrier_ok(dev)) {
		tlan_dio_write8(dev->base_addr, TLAN_LED_REG, TLAN_LED_LINK);
		printk(KERN_DEBUG "TLAN: %s has reestablished link\n",
		       dev->name);
		netif_carrier_on(dev);
	}
	priv->media_timer.expires = jiffies + HZ;
	add_timer(&priv->media_timer);
}


/*****************************************************************************
******************************************************************************

ThunderLAN driver MII routines

these routines are based on the information in chap. 2 of the
"ThunderLAN Programmer's Guide", pp. 15-24.

******************************************************************************
*****************************************************************************/


/***************************************************************
 *	tlan_mii_read_reg
 *
 *	Returns:
 *		false	if ack received ok
 *		true	if no ack received or other error
 *
 *	Parms:
 *		dev		The device structure containing
 *				The io address and interrupt count
 *				for this device.
 *		phy		The address of the PHY to be queried.
 *		reg		The register whose contents are to be
 *				retrieved.
 *		val		A pointer to a variable to store the
 *				retrieved value.
 *
 *	This function uses the TLAN's MII bus to retrieve the contents
 *	of a given register on a PHY.  It sends the appropriate info
 *	and then reads the 16-bit register value from the MII bus via
 *	the TLAN SIO register.
 *
 **************************************************************/

static bool
tlan_mii_read_reg(struct net_device *dev, u16 phy, u16 reg, u16 *val)
{
	u8	nack;
	u16	sio, tmp;
	u32	i;
	bool	err;
	int	minten;
	struct tlan_priv *priv = netdev_priv(dev);
	unsigned long flags = 0;

	err = false;
	outw(TLAN_NET_SIO, dev->base_addr + TLAN_DIO_ADR);
	sio = dev->base_addr + TLAN_DIO_DATA + TLAN_NET_SIO;

	if (!in_irq())
		spin_lock_irqsave(&priv->lock, flags);

	tlan_mii_sync(dev->base_addr);

	minten = tlan_get_bit(TLAN_NET_SIO_MINTEN, sio);
	if (minten)
		tlan_clear_bit(TLAN_NET_SIO_MINTEN, sio);

	tlan_mii_send_data(dev->base_addr, 0x1, 2);	/* start (01b) */
	tlan_mii_send_data(dev->base_addr, 0x2, 2);	/* read  (10b) */
	tlan_mii_send_data(dev->base_addr, phy, 5);	/* device #      */
	tlan_mii_send_data(dev->base_addr, reg, 5);	/* register #    */


	tlan_clear_bit(TLAN_NET_SIO_MTXEN, sio);	/* change direction */

	tlan_clear_bit(TLAN_NET_SIO_MCLK, sio);		/* clock idle bit */
	tlan_set_bit(TLAN_NET_SIO_MCLK, sio);
	tlan_clear_bit(TLAN_NET_SIO_MCLK, sio);		/* wait 300ns */

	nack = tlan_get_bit(TLAN_NET_SIO_MDATA, sio);	/* check for ACK */
	tlan_set_bit(TLAN_NET_SIO_MCLK, sio);		/* finish ACK */
	if (nack) {					/* no ACK, so fake it */
		for (i = 0; i < 16; i++) {
			tlan_clear_bit(TLAN_NET_SIO_MCLK, sio);
			tlan_set_bit(TLAN_NET_SIO_MCLK, sio);
		}
		tmp = 0xffff;
		err = true;
	} else {					/* ACK, so read data */
		for (tmp = 0, i = 0x8000; i; i >>= 1) {
			tlan_clear_bit(TLAN_NET_SIO_MCLK, sio);
			if (tlan_get_bit(TLAN_NET_SIO_MDATA, sio))
				tmp |= i;
			tlan_set_bit(TLAN_NET_SIO_MCLK, sio);
		}
	}


	tlan_clear_bit(TLAN_NET_SIO_MCLK, sio);		/* idle cycle */
	tlan_set_bit(TLAN_NET_SIO_MCLK, sio);

	if (minten)
		tlan_set_bit(TLAN_NET_SIO_MINTEN, sio);

	*val = tmp;

	if (!in_irq())
		spin_unlock_irqrestore(&priv->lock, flags);

	return err;

}




/***************************************************************
 *	tlan_mii_send_data
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		base_port	The base IO port of the adapter	in
 *				question.
 *		dev		The address of the PHY to be queried.
 *		data		The value to be placed on the MII bus.
 *		num_bits	The number of bits in data that are to
 *				be placed on the MII bus.
 *
 *	This function sends on sequence of bits on the MII
 *	configuration bus.
 *
 **************************************************************/

static void tlan_mii_send_data(u16 base_port, u32 data, unsigned num_bits)
{
	u16 sio;
	u32 i;

	if (num_bits == 0)
		return;

	outw(TLAN_NET_SIO, base_port + TLAN_DIO_ADR);
	sio = base_port + TLAN_DIO_DATA + TLAN_NET_SIO;
	tlan_set_bit(TLAN_NET_SIO_MTXEN, sio);

	for (i = (0x1 << (num_bits - 1)); i; i >>= 1) {
		tlan_clear_bit(TLAN_NET_SIO_MCLK, sio);
		(void) tlan_get_bit(TLAN_NET_SIO_MCLK, sio);
		if (data & i)
			tlan_set_bit(TLAN_NET_SIO_MDATA, sio);
		else
			tlan_clear_bit(TLAN_NET_SIO_MDATA, sio);
		tlan_set_bit(TLAN_NET_SIO_MCLK, sio);
		(void) tlan_get_bit(TLAN_NET_SIO_MCLK, sio);
	}

}




/***************************************************************
 *	TLan_MiiSync
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		base_port	The base IO port of the adapter in
 *				question.
 *
 *	This functions syncs all PHYs in terms of the MII configuration
 *	bus.
 *
 **************************************************************/

static void tlan_mii_sync(u16 base_port)
{
	int i;
	u16 sio;

	outw(TLAN_NET_SIO, base_port + TLAN_DIO_ADR);
	sio = base_port + TLAN_DIO_DATA + TLAN_NET_SIO;

	tlan_clear_bit(TLAN_NET_SIO_MTXEN, sio);
	for (i = 0; i < 32; i++) {
		tlan_clear_bit(TLAN_NET_SIO_MCLK, sio);
		tlan_set_bit(TLAN_NET_SIO_MCLK, sio);
	}

}




/***************************************************************
 *	tlan_mii_write_reg
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		dev		The device structure for the device
 *				to write to.
 *		phy		The address of the PHY to be written to.
 *		reg		The register whose contents are to be
 *				written.
 *		val		The value to be written to the register.
 *
 *	This function uses the TLAN's MII bus to write the contents of a
 *	given register on a PHY.  It sends the appropriate info and then
 *	writes the 16-bit register value from the MII configuration bus
 *	via the TLAN SIO register.
 *
 **************************************************************/

static void
tlan_mii_write_reg(struct net_device *dev, u16 phy, u16 reg, u16 val)
{
	u16	sio;
	int	minten;
	unsigned long flags = 0;
	struct tlan_priv *priv = netdev_priv(dev);

	outw(TLAN_NET_SIO, dev->base_addr + TLAN_DIO_ADR);
	sio = dev->base_addr + TLAN_DIO_DATA + TLAN_NET_SIO;

	if (!in_irq())
		spin_lock_irqsave(&priv->lock, flags);

	tlan_mii_sync(dev->base_addr);

	minten = tlan_get_bit(TLAN_NET_SIO_MINTEN, sio);
	if (minten)
		tlan_clear_bit(TLAN_NET_SIO_MINTEN, sio);

	tlan_mii_send_data(dev->base_addr, 0x1, 2);	/* start (01b) */
	tlan_mii_send_data(dev->base_addr, 0x1, 2);	/* write (01b) */
	tlan_mii_send_data(dev->base_addr, phy, 5);	/* device #      */
	tlan_mii_send_data(dev->base_addr, reg, 5);	/* register #    */

	tlan_mii_send_data(dev->base_addr, 0x2, 2);	/* send ACK */
	tlan_mii_send_data(dev->base_addr, val, 16);	/* send data */

	tlan_clear_bit(TLAN_NET_SIO_MCLK, sio);	/* idle cycle */
	tlan_set_bit(TLAN_NET_SIO_MCLK, sio);

	if (minten)
		tlan_set_bit(TLAN_NET_SIO_MINTEN, sio);

	if (!in_irq())
		spin_unlock_irqrestore(&priv->lock, flags);

}




/*****************************************************************************
******************************************************************************

ThunderLAN driver eeprom routines

the Compaq netelligent 10 and 10/100 cards use a microchip 24C02A
EEPROM.  these functions are based on information in microchip's
data sheet.  I don't know how well this functions will work with
other Eeproms.

******************************************************************************
*****************************************************************************/


/***************************************************************
 *	tlan_ee_send_start
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		io_base		The IO port base address for the
 *				TLAN device with the EEPROM to
 *				use.
 *
 *	This function sends a start cycle to an EEPROM attached
 *	to a TLAN chip.
 *
 **************************************************************/

static void tlan_ee_send_start(u16 io_base)
{
	u16	sio;

	outw(TLAN_NET_SIO, io_base + TLAN_DIO_ADR);
	sio = io_base + TLAN_DIO_DATA + TLAN_NET_SIO;

	tlan_set_bit(TLAN_NET_SIO_ECLOK, sio);
	tlan_set_bit(TLAN_NET_SIO_EDATA, sio);
	tlan_set_bit(TLAN_NET_SIO_ETXEN, sio);
	tlan_clear_bit(TLAN_NET_SIO_EDATA, sio);
	tlan_clear_bit(TLAN_NET_SIO_ECLOK, sio);

}




/***************************************************************
 *	tlan_ee_send_byte
 *
 *	Returns:
 *		If the correct ack was received, 0, otherwise 1
 *	Parms:	io_base		The IO port base address for the
 *				TLAN device with the EEPROM to
 *				use.
 *		data		The 8 bits of information to
 *				send to the EEPROM.
 *		stop		If TLAN_EEPROM_STOP is passed, a
 *				stop cycle is sent after the
 *				byte is sent after the ack is
 *				read.
 *
 *	This function sends a byte on the serial EEPROM line,
 *	driving the clock to send each bit. The function then
 *	reverses transmission direction and reads an acknowledge
 *	bit.
 *
 **************************************************************/

static int tlan_ee_send_byte(u16 io_base, u8 data, int stop)
{
	int	err;
	u8	place;
	u16	sio;

	outw(TLAN_NET_SIO, io_base + TLAN_DIO_ADR);
	sio = io_base + TLAN_DIO_DATA + TLAN_NET_SIO;

	/* Assume clock is low, tx is enabled; */
	for (place = 0x80; place != 0; place >>= 1) {
		if (place & data)
			tlan_set_bit(TLAN_NET_SIO_EDATA, sio);
		else
			tlan_clear_bit(TLAN_NET_SIO_EDATA, sio);
		tlan_set_bit(TLAN_NET_SIO_ECLOK, sio);
		tlan_clear_bit(TLAN_NET_SIO_ECLOK, sio);
	}
	tlan_clear_bit(TLAN_NET_SIO_ETXEN, sio);
	tlan_set_bit(TLAN_NET_SIO_ECLOK, sio);
	err = tlan_get_bit(TLAN_NET_SIO_EDATA, sio);
	tlan_clear_bit(TLAN_NET_SIO_ECLOK, sio);
	tlan_set_bit(TLAN_NET_SIO_ETXEN, sio);

	if ((!err) && stop) {
		/* STOP, raise data while clock is high */
		tlan_clear_bit(TLAN_NET_SIO_EDATA, sio);
		tlan_set_bit(TLAN_NET_SIO_ECLOK, sio);
		tlan_set_bit(TLAN_NET_SIO_EDATA, sio);
	}

	return err;

}




/***************************************************************
 *	tlan_ee_receive_byte
 *
 *	Returns:
 *		Nothing
 *	Parms:
 *		io_base		The IO port base address for the
 *				TLAN device with the EEPROM to
 *				use.
 *		data		An address to a char to hold the
 *				data sent from the EEPROM.
 *		stop		If TLAN_EEPROM_STOP is passed, a
 *				stop cycle is sent after the
 *				byte is received, and no ack is
 *				sent.
 *
 *	This function receives 8 bits of data from the EEPROM
 *	over the serial link.  It then sends and ack bit, or no
 *	ack and a stop bit.  This function is used to retrieve
 *	data after the address of a byte in the EEPROM has been
 *	sent.
 *
 **************************************************************/

static void tlan_ee_receive_byte(u16 io_base, u8 *data, int stop)
{
	u8  place;
	u16 sio;

	outw(TLAN_NET_SIO, io_base + TLAN_DIO_ADR);
	sio = io_base + TLAN_DIO_DATA + TLAN_NET_SIO;
	*data = 0;

	/* Assume clock is low, tx is enabled; */
	tlan_clear_bit(TLAN_NET_SIO_ETXEN, sio);
	for (place = 0x80; place; place >>= 1) {
		tlan_set_bit(TLAN_NET_SIO_ECLOK, sio);
		if (tlan_get_bit(TLAN_NET_SIO_EDATA, sio))
			*data |= place;
		tlan_clear_bit(TLAN_NET_SIO_ECLOK, sio);
	}

	tlan_set_bit(TLAN_NET_SIO_ETXEN, sio);
	if (!stop) {
		tlan_clear_bit(TLAN_NET_SIO_EDATA, sio); /* ack = 0 */
		tlan_set_bit(TLAN_NET_SIO_ECLOK, sio);
		tlan_clear_bit(TLAN_NET_SIO_ECLOK, sio);
	} else {
		tlan_set_bit(TLAN_NET_SIO_EDATA, sio);	/* no ack = 1 (?) */
		tlan_set_bit(TLAN_NET_SIO_ECLOK, sio);
		tlan_clear_bit(TLAN_NET_SIO_ECLOK, sio);
		/* STOP, raise data while clock is high */
		tlan_clear_bit(TLAN_NET_SIO_EDATA, sio);
		tlan_set_bit(TLAN_NET_SIO_ECLOK, sio);
		tlan_set_bit(TLAN_NET_SIO_EDATA, sio);
	}

}




/***************************************************************
 *	tlan_ee_read_byte
 *
 *	Returns:
 *		No error = 0, else, the stage at which the error
 *		occurred.
 *	Parms:
 *		io_base		The IO port base address for the
 *				TLAN device with the EEPROM to
 *				use.
 *		ee_addr		The address of the byte in the
 *				EEPROM whose contents are to be
 *				retrieved.
 *		data		An address to a char to hold the
 *				data obtained from the EEPROM.
 *
 *	This function reads a byte of information from an byte
 *	cell in the EEPROM.
 *
 **************************************************************/

static int tlan_ee_read_byte(struct net_device *dev, u8 ee_addr, u8 *data)
{
	int err;
	struct tlan_priv *priv = netdev_priv(dev);
	unsigned long flags = 0;
	int ret = 0;

	spin_lock_irqsave(&priv->lock, flags);

	tlan_ee_send_start(dev->base_addr);
	err = tlan_ee_send_byte(dev->base_addr, 0xa0, TLAN_EEPROM_ACK);
	if (err) {
		ret = 1;
		goto fail;
	}
	err = tlan_ee_send_byte(dev->base_addr, ee_addr, TLAN_EEPROM_ACK);
	if (err) {
		ret = 2;
		goto fail;
	}
	tlan_ee_send_start(dev->base_addr);
	err = tlan_ee_send_byte(dev->base_addr, 0xa1, TLAN_EEPROM_ACK);
	if (err) {
		ret = 3;
		goto fail;
	}
	tlan_ee_receive_byte(dev->base_addr, data, TLAN_EEPROM_STOP);
fail:
	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;

}



