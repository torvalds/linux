/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 1999 - 2008 Intel Corporation. */

#ifndef _IXGB_HW_H_
#define _IXGB_HW_H_

#include <linux/mdio.h>

#include "ixgb_osdep.h"

/* Enums */
typedef enum {
	ixgb_mac_unknown = 0,
	ixgb_82597,
	ixgb_num_macs
} ixgb_mac_type;

/* Types of physical layer modules */
typedef enum {
	ixgb_phy_type_unknown = 0,
	ixgb_phy_type_g6005,	/* 850nm, MM fiber, XPAK transceiver */
	ixgb_phy_type_g6104,	/* 1310nm, SM fiber, XPAK transceiver */
	ixgb_phy_type_txn17201,	/* 850nm, MM fiber, XPAK transceiver */
	ixgb_phy_type_txn17401,	/* 1310nm, SM fiber, XENPAK transceiver */
	ixgb_phy_type_bcm	/* SUN specific board */
} ixgb_phy_type;

/* XPAK transceiver vendors, for the SR adapters */
typedef enum {
	ixgb_xpak_vendor_intel,
	ixgb_xpak_vendor_infineon
} ixgb_xpak_vendor;

/* Media Types */
typedef enum {
	ixgb_media_type_unknown = 0,
	ixgb_media_type_fiber = 1,
	ixgb_media_type_copper = 2,
	ixgb_num_media_types
} ixgb_media_type;

/* Flow Control Settings */
typedef enum {
	ixgb_fc_none = 0,
	ixgb_fc_rx_pause = 1,
	ixgb_fc_tx_pause = 2,
	ixgb_fc_full = 3,
	ixgb_fc_default = 0xFF
} ixgb_fc_type;

/* PCI bus types */
typedef enum {
	ixgb_bus_type_unknown = 0,
	ixgb_bus_type_pci,
	ixgb_bus_type_pcix
} ixgb_bus_type;

/* PCI bus speeds */
typedef enum {
	ixgb_bus_speed_unknown = 0,
	ixgb_bus_speed_33,
	ixgb_bus_speed_66,
	ixgb_bus_speed_100,
	ixgb_bus_speed_133,
	ixgb_bus_speed_reserved
} ixgb_bus_speed;

/* PCI bus widths */
typedef enum {
	ixgb_bus_width_unknown = 0,
	ixgb_bus_width_32,
	ixgb_bus_width_64
} ixgb_bus_width;

#define IXGB_EEPROM_SIZE    64	/* Size in words */

#define SPEED_10000  10000
#define FULL_DUPLEX  2

#define MIN_NUMBER_OF_DESCRIPTORS       8
#define MAX_NUMBER_OF_DESCRIPTORS  0xFFF8	/* 13 bits in RDLEN/TDLEN, 128B aligned     */

#define IXGB_DELAY_BEFORE_RESET        10	/* allow 10ms after idling rx/tx units      */
#define IXGB_DELAY_AFTER_RESET          1	/* allow 1ms after the reset                */
#define IXGB_DELAY_AFTER_EE_RESET      10	/* allow 10ms after the EEPROM reset        */

#define IXGB_DELAY_USECS_AFTER_LINK_RESET    13	/* allow 13 microseconds after the reset    */
					   /* NOTE: this is MICROSECONDS               */
#define MAX_RESET_ITERATIONS            8	/* number of iterations to get things right */

/* General Registers */
#define IXGB_CTRL0   0x00000	/* Device Control Register 0 - RW */
#define IXGB_CTRL1   0x00008	/* Device Control Register 1 - RW */
#define IXGB_STATUS  0x00010	/* Device Status Register - RO */
#define IXGB_EECD    0x00018	/* EEPROM/Flash Control/Data Register - RW */
#define IXGB_MFS     0x00020	/* Maximum Frame Size - RW */

/* Interrupt */
#define IXGB_ICR     0x00080	/* Interrupt Cause Read - R/clr */
#define IXGB_ICS     0x00088	/* Interrupt Cause Set - RW */
#define IXGB_IMS     0x00090	/* Interrupt Mask Set/Read - RW */
#define IXGB_IMC     0x00098	/* Interrupt Mask Clear - WO */

/* Receive */
#define IXGB_RCTL    0x00100	/* RX Control - RW */
#define IXGB_FCRTL   0x00108	/* Flow Control Receive Threshold Low - RW */
#define IXGB_FCRTH   0x00110	/* Flow Control Receive Threshold High - RW */
#define IXGB_RDBAL   0x00118	/* RX Descriptor Base Low - RW */
#define IXGB_RDBAH   0x0011C	/* RX Descriptor Base High - RW */
#define IXGB_RDLEN   0x00120	/* RX Descriptor Length - RW */
#define IXGB_RDH     0x00128	/* RX Descriptor Head - RW */
#define IXGB_RDT     0x00130	/* RX Descriptor Tail - RW */
#define IXGB_RDTR    0x00138	/* RX Delay Timer Ring - RW */
#define IXGB_RXDCTL  0x00140	/* Receive Descriptor Control - RW */
#define IXGB_RAIDC   0x00148	/* Receive Adaptive Interrupt Delay Control - RW */
#define IXGB_RXCSUM  0x00158	/* Receive Checksum Control - RW */
#define IXGB_RA      0x00180	/* Receive Address Array Base - RW */
#define IXGB_RAL     0x00180	/* Receive Address Low [0:15] - RW */
#define IXGB_RAH     0x00184	/* Receive Address High [0:15] - RW */
#define IXGB_MTA     0x00200	/* Multicast Table Array [0:127] - RW */
#define IXGB_VFTA    0x00400	/* VLAN Filter Table Array [0:127] - RW */
#define IXGB_REQ_RX_DESCRIPTOR_MULTIPLE 8

/* Transmit */
#define IXGB_TCTL    0x00600	/* TX Control - RW */
#define IXGB_TDBAL   0x00608	/* TX Descriptor Base Low - RW */
#define IXGB_TDBAH   0x0060C	/* TX Descriptor Base High - RW */
#define IXGB_TDLEN   0x00610	/* TX Descriptor Length - RW */
#define IXGB_TDH     0x00618	/* TX Descriptor Head - RW */
#define IXGB_TDT     0x00620	/* TX Descriptor Tail - RW */
#define IXGB_TIDV    0x00628	/* TX Interrupt Delay Value - RW */
#define IXGB_TXDCTL  0x00630	/* Transmit Descriptor Control - RW */
#define IXGB_TSPMT   0x00638	/* TCP Segmentation PAD & Min Threshold - RW */
#define IXGB_PAP     0x00640	/* Pause and Pace - RW */
#define IXGB_REQ_TX_DESCRIPTOR_MULTIPLE 8

/* Physical */
#define IXGB_PCSC1   0x00700	/* PCS Control 1 - RW */
#define IXGB_PCSC2   0x00708	/* PCS Control 2 - RW */
#define IXGB_PCSS1   0x00710	/* PCS Status 1 - RO */
#define IXGB_PCSS2   0x00718	/* PCS Status 2 - RO */
#define IXGB_XPCSS   0x00720	/* 10GBASE-X PCS Status (or XGXS Lane Status) - RO */
#define IXGB_UCCR    0x00728	/* Unilink Circuit Control Register */
#define IXGB_XPCSTC  0x00730	/* 10GBASE-X PCS Test Control */
#define IXGB_MACA    0x00738	/* MDI Autoscan Command and Address - RW */
#define IXGB_APAE    0x00740	/* Autoscan PHY Address Enable - RW */
#define IXGB_ARD     0x00748	/* Autoscan Read Data - RO */
#define IXGB_AIS     0x00750	/* Autoscan Interrupt Status - RO */
#define IXGB_MSCA    0x00758	/* MDI Single Command and Address - RW */
#define IXGB_MSRWD   0x00760	/* MDI Single Read and Write Data - RW, RO */

/* Wake-up */
#define IXGB_WUFC    0x00808	/* Wake Up Filter Control - RW */
#define IXGB_WUS     0x00810	/* Wake Up Status - RO */
#define IXGB_FFLT    0x01000	/* Flexible Filter Length Table - RW */
#define IXGB_FFMT    0x01020	/* Flexible Filter Mask Table - RW */
#define IXGB_FTVT    0x01420	/* Flexible Filter Value Table - RW */

/* Statistics */
#define IXGB_TPRL    0x02000	/* Total Packets Received (Low) */
#define IXGB_TPRH    0x02004	/* Total Packets Received (High) */
#define IXGB_GPRCL   0x02008	/* Good Packets Received Count (Low) */
#define IXGB_GPRCH   0x0200C	/* Good Packets Received Count (High) */
#define IXGB_BPRCL   0x02010	/* Broadcast Packets Received Count (Low) */
#define IXGB_BPRCH   0x02014	/* Broadcast Packets Received Count (High) */
#define IXGB_MPRCL   0x02018	/* Multicast Packets Received Count (Low) */
#define IXGB_MPRCH   0x0201C	/* Multicast Packets Received Count (High) */
#define IXGB_UPRCL   0x02020	/* Unicast Packets Received Count (Low) */
#define IXGB_UPRCH   0x02024	/* Unicast Packets Received Count (High) */
#define IXGB_VPRCL   0x02028	/* VLAN Packets Received Count (Low) */
#define IXGB_VPRCH   0x0202C	/* VLAN Packets Received Count (High) */
#define IXGB_JPRCL   0x02030	/* Jumbo Packets Received Count (Low) */
#define IXGB_JPRCH   0x02034	/* Jumbo Packets Received Count (High) */
#define IXGB_GORCL   0x02038	/* Good Octets Received Count (Low) */
#define IXGB_GORCH   0x0203C	/* Good Octets Received Count (High) */
#define IXGB_TORL    0x02040	/* Total Octets Received (Low) */
#define IXGB_TORH    0x02044	/* Total Octets Received (High) */
#define IXGB_RNBC    0x02048	/* Receive No Buffers Count */
#define IXGB_RUC     0x02050	/* Receive Undersize Count */
#define IXGB_ROC     0x02058	/* Receive Oversize Count */
#define IXGB_RLEC    0x02060	/* Receive Length Error Count */
#define IXGB_CRCERRS 0x02068	/* CRC Error Count */
#define IXGB_ICBC    0x02070	/* Illegal control byte in mid-packet Count */
#define IXGB_ECBC    0x02078	/* Error Control byte in mid-packet Count */
#define IXGB_MPC     0x02080	/* Missed Packets Count */
#define IXGB_TPTL    0x02100	/* Total Packets Transmitted (Low) */
#define IXGB_TPTH    0x02104	/* Total Packets Transmitted (High) */
#define IXGB_GPTCL   0x02108	/* Good Packets Transmitted Count (Low) */
#define IXGB_GPTCH   0x0210C	/* Good Packets Transmitted Count (High) */
#define IXGB_BPTCL   0x02110	/* Broadcast Packets Transmitted Count (Low) */
#define IXGB_BPTCH   0x02114	/* Broadcast Packets Transmitted Count (High) */
#define IXGB_MPTCL   0x02118	/* Multicast Packets Transmitted Count (Low) */
#define IXGB_MPTCH   0x0211C	/* Multicast Packets Transmitted Count (High) */
#define IXGB_UPTCL   0x02120	/* Unicast Packets Transmitted Count (Low) */
#define IXGB_UPTCH   0x02124	/* Unicast Packets Transmitted Count (High) */
#define IXGB_VPTCL   0x02128	/* VLAN Packets Transmitted Count (Low) */
#define IXGB_VPTCH   0x0212C	/* VLAN Packets Transmitted Count (High) */
#define IXGB_JPTCL   0x02130	/* Jumbo Packets Transmitted Count (Low) */
#define IXGB_JPTCH   0x02134	/* Jumbo Packets Transmitted Count (High) */
#define IXGB_GOTCL   0x02138	/* Good Octets Transmitted Count (Low) */
#define IXGB_GOTCH   0x0213C	/* Good Octets Transmitted Count (High) */
#define IXGB_TOTL    0x02140	/* Total Octets Transmitted Count (Low) */
#define IXGB_TOTH    0x02144	/* Total Octets Transmitted Count (High) */
#define IXGB_DC      0x02148	/* Defer Count */
#define IXGB_PLT64C  0x02150	/* Packet Transmitted was less than 64 bytes Count */
#define IXGB_TSCTC   0x02170	/* TCP Segmentation Context Transmitted Count */
#define IXGB_TSCTFC  0x02178	/* TCP Segmentation Context Tx Fail Count */
#define IXGB_IBIC    0x02180	/* Illegal byte during Idle stream count */
#define IXGB_RFC     0x02188	/* Remote Fault Count */
#define IXGB_LFC     0x02190	/* Local Fault Count */
#define IXGB_PFRC    0x02198	/* Pause Frame Receive Count */
#define IXGB_PFTC    0x021A0	/* Pause Frame Transmit Count */
#define IXGB_MCFRC   0x021A8	/* MAC Control Frames (non-Pause) Received Count */
#define IXGB_MCFTC   0x021B0	/* MAC Control Frames (non-Pause) Transmitted Count */
#define IXGB_XONRXC  0x021B8	/* XON Received Count */
#define IXGB_XONTXC  0x021C0	/* XON Transmitted Count */
#define IXGB_XOFFRXC 0x021C8	/* XOFF Received Count */
#define IXGB_XOFFTXC 0x021D0	/* XOFF Transmitted Count */
#define IXGB_RJC     0x021D8	/* Receive Jabber Count */

/* CTRL0 Bit Masks */
#define IXGB_CTRL0_LRST     0x00000008
#define IXGB_CTRL0_JFE      0x00000010
#define IXGB_CTRL0_XLE      0x00000020
#define IXGB_CTRL0_MDCS     0x00000040
#define IXGB_CTRL0_CMDC     0x00000080
#define IXGB_CTRL0_SDP0     0x00040000
#define IXGB_CTRL0_SDP1     0x00080000
#define IXGB_CTRL0_SDP2     0x00100000
#define IXGB_CTRL0_SDP3     0x00200000
#define IXGB_CTRL0_SDP0_DIR 0x00400000
#define IXGB_CTRL0_SDP1_DIR 0x00800000
#define IXGB_CTRL0_SDP2_DIR 0x01000000
#define IXGB_CTRL0_SDP3_DIR 0x02000000
#define IXGB_CTRL0_RST      0x04000000
#define IXGB_CTRL0_RPE      0x08000000
#define IXGB_CTRL0_TPE      0x10000000
#define IXGB_CTRL0_VME      0x40000000

/* CTRL1 Bit Masks */
#define IXGB_CTRL1_GPI0_EN     0x00000001
#define IXGB_CTRL1_GPI1_EN     0x00000002
#define IXGB_CTRL1_GPI2_EN     0x00000004
#define IXGB_CTRL1_GPI3_EN     0x00000008
#define IXGB_CTRL1_SDP4        0x00000010
#define IXGB_CTRL1_SDP5        0x00000020
#define IXGB_CTRL1_SDP6        0x00000040
#define IXGB_CTRL1_SDP7        0x00000080
#define IXGB_CTRL1_SDP4_DIR    0x00000100
#define IXGB_CTRL1_SDP5_DIR    0x00000200
#define IXGB_CTRL1_SDP6_DIR    0x00000400
#define IXGB_CTRL1_SDP7_DIR    0x00000800
#define IXGB_CTRL1_EE_RST      0x00002000
#define IXGB_CTRL1_RO_DIS      0x00020000
#define IXGB_CTRL1_PCIXHM_MASK 0x00C00000
#define IXGB_CTRL1_PCIXHM_1_2  0x00000000
#define IXGB_CTRL1_PCIXHM_5_8  0x00400000
#define IXGB_CTRL1_PCIXHM_3_4  0x00800000
#define IXGB_CTRL1_PCIXHM_7_8  0x00C00000

/* STATUS Bit Masks */
#define IXGB_STATUS_LU            0x00000002
#define IXGB_STATUS_AIP           0x00000004
#define IXGB_STATUS_TXOFF         0x00000010
#define IXGB_STATUS_XAUIME        0x00000020
#define IXGB_STATUS_RES           0x00000040
#define IXGB_STATUS_RIS           0x00000080
#define IXGB_STATUS_RIE           0x00000100
#define IXGB_STATUS_RLF           0x00000200
#define IXGB_STATUS_RRF           0x00000400
#define IXGB_STATUS_PCI_SPD       0x00000800
#define IXGB_STATUS_BUS64         0x00001000
#define IXGB_STATUS_PCIX_MODE     0x00002000
#define IXGB_STATUS_PCIX_SPD_MASK 0x0000C000
#define IXGB_STATUS_PCIX_SPD_66   0x00000000
#define IXGB_STATUS_PCIX_SPD_100  0x00004000
#define IXGB_STATUS_PCIX_SPD_133  0x00008000
#define IXGB_STATUS_REV_ID_MASK   0x000F0000
#define IXGB_STATUS_REV_ID_SHIFT  16

/* EECD Bit Masks */
#define IXGB_EECD_SK       0x00000001
#define IXGB_EECD_CS       0x00000002
#define IXGB_EECD_DI       0x00000004
#define IXGB_EECD_DO       0x00000008
#define IXGB_EECD_FWE_MASK 0x00000030
#define IXGB_EECD_FWE_DIS  0x00000010
#define IXGB_EECD_FWE_EN   0x00000020

/* MFS */
#define IXGB_MFS_SHIFT 16

/* Interrupt Register Bit Masks (used for ICR, ICS, IMS, and IMC) */
#define IXGB_INT_TXDW     0x00000001
#define IXGB_INT_TXQE     0x00000002
#define IXGB_INT_LSC      0x00000004
#define IXGB_INT_RXSEQ    0x00000008
#define IXGB_INT_RXDMT0   0x00000010
#define IXGB_INT_RXO      0x00000040
#define IXGB_INT_RXT0     0x00000080
#define IXGB_INT_AUTOSCAN 0x00000200
#define IXGB_INT_GPI0     0x00000800
#define IXGB_INT_GPI1     0x00001000
#define IXGB_INT_GPI2     0x00002000
#define IXGB_INT_GPI3     0x00004000

/* RCTL Bit Masks */
#define IXGB_RCTL_RXEN        0x00000002
#define IXGB_RCTL_SBP         0x00000004
#define IXGB_RCTL_UPE         0x00000008
#define IXGB_RCTL_MPE         0x00000010
#define IXGB_RCTL_RDMTS_MASK  0x00000300
#define IXGB_RCTL_RDMTS_1_2   0x00000000
#define IXGB_RCTL_RDMTS_1_4   0x00000100
#define IXGB_RCTL_RDMTS_1_8   0x00000200
#define IXGB_RCTL_MO_MASK     0x00003000
#define IXGB_RCTL_MO_47_36    0x00000000
#define IXGB_RCTL_MO_46_35    0x00001000
#define IXGB_RCTL_MO_45_34    0x00002000
#define IXGB_RCTL_MO_43_32    0x00003000
#define IXGB_RCTL_MO_SHIFT    12
#define IXGB_RCTL_BAM         0x00008000
#define IXGB_RCTL_BSIZE_MASK  0x00030000
#define IXGB_RCTL_BSIZE_2048  0x00000000
#define IXGB_RCTL_BSIZE_4096  0x00010000
#define IXGB_RCTL_BSIZE_8192  0x00020000
#define IXGB_RCTL_BSIZE_16384 0x00030000
#define IXGB_RCTL_VFE         0x00040000
#define IXGB_RCTL_CFIEN       0x00080000
#define IXGB_RCTL_CFI         0x00100000
#define IXGB_RCTL_RPDA_MASK   0x00600000
#define IXGB_RCTL_RPDA_MC_MAC 0x00000000
#define IXGB_RCTL_MC_ONLY     0x00400000
#define IXGB_RCTL_CFF         0x00800000
#define IXGB_RCTL_SECRC       0x04000000
#define IXGB_RDT_FPDB         0x80000000

#define IXGB_RCTL_IDLE_RX_UNIT 0

/* FCRTL Bit Masks */
#define IXGB_FCRTL_XONE       0x80000000

/* RXDCTL Bit Masks */
#define IXGB_RXDCTL_PTHRESH_MASK  0x000001FF
#define IXGB_RXDCTL_PTHRESH_SHIFT 0
#define IXGB_RXDCTL_HTHRESH_MASK  0x0003FE00
#define IXGB_RXDCTL_HTHRESH_SHIFT 9
#define IXGB_RXDCTL_WTHRESH_MASK  0x07FC0000
#define IXGB_RXDCTL_WTHRESH_SHIFT 18

/* RAIDC Bit Masks */
#define IXGB_RAIDC_HIGHTHRS_MASK 0x0000003F
#define IXGB_RAIDC_DELAY_MASK    0x000FF800
#define IXGB_RAIDC_DELAY_SHIFT   11
#define IXGB_RAIDC_POLL_MASK     0x1FF00000
#define IXGB_RAIDC_POLL_SHIFT    20
#define IXGB_RAIDC_RXT_GATE      0x40000000
#define IXGB_RAIDC_EN            0x80000000

#define IXGB_RAIDC_POLL_1000_INTERRUPTS_PER_SECOND      1220
#define IXGB_RAIDC_POLL_5000_INTERRUPTS_PER_SECOND      244
#define IXGB_RAIDC_POLL_10000_INTERRUPTS_PER_SECOND     122
#define IXGB_RAIDC_POLL_20000_INTERRUPTS_PER_SECOND     61

/* RXCSUM Bit Masks */
#define IXGB_RXCSUM_IPOFL 0x00000100
#define IXGB_RXCSUM_TUOFL 0x00000200

/* RAH Bit Masks */
#define IXGB_RAH_ASEL_MASK 0x00030000
#define IXGB_RAH_ASEL_DEST 0x00000000
#define IXGB_RAH_ASEL_SRC  0x00010000
#define IXGB_RAH_AV        0x80000000

/* TCTL Bit Masks */
#define IXGB_TCTL_TCE  0x00000001
#define IXGB_TCTL_TXEN 0x00000002
#define IXGB_TCTL_TPDE 0x00000004

#define IXGB_TCTL_IDLE_TX_UNIT  0

/* TXDCTL Bit Masks */
#define IXGB_TXDCTL_PTHRESH_MASK  0x0000007F
#define IXGB_TXDCTL_HTHRESH_MASK  0x00007F00
#define IXGB_TXDCTL_HTHRESH_SHIFT 8
#define IXGB_TXDCTL_WTHRESH_MASK  0x007F0000
#define IXGB_TXDCTL_WTHRESH_SHIFT 16

/* TSPMT Bit Masks */
#define IXGB_TSPMT_TSMT_MASK   0x0000FFFF
#define IXGB_TSPMT_TSPBP_MASK  0xFFFF0000
#define IXGB_TSPMT_TSPBP_SHIFT 16

/* PAP Bit Masks */
#define IXGB_PAP_TXPC_MASK 0x0000FFFF
#define IXGB_PAP_TXPV_MASK 0x000F0000
#define IXGB_PAP_TXPV_10G  0x00000000
#define IXGB_PAP_TXPV_1G   0x00010000
#define IXGB_PAP_TXPV_2G   0x00020000
#define IXGB_PAP_TXPV_3G   0x00030000
#define IXGB_PAP_TXPV_4G   0x00040000
#define IXGB_PAP_TXPV_5G   0x00050000
#define IXGB_PAP_TXPV_6G   0x00060000
#define IXGB_PAP_TXPV_7G   0x00070000
#define IXGB_PAP_TXPV_8G   0x00080000
#define IXGB_PAP_TXPV_9G   0x00090000
#define IXGB_PAP_TXPV_WAN  0x000F0000

/* PCSC1 Bit Masks */
#define IXGB_PCSC1_LOOPBACK 0x00004000

/* PCSC2 Bit Masks */
#define IXGB_PCSC2_PCS_TYPE_MASK  0x00000003
#define IXGB_PCSC2_PCS_TYPE_10GBX 0x00000001

/* PCSS1 Bit Masks */
#define IXGB_PCSS1_LOCAL_FAULT    0x00000080
#define IXGB_PCSS1_RX_LINK_STATUS 0x00000004

/* PCSS2 Bit Masks */
#define IXGB_PCSS2_DEV_PRES_MASK 0x0000C000
#define IXGB_PCSS2_DEV_PRES      0x00004000
#define IXGB_PCSS2_TX_LF         0x00000800
#define IXGB_PCSS2_RX_LF         0x00000400
#define IXGB_PCSS2_10GBW         0x00000004
#define IXGB_PCSS2_10GBX         0x00000002
#define IXGB_PCSS2_10GBR         0x00000001

/* XPCSS Bit Masks */
#define IXGB_XPCSS_ALIGN_STATUS 0x00001000
#define IXGB_XPCSS_PATTERN_TEST 0x00000800
#define IXGB_XPCSS_LANE_3_SYNC  0x00000008
#define IXGB_XPCSS_LANE_2_SYNC  0x00000004
#define IXGB_XPCSS_LANE_1_SYNC  0x00000002
#define IXGB_XPCSS_LANE_0_SYNC  0x00000001

/* XPCSTC Bit Masks */
#define IXGB_XPCSTC_BERT_TRIG       0x00200000
#define IXGB_XPCSTC_BERT_SST        0x00100000
#define IXGB_XPCSTC_BERT_PSZ_MASK   0x000C0000
#define IXGB_XPCSTC_BERT_PSZ_SHIFT  17
#define IXGB_XPCSTC_BERT_PSZ_INF    0x00000003
#define IXGB_XPCSTC_BERT_PSZ_68     0x00000001
#define IXGB_XPCSTC_BERT_PSZ_1028   0x00000000

/* MSCA bit Masks */
/* New Protocol Address */
#define IXGB_MSCA_NP_ADDR_MASK      0x0000FFFF
#define IXGB_MSCA_NP_ADDR_SHIFT     0
/* Either Device Type or Register Address,depending on ST_CODE */
#define IXGB_MSCA_DEV_TYPE_MASK     0x001F0000
#define IXGB_MSCA_DEV_TYPE_SHIFT    16
#define IXGB_MSCA_PHY_ADDR_MASK     0x03E00000
#define IXGB_MSCA_PHY_ADDR_SHIFT    21
#define IXGB_MSCA_OP_CODE_MASK      0x0C000000
/* OP_CODE == 00, Address cycle, New Protocol           */
/* OP_CODE == 01, Write operation                       */
/* OP_CODE == 10, Read operation                        */
/* OP_CODE == 11, Read, auto increment, New Protocol    */
#define IXGB_MSCA_ADDR_CYCLE        0x00000000
#define IXGB_MSCA_WRITE             0x04000000
#define IXGB_MSCA_READ              0x08000000
#define IXGB_MSCA_READ_AUTOINC      0x0C000000
#define IXGB_MSCA_OP_CODE_SHIFT     26
#define IXGB_MSCA_ST_CODE_MASK      0x30000000
/* ST_CODE == 00, New Protocol  */
/* ST_CODE == 01, Old Protocol  */
#define IXGB_MSCA_NEW_PROTOCOL      0x00000000
#define IXGB_MSCA_OLD_PROTOCOL      0x10000000
#define IXGB_MSCA_ST_CODE_SHIFT     28
/* Initiate command, self-clearing when command completes */
#define IXGB_MSCA_MDI_COMMAND       0x40000000
/*MDI In Progress Enable. */
#define IXGB_MSCA_MDI_IN_PROG_EN    0x80000000

/* MSRWD bit masks */
#define IXGB_MSRWD_WRITE_DATA_MASK  0x0000FFFF
#define IXGB_MSRWD_WRITE_DATA_SHIFT 0
#define IXGB_MSRWD_READ_DATA_MASK   0xFFFF0000
#define IXGB_MSRWD_READ_DATA_SHIFT  16

/* Definitions for the optics devices on the MDIO bus. */
#define IXGB_PHY_ADDRESS             0x0	/* Single PHY, multiple "Devices" */

#define MDIO_PMA_PMD_XPAK_VENDOR_NAME       0x803A	/* XPAK/XENPAK devices only */

/* Vendor-specific MDIO registers */
#define G6XXX_PMA_PMD_VS1                   0xC001	/* Vendor-specific register */
#define G6XXX_XGXS_XAUI_VS2                 0x18	/* Vendor-specific register */

#define G6XXX_PMA_PMD_VS1_PLL_RESET         0x80
#define G6XXX_PMA_PMD_VS1_REMOVE_PLL_RESET  0x00
#define G6XXX_XGXS_XAUI_VS2_INPUT_MASK      0x0F	/* XAUI lanes synchronized */

/* Layout of a single receive descriptor.  The controller assumes that this
 * structure is packed into 16 bytes, which is a safe assumption with most
 * compilers.  However, some compilers may insert padding between the fields,
 * in which case the structure must be packed in some compiler-specific
 * manner. */
struct ixgb_rx_desc {
	__le64 buff_addr;
	__le16 length;
	__le16 reserved;
	u8 status;
	u8 errors;
	__le16 special;
};

#define IXGB_RX_DESC_STATUS_DD    0x01
#define IXGB_RX_DESC_STATUS_EOP   0x02
#define IXGB_RX_DESC_STATUS_IXSM  0x04
#define IXGB_RX_DESC_STATUS_VP    0x08
#define IXGB_RX_DESC_STATUS_TCPCS 0x20
#define IXGB_RX_DESC_STATUS_IPCS  0x40
#define IXGB_RX_DESC_STATUS_PIF   0x80

#define IXGB_RX_DESC_ERRORS_CE   0x01
#define IXGB_RX_DESC_ERRORS_SE   0x02
#define IXGB_RX_DESC_ERRORS_P    0x08
#define IXGB_RX_DESC_ERRORS_TCPE 0x20
#define IXGB_RX_DESC_ERRORS_IPE  0x40
#define IXGB_RX_DESC_ERRORS_RXE  0x80

#define IXGB_RX_DESC_SPECIAL_VLAN_MASK  0x0FFF	/* VLAN ID is in lower 12 bits */
#define IXGB_RX_DESC_SPECIAL_PRI_MASK   0xE000	/* Priority is in upper 3 bits */
#define IXGB_RX_DESC_SPECIAL_PRI_SHIFT  0x000D	/* Priority is in upper 3 of 16 */

/* Layout of a single transmit descriptor.  The controller assumes that this
 * structure is packed into 16 bytes, which is a safe assumption with most
 * compilers.  However, some compilers may insert padding between the fields,
 * in which case the structure must be packed in some compiler-specific
 * manner. */
struct ixgb_tx_desc {
	__le64 buff_addr;
	__le32 cmd_type_len;
	u8 status;
	u8 popts;
	__le16 vlan;
};

#define IXGB_TX_DESC_LENGTH_MASK    0x000FFFFF
#define IXGB_TX_DESC_TYPE_MASK      0x00F00000
#define IXGB_TX_DESC_TYPE_SHIFT     20
#define IXGB_TX_DESC_CMD_MASK       0xFF000000
#define IXGB_TX_DESC_CMD_SHIFT      24
#define IXGB_TX_DESC_CMD_EOP        0x01000000
#define IXGB_TX_DESC_CMD_TSE        0x04000000
#define IXGB_TX_DESC_CMD_RS         0x08000000
#define IXGB_TX_DESC_CMD_VLE        0x40000000
#define IXGB_TX_DESC_CMD_IDE        0x80000000

#define IXGB_TX_DESC_TYPE           0x00100000

#define IXGB_TX_DESC_STATUS_DD  0x01

#define IXGB_TX_DESC_POPTS_IXSM 0x01
#define IXGB_TX_DESC_POPTS_TXSM 0x02
#define IXGB_TX_DESC_SPECIAL_PRI_SHIFT  IXGB_RX_DESC_SPECIAL_PRI_SHIFT	/* Priority is in upper 3 of 16 */

struct ixgb_context_desc {
	u8 ipcss;
	u8 ipcso;
	__le16 ipcse;
	u8 tucss;
	u8 tucso;
	__le16 tucse;
	__le32 cmd_type_len;
	u8 status;
	u8 hdr_len;
	__le16 mss;
};

#define IXGB_CONTEXT_DESC_CMD_TCP 0x01000000
#define IXGB_CONTEXT_DESC_CMD_IP  0x02000000
#define IXGB_CONTEXT_DESC_CMD_TSE 0x04000000
#define IXGB_CONTEXT_DESC_CMD_RS  0x08000000
#define IXGB_CONTEXT_DESC_CMD_IDE 0x80000000

#define IXGB_CONTEXT_DESC_TYPE 0x00000000

#define IXGB_CONTEXT_DESC_STATUS_DD 0x01

/* Filters */
#define IXGB_MC_TBL_SIZE          128	/* Multicast Filter Table (4096 bits) */
#define IXGB_VLAN_FILTER_TBL_SIZE 128	/* VLAN Filter Table (4096 bits) */
#define IXGB_RAR_ENTRIES		  3	/* Number of entries in Rx Address array */

#define IXGB_MEMORY_REGISTER_BASE_ADDRESS   0
#define ENET_HEADER_SIZE			14
#define ENET_FCS_LENGTH			 4
#define IXGB_MAX_NUM_MULTICAST_ADDRESSES	128
#define IXGB_MIN_ENET_FRAME_SIZE_WITHOUT_FCS	60
#define IXGB_MAX_ENET_FRAME_SIZE_WITHOUT_FCS	1514
#define IXGB_MAX_JUMBO_FRAME_SIZE		0x3F00

/* Phy Addresses */
#define IXGB_OPTICAL_PHY_ADDR 0x0	/* Optical Module phy address */
#define IXGB_XAUII_PHY_ADDR   0x1	/* Xauii transceiver phy address */
#define IXGB_DIAG_PHY_ADDR    0x1F	/* Diagnostic Device phy address */

/* This structure takes a 64k flash and maps it for identification commands */
struct ixgb_flash_buffer {
	u8 manufacturer_id;
	u8 device_id;
	u8 filler1[0x2AA8];
	u8 cmd2;
	u8 filler2[0x2AAA];
	u8 cmd1;
	u8 filler3[0xAAAA];
};

/* Flow control parameters */
struct ixgb_fc {
	u32 high_water;	/* Flow Control High-water          */
	u32 low_water;	/* Flow Control Low-water           */
	u16 pause_time;	/* Flow Control Pause timer         */
	bool send_xon;		/* Flow control send XON            */
	ixgb_fc_type type;	/* Type of flow control             */
};

/* The historical defaults for the flow control values are given below. */
#define FC_DEFAULT_HI_THRESH        (0x8000)	/* 32KB */
#define FC_DEFAULT_LO_THRESH        (0x4000)	/* 16KB */
#define FC_DEFAULT_TX_TIMER         (0x100)	/* ~130 us */

/* Phy definitions */
#define IXGB_MAX_PHY_REG_ADDRESS    0xFFFF
#define IXGB_MAX_PHY_ADDRESS        31
#define IXGB_MAX_PHY_DEV_TYPE       31

/* Bus parameters */
struct ixgb_bus {
	ixgb_bus_speed speed;
	ixgb_bus_width width;
	ixgb_bus_type type;
};

struct ixgb_hw {
	u8 __iomem *hw_addr;/* Base Address of the hardware     */
	void *back;		/* Pointer to OS-dependent struct   */
	struct ixgb_fc fc;	/* Flow control parameters          */
	struct ixgb_bus bus;	/* Bus parameters                   */
	u32 phy_id;	/* Phy Identifier                   */
	u32 phy_addr;	/* XGMII address of Phy             */
	ixgb_mac_type mac_type;	/* Identifier for MAC controller    */
	ixgb_phy_type phy_type;	/* Transceiver/phy identifier       */
	u32 max_frame_size;	/* Maximum frame size supported     */
	u32 mc_filter_type;	/* Multicast filter hash type       */
	u32 num_mc_addrs;	/* Number of current Multicast addrs */
	u8 curr_mac_addr[ETH_ALEN];	/* Individual address currently programmed in MAC */
	u32 num_tx_desc;	/* Number of Transmit descriptors   */
	u32 num_rx_desc;	/* Number of Receive descriptors    */
	u32 rx_buffer_size;	/* Size of Receive buffer           */
	bool link_up;		/* true if link is valid            */
	bool adapter_stopped;	/* State of adapter                 */
	u16 device_id;	/* device id from PCI configuration space */
	u16 vendor_id;	/* vendor id from PCI configuration space */
	u8 revision_id;	/* revision id from PCI configuration space */
	u16 subsystem_vendor_id;	/* subsystem vendor id from PCI configuration space */
	u16 subsystem_id;	/* subsystem id from PCI configuration space */
	u32 bar0;		/* Base Address registers           */
	u32 bar1;
	u32 bar2;
	u32 bar3;
	u16 pci_cmd_word;	/* PCI command register id from PCI configuration space */
	__le16 eeprom[IXGB_EEPROM_SIZE];	/* EEPROM contents read at init time  */
	unsigned long io_base;	/* Our I/O mapped location */
	u32 lastLFC;
	u32 lastRFC;
};

/* Statistics reported by the hardware */
struct ixgb_hw_stats {
	u64 tprl;
	u64 tprh;
	u64 gprcl;
	u64 gprch;
	u64 bprcl;
	u64 bprch;
	u64 mprcl;
	u64 mprch;
	u64 uprcl;
	u64 uprch;
	u64 vprcl;
	u64 vprch;
	u64 jprcl;
	u64 jprch;
	u64 gorcl;
	u64 gorch;
	u64 torl;
	u64 torh;
	u64 rnbc;
	u64 ruc;
	u64 roc;
	u64 rlec;
	u64 crcerrs;
	u64 icbc;
	u64 ecbc;
	u64 mpc;
	u64 tptl;
	u64 tpth;
	u64 gptcl;
	u64 gptch;
	u64 bptcl;
	u64 bptch;
	u64 mptcl;
	u64 mptch;
	u64 uptcl;
	u64 uptch;
	u64 vptcl;
	u64 vptch;
	u64 jptcl;
	u64 jptch;
	u64 gotcl;
	u64 gotch;
	u64 totl;
	u64 toth;
	u64 dc;
	u64 plt64c;
	u64 tsctc;
	u64 tsctfc;
	u64 ibic;
	u64 rfc;
	u64 lfc;
	u64 pfrc;
	u64 pftc;
	u64 mcfrc;
	u64 mcftc;
	u64 xonrxc;
	u64 xontxc;
	u64 xoffrxc;
	u64 xofftxc;
	u64 rjc;
};

/* Function Prototypes */
bool ixgb_adapter_stop(struct ixgb_hw *hw);
bool ixgb_init_hw(struct ixgb_hw *hw);
bool ixgb_adapter_start(struct ixgb_hw *hw);
void ixgb_check_for_link(struct ixgb_hw *hw);
bool ixgb_check_for_bad_link(struct ixgb_hw *hw);

void ixgb_rar_set(struct ixgb_hw *hw, const u8 *addr, u32 index);

/* Filters (multicast, vlan, receive) */
void ixgb_mc_addr_list_update(struct ixgb_hw *hw, u8 *mc_addr_list,
			      u32 mc_addr_count, u32 pad);

/* Vfta functions */
void ixgb_write_vfta(struct ixgb_hw *hw, u32 offset, u32 value);

/* Access functions to eeprom data */
void ixgb_get_ee_mac_addr(struct ixgb_hw *hw, u8 *mac_addr);
u32 ixgb_get_ee_pba_number(struct ixgb_hw *hw);
u16 ixgb_get_ee_device_id(struct ixgb_hw *hw);
bool ixgb_get_eeprom_data(struct ixgb_hw *hw);
__le16 ixgb_get_eeprom_word(struct ixgb_hw *hw, u16 index);

/* Everything else */
void ixgb_led_on(struct ixgb_hw *hw);
void ixgb_led_off(struct ixgb_hw *hw);
void ixgb_write_pci_cfg(struct ixgb_hw *hw,
			 u32 reg,
			 u16 * value);


#endif /* _IXGB_HW_H_ */
