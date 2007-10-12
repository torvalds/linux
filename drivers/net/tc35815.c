/*
 * tc35815.c: A TOSHIBA TC35815CF PCI 10/100Mbps ethernet driver for linux.
 *
 * Based on skelton.c by Donald Becker.
 *
 * This driver is a replacement of older and less maintained version.
 * This is a header of the older version:
 *	-----<snip>-----
 *	Copyright 2001 MontaVista Software Inc.
 *	Author: MontaVista Software, Inc.
 *		ahennessy@mvista.com
 *	Copyright (C) 2000-2001 Toshiba Corporation
 *	static const char *version =
 *		"tc35815.c:v0.00 26/07/2000 by Toshiba Corporation\n";
 *	-----<snip>-----
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * (C) Copyright TOSHIBA CORPORATION 2004-2005
 * All Rights Reserved.
 */

#ifdef TC35815_NAPI
#define DRV_VERSION	"1.36-NAPI"
#else
#define DRV_VERSION	"1.36"
#endif
static const char *version = "tc35815.c:v" DRV_VERSION "\n";
#define MODNAME			"tc35815"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/byteorder.h>

/* First, a few definitions that the brave might change. */

#define GATHER_TXINT	/* On-Demand Tx Interrupt */
#define WORKAROUND_LOSTCAR
#define WORKAROUND_100HALF_PROMISC
/* #define TC35815_USE_PACKEDBUFFER */

typedef enum {
	TC35815CF = 0,
	TC35815_NWU,
	TC35815_TX4939,
} board_t;

/* indexed by board_t, above */
static const struct {
	const char *name;
} board_info[] __devinitdata = {
	{ "TOSHIBA TC35815CF 10/100BaseTX" },
	{ "TOSHIBA TC35815 with Wake on LAN" },
	{ "TOSHIBA TC35815/TX4939" },
};

static const struct pci_device_id tc35815_pci_tbl[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_TOSHIBA_2, PCI_DEVICE_ID_TOSHIBA_TC35815CF), .driver_data = TC35815CF },
	{PCI_DEVICE(PCI_VENDOR_ID_TOSHIBA_2, PCI_DEVICE_ID_TOSHIBA_TC35815_NWU), .driver_data = TC35815_NWU },
	{PCI_DEVICE(PCI_VENDOR_ID_TOSHIBA_2, PCI_DEVICE_ID_TOSHIBA_TC35815_TX4939), .driver_data = TC35815_TX4939 },
	{0,}
};
MODULE_DEVICE_TABLE (pci, tc35815_pci_tbl);

/* see MODULE_PARM_DESC */
static struct tc35815_options {
	int speed;
	int duplex;
	int doforce;
} options;

/*
 * Registers
 */
struct tc35815_regs {
	volatile __u32 DMA_Ctl;		/* 0x00 */
	volatile __u32 TxFrmPtr;
	volatile __u32 TxThrsh;
	volatile __u32 TxPollCtr;
	volatile __u32 BLFrmPtr;
	volatile __u32 RxFragSize;
	volatile __u32 Int_En;
	volatile __u32 FDA_Bas;
	volatile __u32 FDA_Lim;		/* 0x20 */
	volatile __u32 Int_Src;
	volatile __u32 unused0[2];
	volatile __u32 PauseCnt;
	volatile __u32 RemPauCnt;
	volatile __u32 TxCtlFrmStat;
	volatile __u32 unused1;
	volatile __u32 MAC_Ctl;		/* 0x40 */
	volatile __u32 CAM_Ctl;
	volatile __u32 Tx_Ctl;
	volatile __u32 Tx_Stat;
	volatile __u32 Rx_Ctl;
	volatile __u32 Rx_Stat;
	volatile __u32 MD_Data;
	volatile __u32 MD_CA;
	volatile __u32 CAM_Adr;		/* 0x60 */
	volatile __u32 CAM_Data;
	volatile __u32 CAM_Ena;
	volatile __u32 PROM_Ctl;
	volatile __u32 PROM_Data;
	volatile __u32 Algn_Cnt;
	volatile __u32 CRC_Cnt;
	volatile __u32 Miss_Cnt;
};

/*
 * Bit assignments
 */
/* DMA_Ctl bit asign ------------------------------------------------------- */
#define DMA_RxAlign            0x00c00000 /* 1:Reception Alignment           */
#define DMA_RxAlign_1          0x00400000
#define DMA_RxAlign_2          0x00800000
#define DMA_RxAlign_3          0x00c00000
#define DMA_M66EnStat          0x00080000 /* 1:66MHz Enable State            */
#define DMA_IntMask            0x00040000 /* 1:Interupt mask                 */
#define DMA_SWIntReq           0x00020000 /* 1:Software Interrupt request    */
#define DMA_TxWakeUp           0x00010000 /* 1:Transmit Wake Up              */
#define DMA_RxBigE             0x00008000 /* 1:Receive Big Endian            */
#define DMA_TxBigE             0x00004000 /* 1:Transmit Big Endian           */
#define DMA_TestMode           0x00002000 /* 1:Test Mode                     */
#define DMA_PowrMgmnt          0x00001000 /* 1:Power Management              */
#define DMA_DmBurst_Mask       0x000001fc /* DMA Burst size                  */

/* RxFragSize bit asign ---------------------------------------------------- */
#define RxFrag_EnPack          0x00008000 /* 1:Enable Packing                */
#define RxFrag_MinFragMask     0x00000ffc /* Minimum Fragment                */

/* MAC_Ctl bit asign ------------------------------------------------------- */
#define MAC_Link10             0x00008000 /* 1:Link Status 10Mbits           */
#define MAC_EnMissRoll         0x00002000 /* 1:Enable Missed Roll            */
#define MAC_MissRoll           0x00000400 /* 1:Missed Roll                   */
#define MAC_Loop10             0x00000080 /* 1:Loop 10 Mbps                  */
#define MAC_Conn_Auto          0x00000000 /*00:Connection mode (Automatic)   */
#define MAC_Conn_10M           0x00000020 /*01:                (10Mbps endec)*/
#define MAC_Conn_Mll           0x00000040 /*10:                (Mll clock)   */
#define MAC_MacLoop            0x00000010 /* 1:MAC Loopback                  */
#define MAC_FullDup            0x00000008 /* 1:Full Duplex 0:Half Duplex     */
#define MAC_Reset              0x00000004 /* 1:Software Reset                */
#define MAC_HaltImm            0x00000002 /* 1:Halt Immediate                */
#define MAC_HaltReq            0x00000001 /* 1:Halt request                  */

/* PROM_Ctl bit asign ------------------------------------------------------ */
#define PROM_Busy              0x00008000 /* 1:Busy (Start Operation)        */
#define PROM_Read              0x00004000 /*10:Read operation                */
#define PROM_Write             0x00002000 /*01:Write operation               */
#define PROM_Erase             0x00006000 /*11:Erase operation               */
                                          /*00:Enable or Disable Writting,   */
                                          /*      as specified in PROM_Addr. */
#define PROM_Addr_Ena          0x00000030 /*11xxxx:PROM Write enable         */
                                          /*00xxxx:           disable        */

/* CAM_Ctl bit asign ------------------------------------------------------- */
#define CAM_CompEn             0x00000010 /* 1:CAM Compare Enable            */
#define CAM_NegCAM             0x00000008 /* 1:Reject packets CAM recognizes,*/
                                          /*                    accept other */
#define CAM_BroadAcc           0x00000004 /* 1:Broadcast assept              */
#define CAM_GroupAcc           0x00000002 /* 1:Multicast assept              */
#define CAM_StationAcc         0x00000001 /* 1:unicast accept                */

/* CAM_Ena bit asign ------------------------------------------------------- */
#define CAM_ENTRY_MAX                  21   /* CAM Data entry max count      */
#define CAM_Ena_Mask ((1<<CAM_ENTRY_MAX)-1) /* CAM Enable bits (Max 21bits)  */
#define CAM_Ena_Bit(index)         (1<<(index))
#define CAM_ENTRY_DESTINATION	0
#define CAM_ENTRY_SOURCE	1
#define CAM_ENTRY_MACCTL	20

/* Tx_Ctl bit asign -------------------------------------------------------- */
#define Tx_En                  0x00000001 /* 1:Transmit enable               */
#define Tx_TxHalt              0x00000002 /* 1:Transmit Halt Request         */
#define Tx_NoPad               0x00000004 /* 1:Suppress Padding              */
#define Tx_NoCRC               0x00000008 /* 1:Suppress Padding              */
#define Tx_FBack               0x00000010 /* 1:Fast Back-off                 */
#define Tx_EnUnder             0x00000100 /* 1:Enable Underrun               */
#define Tx_EnExDefer           0x00000200 /* 1:Enable Excessive Deferral     */
#define Tx_EnLCarr             0x00000400 /* 1:Enable Lost Carrier           */
#define Tx_EnExColl            0x00000800 /* 1:Enable Excessive Collision    */
#define Tx_EnLateColl          0x00001000 /* 1:Enable Late Collision         */
#define Tx_EnTxPar             0x00002000 /* 1:Enable Transmit Parity        */
#define Tx_EnComp              0x00004000 /* 1:Enable Completion             */

/* Tx_Stat bit asign ------------------------------------------------------- */
#define Tx_TxColl_MASK         0x0000000F /* Tx Collision Count              */
#define Tx_ExColl              0x00000010 /* Excessive Collision             */
#define Tx_TXDefer             0x00000020 /* Transmit Defered                */
#define Tx_Paused              0x00000040 /* Transmit Paused                 */
#define Tx_IntTx               0x00000080 /* Interrupt on Tx                 */
#define Tx_Under               0x00000100 /* Underrun                        */
#define Tx_Defer               0x00000200 /* Deferral                        */
#define Tx_NCarr               0x00000400 /* No Carrier                      */
#define Tx_10Stat              0x00000800 /* 10Mbps Status                   */
#define Tx_LateColl            0x00001000 /* Late Collision                  */
#define Tx_TxPar               0x00002000 /* Tx Parity Error                 */
#define Tx_Comp                0x00004000 /* Completion                      */
#define Tx_Halted              0x00008000 /* Tx Halted                       */
#define Tx_SQErr               0x00010000 /* Signal Quality Error(SQE)       */

/* Rx_Ctl bit asign -------------------------------------------------------- */
#define Rx_EnGood              0x00004000 /* 1:Enable Good                   */
#define Rx_EnRxPar             0x00002000 /* 1:Enable Receive Parity         */
#define Rx_EnLongErr           0x00000800 /* 1:Enable Long Error             */
#define Rx_EnOver              0x00000400 /* 1:Enable OverFlow               */
#define Rx_EnCRCErr            0x00000200 /* 1:Enable CRC Error              */
#define Rx_EnAlign             0x00000100 /* 1:Enable Alignment              */
#define Rx_IgnoreCRC           0x00000040 /* 1:Ignore CRC Value              */
#define Rx_StripCRC            0x00000010 /* 1:Strip CRC Value               */
#define Rx_ShortEn             0x00000008 /* 1:Short Enable                  */
#define Rx_LongEn              0x00000004 /* 1:Long Enable                   */
#define Rx_RxHalt              0x00000002 /* 1:Receive Halt Request          */
#define Rx_RxEn                0x00000001 /* 1:Receive Intrrupt Enable       */

/* Rx_Stat bit asign ------------------------------------------------------- */
#define Rx_Halted              0x00008000 /* Rx Halted                       */
#define Rx_Good                0x00004000 /* Rx Good                         */
#define Rx_RxPar               0x00002000 /* Rx Parity Error                 */
                            /* 0x00001000    not use                         */
#define Rx_LongErr             0x00000800 /* Rx Long Error                   */
#define Rx_Over                0x00000400 /* Rx Overflow                     */
#define Rx_CRCErr              0x00000200 /* Rx CRC Error                    */
#define Rx_Align               0x00000100 /* Rx Alignment Error              */
#define Rx_10Stat              0x00000080 /* Rx 10Mbps Status                */
#define Rx_IntRx               0x00000040 /* Rx Interrupt                    */
#define Rx_CtlRecd             0x00000020 /* Rx Control Receive              */

#define Rx_Stat_Mask           0x0000EFC0 /* Rx All Status Mask              */

/* Int_En bit asign -------------------------------------------------------- */
#define Int_NRAbtEn            0x00000800 /* 1:Non-recoverable Abort Enable  */
#define Int_TxCtlCmpEn         0x00000400 /* 1:Transmit Control Complete Enable */
#define Int_DmParErrEn         0x00000200 /* 1:DMA Parity Error Enable       */
#define Int_DParDEn            0x00000100 /* 1:Data Parity Error Enable      */
#define Int_EarNotEn           0x00000080 /* 1:Early Notify Enable           */
#define Int_DParErrEn          0x00000040 /* 1:Detected Parity Error Enable  */
#define Int_SSysErrEn          0x00000020 /* 1:Signalled System Error Enable */
#define Int_RMasAbtEn          0x00000010 /* 1:Received Master Abort Enable  */
#define Int_RTargAbtEn         0x00000008 /* 1:Received Target Abort Enable  */
#define Int_STargAbtEn         0x00000004 /* 1:Signalled Target Abort Enable */
#define Int_BLExEn             0x00000002 /* 1:Buffer List Exhausted Enable  */
#define Int_FDAExEn            0x00000001 /* 1:Free Descriptor Area          */
                                          /*               Exhausted Enable  */

/* Int_Src bit asign ------------------------------------------------------- */
#define Int_NRabt              0x00004000 /* 1:Non Recoverable error         */
#define Int_DmParErrStat       0x00002000 /* 1:DMA Parity Error & Clear      */
#define Int_BLEx               0x00001000 /* 1:Buffer List Empty & Clear     */
#define Int_FDAEx              0x00000800 /* 1:FDA Empty & Clear             */
#define Int_IntNRAbt           0x00000400 /* 1:Non Recoverable Abort         */
#define	Int_IntCmp             0x00000200 /* 1:MAC control packet complete   */
#define Int_IntExBD            0x00000100 /* 1:Interrupt Extra BD & Clear    */
#define Int_DmParErr           0x00000080 /* 1:DMA Parity Error & Clear      */
#define Int_IntEarNot          0x00000040 /* 1:Receive Data write & Clear    */
#define Int_SWInt              0x00000020 /* 1:Software request & Clear      */
#define Int_IntBLEx            0x00000010 /* 1:Buffer List Empty & Clear     */
#define Int_IntFDAEx           0x00000008 /* 1:FDA Empty & Clear             */
#define Int_IntPCI             0x00000004 /* 1:PCI controller & Clear        */
#define Int_IntMacRx           0x00000002 /* 1:Rx controller & Clear         */
#define Int_IntMacTx           0x00000001 /* 1:Tx controller & Clear         */

/* MD_CA bit asign --------------------------------------------------------- */
#define MD_CA_PreSup           0x00001000 /* 1:Preamble Supress              */
#define MD_CA_Busy             0x00000800 /* 1:Busy (Start Operation)        */
#define MD_CA_Wr               0x00000400 /* 1:Write 0:Read                  */


/*
 * Descriptors
 */

/* Frame descripter */
struct FDesc {
	volatile __u32 FDNext;
	volatile __u32 FDSystem;
	volatile __u32 FDStat;
	volatile __u32 FDCtl;
};

/* Buffer descripter */
struct BDesc {
	volatile __u32 BuffData;
	volatile __u32 BDCtl;
};

#define FD_ALIGN	16

/* Frame Descripter bit asign ---------------------------------------------- */
#define FD_FDLength_MASK       0x0000FFFF /* Length MASK                     */
#define FD_BDCnt_MASK          0x001F0000 /* BD count MASK in FD             */
#define FD_FrmOpt_MASK         0x7C000000 /* Frame option MASK               */
#define FD_FrmOpt_BigEndian    0x40000000 /* Tx/Rx */
#define FD_FrmOpt_IntTx        0x20000000 /* Tx only */
#define FD_FrmOpt_NoCRC        0x10000000 /* Tx only */
#define FD_FrmOpt_NoPadding    0x08000000 /* Tx only */
#define FD_FrmOpt_Packing      0x04000000 /* Rx only */
#define FD_CownsFD             0x80000000 /* FD Controller owner bit         */
#define FD_Next_EOL            0x00000001 /* FD EOL indicator                */
#define FD_BDCnt_SHIFT         16

/* Buffer Descripter bit asign --------------------------------------------- */
#define BD_BuffLength_MASK     0x0000FFFF /* Recieve Data Size               */
#define BD_RxBDID_MASK         0x00FF0000 /* BD ID Number MASK               */
#define BD_RxBDSeqN_MASK       0x7F000000 /* Rx BD Sequence Number           */
#define BD_CownsBD             0x80000000 /* BD Controller owner bit         */
#define BD_RxBDID_SHIFT        16
#define BD_RxBDSeqN_SHIFT      24


/* Some useful constants. */
#undef NO_CHECK_CARRIER	/* Does not check No-Carrier with TP */

#ifdef NO_CHECK_CARRIER
#define TX_CTL_CMD	(Tx_EnComp | Tx_EnTxPar | Tx_EnLateColl | \
	Tx_EnExColl | Tx_EnExDefer | Tx_EnUnder | \
	Tx_En)	/* maybe  0x7b01 */
#else
#define TX_CTL_CMD	(Tx_EnComp | Tx_EnTxPar | Tx_EnLateColl | \
	Tx_EnExColl | Tx_EnLCarr | Tx_EnExDefer | Tx_EnUnder | \
	Tx_En)	/* maybe  0x7b01 */
#endif
#define RX_CTL_CMD	(Rx_EnGood | Rx_EnRxPar | Rx_EnLongErr | Rx_EnOver \
	| Rx_EnCRCErr | Rx_EnAlign | Rx_RxEn)	/* maybe 0x6f01 */
#define INT_EN_CMD  (Int_NRAbtEn | \
	Int_DmParErrEn | Int_DParDEn | Int_DParErrEn | \
	Int_SSysErrEn  | Int_RMasAbtEn | Int_RTargAbtEn | \
	Int_STargAbtEn | \
	Int_BLExEn  | Int_FDAExEn) /* maybe 0xb7f*/
#define DMA_CTL_CMD	DMA_BURST_SIZE
#define HAVE_DMA_RXALIGN(lp)	likely((lp)->boardtype != TC35815CF)

/* Tuning parameters */
#define DMA_BURST_SIZE	32
#define TX_THRESHOLD	1024
#define TX_THRESHOLD_MAX 1536       /* used threshold with packet max byte for low pci transfer ability.*/
#define TX_THRESHOLD_KEEP_LIMIT 10  /* setting threshold max value when overrun error occured this count. */

/* 16 + RX_BUF_NUM * 8 + RX_FD_NUM * 16 + TX_FD_NUM * 32 <= PAGE_SIZE*FD_PAGE_NUM */
#ifdef TC35815_USE_PACKEDBUFFER
#define FD_PAGE_NUM 2
#define RX_BUF_NUM	8	/* >= 2 */
#define RX_FD_NUM	250	/* >= 32 */
#define TX_FD_NUM	128
#define RX_BUF_SIZE	PAGE_SIZE
#else /* TC35815_USE_PACKEDBUFFER */
#define FD_PAGE_NUM 4
#define RX_BUF_NUM	128	/* < 256 */
#define RX_FD_NUM	256	/* >= 32 */
#define TX_FD_NUM	128
#if RX_CTL_CMD & Rx_LongEn
#define RX_BUF_SIZE	PAGE_SIZE
#elif RX_CTL_CMD & Rx_StripCRC
#define RX_BUF_SIZE	ALIGN(ETH_FRAME_LEN + 4 + 2, 32) /* +2: reserve */
#else
#define RX_BUF_SIZE	ALIGN(ETH_FRAME_LEN + 2, 32) /* +2: reserve */
#endif
#endif /* TC35815_USE_PACKEDBUFFER */
#define RX_FD_RESERVE	(2 / 2)	/* max 2 BD per RxFD */
#define NAPI_WEIGHT	16

struct TxFD {
	struct FDesc fd;
	struct BDesc bd;
	struct BDesc unused;
};

struct RxFD {
	struct FDesc fd;
	struct BDesc bd[0];	/* variable length */
};

struct FrFD {
	struct FDesc fd;
	struct BDesc bd[RX_BUF_NUM];
};


#define tc_readl(addr)	readl(addr)
#define tc_writel(d, addr)	writel(d, addr)

#define TC35815_TX_TIMEOUT  msecs_to_jiffies(400)

/* Timer state engine. */
enum tc35815_timer_state {
	arbwait  = 0,	/* Waiting for auto negotiation to complete.          */
	lupwait  = 1,	/* Auto-neg complete, awaiting link-up status.        */
	ltrywait = 2,	/* Forcing try of all modes, from fastest to slowest. */
	asleep   = 3,	/* Time inactive.                                     */
	lcheck   = 4,	/* Check link status.                                 */
};

/* Information that need to be kept for each board. */
struct tc35815_local {
	struct pci_dev *pci_dev;

	struct net_device *dev;
	struct napi_struct napi;

	/* statistics */
	struct net_device_stats stats;
	struct {
		int max_tx_qlen;
		int tx_ints;
		int rx_ints;
	        int tx_underrun;
	} lstats;

	/* Tx control lock.  This protects the transmit buffer ring
	 * state along with the "tx full" state of the driver.  This
	 * means all netif_queue flow control actions are protected
	 * by this lock as well.
	 */
	spinlock_t lock;

	int phy_addr;
	int fullduplex;
	unsigned short saved_lpa;
	struct timer_list timer;
	enum tc35815_timer_state timer_state; /* State of auto-neg timer. */
	unsigned int timer_ticks;	/* Number of clicks at each state  */

	/*
	 * Transmitting: Batch Mode.
	 *	1 BD in 1 TxFD.
	 * Receiving: Packing Mode. (TC35815_USE_PACKEDBUFFER)
	 *	1 circular FD for Free Buffer List.
	 *	RX_BUF_NUM BD in Free Buffer FD.
	 *	One Free Buffer BD has PAGE_SIZE data buffer.
	 * Or Non-Packing Mode.
	 *	1 circular FD for Free Buffer List.
	 *	RX_BUF_NUM BD in Free Buffer FD.
	 *	One Free Buffer BD has ETH_FRAME_LEN data buffer.
	 */
	void * fd_buf;	/* for TxFD, RxFD, FrFD */
	dma_addr_t fd_buf_dma;
	struct TxFD *tfd_base;
	unsigned int tfd_start;
	unsigned int tfd_end;
	struct RxFD *rfd_base;
	struct RxFD *rfd_limit;
	struct RxFD *rfd_cur;
	struct FrFD *fbl_ptr;
#ifdef TC35815_USE_PACKEDBUFFER
	unsigned char fbl_curid;
	void * data_buf[RX_BUF_NUM];		/* packing */
	dma_addr_t data_buf_dma[RX_BUF_NUM];
	struct {
		struct sk_buff *skb;
		dma_addr_t skb_dma;
	} tx_skbs[TX_FD_NUM];
#else
	unsigned int fbl_count;
	struct {
		struct sk_buff *skb;
		dma_addr_t skb_dma;
	} tx_skbs[TX_FD_NUM], rx_skbs[RX_BUF_NUM];
#endif
	struct mii_if_info mii;
	unsigned short mii_id[2];
	u32 msg_enable;
	board_t boardtype;
};

static inline dma_addr_t fd_virt_to_bus(struct tc35815_local *lp, void *virt)
{
	return lp->fd_buf_dma + ((u8 *)virt - (u8 *)lp->fd_buf);
}
#ifdef DEBUG
static inline void *fd_bus_to_virt(struct tc35815_local *lp, dma_addr_t bus)
{
	return (void *)((u8 *)lp->fd_buf + (bus - lp->fd_buf_dma));
}
#endif
#ifdef TC35815_USE_PACKEDBUFFER
static inline void *rxbuf_bus_to_virt(struct tc35815_local *lp, dma_addr_t bus)
{
	int i;
	for (i = 0; i < RX_BUF_NUM; i++) {
		if (bus >= lp->data_buf_dma[i] &&
		    bus < lp->data_buf_dma[i] + PAGE_SIZE)
			return (void *)((u8 *)lp->data_buf[i] +
					(bus - lp->data_buf_dma[i]));
	}
	return NULL;
}

#define TC35815_DMA_SYNC_ONDEMAND
static void* alloc_rxbuf_page(struct pci_dev *hwdev, dma_addr_t *dma_handle)
{
#ifdef TC35815_DMA_SYNC_ONDEMAND
	void *buf;
	/* pci_map + pci_dma_sync will be more effective than
	 * pci_alloc_consistent on some archs. */
	if ((buf = (void *)__get_free_page(GFP_ATOMIC)) == NULL)
		return NULL;
	*dma_handle = pci_map_single(hwdev, buf, PAGE_SIZE,
				     PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(*dma_handle)) {
		free_page((unsigned long)buf);
		return NULL;
	}
	return buf;
#else
	return pci_alloc_consistent(hwdev, PAGE_SIZE, dma_handle);
#endif
}

static void free_rxbuf_page(struct pci_dev *hwdev, void *buf, dma_addr_t dma_handle)
{
#ifdef TC35815_DMA_SYNC_ONDEMAND
	pci_unmap_single(hwdev, dma_handle, PAGE_SIZE, PCI_DMA_FROMDEVICE);
	free_page((unsigned long)buf);
#else
	pci_free_consistent(hwdev, PAGE_SIZE, buf, dma_handle);
#endif
}
#else /* TC35815_USE_PACKEDBUFFER */
static struct sk_buff *alloc_rxbuf_skb(struct net_device *dev,
				       struct pci_dev *hwdev,
				       dma_addr_t *dma_handle)
{
	struct sk_buff *skb;
	skb = dev_alloc_skb(RX_BUF_SIZE);
	if (!skb)
		return NULL;
	*dma_handle = pci_map_single(hwdev, skb->data, RX_BUF_SIZE,
				     PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(*dma_handle)) {
		dev_kfree_skb_any(skb);
		return NULL;
	}
	skb_reserve(skb, 2);	/* make IP header 4byte aligned */
	return skb;
}

static void free_rxbuf_skb(struct pci_dev *hwdev, struct sk_buff *skb, dma_addr_t dma_handle)
{
	pci_unmap_single(hwdev, dma_handle, RX_BUF_SIZE,
			 PCI_DMA_FROMDEVICE);
	dev_kfree_skb_any(skb);
}
#endif /* TC35815_USE_PACKEDBUFFER */

/* Index to functions, as function prototypes. */

static int	tc35815_open(struct net_device *dev);
static int	tc35815_send_packet(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t	tc35815_interrupt(int irq, void *dev_id);
#ifdef TC35815_NAPI
static int	tc35815_rx(struct net_device *dev, int limit);
static int	tc35815_poll(struct napi_struct *napi, int budget);
#else
static void	tc35815_rx(struct net_device *dev);
#endif
static void	tc35815_txdone(struct net_device *dev);
static int	tc35815_close(struct net_device *dev);
static struct	net_device_stats *tc35815_get_stats(struct net_device *dev);
static void	tc35815_set_multicast_list(struct net_device *dev);
static void     tc35815_tx_timeout(struct net_device *dev);
static int	tc35815_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
#ifdef CONFIG_NET_POLL_CONTROLLER
static void	tc35815_poll_controller(struct net_device *dev);
#endif
static const struct ethtool_ops tc35815_ethtool_ops;

/* Example routines you must write ;->. */
static void 	tc35815_chip_reset(struct net_device *dev);
static void 	tc35815_chip_init(struct net_device *dev);
static void	tc35815_find_phy(struct net_device *dev);
static void 	tc35815_phy_chip_init(struct net_device *dev);

#ifdef DEBUG
static void	panic_queues(struct net_device *dev);
#endif

static void tc35815_timer(unsigned long data);
static void tc35815_start_auto_negotiation(struct net_device *dev,
					   struct ethtool_cmd *ep);
static int tc_mdio_read(struct net_device *dev, int phy_id, int location);
static void tc_mdio_write(struct net_device *dev, int phy_id, int location,
			  int val);

#ifdef CONFIG_CPU_TX49XX
/*
 * Find a platform_device providing a MAC address.  The platform code
 * should provide a "tc35815-mac" device with a MAC address in its
 * platform_data.
 */
static int __devinit tc35815_mac_match(struct device *dev, void *data)
{
	struct platform_device *plat_dev = to_platform_device(dev);
	struct pci_dev *pci_dev = data;
	unsigned int id = (pci_dev->bus->number << 8) | pci_dev->devfn;
	return !strcmp(plat_dev->name, "tc35815-mac") && plat_dev->id == id;
}

static int __devinit tc35815_read_plat_dev_addr(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	struct device *pd = bus_find_device(&platform_bus_type, NULL,
					    lp->pci_dev, tc35815_mac_match);
	if (pd) {
		if (pd->platform_data)
			memcpy(dev->dev_addr, pd->platform_data, ETH_ALEN);
		put_device(pd);
		return is_valid_ether_addr(dev->dev_addr) ? 0 : -ENODEV;
	}
	return -ENODEV;
}
#else
static int __devinit tc35815_read_plat_dev_addr(struct net_device *dev)
{
	return -ENODEV;
}
#endif

static int __devinit tc35815_init_dev_addr (struct net_device *dev)
{
	struct tc35815_regs __iomem *tr =
		(struct tc35815_regs __iomem *)dev->base_addr;
	int i;

	while (tc_readl(&tr->PROM_Ctl) & PROM_Busy)
		;
	for (i = 0; i < 6; i += 2) {
		unsigned short data;
		tc_writel(PROM_Busy | PROM_Read | (i / 2 + 2), &tr->PROM_Ctl);
		while (tc_readl(&tr->PROM_Ctl) & PROM_Busy)
			;
		data = tc_readl(&tr->PROM_Data);
		dev->dev_addr[i] = data & 0xff;
		dev->dev_addr[i+1] = data >> 8;
	}
	if (!is_valid_ether_addr(dev->dev_addr))
		return tc35815_read_plat_dev_addr(dev);
	return 0;
}

static int __devinit tc35815_init_one (struct pci_dev *pdev,
				       const struct pci_device_id *ent)
{
	void __iomem *ioaddr = NULL;
	struct net_device *dev;
	struct tc35815_local *lp;
	int rc;
	unsigned long mmio_start, mmio_end, mmio_flags, mmio_len;

	static int printed_version;
	if (!printed_version++) {
		printk(version);
		dev_printk(KERN_DEBUG, &pdev->dev,
			   "speed:%d duplex:%d doforce:%d\n",
			   options.speed, options.duplex, options.doforce);
	}

	if (!pdev->irq) {
		dev_warn(&pdev->dev, "no IRQ assigned.\n");
		return -ENODEV;
	}

	/* dev zeroed in alloc_etherdev */
	dev = alloc_etherdev (sizeof (*lp));
	if (dev == NULL) {
		dev_err(&pdev->dev, "unable to alloc new ethernet\n");
		return -ENOMEM;
	}
	SET_NETDEV_DEV(dev, &pdev->dev);
	lp = dev->priv;
	lp->dev = dev;

	/* enable device (incl. PCI PM wakeup), and bus-mastering */
	rc = pci_enable_device (pdev);
	if (rc)
		goto err_out;

	mmio_start = pci_resource_start (pdev, 1);
	mmio_end = pci_resource_end (pdev, 1);
	mmio_flags = pci_resource_flags (pdev, 1);
	mmio_len = pci_resource_len (pdev, 1);

	/* set this immediately, we need to know before
	 * we talk to the chip directly */

	/* make sure PCI base addr 1 is MMIO */
	if (!(mmio_flags & IORESOURCE_MEM)) {
		dev_err(&pdev->dev, "region #1 not an MMIO resource, aborting\n");
		rc = -ENODEV;
		goto err_out;
	}

	/* check for weird/broken PCI region reporting */
	if ((mmio_len < sizeof(struct tc35815_regs))) {
		dev_err(&pdev->dev, "Invalid PCI region size(s), aborting\n");
		rc = -ENODEV;
		goto err_out;
	}

	rc = pci_request_regions (pdev, MODNAME);
	if (rc)
		goto err_out;

	pci_set_master (pdev);

	/* ioremap MMIO region */
	ioaddr = ioremap (mmio_start, mmio_len);
	if (ioaddr == NULL) {
		dev_err(&pdev->dev, "cannot remap MMIO, aborting\n");
		rc = -EIO;
		goto err_out_free_res;
	}

	/* Initialize the device structure. */
	dev->open = tc35815_open;
	dev->hard_start_xmit = tc35815_send_packet;
	dev->stop = tc35815_close;
	dev->get_stats = tc35815_get_stats;
	dev->set_multicast_list = tc35815_set_multicast_list;
	dev->do_ioctl = tc35815_ioctl;
	dev->ethtool_ops = &tc35815_ethtool_ops;
	dev->tx_timeout = tc35815_tx_timeout;
	dev->watchdog_timeo = TC35815_TX_TIMEOUT;
#ifdef TC35815_NAPI
	netif_napi_add(dev, &lp->napi, tc35815_poll, NAPI_WEIGHT);
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
	dev->poll_controller = tc35815_poll_controller;
#endif

	dev->irq = pdev->irq;
	dev->base_addr = (unsigned long) ioaddr;

	spin_lock_init(&lp->lock);
	lp->pci_dev = pdev;
	lp->boardtype = ent->driver_data;

	lp->msg_enable = NETIF_MSG_TX_ERR | NETIF_MSG_HW | NETIF_MSG_DRV | NETIF_MSG_LINK;
	pci_set_drvdata(pdev, dev);

	/* Soft reset the chip. */
	tc35815_chip_reset(dev);

	/* Retrieve the ethernet address. */
	if (tc35815_init_dev_addr(dev)) {
		dev_warn(&pdev->dev, "not valid ether addr\n");
		random_ether_addr(dev->dev_addr);
	}

	rc = register_netdev (dev);
	if (rc)
		goto err_out_unmap;

	memcpy(dev->perm_addr, dev->dev_addr, dev->addr_len);
	printk(KERN_INFO "%s: %s at 0x%lx, "
		"%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x, "
		"IRQ %d\n",
		dev->name,
		board_info[ent->driver_data].name,
		dev->base_addr,
		dev->dev_addr[0], dev->dev_addr[1],
		dev->dev_addr[2], dev->dev_addr[3],
		dev->dev_addr[4], dev->dev_addr[5],
		dev->irq);

	setup_timer(&lp->timer, tc35815_timer, (unsigned long) dev);
	lp->mii.dev = dev;
	lp->mii.mdio_read = tc_mdio_read;
	lp->mii.mdio_write = tc_mdio_write;
	lp->mii.phy_id_mask = 0x1f;
	lp->mii.reg_num_mask = 0x1f;
	tc35815_find_phy(dev);
	lp->mii.phy_id = lp->phy_addr;
	lp->mii.full_duplex = 0;
	lp->mii.force_media = 0;

	return 0;

err_out_unmap:
	iounmap(ioaddr);
err_out_free_res:
	pci_release_regions (pdev);
err_out:
	free_netdev (dev);
	return rc;
}


static void __devexit tc35815_remove_one (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata (pdev);
	unsigned long mmio_addr;

	mmio_addr = dev->base_addr;

	unregister_netdev (dev);

	if (mmio_addr) {
		iounmap ((void __iomem *)mmio_addr);
		pci_release_regions (pdev);
	}

	free_netdev (dev);

	pci_set_drvdata (pdev, NULL);
}

static int
tc35815_init_queues(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	int i;
	unsigned long fd_addr;

	if (!lp->fd_buf) {
		BUG_ON(sizeof(struct FDesc) +
		       sizeof(struct BDesc) * RX_BUF_NUM +
		       sizeof(struct FDesc) * RX_FD_NUM +
		       sizeof(struct TxFD) * TX_FD_NUM >
		       PAGE_SIZE * FD_PAGE_NUM);

		if ((lp->fd_buf = pci_alloc_consistent(lp->pci_dev, PAGE_SIZE * FD_PAGE_NUM, &lp->fd_buf_dma)) == 0)
			return -ENOMEM;
		for (i = 0; i < RX_BUF_NUM; i++) {
#ifdef TC35815_USE_PACKEDBUFFER
			if ((lp->data_buf[i] = alloc_rxbuf_page(lp->pci_dev, &lp->data_buf_dma[i])) == NULL) {
				while (--i >= 0) {
					free_rxbuf_page(lp->pci_dev,
							lp->data_buf[i],
							lp->data_buf_dma[i]);
					lp->data_buf[i] = NULL;
				}
				pci_free_consistent(lp->pci_dev,
						    PAGE_SIZE * FD_PAGE_NUM,
						    lp->fd_buf,
						    lp->fd_buf_dma);
				lp->fd_buf = NULL;
				return -ENOMEM;
			}
#else
			lp->rx_skbs[i].skb =
				alloc_rxbuf_skb(dev, lp->pci_dev,
						&lp->rx_skbs[i].skb_dma);
			if (!lp->rx_skbs[i].skb) {
				while (--i >= 0) {
					free_rxbuf_skb(lp->pci_dev,
						       lp->rx_skbs[i].skb,
						       lp->rx_skbs[i].skb_dma);
					lp->rx_skbs[i].skb = NULL;
				}
				pci_free_consistent(lp->pci_dev,
						    PAGE_SIZE * FD_PAGE_NUM,
						    lp->fd_buf,
						    lp->fd_buf_dma);
				lp->fd_buf = NULL;
				return -ENOMEM;
			}
#endif
		}
		printk(KERN_DEBUG "%s: FD buf %p DataBuf",
		       dev->name, lp->fd_buf);
#ifdef TC35815_USE_PACKEDBUFFER
		printk(" DataBuf");
		for (i = 0; i < RX_BUF_NUM; i++)
			printk(" %p", lp->data_buf[i]);
#endif
		printk("\n");
	} else {
		for (i = 0; i < FD_PAGE_NUM; i++) {
			clear_page((void *)((unsigned long)lp->fd_buf + i * PAGE_SIZE));
		}
	}
	fd_addr = (unsigned long)lp->fd_buf;

	/* Free Descriptors (for Receive) */
	lp->rfd_base = (struct RxFD *)fd_addr;
	fd_addr += sizeof(struct RxFD) * RX_FD_NUM;
	for (i = 0; i < RX_FD_NUM; i++) {
		lp->rfd_base[i].fd.FDCtl = cpu_to_le32(FD_CownsFD);
	}
	lp->rfd_cur = lp->rfd_base;
	lp->rfd_limit = (struct RxFD *)fd_addr - (RX_FD_RESERVE + 1);

	/* Transmit Descriptors */
	lp->tfd_base = (struct TxFD *)fd_addr;
	fd_addr += sizeof(struct TxFD) * TX_FD_NUM;
	for (i = 0; i < TX_FD_NUM; i++) {
		lp->tfd_base[i].fd.FDNext = cpu_to_le32(fd_virt_to_bus(lp, &lp->tfd_base[i+1]));
		lp->tfd_base[i].fd.FDSystem = cpu_to_le32(0xffffffff);
		lp->tfd_base[i].fd.FDCtl = cpu_to_le32(0);
	}
	lp->tfd_base[TX_FD_NUM-1].fd.FDNext = cpu_to_le32(fd_virt_to_bus(lp, &lp->tfd_base[0]));
	lp->tfd_start = 0;
	lp->tfd_end = 0;

	/* Buffer List (for Receive) */
	lp->fbl_ptr = (struct FrFD *)fd_addr;
	lp->fbl_ptr->fd.FDNext = cpu_to_le32(fd_virt_to_bus(lp, lp->fbl_ptr));
	lp->fbl_ptr->fd.FDCtl = cpu_to_le32(RX_BUF_NUM | FD_CownsFD);
#ifndef TC35815_USE_PACKEDBUFFER
	/*
	 * move all allocated skbs to head of rx_skbs[] array.
	 * fbl_count mighe not be RX_BUF_NUM if alloc_rxbuf_skb() in
	 * tc35815_rx() had failed.
	 */
	lp->fbl_count = 0;
	for (i = 0; i < RX_BUF_NUM; i++) {
		if (lp->rx_skbs[i].skb) {
			if (i != lp->fbl_count) {
				lp->rx_skbs[lp->fbl_count].skb =
					lp->rx_skbs[i].skb;
				lp->rx_skbs[lp->fbl_count].skb_dma =
					lp->rx_skbs[i].skb_dma;
			}
			lp->fbl_count++;
		}
	}
#endif
	for (i = 0; i < RX_BUF_NUM; i++) {
#ifdef TC35815_USE_PACKEDBUFFER
		lp->fbl_ptr->bd[i].BuffData = cpu_to_le32(lp->data_buf_dma[i]);
#else
		if (i >= lp->fbl_count) {
			lp->fbl_ptr->bd[i].BuffData = 0;
			lp->fbl_ptr->bd[i].BDCtl = 0;
			continue;
		}
		lp->fbl_ptr->bd[i].BuffData =
			cpu_to_le32(lp->rx_skbs[i].skb_dma);
#endif
		/* BDID is index of FrFD.bd[] */
		lp->fbl_ptr->bd[i].BDCtl =
			cpu_to_le32(BD_CownsBD | (i << BD_RxBDID_SHIFT) |
				    RX_BUF_SIZE);
	}
#ifdef TC35815_USE_PACKEDBUFFER
	lp->fbl_curid = 0;
#endif

	printk(KERN_DEBUG "%s: TxFD %p RxFD %p FrFD %p\n",
	       dev->name, lp->tfd_base, lp->rfd_base, lp->fbl_ptr);
	return 0;
}

static void
tc35815_clear_queues(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	int i;

	for (i = 0; i < TX_FD_NUM; i++) {
		u32 fdsystem = le32_to_cpu(lp->tfd_base[i].fd.FDSystem);
		struct sk_buff *skb =
			fdsystem != 0xffffffff ?
			lp->tx_skbs[fdsystem].skb : NULL;
#ifdef DEBUG
		if (lp->tx_skbs[i].skb != skb) {
			printk("%s: tx_skbs mismatch(%d).\n", dev->name, i);
			panic_queues(dev);
		}
#else
		BUG_ON(lp->tx_skbs[i].skb != skb);
#endif
		if (skb) {
			pci_unmap_single(lp->pci_dev, lp->tx_skbs[i].skb_dma, skb->len, PCI_DMA_TODEVICE);
			lp->tx_skbs[i].skb = NULL;
			lp->tx_skbs[i].skb_dma = 0;
			dev_kfree_skb_any(skb);
		}
		lp->tfd_base[i].fd.FDSystem = cpu_to_le32(0xffffffff);
	}

	tc35815_init_queues(dev);
}

static void
tc35815_free_queues(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	int i;

	if (lp->tfd_base) {
		for (i = 0; i < TX_FD_NUM; i++) {
			u32 fdsystem = le32_to_cpu(lp->tfd_base[i].fd.FDSystem);
			struct sk_buff *skb =
				fdsystem != 0xffffffff ?
				lp->tx_skbs[fdsystem].skb : NULL;
#ifdef DEBUG
			if (lp->tx_skbs[i].skb != skb) {
				printk("%s: tx_skbs mismatch(%d).\n", dev->name, i);
				panic_queues(dev);
			}
#else
			BUG_ON(lp->tx_skbs[i].skb != skb);
#endif
			if (skb) {
				dev_kfree_skb(skb);
				pci_unmap_single(lp->pci_dev, lp->tx_skbs[i].skb_dma, skb->len, PCI_DMA_TODEVICE);
				lp->tx_skbs[i].skb = NULL;
				lp->tx_skbs[i].skb_dma = 0;
			}
			lp->tfd_base[i].fd.FDSystem = cpu_to_le32(0xffffffff);
		}
	}

	lp->rfd_base = NULL;
	lp->rfd_limit = NULL;
	lp->rfd_cur = NULL;
	lp->fbl_ptr = NULL;

	for (i = 0; i < RX_BUF_NUM; i++) {
#ifdef TC35815_USE_PACKEDBUFFER
		if (lp->data_buf[i]) {
			free_rxbuf_page(lp->pci_dev,
					lp->data_buf[i], lp->data_buf_dma[i]);
			lp->data_buf[i] = NULL;
		}
#else
		if (lp->rx_skbs[i].skb) {
			free_rxbuf_skb(lp->pci_dev, lp->rx_skbs[i].skb,
				       lp->rx_skbs[i].skb_dma);
			lp->rx_skbs[i].skb = NULL;
		}
#endif
	}
	if (lp->fd_buf) {
		pci_free_consistent(lp->pci_dev, PAGE_SIZE * FD_PAGE_NUM,
				    lp->fd_buf, lp->fd_buf_dma);
		lp->fd_buf = NULL;
	}
}

static void
dump_txfd(struct TxFD *fd)
{
	printk("TxFD(%p): %08x %08x %08x %08x\n", fd,
	       le32_to_cpu(fd->fd.FDNext),
	       le32_to_cpu(fd->fd.FDSystem),
	       le32_to_cpu(fd->fd.FDStat),
	       le32_to_cpu(fd->fd.FDCtl));
	printk("BD: ");
	printk(" %08x %08x",
	       le32_to_cpu(fd->bd.BuffData),
	       le32_to_cpu(fd->bd.BDCtl));
	printk("\n");
}

static int
dump_rxfd(struct RxFD *fd)
{
	int i, bd_count = (le32_to_cpu(fd->fd.FDCtl) & FD_BDCnt_MASK) >> FD_BDCnt_SHIFT;
	if (bd_count > 8)
		bd_count = 8;
	printk("RxFD(%p): %08x %08x %08x %08x\n", fd,
	       le32_to_cpu(fd->fd.FDNext),
	       le32_to_cpu(fd->fd.FDSystem),
	       le32_to_cpu(fd->fd.FDStat),
	       le32_to_cpu(fd->fd.FDCtl));
	if (le32_to_cpu(fd->fd.FDCtl) & FD_CownsFD)
	    return 0;
	printk("BD: ");
	for (i = 0; i < bd_count; i++)
		printk(" %08x %08x",
		       le32_to_cpu(fd->bd[i].BuffData),
		       le32_to_cpu(fd->bd[i].BDCtl));
	printk("\n");
	return bd_count;
}

#if defined(DEBUG) || defined(TC35815_USE_PACKEDBUFFER)
static void
dump_frfd(struct FrFD *fd)
{
	int i;
	printk("FrFD(%p): %08x %08x %08x %08x\n", fd,
	       le32_to_cpu(fd->fd.FDNext),
	       le32_to_cpu(fd->fd.FDSystem),
	       le32_to_cpu(fd->fd.FDStat),
	       le32_to_cpu(fd->fd.FDCtl));
	printk("BD: ");
	for (i = 0; i < RX_BUF_NUM; i++)
		printk(" %08x %08x",
		       le32_to_cpu(fd->bd[i].BuffData),
		       le32_to_cpu(fd->bd[i].BDCtl));
	printk("\n");
}
#endif

#ifdef DEBUG
static void
panic_queues(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	int i;

	printk("TxFD base %p, start %u, end %u\n",
	       lp->tfd_base, lp->tfd_start, lp->tfd_end);
	printk("RxFD base %p limit %p cur %p\n",
	       lp->rfd_base, lp->rfd_limit, lp->rfd_cur);
	printk("FrFD %p\n", lp->fbl_ptr);
	for (i = 0; i < TX_FD_NUM; i++)
		dump_txfd(&lp->tfd_base[i]);
	for (i = 0; i < RX_FD_NUM; i++) {
		int bd_count = dump_rxfd(&lp->rfd_base[i]);
		i += (bd_count + 1) / 2;	/* skip BDs */
	}
	dump_frfd(lp->fbl_ptr);
	panic("%s: Illegal queue state.", dev->name);
}
#endif

static void print_eth(char *add)
{
	int i;

	printk("print_eth(%p)\n", add);
	for (i = 0; i < 6; i++)
		printk(" %2.2X", (unsigned char) add[i + 6]);
	printk(" =>");
	for (i = 0; i < 6; i++)
		printk(" %2.2X", (unsigned char) add[i]);
	printk(" : %2.2X%2.2X\n", (unsigned char) add[12], (unsigned char) add[13]);
}

static int tc35815_tx_full(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	return ((lp->tfd_start + 1) % TX_FD_NUM == lp->tfd_end);
}

static void tc35815_restart(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	int pid = lp->phy_addr;
	int do_phy_reset = 1;
	del_timer(&lp->timer);		/* Kill if running	*/

	if (lp->mii_id[0] == 0x0016 && (lp->mii_id[1] & 0xfc00) == 0xf800) {
		/* Resetting PHY cause problem on some chip... (SEEQ 80221) */
		do_phy_reset = 0;
	}
	if (do_phy_reset) {
		int timeout;
		tc_mdio_write(dev, pid, MII_BMCR, BMCR_RESET);
		timeout = 100;
		while (--timeout) {
			if (!(tc_mdio_read(dev, pid, MII_BMCR) & BMCR_RESET))
				break;
			udelay(1);
		}
		if (!timeout)
			printk(KERN_ERR "%s: BMCR reset failed.\n", dev->name);
	}

	tc35815_chip_reset(dev);
	tc35815_clear_queues(dev);
	tc35815_chip_init(dev);
	/* Reconfigure CAM again since tc35815_chip_init() initialize it. */
	tc35815_set_multicast_list(dev);
}

static void tc35815_tx_timeout(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	struct tc35815_regs __iomem *tr =
		(struct tc35815_regs __iomem *)dev->base_addr;

	printk(KERN_WARNING "%s: transmit timed out, status %#x\n",
	       dev->name, tc_readl(&tr->Tx_Stat));

	/* Try to restart the adaptor. */
	spin_lock_irq(&lp->lock);
	tc35815_restart(dev);
	spin_unlock_irq(&lp->lock);

	lp->stats.tx_errors++;

	/* If we have space available to accept new transmit
	 * requests, wake up the queueing layer.  This would
	 * be the case if the chipset_init() call above just
	 * flushes out the tx queue and empties it.
	 *
	 * If instead, the tx queue is retained then the
	 * netif_wake_queue() call should be placed in the
	 * TX completion interrupt handler of the driver instead
	 * of here.
	 */
	if (!tc35815_tx_full(dev))
		netif_wake_queue(dev);
}

/*
 * Open/initialize the board. This is called (in the current kernel)
 * sometime after booting when the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is non-reboot way to recover if something goes wrong.
 */
static int
tc35815_open(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;

	/*
	 * This is used if the interrupt line can turned off (shared).
	 * See 3c503.c for an example of selecting the IRQ at config-time.
	 */
	if (request_irq(dev->irq, &tc35815_interrupt, IRQF_SHARED, dev->name, dev)) {
		return -EAGAIN;
	}

	del_timer(&lp->timer);		/* Kill if running	*/
	tc35815_chip_reset(dev);

	if (tc35815_init_queues(dev) != 0) {
		free_irq(dev->irq, dev);
		return -EAGAIN;
	}

#ifdef TC35815_NAPI
	napi_enable(&lp->napi);
#endif

	/* Reset the hardware here. Don't forget to set the station address. */
	spin_lock_irq(&lp->lock);
	tc35815_chip_init(dev);
	spin_unlock_irq(&lp->lock);

	/* We are now ready to accept transmit requeusts from
	 * the queueing layer of the networking.
	 */
	netif_start_queue(dev);

	return 0;
}

/* This will only be invoked if your driver is _not_ in XOFF state.
 * What this means is that you need not check it, and that this
 * invariant will hold if you make sure that the netif_*_queue()
 * calls are done at the proper times.
 */
static int tc35815_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	struct TxFD *txfd;
	unsigned long flags;

	/* If some error occurs while trying to transmit this
	 * packet, you should return '1' from this function.
	 * In such a case you _may not_ do anything to the
	 * SKB, it is still owned by the network queueing
	 * layer when an error is returned.  This means you
	 * may not modify any SKB fields, you may not free
	 * the SKB, etc.
	 */

	/* This is the most common case for modern hardware.
	 * The spinlock protects this code from the TX complete
	 * hardware interrupt handler.  Queue flow control is
	 * thus managed under this lock as well.
	 */
	spin_lock_irqsave(&lp->lock, flags);

	/* failsafe... (handle txdone now if half of FDs are used) */
	if ((lp->tfd_start + TX_FD_NUM - lp->tfd_end) % TX_FD_NUM >
	    TX_FD_NUM / 2)
		tc35815_txdone(dev);

	if (netif_msg_pktdata(lp))
		print_eth(skb->data);
#ifdef DEBUG
	if (lp->tx_skbs[lp->tfd_start].skb) {
		printk("%s: tx_skbs conflict.\n", dev->name);
		panic_queues(dev);
	}
#else
	BUG_ON(lp->tx_skbs[lp->tfd_start].skb);
#endif
	lp->tx_skbs[lp->tfd_start].skb = skb;
	lp->tx_skbs[lp->tfd_start].skb_dma = pci_map_single(lp->pci_dev, skb->data, skb->len, PCI_DMA_TODEVICE);

	/*add to ring */
	txfd = &lp->tfd_base[lp->tfd_start];
	txfd->bd.BuffData = cpu_to_le32(lp->tx_skbs[lp->tfd_start].skb_dma);
	txfd->bd.BDCtl = cpu_to_le32(skb->len);
	txfd->fd.FDSystem = cpu_to_le32(lp->tfd_start);
	txfd->fd.FDCtl = cpu_to_le32(FD_CownsFD | (1 << FD_BDCnt_SHIFT));

	if (lp->tfd_start == lp->tfd_end) {
		struct tc35815_regs __iomem *tr =
			(struct tc35815_regs __iomem *)dev->base_addr;
		/* Start DMA Transmitter. */
		txfd->fd.FDNext |= cpu_to_le32(FD_Next_EOL);
#ifdef GATHER_TXINT
		txfd->fd.FDCtl |= cpu_to_le32(FD_FrmOpt_IntTx);
#endif
		if (netif_msg_tx_queued(lp)) {
			printk("%s: starting TxFD.\n", dev->name);
			dump_txfd(txfd);
		}
		tc_writel(fd_virt_to_bus(lp, txfd), &tr->TxFrmPtr);
	} else {
		txfd->fd.FDNext &= cpu_to_le32(~FD_Next_EOL);
		if (netif_msg_tx_queued(lp)) {
			printk("%s: queueing TxFD.\n", dev->name);
			dump_txfd(txfd);
		}
	}
	lp->tfd_start = (lp->tfd_start + 1) % TX_FD_NUM;

	dev->trans_start = jiffies;

	/* If we just used up the very last entry in the
	 * TX ring on this device, tell the queueing
	 * layer to send no more.
	 */
	if (tc35815_tx_full(dev)) {
		if (netif_msg_tx_queued(lp))
			printk(KERN_WARNING "%s: TxFD Exhausted.\n", dev->name);
		netif_stop_queue(dev);
	}

	/* When the TX completion hw interrupt arrives, this
	 * is when the transmit statistics are updated.
	 */

	spin_unlock_irqrestore(&lp->lock, flags);
	return 0;
}

#define FATAL_ERROR_INT \
	(Int_IntPCI | Int_DmParErr | Int_IntNRAbt)
static void tc35815_fatal_error_interrupt(struct net_device *dev, u32 status)
{
	static int count;
	printk(KERN_WARNING "%s: Fatal Error Intterrupt (%#x):",
	       dev->name, status);
	if (status & Int_IntPCI)
		printk(" IntPCI");
	if (status & Int_DmParErr)
		printk(" DmParErr");
	if (status & Int_IntNRAbt)
		printk(" IntNRAbt");
	printk("\n");
	if (count++ > 100)
		panic("%s: Too many fatal errors.", dev->name);
	printk(KERN_WARNING "%s: Resetting ...\n", dev->name);
	/* Try to restart the adaptor. */
	tc35815_restart(dev);
}

#ifdef TC35815_NAPI
static int tc35815_do_interrupt(struct net_device *dev, u32 status, int limit)
#else
static int tc35815_do_interrupt(struct net_device *dev, u32 status)
#endif
{
	struct tc35815_local *lp = dev->priv;
	struct tc35815_regs __iomem *tr =
		(struct tc35815_regs __iomem *)dev->base_addr;
	int ret = -1;

	/* Fatal errors... */
	if (status & FATAL_ERROR_INT) {
		tc35815_fatal_error_interrupt(dev, status);
		return 0;
	}
	/* recoverable errors */
	if (status & Int_IntFDAEx) {
		/* disable FDAEx int. (until we make rooms...) */
		tc_writel(tc_readl(&tr->Int_En) & ~Int_FDAExEn, &tr->Int_En);
		printk(KERN_WARNING
		       "%s: Free Descriptor Area Exhausted (%#x).\n",
		       dev->name, status);
		lp->stats.rx_dropped++;
		ret = 0;
	}
	if (status & Int_IntBLEx) {
		/* disable BLEx int. (until we make rooms...) */
		tc_writel(tc_readl(&tr->Int_En) & ~Int_BLExEn, &tr->Int_En);
		printk(KERN_WARNING
		       "%s: Buffer List Exhausted (%#x).\n",
		       dev->name, status);
		lp->stats.rx_dropped++;
		ret = 0;
	}
	if (status & Int_IntExBD) {
		printk(KERN_WARNING
		       "%s: Excessive Buffer Descriptiors (%#x).\n",
		       dev->name, status);
		lp->stats.rx_length_errors++;
		ret = 0;
	}

	/* normal notification */
	if (status & Int_IntMacRx) {
		/* Got a packet(s). */
#ifdef TC35815_NAPI
		ret = tc35815_rx(dev, limit);
#else
		tc35815_rx(dev);
		ret = 0;
#endif
		lp->lstats.rx_ints++;
	}
	if (status & Int_IntMacTx) {
		/* Transmit complete. */
		lp->lstats.tx_ints++;
		tc35815_txdone(dev);
		netif_wake_queue(dev);
		ret = 0;
	}
	return ret;
}

/*
 * The typical workload of the driver:
 * Handle the network interface interrupts.
 */
static irqreturn_t tc35815_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct tc35815_local *lp = netdev_priv(dev);
	struct tc35815_regs __iomem *tr =
		(struct tc35815_regs __iomem *)dev->base_addr;
#ifdef TC35815_NAPI
	u32 dmactl = tc_readl(&tr->DMA_Ctl);

	if (!(dmactl & DMA_IntMask)) {
		/* disable interrupts */
		tc_writel(dmactl | DMA_IntMask, &tr->DMA_Ctl);
		if (netif_rx_schedule_prep(dev, &lp->napi))
			__netif_rx_schedule(dev, &lp->napi);
		else {
			printk(KERN_ERR "%s: interrupt taken in poll\n",
			       dev->name);
			BUG();
		}
		(void)tc_readl(&tr->Int_Src);	/* flush */
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
#else
	struct tc35815_local *lp = dev->priv;
	int handled;
	u32 status;

	spin_lock(&lp->lock);
	status = tc_readl(&tr->Int_Src);
	tc_writel(status, &tr->Int_Src);	/* write to clear */
	handled = tc35815_do_interrupt(dev, status);
	(void)tc_readl(&tr->Int_Src);	/* flush */
	spin_unlock(&lp->lock);
	return IRQ_RETVAL(handled >= 0);
#endif /* TC35815_NAPI */
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void tc35815_poll_controller(struct net_device *dev)
{
	disable_irq(dev->irq);
	tc35815_interrupt(dev->irq, dev);
	enable_irq(dev->irq);
}
#endif

/* We have a good packet(s), get it/them out of the buffers. */
#ifdef TC35815_NAPI
static int
tc35815_rx(struct net_device *dev, int limit)
#else
static void
tc35815_rx(struct net_device *dev)
#endif
{
	struct tc35815_local *lp = dev->priv;
	unsigned int fdctl;
	int i;
	int buf_free_count = 0;
	int fd_free_count = 0;
#ifdef TC35815_NAPI
	int received = 0;
#endif

	while (!((fdctl = le32_to_cpu(lp->rfd_cur->fd.FDCtl)) & FD_CownsFD)) {
		int status = le32_to_cpu(lp->rfd_cur->fd.FDStat);
		int pkt_len = fdctl & FD_FDLength_MASK;
		int bd_count = (fdctl & FD_BDCnt_MASK) >> FD_BDCnt_SHIFT;
#ifdef DEBUG
		struct RxFD *next_rfd;
#endif
#if (RX_CTL_CMD & Rx_StripCRC) == 0
		pkt_len -= 4;
#endif

		if (netif_msg_rx_status(lp))
			dump_rxfd(lp->rfd_cur);
		if (status & Rx_Good) {
			struct sk_buff *skb;
			unsigned char *data;
			int cur_bd;
#ifdef TC35815_USE_PACKEDBUFFER
			int offset;
#endif

#ifdef TC35815_NAPI
			if (--limit < 0)
				break;
#endif
#ifdef TC35815_USE_PACKEDBUFFER
			BUG_ON(bd_count > 2);
			skb = dev_alloc_skb(pkt_len + 2); /* +2: for reserve */
			if (skb == NULL) {
				printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n",
				       dev->name);
				lp->stats.rx_dropped++;
				break;
			}
			skb_reserve(skb, 2);   /* 16 bit alignment */

			data = skb_put(skb, pkt_len);

			/* copy from receive buffer */
			cur_bd = 0;
			offset = 0;
			while (offset < pkt_len && cur_bd < bd_count) {
				int len = le32_to_cpu(lp->rfd_cur->bd[cur_bd].BDCtl) &
					BD_BuffLength_MASK;
				dma_addr_t dma = le32_to_cpu(lp->rfd_cur->bd[cur_bd].BuffData);
				void *rxbuf = rxbuf_bus_to_virt(lp, dma);
				if (offset + len > pkt_len)
					len = pkt_len - offset;
#ifdef TC35815_DMA_SYNC_ONDEMAND
				pci_dma_sync_single_for_cpu(lp->pci_dev,
							    dma, len,
							    PCI_DMA_FROMDEVICE);
#endif
				memcpy(data + offset, rxbuf, len);
#ifdef TC35815_DMA_SYNC_ONDEMAND
				pci_dma_sync_single_for_device(lp->pci_dev,
							       dma, len,
							       PCI_DMA_FROMDEVICE);
#endif
				offset += len;
				cur_bd++;
			}
#else /* TC35815_USE_PACKEDBUFFER */
			BUG_ON(bd_count > 1);
			cur_bd = (le32_to_cpu(lp->rfd_cur->bd[0].BDCtl)
				  & BD_RxBDID_MASK) >> BD_RxBDID_SHIFT;
#ifdef DEBUG
			if (cur_bd >= RX_BUF_NUM) {
				printk("%s: invalid BDID.\n", dev->name);
				panic_queues(dev);
			}
			BUG_ON(lp->rx_skbs[cur_bd].skb_dma !=
			       (le32_to_cpu(lp->rfd_cur->bd[0].BuffData) & ~3));
			if (!lp->rx_skbs[cur_bd].skb) {
				printk("%s: NULL skb.\n", dev->name);
				panic_queues(dev);
			}
#else
			BUG_ON(cur_bd >= RX_BUF_NUM);
#endif
			skb = lp->rx_skbs[cur_bd].skb;
			prefetch(skb->data);
			lp->rx_skbs[cur_bd].skb = NULL;
			lp->fbl_count--;
			pci_unmap_single(lp->pci_dev,
					 lp->rx_skbs[cur_bd].skb_dma,
					 RX_BUF_SIZE, PCI_DMA_FROMDEVICE);
			if (!HAVE_DMA_RXALIGN(lp))
				memmove(skb->data, skb->data - 2, pkt_len);
			data = skb_put(skb, pkt_len);
#endif /* TC35815_USE_PACKEDBUFFER */
			if (netif_msg_pktdata(lp))
				print_eth(data);
			skb->protocol = eth_type_trans(skb, dev);
#ifdef TC35815_NAPI
			netif_receive_skb(skb);
			received++;
#else
			netif_rx(skb);
#endif
			dev->last_rx = jiffies;
			lp->stats.rx_packets++;
			lp->stats.rx_bytes += pkt_len;
		} else {
			lp->stats.rx_errors++;
			printk(KERN_DEBUG "%s: Rx error (status %x)\n",
			       dev->name, status & Rx_Stat_Mask);
			/* WORKAROUND: LongErr and CRCErr means Overflow. */
			if ((status & Rx_LongErr) && (status & Rx_CRCErr)) {
				status &= ~(Rx_LongErr|Rx_CRCErr);
				status |= Rx_Over;
			}
			if (status & Rx_LongErr) lp->stats.rx_length_errors++;
			if (status & Rx_Over) lp->stats.rx_fifo_errors++;
			if (status & Rx_CRCErr) lp->stats.rx_crc_errors++;
			if (status & Rx_Align) lp->stats.rx_frame_errors++;
		}

		if (bd_count > 0) {
			/* put Free Buffer back to controller */
			int bdctl = le32_to_cpu(lp->rfd_cur->bd[bd_count - 1].BDCtl);
			unsigned char id =
				(bdctl & BD_RxBDID_MASK) >> BD_RxBDID_SHIFT;
#ifdef DEBUG
			if (id >= RX_BUF_NUM) {
				printk("%s: invalid BDID.\n", dev->name);
				panic_queues(dev);
			}
#else
			BUG_ON(id >= RX_BUF_NUM);
#endif
			/* free old buffers */
#ifdef TC35815_USE_PACKEDBUFFER
			while (lp->fbl_curid != id)
#else
			while (lp->fbl_count < RX_BUF_NUM)
#endif
			{
#ifdef TC35815_USE_PACKEDBUFFER
				unsigned char curid = lp->fbl_curid;
#else
				unsigned char curid =
					(id + 1 + lp->fbl_count) % RX_BUF_NUM;
#endif
				struct BDesc *bd = &lp->fbl_ptr->bd[curid];
#ifdef DEBUG
				bdctl = le32_to_cpu(bd->BDCtl);
				if (bdctl & BD_CownsBD) {
					printk("%s: Freeing invalid BD.\n",
					       dev->name);
					panic_queues(dev);
				}
#endif
				/* pass BD to controler */
#ifndef TC35815_USE_PACKEDBUFFER
				if (!lp->rx_skbs[curid].skb) {
					lp->rx_skbs[curid].skb =
						alloc_rxbuf_skb(dev,
								lp->pci_dev,
								&lp->rx_skbs[curid].skb_dma);
					if (!lp->rx_skbs[curid].skb)
						break; /* try on next reception */
					bd->BuffData = cpu_to_le32(lp->rx_skbs[curid].skb_dma);
				}
#endif /* TC35815_USE_PACKEDBUFFER */
				/* Note: BDLength was modified by chip. */
				bd->BDCtl = cpu_to_le32(BD_CownsBD |
							(curid << BD_RxBDID_SHIFT) |
							RX_BUF_SIZE);
#ifdef TC35815_USE_PACKEDBUFFER
				lp->fbl_curid = (curid + 1) % RX_BUF_NUM;
				if (netif_msg_rx_status(lp)) {
					printk("%s: Entering new FBD %d\n",
					       dev->name, lp->fbl_curid);
					dump_frfd(lp->fbl_ptr);
				}
#else
				lp->fbl_count++;
#endif
				buf_free_count++;
			}
		}

		/* put RxFD back to controller */
#ifdef DEBUG
		next_rfd = fd_bus_to_virt(lp,
					  le32_to_cpu(lp->rfd_cur->fd.FDNext));
		if (next_rfd < lp->rfd_base || next_rfd > lp->rfd_limit) {
			printk("%s: RxFD FDNext invalid.\n", dev->name);
			panic_queues(dev);
		}
#endif
		for (i = 0; i < (bd_count + 1) / 2 + 1; i++) {
			/* pass FD to controler */
#ifdef DEBUG
			lp->rfd_cur->fd.FDNext = cpu_to_le32(0xdeaddead);
#else
			lp->rfd_cur->fd.FDNext = cpu_to_le32(FD_Next_EOL);
#endif
			lp->rfd_cur->fd.FDCtl = cpu_to_le32(FD_CownsFD);
			lp->rfd_cur++;
			fd_free_count++;
		}
		if (lp->rfd_cur > lp->rfd_limit)
			lp->rfd_cur = lp->rfd_base;
#ifdef DEBUG
		if (lp->rfd_cur != next_rfd)
			printk("rfd_cur = %p, next_rfd %p\n",
			       lp->rfd_cur, next_rfd);
#endif
	}

	/* re-enable BL/FDA Exhaust interrupts. */
	if (fd_free_count) {
		struct tc35815_regs __iomem *tr =
			(struct tc35815_regs __iomem *)dev->base_addr;
		u32 en, en_old = tc_readl(&tr->Int_En);
		en = en_old | Int_FDAExEn;
		if (buf_free_count)
			en |= Int_BLExEn;
		if (en != en_old)
			tc_writel(en, &tr->Int_En);
	}
#ifdef TC35815_NAPI
	return received;
#endif
}

#ifdef TC35815_NAPI
static int tc35815_poll(struct napi_struct *napi, int budget)
{
	struct tc35815_local *lp = container_of(napi, struct tc35815_local, napi);
	struct net_device *dev = lp->dev;
	struct tc35815_regs __iomem *tr =
		(struct tc35815_regs __iomem *)dev->base_addr;
	int received = 0, handled;
	u32 status;

	spin_lock(&lp->lock);
	status = tc_readl(&tr->Int_Src);
	do {
		tc_writel(status, &tr->Int_Src);	/* write to clear */

		handled = tc35815_do_interrupt(dev, status, limit);
		if (handled >= 0) {
			received += handled;
			if (received >= budget)
				break;
		}
		status = tc_readl(&tr->Int_Src);
	} while (status);
	spin_unlock(&lp->lock);

	if (received < budget) {
		netif_rx_complete(dev, napi);
		/* enable interrupts */
		tc_writel(tc_readl(&tr->DMA_Ctl) & ~DMA_IntMask, &tr->DMA_Ctl);
	}
	return received;
}
#endif

#ifdef NO_CHECK_CARRIER
#define TX_STA_ERR	(Tx_ExColl|Tx_Under|Tx_Defer|Tx_LateColl|Tx_TxPar|Tx_SQErr)
#else
#define TX_STA_ERR	(Tx_ExColl|Tx_Under|Tx_Defer|Tx_NCarr|Tx_LateColl|Tx_TxPar|Tx_SQErr)
#endif

static void
tc35815_check_tx_stat(struct net_device *dev, int status)
{
	struct tc35815_local *lp = dev->priv;
	const char *msg = NULL;

	/* count collisions */
	if (status & Tx_ExColl)
		lp->stats.collisions += 16;
	if (status & Tx_TxColl_MASK)
		lp->stats.collisions += status & Tx_TxColl_MASK;

#ifndef NO_CHECK_CARRIER
	/* TX4939 does not have NCarr */
	if (lp->boardtype == TC35815_TX4939)
		status &= ~Tx_NCarr;
#ifdef WORKAROUND_LOSTCAR
	/* WORKAROUND: ignore LostCrS in full duplex operation */
	if ((lp->timer_state != asleep && lp->timer_state != lcheck)
	    || lp->fullduplex)
		status &= ~Tx_NCarr;
#endif
#endif

	if (!(status & TX_STA_ERR)) {
		/* no error. */
		lp->stats.tx_packets++;
		return;
	}

	lp->stats.tx_errors++;
	if (status & Tx_ExColl) {
		lp->stats.tx_aborted_errors++;
		msg = "Excessive Collision.";
	}
	if (status & Tx_Under) {
		lp->stats.tx_fifo_errors++;
		msg = "Tx FIFO Underrun.";
		if (lp->lstats.tx_underrun < TX_THRESHOLD_KEEP_LIMIT) {
			lp->lstats.tx_underrun++;
			if (lp->lstats.tx_underrun >= TX_THRESHOLD_KEEP_LIMIT) {
				struct tc35815_regs __iomem *tr =
					(struct tc35815_regs __iomem *)dev->base_addr;
				tc_writel(TX_THRESHOLD_MAX, &tr->TxThrsh);
				msg = "Tx FIFO Underrun.Change Tx threshold to max.";
			}
		}
	}
	if (status & Tx_Defer) {
		lp->stats.tx_fifo_errors++;
		msg = "Excessive Deferral.";
	}
#ifndef NO_CHECK_CARRIER
	if (status & Tx_NCarr) {
		lp->stats.tx_carrier_errors++;
		msg = "Lost Carrier Sense.";
	}
#endif
	if (status & Tx_LateColl) {
		lp->stats.tx_aborted_errors++;
		msg = "Late Collision.";
	}
	if (status & Tx_TxPar) {
		lp->stats.tx_fifo_errors++;
		msg = "Transmit Parity Error.";
	}
	if (status & Tx_SQErr) {
		lp->stats.tx_heartbeat_errors++;
		msg = "Signal Quality Error.";
	}
	if (msg && netif_msg_tx_err(lp))
		printk(KERN_WARNING "%s: %s (%#x)\n", dev->name, msg, status);
}

/* This handles TX complete events posted by the device
 * via interrupts.
 */
static void
tc35815_txdone(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	struct TxFD *txfd;
	unsigned int fdctl;

	txfd = &lp->tfd_base[lp->tfd_end];
	while (lp->tfd_start != lp->tfd_end &&
	       !((fdctl = le32_to_cpu(txfd->fd.FDCtl)) & FD_CownsFD)) {
		int status = le32_to_cpu(txfd->fd.FDStat);
		struct sk_buff *skb;
		unsigned long fdnext = le32_to_cpu(txfd->fd.FDNext);
		u32 fdsystem = le32_to_cpu(txfd->fd.FDSystem);

		if (netif_msg_tx_done(lp)) {
			printk("%s: complete TxFD.\n", dev->name);
			dump_txfd(txfd);
		}
		tc35815_check_tx_stat(dev, status);

		skb = fdsystem != 0xffffffff ?
			lp->tx_skbs[fdsystem].skb : NULL;
#ifdef DEBUG
		if (lp->tx_skbs[lp->tfd_end].skb != skb) {
			printk("%s: tx_skbs mismatch.\n", dev->name);
			panic_queues(dev);
		}
#else
		BUG_ON(lp->tx_skbs[lp->tfd_end].skb != skb);
#endif
		if (skb) {
			lp->stats.tx_bytes += skb->len;
			pci_unmap_single(lp->pci_dev, lp->tx_skbs[lp->tfd_end].skb_dma, skb->len, PCI_DMA_TODEVICE);
			lp->tx_skbs[lp->tfd_end].skb = NULL;
			lp->tx_skbs[lp->tfd_end].skb_dma = 0;
#ifdef TC35815_NAPI
			dev_kfree_skb_any(skb);
#else
			dev_kfree_skb_irq(skb);
#endif
		}
		txfd->fd.FDSystem = cpu_to_le32(0xffffffff);

		lp->tfd_end = (lp->tfd_end + 1) % TX_FD_NUM;
		txfd = &lp->tfd_base[lp->tfd_end];
#ifdef DEBUG
		if ((fdnext & ~FD_Next_EOL) != fd_virt_to_bus(lp, txfd)) {
			printk("%s: TxFD FDNext invalid.\n", dev->name);
			panic_queues(dev);
		}
#endif
		if (fdnext & FD_Next_EOL) {
			/* DMA Transmitter has been stopping... */
			if (lp->tfd_end != lp->tfd_start) {
				struct tc35815_regs __iomem *tr =
					(struct tc35815_regs __iomem *)dev->base_addr;
				int head = (lp->tfd_start + TX_FD_NUM - 1) % TX_FD_NUM;
				struct TxFD* txhead = &lp->tfd_base[head];
				int qlen = (lp->tfd_start + TX_FD_NUM
					    - lp->tfd_end) % TX_FD_NUM;

#ifdef DEBUG
				if (!(le32_to_cpu(txfd->fd.FDCtl) & FD_CownsFD)) {
					printk("%s: TxFD FDCtl invalid.\n", dev->name);
					panic_queues(dev);
				}
#endif
				/* log max queue length */
				if (lp->lstats.max_tx_qlen < qlen)
					lp->lstats.max_tx_qlen = qlen;


				/* start DMA Transmitter again */
				txhead->fd.FDNext |= cpu_to_le32(FD_Next_EOL);
#ifdef GATHER_TXINT
				txhead->fd.FDCtl |= cpu_to_le32(FD_FrmOpt_IntTx);
#endif
				if (netif_msg_tx_queued(lp)) {
					printk("%s: start TxFD on queue.\n",
					       dev->name);
					dump_txfd(txfd);
				}
				tc_writel(fd_virt_to_bus(lp, txfd), &tr->TxFrmPtr);
			}
			break;
		}
	}

	/* If we had stopped the queue due to a "tx full"
	 * condition, and space has now been made available,
	 * wake up the queue.
	 */
	if (netif_queue_stopped(dev) && ! tc35815_tx_full(dev))
		netif_wake_queue(dev);
}

/* The inverse routine to tc35815_open(). */
static int
tc35815_close(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;

	netif_stop_queue(dev);
#ifdef TC35815_NAPI
	napi_disable(&lp->napi);
#endif

	/* Flush the Tx and disable Rx here. */

	del_timer(&lp->timer);		/* Kill if running	*/
	tc35815_chip_reset(dev);
	free_irq(dev->irq, dev);

	tc35815_free_queues(dev);

	return 0;

}

/*
 * Get the current statistics.
 * This may be called with the card open or closed.
 */
static struct net_device_stats *tc35815_get_stats(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	struct tc35815_regs __iomem *tr =
		(struct tc35815_regs __iomem *)dev->base_addr;
	if (netif_running(dev)) {
		/* Update the statistics from the device registers. */
		lp->stats.rx_missed_errors = tc_readl(&tr->Miss_Cnt);
	}

	return &lp->stats;
}

static void tc35815_set_cam_entry(struct net_device *dev, int index, unsigned char *addr)
{
	struct tc35815_local *lp = dev->priv;
	struct tc35815_regs __iomem *tr =
		(struct tc35815_regs __iomem *)dev->base_addr;
	int cam_index = index * 6;
	u32 cam_data;
	u32 saved_addr;
	saved_addr = tc_readl(&tr->CAM_Adr);

	if (netif_msg_hw(lp)) {
		int i;
		printk(KERN_DEBUG "%s: CAM %d:", dev->name, index);
		for (i = 0; i < 6; i++)
			printk(" %02x", addr[i]);
		printk("\n");
	}
	if (index & 1) {
		/* read modify write */
		tc_writel(cam_index - 2, &tr->CAM_Adr);
		cam_data = tc_readl(&tr->CAM_Data) & 0xffff0000;
		cam_data |= addr[0] << 8 | addr[1];
		tc_writel(cam_data, &tr->CAM_Data);
		/* write whole word */
		tc_writel(cam_index + 2, &tr->CAM_Adr);
		cam_data = (addr[2] << 24) | (addr[3] << 16) | (addr[4] << 8) | addr[5];
		tc_writel(cam_data, &tr->CAM_Data);
	} else {
		/* write whole word */
		tc_writel(cam_index, &tr->CAM_Adr);
		cam_data = (addr[0] << 24) | (addr[1] << 16) | (addr[2] << 8) | addr[3];
		tc_writel(cam_data, &tr->CAM_Data);
		/* read modify write */
		tc_writel(cam_index + 4, &tr->CAM_Adr);
		cam_data = tc_readl(&tr->CAM_Data) & 0x0000ffff;
		cam_data |= addr[4] << 24 | (addr[5] << 16);
		tc_writel(cam_data, &tr->CAM_Data);
	}

	tc_writel(saved_addr, &tr->CAM_Adr);
}


/*
 * Set or clear the multicast filter for this adaptor.
 * num_addrs == -1	Promiscuous mode, receive all packets
 * num_addrs == 0	Normal mode, clear multicast list
 * num_addrs > 0	Multicast mode, receive normal and MC packets,
 *			and do best-effort filtering.
 */
static void
tc35815_set_multicast_list(struct net_device *dev)
{
	struct tc35815_regs __iomem *tr =
		(struct tc35815_regs __iomem *)dev->base_addr;

	if (dev->flags&IFF_PROMISC)
	{
#ifdef WORKAROUND_100HALF_PROMISC
		/* With some (all?) 100MHalf HUB, controller will hang
		 * if we enabled promiscuous mode before linkup... */
		struct tc35815_local *lp = dev->priv;
		int pid = lp->phy_addr;
		if (!(tc_mdio_read(dev, pid, MII_BMSR) & BMSR_LSTATUS))
			return;
#endif
		/* Enable promiscuous mode */
		tc_writel(CAM_CompEn | CAM_BroadAcc | CAM_GroupAcc | CAM_StationAcc, &tr->CAM_Ctl);
	}
	else if((dev->flags&IFF_ALLMULTI) || dev->mc_count > CAM_ENTRY_MAX - 3)
	{
		/* CAM 0, 1, 20 are reserved. */
		/* Disable promiscuous mode, use normal mode. */
		tc_writel(CAM_CompEn | CAM_BroadAcc | CAM_GroupAcc, &tr->CAM_Ctl);
	}
	else if(dev->mc_count)
	{
		struct dev_mc_list* cur_addr = dev->mc_list;
		int i;
		int ena_bits = CAM_Ena_Bit(CAM_ENTRY_SOURCE);

		tc_writel(0, &tr->CAM_Ctl);
		/* Walk the address list, and load the filter */
		for (i = 0; i < dev->mc_count; i++, cur_addr = cur_addr->next) {
			if (!cur_addr)
				break;
			/* entry 0,1 is reserved. */
			tc35815_set_cam_entry(dev, i + 2, cur_addr->dmi_addr);
			ena_bits |= CAM_Ena_Bit(i + 2);
		}
		tc_writel(ena_bits, &tr->CAM_Ena);
		tc_writel(CAM_CompEn | CAM_BroadAcc, &tr->CAM_Ctl);
	}
	else {
		tc_writel(CAM_Ena_Bit(CAM_ENTRY_SOURCE), &tr->CAM_Ena);
		tc_writel(CAM_CompEn | CAM_BroadAcc, &tr->CAM_Ctl);
	}
}

static void tc35815_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct tc35815_local *lp = dev->priv;
	strcpy(info->driver, MODNAME);
	strcpy(info->version, DRV_VERSION);
	strcpy(info->bus_info, pci_name(lp->pci_dev));
}

static int tc35815_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct tc35815_local *lp = dev->priv;
	spin_lock_irq(&lp->lock);
	mii_ethtool_gset(&lp->mii, cmd);
	spin_unlock_irq(&lp->lock);
	return 0;
}

static int tc35815_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct tc35815_local *lp = dev->priv;
	int rc;
#if 1	/* use our negotiation method... */
	/* Verify the settings we care about. */
	if (cmd->autoneg != AUTONEG_ENABLE &&
	    cmd->autoneg != AUTONEG_DISABLE)
		return -EINVAL;
	if (cmd->autoneg == AUTONEG_DISABLE &&
	    ((cmd->speed != SPEED_100 &&
	      cmd->speed != SPEED_10) ||
	     (cmd->duplex != DUPLEX_HALF &&
	      cmd->duplex != DUPLEX_FULL)))
		return -EINVAL;

	/* Ok, do it to it. */
	spin_lock_irq(&lp->lock);
	del_timer(&lp->timer);
	tc35815_start_auto_negotiation(dev, cmd);
	spin_unlock_irq(&lp->lock);
	rc = 0;
#else
	spin_lock_irq(&lp->lock);
	rc = mii_ethtool_sset(&lp->mii, cmd);
	spin_unlock_irq(&lp->lock);
#endif
	return rc;
}

static int tc35815_nway_reset(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	int rc;
	spin_lock_irq(&lp->lock);
	rc = mii_nway_restart(&lp->mii);
	spin_unlock_irq(&lp->lock);
	return rc;
}

static u32 tc35815_get_link(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	int rc;
	spin_lock_irq(&lp->lock);
	rc = mii_link_ok(&lp->mii);
	spin_unlock_irq(&lp->lock);
	return rc;
}

static u32 tc35815_get_msglevel(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	return lp->msg_enable;
}

static void tc35815_set_msglevel(struct net_device *dev, u32 datum)
{
	struct tc35815_local *lp = dev->priv;
	lp->msg_enable = datum;
}

static int tc35815_get_sset_count(struct net_device *dev, int sset)
{
	struct tc35815_local *lp = dev->priv;

	switch (sset) {
	case ETH_SS_STATS:
		return sizeof(lp->lstats) / sizeof(int);
	default:
		return -EOPNOTSUPP;
	}
}

static void tc35815_get_ethtool_stats(struct net_device *dev, struct ethtool_stats *stats, u64 *data)
{
	struct tc35815_local *lp = dev->priv;
	data[0] = lp->lstats.max_tx_qlen;
	data[1] = lp->lstats.tx_ints;
	data[2] = lp->lstats.rx_ints;
	data[3] = lp->lstats.tx_underrun;
}

static struct {
	const char str[ETH_GSTRING_LEN];
} ethtool_stats_keys[] = {
	{ "max_tx_qlen" },
	{ "tx_ints" },
	{ "rx_ints" },
	{ "tx_underrun" },
};

static void tc35815_get_strings(struct net_device *dev, u32 stringset, u8 *data)
{
	memcpy(data, ethtool_stats_keys, sizeof(ethtool_stats_keys));
}

static const struct ethtool_ops tc35815_ethtool_ops = {
	.get_drvinfo		= tc35815_get_drvinfo,
	.get_settings		= tc35815_get_settings,
	.set_settings		= tc35815_set_settings,
	.nway_reset		= tc35815_nway_reset,
	.get_link		= tc35815_get_link,
	.get_msglevel		= tc35815_get_msglevel,
	.set_msglevel		= tc35815_set_msglevel,
	.get_strings		= tc35815_get_strings,
	.get_sset_count		= tc35815_get_sset_count,
	.get_ethtool_stats	= tc35815_get_ethtool_stats,
};

static int tc35815_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct tc35815_local *lp = dev->priv;
	int rc;

	if (!netif_running(dev))
		return -EINVAL;

	spin_lock_irq(&lp->lock);
	rc = generic_mii_ioctl(&lp->mii, if_mii(rq), cmd, NULL);
	spin_unlock_irq(&lp->lock);

	return rc;
}

static int tc_mdio_read(struct net_device *dev, int phy_id, int location)
{
	struct tc35815_regs __iomem *tr =
		(struct tc35815_regs __iomem *)dev->base_addr;
	u32 data;
	tc_writel(MD_CA_Busy | (phy_id << 5) | location, &tr->MD_CA);
	while (tc_readl(&tr->MD_CA) & MD_CA_Busy)
		;
	data = tc_readl(&tr->MD_Data);
	return data & 0xffff;
}

static void tc_mdio_write(struct net_device *dev, int phy_id, int location,
			  int val)
{
	struct tc35815_regs __iomem *tr =
		(struct tc35815_regs __iomem *)dev->base_addr;
	tc_writel(val, &tr->MD_Data);
	tc_writel(MD_CA_Busy | MD_CA_Wr | (phy_id << 5) | location, &tr->MD_CA);
	while (tc_readl(&tr->MD_CA) & MD_CA_Busy)
		;
}

/* Auto negotiation.  The scheme is very simple.  We have a timer routine
 * that keeps watching the auto negotiation process as it progresses.
 * The DP83840 is first told to start doing it's thing, we set up the time
 * and place the timer state machine in it's initial state.
 *
 * Here the timer peeks at the DP83840 status registers at each click to see
 * if the auto negotiation has completed, we assume here that the DP83840 PHY
 * will time out at some point and just tell us what (didn't) happen.  For
 * complete coverage we only allow so many of the ticks at this level to run,
 * when this has expired we print a warning message and try another strategy.
 * This "other" strategy is to force the interface into various speed/duplex
 * configurations and we stop when we see a link-up condition before the
 * maximum number of "peek" ticks have occurred.
 *
 * Once a valid link status has been detected we configure the BigMAC and
 * the rest of the Happy Meal to speak the most efficient protocol we could
 * get a clean link for.  The priority for link configurations, highest first
 * is:
 *                 100 Base-T Full Duplex
 *                 100 Base-T Half Duplex
 *                 10 Base-T Full Duplex
 *                 10 Base-T Half Duplex
 *
 * We start a new timer now, after a successful auto negotiation status has
 * been detected.  This timer just waits for the link-up bit to get set in
 * the BMCR of the DP83840.  When this occurs we print a kernel log message
 * describing the link type in use and the fact that it is up.
 *
 * If a fatal error of some sort is signalled and detected in the interrupt
 * service routine, and the chip is reset, or the link is ifconfig'd down
 * and then back up, this entire process repeats itself all over again.
 */
/* Note: Above comments are come from sunhme driver. */

static int tc35815_try_next_permutation(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	int pid = lp->phy_addr;
	unsigned short bmcr;

	bmcr = tc_mdio_read(dev, pid, MII_BMCR);

	/* Downgrade from full to half duplex.  Only possible via ethtool.  */
	if (bmcr & BMCR_FULLDPLX) {
		bmcr &= ~BMCR_FULLDPLX;
		printk(KERN_DEBUG "%s: try next permutation (BMCR %x)\n", dev->name, bmcr);
		tc_mdio_write(dev, pid, MII_BMCR, bmcr);
		return 0;
	}

	/* Downgrade from 100 to 10. */
	if (bmcr & BMCR_SPEED100) {
		bmcr &= ~BMCR_SPEED100;
		printk(KERN_DEBUG "%s: try next permutation (BMCR %x)\n", dev->name, bmcr);
		tc_mdio_write(dev, pid, MII_BMCR, bmcr);
		return 0;
	}

	/* We've tried everything. */
	return -1;
}

static void
tc35815_display_link_mode(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	int pid = lp->phy_addr;
	unsigned short lpa, bmcr;
	char *speed = "", *duplex = "";

	lpa = tc_mdio_read(dev, pid, MII_LPA);
	bmcr = tc_mdio_read(dev, pid, MII_BMCR);
	if (options.speed ? (bmcr & BMCR_SPEED100) : (lpa & (LPA_100HALF | LPA_100FULL)))
		speed = "100Mb/s";
	else
		speed = "10Mb/s";
	if (options.duplex ? (bmcr & BMCR_FULLDPLX) : (lpa & (LPA_100FULL | LPA_10FULL)))
		duplex = "Full Duplex";
	else
		duplex = "Half Duplex";

	if (netif_msg_link(lp))
		printk(KERN_INFO "%s: Link is up at %s, %s.\n",
		       dev->name, speed, duplex);
	printk(KERN_DEBUG "%s: MII BMCR %04x BMSR %04x LPA %04x\n",
	       dev->name,
	       bmcr, tc_mdio_read(dev, pid, MII_BMSR), lpa);
}

static void tc35815_display_forced_link_mode(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	int pid = lp->phy_addr;
	unsigned short bmcr;
	char *speed = "", *duplex = "";

	bmcr = tc_mdio_read(dev, pid, MII_BMCR);
	if (bmcr & BMCR_SPEED100)
		speed = "100Mb/s";
	else
		speed = "10Mb/s";
	if (bmcr & BMCR_FULLDPLX)
		duplex = "Full Duplex.\n";
	else
		duplex = "Half Duplex.\n";

	if (netif_msg_link(lp))
		printk(KERN_INFO "%s: Link has been forced up at %s, %s",
		       dev->name, speed, duplex);
}

static void tc35815_set_link_modes(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	struct tc35815_regs __iomem *tr =
		(struct tc35815_regs __iomem *)dev->base_addr;
	int pid = lp->phy_addr;
	unsigned short bmcr, lpa;
	int speed;

	if (lp->timer_state == arbwait) {
		lpa = tc_mdio_read(dev, pid, MII_LPA);
		bmcr = tc_mdio_read(dev, pid, MII_BMCR);
		printk(KERN_DEBUG "%s: MII BMCR %04x BMSR %04x LPA %04x\n",
		       dev->name,
		       bmcr, tc_mdio_read(dev, pid, MII_BMSR), lpa);
		if (!(lpa & (LPA_10HALF | LPA_10FULL |
			     LPA_100HALF | LPA_100FULL))) {
			/* fall back to 10HALF */
			printk(KERN_INFO "%s: bad ability %04x - falling back to 10HD.\n",
			       dev->name, lpa);
			lpa = LPA_10HALF;
		}
		if (options.duplex ? (bmcr & BMCR_FULLDPLX) : (lpa & (LPA_100FULL | LPA_10FULL)))
			lp->fullduplex = 1;
		else
			lp->fullduplex = 0;
		if (options.speed ? (bmcr & BMCR_SPEED100) : (lpa & (LPA_100HALF | LPA_100FULL)))
			speed = 100;
		else
			speed = 10;
	} else {
		/* Forcing a link mode. */
		bmcr = tc_mdio_read(dev, pid, MII_BMCR);
		if (bmcr & BMCR_FULLDPLX)
			lp->fullduplex = 1;
		else
			lp->fullduplex = 0;
		if (bmcr & BMCR_SPEED100)
			speed = 100;
		else
			speed = 10;
	}

	tc_writel(tc_readl(&tr->MAC_Ctl) | MAC_HaltReq, &tr->MAC_Ctl);
	if (lp->fullduplex) {
		tc_writel(tc_readl(&tr->MAC_Ctl) | MAC_FullDup, &tr->MAC_Ctl);
	} else {
		tc_writel(tc_readl(&tr->MAC_Ctl) & ~MAC_FullDup, &tr->MAC_Ctl);
	}
	tc_writel(tc_readl(&tr->MAC_Ctl) & ~MAC_HaltReq, &tr->MAC_Ctl);

	/* TX4939 PCFG.SPEEDn bit will be changed on NETDEV_CHANGE event. */

#ifndef NO_CHECK_CARRIER
	/* TX4939 does not have EnLCarr */
	if (lp->boardtype != TC35815_TX4939) {
#ifdef WORKAROUND_LOSTCAR
		/* WORKAROUND: enable LostCrS only if half duplex operation */
		if (!lp->fullduplex && lp->boardtype != TC35815_TX4939)
			tc_writel(tc_readl(&tr->Tx_Ctl) | Tx_EnLCarr, &tr->Tx_Ctl);
#endif
	}
#endif
	lp->mii.full_duplex = lp->fullduplex;
}

static void tc35815_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct tc35815_local *lp = dev->priv;
	int pid = lp->phy_addr;
	unsigned short bmsr, bmcr, lpa;
	int restart_timer = 0;

	spin_lock_irq(&lp->lock);

	lp->timer_ticks++;
	switch (lp->timer_state) {
	case arbwait:
		/*
		 * Only allow for 5 ticks, thats 10 seconds and much too
		 * long to wait for arbitration to complete.
		 */
		/* TC35815 need more times... */
		if (lp->timer_ticks >= 10) {
			/* Enter force mode. */
			if (!options.doforce) {
				printk(KERN_NOTICE "%s: Auto-Negotiation unsuccessful,"
				       " cable probblem?\n", dev->name);
				/* Try to restart the adaptor. */
				tc35815_restart(dev);
				goto out;
			}
			printk(KERN_NOTICE "%s: Auto-Negotiation unsuccessful,"
			       " trying force link mode\n", dev->name);
			printk(KERN_DEBUG "%s: BMCR %x BMSR %x\n", dev->name,
			       tc_mdio_read(dev, pid, MII_BMCR),
			       tc_mdio_read(dev, pid, MII_BMSR));
			bmcr = BMCR_SPEED100;
			tc_mdio_write(dev, pid, MII_BMCR, bmcr);

			/*
			 * OK, seems we need do disable the transceiver
			 * for the first tick to make sure we get an
			 * accurate link state at the second tick.
			 */

			lp->timer_state = ltrywait;
			lp->timer_ticks = 0;
			restart_timer = 1;
		} else {
			/* Anything interesting happen? */
			bmsr = tc_mdio_read(dev, pid, MII_BMSR);
			if (bmsr & BMSR_ANEGCOMPLETE) {
				/* Just what we've been waiting for... */
				tc35815_set_link_modes(dev);

				/*
				 * Success, at least so far, advance our state
				 * engine.
				 */
				lp->timer_state = lupwait;
				restart_timer = 1;
			} else {
				restart_timer = 1;
			}
		}
		break;

	case lupwait:
		/*
		 * Auto negotiation was successful and we are awaiting a
		 * link up status.  I have decided to let this timer run
		 * forever until some sort of error is signalled, reporting
		 * a message to the user at 10 second intervals.
		 */
		bmsr = tc_mdio_read(dev, pid, MII_BMSR);
		if (bmsr & BMSR_LSTATUS) {
			/*
			 * Wheee, it's up, display the link mode in use and put
			 * the timer to sleep.
			 */
			tc35815_display_link_mode(dev);
			netif_carrier_on(dev);
#ifdef WORKAROUND_100HALF_PROMISC
			/* delayed promiscuous enabling */
			if (dev->flags & IFF_PROMISC)
				tc35815_set_multicast_list(dev);
#endif
#if 1
			lp->saved_lpa = tc_mdio_read(dev, pid, MII_LPA);
			lp->timer_state = lcheck;
			restart_timer = 1;
#else
			lp->timer_state = asleep;
			restart_timer = 0;
#endif
		} else {
			if (lp->timer_ticks >= 10) {
				printk(KERN_NOTICE "%s: Auto negotiation successful, link still "
				       "not completely up.\n", dev->name);
				lp->timer_ticks = 0;
				restart_timer = 1;
			} else {
				restart_timer = 1;
			}
		}
		break;

	case ltrywait:
		/*
		 * Making the timeout here too long can make it take
		 * annoyingly long to attempt all of the link mode
		 * permutations, but then again this is essentially
		 * error recovery code for the most part.
		 */
		bmsr = tc_mdio_read(dev, pid, MII_BMSR);
		bmcr = tc_mdio_read(dev, pid, MII_BMCR);
		if (lp->timer_ticks == 1) {
			/*
			 * Re-enable transceiver, we'll re-enable the
			 * transceiver next tick, then check link state
			 * on the following tick.
			 */
			restart_timer = 1;
			break;
		}
		if (lp->timer_ticks == 2) {
			restart_timer = 1;
			break;
		}
		if (bmsr & BMSR_LSTATUS) {
			/* Force mode selection success. */
			tc35815_display_forced_link_mode(dev);
			netif_carrier_on(dev);
			tc35815_set_link_modes(dev);
#ifdef WORKAROUND_100HALF_PROMISC
			/* delayed promiscuous enabling */
			if (dev->flags & IFF_PROMISC)
				tc35815_set_multicast_list(dev);
#endif
#if 1
			lp->saved_lpa = tc_mdio_read(dev, pid, MII_LPA);
			lp->timer_state = lcheck;
			restart_timer = 1;
#else
			lp->timer_state = asleep;
			restart_timer = 0;
#endif
		} else {
			if (lp->timer_ticks >= 4) { /* 6 seconds or so... */
				int ret;

				ret = tc35815_try_next_permutation(dev);
				if (ret == -1) {
					/*
					 * Aieee, tried them all, reset the
					 * chip and try all over again.
					 */
					printk(KERN_NOTICE "%s: Link down, "
					       "cable problem?\n",
					       dev->name);

					/* Try to restart the adaptor. */
					tc35815_restart(dev);
					goto out;
				}
				lp->timer_ticks = 0;
				restart_timer = 1;
			} else {
				restart_timer = 1;
			}
		}
		break;

	case lcheck:
		bmcr = tc_mdio_read(dev, pid, MII_BMCR);
		lpa = tc_mdio_read(dev, pid, MII_LPA);
		if (bmcr & (BMCR_PDOWN | BMCR_ISOLATE | BMCR_RESET)) {
			printk(KERN_ERR "%s: PHY down? (BMCR %x)\n", dev->name,
			       bmcr);
		} else if ((lp->saved_lpa ^ lpa) &
			   (LPA_100FULL|LPA_100HALF|LPA_10FULL|LPA_10HALF)) {
			printk(KERN_NOTICE "%s: link status changed"
			       " (BMCR %x LPA %x->%x)\n", dev->name,
			       bmcr, lp->saved_lpa, lpa);
		} else {
			/* go on */
			restart_timer = 1;
			break;
		}
		/* Try to restart the adaptor. */
		tc35815_restart(dev);
		goto out;

	case asleep:
	default:
		/* Can't happens.... */
		printk(KERN_ERR "%s: Aieee, link timer is asleep but we got "
		       "one anyways!\n", dev->name);
		restart_timer = 0;
		lp->timer_ticks = 0;
		lp->timer_state = asleep; /* foo on you */
		break;
	}

	if (restart_timer) {
		lp->timer.expires = jiffies + msecs_to_jiffies(1200);
		add_timer(&lp->timer);
	}
out:
	spin_unlock_irq(&lp->lock);
}

static void tc35815_start_auto_negotiation(struct net_device *dev,
					   struct ethtool_cmd *ep)
{
	struct tc35815_local *lp = dev->priv;
	int pid = lp->phy_addr;
	unsigned short bmsr, bmcr, advertize;
	int timeout;

	netif_carrier_off(dev);
	bmsr = tc_mdio_read(dev, pid, MII_BMSR);
	bmcr = tc_mdio_read(dev, pid, MII_BMCR);
	advertize = tc_mdio_read(dev, pid, MII_ADVERTISE);

	if (ep == NULL || ep->autoneg == AUTONEG_ENABLE) {
		if (options.speed || options.duplex) {
			/* Advertise only specified configuration. */
			advertize &= ~(ADVERTISE_10HALF |
				       ADVERTISE_10FULL |
				       ADVERTISE_100HALF |
				       ADVERTISE_100FULL);
			if (options.speed != 10) {
				if (options.duplex != 1)
					advertize |= ADVERTISE_100FULL;
				if (options.duplex != 2)
					advertize |= ADVERTISE_100HALF;
			}
			if (options.speed != 100) {
				if (options.duplex != 1)
					advertize |= ADVERTISE_10FULL;
				if (options.duplex != 2)
					advertize |= ADVERTISE_10HALF;
			}
			if (options.speed == 100)
				bmcr |= BMCR_SPEED100;
			else if (options.speed == 10)
				bmcr &= ~BMCR_SPEED100;
			if (options.duplex == 2)
				bmcr |= BMCR_FULLDPLX;
			else if (options.duplex == 1)
				bmcr &= ~BMCR_FULLDPLX;
		} else {
			/* Advertise everything we can support. */
			if (bmsr & BMSR_10HALF)
				advertize |= ADVERTISE_10HALF;
			else
				advertize &= ~ADVERTISE_10HALF;
			if (bmsr & BMSR_10FULL)
				advertize |= ADVERTISE_10FULL;
			else
				advertize &= ~ADVERTISE_10FULL;
			if (bmsr & BMSR_100HALF)
				advertize |= ADVERTISE_100HALF;
			else
				advertize &= ~ADVERTISE_100HALF;
			if (bmsr & BMSR_100FULL)
				advertize |= ADVERTISE_100FULL;
			else
				advertize &= ~ADVERTISE_100FULL;
		}

		tc_mdio_write(dev, pid, MII_ADVERTISE, advertize);

		/* Enable Auto-Negotiation, this is usually on already... */
		bmcr |= BMCR_ANENABLE;
		tc_mdio_write(dev, pid, MII_BMCR, bmcr);

		/* Restart it to make sure it is going. */
		bmcr |= BMCR_ANRESTART;
		tc_mdio_write(dev, pid, MII_BMCR, bmcr);
		printk(KERN_DEBUG "%s: ADVERTISE %x BMCR %x\n", dev->name, advertize, bmcr);

		/* BMCR_ANRESTART self clears when the process has begun. */
		timeout = 64;  /* More than enough. */
		while (--timeout) {
			bmcr = tc_mdio_read(dev, pid, MII_BMCR);
			if (!(bmcr & BMCR_ANRESTART))
				break; /* got it. */
			udelay(10);
		}
		if (!timeout) {
			printk(KERN_ERR "%s: TC35815 would not start auto "
			       "negotiation BMCR=0x%04x\n",
			       dev->name, bmcr);
			printk(KERN_NOTICE "%s: Performing force link "
			       "detection.\n", dev->name);
			goto force_link;
		} else {
			printk(KERN_DEBUG "%s: auto negotiation started.\n", dev->name);
			lp->timer_state = arbwait;
		}
	} else {
force_link:
		/* Force the link up, trying first a particular mode.
		 * Either we are here at the request of ethtool or
		 * because the Happy Meal would not start to autoneg.
		 */

		/* Disable auto-negotiation in BMCR, enable the duplex and
		 * speed setting, init the timer state machine, and fire it off.
		 */
		if (ep == NULL || ep->autoneg == AUTONEG_ENABLE) {
			bmcr = BMCR_SPEED100;
		} else {
			if (ep->speed == SPEED_100)
				bmcr = BMCR_SPEED100;
			else
				bmcr = 0;
			if (ep->duplex == DUPLEX_FULL)
				bmcr |= BMCR_FULLDPLX;
		}
		tc_mdio_write(dev, pid, MII_BMCR, bmcr);

		/* OK, seems we need do disable the transceiver for the first
		 * tick to make sure we get an accurate link state at the
		 * second tick.
		 */
		lp->timer_state = ltrywait;
	}

	del_timer(&lp->timer);
	lp->timer_ticks = 0;
	lp->timer.expires = jiffies + msecs_to_jiffies(1200);
	add_timer(&lp->timer);
}

static void tc35815_find_phy(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	int pid = lp->phy_addr;
	unsigned short id0;

	/* find MII phy */
	for (pid = 31; pid >= 0; pid--) {
		id0 = tc_mdio_read(dev, pid, MII_BMSR);
		if (id0 != 0xffff && id0 != 0x0000 &&
		    (id0 & BMSR_RESV) != (0xffff & BMSR_RESV) /* paranoia? */
			) {
			lp->phy_addr = pid;
			break;
		}
	}
	if (pid < 0) {
		printk(KERN_ERR "%s: No MII Phy found.\n",
		       dev->name);
		lp->phy_addr = pid = 0;
	}

	lp->mii_id[0] = tc_mdio_read(dev, pid, MII_PHYSID1);
	lp->mii_id[1] = tc_mdio_read(dev, pid, MII_PHYSID2);
	if (netif_msg_hw(lp))
		printk(KERN_INFO "%s: PHY(%02x) ID %04x %04x\n", dev->name,
		       pid, lp->mii_id[0], lp->mii_id[1]);
}

static void tc35815_phy_chip_init(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	int pid = lp->phy_addr;
	unsigned short bmcr;
	struct ethtool_cmd ecmd, *ep;

	/* dis-isolate if needed. */
	bmcr = tc_mdio_read(dev, pid, MII_BMCR);
	if (bmcr & BMCR_ISOLATE) {
		int count = 32;
		printk(KERN_DEBUG "%s: unisolating...", dev->name);
		tc_mdio_write(dev, pid, MII_BMCR, bmcr & ~BMCR_ISOLATE);
		while (--count) {
			if (!(tc_mdio_read(dev, pid, MII_BMCR) & BMCR_ISOLATE))
				break;
			udelay(20);
		}
		printk(" %s.\n", count ? "done" : "failed");
	}

	if (options.speed && options.duplex) {
		ecmd.autoneg = AUTONEG_DISABLE;
		ecmd.speed = options.speed == 10 ? SPEED_10 : SPEED_100;
		ecmd.duplex = options.duplex == 1 ? DUPLEX_HALF : DUPLEX_FULL;
		ep = &ecmd;
	} else {
		ep = NULL;
	}
	tc35815_start_auto_negotiation(dev, ep);
}

static void tc35815_chip_reset(struct net_device *dev)
{
	struct tc35815_regs __iomem *tr =
		(struct tc35815_regs __iomem *)dev->base_addr;
	int i;
	/* reset the controller */
	tc_writel(MAC_Reset, &tr->MAC_Ctl);
	udelay(4); /* 3200ns */
	i = 0;
	while (tc_readl(&tr->MAC_Ctl) & MAC_Reset) {
		if (i++ > 100) {
			printk(KERN_ERR "%s: MAC reset failed.\n", dev->name);
			break;
		}
		mdelay(1);
	}
	tc_writel(0, &tr->MAC_Ctl);

	/* initialize registers to default value */
	tc_writel(0, &tr->DMA_Ctl);
	tc_writel(0, &tr->TxThrsh);
	tc_writel(0, &tr->TxPollCtr);
	tc_writel(0, &tr->RxFragSize);
	tc_writel(0, &tr->Int_En);
	tc_writel(0, &tr->FDA_Bas);
	tc_writel(0, &tr->FDA_Lim);
	tc_writel(0xffffffff, &tr->Int_Src);	/* Write 1 to clear */
	tc_writel(0, &tr->CAM_Ctl);
	tc_writel(0, &tr->Tx_Ctl);
	tc_writel(0, &tr->Rx_Ctl);
	tc_writel(0, &tr->CAM_Ena);
	(void)tc_readl(&tr->Miss_Cnt);	/* Read to clear */

	/* initialize internal SRAM */
	tc_writel(DMA_TestMode, &tr->DMA_Ctl);
	for (i = 0; i < 0x1000; i += 4) {
		tc_writel(i, &tr->CAM_Adr);
		tc_writel(0, &tr->CAM_Data);
	}
	tc_writel(0, &tr->DMA_Ctl);
}

static void tc35815_chip_init(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	struct tc35815_regs __iomem *tr =
		(struct tc35815_regs __iomem *)dev->base_addr;
	unsigned long txctl = TX_CTL_CMD;

	tc35815_phy_chip_init(dev);

	/* load station address to CAM */
	tc35815_set_cam_entry(dev, CAM_ENTRY_SOURCE, dev->dev_addr);

	/* Enable CAM (broadcast and unicast) */
	tc_writel(CAM_Ena_Bit(CAM_ENTRY_SOURCE), &tr->CAM_Ena);
	tc_writel(CAM_CompEn | CAM_BroadAcc, &tr->CAM_Ctl);

	/* Use DMA_RxAlign_2 to make IP header 4-byte aligned. */
	if (HAVE_DMA_RXALIGN(lp))
		tc_writel(DMA_BURST_SIZE | DMA_RxAlign_2, &tr->DMA_Ctl);
	else
		tc_writel(DMA_BURST_SIZE, &tr->DMA_Ctl);
#ifdef TC35815_USE_PACKEDBUFFER
	tc_writel(RxFrag_EnPack | ETH_ZLEN, &tr->RxFragSize);	/* Packing */
#else
	tc_writel(ETH_ZLEN, &tr->RxFragSize);
#endif
	tc_writel(0, &tr->TxPollCtr);	/* Batch mode */
	tc_writel(TX_THRESHOLD, &tr->TxThrsh);
	tc_writel(INT_EN_CMD, &tr->Int_En);

	/* set queues */
	tc_writel(fd_virt_to_bus(lp, lp->rfd_base), &tr->FDA_Bas);
	tc_writel((unsigned long)lp->rfd_limit - (unsigned long)lp->rfd_base,
		  &tr->FDA_Lim);
	/*
	 * Activation method:
	 * First, enable the MAC Transmitter and the DMA Receive circuits.
	 * Then enable the DMA Transmitter and the MAC Receive circuits.
	 */
	tc_writel(fd_virt_to_bus(lp, lp->fbl_ptr), &tr->BLFrmPtr);	/* start DMA receiver */
	tc_writel(RX_CTL_CMD, &tr->Rx_Ctl);	/* start MAC receiver */

	/* start MAC transmitter */
#ifndef NO_CHECK_CARRIER
	/* TX4939 does not have EnLCarr */
	if (lp->boardtype == TC35815_TX4939)
		txctl &= ~Tx_EnLCarr;
#ifdef WORKAROUND_LOSTCAR
	/* WORKAROUND: ignore LostCrS in full duplex operation */
	if ((lp->timer_state != asleep && lp->timer_state != lcheck) ||
	    lp->fullduplex)
		txctl &= ~Tx_EnLCarr;
#endif
#endif /* !NO_CHECK_CARRIER */
#ifdef GATHER_TXINT
	txctl &= ~Tx_EnComp;	/* disable global tx completion int. */
#endif
	tc_writel(txctl, &tr->Tx_Ctl);
}

#ifdef CONFIG_PM
static int tc35815_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct tc35815_local *lp = dev->priv;
	unsigned long flags;

	pci_save_state(pdev);
	if (!netif_running(dev))
		return 0;
	netif_device_detach(dev);
	spin_lock_irqsave(&lp->lock, flags);
	del_timer(&lp->timer);		/* Kill if running	*/
	tc35815_chip_reset(dev);
	spin_unlock_irqrestore(&lp->lock, flags);
	pci_set_power_state(pdev, PCI_D3hot);
	return 0;
}

static int tc35815_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct tc35815_local *lp = dev->priv;
	unsigned long flags;

	pci_restore_state(pdev);
	if (!netif_running(dev))
		return 0;
	pci_set_power_state(pdev, PCI_D0);
	spin_lock_irqsave(&lp->lock, flags);
	tc35815_restart(dev);
	spin_unlock_irqrestore(&lp->lock, flags);
	netif_device_attach(dev);
	return 0;
}
#endif /* CONFIG_PM */

static struct pci_driver tc35815_pci_driver = {
	.name		= MODNAME,
	.id_table	= tc35815_pci_tbl,
	.probe		= tc35815_init_one,
	.remove		= __devexit_p(tc35815_remove_one),
#ifdef CONFIG_PM
	.suspend	= tc35815_suspend,
	.resume		= tc35815_resume,
#endif
};

module_param_named(speed, options.speed, int, 0);
MODULE_PARM_DESC(speed, "0:auto, 10:10Mbps, 100:100Mbps");
module_param_named(duplex, options.duplex, int, 0);
MODULE_PARM_DESC(duplex, "0:auto, 1:half, 2:full");
module_param_named(doforce, options.doforce, int, 0);
MODULE_PARM_DESC(doforce, "try force link mode if auto-negotiation failed");

static int __init tc35815_init_module(void)
{
	return pci_register_driver(&tc35815_pci_driver);
}

static void __exit tc35815_cleanup_module(void)
{
	pci_unregister_driver(&tc35815_pci_driver);
}

module_init(tc35815_init_module);
module_exit(tc35815_cleanup_module);

MODULE_DESCRIPTION("TOSHIBA TC35815 PCI 10M/100M Ethernet driver");
MODULE_LICENSE("GPL");
