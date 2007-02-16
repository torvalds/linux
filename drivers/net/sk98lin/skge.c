/******************************************************************************
 *
 * Name:	skge.c
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.45 $
 * Date:       	$Date: 2004/02/12 14:41:02 $
 * Purpose:	The main driver source module
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect GmbH.
 *	(C)Copyright 2002-2003 Marvell.
 *
 *	Driver for Marvell Yukon chipset and SysKonnect Gigabit Ethernet 
 *      Server Adapters.
 *
 *	Created 10-Feb-1999, based on Linux' acenic.c, 3c59x.c and
 *	SysKonnects GEnesis Solaris driver
 *	Author: Christoph Goos (cgoos@syskonnect.de)
 *	        Mirko Lindner (mlindner@syskonnect.de)
 *
 *	Address all question to: linux@syskonnect.de
 *
 *	The technical manual for the adapters is available from SysKonnect's
 *	web pages: www.syskonnect.com
 *	Goto "Support" and search Knowledge Base for "manual".
 *	
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/******************************************************************************
 *
 * Possible compiler options (#define xxx / -Dxxx):
 *
 *	debugging can be enable by changing SK_DEBUG_CHKMOD and
 *	SK_DEBUG_CHKCAT in makefile (described there).
 *
 ******************************************************************************/

/******************************************************************************
 *
 * Description:
 *
 *	This is the main module of the Linux GE driver.
 *	
 *	All source files except skge.c, skdrv1st.h, skdrv2nd.h and sktypes.h
 *	are part of SysKonnect's COMMON MODULES for the SK-98xx adapters.
 *	Those are used for drivers on multiple OS', so some thing may seem
 *	unnecessary complicated on Linux. Please do not try to 'clean up'
 *	them without VERY good reasons, because this will make it more
 *	difficult to keep the Linux driver in synchronisation with the
 *	other versions.
 *
 * Include file hierarchy:
 *
 *	<linux/module.h>
 *
 *	"h/skdrv1st.h"
 *		<linux/types.h>
 *		<linux/kernel.h>
 *		<linux/string.h>
 *		<linux/errno.h>
 *		<linux/ioport.h>
 *		<linux/slab.h>
 *		<linux/interrupt.h>
 *		<linux/pci.h>
 *		<linux/bitops.h>
 *		<asm/byteorder.h>
 *		<asm/io.h>
 *		<linux/netdevice.h>
 *		<linux/etherdevice.h>
 *		<linux/skbuff.h>
 *	    those three depending on kernel version used:
 *		<linux/bios32.h>
 *		<linux/init.h>
 *		<asm/uaccess.h>
 *		<net/checksum.h>
 *
 *		"h/skerror.h"
 *		"h/skdebug.h"
 *		"h/sktypes.h"
 *		"h/lm80.h"
 *		"h/xmac_ii.h"
 *
 *      "h/skdrv2nd.h"
 *		"h/skqueue.h"
 *		"h/skgehwt.h"
 *		"h/sktimer.h"
 *		"h/ski2c.h"
 *		"h/skgepnmi.h"
 *		"h/skvpd.h"
 *		"h/skgehw.h"
 *		"h/skgeinit.h"
 *		"h/skaddr.h"
 *		"h/skgesirq.h"
 *		"h/skrlmt.h"
 *
 ******************************************************************************/

#include	"h/skversion.h"

#include	<linux/in.h>
#include	<linux/module.h>
#include	<linux/moduleparam.h>
#include	<linux/init.h>
#include	<linux/dma-mapping.h>
#include	<linux/ip.h>
#include	<linux/mii.h>
#include	<linux/mm.h>

#include	"h/skdrv1st.h"
#include	"h/skdrv2nd.h"

/*******************************************************************************
 *
 * Defines
 *
 ******************************************************************************/

/* for debuging on x86 only */
/* #define BREAKPOINT() asm(" int $3"); */

/* use the transmit hw checksum driver functionality */
#define USE_SK_TX_CHECKSUM

/* use the receive hw checksum driver functionality */
#define USE_SK_RX_CHECKSUM

/* use the scatter-gather functionality with sendfile() */
#define SK_ZEROCOPY

/* use of a transmit complete interrupt */
#define USE_TX_COMPLETE

/*
 * threshold for copying small receive frames
 * set to 0 to avoid copying, set to 9001 to copy all frames
 */
#define SK_COPY_THRESHOLD	50

/* number of adapters that can be configured via command line params */
#define SK_MAX_CARD_PARAM	16



/*
 * use those defines for a compile-in version of the driver instead
 * of command line parameters
 */
// #define LINK_SPEED_A	{"Auto", }
// #define LINK_SPEED_B	{"Auto", }
// #define AUTO_NEG_A	{"Sense", }
// #define AUTO_NEG_B	{"Sense", }
// #define DUP_CAP_A	{"Both", }
// #define DUP_CAP_B	{"Both", }
// #define FLOW_CTRL_A	{"SymOrRem", }
// #define FLOW_CTRL_B	{"SymOrRem", }
// #define ROLE_A	{"Auto", }
// #define ROLE_B	{"Auto", }
// #define PREF_PORT	{"A", }
// #define CON_TYPE 	{"Auto", }
// #define RLMT_MODE	{"CheckLinkState", }

#define DEV_KFREE_SKB(skb) dev_kfree_skb(skb)
#define DEV_KFREE_SKB_IRQ(skb) dev_kfree_skb_irq(skb)
#define DEV_KFREE_SKB_ANY(skb) dev_kfree_skb_any(skb)


/* Set blink mode*/
#define OEM_CONFIG_VALUE (	SK_ACT_LED_BLINK | \
				SK_DUP_LED_NORMAL | \
				SK_LED_LINK100_ON)


/* Isr return value */
#define SkIsrRetVar	irqreturn_t
#define SkIsrRetNone	IRQ_NONE
#define SkIsrRetHandled	IRQ_HANDLED


/*******************************************************************************
 *
 * Local Function Prototypes
 *
 ******************************************************************************/

static void	FreeResources(struct SK_NET_DEVICE *dev);
static int	SkGeBoardInit(struct SK_NET_DEVICE *dev, SK_AC *pAC);
static SK_BOOL	BoardAllocMem(SK_AC *pAC);
static void	BoardFreeMem(SK_AC *pAC);
static void	BoardInitMem(SK_AC *pAC);
static void	SetupRing(SK_AC*, void*, uintptr_t, RXD**, RXD**, RXD**, int*, SK_BOOL);
static SkIsrRetVar	SkGeIsr(int irq, void *dev_id);
static SkIsrRetVar	SkGeIsrOnePort(int irq, void *dev_id);
static int	SkGeOpen(struct SK_NET_DEVICE *dev);
static int	SkGeClose(struct SK_NET_DEVICE *dev);
static int	SkGeXmit(struct sk_buff *skb, struct SK_NET_DEVICE *dev);
static int	SkGeSetMacAddr(struct SK_NET_DEVICE *dev, void *p);
static void	SkGeSetRxMode(struct SK_NET_DEVICE *dev);
static struct	net_device_stats *SkGeStats(struct SK_NET_DEVICE *dev);
static int	SkGeIoctl(struct SK_NET_DEVICE *dev, struct ifreq *rq, int cmd);
static void	GetConfiguration(SK_AC*);
static int	XmitFrame(SK_AC*, TX_PORT*, struct sk_buff*);
static void	FreeTxDescriptors(SK_AC*pAC, TX_PORT*);
static void	FillRxRing(SK_AC*, RX_PORT*);
static SK_BOOL	FillRxDescriptor(SK_AC*, RX_PORT*);
static void	ReceiveIrq(SK_AC*, RX_PORT*, SK_BOOL);
static void	ClearAndStartRx(SK_AC*, int);
static void	ClearTxIrq(SK_AC*, int, int);
static void	ClearRxRing(SK_AC*, RX_PORT*);
static void	ClearTxRing(SK_AC*, TX_PORT*);
static int	SkGeChangeMtu(struct SK_NET_DEVICE *dev, int new_mtu);
static void	PortReInitBmu(SK_AC*, int);
static int	SkGeIocMib(DEV_NET*, unsigned int, int);
static int	SkGeInitPCI(SK_AC *pAC);
static void	StartDrvCleanupTimer(SK_AC *pAC);
static void	StopDrvCleanupTimer(SK_AC *pAC);
static int	XmitFrameSG(SK_AC*, TX_PORT*, struct sk_buff*);

#ifdef SK_DIAG_SUPPORT
static SK_U32   ParseDeviceNbrFromSlotName(const char *SlotName);
static int      SkDrvInitAdapter(SK_AC *pAC, int devNbr);
static int      SkDrvDeInitAdapter(SK_AC *pAC, int devNbr);
#endif

/*******************************************************************************
 *
 * Extern Function Prototypes
 *
 ******************************************************************************/
extern void SkDimEnableModerationIfNeeded(SK_AC *pAC);	
extern void SkDimDisplayModerationSettings(SK_AC *pAC);
extern void SkDimStartModerationTimer(SK_AC *pAC);
extern void SkDimModerate(SK_AC *pAC);
extern void SkGeBlinkTimer(unsigned long data);

#ifdef DEBUG
static void	DumpMsg(struct sk_buff*, char*);
static void	DumpData(char*, int);
static void	DumpLong(char*, int);
#endif

/* global variables *********************************************************/
static SK_BOOL DoPrintInterfaceChange = SK_TRUE;
extern const struct ethtool_ops SkGeEthtoolOps;

/* local variables **********************************************************/
static uintptr_t TxQueueAddr[SK_MAX_MACS][2] = {{0x680, 0x600},{0x780, 0x700}};
static uintptr_t RxQueueAddr[SK_MAX_MACS] = {0x400, 0x480};

/*****************************************************************************
 *
 *	SkPciWriteCfgDWord - write a 32 bit value to pci config space
 *
 * Description:
 *	This routine writes a 32 bit value to the pci configuration
 *	space.
 *
 * Returns:
 *	0 - indicate everything worked ok.
 *	!= 0 - error indication
 */
static inline int SkPciWriteCfgDWord(
SK_AC *pAC,	/* Adapter Control structure pointer */
int PciAddr,		/* PCI register address */
SK_U32 Val)		/* pointer to store the read value */
{
	pci_write_config_dword(pAC->PciDev, PciAddr, Val);
	return(0);
} /* SkPciWriteCfgDWord */

/*****************************************************************************
 *
 * 	SkGeInitPCI - Init the PCI resources
 *
 * Description:
 *	This function initialize the PCI resources and IO
 *
 * Returns:
 *	0 - indicate everything worked ok.
 *	!= 0 - error indication
 */
static __devinit int SkGeInitPCI(SK_AC *pAC)
{
	struct SK_NET_DEVICE *dev = pAC->dev[0];
	struct pci_dev *pdev = pAC->PciDev;
	int retval;

	dev->mem_start = pci_resource_start (pdev, 0);
	pci_set_master(pdev);

	retval = pci_request_regions(pdev, "sk98lin");
	if (retval)
		goto out;

#ifdef SK_BIG_ENDIAN
	/*
	 * On big endian machines, we use the adapter's aibility of
	 * reading the descriptors as big endian.
	 */
	{
		SK_U32		our2;
		SkPciReadCfgDWord(pAC, PCI_OUR_REG_2, &our2);
		our2 |= PCI_REV_DESC;
		SkPciWriteCfgDWord(pAC, PCI_OUR_REG_2, our2);
	}
#endif

	/*
	 * Remap the regs into kernel space.
	 */
	pAC->IoBase = ioremap_nocache(dev->mem_start, 0x4000);
	if (!pAC->IoBase) {
		retval = -EIO;
		goto out_release;
	}

	return 0;

 out_release:
	pci_release_regions(pdev);
 out:
	return retval;
}


/*****************************************************************************
 *
 * 	FreeResources - release resources allocated for adapter
 *
 * Description:
 *	This function releases the IRQ, unmaps the IO and
 *	frees the desriptor ring.
 *
 * Returns: N/A
 *	
 */
static void FreeResources(struct SK_NET_DEVICE *dev)
{
SK_U32 AllocFlag;
DEV_NET		*pNet;
SK_AC		*pAC;

	pNet = netdev_priv(dev);
	pAC = pNet->pAC;
	AllocFlag = pAC->AllocFlag;
	if (pAC->PciDev) {
		pci_release_regions(pAC->PciDev);
	}
	if (AllocFlag & SK_ALLOC_IRQ) {
		free_irq(dev->irq, dev);
	}
	if (pAC->IoBase) {
		iounmap(pAC->IoBase);
	}
	if (pAC->pDescrMem) {
		BoardFreeMem(pAC);
	}
	
} /* FreeResources */

MODULE_AUTHOR("Mirko Lindner <mlindner@syskonnect.de>");
MODULE_DESCRIPTION("SysKonnect SK-NET Gigabit Ethernet SK-98xx driver");
MODULE_LICENSE("GPL");

#ifdef LINK_SPEED_A
static char *Speed_A[SK_MAX_CARD_PARAM] = LINK_SPEED;
#else
static char *Speed_A[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef LINK_SPEED_B
static char *Speed_B[SK_MAX_CARD_PARAM] = LINK_SPEED;
#else
static char *Speed_B[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef AUTO_NEG_A
static char *AutoNeg_A[SK_MAX_CARD_PARAM] = AUTO_NEG_A;
#else
static char *AutoNeg_A[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef DUP_CAP_A
static char *DupCap_A[SK_MAX_CARD_PARAM] = DUP_CAP_A;
#else
static char *DupCap_A[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef FLOW_CTRL_A
static char *FlowCtrl_A[SK_MAX_CARD_PARAM] = FLOW_CTRL_A;
#else
static char *FlowCtrl_A[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef ROLE_A
static char *Role_A[SK_MAX_CARD_PARAM] = ROLE_A;
#else
static char *Role_A[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef AUTO_NEG_B
static char *AutoNeg_B[SK_MAX_CARD_PARAM] = AUTO_NEG_B;
#else
static char *AutoNeg_B[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef DUP_CAP_B
static char *DupCap_B[SK_MAX_CARD_PARAM] = DUP_CAP_B;
#else
static char *DupCap_B[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef FLOW_CTRL_B
static char *FlowCtrl_B[SK_MAX_CARD_PARAM] = FLOW_CTRL_B;
#else
static char *FlowCtrl_B[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef ROLE_B
static char *Role_B[SK_MAX_CARD_PARAM] = ROLE_B;
#else
static char *Role_B[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef CON_TYPE
static char *ConType[SK_MAX_CARD_PARAM] = CON_TYPE;
#else
static char *ConType[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef PREF_PORT
static char *PrefPort[SK_MAX_CARD_PARAM] = PREF_PORT;
#else
static char *PrefPort[SK_MAX_CARD_PARAM] = {"", };
#endif

#ifdef RLMT_MODE
static char *RlmtMode[SK_MAX_CARD_PARAM] = RLMT_MODE;
#else
static char *RlmtMode[SK_MAX_CARD_PARAM] = {"", };
#endif

static int   IntsPerSec[SK_MAX_CARD_PARAM];
static char *Moderation[SK_MAX_CARD_PARAM];
static char *ModerationMask[SK_MAX_CARD_PARAM];
static char *AutoSizing[SK_MAX_CARD_PARAM];
static char *Stats[SK_MAX_CARD_PARAM];

module_param_array(Speed_A, charp, NULL, 0);
module_param_array(Speed_B, charp, NULL, 0);
module_param_array(AutoNeg_A, charp, NULL, 0);
module_param_array(AutoNeg_B, charp, NULL, 0);
module_param_array(DupCap_A, charp, NULL, 0);
module_param_array(DupCap_B, charp, NULL, 0);
module_param_array(FlowCtrl_A, charp, NULL, 0);
module_param_array(FlowCtrl_B, charp, NULL, 0);
module_param_array(Role_A, charp, NULL, 0);
module_param_array(Role_B, charp, NULL, 0);
module_param_array(ConType, charp, NULL, 0);
module_param_array(PrefPort, charp, NULL, 0);
module_param_array(RlmtMode, charp, NULL, 0);
/* used for interrupt moderation */
module_param_array(IntsPerSec, int, NULL, 0);
module_param_array(Moderation, charp, NULL, 0);
module_param_array(Stats, charp, NULL, 0);
module_param_array(ModerationMask, charp, NULL, 0);
module_param_array(AutoSizing, charp, NULL, 0);

/*****************************************************************************
 *
 * 	SkGeBoardInit - do level 0 and 1 initialization
 *
 * Description:
 *	This function prepares the board hardware for running. The desriptor
 *	ring is set up, the IRQ is allocated and the configuration settings
 *	are examined.
 *
 * Returns:
 *	0, if everything is ok
 *	!=0, on error
 */
static int __devinit SkGeBoardInit(struct SK_NET_DEVICE *dev, SK_AC *pAC)
{
short	i;
unsigned long Flags;
char	*DescrString = "sk98lin: Driver for Linux"; /* this is given to PNMI */
char	*VerStr	= VER_STRING;
int	Ret;			/* return code of request_irq */
SK_BOOL	DualNet;

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("IoBase: %08lX\n", (unsigned long)pAC->IoBase));
	for (i=0; i<SK_MAX_MACS; i++) {
		pAC->TxPort[i][0].HwAddr = pAC->IoBase + TxQueueAddr[i][0];
		pAC->TxPort[i][0].PortIndex = i;
		pAC->RxPort[i].HwAddr = pAC->IoBase + RxQueueAddr[i];
		pAC->RxPort[i].PortIndex = i;
	}

	/* Initialize the mutexes */
	for (i=0; i<SK_MAX_MACS; i++) {
		spin_lock_init(&pAC->TxPort[i][0].TxDesRingLock);
		spin_lock_init(&pAC->RxPort[i].RxDesRingLock);
	}
	spin_lock_init(&pAC->SlowPathLock);

	/* setup phy_id blink timer */
	pAC->BlinkTimer.function = SkGeBlinkTimer;
	pAC->BlinkTimer.data = (unsigned long) dev;
	init_timer(&pAC->BlinkTimer);

	/* level 0 init common modules here */
	
	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	/* Does a RESET on board ...*/
	if (SkGeInit(pAC, pAC->IoBase, SK_INIT_DATA) != 0) {
		printk("HWInit (0) failed.\n");
		spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
		return -EIO;
	}
	SkI2cInit(  pAC, pAC->IoBase, SK_INIT_DATA);
	SkEventInit(pAC, pAC->IoBase, SK_INIT_DATA);
	SkPnmiInit( pAC, pAC->IoBase, SK_INIT_DATA);
	SkAddrInit( pAC, pAC->IoBase, SK_INIT_DATA);
	SkRlmtInit( pAC, pAC->IoBase, SK_INIT_DATA);
	SkTimerInit(pAC, pAC->IoBase, SK_INIT_DATA);

	pAC->BoardLevel = SK_INIT_DATA;
	pAC->RxBufSize  = ETH_BUF_SIZE;

	SK_PNMI_SET_DRIVER_DESCR(pAC, DescrString);
	SK_PNMI_SET_DRIVER_VER(pAC, VerStr);

	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);

	/* level 1 init common modules here (HW init) */
	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	if (SkGeInit(pAC, pAC->IoBase, SK_INIT_IO) != 0) {
		printk("sk98lin: HWInit (1) failed.\n");
		spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
		return -EIO;
	}
	SkI2cInit(  pAC, pAC->IoBase, SK_INIT_IO);
	SkEventInit(pAC, pAC->IoBase, SK_INIT_IO);
	SkPnmiInit( pAC, pAC->IoBase, SK_INIT_IO);
	SkAddrInit( pAC, pAC->IoBase, SK_INIT_IO);
	SkRlmtInit( pAC, pAC->IoBase, SK_INIT_IO);
	SkTimerInit(pAC, pAC->IoBase, SK_INIT_IO);

	/* Set chipset type support */
	pAC->ChipsetType = 0;
	if ((pAC->GIni.GIChipId == CHIP_ID_YUKON) ||
		(pAC->GIni.GIChipId == CHIP_ID_YUKON_LITE)) {
		pAC->ChipsetType = 1;
	}

	GetConfiguration(pAC);
	if (pAC->RlmtNets == 2) {
		pAC->GIni.GIPortUsage = SK_MUL_LINK;
	}

	pAC->BoardLevel = SK_INIT_IO;
	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);

	if (pAC->GIni.GIMacsFound == 2) {
		 Ret = request_irq(dev->irq, SkGeIsr, IRQF_SHARED, "sk98lin", dev);
	} else if (pAC->GIni.GIMacsFound == 1) {
		Ret = request_irq(dev->irq, SkGeIsrOnePort, IRQF_SHARED,
			"sk98lin", dev);
	} else {
		printk(KERN_WARNING "sk98lin: Illegal number of ports: %d\n",
		       pAC->GIni.GIMacsFound);
		return -EIO;
	}

	if (Ret) {
		printk(KERN_WARNING "sk98lin: Requested IRQ %d is busy.\n",
		       dev->irq);
		return Ret;
	}
	pAC->AllocFlag |= SK_ALLOC_IRQ;

	/* Alloc memory for this board (Mem for RxD/TxD) : */
	if(!BoardAllocMem(pAC)) {
		printk("No memory for descriptor rings.\n");
		return -ENOMEM;
	}

	BoardInitMem(pAC);
	/* tschilling: New common function with minimum size check. */
	DualNet = SK_FALSE;
	if (pAC->RlmtNets == 2) {
		DualNet = SK_TRUE;
	}
	
	if (SkGeInitAssignRamToQueues(
		pAC,
		pAC->ActivePort,
		DualNet)) {
		BoardFreeMem(pAC);
		printk("sk98lin: SkGeInitAssignRamToQueues failed.\n");
		return -EIO;
	}

	return (0);
} /* SkGeBoardInit */


/*****************************************************************************
 *
 * 	BoardAllocMem - allocate the memory for the descriptor rings
 *
 * Description:
 *	This function allocates the memory for all descriptor rings.
 *	Each ring is aligned for the desriptor alignment and no ring
 *	has a 4 GByte boundary in it (because the upper 32 bit must
 *	be constant for all descriptiors in one rings).
 *
 * Returns:
 *	SK_TRUE, if all memory could be allocated
 *	SK_FALSE, if not
 */
static __devinit SK_BOOL BoardAllocMem(SK_AC	*pAC)
{
caddr_t		pDescrMem;	/* pointer to descriptor memory area */
size_t		AllocLength;	/* length of complete descriptor area */
int		i;		/* loop counter */
unsigned long	BusAddr;

	
	/* rings plus one for alignment (do not cross 4 GB boundary) */
	/* RX_RING_SIZE is assumed bigger than TX_RING_SIZE */
#if (BITS_PER_LONG == 32)
	AllocLength = (RX_RING_SIZE + TX_RING_SIZE) * pAC->GIni.GIMacsFound + 8;
#else
	AllocLength = (RX_RING_SIZE + TX_RING_SIZE) * pAC->GIni.GIMacsFound
		+ RX_RING_SIZE + 8;
#endif

	pDescrMem = pci_alloc_consistent(pAC->PciDev, AllocLength,
					 &pAC->pDescrMemDMA);

	if (pDescrMem == NULL) {
		return (SK_FALSE);
	}
	pAC->pDescrMem = pDescrMem;
	BusAddr = (unsigned long) pAC->pDescrMemDMA;

	/* Descriptors need 8 byte alignment, and this is ensured
	 * by pci_alloc_consistent.
	 */
	for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
			("TX%d/A: pDescrMem: %lX,   PhysDescrMem: %lX\n",
			i, (unsigned long) pDescrMem,
			BusAddr));
		pAC->TxPort[i][0].pTxDescrRing = pDescrMem;
		pAC->TxPort[i][0].VTxDescrRing = BusAddr;
		pDescrMem += TX_RING_SIZE;
		BusAddr += TX_RING_SIZE;
	
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
			("RX%d: pDescrMem: %lX,   PhysDescrMem: %lX\n",
			i, (unsigned long) pDescrMem,
			(unsigned long)BusAddr));
		pAC->RxPort[i].pRxDescrRing = pDescrMem;
		pAC->RxPort[i].VRxDescrRing = BusAddr;
		pDescrMem += RX_RING_SIZE;
		BusAddr += RX_RING_SIZE;
	} /* for */
	
	return (SK_TRUE);
} /* BoardAllocMem */


/****************************************************************************
 *
 *	BoardFreeMem - reverse of BoardAllocMem
 *
 * Description:
 *	Free all memory allocated in BoardAllocMem: adapter context,
 *	descriptor rings, locks.
 *
 * Returns:	N/A
 */
static void BoardFreeMem(
SK_AC		*pAC)
{
size_t		AllocLength;	/* length of complete descriptor area */

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("BoardFreeMem\n"));
#if (BITS_PER_LONG == 32)
	AllocLength = (RX_RING_SIZE + TX_RING_SIZE) * pAC->GIni.GIMacsFound + 8;
#else
	AllocLength = (RX_RING_SIZE + TX_RING_SIZE) * pAC->GIni.GIMacsFound
		+ RX_RING_SIZE + 8;
#endif

	pci_free_consistent(pAC->PciDev, AllocLength,
			    pAC->pDescrMem, pAC->pDescrMemDMA);
	pAC->pDescrMem = NULL;
} /* BoardFreeMem */


/*****************************************************************************
 *
 * 	BoardInitMem - initiate the descriptor rings
 *
 * Description:
 *	This function sets the descriptor rings up in memory.
 *	The adapter is initialized with the descriptor start addresses.
 *
 * Returns:	N/A
 */
static __devinit void BoardInitMem(SK_AC *pAC)
{
int	i;		/* loop counter */
int	RxDescrSize;	/* the size of a rx descriptor rounded up to alignment*/
int	TxDescrSize;	/* the size of a tx descriptor rounded up to alignment*/

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("BoardInitMem\n"));

	RxDescrSize = (((sizeof(RXD) - 1) / DESCR_ALIGN) + 1) * DESCR_ALIGN;
	pAC->RxDescrPerRing = RX_RING_SIZE / RxDescrSize;
	TxDescrSize = (((sizeof(TXD) - 1) / DESCR_ALIGN) + 1) * DESCR_ALIGN;
	pAC->TxDescrPerRing = TX_RING_SIZE / RxDescrSize;
	
	for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		SetupRing(
			pAC,
			pAC->TxPort[i][0].pTxDescrRing,
			pAC->TxPort[i][0].VTxDescrRing,
			(RXD**)&pAC->TxPort[i][0].pTxdRingHead,
			(RXD**)&pAC->TxPort[i][0].pTxdRingTail,
			(RXD**)&pAC->TxPort[i][0].pTxdRingPrev,
			&pAC->TxPort[i][0].TxdRingFree,
			SK_TRUE);
		SetupRing(
			pAC,
			pAC->RxPort[i].pRxDescrRing,
			pAC->RxPort[i].VRxDescrRing,
			&pAC->RxPort[i].pRxdRingHead,
			&pAC->RxPort[i].pRxdRingTail,
			&pAC->RxPort[i].pRxdRingPrev,
			&pAC->RxPort[i].RxdRingFree,
			SK_FALSE);
	}
} /* BoardInitMem */


/*****************************************************************************
 *
 * 	SetupRing - create one descriptor ring
 *
 * Description:
 *	This function creates one descriptor ring in the given memory area.
 *	The head, tail and number of free descriptors in the ring are set.
 *
 * Returns:
 *	none
 */
static void SetupRing(
SK_AC		*pAC,
void		*pMemArea,	/* a pointer to the memory area for the ring */
uintptr_t	VMemArea,	/* the virtual bus address of the memory area */
RXD		**ppRingHead,	/* address where the head should be written */
RXD		**ppRingTail,	/* address where the tail should be written */
RXD		**ppRingPrev,	/* address where the tail should be written */
int		*pRingFree,	/* address where the # of free descr. goes */
SK_BOOL		IsTx)		/* flag: is this a tx ring */
{
int	i;		/* loop counter */
int	DescrSize;	/* the size of a descriptor rounded up to alignment*/
int	DescrNum;	/* number of descriptors per ring */
RXD	*pDescr;	/* pointer to a descriptor (receive or transmit) */
RXD	*pNextDescr;	/* pointer to the next descriptor */
RXD	*pPrevDescr;	/* pointer to the previous descriptor */
uintptr_t VNextDescr;	/* the virtual bus address of the next descriptor */

	if (IsTx == SK_TRUE) {
		DescrSize = (((sizeof(TXD) - 1) / DESCR_ALIGN) + 1) *
			DESCR_ALIGN;
		DescrNum = TX_RING_SIZE / DescrSize;
	} else {
		DescrSize = (((sizeof(RXD) - 1) / DESCR_ALIGN) + 1) *
			DESCR_ALIGN;
		DescrNum = RX_RING_SIZE / DescrSize;
	}
	
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS,
		("Descriptor size: %d   Descriptor Number: %d\n",
		DescrSize,DescrNum));
	
	pDescr = (RXD*) pMemArea;
	pPrevDescr = NULL;
	pNextDescr = (RXD*) (((char*)pDescr) + DescrSize);
	VNextDescr = VMemArea + DescrSize;
	for(i=0; i<DescrNum; i++) {
		/* set the pointers right */
		pDescr->VNextRxd = VNextDescr & 0xffffffffULL;
		pDescr->pNextRxd = pNextDescr;
		if (!IsTx) pDescr->TcpSumStarts = ETH_HLEN << 16 | ETH_HLEN;

		/* advance one step */
		pPrevDescr = pDescr;
		pDescr = pNextDescr;
		pNextDescr = (RXD*) (((char*)pDescr) + DescrSize);
		VNextDescr += DescrSize;
	}
	pPrevDescr->pNextRxd = (RXD*) pMemArea;
	pPrevDescr->VNextRxd = VMemArea;
	pDescr = (RXD*) pMemArea;
	*ppRingHead = (RXD*) pMemArea;
	*ppRingTail = *ppRingHead;
	*ppRingPrev = pPrevDescr;
	*pRingFree = DescrNum;
} /* SetupRing */


/*****************************************************************************
 *
 * 	PortReInitBmu - re-initiate the descriptor rings for one port
 *
 * Description:
 *	This function reinitializes the descriptor rings of one port
 *	in memory. The port must be stopped before.
 *	The HW is initialized with the descriptor start addresses.
 *
 * Returns:
 *	none
 */
static void PortReInitBmu(
SK_AC	*pAC,		/* pointer to adapter context */
int	PortIndex)	/* index of the port for which to re-init */
{
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("PortReInitBmu "));

	/* set address of first descriptor of ring in BMU */
	SK_OUT32(pAC->IoBase, TxQueueAddr[PortIndex][TX_PRIO_LOW]+ Q_DA_L,
		(uint32_t)(((caddr_t)
		(pAC->TxPort[PortIndex][TX_PRIO_LOW].pTxdRingHead) -
		pAC->TxPort[PortIndex][TX_PRIO_LOW].pTxDescrRing +
		pAC->TxPort[PortIndex][TX_PRIO_LOW].VTxDescrRing) &
		0xFFFFFFFF));
	SK_OUT32(pAC->IoBase, TxQueueAddr[PortIndex][TX_PRIO_LOW]+ Q_DA_H,
		(uint32_t)(((caddr_t)
		(pAC->TxPort[PortIndex][TX_PRIO_LOW].pTxdRingHead) -
		pAC->TxPort[PortIndex][TX_PRIO_LOW].pTxDescrRing +
		pAC->TxPort[PortIndex][TX_PRIO_LOW].VTxDescrRing) >> 32));
	SK_OUT32(pAC->IoBase, RxQueueAddr[PortIndex]+Q_DA_L,
		(uint32_t)(((caddr_t)(pAC->RxPort[PortIndex].pRxdRingHead) -
		pAC->RxPort[PortIndex].pRxDescrRing +
		pAC->RxPort[PortIndex].VRxDescrRing) & 0xFFFFFFFF));
	SK_OUT32(pAC->IoBase, RxQueueAddr[PortIndex]+Q_DA_H,
		(uint32_t)(((caddr_t)(pAC->RxPort[PortIndex].pRxdRingHead) -
		pAC->RxPort[PortIndex].pRxDescrRing +
		pAC->RxPort[PortIndex].VRxDescrRing) >> 32));
} /* PortReInitBmu */


/****************************************************************************
 *
 *	SkGeIsr - handle adapter interrupts
 *
 * Description:
 *	The interrupt routine is called when the network adapter
 *	generates an interrupt. It may also be called if another device
 *	shares this interrupt vector with the driver.
 *
 * Returns: N/A
 *
 */
static SkIsrRetVar SkGeIsr(int irq, void *dev_id)
{
struct SK_NET_DEVICE *dev = (struct SK_NET_DEVICE *)dev_id;
DEV_NET		*pNet;
SK_AC		*pAC;
SK_U32		IntSrc;		/* interrupts source register contents */	

	pNet = netdev_priv(dev);
	pAC = pNet->pAC;
	
	/*
	 * Check and process if its our interrupt
	 */
	SK_IN32(pAC->IoBase, B0_SP_ISRC, &IntSrc);
	if (IntSrc == 0) {
		return SkIsrRetNone;
	}

	while (((IntSrc & IRQ_MASK) & ~SPECIAL_IRQS) != 0) {
#if 0 /* software irq currently not used */
		if (IntSrc & IS_IRQ_SW) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("Software IRQ\n"));
		}
#endif
		if (IntSrc & IS_R1_F) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF RX1 IRQ\n"));
			ReceiveIrq(pAC, &pAC->RxPort[0], SK_TRUE);
			SK_PNMI_CNT_RX_INTR(pAC, 0);
		}
		if (IntSrc & IS_R2_F) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF RX2 IRQ\n"));
			ReceiveIrq(pAC, &pAC->RxPort[1], SK_TRUE);
			SK_PNMI_CNT_RX_INTR(pAC, 1);
		}
#ifdef USE_TX_COMPLETE /* only if tx complete interrupt used */
		if (IntSrc & IS_XA1_F) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF AS TX1 IRQ\n"));
			SK_PNMI_CNT_TX_INTR(pAC, 0);
			spin_lock(&pAC->TxPort[0][TX_PRIO_LOW].TxDesRingLock);
			FreeTxDescriptors(pAC, &pAC->TxPort[0][TX_PRIO_LOW]);
			spin_unlock(&pAC->TxPort[0][TX_PRIO_LOW].TxDesRingLock);
		}
		if (IntSrc & IS_XA2_F) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF AS TX2 IRQ\n"));
			SK_PNMI_CNT_TX_INTR(pAC, 1);
			spin_lock(&pAC->TxPort[1][TX_PRIO_LOW].TxDesRingLock);
			FreeTxDescriptors(pAC, &pAC->TxPort[1][TX_PRIO_LOW]);
			spin_unlock(&pAC->TxPort[1][TX_PRIO_LOW].TxDesRingLock);
		}
#if 0 /* only if sync. queues used */
		if (IntSrc & IS_XS1_F) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF SY TX1 IRQ\n"));
			SK_PNMI_CNT_TX_INTR(pAC, 1);
			spin_lock(&pAC->TxPort[0][TX_PRIO_HIGH].TxDesRingLock);
			FreeTxDescriptors(pAC, 0, TX_PRIO_HIGH);
			spin_unlock(&pAC->TxPort[0][TX_PRIO_HIGH].TxDesRingLock);
			ClearTxIrq(pAC, 0, TX_PRIO_HIGH);
		}
		if (IntSrc & IS_XS2_F) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF SY TX2 IRQ\n"));
			SK_PNMI_CNT_TX_INTR(pAC, 1);
			spin_lock(&pAC->TxPort[1][TX_PRIO_HIGH].TxDesRingLock);
			FreeTxDescriptors(pAC, 1, TX_PRIO_HIGH);
			spin_unlock(&pAC->TxPort[1][TX_PRIO_HIGH].TxDesRingLock);
			ClearTxIrq(pAC, 1, TX_PRIO_HIGH);
		}
#endif
#endif

		/* do all IO at once */
		if (IntSrc & IS_R1_F)
			ClearAndStartRx(pAC, 0);
		if (IntSrc & IS_R2_F)
			ClearAndStartRx(pAC, 1);
#ifdef USE_TX_COMPLETE /* only if tx complete interrupt used */
		if (IntSrc & IS_XA1_F)
			ClearTxIrq(pAC, 0, TX_PRIO_LOW);
		if (IntSrc & IS_XA2_F)
			ClearTxIrq(pAC, 1, TX_PRIO_LOW);
#endif
		SK_IN32(pAC->IoBase, B0_ISRC, &IntSrc);
	} /* while (IntSrc & IRQ_MASK != 0) */

	IntSrc &= pAC->GIni.GIValIrqMask;
	if ((IntSrc & SPECIAL_IRQS) || pAC->CheckQueue) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
			("SPECIAL IRQ DP-Cards => %x\n", IntSrc));
		pAC->CheckQueue = SK_FALSE;
		spin_lock(&pAC->SlowPathLock);
		if (IntSrc & SPECIAL_IRQS)
			SkGeSirqIsr(pAC, pAC->IoBase, IntSrc);

		SkEventDispatcher(pAC, pAC->IoBase);
		spin_unlock(&pAC->SlowPathLock);
	}
	/*
	 * do it all again is case we cleared an interrupt that
	 * came in after handling the ring (OUTs may be delayed
	 * in hardware buffers, but are through after IN)
	 *
	 * rroesler: has been commented out and shifted to
	 *           SkGeDrvEvent(), because it is timer
	 *           guarded now
	 *
	ReceiveIrq(pAC, &pAC->RxPort[0], SK_TRUE);
	ReceiveIrq(pAC, &pAC->RxPort[1], SK_TRUE);
	 */

	if (pAC->CheckQueue) {
		pAC->CheckQueue = SK_FALSE;
		spin_lock(&pAC->SlowPathLock);
		SkEventDispatcher(pAC, pAC->IoBase);
		spin_unlock(&pAC->SlowPathLock);
	}

	/* IRQ is processed - Enable IRQs again*/
	SK_OUT32(pAC->IoBase, B0_IMSK, pAC->GIni.GIValIrqMask);

		return SkIsrRetHandled;
} /* SkGeIsr */


/****************************************************************************
 *
 *	SkGeIsrOnePort - handle adapter interrupts for single port adapter
 *
 * Description:
 *	The interrupt routine is called when the network adapter
 *	generates an interrupt. It may also be called if another device
 *	shares this interrupt vector with the driver.
 *	This is the same as above, but handles only one port.
 *
 * Returns: N/A
 *
 */
static SkIsrRetVar SkGeIsrOnePort(int irq, void *dev_id)
{
struct SK_NET_DEVICE *dev = (struct SK_NET_DEVICE *)dev_id;
DEV_NET		*pNet;
SK_AC		*pAC;
SK_U32		IntSrc;		/* interrupts source register contents */	

	pNet = netdev_priv(dev);
	pAC = pNet->pAC;
	
	/*
	 * Check and process if its our interrupt
	 */
	SK_IN32(pAC->IoBase, B0_SP_ISRC, &IntSrc);
	if (IntSrc == 0) {
		return SkIsrRetNone;
	}
	
	while (((IntSrc & IRQ_MASK) & ~SPECIAL_IRQS) != 0) {
#if 0 /* software irq currently not used */
		if (IntSrc & IS_IRQ_SW) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("Software IRQ\n"));
		}
#endif
		if (IntSrc & IS_R1_F) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF RX1 IRQ\n"));
			ReceiveIrq(pAC, &pAC->RxPort[0], SK_TRUE);
			SK_PNMI_CNT_RX_INTR(pAC, 0);
		}
#ifdef USE_TX_COMPLETE /* only if tx complete interrupt used */
		if (IntSrc & IS_XA1_F) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF AS TX1 IRQ\n"));
			SK_PNMI_CNT_TX_INTR(pAC, 0);
			spin_lock(&pAC->TxPort[0][TX_PRIO_LOW].TxDesRingLock);
			FreeTxDescriptors(pAC, &pAC->TxPort[0][TX_PRIO_LOW]);
			spin_unlock(&pAC->TxPort[0][TX_PRIO_LOW].TxDesRingLock);
		}
#if 0 /* only if sync. queues used */
		if (IntSrc & IS_XS1_F) {
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_INT_SRC,
				("EOF SY TX1 IRQ\n"));
			SK_PNMI_CNT_TX_INTR(pAC, 0);
			spin_lock(&pAC->TxPort[0][TX_PRIO_HIGH].TxDesRingLock);
			FreeTxDescriptors(pAC, 0, TX_PRIO_HIGH);
			spin_unlock(&pAC->TxPort[0][TX_PRIO_HIGH].TxDesRingLock);
			ClearTxIrq(pAC, 0, TX_PRIO_HIGH);
		}
#endif
#endif

		/* do all IO at once */
		if (IntSrc & IS_R1_F)
			ClearAndStartRx(pAC, 0);
#ifdef USE_TX_COMPLETE /* only if tx complete interrupt used */
		if (IntSrc & IS_XA1_F)
			ClearTxIrq(pAC, 0, TX_PRIO_LOW);
#endif
		SK_IN32(pAC->IoBase, B0_ISRC, &IntSrc);
	} /* while (IntSrc & IRQ_MASK != 0) */
	
	IntSrc &= pAC->GIni.GIValIrqMask;
	if ((IntSrc & SPECIAL_IRQS) || pAC->CheckQueue) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_INT_SRC,
			("SPECIAL IRQ SP-Cards => %x\n", IntSrc));
		pAC->CheckQueue = SK_FALSE;
		spin_lock(&pAC->SlowPathLock);
		if (IntSrc & SPECIAL_IRQS)
			SkGeSirqIsr(pAC, pAC->IoBase, IntSrc);

		SkEventDispatcher(pAC, pAC->IoBase);
		spin_unlock(&pAC->SlowPathLock);
	}
	/*
	 * do it all again is case we cleared an interrupt that
	 * came in after handling the ring (OUTs may be delayed
	 * in hardware buffers, but are through after IN)
	 *
	 * rroesler: has been commented out and shifted to
	 *           SkGeDrvEvent(), because it is timer
	 *           guarded now
	 *
	ReceiveIrq(pAC, &pAC->RxPort[0], SK_TRUE);
	 */

	/* IRQ is processed - Enable IRQs again*/
	SK_OUT32(pAC->IoBase, B0_IMSK, pAC->GIni.GIValIrqMask);

		return SkIsrRetHandled;
} /* SkGeIsrOnePort */

#ifdef CONFIG_NET_POLL_CONTROLLER
/****************************************************************************
 *
 * 	SkGePollController - polling receive, for netconsole
 *
 * Description:
 *	Polling receive - used by netconsole and other diagnostic tools
 *	to allow network i/o with interrupts disabled.
 *
 * Returns: N/A
 */
static void SkGePollController(struct net_device *dev)
{
	disable_irq(dev->irq);
	SkGeIsr(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

/****************************************************************************
 *
 *	SkGeOpen - handle start of initialized adapter
 *
 * Description:
 *	This function starts the initialized adapter.
 *	The board level variable is set and the adapter is
 *	brought to full functionality.
 *	The device flags are set for operation.
 *	Do all necessary level 2 initialization, enable interrupts and
 *	give start command to RLMT.
 *
 * Returns:
 *	0 on success
 *	!= 0 on error
 */
static int SkGeOpen(
struct SK_NET_DEVICE	*dev)
{
	DEV_NET			*pNet;
	SK_AC			*pAC;
	unsigned long	Flags;		/* for spin lock */
	int				i;
	SK_EVPARA		EvPara;		/* an event parameter union */

	pNet = netdev_priv(dev);
	pAC = pNet->pAC;
	
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeOpen: pAC=0x%lX:\n", (unsigned long)pAC));

#ifdef SK_DIAG_SUPPORT
	if (pAC->DiagModeActive == DIAG_ACTIVE) {
		if (pAC->Pnmi.DiagAttached == SK_DIAG_RUNNING) {
			return (-1);   /* still in use by diag; deny actions */
		} 
	}
#endif

	/* Set blink mode */
	if ((pAC->PciDev->vendor == 0x1186) || (pAC->PciDev->vendor == 0x11ab ))
		pAC->GIni.GILedBlinkCtrl = OEM_CONFIG_VALUE;

	if (pAC->BoardLevel == SK_INIT_DATA) {
		/* level 1 init common modules here */
		if (SkGeInit(pAC, pAC->IoBase, SK_INIT_IO) != 0) {
			printk("%s: HWInit (1) failed.\n", pAC->dev[pNet->PortNr]->name);
			return (-1);
		}
		SkI2cInit	(pAC, pAC->IoBase, SK_INIT_IO);
		SkEventInit	(pAC, pAC->IoBase, SK_INIT_IO);
		SkPnmiInit	(pAC, pAC->IoBase, SK_INIT_IO);
		SkAddrInit	(pAC, pAC->IoBase, SK_INIT_IO);
		SkRlmtInit	(pAC, pAC->IoBase, SK_INIT_IO);
		SkTimerInit	(pAC, pAC->IoBase, SK_INIT_IO);
		pAC->BoardLevel = SK_INIT_IO;
	}

	if (pAC->BoardLevel != SK_INIT_RUN) {
		/* tschilling: Level 2 init modules here, check return value. */
		if (SkGeInit(pAC, pAC->IoBase, SK_INIT_RUN) != 0) {
			printk("%s: HWInit (2) failed.\n", pAC->dev[pNet->PortNr]->name);
			return (-1);
		}
		SkI2cInit	(pAC, pAC->IoBase, SK_INIT_RUN);
		SkEventInit	(pAC, pAC->IoBase, SK_INIT_RUN);
		SkPnmiInit	(pAC, pAC->IoBase, SK_INIT_RUN);
		SkAddrInit	(pAC, pAC->IoBase, SK_INIT_RUN);
		SkRlmtInit	(pAC, pAC->IoBase, SK_INIT_RUN);
		SkTimerInit	(pAC, pAC->IoBase, SK_INIT_RUN);
		pAC->BoardLevel = SK_INIT_RUN;
	}

	for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		/* Enable transmit descriptor polling. */
		SkGePollTxD(pAC, pAC->IoBase, i, SK_TRUE);
		FillRxRing(pAC, &pAC->RxPort[i]);
	}
	SkGeYellowLED(pAC, pAC->IoBase, 1);

	StartDrvCleanupTimer(pAC);
	SkDimEnableModerationIfNeeded(pAC);	
	SkDimDisplayModerationSettings(pAC);

	pAC->GIni.GIValIrqMask &= IRQ_MASK;

	/* enable Interrupts */
	SK_OUT32(pAC->IoBase, B0_IMSK, pAC->GIni.GIValIrqMask);
	SK_OUT32(pAC->IoBase, B0_HWE_IMSK, IRQ_HWE_MASK);

	spin_lock_irqsave(&pAC->SlowPathLock, Flags);

	if ((pAC->RlmtMode != 0) && (pAC->MaxPorts == 0)) {
		EvPara.Para32[0] = pAC->RlmtNets;
		EvPara.Para32[1] = -1;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_SET_NETS,
			EvPara);
		EvPara.Para32[0] = pAC->RlmtMode;
		EvPara.Para32[1] = 0;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_MODE_CHANGE,
			EvPara);
	}

	EvPara.Para32[0] = pNet->NetNr;
	EvPara.Para32[1] = -1;
	SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_START, EvPara);
	SkEventDispatcher(pAC, pAC->IoBase);
	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);

	pAC->MaxPorts++;


	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeOpen suceeded\n"));

	return (0);
} /* SkGeOpen */


/****************************************************************************
 *
 *	SkGeClose - Stop initialized adapter
 *
 * Description:
 *	Close initialized adapter.
 *
 * Returns:
 *	0 - on success
 *	error code - on error
 */
static int SkGeClose(
struct SK_NET_DEVICE	*dev)
{
	DEV_NET		*pNet;
	DEV_NET		*newPtrNet;
	SK_AC		*pAC;

	unsigned long	Flags;		/* for spin lock */
	int		i;
	int		PortIdx;
	SK_EVPARA	EvPara;

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeClose: pAC=0x%lX ", (unsigned long)pAC));

	pNet = netdev_priv(dev);
	pAC = pNet->pAC;

#ifdef SK_DIAG_SUPPORT
	if (pAC->DiagModeActive == DIAG_ACTIVE) {
		if (pAC->DiagFlowCtrl == SK_FALSE) {
			/* 
			** notify that the interface which has been closed
			** by operator interaction must not be started up 
			** again when the DIAG has finished. 
			*/
			newPtrNet = netdev_priv(pAC->dev[0]);
			if (newPtrNet == pNet) {
				pAC->WasIfUp[0] = SK_FALSE;
			} else {
				pAC->WasIfUp[1] = SK_FALSE;
			}
			return 0; /* return to system everything is fine... */
		} else {
			pAC->DiagFlowCtrl = SK_FALSE;
		}
	}
#endif

	netif_stop_queue(dev);

	if (pAC->RlmtNets == 1)
		PortIdx = pAC->ActivePort;
	else
		PortIdx = pNet->NetNr;

        StopDrvCleanupTimer(pAC);

	/*
	 * Clear multicast table, promiscuous mode ....
	 */
	SkAddrMcClear(pAC, pAC->IoBase, PortIdx, 0);
	SkAddrPromiscuousChange(pAC, pAC->IoBase, PortIdx,
		SK_PROM_MODE_NONE);

	if (pAC->MaxPorts == 1) {
		spin_lock_irqsave(&pAC->SlowPathLock, Flags);
		/* disable interrupts */
		SK_OUT32(pAC->IoBase, B0_IMSK, 0);
		EvPara.Para32[0] = pNet->NetNr;
		EvPara.Para32[1] = -1;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_STOP, EvPara);
		SkEventDispatcher(pAC, pAC->IoBase);
		SK_OUT32(pAC->IoBase, B0_IMSK, 0);
		/* stop the hardware */
		SkGeDeInit(pAC, pAC->IoBase);
		pAC->BoardLevel = SK_INIT_DATA;
		spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
	} else {

		spin_lock_irqsave(&pAC->SlowPathLock, Flags);
		EvPara.Para32[0] = pNet->NetNr;
		EvPara.Para32[1] = -1;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_STOP, EvPara);
		SkPnmiEvent(pAC, pAC->IoBase, SK_PNMI_EVT_XMAC_RESET, EvPara);
		SkEventDispatcher(pAC, pAC->IoBase);
		spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
		
		/* Stop port */
		spin_lock_irqsave(&pAC->TxPort[pNet->PortNr]
			[TX_PRIO_LOW].TxDesRingLock, Flags);
		SkGeStopPort(pAC, pAC->IoBase, pNet->PortNr,
			SK_STOP_ALL, SK_HARD_RST);
		spin_unlock_irqrestore(&pAC->TxPort[pNet->PortNr]
			[TX_PRIO_LOW].TxDesRingLock, Flags);
	}

	if (pAC->RlmtNets == 1) {
		/* clear all descriptor rings */
		for (i=0; i<pAC->GIni.GIMacsFound; i++) {
			ReceiveIrq(pAC, &pAC->RxPort[i], SK_TRUE);
			ClearRxRing(pAC, &pAC->RxPort[i]);
			ClearTxRing(pAC, &pAC->TxPort[i][TX_PRIO_LOW]);
		}
	} else {
		/* clear port descriptor rings */
		ReceiveIrq(pAC, &pAC->RxPort[pNet->PortNr], SK_TRUE);
		ClearRxRing(pAC, &pAC->RxPort[pNet->PortNr]);
		ClearTxRing(pAC, &pAC->TxPort[pNet->PortNr][TX_PRIO_LOW]);
	}

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeClose: done "));

	SK_MEMSET(&(pAC->PnmiBackup), 0, sizeof(SK_PNMI_STRUCT_DATA));
	SK_MEMCPY(&(pAC->PnmiBackup), &(pAC->PnmiStruct), 
			sizeof(SK_PNMI_STRUCT_DATA));

	pAC->MaxPorts--;

	return (0);
} /* SkGeClose */


/*****************************************************************************
 *
 * 	SkGeXmit - Linux frame transmit function
 *
 * Description:
 *	The system calls this function to send frames onto the wire.
 *	It puts the frame in the tx descriptor ring. If the ring is
 *	full then, the 'tbusy' flag is set.
 *
 * Returns:
 *	0, if everything is ok
 *	!=0, on error
 * WARNING: returning 1 in 'tbusy' case caused system crashes (double
 *	allocated skb's) !!!
 */
static int SkGeXmit(struct sk_buff *skb, struct SK_NET_DEVICE *dev)
{
DEV_NET		*pNet;
SK_AC		*pAC;
int			Rc;	/* return code of XmitFrame */

	pNet = netdev_priv(dev);
	pAC = pNet->pAC;

	if ((!skb_shinfo(skb)->nr_frags) ||
		(pAC->GIni.GIChipId == CHIP_ID_GENESIS)) {
		/* Don't activate scatter-gather and hardware checksum */

		if (pAC->RlmtNets == 2)
			Rc = XmitFrame(
				pAC,
				&pAC->TxPort[pNet->PortNr][TX_PRIO_LOW],
				skb);
		else
			Rc = XmitFrame(
				pAC,
				&pAC->TxPort[pAC->ActivePort][TX_PRIO_LOW],
				skb);
	} else {
		/* scatter-gather and hardware TCP checksumming anabled*/
		if (pAC->RlmtNets == 2)
			Rc = XmitFrameSG(
				pAC,
				&pAC->TxPort[pNet->PortNr][TX_PRIO_LOW],
				skb);
		else
			Rc = XmitFrameSG(
				pAC,
				&pAC->TxPort[pAC->ActivePort][TX_PRIO_LOW],
				skb);
	}

	/* Transmitter out of resources? */
	if (Rc <= 0) {
		netif_stop_queue(dev);
	}

	/* If not taken, give buffer ownership back to the
	 * queueing layer.
	 */
	if (Rc < 0)
		return (1);

	dev->trans_start = jiffies;
	return (0);
} /* SkGeXmit */


/*****************************************************************************
 *
 * 	XmitFrame - fill one socket buffer into the transmit ring
 *
 * Description:
 *	This function puts a message into the transmit descriptor ring
 *	if there is a descriptors left.
 *	Linux skb's consist of only one continuous buffer.
 *	The first step locks the ring. It is held locked
 *	all time to avoid problems with SWITCH_../PORT_RESET.
 *	Then the descriptoris allocated.
 *	The second part is linking the buffer to the descriptor.
 *	At the very last, the Control field of the descriptor
 *	is made valid for the BMU and a start TX command is given
 *	if necessary.
 *
 * Returns:
 *	> 0 - on succes: the number of bytes in the message
 *	= 0 - on resource shortage: this frame sent or dropped, now
 *		the ring is full ( -> set tbusy)
 *	< 0 - on failure: other problems ( -> return failure to upper layers)
 */
static int XmitFrame(
SK_AC 		*pAC,		/* pointer to adapter context           */
TX_PORT		*pTxPort,	/* pointer to struct of port to send to */
struct sk_buff	*pMessage)	/* pointer to send-message              */
{
	TXD		*pTxd;		/* the rxd to fill */
	TXD		*pOldTxd;
	unsigned long	 Flags;
	SK_U64		 PhysAddr;
	int		 BytesSend = pMessage->len;

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_TX_PROGRESS, ("X"));

	spin_lock_irqsave(&pTxPort->TxDesRingLock, Flags);
#ifndef USE_TX_COMPLETE
	FreeTxDescriptors(pAC, pTxPort);
#endif
	if (pTxPort->TxdRingFree == 0) {
		/* 
		** no enough free descriptors in ring at the moment.
		** Maybe free'ing some old one help?
		*/
		FreeTxDescriptors(pAC, pTxPort);
		if (pTxPort->TxdRingFree == 0) {
			spin_unlock_irqrestore(&pTxPort->TxDesRingLock, Flags);
			SK_PNMI_CNT_NO_TX_BUF(pAC, pTxPort->PortIndex);
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_TX_PROGRESS,
				("XmitFrame failed\n"));
			/* 
			** the desired message can not be sent
			** Because tbusy seems to be set, the message 
			** should not be freed here. It will be used 
			** by the scheduler of the ethernet handler 
			*/
			return (-1);
		}
	}

	/*
	** If the passed socket buffer is of smaller MTU-size than 60,
	** copy everything into new buffer and fill all bytes between
	** the original packet end and the new packet end of 60 with 0x00.
	** This is to resolve faulty padding by the HW with 0xaa bytes.
	*/
	if (BytesSend < C_LEN_ETHERNET_MINSIZE) {
		if (skb_padto(pMessage, C_LEN_ETHERNET_MINSIZE)) {
			spin_unlock_irqrestore(&pTxPort->TxDesRingLock, Flags);
			return 0;
		}
		pMessage->len = C_LEN_ETHERNET_MINSIZE;
	}

	/* 
	** advance head counter behind descriptor needed for this frame, 
	** so that needed descriptor is reserved from that on. The next
	** action will be to add the passed buffer to the TX-descriptor
	*/
	pTxd = pTxPort->pTxdRingHead;
	pTxPort->pTxdRingHead = pTxd->pNextTxd;
	pTxPort->TxdRingFree--;

#ifdef SK_DUMP_TX
	DumpMsg(pMessage, "XmitFrame");
#endif

	/* 
	** First step is to map the data to be sent via the adapter onto
	** the DMA memory. Kernel 2.2 uses virt_to_bus(), but kernels 2.4
	** and 2.6 need to use pci_map_page() for that mapping.
	*/
	PhysAddr = (SK_U64) pci_map_page(pAC->PciDev,
					virt_to_page(pMessage->data),
					((unsigned long) pMessage->data & ~PAGE_MASK),
					pMessage->len,
					PCI_DMA_TODEVICE);
	pTxd->VDataLow  = (SK_U32) (PhysAddr & 0xffffffff);
	pTxd->VDataHigh = (SK_U32) (PhysAddr >> 32);
	pTxd->pMBuf     = pMessage;

	if (pMessage->ip_summed == CHECKSUM_PARTIAL) {
		u16 hdrlen = pMessage->h.raw - pMessage->data;
		u16 offset = hdrlen + pMessage->csum_offset;

		if ((pMessage->h.ipiph->protocol == IPPROTO_UDP ) &&
			(pAC->GIni.GIChipRev == 0) &&
			(pAC->GIni.GIChipId == CHIP_ID_YUKON)) {
			pTxd->TBControl = BMU_TCP_CHECK;
		} else {
			pTxd->TBControl = BMU_UDP_CHECK;
		}

		pTxd->TcpSumOfs = 0;
		pTxd->TcpSumSt  = hdrlen;
		pTxd->TcpSumWr  = offset;

		pTxd->TBControl |= BMU_OWN | BMU_STF | 
				   BMU_SW  | BMU_EOF |
#ifdef USE_TX_COMPLETE
				   BMU_IRQ_EOF |
#endif
				   pMessage->len;
        } else {
		pTxd->TBControl = BMU_OWN | BMU_STF | BMU_CHECK | 
				  BMU_SW  | BMU_EOF |
#ifdef USE_TX_COMPLETE
				   BMU_IRQ_EOF |
#endif
			pMessage->len;
	}

	/* 
	** If previous descriptor already done, give TX start cmd 
	*/
	pOldTxd = xchg(&pTxPort->pTxdRingPrev, pTxd);
	if ((pOldTxd->TBControl & BMU_OWN) == 0) {
		SK_OUT8(pTxPort->HwAddr, Q_CSR, CSR_START);
	}	

	/* 
	** after releasing the lock, the skb may immediately be free'd 
	*/
	spin_unlock_irqrestore(&pTxPort->TxDesRingLock, Flags);
	if (pTxPort->TxdRingFree != 0) {
		return (BytesSend);
	} else {
		return (0);
	}

} /* XmitFrame */

/*****************************************************************************
 *
 * 	XmitFrameSG - fill one socket buffer into the transmit ring
 *                (use SG and TCP/UDP hardware checksumming)
 *
 * Description:
 *	This function puts a message into the transmit descriptor ring
 *	if there is a descriptors left.
 *
 * Returns:
 *	> 0 - on succes: the number of bytes in the message
 *	= 0 - on resource shortage: this frame sent or dropped, now
 *		the ring is full ( -> set tbusy)
 *	< 0 - on failure: other problems ( -> return failure to upper layers)
 */
static int XmitFrameSG(
SK_AC 		*pAC,		/* pointer to adapter context           */
TX_PORT		*pTxPort,	/* pointer to struct of port to send to */
struct sk_buff	*pMessage)	/* pointer to send-message              */
{

	TXD		*pTxd;
	TXD		*pTxdFst;
	TXD		*pTxdLst;
	int 	 	 CurrFrag;
	int		 BytesSend;
	skb_frag_t	*sk_frag;
	SK_U64		 PhysAddr;
	unsigned long	 Flags;
	SK_U32		 Control;

	spin_lock_irqsave(&pTxPort->TxDesRingLock, Flags);
#ifndef USE_TX_COMPLETE
	FreeTxDescriptors(pAC, pTxPort);
#endif
	if ((skb_shinfo(pMessage)->nr_frags +1) > pTxPort->TxdRingFree) {
		FreeTxDescriptors(pAC, pTxPort);
		if ((skb_shinfo(pMessage)->nr_frags + 1) > pTxPort->TxdRingFree) {
			spin_unlock_irqrestore(&pTxPort->TxDesRingLock, Flags);
			SK_PNMI_CNT_NO_TX_BUF(pAC, pTxPort->PortIndex);
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_TX_PROGRESS,
				("XmitFrameSG failed - Ring full\n"));
				/* this message can not be sent now */
			return(-1);
		}
	}

	pTxd      = pTxPort->pTxdRingHead;
	pTxdFst   = pTxd;
	pTxdLst   = pTxd;
	BytesSend = 0;

	/* 
	** Map the first fragment (header) into the DMA-space
	*/
	PhysAddr = (SK_U64) pci_map_page(pAC->PciDev,
			virt_to_page(pMessage->data),
			((unsigned long) pMessage->data & ~PAGE_MASK),
			skb_headlen(pMessage),
			PCI_DMA_TODEVICE);

	pTxd->VDataLow  = (SK_U32) (PhysAddr & 0xffffffff);
	pTxd->VDataHigh = (SK_U32) (PhysAddr >> 32);

	/* 
	** Does the HW need to evaluate checksum for TCP or UDP packets? 
	*/
	if (pMessage->ip_summed == CHECKSUM_PARTIAL) {
		u16 hdrlen = pMessage->h.raw - pMessage->data;
		u16 offset = hdrlen + pMessage->csum_offset;

		Control = BMU_STFWD;

		/* 
		** We have to use the opcode for tcp here,  because the
		** opcode for udp is not working in the hardware yet 
		** (Revision 2.0)
		*/
		if ((pMessage->h.ipiph->protocol == IPPROTO_UDP ) &&
			(pAC->GIni.GIChipRev == 0) &&
			(pAC->GIni.GIChipId == CHIP_ID_YUKON)) {
			Control |= BMU_TCP_CHECK;
		} else {
			Control |= BMU_UDP_CHECK;
		}

		pTxd->TcpSumOfs = 0;
		pTxd->TcpSumSt  = hdrlen;
		pTxd->TcpSumWr  = offset;
	} else
		Control = BMU_CHECK | BMU_SW;

	pTxd->TBControl = BMU_STF | Control | skb_headlen(pMessage);

	pTxd = pTxd->pNextTxd;
	pTxPort->TxdRingFree--;
	BytesSend += skb_headlen(pMessage);

	/* 
	** Browse over all SG fragments and map each of them into the DMA space
	*/
	for (CurrFrag = 0; CurrFrag < skb_shinfo(pMessage)->nr_frags; CurrFrag++) {
		sk_frag = &skb_shinfo(pMessage)->frags[CurrFrag];
		/* 
		** we already have the proper value in entry
		*/
		PhysAddr = (SK_U64) pci_map_page(pAC->PciDev,
						 sk_frag->page,
						 sk_frag->page_offset,
						 sk_frag->size,
						 PCI_DMA_TODEVICE);

		pTxd->VDataLow  = (SK_U32) (PhysAddr & 0xffffffff);
		pTxd->VDataHigh = (SK_U32) (PhysAddr >> 32);
		pTxd->pMBuf     = pMessage;
		
		pTxd->TBControl = Control | BMU_OWN | sk_frag->size;

		/* 
		** Do we have the last fragment? 
		*/
		if( (CurrFrag+1) == skb_shinfo(pMessage)->nr_frags )  {
#ifdef USE_TX_COMPLETE
			pTxd->TBControl |= BMU_EOF | BMU_IRQ_EOF;
#else
			pTxd->TBControl |= BMU_EOF;
#endif
			pTxdFst->TBControl |= BMU_OWN | BMU_SW;
		}
		pTxdLst = pTxd;
		pTxd    = pTxd->pNextTxd;
		pTxPort->TxdRingFree--;
		BytesSend += sk_frag->size;
	}

	/* 
	** If previous descriptor already done, give TX start cmd 
	*/
	if ((pTxPort->pTxdRingPrev->TBControl & BMU_OWN) == 0) {
		SK_OUT8(pTxPort->HwAddr, Q_CSR, CSR_START);
	}

	pTxPort->pTxdRingPrev = pTxdLst;
	pTxPort->pTxdRingHead = pTxd;

	spin_unlock_irqrestore(&pTxPort->TxDesRingLock, Flags);

	if (pTxPort->TxdRingFree > 0) {
		return (BytesSend);
	} else {
		return (0);
	}
}

/*****************************************************************************
 *
 * 	FreeTxDescriptors - release descriptors from the descriptor ring
 *
 * Description:
 *	This function releases descriptors from a transmit ring if they
 *	have been sent by the BMU.
 *	If a descriptors is sent, it can be freed and the message can
 *	be freed, too.
 *	The SOFTWARE controllable bit is used to prevent running around a
 *	completely free ring for ever. If this bit is no set in the
 *	frame (by XmitFrame), this frame has never been sent or is
 *	already freed.
 *	The Tx descriptor ring lock must be held while calling this function !!!
 *
 * Returns:
 *	none
 */
static void FreeTxDescriptors(
SK_AC	*pAC,		/* pointer to the adapter context */
TX_PORT	*pTxPort)	/* pointer to destination port structure */
{
TXD	*pTxd;		/* pointer to the checked descriptor */
TXD	*pNewTail;	/* pointer to 'end' of the ring */
SK_U32	Control;	/* TBControl field of descriptor */
SK_U64	PhysAddr;	/* address of DMA mapping */

	pNewTail = pTxPort->pTxdRingTail;
	pTxd     = pNewTail;
	/*
	** loop forever; exits if BMU_SW bit not set in start frame
	** or BMU_OWN bit set in any frame
	*/
	while (1) {
		Control = pTxd->TBControl;
		if ((Control & BMU_SW) == 0) {
			/*
			** software controllable bit is set in first
			** fragment when given to BMU. Not set means that
			** this fragment was never sent or is already
			** freed ( -> ring completely free now).
			*/
			pTxPort->pTxdRingTail = pTxd;
			netif_wake_queue(pAC->dev[pTxPort->PortIndex]);
			return;
		}
		if (Control & BMU_OWN) {
			pTxPort->pTxdRingTail = pTxd;
			if (pTxPort->TxdRingFree > 0) {
				netif_wake_queue(pAC->dev[pTxPort->PortIndex]);
			}
			return;
		}
		
		/* 
		** release the DMA mapping, because until not unmapped
		** this buffer is considered being under control of the
		** adapter card!
		*/
		PhysAddr = ((SK_U64) pTxd->VDataHigh) << (SK_U64) 32;
		PhysAddr |= (SK_U64) pTxd->VDataLow;
		pci_unmap_page(pAC->PciDev, PhysAddr,
				 pTxd->pMBuf->len,
				 PCI_DMA_TODEVICE);

		if (Control & BMU_EOF)
			DEV_KFREE_SKB_ANY(pTxd->pMBuf);	/* free message */

		pTxPort->TxdRingFree++;
		pTxd->TBControl &= ~BMU_SW;
		pTxd = pTxd->pNextTxd; /* point behind fragment with EOF */
	} /* while(forever) */
} /* FreeTxDescriptors */

/*****************************************************************************
 *
 * 	FillRxRing - fill the receive ring with valid descriptors
 *
 * Description:
 *	This function fills the receive ring descriptors with data
 *	segments and makes them valid for the BMU.
 *	The active ring is filled completely, if possible.
 *	The non-active ring is filled only partial to save memory.
 *
 * Description of rx ring structure:
 *	head - points to the descriptor which will be used next by the BMU
 *	tail - points to the next descriptor to give to the BMU
 *	
 * Returns:	N/A
 */
static void FillRxRing(
SK_AC		*pAC,		/* pointer to the adapter context */
RX_PORT		*pRxPort)	/* ptr to port struct for which the ring
				   should be filled */
{
unsigned long	Flags;

	spin_lock_irqsave(&pRxPort->RxDesRingLock, Flags);
	while (pRxPort->RxdRingFree > pRxPort->RxFillLimit) {
		if(!FillRxDescriptor(pAC, pRxPort))
			break;
	}
	spin_unlock_irqrestore(&pRxPort->RxDesRingLock, Flags);
} /* FillRxRing */


/*****************************************************************************
 *
 * 	FillRxDescriptor - fill one buffer into the receive ring
 *
 * Description:
 *	The function allocates a new receive buffer and
 *	puts it into the next descriptor.
 *
 * Returns:
 *	SK_TRUE - a buffer was added to the ring
 *	SK_FALSE - a buffer could not be added
 */
static SK_BOOL FillRxDescriptor(
SK_AC		*pAC,		/* pointer to the adapter context struct */
RX_PORT		*pRxPort)	/* ptr to port struct of ring to fill */
{
struct sk_buff	*pMsgBlock;	/* pointer to a new message block */
RXD		*pRxd;		/* the rxd to fill */
SK_U16		Length;		/* data fragment length */
SK_U64		PhysAddr;	/* physical address of a rx buffer */

	pMsgBlock = alloc_skb(pAC->RxBufSize, GFP_ATOMIC);
	if (pMsgBlock == NULL) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
			SK_DBGCAT_DRV_ENTRY,
			("%s: Allocation of rx buffer failed !\n",
			pAC->dev[pRxPort->PortIndex]->name));
		SK_PNMI_CNT_NO_RX_BUF(pAC, pRxPort->PortIndex);
		return(SK_FALSE);
	}
	skb_reserve(pMsgBlock, 2); /* to align IP frames */
	/* skb allocated ok, so add buffer */
	pRxd = pRxPort->pRxdRingTail;
	pRxPort->pRxdRingTail = pRxd->pNextRxd;
	pRxPort->RxdRingFree--;
	Length = pAC->RxBufSize;
	PhysAddr = (SK_U64) pci_map_page(pAC->PciDev,
		virt_to_page(pMsgBlock->data),
		((unsigned long) pMsgBlock->data &
		~PAGE_MASK),
		pAC->RxBufSize - 2,
		PCI_DMA_FROMDEVICE);

	pRxd->VDataLow  = (SK_U32) (PhysAddr & 0xffffffff);
	pRxd->VDataHigh = (SK_U32) (PhysAddr >> 32);
	pRxd->pMBuf     = pMsgBlock;
	pRxd->RBControl = BMU_OWN       | 
			  BMU_STF       | 
			  BMU_IRQ_EOF   | 
			  BMU_TCP_CHECK | 
			  Length;
	return (SK_TRUE);

} /* FillRxDescriptor */


/*****************************************************************************
 *
 * 	ReQueueRxBuffer - fill one buffer back into the receive ring
 *
 * Description:
 *	Fill a given buffer back into the rx ring. The buffer
 *	has been previously allocated and aligned, and its phys.
 *	address calculated, so this is no more necessary.
 *
 * Returns: N/A
 */
static void ReQueueRxBuffer(
SK_AC		*pAC,		/* pointer to the adapter context struct */
RX_PORT		*pRxPort,	/* ptr to port struct of ring to fill */
struct sk_buff	*pMsg,		/* pointer to the buffer */
SK_U32		PhysHigh,	/* phys address high dword */
SK_U32		PhysLow)	/* phys address low dword */
{
RXD		*pRxd;		/* the rxd to fill */
SK_U16		Length;		/* data fragment length */

	pRxd = pRxPort->pRxdRingTail;
	pRxPort->pRxdRingTail = pRxd->pNextRxd;
	pRxPort->RxdRingFree--;
	Length = pAC->RxBufSize;

	pRxd->VDataLow  = PhysLow;
	pRxd->VDataHigh = PhysHigh;
	pRxd->pMBuf     = pMsg;
	pRxd->RBControl = BMU_OWN       | 
			  BMU_STF       |
			  BMU_IRQ_EOF   | 
			  BMU_TCP_CHECK | 
			  Length;
	return;
} /* ReQueueRxBuffer */

/*****************************************************************************
 *
 * 	ReceiveIrq - handle a receive IRQ
 *
 * Description:
 *	This function is called when a receive IRQ is set.
 *	It walks the receive descriptor ring and sends up all
 *	frames that are complete.
 *
 * Returns:	N/A
 */
static void ReceiveIrq(
	SK_AC		*pAC,			/* pointer to adapter context */
	RX_PORT		*pRxPort,		/* pointer to receive port struct */
	SK_BOOL		SlowPathLock)	/* indicates if SlowPathLock is needed */
{
RXD				*pRxd;			/* pointer to receive descriptors */
SK_U32			Control;		/* control field of descriptor */
struct sk_buff	*pMsg;			/* pointer to message holding frame */
struct sk_buff	*pNewMsg;		/* pointer to a new message for copying frame */
int				FrameLength;	/* total length of received frame */
SK_MBUF			*pRlmtMbuf;		/* ptr to a buffer for giving a frame to rlmt */
SK_EVPARA		EvPara;			/* an event parameter union */	
unsigned long	Flags;			/* for spin lock */
int				PortIndex = pRxPort->PortIndex;
unsigned int	Offset;
unsigned int	NumBytes;
unsigned int	ForRlmt;
SK_BOOL			IsBc;
SK_BOOL			IsMc;
SK_BOOL  IsBadFrame; 			/* Bad frame */

SK_U32			FrameStat;
SK_U64			PhysAddr;

rx_start:	
	/* do forever; exit if BMU_OWN found */
	for ( pRxd = pRxPort->pRxdRingHead ;
		  pRxPort->RxdRingFree < pAC->RxDescrPerRing ;
		  pRxd = pRxd->pNextRxd,
		  pRxPort->pRxdRingHead = pRxd,
		  pRxPort->RxdRingFree ++) {

		/*
		 * For a better understanding of this loop
		 * Go through every descriptor beginning at the head
		 * Please note: the ring might be completely received so the OWN bit
		 * set is not a good crirteria to leave that loop.
		 * Therefore the RingFree counter is used.
		 * On entry of this loop pRxd is a pointer to the Rxd that needs
		 * to be checked next.
		 */

		Control = pRxd->RBControl;
	
		/* check if this descriptor is ready */
		if ((Control & BMU_OWN) != 0) {
			/* this descriptor is not yet ready */
			/* This is the usual end of the loop */
			/* We don't need to start the ring again */
			FillRxRing(pAC, pRxPort);
			return;
		}
                pAC->DynIrqModInfo.NbrProcessedDescr++;

		/* get length of frame and check it */
		FrameLength = Control & BMU_BBC;
		if (FrameLength > pAC->RxBufSize) {
			goto rx_failed;
		}

		/* check for STF and EOF */
		if ((Control & (BMU_STF | BMU_EOF)) != (BMU_STF | BMU_EOF)) {
			goto rx_failed;
		}

		/* here we have a complete frame in the ring */
		pMsg = pRxd->pMBuf;

		FrameStat = pRxd->FrameStat;

		/* check for frame length mismatch */
#define XMR_FS_LEN_SHIFT        18
#define GMR_FS_LEN_SHIFT        16
		if (pAC->GIni.GIChipId == CHIP_ID_GENESIS) {
			if (FrameLength != (SK_U32) (FrameStat >> XMR_FS_LEN_SHIFT)) {
				SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
					SK_DBGCAT_DRV_RX_PROGRESS,
					("skge: Frame length mismatch (%u/%u).\n",
					FrameLength,
					(SK_U32) (FrameStat >> XMR_FS_LEN_SHIFT)));
				goto rx_failed;
			}
		}
		else {
			if (FrameLength != (SK_U32) (FrameStat >> GMR_FS_LEN_SHIFT)) {
				SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
					SK_DBGCAT_DRV_RX_PROGRESS,
					("skge: Frame length mismatch (%u/%u).\n",
					FrameLength,
					(SK_U32) (FrameStat >> XMR_FS_LEN_SHIFT)));
				goto rx_failed;
			}
		}

		/* Set Rx Status */
		if (pAC->GIni.GIChipId == CHIP_ID_GENESIS) {
			IsBc = (FrameStat & XMR_FS_BC) != 0;
			IsMc = (FrameStat & XMR_FS_MC) != 0;
			IsBadFrame = (FrameStat &
				(XMR_FS_ANY_ERR | XMR_FS_2L_VLAN)) != 0;
		} else {
			IsBc = (FrameStat & GMR_FS_BC) != 0;
			IsMc = (FrameStat & GMR_FS_MC) != 0;
			IsBadFrame = (((FrameStat & GMR_FS_ANY_ERR) != 0) ||
							((FrameStat & GMR_FS_RX_OK) == 0));
		}

		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, 0,
			("Received frame of length %d on port %d\n",
			FrameLength, PortIndex));
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, 0,
			("Number of free rx descriptors: %d\n",
			pRxPort->RxdRingFree));
/* DumpMsg(pMsg, "Rx");	*/

		if ((Control & BMU_STAT_VAL) != BMU_STAT_VAL || (IsBadFrame)) {
#if 0
			(FrameStat & (XMR_FS_ANY_ERR | XMR_FS_2L_VLAN)) != 0) {
#endif
			/* there is a receive error in this frame */
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_RX_PROGRESS,
				("skge: Error in received frame, dropped!\n"
				"Control: %x\nRxStat: %x\n",
				Control, FrameStat));

			ReQueueRxBuffer(pAC, pRxPort, pMsg,
				pRxd->VDataHigh, pRxd->VDataLow);

			continue;
		}

		/*
		 * if short frame then copy data to reduce memory waste
		 */
		if ((FrameLength < SK_COPY_THRESHOLD) &&
			((pNewMsg = alloc_skb(FrameLength+2, GFP_ATOMIC)) != NULL)) {
			/*
			 * Short frame detected and allocation successfull
			 */
			/* use new skb and copy data */
			skb_reserve(pNewMsg, 2);
			skb_put(pNewMsg, FrameLength);
			PhysAddr = ((SK_U64) pRxd->VDataHigh) << (SK_U64)32;
			PhysAddr |= (SK_U64) pRxd->VDataLow;

			pci_dma_sync_single_for_cpu(pAC->PciDev,
						    (dma_addr_t) PhysAddr,
						    FrameLength,
						    PCI_DMA_FROMDEVICE);
			memcpy(pNewMsg->data, pMsg, FrameLength);

			pci_dma_sync_single_for_device(pAC->PciDev,
						       (dma_addr_t) PhysAddr,
						       FrameLength,
						       PCI_DMA_FROMDEVICE);
			ReQueueRxBuffer(pAC, pRxPort, pMsg,
				pRxd->VDataHigh, pRxd->VDataLow);

			pMsg = pNewMsg;

		}
		else {
			/*
			 * if large frame, or SKB allocation failed, pass
			 * the SKB directly to the networking
			 */

			PhysAddr = ((SK_U64) pRxd->VDataHigh) << (SK_U64)32;
			PhysAddr |= (SK_U64) pRxd->VDataLow;

			/* release the DMA mapping */
			pci_unmap_single(pAC->PciDev,
					 PhysAddr,
					 pAC->RxBufSize - 2,
					 PCI_DMA_FROMDEVICE);

			/* set length in message */
			skb_put(pMsg, FrameLength);
		} /* frame > SK_COPY_TRESHOLD */

#ifdef USE_SK_RX_CHECKSUM
		pMsg->csum = pRxd->TcpSums & 0xffff;
		pMsg->ip_summed = CHECKSUM_COMPLETE;
#else
		pMsg->ip_summed = CHECKSUM_NONE;
#endif

		SK_DBG_MSG(NULL, SK_DBGMOD_DRV,	1,("V"));
		ForRlmt = SK_RLMT_RX_PROTOCOL;
#if 0
		IsBc = (FrameStat & XMR_FS_BC)==XMR_FS_BC;
#endif
		SK_RLMT_PRE_LOOKAHEAD(pAC, PortIndex, FrameLength,
			IsBc, &Offset, &NumBytes);
		if (NumBytes != 0) {
#if 0
			IsMc = (FrameStat & XMR_FS_MC)==XMR_FS_MC;
#endif
			SK_RLMT_LOOKAHEAD(pAC, PortIndex,
				&pMsg->data[Offset],
				IsBc, IsMc, &ForRlmt);
		}
		if (ForRlmt == SK_RLMT_RX_PROTOCOL) {
					SK_DBG_MSG(NULL, SK_DBGMOD_DRV,	1,("W"));
			/* send up only frames from active port */
			if ((PortIndex == pAC->ActivePort) ||
				(pAC->RlmtNets == 2)) {
				/* frame for upper layer */
				SK_DBG_MSG(NULL, SK_DBGMOD_DRV, 1,("U"));
#ifdef xDEBUG
				DumpMsg(pMsg, "Rx");
#endif
				SK_PNMI_CNT_RX_OCTETS_DELIVERED(pAC,
					FrameLength, pRxPort->PortIndex);

				pMsg->dev = pAC->dev[pRxPort->PortIndex];
				pMsg->protocol = eth_type_trans(pMsg,
					pAC->dev[pRxPort->PortIndex]);
				netif_rx(pMsg);
				pAC->dev[pRxPort->PortIndex]->last_rx = jiffies;
			}
			else {
				/* drop frame */
				SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
					SK_DBGCAT_DRV_RX_PROGRESS,
					("D"));
				DEV_KFREE_SKB(pMsg);
			}
			
		} /* if not for rlmt */
		else {
			/* packet for rlmt */
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
				SK_DBGCAT_DRV_RX_PROGRESS, ("R"));
			pRlmtMbuf = SkDrvAllocRlmtMbuf(pAC,
				pAC->IoBase, FrameLength);
			if (pRlmtMbuf != NULL) {
				pRlmtMbuf->pNext = NULL;
				pRlmtMbuf->Length = FrameLength;
				pRlmtMbuf->PortIdx = PortIndex;
				EvPara.pParaPtr = pRlmtMbuf;
				memcpy((char*)(pRlmtMbuf->pData),
					   (char*)(pMsg->data),
					   FrameLength);

				/* SlowPathLock needed? */
				if (SlowPathLock == SK_TRUE) {
					spin_lock_irqsave(&pAC->SlowPathLock, Flags);
					SkEventQueue(pAC, SKGE_RLMT,
						SK_RLMT_PACKET_RECEIVED,
						EvPara);
					pAC->CheckQueue = SK_TRUE;
					spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
				} else {
					SkEventQueue(pAC, SKGE_RLMT,
						SK_RLMT_PACKET_RECEIVED,
						EvPara);
					pAC->CheckQueue = SK_TRUE;
				}

				SK_DBG_MSG(NULL, SK_DBGMOD_DRV,
					SK_DBGCAT_DRV_RX_PROGRESS,
					("Q"));
			}
			if ((pAC->dev[pRxPort->PortIndex]->flags &
				(IFF_PROMISC | IFF_ALLMULTI)) != 0 ||
				(ForRlmt & SK_RLMT_RX_PROTOCOL) ==
				SK_RLMT_RX_PROTOCOL) {
				pMsg->dev = pAC->dev[pRxPort->PortIndex];
				pMsg->protocol = eth_type_trans(pMsg,
					pAC->dev[pRxPort->PortIndex]);
				netif_rx(pMsg);
				pAC->dev[pRxPort->PortIndex]->last_rx = jiffies;
			}
			else {
				DEV_KFREE_SKB(pMsg);
			}

		} /* if packet for rlmt */
	} /* for ... scanning the RXD ring */

	/* RXD ring is empty -> fill and restart */
	FillRxRing(pAC, pRxPort);
	/* do not start if called from Close */
	if (pAC->BoardLevel > SK_INIT_DATA) {
		ClearAndStartRx(pAC, PortIndex);
	}
	return;

rx_failed:
	/* remove error frame */
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ERROR,
		("Schrottdescriptor, length: 0x%x\n", FrameLength));

	/* release the DMA mapping */

	PhysAddr = ((SK_U64) pRxd->VDataHigh) << (SK_U64)32;
	PhysAddr |= (SK_U64) pRxd->VDataLow;
	pci_unmap_page(pAC->PciDev,
			 PhysAddr,
			 pAC->RxBufSize - 2,
			 PCI_DMA_FROMDEVICE);
	DEV_KFREE_SKB_IRQ(pRxd->pMBuf);
	pRxd->pMBuf = NULL;
	pRxPort->RxdRingFree++;
	pRxPort->pRxdRingHead = pRxd->pNextRxd;
	goto rx_start;

} /* ReceiveIrq */


/*****************************************************************************
 *
 * 	ClearAndStartRx - give a start receive command to BMU, clear IRQ
 *
 * Description:
 *	This function sends a start command and a clear interrupt
 *	command for one receive queue to the BMU.
 *
 * Returns: N/A
 *	none
 */
static void ClearAndStartRx(
SK_AC	*pAC,		/* pointer to the adapter context */
int	PortIndex)	/* index of the receive port (XMAC) */
{
	SK_OUT8(pAC->IoBase,
		RxQueueAddr[PortIndex]+Q_CSR,
		CSR_START | CSR_IRQ_CL_F);
} /* ClearAndStartRx */


/*****************************************************************************
 *
 * 	ClearTxIrq - give a clear transmit IRQ command to BMU
 *
 * Description:
 *	This function sends a clear tx IRQ command for one
 *	transmit queue to the BMU.
 *
 * Returns: N/A
 */
static void ClearTxIrq(
SK_AC	*pAC,		/* pointer to the adapter context */
int	PortIndex,	/* index of the transmit port (XMAC) */
int	Prio)		/* priority or normal queue */
{
	SK_OUT8(pAC->IoBase, 
		TxQueueAddr[PortIndex][Prio]+Q_CSR,
		CSR_IRQ_CL_F);
} /* ClearTxIrq */


/*****************************************************************************
 *
 * 	ClearRxRing - remove all buffers from the receive ring
 *
 * Description:
 *	This function removes all receive buffers from the ring.
 *	The receive BMU must be stopped before calling this function.
 *
 * Returns: N/A
 */
static void ClearRxRing(
SK_AC	*pAC,		/* pointer to adapter context */
RX_PORT	*pRxPort)	/* pointer to rx port struct */
{
RXD		*pRxd;	/* pointer to the current descriptor */
unsigned long	Flags;
SK_U64		PhysAddr;

	if (pRxPort->RxdRingFree == pAC->RxDescrPerRing) {
		return;
	}
	spin_lock_irqsave(&pRxPort->RxDesRingLock, Flags);
	pRxd = pRxPort->pRxdRingHead;
	do {
		if (pRxd->pMBuf != NULL) {

			PhysAddr = ((SK_U64) pRxd->VDataHigh) << (SK_U64)32;
			PhysAddr |= (SK_U64) pRxd->VDataLow;
			pci_unmap_page(pAC->PciDev,
					 PhysAddr,
					 pAC->RxBufSize - 2,
					 PCI_DMA_FROMDEVICE);
			DEV_KFREE_SKB(pRxd->pMBuf);
			pRxd->pMBuf = NULL;
		}
		pRxd->RBControl &= BMU_OWN;
		pRxd = pRxd->pNextRxd;
		pRxPort->RxdRingFree++;
	} while (pRxd != pRxPort->pRxdRingTail);
	pRxPort->pRxdRingTail = pRxPort->pRxdRingHead;
	spin_unlock_irqrestore(&pRxPort->RxDesRingLock, Flags);
} /* ClearRxRing */

/*****************************************************************************
 *
 *	ClearTxRing - remove all buffers from the transmit ring
 *
 * Description:
 *	This function removes all transmit buffers from the ring.
 *	The transmit BMU must be stopped before calling this function
 *	and transmitting at the upper level must be disabled.
 *	The BMU own bit of all descriptors is cleared, the rest is
 *	done by calling FreeTxDescriptors.
 *
 * Returns: N/A
 */
static void ClearTxRing(
SK_AC	*pAC,		/* pointer to adapter context */
TX_PORT	*pTxPort)	/* pointer to tx prt struct */
{
TXD		*pTxd;		/* pointer to the current descriptor */
int		i;
unsigned long	Flags;

	spin_lock_irqsave(&pTxPort->TxDesRingLock, Flags);
	pTxd = pTxPort->pTxdRingHead;
	for (i=0; i<pAC->TxDescrPerRing; i++) {
		pTxd->TBControl &= ~BMU_OWN;
		pTxd = pTxd->pNextTxd;
	}
	FreeTxDescriptors(pAC, pTxPort);
	spin_unlock_irqrestore(&pTxPort->TxDesRingLock, Flags);
} /* ClearTxRing */

/*****************************************************************************
 *
 * 	SkGeSetMacAddr - Set the hardware MAC address
 *
 * Description:
 *	This function sets the MAC address used by the adapter.
 *
 * Returns:
 *	0, if everything is ok
 *	!=0, on error
 */
static int SkGeSetMacAddr(struct SK_NET_DEVICE *dev, void *p)
{

DEV_NET *pNet = netdev_priv(dev);
SK_AC	*pAC = pNet->pAC;

struct sockaddr	*addr = p;
unsigned long	Flags;
	
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeSetMacAddr starts now...\n"));
	if(netif_running(dev))
		return -EBUSY;

	memcpy(dev->dev_addr, addr->sa_data,dev->addr_len);
	
	spin_lock_irqsave(&pAC->SlowPathLock, Flags);

	if (pAC->RlmtNets == 2)
		SkAddrOverride(pAC, pAC->IoBase, pNet->NetNr,
			(SK_MAC_ADDR*)dev->dev_addr, SK_ADDR_VIRTUAL_ADDRESS);
	else
		SkAddrOverride(pAC, pAC->IoBase, pAC->ActivePort,
			(SK_MAC_ADDR*)dev->dev_addr, SK_ADDR_VIRTUAL_ADDRESS);

	
	
	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
	return 0;
} /* SkGeSetMacAddr */


/*****************************************************************************
 *
 * 	SkGeSetRxMode - set receive mode
 *
 * Description:
 *	This function sets the receive mode of an adapter. The adapter
 *	supports promiscuous mode, allmulticast mode and a number of
 *	multicast addresses. If more multicast addresses the available
 *	are selected, a hash function in the hardware is used.
 *
 * Returns:
 *	0, if everything is ok
 *	!=0, on error
 */
static void SkGeSetRxMode(struct SK_NET_DEVICE *dev)
{

DEV_NET		*pNet;
SK_AC		*pAC;

struct dev_mc_list	*pMcList;
int			i;
int			PortIdx;
unsigned long		Flags;

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeSetRxMode starts now... "));

	pNet = netdev_priv(dev);
	pAC = pNet->pAC;
	if (pAC->RlmtNets == 1)
		PortIdx = pAC->ActivePort;
	else
		PortIdx = pNet->NetNr;

	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	if (dev->flags & IFF_PROMISC) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
			("PROMISCUOUS mode\n"));
		SkAddrPromiscuousChange(pAC, pAC->IoBase, PortIdx,
			SK_PROM_MODE_LLC);
	} else if (dev->flags & IFF_ALLMULTI) {
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
			("ALLMULTI mode\n"));
		SkAddrPromiscuousChange(pAC, pAC->IoBase, PortIdx,
			SK_PROM_MODE_ALL_MC);
	} else {
		SkAddrPromiscuousChange(pAC, pAC->IoBase, PortIdx,
			SK_PROM_MODE_NONE);
		SkAddrMcClear(pAC, pAC->IoBase, PortIdx, 0);

		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
			("Number of MC entries: %d ", dev->mc_count));
		
		pMcList = dev->mc_list;
		for (i=0; i<dev->mc_count; i++, pMcList = pMcList->next) {
			SkAddrMcAdd(pAC, pAC->IoBase, PortIdx,
				(SK_MAC_ADDR*)pMcList->dmi_addr, 0);
			SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_MCA,
				("%02x:%02x:%02x:%02x:%02x:%02x\n",
				pMcList->dmi_addr[0],
				pMcList->dmi_addr[1],
				pMcList->dmi_addr[2],
				pMcList->dmi_addr[3],
				pMcList->dmi_addr[4],
				pMcList->dmi_addr[5]));
		}
		SkAddrMcUpdate(pAC, pAC->IoBase, PortIdx);
	}
	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
	
	return;
} /* SkGeSetRxMode */


/*****************************************************************************
 *
 * 	SkGeChangeMtu - set the MTU to another value
 *
 * Description:
 *	This function sets is called whenever the MTU size is changed
 *	(ifconfig mtu xxx dev ethX). If the MTU is bigger than standard
 *	ethernet MTU size, long frame support is activated.
 *
 * Returns:
 *	0, if everything is ok
 *	!=0, on error
 */
static int SkGeChangeMtu(struct SK_NET_DEVICE *dev, int NewMtu)
{
DEV_NET		*pNet;
struct net_device *pOtherDev;
SK_AC		*pAC;
unsigned long	Flags;
int		i;
SK_EVPARA 	EvPara;

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeChangeMtu starts now...\n"));

	pNet = netdev_priv(dev);
	pAC  = pNet->pAC;

	if ((NewMtu < 68) || (NewMtu > SK_JUMBO_MTU)) {
		return -EINVAL;
	}

	if(pAC->BoardLevel != SK_INIT_RUN) {
		return -EINVAL;
	}

#ifdef SK_DIAG_SUPPORT
	if (pAC->DiagModeActive == DIAG_ACTIVE) {
		if (pAC->DiagFlowCtrl == SK_FALSE) {
			return -1; /* still in use, deny any actions of MTU */
		} else {
			pAC->DiagFlowCtrl = SK_FALSE;
		}
	}
#endif

	pOtherDev = pAC->dev[1 - pNet->NetNr];

	if ( netif_running(pOtherDev) && (pOtherDev->mtu > 1500)
	     && (NewMtu <= 1500))
		return 0;

	pAC->RxBufSize = NewMtu + 32;
	dev->mtu = NewMtu;

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("New MTU: %d\n", NewMtu));

	/* 
	** Prevent any reconfiguration while changing the MTU 
	** by disabling any interrupts 
	*/
	SK_OUT32(pAC->IoBase, B0_IMSK, 0);
	spin_lock_irqsave(&pAC->SlowPathLock, Flags);

	/* 
	** Notify RLMT that any ports are to be stopped
	*/
	EvPara.Para32[0] =  0;
	EvPara.Para32[1] = -1;
	if ((pAC->GIni.GIMacsFound == 2 ) && (pAC->RlmtNets == 2)) {
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_STOP, EvPara);
		EvPara.Para32[0] =  1;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_STOP, EvPara);
	} else {
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_STOP, EvPara);
	}

	/*
	** After calling the SkEventDispatcher(), RLMT is aware about
	** the stopped ports -> configuration can take place!
	*/
	SkEventDispatcher(pAC, pAC->IoBase);

	for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		spin_lock(&pAC->TxPort[i][TX_PRIO_LOW].TxDesRingLock);
		netif_stop_queue(pAC->dev[i]);

	}

	/*
	** Depending on the desired MTU size change, a different number of 
	** RX buffers need to be allocated
	*/
	if (NewMtu > 1500) {
	    /* 
	    ** Use less rx buffers 
	    */
	    for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		if ((pAC->GIni.GIMacsFound == 2 ) && (pAC->RlmtNets == 2)) {
		    pAC->RxPort[i].RxFillLimit =  pAC->RxDescrPerRing -
						 (pAC->RxDescrPerRing / 4);
		} else {
		    if (i == pAC->ActivePort) {
			pAC->RxPort[i].RxFillLimit = pAC->RxDescrPerRing - 
						    (pAC->RxDescrPerRing / 4);
		    } else {
			pAC->RxPort[i].RxFillLimit = pAC->RxDescrPerRing - 
						    (pAC->RxDescrPerRing / 10);
		    }
		}
	    }
	} else {
	    /* 
	    ** Use the normal amount of rx buffers 
	    */
	    for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		if ((pAC->GIni.GIMacsFound == 2 ) && (pAC->RlmtNets == 2)) {
		    pAC->RxPort[i].RxFillLimit = 1;
		} else {
		    if (i == pAC->ActivePort) {
			pAC->RxPort[i].RxFillLimit = 1;
		    } else {
			pAC->RxPort[i].RxFillLimit = pAC->RxDescrPerRing -
						    (pAC->RxDescrPerRing / 4);
		    }
		}
	    }
	}
	
	SkGeDeInit(pAC, pAC->IoBase);

	/*
	** enable/disable hardware support for long frames
	*/
	if (NewMtu > 1500) {
// pAC->JumboActivated = SK_TRUE; /* is never set back !!! */
		pAC->GIni.GIPortUsage = SK_JUMBO_LINK;
	} else {
	    if ((pAC->GIni.GIMacsFound == 2 ) && (pAC->RlmtNets == 2)) {
		pAC->GIni.GIPortUsage = SK_MUL_LINK;
	    } else {
		pAC->GIni.GIPortUsage = SK_RED_LINK;
	    }
	}

	SkGeInit(   pAC, pAC->IoBase, SK_INIT_IO);
	SkI2cInit(  pAC, pAC->IoBase, SK_INIT_IO);
	SkEventInit(pAC, pAC->IoBase, SK_INIT_IO);
	SkPnmiInit( pAC, pAC->IoBase, SK_INIT_IO);
	SkAddrInit( pAC, pAC->IoBase, SK_INIT_IO);
	SkRlmtInit( pAC, pAC->IoBase, SK_INIT_IO);
	SkTimerInit(pAC, pAC->IoBase, SK_INIT_IO);
	
	/*
	** tschilling:
	** Speed and others are set back to default in level 1 init!
	*/
	GetConfiguration(pAC);
	
	SkGeInit(   pAC, pAC->IoBase, SK_INIT_RUN);
	SkI2cInit(  pAC, pAC->IoBase, SK_INIT_RUN);
	SkEventInit(pAC, pAC->IoBase, SK_INIT_RUN);
	SkPnmiInit( pAC, pAC->IoBase, SK_INIT_RUN);
	SkAddrInit( pAC, pAC->IoBase, SK_INIT_RUN);
	SkRlmtInit( pAC, pAC->IoBase, SK_INIT_RUN);
	SkTimerInit(pAC, pAC->IoBase, SK_INIT_RUN);

	/*
	** clear and reinit the rx rings here
	*/
	for (i=0; i<pAC->GIni.GIMacsFound; i++) {
		ReceiveIrq(pAC, &pAC->RxPort[i], SK_TRUE);
		ClearRxRing(pAC, &pAC->RxPort[i]);
		FillRxRing(pAC, &pAC->RxPort[i]);

		/* 
		** Enable transmit descriptor polling
		*/
		SkGePollTxD(pAC, pAC->IoBase, i, SK_TRUE);
		FillRxRing(pAC, &pAC->RxPort[i]);
	};

	SkGeYellowLED(pAC, pAC->IoBase, 1);
	SkDimEnableModerationIfNeeded(pAC);	
	SkDimDisplayModerationSettings(pAC);

	netif_start_queue(pAC->dev[pNet->PortNr]);
	for (i=pAC->GIni.GIMacsFound-1; i>=0; i--) {
		spin_unlock(&pAC->TxPort[i][TX_PRIO_LOW].TxDesRingLock);
	}

	/* 
	** Enable Interrupts again 
	*/
	SK_OUT32(pAC->IoBase, B0_IMSK, pAC->GIni.GIValIrqMask);
	SK_OUT32(pAC->IoBase, B0_HWE_IMSK, IRQ_HWE_MASK);

	SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_START, EvPara);
	SkEventDispatcher(pAC, pAC->IoBase);

	/* 
	** Notify RLMT about the changing and restarting one (or more) ports
	*/
	if ((pAC->GIni.GIMacsFound == 2 ) && (pAC->RlmtNets == 2)) {
		EvPara.Para32[0] = pAC->RlmtNets;
		EvPara.Para32[1] = -1;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_SET_NETS, EvPara);
		EvPara.Para32[0] = pNet->PortNr;
		EvPara.Para32[1] = -1;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_START, EvPara);
			
		if (netif_running(pOtherDev)) {
			DEV_NET *pOtherNet = netdev_priv(pOtherDev);
			EvPara.Para32[0] = pOtherNet->PortNr;
			SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_START, EvPara);
		}
	} else {
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_START, EvPara);
	}

	SkEventDispatcher(pAC, pAC->IoBase);
	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
	
	/*
	** While testing this driver with latest kernel 2.5 (2.5.70), it 
	** seems as if upper layers have a problem to handle a successful
	** return value of '0'. If such a zero is returned, the complete 
	** system hangs for several minutes (!), which is in acceptable.
	**
	** Currently it is not clear, what the exact reason for this problem
	** is. The implemented workaround for 2.5 is to return the desired 
	** new MTU size if all needed changes for the new MTU size where 
	** performed. In kernels 2.2 and 2.4, a zero value is returned,
	** which indicates the successful change of the mtu-size.
	*/
	return NewMtu;

} /* SkGeChangeMtu */


/*****************************************************************************
 *
 * 	SkGeStats - return ethernet device statistics
 *
 * Description:
 *	This function return statistic data about the ethernet device
 *	to the operating system.
 *
 * Returns:
 *	pointer to the statistic structure.
 */
static struct net_device_stats *SkGeStats(struct SK_NET_DEVICE *dev)
{
DEV_NET *pNet = netdev_priv(dev);
SK_AC	*pAC = pNet->pAC;
SK_PNMI_STRUCT_DATA *pPnmiStruct;       /* structure for all Pnmi-Data */
SK_PNMI_STAT    *pPnmiStat;             /* pointer to virtual XMAC stat. data */
SK_PNMI_CONF    *pPnmiConf;             /* pointer to virtual link config. */
unsigned int    Size;                   /* size of pnmi struct */
unsigned long	Flags;			/* for spin lock */

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeStats starts now...\n"));
	pPnmiStruct = &pAC->PnmiStruct;

#ifdef SK_DIAG_SUPPORT
        if ((pAC->DiagModeActive == DIAG_NOTACTIVE) &&
                (pAC->BoardLevel == SK_INIT_RUN)) {
#endif
        SK_MEMSET(pPnmiStruct, 0, sizeof(SK_PNMI_STRUCT_DATA));
        spin_lock_irqsave(&pAC->SlowPathLock, Flags);
        Size = SK_PNMI_STRUCT_SIZE;
		SkPnmiGetStruct(pAC, pAC->IoBase, pPnmiStruct, &Size, pNet->NetNr);
        spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
#ifdef SK_DIAG_SUPPORT
	}
#endif

        pPnmiStat = &pPnmiStruct->Stat[0];
        pPnmiConf = &pPnmiStruct->Conf[0];

	pAC->stats.rx_packets = (SK_U32) pPnmiStruct->RxDeliveredCts & 0xFFFFFFFF;
	pAC->stats.tx_packets = (SK_U32) pPnmiStat->StatTxOkCts & 0xFFFFFFFF;
	pAC->stats.rx_bytes = (SK_U32) pPnmiStruct->RxOctetsDeliveredCts;
	pAC->stats.tx_bytes = (SK_U32) pPnmiStat->StatTxOctetsOkCts;
	
        if (dev->mtu <= 1500) {
                pAC->stats.rx_errors = (SK_U32) pPnmiStruct->InErrorsCts & 0xFFFFFFFF;
        } else {
                pAC->stats.rx_errors = (SK_U32) ((pPnmiStruct->InErrorsCts -
                        pPnmiStat->StatRxTooLongCts) & 0xFFFFFFFF);
	}


	if (pAC->GIni.GP[0].PhyType == SK_PHY_XMAC && pAC->HWRevision < 12)
		pAC->stats.rx_errors = pAC->stats.rx_errors - pPnmiStat->StatRxShortsCts;

	pAC->stats.tx_errors = (SK_U32) pPnmiStat->StatTxSingleCollisionCts & 0xFFFFFFFF;
	pAC->stats.rx_dropped = (SK_U32) pPnmiStruct->RxNoBufCts & 0xFFFFFFFF;
	pAC->stats.tx_dropped = (SK_U32) pPnmiStruct->TxNoBufCts & 0xFFFFFFFF;
	pAC->stats.multicast = (SK_U32) pPnmiStat->StatRxMulticastOkCts & 0xFFFFFFFF;
	pAC->stats.collisions = (SK_U32) pPnmiStat->StatTxSingleCollisionCts & 0xFFFFFFFF;

	/* detailed rx_errors: */
	pAC->stats.rx_length_errors = (SK_U32) pPnmiStat->StatRxRuntCts & 0xFFFFFFFF;
	pAC->stats.rx_over_errors = (SK_U32) pPnmiStat->StatRxFifoOverflowCts & 0xFFFFFFFF;
	pAC->stats.rx_crc_errors = (SK_U32) pPnmiStat->StatRxFcsCts & 0xFFFFFFFF;
	pAC->stats.rx_frame_errors = (SK_U32) pPnmiStat->StatRxFramingCts & 0xFFFFFFFF;
	pAC->stats.rx_fifo_errors = (SK_U32) pPnmiStat->StatRxFifoOverflowCts & 0xFFFFFFFF;
	pAC->stats.rx_missed_errors = (SK_U32) pPnmiStat->StatRxMissedCts & 0xFFFFFFFF;

	/* detailed tx_errors */
	pAC->stats.tx_aborted_errors = (SK_U32) 0;
	pAC->stats.tx_carrier_errors = (SK_U32) pPnmiStat->StatTxCarrierCts & 0xFFFFFFFF;
	pAC->stats.tx_fifo_errors = (SK_U32) pPnmiStat->StatTxFifoUnderrunCts & 0xFFFFFFFF;
	pAC->stats.tx_heartbeat_errors = (SK_U32) pPnmiStat->StatTxCarrierCts & 0xFFFFFFFF;
	pAC->stats.tx_window_errors = (SK_U32) 0;

	return(&pAC->stats);
} /* SkGeStats */

/*
 * Basic MII register access
 */
static int SkGeMiiIoctl(struct net_device *dev,
			struct mii_ioctl_data *data, int cmd)
{
	DEV_NET *pNet = netdev_priv(dev);
	SK_AC *pAC = pNet->pAC;
	SK_IOC IoC = pAC->IoBase;
	int Port = pNet->PortNr;
	SK_GEPORT *pPrt = &pAC->GIni.GP[Port];
	unsigned long Flags;
	int err = 0;
	int reg = data->reg_num & 0x1f;
	SK_U16 val = data->val_in;

	if (!netif_running(dev))
		return -ENODEV;	/* Phy still in reset */

	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	switch(cmd) {
	case SIOCGMIIPHY:
		data->phy_id = pPrt->PhyAddr;

		/* fallthru */
	case SIOCGMIIREG:
		if (pAC->GIni.GIGenesis)
			SkXmPhyRead(pAC, IoC, Port, reg, &val);
		else
			SkGmPhyRead(pAC, IoC, Port, reg, &val);

		data->val_out = val;
		break;

	case SIOCSMIIREG:
		if (!capable(CAP_NET_ADMIN))
			err = -EPERM;

		else if (pAC->GIni.GIGenesis)
			SkXmPhyWrite(pAC, IoC, Port, reg, val);
		else
			SkGmPhyWrite(pAC, IoC, Port, reg, val);
		break;
	default:
		err = -EOPNOTSUPP;
	}
        spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
	return err;
}


/*****************************************************************************
 *
 * 	SkGeIoctl - IO-control function
 *
 * Description:
 *	This function is called if an ioctl is issued on the device.
 *	There are three subfunction for reading, writing and test-writing
 *	the private MIB data structure (useful for SysKonnect-internal tools).
 *
 * Returns:
 *	0, if everything is ok
 *	!=0, on error
 */
static int SkGeIoctl(struct SK_NET_DEVICE *dev, struct ifreq *rq, int cmd)
{
DEV_NET		*pNet;
SK_AC		*pAC;
void		*pMemBuf;
struct pci_dev  *pdev = NULL;
SK_GE_IOCTL	Ioctl;
unsigned int	Err = 0;
int		Size = 0;
int             Ret = 0;
unsigned int	Length = 0;
int		HeaderLength = sizeof(SK_U32) + sizeof(SK_U32);

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeIoctl starts now...\n"));

	pNet = netdev_priv(dev);
	pAC = pNet->pAC;
	
	if (cmd == SIOCGMIIPHY || cmd == SIOCSMIIREG || cmd == SIOCGMIIREG)
	    return SkGeMiiIoctl(dev, if_mii(rq), cmd);

	if(copy_from_user(&Ioctl, rq->ifr_data, sizeof(SK_GE_IOCTL))) {
		return -EFAULT;
	}

	switch(cmd) {
	case SK_IOCTL_SETMIB:
	case SK_IOCTL_PRESETMIB:
		if (!capable(CAP_NET_ADMIN)) return -EPERM;
 	case SK_IOCTL_GETMIB:
		if(copy_from_user(&pAC->PnmiStruct, Ioctl.pData,
			Ioctl.Len<sizeof(pAC->PnmiStruct)?
			Ioctl.Len : sizeof(pAC->PnmiStruct))) {
			return -EFAULT;
		}
		Size = SkGeIocMib(pNet, Ioctl.Len, cmd);
		if(copy_to_user(Ioctl.pData, &pAC->PnmiStruct,
			Ioctl.Len<Size? Ioctl.Len : Size)) {
			return -EFAULT;
		}
		Ioctl.Len = Size;
		if(copy_to_user(rq->ifr_data, &Ioctl, sizeof(SK_GE_IOCTL))) {
			return -EFAULT;
		}
		break;
	case SK_IOCTL_GEN:
		if (Ioctl.Len < (sizeof(pAC->PnmiStruct) + HeaderLength)) {
			Length = Ioctl.Len;
		} else {
			Length = sizeof(pAC->PnmiStruct) + HeaderLength;
		}
		if (NULL == (pMemBuf = kmalloc(Length, GFP_KERNEL))) {
			return -ENOMEM;
		}
		if(copy_from_user(pMemBuf, Ioctl.pData, Length)) {
			Err = -EFAULT;
			goto fault_gen;
		}
		if ((Ret = SkPnmiGenIoctl(pAC, pAC->IoBase, pMemBuf, &Length, 0)) < 0) {
			Err = -EFAULT;
			goto fault_gen;
		}
		if(copy_to_user(Ioctl.pData, pMemBuf, Length) ) {
			Err = -EFAULT;
			goto fault_gen;
		}
		Ioctl.Len = Length;
		if(copy_to_user(rq->ifr_data, &Ioctl, sizeof(SK_GE_IOCTL))) {
			Err = -EFAULT;
			goto fault_gen;
		}
fault_gen:
		kfree(pMemBuf); /* cleanup everything */
		break;
#ifdef SK_DIAG_SUPPORT
       case SK_IOCTL_DIAG:
		if (!capable(CAP_NET_ADMIN)) return -EPERM;
		if (Ioctl.Len < (sizeof(pAC->PnmiStruct) + HeaderLength)) {
			Length = Ioctl.Len;
		} else {
			Length = sizeof(pAC->PnmiStruct) + HeaderLength;
		}
		if (NULL == (pMemBuf = kmalloc(Length, GFP_KERNEL))) {
			return -ENOMEM;
		}
		if(copy_from_user(pMemBuf, Ioctl.pData, Length)) {
			Err = -EFAULT;
			goto fault_diag;
		}
		pdev = pAC->PciDev;
		Length = 3 * sizeof(SK_U32);  /* Error, Bus and Device */
		/* 
		** While coding this new IOCTL interface, only a few lines of code
		** are to to be added. Therefore no dedicated function has been 
		** added. If more functionality is added, a separate function 
		** should be used...
		*/
		* ((SK_U32 *)pMemBuf) = 0;
		* ((SK_U32 *)pMemBuf + 1) = pdev->bus->number;
		* ((SK_U32 *)pMemBuf + 2) = ParseDeviceNbrFromSlotName(pci_name(pdev));
		if(copy_to_user(Ioctl.pData, pMemBuf, Length) ) {
			Err = -EFAULT;
			goto fault_diag;
		}
		Ioctl.Len = Length;
		if(copy_to_user(rq->ifr_data, &Ioctl, sizeof(SK_GE_IOCTL))) {
			Err = -EFAULT;
			goto fault_diag;
		}
fault_diag:
		kfree(pMemBuf); /* cleanup everything */
		break;
#endif
	default:
		Err = -EOPNOTSUPP;
	}

	return(Err);

} /* SkGeIoctl */


/*****************************************************************************
 *
 * 	SkGeIocMib - handle a GetMib, SetMib- or PresetMib-ioctl message
 *
 * Description:
 *	This function reads/writes the MIB data using PNMI (Private Network
 *	Management Interface).
 *	The destination for the data must be provided with the
 *	ioctl call and is given to the driver in the form of
 *	a user space address.
 *	Copying from the user-provided data area into kernel messages
 *	and back is done by copy_from_user and copy_to_user calls in
 *	SkGeIoctl.
 *
 * Returns:
 *	returned size from PNMI call
 */
static int SkGeIocMib(
DEV_NET		*pNet,	/* pointer to the adapter context */
unsigned int	Size,	/* length of ioctl data */
int		mode)	/* flag for set/preset */
{
unsigned long	Flags;	/* for spin lock */
SK_AC		*pAC;

	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("SkGeIocMib starts now...\n"));
	pAC = pNet->pAC;
	/* access MIB */
	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	switch(mode) {
	case SK_IOCTL_GETMIB:
		SkPnmiGetStruct(pAC, pAC->IoBase, &pAC->PnmiStruct, &Size,
			pNet->NetNr);
		break;
	case SK_IOCTL_PRESETMIB:
		SkPnmiPreSetStruct(pAC, pAC->IoBase, &pAC->PnmiStruct, &Size,
			pNet->NetNr);
		break;
	case SK_IOCTL_SETMIB:
		SkPnmiSetStruct(pAC, pAC->IoBase, &pAC->PnmiStruct, &Size,
			pNet->NetNr);
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_ENTRY,
		("MIB data access succeeded\n"));
	return (Size);
} /* SkGeIocMib */


/*****************************************************************************
 *
 * 	GetConfiguration - read configuration information
 *
 * Description:
 *	This function reads per-adapter configuration information from
 *	the options provided on the command line.
 *
 * Returns:
 *	none
 */
static void GetConfiguration(
SK_AC	*pAC)	/* pointer to the adapter context structure */
{
SK_I32	Port;		/* preferred port */
SK_BOOL	AutoSet;
SK_BOOL DupSet;
int	LinkSpeed          = SK_LSPEED_AUTO;	/* Link speed */
int	AutoNeg            = 1;			/* autoneg off (0) or on (1) */
int	DuplexCap          = 0;			/* 0=both,1=full,2=half */
int	FlowCtrl           = SK_FLOW_MODE_SYM_OR_REM;	/* FlowControl  */
int	MSMode             = SK_MS_MODE_AUTO;	/* master/slave mode    */

SK_BOOL IsConTypeDefined   = SK_TRUE;
SK_BOOL IsLinkSpeedDefined = SK_TRUE;
SK_BOOL IsFlowCtrlDefined  = SK_TRUE;
SK_BOOL IsRoleDefined      = SK_TRUE;
SK_BOOL IsModeDefined      = SK_TRUE;
/*
 *	The two parameters AutoNeg. and DuplexCap. map to one configuration
 *	parameter. The mapping is described by this table:
 *	DuplexCap ->	|	both	|	full	|	half	|
 *	AutoNeg		|		|		|		|
 *	-----------------------------------------------------------------
 *	Off		|    illegal	|	Full	|	Half	|
 *	-----------------------------------------------------------------
 *	On		|   AutoBoth	|   AutoFull	|   AutoHalf	|
 *	-----------------------------------------------------------------
 *	Sense		|   AutoSense	|   AutoSense	|   AutoSense	|
 */
int	Capabilities[3][3] =
		{ {                -1, SK_LMODE_FULL     , SK_LMODE_HALF     },
		  {SK_LMODE_AUTOBOTH , SK_LMODE_AUTOFULL , SK_LMODE_AUTOHALF },
		  {SK_LMODE_AUTOSENSE, SK_LMODE_AUTOSENSE, SK_LMODE_AUTOSENSE} };

#define DC_BOTH	0
#define DC_FULL 1
#define DC_HALF 2
#define AN_OFF	0
#define AN_ON	1
#define AN_SENS	2
#define M_CurrPort pAC->GIni.GP[Port]


	/*
	** Set the default values first for both ports!
	*/
	for (Port = 0; Port < SK_MAX_MACS; Port++) {
		M_CurrPort.PLinkModeConf = Capabilities[AN_ON][DC_BOTH];
		M_CurrPort.PFlowCtrlMode = SK_FLOW_MODE_SYM_OR_REM;
		M_CurrPort.PMSMode       = SK_MS_MODE_AUTO;
		M_CurrPort.PLinkSpeed    = SK_LSPEED_AUTO;
	}

	/*
	** Check merged parameter ConType. If it has not been used,
	** verify any other parameter (e.g. AutoNeg) and use default values. 
	**
	** Stating both ConType and other lowlevel link parameters is also
	** possible. If this is the case, the passed ConType-parameter is 
	** overwritten by the lowlevel link parameter.
	**
	** The following settings are used for a merged ConType-parameter:
	**
	** ConType   DupCap   AutoNeg   FlowCtrl      Role      Speed
	** -------   ------   -------   --------   ----------   -----
	**  Auto      Both      On      SymOrRem      Auto       Auto
	**  100FD     Full      Off       None      <ignored>    100
	**  100HD     Half      Off       None      <ignored>    100
	**  10FD      Full      Off       None      <ignored>    10
	**  10HD      Half      Off       None      <ignored>    10
	** 
	** This ConType parameter is used for all ports of the adapter!
	*/
        if ( (ConType != NULL)                && 
	     (pAC->Index < SK_MAX_CARD_PARAM) &&
	     (ConType[pAC->Index] != NULL) ) {

			/* Check chipset family */
			if ((!pAC->ChipsetType) && 
				(strcmp(ConType[pAC->Index],"Auto")!=0) &&
				(strcmp(ConType[pAC->Index],"")!=0)) {
				/* Set the speed parameter back */
					printk("sk98lin: Illegal value \"%s\" " 
							"for ConType."
							" Using Auto.\n", 
							ConType[pAC->Index]);

					sprintf(ConType[pAC->Index], "Auto");	
			}

				if (strcmp(ConType[pAC->Index],"")==0) {
			IsConTypeDefined = SK_FALSE; /* No ConType defined */
				} else if (strcmp(ConType[pAC->Index],"Auto")==0) {
		    for (Port = 0; Port < SK_MAX_MACS; Port++) {
			M_CurrPort.PLinkModeConf = Capabilities[AN_ON][DC_BOTH];
			M_CurrPort.PFlowCtrlMode = SK_FLOW_MODE_SYM_OR_REM;
			M_CurrPort.PMSMode       = SK_MS_MODE_AUTO;
			M_CurrPort.PLinkSpeed    = SK_LSPEED_AUTO;
		    }
                } else if (strcmp(ConType[pAC->Index],"100FD")==0) {
		    for (Port = 0; Port < SK_MAX_MACS; Port++) {
			M_CurrPort.PLinkModeConf = Capabilities[AN_OFF][DC_FULL];
			M_CurrPort.PFlowCtrlMode = SK_FLOW_MODE_NONE;
			M_CurrPort.PMSMode       = SK_MS_MODE_AUTO;
			M_CurrPort.PLinkSpeed    = SK_LSPEED_100MBPS;
		    }
                } else if (strcmp(ConType[pAC->Index],"100HD")==0) {
		    for (Port = 0; Port < SK_MAX_MACS; Port++) {
			M_CurrPort.PLinkModeConf = Capabilities[AN_OFF][DC_HALF];
			M_CurrPort.PFlowCtrlMode = SK_FLOW_MODE_NONE;
			M_CurrPort.PMSMode       = SK_MS_MODE_AUTO;
			M_CurrPort.PLinkSpeed    = SK_LSPEED_100MBPS;
		    }
                } else if (strcmp(ConType[pAC->Index],"10FD")==0) {
		    for (Port = 0; Port < SK_MAX_MACS; Port++) {
			M_CurrPort.PLinkModeConf = Capabilities[AN_OFF][DC_FULL];
			M_CurrPort.PFlowCtrlMode = SK_FLOW_MODE_NONE;
			M_CurrPort.PMSMode       = SK_MS_MODE_AUTO;
			M_CurrPort.PLinkSpeed    = SK_LSPEED_10MBPS;
		    }
                } else if (strcmp(ConType[pAC->Index],"10HD")==0) {
		    for (Port = 0; Port < SK_MAX_MACS; Port++) {
			M_CurrPort.PLinkModeConf = Capabilities[AN_OFF][DC_HALF];
			M_CurrPort.PFlowCtrlMode = SK_FLOW_MODE_NONE;
			M_CurrPort.PMSMode       = SK_MS_MODE_AUTO;
			M_CurrPort.PLinkSpeed    = SK_LSPEED_10MBPS;
		    }
                } else { 
		    printk("sk98lin: Illegal value \"%s\" for ConType\n", 
			ConType[pAC->Index]);
		    IsConTypeDefined = SK_FALSE; /* Wrong ConType defined */
		}
        } else {
	    IsConTypeDefined = SK_FALSE; /* No ConType defined */
	}

	/*
	** Parse any parameter settings for port A:
	** a) any LinkSpeed stated?
	*/
	if (Speed_A != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		Speed_A[pAC->Index] != NULL) {
		if (strcmp(Speed_A[pAC->Index],"")==0) {
		    IsLinkSpeedDefined = SK_FALSE;
		} else if (strcmp(Speed_A[pAC->Index],"Auto")==0) {
		    LinkSpeed = SK_LSPEED_AUTO;
		} else if (strcmp(Speed_A[pAC->Index],"10")==0) {
		    LinkSpeed = SK_LSPEED_10MBPS;
		} else if (strcmp(Speed_A[pAC->Index],"100")==0) {
		    LinkSpeed = SK_LSPEED_100MBPS;
		} else if (strcmp(Speed_A[pAC->Index],"1000")==0) {
		    LinkSpeed = SK_LSPEED_1000MBPS;
		} else {
		    printk("sk98lin: Illegal value \"%s\" for Speed_A\n",
			Speed_A[pAC->Index]);
		    IsLinkSpeedDefined = SK_FALSE;
		}
	} else {
	    IsLinkSpeedDefined = SK_FALSE;
	}

	/* 
	** Check speed parameter: 
	**    Only copper type adapter and GE V2 cards 
	*/
	if (((!pAC->ChipsetType) || (pAC->GIni.GICopperType != SK_TRUE)) &&
		((LinkSpeed != SK_LSPEED_AUTO) &&
		(LinkSpeed != SK_LSPEED_1000MBPS))) {
		printk("sk98lin: Illegal value for Speed_A. "
			"Not a copper card or GE V2 card\n    Using "
			"speed 1000\n");
		LinkSpeed = SK_LSPEED_1000MBPS;
	}
	
	/*	
	** Decide whether to set new config value if somethig valid has
	** been received.
	*/
	if (IsLinkSpeedDefined) {
		pAC->GIni.GP[0].PLinkSpeed = LinkSpeed;
	} 

	/* 
	** b) Any Autonegotiation and DuplexCapabilities set?
	**    Please note that both belong together...
	*/
	AutoNeg = AN_ON; /* tschilling: Default: Autonegotiation on! */
	AutoSet = SK_FALSE;
	if (AutoNeg_A != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		AutoNeg_A[pAC->Index] != NULL) {
		AutoSet = SK_TRUE;
		if (strcmp(AutoNeg_A[pAC->Index],"")==0) {
		    AutoSet = SK_FALSE;
		} else if (strcmp(AutoNeg_A[pAC->Index],"On")==0) {
		    AutoNeg = AN_ON;
		} else if (strcmp(AutoNeg_A[pAC->Index],"Off")==0) {
		    AutoNeg = AN_OFF;
		} else if (strcmp(AutoNeg_A[pAC->Index],"Sense")==0) {
		    AutoNeg = AN_SENS;
		} else {
		    printk("sk98lin: Illegal value \"%s\" for AutoNeg_A\n",
			AutoNeg_A[pAC->Index]);
		}
	}

	DuplexCap = DC_BOTH;
	DupSet    = SK_FALSE;
	if (DupCap_A != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		DupCap_A[pAC->Index] != NULL) {
		DupSet = SK_TRUE;
		if (strcmp(DupCap_A[pAC->Index],"")==0) {
		    DupSet = SK_FALSE;
		} else if (strcmp(DupCap_A[pAC->Index],"Both")==0) {
		    DuplexCap = DC_BOTH;
		} else if (strcmp(DupCap_A[pAC->Index],"Full")==0) {
		    DuplexCap = DC_FULL;
		} else if (strcmp(DupCap_A[pAC->Index],"Half")==0) {
		    DuplexCap = DC_HALF;
		} else {
		    printk("sk98lin: Illegal value \"%s\" for DupCap_A\n",
			DupCap_A[pAC->Index]);
		}
	}

	/* 
	** Check for illegal combinations 
	*/
	if ((LinkSpeed == SK_LSPEED_1000MBPS) &&
		((DuplexCap == SK_LMODE_STAT_AUTOHALF) ||
		(DuplexCap == SK_LMODE_STAT_HALF)) &&
		(pAC->ChipsetType)) {
		    printk("sk98lin: Half Duplex not possible with Gigabit speed!\n"
					"    Using Full Duplex.\n");
				DuplexCap = DC_FULL;
	}

	if ( AutoSet && AutoNeg==AN_SENS && DupSet) {
		printk("sk98lin, Port A: DuplexCapabilities"
			" ignored using Sense mode\n");
	}

	if (AutoSet && AutoNeg==AN_OFF && DupSet && DuplexCap==DC_BOTH){
		printk("sk98lin: Port A: Illegal combination"
			" of values AutoNeg. and DuplexCap.\n    Using "
			"Full Duplex\n");
		DuplexCap = DC_FULL;
	}

	if (AutoSet && AutoNeg==AN_OFF && !DupSet) {
		DuplexCap = DC_FULL;
	}
	
	if (!AutoSet && DupSet) {
		printk("sk98lin: Port A: Duplex setting not"
			" possible in\n    default AutoNegotiation mode"
			" (Sense).\n    Using AutoNegotiation On\n");
		AutoNeg = AN_ON;
	}
	
	/* 
	** set the desired mode 
	*/
	if (AutoSet || DupSet) {
	    pAC->GIni.GP[0].PLinkModeConf = Capabilities[AutoNeg][DuplexCap];
	}
	
	/* 
	** c) Any Flowcontrol-parameter set?
	*/
	if (FlowCtrl_A != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		FlowCtrl_A[pAC->Index] != NULL) {
		if (strcmp(FlowCtrl_A[pAC->Index],"") == 0) {
		    IsFlowCtrlDefined = SK_FALSE;
		} else if (strcmp(FlowCtrl_A[pAC->Index],"SymOrRem") == 0) {
		    FlowCtrl = SK_FLOW_MODE_SYM_OR_REM;
		} else if (strcmp(FlowCtrl_A[pAC->Index],"Sym")==0) {
		    FlowCtrl = SK_FLOW_MODE_SYMMETRIC;
		} else if (strcmp(FlowCtrl_A[pAC->Index],"LocSend")==0) {
		    FlowCtrl = SK_FLOW_MODE_LOC_SEND;
		} else if (strcmp(FlowCtrl_A[pAC->Index],"None")==0) {
		    FlowCtrl = SK_FLOW_MODE_NONE;
		} else {
		    printk("sk98lin: Illegal value \"%s\" for FlowCtrl_A\n",
                        FlowCtrl_A[pAC->Index]);
		    IsFlowCtrlDefined = SK_FALSE;
		}
	} else {
	   IsFlowCtrlDefined = SK_FALSE;
	}

	if (IsFlowCtrlDefined) {
	    if ((AutoNeg == AN_OFF) && (FlowCtrl != SK_FLOW_MODE_NONE)) {
		printk("sk98lin: Port A: FlowControl"
			" impossible without AutoNegotiation,"
			" disabled\n");
		FlowCtrl = SK_FLOW_MODE_NONE;
	    }
	    pAC->GIni.GP[0].PFlowCtrlMode = FlowCtrl;
	}

	/*
	** d) What is with the RoleParameter?
	*/
	if (Role_A != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		Role_A[pAC->Index] != NULL) {
		if (strcmp(Role_A[pAC->Index],"")==0) {
		   IsRoleDefined = SK_FALSE;
		} else if (strcmp(Role_A[pAC->Index],"Auto")==0) {
		    MSMode = SK_MS_MODE_AUTO;
		} else if (strcmp(Role_A[pAC->Index],"Master")==0) {
		    MSMode = SK_MS_MODE_MASTER;
		} else if (strcmp(Role_A[pAC->Index],"Slave")==0) {
		    MSMode = SK_MS_MODE_SLAVE;
		} else {
		    printk("sk98lin: Illegal value \"%s\" for Role_A\n",
			Role_A[pAC->Index]);
		    IsRoleDefined = SK_FALSE;
		}
	} else {
	   IsRoleDefined = SK_FALSE;
	}

	if (IsRoleDefined == SK_TRUE) {
	    pAC->GIni.GP[0].PMSMode = MSMode;
	}
	

	
	/* 
	** Parse any parameter settings for port B:
	** a) any LinkSpeed stated?
	*/
	IsConTypeDefined   = SK_TRUE;
	IsLinkSpeedDefined = SK_TRUE;
	IsFlowCtrlDefined  = SK_TRUE;
	IsModeDefined      = SK_TRUE;

	if (Speed_B != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		Speed_B[pAC->Index] != NULL) {
		if (strcmp(Speed_B[pAC->Index],"")==0) {
		    IsLinkSpeedDefined = SK_FALSE;
		} else if (strcmp(Speed_B[pAC->Index],"Auto")==0) {
		    LinkSpeed = SK_LSPEED_AUTO;
		} else if (strcmp(Speed_B[pAC->Index],"10")==0) {
		    LinkSpeed = SK_LSPEED_10MBPS;
		} else if (strcmp(Speed_B[pAC->Index],"100")==0) {
		    LinkSpeed = SK_LSPEED_100MBPS;
		} else if (strcmp(Speed_B[pAC->Index],"1000")==0) {
		    LinkSpeed = SK_LSPEED_1000MBPS;
		} else {
		    printk("sk98lin: Illegal value \"%s\" for Speed_B\n",
			Speed_B[pAC->Index]);
		    IsLinkSpeedDefined = SK_FALSE;
		}
	} else {
	    IsLinkSpeedDefined = SK_FALSE;
	}

	/* 
	** Check speed parameter:
	**    Only copper type adapter and GE V2 cards 
	*/
	if (((!pAC->ChipsetType) || (pAC->GIni.GICopperType != SK_TRUE)) &&
		((LinkSpeed != SK_LSPEED_AUTO) &&
		(LinkSpeed != SK_LSPEED_1000MBPS))) {
		printk("sk98lin: Illegal value for Speed_B. "
			"Not a copper card or GE V2 card\n    Using "
			"speed 1000\n");
		LinkSpeed = SK_LSPEED_1000MBPS;
	}

	/*      
	** Decide whether to set new config value if somethig valid has
	** been received.
	*/
        if (IsLinkSpeedDefined) {
	    pAC->GIni.GP[1].PLinkSpeed = LinkSpeed;
	}

	/* 
	** b) Any Autonegotiation and DuplexCapabilities set?
	**    Please note that both belong together...
	*/
	AutoNeg = AN_SENS; /* default: do auto Sense */
	AutoSet = SK_FALSE;
	if (AutoNeg_B != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		AutoNeg_B[pAC->Index] != NULL) {
		AutoSet = SK_TRUE;
		if (strcmp(AutoNeg_B[pAC->Index],"")==0) {
		    AutoSet = SK_FALSE;
		} else if (strcmp(AutoNeg_B[pAC->Index],"On")==0) {
		    AutoNeg = AN_ON;
		} else if (strcmp(AutoNeg_B[pAC->Index],"Off")==0) {
		    AutoNeg = AN_OFF;
		} else if (strcmp(AutoNeg_B[pAC->Index],"Sense")==0) {
		    AutoNeg = AN_SENS;
		} else {
		    printk("sk98lin: Illegal value \"%s\" for AutoNeg_B\n",
			AutoNeg_B[pAC->Index]);
		}
	}

	DuplexCap = DC_BOTH;
	DupSet    = SK_FALSE;
	if (DupCap_B != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		DupCap_B[pAC->Index] != NULL) {
		DupSet = SK_TRUE;
		if (strcmp(DupCap_B[pAC->Index],"")==0) {
		    DupSet = SK_FALSE;
		} else if (strcmp(DupCap_B[pAC->Index],"Both")==0) {
		    DuplexCap = DC_BOTH;
		} else if (strcmp(DupCap_B[pAC->Index],"Full")==0) {
		    DuplexCap = DC_FULL;
		} else if (strcmp(DupCap_B[pAC->Index],"Half")==0) {
		    DuplexCap = DC_HALF;
		} else {
		    printk("sk98lin: Illegal value \"%s\" for DupCap_B\n",
			DupCap_B[pAC->Index]);
		}
	}

	
	/* 
	** Check for illegal combinations 
	*/
	if ((LinkSpeed == SK_LSPEED_1000MBPS) &&
		((DuplexCap == SK_LMODE_STAT_AUTOHALF) ||
		(DuplexCap == SK_LMODE_STAT_HALF)) &&
		(pAC->ChipsetType)) {
		    printk("sk98lin: Half Duplex not possible with Gigabit speed!\n"
					"    Using Full Duplex.\n");
				DuplexCap = DC_FULL;
	}

	if (AutoSet && AutoNeg==AN_SENS && DupSet) {
		printk("sk98lin, Port B: DuplexCapabilities"
			" ignored using Sense mode\n");
	}

	if (AutoSet && AutoNeg==AN_OFF && DupSet && DuplexCap==DC_BOTH){
		printk("sk98lin: Port B: Illegal combination"
			" of values AutoNeg. and DuplexCap.\n    Using "
			"Full Duplex\n");
		DuplexCap = DC_FULL;
	}

	if (AutoSet && AutoNeg==AN_OFF && !DupSet) {
		DuplexCap = DC_FULL;
	}
	
	if (!AutoSet && DupSet) {
		printk("sk98lin: Port B: Duplex setting not"
			" possible in\n    default AutoNegotiation mode"
			" (Sense).\n    Using AutoNegotiation On\n");
		AutoNeg = AN_ON;
	}

	/* 
	** set the desired mode 
	*/
	if (AutoSet || DupSet) {
	    pAC->GIni.GP[1].PLinkModeConf = Capabilities[AutoNeg][DuplexCap];
	}

	/*
	** c) Any FlowCtrl parameter set?
	*/
	if (FlowCtrl_B != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		FlowCtrl_B[pAC->Index] != NULL) {
		if (strcmp(FlowCtrl_B[pAC->Index],"") == 0) {
		    IsFlowCtrlDefined = SK_FALSE;
		} else if (strcmp(FlowCtrl_B[pAC->Index],"SymOrRem") == 0) {
		    FlowCtrl = SK_FLOW_MODE_SYM_OR_REM;
		} else if (strcmp(FlowCtrl_B[pAC->Index],"Sym")==0) {
		    FlowCtrl = SK_FLOW_MODE_SYMMETRIC;
		} else if (strcmp(FlowCtrl_B[pAC->Index],"LocSend")==0) {
		    FlowCtrl = SK_FLOW_MODE_LOC_SEND;
		} else if (strcmp(FlowCtrl_B[pAC->Index],"None")==0) {
		    FlowCtrl = SK_FLOW_MODE_NONE;
		} else {
		    printk("sk98lin: Illegal value \"%s\" for FlowCtrl_B\n",
			FlowCtrl_B[pAC->Index]);
		    IsFlowCtrlDefined = SK_FALSE;
		}
	} else {
		IsFlowCtrlDefined = SK_FALSE;
	}

	if (IsFlowCtrlDefined) {
	    if ((AutoNeg == AN_OFF) && (FlowCtrl != SK_FLOW_MODE_NONE)) {
		printk("sk98lin: Port B: FlowControl"
			" impossible without AutoNegotiation,"
			" disabled\n");
		FlowCtrl = SK_FLOW_MODE_NONE;
	    }
	    pAC->GIni.GP[1].PFlowCtrlMode = FlowCtrl;
	}

	/*
	** d) What is the RoleParameter?
	*/
	if (Role_B != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		Role_B[pAC->Index] != NULL) {
		if (strcmp(Role_B[pAC->Index],"")==0) {
		    IsRoleDefined = SK_FALSE;
		} else if (strcmp(Role_B[pAC->Index],"Auto")==0) {
		    MSMode = SK_MS_MODE_AUTO;
		} else if (strcmp(Role_B[pAC->Index],"Master")==0) {
		    MSMode = SK_MS_MODE_MASTER;
		} else if (strcmp(Role_B[pAC->Index],"Slave")==0) {
		    MSMode = SK_MS_MODE_SLAVE;
		} else {
		    printk("sk98lin: Illegal value \"%s\" for Role_B\n",
			Role_B[pAC->Index]);
		    IsRoleDefined = SK_FALSE;
		}
	} else {
	    IsRoleDefined = SK_FALSE;
	}

	if (IsRoleDefined) {
	    pAC->GIni.GP[1].PMSMode = MSMode;
	}
	
	/*
	** Evaluate settings for both ports
	*/
	pAC->ActivePort = 0;
	if (PrefPort != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		PrefPort[pAC->Index] != NULL) {
		if (strcmp(PrefPort[pAC->Index],"") == 0) { /* Auto */
			pAC->ActivePort             =  0;
			pAC->Rlmt.Net[0].Preference = -1; /* auto */
			pAC->Rlmt.Net[0].PrefPort   =  0;
		} else if (strcmp(PrefPort[pAC->Index],"A") == 0) {
			/*
			** do not set ActivePort here, thus a port
			** switch is issued after net up.
			*/
			Port                        = 0;
			pAC->Rlmt.Net[0].Preference = Port;
			pAC->Rlmt.Net[0].PrefPort   = Port;
		} else if (strcmp(PrefPort[pAC->Index],"B") == 0) {
			/*
			** do not set ActivePort here, thus a port
			** switch is issued after net up.
			*/
			if (pAC->GIni.GIMacsFound == 1) {
				printk("sk98lin: Illegal value \"B\" for PrefPort.\n"
					"      Port B not available on single port adapters.\n");

				pAC->ActivePort             =  0;
				pAC->Rlmt.Net[0].Preference = -1; /* auto */
				pAC->Rlmt.Net[0].PrefPort   =  0;
			} else {
				Port                        = 1;
				pAC->Rlmt.Net[0].Preference = Port;
				pAC->Rlmt.Net[0].PrefPort   = Port;
			}
		} else {
		    printk("sk98lin: Illegal value \"%s\" for PrefPort\n",
			PrefPort[pAC->Index]);
		}
	}

	pAC->RlmtNets = 1;

	if (RlmtMode != NULL && pAC->Index<SK_MAX_CARD_PARAM &&
		RlmtMode[pAC->Index] != NULL) {
		if (strcmp(RlmtMode[pAC->Index], "") == 0) {
			pAC->RlmtMode = 0;
		} else if (strcmp(RlmtMode[pAC->Index], "CheckLinkState") == 0) {
			pAC->RlmtMode = SK_RLMT_CHECK_LINK;
		} else if (strcmp(RlmtMode[pAC->Index], "CheckLocalPort") == 0) {
			pAC->RlmtMode = SK_RLMT_CHECK_LINK |
					SK_RLMT_CHECK_LOC_LINK;
		} else if (strcmp(RlmtMode[pAC->Index], "CheckSeg") == 0) {
			pAC->RlmtMode = SK_RLMT_CHECK_LINK     |
					SK_RLMT_CHECK_LOC_LINK |
					SK_RLMT_CHECK_SEG;
		} else if ((strcmp(RlmtMode[pAC->Index], "DualNet") == 0) &&
			(pAC->GIni.GIMacsFound == 2)) {
			pAC->RlmtMode = SK_RLMT_CHECK_LINK;
			pAC->RlmtNets = 2;
		} else {
		    printk("sk98lin: Illegal value \"%s\" for"
			" RlmtMode, using default\n", 
			RlmtMode[pAC->Index]);
			pAC->RlmtMode = 0;
		}
	} else {
		pAC->RlmtMode = 0;
	}
	
	/*
	** Check the interrupt moderation parameters
	*/
	if (Moderation[pAC->Index] != NULL) {
		if (strcmp(Moderation[pAC->Index], "") == 0) {
			pAC->DynIrqModInfo.IntModTypeSelect = C_INT_MOD_NONE;
		} else if (strcmp(Moderation[pAC->Index], "Static") == 0) {
			pAC->DynIrqModInfo.IntModTypeSelect = C_INT_MOD_STATIC;
		} else if (strcmp(Moderation[pAC->Index], "Dynamic") == 0) {
			pAC->DynIrqModInfo.IntModTypeSelect = C_INT_MOD_DYNAMIC;
		} else if (strcmp(Moderation[pAC->Index], "None") == 0) {
			pAC->DynIrqModInfo.IntModTypeSelect = C_INT_MOD_NONE;
		} else {
	   		printk("sk98lin: Illegal value \"%s\" for Moderation.\n"
				"      Disable interrupt moderation.\n",
				Moderation[pAC->Index]);
			pAC->DynIrqModInfo.IntModTypeSelect = C_INT_MOD_NONE;
		}
	} else {
		pAC->DynIrqModInfo.IntModTypeSelect = C_INT_MOD_NONE;
	}

	if (Stats[pAC->Index] != NULL) {
		if (strcmp(Stats[pAC->Index], "Yes") == 0) {
			pAC->DynIrqModInfo.DisplayStats = SK_TRUE;
		} else {
			pAC->DynIrqModInfo.DisplayStats = SK_FALSE;
		}
	} else {
		pAC->DynIrqModInfo.DisplayStats = SK_FALSE;
	}

	if (ModerationMask[pAC->Index] != NULL) {
		if (strcmp(ModerationMask[pAC->Index], "Rx") == 0) {
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_RX_ONLY;
		} else if (strcmp(ModerationMask[pAC->Index], "Tx") == 0) {
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_TX_ONLY;
		} else if (strcmp(ModerationMask[pAC->Index], "Sp") == 0) {
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_SP_ONLY;
		} else if (strcmp(ModerationMask[pAC->Index], "RxSp") == 0) {
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_SP_RX;
		} else if (strcmp(ModerationMask[pAC->Index], "SpRx") == 0) {
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_SP_RX;
		} else if (strcmp(ModerationMask[pAC->Index], "RxTx") == 0) {
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_TX_RX;
		} else if (strcmp(ModerationMask[pAC->Index], "TxRx") == 0) {
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_TX_RX;
		} else if (strcmp(ModerationMask[pAC->Index], "TxSp") == 0) {
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_SP_TX;
		} else if (strcmp(ModerationMask[pAC->Index], "SpTx") == 0) {
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_SP_TX;
		} else if (strcmp(ModerationMask[pAC->Index], "RxTxSp") == 0) {
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_RX_TX_SP;
		} else if (strcmp(ModerationMask[pAC->Index], "RxSpTx") == 0) {
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_RX_TX_SP;
		} else if (strcmp(ModerationMask[pAC->Index], "TxRxSp") == 0) {
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_RX_TX_SP;
		} else if (strcmp(ModerationMask[pAC->Index], "TxSpRx") == 0) {
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_RX_TX_SP;
		} else if (strcmp(ModerationMask[pAC->Index], "SpTxRx") == 0) {
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_RX_TX_SP;
		} else if (strcmp(ModerationMask[pAC->Index], "SpRxTx") == 0) {
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_RX_TX_SP;
		} else { /* some rubbish */
			pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_RX_ONLY;
		}
	} else {  /* operator has stated nothing */
		pAC->DynIrqModInfo.MaskIrqModeration = IRQ_MASK_TX_RX;
	}

	if (AutoSizing[pAC->Index] != NULL) {
		if (strcmp(AutoSizing[pAC->Index], "On") == 0) {
			pAC->DynIrqModInfo.AutoSizing = SK_FALSE;
		} else {
			pAC->DynIrqModInfo.AutoSizing = SK_FALSE;
		}
	} else {  /* operator has stated nothing */
		pAC->DynIrqModInfo.AutoSizing = SK_FALSE;
	}

	if (IntsPerSec[pAC->Index] != 0) {
		if ((IntsPerSec[pAC->Index]< C_INT_MOD_IPS_LOWER_RANGE) || 
			(IntsPerSec[pAC->Index] > C_INT_MOD_IPS_UPPER_RANGE)) {
	   		printk("sk98lin: Illegal value \"%d\" for IntsPerSec. (Range: %d - %d)\n"
				"      Using default value of %i.\n", 
				IntsPerSec[pAC->Index],
				C_INT_MOD_IPS_LOWER_RANGE,
				C_INT_MOD_IPS_UPPER_RANGE,
				C_INTS_PER_SEC_DEFAULT);
			pAC->DynIrqModInfo.MaxModIntsPerSec = C_INTS_PER_SEC_DEFAULT;
		} else {
			pAC->DynIrqModInfo.MaxModIntsPerSec = IntsPerSec[pAC->Index];
		}
	} else {
		pAC->DynIrqModInfo.MaxModIntsPerSec = C_INTS_PER_SEC_DEFAULT;
	}

	/*
	** Evaluate upper and lower moderation threshold
	*/
	pAC->DynIrqModInfo.MaxModIntsPerSecUpperLimit =
		pAC->DynIrqModInfo.MaxModIntsPerSec +
		(pAC->DynIrqModInfo.MaxModIntsPerSec / 2);

	pAC->DynIrqModInfo.MaxModIntsPerSecLowerLimit =
		pAC->DynIrqModInfo.MaxModIntsPerSec -
		(pAC->DynIrqModInfo.MaxModIntsPerSec / 2);

	pAC->DynIrqModInfo.PrevTimeVal = jiffies;  /* initial value */


} /* GetConfiguration */


/*****************************************************************************
 *
 * 	ProductStr - return a adapter identification string from vpd
 *
 * Description:
 *	This function reads the product name string from the vpd area
 *	and puts it the field pAC->DeviceString.
 *
 * Returns: N/A
 */
static inline int ProductStr(
	SK_AC	*pAC,		/* pointer to adapter context */
	char    *DeviceStr,	/* result string */
	int      StrLen		/* length of the string */
)
{
char	Keyword[] = VPD_NAME;	/* vpd productname identifier */
int	ReturnCode;		/* return code from vpd_read */
unsigned long Flags;

	spin_lock_irqsave(&pAC->SlowPathLock, Flags);
	ReturnCode = VpdRead(pAC, pAC->IoBase, Keyword, DeviceStr, &StrLen);
	spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);

	return ReturnCode;
} /* ProductStr */

/*****************************************************************************
 *
 *      StartDrvCleanupTimer - Start timer to check for descriptors which
 *                             might be placed in descriptor ring, but
 *                             havent been handled up to now
 *
 * Description:
 *      This function requests a HW-timer fo the Yukon card. The actions to
 *      perform when this timer expires, are located in the SkDrvEvent().
 *
 * Returns: N/A
 */
static void
StartDrvCleanupTimer(SK_AC *pAC) {
    SK_EVPARA    EventParam;   /* Event struct for timer event */

    SK_MEMSET((char *) &EventParam, 0, sizeof(EventParam));
    EventParam.Para32[0] = SK_DRV_RX_CLEANUP_TIMER;
    SkTimerStart(pAC, pAC->IoBase, &pAC->DrvCleanupTimer,
                 SK_DRV_RX_CLEANUP_TIMER_LENGTH,
                 SKGE_DRV, SK_DRV_TIMER, EventParam);
}

/*****************************************************************************
 *
 *      StopDrvCleanupTimer - Stop timer to check for descriptors
 *
 * Description:
 *      This function requests a HW-timer fo the Yukon card. The actions to
 *      perform when this timer expires, are located in the SkDrvEvent().
 *
 * Returns: N/A
 */
static void
StopDrvCleanupTimer(SK_AC *pAC) {
    SkTimerStop(pAC, pAC->IoBase, &pAC->DrvCleanupTimer);
    SK_MEMSET((char *) &pAC->DrvCleanupTimer, 0, sizeof(SK_TIMER));
}

/****************************************************************************/
/* functions for common modules *********************************************/
/****************************************************************************/


/*****************************************************************************
 *
 *	SkDrvAllocRlmtMbuf - allocate an RLMT mbuf
 *
 * Description:
 *	This routine returns an RLMT mbuf or NULL. The RLMT Mbuf structure
 *	is embedded into a socket buff data area.
 *
 * Context:
 *	runtime
 *
 * Returns:
 *	NULL or pointer to Mbuf.
 */
SK_MBUF *SkDrvAllocRlmtMbuf(
SK_AC		*pAC,		/* pointer to adapter context */
SK_IOC		IoC,		/* the IO-context */
unsigned	BufferSize)	/* size of the requested buffer */
{
SK_MBUF		*pRlmtMbuf;	/* pointer to a new rlmt-mbuf structure */
struct sk_buff	*pMsgBlock;	/* pointer to a new message block */

	pMsgBlock = alloc_skb(BufferSize + sizeof(SK_MBUF), GFP_ATOMIC);
	if (pMsgBlock == NULL) {
		return (NULL);
	}
	pRlmtMbuf = (SK_MBUF*) pMsgBlock->data;
	skb_reserve(pMsgBlock, sizeof(SK_MBUF));
	pRlmtMbuf->pNext = NULL;
	pRlmtMbuf->pOs = pMsgBlock;
	pRlmtMbuf->pData = pMsgBlock->data;	/* Data buffer. */
	pRlmtMbuf->Size = BufferSize;		/* Data buffer size. */
	pRlmtMbuf->Length = 0;		/* Length of packet (<= Size). */
	return (pRlmtMbuf);

} /* SkDrvAllocRlmtMbuf */


/*****************************************************************************
 *
 *	SkDrvFreeRlmtMbuf - free an RLMT mbuf
 *
 * Description:
 *	This routine frees one or more RLMT mbuf(s).
 *
 * Context:
 *	runtime
 *
 * Returns:
 *	Nothing
 */
void  SkDrvFreeRlmtMbuf(
SK_AC		*pAC,		/* pointer to adapter context */
SK_IOC		IoC,		/* the IO-context */
SK_MBUF		*pMbuf)		/* size of the requested buffer */
{
SK_MBUF		*pFreeMbuf;
SK_MBUF		*pNextMbuf;

	pFreeMbuf = pMbuf;
	do {
		pNextMbuf = pFreeMbuf->pNext;
		DEV_KFREE_SKB_ANY(pFreeMbuf->pOs);
		pFreeMbuf = pNextMbuf;
	} while ( pFreeMbuf != NULL );
} /* SkDrvFreeRlmtMbuf */


/*****************************************************************************
 *
 *	SkOsGetTime - provide a time value
 *
 * Description:
 *	This routine provides a time value. The unit is 1/HZ (defined by Linux).
 *	It is not used for absolute time, but only for time differences.
 *
 *
 * Returns:
 *	Time value
 */
SK_U64 SkOsGetTime(SK_AC *pAC)
{
	SK_U64	PrivateJiffies;
	SkOsGetTimeCurrent(pAC, &PrivateJiffies);
	return PrivateJiffies;
} /* SkOsGetTime */


/*****************************************************************************
 *
 *	SkPciReadCfgDWord - read a 32 bit value from pci config space
 *
 * Description:
 *	This routine reads a 32 bit value from the pci configuration
 *	space.
 *
 * Returns:
 *	0 - indicate everything worked ok.
 *	!= 0 - error indication
 */
int SkPciReadCfgDWord(
SK_AC *pAC,		/* Adapter Control structure pointer */
int PciAddr,		/* PCI register address */
SK_U32 *pVal)		/* pointer to store the read value */
{
	pci_read_config_dword(pAC->PciDev, PciAddr, pVal);
	return(0);
} /* SkPciReadCfgDWord */


/*****************************************************************************
 *
 *	SkPciReadCfgWord - read a 16 bit value from pci config space
 *
 * Description:
 *	This routine reads a 16 bit value from the pci configuration
 *	space.
 *
 * Returns:
 *	0 - indicate everything worked ok.
 *	!= 0 - error indication
 */
int SkPciReadCfgWord(
SK_AC *pAC,	/* Adapter Control structure pointer */
int PciAddr,		/* PCI register address */
SK_U16 *pVal)		/* pointer to store the read value */
{
	pci_read_config_word(pAC->PciDev, PciAddr, pVal);
	return(0);
} /* SkPciReadCfgWord */


/*****************************************************************************
 *
 *	SkPciReadCfgByte - read a 8 bit value from pci config space
 *
 * Description:
 *	This routine reads a 8 bit value from the pci configuration
 *	space.
 *
 * Returns:
 *	0 - indicate everything worked ok.
 *	!= 0 - error indication
 */
int SkPciReadCfgByte(
SK_AC *pAC,	/* Adapter Control structure pointer */
int PciAddr,		/* PCI register address */
SK_U8 *pVal)		/* pointer to store the read value */
{
	pci_read_config_byte(pAC->PciDev, PciAddr, pVal);
	return(0);
} /* SkPciReadCfgByte */


/*****************************************************************************
 *
 *	SkPciWriteCfgWord - write a 16 bit value to pci config space
 *
 * Description:
 *	This routine writes a 16 bit value to the pci configuration
 *	space. The flag PciConfigUp indicates whether the config space
 *	is accesible or must be set up first.
 *
 * Returns:
 *	0 - indicate everything worked ok.
 *	!= 0 - error indication
 */
int SkPciWriteCfgWord(
SK_AC *pAC,	/* Adapter Control structure pointer */
int PciAddr,		/* PCI register address */
SK_U16 Val)		/* pointer to store the read value */
{
	pci_write_config_word(pAC->PciDev, PciAddr, Val);
	return(0);
} /* SkPciWriteCfgWord */


/*****************************************************************************
 *
 *	SkPciWriteCfgWord - write a 8 bit value to pci config space
 *
 * Description:
 *	This routine writes a 8 bit value to the pci configuration
 *	space. The flag PciConfigUp indicates whether the config space
 *	is accesible or must be set up first.
 *
 * Returns:
 *	0 - indicate everything worked ok.
 *	!= 0 - error indication
 */
int SkPciWriteCfgByte(
SK_AC *pAC,	/* Adapter Control structure pointer */
int PciAddr,		/* PCI register address */
SK_U8 Val)		/* pointer to store the read value */
{
	pci_write_config_byte(pAC->PciDev, PciAddr, Val);
	return(0);
} /* SkPciWriteCfgByte */


/*****************************************************************************
 *
 *	SkDrvEvent - handle driver events
 *
 * Description:
 *	This function handles events from all modules directed to the driver
 *
 * Context:
 *	Is called under protection of slow path lock.
 *
 * Returns:
 *	0 if everything ok
 *	< 0  on error
 *	
 */
int SkDrvEvent(
SK_AC *pAC,		/* pointer to adapter context */
SK_IOC IoC,		/* io-context */
SK_U32 Event,		/* event-id */
SK_EVPARA Param)	/* event-parameter */
{
SK_MBUF		*pRlmtMbuf;	/* pointer to a rlmt-mbuf structure */
struct sk_buff	*pMsg;		/* pointer to a message block */
int		FromPort;	/* the port from which we switch away */
int		ToPort;		/* the port we switch to */
SK_EVPARA	NewPara;	/* parameter for further events */
int		Stat;
unsigned long	Flags;
SK_BOOL		DualNet;

	switch (Event) {
	case SK_DRV_ADAP_FAIL:
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("ADAPTER FAIL EVENT\n"));
		printk("%s: Adapter failed.\n", pAC->dev[0]->name);
		/* disable interrupts */
		SK_OUT32(pAC->IoBase, B0_IMSK, 0);
		/* cgoos */
		break;
	case SK_DRV_PORT_FAIL:
		FromPort = Param.Para32[0];
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("PORT FAIL EVENT, Port: %d\n", FromPort));
		if (FromPort == 0) {
			printk("%s: Port A failed.\n", pAC->dev[0]->name);
		} else {
			printk("%s: Port B failed.\n", pAC->dev[1]->name);
		}
		/* cgoos */
		break;
	case SK_DRV_PORT_RESET:	 /* SK_U32 PortIdx */
		/* action list 4 */
		FromPort = Param.Para32[0];
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("PORT RESET EVENT, Port: %d ", FromPort));
		NewPara.Para64 = FromPort;
		SkPnmiEvent(pAC, IoC, SK_PNMI_EVT_XMAC_RESET, NewPara);
		spin_lock_irqsave(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
			Flags);

		SkGeStopPort(pAC, IoC, FromPort, SK_STOP_ALL, SK_HARD_RST);
		netif_carrier_off(pAC->dev[Param.Para32[0]]);
		spin_unlock_irqrestore(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
			Flags);
		
		/* clear rx ring from received frames */
		ReceiveIrq(pAC, &pAC->RxPort[FromPort], SK_FALSE);
		
		ClearTxRing(pAC, &pAC->TxPort[FromPort][TX_PRIO_LOW]);
		spin_lock_irqsave(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
			Flags);
		
		/* tschilling: Handling of return value inserted. */
		if (SkGeInitPort(pAC, IoC, FromPort)) {
			if (FromPort == 0) {
				printk("%s: SkGeInitPort A failed.\n", pAC->dev[0]->name);
			} else {
				printk("%s: SkGeInitPort B failed.\n", pAC->dev[1]->name);
			}
		}
		SkAddrMcUpdate(pAC,IoC, FromPort);
		PortReInitBmu(pAC, FromPort);
		SkGePollTxD(pAC, IoC, FromPort, SK_TRUE);
		ClearAndStartRx(pAC, FromPort);
		spin_unlock_irqrestore(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
			Flags);
		break;
	case SK_DRV_NET_UP:	 /* SK_U32 PortIdx */
	{	struct net_device *dev = pAC->dev[Param.Para32[0]];
		/* action list 5 */
		FromPort = Param.Para32[0];
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("NET UP EVENT, Port: %d ", Param.Para32[0]));
		/* Mac update */
		SkAddrMcUpdate(pAC,IoC, FromPort);

		if (DoPrintInterfaceChange) {
		printk("%s: network connection up using"
			" port %c\n", pAC->dev[Param.Para32[0]]->name, 'A'+Param.Para32[0]);

		/* tschilling: Values changed according to LinkSpeedUsed. */
		Stat = pAC->GIni.GP[FromPort].PLinkSpeedUsed;
		if (Stat == SK_LSPEED_STAT_10MBPS) {
			printk("    speed:           10\n");
		} else if (Stat == SK_LSPEED_STAT_100MBPS) {
			printk("    speed:           100\n");
		} else if (Stat == SK_LSPEED_STAT_1000MBPS) {
			printk("    speed:           1000\n");
		} else {
			printk("    speed:           unknown\n");
		}


		Stat = pAC->GIni.GP[FromPort].PLinkModeStatus;
		if (Stat == SK_LMODE_STAT_AUTOHALF ||
			Stat == SK_LMODE_STAT_AUTOFULL) {
			printk("    autonegotiation: yes\n");
		}
		else {
			printk("    autonegotiation: no\n");
		}
		if (Stat == SK_LMODE_STAT_AUTOHALF ||
			Stat == SK_LMODE_STAT_HALF) {
			printk("    duplex mode:     half\n");
		}
		else {
			printk("    duplex mode:     full\n");
		}
		Stat = pAC->GIni.GP[FromPort].PFlowCtrlStatus;
		if (Stat == SK_FLOW_STAT_REM_SEND ) {
			printk("    flowctrl:        remote send\n");
		}
		else if (Stat == SK_FLOW_STAT_LOC_SEND ){
			printk("    flowctrl:        local send\n");
		}
		else if (Stat == SK_FLOW_STAT_SYMMETRIC ){
			printk("    flowctrl:        symmetric\n");
		}
		else {
			printk("    flowctrl:        none\n");
		}
		
		/* tschilling: Check against CopperType now. */
		if ((pAC->GIni.GICopperType == SK_TRUE) &&
			(pAC->GIni.GP[FromPort].PLinkSpeedUsed ==
			SK_LSPEED_STAT_1000MBPS)) {
			Stat = pAC->GIni.GP[FromPort].PMSStatus;
			if (Stat == SK_MS_STAT_MASTER ) {
				printk("    role:            master\n");
			}
			else if (Stat == SK_MS_STAT_SLAVE ) {
				printk("    role:            slave\n");
			}
			else {
				printk("    role:            ???\n");
			}
		}

		/* 
		   Display dim (dynamic interrupt moderation) 
		   informations
		 */
		if (pAC->DynIrqModInfo.IntModTypeSelect == C_INT_MOD_STATIC)
			printk("    irq moderation:  static (%d ints/sec)\n",
					pAC->DynIrqModInfo.MaxModIntsPerSec);
		else if (pAC->DynIrqModInfo.IntModTypeSelect == C_INT_MOD_DYNAMIC)
			printk("    irq moderation:  dynamic (%d ints/sec)\n",
					pAC->DynIrqModInfo.MaxModIntsPerSec);
		else
			printk("    irq moderation:  disabled\n");


		printk("    scatter-gather:  %s\n",
		       (dev->features & NETIF_F_SG) ? "enabled" : "disabled");
		printk("    tx-checksum:     %s\n",
		       (dev->features & NETIF_F_IP_CSUM) ? "enabled" : "disabled");
		printk("    rx-checksum:     %s\n",
		       pAC->RxPort[Param.Para32[0]].RxCsum ? "enabled" : "disabled");

		} else {
                        DoPrintInterfaceChange = SK_TRUE;
                }
	
		if ((Param.Para32[0] != pAC->ActivePort) &&
			(pAC->RlmtNets == 1)) {
			NewPara.Para32[0] = pAC->ActivePort;
			NewPara.Para32[1] = Param.Para32[0];
			SkEventQueue(pAC, SKGE_DRV, SK_DRV_SWITCH_INTERN,
				NewPara);
		}

		/* Inform the world that link protocol is up. */
		netif_carrier_on(dev);
		break;
	}
	case SK_DRV_NET_DOWN:	 /* SK_U32 Reason */
		/* action list 7 */
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("NET DOWN EVENT "));
		if (DoPrintInterfaceChange) {
			printk("%s: network connection down\n", 
				pAC->dev[Param.Para32[1]]->name);
		} else {
			DoPrintInterfaceChange = SK_TRUE;
		}
		netif_carrier_off(pAC->dev[Param.Para32[1]]);
		break;
	case SK_DRV_SWITCH_HARD: /* SK_U32 FromPortIdx SK_U32 ToPortIdx */
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("PORT SWITCH HARD "));
	case SK_DRV_SWITCH_SOFT: /* SK_U32 FromPortIdx SK_U32 ToPortIdx */
	/* action list 6 */
		printk("%s: switching to port %c\n", pAC->dev[0]->name,
			'A'+Param.Para32[1]);
	case SK_DRV_SWITCH_INTERN: /* SK_U32 FromPortIdx SK_U32 ToPortIdx */
		FromPort = Param.Para32[0];
		ToPort = Param.Para32[1];
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("PORT SWITCH EVENT, From: %d  To: %d (Pref %d) ",
			FromPort, ToPort, pAC->Rlmt.Net[0].PrefPort));
		NewPara.Para64 = FromPort;
		SkPnmiEvent(pAC, IoC, SK_PNMI_EVT_XMAC_RESET, NewPara);
		NewPara.Para64 = ToPort;
		SkPnmiEvent(pAC, IoC, SK_PNMI_EVT_XMAC_RESET, NewPara);
		spin_lock_irqsave(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
			Flags);
		spin_lock(&pAC->TxPort[ToPort][TX_PRIO_LOW].TxDesRingLock);
		SkGeStopPort(pAC, IoC, FromPort, SK_STOP_ALL, SK_SOFT_RST);
		SkGeStopPort(pAC, IoC, ToPort, SK_STOP_ALL, SK_SOFT_RST);
		spin_unlock(&pAC->TxPort[ToPort][TX_PRIO_LOW].TxDesRingLock);
		spin_unlock_irqrestore(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
			Flags);

		ReceiveIrq(pAC, &pAC->RxPort[FromPort], SK_FALSE); /* clears rx ring */
		ReceiveIrq(pAC, &pAC->RxPort[ToPort], SK_FALSE); /* clears rx ring */
		
		ClearTxRing(pAC, &pAC->TxPort[FromPort][TX_PRIO_LOW]);
		ClearTxRing(pAC, &pAC->TxPort[ToPort][TX_PRIO_LOW]);
		spin_lock_irqsave(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
			Flags);
		spin_lock(&pAC->TxPort[ToPort][TX_PRIO_LOW].TxDesRingLock);
		pAC->ActivePort = ToPort;
#if 0
		SetQueueSizes(pAC);
#else
		/* tschilling: New common function with minimum size check. */
		DualNet = SK_FALSE;
		if (pAC->RlmtNets == 2) {
			DualNet = SK_TRUE;
		}
		
		if (SkGeInitAssignRamToQueues(
			pAC,
			pAC->ActivePort,
			DualNet)) {
			spin_unlock(&pAC->TxPort[ToPort][TX_PRIO_LOW].TxDesRingLock);
			spin_unlock_irqrestore(
				&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
				Flags);
			printk("SkGeInitAssignRamToQueues failed.\n");
			break;
		}
#endif
		/* tschilling: Handling of return values inserted. */
		if (SkGeInitPort(pAC, IoC, FromPort) ||
			SkGeInitPort(pAC, IoC, ToPort)) {
			printk("%s: SkGeInitPort failed.\n", pAC->dev[0]->name);
		}
		if (Event == SK_DRV_SWITCH_SOFT) {
			SkMacRxTxEnable(pAC, IoC, FromPort);
		}
		SkMacRxTxEnable(pAC, IoC, ToPort);
		SkAddrSwap(pAC, IoC, FromPort, ToPort);
		SkAddrMcUpdate(pAC, IoC, FromPort);
		SkAddrMcUpdate(pAC, IoC, ToPort);
		PortReInitBmu(pAC, FromPort);
		PortReInitBmu(pAC, ToPort);
		SkGePollTxD(pAC, IoC, FromPort, SK_TRUE);
		SkGePollTxD(pAC, IoC, ToPort, SK_TRUE);
		ClearAndStartRx(pAC, FromPort);
		ClearAndStartRx(pAC, ToPort);
		spin_unlock(&pAC->TxPort[ToPort][TX_PRIO_LOW].TxDesRingLock);
		spin_unlock_irqrestore(
			&pAC->TxPort[FromPort][TX_PRIO_LOW].TxDesRingLock,
			Flags);
		break;
	case SK_DRV_RLMT_SEND:	 /* SK_MBUF *pMb */
		SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
			("RLS "));
		pRlmtMbuf = (SK_MBUF*) Param.pParaPtr;
		pMsg = (struct sk_buff*) pRlmtMbuf->pOs;
		skb_put(pMsg, pRlmtMbuf->Length);
		if (XmitFrame(pAC, &pAC->TxPort[pRlmtMbuf->PortIdx][TX_PRIO_LOW],
			pMsg) < 0)

			DEV_KFREE_SKB_ANY(pMsg);
		break;
	case SK_DRV_TIMER:
		if (Param.Para32[0] == SK_DRV_MODERATION_TIMER) {
			/*
			** expiration of the moderation timer implies that
			** dynamic moderation is to be applied
			*/
			SkDimStartModerationTimer(pAC);
			SkDimModerate(pAC);
                        if (pAC->DynIrqModInfo.DisplayStats) {
			    SkDimDisplayModerationSettings(pAC);
                        }
                } else if (Param.Para32[0] == SK_DRV_RX_CLEANUP_TIMER) {
			/*
			** check if we need to check for descriptors which
			** haven't been handled the last millisecs
			*/
			StartDrvCleanupTimer(pAC);
			if (pAC->GIni.GIMacsFound == 2) {
				ReceiveIrq(pAC, &pAC->RxPort[1], SK_FALSE);
			}
			ReceiveIrq(pAC, &pAC->RxPort[0], SK_FALSE);
		} else {
			printk("Expiration of unknown timer\n");
		}
		break;
	default:
		break;
	}
	SK_DBG_MSG(NULL, SK_DBGMOD_DRV, SK_DBGCAT_DRV_EVENT,
		("END EVENT "));
	
	return (0);
} /* SkDrvEvent */


/*****************************************************************************
 *
 *	SkErrorLog - log errors
 *
 * Description:
 *	This function logs errors to the system buffer and to the console
 *
 * Returns:
 *	0 if everything ok
 *	< 0  on error
 *	
 */
void SkErrorLog(
SK_AC	*pAC,
int	ErrClass,
int	ErrNum,
char	*pErrorMsg)
{
char	ClassStr[80];

	switch (ErrClass) {
	case SK_ERRCL_OTHER:
		strcpy(ClassStr, "Other error");
		break;
	case SK_ERRCL_CONFIG:
		strcpy(ClassStr, "Configuration error");
		break;
	case SK_ERRCL_INIT:
		strcpy(ClassStr, "Initialization error");
		break;
	case SK_ERRCL_NORES:
		strcpy(ClassStr, "Out of resources error");
		break;
	case SK_ERRCL_SW:
		strcpy(ClassStr, "internal Software error");
		break;
	case SK_ERRCL_HW:
		strcpy(ClassStr, "Hardware failure");
		break;
	case SK_ERRCL_COMM:
		strcpy(ClassStr, "Communication error");
		break;
	}
	printk(KERN_INFO "%s: -- ERROR --\n        Class:  %s\n"
		"        Nr:  0x%x\n        Msg:  %s\n", pAC->dev[0]->name,
		ClassStr, ErrNum, pErrorMsg);

} /* SkErrorLog */

#ifdef SK_DIAG_SUPPORT

/*****************************************************************************
 *
 *	SkDrvEnterDiagMode - handles DIAG attach request
 *
 * Description:
 *	Notify the kernel to NOT access the card any longer due to DIAG
 *	Deinitialize the Card
 *
 * Returns:
 *	int
 */
int SkDrvEnterDiagMode(
SK_AC   *pAc)   /* pointer to adapter context */
{
	DEV_NET *pNet = netdev_priv(pAc->dev[0]);
	SK_AC   *pAC  = pNet->pAC;

	SK_MEMCPY(&(pAc->PnmiBackup), &(pAc->PnmiStruct), 
			sizeof(SK_PNMI_STRUCT_DATA));

	pAC->DiagModeActive = DIAG_ACTIVE;
	if (pAC->BoardLevel > SK_INIT_DATA) {
		if (netif_running(pAC->dev[0])) {
			pAC->WasIfUp[0] = SK_TRUE;
			pAC->DiagFlowCtrl = SK_TRUE; /* for SkGeClose      */
			DoPrintInterfaceChange = SK_FALSE;
			SkDrvDeInitAdapter(pAC, 0);  /* performs SkGeClose */
		} else {
			pAC->WasIfUp[0] = SK_FALSE;
		}
		if (pNet != netdev_priv(pAC->dev[1])) {
			pNet = netdev_priv(pAC->dev[1]);
			if (netif_running(pAC->dev[1])) {
				pAC->WasIfUp[1] = SK_TRUE;
				pAC->DiagFlowCtrl = SK_TRUE; /* for SkGeClose */
				DoPrintInterfaceChange = SK_FALSE;
				SkDrvDeInitAdapter(pAC, 1);  /* do SkGeClose  */
			} else {
				pAC->WasIfUp[1] = SK_FALSE;
			}
		}
		pAC->BoardLevel = SK_INIT_DATA;
	}
	return(0);
}

/*****************************************************************************
 *
 *	SkDrvLeaveDiagMode - handles DIAG detach request
 *
 * Description:
 *	Notify the kernel to may access the card again after use by DIAG
 *	Initialize the Card
 *
 * Returns:
 * 	int
 */
int SkDrvLeaveDiagMode(
SK_AC   *pAc)   /* pointer to adapter control context */
{ 
	SK_MEMCPY(&(pAc->PnmiStruct), &(pAc->PnmiBackup), 
			sizeof(SK_PNMI_STRUCT_DATA));
	pAc->DiagModeActive    = DIAG_NOTACTIVE;
	pAc->Pnmi.DiagAttached = SK_DIAG_IDLE;
        if (pAc->WasIfUp[0] == SK_TRUE) {
                pAc->DiagFlowCtrl = SK_TRUE; /* for SkGeClose */
		DoPrintInterfaceChange = SK_FALSE;
                SkDrvInitAdapter(pAc, 0);    /* first device  */
        }
        if (pAc->WasIfUp[1] == SK_TRUE) {
                pAc->DiagFlowCtrl = SK_TRUE; /* for SkGeClose */
		DoPrintInterfaceChange = SK_FALSE;
                SkDrvInitAdapter(pAc, 1);    /* second device */
        }
	return(0);
}

/*****************************************************************************
 *
 *	ParseDeviceNbrFromSlotName - Evaluate PCI device number
 *
 * Description:
 * 	This function parses the PCI slot name information string and will
 *	retrieve the devcie number out of it. The slot_name maintianed by
 *	linux is in the form of '02:0a.0', whereas the first two characters 
 *	represent the bus number in hex (in the sample above this is 
 *	pci bus 0x02) and the next two characters the device number (0x0a).
 *
 * Returns:
 *	SK_U32: The device number from the PCI slot name
 */ 

static SK_U32 ParseDeviceNbrFromSlotName(
const char *SlotName)   /* pointer to pci slot name eg. '02:0a.0' */
{
	char	*CurrCharPos	= (char *) SlotName;
	int	FirstNibble	= -1;
	int	SecondNibble	= -1;
	SK_U32	Result		=  0;

	while (*CurrCharPos != '\0') {
		if (*CurrCharPos == ':') { 
			while (*CurrCharPos != '.') {
				CurrCharPos++;  
				if (	(*CurrCharPos >= '0') && 
					(*CurrCharPos <= '9')) {
					if (FirstNibble == -1) {
						/* dec. value for '0' */
						FirstNibble = *CurrCharPos - 48;
					} else {
						SecondNibble = *CurrCharPos - 48;
					}  
				} else if (	(*CurrCharPos >= 'a') && 
						(*CurrCharPos <= 'f')  ) {
					if (FirstNibble == -1) {
						FirstNibble = *CurrCharPos - 87; 
					} else {
						SecondNibble = *CurrCharPos - 87; 
					}
				} else {
					Result = 0;
				}
			}

			Result = FirstNibble;
			Result = Result << 4; /* first nibble is higher one */
			Result = Result | SecondNibble;
		}
		CurrCharPos++;   /* next character */
	}
	return (Result);
}

/****************************************************************************
 *
 *	SkDrvDeInitAdapter - deinitialize adapter (this function is only 
 *				called if Diag attaches to that card)
 *
 * Description:
 *	Close initialized adapter.
 *
 * Returns:
 *	0 - on success
 *	error code - on error
 */
static int SkDrvDeInitAdapter(
SK_AC   *pAC,		/* pointer to adapter context   */
int      devNbr)	/* what device is to be handled */
{
	struct SK_NET_DEVICE *dev;

	dev = pAC->dev[devNbr];

	/* On Linux 2.6 the network driver does NOT mess with reference
	** counts.  The driver MUST be able to be unloaded at any time
	** due to the possibility of hotplug.
	*/
	if (SkGeClose(dev) != 0) {
		return (-1);
	}
	return (0);

} /* SkDrvDeInitAdapter() */

/****************************************************************************
 *
 *	SkDrvInitAdapter - Initialize adapter (this function is only 
 *				called if Diag deattaches from that card)
 *
 * Description:
 *	Close initialized adapter.
 *
 * Returns:
 *	0 - on success
 *	error code - on error
 */
static int SkDrvInitAdapter(
SK_AC   *pAC,		/* pointer to adapter context   */
int      devNbr)	/* what device is to be handled */
{
	struct SK_NET_DEVICE *dev;

	dev = pAC->dev[devNbr];

	if (SkGeOpen(dev) != 0) {
		return (-1);
	}

	/*
	** Use correct MTU size and indicate to kernel TX queue can be started
	*/ 
	if (SkGeChangeMtu(dev, dev->mtu) != 0) {
		return (-1);
	} 
	return (0);

} /* SkDrvInitAdapter */

#endif

#ifdef DEBUG
/****************************************************************************/
/* "debug only" section *****************************************************/
/****************************************************************************/


/*****************************************************************************
 *
 *	DumpMsg - print a frame
 *
 * Description:
 *	This function prints frames to the system logfile/to the console.
 *
 * Returns: N/A
 *	
 */
static void DumpMsg(struct sk_buff *skb, char *str)
{
	int	msglen;

	if (skb == NULL) {
		printk("DumpMsg(): NULL-Message\n");
		return;
	}

	if (skb->data == NULL) {
		printk("DumpMsg(): Message empty\n");
		return;
	}

	msglen = skb->len;
	if (msglen > 64)
		msglen = 64;

	printk("--- Begin of message from %s , len %d (from %d) ----\n", str, msglen, skb->len);

	DumpData((char *)skb->data, msglen);

	printk("------- End of message ---------\n");
} /* DumpMsg */



/*****************************************************************************
 *
 *	DumpData - print a data area
 *
 * Description:
 *	This function prints a area of data to the system logfile/to the
 *	console.
 *
 * Returns: N/A
 *	
 */
static void DumpData(char *p, int size)
{
register int    i;
int	haddr, addr;
char	hex_buffer[180];
char	asc_buffer[180];
char	HEXCHAR[] = "0123456789ABCDEF";

	addr = 0;
	haddr = 0;
	hex_buffer[0] = 0;
	asc_buffer[0] = 0;
	for (i=0; i < size; ) {
		if (*p >= '0' && *p <='z')
			asc_buffer[addr] = *p;
		else
			asc_buffer[addr] = '.';
		addr++;
		asc_buffer[addr] = 0;
		hex_buffer[haddr] = HEXCHAR[(*p & 0xf0) >> 4];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[*p & 0x0f];
		haddr++;
		hex_buffer[haddr] = ' ';
		haddr++;
		hex_buffer[haddr] = 0;
		p++;
		i++;
		if (i%16 == 0) {
			printk("%s  %s\n", hex_buffer, asc_buffer);
			addr = 0;
			haddr = 0;
		}
	}
} /* DumpData */


/*****************************************************************************
 *
 *	DumpLong - print a data area as long values
 *
 * Description:
 *	This function prints a area of data to the system logfile/to the
 *	console.
 *
 * Returns: N/A
 *	
 */
static void DumpLong(char *pc, int size)
{
register int    i;
int	haddr, addr;
char	hex_buffer[180];
char	asc_buffer[180];
char	HEXCHAR[] = "0123456789ABCDEF";
long	*p;
int	l;

	addr = 0;
	haddr = 0;
	hex_buffer[0] = 0;
	asc_buffer[0] = 0;
	p = (long*) pc;
	for (i=0; i < size; ) {
		l = (long) *p;
		hex_buffer[haddr] = HEXCHAR[(l >> 28) & 0xf];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[(l >> 24) & 0xf];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[(l >> 20) & 0xf];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[(l >> 16) & 0xf];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[(l >> 12) & 0xf];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[(l >> 8) & 0xf];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[(l >> 4) & 0xf];
		haddr++;
		hex_buffer[haddr] = HEXCHAR[l & 0x0f];
		haddr++;
		hex_buffer[haddr] = ' ';
		haddr++;
		hex_buffer[haddr] = 0;
		p++;
		i++;
		if (i%8 == 0) {
			printk("%4x %s\n", (i-8)*4, hex_buffer);
			haddr = 0;
		}
	}
	printk("------------------------\n");
} /* DumpLong */

#endif

static int __devinit skge_probe_one(struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	SK_AC			*pAC;
	DEV_NET			*pNet = NULL;
	struct net_device	*dev = NULL;
	static int boards_found = 0;
	int error = -ENODEV;
	int using_dac = 0;
	char DeviceStr[80];

	if (pci_enable_device(pdev))
		goto out;
 
	/* Configure DMA attributes. */
	if (sizeof(dma_addr_t) > sizeof(u32) &&
	    !(error = pci_set_dma_mask(pdev, DMA_64BIT_MASK))) {
		using_dac = 1;
		error = pci_set_consistent_dma_mask(pdev, DMA_64BIT_MASK);
		if (error < 0) {
			printk(KERN_ERR "sk98lin %s unable to obtain 64 bit DMA "
			       "for consistent allocations\n", pci_name(pdev));
			goto out_disable_device;
		}
	} else {
		error = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (error) {
			printk(KERN_ERR "sk98lin %s no usable DMA configuration\n",
			       pci_name(pdev));
			goto out_disable_device;
		}
	}

 	error = -ENOMEM;
 	dev = alloc_etherdev(sizeof(DEV_NET));
 	if (!dev) {
		printk(KERN_ERR "sk98lin: unable to allocate etherdev "
		       "structure!\n");
		goto out_disable_device;
	}

	pNet = netdev_priv(dev);
	pNet->pAC = kzalloc(sizeof(SK_AC), GFP_KERNEL);
	if (!pNet->pAC) {
		printk(KERN_ERR "sk98lin: unable to allocate adapter "
		       "structure!\n");
		goto out_free_netdev;
	}

	pAC = pNet->pAC;
	pAC->PciDev = pdev;

	pAC->dev[0] = dev;
	pAC->dev[1] = dev;
	pAC->CheckQueue = SK_FALSE;

	dev->irq = pdev->irq;

	error = SkGeInitPCI(pAC);
	if (error) {
		printk(KERN_ERR "sk98lin: PCI setup failed: %i\n", error);
		goto out_free_netdev;
	}

	SET_MODULE_OWNER(dev);
	dev->open =		&SkGeOpen;
	dev->stop =		&SkGeClose;
	dev->hard_start_xmit =	&SkGeXmit;
	dev->get_stats =	&SkGeStats;
	dev->set_multicast_list = &SkGeSetRxMode;
	dev->set_mac_address =	&SkGeSetMacAddr;
	dev->do_ioctl =		&SkGeIoctl;
	dev->change_mtu =	&SkGeChangeMtu;
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller =	&SkGePollController;
#endif
	SET_NETDEV_DEV(dev, &pdev->dev);
	SET_ETHTOOL_OPS(dev, &SkGeEthtoolOps);

	/* Use only if yukon hardware */
	if (pAC->ChipsetType) {
#ifdef USE_SK_TX_CHECKSUM
		dev->features |= NETIF_F_IP_CSUM;
#endif
#ifdef SK_ZEROCOPY
		dev->features |= NETIF_F_SG;
#endif
#ifdef USE_SK_RX_CHECKSUM
		pAC->RxPort[0].RxCsum = 1;
#endif
	}

	if (using_dac)
		dev->features |= NETIF_F_HIGHDMA;

	pAC->Index = boards_found++;

	error = SkGeBoardInit(dev, pAC);
	if (error)
		goto out_free_netdev;

	/* Read Adapter name from VPD */
	if (ProductStr(pAC, DeviceStr, sizeof(DeviceStr)) != 0) {
		error = -EIO;
		printk(KERN_ERR "sk98lin: Could not read VPD data.\n");
		goto out_free_resources;
	}

	/* Register net device */
	error = register_netdev(dev);
	if (error) {
		printk(KERN_ERR "sk98lin: Could not register device.\n");
		goto out_free_resources;
	}

	/* Print adapter specific string from vpd */
	printk("%s: %s\n", dev->name, DeviceStr);

	/* Print configuration settings */
	printk("      PrefPort:%c  RlmtMode:%s\n",
		'A' + pAC->Rlmt.Net[0].Port[pAC->Rlmt.Net[0].PrefPort]->PortNumber,
		(pAC->RlmtMode==0)  ? "Check Link State" :
		((pAC->RlmtMode==1) ? "Check Link State" :
		((pAC->RlmtMode==3) ? "Check Local Port" :
		((pAC->RlmtMode==7) ? "Check Segmentation" :
		((pAC->RlmtMode==17) ? "Dual Check Link State" :"Error")))));

	SkGeYellowLED(pAC, pAC->IoBase, 1);

	memcpy(&dev->dev_addr, &pAC->Addr.Net[0].CurrentMacAddress, 6);
	memcpy(dev->perm_addr, dev->dev_addr, dev->addr_len);

	pNet->PortNr = 0;
	pNet->NetNr  = 0;

	boards_found++;

	pci_set_drvdata(pdev, dev);

	/* More then one port found */
	if ((pAC->GIni.GIMacsFound == 2 ) && (pAC->RlmtNets == 2)) {
		dev = alloc_etherdev(sizeof(DEV_NET));
		if (!dev) {
			printk(KERN_ERR "sk98lin: unable to allocate etherdev "
				"structure!\n");
			goto single_port;
		}

		pNet          = netdev_priv(dev);
		pNet->PortNr  = 1;
		pNet->NetNr   = 1;
		pNet->pAC     = pAC;

		dev->open               = &SkGeOpen;
		dev->stop               = &SkGeClose;
		dev->hard_start_xmit    = &SkGeXmit;
		dev->get_stats          = &SkGeStats;
		dev->set_multicast_list = &SkGeSetRxMode;
		dev->set_mac_address    = &SkGeSetMacAddr;
		dev->do_ioctl           = &SkGeIoctl;
		dev->change_mtu         = &SkGeChangeMtu;
		SET_NETDEV_DEV(dev, &pdev->dev);
		SET_ETHTOOL_OPS(dev, &SkGeEthtoolOps);

		if (pAC->ChipsetType) {
#ifdef USE_SK_TX_CHECKSUM
			dev->features |= NETIF_F_IP_CSUM;
#endif
#ifdef SK_ZEROCOPY
			dev->features |= NETIF_F_SG;
#endif
#ifdef USE_SK_RX_CHECKSUM
			pAC->RxPort[1].RxCsum = 1;
#endif
		}

		if (using_dac)
			dev->features |= NETIF_F_HIGHDMA;

		error = register_netdev(dev);
		if (error) {
			printk(KERN_ERR "sk98lin: Could not register device"
			       " for second port. (%d)\n", error);
			free_netdev(dev);
			goto single_port;
		}

		pAC->dev[1]   = dev;
		memcpy(&dev->dev_addr,
		       &pAC->Addr.Net[1].CurrentMacAddress, 6);
		memcpy(dev->perm_addr, dev->dev_addr, dev->addr_len);

		printk("%s: %s\n", dev->name, DeviceStr);
		printk("      PrefPort:B  RlmtMode:Dual Check Link State\n");
	}

single_port:

	/* Save the hardware revision */
	pAC->HWRevision = (((pAC->GIni.GIPciHwRev >> 4) & 0x0F)*10) +
		(pAC->GIni.GIPciHwRev & 0x0F);

	/* Set driver globals */
	pAC->Pnmi.pDriverFileName    = DRIVER_FILE_NAME;
	pAC->Pnmi.pDriverReleaseDate = DRIVER_REL_DATE;

	memset(&pAC->PnmiBackup, 0, sizeof(SK_PNMI_STRUCT_DATA));
	memcpy(&pAC->PnmiBackup, &pAC->PnmiStruct, sizeof(SK_PNMI_STRUCT_DATA));

	return 0;

 out_free_resources:
	FreeResources(dev);
 out_free_netdev:
	free_netdev(dev);
 out_disable_device:
	pci_disable_device(pdev);
 out:
	return error;
}

static void __devexit skge_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	DEV_NET *pNet = netdev_priv(dev);
	SK_AC *pAC = pNet->pAC;
	struct net_device *otherdev = pAC->dev[1];

	unregister_netdev(dev);

	SkGeYellowLED(pAC, pAC->IoBase, 0);

	if (pAC->BoardLevel == SK_INIT_RUN) {
		SK_EVPARA EvPara;
		unsigned long Flags;

		/* board is still alive */
		spin_lock_irqsave(&pAC->SlowPathLock, Flags);
		EvPara.Para32[0] = 0;
		EvPara.Para32[1] = -1;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_STOP, EvPara);
		EvPara.Para32[0] = 1;
		EvPara.Para32[1] = -1;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_STOP, EvPara);
		SkEventDispatcher(pAC, pAC->IoBase);
		/* disable interrupts */
		SK_OUT32(pAC->IoBase, B0_IMSK, 0);
		SkGeDeInit(pAC, pAC->IoBase);
		spin_unlock_irqrestore(&pAC->SlowPathLock, Flags);
		pAC->BoardLevel = SK_INIT_DATA;
		/* We do NOT check here, if IRQ was pending, of course*/
	}

	if (pAC->BoardLevel == SK_INIT_IO) {
		/* board is still alive */
		SkGeDeInit(pAC, pAC->IoBase);
		pAC->BoardLevel = SK_INIT_DATA;
	}

	FreeResources(dev);
	free_netdev(dev);
	if (otherdev != dev)
		free_netdev(otherdev);
	kfree(pAC);
}

#ifdef CONFIG_PM
static int skge_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	DEV_NET *pNet = netdev_priv(dev);
	SK_AC *pAC = pNet->pAC;
	struct net_device *otherdev = pAC->dev[1];

	if (netif_running(dev)) {
		netif_carrier_off(dev);
		DoPrintInterfaceChange = SK_FALSE;
		SkDrvDeInitAdapter(pAC, 0);  /* performs SkGeClose */
		netif_device_detach(dev);
	}
	if (otherdev != dev) {
		if (netif_running(otherdev)) {
			netif_carrier_off(otherdev);
			DoPrintInterfaceChange = SK_FALSE;
			SkDrvDeInitAdapter(pAC, 1);  /* performs SkGeClose */
			netif_device_detach(otherdev);
		}
	}

	pci_save_state(pdev);
	pci_enable_wake(pdev, pci_choose_state(pdev, state), 0);
	if (pAC->AllocFlag & SK_ALLOC_IRQ) {
		free_irq(dev->irq, dev);
	}
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int skge_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	DEV_NET *pNet = netdev_priv(dev);
	SK_AC *pAC = pNet->pAC;
	struct net_device *otherdev = pAC->dev[1];
	int ret;

	pci_set_power_state(pdev, PCI_D0);
	pci_restore_state(pdev);
	pci_enable_device(pdev);
	pci_set_master(pdev);
	if (pAC->GIni.GIMacsFound == 2)
		ret = request_irq(dev->irq, SkGeIsr, IRQF_SHARED, "sk98lin", dev);
	else
		ret = request_irq(dev->irq, SkGeIsrOnePort, IRQF_SHARED, "sk98lin", dev);
	if (ret) {
		printk(KERN_WARNING "sk98lin: unable to acquire IRQ %d\n", dev->irq);
		pAC->AllocFlag &= ~SK_ALLOC_IRQ;
		dev->irq = 0;
		pci_disable_device(pdev);
		return -EBUSY;
	}

	netif_device_attach(dev);
	if (netif_running(dev)) {
		DoPrintInterfaceChange = SK_FALSE;
		SkDrvInitAdapter(pAC, 0);    /* first device  */
	}
	if (otherdev != dev) {
		netif_device_attach(otherdev);
		if (netif_running(otherdev)) {
			DoPrintInterfaceChange = SK_FALSE;
			SkDrvInitAdapter(pAC, 1);    /* second device  */
		}
	}

	return 0;
}
#else
#define skge_suspend NULL
#define skge_resume NULL
#endif

static struct pci_device_id skge_pci_tbl[] = {
	{ PCI_VENDOR_ID_3COM, 0x1700, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_3COM, 0x80eb, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_SYSKONNECT, 0x4300, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_SYSKONNECT, 0x4320, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
/* DLink card does not have valid VPD so this driver gags
 *	{ PCI_VENDOR_ID_DLINK, 0x4c00, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
 */
	{ PCI_VENDOR_ID_MARVELL, 0x4320, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_MARVELL, 0x5005, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_CNET, 0x434e, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_LINKSYS, 0x1032, PCI_ANY_ID, 0x0015, },
	{ PCI_VENDOR_ID_LINKSYS, 0x1064, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, skge_pci_tbl);

static struct pci_driver skge_driver = {
	.name		= "sk98lin",
	.id_table	= skge_pci_tbl,
	.probe		= skge_probe_one,
	.remove		= __devexit_p(skge_remove_one),
	.suspend	= skge_suspend,
	.resume		= skge_resume,
};

static int __init skge_init(void)
{
	printk(KERN_NOTICE "sk98lin: driver has been replaced by the skge driver"
	       " and is scheduled for removal\n");

	return pci_register_driver(&skge_driver);
}

static void __exit skge_exit(void)
{
	pci_unregister_driver(&skge_driver);
}

module_init(skge_init);
module_exit(skge_exit);
