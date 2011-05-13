/* $Id: sunbmac.h,v 1.7 2000/07/11 22:35:22 davem Exp $
 * sunbmac.h: Defines for the Sun "Big MAC" 100baseT ethernet cards.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SUNBMAC_H
#define _SUNBMAC_H

/* QEC global registers. */
#define GLOB_CTRL	0x00UL	/* Control                  */
#define GLOB_STAT	0x04UL	/* Status                   */
#define GLOB_PSIZE	0x08UL	/* Packet Size              */
#define GLOB_MSIZE	0x0cUL	/* Local-mem size (64K)     */
#define GLOB_RSIZE	0x10UL	/* Receive partition size   */
#define GLOB_TSIZE	0x14UL	/* Transmit partition size  */
#define GLOB_REG_SIZE	0x18UL

#define GLOB_CTRL_MMODE       0x40000000 /* MACE qec mode            */
#define GLOB_CTRL_BMODE       0x10000000 /* BigMAC qec mode          */
#define GLOB_CTRL_EPAR        0x00000020 /* Enable parity            */
#define GLOB_CTRL_ACNTRL      0x00000018 /* SBUS arbitration control */
#define GLOB_CTRL_B64         0x00000004 /* 64 byte dvma bursts      */
#define GLOB_CTRL_B32         0x00000002 /* 32 byte dvma bursts      */
#define GLOB_CTRL_B16         0x00000000 /* 16 byte dvma bursts      */
#define GLOB_CTRL_RESET       0x00000001 /* Reset the QEC            */

#define GLOB_STAT_TX          0x00000008 /* BigMAC Transmit IRQ      */
#define GLOB_STAT_RX          0x00000004 /* BigMAC Receive IRQ       */
#define GLOB_STAT_BM          0x00000002 /* BigMAC Global IRQ        */
#define GLOB_STAT_ER          0x00000001 /* BigMAC Error IRQ         */

#define GLOB_PSIZE_2048       0x00       /* 2k packet size           */
#define GLOB_PSIZE_4096       0x01       /* 4k packet size           */
#define GLOB_PSIZE_6144       0x10       /* 6k packet size           */
#define GLOB_PSIZE_8192       0x11       /* 8k packet size           */

/* QEC BigMAC channel registers. */
#define CREG_CTRL	0x00UL	/* Control                   */
#define CREG_STAT	0x04UL	/* Status                    */
#define CREG_RXDS	0x08UL	/* RX descriptor ring ptr    */
#define CREG_TXDS	0x0cUL	/* TX descriptor ring ptr    */
#define CREG_RIMASK	0x10UL	/* RX Interrupt Mask         */
#define CREG_TIMASK	0x14UL	/* TX Interrupt Mask         */
#define CREG_QMASK	0x18UL	/* QEC Error Interrupt Mask  */
#define CREG_BMASK	0x1cUL	/* BigMAC Error Interrupt Mask*/
#define CREG_RXWBUFPTR	0x20UL	/* Local memory rx write ptr */
#define CREG_RXRBUFPTR	0x24UL	/* Local memory rx read ptr  */
#define CREG_TXWBUFPTR	0x28UL	/* Local memory tx write ptr */
#define CREG_TXRBUFPTR	0x2cUL	/* Local memory tx read ptr  */
#define CREG_CCNT	0x30UL	/* Collision Counter         */
#define CREG_REG_SIZE	0x34UL

#define CREG_CTRL_TWAKEUP     0x00000001  /* Transmitter Wakeup, 'go'. */

#define CREG_STAT_BERROR      0x80000000  /* BigMAC error              */
#define CREG_STAT_TXIRQ       0x00200000  /* Transmit Interrupt        */
#define CREG_STAT_TXDERROR    0x00080000  /* TX Descriptor is bogus    */
#define CREG_STAT_TXLERR      0x00040000  /* Late Transmit Error       */
#define CREG_STAT_TXPERR      0x00020000  /* Transmit Parity Error     */
#define CREG_STAT_TXSERR      0x00010000  /* Transmit SBUS error ack   */
#define CREG_STAT_RXIRQ       0x00000020  /* Receive Interrupt         */
#define CREG_STAT_RXDROP      0x00000010  /* Dropped a RX'd packet     */
#define CREG_STAT_RXSMALL     0x00000008  /* Receive buffer too small  */
#define CREG_STAT_RXLERR      0x00000004  /* Receive Late Error        */
#define CREG_STAT_RXPERR      0x00000002  /* Receive Parity Error      */
#define CREG_STAT_RXSERR      0x00000001  /* Receive SBUS Error ACK    */

#define CREG_STAT_ERRORS      (CREG_STAT_BERROR|CREG_STAT_TXDERROR|CREG_STAT_TXLERR|   \
                               CREG_STAT_TXPERR|CREG_STAT_TXSERR|CREG_STAT_RXDROP|     \
                               CREG_STAT_RXSMALL|CREG_STAT_RXLERR|CREG_STAT_RXPERR|    \
                               CREG_STAT_RXSERR)

#define CREG_QMASK_TXDERROR   0x00080000  /* TXD error                 */
#define CREG_QMASK_TXLERR     0x00040000  /* TX late error             */
#define CREG_QMASK_TXPERR     0x00020000  /* TX parity error           */
#define CREG_QMASK_TXSERR     0x00010000  /* TX sbus error ack         */
#define CREG_QMASK_RXDROP     0x00000010  /* RX drop                   */
#define CREG_QMASK_RXBERROR   0x00000008  /* RX buffer error           */
#define CREG_QMASK_RXLEERR    0x00000004  /* RX late error             */
#define CREG_QMASK_RXPERR     0x00000002  /* RX parity error           */
#define CREG_QMASK_RXSERR     0x00000001  /* RX sbus error ack         */

/* BIGMAC core registers */
#define BMAC_XIFCFG	0x000UL	/* XIF config register                */
	/* 0x004-->0x0fc, reserved */
#define BMAC_STATUS	0x100UL	/* Status register, clear on read     */
#define BMAC_IMASK	0x104UL	/* Interrupt mask register            */
	/* 0x108-->0x204, reserved */
#define BMAC_TXSWRESET	0x208UL	/* Transmitter software reset         */
#define BMAC_TXCFG	0x20cUL	/* Transmitter config register        */
#define BMAC_IGAP1	0x210UL	/* Inter-packet gap 1                 */
#define BMAC_IGAP2	0x214UL	/* Inter-packet gap 2                 */
#define BMAC_ALIMIT	0x218UL	/* Transmit attempt limit             */
#define BMAC_STIME	0x21cUL	/* Transmit slot time                 */
#define BMAC_PLEN	0x220UL	/* Size of transmit preamble          */
#define BMAC_PPAT	0x224UL	/* Pattern for transmit preamble      */
#define BMAC_TXDELIM	0x228UL	/* Transmit delimiter                 */
#define BMAC_JSIZE	0x22cUL	/* Toe jam...                         */
#define BMAC_TXPMAX	0x230UL	/* Transmit max pkt size              */
#define BMAC_TXPMIN	0x234UL	/* Transmit min pkt size              */
#define BMAC_PATTEMPT	0x238UL	/* Count of transmit peak attempts    */
#define BMAC_DTCTR	0x23cUL	/* Transmit defer timer               */
#define BMAC_NCCTR	0x240UL	/* Transmit normal-collision counter  */
#define BMAC_FCCTR	0x244UL	/* Transmit first-collision counter   */
#define BMAC_EXCTR	0x248UL	/* Transmit excess-collision counter  */
#define BMAC_LTCTR	0x24cUL	/* Transmit late-collision counter    */
#define BMAC_RSEED	0x250UL	/* Transmit random number seed        */
#define BMAC_TXSMACHINE	0x254UL /* Transmit state machine             */
	/* 0x258-->0x304, reserved */
#define BMAC_RXSWRESET	0x308UL	/* Receiver software reset            */
#define BMAC_RXCFG	0x30cUL	/* Receiver config register           */
#define BMAC_RXPMAX	0x310UL	/* Receive max pkt size               */
#define BMAC_RXPMIN	0x314UL	/* Receive min pkt size               */
#define BMAC_MACADDR2	0x318UL	/* Ether address register 2           */
#define BMAC_MACADDR1	0x31cUL	/* Ether address register 1           */
#define BMAC_MACADDR0	0x320UL	/* Ether address register 0           */
#define BMAC_FRCTR	0x324UL	/* Receive frame receive counter      */
#define BMAC_GLECTR	0x328UL	/* Receive giant-length error counter */
#define BMAC_UNALECTR	0x32cUL	/* Receive unaligned error counter    */
#define BMAC_RCRCECTR	0x330UL	/* Receive CRC error counter          */
#define BMAC_RXSMACHINE	0x334UL	/* Receiver state machine             */
#define BMAC_RXCVALID	0x338UL	/* Receiver code violation            */
	/* 0x33c, reserved */
#define BMAC_HTABLE3	0x340UL	/* Hash table 3                       */
#define BMAC_HTABLE2	0x344UL	/* Hash table 2                       */
#define BMAC_HTABLE1	0x348UL	/* Hash table 1                       */
#define BMAC_HTABLE0	0x34cUL	/* Hash table 0                       */
#define BMAC_AFILTER2	0x350UL	/* Address filter 2                   */
#define BMAC_AFILTER1	0x354UL	/* Address filter 1                   */
#define BMAC_AFILTER0	0x358UL	/* Address filter 0                   */
#define BMAC_AFMASK	0x35cUL	/* Address filter mask                */
#define BMAC_REG_SIZE	0x360UL

/* BigMac XIF config register. */
#define BIGMAC_XCFG_ODENABLE   0x00000001 /* Output driver enable                     */
#define BIGMAC_XCFG_RESV       0x00000002 /* Reserved, write always as 1              */
#define BIGMAC_XCFG_MLBACK     0x00000004 /* Loopback-mode MII enable                 */
#define BIGMAC_XCFG_SMODE      0x00000008 /* Enable serial mode                       */

/* BigMAC status register. */
#define BIGMAC_STAT_GOTFRAME   0x00000001 /* Received a frame                         */
#define BIGMAC_STAT_RCNTEXP    0x00000002 /* Receive frame counter expired            */
#define BIGMAC_STAT_ACNTEXP    0x00000004 /* Align-error counter expired              */
#define BIGMAC_STAT_CCNTEXP    0x00000008 /* CRC-error counter expired                */
#define BIGMAC_STAT_LCNTEXP    0x00000010 /* Length-error counter expired             */
#define BIGMAC_STAT_RFIFOVF    0x00000020 /* Receive FIFO overflow                    */
#define BIGMAC_STAT_CVCNTEXP   0x00000040 /* Code-violation counter expired           */
#define BIGMAC_STAT_SENTFRAME  0x00000100 /* Transmitted a frame                      */
#define BIGMAC_STAT_TFIFO_UND  0x00000200 /* Transmit FIFO underrun                   */
#define BIGMAC_STAT_MAXPKTERR  0x00000400 /* Max-packet size error                    */
#define BIGMAC_STAT_NCNTEXP    0x00000800 /* Normal-collision counter expired         */
#define BIGMAC_STAT_ECNTEXP    0x00001000 /* Excess-collision counter expired         */
#define BIGMAC_STAT_LCCNTEXP   0x00002000 /* Late-collision counter expired           */
#define BIGMAC_STAT_FCNTEXP    0x00004000 /* First-collision counter expired          */
#define BIGMAC_STAT_DTIMEXP    0x00008000 /* Defer-timer expired                      */

/* BigMAC interrupt mask register. */
#define BIGMAC_IMASK_GOTFRAME  0x00000001 /* Received a frame                         */
#define BIGMAC_IMASK_RCNTEXP   0x00000002 /* Receive frame counter expired            */
#define BIGMAC_IMASK_ACNTEXP   0x00000004 /* Align-error counter expired              */
#define BIGMAC_IMASK_CCNTEXP   0x00000008 /* CRC-error counter expired                */
#define BIGMAC_IMASK_LCNTEXP   0x00000010 /* Length-error counter expired             */
#define BIGMAC_IMASK_RFIFOVF   0x00000020 /* Receive FIFO overflow                    */
#define BIGMAC_IMASK_CVCNTEXP  0x00000040 /* Code-violation counter expired           */
#define BIGMAC_IMASK_SENTFRAME 0x00000100 /* Transmitted a frame                      */
#define BIGMAC_IMASK_TFIFO_UND 0x00000200 /* Transmit FIFO underrun                   */
#define BIGMAC_IMASK_MAXPKTERR 0x00000400 /* Max-packet size error                    */
#define BIGMAC_IMASK_NCNTEXP   0x00000800 /* Normal-collision counter expired         */
#define BIGMAC_IMASK_ECNTEXP   0x00001000 /* Excess-collision counter expired         */
#define BIGMAC_IMASK_LCCNTEXP  0x00002000 /* Late-collision counter expired           */
#define BIGMAC_IMASK_FCNTEXP   0x00004000 /* First-collision counter expired          */
#define BIGMAC_IMASK_DTIMEXP   0x00008000 /* Defer-timer expired                      */

/* BigMac transmit config register. */
#define BIGMAC_TXCFG_ENABLE    0x00000001 /* Enable the transmitter                   */
#define BIGMAC_TXCFG_FIFO      0x00000010 /* Default tx fthresh...                    */
#define BIGMAC_TXCFG_SMODE     0x00000020 /* Enable slow transmit mode                */
#define BIGMAC_TXCFG_CIGN      0x00000040 /* Ignore transmit collisions               */
#define BIGMAC_TXCFG_FCSOFF    0x00000080 /* Do not emit FCS                          */
#define BIGMAC_TXCFG_DBACKOFF  0x00000100 /* Disable backoff                          */
#define BIGMAC_TXCFG_FULLDPLX  0x00000200 /* Enable full-duplex                       */

/* BigMac receive config register. */
#define BIGMAC_RXCFG_ENABLE    0x00000001 /* Enable the receiver                      */
#define BIGMAC_RXCFG_FIFO      0x0000000e /* Default rx fthresh...                    */
#define BIGMAC_RXCFG_PSTRIP    0x00000020 /* Pad byte strip enable                    */
#define BIGMAC_RXCFG_PMISC     0x00000040 /* Enable promiscuous mode                   */
#define BIGMAC_RXCFG_DERR      0x00000080 /* Disable error checking                   */
#define BIGMAC_RXCFG_DCRCS     0x00000100 /* Disable CRC stripping                    */
#define BIGMAC_RXCFG_ME        0x00000200 /* Receive packets addressed to me          */
#define BIGMAC_RXCFG_PGRP      0x00000400 /* Enable promisc group mode                */
#define BIGMAC_RXCFG_HENABLE   0x00000800 /* Enable the hash filter                   */
#define BIGMAC_RXCFG_AENABLE   0x00001000 /* Enable the address filter                */

/* The BigMAC PHY transceiver.  Not nearly as sophisticated as the happy meal
 * one.  But it does have the "bit banger", oh baby.
 */
#define TCVR_TPAL	0x00UL
#define TCVR_MPAL	0x04UL
#define TCVR_REG_SIZE	0x08UL

/* Frame commands. */
#define FRAME_WRITE           0x50020000
#define FRAME_READ            0x60020000

/* Tranceiver registers. */
#define TCVR_PAL_SERIAL       0x00000001 /* Enable serial mode              */
#define TCVR_PAL_EXTLBACK     0x00000002 /* Enable external loopback        */
#define TCVR_PAL_MSENSE       0x00000004 /* Media sense                     */
#define TCVR_PAL_LTENABLE     0x00000008 /* Link test enable                */
#define TCVR_PAL_LTSTATUS     0x00000010 /* Link test status  (P1 only)     */

/* Management PAL. */
#define MGMT_PAL_DCLOCK       0x00000001 /* Data clock                      */
#define MGMT_PAL_OENAB        0x00000002 /* Output enabler                  */
#define MGMT_PAL_MDIO         0x00000004 /* MDIO Data/attached              */
#define MGMT_PAL_TIMEO        0x00000008 /* Transmit enable timeout error   */
#define MGMT_PAL_EXT_MDIO     MGMT_PAL_MDIO
#define MGMT_PAL_INT_MDIO     MGMT_PAL_TIMEO

/* Here are some PHY addresses. */
#define BIGMAC_PHY_EXTERNAL   0 /* External transceiver */
#define BIGMAC_PHY_INTERNAL   1 /* Internal transceiver */

/* PHY registers */
#define BIGMAC_BMCR           0x00 /* Basic mode control register	*/
#define BIGMAC_BMSR           0x01 /* Basic mode status register	*/

/* BMCR bits */
#define BMCR_ISOLATE            0x0400  /* Disconnect DP83840 from MII */
#define BMCR_PDOWN              0x0800  /* Powerdown the DP83840       */
#define BMCR_ANENABLE           0x1000  /* Enable auto negotiation     */
#define BMCR_SPEED100           0x2000  /* Select 100Mbps              */
#define BMCR_LOOPBACK           0x4000  /* TXD loopback bits           */
#define BMCR_RESET              0x8000  /* Reset the DP83840           */

/* BMSR bits */
#define BMSR_ERCAP              0x0001  /* Ext-reg capability          */
#define BMSR_JCD                0x0002  /* Jabber detected             */
#define BMSR_LSTATUS            0x0004  /* Link status                 */

/* Ring descriptors and such, same as Quad Ethernet. */
struct be_rxd {
	u32 rx_flags;
	u32 rx_addr;
};

#define RXD_OWN      0x80000000 /* Ownership.      */
#define RXD_UPDATE   0x10000000 /* Being Updated?  */
#define RXD_LENGTH   0x000007ff /* Packet Length.  */

struct be_txd {
	u32 tx_flags;
	u32 tx_addr;
};

#define TXD_OWN      0x80000000 /* Ownership.      */
#define TXD_SOP      0x40000000 /* Start Of Packet */
#define TXD_EOP      0x20000000 /* End Of Packet   */
#define TXD_UPDATE   0x10000000 /* Being Updated?  */
#define TXD_LENGTH   0x000007ff /* Packet Length.  */

#define TX_RING_MAXSIZE   256
#define RX_RING_MAXSIZE   256

#define TX_RING_SIZE      256
#define RX_RING_SIZE      256

#define NEXT_RX(num)       (((num) + 1) & (RX_RING_SIZE - 1))
#define NEXT_TX(num)       (((num) + 1) & (TX_RING_SIZE - 1))
#define PREV_RX(num)       (((num) - 1) & (RX_RING_SIZE - 1))
#define PREV_TX(num)       (((num) - 1) & (TX_RING_SIZE - 1))

#define TX_BUFFS_AVAIL(bp)                                    \
        (((bp)->tx_old <= (bp)->tx_new) ?                     \
	  (bp)->tx_old + (TX_RING_SIZE - 1) - (bp)->tx_new :  \
			    (bp)->tx_old - (bp)->tx_new - 1)


#define RX_COPY_THRESHOLD  256
#define RX_BUF_ALLOC_SIZE  (ETH_FRAME_LEN + (64 * 3))

struct bmac_init_block {
	struct be_rxd be_rxd[RX_RING_MAXSIZE];
	struct be_txd be_txd[TX_RING_MAXSIZE];
};

#define bib_offset(mem, elem) \
((__u32)((unsigned long)(&(((struct bmac_init_block *)0)->mem[elem]))))

/* Now software state stuff. */
enum bigmac_transceiver {
	external = 0,
	internal = 1,
	none     = 2,
};

/* Timer state engine. */
enum bigmac_timer_state {
	ltrywait = 1,  /* Forcing try of all modes, from fastest to slowest. */
	asleep   = 2,  /* Timer inactive.                                    */
};

struct bigmac {
	void __iomem	*gregs;	/* QEC Global Registers               */
	void __iomem	*creg;	/* QEC BigMAC Channel Registers       */
	void __iomem	*bregs;	/* BigMAC Registers                   */
	void __iomem	*tregs;	/* BigMAC Transceiver                 */
	struct bmac_init_block	*bmac_block;	/* RX and TX descriptors */
	__u32			 bblock_dvma;	/* RX and TX descriptors */

	spinlock_t		lock;

	struct sk_buff		*rx_skbs[RX_RING_SIZE];
	struct sk_buff		*tx_skbs[TX_RING_SIZE];

	int rx_new, tx_new, rx_old, tx_old;

	int board_rev;				/* BigMAC board revision.             */

	enum bigmac_transceiver	tcvr_type;
	unsigned int		bigmac_bursts;
	unsigned int		paddr;
	unsigned short		sw_bmsr;         /* SW copy of PHY BMSR               */
	unsigned short		sw_bmcr;         /* SW copy of PHY BMCR               */
	struct timer_list	bigmac_timer;
	enum bigmac_timer_state	timer_state;
	unsigned int		timer_ticks;

	struct net_device_stats	enet_stats;
	struct platform_device	*qec_op;
	struct platform_device	*bigmac_op;
	struct net_device	*dev;
};

/* We use this to acquire receive skb's that we can DMA directly into. */
#define ALIGNED_RX_SKB_ADDR(addr) \
        ((((unsigned long)(addr) + (64 - 1)) & ~(64 - 1)) - (unsigned long)(addr))

static inline struct sk_buff *big_mac_alloc_skb(unsigned int length, gfp_t gfp_flags)
{
	struct sk_buff *skb;

	skb = alloc_skb(length + 64, gfp_flags);
	if(skb) {
		int offset = ALIGNED_RX_SKB_ADDR(skb->data);

		if(offset)
			skb_reserve(skb, offset);
	}
	return skb;
}

#endif /* !(_SUNBMAC_H) */
