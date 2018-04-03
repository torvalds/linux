/* SPDX-License-Identifier: GPL-2.0 */
/* Intel PRO/1000 Linux driver
 * Copyright(c) 1999 - 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Linux NICS <linux.nics@intel.com>
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#ifndef _E1000E_REGS_H_
#define _E1000E_REGS_H_

#define E1000_CTRL	0x00000	/* Device Control - RW */
#define E1000_STATUS	0x00008	/* Device Status - RO */
#define E1000_EECD	0x00010	/* EEPROM/Flash Control - RW */
#define E1000_EERD	0x00014	/* EEPROM Read - RW */
#define E1000_CTRL_EXT	0x00018	/* Extended Device Control - RW */
#define E1000_FLA	0x0001C	/* Flash Access - RW */
#define E1000_MDIC	0x00020	/* MDI Control - RW */
#define E1000_SCTL	0x00024	/* SerDes Control - RW */
#define E1000_FCAL	0x00028	/* Flow Control Address Low - RW */
#define E1000_FCAH	0x0002C	/* Flow Control Address High -RW */
#define E1000_FEXT	0x0002C	/* Future Extended - RW */
#define E1000_FEXTNVM	0x00028	/* Future Extended NVM - RW */
#define E1000_FEXTNVM3	0x0003C	/* Future Extended NVM 3 - RW */
#define E1000_FEXTNVM4	0x00024	/* Future Extended NVM 4 - RW */
#define E1000_FEXTNVM6	0x00010	/* Future Extended NVM 6 - RW */
#define E1000_FEXTNVM7	0x000E4	/* Future Extended NVM 7 - RW */
#define E1000_FEXTNVM9	0x5BB4	/* Future Extended NVM 9 - RW */
#define E1000_FEXTNVM11	0x5BBC	/* Future Extended NVM 11 - RW */
#define E1000_PCIEANACFG	0x00F18	/* PCIE Analog Config */
#define E1000_FCT	0x00030	/* Flow Control Type - RW */
#define E1000_VET	0x00038	/* VLAN Ether Type - RW */
#define E1000_ICR	0x000C0	/* Interrupt Cause Read - R/clr */
#define E1000_ITR	0x000C4	/* Interrupt Throttling Rate - RW */
#define E1000_ICS	0x000C8	/* Interrupt Cause Set - WO */
#define E1000_IMS	0x000D0	/* Interrupt Mask Set - RW */
#define E1000_IMC	0x000D8	/* Interrupt Mask Clear - WO */
#define E1000_IAM	0x000E0	/* Interrupt Acknowledge Auto Mask */
#define E1000_IVAR	0x000E4	/* Interrupt Vector Allocation Register - RW */
#define E1000_SVCR	0x000F0
#define E1000_SVT	0x000F4
#define E1000_LPIC	0x000FC	/* Low Power IDLE control */
#define E1000_RCTL	0x00100	/* Rx Control - RW */
#define E1000_FCTTV	0x00170	/* Flow Control Transmit Timer Value - RW */
#define E1000_TXCW	0x00178	/* Tx Configuration Word - RW */
#define E1000_RXCW	0x00180	/* Rx Configuration Word - RO */
#define E1000_PBA_ECC	0x01100	/* PBA ECC Register */
#define E1000_TCTL	0x00400	/* Tx Control - RW */
#define E1000_TCTL_EXT	0x00404	/* Extended Tx Control - RW */
#define E1000_TIPG	0x00410	/* Tx Inter-packet gap -RW */
#define E1000_AIT	0x00458	/* Adaptive Interframe Spacing Throttle - RW */
#define E1000_LEDCTL	0x00E00	/* LED Control - RW */
#define E1000_EXTCNF_CTRL	0x00F00	/* Extended Configuration Control */
#define E1000_EXTCNF_SIZE	0x00F08	/* Extended Configuration Size */
#define E1000_PHY_CTRL	0x00F10	/* PHY Control Register in CSR */
#define E1000_POEMB	E1000_PHY_CTRL	/* PHY OEM Bits */
#define E1000_PBA	0x01000	/* Packet Buffer Allocation - RW */
#define E1000_PBS	0x01008	/* Packet Buffer Size */
#define E1000_PBECCSTS	0x0100C	/* Packet Buffer ECC Status - RW */
#define E1000_IOSFPC	0x00F28	/* TX corrupted data  */
#define E1000_EEMNGCTL	0x01010	/* MNG EEprom Control */
#define E1000_EEWR	0x0102C	/* EEPROM Write Register - RW */
#define E1000_FLOP	0x0103C	/* FLASH Opcode Register */
#define E1000_ERT	0x02008	/* Early Rx Threshold - RW */
#define E1000_FCRTL	0x02160	/* Flow Control Receive Threshold Low - RW */
#define E1000_FCRTH	0x02168	/* Flow Control Receive Threshold High - RW */
#define E1000_PSRCTL	0x02170	/* Packet Split Receive Control - RW */
#define E1000_RDFH	0x02410	/* Rx Data FIFO Head - RW */
#define E1000_RDFT	0x02418	/* Rx Data FIFO Tail - RW */
#define E1000_RDFHS	0x02420	/* Rx Data FIFO Head Saved - RW */
#define E1000_RDFTS	0x02428	/* Rx Data FIFO Tail Saved - RW */
#define E1000_RDFPC	0x02430	/* Rx Data FIFO Packet Count - RW */
/* Split and Replication Rx Control - RW */
#define E1000_RDTR	0x02820	/* Rx Delay Timer - RW */
#define E1000_RADV	0x0282C	/* Rx Interrupt Absolute Delay Timer - RW */
/* Convenience macros
 *
 * Note: "_n" is the queue number of the register to be written to.
 *
 * Example usage:
 * E1000_RDBAL_REG(current_rx_queue)
 */
#define E1000_RDBAL(_n)	((_n) < 4 ? (0x02800 + ((_n) * 0x100)) : \
			 (0x0C000 + ((_n) * 0x40)))
#define E1000_RDBAH(_n)	((_n) < 4 ? (0x02804 + ((_n) * 0x100)) : \
			 (0x0C004 + ((_n) * 0x40)))
#define E1000_RDLEN(_n)	((_n) < 4 ? (0x02808 + ((_n) * 0x100)) : \
			 (0x0C008 + ((_n) * 0x40)))
#define E1000_RDH(_n)	((_n) < 4 ? (0x02810 + ((_n) * 0x100)) : \
			 (0x0C010 + ((_n) * 0x40)))
#define E1000_RDT(_n)	((_n) < 4 ? (0x02818 + ((_n) * 0x100)) : \
			 (0x0C018 + ((_n) * 0x40)))
#define E1000_RXDCTL(_n)	((_n) < 4 ? (0x02828 + ((_n) * 0x100)) : \
				 (0x0C028 + ((_n) * 0x40)))
#define E1000_TDBAL(_n)	((_n) < 4 ? (0x03800 + ((_n) * 0x100)) : \
			 (0x0E000 + ((_n) * 0x40)))
#define E1000_TDBAH(_n)	((_n) < 4 ? (0x03804 + ((_n) * 0x100)) : \
			 (0x0E004 + ((_n) * 0x40)))
#define E1000_TDLEN(_n)	((_n) < 4 ? (0x03808 + ((_n) * 0x100)) : \
			 (0x0E008 + ((_n) * 0x40)))
#define E1000_TDH(_n)	((_n) < 4 ? (0x03810 + ((_n) * 0x100)) : \
			 (0x0E010 + ((_n) * 0x40)))
#define E1000_TDT(_n)	((_n) < 4 ? (0x03818 + ((_n) * 0x100)) : \
			 (0x0E018 + ((_n) * 0x40)))
#define E1000_TXDCTL(_n)	((_n) < 4 ? (0x03828 + ((_n) * 0x100)) : \
				 (0x0E028 + ((_n) * 0x40)))
#define E1000_TARC(_n)		(0x03840 + ((_n) * 0x100))
#define E1000_KABGTXD		0x03004	/* AFE Band Gap Transmit Ref Data */
#define E1000_RAL(_i)		(((_i) <= 15) ? (0x05400 + ((_i) * 8)) : \
				 (0x054E0 + ((_i - 16) * 8)))
#define E1000_RAH(_i)		(((_i) <= 15) ? (0x05404 + ((_i) * 8)) : \
				 (0x054E4 + ((_i - 16) * 8)))
#define E1000_SHRAL(_i)		(0x05438 + ((_i) * 8))
#define E1000_SHRAH(_i)		(0x0543C + ((_i) * 8))
#define E1000_TDFH		0x03410	/* Tx Data FIFO Head - RW */
#define E1000_TDFT		0x03418	/* Tx Data FIFO Tail - RW */
#define E1000_TDFHS		0x03420	/* Tx Data FIFO Head Saved - RW */
#define E1000_TDFTS		0x03428	/* Tx Data FIFO Tail Saved - RW */
#define E1000_TDFPC		0x03430	/* Tx Data FIFO Packet Count - RW */
#define E1000_TIDV	0x03820	/* Tx Interrupt Delay Value - RW */
#define E1000_TADV	0x0382C	/* Tx Interrupt Absolute Delay Val - RW */
#define E1000_CRCERRS	0x04000	/* CRC Error Count - R/clr */
#define E1000_ALGNERRC	0x04004	/* Alignment Error Count - R/clr */
#define E1000_SYMERRS	0x04008	/* Symbol Error Count - R/clr */
#define E1000_RXERRC	0x0400C	/* Receive Error Count - R/clr */
#define E1000_MPC	0x04010	/* Missed Packet Count - R/clr */
#define E1000_SCC	0x04014	/* Single Collision Count - R/clr */
#define E1000_ECOL	0x04018	/* Excessive Collision Count - R/clr */
#define E1000_MCC	0x0401C	/* Multiple Collision Count - R/clr */
#define E1000_LATECOL	0x04020	/* Late Collision Count - R/clr */
#define E1000_COLC	0x04028	/* Collision Count - R/clr */
#define E1000_DC	0x04030	/* Defer Count - R/clr */
#define E1000_TNCRS	0x04034	/* Tx-No CRS - R/clr */
#define E1000_SEC	0x04038	/* Sequence Error Count - R/clr */
#define E1000_CEXTERR	0x0403C	/* Carrier Extension Error Count - R/clr */
#define E1000_RLEC	0x04040	/* Receive Length Error Count - R/clr */
#define E1000_XONRXC	0x04048	/* XON Rx Count - R/clr */
#define E1000_XONTXC	0x0404C	/* XON Tx Count - R/clr */
#define E1000_XOFFRXC	0x04050	/* XOFF Rx Count - R/clr */
#define E1000_XOFFTXC	0x04054	/* XOFF Tx Count - R/clr */
#define E1000_FCRUC	0x04058	/* Flow Control Rx Unsupported Count- R/clr */
#define E1000_PRC64	0x0405C	/* Packets Rx (64 bytes) - R/clr */
#define E1000_PRC127	0x04060	/* Packets Rx (65-127 bytes) - R/clr */
#define E1000_PRC255	0x04064	/* Packets Rx (128-255 bytes) - R/clr */
#define E1000_PRC511	0x04068	/* Packets Rx (255-511 bytes) - R/clr */
#define E1000_PRC1023	0x0406C	/* Packets Rx (512-1023 bytes) - R/clr */
#define E1000_PRC1522	0x04070	/* Packets Rx (1024-1522 bytes) - R/clr */
#define E1000_GPRC	0x04074	/* Good Packets Rx Count - R/clr */
#define E1000_BPRC	0x04078	/* Broadcast Packets Rx Count - R/clr */
#define E1000_MPRC	0x0407C	/* Multicast Packets Rx Count - R/clr */
#define E1000_GPTC	0x04080	/* Good Packets Tx Count - R/clr */
#define E1000_GORCL	0x04088	/* Good Octets Rx Count Low - R/clr */
#define E1000_GORCH	0x0408C	/* Good Octets Rx Count High - R/clr */
#define E1000_GOTCL	0x04090	/* Good Octets Tx Count Low - R/clr */
#define E1000_GOTCH	0x04094	/* Good Octets Tx Count High - R/clr */
#define E1000_RNBC	0x040A0	/* Rx No Buffers Count - R/clr */
#define E1000_RUC	0x040A4	/* Rx Undersize Count - R/clr */
#define E1000_RFC	0x040A8	/* Rx Fragment Count - R/clr */
#define E1000_ROC	0x040AC	/* Rx Oversize Count - R/clr */
#define E1000_RJC	0x040B0	/* Rx Jabber Count - R/clr */
#define E1000_MGTPRC	0x040B4	/* Management Packets Rx Count - R/clr */
#define E1000_MGTPDC	0x040B8	/* Management Packets Dropped Count - R/clr */
#define E1000_MGTPTC	0x040BC	/* Management Packets Tx Count - R/clr */
#define E1000_TORL	0x040C0	/* Total Octets Rx Low - R/clr */
#define E1000_TORH	0x040C4	/* Total Octets Rx High - R/clr */
#define E1000_TOTL	0x040C8	/* Total Octets Tx Low - R/clr */
#define E1000_TOTH	0x040CC	/* Total Octets Tx High - R/clr */
#define E1000_TPR	0x040D0	/* Total Packets Rx - R/clr */
#define E1000_TPT	0x040D4	/* Total Packets Tx - R/clr */
#define E1000_PTC64	0x040D8	/* Packets Tx (64 bytes) - R/clr */
#define E1000_PTC127	0x040DC	/* Packets Tx (65-127 bytes) - R/clr */
#define E1000_PTC255	0x040E0	/* Packets Tx (128-255 bytes) - R/clr */
#define E1000_PTC511	0x040E4	/* Packets Tx (256-511 bytes) - R/clr */
#define E1000_PTC1023	0x040E8	/* Packets Tx (512-1023 bytes) - R/clr */
#define E1000_PTC1522	0x040EC	/* Packets Tx (1024-1522 Bytes) - R/clr */
#define E1000_MPTC	0x040F0	/* Multicast Packets Tx Count - R/clr */
#define E1000_BPTC	0x040F4	/* Broadcast Packets Tx Count - R/clr */
#define E1000_TSCTC	0x040F8	/* TCP Segmentation Context Tx - R/clr */
#define E1000_TSCTFC	0x040FC	/* TCP Segmentation Context Tx Fail - R/clr */
#define E1000_IAC	0x04100	/* Interrupt Assertion Count */
#define E1000_ICRXPTC	0x04104	/* Interrupt Cause Rx Pkt Timer Expire Count */
#define E1000_ICRXATC	0x04108	/* Interrupt Cause Rx Abs Timer Expire Count */
#define E1000_ICTXPTC	0x0410C	/* Interrupt Cause Tx Pkt Timer Expire Count */
#define E1000_ICTXATC	0x04110	/* Interrupt Cause Tx Abs Timer Expire Count */
#define E1000_ICTXQEC	0x04118	/* Interrupt Cause Tx Queue Empty Count */
#define E1000_ICTXQMTC	0x0411C	/* Interrupt Cause Tx Queue Min Thresh Count */
#define E1000_ICRXDMTC	0x04120	/* Interrupt Cause Rx Desc Min Thresh Count */
#define E1000_ICRXOC	0x04124	/* Interrupt Cause Receiver Overrun Count */
#define E1000_CRC_OFFSET	0x05F50	/* CRC Offset register */

#define E1000_PCS_LCTL	0x04208	/* PCS Link Control - RW */
#define E1000_PCS_LSTAT	0x0420C	/* PCS Link Status - RO */
#define E1000_PCS_ANADV	0x04218	/* AN advertisement - RW */
#define E1000_PCS_LPAB	0x0421C	/* Link Partner Ability - RW */
#define E1000_RXCSUM	0x05000	/* Rx Checksum Control - RW */
#define E1000_RFCTL	0x05008	/* Receive Filter Control */
#define E1000_MTA	0x05200	/* Multicast Table Array - RW Array */
#define E1000_RA	0x05400	/* Receive Address - RW Array */
#define E1000_VFTA	0x05600	/* VLAN Filter Table Array - RW Array */
#define E1000_WUC	0x05800	/* Wakeup Control - RW */
#define E1000_WUFC	0x05808	/* Wakeup Filter Control - RW */
#define E1000_WUS	0x05810	/* Wakeup Status - RO */
#define E1000_MANC	0x05820	/* Management Control - RW */
#define E1000_FFLT	0x05F00	/* Flexible Filter Length Table - RW Array */
#define E1000_HOST_IF	0x08800	/* Host Interface */

#define E1000_KMRNCTRLSTA	0x00034	/* MAC-PHY interface - RW */
#define E1000_MANC2H		0x05860	/* Management Control To Host - RW */
/* Management Decision Filters */
#define E1000_MDEF(_n)		(0x05890 + (4 * (_n)))
#define E1000_SW_FW_SYNC	0x05B5C	/* SW-FW Synchronization - RW */
#define E1000_GCR	0x05B00	/* PCI-Ex Control */
#define E1000_GCR2	0x05B64	/* PCI-Ex Control #2 */
#define E1000_FACTPS	0x05B30	/* Function Active and Power State to MNG */
#define E1000_SWSM	0x05B50	/* SW Semaphore */
#define E1000_FWSM	0x05B54	/* FW Semaphore */
/* Driver-only SW semaphore (not used by BOOT agents) */
#define E1000_SWSM2	0x05B58
#define E1000_FFLT_DBG	0x05F04	/* Debug Register */
#define E1000_HICR	0x08F00	/* Host Interface Control */

/* RSS registers */
#define E1000_MRQC	0x05818	/* Multiple Receive Control - RW */
#define E1000_RETA(_i)	(0x05C00 + ((_i) * 4))	/* Redirection Table - RW */
#define E1000_RSSRK(_i)	(0x05C80 + ((_i) * 4))	/* RSS Random Key - RW */
#define E1000_TSYNCRXCTL	0x0B620	/* Rx Time Sync Control register - RW */
#define E1000_TSYNCTXCTL	0x0B614	/* Tx Time Sync Control register - RW */
#define E1000_RXSTMPL	0x0B624	/* Rx timestamp Low - RO */
#define E1000_RXSTMPH	0x0B628	/* Rx timestamp High - RO */
#define E1000_TXSTMPL	0x0B618	/* Tx timestamp value Low - RO */
#define E1000_TXSTMPH	0x0B61C	/* Tx timestamp value High - RO */
#define E1000_SYSTIML	0x0B600	/* System time register Low - RO */
#define E1000_SYSTIMH	0x0B604	/* System time register High - RO */
#define E1000_TIMINCA	0x0B608	/* Increment attributes register - RW */
#define E1000_SYSSTMPL  0x0B648 /* HH Timesync system stamp low register */
#define E1000_SYSSTMPH  0x0B64C /* HH Timesync system stamp hi register */
#define E1000_PLTSTMPL  0x0B640 /* HH Timesync platform stamp low register */
#define E1000_PLTSTMPH  0x0B644 /* HH Timesync platform stamp hi register */
#define E1000_RXMTRL	0x0B634	/* Time sync Rx EtherType and Msg Type - RW */
#define E1000_RXUDP	0x0B638	/* Time Sync Rx UDP Port - RW */

#endif
