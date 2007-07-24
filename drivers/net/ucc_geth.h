/*
 * Copyright (C) Freescale Semicondutor, Inc. 2006. All rights reserved.
 *
 * Author: Shlomi Gridish <gridish@freescale.com>
 *
 * Description:
 * Internal header file for UCC Gigabit Ethernet unit routines.
 *
 * Changelog:
 * Jun 28, 2006 Li Yang <LeoLi@freescale.com>
 * - Rearrange code and style fixes
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#ifndef __UCC_GETH_H__
#define __UCC_GETH_H__

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/fsl_devices.h>

#include <asm/immap_qe.h>
#include <asm/qe.h>

#include <asm/ucc.h>
#include <asm/ucc_fast.h>

#include "ucc_geth_mii.h"

#define DRV_DESC "QE UCC Gigabit Ethernet Controller"
#define DRV_NAME "ucc_geth"
#define DRV_VERSION "1.1"

#define NUM_TX_QUEUES                   8
#define NUM_RX_QUEUES                   8
#define NUM_BDS_IN_PREFETCHED_BDS       4
#define TX_IP_OFFSET_ENTRY_MAX          8
#define NUM_OF_PADDRS                   4
#define ENET_INIT_PARAM_MAX_ENTRIES_RX  9
#define ENET_INIT_PARAM_MAX_ENTRIES_TX  8

struct ucc_geth {
	struct ucc_fast uccf;

	u32 maccfg1;		/* mac configuration reg. 1 */
	u32 maccfg2;		/* mac configuration reg. 2 */
	u32 ipgifg;		/* interframe gap reg.  */
	u32 hafdup;		/* half-duplex reg.  */
	u8 res1[0x10];
	u8 miimng[0x18];	/* MII management structure moved to _mii.h */
	u32 ifctl;		/* interface control reg */
	u32 ifstat;		/* interface statux reg */
	u32 macstnaddr1;	/* mac station address part 1 reg */
	u32 macstnaddr2;	/* mac station address part 2 reg */
	u8 res2[0x8];
	u32 uempr;		/* UCC Ethernet Mac parameter reg */
	u32 utbipar;		/* UCC tbi address reg */
	u16 uescr;		/* UCC Ethernet statistics control reg */
	u8 res3[0x180 - 0x15A];
	u32 tx64;		/* Total number of frames (including bad
				   frames) transmitted that were exactly of the
				   minimal length (64 for un tagged, 68 for
				   tagged, or with length exactly equal to the
				   parameter MINLength */
	u32 tx127;		/* Total number of frames (including bad
				   frames) transmitted that were between
				   MINLength (Including FCS length==4) and 127
				   octets */
	u32 tx255;		/* Total number of frames (including bad
				   frames) transmitted that were between 128
				   (Including FCS length==4) and 255 octets */
	u32 rx64;		/* Total number of frames received including
				   bad frames that were exactly of the mninimal
				   length (64 bytes) */
	u32 rx127;		/* Total number of frames (including bad
				   frames) received that were between MINLength
				   (Including FCS length==4) and 127 octets */
	u32 rx255;		/* Total number of frames (including bad
				   frames) received that were between 128
				   (Including FCS length==4) and 255 octets */
	u32 txok;		/* Total number of octets residing in frames
				   that where involved in succesfull
				   transmission */
	u16 txcf;		/* Total number of PAUSE control frames
				   transmitted by this MAC */
	u8 res4[0x2];
	u32 tmca;		/* Total number of frames that were transmitted
				   succesfully with the group address bit set
				   that are not broadcast frames */
	u32 tbca;		/* Total number of frames transmitted
				   succesfully that had destination address
				   field equal to the broadcast address */
	u32 rxfok;		/* Total number of frames received OK */
	u32 rxbok;		/* Total number of octets received OK */
	u32 rbyt;		/* Total number of octets received including
				   octets in bad frames. Must be implemented in
				   HW because it includes octets in frames that
				   never even reach the UCC */
	u32 rmca;		/* Total number of frames that were received
				   succesfully with the group address bit set
				   that are not broadcast frames */
	u32 rbca;		/* Total number of frames received succesfully
				   that had destination address equal to the
				   broadcast address */
	u32 scar;		/* Statistics carry register */
	u32 scam;		/* Statistics caryy mask register */
	u8 res5[0x200 - 0x1c4];
} __attribute__ ((packed));

/* UCC GETH TEMODR Register */
#define TEMODER_TX_RMON_STATISTICS_ENABLE       0x0100	/* enable Tx statistics
							 */
#define TEMODER_SCHEDULER_ENABLE                0x2000	/* enable scheduler */
#define TEMODER_IP_CHECKSUM_GENERATE            0x0400	/* generate IPv4
							   checksums */
#define TEMODER_PERFORMANCE_OPTIMIZATION_MODE1  0x0200	/* enable performance
							   optimization
							   enhancement (mode1) */
#define TEMODER_RMON_STATISTICS                 0x0100	/* enable tx statistics
							 */
#define TEMODER_NUM_OF_QUEUES_SHIFT             (15-15)	/* Number of queues <<
							   shift */

/* UCC GETH TEMODR Register */
#define REMODER_RX_RMON_STATISTICS_ENABLE       0x00001000	/* enable Rx
								   statistics */
#define REMODER_RX_EXTENDED_FEATURES            0x80000000	/* enable
								   extended
								   features */
#define REMODER_VLAN_OPERATION_TAGGED_SHIFT     (31-9 )	/* vlan operation
							   tagged << shift */
#define REMODER_VLAN_OPERATION_NON_TAGGED_SHIFT (31-10)	/* vlan operation non
							   tagged << shift */
#define REMODER_RX_QOS_MODE_SHIFT               (31-15)	/* rx QoS mode << shift
							 */
#define REMODER_RMON_STATISTICS                 0x00001000	/* enable rx
								   statistics */
#define REMODER_RX_EXTENDED_FILTERING           0x00000800	/* extended
								   filtering
								   vs.
								   mpc82xx-like
								   filtering */
#define REMODER_NUM_OF_QUEUES_SHIFT             (31-23)	/* Number of queues <<
							   shift */
#define REMODER_DYNAMIC_MAX_FRAME_LENGTH        0x00000008	/* enable
								   dynamic max
								   frame length
								 */
#define REMODER_DYNAMIC_MIN_FRAME_LENGTH        0x00000004	/* enable
								   dynamic min
								   frame length
								 */
#define REMODER_IP_CHECKSUM_CHECK               0x00000002	/* check IPv4
								   checksums */
#define REMODER_IP_ADDRESS_ALIGNMENT            0x00000001	/* align ip
								   address to
								   4-byte
								   boundary */

/* UCC GETH Event Register */
#define UCCE_MPD                                0x80000000	/* Magic packet
								   detection */
#define UCCE_SCAR                               0x40000000
#define UCCE_GRA                                0x20000000	/* Tx graceful
								   stop
								   complete */
#define UCCE_CBPR                               0x10000000
#define UCCE_BSY                                0x08000000
#define UCCE_RXC                                0x04000000
#define UCCE_TXC                                0x02000000
#define UCCE_TXE                                0x01000000
#define UCCE_TXB7                               0x00800000
#define UCCE_TXB6                               0x00400000
#define UCCE_TXB5                               0x00200000
#define UCCE_TXB4                               0x00100000
#define UCCE_TXB3                               0x00080000
#define UCCE_TXB2                               0x00040000
#define UCCE_TXB1                               0x00020000
#define UCCE_TXB0                               0x00010000
#define UCCE_RXB7                               0x00008000
#define UCCE_RXB6                               0x00004000
#define UCCE_RXB5                               0x00002000
#define UCCE_RXB4                               0x00001000
#define UCCE_RXB3                               0x00000800
#define UCCE_RXB2                               0x00000400
#define UCCE_RXB1                               0x00000200
#define UCCE_RXB0                               0x00000100
#define UCCE_RXF7                               0x00000080
#define UCCE_RXF6                               0x00000040
#define UCCE_RXF5                               0x00000020
#define UCCE_RXF4                               0x00000010
#define UCCE_RXF3                               0x00000008
#define UCCE_RXF2                               0x00000004
#define UCCE_RXF1                               0x00000002
#define UCCE_RXF0                               0x00000001

#define UCCE_RXBF_SINGLE_MASK                   (UCCE_RXF0)
#define UCCE_TXBF_SINGLE_MASK                   (UCCE_TXB0)

#define UCCE_TXB         (UCCE_TXB7 | UCCE_TXB6 | UCCE_TXB5 | UCCE_TXB4 |\
			UCCE_TXB3 | UCCE_TXB2 | UCCE_TXB1 | UCCE_TXB0)
#define UCCE_RXB         (UCCE_RXB7 | UCCE_RXB6 | UCCE_RXB5 | UCCE_RXB4 |\
			UCCE_RXB3 | UCCE_RXB2 | UCCE_RXB1 | UCCE_RXB0)
#define UCCE_RXF         (UCCE_RXF7 | UCCE_RXF6 | UCCE_RXF5 | UCCE_RXF4 |\
			UCCE_RXF3 | UCCE_RXF2 | UCCE_RXF1 | UCCE_RXF0)
#define UCCE_OTHER       (UCCE_SCAR | UCCE_GRA  | UCCE_CBPR | UCCE_BSY  |\
			UCCE_RXC  | UCCE_TXC  | UCCE_TXE)

#define UCCE_RX_EVENTS							(UCCE_RXF | UCCE_BSY)
#define UCCE_TX_EVENTS							(UCCE_TXB | UCCE_TXE)

/* UCC GETH UPSMR (Protocol Specific Mode Register) */
#define UPSMR_ECM                               0x04000000	/* Enable CAM
								   Miss or
								   Enable
								   Filtering
								   Miss */
#define UPSMR_HSE                               0x02000000	/* Hardware
								   Statistics
								   Enable */
#define UPSMR_PRO                               0x00400000	/* Promiscuous*/
#define UPSMR_CAP                               0x00200000	/* CAM polarity
								 */
#define UPSMR_RSH                               0x00100000	/* Receive
								   Short Frames
								 */
#define UPSMR_RPM                               0x00080000	/* Reduced Pin
								   Mode
								   interfaces */
#define UPSMR_R10M                              0x00040000	/* RGMII/RMII
								   10 Mode */
#define UPSMR_RLPB                              0x00020000	/* RMII
								   Loopback
								   Mode */
#define UPSMR_TBIM                              0x00010000	/* Ten-bit
								   Interface
								   Mode */
#define UPSMR_RMM                               0x00001000	/* RMII/RGMII
								   Mode */
#define UPSMR_CAM                               0x00000400	/* CAM Address
								   Matching */
#define UPSMR_BRO                               0x00000200	/* Broadcast
								   Address */
#define UPSMR_RES1                              0x00002000	/* Reserved
								   feild - must
								   be 1 */

/* UCC GETH MACCFG1 (MAC Configuration 1 Register) */
#define MACCFG1_FLOW_RX                         0x00000020	/* Flow Control
								   Rx */
#define MACCFG1_FLOW_TX                         0x00000010	/* Flow Control
								   Tx */
#define MACCFG1_ENABLE_SYNCHED_RX               0x00000008	/* Rx Enable
								   synchronized
								   to Rx stream
								 */
#define MACCFG1_ENABLE_RX                       0x00000004	/* Enable Rx */
#define MACCFG1_ENABLE_SYNCHED_TX               0x00000002	/* Tx Enable
								   synchronized
								   to Tx stream
								 */
#define MACCFG1_ENABLE_TX                       0x00000001	/* Enable Tx */

/* UCC GETH MACCFG2 (MAC Configuration 2 Register) */
#define MACCFG2_PREL_SHIFT                      (31 - 19)	/* Preamble
								   Length <<
								   shift */
#define MACCFG2_PREL_MASK                       0x0000f000	/* Preamble
								   Length mask */
#define MACCFG2_SRP                             0x00000080	/* Soft Receive
								   Preamble */
#define MACCFG2_STP                             0x00000040	/* Soft
								   Transmit
								   Preamble */
#define MACCFG2_RESERVED_1                      0x00000020	/* Reserved -
								   must be set
								   to 1 */
#define MACCFG2_LC                              0x00000010	/* Length Check
								 */
#define MACCFG2_MPE                             0x00000008	/* Magic packet
								   detect */
#define MACCFG2_FDX                             0x00000001	/* Full Duplex */
#define MACCFG2_FDX_MASK                        0x00000001	/* Full Duplex
								   mask */
#define MACCFG2_PAD_CRC                         0x00000004
#define MACCFG2_CRC_EN                          0x00000002
#define MACCFG2_PAD_AND_CRC_MODE_NONE           0x00000000	/* Neither
								   Padding
								   short frames
								   nor CRC */
#define MACCFG2_PAD_AND_CRC_MODE_CRC_ONLY       0x00000002	/* Append CRC
								   only */
#define MACCFG2_PAD_AND_CRC_MODE_PAD_AND_CRC    0x00000004
#define MACCFG2_INTERFACE_MODE_NIBBLE           0x00000100	/* nibble mode
								   (MII/RMII/RGMII
								   10/100bps) */
#define MACCFG2_INTERFACE_MODE_BYTE             0x00000200	/* byte mode
								   (GMII/TBI/RTB/RGMII
								   1000bps ) */
#define MACCFG2_INTERFACE_MODE_MASK             0x00000300	/* mask
								   covering all
								   relevant
								   bits */

/* UCC GETH IPGIFG (Inter-frame Gap / Inter-Frame Gap Register) */
#define IPGIFG_NON_BACK_TO_BACK_IFG_PART1_SHIFT (31 -  7)	/* Non
								   back-to-back
								   inter frame
								   gap part 1.
								   << shift */
#define IPGIFG_NON_BACK_TO_BACK_IFG_PART2_SHIFT (31 - 15)	/* Non
								   back-to-back
								   inter frame
								   gap part 2.
								   << shift */
#define IPGIFG_MINIMUM_IFG_ENFORCEMENT_SHIFT    (31 - 23)	/* Mimimum IFG
								   Enforcement
								   << shift */
#define IPGIFG_BACK_TO_BACK_IFG_SHIFT           (31 - 31)	/* back-to-back
								   inter frame
								   gap << shift
								 */
#define IPGIFG_NON_BACK_TO_BACK_IFG_PART1_MAX   127	/* Non back-to-back
							   inter frame gap part
							   1. max val */
#define IPGIFG_NON_BACK_TO_BACK_IFG_PART2_MAX   127	/* Non back-to-back
							   inter frame gap part
							   2. max val */
#define IPGIFG_MINIMUM_IFG_ENFORCEMENT_MAX      255	/* Mimimum IFG
							   Enforcement max val */
#define IPGIFG_BACK_TO_BACK_IFG_MAX             127	/* back-to-back inter
							   frame gap max val */
#define IPGIFG_NBTB_CS_IPG_MASK                 0x7F000000
#define IPGIFG_NBTB_IPG_MASK                    0x007F0000
#define IPGIFG_MIN_IFG_MASK                     0x0000FF00
#define IPGIFG_BTB_IPG_MASK                     0x0000007F

/* UCC GETH HAFDUP (Half Duplex Register) */
#define HALFDUP_ALT_BEB_TRUNCATION_SHIFT        (31 - 11)	/* Alternate
								   Binary
								   Exponential
								   Backoff
								   Truncation
								   << shift */
#define HALFDUP_ALT_BEB_TRUNCATION_MAX          0xf	/* Alternate Binary
							   Exponential Backoff
							   Truncation max val */
#define HALFDUP_ALT_BEB                         0x00080000	/* Alternate
								   Binary
								   Exponential
								   Backoff */
#define HALFDUP_BACK_PRESSURE_NO_BACKOFF        0x00040000	/* Back
								   pressure no
								   backoff */
#define HALFDUP_NO_BACKOFF                      0x00020000	/* No Backoff */
#define HALFDUP_EXCESSIVE_DEFER                 0x00010000	/* Excessive
								   Defer */
#define HALFDUP_MAX_RETRANSMISSION_SHIFT        (31 - 19)	/* Maximum
								   Retransmission
								   << shift */
#define HALFDUP_MAX_RETRANSMISSION_MAX          0xf	/* Maximum
							   Retransmission max
							   val */
#define HALFDUP_COLLISION_WINDOW_SHIFT          (31 - 31)	/* Collision
								   Window <<
								   shift */
#define HALFDUP_COLLISION_WINDOW_MAX            0x3f	/* Collision Window max
							   val */
#define HALFDUP_ALT_BEB_TR_MASK                 0x00F00000
#define HALFDUP_RETRANS_MASK                    0x0000F000
#define HALFDUP_COL_WINDOW_MASK                 0x0000003F

/* UCC GETH UCCS (Ethernet Status Register) */
#define UCCS_BPR                                0x02	/* Back pressure (in
							   half duplex mode) */
#define UCCS_PAU                                0x02	/* Pause state (in full
							   duplex mode) */
#define UCCS_MPD                                0x01	/* Magic Packet
							   Detected */

/* UCC GETH IFSTAT (Interface Status Register) */
#define IFSTAT_EXCESS_DEFER                     0x00000200	/* Excessive
								   transmission
								   defer */

/* UCC GETH MACSTNADDR1 (Station Address Part 1 Register) */
#define MACSTNADDR1_OCTET_6_SHIFT               (31 -  7)	/* Station
								   address 6th
								   octet <<
								   shift */
#define MACSTNADDR1_OCTET_5_SHIFT               (31 - 15)	/* Station
								   address 5th
								   octet <<
								   shift */
#define MACSTNADDR1_OCTET_4_SHIFT               (31 - 23)	/* Station
								   address 4th
								   octet <<
								   shift */
#define MACSTNADDR1_OCTET_3_SHIFT               (31 - 31)	/* Station
								   address 3rd
								   octet <<
								   shift */

/* UCC GETH MACSTNADDR2 (Station Address Part 2 Register) */
#define MACSTNADDR2_OCTET_2_SHIFT               (31 -  7)	/* Station
								   address 2nd
								   octet <<
								   shift */
#define MACSTNADDR2_OCTET_1_SHIFT               (31 - 15)	/* Station
								   address 1st
								   octet <<
								   shift */

/* UCC GETH UEMPR (Ethernet Mac Parameter Register) */
#define UEMPR_PAUSE_TIME_VALUE_SHIFT            (31 - 15)	/* Pause time
								   value <<
								   shift */
#define UEMPR_EXTENDED_PAUSE_TIME_VALUE_SHIFT   (31 - 31)	/* Extended
								   pause time
								   value <<
								   shift */

/* UCC GETH UTBIPAR (Ten Bit Interface Physical Address Register) */
#define UTBIPAR_PHY_ADDRESS_SHIFT               (31 - 31)	/* Phy address
								   << shift */
#define UTBIPAR_PHY_ADDRESS_MASK                0x0000001f	/* Phy address
								   mask */

/* UCC GETH UESCR (Ethernet Statistics Control Register) */
#define UESCR_AUTOZ                             0x8000	/* Automatically zero
							   addressed
							   statistical counter
							   values */
#define UESCR_CLRCNT                            0x4000	/* Clear all statistics
							   counters */
#define UESCR_MAXCOV_SHIFT                      (15 -  7)	/* Max
								   Coalescing
								   Value <<
								   shift */
#define UESCR_SCOV_SHIFT                        (15 - 15)	/* Status
								   Coalescing
								   Value <<
								   shift */

/* UCC GETH UDSR (Data Synchronization Register) */
#define UDSR_MAGIC                              0x067E

struct ucc_geth_thread_data_tx {
	u8 res0[104];
} __attribute__ ((packed));

struct ucc_geth_thread_data_rx {
	u8 res0[40];
} __attribute__ ((packed));

/* Send Queue Queue-Descriptor */
struct ucc_geth_send_queue_qd {
	u32 bd_ring_base;	/* pointer to BD ring base address */
	u8 res0[0x8];
	u32 last_bd_completed_address;/* initialize to last entry in BD ring */
	u8 res1[0x30];
} __attribute__ ((packed));

struct ucc_geth_send_queue_mem_region {
	struct ucc_geth_send_queue_qd sqqd[NUM_TX_QUEUES];
} __attribute__ ((packed));

struct ucc_geth_thread_tx_pram {
	u8 res0[64];
} __attribute__ ((packed));

struct ucc_geth_thread_rx_pram {
	u8 res0[128];
} __attribute__ ((packed));

#define THREAD_RX_PRAM_ADDITIONAL_FOR_EXTENDED_FILTERING        64
#define THREAD_RX_PRAM_ADDITIONAL_FOR_EXTENDED_FILTERING_8      64
#define THREAD_RX_PRAM_ADDITIONAL_FOR_EXTENDED_FILTERING_16     96

struct ucc_geth_scheduler {
	u16 cpucount0;		/* CPU packet counter */
	u16 cpucount1;		/* CPU packet counter */
	u16 cecount0;		/* QE packet counter */
	u16 cecount1;		/* QE packet counter */
	u16 cpucount2;		/* CPU packet counter */
	u16 cpucount3;		/* CPU packet counter */
	u16 cecount2;		/* QE packet counter */
	u16 cecount3;		/* QE packet counter */
	u16 cpucount4;		/* CPU packet counter */
	u16 cpucount5;		/* CPU packet counter */
	u16 cecount4;		/* QE packet counter */
	u16 cecount5;		/* QE packet counter */
	u16 cpucount6;		/* CPU packet counter */
	u16 cpucount7;		/* CPU packet counter */
	u16 cecount6;		/* QE packet counter */
	u16 cecount7;		/* QE packet counter */
	u32 weightstatus[NUM_TX_QUEUES];	/* accumulated weight factor */
	u32 rtsrshadow;		/* temporary variable handled by QE */
	u32 time;		/* temporary variable handled by QE */
	u32 ttl;		/* temporary variable handled by QE */
	u32 mblinterval;	/* max burst length interval */
	u16 nortsrbytetime;	/* normalized value of byte time in tsr units */
	u8 fracsiz;		/* radix 2 log value of denom. of
				   NorTSRByteTime */
	u8 res0[1];
	u8 strictpriorityq;	/* Strict Priority Mask register */
	u8 txasap;		/* Transmit ASAP register */
	u8 extrabw;		/* Extra BandWidth register */
	u8 oldwfqmask;		/* temporary variable handled by QE */
	u8 weightfactor[NUM_TX_QUEUES];
				      /**< weight factor for queues   */
	u32 minw;		/* temporary variable handled by QE */
	u8 res1[0x70 - 0x64];
} __attribute__ ((packed));

struct ucc_geth_tx_firmware_statistics_pram {
	u32 sicoltx;		/* single collision */
	u32 mulcoltx;		/* multiple collision */
	u32 latecoltxfr;	/* late collision */
	u32 frabortduecol;	/* frames aborted due to transmit collision */
	u32 frlostinmactxer;	/* frames lost due to internal MAC error
				   transmission that are not counted on any
				   other counter */
	u32 carriersenseertx;	/* carrier sense error */
	u32 frtxok;		/* frames transmitted OK */
	u32 txfrexcessivedefer;	/* frames with defferal time greater than
				   specified threshold */
	u32 txpkts256;		/* total packets (including bad) between 256
				   and 511 octets */
	u32 txpkts512;		/* total packets (including bad) between 512
				   and 1023 octets */
	u32 txpkts1024;		/* total packets (including bad) between 1024
				   and 1518 octets */
	u32 txpktsjumbo;	/* total packets (including bad) between 1024
				   and MAXLength octets */
} __attribute__ ((packed));

struct ucc_geth_rx_firmware_statistics_pram {
	u32 frrxfcser;		/* frames with crc error */
	u32 fraligner;		/* frames with alignment error */
	u32 inrangelenrxer;	/* in range length error */
	u32 outrangelenrxer;	/* out of range length error */
	u32 frtoolong;		/* frame too long */
	u32 runt;		/* runt */
	u32 verylongevent;	/* very long event */
	u32 symbolerror;	/* symbol error */
	u32 dropbsy;		/* drop because of BD not ready */
	u8 res0[0x8];
	u32 mismatchdrop;	/* drop because of MAC filtering (e.g. address
				   or type mismatch) */
	u32 underpkts;		/* total frames less than 64 octets */
	u32 pkts256;		/* total frames (including bad) between 256 and
				   511 octets */
	u32 pkts512;		/* total frames (including bad) between 512 and
				   1023 octets */
	u32 pkts1024;		/* total frames (including bad) between 1024
				   and 1518 octets */
	u32 pktsjumbo;		/* total frames (including bad) between 1024
				   and MAXLength octets */
	u32 frlossinmacer;	/* frames lost because of internal MAC error
				   that is not counted in any other counter */
	u32 pausefr;		/* pause frames */
	u8 res1[0x4];
	u32 removevlan;		/* total frames that had their VLAN tag removed
				 */
	u32 replacevlan;	/* total frames that had their VLAN tag
				   replaced */
	u32 insertvlan;		/* total frames that had their VLAN tag
				   inserted */
} __attribute__ ((packed));

struct ucc_geth_rx_interrupt_coalescing_entry {
	u32 interruptcoalescingmaxvalue;	/* interrupt coalescing max
						   value */
	u32 interruptcoalescingcounter;	/* interrupt coalescing counter,
					   initialize to
					   interruptcoalescingmaxvalue */
} __attribute__ ((packed));

struct ucc_geth_rx_interrupt_coalescing_table {
	struct ucc_geth_rx_interrupt_coalescing_entry coalescingentry[NUM_RX_QUEUES];
				       /**< interrupt coalescing entry */
} __attribute__ ((packed));

struct ucc_geth_rx_prefetched_bds {
	struct qe_bd bd[NUM_BDS_IN_PREFETCHED_BDS];	/* prefetched bd */
} __attribute__ ((packed));

struct ucc_geth_rx_bd_queues_entry {
	u32 bdbaseptr;		/* BD base pointer */
	u32 bdptr;		/* BD pointer */
	u32 externalbdbaseptr;	/* external BD base pointer */
	u32 externalbdptr;	/* external BD pointer */
} __attribute__ ((packed));

struct ucc_geth_tx_global_pram {
	u16 temoder;
	u8 res0[0x38 - 0x02];
	u32 sqptr;		/* a base pointer to send queue memory region */
	u32 schedulerbasepointer;	/* a base pointer to scheduler memory
					   region */
	u32 txrmonbaseptr;	/* base pointer to Tx RMON statistics counter */
	u32 tstate;		/* tx internal state. High byte contains
				   function code */
	u8 iphoffset[TX_IP_OFFSET_ENTRY_MAX];
	u32 vtagtable[0x8];	/* 8 4-byte VLAN tags */
	u32 tqptr;		/* a base pointer to the Tx Queues Memory
				   Region */
	u8 res2[0x80 - 0x74];
} __attribute__ ((packed));

/* structure representing Extended Filtering Global Parameters in PRAM */
struct ucc_geth_exf_global_pram {
	u32 l2pcdptr;		/* individual address filter, high */
	u8 res0[0x10 - 0x04];
} __attribute__ ((packed));

struct ucc_geth_rx_global_pram {
	u32 remoder;		/* ethernet mode reg. */
	u32 rqptr;		/* base pointer to the Rx Queues Memory Region*/
	u32 res0[0x1];
	u8 res1[0x20 - 0xC];
	u16 typeorlen;		/* cutoff point less than which, type/len field
				   is considered length */
	u8 res2[0x1];
	u8 rxgstpack;		/* acknowledgement on GRACEFUL STOP RX command*/
	u32 rxrmonbaseptr;	/* base pointer to Rx RMON statistics counter */
	u8 res3[0x30 - 0x28];
	u32 intcoalescingptr;	/* Interrupt coalescing table pointer */
	u8 res4[0x36 - 0x34];
	u8 rstate;		/* rx internal state. High byte contains
				   function code */
	u8 res5[0x46 - 0x37];
	u16 mrblr;		/* max receive buffer length reg. */
	u32 rbdqptr;		/* base pointer to RxBD parameter table
				   description */
	u16 mflr;		/* max frame length reg. */
	u16 minflr;		/* min frame length reg. */
	u16 maxd1;		/* max dma1 length reg. */
	u16 maxd2;		/* max dma2 length reg. */
	u32 ecamptr;		/* external CAM address */
	u32 l2qt;		/* VLAN priority mapping table. */
	u32 l3qt[0x8];		/* IP priority mapping table. */
	u16 vlantype;		/* vlan type */
	u16 vlantci;		/* default vlan tci */
	u8 addressfiltering[64];	/* address filtering data structure */
	u32 exfGlobalParam;	/* base address for extended filtering global
				   parameters */
	u8 res6[0x100 - 0xC4];	/* Initialize to zero */
} __attribute__ ((packed));

#define GRACEFUL_STOP_ACKNOWLEDGE_RX            0x01

/* structure representing InitEnet command */
struct ucc_geth_init_pram {
	u8 resinit1;
	u8 resinit2;
	u8 resinit3;
	u8 resinit4;
	u16 resinit5;
	u8 res1[0x1];
	u8 largestexternallookupkeysize;
	u32 rgftgfrxglobal;
	u32 rxthread[ENET_INIT_PARAM_MAX_ENTRIES_RX];	/* rx threads */
	u8 res2[0x38 - 0x30];
	u32 txglobal;		/* tx global */
	u32 txthread[ENET_INIT_PARAM_MAX_ENTRIES_TX];	/* tx threads */
	u8 res3[0x1];
} __attribute__ ((packed));

#define ENET_INIT_PARAM_RGF_SHIFT               (32 - 4)
#define ENET_INIT_PARAM_TGF_SHIFT               (32 - 8)

#define ENET_INIT_PARAM_RISC_MASK               0x0000003f
#define ENET_INIT_PARAM_PTR_MASK                0x00ffffc0
#define ENET_INIT_PARAM_SNUM_MASK               0xff000000
#define ENET_INIT_PARAM_SNUM_SHIFT              24

#define ENET_INIT_PARAM_MAGIC_RES_INIT1         0x06
#define ENET_INIT_PARAM_MAGIC_RES_INIT2         0x30
#define ENET_INIT_PARAM_MAGIC_RES_INIT3         0xff
#define ENET_INIT_PARAM_MAGIC_RES_INIT4         0x00
#define ENET_INIT_PARAM_MAGIC_RES_INIT5         0x0400

/* structure representing 82xx Address Filtering Enet Address in PRAM */
struct ucc_geth_82xx_enet_address {
	u8 res1[0x2];
	u16 h;			/* address (MSB) */
	u16 m;			/* address */
	u16 l;			/* address (LSB) */
} __attribute__ ((packed));

/* structure representing 82xx Address Filtering PRAM */
struct ucc_geth_82xx_address_filtering_pram {
	u32 iaddr_h;		/* individual address filter, high */
	u32 iaddr_l;		/* individual address filter, low */
	u32 gaddr_h;		/* group address filter, high */
	u32 gaddr_l;		/* group address filter, low */
	struct ucc_geth_82xx_enet_address taddr;
	struct ucc_geth_82xx_enet_address paddr[NUM_OF_PADDRS];
	u8 res0[0x40 - 0x38];
} __attribute__ ((packed));

/* GETH Tx firmware statistics structure, used when calling
   UCC_GETH_GetStatistics. */
struct ucc_geth_tx_firmware_statistics {
	u32 sicoltx;		/* single collision */
	u32 mulcoltx;		/* multiple collision */
	u32 latecoltxfr;	/* late collision */
	u32 frabortduecol;	/* frames aborted due to transmit collision */
	u32 frlostinmactxer;	/* frames lost due to internal MAC error
				   transmission that are not counted on any
				   other counter */
	u32 carriersenseertx;	/* carrier sense error */
	u32 frtxok;		/* frames transmitted OK */
	u32 txfrexcessivedefer;	/* frames with defferal time greater than
				   specified threshold */
	u32 txpkts256;		/* total packets (including bad) between 256
				   and 511 octets */
	u32 txpkts512;		/* total packets (including bad) between 512
				   and 1023 octets */
	u32 txpkts1024;		/* total packets (including bad) between 1024
				   and 1518 octets */
	u32 txpktsjumbo;	/* total packets (including bad) between 1024
				   and MAXLength octets */
} __attribute__ ((packed));

/* GETH Rx firmware statistics structure, used when calling
   UCC_GETH_GetStatistics. */
struct ucc_geth_rx_firmware_statistics {
	u32 frrxfcser;		/* frames with crc error */
	u32 fraligner;		/* frames with alignment error */
	u32 inrangelenrxer;	/* in range length error */
	u32 outrangelenrxer;	/* out of range length error */
	u32 frtoolong;		/* frame too long */
	u32 runt;		/* runt */
	u32 verylongevent;	/* very long event */
	u32 symbolerror;	/* symbol error */
	u32 dropbsy;		/* drop because of BD not ready */
	u8 res0[0x8];
	u32 mismatchdrop;	/* drop because of MAC filtering (e.g. address
				   or type mismatch) */
	u32 underpkts;		/* total frames less than 64 octets */
	u32 pkts256;		/* total frames (including bad) between 256 and
				   511 octets */
	u32 pkts512;		/* total frames (including bad) between 512 and
				   1023 octets */
	u32 pkts1024;		/* total frames (including bad) between 1024
				   and 1518 octets */
	u32 pktsjumbo;		/* total frames (including bad) between 1024
				   and MAXLength octets */
	u32 frlossinmacer;	/* frames lost because of internal MAC error
				   that is not counted in any other counter */
	u32 pausefr;		/* pause frames */
	u8 res1[0x4];
	u32 removevlan;		/* total frames that had their VLAN tag removed
				 */
	u32 replacevlan;	/* total frames that had their VLAN tag
				   replaced */
	u32 insertvlan;		/* total frames that had their VLAN tag
				   inserted */
} __attribute__ ((packed));

/* GETH hardware statistics structure, used when calling
   UCC_GETH_GetStatistics. */
struct ucc_geth_hardware_statistics {
	u32 tx64;		/* Total number of frames (including bad
				   frames) transmitted that were exactly of the
				   minimal length (64 for un tagged, 68 for
				   tagged, or with length exactly equal to the
				   parameter MINLength */
	u32 tx127;		/* Total number of frames (including bad
				   frames) transmitted that were between
				   MINLength (Including FCS length==4) and 127
				   octets */
	u32 tx255;		/* Total number of frames (including bad
				   frames) transmitted that were between 128
				   (Including FCS length==4) and 255 octets */
	u32 rx64;		/* Total number of frames received including
				   bad frames that were exactly of the mninimal
				   length (64 bytes) */
	u32 rx127;		/* Total number of frames (including bad
				   frames) received that were between MINLength
				   (Including FCS length==4) and 127 octets */
	u32 rx255;		/* Total number of frames (including bad
				   frames) received that were between 128
				   (Including FCS length==4) and 255 octets */
	u32 txok;		/* Total number of octets residing in frames
				   that where involved in succesfull
				   transmission */
	u16 txcf;		/* Total number of PAUSE control frames
				   transmitted by this MAC */
	u32 tmca;		/* Total number of frames that were transmitted
				   succesfully with the group address bit set
				   that are not broadcast frames */
	u32 tbca;		/* Total number of frames transmitted
				   succesfully that had destination address
				   field equal to the broadcast address */
	u32 rxfok;		/* Total number of frames received OK */
	u32 rxbok;		/* Total number of octets received OK */
	u32 rbyt;		/* Total number of octets received including
				   octets in bad frames. Must be implemented in
				   HW because it includes octets in frames that
				   never even reach the UCC */
	u32 rmca;		/* Total number of frames that were received
				   succesfully with the group address bit set
				   that are not broadcast frames */
	u32 rbca;		/* Total number of frames received succesfully
				   that had destination address equal to the
				   broadcast address */
} __attribute__ ((packed));

/* UCC GETH Tx errors returned via TxConf callback */
#define TX_ERRORS_DEF      0x0200
#define TX_ERRORS_EXDEF    0x0100
#define TX_ERRORS_LC       0x0080
#define TX_ERRORS_RL       0x0040
#define TX_ERRORS_RC_MASK  0x003C
#define TX_ERRORS_RC_SHIFT 2
#define TX_ERRORS_UN       0x0002
#define TX_ERRORS_CSL      0x0001

/* UCC GETH Rx errors returned via RxStore callback */
#define RX_ERRORS_CMR      0x0200
#define RX_ERRORS_M        0x0100
#define RX_ERRORS_BC       0x0080
#define RX_ERRORS_MC       0x0040

/* Transmit BD. These are in addition to values defined in uccf. */
#define T_VID      0x003c0000	/* insert VLAN id index mask. */
#define T_DEF      (((u32) TX_ERRORS_DEF     ) << 16)
#define T_EXDEF    (((u32) TX_ERRORS_EXDEF   ) << 16)
#define T_LC       (((u32) TX_ERRORS_LC      ) << 16)
#define T_RL       (((u32) TX_ERRORS_RL      ) << 16)
#define T_RC_MASK  (((u32) TX_ERRORS_RC_MASK ) << 16)
#define T_UN       (((u32) TX_ERRORS_UN      ) << 16)
#define T_CSL      (((u32) TX_ERRORS_CSL     ) << 16)
#define T_ERRORS_REPORT  (T_DEF | T_EXDEF | T_LC | T_RL | T_RC_MASK \
		| T_UN | T_CSL)	/* transmit errors to report */

/* Receive BD. These are in addition to values defined in uccf. */
#define R_LG    0x00200000	/* Frame length violation.  */
#define R_NO    0x00100000	/* Non-octet aligned frame.  */
#define R_SH    0x00080000	/* Short frame.  */
#define R_CR    0x00040000	/* CRC error.  */
#define R_OV    0x00020000	/* Overrun.  */
#define R_IPCH  0x00010000	/* IP checksum check failed. */
#define R_CMR   (((u32) RX_ERRORS_CMR  ) << 16)
#define R_M     (((u32) RX_ERRORS_M    ) << 16)
#define R_BC    (((u32) RX_ERRORS_BC   ) << 16)
#define R_MC    (((u32) RX_ERRORS_MC   ) << 16)
#define R_ERRORS_REPORT (R_CMR | R_M | R_BC | R_MC)	/* receive errors to
							   report */
#define R_ERRORS_FATAL  (R_LG  | R_NO | R_SH | R_CR | \
		R_OV | R_IPCH)	/* receive errors to discard */

/* Alignments */
#define UCC_GETH_RX_GLOBAL_PRAM_ALIGNMENT	256
#define UCC_GETH_TX_GLOBAL_PRAM_ALIGNMENT       128
#define UCC_GETH_THREAD_RX_PRAM_ALIGNMENT       128
#define UCC_GETH_THREAD_TX_PRAM_ALIGNMENT       64
#define UCC_GETH_THREAD_DATA_ALIGNMENT          256	/* spec gives values
							   based on num of
							   threads, but always
							   using the maximum is
							   easier */
#define UCC_GETH_SEND_QUEUE_QUEUE_DESCRIPTOR_ALIGNMENT	32
#define UCC_GETH_SCHEDULER_ALIGNMENT		4	/* This is a guess */
#define UCC_GETH_TX_STATISTICS_ALIGNMENT	4	/* This is a guess */
#define UCC_GETH_RX_STATISTICS_ALIGNMENT	4	/* This is a guess */
#define UCC_GETH_RX_INTERRUPT_COALESCING_ALIGNMENT	64
#define UCC_GETH_RX_BD_QUEUES_ALIGNMENT		8	/* This is a guess */
#define UCC_GETH_RX_PREFETCHED_BDS_ALIGNMENT	128	/* This is a guess */
#define UCC_GETH_RX_EXTENDED_FILTERING_GLOBAL_PARAMETERS_ALIGNMENT 4	/* This
									   is a
									   guess
									 */
#define UCC_GETH_RX_BD_RING_ALIGNMENT		32
#define UCC_GETH_TX_BD_RING_ALIGNMENT		32
#define UCC_GETH_MRBLR_ALIGNMENT		128
#define UCC_GETH_RX_BD_RING_SIZE_ALIGNMENT	4
#define UCC_GETH_TX_BD_RING_SIZE_MEMORY_ALIGNMENT	32
#define UCC_GETH_RX_DATA_BUF_ALIGNMENT		64

#define UCC_GETH_TAD_EF                         0x80
#define UCC_GETH_TAD_V                          0x40
#define UCC_GETH_TAD_REJ                        0x20
#define UCC_GETH_TAD_VTAG_OP_RIGHT_SHIFT        2
#define UCC_GETH_TAD_VTAG_OP_SHIFT              6
#define UCC_GETH_TAD_V_NON_VTAG_OP              0x20
#define UCC_GETH_TAD_RQOS_SHIFT                 0
#define UCC_GETH_TAD_V_PRIORITY_SHIFT           5
#define UCC_GETH_TAD_CFI                        0x10

#define UCC_GETH_VLAN_PRIORITY_MAX              8
#define UCC_GETH_IP_PRIORITY_MAX                64
#define UCC_GETH_TX_VTAG_TABLE_ENTRY_MAX        8
#define UCC_GETH_RX_BD_RING_SIZE_MIN            8
#define UCC_GETH_TX_BD_RING_SIZE_MIN            2
#define UCC_GETH_BD_RING_SIZE_MAX		0xffff

#define UCC_GETH_SIZE_OF_BD                     QE_SIZEOF_BD

/* Driver definitions */
#define TX_BD_RING_LEN                          0x10
#define RX_BD_RING_LEN                          0x10
#define UCC_GETH_DEV_WEIGHT                     TX_BD_RING_LEN

#define TX_RING_MOD_MASK(size)                  (size-1)
#define RX_RING_MOD_MASK(size)                  (size-1)

#define ENET_NUM_OCTETS_PER_ADDRESS             6
#define ENET_GROUP_ADDR                         0x01	/* Group address mask
							   for ethernet
							   addresses */

#define TX_TIMEOUT                              (1*HZ)
#define SKB_ALLOC_TIMEOUT                       100000
#define PHY_INIT_TIMEOUT                        100000
#define PHY_CHANGE_TIME                         2

/* Fast Ethernet (10/100 Mbps) */
#define UCC_GETH_URFS_INIT                      512	/* Rx virtual FIFO size
							 */
#define UCC_GETH_URFET_INIT                     256	/* 1/2 urfs */
#define UCC_GETH_URFSET_INIT                    384	/* 3/4 urfs */
#define UCC_GETH_UTFS_INIT                      512	/* Tx virtual FIFO size
							 */
#define UCC_GETH_UTFET_INIT                     256	/* 1/2 utfs */
#define UCC_GETH_UTFTT_INIT                     128
/* Gigabit Ethernet (1000 Mbps) */
#define UCC_GETH_URFS_GIGA_INIT                 4096/*2048*/	/* Rx virtual
								   FIFO size */
#define UCC_GETH_URFET_GIGA_INIT                2048/*1024*/	/* 1/2 urfs */
#define UCC_GETH_URFSET_GIGA_INIT               3072/*1536*/	/* 3/4 urfs */
#define UCC_GETH_UTFS_GIGA_INIT                 8192/*2048*/	/* Tx virtual
								   FIFO size */
#define UCC_GETH_UTFET_GIGA_INIT                4096/*1024*/	/* 1/2 utfs */
#define UCC_GETH_UTFTT_GIGA_INIT                0x400/*0x40*/	/* */

#define UCC_GETH_REMODER_INIT                   0	/* bits that must be
							   set */
#define UCC_GETH_TEMODER_INIT                   0xC000	/* bits that must */
#define UCC_GETH_UPSMR_INIT                     (UPSMR_RES1)	/* Start value
								   for this
								   register */
#define UCC_GETH_MACCFG1_INIT                   0
#define UCC_GETH_MACCFG2_INIT                   (MACCFG2_RESERVED_1)

/* Ethernet Address Type. */
enum enet_addr_type {
	ENET_ADDR_TYPE_INDIVIDUAL,
	ENET_ADDR_TYPE_GROUP,
	ENET_ADDR_TYPE_BROADCAST
};

/* UCC GETH 82xx Ethernet Address Recognition Location */
enum ucc_geth_enet_address_recognition_location {
	UCC_GETH_ENET_ADDRESS_RECOGNITION_LOCATION_STATION_ADDRESS,/* station
								      address */
	UCC_GETH_ENET_ADDRESS_RECOGNITION_LOCATION_PADDR_FIRST,	/* additional
								   station
								   address
								   paddr1 */
	UCC_GETH_ENET_ADDRESS_RECOGNITION_LOCATION_PADDR2,	/* additional
								   station
								   address
								   paddr2 */
	UCC_GETH_ENET_ADDRESS_RECOGNITION_LOCATION_PADDR3,	/* additional
								   station
								   address
								   paddr3 */
	UCC_GETH_ENET_ADDRESS_RECOGNITION_LOCATION_PADDR_LAST,	/* additional
								   station
								   address
								   paddr4 */
	UCC_GETH_ENET_ADDRESS_RECOGNITION_LOCATION_GROUP_HASH,	/* group hash */
	UCC_GETH_ENET_ADDRESS_RECOGNITION_LOCATION_INDIVIDUAL_HASH /* individual
								      hash */
};

/* UCC GETH vlan operation tagged */
enum ucc_geth_vlan_operation_tagged {
	UCC_GETH_VLAN_OPERATION_TAGGED_NOP = 0x0,	/* Tagged - nop */
	UCC_GETH_VLAN_OPERATION_TAGGED_REPLACE_VID_PORTION_OF_Q_TAG
		= 0x1,	/* Tagged - replace vid portion of q tag */
	UCC_GETH_VLAN_OPERATION_TAGGED_IF_VID0_REPLACE_VID_WITH_DEFAULT_VALUE
		= 0x2,	/* Tagged - if vid0 replace vid with default value  */
	UCC_GETH_VLAN_OPERATION_TAGGED_EXTRACT_Q_TAG_FROM_FRAME
		= 0x3	/* Tagged - extract q tag from frame */
};

/* UCC GETH vlan operation non-tagged */
enum ucc_geth_vlan_operation_non_tagged {
	UCC_GETH_VLAN_OPERATION_NON_TAGGED_NOP = 0x0,	/* Non tagged - nop */
	UCC_GETH_VLAN_OPERATION_NON_TAGGED_Q_TAG_INSERT = 0x1	/* Non tagged -
								   q tag insert
								 */
};

/* UCC GETH Rx Quality of Service Mode */
enum ucc_geth_qos_mode {
	UCC_GETH_QOS_MODE_DEFAULT = 0x0,	/* default queue */
	UCC_GETH_QOS_MODE_QUEUE_NUM_FROM_L2_CRITERIA = 0x1,	/* queue
								   determined
								   by L2
								   criteria */
	UCC_GETH_QOS_MODE_QUEUE_NUM_FROM_L3_CRITERIA = 0x2	/* queue
								   determined
								   by L3
								   criteria */
};

/* UCC GETH Statistics Gathering Mode - These are bit flags, 'or' them together
   for combined functionality */
enum ucc_geth_statistics_gathering_mode {
	UCC_GETH_STATISTICS_GATHERING_MODE_NONE = 0x00000000,	/* No
								   statistics
								   gathering */
	UCC_GETH_STATISTICS_GATHERING_MODE_HARDWARE = 0x00000001,/* Enable
								    hardware
								    statistics
								    gathering
								  */
	UCC_GETH_STATISTICS_GATHERING_MODE_FIRMWARE_TX = 0x00000004,/*Enable
								      firmware
								      tx
								      statistics
								      gathering
								     */
	UCC_GETH_STATISTICS_GATHERING_MODE_FIRMWARE_RX = 0x00000008/* Enable
								      firmware
								      rx
								      statistics
								      gathering
								    */
};

/* UCC GETH Pad and CRC Mode - Note, Padding without CRC is not possible */
enum ucc_geth_maccfg2_pad_and_crc_mode {
	UCC_GETH_PAD_AND_CRC_MODE_NONE
		= MACCFG2_PAD_AND_CRC_MODE_NONE,	/* Neither Padding
							   short frames
							   nor CRC */
	UCC_GETH_PAD_AND_CRC_MODE_CRC_ONLY
		= MACCFG2_PAD_AND_CRC_MODE_CRC_ONLY,	/* Append
							   CRC only */
	UCC_GETH_PAD_AND_CRC_MODE_PAD_AND_CRC =
	    MACCFG2_PAD_AND_CRC_MODE_PAD_AND_CRC
};

/* UCC GETH upsmr Flow Control Mode */
enum ucc_geth_flow_control_mode {
	UPSMR_AUTOMATIC_FLOW_CONTROL_MODE_NONE = 0x00000000,	/* No automatic
								   flow control
								 */
	UPSMR_AUTOMATIC_FLOW_CONTROL_MODE_PAUSE_WHEN_EMERGENCY
		= 0x00004000	/* Send pause frame when RxFIFO reaches its
				   emergency threshold */
};

/* UCC GETH number of threads */
enum ucc_geth_num_of_threads {
	UCC_GETH_NUM_OF_THREADS_1 = 0x1,	/* 1 */
	UCC_GETH_NUM_OF_THREADS_2 = 0x2,	/* 2 */
	UCC_GETH_NUM_OF_THREADS_4 = 0x0,	/* 4 */
	UCC_GETH_NUM_OF_THREADS_6 = 0x3,	/* 6 */
	UCC_GETH_NUM_OF_THREADS_8 = 0x4	/* 8 */
};

/* UCC GETH number of station addresses */
enum ucc_geth_num_of_station_addresses {
	UCC_GETH_NUM_OF_STATION_ADDRESSES_1,	/* 1 */
	UCC_GETH_NUM_OF_STATION_ADDRESSES_5	/* 5 */
};

/* UCC GETH 82xx Ethernet Address Container */
struct enet_addr_container {
	u8 address[ENET_NUM_OCTETS_PER_ADDRESS];	/* ethernet address */
	enum ucc_geth_enet_address_recognition_location location;	/* location in
								   82xx address
								   recognition
								   hardware */
	struct list_head node;
};

#define ENET_ADDR_CONT_ENTRY(ptr) list_entry(ptr, struct enet_addr_container, node)

/* UCC GETH Termination Action Descriptor (TAD) structure. */
struct ucc_geth_tad_params {
	int rx_non_dynamic_extended_features_mode;
	int reject_frame;
	enum ucc_geth_vlan_operation_tagged vtag_op;
	enum ucc_geth_vlan_operation_non_tagged vnontag_op;
	enum ucc_geth_qos_mode rqos;
	u8 vpri;
	u16 vid;
};

/* GETH protocol initialization structure */
struct ucc_geth_info {
	struct ucc_fast_info uf_info;
	u8 numQueuesTx;
	u8 numQueuesRx;
	int ipCheckSumCheck;
	int ipCheckSumGenerate;
	int rxExtendedFiltering;
	u32 extendedFilteringChainPointer;
	u16 typeorlen;
	int dynamicMaxFrameLength;
	int dynamicMinFrameLength;
	u8 nonBackToBackIfgPart1;
	u8 nonBackToBackIfgPart2;
	u8 miminumInterFrameGapEnforcement;
	u8 backToBackInterFrameGap;
	int ipAddressAlignment;
	int lengthCheckRx;
	u32 mblinterval;
	u16 nortsrbytetime;
	u8 fracsiz;
	u8 strictpriorityq;
	u8 txasap;
	u8 extrabw;
	int miiPreambleSupress;
	u8 altBebTruncation;
	int altBeb;
	int backPressureNoBackoff;
	int noBackoff;
	int excessDefer;
	u8 maxRetransmission;
	u8 collisionWindow;
	int pro;
	int cap;
	int rsh;
	int rlpb;
	int cam;
	int bro;
	int ecm;
	int receiveFlowControl;
	int transmitFlowControl;
	u8 maxGroupAddrInHash;
	u8 maxIndAddrInHash;
	u8 prel;
	u16 maxFrameLength;
	u16 minFrameLength;
	u16 maxD1Length;
	u16 maxD2Length;
	u16 vlantype;
	u16 vlantci;
	u32 ecamptr;
	u32 eventRegMask;
	u16 pausePeriod;
	u16 extensionField;
	u8 phy_address;
	u32 mdio_bus;
	u8 weightfactor[NUM_TX_QUEUES];
	u8 interruptcoalescingmaxvalue[NUM_RX_QUEUES];
	u8 l2qt[UCC_GETH_VLAN_PRIORITY_MAX];
	u8 l3qt[UCC_GETH_IP_PRIORITY_MAX];
	u32 vtagtable[UCC_GETH_TX_VTAG_TABLE_ENTRY_MAX];
	u8 iphoffset[TX_IP_OFFSET_ENTRY_MAX];
	u16 bdRingLenTx[NUM_TX_QUEUES];
	u16 bdRingLenRx[NUM_RX_QUEUES];
	enum ucc_geth_num_of_station_addresses numStationAddresses;
	enum qe_fltr_largest_external_tbl_lookup_key_size
	    largestexternallookupkeysize;
	enum ucc_geth_statistics_gathering_mode statisticsMode;
	enum ucc_geth_vlan_operation_tagged vlanOperationTagged;
	enum ucc_geth_vlan_operation_non_tagged vlanOperationNonTagged;
	enum ucc_geth_qos_mode rxQoSMode;
	enum ucc_geth_flow_control_mode aufc;
	enum ucc_geth_maccfg2_pad_and_crc_mode padAndCrc;
	enum ucc_geth_num_of_threads numThreadsTx;
	enum ucc_geth_num_of_threads numThreadsRx;
	enum qe_risc_allocation riscTx;
	enum qe_risc_allocation riscRx;
};

/* structure representing UCC GETH */
struct ucc_geth_private {
	struct ucc_geth_info *ug_info;
	struct ucc_fast_private *uccf;
	struct net_device *dev;
	struct net_device_stats stats;	/* linux network statistics */
	struct ucc_geth *ug_regs;
	struct ucc_geth_init_pram *p_init_enet_param_shadow;
	struct ucc_geth_exf_global_pram *p_exf_glbl_param;
	u32 exf_glbl_param_offset;
	struct ucc_geth_rx_global_pram *p_rx_glbl_pram;
	u32 rx_glbl_pram_offset;
	struct ucc_geth_tx_global_pram *p_tx_glbl_pram;
	u32 tx_glbl_pram_offset;
	struct ucc_geth_send_queue_mem_region *p_send_q_mem_reg;
	u32 send_q_mem_reg_offset;
	struct ucc_geth_thread_data_tx *p_thread_data_tx;
	u32 thread_dat_tx_offset;
	struct ucc_geth_thread_data_rx *p_thread_data_rx;
	u32 thread_dat_rx_offset;
	struct ucc_geth_scheduler *p_scheduler;
	u32 scheduler_offset;
	struct ucc_geth_tx_firmware_statistics_pram *p_tx_fw_statistics_pram;
	u32 tx_fw_statistics_pram_offset;
	struct ucc_geth_rx_firmware_statistics_pram *p_rx_fw_statistics_pram;
	u32 rx_fw_statistics_pram_offset;
	struct ucc_geth_rx_interrupt_coalescing_table *p_rx_irq_coalescing_tbl;
	u32 rx_irq_coalescing_tbl_offset;
	struct ucc_geth_rx_bd_queues_entry *p_rx_bd_qs_tbl;
	u32 rx_bd_qs_tbl_offset;
	u8 *p_tx_bd_ring[NUM_TX_QUEUES];
	u32 tx_bd_ring_offset[NUM_TX_QUEUES];
	u8 *p_rx_bd_ring[NUM_RX_QUEUES];
	u32 rx_bd_ring_offset[NUM_RX_QUEUES];
	u8 *confBd[NUM_TX_QUEUES];
	u8 *txBd[NUM_TX_QUEUES];
	u8 *rxBd[NUM_RX_QUEUES];
	int badFrame[NUM_RX_QUEUES];
	u16 cpucount[NUM_TX_QUEUES];
	volatile u16 *p_cpucount[NUM_TX_QUEUES];
	int indAddrRegUsed[NUM_OF_PADDRS];
	u8 paddr[NUM_OF_PADDRS][ENET_NUM_OCTETS_PER_ADDRESS];	/* ethernet address */
	u8 numGroupAddrInHash;
	u8 numIndAddrInHash;
	u8 numIndAddrInReg;
	int rx_extended_features;
	int rx_non_dynamic_extended_features;
	struct list_head conf_skbs;
	struct list_head group_hash_q;
	struct list_head ind_hash_q;
	u32 saved_uccm;
	spinlock_t lock;
	/* pointers to arrays of skbuffs for tx and rx */
	struct sk_buff **tx_skbuff[NUM_TX_QUEUES];
	struct sk_buff **rx_skbuff[NUM_RX_QUEUES];
	/* indices pointing to the next free sbk in skb arrays */
	u16 skb_curtx[NUM_TX_QUEUES];
	u16 skb_currx[NUM_RX_QUEUES];
	/* index of the first skb which hasn't been transmitted yet. */
	u16 skb_dirtytx[NUM_TX_QUEUES];

	struct ugeth_mii_info *mii_info;
	struct phy_device *phydev;
	phy_interface_t phy_interface;
	int max_speed;
	uint32_t msg_enable;
	int oldspeed;
	int oldduplex;
	int oldlink;
};

#endif				/* __UCC_GETH_H__ */
