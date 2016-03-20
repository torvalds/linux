/**********************************************************************
* Author: Cavium, Inc.
*
* Contact: support@cavium.com
*          Please include "LiquidIO" in the subject.
*
* Copyright (c) 2003-2015 Cavium, Inc.
*
* This file is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, Version 2, as
* published by the Free Software Foundation.
*
* This file is distributed in the hope that it will be useful, but
* AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
* of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
* NONINFRINGEMENT.  See the GNU General Public License for more
* details.
*
* This file may also be available under a different license from Cavium.
* Contact Cavium, Inc. for more information
**********************************************************************/

/*!  \file  liquidio_common.h
 *   \brief Common: Structures and macros used in PCI-NIC package by core and
 *   host driver.
 */

#ifndef __LIQUIDIO_COMMON_H__
#define __LIQUIDIO_COMMON_H__

#include "octeon_config.h"

#define LIQUIDIO_VERSION        "1.1.9"
#define LIQUIDIO_MAJOR_VERSION  1
#define LIQUIDIO_MINOR_VERSION  1
#define LIQUIDIO_MICRO_VERSION  9

#define CONTROL_IQ 0
/** Tag types used by Octeon cores in its work. */
enum octeon_tag_type {
	ORDERED_TAG = 0,
	ATOMIC_TAG = 1,
	NULL_TAG = 2,
	NULL_NULL_TAG = 3
};

/* pre-defined host->NIC tag values */
#define LIO_CONTROL  (0x11111110)
#define LIO_DATA(i)  (0x11111111 + (i))

/* Opcodes used by host driver/apps to perform operations on the core.
 * These are used to identify the major subsystem that the operation
 * is for.
 */
#define OPCODE_CORE 0           /* used for generic core operations */
#define OPCODE_NIC  1           /* used for NIC operations */
#define OPCODE_LAST OPCODE_NIC

/* Subcodes are used by host driver/apps to identify the sub-operation
 * for the core. They only need to by unique for a given subsystem.
 */
#define OPCODE_SUBCODE(op, sub)       (((op & 0x0f) << 8) | ((sub) & 0x7f))

/** OPCODE_CORE subcodes. For future use. */

/** OPCODE_NIC subcodes */

/* This subcode is sent by core PCI driver to indicate cores are ready. */
#define OPCODE_NIC_CORE_DRV_ACTIVE     0x01
#define OPCODE_NIC_NW_DATA             0x02     /* network packet data */
#define OPCODE_NIC_CMD                 0x03
#define OPCODE_NIC_INFO                0x04
#define OPCODE_NIC_PORT_STATS          0x05
#define OPCODE_NIC_MDIO45              0x06
#define OPCODE_NIC_TIMESTAMP           0x07
#define OPCODE_NIC_INTRMOD_CFG         0x08
#define OPCODE_NIC_IF_CFG              0x09

#define CORE_DRV_TEST_SCATTER_OP    0xFFF5

#define OPCODE_SLOW_PATH(rh)  \
	(OPCODE_SUBCODE(rh->r.opcode, rh->r.subcode) != \
		OPCODE_SUBCODE(OPCODE_NIC, OPCODE_NIC_NW_DATA))

/* Application codes advertised by the core driver initialization packet. */
#define CVM_DRV_APP_START           0x0
#define CVM_DRV_NO_APP              0
#define CVM_DRV_APP_COUNT           0x2
#define CVM_DRV_BASE_APP            (CVM_DRV_APP_START + 0x0)
#define CVM_DRV_NIC_APP             (CVM_DRV_APP_START + 0x1)
#define CVM_DRV_INVALID_APP         (CVM_DRV_APP_START + 0x2)
#define CVM_DRV_APP_END             (CVM_DRV_INVALID_APP - 1)

/* Macro to increment index.
 * Index is incremented by count; if the sum exceeds
 * max, index is wrapped-around to the start.
 */
#define INCR_INDEX(index, count, max)                \
do {                                                 \
	if (((index) + (count)) >= (max))            \
		index = ((index) + (count)) - (max); \
	else                                         \
		index += (count);                    \
} while (0)

#define INCR_INDEX_BY1(index, max)	\
do {                                    \
	if ((++(index)) == (max))       \
		index = 0;	        \
} while (0)

#define DECR_INDEX(index, count, max)                  \
do {						       \
	if ((count) > (index))                         \
		index = ((max) - ((count - index)));   \
	else                                           \
		index -= count;			       \
} while (0)

#define OCT_BOARD_NAME 32
#define OCT_SERIAL_LEN 64

/* Structure used by core driver to send indication that the Octeon
 * application is ready.
 */
struct octeon_core_setup {
	u64 corefreq;

	char boardname[OCT_BOARD_NAME];

	char board_serial_number[OCT_SERIAL_LEN];

	u64 board_rev_major;

	u64 board_rev_minor;

};

/*---------------------------  SCATTER GATHER ENTRY  -----------------------*/

/* The Scatter-Gather List Entry. The scatter or gather component used with
 * a Octeon input instruction has this format.
 */
struct octeon_sg_entry {
	/** The first 64 bit gives the size of data in each dptr.*/
	union {
		u16 size[4];
		u64 size64;
	} u;

	/** The 4 dptr pointers for this entry. */
	u64 ptr[4];

};

#define OCT_SG_ENTRY_SIZE    (sizeof(struct octeon_sg_entry))

/* \brief Add size to gather list
 * @param sg_entry scatter/gather entry
 * @param size size to add
 * @param pos position to add it.
 */
static inline void add_sg_size(struct octeon_sg_entry *sg_entry,
			       u16 size,
			       u32 pos)
{
#ifdef __BIG_ENDIAN_BITFIELD
	sg_entry->u.size[pos] = size;
#else
	sg_entry->u.size[3 - pos] = size;
#endif
}

/*------------------------- End Scatter/Gather ---------------------------*/

#define   OCTNET_FRM_PTP_HEADER_SIZE  8
#define   OCTNET_FRM_HEADER_SIZE     30 /* PTP timestamp + VLAN + Ethernet */

#define   OCTNET_MIN_FRM_SIZE        (64  + OCTNET_FRM_PTP_HEADER_SIZE)
#define   OCTNET_MAX_FRM_SIZE        (16000 + OCTNET_FRM_HEADER_SIZE)

#define   OCTNET_DEFAULT_FRM_SIZE    (1500 + OCTNET_FRM_HEADER_SIZE)

/** NIC Commands are sent using this Octeon Input Queue */
#define   OCTNET_CMD_Q                0

/* NIC Command types */
#define   OCTNET_CMD_CHANGE_MTU       0x1
#define   OCTNET_CMD_CHANGE_MACADDR   0x2
#define   OCTNET_CMD_CHANGE_DEVFLAGS  0x3
#define   OCTNET_CMD_RX_CTL           0x4

#define	  OCTNET_CMD_SET_MULTI_LIST   0x5
#define   OCTNET_CMD_CLEAR_STATS      0x6

/* command for setting the speed, duplex & autoneg */
#define   OCTNET_CMD_SET_SETTINGS     0x7
#define   OCTNET_CMD_SET_FLOW_CTL     0x8

#define   OCTNET_CMD_MDIO_READ_WRITE  0x9
#define   OCTNET_CMD_GPIO_ACCESS      0xA
#define   OCTNET_CMD_LRO_ENABLE       0xB
#define   OCTNET_CMD_LRO_DISABLE      0xC
#define   OCTNET_CMD_SET_RSS          0xD
#define   OCTNET_CMD_WRITE_SA         0xE
#define   OCTNET_CMD_DELETE_SA        0xF
#define   OCTNET_CMD_UPDATE_SA        0x12

#define   OCTNET_CMD_TNL_RX_CSUM_CTL 0x10
#define   OCTNET_CMD_TNL_TX_CSUM_CTL 0x11
#define   OCTNET_CMD_IPSECV2_AH_ESP_CTL 0x13
#define   OCTNET_CMD_VERBOSE_ENABLE   0x14
#define   OCTNET_CMD_VERBOSE_DISABLE  0x15

/* RX(packets coming from wire) Checksum verification flags */
/* TCP/UDP csum */
#define   CNNIC_L4SUM_VERIFIED             0x1
#define   CNNIC_IPSUM_VERIFIED             0x2
#define   CNNIC_TUN_CSUM_VERIFIED          0x4
#define   CNNIC_CSUM_VERIFIED (CNNIC_IPSUM_VERIFIED | CNNIC_L4SUM_VERIFIED)

/*LROIPV4 and LROIPV6 Flags*/
#define   OCTNIC_LROIPV4    0x1
#define   OCTNIC_LROIPV6    0x2

/* Interface flags communicated between host driver and core app. */
enum octnet_ifflags {
	OCTNET_IFFLAG_PROMISC   = 0x01,
	OCTNET_IFFLAG_ALLMULTI  = 0x02,
	OCTNET_IFFLAG_MULTICAST = 0x04,
	OCTNET_IFFLAG_BROADCAST = 0x08,
	OCTNET_IFFLAG_UNICAST   = 0x10
};

/*   wqe
 *  ---------------  0
 * |  wqe  word0-3 |
 *  ---------------  32
 * |    PCI IH     |
 *  ---------------  40
 * |     RPTR      |
 *  ---------------  48
 * |    PCI IRH    |
 *  ---------------  56
 * |  OCT_NET_CMD  |
 *  ---------------  64
 * | Addtl 8-BData |
 * |               |
 *  ---------------
 */

union octnet_cmd {
	u64 u64;

	struct {
#ifdef __BIG_ENDIAN_BITFIELD
		u64 cmd:5;

		u64 more:6; /* How many udd words follow the command */

		u64 param1:29;

		u64 param2:16;

		u64 param3:8;

#else

		u64 param3:8;

		u64 param2:16;

		u64 param1:29;

		u64 more:6;

		u64 cmd:5;

#endif
	} s;

};

#define   OCTNET_CMD_SIZE     (sizeof(union octnet_cmd))

/** Instruction Header */
struct octeon_instr_ih {
#ifdef __BIG_ENDIAN_BITFIELD
	/** Raw mode indicator 1 = RAW */
	u64 raw:1;

	/** Gather indicator 1=gather*/
	u64 gather:1;

	/** Data length OR no. of entries in gather list */
	u64 dlengsz:14;

	/** Front Data size */
	u64 fsz:6;

	/** Packet Order / Work Unit selection (1 of 8)*/
	u64 qos:3;

	/** Core group selection (1 of 16) */
	u64 grp:4;

	/** Short Raw Packet Indicator 1=short raw pkt */
	u64 rs:1;

	/** Tag type */
	u64 tagtype:2;

	/** Tag Value */
	u64 tag:32;
#else
	/** Tag Value */
	u64 tag:32;

	/** Tag type */
	u64 tagtype:2;

	/** Short Raw Packet Indicator 1=short raw pkt */
	u64 rs:1;

	/** Core group selection (1 of 16) */
	u64 grp:4;

	/** Packet Order / Work Unit selection (1 of 8)*/
	u64 qos:3;

	/** Front Data size */
	u64 fsz:6;

	/** Data length OR no. of entries in gather list */
	u64 dlengsz:14;

	/** Gather indicator 1=gather*/
	u64 gather:1;

	/** Raw mode indicator 1 = RAW */
	u64 raw:1;
#endif
};

/** Input Request Header */
struct octeon_instr_irh {
#ifdef __BIG_ENDIAN_BITFIELD
	u64 opcode:4;
	u64 rflag:1;
	u64 subcode:7;
	u64 len:3;
	u64 rid:13;
	u64 reserved:4;
	u64 ossp:32;             /* opcode/subcode specific parameters */
#else
	u64 ossp:32;             /* opcode/subcode specific parameters */
	u64 reserved:4;
	u64 rid:13;
	u64 len:3;
	u64 subcode:7;
	u64 rflag:1;
	u64 opcode:4;
#endif
};

/** Return Data Parameters */
struct octeon_instr_rdp {
#ifdef __BIG_ENDIAN_BITFIELD
	u64 reserved:49;
	u64 pcie_port:3;
	u64 rlen:12;
#else
	u64 rlen:12;
	u64 pcie_port:3;
	u64 reserved:49;
#endif
};

/** Receive Header */
union octeon_rh {
#ifdef __BIG_ENDIAN_BITFIELD
	u64 u64;
	struct {
		u64 opcode:4;
		u64 subcode:8;
		u64 len:3;       /** additional 64-bit words */
		u64 rid:13;      /** request id in response to pkt sent by host */
		u64 reserved:4;
		u64 ossp:32;     /** opcode/subcode specific parameters */
	} r;
	struct {
		u64 opcode:4;
		u64 subcode:8;
		u64 len:3;       /** additional 64-bit words */
		u64 rid:13;      /** request id in response to pkt sent by host */
		u64 extra:24;
		u64 link:8;
		u64 csum_verified:3;     /** checksum verified. */
		u64 has_hwtstamp:1;      /** Has hardware timestamp. 1 = yes. */
	} r_dh;
	struct {
		u64 opcode:4;
		u64 subcode:8;
		u64 len:3;       /** additional 64-bit words */
		u64 rid:13;      /** request id in response to pkt sent by host */
		u64 num_gmx_ports:8;
		u64 max_nic_ports:8;
		u64 app_cap_flags:4;
		u64 app_mode:16;
	} r_core_drv_init;
	struct {
		u64 opcode:4;
		u64 subcode:8;
		u64 len:3;       /** additional 64-bit words */
		u64 rid:13;
		u64 reserved:4;
		u64 extra:25;
		u64 ifidx:7;
	} r_nic_info;
#else
	u64 u64;
	struct {
		u64 ossp:32;  /** opcode/subcode specific parameters */
		u64 reserved:4;
		u64 rid:13;   /** req id in response to pkt sent by host */
		u64 len:3;    /** additional 64-bit words */
		u64 subcode:8;
		u64 opcode:4;
	} r;
	struct {
		u64 has_hwtstamp:1;      /** 1 = has hwtstamp */
		u64 csum_verified:3;     /** checksum verified. */
		u64 link:8;
		u64 extra:24;
		u64 rid:13;   /** req id in response to pkt sent by host */
		u64 len:3;    /** additional 64-bit words */
		u64 subcode:8;
		u64 opcode:4;
	} r_dh;
	struct {
		u64 app_mode:16;
		u64 app_cap_flags:4;
		u64 max_nic_ports:8;
		u64 num_gmx_ports:8;
		u64 rid:13;
		u64 len:3;       /** additional 64-bit words */
		u64 subcode:8;
		u64 opcode:4;
	} r_core_drv_init;
	struct {
		u64 ifidx:7;
		u64 extra:25;
		u64 reserved:4;
		u64 rid:13;
		u64 len:3;       /** additional 64-bit words */
		u64 subcode:8;
		u64 opcode:4;
	} r_nic_info;
#endif
};

#define  OCT_RH_SIZE   (sizeof(union  octeon_rh))

#define OCT_PKT_PARAM_IPV4OPTS   1
#define OCT_PKT_PARAM_IPV6EXTHDR 2

union octnic_packet_params {
	u32 u32;
	struct {
#ifdef __BIG_ENDIAN_BITFIELD
		u32 reserved:6;
		u32 tnl_csum:1;
		u32 ip_csum:1;
		u32 ipv4opts_ipv6exthdr:2;
		u32 ipsec_ops:4;
		u32 tsflag:1;
		u32 csoffset:9;
		u32 ifidx:8;
#else
		u32 ifidx:8;
		u32 csoffset:9;
		u32 tsflag:1;
		u32 ipsec_ops:4;
		u32 ipv4opts_ipv6exthdr:2;
		u32 ip_csum:1;
		u32 tnl_csum:1;
		u32 reserved:6;
#endif
	} s;
};

/** Status of a RGMII Link on Octeon as seen by core driver. */
union oct_link_status {
	u64 u64;

	struct {
#ifdef __BIG_ENDIAN_BITFIELD
		u64 duplex:8;
		u64 status:8;
		u64 mtu:16;
		u64 speed:16;
		u64 autoneg:1;
		u64 interface:4;
		u64 pause:1;
		u64 reserved:10;
#else
		u64 reserved:10;
		u64 pause:1;
		u64 interface:4;
		u64 autoneg:1;
		u64 speed:16;
		u64 mtu:16;
		u64 status:8;
		u64 duplex:8;
#endif
	} s;
};

/** Information for a OCTEON ethernet interface shared between core & host. */
struct oct_link_info {
	union oct_link_status link;
	u64 hw_addr;

#ifdef __BIG_ENDIAN_BITFIELD
	u16 gmxport;
	u8 rsvd[3];
	u8 num_txpciq;
	u8 num_rxpciq;
	u8 ifidx;
#else
	u8 ifidx;
	u8 num_rxpciq;
	u8 num_txpciq;
	u8 rsvd[3];
	u16 gmxport;
#endif

	u8 txpciq[MAX_IOQS_PER_NICIF];
	u8 rxpciq[MAX_IOQS_PER_NICIF];
};

#define OCT_LINK_INFO_SIZE   (sizeof(struct oct_link_info))

struct liquidio_if_cfg_info {
	u64 ifidx;
	u64 iqmask; /** mask for IQs enabled for  the port */
	u64 oqmask; /** mask for OQs enabled for the port */
	struct oct_link_info linfo; /** initial link information */
};

/** Stats for each NIC port in RX direction. */
struct nic_rx_stats {
	/* link-level stats */
	u64 total_rcvd;
	u64 bytes_rcvd;
	u64 total_bcst;
	u64 total_mcst;
	u64 runts;
	u64 ctl_rcvd;
	u64 fifo_err;      /* Accounts for over/under-run of buffers */
	u64 dmac_drop;
	u64 fcs_err;
	u64 jabber_err;
	u64 l2_err;
	u64 frame_err;

	/* firmware stats */
	u64 fw_total_rcvd;
	u64 fw_total_fwd;
	u64 fw_err_pko;
	u64 fw_err_link;
	u64 fw_err_drop;
	u64 fw_lro_pkts;   /* Number of packets that are LROed      */
	u64 fw_lro_octs;   /* Number of octets that are LROed       */
	u64 fw_total_lro;  /* Number of LRO packets formed          */
	u64 fw_lro_aborts; /* Number of times lRO of packet aborted */
	/* intrmod: packet forward rate */
	u64 fwd_rate;
};

/** Stats for each NIC port in RX direction. */
struct nic_tx_stats {
	/* link-level stats */
	u64 total_pkts_sent;
	u64 total_bytes_sent;
	u64 mcast_pkts_sent;
	u64 bcast_pkts_sent;
	u64 ctl_sent;
	u64 one_collision_sent;   /* Packets sent after one collision*/
	u64 multi_collision_sent; /* Packets sent after multiple collision*/
	u64 max_collision_fail;   /* Packets not sent due to max collisions */
	u64 max_deferral_fail;   /* Packets not sent due to max deferrals */
	u64 fifo_err;       /* Accounts for over/under-run of buffers */
	u64 runts;
	u64 total_collisions; /* Total number of collisions detected */

	/* firmware stats */
	u64 fw_total_sent;
	u64 fw_total_fwd;
	u64 fw_err_pko;
	u64 fw_err_link;
	u64 fw_err_drop;
};

struct oct_link_stats {
	struct nic_rx_stats fromwire;
	struct nic_tx_stats fromhost;

};

#define LIO68XX_LED_CTRL_ADDR     0x3501
#define LIO68XX_LED_CTRL_CFGON    0x1f
#define LIO68XX_LED_CTRL_CFGOFF   0x100
#define LIO68XX_LED_BEACON_ADDR   0x3508
#define LIO68XX_LED_BEACON_CFGON  0x47fd
#define LIO68XX_LED_BEACON_CFGOFF 0x11fc
#define VITESSE_PHY_GPIO_DRIVEON  0x1
#define VITESSE_PHY_GPIO_CFG      0x8
#define VITESSE_PHY_GPIO_DRIVEOFF 0x4
#define VITESSE_PHY_GPIO_HIGH     0x2
#define VITESSE_PHY_GPIO_LOW      0x3

struct oct_mdio_cmd {
	u64 op;
	u64 mdio_addr;
	u64 value1;
	u64 value2;
	u64 value3;
};

#define OCT_LINK_STATS_SIZE   (sizeof(struct oct_link_stats))

#define LIO_INTRMOD_CHECK_INTERVAL  1
#define LIO_INTRMOD_MAXPKT_RATETHR  196608 /* max pkt rate threshold */
#define LIO_INTRMOD_MINPKT_RATETHR  9216   /* min pkt rate threshold */
#define LIO_INTRMOD_MAXCNT_TRIGGER  384    /* max pkts to trigger interrupt */
#define LIO_INTRMOD_MINCNT_TRIGGER  1      /* min pkts to trigger interrupt */
#define LIO_INTRMOD_MAXTMR_TRIGGER  128    /* max time to trigger interrupt */
#define LIO_INTRMOD_MINTMR_TRIGGER  32     /* min time to trigger interrupt */

struct oct_intrmod_cfg {
	u64 intrmod_enable;
	u64 intrmod_check_intrvl;
	u64 intrmod_maxpkt_ratethr;
	u64 intrmod_minpkt_ratethr;
	u64 intrmod_maxcnt_trigger;
	u64 intrmod_maxtmr_trigger;
	u64 intrmod_mincnt_trigger;
	u64 intrmod_mintmr_trigger;
};

#define BASE_QUEUE_NOT_REQUESTED 65535

union oct_nic_if_cfg {
	u64 u64;
	struct {
#ifdef __BIG_ENDIAN_BITFIELD
		u64 base_queue:16;
		u64 num_iqueues:16;
		u64 num_oqueues:16;
		u64 gmx_port_id:8;
		u64 reserved:8;
#else
		u64 reserved:8;
		u64 gmx_port_id:8;
		u64 num_oqueues:16;
		u64 num_iqueues:16;
		u64 base_queue:16;
#endif
	} s;
};

#endif
