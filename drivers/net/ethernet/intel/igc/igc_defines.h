/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2018 Intel Corporation */

#ifndef _IGC_DEFINES_H_
#define _IGC_DEFINES_H_

/* Number of Transmit and Receive Descriptors must be a multiple of 8 */
#define REQ_TX_DESCRIPTOR_MULTIPLE	8
#define REQ_RX_DESCRIPTOR_MULTIPLE	8

#define IGC_CTRL_EXT_DRV_LOAD	0x10000000 /* Drv loaded bit for FW */

/* Definitions for power management and wakeup registers */
/* Wake Up Control */
#define IGC_WUC_PME_EN	0x00000002 /* PME Enable */

/* Wake Up Filter Control */
#define IGC_WUFC_LNKC	0x00000001 /* Link Status Change Wakeup Enable */
#define IGC_WUFC_MAG	0x00000002 /* Magic Packet Wakeup Enable */
#define IGC_WUFC_EX	0x00000004 /* Directed Exact Wakeup Enable */
#define IGC_WUFC_MC	0x00000008 /* Directed Multicast Wakeup Enable */
#define IGC_WUFC_BC	0x00000010 /* Broadcast Wakeup Enable */

#define IGC_CTRL_ADVD3WUC	0x00100000  /* D3 WUC */

/* Wake Up Status */
#define IGC_WUS_EX	0x00000004 /* Directed Exact */
#define IGC_WUS_ARPD	0x00000020 /* Directed ARP Request */
#define IGC_WUS_IPV4	0x00000040 /* Directed IPv4 */
#define IGC_WUS_IPV6	0x00000080 /* Directed IPv6 */
#define IGC_WUS_NSD	0x00000400 /* Directed IPv6 Neighbor Solicitation */

/* Packet types that are enabled for wake packet delivery */
#define WAKE_PKT_WUS ( \
	IGC_WUS_EX   | \
	IGC_WUS_ARPD | \
	IGC_WUS_IPV4 | \
	IGC_WUS_IPV6 | \
	IGC_WUS_NSD)

/* Wake Up Packet Length */
#define IGC_WUPL_MASK	0x00000FFF

/* Wake Up Packet Memory stores the first 128 bytes of the wake up packet */
#define IGC_WUPM_BYTES	128

/* Loop limit on how long we wait for auto-negotiation to complete */
#define COPPER_LINK_UP_LIMIT		10
#define PHY_AUTO_NEG_LIMIT		45

/* Number of 100 microseconds we wait for PCI Express master disable */
#define MASTER_DISABLE_TIMEOUT		800
/*Blocks new Master requests */
#define IGC_CTRL_GIO_MASTER_DISABLE	0x00000004
/* Status of Master requests. */
#define IGC_STATUS_GIO_MASTER_ENABLE	0x00080000

/* Receive Address
 * Number of high/low register pairs in the RAR. The RAR (Receive Address
 * Registers) holds the directed and multicast addresses that we monitor.
 * Technically, we have 16 spots.  However, we reserve one of these spots
 * (RAR[15]) for our directed address used by controllers with
 * manageability enabled, allowing us room for 15 multicast addresses.
 */
#define IGC_RAH_RAH_MASK	0x0000FFFF
#define IGC_RAH_ASEL_MASK	0x00030000
#define IGC_RAH_ASEL_SRC_ADDR	BIT(16)
#define IGC_RAH_QSEL_MASK	0x000C0000
#define IGC_RAH_QSEL_SHIFT	18
#define IGC_RAH_QSEL_ENABLE	BIT(28)
#define IGC_RAH_AV		0x80000000 /* Receive descriptor valid */

#define IGC_RAL_MAC_ADDR_LEN	4
#define IGC_RAH_MAC_ADDR_LEN	2

/* Error Codes */
#define IGC_SUCCESS			0
#define IGC_ERR_NVM			1
#define IGC_ERR_PHY			2
#define IGC_ERR_CONFIG			3
#define IGC_ERR_PARAM			4
#define IGC_ERR_MAC_INIT		5
#define IGC_ERR_RESET			9
#define IGC_ERR_MASTER_REQUESTS_PENDING	10
#define IGC_ERR_BLK_PHY_RESET		12
#define IGC_ERR_SWFW_SYNC		13

/* Device Control */
#define IGC_CTRL_DEV_RST	0x20000000  /* Device reset */

#define IGC_CTRL_PHY_RST	0x80000000  /* PHY Reset */
#define IGC_CTRL_SLU		0x00000040  /* Set link up (Force Link) */
#define IGC_CTRL_FRCSPD		0x00000800  /* Force Speed */
#define IGC_CTRL_FRCDPX		0x00001000  /* Force Duplex */

#define IGC_CTRL_RFCE		0x08000000  /* Receive Flow Control enable */
#define IGC_CTRL_TFCE		0x10000000  /* Transmit flow control enable */

/* As per the EAS the maximum supported size is 9.5KB (9728 bytes) */
#define MAX_JUMBO_FRAME_SIZE	0x2600

/* PBA constants */
#define IGC_PBA_34K		0x0022

/* SW Semaphore Register */
#define IGC_SWSM_SMBI		0x00000001 /* Driver Semaphore bit */
#define IGC_SWSM_SWESMBI	0x00000002 /* FW Semaphore bit */

/* SWFW_SYNC Definitions */
#define IGC_SWFW_EEP_SM		0x1
#define IGC_SWFW_PHY0_SM	0x2

/* Autoneg Advertisement Register */
#define NWAY_AR_10T_HD_CAPS	0x0020   /* 10T   Half Duplex Capable */
#define NWAY_AR_10T_FD_CAPS	0x0040   /* 10T   Full Duplex Capable */
#define NWAY_AR_100TX_HD_CAPS	0x0080   /* 100TX Half Duplex Capable */
#define NWAY_AR_100TX_FD_CAPS	0x0100   /* 100TX Full Duplex Capable */
#define NWAY_AR_PAUSE		0x0400   /* Pause operation desired */
#define NWAY_AR_ASM_DIR		0x0800   /* Asymmetric Pause Direction bit */

/* Link Partner Ability Register (Base Page) */
#define NWAY_LPAR_PAUSE		0x0400 /* LP Pause operation desired */
#define NWAY_LPAR_ASM_DIR	0x0800 /* LP Asymmetric Pause Direction bit */

/* 1000BASE-T Control Register */
#define CR_1000T_ASYM_PAUSE	0x0080 /* Advertise asymmetric pause bit */
#define CR_1000T_HD_CAPS	0x0100 /* Advertise 1000T HD capability */
#define CR_1000T_FD_CAPS	0x0200 /* Advertise 1000T FD capability  */

/* 1000BASE-T Status Register */
#define SR_1000T_REMOTE_RX_STATUS	0x1000 /* Remote receiver OK */
#define SR_1000T_LOCAL_RX_STATUS	0x2000 /* Local receiver OK */

/* PHY GPY 211 registers */
#define STANDARD_AN_REG_MASK	0x0007 /* MMD */
#define ANEG_MULTIGBT_AN_CTRL	0x0020 /* MULTI GBT AN Control Register */
#define MMD_DEVADDR_SHIFT	16     /* Shift MMD to higher bits */
#define CR_2500T_FD_CAPS	0x0080 /* Advertise 2500T FD capability */

/* NVM Control */
/* Number of milliseconds for NVM auto read done after MAC reset. */
#define AUTO_READ_DONE_TIMEOUT		10
#define IGC_EECD_AUTO_RD		0x00000200  /* NVM Auto Read done */
#define IGC_EECD_REQ		0x00000040 /* NVM Access Request */
#define IGC_EECD_GNT		0x00000080 /* NVM Access Grant */
/* NVM Addressing bits based on type 0=small, 1=large */
#define IGC_EECD_ADDR_BITS		0x00000400
#define IGC_NVM_GRANT_ATTEMPTS		1000 /* NVM # attempts to gain grant */
#define IGC_EECD_SIZE_EX_MASK		0x00007800  /* NVM Size */
#define IGC_EECD_SIZE_EX_SHIFT		11
#define IGC_EECD_FLUPD_I225		0x00800000 /* Update FLASH */
#define IGC_EECD_FLUDONE_I225		0x04000000 /* Update FLASH done*/
#define IGC_EECD_FLASH_DETECTED_I225	0x00080000 /* FLASH detected */
#define IGC_FLUDONE_ATTEMPTS		20000
#define IGC_EERD_EEWR_MAX_COUNT		512 /* buffered EEPROM words rw */

/* Offset to data in NVM read/write registers */
#define IGC_NVM_RW_REG_DATA	16
#define IGC_NVM_RW_REG_DONE	2    /* Offset to READ/WRITE done bit */
#define IGC_NVM_RW_REG_START	1    /* Start operation */
#define IGC_NVM_RW_ADDR_SHIFT	2    /* Shift to the address bits */
#define IGC_NVM_POLL_READ	0    /* Flag for polling for read complete */

/* NVM Word Offsets */
#define NVM_CHECKSUM_REG		0x003F

/* For checksumming, the sum of all words in the NVM should equal 0xBABA. */
#define NVM_SUM				0xBABA
#define NVM_WORD_SIZE_BASE_SHIFT	6

/* Collision related configuration parameters */
#define IGC_COLLISION_THRESHOLD		15
#define IGC_CT_SHIFT			4
#define IGC_COLLISION_DISTANCE		63
#define IGC_COLD_SHIFT			12

/* Device Status */
#define IGC_STATUS_FD		0x00000001      /* Full duplex.0=half,1=full */
#define IGC_STATUS_LU		0x00000002      /* Link up.0=no,1=link */
#define IGC_STATUS_FUNC_MASK	0x0000000C      /* PCI Function Mask */
#define IGC_STATUS_FUNC_SHIFT	2
#define IGC_STATUS_FUNC_1	0x00000004      /* Function 1 */
#define IGC_STATUS_TXOFF	0x00000010      /* transmission paused */
#define IGC_STATUS_SPEED_100	0x00000040      /* Speed 100Mb/s */
#define IGC_STATUS_SPEED_1000	0x00000080      /* Speed 1000Mb/s */
#define IGC_STATUS_SPEED_2500	0x00400000	/* Speed 2.5Gb/s */

#define SPEED_10		10
#define SPEED_100		100
#define SPEED_1000		1000
#define SPEED_2500		2500
#define HALF_DUPLEX		1
#define FULL_DUPLEX		2

/* 1Gbps and 2.5Gbps half duplex is not supported, nor spec-compliant. */
#define ADVERTISE_10_HALF		0x0001
#define ADVERTISE_10_FULL		0x0002
#define ADVERTISE_100_HALF		0x0004
#define ADVERTISE_100_FULL		0x0008
#define ADVERTISE_1000_HALF		0x0010 /* Not used, just FYI */
#define ADVERTISE_1000_FULL		0x0020
#define ADVERTISE_2500_HALF		0x0040 /* Not used, just FYI */
#define ADVERTISE_2500_FULL		0x0080

#define IGC_ALL_SPEED_DUPLEX_2500 ( \
	ADVERTISE_10_HALF | ADVERTISE_10_FULL | ADVERTISE_100_HALF | \
	ADVERTISE_100_FULL | ADVERTISE_1000_FULL | ADVERTISE_2500_FULL)

#define AUTONEG_ADVERTISE_SPEED_DEFAULT_2500	IGC_ALL_SPEED_DUPLEX_2500

/* Interrupt Cause Read */
#define IGC_ICR_TXDW		BIT(0)	/* Transmit desc written back */
#define IGC_ICR_TXQE		BIT(1)	/* Transmit Queue empty */
#define IGC_ICR_LSC		BIT(2)	/* Link Status Change */
#define IGC_ICR_RXSEQ		BIT(3)	/* Rx sequence error */
#define IGC_ICR_RXDMT0		BIT(4)	/* Rx desc min. threshold (0) */
#define IGC_ICR_RXO		BIT(6)	/* Rx overrun */
#define IGC_ICR_RXT0		BIT(7)	/* Rx timer intr (ring 0) */
#define IGC_ICR_TS		BIT(19)	/* Time Sync Interrupt */
#define IGC_ICR_DRSTA		BIT(30)	/* Device Reset Asserted */

/* If this bit asserted, the driver should claim the interrupt */
#define IGC_ICR_INT_ASSERTED	BIT(31)

#define IGC_ICS_RXT0		IGC_ICR_RXT0 /* Rx timer intr */

#define IMS_ENABLE_MASK ( \
	IGC_IMS_RXT0   |    \
	IGC_IMS_TXDW   |    \
	IGC_IMS_RXDMT0 |    \
	IGC_IMS_RXSEQ  |    \
	IGC_IMS_LSC)

/* Interrupt Mask Set */
#define IGC_IMS_TXDW		IGC_ICR_TXDW	/* Tx desc written back */
#define IGC_IMS_RXSEQ		IGC_ICR_RXSEQ	/* Rx sequence error */
#define IGC_IMS_LSC		IGC_ICR_LSC	/* Link Status Change */
#define IGC_IMS_DOUTSYNC	IGC_ICR_DOUTSYNC /* NIC DMA out of sync */
#define IGC_IMS_DRSTA		IGC_ICR_DRSTA	/* Device Reset Asserted */
#define IGC_IMS_RXT0		IGC_ICR_RXT0	/* Rx timer intr */
#define IGC_IMS_RXDMT0		IGC_ICR_RXDMT0	/* Rx desc min. threshold */
#define IGC_IMS_TS		IGC_ICR_TS	/* Time Sync Interrupt */

#define IGC_QVECTOR_MASK	0x7FFC		/* Q-vector mask */
#define IGC_ITR_VAL_MASK	0x04		/* ITR value mask */

/* Interrupt Cause Set */
#define IGC_ICS_LSC		IGC_ICR_LSC       /* Link Status Change */
#define IGC_ICS_RXDMT0		IGC_ICR_RXDMT0    /* rx desc min. threshold */

#define IGC_ICR_DOUTSYNC	0x10000000 /* NIC DMA out of sync */
#define IGC_EITR_CNT_IGNR	0x80000000 /* Don't reset counters on write */
#define IGC_IVAR_VALID		0x80
#define IGC_GPIE_NSICR		0x00000001
#define IGC_GPIE_MSIX_MODE	0x00000010
#define IGC_GPIE_EIAME		0x40000000
#define IGC_GPIE_PBA		0x80000000

/* Receive Descriptor bit definitions */
#define IGC_RXD_STAT_DD		0x01    /* Descriptor Done */

/* Transmit Descriptor bit definitions */
#define IGC_TXD_DTYP_D		0x00100000 /* Data Descriptor */
#define IGC_TXD_DTYP_C		0x00000000 /* Context Descriptor */
#define IGC_TXD_POPTS_IXSM	0x01       /* Insert IP checksum */
#define IGC_TXD_POPTS_TXSM	0x02       /* Insert TCP/UDP checksum */
#define IGC_TXD_CMD_EOP		0x01000000 /* End of Packet */
#define IGC_TXD_CMD_IC		0x04000000 /* Insert Checksum */
#define IGC_TXD_CMD_DEXT	0x20000000 /* Desc extension (0 = legacy) */
#define IGC_TXD_CMD_VLE		0x40000000 /* Add VLAN tag */
#define IGC_TXD_STAT_DD		0x00000001 /* Descriptor Done */
#define IGC_TXD_CMD_TCP		0x01000000 /* TCP packet */
#define IGC_TXD_CMD_IP		0x02000000 /* IP packet */
#define IGC_TXD_CMD_TSE		0x04000000 /* TCP Seg enable */
#define IGC_TXD_EXTCMD_TSTAMP	0x00000010 /* IEEE1588 Timestamp packet */

/* IPSec Encrypt Enable */
#define IGC_ADVTXD_L4LEN_SHIFT	8  /* Adv ctxt L4LEN shift */
#define IGC_ADVTXD_MSS_SHIFT	16 /* Adv ctxt MSS shift */

/* Transmit Control */
#define IGC_TCTL_EN		0x00000002 /* enable Tx */
#define IGC_TCTL_PSP		0x00000008 /* pad short packets */
#define IGC_TCTL_CT		0x00000ff0 /* collision threshold */
#define IGC_TCTL_COLD		0x003ff000 /* collision distance */
#define IGC_TCTL_RTLC		0x01000000 /* Re-transmit on late collision */
#define IGC_TCTL_MULR		0x10000000 /* Multiple request support */

/* Flow Control Constants */
#define FLOW_CONTROL_ADDRESS_LOW	0x00C28001
#define FLOW_CONTROL_ADDRESS_HIGH	0x00000100
#define FLOW_CONTROL_TYPE		0x8808
/* Enable XON frame transmission */
#define IGC_FCRTL_XONE			0x80000000

/* Management Control */
#define IGC_MANC_RCV_TCO_EN	0x00020000 /* Receive TCO Packets Enabled */
#define IGC_MANC_BLK_PHY_RST_ON_IDE	0x00040000 /* Block phy resets */

/* Receive Control */
#define IGC_RCTL_RST		0x00000001 /* Software reset */
#define IGC_RCTL_EN		0x00000002 /* enable */
#define IGC_RCTL_SBP		0x00000004 /* store bad packet */
#define IGC_RCTL_UPE		0x00000008 /* unicast promisc enable */
#define IGC_RCTL_MPE		0x00000010 /* multicast promisc enable */
#define IGC_RCTL_LPE		0x00000020 /* long packet enable */
#define IGC_RCTL_LBM_MAC	0x00000040 /* MAC loopback mode */
#define IGC_RCTL_LBM_TCVR	0x000000C0 /* tcvr loopback mode */

#define IGC_RCTL_RDMTS_HALF	0x00000000 /* Rx desc min thresh size */
#define IGC_RCTL_BAM		0x00008000 /* broadcast enable */

/* Split Replication Receive Control */
#define IGC_SRRCTL_TIMESTAMP		0x40000000
#define IGC_SRRCTL_TIMER1SEL(timer)	(((timer) & 0x3) << 14)
#define IGC_SRRCTL_TIMER0SEL(timer)	(((timer) & 0x3) << 17)

/* Receive Descriptor bit definitions */
#define IGC_RXD_STAT_EOP	0x02	/* End of Packet */
#define IGC_RXD_STAT_IXSM	0x04	/* Ignore checksum */
#define IGC_RXD_STAT_UDPCS	0x10	/* UDP xsum calculated */
#define IGC_RXD_STAT_TCPCS	0x20	/* TCP xsum calculated */

/* Advanced Receive Descriptor bit definitions */
#define IGC_RXDADV_STAT_TSIP	0x08000 /* timestamp in packet */
#define IGC_RXDADV_STAT_TS	0x10000 /* Pkt was time stamped */

#define IGC_RXDEXT_STATERR_CE		0x01000000
#define IGC_RXDEXT_STATERR_SE		0x02000000
#define IGC_RXDEXT_STATERR_SEQ		0x04000000
#define IGC_RXDEXT_STATERR_CXE		0x10000000
#define IGC_RXDEXT_STATERR_TCPE		0x20000000
#define IGC_RXDEXT_STATERR_IPE		0x40000000
#define IGC_RXDEXT_STATERR_RXE		0x80000000

/* Same mask, but for extended and packet split descriptors */
#define IGC_RXDEXT_ERR_FRAME_ERR_MASK ( \
	IGC_RXDEXT_STATERR_CE  |	\
	IGC_RXDEXT_STATERR_SE  |	\
	IGC_RXDEXT_STATERR_SEQ |	\
	IGC_RXDEXT_STATERR_CXE |	\
	IGC_RXDEXT_STATERR_RXE)

#define IGC_MRQC_RSS_FIELD_IPV4_TCP	0x00010000
#define IGC_MRQC_RSS_FIELD_IPV4		0x00020000
#define IGC_MRQC_RSS_FIELD_IPV6_TCP_EX	0x00040000
#define IGC_MRQC_RSS_FIELD_IPV6		0x00100000
#define IGC_MRQC_RSS_FIELD_IPV6_TCP	0x00200000

/* Header split receive */
#define IGC_RFCTL_IPV6_EX_DIS	0x00010000
#define IGC_RFCTL_LEF		0x00040000

#define IGC_RCTL_SZ_256		0x00030000 /* Rx buffer size 256 */

#define IGC_RCTL_MO_SHIFT	12 /* multicast offset shift */
#define IGC_RCTL_CFIEN		0x00080000 /* canonical form enable */
#define IGC_RCTL_DPF		0x00400000 /* discard pause frames */
#define IGC_RCTL_PMCF		0x00800000 /* pass MAC control frames */
#define IGC_RCTL_SECRC		0x04000000 /* Strip Ethernet CRC */

#define I225_RXPBSIZE_DEFAULT	0x000000A2 /* RXPBSIZE default */
#define I225_TXPBSIZE_DEFAULT	0x04000014 /* TXPBSIZE default */
#define IGC_RXPBS_CFG_TS_EN	0x80000000 /* Timestamp in Rx buffer */

#define IGC_TXPBSIZE_TSN	0x04145145 /* 5k bytes buffer for each queue */

#define IGC_DTXMXPKTSZ_TSN	0x19 /* 1600 bytes of max TX DMA packet size */
#define IGC_DTXMXPKTSZ_DEFAULT	0x98 /* 9728-byte Jumbo frames */

/* Time Sync Interrupt Causes */
#define IGC_TSICR_SYS_WRAP	BIT(0) /* SYSTIM Wrap around. */
#define IGC_TSICR_TXTS		BIT(1) /* Transmit Timestamp. */
#define IGC_TSICR_TT0		BIT(3) /* Target Time 0 Trigger. */
#define IGC_TSICR_TT1		BIT(4) /* Target Time 1 Trigger. */
#define IGC_TSICR_AUTT0		BIT(5) /* Auxiliary Timestamp 0 Taken. */
#define IGC_TSICR_AUTT1		BIT(6) /* Auxiliary Timestamp 1 Taken. */

#define IGC_TSICR_INTERRUPTS	IGC_TSICR_TXTS

#define IGC_FTQF_VF_BP		0x00008000
#define IGC_FTQF_1588_TIME_STAMP	0x08000000
#define IGC_FTQF_MASK			0xF0000000
#define IGC_FTQF_MASK_PROTO_BP	0x10000000

/* Time Sync Receive Control bit definitions */
#define IGC_TSYNCRXCTL_VALID		0x00000001  /* Rx timestamp valid */
#define IGC_TSYNCRXCTL_TYPE_MASK	0x0000000E  /* Rx type mask */
#define IGC_TSYNCRXCTL_TYPE_L2_V2	0x00
#define IGC_TSYNCRXCTL_TYPE_L4_V1	0x02
#define IGC_TSYNCRXCTL_TYPE_L2_L4_V2	0x04
#define IGC_TSYNCRXCTL_TYPE_ALL		0x08
#define IGC_TSYNCRXCTL_TYPE_EVENT_V2	0x0A
#define IGC_TSYNCRXCTL_ENABLED		0x00000010  /* enable Rx timestamping */
#define IGC_TSYNCRXCTL_SYSCFI		0x00000020  /* Sys clock frequency */
#define IGC_TSYNCRXCTL_RXSYNSIG		0x00000400  /* Sample RX tstamp in PHY sop */

/* Time Sync Receive Configuration */
#define IGC_TSYNCRXCFG_PTP_V1_CTRLT_MASK	0x000000FF
#define IGC_TSYNCRXCFG_PTP_V1_SYNC_MESSAGE	0x00
#define IGC_TSYNCRXCFG_PTP_V1_DELAY_REQ_MESSAGE	0x01

/* Immediate Interrupt Receive */
#define IGC_IMIR_CLEAR_MASK	0xF001FFFF /* IMIR Reg Clear Mask */
#define IGC_IMIR_PORT_BYPASS	0x20000 /* IMIR Port Bypass Bit */
#define IGC_IMIR_PRIORITY_SHIFT	29 /* IMIR Priority Shift */
#define IGC_IMIREXT_CLEAR_MASK	0x7FFFF /* IMIREXT Reg Clear Mask */

/* Immediate Interrupt Receive Extended */
#define IGC_IMIREXT_CTRL_BP	0x00080000  /* Bypass check of ctrl bits */
#define IGC_IMIREXT_SIZE_BP	0x00001000  /* Packet size bypass */

/* Time Sync Transmit Control bit definitions */
#define IGC_TSYNCTXCTL_VALID			0x00000001  /* Tx timestamp valid */
#define IGC_TSYNCTXCTL_ENABLED			0x00000010  /* enable Tx timestamping */
#define IGC_TSYNCTXCTL_MAX_ALLOWED_DLY_MASK	0x0000F000  /* max delay */
#define IGC_TSYNCTXCTL_SYNC_COMP_ERR		0x20000000  /* sync err */
#define IGC_TSYNCTXCTL_SYNC_COMP		0x40000000  /* sync complete */
#define IGC_TSYNCTXCTL_START_SYNC		0x80000000  /* initiate sync */
#define IGC_TSYNCTXCTL_TXSYNSIG			0x00000020  /* Sample TX tstamp in PHY sop */

/* Transmit Scheduling */
#define IGC_TQAVCTRL_TRANSMIT_MODE_TSN	0x00000001
#define IGC_TQAVCTRL_ENHANCED_QAV	0x00000008

#define IGC_TXQCTL_QUEUE_MODE_LAUNCHT	0x00000001
#define IGC_TXQCTL_STRICT_CYCLE		0x00000002
#define IGC_TXQCTL_STRICT_END		0x00000004

/* Receive Checksum Control */
#define IGC_RXCSUM_CRCOFL	0x00000800   /* CRC32 offload enable */
#define IGC_RXCSUM_PCSD		0x00002000   /* packet checksum disabled */

/* GPY211 - I225 defines */
#define GPY_MMD_MASK		0xFFFF0000
#define GPY_MMD_SHIFT		16
#define GPY_REG_MASK		0x0000FFFF

#define IGC_MMDAC_FUNC_DATA	0x4000 /* Data, no post increment */

/* MAC definitions */
#define IGC_FACTPS_MNGCG	0x20000000
#define IGC_FWSM_MODE_MASK	0xE
#define IGC_FWSM_MODE_SHIFT	1

/* Management Control */
#define IGC_MANC_SMBUS_EN	0x00000001 /* SMBus Enabled - RO */
#define IGC_MANC_ASF_EN		0x00000002 /* ASF Enabled - RO */

/* PHY */
#define PHY_REVISION_MASK	0xFFFFFFF0
#define MAX_PHY_REG_ADDRESS	0x1F  /* 5 bit address bus (0-0x1F) */
#define IGC_GEN_POLL_TIMEOUT	1920

/* PHY Control Register */
#define MII_CR_FULL_DUPLEX	0x0100  /* FDX =1, half duplex =0 */
#define MII_CR_RESTART_AUTO_NEG	0x0200  /* Restart auto negotiation */
#define MII_CR_POWER_DOWN	0x0800  /* Power down */
#define MII_CR_AUTO_NEG_EN	0x1000  /* Auto Neg Enable */
#define MII_CR_LOOPBACK		0x4000  /* 0 = normal, 1 = loopback */
#define MII_CR_RESET		0x8000  /* 0 = normal, 1 = PHY reset */
#define MII_CR_SPEED_1000	0x0040
#define MII_CR_SPEED_100	0x2000
#define MII_CR_SPEED_10		0x0000

/* PHY Status Register */
#define MII_SR_LINK_STATUS	0x0004 /* Link Status 1 = link */
#define MII_SR_AUTONEG_COMPLETE	0x0020 /* Auto Neg Complete */
#define IGC_PHY_RST_COMP	0x0100 /* Internal PHY reset completion */

/* PHY 1000 MII Register/Bit Definitions */
/* PHY Registers defined by IEEE */
#define PHY_CONTROL		0x00 /* Control Register */
#define PHY_STATUS		0x01 /* Status Register */
#define PHY_ID1			0x02 /* Phy Id Reg (word 1) */
#define PHY_ID2			0x03 /* Phy Id Reg (word 2) */
#define PHY_AUTONEG_ADV		0x04 /* Autoneg Advertisement */
#define PHY_LP_ABILITY		0x05 /* Link Partner Ability (Base Page) */
#define PHY_1000T_CTRL		0x09 /* 1000Base-T Control Reg */
#define PHY_1000T_STATUS	0x0A /* 1000Base-T Status Reg */

/* Bit definitions for valid PHY IDs. I = Integrated E = External */
#define I225_I_PHY_ID		0x67C9DC00

/* MDI Control */
#define IGC_MDIC_DATA_MASK	0x0000FFFF
#define IGC_MDIC_REG_MASK	0x001F0000
#define IGC_MDIC_REG_SHIFT	16
#define IGC_MDIC_PHY_MASK	0x03E00000
#define IGC_MDIC_PHY_SHIFT	21
#define IGC_MDIC_OP_WRITE	0x04000000
#define IGC_MDIC_OP_READ	0x08000000
#define IGC_MDIC_READY		0x10000000
#define IGC_MDIC_INT_EN		0x20000000
#define IGC_MDIC_ERROR		0x40000000

#define IGC_N0_QUEUE		-1

#define IGC_MAX_MAC_HDR_LEN	127
#define IGC_MAX_NETWORK_HDR_LEN	511

#define IGC_VLANPQF_QSEL(_n, q_idx) ((q_idx) << ((_n) * 4))
#define IGC_VLANPQF_VALID(_n)	(0x1 << (3 + (_n) * 4))
#define IGC_VLANPQF_QUEUE_MASK	0x03

#define IGC_ADVTXD_MACLEN_SHIFT		9  /* Adv ctxt desc mac len shift */
#define IGC_ADVTXD_TUCMD_IPV4		0x00000400  /* IP Packet Type:1=IPv4 */
#define IGC_ADVTXD_TUCMD_L4T_TCP	0x00000800  /* L4 Packet Type of TCP */
#define IGC_ADVTXD_TUCMD_L4T_SCTP	0x00001000 /* L4 packet TYPE of SCTP */

/* Maximum size of the MTA register table in all supported adapters */
#define MAX_MTA_REG			128

#endif /* _IGC_DEFINES_H_ */
