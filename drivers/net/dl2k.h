/*  D-Link DL2000-based Gigabit Ethernet Adapter Linux driver */
/*
    Copyright (c) 2001, 2002 by D-Link Corporation
    Written by Edward Peng.<edward_peng@dlink.com.tw>
    Created 03-May-2001, base on Linux' sundance.c.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/

#ifndef __DL2K_H__
#define __DL2K_H__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>
#include <linux/crc32.h>
#include <linux/ethtool.h>
#include <linux/bitops.h>
#include <asm/processor.h>	/* Processor type for cache alignment. */
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#define TX_RING_SIZE	256
#define TX_QUEUE_LEN	(TX_RING_SIZE - 1) /* Limit ring entries actually used.*/
#define RX_RING_SIZE 	256
#define TX_TOTAL_SIZE	TX_RING_SIZE*sizeof(struct netdev_desc)
#define RX_TOTAL_SIZE	RX_RING_SIZE*sizeof(struct netdev_desc)

/* This driver was written to use PCI memory space, however x86-oriented
   hardware often uses I/O space accesses. */
#ifndef MEM_MAPPING
#undef readb
#undef readw
#undef readl
#undef writeb
#undef writew
#undef writel
#define readb inb
#define readw inw
#define readl inl
#define writeb outb
#define writew outw
#define writel outl
#endif

/* Offsets to the device registers.
   Unlike software-only systems, device drivers interact with complex hardware.
   It's not useful to define symbolic names for every register bit in the
   device.  The name can only partially document the semantics and make
   the driver longer and more difficult to read.
   In general, only the important configuration values or bits changed
   multiple times should be defined symbolically.
*/
enum dl2x_offsets {
	/* I/O register offsets */
	DMACtrl = 0x00,
	RxDMAStatus = 0x08,
	TFDListPtr0 = 0x10,
	TFDListPtr1 = 0x14,
	TxDMABurstThresh = 0x18,
	TxDMAUrgentThresh = 0x19,
	TxDMAPollPeriod = 0x1a,
	RFDListPtr0 = 0x1c,
	RFDListPtr1 = 0x20,
	RxDMABurstThresh = 0x24,
	RxDMAUrgentThresh = 0x25,
	RxDMAPollPeriod = 0x26,
	RxDMAIntCtrl = 0x28,
	DebugCtrl = 0x2c,
	ASICCtrl = 0x30,
	FifoCtrl = 0x38,
	RxEarlyThresh = 0x3a,
	FlowOffThresh = 0x3c,
	FlowOnThresh = 0x3e,
	TxStartThresh = 0x44,
	EepromData = 0x48,
	EepromCtrl = 0x4a,
	ExpromAddr = 0x4c,
	Exprodata = 0x50,
	WakeEvent = 0x51,
	CountDown = 0x54,
	IntStatusAck = 0x5a,
	IntEnable = 0x5c,
	IntStatus = 0x5e,
	TxStatus = 0x60,
	MACCtrl = 0x6c,
	VLANTag = 0x70,
	PhyCtrl = 0x76,
	StationAddr0 = 0x78,
	StationAddr1 = 0x7a,
	StationAddr2 = 0x7c,
	VLANId = 0x80,
	MaxFrameSize = 0x86,
	ReceiveMode = 0x88,
	HashTable0 = 0x8c,
	HashTable1 = 0x90,
	RmonStatMask = 0x98,
	StatMask = 0x9c,
	RxJumboFrames = 0xbc,
	TCPCheckSumErrors = 0xc0,
	IPCheckSumErrors = 0xc2,
	UDPCheckSumErrors = 0xc4,
	TxJumboFrames = 0xf4,
	/* Ethernet MIB statistic register offsets */
	OctetRcvOk = 0xa8,
	McstOctetRcvOk = 0xac,
	BcstOctetRcvOk = 0xb0,
	FramesRcvOk = 0xb4,
	McstFramesRcvdOk = 0xb8,
	BcstFramesRcvdOk = 0xbe,
	MacControlFramesRcvd = 0xc6,
	FrameTooLongErrors = 0xc8,
	InRangeLengthErrors = 0xca,
	FramesCheckSeqErrors = 0xcc,
	FramesLostRxErrors = 0xce,
	OctetXmtOk = 0xd0,
	McstOctetXmtOk = 0xd4,
	BcstOctetXmtOk = 0xd8,
	FramesXmtOk = 0xdc,
	McstFramesXmtdOk = 0xe0,
	FramesWDeferredXmt = 0xe4,
	LateCollisions = 0xe8,
	MultiColFrames = 0xec,
	SingleColFrames = 0xf0,
	BcstFramesXmtdOk = 0xf6,
	CarrierSenseErrors = 0xf8,
	MacControlFramesXmtd = 0xfa,
	FramesAbortXSColls = 0xfc,
	FramesWEXDeferal = 0xfe,
	/* RMON statistic register offsets */
	EtherStatsCollisions = 0x100,
	EtherStatsOctetsTransmit = 0x104,
	EtherStatsPktsTransmit = 0x108,
	EtherStatsPkts64OctetTransmit = 0x10c,
	EtherStats65to127OctetsTransmit = 0x110,
	EtherStatsPkts128to255OctetsTransmit = 0x114,
	EtherStatsPkts256to511OctetsTransmit = 0x118,
	EtherStatsPkts512to1023OctetsTransmit = 0x11c,
	EtherStatsPkts1024to1518OctetsTransmit = 0x120,
	EtherStatsCRCAlignErrors = 0x124,
	EtherStatsUndersizePkts = 0x128,
	EtherStatsFragments = 0x12c,
	EtherStatsJabbers = 0x130,
	EtherStatsOctets = 0x134,
	EtherStatsPkts = 0x138,
	EtherStats64Octets = 0x13c,
	EtherStatsPkts65to127Octets = 0x140,
	EtherStatsPkts128to255Octets = 0x144,
	EtherStatsPkts256to511Octets = 0x148,
	EtherStatsPkts512to1023Octets = 0x14c,
	EtherStatsPkts1024to1518Octets = 0x150,
};

/* Bits in the interrupt status/mask registers. */
enum IntStatus_bits {
	InterruptStatus = 0x0001,
	HostError = 0x0002,
	MACCtrlFrame = 0x0008,
	TxComplete = 0x0004,
	RxComplete = 0x0010,
	RxEarly = 0x0020,
	IntRequested = 0x0040,
	UpdateStats = 0x0080,
	LinkEvent = 0x0100,
	TxDMAComplete = 0x0200,
	RxDMAComplete = 0x0400,
	RFDListEnd = 0x0800,
	RxDMAPriority = 0x1000,
};

/* Bits in the ReceiveMode register. */
enum ReceiveMode_bits {
	ReceiveUnicast = 0x0001,
	ReceiveMulticast = 0x0002,
	ReceiveBroadcast = 0x0004,
	ReceiveAllFrames = 0x0008,
	ReceiveMulticastHash = 0x0010,
	ReceiveIPMulticast = 0x0020,
	ReceiveVLANMatch = 0x0100,
	ReceiveVLANHash = 0x0200,
};
/* Bits in MACCtrl. */
enum MACCtrl_bits {
	DuplexSelect = 0x20,
	TxFlowControlEnable = 0x80,
	RxFlowControlEnable = 0x0100,
	RcvFCS = 0x200,
	AutoVLANtagging = 0x1000,
	AutoVLANuntagging = 0x2000,
	StatsEnable = 0x00200000,
	StatsDisable = 0x00400000,
	StatsEnabled = 0x00800000,
	TxEnable = 0x01000000,
	TxDisable = 0x02000000,
	TxEnabled = 0x04000000,
	RxEnable = 0x08000000,
	RxDisable = 0x10000000,
	RxEnabled = 0x20000000,
};

enum ASICCtrl_LoWord_bits {
	PhyMedia = 0x0080,
};

enum ASICCtrl_HiWord_bits {
	GlobalReset = 0x0001,
	RxReset = 0x0002,
	TxReset = 0x0004,
	DMAReset = 0x0008,
	FIFOReset = 0x0010,
	NetworkReset = 0x0020,
	HostReset = 0x0040,
	ResetBusy = 0x0400,
};

/* Transmit Frame Control bits */
enum TFC_bits {
	DwordAlign = 0x00000000,
	WordAlignDisable = 0x00030000,
	WordAlign = 0x00020000,
	TCPChecksumEnable = 0x00040000,
	UDPChecksumEnable = 0x00080000,
	IPChecksumEnable = 0x00100000,
	FCSAppendDisable = 0x00200000,
	TxIndicate = 0x00400000,
	TxDMAIndicate = 0x00800000,
	FragCountShift = 24,
	VLANTagInsert = 0x0000000010000000,
	TFDDone = 0x80000000,
	VIDShift = 32,
	UsePriorityShift = 48,
};

/* Receive Frames Status bits */
enum RFS_bits {
	RxFIFOOverrun = 0x00010000,
	RxRuntFrame = 0x00020000,
	RxAlignmentError = 0x00040000,
	RxFCSError = 0x00080000,
	RxOverSizedFrame = 0x00100000,
	RxLengthError = 0x00200000,
	VLANDetected = 0x00400000,
	TCPDetected = 0x00800000,
	TCPError = 0x01000000,
	UDPDetected = 0x02000000,
	UDPError = 0x04000000,
	IPDetected = 0x08000000,
	IPError = 0x10000000,
	FrameStart = 0x20000000,
	FrameEnd = 0x40000000,
	RFDDone = 0x80000000,
	TCIShift = 32,
	RFS_Errors = 0x003f0000,
};

#define MII_RESET_TIME_OUT		10000
/* MII register */
enum _mii_reg {
	MII_BMCR = 0,
	MII_BMSR = 1,
	MII_PHY_ID1 = 2,
	MII_PHY_ID2 = 3,
	MII_ANAR = 4,
	MII_ANLPAR = 5,
	MII_ANER = 6,
	MII_ANNPT = 7,
	MII_ANLPRNP = 8,
	MII_MSCR = 9,
	MII_MSSR = 10,
	MII_ESR = 15,
	MII_PHY_SCR = 16,
};
/* PCS register */
enum _pcs_reg {
	PCS_BMCR = 0,
	PCS_BMSR = 1,
	PCS_ANAR = 4,
	PCS_ANLPAR = 5,
	PCS_ANER = 6,
	PCS_ANNPT = 7,
	PCS_ANLPRNP = 8,
	PCS_ESR = 15,
};

/* Basic Mode Control Register */
enum _mii_bmcr {
	MII_BMCR_RESET = 0x8000,
	MII_BMCR_LOOP_BACK = 0x4000,
	MII_BMCR_SPEED_LSB = 0x2000,
	MII_BMCR_AN_ENABLE = 0x1000,
	MII_BMCR_POWER_DOWN = 0x0800,
	MII_BMCR_ISOLATE = 0x0400,
	MII_BMCR_RESTART_AN = 0x0200,
	MII_BMCR_DUPLEX_MODE = 0x0100,
	MII_BMCR_COL_TEST = 0x0080,
	MII_BMCR_SPEED_MSB = 0x0040,
	MII_BMCR_SPEED_RESERVED = 0x003f,
	MII_BMCR_SPEED_10 = 0,
	MII_BMCR_SPEED_100 = MII_BMCR_SPEED_LSB,
	MII_BMCR_SPEED_1000 = MII_BMCR_SPEED_MSB,
};

/* Basic Mode Status Register */
enum _mii_bmsr {
	MII_BMSR_100BT4 = 0x8000,
	MII_BMSR_100BX_FD = 0x4000,
	MII_BMSR_100BX_HD = 0x2000,
	MII_BMSR_10BT_FD = 0x1000,
	MII_BMSR_10BT_HD = 0x0800,
	MII_BMSR_100BT2_FD = 0x0400,
	MII_BMSR_100BT2_HD = 0x0200,
	MII_BMSR_EXT_STATUS = 0x0100,
	MII_BMSR_PREAMBLE_SUPP = 0x0040,
	MII_BMSR_AN_COMPLETE = 0x0020,
	MII_BMSR_REMOTE_FAULT = 0x0010,
	MII_BMSR_AN_ABILITY = 0x0008,
	MII_BMSR_LINK_STATUS = 0x0004,
	MII_BMSR_JABBER_DETECT = 0x0002,
	MII_BMSR_EXT_CAP = 0x0001,
};

/* ANAR */
enum _mii_anar {
	MII_ANAR_NEXT_PAGE = 0x8000,
	MII_ANAR_REMOTE_FAULT = 0x4000,
	MII_ANAR_ASYMMETRIC = 0x0800,
	MII_ANAR_PAUSE = 0x0400,
	MII_ANAR_100BT4 = 0x0200,
	MII_ANAR_100BX_FD = 0x0100,
	MII_ANAR_100BX_HD = 0x0080,
	MII_ANAR_10BT_FD = 0x0020,
	MII_ANAR_10BT_HD = 0x0010,
	MII_ANAR_SELECTOR = 0x001f,
	MII_IEEE8023_CSMACD = 0x0001,
};

/* ANLPAR */
enum _mii_anlpar {
	MII_ANLPAR_NEXT_PAGE = MII_ANAR_NEXT_PAGE,
	MII_ANLPAR_REMOTE_FAULT = MII_ANAR_REMOTE_FAULT,
	MII_ANLPAR_ASYMMETRIC = MII_ANAR_ASYMMETRIC,
	MII_ANLPAR_PAUSE = MII_ANAR_PAUSE,
	MII_ANLPAR_100BT4 = MII_ANAR_100BT4,
	MII_ANLPAR_100BX_FD = MII_ANAR_100BX_FD,
	MII_ANLPAR_100BX_HD = MII_ANAR_100BX_HD,
	MII_ANLPAR_10BT_FD = MII_ANAR_10BT_FD,
	MII_ANLPAR_10BT_HD = MII_ANAR_10BT_HD,
	MII_ANLPAR_SELECTOR = MII_ANAR_SELECTOR,
};

/* Auto-Negotiation Expansion Register */
enum _mii_aner {
	MII_ANER_PAR_DETECT_FAULT = 0x0010,
	MII_ANER_LP_NEXTPAGABLE = 0x0008,
	MII_ANER_NETXTPAGABLE = 0x0004,
	MII_ANER_PAGE_RECEIVED = 0x0002,
	MII_ANER_LP_NEGOTIABLE = 0x0001,
};

/* MASTER-SLAVE Control Register */
enum _mii_mscr {
	MII_MSCR_TEST_MODE = 0xe000,
	MII_MSCR_CFG_ENABLE = 0x1000,
	MII_MSCR_CFG_VALUE = 0x0800,
	MII_MSCR_PORT_VALUE = 0x0400,
	MII_MSCR_1000BT_FD = 0x0200,
	MII_MSCR_1000BT_HD = 0X0100,
};

/* MASTER-SLAVE Status Register */
enum _mii_mssr {
	MII_MSSR_CFG_FAULT = 0x8000,
	MII_MSSR_CFG_RES = 0x4000,
	MII_MSSR_LOCAL_RCV_STATUS = 0x2000,
	MII_MSSR_REMOTE_RCVR = 0x1000,
	MII_MSSR_LP_1000BT_FD = 0x0800,
	MII_MSSR_LP_1000BT_HD = 0x0400,
	MII_MSSR_IDLE_ERR_COUNT = 0x00ff,
};

/* IEEE Extened Status Register */
enum _mii_esr {
	MII_ESR_1000BX_FD = 0x8000,
	MII_ESR_1000BX_HD = 0x4000,
	MII_ESR_1000BT_FD = 0x2000,
	MII_ESR_1000BT_HD = 0x1000,
};
/* PHY Specific Control Register */
#if 0
typedef union t_MII_PHY_SCR {
	u16 image;
	struct {
		u16 disable_jabber:1;	// bit 0
		u16 polarity_reversal:1;	// bit 1
		u16 SEQ_test:1;	// bit 2
		u16 _bit_3:1;	// bit 3
		u16 disable_CLK125:1;	// bit 4
		u16 mdi_crossover_mode:2;	// bit 6:5
		u16 enable_ext_dist:1;	// bit 7
		u16 _bit_8_9:2;	// bit 9:8
		u16 force_link:1;	// bit 10
		u16 assert_CRS:1;	// bit 11
		u16 rcv_fifo_depth:2;	// bit 13:12
		u16 xmit_fifo_depth:2;	// bit 15:14
	} bits;
} PHY_SCR_t, *PPHY_SCR_t;
#endif

typedef enum t_MII_ADMIN_STATUS {
	adm_reset,
	adm_operational,
	adm_loopback,
	adm_power_down,
	adm_isolate
} MII_ADMIN_t, *PMII_ADMIN_t;

/* Physical Coding Sublayer Management (PCS) */
/* PCS control and status registers bitmap as the same as MII */
/* PCS Extended Status register bitmap as the same as MII */
/* PCS ANAR */
enum _pcs_anar {
	PCS_ANAR_NEXT_PAGE = 0x8000,
	PCS_ANAR_REMOTE_FAULT = 0x3000,
	PCS_ANAR_ASYMMETRIC = 0x0100,
	PCS_ANAR_PAUSE = 0x0080,
	PCS_ANAR_HALF_DUPLEX = 0x0040,
	PCS_ANAR_FULL_DUPLEX = 0x0020,
};
/* PCS ANLPAR */
enum _pcs_anlpar {
	PCS_ANLPAR_NEXT_PAGE = PCS_ANAR_NEXT_PAGE,
	PCS_ANLPAR_REMOTE_FAULT = PCS_ANAR_REMOTE_FAULT,
	PCS_ANLPAR_ASYMMETRIC = PCS_ANAR_ASYMMETRIC,
	PCS_ANLPAR_PAUSE = PCS_ANAR_PAUSE,
	PCS_ANLPAR_HALF_DUPLEX = PCS_ANAR_HALF_DUPLEX,
	PCS_ANLPAR_FULL_DUPLEX = PCS_ANAR_FULL_DUPLEX,
};

typedef struct t_SROM {
	u16 config_param;	/* 0x00 */
	u16 asic_ctrl;		/* 0x02 */
	u16 sub_vendor_id;	/* 0x04 */
	u16 sub_system_id;	/* 0x06 */
	u16 reserved1[12];	/* 0x08-0x1f */
	u8 mac_addr[6];		/* 0x20-0x25 */
	u8 reserved2[10];	/* 0x26-0x2f */
	u8 sib[204];		/* 0x30-0xfb */
	u32 crc;		/* 0xfc-0xff */
} SROM_t, *PSROM_t;

/* Ioctl custom data */
struct ioctl_data {
	char signature[10];
	int cmd;
	int len;
	char *data;
};

struct mii_data {
	__u16 reserved;
	__u16 reg_num;
	__u16 in_value;
	__u16 out_value;
};

/* The Rx and Tx buffer descriptors. */
struct netdev_desc {
	__le64 next_desc;
	__le64 status;
	__le64 fraginfo;
};

#define PRIV_ALIGN	15	/* Required alignment mask */
/* Use  __attribute__((aligned (L1_CACHE_BYTES)))  to maintain alignment
   within the structure. */
struct netdev_private {
	/* Descriptor rings first for alignment. */
	struct netdev_desc *rx_ring;
	struct netdev_desc *tx_ring;
	struct sk_buff *rx_skbuff[RX_RING_SIZE];
	struct sk_buff *tx_skbuff[TX_RING_SIZE];
	dma_addr_t tx_ring_dma;
	dma_addr_t rx_ring_dma;
	struct pci_dev *pdev;
	spinlock_t tx_lock;
	spinlock_t rx_lock;
	struct net_device_stats stats;
	unsigned int rx_buf_sz;		/* Based on MTU+slack. */
	unsigned int speed;		/* Operating speed */
	unsigned int vlan;		/* VLAN Id */
	unsigned int chip_id;		/* PCI table chip id */
	unsigned int rx_coalesce; 	/* Maximum frames each RxDMAComplete intr */
	unsigned int rx_timeout; 	/* Wait time between RxDMAComplete intr */
	unsigned int tx_coalesce;	/* Maximum frames each tx interrupt */
	unsigned int full_duplex:1;	/* Full-duplex operation requested. */
	unsigned int an_enable:2;	/* Auto-Negotiated Enable */
	unsigned int jumbo:1;		/* Jumbo frame enable */
	unsigned int coalesce:1;	/* Rx coalescing enable */
	unsigned int tx_flow:1;		/* Tx flow control enable */
	unsigned int rx_flow:1;		/* Rx flow control enable */
	unsigned int phy_media:1;	/* 1: fiber, 0: copper */
	unsigned int link_status:1;	/* Current link status */
	struct netdev_desc *last_tx;	/* Last Tx descriptor used. */
	unsigned long cur_rx, old_rx;	/* Producer/consumer ring indices */
	unsigned long cur_tx, old_tx;
	struct timer_list timer;
	int wake_polarity;
	char name[256];		/* net device description */
	u8 duplex_polarity;
	u16 mcast_filter[4];
	u16 advertising;	/* NWay media advertisement */
	u16 negotiate;		/* Negotiated media */
	int phy_addr;		/* PHY addresses. */
};

/* The station address location in the EEPROM. */
/* The struct pci_device_id consist of:
        vendor, device          Vendor and device ID to match (or PCI_ANY_ID)
        subvendor, subdevice    Subsystem vendor and device ID to match (or PCI_ANY_ID)
        class                   Device class to match. The class_mask tells which bits
        class_mask              of the class are honored during the comparison.
        driver_data             Data private to the driver.
*/

static const struct pci_device_id rio_pci_tbl[] = {
	{0x1186, 0x4000, PCI_ANY_ID, PCI_ANY_ID, },
	{0x13f0, 0x1021, PCI_ANY_ID, PCI_ANY_ID, },
	{ }
};
MODULE_DEVICE_TABLE (pci, rio_pci_tbl);
#define TX_TIMEOUT  (4*HZ)
#define PACKET_SIZE		1536
#define MAX_JUMBO		8000
#define RIO_IO_SIZE             340
#define DEFAULT_RXC		5
#define DEFAULT_RXT		750
#define DEFAULT_TXC		1
#define MAX_TXC			8
#endif				/* __DL2K_H__ */
