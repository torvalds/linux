/* $Id: sunqe.h,v 1.13 2000/02/09 11:15:42 davem Exp $
 * sunqe.h: Definitions for the Sun QuadEthernet driver.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SUNQE_H
#define _SUNQE_H

/* QEC global registers. */
#define GLOB_CTRL	0x00UL		/* Control			*/
#define GLOB_STAT	0x04UL		/* Status			*/
#define GLOB_PSIZE	0x08UL		/* Packet Size			*/
#define GLOB_MSIZE	0x0cUL		/* Local-memory Size		*/
#define GLOB_RSIZE	0x10UL		/* Receive partition size	*/
#define GLOB_TSIZE	0x14UL		/* Transmit partition size	*/
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

/* In MACE mode, there are four qe channels.  Each channel has it's own
 * status bits in the QEC status register.  This macro picks out the
 * ones you want.
 */
#define GLOB_STAT_PER_QE(status, channel) (((status) >> ((channel) * 4)) & 0xf)

/* The following registers are for per-qe channel information/status. */
#define CREG_CTRL	0x00UL	/* Control                   */
#define CREG_STAT	0x04UL	/* Status                    */
#define CREG_RXDS	0x08UL	/* RX descriptor ring ptr    */
#define CREG_TXDS	0x0cUL	/* TX descriptor ring ptr    */
#define CREG_RIMASK	0x10UL	/* RX Interrupt Mask         */
#define CREG_TIMASK	0x14UL	/* TX Interrupt Mask         */
#define CREG_QMASK	0x18UL	/* QEC Error Interrupt Mask  */
#define CREG_MMASK	0x1cUL	/* MACE Error Interrupt Mask */
#define CREG_RXWBUFPTR	0x20UL	/* Local memory rx write ptr */
#define CREG_RXRBUFPTR	0x24UL	/* Local memory rx read ptr  */
#define CREG_TXWBUFPTR	0x28UL	/* Local memory tx write ptr */
#define CREG_TXRBUFPTR	0x2cUL	/* Local memory tx read ptr  */
#define CREG_CCNT	0x30UL	/* Collision Counter         */
#define CREG_PIPG	0x34UL	/* Inter-Frame Gap           */
#define CREG_REG_SIZE	0x38UL

#define CREG_CTRL_RXOFF       0x00000004  /* Disable this qe's receiver*/
#define CREG_CTRL_RESET       0x00000002  /* Reset this qe channel     */
#define CREG_CTRL_TWAKEUP     0x00000001  /* Transmitter Wakeup, 'go'. */

#define CREG_STAT_EDEFER      0x10000000  /* Excessive Defers          */
#define CREG_STAT_CLOSS       0x08000000  /* Carrier Loss              */
#define CREG_STAT_ERETRIES    0x04000000  /* More than 16 retries      */
#define CREG_STAT_LCOLL       0x02000000  /* Late TX Collision         */
#define CREG_STAT_FUFLOW      0x01000000  /* FIFO Underflow            */
#define CREG_STAT_JERROR      0x00800000  /* Jabber Error              */
#define CREG_STAT_BERROR      0x00400000  /* Babble Error              */
#define CREG_STAT_TXIRQ       0x00200000  /* Transmit Interrupt        */
#define CREG_STAT_CCOFLOW     0x00100000  /* TX Coll-counter Overflow  */
#define CREG_STAT_TXDERROR    0x00080000  /* TX Descriptor is bogus    */
#define CREG_STAT_TXLERR      0x00040000  /* Late Transmit Error       */
#define CREG_STAT_TXPERR      0x00020000  /* Transmit Parity Error     */
#define CREG_STAT_TXSERR      0x00010000  /* Transmit SBUS error ack   */
#define CREG_STAT_RCCOFLOW    0x00001000  /* RX Coll-counter Overflow  */
#define CREG_STAT_RUOFLOW     0x00000800  /* Runt Counter Overflow     */
#define CREG_STAT_MCOFLOW     0x00000400  /* Missed Counter Overflow   */
#define CREG_STAT_RXFOFLOW    0x00000200  /* RX FIFO Overflow          */
#define CREG_STAT_RLCOLL      0x00000100  /* RX Late Collision         */
#define CREG_STAT_FCOFLOW     0x00000080  /* Frame Counter Overflow    */
#define CREG_STAT_CECOFLOW    0x00000040  /* CRC Error-counter Overflow*/
#define CREG_STAT_RXIRQ       0x00000020  /* Receive Interrupt         */
#define CREG_STAT_RXDROP      0x00000010  /* Dropped a RX'd packet     */
#define CREG_STAT_RXSMALL     0x00000008  /* Receive buffer too small  */
#define CREG_STAT_RXLERR      0x00000004  /* Receive Late Error        */
#define CREG_STAT_RXPERR      0x00000002  /* Receive Parity Error      */
#define CREG_STAT_RXSERR      0x00000001  /* Receive SBUS Error ACK    */

#define CREG_STAT_ERRORS      (CREG_STAT_EDEFER|CREG_STAT_CLOSS|CREG_STAT_ERETRIES|     \
			       CREG_STAT_LCOLL|CREG_STAT_FUFLOW|CREG_STAT_JERROR|       \
			       CREG_STAT_BERROR|CREG_STAT_CCOFLOW|CREG_STAT_TXDERROR|   \
			       CREG_STAT_TXLERR|CREG_STAT_TXPERR|CREG_STAT_TXSERR|      \
			       CREG_STAT_RCCOFLOW|CREG_STAT_RUOFLOW|CREG_STAT_MCOFLOW| \
			       CREG_STAT_RXFOFLOW|CREG_STAT_RLCOLL|CREG_STAT_FCOFLOW|   \
			       CREG_STAT_CECOFLOW|CREG_STAT_RXDROP|CREG_STAT_RXSMALL|   \
			       CREG_STAT_RXLERR|CREG_STAT_RXPERR|CREG_STAT_RXSERR)

#define CREG_QMASK_COFLOW     0x00100000  /* CollCntr overflow         */
#define CREG_QMASK_TXDERROR   0x00080000  /* TXD error                 */
#define CREG_QMASK_TXLERR     0x00040000  /* TX late error             */
#define CREG_QMASK_TXPERR     0x00020000  /* TX parity error           */
#define CREG_QMASK_TXSERR     0x00010000  /* TX sbus error ack         */
#define CREG_QMASK_RXDROP     0x00000010  /* RX drop                   */
#define CREG_QMASK_RXBERROR   0x00000008  /* RX buffer error           */
#define CREG_QMASK_RXLEERR    0x00000004  /* RX late error             */
#define CREG_QMASK_RXPERR     0x00000002  /* RX parity error           */
#define CREG_QMASK_RXSERR     0x00000001  /* RX sbus error ack         */

#define CREG_MMASK_EDEFER     0x10000000  /* Excess defer              */
#define CREG_MMASK_CLOSS      0x08000000  /* Carrier loss              */
#define CREG_MMASK_ERETRY     0x04000000  /* Excess retry              */
#define CREG_MMASK_LCOLL      0x02000000  /* Late collision error      */
#define CREG_MMASK_UFLOW      0x01000000  /* Underflow                 */
#define CREG_MMASK_JABBER     0x00800000  /* Jabber error              */
#define CREG_MMASK_BABBLE     0x00400000  /* Babble error              */
#define CREG_MMASK_OFLOW      0x00000800  /* Overflow                  */
#define CREG_MMASK_RXCOLL     0x00000400  /* RX Coll-Cntr overflow     */
#define CREG_MMASK_RPKT       0x00000200  /* Runt pkt overflow         */
#define CREG_MMASK_MPKT       0x00000100  /* Missed pkt overflow       */

#define CREG_PIPG_TENAB       0x00000020  /* Enable Throttle           */
#define CREG_PIPG_MMODE       0x00000010  /* Manual Mode               */
#define CREG_PIPG_WMASK       0x0000000f  /* SBUS Wait Mask            */

/* Per-channel AMD 79C940 MACE registers. */
#define MREGS_RXFIFO	0x00UL	/* Receive FIFO                   */
#define MREGS_TXFIFO	0x01UL	/* Transmit FIFO                  */
#define MREGS_TXFCNTL	0x02UL	/* Transmit Frame Control         */
#define MREGS_TXFSTAT	0x03UL	/* Transmit Frame Status          */
#define MREGS_TXRCNT	0x04UL	/* Transmit Retry Count           */
#define MREGS_RXFCNTL	0x05UL	/* Receive Frame Control          */
#define MREGS_RXFSTAT	0x06UL	/* Receive Frame Status           */
#define MREGS_FFCNT	0x07UL	/* FIFO Frame Count               */
#define MREGS_IREG	0x08UL	/* Interrupt Register             */
#define MREGS_IMASK	0x09UL	/* Interrupt Mask                 */
#define MREGS_POLL	0x0aUL	/* POLL Register                  */
#define MREGS_BCONFIG	0x0bUL	/* BIU Config                     */
#define MREGS_FCONFIG	0x0cUL	/* FIFO Config                    */
#define MREGS_MCONFIG	0x0dUL	/* MAC Config                     */
#define MREGS_PLSCONFIG	0x0eUL	/* PLS Config                     */
#define MREGS_PHYCONFIG	0x0fUL	/* PHY Config                     */
#define MREGS_CHIPID1	0x10UL	/* Chip-ID, low bits              */
#define MREGS_CHIPID2	0x11UL	/* Chip-ID, high bits             */
#define MREGS_IACONFIG	0x12UL	/* Internal Address Config        */
	/* 0x13UL, reserved */
#define MREGS_FILTER	0x14UL	/* Logical Address Filter         */
#define MREGS_ETHADDR	0x15UL	/* Our Ethernet Address           */
	/* 0x16UL, reserved */
	/* 0x17UL, reserved */
#define MREGS_MPCNT	0x18UL	/* Missed Packet Count            */
	/* 0x19UL, reserved */
#define MREGS_RPCNT	0x1aUL	/* Runt Packet Count              */
#define MREGS_RCCNT	0x1bUL	/* RX Collision Count             */
	/* 0x1cUL, reserved */
#define MREGS_UTEST	0x1dUL	/* User Test                      */
#define MREGS_RTEST1	0x1eUL	/* Reserved Test 1                */
#define MREGS_RTEST2	0x1fUL	/* Reserved Test 2                */
#define MREGS_REG_SIZE	0x20UL

#define MREGS_TXFCNTL_DRETRY        0x80 /* Retry disable                  */
#define MREGS_TXFCNTL_DFCS          0x08 /* Disable TX FCS                 */
#define MREGS_TXFCNTL_AUTOPAD       0x01 /* TX auto pad                    */

#define MREGS_TXFSTAT_VALID         0x80 /* TX valid                       */
#define MREGS_TXFSTAT_UNDERFLOW     0x40 /* TX underflow                   */
#define MREGS_TXFSTAT_LCOLL         0x20 /* TX late collision              */
#define MREGS_TXFSTAT_MRETRY        0x10 /* TX > 1 retries                 */
#define MREGS_TXFSTAT_ORETRY        0x08 /* TX 1 retry                     */
#define MREGS_TXFSTAT_PDEFER        0x04 /* TX pkt deferred                */
#define MREGS_TXFSTAT_CLOSS         0x02 /* TX carrier lost                */
#define MREGS_TXFSTAT_RERROR        0x01 /* TX retry error                 */

#define MREGS_TXRCNT_EDEFER         0x80 /* TX Excess defers               */
#define MREGS_TXRCNT_CMASK          0x0f /* TX retry count                 */

#define MREGS_RXFCNTL_LOWLAT        0x08 /* RX low latency                 */
#define MREGS_RXFCNTL_AREJECT       0x04 /* RX addr match rej              */
#define MREGS_RXFCNTL_AUTOSTRIP     0x01 /* RX auto strip                  */

#define MREGS_RXFSTAT_OVERFLOW      0x80 /* RX overflow                    */
#define MREGS_RXFSTAT_LCOLL         0x40 /* RX late collision              */
#define MREGS_RXFSTAT_FERROR        0x20 /* RX framing error               */
#define MREGS_RXFSTAT_FCSERROR      0x10 /* RX FCS error                   */
#define MREGS_RXFSTAT_RBCNT         0x0f /* RX msg byte count              */

#define MREGS_FFCNT_RX              0xf0 /* RX FIFO frame cnt              */
#define MREGS_FFCNT_TX              0x0f /* TX FIFO frame cnt              */

#define MREGS_IREG_JABBER           0x80 /* IRQ Jabber error               */
#define MREGS_IREG_BABBLE           0x40 /* IRQ Babble error               */
#define MREGS_IREG_COLL             0x20 /* IRQ Collision error            */
#define MREGS_IREG_RCCO             0x10 /* IRQ Collision cnt overflow     */
#define MREGS_IREG_RPKTCO           0x08 /* IRQ Runt packet count overflow */
#define MREGS_IREG_MPKTCO           0x04 /* IRQ missed packet cnt overflow */
#define MREGS_IREG_RXIRQ            0x02 /* IRQ RX'd a packet              */
#define MREGS_IREG_TXIRQ            0x01 /* IRQ TX'd a packet              */

#define MREGS_IMASK_BABBLE          0x40 /* IMASK Babble errors            */
#define MREGS_IMASK_COLL            0x20 /* IMASK Collision errors         */
#define MREGS_IMASK_MPKTCO          0x04 /* IMASK Missed pkt cnt overflow  */
#define MREGS_IMASK_RXIRQ           0x02 /* IMASK RX interrupts            */
#define MREGS_IMASK_TXIRQ           0x01 /* IMASK TX interrupts            */

#define MREGS_POLL_TXVALID          0x80 /* TX is valid                    */
#define MREGS_POLL_TDTR             0x40 /* TX data transfer request       */
#define MREGS_POLL_RDTR             0x20 /* RX data transfer request       */

#define MREGS_BCONFIG_BSWAP         0x40 /* Byte Swap                      */
#define MREGS_BCONFIG_4TS           0x00 /* 4byte transmit start point     */
#define MREGS_BCONFIG_16TS          0x10 /* 16byte transmit start point    */
#define MREGS_BCONFIG_64TS          0x20 /* 64byte transmit start point    */
#define MREGS_BCONFIG_112TS         0x30 /* 112byte transmit start point   */
#define MREGS_BCONFIG_RESET         0x01 /* SW-Reset the MACE              */

#define MREGS_FCONFIG_TXF8          0x00 /* TX fifo 8 write cycles         */
#define MREGS_FCONFIG_TXF32         0x80 /* TX fifo 32 write cycles        */
#define MREGS_FCONFIG_TXF16         0x40 /* TX fifo 16 write cycles        */
#define MREGS_FCONFIG_RXF64         0x20 /* RX fifo 64 write cycles        */
#define MREGS_FCONFIG_RXF32         0x10 /* RX fifo 32 write cycles        */
#define MREGS_FCONFIG_RXF16         0x00 /* RX fifo 16 write cycles        */
#define MREGS_FCONFIG_TFWU          0x08 /* TX fifo watermark update       */
#define MREGS_FCONFIG_RFWU          0x04 /* RX fifo watermark update       */
#define MREGS_FCONFIG_TBENAB        0x02 /* TX burst enable                */
#define MREGS_FCONFIG_RBENAB        0x01 /* RX burst enable                */

#define MREGS_MCONFIG_PROMISC       0x80 /* Promiscuous mode enable        */
#define MREGS_MCONFIG_TPDDISAB      0x40 /* TX 2part deferral enable       */
#define MREGS_MCONFIG_MBAENAB       0x20 /* Modified backoff enable        */
#define MREGS_MCONFIG_RPADISAB      0x08 /* RX physical addr disable       */
#define MREGS_MCONFIG_RBDISAB       0x04 /* RX broadcast disable           */
#define MREGS_MCONFIG_TXENAB        0x02 /* Enable transmitter             */
#define MREGS_MCONFIG_RXENAB        0x01 /* Enable receiver                */

#define MREGS_PLSCONFIG_TXMS        0x08 /* TX mode select                 */
#define MREGS_PLSCONFIG_GPSI        0x06 /* Use GPSI connector             */
#define MREGS_PLSCONFIG_DAI         0x04 /* Use DAI connector              */
#define MREGS_PLSCONFIG_TP          0x02 /* Use TwistedPair connector      */
#define MREGS_PLSCONFIG_AUI         0x00 /* Use AUI connector              */
#define MREGS_PLSCONFIG_IOENAB      0x01 /* PLS I/O enable                 */

#define MREGS_PHYCONFIG_LSTAT       0x80 /* Link status                    */
#define MREGS_PHYCONFIG_LTESTDIS    0x40 /* Disable link test logic        */
#define MREGS_PHYCONFIG_RXPOLARITY  0x20 /* RX polarity                    */
#define MREGS_PHYCONFIG_APCDISAB    0x10 /* AutoPolarityCorrect disab      */
#define MREGS_PHYCONFIG_LTENAB      0x08 /* Select low threshold           */
#define MREGS_PHYCONFIG_AUTO        0x04 /* Connector port auto-sel        */
#define MREGS_PHYCONFIG_RWU         0x02 /* Remote WakeUp                  */
#define MREGS_PHYCONFIG_AW          0x01 /* Auto Wakeup                    */

#define MREGS_IACONFIG_ACHNGE       0x80 /* Do address change              */
#define MREGS_IACONFIG_PARESET      0x04 /* Physical address reset         */
#define MREGS_IACONFIG_LARESET      0x02 /* Logical address reset          */

#define MREGS_UTEST_RTRENAB         0x80 /* Enable resv test register      */
#define MREGS_UTEST_RTRDISAB        0x40 /* Disab resv test register       */
#define MREGS_UTEST_RPACCEPT        0x20 /* Accept runt packets            */
#define MREGS_UTEST_FCOLL           0x10 /* Force collision status         */
#define MREGS_UTEST_FCSENAB         0x08 /* Enable FCS on RX               */
#define MREGS_UTEST_INTLOOPM        0x06 /* Intern lpback w/MENDEC         */
#define MREGS_UTEST_INTLOOP         0x04 /* Intern lpback                  */
#define MREGS_UTEST_EXTLOOP         0x02 /* Extern lpback                  */
#define MREGS_UTEST_NOLOOP          0x00 /* No loopback                    */

struct qe_rxd {
	u32 rx_flags;
	u32 rx_addr;
};

#define RXD_OWN      0x80000000 /* Ownership.      */
#define RXD_UPDATE   0x10000000 /* Being Updated?  */
#define RXD_LENGTH   0x000007ff /* Packet Length.  */

struct qe_txd {
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

#define TX_RING_SIZE      16
#define RX_RING_SIZE      16

#define NEXT_RX(num)       (((num) + 1) & (RX_RING_MAXSIZE - 1))
#define NEXT_TX(num)       (((num) + 1) & (TX_RING_MAXSIZE - 1))
#define PREV_RX(num)       (((num) - 1) & (RX_RING_MAXSIZE - 1))
#define PREV_TX(num)       (((num) - 1) & (TX_RING_MAXSIZE - 1))

#define TX_BUFFS_AVAIL(qp)                                    \
        (((qp)->tx_old <= (qp)->tx_new) ?                     \
	  (qp)->tx_old + (TX_RING_SIZE - 1) - (qp)->tx_new :  \
			    (qp)->tx_old - (qp)->tx_new - 1)

struct qe_init_block {
	struct qe_rxd qe_rxd[RX_RING_MAXSIZE];
	struct qe_txd qe_txd[TX_RING_MAXSIZE];
};

#define qib_offset(mem, elem) \
((__u32)((unsigned long)(&(((struct qe_init_block *)0)->mem[elem]))))

struct sunqe;

struct sunqec {
	void __iomem		*gregs;		/* QEC Global Registers         */
	struct sunqe		*qes[4];	/* Each child MACE              */
	unsigned int            qec_bursts;	/* Support burst sizes          */
	struct sbus_dev		*qec_sdev;	/* QEC's SBUS device            */
	struct sunqec		*next_module;	/* List of all QECs in system   */
};

#define PKT_BUF_SZ	1664
#define RXD_PKT_SZ	1664

struct sunqe_buffers {
	u8	tx_buf[TX_RING_SIZE][PKT_BUF_SZ];
	u8	__pad[2];
	u8	rx_buf[RX_RING_SIZE][PKT_BUF_SZ];
};

#define qebuf_offset(mem, elem) \
((__u32)((unsigned long)(&(((struct sunqe_buffers *)0)->mem[elem][0]))))

struct sunqe {
	void __iomem			*qcregs;		/* QEC per-channel Registers   */
	void __iomem			*mregs;		/* Per-channel MACE Registers  */
	struct qe_init_block      	*qe_block;	/* RX and TX descriptors       */
	__u32                      	qblock_dvma;	/* RX and TX descriptors       */
	spinlock_t			lock;		/* Protects txfull state       */
	int                        	rx_new, rx_old;	/* RX ring extents	       */
	int			   	tx_new, tx_old;	/* TX ring extents	       */
	struct sunqe_buffers		*buffers;	/* CPU visible address.        */
	__u32				buffers_dvma;	/* DVMA visible address.       */
	struct sunqec			*parent;
	u8				mconfig;	/* Base MACE mconfig value     */
	struct sbus_dev			*qe_sdev;	/* QE's SBUS device struct     */
	struct net_device		*dev;		/* QE's netdevice struct       */
	int				channel;	/* Who am I?                   */
};

#endif /* !(_SUNQE_H) */
