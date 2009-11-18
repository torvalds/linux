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
 * Change History
 *
 *	Tigran Aivazian <tigran@sco.com>:	TLan_PciProbe() now uses
 *						new PCI BIOS interface.
 *	Alan Cox	<alan@lxorguk.ukuu.org.uk>:
 *						Fixed the out of memory
 *						handling.
 *
 *	Torben Mathiasen <torben.mathiasen@compaq.com> New Maintainer!
 *
 *	v1.1 Dec 20, 1999    - Removed linux version checking
 *			       Patch from Tigran Aivazian.
 *			     - v1.1 includes Alan's SMP updates.
 *			     - We still have problems on SMP though,
 *			       but I'm looking into that.
 *
 *	v1.2 Jan 02, 2000    - Hopefully fixed the SMP deadlock.
 *			     - Removed dependency of HZ being 100.
 *			     - We now allow higher priority timers to
 *			       overwrite timers like TLAN_TIMER_ACTIVITY
 *			       Patch from John Cagle <john.cagle@compaq.com>.
 *			     - Fixed a few compiler warnings.
 *
 *	v1.3 Feb 04, 2000    - Fixed the remaining HZ issues.
 *			     - Removed call to pci_present().
 *			     - Removed SA_INTERRUPT flag from irq handler.
 *			     - Added __init and __initdata to reduce resisdent
 *			       code size.
 *			     - Driver now uses module_init/module_exit.
 *			     - Rewrote init_module and tlan_probe to
 *			       share a lot more code. We now use tlan_probe
 *			       with builtin and module driver.
 *			     - Driver ported to new net API.
 *			     - tlan.txt has been reworked to reflect current
 *			       driver (almost)
 *			     - Other minor stuff
 *
 *	v1.4 Feb 10, 2000    - Updated with more changes required after Dave's
 *	                       network cleanup in 2.3.43pre7 (Tigran & myself)
 *	                     - Minor stuff.
 *
 *	v1.5 March 22, 2000  - Fixed another timer bug that would hang the driver
 *			       if no cable/link were present.
 *			     - Cosmetic changes.
 *			     - TODO: Port completely to new PCI/DMA API
 *			     	     Auto-Neg fallback.
 *
 * 	v1.6 April 04, 2000  - Fixed driver support for kernel-parameters. Haven't
 * 			       tested it though, as the kernel support is currently
 * 			       broken (2.3.99p4p3).
 * 			     - Updated tlan.txt accordingly.
 * 			     - Adjusted minimum/maximum frame length.
 * 			     - There is now a TLAN website up at
 * 			       http://tlan.kernel.dk
 *
 * 	v1.7 April 07, 2000  - Started to implement custom ioctls. Driver now
 * 			       reports PHY information when used with Donald
 * 			       Beckers userspace MII diagnostics utility.
 *
 * 	v1.8 April 23, 2000  - Fixed support for forced speed/duplex settings.
 * 			     - Added link information to Auto-Neg and forced
 * 			       modes. When NIC operates with auto-neg the driver
 * 			       will report Link speed & duplex modes as well as
 * 			       link partner abilities. When forced link is used,
 * 			       the driver will report status of the established
 * 			       link.
 * 			       Please read tlan.txt for additional information.
 * 			     - Removed call to check_region(), and used
 * 			       return value of request_region() instead.
 *
 *	v1.8a May 28, 2000   - Minor updates.
 *
 *	v1.9 July 25, 2000   - Fixed a few remaining Full-Duplex issues.
 *	                     - Updated with timer fixes from Andrew Morton.
 *	                     - Fixed module race in TLan_Open.
 *	                     - Added routine to monitor PHY status.
 *	                     - Added activity led support for Proliant devices.
 *
 *	v1.10 Aug 30, 2000   - Added support for EISA based tlan controllers
 *			       like the Compaq NetFlex3/E.
 *			     - Rewrote tlan_probe to better handle multiple
 *			       bus probes. Probing and device setup is now
 *			       done through TLan_Probe and TLan_init_one. Actual
 *			       hardware probe is done with kernel API and
 *			       TLan_EisaProbe.
 *			     - Adjusted debug information for probing.
 *			     - Fixed bug that would cause general debug information
 *			       to be printed after driver removal.
 *			     - Added transmit timeout handling.
 *			     - Fixed OOM return values in tlan_probe.
 *			     - Fixed possible mem leak in tlan_exit
 *			       (now tlan_remove_one).
 *			     - Fixed timer bug in TLan_phyMonitor.
 *			     - This driver version is alpha quality, please
 *			       send me any bug issues you may encounter.
 *
 *	v1.11 Aug 31, 2000   - Do not try to register irq 0 if no irq line was
 *			       set for EISA cards.
 *			     - Added support for NetFlex3/E with nibble-rate
 *			       10Base-T PHY. This is untestet as I haven't got
 *			       one of these cards.
 *			     - Fixed timer being added twice.
 *			     - Disabled PhyMonitoring by default as this is
 *			       work in progress. Define MONITOR to enable it.
 *			     - Now we don't display link info with PHYs that
 *			       doesn't support it (level1).
 *			     - Incresed tx_timeout beacuse of auto-neg.
 *			     - Adjusted timers for forced speeds.
 *
 *	v1.12 Oct 12, 2000   - Minor fixes (memleak, init, etc.)
 *
 * 	v1.13 Nov 28, 2000   - Stop flooding console with auto-neg issues
 * 			       when link can't be established.
 *			     - Added the bbuf option as a kernel parameter.
 *			     - Fixed ioaddr probe bug.
 *			     - Fixed stupid deadlock with MII interrupts.
 *			     - Added support for speed/duplex selection with
 *			       multiple nics.
 *			     - Added partly fix for TX Channel lockup with
 *			       TLAN v1.0 silicon. This needs to be investigated
 *			       further.
 *
 * 	v1.14 Dec 16, 2000   - Added support for servicing multiple frames per.
 * 			       interrupt. Thanks goes to
 * 			       Adam Keys <adam@ti.com>
 * 			       Denis Beaudoin <dbeaudoin@ti.com>
 * 			       for providing the patch.
 * 			     - Fixed auto-neg output when using multiple
 * 			       adapters.
 * 			     - Converted to use new taskq interface.
 *
 * 	v1.14a Jan 6, 2001   - Minor adjustments (spinlocks, etc.)
 *
 *	Samuel Chessman <chessman@tux.org> New Maintainer!
 *
 *	v1.15 Apr 4, 2002    - Correct operation when aui=1 to be
 *	                       10T half duplex no loopback
 *	                       Thanks to Gunnar Eikman
 *
 *	Sakari Ailus <sakari.ailus@iki.fi>:
 *
 *	v1.15a Dec 15 2008   - Remove bbuf support, it doesn't work anyway.
 *
 *******************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
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

typedef u32 (TLanIntVectorFunc)( struct net_device *, u16 );


/* For removing EISA devices */
static	struct net_device	*TLan_Eisa_Devices;

static	int		TLanDevicesInstalled;

/* Set speed, duplex and aui settings */
static  int aui[MAX_TLAN_BOARDS];
static  int duplex[MAX_TLAN_BOARDS];
static  int speed[MAX_TLAN_BOARDS];
static  int boards_found;
module_param_array(aui, int, NULL, 0);
module_param_array(duplex, int, NULL, 0);
module_param_array(speed, int, NULL, 0);
MODULE_PARM_DESC(aui, "ThunderLAN use AUI port(s) (0-1)");
MODULE_PARM_DESC(duplex, "ThunderLAN duplex setting(s) (0-default, 1-half, 2-full)");
MODULE_PARM_DESC(speed, "ThunderLAN port speen setting(s) (0,10,100)");

MODULE_AUTHOR("Maintainer: Samuel Chessman <chessman@tux.org>");
MODULE_DESCRIPTION("Driver for TI ThunderLAN based ethernet PCI adapters");
MODULE_LICENSE("GPL");


/* Define this to enable Link beat monitoring */
#undef MONITOR

/* Turn on debugging. See Documentation/networking/tlan.txt for details */
static  int		debug;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "ThunderLAN debug mask");

static	const char TLanSignature[] = "TLAN";
static  const char tlan_banner[] = "ThunderLAN driver v1.15a\n";
static  int tlan_have_pci;
static  int tlan_have_eisa;

static const char *media[] = {
	"10BaseT-HD ", "10BaseT-FD ","100baseTx-HD ",
	"100baseTx-FD", "100baseT4", NULL
};

static struct board {
	const char	*deviceLabel;
	u32	   	flags;
	u16	   	addrOfs;
} board_info[] = {
	{ "Compaq Netelligent 10 T PCI UTP", TLAN_ADAPTER_ACTIVITY_LED, 0x83 },
	{ "Compaq Netelligent 10/100 TX PCI UTP", TLAN_ADAPTER_ACTIVITY_LED, 0x83 },
	{ "Compaq Integrated NetFlex-3/P", TLAN_ADAPTER_NONE, 0x83 },
	{ "Compaq NetFlex-3/P",
	  TLAN_ADAPTER_UNMANAGED_PHY | TLAN_ADAPTER_BIT_RATE_PHY, 0x83 },
	{ "Compaq NetFlex-3/P", TLAN_ADAPTER_NONE, 0x83 },
	{ "Compaq Netelligent Integrated 10/100 TX UTP",
	  TLAN_ADAPTER_ACTIVITY_LED, 0x83 },
	{ "Compaq Netelligent Dual 10/100 TX PCI UTP", TLAN_ADAPTER_NONE, 0x83 },
	{ "Compaq Netelligent 10/100 TX Embedded UTP", TLAN_ADAPTER_NONE, 0x83 },
	{ "Olicom OC-2183/2185", TLAN_ADAPTER_USE_INTERN_10, 0x83 },
	{ "Olicom OC-2325", TLAN_ADAPTER_UNMANAGED_PHY, 0xF8 },
	{ "Olicom OC-2326", TLAN_ADAPTER_USE_INTERN_10, 0xF8 },
	{ "Compaq Netelligent 10/100 TX UTP", TLAN_ADAPTER_ACTIVITY_LED, 0x83 },
	{ "Compaq Netelligent 10 T/2 PCI UTP/Coax", TLAN_ADAPTER_NONE, 0x83 },
	{ "Compaq NetFlex-3/E",
	  TLAN_ADAPTER_ACTIVITY_LED | 	/* EISA card */
	  TLAN_ADAPTER_UNMANAGED_PHY | TLAN_ADAPTER_BIT_RATE_PHY, 0x83 },
	{ "Compaq NetFlex-3/E", TLAN_ADAPTER_ACTIVITY_LED, 0x83 }, /* EISA card */
};

static struct pci_device_id tlan_pci_tbl[] = {
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

static void	TLan_EisaProbe( void );
static void	TLan_Eisa_Cleanup( void );
static int      TLan_Init( struct net_device * );
static int	TLan_Open( struct net_device *dev );
static netdev_tx_t TLan_StartTx( struct sk_buff *, struct net_device *);
static irqreturn_t TLan_HandleInterrupt( int, void *);
static int	TLan_Close( struct net_device *);
static struct	net_device_stats *TLan_GetStats( struct net_device *);
static void	TLan_SetMulticastList( struct net_device *);
static int	TLan_ioctl( struct net_device *dev, struct ifreq *rq, int cmd);
static int      TLan_probe1( struct pci_dev *pdev, long ioaddr,
			     int irq, int rev, const struct pci_device_id *ent);
static void	TLan_tx_timeout( struct net_device *dev);
static void	TLan_tx_timeout_work(struct work_struct *work);
static int 	tlan_init_one( struct pci_dev *pdev, const struct pci_device_id *ent);

static u32	TLan_HandleTxEOF( struct net_device *, u16 );
static u32	TLan_HandleStatOverflow( struct net_device *, u16 );
static u32	TLan_HandleRxEOF( struct net_device *, u16 );
static u32	TLan_HandleDummy( struct net_device *, u16 );
static u32	TLan_HandleTxEOC( struct net_device *, u16 );
static u32	TLan_HandleStatusCheck( struct net_device *, u16 );
static u32	TLan_HandleRxEOC( struct net_device *, u16 );

static void	TLan_Timer( unsigned long );

static void	TLan_ResetLists( struct net_device * );
static void	TLan_FreeLists( struct net_device * );
static void	TLan_PrintDio( u16 );
static void	TLan_PrintList( TLanList *, char *, int );
static void	TLan_ReadAndClearStats( struct net_device *, int );
static void	TLan_ResetAdapter( struct net_device * );
static void	TLan_FinishReset( struct net_device * );
static void	TLan_SetMac( struct net_device *, int areg, char *mac );

static void	TLan_PhyPrint( struct net_device * );
static void	TLan_PhyDetect( struct net_device * );
static void	TLan_PhyPowerDown( struct net_device * );
static void	TLan_PhyPowerUp( struct net_device * );
static void	TLan_PhyReset( struct net_device * );
static void	TLan_PhyStartLink( struct net_device * );
static void	TLan_PhyFinishAutoNeg( struct net_device * );
#ifdef MONITOR
static void     TLan_PhyMonitor( struct net_device * );
#endif

/*
static int	TLan_PhyNop( struct net_device * );
static int	TLan_PhyInternalCheck( struct net_device * );
static int	TLan_PhyInternalService( struct net_device * );
static int	TLan_PhyDp83840aCheck( struct net_device * );
*/

static int	TLan_MiiReadReg( struct net_device *, u16, u16, u16 * );
static void	TLan_MiiSendData( u16, u32, unsigned );
static void	TLan_MiiSync( u16 );
static void	TLan_MiiWriteReg( struct net_device *, u16, u16, u16 );

static void	TLan_EeSendStart( u16 );
static int	TLan_EeSendByte( u16, u8, int );
static void	TLan_EeReceiveByte( u16, u8 *, int );
static int	TLan_EeReadByte( struct net_device *, u8, u8 * );


static inline void
TLan_StoreSKB( struct tlan_list_tag *tag, struct sk_buff *skb)
{
	unsigned long addr = (unsigned long)skb;
	tag->buffer[9].address = addr;
	tag->buffer[8].address = upper_32_bits(addr);
}

static inline struct sk_buff *
TLan_GetSKB( const struct tlan_list_tag *tag)
{
	unsigned long addr;

	addr = tag->buffer[9].address;
	addr |= (tag->buffer[8].address << 16) << 16;
	return (struct sk_buff *) addr;
}


static TLanIntVectorFunc *TLanIntVector[TLAN_INT_NUMBER_OF_INTS] = {
	NULL,
	TLan_HandleTxEOF,
	TLan_HandleStatOverflow,
	TLan_HandleRxEOF,
	TLan_HandleDummy,
	TLan_HandleTxEOC,
	TLan_HandleStatusCheck,
	TLan_HandleRxEOC
};

static inline void
TLan_SetTimer( struct net_device *dev, u32 ticks, u32 type )
{
	TLanPrivateInfo *priv = netdev_priv(dev);
	unsigned long flags = 0;

	if (!in_irq())
		spin_lock_irqsave(&priv->lock, flags);
	if ( priv->timer.function != NULL &&
		priv->timerType != TLAN_TIMER_ACTIVITY ) {
		if (!in_irq())
			spin_unlock_irqrestore(&priv->lock, flags);
		return;
	}
	priv->timer.function = &TLan_Timer;
	if (!in_irq())
		spin_unlock_irqrestore(&priv->lock, flags);

	priv->timer.data = (unsigned long) dev;
	priv->timerSetAt = jiffies;
	priv->timerType = type;
	mod_timer(&priv->timer, jiffies + ticks);

} /* TLan_SetTimer */


/*****************************************************************************
******************************************************************************

	ThunderLAN Driver Primary Functions

	These functions are more or less common to all Linux network drivers.

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


static void __devexit tlan_remove_one( struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata( pdev );
	TLanPrivateInfo	*priv = netdev_priv(dev);

	unregister_netdev( dev );

	if ( priv->dmaStorage ) {
		pci_free_consistent(priv->pciDev,
				    priv->dmaSize, priv->dmaStorage,
				    priv->dmaStorageDMA );
	}

#ifdef CONFIG_PCI
	pci_release_regions(pdev);
#endif

	free_netdev( dev );

	pci_set_drvdata( pdev, NULL );
}

static struct pci_driver tlan_driver = {
	.name		= "tlan",
	.id_table	= tlan_pci_tbl,
	.probe		= tlan_init_one,
	.remove		= __devexit_p(tlan_remove_one),
};

static int __init tlan_probe(void)
{
	int rc = -ENODEV;

	printk(KERN_INFO "%s", tlan_banner);

	TLAN_DBG(TLAN_DEBUG_PROBE, "Starting PCI Probe....\n");

	/* Use new style PCI probing. Now the kernel will
	   do most of this for us */
	rc = pci_register_driver(&tlan_driver);

	if (rc != 0) {
		printk(KERN_ERR "TLAN: Could not register pci driver.\n");
		goto err_out_pci_free;
	}

	TLAN_DBG(TLAN_DEBUG_PROBE, "Starting EISA Probe....\n");
	TLan_EisaProbe();

	printk(KERN_INFO "TLAN: %d device%s installed, PCI: %d  EISA: %d\n",
		 TLanDevicesInstalled, TLanDevicesInstalled == 1 ? "" : "s",
		 tlan_have_pci, tlan_have_eisa);

	if (TLanDevicesInstalled == 0) {
		rc = -ENODEV;
		goto  err_out_pci_unreg;
	}
	return 0;

err_out_pci_unreg:
	pci_unregister_driver(&tlan_driver);
err_out_pci_free:
	return rc;
}


static int __devinit tlan_init_one( struct pci_dev *pdev,
				    const struct pci_device_id *ent)
{
	return TLan_probe1( pdev, -1, -1, 0, ent);
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

static int __devinit TLan_probe1(struct pci_dev *pdev,
				 long ioaddr, int irq, int rev,
				 const struct pci_device_id *ent )
{

	struct net_device  *dev;
	TLanPrivateInfo    *priv;
	u16		   device_id;
	int		   reg, rc = -ENODEV;

#ifdef CONFIG_PCI
	if (pdev) {
		rc = pci_enable_device(pdev);
		if (rc)
			return rc;

		rc = pci_request_regions(pdev, TLanSignature);
		if (rc) {
			printk(KERN_ERR "TLAN: Could not reserve IO regions\n");
			goto err_out;
		}
	}
#endif  /*  CONFIG_PCI  */

	dev = alloc_etherdev(sizeof(TLanPrivateInfo));
	if (dev == NULL) {
		printk(KERN_ERR "TLAN: Could not allocate memory for device.\n");
		rc = -ENOMEM;
		goto err_out_regions;
	}
	SET_NETDEV_DEV(dev, &pdev->dev);

	priv = netdev_priv(dev);

	priv->pciDev = pdev;
	priv->dev = dev;

	/* Is this a PCI device? */
	if (pdev) {
		u32 		   pci_io_base = 0;

		priv->adapter = &board_info[ent->driver_data];

		rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (rc) {
			printk(KERN_ERR "TLAN: No suitable PCI mapping available.\n");
			goto err_out_free_dev;
		}

		for ( reg= 0; reg <= 5; reg ++ ) {
			if (pci_resource_flags(pdev, reg) & IORESOURCE_IO) {
				pci_io_base = pci_resource_start(pdev, reg);
				TLAN_DBG( TLAN_DEBUG_GNRL, "IO mapping is available at %x.\n",
						pci_io_base);
				break;
			}
		}
		if (!pci_io_base) {
			printk(KERN_ERR "TLAN: No IO mappings available\n");
			rc = -EIO;
			goto err_out_free_dev;
		}

		dev->base_addr = pci_io_base;
		dev->irq = pdev->irq;
		priv->adapterRev = pdev->revision;
		pci_set_master(pdev);
		pci_set_drvdata(pdev, dev);

	} else	{     /* EISA card */
		/* This is a hack. We need to know which board structure
		 * is suited for this adapter */
		device_id = inw(ioaddr + EISA_ID2);
		priv->is_eisa = 1;
		if (device_id == 0x20F1) {
			priv->adapter = &board_info[13]; 	/* NetFlex-3/E */
			priv->adapterRev = 23;			/* TLAN 2.3 */
		} else {
			priv->adapter = &board_info[14];
			priv->adapterRev = 10;			/* TLAN 1.0 */
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

		if (priv->speed == 0x1) {
			priv->speed = TLAN_SPEED_10;
		} else if (priv->speed == 0x2) {
			priv->speed = TLAN_SPEED_100;
		}
		debug = priv->debug = dev->mem_end;
	} else {
		priv->aui    = aui[boards_found];
		priv->speed  = speed[boards_found];
		priv->duplex = duplex[boards_found];
		priv->debug = debug;
	}

	/* This will be used when we get an adapter error from
	 * within our irq handler */
	INIT_WORK(&priv->tlan_tqueue, TLan_tx_timeout_work);

	spin_lock_init(&priv->lock);

	rc = TLan_Init(dev);
	if (rc) {
		printk(KERN_ERR "TLAN: Could not set up device.\n");
		goto err_out_free_dev;
	}

	rc = register_netdev(dev);
	if (rc) {
		printk(KERN_ERR "TLAN: Could not register device.\n");
		goto err_out_uninit;
	}


	TLanDevicesInstalled++;
	boards_found++;

	/* pdev is NULL if this is an EISA device */
	if (pdev)
		tlan_have_pci++;
	else {
		priv->nextDevice = TLan_Eisa_Devices;
		TLan_Eisa_Devices = dev;
		tlan_have_eisa++;
	}

	printk(KERN_INFO "TLAN: %s irq=%2d, io=%04x, %s, Rev. %d\n",
			dev->name,
			(int) dev->irq,
			(int) dev->base_addr,
			priv->adapter->deviceLabel,
			priv->adapterRev);
	return 0;

err_out_uninit:
	pci_free_consistent(priv->pciDev, priv->dmaSize, priv->dmaStorage,
			    priv->dmaStorageDMA );
err_out_free_dev:
	free_netdev(dev);
err_out_regions:
#ifdef CONFIG_PCI
	if (pdev)
		pci_release_regions(pdev);
#endif
err_out:
	if (pdev)
		pci_disable_device(pdev);
	return rc;
}


static void TLan_Eisa_Cleanup(void)
{
	struct net_device *dev;
	TLanPrivateInfo *priv;

	while( tlan_have_eisa ) {
		dev = TLan_Eisa_Devices;
		priv = netdev_priv(dev);
		if (priv->dmaStorage) {
			pci_free_consistent(priv->pciDev, priv->dmaSize,
					    priv->dmaStorage, priv->dmaStorageDMA );
		}
		release_region( dev->base_addr, 0x10);
		unregister_netdev( dev );
		TLan_Eisa_Devices = priv->nextDevice;
		free_netdev( dev );
		tlan_have_eisa--;
	}
}


static void __exit tlan_exit(void)
{
	pci_unregister_driver(&tlan_driver);

	if (tlan_have_eisa)
		TLan_Eisa_Cleanup();

}


/* Module loading/unloading */
module_init(tlan_probe);
module_exit(tlan_exit);



	/**************************************************************
	 * 	TLan_EisaProbe
	 *
	 *  	Returns: 0 on success, 1 otherwise
	 *
	 *  	Parms:	 None
	 *
	 *
	 *  	This functions probes for EISA devices and calls
	 *  	TLan_probe1 when one is found.
	 *
	 *************************************************************/

static void  __init TLan_EisaProbe (void)
{
	long 	ioaddr;
	int 	rc = -ENODEV;
	int 	irq;
	u16	device_id;

	if (!EISA_bus) {
		TLAN_DBG(TLAN_DEBUG_PROBE, "No EISA bus present\n");
		return;
	}

	/* Loop through all slots of the EISA bus */
	for (ioaddr = 0x1000; ioaddr < 0x9000; ioaddr += 0x1000) {

	TLAN_DBG(TLAN_DEBUG_PROBE,"EISA_ID 0x%4x: 0x%4x\n",
		 (int) ioaddr + 0xC80, inw(ioaddr + EISA_ID));
	TLAN_DBG(TLAN_DEBUG_PROBE,"EISA_ID 0x%4x: 0x%4x\n",
		 (int) ioaddr + 0xC82, inw(ioaddr + EISA_ID2));


		TLAN_DBG(TLAN_DEBUG_PROBE, "Probing for EISA adapter at IO: 0x%4x : ",
				   	(int) ioaddr);
		if (request_region(ioaddr, 0x10, TLanSignature) == NULL)
			goto out;

		if (inw(ioaddr + EISA_ID) != 0x110E) {
			release_region(ioaddr, 0x10);
			goto out;
		}

		device_id = inw(ioaddr + EISA_ID2);
		if (device_id !=  0x20F1 && device_id != 0x40F1) {
			release_region (ioaddr, 0x10);
			goto out;
		}

	 	if (inb(ioaddr + EISA_CR) != 0x1) { 	/* Check if adapter is enabled */
			release_region (ioaddr, 0x10);
			goto out2;
		}

		if (debug == 0x10)
			printk("Found one\n");


		/* Get irq from board */
		switch (inb(ioaddr + 0xCC0)) {
			case(0x10):
				irq=5;
				break;
			case(0x20):
				irq=9;
				break;
			case(0x40):
				irq=10;
				break;
			case(0x80):
				irq=11;
				break;
			default:
				goto out;
		}


		/* Setup the newly found eisa adapter */
		rc = TLan_probe1( NULL, ioaddr, irq,
					12, NULL);
		continue;

		out:
			if (debug == 0x10)
				printk("None found\n");
			continue;

		out2:	if (debug == 0x10)
				printk("Card found but it is not enabled, skipping\n");
			continue;

	}

} /* TLan_EisaProbe */

#ifdef CONFIG_NET_POLL_CONTROLLER
static void TLan_Poll(struct net_device *dev)
{
	disable_irq(dev->irq);
	TLan_HandleInterrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

static const struct net_device_ops TLan_netdev_ops = {
	.ndo_open 		= TLan_Open,
	.ndo_stop		= TLan_Close,
	.ndo_start_xmit		= TLan_StartTx,
	.ndo_tx_timeout		= TLan_tx_timeout,
	.ndo_get_stats		= TLan_GetStats,
	.ndo_set_multicast_list = TLan_SetMulticastList,
	.ndo_do_ioctl		= TLan_ioctl,
	.ndo_change_mtu		= eth_change_mtu,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	 = TLan_Poll,
#endif
};



	/***************************************************************
	 *	TLan_Init
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

static int TLan_Init( struct net_device *dev )
{
	int		dma_size;
	int 		err;
	int		i;
	TLanPrivateInfo	*priv;

	priv = netdev_priv(dev);

	dma_size = ( TLAN_NUM_RX_LISTS + TLAN_NUM_TX_LISTS )
		* ( sizeof(TLanList) );
	priv->dmaStorage = pci_alloc_consistent(priv->pciDev,
						dma_size, &priv->dmaStorageDMA);
	priv->dmaSize = dma_size;

	if ( priv->dmaStorage == NULL ) {
		printk(KERN_ERR "TLAN:  Could not allocate lists and buffers for %s.\n",
			dev->name );
		return -ENOMEM;
	}
	memset( priv->dmaStorage, 0, dma_size );
	priv->rxList = (TLanList *) ALIGN((unsigned long)priv->dmaStorage, 8);
	priv->rxListDMA = ALIGN(priv->dmaStorageDMA, 8);
	priv->txList = priv->rxList + TLAN_NUM_RX_LISTS;
	priv->txListDMA = priv->rxListDMA + sizeof(TLanList) * TLAN_NUM_RX_LISTS;

	err = 0;
	for ( i = 0;  i < 6 ; i++ )
		err |= TLan_EeReadByte( dev,
					(u8) priv->adapter->addrOfs + i,
					(u8 *) &dev->dev_addr[i] );
	if ( err ) {
		printk(KERN_ERR "TLAN: %s: Error reading MAC from eeprom: %d\n",
			dev->name,
			err );
	}
	dev->addr_len = 6;

	netif_carrier_off(dev);

	/* Device methods */
	dev->netdev_ops = &TLan_netdev_ops;
	dev->watchdog_timeo = TX_TIMEOUT;

	return 0;

} /* TLan_Init */




	/***************************************************************
	 *	TLan_Open
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

static int TLan_Open( struct net_device *dev )
{
	TLanPrivateInfo	*priv = netdev_priv(dev);
	int		err;

	priv->tlanRev = TLan_DioRead8( dev->base_addr, TLAN_DEF_REVISION );
	err = request_irq( dev->irq, TLan_HandleInterrupt, IRQF_SHARED,
			   dev->name, dev );

	if ( err ) {
		pr_err("TLAN:  Cannot open %s because IRQ %d is already in use.\n",
		       dev->name, dev->irq );
		return err;
	}

	init_timer(&priv->timer);
	netif_start_queue(dev);

	/* NOTE: It might not be necessary to read the stats before a
			 reset if you don't care what the values are.
	*/
	TLan_ResetLists( dev );
	TLan_ReadAndClearStats( dev, TLAN_IGNORE );
	TLan_ResetAdapter( dev );

	TLAN_DBG( TLAN_DEBUG_GNRL, "%s: Opened.  TLAN Chip Rev: %x\n",
		  dev->name, priv->tlanRev );

	return 0;

} /* TLan_Open */



	/**************************************************************
	 *	TLan_ioctl
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

static int TLan_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	TLanPrivateInfo *priv = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(rq);
	u32 phy   = priv->phy[priv->phyNum];

	if (!priv->phyOnline)
		return -EAGAIN;

	switch(cmd) {
	case SIOCGMIIPHY:		/* Get address of MII PHY in use. */
			data->phy_id = phy;


	case SIOCGMIIREG:		/* Read MII PHY register. */
			TLan_MiiReadReg(dev, data->phy_id & 0x1f,
					data->reg_num & 0x1f, &data->val_out);
			return 0;


	case SIOCSMIIREG:		/* Write MII PHY register. */
			TLan_MiiWriteReg(dev, data->phy_id & 0x1f,
					 data->reg_num & 0x1f, data->val_in);
			return 0;
		default:
			return -EOPNOTSUPP;
	}
} /* tlan_ioctl */


	/***************************************************************
	 * 	TLan_tx_timeout
	 *
	 * 	Returns: nothing
	 *
	 * 	Params:
	 * 		dev	structure of device which timed out
	 * 			during transmit.
	 *
	 **************************************************************/

static void TLan_tx_timeout(struct net_device *dev)
{

	TLAN_DBG( TLAN_DEBUG_GNRL, "%s: Transmit timed out.\n", dev->name);

	/* Ok so we timed out, lets see what we can do about it...*/
	TLan_FreeLists( dev );
	TLan_ResetLists( dev );
	TLan_ReadAndClearStats( dev, TLAN_IGNORE );
	TLan_ResetAdapter( dev );
	dev->trans_start = jiffies;
	netif_wake_queue( dev );

}


	/***************************************************************
	 * 	TLan_tx_timeout_work
	 *
	 * 	Returns: nothing
	 *
	 * 	Params:
	 * 		work	work item of device which timed out
	 *
	 **************************************************************/

static void TLan_tx_timeout_work(struct work_struct *work)
{
	TLanPrivateInfo	*priv =
		container_of(work, TLanPrivateInfo, tlan_tqueue);

	TLan_tx_timeout(priv->dev);
}



	/***************************************************************
	 *	TLan_StartTx
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

static netdev_tx_t TLan_StartTx( struct sk_buff *skb, struct net_device *dev )
{
	TLanPrivateInfo *priv = netdev_priv(dev);
	dma_addr_t	tail_list_phys;
	TLanList	*tail_list;
	unsigned long	flags;
	unsigned int    txlen;

	if ( ! priv->phyOnline ) {
		TLAN_DBG( TLAN_DEBUG_TX, "TRANSMIT:  %s PHY is not ready\n",
			  dev->name );
		dev_kfree_skb_any(skb);
		return NETDEV_TX_OK;
	}

	if (skb_padto(skb, TLAN_MIN_FRAME_SIZE))
		return NETDEV_TX_OK;
	txlen = max(skb->len, (unsigned int)TLAN_MIN_FRAME_SIZE);

	tail_list = priv->txList + priv->txTail;
	tail_list_phys = priv->txListDMA + sizeof(TLanList) * priv->txTail;

	if ( tail_list->cStat != TLAN_CSTAT_UNUSED ) {
		TLAN_DBG( TLAN_DEBUG_TX,
			  "TRANSMIT:  %s is busy (Head=%d Tail=%d)\n",
			  dev->name, priv->txHead, priv->txTail );
		netif_stop_queue(dev);
		priv->txBusyCount++;
		return NETDEV_TX_BUSY;
	}

	tail_list->forward = 0;

	tail_list->buffer[0].address = pci_map_single(priv->pciDev,
						      skb->data, txlen,
						      PCI_DMA_TODEVICE);
	TLan_StoreSKB(tail_list, skb);

	tail_list->frameSize = (u16) txlen;
	tail_list->buffer[0].count = TLAN_LAST_BUFFER | (u32) txlen;
	tail_list->buffer[1].count = 0;
	tail_list->buffer[1].address = 0;

	spin_lock_irqsave(&priv->lock, flags);
	tail_list->cStat = TLAN_CSTAT_READY;
	if ( ! priv->txInProgress ) {
		priv->txInProgress = 1;
		TLAN_DBG( TLAN_DEBUG_TX,
			  "TRANSMIT:  Starting TX on buffer %d\n", priv->txTail );
		outl( tail_list_phys, dev->base_addr + TLAN_CH_PARM );
		outl( TLAN_HC_GO, dev->base_addr + TLAN_HOST_CMD );
	} else {
		TLAN_DBG( TLAN_DEBUG_TX, "TRANSMIT:  Adding buffer %d to TX channel\n",
			  priv->txTail );
		if ( priv->txTail == 0 ) {
			( priv->txList + ( TLAN_NUM_TX_LISTS - 1 ) )->forward
				= tail_list_phys;
		} else {
			( priv->txList + ( priv->txTail - 1 ) )->forward
				= tail_list_phys;
		}
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	CIRC_INC( priv->txTail, TLAN_NUM_TX_LISTS );

	dev->trans_start = jiffies;
	return NETDEV_TX_OK;

} /* TLan_StartTx */




	/***************************************************************
	 *	TLan_HandleInterrupt
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

static irqreturn_t TLan_HandleInterrupt(int irq, void *dev_id)
{
	struct net_device	*dev = dev_id;
	TLanPrivateInfo *priv = netdev_priv(dev);
	u16		host_int;
	u16		type;

	spin_lock(&priv->lock);

	host_int = inw( dev->base_addr + TLAN_HOST_INT );
	type = ( host_int & TLAN_HI_IT_MASK ) >> 2;
	if ( type ) {
		u32	ack;
		u32	host_cmd;

		outw( host_int, dev->base_addr + TLAN_HOST_INT );
		ack = TLanIntVector[type]( dev, host_int );

		if ( ack ) {
			host_cmd = TLAN_HC_ACK | ack | ( type << 18 );
			outl( host_cmd, dev->base_addr + TLAN_HOST_CMD );
		}
	}

	spin_unlock(&priv->lock);

	return IRQ_RETVAL(type);
} /* TLan_HandleInterrupts */




	/***************************************************************
	 *	TLan_Close
	 *
	 * 	Returns:
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

static int TLan_Close(struct net_device *dev)
{
	TLanPrivateInfo *priv = netdev_priv(dev);

	netif_stop_queue(dev);
	priv->neg_be_verbose = 0;

	TLan_ReadAndClearStats( dev, TLAN_RECORD );
	outl( TLAN_HC_AD_RST, dev->base_addr + TLAN_HOST_CMD );
	if ( priv->timer.function != NULL ) {
		del_timer_sync( &priv->timer );
		priv->timer.function = NULL;
	}

	free_irq( dev->irq, dev );
	TLan_FreeLists( dev );
	TLAN_DBG( TLAN_DEBUG_GNRL, "Device %s closed.\n", dev->name );

	return 0;

} /* TLan_Close */




	/***************************************************************
	 *	TLan_GetStats
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

static struct net_device_stats *TLan_GetStats( struct net_device *dev )
{
	TLanPrivateInfo	*priv = netdev_priv(dev);
	int i;

	/* Should only read stats if open ? */
	TLan_ReadAndClearStats( dev, TLAN_RECORD );

	TLAN_DBG( TLAN_DEBUG_RX, "RECEIVE:  %s EOC count = %d\n", dev->name,
		  priv->rxEocCount );
	TLAN_DBG( TLAN_DEBUG_TX, "TRANSMIT:  %s Busy count = %d\n", dev->name,
		  priv->txBusyCount );
	if ( debug & TLAN_DEBUG_GNRL ) {
		TLan_PrintDio( dev->base_addr );
		TLan_PhyPrint( dev );
	}
	if ( debug & TLAN_DEBUG_LIST ) {
		for ( i = 0; i < TLAN_NUM_RX_LISTS; i++ )
			TLan_PrintList( priv->rxList + i, "RX", i );
		for ( i = 0; i < TLAN_NUM_TX_LISTS; i++ )
			TLan_PrintList( priv->txList + i, "TX", i );
	}

	return &dev->stats;

} /* TLan_GetStats */




	/***************************************************************
	 *	TLan_SetMulticastList
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

static void TLan_SetMulticastList( struct net_device *dev )
{
	struct dev_mc_list	*dmi = dev->mc_list;
	u32			hash1 = 0;
	u32			hash2 = 0;
	int			i;
	u32			offset;
	u8			tmp;

	if ( dev->flags & IFF_PROMISC ) {
		tmp = TLan_DioRead8( dev->base_addr, TLAN_NET_CMD );
		TLan_DioWrite8( dev->base_addr,
				TLAN_NET_CMD, tmp | TLAN_NET_CMD_CAF );
	} else {
		tmp = TLan_DioRead8( dev->base_addr, TLAN_NET_CMD );
		TLan_DioWrite8( dev->base_addr,
				TLAN_NET_CMD, tmp & ~TLAN_NET_CMD_CAF );
		if ( dev->flags & IFF_ALLMULTI ) {
			for ( i = 0; i < 3; i++ )
				TLan_SetMac( dev, i + 1, NULL );
			TLan_DioWrite32( dev->base_addr, TLAN_HASH_1, 0xFFFFFFFF );
			TLan_DioWrite32( dev->base_addr, TLAN_HASH_2, 0xFFFFFFFF );
		} else {
			for ( i = 0; i < dev->mc_count; i++ ) {
				if ( i < 3 ) {
					TLan_SetMac( dev, i + 1,
						     (char *) &dmi->dmi_addr );
				} else {
					offset = TLan_HashFunc( (u8 *) &dmi->dmi_addr );
					if ( offset < 32 )
						hash1 |= ( 1 << offset );
					else
						hash2 |= ( 1 << ( offset - 32 ) );
				}
				dmi = dmi->next;
			}
			for ( ; i < 3; i++ )
				TLan_SetMac( dev, i + 1, NULL );
			TLan_DioWrite32( dev->base_addr, TLAN_HASH_1, hash1 );
			TLan_DioWrite32( dev->base_addr, TLAN_HASH_2, hash2 );
		}
	}

} /* TLan_SetMulticastList */



/*****************************************************************************
******************************************************************************

        ThunderLAN Driver Interrupt Vectors and Table

	Please see Chap. 4, "Interrupt Handling" of the "ThunderLAN
	Programmer's Guide" for more informations on handling interrupts
	generated by TLAN based adapters.

******************************************************************************
*****************************************************************************/




	/***************************************************************
	 *	TLan_HandleTxEOF
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

static u32 TLan_HandleTxEOF( struct net_device *dev, u16 host_int )
{
	TLanPrivateInfo	*priv = netdev_priv(dev);
	int		eoc = 0;
	TLanList	*head_list;
	dma_addr_t	head_list_phys;
	u32		ack = 0;
	u16		tmpCStat;

	TLAN_DBG( TLAN_DEBUG_TX, "TRANSMIT:  Handling TX EOF (Head=%d Tail=%d)\n",
		  priv->txHead, priv->txTail );
	head_list = priv->txList + priv->txHead;

	while (((tmpCStat = head_list->cStat ) & TLAN_CSTAT_FRM_CMP) && (ack < 255)) {
		struct sk_buff *skb = TLan_GetSKB(head_list);

		ack++;
		pci_unmap_single(priv->pciDev, head_list->buffer[0].address,
				 max(skb->len,
				     (unsigned int)TLAN_MIN_FRAME_SIZE),
				 PCI_DMA_TODEVICE);
		dev_kfree_skb_any(skb);
		head_list->buffer[8].address = 0;
		head_list->buffer[9].address = 0;

		if ( tmpCStat & TLAN_CSTAT_EOC )
			eoc = 1;

		dev->stats.tx_bytes += head_list->frameSize;

		head_list->cStat = TLAN_CSTAT_UNUSED;
		netif_start_queue(dev);
		CIRC_INC( priv->txHead, TLAN_NUM_TX_LISTS );
		head_list = priv->txList + priv->txHead;
	}

	if (!ack)
		printk(KERN_INFO "TLAN: Received interrupt for uncompleted TX frame.\n");

	if ( eoc ) {
		TLAN_DBG( TLAN_DEBUG_TX,
			  "TRANSMIT:  Handling TX EOC (Head=%d Tail=%d)\n",
			  priv->txHead, priv->txTail );
		head_list = priv->txList + priv->txHead;
		head_list_phys = priv->txListDMA + sizeof(TLanList) * priv->txHead;
		if ( ( head_list->cStat & TLAN_CSTAT_READY ) == TLAN_CSTAT_READY ) {
			outl(head_list_phys, dev->base_addr + TLAN_CH_PARM );
			ack |= TLAN_HC_GO;
		} else {
			priv->txInProgress = 0;
		}
	}

	if ( priv->adapter->flags & TLAN_ADAPTER_ACTIVITY_LED ) {
		TLan_DioWrite8( dev->base_addr,
				TLAN_LED_REG, TLAN_LED_LINK | TLAN_LED_ACT );
		if ( priv->timer.function == NULL ) {
			 priv->timer.function = &TLan_Timer;
			 priv->timer.data = (unsigned long) dev;
			 priv->timer.expires = jiffies + TLAN_TIMER_ACT_DELAY;
			 priv->timerSetAt = jiffies;
			 priv->timerType = TLAN_TIMER_ACTIVITY;
			 add_timer(&priv->timer);
		} else if ( priv->timerType == TLAN_TIMER_ACTIVITY ) {
			priv->timerSetAt = jiffies;
		}
	}

	return ack;

} /* TLan_HandleTxEOF */




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

static u32 TLan_HandleStatOverflow( struct net_device *dev, u16 host_int )
{
	TLan_ReadAndClearStats( dev, TLAN_RECORD );

	return 1;

} /* TLan_HandleStatOverflow */




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

static u32 TLan_HandleRxEOF( struct net_device *dev, u16 host_int )
{
	TLanPrivateInfo	*priv = netdev_priv(dev);
	u32		ack = 0;
	int		eoc = 0;
	TLanList	*head_list;
	struct sk_buff	*skb;
	TLanList	*tail_list;
	u16		tmpCStat;
	dma_addr_t	head_list_phys;

	TLAN_DBG( TLAN_DEBUG_RX, "RECEIVE:  Handling RX EOF (Head=%d Tail=%d)\n",
		  priv->rxHead, priv->rxTail );
	head_list = priv->rxList + priv->rxHead;
	head_list_phys = priv->rxListDMA + sizeof(TLanList) * priv->rxHead;

	while (((tmpCStat = head_list->cStat) & TLAN_CSTAT_FRM_CMP) && (ack < 255)) {
		dma_addr_t frameDma = head_list->buffer[0].address;
		u32 frameSize = head_list->frameSize;
		struct sk_buff *new_skb;

		ack++;
		if (tmpCStat & TLAN_CSTAT_EOC)
			eoc = 1;

		new_skb = netdev_alloc_skb(dev, TLAN_MAX_FRAME_SIZE + 7 );
		if ( !new_skb )
			goto drop_and_reuse;

		skb = TLan_GetSKB(head_list);
		pci_unmap_single(priv->pciDev, frameDma,
				 TLAN_MAX_FRAME_SIZE, PCI_DMA_FROMDEVICE);
		skb_put( skb, frameSize );

		dev->stats.rx_bytes += frameSize;

		skb->protocol = eth_type_trans( skb, dev );
		netif_rx( skb );

		skb_reserve( new_skb, NET_IP_ALIGN );
		head_list->buffer[0].address = pci_map_single(priv->pciDev,
							      new_skb->data,
							      TLAN_MAX_FRAME_SIZE,
							      PCI_DMA_FROMDEVICE);

		TLan_StoreSKB(head_list, new_skb);
drop_and_reuse:
		head_list->forward = 0;
		head_list->cStat = 0;
		tail_list = priv->rxList + priv->rxTail;
		tail_list->forward = head_list_phys;

		CIRC_INC( priv->rxHead, TLAN_NUM_RX_LISTS );
		CIRC_INC( priv->rxTail, TLAN_NUM_RX_LISTS );
		head_list = priv->rxList + priv->rxHead;
		head_list_phys = priv->rxListDMA + sizeof(TLanList) * priv->rxHead;
	}

	if (!ack)
		printk(KERN_INFO "TLAN: Received interrupt for uncompleted RX frame.\n");


	if ( eoc ) {
		TLAN_DBG( TLAN_DEBUG_RX,
			  "RECEIVE:  Handling RX EOC (Head=%d Tail=%d)\n",
			  priv->rxHead, priv->rxTail );
		head_list = priv->rxList + priv->rxHead;
		head_list_phys = priv->rxListDMA + sizeof(TLanList) * priv->rxHead;
		outl(head_list_phys, dev->base_addr + TLAN_CH_PARM );
		ack |= TLAN_HC_GO | TLAN_HC_RT;
		priv->rxEocCount++;
	}

	if ( priv->adapter->flags & TLAN_ADAPTER_ACTIVITY_LED ) {
		TLan_DioWrite8( dev->base_addr,
				TLAN_LED_REG, TLAN_LED_LINK | TLAN_LED_ACT );
		if ( priv->timer.function == NULL )  {
			priv->timer.function = &TLan_Timer;
			priv->timer.data = (unsigned long) dev;
			priv->timer.expires = jiffies + TLAN_TIMER_ACT_DELAY;
			priv->timerSetAt = jiffies;
			priv->timerType = TLAN_TIMER_ACTIVITY;
			add_timer(&priv->timer);
		} else if ( priv->timerType == TLAN_TIMER_ACTIVITY ) {
			priv->timerSetAt = jiffies;
		}
	}

	return ack;

} /* TLan_HandleRxEOF */




	/***************************************************************
	 *	TLan_HandleDummy
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

static u32 TLan_HandleDummy( struct net_device *dev, u16 host_int )
{
	printk( "TLAN:  Test interrupt on %s.\n", dev->name );
	return 1;

} /* TLan_HandleDummy */




	/***************************************************************
	 *	TLan_HandleTxEOC
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

static u32 TLan_HandleTxEOC( struct net_device *dev, u16 host_int )
{
	TLanPrivateInfo	*priv = netdev_priv(dev);
	TLanList		*head_list;
	dma_addr_t		head_list_phys;
	u32			ack = 1;

	host_int = 0;
	if ( priv->tlanRev < 0x30 ) {
		TLAN_DBG( TLAN_DEBUG_TX,
			  "TRANSMIT:  Handling TX EOC (Head=%d Tail=%d) -- IRQ\n",
			  priv->txHead, priv->txTail );
		head_list = priv->txList + priv->txHead;
		head_list_phys = priv->txListDMA + sizeof(TLanList) * priv->txHead;
		if ( ( head_list->cStat & TLAN_CSTAT_READY ) == TLAN_CSTAT_READY ) {
			netif_stop_queue(dev);
			outl( head_list_phys, dev->base_addr + TLAN_CH_PARM );
			ack |= TLAN_HC_GO;
		} else {
			priv->txInProgress = 0;
		}
	}

	return ack;

} /* TLan_HandleTxEOC */




	/***************************************************************
	 *	TLan_HandleStatusCheck
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

static u32 TLan_HandleStatusCheck( struct net_device *dev, u16 host_int )
{
	TLanPrivateInfo	*priv = netdev_priv(dev);
	u32		ack;
	u32		error;
	u8		net_sts;
	u32		phy;
	u16		tlphy_ctl;
	u16		tlphy_sts;

	ack = 1;
	if ( host_int & TLAN_HI_IV_MASK ) {
		netif_stop_queue( dev );
		error = inl( dev->base_addr + TLAN_CH_PARM );
		printk( "TLAN:  %s: Adaptor Error = 0x%x\n", dev->name, error );
		TLan_ReadAndClearStats( dev, TLAN_RECORD );
		outl( TLAN_HC_AD_RST, dev->base_addr + TLAN_HOST_CMD );

		schedule_work(&priv->tlan_tqueue);

		netif_wake_queue(dev);
		ack = 0;
	} else {
		TLAN_DBG( TLAN_DEBUG_GNRL, "%s: Status Check\n", dev->name );
		phy = priv->phy[priv->phyNum];

		net_sts = TLan_DioRead8( dev->base_addr, TLAN_NET_STS );
		if ( net_sts ) {
			TLan_DioWrite8( dev->base_addr, TLAN_NET_STS, net_sts );
			TLAN_DBG( TLAN_DEBUG_GNRL, "%s:    Net_Sts = %x\n",
				  dev->name, (unsigned) net_sts );
		}
		if ( ( net_sts & TLAN_NET_STS_MIRQ ) &&  ( priv->phyNum == 0 ) ) {
			TLan_MiiReadReg( dev, phy, TLAN_TLPHY_STS, &tlphy_sts );
			TLan_MiiReadReg( dev, phy, TLAN_TLPHY_CTL, &tlphy_ctl );
        		if ( ! ( tlphy_sts & TLAN_TS_POLOK ) &&
			     ! ( tlphy_ctl & TLAN_TC_SWAPOL ) ) {
                		tlphy_ctl |= TLAN_TC_SWAPOL;
                		TLan_MiiWriteReg( dev, phy, TLAN_TLPHY_CTL, tlphy_ctl);
        		} else if ( ( tlphy_sts & TLAN_TS_POLOK )
				    && ( tlphy_ctl & TLAN_TC_SWAPOL ) ) {
                		tlphy_ctl &= ~TLAN_TC_SWAPOL;
                		TLan_MiiWriteReg( dev, phy, TLAN_TLPHY_CTL, tlphy_ctl);
        		}

			if (debug) {
				TLan_PhyPrint( dev );
			}
		}
	}

	return ack;

} /* TLan_HandleStatusCheck */




	/***************************************************************
	 *	TLan_HandleRxEOC
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

static u32 TLan_HandleRxEOC( struct net_device *dev, u16 host_int )
{
	TLanPrivateInfo	*priv = netdev_priv(dev);
	dma_addr_t	head_list_phys;
	u32		ack = 1;

	if (  priv->tlanRev < 0x30 ) {
		TLAN_DBG( TLAN_DEBUG_RX,
			  "RECEIVE:  Handling RX EOC (Head=%d Tail=%d) -- IRQ\n",
			  priv->rxHead, priv->rxTail );
		head_list_phys = priv->rxListDMA + sizeof(TLanList) * priv->rxHead;
		outl( head_list_phys, dev->base_addr + TLAN_CH_PARM );
		ack |= TLAN_HC_GO | TLAN_HC_RT;
		priv->rxEocCount++;
	}

	return ack;

} /* TLan_HandleRxEOC */




/*****************************************************************************
******************************************************************************

	ThunderLAN Driver Timer Function

******************************************************************************
*****************************************************************************/


	/***************************************************************
	 *	TLan_Timer
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

static void TLan_Timer( unsigned long data )
{
	struct net_device	*dev = (struct net_device *) data;
	TLanPrivateInfo	*priv = netdev_priv(dev);
	u32		elapsed;
	unsigned long	flags = 0;

	priv->timer.function = NULL;

	switch ( priv->timerType ) {
#ifdef MONITOR
		case TLAN_TIMER_LINK_BEAT:
			TLan_PhyMonitor( dev );
			break;
#endif
		case TLAN_TIMER_PHY_PDOWN:
			TLan_PhyPowerDown( dev );
			break;
		case TLAN_TIMER_PHY_PUP:
			TLan_PhyPowerUp( dev );
			break;
		case TLAN_TIMER_PHY_RESET:
			TLan_PhyReset( dev );
			break;
		case TLAN_TIMER_PHY_START_LINK:
			TLan_PhyStartLink( dev );
			break;
		case TLAN_TIMER_PHY_FINISH_AN:
			TLan_PhyFinishAutoNeg( dev );
			break;
		case TLAN_TIMER_FINISH_RESET:
			TLan_FinishReset( dev );
			break;
		case TLAN_TIMER_ACTIVITY:
			spin_lock_irqsave(&priv->lock, flags);
			if ( priv->timer.function == NULL ) {
				elapsed = jiffies - priv->timerSetAt;
				if ( elapsed >= TLAN_TIMER_ACT_DELAY ) {
					TLan_DioWrite8( dev->base_addr,
							TLAN_LED_REG, TLAN_LED_LINK );
				} else  {
					priv->timer.function = &TLan_Timer;
					priv->timer.expires = priv->timerSetAt
						+ TLAN_TIMER_ACT_DELAY;
					spin_unlock_irqrestore(&priv->lock, flags);
					add_timer( &priv->timer );
					break;
				}
			}
			spin_unlock_irqrestore(&priv->lock, flags);
			break;
		default:
			break;
	}

} /* TLan_Timer */




/*****************************************************************************
******************************************************************************

	ThunderLAN Driver Adapter Related Routines

******************************************************************************
*****************************************************************************/


	/***************************************************************
	 *	TLan_ResetLists
	 *
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		dev	The device structure with the list
	 *			stuctures to be reset.
	 *
	 *	This routine sets the variables associated with managing
	 *	the TLAN lists to their initial values.
	 *
	 **************************************************************/

static void TLan_ResetLists( struct net_device *dev )
{
	TLanPrivateInfo *priv = netdev_priv(dev);
	int		i;
	TLanList	*list;
	dma_addr_t	list_phys;
	struct sk_buff	*skb;

	priv->txHead = 0;
	priv->txTail = 0;
	for ( i = 0; i < TLAN_NUM_TX_LISTS; i++ ) {
		list = priv->txList + i;
		list->cStat = TLAN_CSTAT_UNUSED;
		list->buffer[0].address = 0;
		list->buffer[2].count = 0;
		list->buffer[2].address = 0;
		list->buffer[8].address = 0;
		list->buffer[9].address = 0;
	}

	priv->rxHead = 0;
	priv->rxTail = TLAN_NUM_RX_LISTS - 1;
	for ( i = 0; i < TLAN_NUM_RX_LISTS; i++ ) {
		list = priv->rxList + i;
		list_phys = priv->rxListDMA + sizeof(TLanList) * i;
		list->cStat = TLAN_CSTAT_READY;
		list->frameSize = TLAN_MAX_FRAME_SIZE;
		list->buffer[0].count = TLAN_MAX_FRAME_SIZE | TLAN_LAST_BUFFER;
		skb = netdev_alloc_skb(dev, TLAN_MAX_FRAME_SIZE + 7 );
		if ( !skb ) {
			pr_err("TLAN: out of memory for received data.\n" );
			break;
		}

		skb_reserve( skb, NET_IP_ALIGN );
		list->buffer[0].address = pci_map_single(priv->pciDev,
							 skb->data,
							 TLAN_MAX_FRAME_SIZE,
							 PCI_DMA_FROMDEVICE);
		TLan_StoreSKB(list, skb);
		list->buffer[1].count = 0;
		list->buffer[1].address = 0;
		list->forward = list_phys + sizeof(TLanList);
	}

	/* in case ran out of memory early, clear bits */
	while (i < TLAN_NUM_RX_LISTS) {
		TLan_StoreSKB(priv->rxList + i, NULL);
		++i;
	}
	list->forward = 0;

} /* TLan_ResetLists */


static void TLan_FreeLists( struct net_device *dev )
{
	TLanPrivateInfo *priv = netdev_priv(dev);
	int		i;
	TLanList	*list;
	struct sk_buff	*skb;

	for ( i = 0; i < TLAN_NUM_TX_LISTS; i++ ) {
		list = priv->txList + i;
		skb = TLan_GetSKB(list);
		if ( skb ) {
			pci_unmap_single(
				priv->pciDev,
				list->buffer[0].address,
				max(skb->len,
				    (unsigned int)TLAN_MIN_FRAME_SIZE),
				PCI_DMA_TODEVICE);
			dev_kfree_skb_any( skb );
			list->buffer[8].address = 0;
			list->buffer[9].address = 0;
		}
	}

	for ( i = 0; i < TLAN_NUM_RX_LISTS; i++ ) {
		list = priv->rxList + i;
		skb = TLan_GetSKB(list);
		if ( skb ) {
			pci_unmap_single(priv->pciDev,
					 list->buffer[0].address,
					 TLAN_MAX_FRAME_SIZE,
					 PCI_DMA_FROMDEVICE);
			dev_kfree_skb_any( skb );
			list->buffer[8].address = 0;
			list->buffer[9].address = 0;
		}
	}
} /* TLan_FreeLists */




	/***************************************************************
	 *	TLan_PrintDio
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

static void TLan_PrintDio( u16 io_base )
{
	u32 data0, data1;
	int	i;

	printk( "TLAN:   Contents of internal registers for io base 0x%04hx.\n",
		io_base );
	printk( "TLAN:      Off.  +0         +4\n" );
	for ( i = 0; i < 0x4C; i+= 8 ) {
		data0 = TLan_DioRead32( io_base, i );
		data1 = TLan_DioRead32( io_base, i + 0x4 );
		printk( "TLAN:      0x%02x  0x%08x 0x%08x\n", i, data0, data1 );
	}

} /* TLan_PrintDio */




	/***************************************************************
	 *	TLan_PrintList
	 *
	 *	Returns:
	 *		Nothing
	 *	Parms:
	 *		list	A pointer to the TLanList structure to
	 *			be printed.
	 *		type	A string to designate type of list,
	 *			"Rx" or "Tx".
	 *		num	The index of the list.
	 *
	 *	This function prints out the contents of the list
	 *	pointed to by the list parameter.
	 *
	 **************************************************************/

static void TLan_PrintList( TLanList *list, char *type, int num)
{
	int i;

	printk( "TLAN:   %s List %d at %p\n", type, num, list );
	printk( "TLAN:      Forward    = 0x%08x\n",  list->forward );
	printk( "TLAN:      CSTAT      = 0x%04hx\n", list->cStat );
	printk( "TLAN:      Frame Size = 0x%04hx\n", list->frameSize );
	/* for ( i = 0; i < 10; i++ ) { */
	for ( i = 0; i < 2; i++ ) {
		printk( "TLAN:      Buffer[%d].count, addr = 0x%08x, 0x%08x\n",
			i, list->buffer[i].count, list->buffer[i].address );
	}

} /* TLan_PrintList */




	/***************************************************************
	 *	TLan_ReadAndClearStats
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

static void TLan_ReadAndClearStats( struct net_device *dev, int record )
{
	u32		tx_good, tx_under;
	u32		rx_good, rx_over;
	u32		def_tx, crc, code;
	u32		multi_col, single_col;
	u32		excess_col, late_col, loss;

	outw( TLAN_GOOD_TX_FRMS, dev->base_addr + TLAN_DIO_ADR );
	tx_good  = inb( dev->base_addr + TLAN_DIO_DATA );
	tx_good += inb( dev->base_addr + TLAN_DIO_DATA + 1 ) << 8;
	tx_good += inb( dev->base_addr + TLAN_DIO_DATA + 2 ) << 16;
	tx_under = inb( dev->base_addr + TLAN_DIO_DATA + 3 );

	outw( TLAN_GOOD_RX_FRMS, dev->base_addr + TLAN_DIO_ADR );
	rx_good  = inb( dev->base_addr + TLAN_DIO_DATA );
	rx_good += inb( dev->base_addr + TLAN_DIO_DATA + 1 ) << 8;
	rx_good += inb( dev->base_addr + TLAN_DIO_DATA + 2 ) << 16;
	rx_over  = inb( dev->base_addr + TLAN_DIO_DATA + 3 );

	outw( TLAN_DEFERRED_TX, dev->base_addr + TLAN_DIO_ADR );
	def_tx  = inb( dev->base_addr + TLAN_DIO_DATA );
	def_tx += inb( dev->base_addr + TLAN_DIO_DATA + 1 ) << 8;
	crc     = inb( dev->base_addr + TLAN_DIO_DATA + 2 );
	code    = inb( dev->base_addr + TLAN_DIO_DATA + 3 );

	outw( TLAN_MULTICOL_FRMS, dev->base_addr + TLAN_DIO_ADR );
	multi_col   = inb( dev->base_addr + TLAN_DIO_DATA );
	multi_col  += inb( dev->base_addr + TLAN_DIO_DATA + 1 ) << 8;
	single_col  = inb( dev->base_addr + TLAN_DIO_DATA + 2 );
	single_col += inb( dev->base_addr + TLAN_DIO_DATA + 3 ) << 8;

	outw( TLAN_EXCESSCOL_FRMS, dev->base_addr + TLAN_DIO_ADR );
	excess_col = inb( dev->base_addr + TLAN_DIO_DATA );
	late_col   = inb( dev->base_addr + TLAN_DIO_DATA + 1 );
	loss       = inb( dev->base_addr + TLAN_DIO_DATA + 2 );

	if ( record ) {
		dev->stats.rx_packets += rx_good;
		dev->stats.rx_errors  += rx_over + crc + code;
		dev->stats.tx_packets += tx_good;
		dev->stats.tx_errors  += tx_under + loss;
		dev->stats.collisions += multi_col + single_col + excess_col + late_col;

		dev->stats.rx_over_errors    += rx_over;
		dev->stats.rx_crc_errors     += crc;
		dev->stats.rx_frame_errors   += code;

		dev->stats.tx_aborted_errors += tx_under;
		dev->stats.tx_carrier_errors += loss;
	}

} /* TLan_ReadAndClearStats */




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
TLan_ResetAdapter( struct net_device *dev )
{
	TLanPrivateInfo	*priv = netdev_priv(dev);
	int		i;
	u32		addr;
	u32		data;
	u8		data8;

	priv->tlanFullDuplex = FALSE;
	priv->phyOnline=0;
	netif_carrier_off(dev);

/*  1.	Assert reset bit. */

	data = inl(dev->base_addr + TLAN_HOST_CMD);
	data |= TLAN_HC_AD_RST;
	outl(data, dev->base_addr + TLAN_HOST_CMD);

	udelay(1000);

/*  2.	Turn off interrupts. ( Probably isn't necessary ) */

	data = inl(dev->base_addr + TLAN_HOST_CMD);
	data |= TLAN_HC_INT_OFF;
	outl(data, dev->base_addr + TLAN_HOST_CMD);

/*  3.	Clear AREGs and HASHs. */

 	for ( i = TLAN_AREG_0; i <= TLAN_HASH_2; i += 4 ) {
		TLan_DioWrite32( dev->base_addr, (u16) i, 0 );
	}

/*  4.	Setup NetConfig register. */

	data = TLAN_NET_CFG_1FRAG | TLAN_NET_CFG_1CHAN | TLAN_NET_CFG_PHY_EN;
	TLan_DioWrite16( dev->base_addr, TLAN_NET_CONFIG, (u16) data );

/*  5.	Load Ld_Tmr and Ld_Thr in HOST_CMD. */

 	outl( TLAN_HC_LD_TMR | 0x3f, dev->base_addr + TLAN_HOST_CMD );
 	outl( TLAN_HC_LD_THR | 0x9, dev->base_addr + TLAN_HOST_CMD );

/*  6.	Unreset the MII by setting NMRST (in NetSio) to 1. */

	outw( TLAN_NET_SIO, dev->base_addr + TLAN_DIO_ADR );
	addr = dev->base_addr + TLAN_DIO_DATA + TLAN_NET_SIO;
	TLan_SetBit( TLAN_NET_SIO_NMRST, addr );

/*  7.	Setup the remaining registers. */

	if ( priv->tlanRev >= 0x30 ) {
		data8 = TLAN_ID_TX_EOC | TLAN_ID_RX_EOC;
		TLan_DioWrite8( dev->base_addr, TLAN_INT_DIS, data8 );
	}
	TLan_PhyDetect( dev );
	data = TLAN_NET_CFG_1FRAG | TLAN_NET_CFG_1CHAN;

	if ( priv->adapter->flags & TLAN_ADAPTER_BIT_RATE_PHY ) {
		data |= TLAN_NET_CFG_BIT;
		if ( priv->aui == 1 ) {
			TLan_DioWrite8( dev->base_addr, TLAN_ACOMMIT, 0x0a );
		} else if ( priv->duplex == TLAN_DUPLEX_FULL ) {
			TLan_DioWrite8( dev->base_addr, TLAN_ACOMMIT, 0x00 );
			priv->tlanFullDuplex = TRUE;
		} else {
			TLan_DioWrite8( dev->base_addr, TLAN_ACOMMIT, 0x08 );
		}
	}

	if ( priv->phyNum == 0 ) {
		data |= TLAN_NET_CFG_PHY_EN;
	}
	TLan_DioWrite16( dev->base_addr, TLAN_NET_CONFIG, (u16) data );

	if ( priv->adapter->flags & TLAN_ADAPTER_UNMANAGED_PHY ) {
		TLan_FinishReset( dev );
	} else {
		TLan_PhyPowerDown( dev );
	}

} /* TLan_ResetAdapter */




static void
TLan_FinishReset( struct net_device *dev )
{
	TLanPrivateInfo	*priv = netdev_priv(dev);
	u8		data;
	u32		phy;
	u8		sio;
	u16		status;
	u16		partner;
	u16		tlphy_ctl;
	u16 		tlphy_par;
	u16		tlphy_id1, tlphy_id2;
	int 		i;

	phy = priv->phy[priv->phyNum];

	data = TLAN_NET_CMD_NRESET | TLAN_NET_CMD_NWRAP;
	if ( priv->tlanFullDuplex ) {
		data |= TLAN_NET_CMD_DUPLEX;
	}
	TLan_DioWrite8( dev->base_addr, TLAN_NET_CMD, data );
	data = TLAN_NET_MASK_MASK4 | TLAN_NET_MASK_MASK5;
	if ( priv->phyNum == 0 ) {
		data |= TLAN_NET_MASK_MASK7;
	}
	TLan_DioWrite8( dev->base_addr, TLAN_NET_MASK, data );
	TLan_DioWrite16( dev->base_addr, TLAN_MAX_RX, ((1536)+7)&~7 );
	TLan_MiiReadReg( dev, phy, MII_GEN_ID_HI, &tlphy_id1 );
	TLan_MiiReadReg( dev, phy, MII_GEN_ID_LO, &tlphy_id2 );

	if ( ( priv->adapter->flags & TLAN_ADAPTER_UNMANAGED_PHY ) ||
	     ( priv->aui ) ) {
		status = MII_GS_LINK;
		printk( "TLAN:  %s: Link forced.\n", dev->name );
	} else {
		TLan_MiiReadReg( dev, phy, MII_GEN_STS, &status );
		udelay( 1000 );
		TLan_MiiReadReg( dev, phy, MII_GEN_STS, &status );
		if ( (status & MII_GS_LINK) &&
		     /* We only support link info on Nat.Sem. PHY's */
			(tlphy_id1 == NAT_SEM_ID1) &&
			(tlphy_id2 == NAT_SEM_ID2) ) {
			TLan_MiiReadReg( dev, phy, MII_AN_LPA, &partner );
			TLan_MiiReadReg( dev, phy, TLAN_TLPHY_PAR, &tlphy_par );

			printk( "TLAN: %s: Link active with ", dev->name );
			if (!(tlphy_par & TLAN_PHY_AN_EN_STAT)) {
			      	 printk( "forced 10%sMbps %s-Duplex\n",
					 tlphy_par & TLAN_PHY_SPEED_100 ? "" : "0",
					 tlphy_par & TLAN_PHY_DUPLEX_FULL ? "Full" : "Half");
			} else {
				printk( "AutoNegotiation enabled, at 10%sMbps %s-Duplex\n",
					tlphy_par & TLAN_PHY_SPEED_100 ? "" : "0",
					tlphy_par & TLAN_PHY_DUPLEX_FULL ? "Full" : "Half");
				printk("TLAN: Partner capability: ");
					for (i = 5; i <= 10; i++)
						if (partner & (1<<i))
							printk("%s",media[i-5]);
				printk("\n");
			}

			TLan_DioWrite8( dev->base_addr, TLAN_LED_REG, TLAN_LED_LINK );
#ifdef MONITOR
			/* We have link beat..for now anyway */
	        	priv->link = 1;
	        	/*Enabling link beat monitoring */
			TLan_SetTimer( dev, (10*HZ), TLAN_TIMER_LINK_BEAT );
#endif
		} else if (status & MII_GS_LINK)  {
			printk( "TLAN: %s: Link active\n", dev->name );
			TLan_DioWrite8( dev->base_addr, TLAN_LED_REG, TLAN_LED_LINK );
		}
	}

	if ( priv->phyNum == 0 ) {
        	TLan_MiiReadReg( dev, phy, TLAN_TLPHY_CTL, &tlphy_ctl );
        	tlphy_ctl |= TLAN_TC_INTEN;
        	TLan_MiiWriteReg( dev, phy, TLAN_TLPHY_CTL, tlphy_ctl );
        	sio = TLan_DioRead8( dev->base_addr, TLAN_NET_SIO );
        	sio |= TLAN_NET_SIO_MINTEN;
        	TLan_DioWrite8( dev->base_addr, TLAN_NET_SIO, sio );
	}

	if ( status & MII_GS_LINK ) {
		TLan_SetMac( dev, 0, dev->dev_addr );
		priv->phyOnline = 1;
		outb( ( TLAN_HC_INT_ON >> 8 ), dev->base_addr + TLAN_HOST_CMD + 1 );
		if ( debug >= 1 && debug != TLAN_DEBUG_PROBE ) {
			outb( ( TLAN_HC_REQ_INT >> 8 ), dev->base_addr + TLAN_HOST_CMD + 1 );
		}
		outl( priv->rxListDMA, dev->base_addr + TLAN_CH_PARM );
		outl( TLAN_HC_GO | TLAN_HC_RT, dev->base_addr + TLAN_HOST_CMD );
		netif_carrier_on(dev);
	} else {
		printk( "TLAN: %s: Link inactive, will retry in 10 secs...\n",
			dev->name );
		TLan_SetTimer( dev, (10*HZ), TLAN_TIMER_FINISH_RESET );
		return;
	}
	TLan_SetMulticastList(dev);

} /* TLan_FinishReset */




	/***************************************************************
	 *	TLan_SetMac
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

static void TLan_SetMac( struct net_device *dev, int areg, char *mac )
{
	int i;

	areg *= 6;

	if ( mac != NULL ) {
		for ( i = 0; i < 6; i++ )
			TLan_DioWrite8( dev->base_addr,
					TLAN_AREG_0 + areg + i, mac[i] );
	} else {
		for ( i = 0; i < 6; i++ )
			TLan_DioWrite8( dev->base_addr,
					TLAN_AREG_0 + areg + i, 0 );
	}

} /* TLan_SetMac */




/*****************************************************************************
******************************************************************************

	ThunderLAN Driver PHY Layer Routines

******************************************************************************
*****************************************************************************/



	/*********************************************************************
	 *	TLan_PhyPrint
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

static void TLan_PhyPrint( struct net_device *dev )
{
	TLanPrivateInfo *priv = netdev_priv(dev);
	u16 i, data0, data1, data2, data3, phy;

	phy = priv->phy[priv->phyNum];

	if ( priv->adapter->flags & TLAN_ADAPTER_UNMANAGED_PHY ) {
		printk( "TLAN:   Device %s, Unmanaged PHY.\n", dev->name );
	} else if ( phy <= TLAN_PHY_MAX_ADDR ) {
		printk( "TLAN:   Device %s, PHY 0x%02x.\n", dev->name, phy );
		printk( "TLAN:      Off.  +0     +1     +2     +3 \n" );
                for ( i = 0; i < 0x20; i+= 4 ) {
			printk( "TLAN:      0x%02x", i );
			TLan_MiiReadReg( dev, phy, i, &data0 );
			printk( " 0x%04hx", data0 );
			TLan_MiiReadReg( dev, phy, i + 1, &data1 );
			printk( " 0x%04hx", data1 );
			TLan_MiiReadReg( dev, phy, i + 2, &data2 );
			printk( " 0x%04hx", data2 );
			TLan_MiiReadReg( dev, phy, i + 3, &data3 );
			printk( " 0x%04hx\n", data3 );
		}
	} else {
		printk( "TLAN:   Device %s, Invalid PHY.\n", dev->name );
	}

} /* TLan_PhyPrint */




	/*********************************************************************
	 *	TLan_PhyDetect
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

static void TLan_PhyDetect( struct net_device *dev )
{
	TLanPrivateInfo *priv = netdev_priv(dev);
	u16		control;
	u16		hi;
	u16		lo;
	u32		phy;

	if ( priv->adapter->flags & TLAN_ADAPTER_UNMANAGED_PHY ) {
		priv->phyNum = 0xFFFF;
		return;
	}

	TLan_MiiReadReg( dev, TLAN_PHY_MAX_ADDR, MII_GEN_ID_HI, &hi );

	if ( hi != 0xFFFF ) {
		priv->phy[0] = TLAN_PHY_MAX_ADDR;
	} else {
		priv->phy[0] = TLAN_PHY_NONE;
	}

	priv->phy[1] = TLAN_PHY_NONE;
	for ( phy = 0; phy <= TLAN_PHY_MAX_ADDR; phy++ ) {
		TLan_MiiReadReg( dev, phy, MII_GEN_CTL, &control );
		TLan_MiiReadReg( dev, phy, MII_GEN_ID_HI, &hi );
		TLan_MiiReadReg( dev, phy, MII_GEN_ID_LO, &lo );
		if ( ( control != 0xFFFF ) ||
		     ( hi != 0xFFFF ) || ( lo != 0xFFFF ) ) {
			TLAN_DBG( TLAN_DEBUG_GNRL,
				  "PHY found at %02x %04x %04x %04x\n",
				  phy, control, hi, lo );
			if ( ( priv->phy[1] == TLAN_PHY_NONE ) &&
			     ( phy != TLAN_PHY_MAX_ADDR ) ) {
				priv->phy[1] = phy;
			}
		}
	}

	if ( priv->phy[1] != TLAN_PHY_NONE ) {
		priv->phyNum = 1;
	} else if ( priv->phy[0] != TLAN_PHY_NONE ) {
		priv->phyNum = 0;
	} else {
		printk( "TLAN:  Cannot initialize device, no PHY was found!\n" );
	}

} /* TLan_PhyDetect */




static void TLan_PhyPowerDown( struct net_device *dev )
{
	TLanPrivateInfo	*priv = netdev_priv(dev);
	u16		value;

	TLAN_DBG( TLAN_DEBUG_GNRL, "%s: Powering down PHY(s).\n", dev->name );
	value = MII_GC_PDOWN | MII_GC_LOOPBK | MII_GC_ISOLATE;
	TLan_MiiSync( dev->base_addr );
	TLan_MiiWriteReg( dev, priv->phy[priv->phyNum], MII_GEN_CTL, value );
	if ( ( priv->phyNum == 0 ) &&
	     ( priv->phy[1] != TLAN_PHY_NONE ) &&
	     ( ! ( priv->adapter->flags & TLAN_ADAPTER_USE_INTERN_10 ) ) ) {
		TLan_MiiSync( dev->base_addr );
		TLan_MiiWriteReg( dev, priv->phy[1], MII_GEN_CTL, value );
	}

	/* Wait for 50 ms and powerup
	 * This is abitrary.  It is intended to make sure the
	 * transceiver settles.
	 */
	TLan_SetTimer( dev, (HZ/20), TLAN_TIMER_PHY_PUP );

} /* TLan_PhyPowerDown */




static void TLan_PhyPowerUp( struct net_device *dev )
{
	TLanPrivateInfo	*priv = netdev_priv(dev);
	u16		value;

	TLAN_DBG( TLAN_DEBUG_GNRL, "%s: Powering up PHY.\n", dev->name );
	TLan_MiiSync( dev->base_addr );
	value = MII_GC_LOOPBK;
	TLan_MiiWriteReg( dev, priv->phy[priv->phyNum], MII_GEN_CTL, value );
	TLan_MiiSync(dev->base_addr);
	/* Wait for 500 ms and reset the
	 * transceiver.  The TLAN docs say both 50 ms and
	 * 500 ms, so do the longer, just in case.
	 */
	TLan_SetTimer( dev, (HZ/20), TLAN_TIMER_PHY_RESET );

} /* TLan_PhyPowerUp */




static void TLan_PhyReset( struct net_device *dev )
{
	TLanPrivateInfo	*priv = netdev_priv(dev);
	u16		phy;
	u16		value;

	phy = priv->phy[priv->phyNum];

	TLAN_DBG( TLAN_DEBUG_GNRL, "%s: Reseting PHY.\n", dev->name );
	TLan_MiiSync( dev->base_addr );
	value = MII_GC_LOOPBK | MII_GC_RESET;
	TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, value );
	TLan_MiiReadReg( dev, phy, MII_GEN_CTL, &value );
	while ( value & MII_GC_RESET ) {
		TLan_MiiReadReg( dev, phy, MII_GEN_CTL, &value );
	}

	/* Wait for 500 ms and initialize.
	 * I don't remember why I wait this long.
	 * I've changed this to 50ms, as it seems long enough.
	 */
	TLan_SetTimer( dev, (HZ/20), TLAN_TIMER_PHY_START_LINK );

} /* TLan_PhyReset */




static void TLan_PhyStartLink( struct net_device *dev )
{
	TLanPrivateInfo	*priv = netdev_priv(dev);
	u16		ability;
	u16		control;
	u16		data;
	u16		phy;
	u16		status;
	u16		tctl;

	phy = priv->phy[priv->phyNum];
	TLAN_DBG( TLAN_DEBUG_GNRL, "%s: Trying to activate link.\n", dev->name );
	TLan_MiiReadReg( dev, phy, MII_GEN_STS, &status );
	TLan_MiiReadReg( dev, phy, MII_GEN_STS, &ability );

	if ( ( status & MII_GS_AUTONEG ) &&
	     ( ! priv->aui ) ) {
		ability = status >> 11;
		if ( priv->speed  == TLAN_SPEED_10 &&
		     priv->duplex == TLAN_DUPLEX_HALF) {
			TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, 0x0000);
		} else if ( priv->speed == TLAN_SPEED_10 &&
			    priv->duplex == TLAN_DUPLEX_FULL) {
			priv->tlanFullDuplex = TRUE;
			TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, 0x0100);
		} else if ( priv->speed == TLAN_SPEED_100 &&
			    priv->duplex == TLAN_DUPLEX_HALF) {
			TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, 0x2000);
		} else if ( priv->speed == TLAN_SPEED_100 &&
			    priv->duplex == TLAN_DUPLEX_FULL) {
			priv->tlanFullDuplex = TRUE;
			TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, 0x2100);
		} else {

			/* Set Auto-Neg advertisement */
			TLan_MiiWriteReg( dev, phy, MII_AN_ADV, (ability << 5) | 1);
			/* Enablee Auto-Neg */
			TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, 0x1000 );
			/* Restart Auto-Neg */
			TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, 0x1200 );
			/* Wait for 4 sec for autonegotiation
		 	* to complete.  The max spec time is less than this
		 	* but the card need additional time to start AN.
		 	* .5 sec should be plenty extra.
		 	*/
			printk( "TLAN: %s: Starting autonegotiation.\n", dev->name );
			TLan_SetTimer( dev, (2*HZ), TLAN_TIMER_PHY_FINISH_AN );
			return;
		}

	}

	if ( ( priv->aui ) && ( priv->phyNum != 0 ) ) {
		priv->phyNum = 0;
		data = TLAN_NET_CFG_1FRAG | TLAN_NET_CFG_1CHAN | TLAN_NET_CFG_PHY_EN;
		TLan_DioWrite16( dev->base_addr, TLAN_NET_CONFIG, data );
		TLan_SetTimer( dev, (40*HZ/1000), TLAN_TIMER_PHY_PDOWN );
		return;
	}  else if ( priv->phyNum == 0 ) {
		control = 0;
        	TLan_MiiReadReg( dev, phy, TLAN_TLPHY_CTL, &tctl );
		if ( priv->aui ) {
                	tctl |= TLAN_TC_AUISEL;
		} else {
                	tctl &= ~TLAN_TC_AUISEL;
			if ( priv->duplex == TLAN_DUPLEX_FULL ) {
				control |= MII_GC_DUPLEX;
				priv->tlanFullDuplex = TRUE;
			}
			if ( priv->speed == TLAN_SPEED_100 ) {
				control |= MII_GC_SPEEDSEL;
			}
		}
		TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, control );
        	TLan_MiiWriteReg( dev, phy, TLAN_TLPHY_CTL, tctl );
	}

	/* Wait for 2 sec to give the transceiver time
	 * to establish link.
	 */
	TLan_SetTimer( dev, (4*HZ), TLAN_TIMER_FINISH_RESET );

} /* TLan_PhyStartLink */




static void TLan_PhyFinishAutoNeg( struct net_device *dev )
{
	TLanPrivateInfo	*priv = netdev_priv(dev);
	u16		an_adv;
	u16		an_lpa;
	u16		data;
	u16		mode;
	u16		phy;
	u16		status;

	phy = priv->phy[priv->phyNum];

	TLan_MiiReadReg( dev, phy, MII_GEN_STS, &status );
	udelay( 1000 );
	TLan_MiiReadReg( dev, phy, MII_GEN_STS, &status );

	if ( ! ( status & MII_GS_AUTOCMPLT ) ) {
		/* Wait for 8 sec to give the process
		 * more time.  Perhaps we should fail after a while.
		 */
		 if (!priv->neg_be_verbose++) {
			 pr_info("TLAN:  Giving autonegotiation more time.\n");
		 	 pr_info("TLAN:  Please check that your adapter has\n");
		 	 pr_info("TLAN:  been properly connected to a HUB or Switch.\n");
			 pr_info("TLAN:  Trying to establish link in the background...\n");
		 }
		TLan_SetTimer( dev, (8*HZ), TLAN_TIMER_PHY_FINISH_AN );
		return;
	}

	printk( "TLAN: %s: Autonegotiation complete.\n", dev->name );
	TLan_MiiReadReg( dev, phy, MII_AN_ADV, &an_adv );
	TLan_MiiReadReg( dev, phy, MII_AN_LPA, &an_lpa );
	mode = an_adv & an_lpa & 0x03E0;
	if ( mode & 0x0100 ) {
		priv->tlanFullDuplex = TRUE;
	} else if ( ! ( mode & 0x0080 ) && ( mode & 0x0040 ) ) {
		priv->tlanFullDuplex = TRUE;
	}

	if ( ( ! ( mode & 0x0180 ) ) &&
	     ( priv->adapter->flags & TLAN_ADAPTER_USE_INTERN_10 ) &&
	     ( priv->phyNum != 0 ) ) {
		priv->phyNum = 0;
		data = TLAN_NET_CFG_1FRAG | TLAN_NET_CFG_1CHAN | TLAN_NET_CFG_PHY_EN;
		TLan_DioWrite16( dev->base_addr, TLAN_NET_CONFIG, data );
		TLan_SetTimer( dev, (400*HZ/1000), TLAN_TIMER_PHY_PDOWN );
		return;
	}

	if ( priv->phyNum == 0 ) {
		if ( ( priv->duplex == TLAN_DUPLEX_FULL ) ||
		     ( an_adv & an_lpa & 0x0040 ) ) {
			TLan_MiiWriteReg( dev, phy, MII_GEN_CTL,
					  MII_GC_AUTOENB | MII_GC_DUPLEX );
			pr_info("TLAN:  Starting internal PHY with FULL-DUPLEX\n" );
		} else {
			TLan_MiiWriteReg( dev, phy, MII_GEN_CTL, MII_GC_AUTOENB );
			pr_info( "TLAN:  Starting internal PHY with HALF-DUPLEX\n" );
		}
	}

	/* Wait for 100 ms.  No reason in partiticular.
	 */
	TLan_SetTimer( dev, (HZ/10), TLAN_TIMER_FINISH_RESET );

} /* TLan_PhyFinishAutoNeg */

#ifdef MONITOR

        /*********************************************************************
        *
        *      TLan_phyMonitor
        *
        *      Returns:
        *              None
        *
        *      Params:
        *              dev             The device structure of this device.
        *
        *
        *      This function monitors PHY condition by reading the status
        *      register via the MII bus. This can be used to give info
        *      about link changes (up/down), and possible switch to alternate
        *      media.
        *
        * ******************************************************************/

void TLan_PhyMonitor( struct net_device *dev )
{
	TLanPrivateInfo *priv = netdev_priv(dev);
	u16     phy;
	u16     phy_status;

	phy = priv->phy[priv->phyNum];

        /* Get PHY status register */
        TLan_MiiReadReg( dev, phy, MII_GEN_STS, &phy_status );

        /* Check if link has been lost */
        if (!(phy_status & MII_GS_LINK)) {
 	       if (priv->link) {
		      priv->link = 0;
	              printk(KERN_DEBUG "TLAN: %s has lost link\n", dev->name);
		      netif_carrier_off(dev);
		      TLan_SetTimer( dev, (2*HZ), TLAN_TIMER_LINK_BEAT );
		      return;
		}
	}

        /* Link restablished? */
        if ((phy_status & MII_GS_LINK) && !priv->link) {
 		priv->link = 1;
        	printk(KERN_DEBUG "TLAN: %s has reestablished link\n", dev->name);
		netif_carrier_on(dev);
        }

	/* Setup a new monitor */
	TLan_SetTimer( dev, (2*HZ), TLAN_TIMER_LINK_BEAT );
}

#endif /* MONITOR */


/*****************************************************************************
******************************************************************************

	ThunderLAN Driver MII Routines

	These routines are based on the information in Chap. 2 of the
	"ThunderLAN Programmer's Guide", pp. 15-24.

******************************************************************************
*****************************************************************************/


	/***************************************************************
	 *	TLan_MiiReadReg
	 *
	 *	Returns:
	 *		0	if ack received ok
	 *		1	otherwise.
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

static int TLan_MiiReadReg( struct net_device *dev, u16 phy, u16 reg, u16 *val )
{
	u8	nack;
	u16	sio, tmp;
 	u32	i;
	int	err;
	int	minten;
	TLanPrivateInfo *priv = netdev_priv(dev);
	unsigned long flags = 0;

	err = FALSE;
	outw(TLAN_NET_SIO, dev->base_addr + TLAN_DIO_ADR);
	sio = dev->base_addr + TLAN_DIO_DATA + TLAN_NET_SIO;

	if (!in_irq())
		spin_lock_irqsave(&priv->lock, flags);

	TLan_MiiSync(dev->base_addr);

	minten = TLan_GetBit( TLAN_NET_SIO_MINTEN, sio );
	if ( minten )
		TLan_ClearBit(TLAN_NET_SIO_MINTEN, sio);

	TLan_MiiSendData( dev->base_addr, 0x1, 2 );	/* Start ( 01b ) */
	TLan_MiiSendData( dev->base_addr, 0x2, 2 );	/* Read  ( 10b ) */
	TLan_MiiSendData( dev->base_addr, phy, 5 );	/* Device #      */
	TLan_MiiSendData( dev->base_addr, reg, 5 );	/* Register #    */


	TLan_ClearBit(TLAN_NET_SIO_MTXEN, sio);		/* Change direction */

	TLan_ClearBit(TLAN_NET_SIO_MCLK, sio);		/* Clock Idle bit */
	TLan_SetBit(TLAN_NET_SIO_MCLK, sio);
	TLan_ClearBit(TLAN_NET_SIO_MCLK, sio);		/* Wait 300ns */

	nack = TLan_GetBit(TLAN_NET_SIO_MDATA, sio);	/* Check for ACK */
	TLan_SetBit(TLAN_NET_SIO_MCLK, sio);		/* Finish ACK */
	if (nack) {					/* No ACK, so fake it */
		for (i = 0; i < 16; i++) {
			TLan_ClearBit(TLAN_NET_SIO_MCLK, sio);
			TLan_SetBit(TLAN_NET_SIO_MCLK, sio);
		}
		tmp = 0xffff;
		err = TRUE;
	} else {					/* ACK, so read data */
		for (tmp = 0, i = 0x8000; i; i >>= 1) {
			TLan_ClearBit(TLAN_NET_SIO_MCLK, sio);
			if (TLan_GetBit(TLAN_NET_SIO_MDATA, sio))
				tmp |= i;
			TLan_SetBit(TLAN_NET_SIO_MCLK, sio);
		}
	}


	TLan_ClearBit(TLAN_NET_SIO_MCLK, sio);		/* Idle cycle */
	TLan_SetBit(TLAN_NET_SIO_MCLK, sio);

	if ( minten )
		TLan_SetBit(TLAN_NET_SIO_MINTEN, sio);

	*val = tmp;

	if (!in_irq())
		spin_unlock_irqrestore(&priv->lock, flags);

	return err;

} /* TLan_MiiReadReg */




	/***************************************************************
	 *	TLan_MiiSendData
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

static void TLan_MiiSendData( u16 base_port, u32 data, unsigned num_bits )
{
	u16 sio;
	u32 i;

	if ( num_bits == 0 )
		return;

	outw( TLAN_NET_SIO, base_port + TLAN_DIO_ADR );
	sio = base_port + TLAN_DIO_DATA + TLAN_NET_SIO;
	TLan_SetBit( TLAN_NET_SIO_MTXEN, sio );

	for ( i = ( 0x1 << ( num_bits - 1 ) ); i; i >>= 1 ) {
		TLan_ClearBit( TLAN_NET_SIO_MCLK, sio );
		(void) TLan_GetBit( TLAN_NET_SIO_MCLK, sio );
		if ( data & i )
			TLan_SetBit( TLAN_NET_SIO_MDATA, sio );
		else
			TLan_ClearBit( TLAN_NET_SIO_MDATA, sio );
		TLan_SetBit( TLAN_NET_SIO_MCLK, sio );
		(void) TLan_GetBit( TLAN_NET_SIO_MCLK, sio );
	}

} /* TLan_MiiSendData */




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

static void TLan_MiiSync( u16 base_port )
{
	int i;
	u16 sio;

	outw( TLAN_NET_SIO, base_port + TLAN_DIO_ADR );
	sio = base_port + TLAN_DIO_DATA + TLAN_NET_SIO;

	TLan_ClearBit( TLAN_NET_SIO_MTXEN, sio );
	for ( i = 0; i < 32; i++ ) {
		TLan_ClearBit( TLAN_NET_SIO_MCLK, sio );
		TLan_SetBit( TLAN_NET_SIO_MCLK, sio );
	}

} /* TLan_MiiSync */




	/***************************************************************
	 *	TLan_MiiWriteReg
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

static void TLan_MiiWriteReg( struct net_device *dev, u16 phy, u16 reg, u16 val )
{
	u16	sio;
	int	minten;
	unsigned long flags = 0;
	TLanPrivateInfo *priv = netdev_priv(dev);

	outw(TLAN_NET_SIO, dev->base_addr + TLAN_DIO_ADR);
	sio = dev->base_addr + TLAN_DIO_DATA + TLAN_NET_SIO;

	if (!in_irq())
		spin_lock_irqsave(&priv->lock, flags);

	TLan_MiiSync( dev->base_addr );

	minten = TLan_GetBit( TLAN_NET_SIO_MINTEN, sio );
	if ( minten )
		TLan_ClearBit( TLAN_NET_SIO_MINTEN, sio );

	TLan_MiiSendData( dev->base_addr, 0x1, 2 );	/* Start ( 01b ) */
	TLan_MiiSendData( dev->base_addr, 0x1, 2 );	/* Write ( 01b ) */
	TLan_MiiSendData( dev->base_addr, phy, 5 );	/* Device #      */
	TLan_MiiSendData( dev->base_addr, reg, 5 );	/* Register #    */

	TLan_MiiSendData( dev->base_addr, 0x2, 2 );	/* Send ACK */
	TLan_MiiSendData( dev->base_addr, val, 16 );	/* Send Data */

	TLan_ClearBit( TLAN_NET_SIO_MCLK, sio );	/* Idle cycle */
	TLan_SetBit( TLAN_NET_SIO_MCLK, sio );

	if ( minten )
		TLan_SetBit( TLAN_NET_SIO_MINTEN, sio );

	if (!in_irq())
		spin_unlock_irqrestore(&priv->lock, flags);

} /* TLan_MiiWriteReg */




/*****************************************************************************
******************************************************************************

	ThunderLAN Driver Eeprom routines

	The Compaq Netelligent 10 and 10/100 cards use a Microchip 24C02A
	EEPROM.  These functions are based on information in Microchip's
	data sheet.  I don't know how well this functions will work with
	other EEPROMs.

******************************************************************************
*****************************************************************************/


	/***************************************************************
	 *	TLan_EeSendStart
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

static void TLan_EeSendStart( u16 io_base )
{
	u16	sio;

	outw( TLAN_NET_SIO, io_base + TLAN_DIO_ADR );
	sio = io_base + TLAN_DIO_DATA + TLAN_NET_SIO;

	TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
	TLan_SetBit( TLAN_NET_SIO_EDATA, sio );
	TLan_SetBit( TLAN_NET_SIO_ETXEN, sio );
	TLan_ClearBit( TLAN_NET_SIO_EDATA, sio );
	TLan_ClearBit( TLAN_NET_SIO_ECLOK, sio );

} /* TLan_EeSendStart */




	/***************************************************************
	 *	TLan_EeSendByte
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

static int TLan_EeSendByte( u16 io_base, u8 data, int stop )
{
	int	err;
	u8	place;
	u16	sio;

	outw( TLAN_NET_SIO, io_base + TLAN_DIO_ADR );
	sio = io_base + TLAN_DIO_DATA + TLAN_NET_SIO;

	/* Assume clock is low, tx is enabled; */
	for ( place = 0x80; place != 0; place >>= 1 ) {
		if ( place & data )
			TLan_SetBit( TLAN_NET_SIO_EDATA, sio );
		else
			TLan_ClearBit( TLAN_NET_SIO_EDATA, sio );
		TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
		TLan_ClearBit( TLAN_NET_SIO_ECLOK, sio );
	}
	TLan_ClearBit( TLAN_NET_SIO_ETXEN, sio );
	TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
	err = TLan_GetBit( TLAN_NET_SIO_EDATA, sio );
	TLan_ClearBit( TLAN_NET_SIO_ECLOK, sio );
	TLan_SetBit( TLAN_NET_SIO_ETXEN, sio );

	if ( ( ! err ) && stop ) {
		/* STOP, raise data while clock is high */
		TLan_ClearBit( TLAN_NET_SIO_EDATA, sio );
		TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
		TLan_SetBit( TLAN_NET_SIO_EDATA, sio );
	}

	return ( err );

} /* TLan_EeSendByte */




	/***************************************************************
	 *	TLan_EeReceiveByte
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

static void TLan_EeReceiveByte( u16 io_base, u8 *data, int stop )
{
	u8  place;
	u16 sio;

	outw( TLAN_NET_SIO, io_base + TLAN_DIO_ADR );
	sio = io_base + TLAN_DIO_DATA + TLAN_NET_SIO;
	*data = 0;

	/* Assume clock is low, tx is enabled; */
	TLan_ClearBit( TLAN_NET_SIO_ETXEN, sio );
	for ( place = 0x80; place; place >>= 1 ) {
		TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
		if ( TLan_GetBit( TLAN_NET_SIO_EDATA, sio ) )
			*data |= place;
		TLan_ClearBit( TLAN_NET_SIO_ECLOK, sio );
	}

	TLan_SetBit( TLAN_NET_SIO_ETXEN, sio );
	if ( ! stop ) {
		TLan_ClearBit( TLAN_NET_SIO_EDATA, sio );	/* Ack = 0 */
		TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
		TLan_ClearBit( TLAN_NET_SIO_ECLOK, sio );
	} else {
		TLan_SetBit( TLAN_NET_SIO_EDATA, sio );		/* No ack = 1 (?) */
		TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
		TLan_ClearBit( TLAN_NET_SIO_ECLOK, sio );
		/* STOP, raise data while clock is high */
		TLan_ClearBit( TLAN_NET_SIO_EDATA, sio );
		TLan_SetBit( TLAN_NET_SIO_ECLOK, sio );
		TLan_SetBit( TLAN_NET_SIO_EDATA, sio );
	}

} /* TLan_EeReceiveByte */




	/***************************************************************
	 *	TLan_EeReadByte
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

static int TLan_EeReadByte( struct net_device *dev, u8 ee_addr, u8 *data )
{
	int err;
	TLanPrivateInfo *priv = netdev_priv(dev);
	unsigned long flags = 0;
	int ret=0;

	spin_lock_irqsave(&priv->lock, flags);

	TLan_EeSendStart( dev->base_addr );
	err = TLan_EeSendByte( dev->base_addr, 0xA0, TLAN_EEPROM_ACK );
	if (err)
	{
		ret=1;
		goto fail;
	}
	err = TLan_EeSendByte( dev->base_addr, ee_addr, TLAN_EEPROM_ACK );
	if (err)
	{
		ret=2;
		goto fail;
	}
	TLan_EeSendStart( dev->base_addr );
	err = TLan_EeSendByte( dev->base_addr, 0xA1, TLAN_EEPROM_ACK );
	if (err)
	{
		ret=3;
		goto fail;
	}
	TLan_EeReceiveByte( dev->base_addr, data, TLAN_EEPROM_STOP );
fail:
	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;

} /* TLan_EeReadByte */



