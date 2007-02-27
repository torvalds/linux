/* tc35815.c: A TOSHIBA TC35815CF PCI 10/100Mbps ethernet driver for linux.
 *
 * Copyright 2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *                ahennessy@mvista.com
 *
 * Based on skelton.c by Donald Becker.
 * Copyright (C) 2000-2001 Toshiba Corporation
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 * WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 * USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

static const char *version =
	"tc35815.c:v0.00 26/07/2000 by Toshiba Corporation\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/byteorder.h>

/*
 * The name of the card. Is used for messages and in the requests for
 * io regions, irqs and dma channels
 */
static const char* cardname = "TC35815CF";
#define TC35815_PROC_ENTRY "net/tc35815"

#define TC35815_MODULE_NAME "TC35815CF"
#define TX_TIMEOUT (4*HZ)

/* First, a few definitions that the brave might change. */

/* use 0 for production, 1 for verification, >2 for debug */
#ifndef TC35815_DEBUG
#define TC35815_DEBUG 1
#endif
static unsigned int tc35815_debug = TC35815_DEBUG;

#define GATHER_TXINT	/* On-Demand Tx Interrupt */

#define vtonocache(p)	KSEG1ADDR(virt_to_phys(p))

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


/* MII register offsets */
#define MII_CONTROL             0x0000
#define MII_STATUS              0x0001
#define MII_PHY_ID0             0x0002
#define MII_PHY_ID1             0x0003
#define MII_ANAR                0x0004
#define MII_ANLPAR              0x0005
#define MII_ANER                0x0006
/* MII Control register bit definitions. */
#define MIICNTL_FDX             0x0100
#define MIICNTL_RST_AUTO        0x0200
#define MIICNTL_ISOLATE         0x0400
#define MIICNTL_PWRDWN          0x0800
#define MIICNTL_AUTO            0x1000
#define MIICNTL_SPEED           0x2000
#define MIICNTL_LPBK            0x4000
#define MIICNTL_RESET           0x8000
/* MII Status register bit significance. */
#define MIISTAT_EXT             0x0001
#define MIISTAT_JAB             0x0002
#define MIISTAT_LINK            0x0004
#define MIISTAT_CAN_AUTO        0x0008
#define MIISTAT_FAULT           0x0010
#define MIISTAT_AUTO_DONE       0x0020
#define MIISTAT_CAN_T           0x0800
#define MIISTAT_CAN_T_FDX       0x1000
#define MIISTAT_CAN_TX          0x2000
#define MIISTAT_CAN_TX_FDX      0x4000
#define MIISTAT_CAN_T4          0x8000
/* MII Auto-Negotiation Expansion/RemoteEnd Register Bits */
#define MII_AN_TX_FDX           0x0100
#define MII_AN_TX_HDX           0x0080
#define MII_AN_10_FDX           0x0040
#define MII_AN_10_HDX           0x0020


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
	Tx_EnExColl | Tx_EnLCarr | Tx_EnExDefer | Tx_EnUnder | \
	Tx_En)	/* maybe  0x7d01 */
#else
#define TX_CTL_CMD	(Tx_EnComp | Tx_EnTxPar | Tx_EnLateColl | \
	Tx_EnExColl | Tx_EnExDefer | Tx_EnUnder | \
	Tx_En)	/* maybe  0x7f01 */
#endif
#define RX_CTL_CMD	(Rx_EnGood | Rx_EnRxPar | Rx_EnLongErr | Rx_EnOver \
	| Rx_EnCRCErr | Rx_EnAlign | Rx_RxEn)	/* maybe 0x6f01 */

#define INT_EN_CMD  (Int_NRAbtEn | \
	 Int_DParDEn | Int_DParErrEn | \
	Int_SSysErrEn  | Int_RMasAbtEn | Int_RTargAbtEn | \
	Int_STargAbtEn | \
	Int_BLExEn  | Int_FDAExEn) /* maybe 0xb7f*/

/* Tuning parameters */
#define DMA_BURST_SIZE	32
#define TX_THRESHOLD	1024

#define FD_PAGE_NUM 2
#define FD_PAGE_ORDER 1
/* 16 + RX_BUF_PAGES * 8 + RX_FD_NUM * 16 + TX_FD_NUM * 32 <= PAGE_SIZE*2 */
#define RX_BUF_PAGES	8	/* >= 2 */
#define RX_FD_NUM	250	/* >= 32 */
#define TX_FD_NUM	128

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
	struct BDesc bd[RX_BUF_PAGES];
};


extern unsigned long tc_readl(volatile __u32 *addr);
extern void tc_writel(unsigned long data, volatile __u32 *addr);

dma_addr_t priv_dma_handle;

/* Information that need to be kept for each board. */
struct tc35815_local {
	struct net_device *next_module;

	/* statistics */
	struct net_device_stats stats;
	struct {
		int max_tx_qlen;
		int tx_ints;
		int rx_ints;
	} lstats;

	int tbusy;
	int option;
#define TC35815_OPT_AUTO	0x00
#define TC35815_OPT_10M	0x01
#define TC35815_OPT_100M	0x02
#define TC35815_OPT_FULLDUP	0x04
	int linkspeed;	/* 10 or 100 */
	int fullduplex;

	/*
	 * Transmitting: Batch Mode.
	 *	1 BD in 1 TxFD.
	 * Receiving: Packing Mode.
	 *	1 circular FD for Free Buffer List.
	 *	RX_BUG_PAGES BD in Free Buffer FD.
	 *	One Free Buffer BD has PAGE_SIZE data buffer.
	 */
        struct pci_dev *pdev;
	dma_addr_t fd_buf_dma_handle;
	void * fd_buf;	/* for TxFD, TxFD, FrFD */
	struct TxFD *tfd_base;
	int tfd_start;
	int tfd_end;
	struct RxFD *rfd_base;
	struct RxFD *rfd_limit;
	struct RxFD *rfd_cur;
	struct FrFD *fbl_ptr;
	unsigned char fbl_curid;
	dma_addr_t data_buf_dma_handle[RX_BUF_PAGES];
	void * data_buf[RX_BUF_PAGES];		/* packing */
	spinlock_t lock;
};

/* Index to functions, as function prototypes. */

static int __devinit tc35815_probe1(struct pci_dev *pdev, unsigned int base_addr, unsigned int irq);

static int	tc35815_open(struct net_device *dev);
static int	tc35815_send_packet(struct sk_buff *skb, struct net_device *dev);
static void     tc35815_tx_timeout(struct net_device *dev);
static irqreturn_t tc35815_interrupt(int irq, void *dev_id);
static void	tc35815_rx(struct net_device *dev);
static void	tc35815_txdone(struct net_device *dev);
static int	tc35815_close(struct net_device *dev);
static struct	net_device_stats *tc35815_get_stats(struct net_device *dev);
static void	tc35815_set_multicast_list(struct net_device *dev);

static void 	tc35815_chip_reset(struct net_device *dev);
static void 	tc35815_chip_init(struct net_device *dev);
static void 	tc35815_phy_chip_init(struct net_device *dev);

/* A list of all installed tc35815 devices. */
static struct net_device *root_tc35815_dev = NULL;

/*
 * PCI device identifiers for "new style" Linux PCI Device Drivers
 */
static struct pci_device_id tc35815_pci_tbl[] = {
    { PCI_VENDOR_ID_TOSHIBA_2, PCI_DEVICE_ID_TOSHIBA_TC35815CF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
    { 0, }
};

MODULE_DEVICE_TABLE (pci, tc35815_pci_tbl);

int
tc35815_probe(struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	int err = 0;
	int ret;
	unsigned long pci_memaddr;
	unsigned int pci_irq_line;

	printk(KERN_INFO "tc35815_probe: found device %#08x.%#08x\n", ent->vendor, ent->device);

	err = pci_enable_device(pdev);
	if (err)
		return err;

        pci_memaddr = pci_resource_start (pdev, 1);

        printk(KERN_INFO "    pci_memaddr=%#08lx  resource_flags=%#08lx\n", pci_memaddr, pci_resource_flags (pdev, 0));

	if (!pci_memaddr) {
		printk(KERN_WARNING "no PCI MEM resources, aborting\n");
		ret = -ENODEV;
		goto err_out;
	}
	pci_irq_line = pdev->irq;
	/* irq disabled. */
	if (pci_irq_line == 0) {
		printk(KERN_WARNING "no PCI irq, aborting\n");
		ret = -ENODEV;
		goto err_out;
	}

	ret =  tc35815_probe1(pdev, pci_memaddr, pci_irq_line);
	if (ret)
		goto err_out;

	pci_set_master(pdev);

	return 0;

err_out:
	pci_disable_device(pdev);
	return ret;
}

static int __devinit tc35815_probe1(struct pci_dev *pdev, unsigned int base_addr, unsigned int irq)
{
	static unsigned version_printed = 0;
	int i, ret;
	struct tc35815_local *lp;
	struct tc35815_regs *tr;
	struct net_device *dev;

	/* Allocate a new 'dev' if needed. */
	dev = alloc_etherdev(sizeof(struct tc35815_local));
	if (dev == NULL)
		return -ENOMEM;

	/*
	 * alloc_etherdev allocs and zeros dev->priv
	 */
	lp = dev->priv;

	if (tc35815_debug  &&  version_printed++ == 0)
		printk(KERN_DEBUG "%s", version);

	/* Fill in the 'dev' fields. */
	dev->irq = irq;
	dev->base_addr = (unsigned long)ioremap(base_addr,
						sizeof(struct tc35815_regs));
	if (!dev->base_addr) {
		ret = -ENOMEM;
		goto err_out;
	}
	tr = (struct tc35815_regs*)dev->base_addr;

	tc35815_chip_reset(dev);

	/* Retrieve and print the ethernet address. */
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

	/* Initialize the device structure. */
	lp->pdev = pdev;
	lp->next_module = root_tc35815_dev;
	root_tc35815_dev = dev;

	spin_lock_init(&lp->lock);

	if (dev->mem_start > 0) {
		lp->option = dev->mem_start;
		if ((lp->option & TC35815_OPT_10M) &&
		    (lp->option & TC35815_OPT_100M)) {
			/* if both speed speficied, auto select. */
			lp->option &= ~(TC35815_OPT_10M | TC35815_OPT_100M);
		}
	}
	//XXX fixme
        lp->option |= TC35815_OPT_10M;

	/* do auto negotiation */
	tc35815_phy_chip_init(dev);

	dev->open		= tc35815_open;
	dev->stop		= tc35815_close;
	dev->tx_timeout         = tc35815_tx_timeout;
	dev->watchdog_timeo     = TX_TIMEOUT;
	dev->hard_start_xmit	= tc35815_send_packet;
	dev->get_stats		= tc35815_get_stats;
	dev->set_multicast_list = tc35815_set_multicast_list;
	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	ret = register_netdev(dev);
	if (ret)
		goto err_out_iounmap;

	printk(KERN_INFO "%s: %s found at %#x, irq %d, MAC",
	       dev->name, cardname, base_addr, irq);
	for (i = 0; i < 6; i++)
		printk(" %2.2x", dev->dev_addr[i]);
	printk("\n");
	printk(KERN_INFO "%s: linkspeed %dMbps, %s Duplex\n",
	       dev->name, lp->linkspeed, lp->fullduplex ? "Full" : "Half");

	return 0;

err_out_iounmap:
	iounmap((void *) dev->base_addr);
err_out:
	free_netdev(dev);
	return ret;
}


static int
tc35815_init_queues(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	int i;
	unsigned long fd_addr;

	if (!lp->fd_buf) {
		if (sizeof(struct FDesc) +
		    sizeof(struct BDesc) * RX_BUF_PAGES +
		    sizeof(struct FDesc) * RX_FD_NUM +
		    sizeof(struct TxFD) * TX_FD_NUM > PAGE_SIZE * FD_PAGE_NUM) {
			printk(KERN_WARNING "%s: Invalid Queue Size.\n", dev->name);
			return -ENOMEM;
		}

		if ((lp->fd_buf = (void *)__get_free_pages(GFP_KERNEL, FD_PAGE_ORDER)) == 0)
			return -ENOMEM;
		for (i = 0; i < RX_BUF_PAGES; i++) {
			if ((lp->data_buf[i] = (void *)get_zeroed_page(GFP_KERNEL)) == 0) {
				while (--i >= 0) {
					free_page((unsigned long)lp->data_buf[i]);
					lp->data_buf[i] = 0;
				}
				free_page((unsigned long)lp->fd_buf);
				lp->fd_buf = 0;
				return -ENOMEM;
			}
#ifdef __mips__
			dma_cache_wback_inv((unsigned long)lp->data_buf[i], PAGE_SIZE * FD_PAGE_NUM);
#endif
		}
#ifdef __mips__
		dma_cache_wback_inv((unsigned long)lp->fd_buf, PAGE_SIZE * FD_PAGE_NUM);
#endif
	} else {
		memset(lp->fd_buf, 0, PAGE_SIZE * FD_PAGE_NUM);
#ifdef __mips__
		dma_cache_wback_inv((unsigned long)lp->fd_buf, PAGE_SIZE * FD_PAGE_NUM);
#endif
	}
#ifdef __mips__
	fd_addr = (unsigned long)vtonocache(lp->fd_buf);
#else
	fd_addr = (unsigned long)lp->fd_buf;
#endif

	/* Free Descriptors (for Receive) */
	lp->rfd_base = (struct RxFD *)fd_addr;
	fd_addr += sizeof(struct RxFD) * RX_FD_NUM;
	for (i = 0; i < RX_FD_NUM; i++) {
		lp->rfd_base[i].fd.FDCtl = cpu_to_le32(FD_CownsFD);
	}
	lp->rfd_cur = lp->rfd_base;
	lp->rfd_limit = (struct RxFD *)(fd_addr -
					sizeof(struct FDesc) -
					sizeof(struct BDesc) * 30);

	/* Transmit Descriptors */
	lp->tfd_base = (struct TxFD *)fd_addr;
	fd_addr += sizeof(struct TxFD) * TX_FD_NUM;
	for (i = 0; i < TX_FD_NUM; i++) {
		lp->tfd_base[i].fd.FDNext = cpu_to_le32(virt_to_bus(&lp->tfd_base[i+1]));
		lp->tfd_base[i].fd.FDSystem = cpu_to_le32(0);
		lp->tfd_base[i].fd.FDCtl = cpu_to_le32(0);
	}
	lp->tfd_base[TX_FD_NUM-1].fd.FDNext = cpu_to_le32(virt_to_bus(&lp->tfd_base[0]));
	lp->tfd_start = 0;
	lp->tfd_end = 0;

	/* Buffer List (for Receive) */
	lp->fbl_ptr = (struct FrFD *)fd_addr;
	lp->fbl_ptr->fd.FDNext = cpu_to_le32(virt_to_bus(lp->fbl_ptr));
	lp->fbl_ptr->fd.FDCtl = cpu_to_le32(RX_BUF_PAGES | FD_CownsFD);
	for (i = 0; i < RX_BUF_PAGES; i++) {
		lp->fbl_ptr->bd[i].BuffData = cpu_to_le32(virt_to_bus(lp->data_buf[i]));
		/* BDID is index of FrFD.bd[] */
		lp->fbl_ptr->bd[i].BDCtl =
			cpu_to_le32(BD_CownsBD | (i << BD_RxBDID_SHIFT) | PAGE_SIZE);
	}
	lp->fbl_curid = 0;

	return 0;
}

static void
tc35815_clear_queues(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	int i;

	for (i = 0; i < TX_FD_NUM; i++) {
		struct sk_buff *skb = (struct sk_buff *)
			le32_to_cpu(lp->tfd_base[i].fd.FDSystem);
		if (skb)
			dev_kfree_skb_any(skb);
		lp->tfd_base[i].fd.FDSystem = cpu_to_le32(0);
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
			struct sk_buff *skb = (struct sk_buff *)
				le32_to_cpu(lp->tfd_base[i].fd.FDSystem);
			if (skb)
				dev_kfree_skb_any(skb);
			lp->tfd_base[i].fd.FDSystem = cpu_to_le32(0);
		}
	}

	lp->rfd_base = NULL;
	lp->rfd_base = NULL;
	lp->rfd_limit = NULL;
	lp->rfd_cur = NULL;
	lp->fbl_ptr = NULL;

	for (i = 0; i < RX_BUF_PAGES; i++) {
		if (lp->data_buf[i])
			free_page((unsigned long)lp->data_buf[i]);
		lp->data_buf[i] = 0;
	}
	if (lp->fd_buf)
		__free_pages(lp->fd_buf, FD_PAGE_ORDER);
	lp->fd_buf = NULL;
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
	for (i = 0; i < RX_BUF_PAGES; i++)
		printk(" %08x %08x",
		       le32_to_cpu(fd->bd[i].BuffData),
		       le32_to_cpu(fd->bd[i].BDCtl));
	printk("\n");
}

static void
panic_queues(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	int i;

	printk("TxFD base %p, start %d, end %d\n",
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

#if 0
static void print_buf(char *add, int length)
{
	int i;
	int len = length;

	printk("print_buf(%08x)(%x)\n", (unsigned int) add,length);

	if (len > 100)
		len = 100;
	for (i = 0; i < len; i++) {
		printk(" %2.2X", (unsigned char) add[i]);
		if (!(i % 16))
			printk("\n");
	}
	printk("\n");
}
#endif

static void print_eth(char *add)
{
	int i;

	printk("print_eth(%08x)\n", (unsigned int) add);
	for (i = 0; i < 6; i++)
		printk(" %2.2X", (unsigned char) add[i + 6]);
	printk(" =>");
	for (i = 0; i < 6; i++)
		printk(" %2.2X", (unsigned char) add[i]);
	printk(" : %2.2X%2.2X\n", (unsigned char) add[12], (unsigned char) add[13]);
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

	if (dev->irq == 0  ||
	    request_irq(dev->irq, &tc35815_interrupt, IRQF_SHARED, cardname, dev)) {
		return -EAGAIN;
	}

	tc35815_chip_reset(dev);

	if (tc35815_init_queues(dev) != 0) {
		free_irq(dev->irq, dev);
		return -EAGAIN;
	}

	/* Reset the hardware here. Don't forget to set the station address. */
	tc35815_chip_init(dev);

	lp->tbusy = 0;
	netif_start_queue(dev);

	return 0;
}

static void tc35815_tx_timeout(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	struct tc35815_regs *tr = (struct tc35815_regs *)dev->base_addr;
	unsigned long flags;

	spin_lock_irqsave(&lp->lock, flags);
	printk(KERN_WARNING "%s: transmit timed out, status %#lx\n",
	       dev->name, tc_readl(&tr->Tx_Stat));
	/* Try to restart the adaptor. */
	tc35815_chip_reset(dev);
	tc35815_clear_queues(dev);
	tc35815_chip_init(dev);
	lp->tbusy=0;
	spin_unlock_irqrestore(&lp->lock, flags);
	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

static int tc35815_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	struct tc35815_regs *tr = (struct tc35815_regs *)dev->base_addr;

	if (netif_queue_stopped(dev)) {
		/*
		 * If we get here, some higher level has decided we are broken.
		 * There should really be a "kick me" function call instead.
		 */
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < 5)
			return 1;
		printk(KERN_WARNING "%s: transmit timed out, status %#lx\n",
		       dev->name, tc_readl(&tr->Tx_Stat));
		/* Try to restart the adaptor. */
		tc35815_chip_reset(dev);
		tc35815_clear_queues(dev);
		tc35815_chip_init(dev);
		lp->tbusy=0;
		dev->trans_start = jiffies;
		netif_wake_queue(dev);
	}

	/*
	 * Block a timer-based transmit from overlapping. This could better be
	 * done with atomic_swap(1, lp->tbusy), but set_bit() works as well.
	 */
	if (test_and_set_bit(0, (void*)&lp->tbusy) != 0) {
		printk(KERN_WARNING "%s: Transmitter access conflict.\n", dev->name);
		dev_kfree_skb_any(skb);
	} else {
		short length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
		unsigned char *buf = skb->data;
		struct TxFD *txfd = &lp->tfd_base[lp->tfd_start];
		unsigned long flags;
		lp->stats.tx_bytes += skb->len;


#ifdef __mips__
		dma_cache_wback_inv((unsigned long)buf, length);
#endif

		spin_lock_irqsave(&lp->lock, flags);

		/* failsafe... */
		if (lp->tfd_start != lp->tfd_end)
			tc35815_txdone(dev);


		txfd->bd.BuffData = cpu_to_le32(virt_to_bus(buf));

		txfd->bd.BDCtl = cpu_to_le32(length);
		txfd->fd.FDSystem = cpu_to_le32((__u32)skb);
		txfd->fd.FDCtl = cpu_to_le32(FD_CownsFD | (1 << FD_BDCnt_SHIFT));

		if (lp->tfd_start == lp->tfd_end) {
			/* Start DMA Transmitter. */
			txfd->fd.FDNext |= cpu_to_le32(FD_Next_EOL);
#ifdef GATHER_TXINT
			txfd->fd.FDCtl |= cpu_to_le32(FD_FrmOpt_IntTx);
#endif
			if (tc35815_debug > 2) {
				printk("%s: starting TxFD.\n", dev->name);
				dump_txfd(txfd);
				if (tc35815_debug > 3)
					print_eth(buf);
			}
			tc_writel(virt_to_bus(txfd), &tr->TxFrmPtr);
		} else {
			txfd->fd.FDNext &= cpu_to_le32(~FD_Next_EOL);
			if (tc35815_debug > 2) {
				printk("%s: queueing TxFD.\n", dev->name);
				dump_txfd(txfd);
				if (tc35815_debug > 3)
					print_eth(buf);
			}
		}
		lp->tfd_start = (lp->tfd_start + 1) % TX_FD_NUM;

		dev->trans_start = jiffies;

		if ((lp->tfd_start + 1) % TX_FD_NUM != lp->tfd_end) {
			/* we can send another packet */
			lp->tbusy = 0;
			netif_start_queue(dev);
		} else {
			netif_stop_queue(dev);
			if (tc35815_debug > 1)
				printk(KERN_WARNING "%s: TxFD Exhausted.\n", dev->name);
		}
		spin_unlock_irqrestore(&lp->lock, flags);
	}

	return 0;
}

#define FATAL_ERROR_INT \
	(Int_IntPCI | Int_DmParErr | Int_IntNRAbt)
static void tc35815_fatal_error_interrupt(struct net_device *dev, int status)
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
	printk(KERN_WARNING "%s: Resetting %s...\n", dev->name, cardname);
	/* Try to restart the adaptor. */
	tc35815_chip_reset(dev);
	tc35815_clear_queues(dev);
	tc35815_chip_init(dev);
}

/*
 * The typical workload of the driver:
 *   Handle the network interface interrupts.
 */
static irqreturn_t tc35815_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct tc35815_regs *tr;
	struct tc35815_local *lp;
	int status, boguscount = 0;
	int handled = 0;

	if (dev == NULL) {
		printk(KERN_WARNING "%s: irq %d for unknown device.\n", cardname, irq);
		return IRQ_NONE;
	}

	tr = (struct tc35815_regs*)dev->base_addr;
	lp = dev->priv;

	do {
		status = tc_readl(&tr->Int_Src);
		if (status == 0)
			break;
		handled = 1;
		tc_writel(status, &tr->Int_Src);	/* write to clear */

		/* Fatal errors... */
		if (status & FATAL_ERROR_INT) {
			tc35815_fatal_error_interrupt(dev, status);
			break;
		}
		/* recoverable errors */
		if (status & Int_IntFDAEx) {
			/* disable FDAEx int. (until we make rooms...) */
			tc_writel(tc_readl(&tr->Int_En) & ~Int_FDAExEn, &tr->Int_En);
			printk(KERN_WARNING
			       "%s: Free Descriptor Area Exhausted (%#x).\n",
			       dev->name, status);
			lp->stats.rx_dropped++;
		}
		if (status & Int_IntBLEx) {
			/* disable BLEx int. (until we make rooms...) */
			tc_writel(tc_readl(&tr->Int_En) & ~Int_BLExEn, &tr->Int_En);
			printk(KERN_WARNING
			       "%s: Buffer List Exhausted (%#x).\n",
			       dev->name, status);
			lp->stats.rx_dropped++;
		}
		if (status & Int_IntExBD) {
			printk(KERN_WARNING
			       "%s: Excessive Buffer Descriptiors (%#x).\n",
			       dev->name, status);
			lp->stats.rx_length_errors++;
		}
		/* normal notification */
		if (status & Int_IntMacRx) {
			/* Got a packet(s). */
			lp->lstats.rx_ints++;
			tc35815_rx(dev);
		}
		if (status & Int_IntMacTx) {
			lp->lstats.tx_ints++;
			tc35815_txdone(dev);
		}
	} while (++boguscount < 20) ;

	return IRQ_RETVAL(handled);
}

/* We have a good packet(s), get it/them out of the buffers. */
static void
tc35815_rx(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	struct tc35815_regs *tr = (struct tc35815_regs*)dev->base_addr;
	unsigned int fdctl;
	int i;
	int buf_free_count = 0;
	int fd_free_count = 0;

	while (!((fdctl = le32_to_cpu(lp->rfd_cur->fd.FDCtl)) & FD_CownsFD)) {
		int status = le32_to_cpu(lp->rfd_cur->fd.FDStat);
		int pkt_len = fdctl & FD_FDLength_MASK;
		struct RxFD *next_rfd;
		int bd_count = (fdctl & FD_BDCnt_MASK) >> FD_BDCnt_SHIFT;

		if (tc35815_debug > 2)
			dump_rxfd(lp->rfd_cur);
		if (status & Rx_Good) {
			/* Malloc up new buffer. */
			struct sk_buff *skb;
			unsigned char *data;
			int cur_bd, offset;

			lp->stats.rx_bytes += pkt_len;

			skb = dev_alloc_skb(pkt_len + 2); /* +2: for reserve */
			if (skb == NULL) {
				printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n",
				       dev->name);
				lp->stats.rx_dropped++;
				break;
			}
			skb_reserve(skb, 2);   /* 16 bit alignment */
			skb->dev = dev;

			data = skb_put(skb, pkt_len);

			/* copy from receive buffer */
			cur_bd = 0;
			offset = 0;
			while (offset < pkt_len && cur_bd < bd_count) {
				int len = le32_to_cpu(lp->rfd_cur->bd[cur_bd].BDCtl) &
					BD_BuffLength_MASK;
				void *rxbuf =
					bus_to_virt(le32_to_cpu(lp->rfd_cur->bd[cur_bd].BuffData));
#ifdef __mips__
				dma_cache_inv((unsigned long)rxbuf, len);
#endif
				memcpy(data + offset, rxbuf, len);
				offset += len;
				cur_bd++;
			}
#if 0
			print_buf(data,pkt_len);
#endif
			if (tc35815_debug > 3)
				print_eth(data);
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);
			lp->stats.rx_packets++;
		} else {
			lp->stats.rx_errors++;
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
			if (id >= RX_BUF_PAGES) {
				printk("%s: invalid BDID.\n", dev->name);
				panic_queues(dev);
			}
			/* free old buffers */
			while (lp->fbl_curid != id) {
				bdctl = le32_to_cpu(lp->fbl_ptr->bd[lp->fbl_curid].BDCtl);
				if (bdctl & BD_CownsBD) {
					printk("%s: Freeing invalid BD.\n",
					       dev->name);
					panic_queues(dev);
				}
				/* pass BD to controler */
				/* Note: BDLength was modified by chip. */
				lp->fbl_ptr->bd[lp->fbl_curid].BDCtl =
					cpu_to_le32(BD_CownsBD |
						    (lp->fbl_curid << BD_RxBDID_SHIFT) |
						    PAGE_SIZE);
				lp->fbl_curid =
					(lp->fbl_curid + 1) % RX_BUF_PAGES;
				if (tc35815_debug > 2) {
					printk("%s: Entering new FBD %d\n",
					       dev->name, lp->fbl_curid);
					dump_frfd(lp->fbl_ptr);
				}
				buf_free_count++;
			}
		}

		/* put RxFD back to controller */
		next_rfd = bus_to_virt(le32_to_cpu(lp->rfd_cur->fd.FDNext));
#ifdef __mips__
		next_rfd = (struct RxFD *)vtonocache(next_rfd);
#endif
		if (next_rfd < lp->rfd_base || next_rfd > lp->rfd_limit) {
			printk("%s: RxFD FDNext invalid.\n", dev->name);
			panic_queues(dev);
		}
		for (i = 0; i < (bd_count + 1) / 2 + 1; i++) {
			/* pass FD to controler */
			lp->rfd_cur->fd.FDNext = cpu_to_le32(0xdeaddead);	/* for debug */
			lp->rfd_cur->fd.FDCtl = cpu_to_le32(FD_CownsFD);
			lp->rfd_cur++;
			fd_free_count++;
		}

		lp->rfd_cur = next_rfd;
	}

	/* re-enable BL/FDA Exhaust interrupts. */
	if (fd_free_count) {
		tc_writel(tc_readl(&tr->Int_En) | Int_FDAExEn, &tr->Int_En);
		if (buf_free_count)
			tc_writel(tc_readl(&tr->Int_En) | Int_BLExEn, &tr->Int_En);
	}
}

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

	/* WORKAROUND: ignore LostCrS in full duplex operation */
	if (lp->fullduplex)
		status &= ~Tx_NCarr;

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
	if (msg)
		printk(KERN_WARNING "%s: %s (%#x)\n", dev->name, msg, status);
}

static void
tc35815_txdone(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	struct tc35815_regs *tr = (struct tc35815_regs*)dev->base_addr;
	struct TxFD *txfd;
	unsigned int fdctl;
	int num_done = 0;

	txfd = &lp->tfd_base[lp->tfd_end];
	while (lp->tfd_start != lp->tfd_end &&
	       !((fdctl = le32_to_cpu(txfd->fd.FDCtl)) & FD_CownsFD)) {
		int status = le32_to_cpu(txfd->fd.FDStat);
		struct sk_buff *skb;
		unsigned long fdnext = le32_to_cpu(txfd->fd.FDNext);

		if (tc35815_debug > 2) {
			printk("%s: complete TxFD.\n", dev->name);
			dump_txfd(txfd);
		}
		tc35815_check_tx_stat(dev, status);

		skb = (struct sk_buff *)le32_to_cpu(txfd->fd.FDSystem);
		if (skb) {
			dev_kfree_skb_any(skb);
		}
		txfd->fd.FDSystem = cpu_to_le32(0);

		num_done++;
		lp->tfd_end = (lp->tfd_end + 1) % TX_FD_NUM;
		txfd = &lp->tfd_base[lp->tfd_end];
		if ((fdnext & ~FD_Next_EOL) != virt_to_bus(txfd)) {
			printk("%s: TxFD FDNext invalid.\n", dev->name);
			panic_queues(dev);
		}
		if (fdnext & FD_Next_EOL) {
			/* DMA Transmitter has been stopping... */
			if (lp->tfd_end != lp->tfd_start) {
				int head = (lp->tfd_start + TX_FD_NUM - 1) % TX_FD_NUM;
				struct TxFD* txhead = &lp->tfd_base[head];
				int qlen = (lp->tfd_start + TX_FD_NUM
					    - lp->tfd_end) % TX_FD_NUM;

				if (!(le32_to_cpu(txfd->fd.FDCtl) & FD_CownsFD)) {
					printk("%s: TxFD FDCtl invalid.\n", dev->name);
					panic_queues(dev);
				}
				/* log max queue length */
				if (lp->lstats.max_tx_qlen < qlen)
					lp->lstats.max_tx_qlen = qlen;


				/* start DMA Transmitter again */
				txhead->fd.FDNext |= cpu_to_le32(FD_Next_EOL);
#ifdef GATHER_TXINT
				txhead->fd.FDCtl |= cpu_to_le32(FD_FrmOpt_IntTx);
#endif
				if (tc35815_debug > 2) {
					printk("%s: start TxFD on queue.\n",
					       dev->name);
					dump_txfd(txfd);
				}
				tc_writel(virt_to_bus(txfd), &tr->TxFrmPtr);
			}
			break;
		}
	}

	if (num_done > 0 && lp->tbusy) {
		lp->tbusy = 0;
		netif_start_queue(dev);
	}
}

/* The inverse routine to tc35815_open(). */
static int
tc35815_close(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;

	lp->tbusy = 1;
	netif_stop_queue(dev);

	/* Flush the Tx and disable Rx here. */

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
	struct tc35815_regs *tr = (struct tc35815_regs*)dev->base_addr;
	unsigned long flags;

	if (netif_running(dev)) {
		spin_lock_irqsave(&lp->lock, flags);
		/* Update the statistics from the device registers. */
		lp->stats.rx_missed_errors = tc_readl(&tr->Miss_Cnt);
		spin_unlock_irqrestore(&lp->lock, flags);
	}

	return &lp->stats;
}

static void tc35815_set_cam_entry(struct tc35815_regs *tr, int index, unsigned char *addr)
{
	int cam_index = index * 6;
	unsigned long cam_data;
	unsigned long saved_addr;
	saved_addr = tc_readl(&tr->CAM_Adr);

	if (tc35815_debug > 1) {
		int i;
		printk(KERN_DEBUG "%s: CAM %d:", cardname, index);
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

	if (tc35815_debug > 2) {
		int i;
		for (i = cam_index / 4; i < cam_index / 4 + 2; i++) {
			tc_writel(i * 4, &tr->CAM_Adr);
			printk("CAM 0x%x: %08lx",
			       i * 4, tc_readl(&tr->CAM_Data));
		}
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
	struct tc35815_regs *tr = (struct tc35815_regs*)dev->base_addr;

	if (dev->flags&IFF_PROMISC)
	{
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
			tc35815_set_cam_entry(tr, i + 2, cur_addr->dmi_addr);
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

static unsigned long tc_phy_read(struct net_device *dev, struct tc35815_regs *tr, int phy, int phy_reg)
{
	struct tc35815_local *lp = dev->priv;
	unsigned long data;
	unsigned long flags;

	spin_lock_irqsave(&lp->lock, flags);

	tc_writel(MD_CA_Busy | (phy << 5) | phy_reg, &tr->MD_CA);
	while (tc_readl(&tr->MD_CA) & MD_CA_Busy)
		;
	data = tc_readl(&tr->MD_Data);
	spin_unlock_irqrestore(&lp->lock, flags);
	return data;
}

static void tc_phy_write(struct net_device *dev, unsigned long d, struct tc35815_regs *tr, int phy, int phy_reg)
{
	struct tc35815_local *lp = dev->priv;
	unsigned long flags;

	spin_lock_irqsave(&lp->lock, flags);

	tc_writel(d, &tr->MD_Data);
	tc_writel(MD_CA_Busy | MD_CA_Wr | (phy << 5) | phy_reg, &tr->MD_CA);
	while (tc_readl(&tr->MD_CA) & MD_CA_Busy)
		;
	spin_unlock_irqrestore(&lp->lock, flags);
}

static void tc35815_phy_chip_init(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	struct tc35815_regs *tr = (struct tc35815_regs*)dev->base_addr;
	static int first = 1;
	unsigned short ctl;

	if (first) {
		unsigned short id0, id1;
		int count;
		first = 0;

		/* first data written to the PHY will be an ID number */
		tc_phy_write(dev, 0, tr, 0, MII_CONTROL);	/* ID:0 */
#if 0
		tc_phy_write(dev, MIICNTL_RESET, tr, 0, MII_CONTROL);
		printk(KERN_INFO "%s: Resetting PHY...", dev->name);
		while (tc_phy_read(dev, tr, 0, MII_CONTROL) & MIICNTL_RESET)
			;
		printk("\n");
		tc_phy_write(dev, MIICNTL_AUTO|MIICNTL_SPEED|MIICNTL_FDX, tr, 0,
			     MII_CONTROL);
#endif
		id0 = tc_phy_read(dev, tr, 0, MII_PHY_ID0);
		id1 = tc_phy_read(dev, tr, 0, MII_PHY_ID1);
		printk(KERN_DEBUG "%s: PHY ID %04x %04x\n", dev->name,
		       id0, id1);
		if (lp->option & TC35815_OPT_10M) {
			lp->linkspeed = 10;
			lp->fullduplex = (lp->option & TC35815_OPT_FULLDUP) != 0;
		} else if (lp->option & TC35815_OPT_100M) {
			lp->linkspeed = 100;
			lp->fullduplex = (lp->option & TC35815_OPT_FULLDUP) != 0;
		} else {
			/* auto negotiation */
			unsigned long neg_result;
			tc_phy_write(dev, MIICNTL_AUTO | MIICNTL_RST_AUTO, tr, 0, MII_CONTROL);
			printk(KERN_INFO "%s: Auto Negotiation...", dev->name);
			count = 0;
			while (!(tc_phy_read(dev, tr, 0, MII_STATUS) & MIISTAT_AUTO_DONE)) {
				if (count++ > 5000) {
					printk(" failed. Assume 10Mbps\n");
					lp->linkspeed = 10;
					lp->fullduplex = 0;
					goto done;
				}
				if (count % 512 == 0)
					printk(".");
				mdelay(1);
			}
			printk(" done.\n");
			neg_result = tc_phy_read(dev, tr, 0, MII_ANLPAR);
			if (neg_result & (MII_AN_TX_FDX | MII_AN_TX_HDX))
				lp->linkspeed = 100;
			else
				lp->linkspeed = 10;
			if (neg_result & (MII_AN_TX_FDX | MII_AN_10_FDX))
				lp->fullduplex = 1;
			else
				lp->fullduplex = 0;
		done:
			;
		}
	}

	ctl = 0;
	if (lp->linkspeed == 100)
		ctl |= MIICNTL_SPEED;
	if (lp->fullduplex)
		ctl |= MIICNTL_FDX;
	tc_phy_write(dev, ctl, tr, 0, MII_CONTROL);

	if (lp->fullduplex) {
		tc_writel(tc_readl(&tr->MAC_Ctl) | MAC_FullDup, &tr->MAC_Ctl);
	}
}

static void tc35815_chip_reset(struct net_device *dev)
{
	struct tc35815_regs *tr = (struct tc35815_regs*)dev->base_addr;

	/* reset the controller */
	tc_writel(MAC_Reset, &tr->MAC_Ctl);
	while (tc_readl(&tr->MAC_Ctl) & MAC_Reset)
		;

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

}

static void tc35815_chip_init(struct net_device *dev)
{
	struct tc35815_local *lp = dev->priv;
	struct tc35815_regs *tr = (struct tc35815_regs*)dev->base_addr;
	unsigned long flags;
	unsigned long txctl = TX_CTL_CMD;

	tc35815_phy_chip_init(dev);

	/* load station address to CAM */
	tc35815_set_cam_entry(tr, CAM_ENTRY_SOURCE, dev->dev_addr);

	/* Enable CAM (broadcast and unicast) */
	tc_writel(CAM_Ena_Bit(CAM_ENTRY_SOURCE), &tr->CAM_Ena);
	tc_writel(CAM_CompEn | CAM_BroadAcc, &tr->CAM_Ctl);

	spin_lock_irqsave(&lp->lock, flags);

	tc_writel(DMA_BURST_SIZE, &tr->DMA_Ctl);

	tc_writel(RxFrag_EnPack | ETH_ZLEN, &tr->RxFragSize);	/* Packing */
	tc_writel(0, &tr->TxPollCtr);	/* Batch mode */
	tc_writel(TX_THRESHOLD, &tr->TxThrsh);
	tc_writel(INT_EN_CMD, &tr->Int_En);

	/* set queues */
	tc_writel(virt_to_bus(lp->rfd_base), &tr->FDA_Bas);
	tc_writel((unsigned long)lp->rfd_limit - (unsigned long)lp->rfd_base,
		  &tr->FDA_Lim);
	/*
	 * Activation method:
	 * First, enable eht MAC Transmitter and the DMA Receive circuits.
	 * Then enable the DMA Transmitter and the MAC Receive circuits.
	 */
	tc_writel(virt_to_bus(lp->fbl_ptr), &tr->BLFrmPtr);	/* start DMA receiver */
	tc_writel(RX_CTL_CMD, &tr->Rx_Ctl);	/* start MAC receiver */
	/* start MAC transmitter */
	/* WORKAROUND: ignore LostCrS in full duplex operation */
	if (lp->fullduplex)
		txctl = TX_CTL_CMD & ~Tx_EnLCarr;
#ifdef GATHER_TXINT
	txctl &= ~Tx_EnComp;	/* disable global tx completion int. */
#endif
	tc_writel(txctl, &tr->Tx_Ctl);
#if 0	/* No need to polling */
	tc_writel(virt_to_bus(lp->tfd_base), &tr->TxFrmPtr);	/* start DMA transmitter */
#endif
	spin_unlock_irqrestore(&lp->lock, flags);
}

static struct pci_driver tc35815_driver = {
	.name = TC35815_MODULE_NAME,
	.probe = tc35815_probe,
	.remove = NULL,
	.id_table = tc35815_pci_tbl,
};

static int __init tc35815_init_module(void)
{
	return pci_register_driver(&tc35815_driver);
}

static void __exit tc35815_cleanup_module(void)
{
	struct net_device *next_dev;

	/*
	 * TODO: implement a tc35815_driver.remove hook, and
	 * move this code into that function.  Then, delete
	 * all root_tc35815_dev list handling code.
	 */
	while (root_tc35815_dev) {
		struct net_device *dev = root_tc35815_dev;
		next_dev = ((struct tc35815_local *)dev->priv)->next_module;
		iounmap((void *)(dev->base_addr));
		unregister_netdev(dev);
		free_netdev(dev);
		root_tc35815_dev = next_dev;
	}

	pci_unregister_driver(&tc35815_driver);
}

module_init(tc35815_init_module);
module_exit(tc35815_cleanup_module);
