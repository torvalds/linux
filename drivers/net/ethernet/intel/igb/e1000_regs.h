/* Intel(R) Gigabit Ethernet Linux driver
 * Copyright(c) 2007-2014 Intel Corporation.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#ifndef _E1000_REGS_H_
#define _E1000_REGS_H_

#define E1000_CTRL     0x00000  /* Device Control - RW */
#define E1000_STATUS   0x00008  /* Device Status - RO */
#define E1000_EECD     0x00010  /* EEPROM/Flash Control - RW */
#define E1000_EERD     0x00014  /* EEPROM Read - RW */
#define E1000_CTRL_EXT 0x00018  /* Extended Device Control - RW */
#define E1000_MDIC     0x00020  /* MDI Control - RW */
#define E1000_MDICNFG  0x00E04  /* MDI Config - RW */
#define E1000_SCTL     0x00024  /* SerDes Control - RW */
#define E1000_FCAL     0x00028  /* Flow Control Address Low - RW */
#define E1000_FCAH     0x0002C  /* Flow Control Address High -RW */
#define E1000_FCT      0x00030  /* Flow Control Type - RW */
#define E1000_CONNSW   0x00034  /* Copper/Fiber switch control - RW */
#define E1000_VET      0x00038  /* VLAN Ether Type - RW */
#define E1000_TSSDP    0x0003C  /* Time Sync SDP Configuration Register - RW */
#define E1000_ICR      0x000C0  /* Interrupt Cause Read - R/clr */
#define E1000_ITR      0x000C4  /* Interrupt Throttling Rate - RW */
#define E1000_ICS      0x000C8  /* Interrupt Cause Set - WO */
#define E1000_IMS      0x000D0  /* Interrupt Mask Set - RW */
#define E1000_IMC      0x000D8  /* Interrupt Mask Clear - WO */
#define E1000_IAM      0x000E0  /* Interrupt Acknowledge Auto Mask */
#define E1000_RCTL     0x00100  /* RX Control - RW */
#define E1000_FCTTV    0x00170  /* Flow Control Transmit Timer Value - RW */
#define E1000_TXCW     0x00178  /* TX Configuration Word - RW */
#define E1000_EICR     0x01580  /* Ext. Interrupt Cause Read - R/clr */
#define E1000_EITR(_n) (0x01680 + (0x4 * (_n)))
#define E1000_EICS     0x01520  /* Ext. Interrupt Cause Set - W0 */
#define E1000_EIMS     0x01524  /* Ext. Interrupt Mask Set/Read - RW */
#define E1000_EIMC     0x01528  /* Ext. Interrupt Mask Clear - WO */
#define E1000_EIAC     0x0152C  /* Ext. Interrupt Auto Clear - RW */
#define E1000_EIAM     0x01530  /* Ext. Interrupt Ack Auto Clear Mask - RW */
#define E1000_GPIE     0x01514  /* General Purpose Interrupt Enable - RW */
#define E1000_IVAR0    0x01700  /* Interrupt Vector Allocation (array) - RW */
#define E1000_IVAR_MISC 0x01740 /* IVAR for "other" causes - RW */
#define E1000_TCTL     0x00400  /* TX Control - RW */
#define E1000_TCTL_EXT 0x00404  /* Extended TX Control - RW */
#define E1000_TIPG     0x00410  /* TX Inter-packet gap -RW */
#define E1000_AIT      0x00458  /* Adaptive Interframe Spacing Throttle - RW */
#define E1000_LEDCTL   0x00E00  /* LED Control - RW */
#define E1000_LEDMUX   0x08130  /* LED MUX Control */
#define E1000_PBA      0x01000  /* Packet Buffer Allocation - RW */
#define E1000_PBS      0x01008  /* Packet Buffer Size */
#define E1000_EEMNGCTL 0x01010  /* MNG EEprom Control */
#define E1000_EEARBC_I210 0x12024  /* EEPROM Auto Read Bus Control */
#define E1000_EEWR     0x0102C  /* EEPROM Write Register - RW */
#define E1000_I2CCMD   0x01028  /* SFPI2C Command Register - RW */
#define E1000_FRTIMER  0x01048  /* Free Running Timer - RW */
#define E1000_TCPTIMER 0x0104C  /* TCP Timer - RW */
#define E1000_FCRTL    0x02160  /* Flow Control Receive Threshold Low - RW */
#define E1000_FCRTH    0x02168  /* Flow Control Receive Threshold High - RW */
#define E1000_FCRTV    0x02460  /* Flow Control Refresh Timer Value - RW */
#define E1000_I2CPARAMS        0x0102C /* SFPI2C Parameters Register - RW */
#define E1000_I2CBB_EN      0x00000100  /* I2C - Bit Bang Enable */
#define E1000_I2C_CLK_OUT   0x00000200  /* I2C- Clock */
#define E1000_I2C_DATA_OUT  0x00000400  /* I2C- Data Out */
#define E1000_I2C_DATA_OE_N 0x00000800  /* I2C- Data Output Enable */
#define E1000_I2C_DATA_IN   0x00001000  /* I2C- Data In */
#define E1000_I2C_CLK_OE_N  0x00002000  /* I2C- Clock Output Enable */
#define E1000_I2C_CLK_IN    0x00004000  /* I2C- Clock In */
#define E1000_MPHY_ADDR_CTRL	0x0024 /* GbE MPHY Address Control */
#define E1000_MPHY_DATA		0x0E10 /* GBE MPHY Data */
#define E1000_MPHY_STAT		0x0E0C /* GBE MPHY Statistics */

/* IEEE 1588 TIMESYNCH */
#define E1000_TSYNCRXCTL 0x0B620 /* Rx Time Sync Control register - RW */
#define E1000_TSYNCTXCTL 0x0B614 /* Tx Time Sync Control register - RW */
#define E1000_TSYNCRXCFG 0x05F50 /* Time Sync Rx Configuration - RW */
#define E1000_RXSTMPL    0x0B624 /* Rx timestamp Low - RO */
#define E1000_RXSTMPH    0x0B628 /* Rx timestamp High - RO */
#define E1000_RXSATRL    0x0B62C /* Rx timestamp attribute low - RO */
#define E1000_RXSATRH    0x0B630 /* Rx timestamp attribute high - RO */
#define E1000_TXSTMPL    0x0B618 /* Tx timestamp value Low - RO */
#define E1000_TXSTMPH    0x0B61C /* Tx timestamp value High - RO */
#define E1000_SYSTIML    0x0B600 /* System time register Low - RO */
#define E1000_SYSTIMH    0x0B604 /* System time register High - RO */
#define E1000_TIMINCA    0x0B608 /* Increment attributes register - RW */
#define E1000_TSAUXC     0x0B640 /* Timesync Auxiliary Control register */
#define E1000_TRGTTIML0  0x0B644 /* Target Time Register 0 Low  - RW */
#define E1000_TRGTTIMH0  0x0B648 /* Target Time Register 0 High - RW */
#define E1000_TRGTTIML1  0x0B64C /* Target Time Register 1 Low  - RW */
#define E1000_TRGTTIMH1  0x0B650 /* Target Time Register 1 High - RW */
#define E1000_FREQOUT0   0x0B654 /* Frequency Out 0 Control Register - RW */
#define E1000_FREQOUT1   0x0B658 /* Frequency Out 1 Control Register - RW */
#define E1000_AUXSTMPL0  0x0B65C /* Auxiliary Time Stamp 0 Register Low  - RO */
#define E1000_AUXSTMPH0  0x0B660 /* Auxiliary Time Stamp 0 Register High - RO */
#define E1000_AUXSTMPL1  0x0B664 /* Auxiliary Time Stamp 1 Register Low  - RO */
#define E1000_AUXSTMPH1  0x0B668 /* Auxiliary Time Stamp 1 Register High - RO */
#define E1000_SYSTIMR    0x0B6F8 /* System time register Residue */
#define E1000_TSICR      0x0B66C /* Interrupt Cause Register */
#define E1000_TSIM       0x0B674 /* Interrupt Mask Register */

/* Filtering Registers */
#define E1000_SAQF(_n) (0x5980 + 4 * (_n))
#define E1000_DAQF(_n) (0x59A0 + 4 * (_n))
#define E1000_SPQF(_n) (0x59C0 + 4 * (_n))
#define E1000_FTQF(_n) (0x59E0 + 4 * (_n))
#define E1000_SAQF0 E1000_SAQF(0)
#define E1000_DAQF0 E1000_DAQF(0)
#define E1000_SPQF0 E1000_SPQF(0)
#define E1000_FTQF0 E1000_FTQF(0)
#define E1000_SYNQF(_n) (0x055FC + (4 * (_n))) /* SYN Packet Queue Fltr */
#define E1000_ETQF(_n)  (0x05CB0 + (4 * (_n))) /* EType Queue Fltr */

#define E1000_RQDPC(_n) (0x0C030 + ((_n) * 0x40))

/* DMA Coalescing registers */
#define E1000_DMACR	0x02508 /* Control Register */
#define E1000_DMCTXTH	0x03550 /* Transmit Threshold */
#define E1000_DMCTLX	0x02514 /* Time to Lx Request */
#define E1000_DMCRTRH	0x05DD0 /* Receive Packet Rate Threshold */
#define E1000_DMCCNT	0x05DD4 /* Current Rx Count */
#define E1000_FCRTC	0x02170 /* Flow Control Rx high watermark */
#define E1000_PCIEMISC	0x05BB8 /* PCIE misc config register */

/* TX Rate Limit Registers */
#define E1000_RTTDQSEL	0x3604 /* Tx Desc Plane Queue Select - WO */
#define E1000_RTTBCNRM	0x3690 /* Tx BCN Rate-scheduler MMW */
#define E1000_RTTBCNRC	0x36B0 /* Tx BCN Rate-Scheduler Config - WO */

/* Split and Replication RX Control - RW */
#define E1000_RXPBS	0x02404 /* Rx Packet Buffer Size - RW */

/* Thermal sensor configuration and status registers */
#define E1000_THMJT	0x08100 /* Junction Temperature */
#define E1000_THLOWTC	0x08104 /* Low Threshold Control */
#define E1000_THMIDTC	0x08108 /* Mid Threshold Control */
#define E1000_THHIGHTC	0x0810C /* High Threshold Control */
#define E1000_THSTAT	0x08110 /* Thermal Sensor Status */

/* Convenience macros
 *
 * Note: "_n" is the queue number of the register to be written to.
 *
 * Example usage:
 * E1000_RDBAL_REG(current_rx_queue)
 */
#define E1000_RDBAL(_n)   ((_n) < 4 ? (0x02800 + ((_n) * 0x100)) \
				    : (0x0C000 + ((_n) * 0x40)))
#define E1000_RDBAH(_n)   ((_n) < 4 ? (0x02804 + ((_n) * 0x100)) \
				    : (0x0C004 + ((_n) * 0x40)))
#define E1000_RDLEN(_n)   ((_n) < 4 ? (0x02808 + ((_n) * 0x100)) \
				    : (0x0C008 + ((_n) * 0x40)))
#define E1000_SRRCTL(_n)  ((_n) < 4 ? (0x0280C + ((_n) * 0x100)) \
				    : (0x0C00C + ((_n) * 0x40)))
#define E1000_RDH(_n)     ((_n) < 4 ? (0x02810 + ((_n) * 0x100)) \
				    : (0x0C010 + ((_n) * 0x40)))
#define E1000_RDT(_n)     ((_n) < 4 ? (0x02818 + ((_n) * 0x100)) \
				    : (0x0C018 + ((_n) * 0x40)))
#define E1000_RXDCTL(_n)  ((_n) < 4 ? (0x02828 + ((_n) * 0x100)) \
				    : (0x0C028 + ((_n) * 0x40)))
#define E1000_TDBAL(_n)   ((_n) < 4 ? (0x03800 + ((_n) * 0x100)) \
				    : (0x0E000 + ((_n) * 0x40)))
#define E1000_TDBAH(_n)   ((_n) < 4 ? (0x03804 + ((_n) * 0x100)) \
				    : (0x0E004 + ((_n) * 0x40)))
#define E1000_TDLEN(_n)   ((_n) < 4 ? (0x03808 + ((_n) * 0x100)) \
				    : (0x0E008 + ((_n) * 0x40)))
#define E1000_TDH(_n)     ((_n) < 4 ? (0x03810 + ((_n) * 0x100)) \
				    : (0x0E010 + ((_n) * 0x40)))
#define E1000_TDT(_n)     ((_n) < 4 ? (0x03818 + ((_n) * 0x100)) \
				    : (0x0E018 + ((_n) * 0x40)))
#define E1000_TXDCTL(_n)  ((_n) < 4 ? (0x03828 + ((_n) * 0x100)) \
				    : (0x0E028 + ((_n) * 0x40)))
#define E1000_RXCTL(_n)	  ((_n) < 4 ? (0x02814 + ((_n) * 0x100)) : \
				      (0x0C014 + ((_n) * 0x40)))
#define E1000_DCA_RXCTRL(_n)	E1000_RXCTL(_n)
#define E1000_TXCTL(_n)   ((_n) < 4 ? (0x03814 + ((_n) * 0x100)) : \
				      (0x0E014 + ((_n) * 0x40)))
#define E1000_DCA_TXCTRL(_n) E1000_TXCTL(_n)
#define E1000_TDWBAL(_n)  ((_n) < 4 ? (0x03838 + ((_n) * 0x100)) \
				    : (0x0E038 + ((_n) * 0x40)))
#define E1000_TDWBAH(_n)  ((_n) < 4 ? (0x0383C + ((_n) * 0x100)) \
				    : (0x0E03C + ((_n) * 0x40)))

#define E1000_RXPBS	0x02404  /* Rx Packet Buffer Size - RW */
#define E1000_TXPBS	0x03404  /* Tx Packet Buffer Size - RW */

#define E1000_TDFH     0x03410  /* TX Data FIFO Head - RW */
#define E1000_TDFT     0x03418  /* TX Data FIFO Tail - RW */
#define E1000_TDFHS    0x03420  /* TX Data FIFO Head Saved - RW */
#define E1000_TDFPC    0x03430  /* TX Data FIFO Packet Count - RW */
#define E1000_DTXCTL   0x03590  /* DMA TX Control - RW */
#define E1000_CRCERRS  0x04000  /* CRC Error Count - R/clr */
#define E1000_ALGNERRC 0x04004  /* Alignment Error Count - R/clr */
#define E1000_SYMERRS  0x04008  /* Symbol Error Count - R/clr */
#define E1000_RXERRC   0x0400C  /* Receive Error Count - R/clr */
#define E1000_MPC      0x04010  /* Missed Packet Count - R/clr */
#define E1000_SCC      0x04014  /* Single Collision Count - R/clr */
#define E1000_ECOL     0x04018  /* Excessive Collision Count - R/clr */
#define E1000_MCC      0x0401C  /* Multiple Collision Count - R/clr */
#define E1000_LATECOL  0x04020  /* Late Collision Count - R/clr */
#define E1000_COLC     0x04028  /* Collision Count - R/clr */
#define E1000_DC       0x04030  /* Defer Count - R/clr */
#define E1000_TNCRS    0x04034  /* TX-No CRS - R/clr */
#define E1000_SEC      0x04038  /* Sequence Error Count - R/clr */
#define E1000_CEXTERR  0x0403C  /* Carrier Extension Error Count - R/clr */
#define E1000_RLEC     0x04040  /* Receive Length Error Count - R/clr */
#define E1000_XONRXC   0x04048  /* XON RX Count - R/clr */
#define E1000_XONTXC   0x0404C  /* XON TX Count - R/clr */
#define E1000_XOFFRXC  0x04050  /* XOFF RX Count - R/clr */
#define E1000_XOFFTXC  0x04054  /* XOFF TX Count - R/clr */
#define E1000_FCRUC    0x04058  /* Flow Control RX Unsupported Count- R/clr */
#define E1000_PRC64    0x0405C  /* Packets RX (64 bytes) - R/clr */
#define E1000_PRC127   0x04060  /* Packets RX (65-127 bytes) - R/clr */
#define E1000_PRC255   0x04064  /* Packets RX (128-255 bytes) - R/clr */
#define E1000_PRC511   0x04068  /* Packets RX (255-511 bytes) - R/clr */
#define E1000_PRC1023  0x0406C  /* Packets RX (512-1023 bytes) - R/clr */
#define E1000_PRC1522  0x04070  /* Packets RX (1024-1522 bytes) - R/clr */
#define E1000_GPRC     0x04074  /* Good Packets RX Count - R/clr */
#define E1000_BPRC     0x04078  /* Broadcast Packets RX Count - R/clr */
#define E1000_MPRC     0x0407C  /* Multicast Packets RX Count - R/clr */
#define E1000_GPTC     0x04080  /* Good Packets TX Count - R/clr */
#define E1000_GORCL    0x04088  /* Good Octets RX Count Low - R/clr */
#define E1000_GORCH    0x0408C  /* Good Octets RX Count High - R/clr */
#define E1000_GOTCL    0x04090  /* Good Octets TX Count Low - R/clr */
#define E1000_GOTCH    0x04094  /* Good Octets TX Count High - R/clr */
#define E1000_RNBC     0x040A0  /* RX No Buffers Count - R/clr */
#define E1000_RUC      0x040A4  /* RX Undersize Count - R/clr */
#define E1000_RFC      0x040A8  /* RX Fragment Count - R/clr */
#define E1000_ROC      0x040AC  /* RX Oversize Count - R/clr */
#define E1000_RJC      0x040B0  /* RX Jabber Count - R/clr */
#define E1000_MGTPRC   0x040B4  /* Management Packets RX Count - R/clr */
#define E1000_MGTPDC   0x040B8  /* Management Packets Dropped Count - R/clr */
#define E1000_MGTPTC   0x040BC  /* Management Packets TX Count - R/clr */
#define E1000_TORL     0x040C0  /* Total Octets RX Low - R/clr */
#define E1000_TORH     0x040C4  /* Total Octets RX High - R/clr */
#define E1000_TOTL     0x040C8  /* Total Octets TX Low - R/clr */
#define E1000_TOTH     0x040CC  /* Total Octets TX High - R/clr */
#define E1000_TPR      0x040D0  /* Total Packets RX - R/clr */
#define E1000_TPT      0x040D4  /* Total Packets TX - R/clr */
#define E1000_PTC64    0x040D8  /* Packets TX (64 bytes) - R/clr */
#define E1000_PTC127   0x040DC  /* Packets TX (65-127 bytes) - R/clr */
#define E1000_PTC255   0x040E0  /* Packets TX (128-255 bytes) - R/clr */
#define E1000_PTC511   0x040E4  /* Packets TX (256-511 bytes) - R/clr */
#define E1000_PTC1023  0x040E8  /* Packets TX (512-1023 bytes) - R/clr */
#define E1000_PTC1522  0x040EC  /* Packets TX (1024-1522 Bytes) - R/clr */
#define E1000_MPTC     0x040F0  /* Multicast Packets TX Count - R/clr */
#define E1000_BPTC     0x040F4  /* Broadcast Packets TX Count - R/clr */
#define E1000_TSCTC    0x040F8  /* TCP Segmentation Context TX - R/clr */
#define E1000_TSCTFC   0x040FC  /* TCP Segmentation Context TX Fail - R/clr */
#define E1000_IAC      0x04100  /* Interrupt Assertion Count */
/* Interrupt Cause Rx Packet Timer Expire Count */
#define E1000_ICRXPTC  0x04104
/* Interrupt Cause Rx Absolute Timer Expire Count */
#define E1000_ICRXATC  0x04108
/* Interrupt Cause Tx Packet Timer Expire Count */
#define E1000_ICTXPTC  0x0410C
/* Interrupt Cause Tx Absolute Timer Expire Count */
#define E1000_ICTXATC  0x04110
/* Interrupt Cause Tx Queue Empty Count */
#define E1000_ICTXQEC  0x04118
/* Interrupt Cause Tx Queue Minimum Threshold Count */
#define E1000_ICTXQMTC 0x0411C
/* Interrupt Cause Rx Descriptor Minimum Threshold Count */
#define E1000_ICRXDMTC 0x04120
#define E1000_ICRXOC   0x04124  /* Interrupt Cause Receiver Overrun Count */
#define E1000_PCS_CFG0    0x04200  /* PCS Configuration 0 - RW */
#define E1000_PCS_LCTL    0x04208  /* PCS Link Control - RW */
#define E1000_PCS_LSTAT   0x0420C  /* PCS Link Status - RO */
#define E1000_CBTMPC      0x0402C  /* Circuit Breaker TX Packet Count */
#define E1000_HTDPMC      0x0403C  /* Host Transmit Discarded Packets */
#define E1000_CBRMPC      0x040FC  /* Circuit Breaker RX Packet Count */
#define E1000_RPTHC       0x04104  /* Rx Packets To Host */
#define E1000_HGPTC       0x04118  /* Host Good Packets TX Count */
#define E1000_HTCBDPC     0x04124  /* Host TX Circuit Breaker Dropped Count */
#define E1000_HGORCL      0x04128  /* Host Good Octets Received Count Low */
#define E1000_HGORCH      0x0412C  /* Host Good Octets Received Count High */
#define E1000_HGOTCL      0x04130  /* Host Good Octets Transmit Count Low */
#define E1000_HGOTCH      0x04134  /* Host Good Octets Transmit Count High */
#define E1000_LENERRS     0x04138  /* Length Errors Count */
#define E1000_SCVPC       0x04228  /* SerDes/SGMII Code Violation Pkt Count */
#define E1000_PCS_ANADV   0x04218  /* AN advertisement - RW */
#define E1000_PCS_LPAB    0x0421C  /* Link Partner Ability - RW */
#define E1000_PCS_NPTX    0x04220  /* AN Next Page Transmit - RW */
#define E1000_PCS_LPABNP  0x04224  /* Link Partner Ability Next Page - RW */
#define E1000_RXCSUM   0x05000  /* RX Checksum Control - RW */
#define E1000_RLPML    0x05004  /* RX Long Packet Max Length */
#define E1000_RFCTL    0x05008  /* Receive Filter Control*/
#define E1000_MTA      0x05200  /* Multicast Table Array - RW Array */
#define E1000_RA       0x05400  /* Receive Address - RW Array */
#define E1000_RA2      0x054E0  /* 2nd half of Rx address array - RW Array */
#define E1000_PSRTYPE(_i)       (0x05480 + ((_i) * 4))
#define E1000_RAL(_i)  (((_i) <= 15) ? (0x05400 + ((_i) * 8)) : \
					(0x054E0 + ((_i - 16) * 8)))
#define E1000_RAH(_i)  (((_i) <= 15) ? (0x05404 + ((_i) * 8)) : \
					(0x054E4 + ((_i - 16) * 8)))
#define E1000_IP4AT_REG(_i)     (0x05840 + ((_i) * 8))
#define E1000_IP6AT_REG(_i)     (0x05880 + ((_i) * 4))
#define E1000_WUPM_REG(_i)      (0x05A00 + ((_i) * 4))
#define E1000_FFMT_REG(_i)      (0x09000 + ((_i) * 8))
#define E1000_FFVT_REG(_i)      (0x09800 + ((_i) * 8))
#define E1000_FFLT_REG(_i)      (0x05F00 + ((_i) * 8))
#define E1000_VFTA     0x05600  /* VLAN Filter Table Array - RW Array */
#define E1000_VT_CTL   0x0581C  /* VMDq Control - RW */
#define E1000_WUC      0x05800  /* Wakeup Control - RW */
#define E1000_WUFC     0x05808  /* Wakeup Filter Control - RW */
#define E1000_WUS      0x05810  /* Wakeup Status - RO */
#define E1000_MANC     0x05820  /* Management Control - RW */
#define E1000_IPAV     0x05838  /* IP Address Valid - RW */
#define E1000_WUPL     0x05900  /* Wakeup Packet Length - RW */

#define E1000_SW_FW_SYNC  0x05B5C /* Software-Firmware Synchronization - RW */
#define E1000_CCMCTL      0x05B48 /* CCM Control Register */
#define E1000_GIOCTL      0x05B44 /* GIO Analog Control Register */
#define E1000_SCCTL       0x05B4C /* PCIc PLL Configuration Register */
#define E1000_GCR         0x05B00 /* PCI-Ex Control */
#define E1000_FACTPS    0x05B30 /* Function Active and Power State to MNG */
#define E1000_SWSM      0x05B50 /* SW Semaphore */
#define E1000_FWSM      0x05B54 /* FW Semaphore */
#define E1000_DCA_CTRL  0x05B74 /* DCA Control - RW */

/* RSS registers */
#define E1000_MRQC      0x05818 /* Multiple Receive Control - RW */
#define E1000_IMIR(_i)      (0x05A80 + ((_i) * 4))  /* Immediate Interrupt */
#define E1000_IMIREXT(_i)   (0x05AA0 + ((_i) * 4))  /* Immediate Interrupt Ext*/
#define E1000_IMIRVP    0x05AC0 /* Immediate Interrupt RX VLAN Priority - RW */
/* MSI-X Allocation Register (_i) - RW */
#define E1000_MSIXBM(_i)    (0x01600 + ((_i) * 4))
/* Redirection Table - RW Array */
#define E1000_RETA(_i)  (0x05C00 + ((_i) * 4))
#define E1000_RSSRK(_i) (0x05C80 + ((_i) * 4)) /* RSS Random Key - RW Array */

/* VT Registers */
#define E1000_MBVFICR   0x00C80 /* Mailbox VF Cause - RWC */
#define E1000_MBVFIMR   0x00C84 /* Mailbox VF int Mask - RW */
#define E1000_VFLRE     0x00C88 /* VF Register Events - RWC */
#define E1000_VFRE      0x00C8C /* VF Receive Enables */
#define E1000_VFTE      0x00C90 /* VF Transmit Enables */
#define E1000_QDE       0x02408 /* Queue Drop Enable - RW */
#define E1000_DTXSWC    0x03500 /* DMA Tx Switch Control - RW */
#define E1000_WVBR      0x03554 /* VM Wrong Behavior - RWS */
#define E1000_RPLOLR    0x05AF0 /* Replication Offload - RW */
#define E1000_UTA       0x0A000 /* Unicast Table Array - RW */
#define E1000_IOVTCL    0x05BBC /* IOV Control Register */
#define E1000_TXSWC     0x05ACC /* Tx Switch Control */
#define E1000_LVMMC	0x03548 /* Last VM Misbehavior cause */
/* These act per VF so an array friendly macro is used */
#define E1000_P2VMAILBOX(_n)   (0x00C00 + (4 * (_n)))
#define E1000_VMBMEM(_n)       (0x00800 + (64 * (_n)))
#define E1000_VMOLR(_n)        (0x05AD0 + (4 * (_n)))
#define E1000_DVMOLR(_n)       (0x0C038 + (64 * (_n)))
#define E1000_VLVF(_n)         (0x05D00 + (4 * (_n))) /* VLAN VM Filter */
#define E1000_VMVIR(_n)        (0x03700 + (4 * (_n)))

struct e1000_hw;

u32 igb_rd32(struct e1000_hw *hw, u32 reg);

/* write operations, indexed using DWORDS */
#define wr32(reg, val) \
do { \
	u8 __iomem *hw_addr = ACCESS_ONCE((hw)->hw_addr); \
	if (!E1000_REMOVED(hw_addr)) \
		writel((val), &hw_addr[(reg)]); \
} while (0)

#define rd32(reg) (igb_rd32(hw, reg))

#define wrfl() ((void)rd32(E1000_STATUS))

#define array_wr32(reg, offset, value) \
	wr32((reg) + ((offset) << 2), (value))

#define array_rd32(reg, offset) \
	(readl(hw->hw_addr + reg + ((offset) << 2)))

/* DMA Coalescing registers */
#define E1000_PCIEMISC	0x05BB8 /* PCIE misc config register */

/* Energy Efficient Ethernet "EEE" register */
#define E1000_IPCNFG	0x0E38 /* Internal PHY Configuration */
#define E1000_EEER	0x0E30 /* Energy Efficient Ethernet */
#define E1000_EEE_SU	0X0E34 /* EEE Setup */
#define E1000_EMIADD	0x10   /* Extended Memory Indirect Address */
#define E1000_EMIDATA	0x11   /* Extended Memory Indirect Data */
#define E1000_MMDAC	13     /* MMD Access Control */
#define E1000_MMDAAD	14     /* MMD Access Address/Data */

/* Thermal Sensor Register */
#define E1000_THSTAT	0x08110 /* Thermal Sensor Status */

/* OS2BMC Registers */
#define E1000_B2OSPC	0x08FE0 /* BMC2OS packets sent by BMC */
#define E1000_B2OGPRC	0x04158 /* BMC2OS packets received by host */
#define E1000_O2BGPTC	0x08FE4 /* OS2BMC packets received by BMC */
#define E1000_O2BSPC	0x0415C /* OS2BMC packets transmitted by host */

#define E1000_SRWR		0x12018  /* Shadow Ram Write Register - RW */
#define E1000_I210_FLMNGCTL	0x12038
#define E1000_I210_FLMNGDATA	0x1203C
#define E1000_I210_FLMNGCNT	0x12040

#define E1000_I210_FLSWCTL	0x12048
#define E1000_I210_FLSWDATA	0x1204C
#define E1000_I210_FLSWCNT	0x12050

#define E1000_I210_FLA		0x1201C

#define E1000_INVM_DATA_REG(_n)	(0x12120 + 4*(_n))
#define E1000_INVM_SIZE		64 /* Number of INVM Data Registers */

#define E1000_REMOVED(h) unlikely(!(h))

#endif
