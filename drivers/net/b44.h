#ifndef _B44_H
#define _B44_H

/* Register layout. (These correspond to struct _bcmenettregs in bcm4400.) */
#define	B44_DEVCTRL	0x0000UL /* Device Control */
#define  DEVCTRL_MPM		0x00000040 /* Magic Packet PME Enable (B0 only) */
#define  DEVCTRL_PFE		0x00000080 /* Pattern Filtering Enable */
#define  DEVCTRL_IPP		0x00000400 /* Internal EPHY Present */
#define  DEVCTRL_EPR		0x00008000 /* EPHY Reset */
#define  DEVCTRL_PME		0x00001000 /* PHY Mode Enable */
#define  DEVCTRL_PMCE		0x00002000 /* PHY Mode Clocks Enable */
#define  DEVCTRL_PADDR		0x0007c000 /* PHY Address */
#define  DEVCTRL_PADDR_SHIFT	18
#define B44_BIST_STAT	0x000CUL /* Built-In Self-Test Status */
#define B44_WKUP_LEN	0x0010UL /* Wakeup Length */
#define  WKUP_LEN_P0_MASK	0x0000007f /* Pattern 0 */
#define  WKUP_LEN_D0		0x00000080
#define  WKUP_LEN_P1_MASK	0x00007f00 /* Pattern 1 */
#define  WKUP_LEN_P1_SHIFT	8
#define  WKUP_LEN_D1		0x00008000
#define  WKUP_LEN_P2_MASK	0x007f0000 /* Pattern 2 */
#define  WKUP_LEN_P2_SHIFT	16
#define  WKUP_LEN_D2		0x00000000
#define  WKUP_LEN_P3_MASK	0x7f000000 /* Pattern 3 */
#define  WKUP_LEN_P3_SHIFT	24
#define  WKUP_LEN_D3		0x80000000
#define  WKUP_LEN_DISABLE	0x80808080
#define  WKUP_LEN_ENABLE_TWO	0x80800000
#define  WKUP_LEN_ENABLE_THREE	0x80000000
#define B44_ISTAT	0x0020UL /* Interrupt Status */
#define  ISTAT_LS		0x00000020 /* Link Change (B0 only) */
#define  ISTAT_PME		0x00000040 /* Power Management Event */
#define  ISTAT_TO		0x00000080 /* General Purpose Timeout */
#define  ISTAT_DSCE		0x00000400 /* Descriptor Error */
#define  ISTAT_DATAE		0x00000800 /* Data Error */
#define  ISTAT_DPE		0x00001000 /* Descr. Protocol Error */
#define  ISTAT_RDU		0x00002000 /* Receive Descr. Underflow */
#define  ISTAT_RFO		0x00004000 /* Receive FIFO Overflow */
#define  ISTAT_TFU		0x00008000 /* Transmit FIFO Underflow */
#define  ISTAT_RX		0x00010000 /* RX Interrupt */
#define  ISTAT_TX		0x01000000 /* TX Interrupt */
#define  ISTAT_EMAC		0x04000000 /* EMAC Interrupt */
#define  ISTAT_MII_WRITE	0x08000000 /* MII Write Interrupt */
#define  ISTAT_MII_READ		0x10000000 /* MII Read Interrupt */
#define  ISTAT_ERRORS (ISTAT_DSCE|ISTAT_DATAE|ISTAT_DPE|ISTAT_RDU|ISTAT_RFO|ISTAT_TFU)
#define B44_IMASK	0x0024UL /* Interrupt Mask */
#define  IMASK_DEF		(ISTAT_ERRORS | ISTAT_TO | ISTAT_RX | ISTAT_TX)
#define B44_GPTIMER	0x0028UL /* General Purpose Timer */
#define B44_ADDR_LO	0x0088UL /* ENET Address Lo (B0 only) */
#define B44_ADDR_HI	0x008CUL /* ENET Address Hi (B0 only) */
#define B44_FILT_ADDR	0x0090UL /* ENET Filter Address */
#define B44_FILT_DATA	0x0094UL /* ENET Filter Data */
#define B44_TXBURST	0x00A0UL /* TX Max Burst Length */
#define B44_RXBURST	0x00A4UL /* RX Max Burst Length */
#define B44_MAC_CTRL	0x00A8UL /* MAC Control */
#define  MAC_CTRL_CRC32_ENAB	0x00000001 /* CRC32 Generation Enable */
#define  MAC_CTRL_PHY_PDOWN	0x00000004 /* Onchip EPHY Powerdown */
#define  MAC_CTRL_PHY_EDET	0x00000008 /* Onchip EPHY Energy Detected */
#define  MAC_CTRL_PHY_LEDCTRL	0x000000e0 /* Onchip EPHY LED Control */
#define  MAC_CTRL_PHY_LEDCTRL_SHIFT 5
#define B44_MAC_FLOW	0x00ACUL /* MAC Flow Control */
#define  MAC_FLOW_RX_HI_WATER	0x000000ff /* Receive FIFO HI Water Mark */
#define  MAC_FLOW_PAUSE_ENAB	0x00008000 /* Enable Pause Frame Generation */
#define B44_RCV_LAZY	0x0100UL /* Lazy Interrupt Control */
#define  RCV_LAZY_TO_MASK	0x00ffffff /* Timeout */
#define  RCV_LAZY_FC_MASK	0xff000000 /* Frame Count */
#define  RCV_LAZY_FC_SHIFT	24
#define B44_DMATX_CTRL	0x0200UL /* DMA TX Control */
#define  DMATX_CTRL_ENABLE	0x00000001 /* Enable */
#define  DMATX_CTRL_SUSPEND	0x00000002 /* Suepend Request */
#define  DMATX_CTRL_LPBACK	0x00000004 /* Loopback Enable */
#define  DMATX_CTRL_FAIRPRIOR	0x00000008 /* Fair Priority */
#define  DMATX_CTRL_FLUSH	0x00000010 /* Flush Request */
#define B44_DMATX_ADDR	0x0204UL /* DMA TX Descriptor Ring Address */
#define B44_DMATX_PTR	0x0208UL /* DMA TX Last Posted Descriptor */
#define B44_DMATX_STAT	0x020CUL /* DMA TX Current Active Desc. + Status */
#define  DMATX_STAT_CDMASK	0x00000fff /* Current Descriptor Mask */
#define  DMATX_STAT_SMASK	0x0000f000 /* State Mask */
#define  DMATX_STAT_SDISABLED	0x00000000 /* State Disabled */
#define  DMATX_STAT_SACTIVE	0x00001000 /* State Active */
#define  DMATX_STAT_SIDLE	0x00002000 /* State Idle Wait */
#define  DMATX_STAT_SSTOPPED	0x00003000 /* State Stopped */
#define  DMATX_STAT_SSUSP	0x00004000 /* State Suspend Pending */
#define  DMATX_STAT_EMASK	0x000f0000 /* Error Mask */
#define  DMATX_STAT_ENONE	0x00000000 /* Error None */
#define  DMATX_STAT_EDPE	0x00010000 /* Error Desc. Protocol Error */
#define  DMATX_STAT_EDFU	0x00020000 /* Error Data FIFO Underrun */
#define  DMATX_STAT_EBEBR	0x00030000 /* Error Bus Error on Buffer Read */
#define  DMATX_STAT_EBEDA	0x00040000 /* Error Bus Error on Desc. Access */
#define  DMATX_STAT_FLUSHED	0x00100000 /* Flushed */
#define B44_DMARX_CTRL	0x0210UL /* DMA RX Control */
#define  DMARX_CTRL_ENABLE	0x00000001 /* Enable */
#define  DMARX_CTRL_ROMASK	0x000000fe /* Receive Offset Mask */
#define  DMARX_CTRL_ROSHIFT	1 	   /* Receive Offset Shift */
#define B44_DMARX_ADDR	0x0214UL /* DMA RX Descriptor Ring Address */
#define B44_DMARX_PTR	0x0218UL /* DMA RX Last Posted Descriptor */
#define B44_DMARX_STAT	0x021CUL /* DMA RX Current Active Desc. + Status */
#define  DMARX_STAT_CDMASK	0x00000fff /* Current Descriptor Mask */
#define  DMARX_STAT_SMASK	0x0000f000 /* State Mask */
#define  DMARX_STAT_SDISABLED	0x00000000 /* State Disbaled */
#define  DMARX_STAT_SACTIVE	0x00001000 /* State Active */
#define  DMARX_STAT_SIDLE	0x00002000 /* State Idle Wait */
#define  DMARX_STAT_SSTOPPED	0x00003000 /* State Stopped */
#define  DMARX_STAT_EMASK	0x000f0000 /* Error Mask */
#define  DMARX_STAT_ENONE	0x00000000 /* Error None */
#define  DMARX_STAT_EDPE	0x00010000 /* Error Desc. Protocol Error */
#define  DMARX_STAT_EDFO	0x00020000 /* Error Data FIFO Overflow */
#define  DMARX_STAT_EBEBW	0x00030000 /* Error Bus Error on Buffer Write */
#define  DMARX_STAT_EBEDA	0x00040000 /* Error Bus Error on Desc. Access */
#define B44_DMAFIFO_AD	0x0220UL /* DMA FIFO Diag Address */
#define  DMAFIFO_AD_OMASK	0x0000ffff /* Offset Mask */
#define  DMAFIFO_AD_SMASK	0x000f0000 /* Select Mask */
#define  DMAFIFO_AD_SXDD	0x00000000 /* Select Transmit DMA Data */
#define  DMAFIFO_AD_SXDP	0x00010000 /* Select Transmit DMA Pointers */
#define  DMAFIFO_AD_SRDD	0x00040000 /* Select Receive DMA Data */
#define  DMAFIFO_AD_SRDP	0x00050000 /* Select Receive DMA Pointers */
#define  DMAFIFO_AD_SXFD	0x00080000 /* Select Transmit FIFO Data */
#define  DMAFIFO_AD_SXFP	0x00090000 /* Select Transmit FIFO Pointers */
#define  DMAFIFO_AD_SRFD	0x000c0000 /* Select Receive FIFO Data */
#define  DMAFIFO_AD_SRFP	0x000c0000 /* Select Receive FIFO Pointers */
#define B44_DMAFIFO_LO	0x0224UL /* DMA FIFO Diag Low Data */
#define B44_DMAFIFO_HI	0x0228UL /* DMA FIFO Diag High Data */
#define B44_RXCONFIG	0x0400UL /* EMAC RX Config */
#define  RXCONFIG_DBCAST	0x00000001 /* Disable Broadcast */
#define  RXCONFIG_ALLMULTI	0x00000002 /* Accept All Multicast */
#define  RXCONFIG_NORX_WHILE_TX	0x00000004 /* Receive Disable While Transmitting */
#define  RXCONFIG_PROMISC	0x00000008 /* Promiscuous Enable */
#define  RXCONFIG_LPBACK	0x00000010 /* Loopback Enable */
#define  RXCONFIG_FLOW		0x00000020 /* Flow Control Enable */
#define  RXCONFIG_FLOW_ACCEPT	0x00000040 /* Accept Unicast Flow Control Frame */
#define  RXCONFIG_RFILT		0x00000080 /* Reject Filter */
#define  RXCONFIG_CAM_ABSENT	0x00000100 /* CAM Absent */
#define B44_RXMAXLEN	0x0404UL /* EMAC RX Max Packet Length */
#define B44_TXMAXLEN	0x0408UL /* EMAC TX Max Packet Length */
#define B44_MDIO_CTRL	0x0410UL /* EMAC MDIO Control */
#define  MDIO_CTRL_MAXF_MASK	0x0000007f /* MDC Frequency */
#define  MDIO_CTRL_PREAMBLE	0x00000080 /* MII Preamble Enable */
#define B44_MDIO_DATA	0x0414UL /* EMAC MDIO Data */
#define  MDIO_DATA_DATA		0x0000ffff /* R/W Data */
#define  MDIO_DATA_TA_MASK	0x00030000 /* Turnaround Value */
#define  MDIO_DATA_TA_SHIFT	16
#define  MDIO_TA_VALID		2
#define  MDIO_DATA_RA_MASK	0x007c0000 /* Register Address */
#define  MDIO_DATA_RA_SHIFT	18
#define  MDIO_DATA_PMD_MASK	0x0f800000 /* Physical Media Device */
#define  MDIO_DATA_PMD_SHIFT	23
#define  MDIO_DATA_OP_MASK	0x30000000 /* Opcode */
#define  MDIO_DATA_OP_SHIFT	28
#define  MDIO_OP_WRITE		1
#define  MDIO_OP_READ		2
#define  MDIO_DATA_SB_MASK	0xc0000000 /* Start Bits */
#define  MDIO_DATA_SB_SHIFT	30
#define  MDIO_DATA_SB_START	0x40000000 /* Start Of Frame */
#define B44_EMAC_IMASK	0x0418UL /* EMAC Interrupt Mask */
#define B44_EMAC_ISTAT	0x041CUL /* EMAC Interrupt Status */
#define  EMAC_INT_MII		0x00000001 /* MII MDIO Interrupt */
#define  EMAC_INT_MIB		0x00000002 /* MIB Interrupt */
#define  EMAC_INT_FLOW		0x00000003 /* Flow Control Interrupt */
#define B44_CAM_DATA_LO	0x0420UL /* EMAC CAM Data Low */
#define B44_CAM_DATA_HI	0x0424UL /* EMAC CAM Data High */
#define  CAM_DATA_HI_VALID	0x00010000 /* Valid Bit */
#define B44_CAM_CTRL	0x0428UL /* EMAC CAM Control */
#define  CAM_CTRL_ENABLE	0x00000001 /* CAM Enable */
#define  CAM_CTRL_MSEL		0x00000002 /* Mask Select */
#define  CAM_CTRL_READ		0x00000004 /* Read */
#define  CAM_CTRL_WRITE		0x00000008 /* Read */
#define  CAM_CTRL_INDEX_MASK	0x003f0000 /* Index Mask */
#define  CAM_CTRL_INDEX_SHIFT	16
#define  CAM_CTRL_BUSY		0x80000000 /* CAM Busy */
#define B44_ENET_CTRL	0x042CUL /* EMAC ENET Control */
#define  ENET_CTRL_ENABLE	0x00000001 /* EMAC Enable */
#define  ENET_CTRL_DISABLE	0x00000002 /* EMAC Disable */
#define  ENET_CTRL_SRST		0x00000004 /* EMAC Soft Reset */
#define  ENET_CTRL_EPSEL	0x00000008 /* External PHY Select */
#define B44_TX_CTRL	0x0430UL /* EMAC TX Control */
#define  TX_CTRL_DUPLEX		0x00000001 /* Full Duplex */
#define  TX_CTRL_FMODE		0x00000002 /* Flow Mode */
#define  TX_CTRL_SBENAB		0x00000004 /* Single Backoff Enable */
#define  TX_CTRL_SMALL_SLOT	0x00000008 /* Small Slottime */
#define B44_TX_WMARK	0x0434UL /* EMAC TX Watermark */
#define B44_MIB_CTRL	0x0438UL /* EMAC MIB Control */
#define  MIB_CTRL_CLR_ON_READ	0x00000001 /* Autoclear on Read */
#define B44_TX_GOOD_O	0x0500UL /* MIB TX Good Octets */
#define B44_TX_GOOD_P	0x0504UL /* MIB TX Good Packets */
#define B44_TX_O	0x0508UL /* MIB TX Octets */
#define B44_TX_P	0x050CUL /* MIB TX Packets */
#define B44_TX_BCAST	0x0510UL /* MIB TX Broadcast Packets */
#define B44_TX_MCAST	0x0514UL /* MIB TX Multicast Packets */
#define B44_TX_64	0x0518UL /* MIB TX <= 64 byte Packets */
#define B44_TX_65_127	0x051CUL /* MIB TX 65 to 127 byte Packets */
#define B44_TX_128_255	0x0520UL /* MIB TX 128 to 255 byte Packets */
#define B44_TX_256_511	0x0524UL /* MIB TX 256 to 511 byte Packets */
#define B44_TX_512_1023	0x0528UL /* MIB TX 512 to 1023 byte Packets */
#define B44_TX_1024_MAX	0x052CUL /* MIB TX 1024 to max byte Packets */
#define B44_TX_JABBER	0x0530UL /* MIB TX Jabber Packets */
#define B44_TX_OSIZE	0x0534UL /* MIB TX Oversize Packets */
#define B44_TX_FRAG	0x0538UL /* MIB TX Fragment Packets */
#define B44_TX_URUNS	0x053CUL /* MIB TX Underruns */
#define B44_TX_TCOLS	0x0540UL /* MIB TX Total Collisions */
#define B44_TX_SCOLS	0x0544UL /* MIB TX Single Collisions */
#define B44_TX_MCOLS	0x0548UL /* MIB TX Multiple Collisions */
#define B44_TX_ECOLS	0x054CUL /* MIB TX Excessive Collisions */
#define B44_TX_LCOLS	0x0550UL /* MIB TX Late Collisions */
#define B44_TX_DEFERED	0x0554UL /* MIB TX Defered Packets */
#define B44_TX_CLOST	0x0558UL /* MIB TX Carrier Lost */
#define B44_TX_PAUSE	0x055CUL /* MIB TX Pause Packets */
#define B44_RX_GOOD_O	0x0580UL /* MIB RX Good Octets */
#define B44_RX_GOOD_P	0x0584UL /* MIB RX Good Packets */
#define B44_RX_O	0x0588UL /* MIB RX Octets */
#define B44_RX_P	0x058CUL /* MIB RX Packets */
#define B44_RX_BCAST	0x0590UL /* MIB RX Broadcast Packets */
#define B44_RX_MCAST	0x0594UL /* MIB RX Multicast Packets */
#define B44_RX_64	0x0598UL /* MIB RX <= 64 byte Packets */
#define B44_RX_65_127	0x059CUL /* MIB RX 65 to 127 byte Packets */
#define B44_RX_128_255	0x05A0UL /* MIB RX 128 to 255 byte Packets */
#define B44_RX_256_511	0x05A4UL /* MIB RX 256 to 511 byte Packets */
#define B44_RX_512_1023	0x05A8UL /* MIB RX 512 to 1023 byte Packets */
#define B44_RX_1024_MAX	0x05ACUL /* MIB RX 1024 to max byte Packets */
#define B44_RX_JABBER	0x05B0UL /* MIB RX Jabber Packets */
#define B44_RX_OSIZE	0x05B4UL /* MIB RX Oversize Packets */
#define B44_RX_FRAG	0x05B8UL /* MIB RX Fragment Packets */
#define B44_RX_MISS	0x05BCUL /* MIB RX Missed Packets */
#define B44_RX_CRCA	0x05C0UL /* MIB RX CRC Align Errors */
#define B44_RX_USIZE	0x05C4UL /* MIB RX Undersize Packets */
#define B44_RX_CRC	0x05C8UL /* MIB RX CRC Errors */
#define B44_RX_ALIGN	0x05CCUL /* MIB RX Align Errors */
#define B44_RX_SYM	0x05D0UL /* MIB RX Symbol Errors */
#define B44_RX_PAUSE	0x05D4UL /* MIB RX Pause Packets */
#define B44_RX_NPAUSE	0x05D8UL /* MIB RX Non-Pause Packets */

/* 4400 PHY registers */
#define B44_MII_AUXCTRL		24	/* Auxiliary Control */
#define  MII_AUXCTRL_DUPLEX	0x0001  /* Full Duplex */
#define  MII_AUXCTRL_SPEED	0x0002  /* 1=100Mbps, 0=10Mbps */
#define  MII_AUXCTRL_FORCED	0x0004	/* Forced 10/100 */
#define B44_MII_ALEDCTRL	26	/* Activity LED */
#define  MII_ALEDCTRL_ALLMSK	0x7fff
#define B44_MII_TLEDCTRL	27	/* Traffic Meter LED */
#define  MII_TLEDCTRL_ENABLE	0x0040

struct dma_desc {
	__le32	ctrl;
	__le32	addr;
};

/* There are only 12 bits in the DMA engine for descriptor offsetting
 * so the table must be aligned on a boundary of this.
 */
#define DMA_TABLE_BYTES		4096

#define DESC_CTRL_LEN	0x00001fff
#define DESC_CTRL_CMASK	0x0ff00000 /* Core specific bits */
#define DESC_CTRL_EOT	0x10000000 /* End of Table */
#define DESC_CTRL_IOC	0x20000000 /* Interrupt On Completion */
#define DESC_CTRL_EOF	0x40000000 /* End of Frame */
#define DESC_CTRL_SOF	0x80000000 /* Start of Frame */

#define RX_COPY_THRESHOLD  	256

struct rx_header {
	__le16	len;
	__le16	flags;
	__le16	pad[12];
};
#define RX_HEADER_LEN	28

#define RX_FLAG_OFIFO	0x00000001 /* FIFO Overflow */
#define RX_FLAG_CRCERR	0x00000002 /* CRC Error */
#define RX_FLAG_SERR	0x00000004 /* Receive Symbol Error */
#define RX_FLAG_ODD	0x00000008 /* Frame has odd number of nibbles */
#define RX_FLAG_LARGE	0x00000010 /* Frame is > RX MAX Length */
#define RX_FLAG_MCAST	0x00000020 /* Dest is Multicast Address */
#define RX_FLAG_BCAST	0x00000040 /* Dest is Broadcast Address */
#define RX_FLAG_MISS	0x00000080 /* Received due to promisc mode */
#define RX_FLAG_LAST	0x00000800 /* Last buffer in frame */
#define RX_FLAG_ERRORS	(RX_FLAG_ODD | RX_FLAG_SERR | RX_FLAG_CRCERR | RX_FLAG_OFIFO)

struct ring_info {
	struct sk_buff		*skb;
	dma_addr_t	mapping;
};

#define B44_MCAST_TABLE_SIZE	32
#define B44_PHY_ADDR_NO_PHY	30
#define B44_MDC_RATIO		5000000

#define	B44_STAT_REG_DECLARE		\
	_B44(tx_good_octets)		\
	_B44(tx_good_pkts)		\
	_B44(tx_octets)			\
	_B44(tx_pkts)			\
	_B44(tx_broadcast_pkts)		\
	_B44(tx_multicast_pkts)		\
	_B44(tx_len_64)			\
	_B44(tx_len_65_to_127)		\
	_B44(tx_len_128_to_255)		\
	_B44(tx_len_256_to_511)		\
	_B44(tx_len_512_to_1023)	\
	_B44(tx_len_1024_to_max)	\
	_B44(tx_jabber_pkts)		\
	_B44(tx_oversize_pkts)		\
	_B44(tx_fragment_pkts)		\
	_B44(tx_underruns)		\
	_B44(tx_total_cols)		\
	_B44(tx_single_cols)		\
	_B44(tx_multiple_cols)		\
	_B44(tx_excessive_cols)		\
	_B44(tx_late_cols)		\
	_B44(tx_defered)		\
	_B44(tx_carrier_lost)		\
	_B44(tx_pause_pkts)		\
	_B44(rx_good_octets)		\
	_B44(rx_good_pkts)		\
	_B44(rx_octets)			\
	_B44(rx_pkts)			\
	_B44(rx_broadcast_pkts)		\
	_B44(rx_multicast_pkts)		\
	_B44(rx_len_64)			\
	_B44(rx_len_65_to_127)		\
	_B44(rx_len_128_to_255)		\
	_B44(rx_len_256_to_511)		\
	_B44(rx_len_512_to_1023)	\
	_B44(rx_len_1024_to_max)	\
	_B44(rx_jabber_pkts)		\
	_B44(rx_oversize_pkts)		\
	_B44(rx_fragment_pkts)		\
	_B44(rx_missed_pkts)		\
	_B44(rx_crc_align_errs)		\
	_B44(rx_undersize)		\
	_B44(rx_crc_errs)		\
	_B44(rx_align_errs)		\
	_B44(rx_symbol_errs)		\
	_B44(rx_pause_pkts)		\
	_B44(rx_nonpause_pkts)

/* SW copy of device statistics, kept up to date by periodic timer
 * which probes HW values. Check b44_stats_update if you mess with
 * the layout
 */
struct b44_hw_stats {
#define _B44(x)	u32 x;
B44_STAT_REG_DECLARE
#undef _B44
};

struct ssb_device;

struct b44 {
	spinlock_t		lock;

	u32			imask, istat;

	struct dma_desc		*rx_ring, *tx_ring;

	u32			tx_prod, tx_cons;
	u32			rx_prod, rx_cons;

	struct ring_info	*rx_buffers;
	struct ring_info	*tx_buffers;

	struct napi_struct	napi;

	u32			dma_offset;
	u32			flags;
#define B44_FLAG_B0_ANDLATER	0x00000001
#define B44_FLAG_BUGGY_TXPTR	0x00000002
#define B44_FLAG_REORDER_BUG	0x00000004
#define B44_FLAG_PAUSE_AUTO	0x00008000
#define B44_FLAG_FULL_DUPLEX	0x00010000
#define B44_FLAG_100_BASE_T	0x00020000
#define B44_FLAG_TX_PAUSE	0x00040000
#define B44_FLAG_RX_PAUSE	0x00080000
#define B44_FLAG_FORCE_LINK	0x00100000
#define B44_FLAG_ADV_10HALF	0x01000000
#define B44_FLAG_ADV_10FULL	0x02000000
#define B44_FLAG_ADV_100HALF	0x04000000
#define B44_FLAG_ADV_100FULL	0x08000000
#define B44_FLAG_INTERNAL_PHY	0x10000000
#define B44_FLAG_RX_RING_HACK	0x20000000
#define B44_FLAG_TX_RING_HACK	0x40000000
#define B44_FLAG_WOL_ENABLE	0x80000000

	u32			msg_enable;

	struct timer_list	timer;

	struct net_device_stats	stats;
	struct b44_hw_stats	hw_stats;

	struct ssb_device	*sdev;
	struct net_device	*dev;

	dma_addr_t		rx_ring_dma, tx_ring_dma;

	u32			rx_pending;
	u32			tx_pending;
	u8			phy_addr;
	u8			force_copybreak;
	struct mii_if_info	mii_if;
};

#endif /* _B44_H */
