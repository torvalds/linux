/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2018 Intel Corporation */

#ifndef _IGC_REGS_H_
#define _IGC_REGS_H_

/* General Register Descriptions */
#define IGC_CTRL		0x00000  /* Device Control - RW */
#define IGC_STATUS		0x00008  /* Device Status - RO */
#define IGC_EECD		0x00010  /* EEPROM/Flash Control - RW */
#define IGC_CTRL_EXT		0x00018  /* Extended Device Control - RW */
#define IGC_MDIC		0x00020  /* MDI Control - RW */
#define IGC_MDICNFG		0x00E04  /* MDC/MDIO Configuration - RW */
#define IGC_CONNSW		0x00034  /* Copper/Fiber switch control - RW */
#define IGC_I225_PHPM		0x00E14  /* I225 PHY Power Management */

/* Internal Packet Buffer Size Registers */
#define IGC_RXPBS		0x02404  /* Rx Packet Buffer Size - RW */
#define IGC_TXPBS		0x03404  /* Tx Packet Buffer Size - RW */

/* NVM  Register Descriptions */
#define IGC_EERD		0x12014  /* EEprom mode read - RW */
#define IGC_EEWR		0x12018  /* EEprom mode write - RW */

/* Flow Control Register Descriptions */
#define IGC_FCAL		0x00028  /* FC Address Low - RW */
#define IGC_FCAH		0x0002C  /* FC Address High - RW */
#define IGC_FCT			0x00030  /* FC Type - RW */
#define IGC_FCTTV		0x00170  /* FC Transmit Timer - RW */
#define IGC_FCRTL		0x02160  /* FC Receive Threshold Low - RW */
#define IGC_FCRTH		0x02168  /* FC Receive Threshold High - RW */
#define IGC_FCRTV		0x02460  /* FC Refresh Timer Value - RW */

/* Semaphore registers */
#define IGC_SW_FW_SYNC		0x05B5C  /* SW-FW Synchronization - RW */
#define IGC_SWSM		0x05B50  /* SW Semaphore */
#define IGC_FWSM		0x05B54  /* FW Semaphore */

/* Function Active and Power State to MNG */
#define IGC_FACTPS		0x05B30

/* Interrupt Register Description */
#define IGC_EICR		0x01580  /* Ext. Interrupt Cause read - W0 */
#define IGC_EICS		0x01520  /* Ext. Interrupt Cause Set - W0 */
#define IGC_EIMS		0x01524  /* Ext. Interrupt Mask Set/Read - RW */
#define IGC_EIMC		0x01528  /* Ext. Interrupt Mask Clear - WO */
#define IGC_EIAC		0x0152C  /* Ext. Interrupt Auto Clear - RW */
#define IGC_EIAM		0x01530  /* Ext. Interrupt Auto Mask - RW */
#define IGC_ICR			0x01500  /* Intr Cause Read - RC/W1C */
#define IGC_ICS			0x01504  /* Intr Cause Set - WO */
#define IGC_IMS			0x01508  /* Intr Mask Set/Read - RW */
#define IGC_IMC			0x0150C  /* Intr Mask Clear - WO */
#define IGC_IAM			0x01510  /* Intr Ack Auto Mask- RW */
/* Intr Throttle - RW */
#define IGC_EITR(_n)		(0x01680 + (0x4 * (_n)))
/* Interrupt Vector Allocation - RW */
#define IGC_IVAR0		0x01700
#define IGC_IVAR_MISC		0x01740  /* IVAR for "other" causes - RW */
#define IGC_GPIE		0x01514  /* General Purpose Intr Enable - RW */

/* MSI-X Table Register Descriptions */
#define IGC_PBACL		0x05B68  /* MSIx PBA Clear - R/W 1 to clear */

/* RSS registers */
#define IGC_MRQC		0x05818 /* Multiple Receive Control - RW */

/* Filtering Registers */
#define IGC_ETQF(_n)		(0x05CB0 + (4 * (_n))) /* EType Queue Fltr */

/* ETQF register bit definitions */
#define IGC_ETQF_FILTER_ENABLE	BIT(26)
#define IGC_ETQF_QUEUE_ENABLE	BIT(31)
#define IGC_ETQF_QUEUE_SHIFT	16
#define IGC_ETQF_QUEUE_MASK	0x00070000
#define IGC_ETQF_ETYPE_MASK	0x0000FFFF

/* Redirection Table - RW Array */
#define IGC_RETA(_i)		(0x05C00 + ((_i) * 4))
/* RSS Random Key - RW Array */
#define IGC_RSSRK(_i)		(0x05C80 + ((_i) * 4))

/* Receive Register Descriptions */
#define IGC_RCTL		0x00100  /* Rx Control - RW */
#define IGC_SRRCTL(_n)		(0x0C00C + ((_n) * 0x40))
#define IGC_PSRTYPE(_i)		(0x05480 + ((_i) * 4))
#define IGC_RDBAL(_n)		(0x0C000 + ((_n) * 0x40))
#define IGC_RDBAH(_n)		(0x0C004 + ((_n) * 0x40))
#define IGC_RDLEN(_n)		(0x0C008 + ((_n) * 0x40))
#define IGC_RDH(_n)		(0x0C010 + ((_n) * 0x40))
#define IGC_RDT(_n)		(0x0C018 + ((_n) * 0x40))
#define IGC_RXDCTL(_n)		(0x0C028 + ((_n) * 0x40))
#define IGC_RQDPC(_n)		(0x0C030 + ((_n) * 0x40))
#define IGC_RXCSUM		0x05000  /* Rx Checksum Control - RW */
#define IGC_RLPML		0x05004  /* Rx Long Packet Max Length */
#define IGC_RFCTL		0x05008  /* Receive Filter Control*/
#define IGC_MTA			0x05200  /* Multicast Table Array - RW Array */
#define IGC_RA			0x05400  /* Receive Address - RW Array */
#define IGC_UTA			0x0A000  /* Unicast Table Array - RW */
#define IGC_RAL(_n)		(0x05400 + ((_n) * 0x08))
#define IGC_RAH(_n)		(0x05404 + ((_n) * 0x08))
#define IGC_VLANPQF		0x055B0  /* VLAN Priority Queue Filter - RW */

/* Transmit Register Descriptions */
#define IGC_TCTL		0x00400  /* Tx Control - RW */
#define IGC_TIPG		0x00410  /* Tx Inter-packet gap - RW */
#define IGC_TDBAL(_n)		(0x0E000 + ((_n) * 0x40))
#define IGC_TDBAH(_n)		(0x0E004 + ((_n) * 0x40))
#define IGC_TDLEN(_n)		(0x0E008 + ((_n) * 0x40))
#define IGC_TDH(_n)		(0x0E010 + ((_n) * 0x40))
#define IGC_TDT(_n)		(0x0E018 + ((_n) * 0x40))
#define IGC_TXDCTL(_n)		(0x0E028 + ((_n) * 0x40))

/* MMD Register Descriptions */
#define IGC_MMDAC		13 /* MMD Access Control */
#define IGC_MMDAAD		14 /* MMD Access Address/Data */

/* Statistics Register Descriptions */
#define IGC_CRCERRS	0x04000  /* CRC Error Count - R/clr */
#define IGC_ALGNERRC	0x04004  /* Alignment Error Count - R/clr */
#define IGC_RXERRC	0x0400C  /* Receive Error Count - R/clr */
#define IGC_MPC		0x04010  /* Missed Packet Count - R/clr */
#define IGC_SCC		0x04014  /* Single Collision Count - R/clr */
#define IGC_ECOL	0x04018  /* Excessive Collision Count - R/clr */
#define IGC_MCC		0x0401C  /* Multiple Collision Count - R/clr */
#define IGC_LATECOL	0x04020  /* Late Collision Count - R/clr */
#define IGC_COLC	0x04028  /* Collision Count - R/clr */
#define IGC_RERC	0x0402C  /* Receive Error Count - R/clr */
#define IGC_DC		0x04030  /* Defer Count - R/clr */
#define IGC_TNCRS	0x04034  /* Tx-No CRS - R/clr */
#define IGC_HTDPMC	0x0403C  /* Host Transmit Discarded by MAC - R/clr */
#define IGC_RLEC	0x04040  /* Receive Length Error Count - R/clr */
#define IGC_XONRXC	0x04048  /* XON Rx Count - R/clr */
#define IGC_XONTXC	0x0404C  /* XON Tx Count - R/clr */
#define IGC_XOFFRXC	0x04050  /* XOFF Rx Count - R/clr */
#define IGC_XOFFTXC	0x04054  /* XOFF Tx Count - R/clr */
#define IGC_FCRUC	0x04058  /* Flow Control Rx Unsupported Count- R/clr */
#define IGC_PRC64	0x0405C  /* Packets Rx (64 bytes) - R/clr */
#define IGC_PRC127	0x04060  /* Packets Rx (65-127 bytes) - R/clr */
#define IGC_PRC255	0x04064  /* Packets Rx (128-255 bytes) - R/clr */
#define IGC_PRC511	0x04068  /* Packets Rx (255-511 bytes) - R/clr */
#define IGC_PRC1023	0x0406C  /* Packets Rx (512-1023 bytes) - R/clr */
#define IGC_PRC1522	0x04070  /* Packets Rx (1024-1522 bytes) - R/clr */
#define IGC_GPRC	0x04074  /* Good Packets Rx Count - R/clr */
#define IGC_BPRC	0x04078  /* Broadcast Packets Rx Count - R/clr */
#define IGC_MPRC	0x0407C  /* Multicast Packets Rx Count - R/clr */
#define IGC_GPTC	0x04080  /* Good Packets Tx Count - R/clr */
#define IGC_GORCL	0x04088  /* Good Octets Rx Count Low - R/clr */
#define IGC_GORCH	0x0408C  /* Good Octets Rx Count High - R/clr */
#define IGC_GOTCL	0x04090  /* Good Octets Tx Count Low - R/clr */
#define IGC_GOTCH	0x04094  /* Good Octets Tx Count High - R/clr */
#define IGC_RNBC	0x040A0  /* Rx No Buffers Count - R/clr */
#define IGC_RUC		0x040A4  /* Rx Undersize Count - R/clr */
#define IGC_RFC		0x040A8  /* Rx Fragment Count - R/clr */
#define IGC_ROC		0x040AC  /* Rx Oversize Count - R/clr */
#define IGC_RJC		0x040B0  /* Rx Jabber Count - R/clr */
#define IGC_MGTPRC	0x040B4  /* Management Packets Rx Count - R/clr */
#define IGC_MGTPDC	0x040B8  /* Management Packets Dropped Count - R/clr */
#define IGC_MGTPTC	0x040BC  /* Management Packets Tx Count - R/clr */
#define IGC_TORL	0x040C0  /* Total Octets Rx Low - R/clr */
#define IGC_TORH	0x040C4  /* Total Octets Rx High - R/clr */
#define IGC_TOTL	0x040C8  /* Total Octets Tx Low - R/clr */
#define IGC_TOTH	0x040CC  /* Total Octets Tx High - R/clr */
#define IGC_TPR		0x040D0  /* Total Packets Rx - R/clr */
#define IGC_TPT		0x040D4  /* Total Packets Tx - R/clr */
#define IGC_PTC64	0x040D8  /* Packets Tx (64 bytes) - R/clr */
#define IGC_PTC127	0x040DC  /* Packets Tx (65-127 bytes) - R/clr */
#define IGC_PTC255	0x040E0  /* Packets Tx (128-255 bytes) - R/clr */
#define IGC_PTC511	0x040E4  /* Packets Tx (256-511 bytes) - R/clr */
#define IGC_PTC1023	0x040E8  /* Packets Tx (512-1023 bytes) - R/clr */
#define IGC_PTC1522	0x040EC  /* Packets Tx (1024-1522 Bytes) - R/clr */
#define IGC_MPTC	0x040F0  /* Multicast Packets Tx Count - R/clr */
#define IGC_BPTC	0x040F4  /* Broadcast Packets Tx Count - R/clr */
#define IGC_TSCTC	0x040F8  /* TCP Segmentation Context Tx - R/clr */
#define IGC_IAC		0x04100  /* Interrupt Assertion Count */
#define IGC_RPTHC	0x04104  /* Rx Packets To Host */
#define IGC_TLPIC	0x04148  /* EEE Tx LPI Count */
#define IGC_RLPIC	0x0414C  /* EEE Rx LPI Count */
#define IGC_HGPTC	0x04118  /* Host Good Packets Tx Count */
#define IGC_RXDMTC	0x04120  /* Rx Descriptor Minimum Threshold Count */
#define IGC_HGORCL	0x04128  /* Host Good Octets Received Count Low */
#define IGC_HGORCH	0x0412C  /* Host Good Octets Received Count High */
#define IGC_HGOTCL	0x04130  /* Host Good Octets Transmit Count Low */
#define IGC_HGOTCH	0x04134  /* Host Good Octets Transmit Count High */
#define IGC_LENERRS	0x04138  /* Length Errors Count */

/* Time sync registers */
#define IGC_TSICR	0x0B66C  /* Time Sync Interrupt Cause */
#define IGC_TSIM	0x0B674  /* Time Sync Interrupt Mask Register */
#define IGC_TSAUXC	0x0B640  /* Timesync Auxiliary Control register */
#define IGC_TSYNCRXCTL	0x0B620  /* Rx Time Sync Control register - RW */
#define IGC_TSYNCTXCTL	0x0B614  /* Tx Time Sync Control register - RW */
#define IGC_TSYNCRXCFG	0x05F50  /* Time Sync Rx Configuration - RW */
#define IGC_TSSDP	0x0003C  /* Time Sync SDP Configuration Register - RW */

#define IGC_IMIR(_i)	(0x05A80 + ((_i) * 4))  /* Immediate Interrupt */
#define IGC_IMIREXT(_i)	(0x05AA0 + ((_i) * 4))  /* Immediate INTR Ext*/

#define IGC_FTQF(_n)	(0x059E0 + (4 * (_n)))  /* 5-tuple Queue Fltr */

/* Transmit Scheduling Registers */
#define IGC_TQAVCTRL		0x3570
#define IGC_TXQCTL(_n)		(0x3344 + 0x4 * (_n))
#define IGC_BASET_L		0x3314
#define IGC_BASET_H		0x3318
#define IGC_QBVCYCLET		0x331C
#define IGC_QBVCYCLET_S		0x3320

#define IGC_STQT(_n)		(0x3324 + 0x4 * (_n))
#define IGC_ENDQT(_n)		(0x3334 + 0x4 * (_n))
#define IGC_DTXMXPKTSZ		0x355C

/* System Time Registers */
#define IGC_SYSTIML	0x0B600  /* System time register Low - RO */
#define IGC_SYSTIMH	0x0B604  /* System time register High - RO */
#define IGC_SYSTIMR	0x0B6F8  /* System time register Residue */
#define IGC_TIMINCA	0x0B608  /* Increment attributes register - RW */

#define IGC_TXSTMPL	0x0B618  /* Tx timestamp value Low - RO */
#define IGC_TXSTMPH	0x0B61C  /* Tx timestamp value High - RO */

/* Management registers */
#define IGC_MANC	0x05820  /* Management Control - RW */

/* Shadow Ram Write Register - RW */
#define IGC_SRWR	0x12018

/* Wake Up registers */
#define IGC_WUC		0x05800  /* Wakeup Control - RW */
#define IGC_WUFC	0x05808  /* Wakeup Filter Control - RW */
#define IGC_WUS		0x05810  /* Wakeup Status - R/W1C */
#define IGC_WUPL	0x05900  /* Wakeup Packet Length - RW */

/* Wake Up packet memory */
#define IGC_WUPM_REG(_i)	(0x05A00 + ((_i) * 4))

/* Energy Efficient Ethernet "EEE" registers */
#define IGC_EEER	0x0E30 /* Energy Efficient Ethernet "EEE"*/
#define IGC_IPCNFG	0x0E38 /* Internal PHY Configuration */
#define IGC_EEE_SU	0x0E34 /* EEE Setup */

/* LTR registers */
#define IGC_LTRC	0x01A0 /* Latency Tolerance Reporting Control */
#define IGC_DMACR	0x02508 /* DMA Coalescing Control Register */
#define IGC_LTRMINV	0x5BB0 /* LTR Minimum Value */
#define IGC_LTRMAXV	0x5BB4 /* LTR Maximum Value */

/* forward declaration */
struct igc_hw;
u32 igc_rd32(struct igc_hw *hw, u32 reg);

/* write operations, indexed using DWORDS */
#define wr32(reg, val) \
do { \
	u8 __iomem *hw_addr = READ_ONCE((hw)->hw_addr); \
	writel((val), &hw_addr[(reg)]); \
} while (0)

#define rd32(reg) (igc_rd32(hw, reg))

#define wrfl() ((void)rd32(IGC_STATUS))

#define array_wr32(reg, offset, value) \
	wr32((reg) + ((offset) << 2), (value))

#define array_rd32(reg, offset) (igc_rd32(hw, (reg) + ((offset) << 2)))

#endif
