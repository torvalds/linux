/*
 * PKUnity Ultra Media Access Layer (UMAL) Ethernet MAC Registers
 */

/* MAC module of UMAL */
/* UMAL's MAC module includes G/MII interface, several additional PHY
 * interfaces, and MAC control sub-layer, which provides support for control
 * frames (e.g. PAUSE frames).
 */
/*
 * TX/RX reset and control UMAL_CFG1
 */
#define UMAL_CFG1		(PKUNITY_UMAL_BASE + 0x0000)
/*
 * MAC interface mode control UMAL_CFG2
 */
#define UMAL_CFG2		(PKUNITY_UMAL_BASE + 0x0004)
/*
 * Inter Packet/Frame Gap UMAL_IPGIFG
 */
#define UMAL_IPGIFG		(PKUNITY_UMAL_BASE + 0x0008)
/*
 * Collision retry or backoff UMAL_HALFDUPLEX
 */
#define UMAL_HALFDUPLEX		(PKUNITY_UMAL_BASE + 0x000c)
/*
 * Maximum Frame Length UMAL_MAXFRAME
 */
#define UMAL_MAXFRAME		(PKUNITY_UMAL_BASE + 0x0010)
/*
 * Test Regsiter UMAL_TESTREG
 */
#define UMAL_TESTREG		(PKUNITY_UMAL_BASE + 0x001c)
/*
 * MII Management Configure UMAL_MIICFG
 */
#define UMAL_MIICFG		(PKUNITY_UMAL_BASE + 0x0020)
/*
 * MII Management Command UMAL_MIICMD
 */
#define UMAL_MIICMD		(PKUNITY_UMAL_BASE + 0x0024)
/*
 * MII Management Address UMAL_MIIADDR
 */
#define UMAL_MIIADDR		(PKUNITY_UMAL_BASE + 0x0028)
/*
 * MII Management Control UMAL_MIICTRL
 */
#define UMAL_MIICTRL		(PKUNITY_UMAL_BASE + 0x002c)
/*
 * MII Management Status UMAL_MIISTATUS
 */
#define UMAL_MIISTATUS		(PKUNITY_UMAL_BASE + 0x0030)
/*
 * MII Management Indicator UMAL_MIIIDCT
 */
#define UMAL_MIIIDCT		(PKUNITY_UMAL_BASE + 0x0034)
/*
 * Interface Control UMAL_IFCTRL
 */
#define UMAL_IFCTRL		(PKUNITY_UMAL_BASE + 0x0038)
/*
 * Interface Status UMAL_IFSTATUS
 */
#define UMAL_IFSTATUS		(PKUNITY_UMAL_BASE + 0x003c)
/*
 * MAC address (high 4 bytes) UMAL_STADDR1
 */
#define UMAL_STADDR1		(PKUNITY_UMAL_BASE + 0x0040)
/*
 * MAC address (low 2 bytes) UMAL_STADDR2
 */
#define UMAL_STADDR2		(PKUNITY_UMAL_BASE + 0x0044)

/* FIFO MODULE OF UMAL */
/* UMAL's FIFO module provides data queuing for increased system level
 * throughput
 */
#define UMAL_FIFOCFG0		(PKUNITY_UMAL_BASE + 0x0048)
#define UMAL_FIFOCFG1		(PKUNITY_UMAL_BASE + 0x004c)
#define UMAL_FIFOCFG2		(PKUNITY_UMAL_BASE + 0x0050)
#define UMAL_FIFOCFG3		(PKUNITY_UMAL_BASE + 0x0054)
#define UMAL_FIFOCFG4		(PKUNITY_UMAL_BASE + 0x0058)
#define UMAL_FIFOCFG5		(PKUNITY_UMAL_BASE + 0x005c)
#define UMAL_FIFORAM0		(PKUNITY_UMAL_BASE + 0x0060)
#define UMAL_FIFORAM1		(PKUNITY_UMAL_BASE + 0x0064)
#define UMAL_FIFORAM2		(PKUNITY_UMAL_BASE + 0x0068)
#define UMAL_FIFORAM3		(PKUNITY_UMAL_BASE + 0x006c)
#define UMAL_FIFORAM4		(PKUNITY_UMAL_BASE + 0x0070)
#define UMAL_FIFORAM5		(PKUNITY_UMAL_BASE + 0x0074)
#define UMAL_FIFORAM6		(PKUNITY_UMAL_BASE + 0x0078)
#define UMAL_FIFORAM7		(PKUNITY_UMAL_BASE + 0x007c)

/* MAHBE MODULE OF UMAL */
/* UMAL's MAHBE module interfaces to the host system through 32-bit AHB Master
 * and Slave ports.Registers within the M-AHBE provide Control and Status
 * information concerning these transfers.
 */
/*
 * Transmit Control UMAL_DMATxCtrl
 */
#define UMAL_DMATxCtrl		(PKUNITY_UMAL_BASE + 0x0180)
/*
 * Pointer to TX Descripter UMAL_DMATxDescriptor
 */
#define UMAL_DMATxDescriptor	(PKUNITY_UMAL_BASE + 0x0184)
/*
 * Status of Tx Packet Transfers UMAL_DMATxStatus
 */
#define UMAL_DMATxStatus	(PKUNITY_UMAL_BASE + 0x0188)
/*
 * Receive Control UMAL_DMARxCtrl
 */
#define UMAL_DMARxCtrl		(PKUNITY_UMAL_BASE + 0x018c)
/*
 * Pointer to Rx Descriptor UMAL_DMARxDescriptor
 */
#define UMAL_DMARxDescriptor	(PKUNITY_UMAL_BASE + 0x0190)
/*
 * Status of Rx Packet Transfers UMAL_DMARxStatus
 */
#define UMAL_DMARxStatus	(PKUNITY_UMAL_BASE + 0x0194)
/*
 * Interrupt Mask UMAL_DMAIntrMask
 */
#define UMAL_DMAIntrMask	(PKUNITY_UMAL_BASE + 0x0198)
/*
 * Interrupts, read only UMAL_DMAInterrupt
 */
#define UMAL_DMAInterrupt	(PKUNITY_UMAL_BASE + 0x019c)

/*
 * Commands for UMAL_CFG1 register
 */
#define UMAL_CFG1_TXENABLE	FIELD(1, 1, 0)
#define UMAL_CFG1_RXENABLE	FIELD(1, 1, 2)
#define UMAL_CFG1_TXFLOWCTL	FIELD(1, 1, 4)
#define UMAL_CFG1_RXFLOWCTL	FIELD(1, 1, 5)
#define UMAL_CFG1_CONFLPBK	FIELD(1, 1, 8)
#define UMAL_CFG1_RESET		FIELD(1, 1, 31)
#define UMAL_CFG1_CONFFLCTL	(MAC_TX_FLOW_CTL | MAC_RX_FLOW_CTL)

/*
 * Commands for UMAL_CFG2 register
 */
#define UMAL_CFG2_FULLDUPLEX	FIELD(1, 1, 0)
#define UMAL_CFG2_CRCENABLE	FIELD(1, 1, 1)
#define UMAL_CFG2_PADCRC	FIELD(1, 1, 2)
#define UMAL_CFG2_LENGTHCHECK	FIELD(1, 1, 4)
#define UMAL_CFG2_MODEMASK	FMASK(2, 8)
#define UMAL_CFG2_NIBBLEMODE	FIELD(1, 2, 8)
#define UMAL_CFG2_BYTEMODE	FIELD(2, 2, 8)
#define UMAL_CFG2_PREAMBLENMASK	FMASK(4, 12)
#define UMAL_CFG2_DEFPREAMBLEN	FIELD(7, 4, 12)
#define UMAL_CFG2_FD100		(UMAL_CFG2_DEFPREAMBLEN | UMAL_CFG2_NIBBLEMODE \
				| UMAL_CFG2_LENGTHCHECK | UMAL_CFG2_PADCRC \
				| UMAL_CFG2_CRCENABLE | UMAL_CFG2_FULLDUPLEX)
#define UMAL_CFG2_FD1000	(UMAL_CFG2_DEFPREAMBLEN | UMAL_CFG2_BYTEMODE \
				| UMAL_CFG2_LENGTHCHECK | UMAL_CFG2_PADCRC \
				| UMAL_CFG2_CRCENABLE | UMAL_CFG2_FULLDUPLEX)
#define UMAL_CFG2_HD100		(UMAL_CFG2_DEFPREAMBLEN | UMAL_CFG2_NIBBLEMODE \
				| UMAL_CFG2_LENGTHCHECK | UMAL_CFG2_PADCRC \
				| UMAL_CFG2_CRCENABLE)

/*
 * Command for UMAL_IFCTRL register
 */
#define UMAL_IFCTRL_RESET	FIELD(1, 1, 31)

/*
 * Command for UMAL_MIICFG register
 */
#define UMAL_MIICFG_RESET	FIELD(1, 1, 31)

/*
 * Command for UMAL_MIICMD register
 */
#define UMAL_MIICMD_READ	FIELD(1, 1, 0)

/*
 * Command for UMAL_MIIIDCT register
 */
#define UMAL_MIIIDCT_BUSY	FIELD(1, 1, 0)
#define UMAL_MIIIDCT_NOTVALID	FIELD(1, 1, 2)

/*
 * Commands for DMATxCtrl regesters
 */
#define UMAL_DMA_Enable		FIELD(1, 1, 0)

/*
 * Commands for DMARxCtrl regesters
 */
#define UMAL_DMAIntrMask_ENABLEHALFWORD	FIELD(1, 1, 16)

/*
 * Command for DMARxStatus
 */
#define CLR_RX_BUS_ERR		FIELD(1, 1, 3)
#define CLR_RX_OVERFLOW		FIELD(1, 1, 2)
#define CLR_RX_PKT		FIELD(1, 1, 0)

/*
 * Command for DMATxStatus
 */
#define CLR_TX_BUS_ERR		FIELD(1, 1, 3)
#define CLR_TX_UNDERRUN		FIELD(1, 1, 1)
#define CLR_TX_PKT		FIELD(1, 1, 0)

/*
 * Commands for DMAIntrMask and DMAInterrupt register
 */
#define INT_RX_MASK		FIELD(0xd, 4, 4)
#define INT_TX_MASK		FIELD(0xb, 4, 0)

#define INT_RX_BUS_ERR		FIELD(1, 1, 7)
#define INT_RX_OVERFLOW		FIELD(1, 1, 6)
#define INT_RX_PKT		FIELD(1, 1, 4)
#define INT_TX_BUS_ERR		FIELD(1, 1, 3)
#define INT_TX_UNDERRUN		FIELD(1, 1, 1)
#define INT_TX_PKT		FIELD(1, 1, 0)

/*
 * MARCOS of UMAL's descriptors
 */
#define UMAL_DESC_PACKETSIZE_EMPTY	FIELD(1, 1, 31)
#define UMAL_DESC_PACKETSIZE_NONEMPTY	FIELD(0, 1, 31)
#define UMAL_DESC_PACKETSIZE_SIZEMASK	FMASK(12, 0)

