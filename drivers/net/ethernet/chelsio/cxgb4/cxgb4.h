/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2016 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __CXGB4_H__
#define __CXGB4_H__

#include "t4_hw.h"

#include <linux/bitops.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/rhashtable.h>
#include <linux/etherdevice.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/ptp_classify.h>
#include <linux/crash_dump.h>
#include <linux/thermal.h>
#include <asm/io.h>
#include "t4_chip_type.h"
#include "cxgb4_uld.h"
#include "t4fw_api.h"

#define CH_WARN(adap, fmt, ...) dev_warn(adap->pdev_dev, fmt, ## __VA_ARGS__)
extern struct list_head adapter_list;
extern struct list_head uld_list;
extern struct mutex uld_mutex;

/* Suspend an Ethernet Tx queue with fewer available descriptors than this.
 * This is the same as calc_tx_descs() for a TSO packet with
 * nr_frags == MAX_SKB_FRAGS.
 */
#define ETHTXQ_STOP_THRES \
	(1 + DIV_ROUND_UP((3 * MAX_SKB_FRAGS) / 2 + (MAX_SKB_FRAGS & 1), 8))

#define FW_PARAM_DEV(param) \
	(FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_DEV) | \
	 FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_DEV_##param))

#define FW_PARAM_PFVF(param) \
	(FW_PARAMS_MNEM_V(FW_PARAMS_MNEM_PFVF) | \
	 FW_PARAMS_PARAM_X_V(FW_PARAMS_PARAM_PFVF_##param) |  \
	 FW_PARAMS_PARAM_Y_V(0) | \
	 FW_PARAMS_PARAM_Z_V(0))

enum {
	MAX_NPORTS	= 4,     /* max # of ports */
	SERNUM_LEN	= 24,    /* Serial # length */
	EC_LEN		= 16,    /* E/C length */
	ID_LEN		= 16,    /* ID length */
	PN_LEN		= 16,    /* Part Number length */
	MACADDR_LEN	= 12,    /* MAC Address length */
};

enum {
	T4_REGMAP_SIZE = (160 * 1024),
	T5_REGMAP_SIZE = (332 * 1024),
};

enum {
	MEM_EDC0,
	MEM_EDC1,
	MEM_MC,
	MEM_MC0 = MEM_MC,
	MEM_MC1,
	MEM_HMA,
};

enum {
	MEMWIN0_APERTURE = 2048,
	MEMWIN0_BASE     = 0x1b800,
	MEMWIN1_APERTURE = 32768,
	MEMWIN1_BASE     = 0x28000,
	MEMWIN1_BASE_T5  = 0x52000,
	MEMWIN2_APERTURE = 65536,
	MEMWIN2_BASE     = 0x30000,
	MEMWIN2_APERTURE_T5 = 131072,
	MEMWIN2_BASE_T5  = 0x60000,
};

enum dev_master {
	MASTER_CANT,
	MASTER_MAY,
	MASTER_MUST
};

enum dev_state {
	DEV_STATE_UNINIT,
	DEV_STATE_INIT,
	DEV_STATE_ERR
};

enum cc_pause {
	PAUSE_RX      = 1 << 0,
	PAUSE_TX      = 1 << 1,
	PAUSE_AUTONEG = 1 << 2
};

enum cc_fec {
	FEC_AUTO      = 1 << 0,	 /* IEEE 802.3 "automatic" */
	FEC_RS        = 1 << 1,  /* Reed-Solomon */
	FEC_BASER_RS  = 1 << 2   /* BaseR/Reed-Solomon */
};

enum {
	CXGB4_ETHTOOL_FLASH_FW = 1,
	CXGB4_ETHTOOL_FLASH_PHY = 2,
	CXGB4_ETHTOOL_FLASH_BOOT = 3,
	CXGB4_ETHTOOL_FLASH_BOOTCFG = 4
};

enum cxgb4_netdev_tls_ops {
	CXGB4_TLSDEV_OPS  = 1,
	CXGB4_XFRMDEV_OPS
};

struct cxgb4_bootcfg_data {
	__le16 signature;
	__u8 reserved[2];
};

struct cxgb4_pcir_data {
	__le32 signature;	/* Signature. The string "PCIR" */
	__le16 vendor_id;	/* Vendor Identification */
	__le16 device_id;	/* Device Identification */
	__u8 vital_product[2];	/* Pointer to Vital Product Data */
	__u8 length[2];		/* PCIR Data Structure Length */
	__u8 revision;		/* PCIR Data Structure Revision */
	__u8 class_code[3];	/* Class Code */
	__u8 image_length[2];	/* Image Length. Multiple of 512B */
	__u8 code_revision[2];	/* Revision Level of Code/Data */
	__u8 code_type;
	__u8 indicator;
	__u8 reserved[2];
};

/* BIOS boot headers */
struct cxgb4_pci_exp_rom_header {
	__le16 signature;	/* ROM Signature. Should be 0xaa55 */
	__u8 reserved[22];	/* Reserved per processor Architecture data */
	__le16 pcir_offset;	/* Offset to PCI Data Structure */
};

/* Legacy PCI Expansion ROM Header */
struct legacy_pci_rom_hdr {
	__u8 signature[2];	/* ROM Signature. Should be 0xaa55 */
	__u8 size512;		/* Current Image Size in units of 512 bytes */
	__u8 initentry_point[4];
	__u8 cksum;		/* Checksum computed on the entire Image */
	__u8 reserved[16];	/* Reserved */
	__le16 pcir_offset;	/* Offset to PCI Data Struture */
};

#define CXGB4_HDR_CODE1 0x00
#define CXGB4_HDR_CODE2 0x03
#define CXGB4_HDR_INDI 0x80

/* BOOT constants */
enum {
	BOOT_CFG_SIG = 0x4243,
	BOOT_SIZE_INC = 512,
	BOOT_SIGNATURE = 0xaa55,
	BOOT_MIN_SIZE = sizeof(struct cxgb4_pci_exp_rom_header),
	BOOT_MAX_SIZE = 1024 * BOOT_SIZE_INC,
	PCIR_SIGNATURE = 0x52494350
};

struct port_stats {
	u64 tx_octets;            /* total # of octets in good frames */
	u64 tx_frames;            /* all good frames */
	u64 tx_bcast_frames;      /* all broadcast frames */
	u64 tx_mcast_frames;      /* all multicast frames */
	u64 tx_ucast_frames;      /* all unicast frames */
	u64 tx_error_frames;      /* all error frames */

	u64 tx_frames_64;         /* # of Tx frames in a particular range */
	u64 tx_frames_65_127;
	u64 tx_frames_128_255;
	u64 tx_frames_256_511;
	u64 tx_frames_512_1023;
	u64 tx_frames_1024_1518;
	u64 tx_frames_1519_max;

	u64 tx_drop;              /* # of dropped Tx frames */
	u64 tx_pause;             /* # of transmitted pause frames */
	u64 tx_ppp0;              /* # of transmitted PPP prio 0 frames */
	u64 tx_ppp1;              /* # of transmitted PPP prio 1 frames */
	u64 tx_ppp2;              /* # of transmitted PPP prio 2 frames */
	u64 tx_ppp3;              /* # of transmitted PPP prio 3 frames */
	u64 tx_ppp4;              /* # of transmitted PPP prio 4 frames */
	u64 tx_ppp5;              /* # of transmitted PPP prio 5 frames */
	u64 tx_ppp6;              /* # of transmitted PPP prio 6 frames */
	u64 tx_ppp7;              /* # of transmitted PPP prio 7 frames */

	u64 rx_octets;            /* total # of octets in good frames */
	u64 rx_frames;            /* all good frames */
	u64 rx_bcast_frames;      /* all broadcast frames */
	u64 rx_mcast_frames;      /* all multicast frames */
	u64 rx_ucast_frames;      /* all unicast frames */
	u64 rx_too_long;          /* # of frames exceeding MTU */
	u64 rx_jabber;            /* # of jabber frames */
	u64 rx_fcs_err;           /* # of received frames with bad FCS */
	u64 rx_len_err;           /* # of received frames with length error */
	u64 rx_symbol_err;        /* symbol errors */
	u64 rx_runt;              /* # of short frames */

	u64 rx_frames_64;         /* # of Rx frames in a particular range */
	u64 rx_frames_65_127;
	u64 rx_frames_128_255;
	u64 rx_frames_256_511;
	u64 rx_frames_512_1023;
	u64 rx_frames_1024_1518;
	u64 rx_frames_1519_max;

	u64 rx_pause;             /* # of received pause frames */
	u64 rx_ppp0;              /* # of received PPP prio 0 frames */
	u64 rx_ppp1;              /* # of received PPP prio 1 frames */
	u64 rx_ppp2;              /* # of received PPP prio 2 frames */
	u64 rx_ppp3;              /* # of received PPP prio 3 frames */
	u64 rx_ppp4;              /* # of received PPP prio 4 frames */
	u64 rx_ppp5;              /* # of received PPP prio 5 frames */
	u64 rx_ppp6;              /* # of received PPP prio 6 frames */
	u64 rx_ppp7;              /* # of received PPP prio 7 frames */

	u64 rx_ovflow0;           /* drops due to buffer-group 0 overflows */
	u64 rx_ovflow1;           /* drops due to buffer-group 1 overflows */
	u64 rx_ovflow2;           /* drops due to buffer-group 2 overflows */
	u64 rx_ovflow3;           /* drops due to buffer-group 3 overflows */
	u64 rx_trunc0;            /* buffer-group 0 truncated packets */
	u64 rx_trunc1;            /* buffer-group 1 truncated packets */
	u64 rx_trunc2;            /* buffer-group 2 truncated packets */
	u64 rx_trunc3;            /* buffer-group 3 truncated packets */
};

struct lb_port_stats {
	u64 octets;
	u64 frames;
	u64 bcast_frames;
	u64 mcast_frames;
	u64 ucast_frames;
	u64 error_frames;

	u64 frames_64;
	u64 frames_65_127;
	u64 frames_128_255;
	u64 frames_256_511;
	u64 frames_512_1023;
	u64 frames_1024_1518;
	u64 frames_1519_max;

	u64 drop;

	u64 ovflow0;
	u64 ovflow1;
	u64 ovflow2;
	u64 ovflow3;
	u64 trunc0;
	u64 trunc1;
	u64 trunc2;
	u64 trunc3;
};

struct tp_tcp_stats {
	u32 tcp_out_rsts;
	u64 tcp_in_segs;
	u64 tcp_out_segs;
	u64 tcp_retrans_segs;
};

struct tp_usm_stats {
	u32 frames;
	u32 drops;
	u64 octets;
};

struct tp_fcoe_stats {
	u32 frames_ddp;
	u32 frames_drop;
	u64 octets_ddp;
};

struct tp_err_stats {
	u32 mac_in_errs[4];
	u32 hdr_in_errs[4];
	u32 tcp_in_errs[4];
	u32 tnl_cong_drops[4];
	u32 ofld_chan_drops[4];
	u32 tnl_tx_drops[4];
	u32 ofld_vlan_drops[4];
	u32 tcp6_in_errs[4];
	u32 ofld_no_neigh;
	u32 ofld_cong_defer;
};

struct tp_cpl_stats {
	u32 req[4];
	u32 rsp[4];
};

struct tp_rdma_stats {
	u32 rqe_dfr_pkt;
	u32 rqe_dfr_mod;
};

struct sge_params {
	u32 hps;			/* host page size for our PF/VF */
	u32 eq_qpp;			/* egress queues/page for our PF/VF */
	u32 iq_qpp;			/* egress queues/page for our PF/VF */
};

struct tp_params {
	unsigned int tre;            /* log2 of core clocks per TP tick */
	unsigned int la_mask;        /* what events are recorded by TP LA */
	unsigned short tx_modq_map;  /* TX modulation scheduler queue to */
				     /* channel map */

	uint32_t dack_re;            /* DACK timer resolution */
	unsigned short tx_modq[NCHAN];	/* channel to modulation queue map */

	u32 vlan_pri_map;               /* cached TP_VLAN_PRI_MAP */
	u32 filter_mask;
	u32 ingress_config;             /* cached TP_INGRESS_CONFIG */

	/* cached TP_OUT_CONFIG compressed error vector
	 * and passing outer header info for encapsulated packets.
	 */
	int rx_pkt_encap;

	/* TP_VLAN_PRI_MAP Compressed Filter Tuple field offsets.  This is a
	 * subset of the set of fields which may be present in the Compressed
	 * Filter Tuple portion of filters and TCP TCB connections.  The
	 * fields which are present are controlled by the TP_VLAN_PRI_MAP.
	 * Since a variable number of fields may or may not be present, their
	 * shifted field positions within the Compressed Filter Tuple may
	 * vary, or not even be present if the field isn't selected in
	 * TP_VLAN_PRI_MAP.  Since some of these fields are needed in various
	 * places we store their offsets here, or a -1 if the field isn't
	 * present.
	 */
	int fcoe_shift;
	int port_shift;
	int vnic_shift;
	int vlan_shift;
	int tos_shift;
	int protocol_shift;
	int ethertype_shift;
	int macmatch_shift;
	int matchtype_shift;
	int frag_shift;

	u64 hash_filter_mask;
};

struct vpd_params {
	unsigned int cclk;
	u8 ec[EC_LEN + 1];
	u8 sn[SERNUM_LEN + 1];
	u8 id[ID_LEN + 1];
	u8 pn[PN_LEN + 1];
	u8 na[MACADDR_LEN + 1];
};

/* Maximum resources provisioned for a PCI PF.
 */
struct pf_resources {
	unsigned int nvi;		/* N virtual interfaces */
	unsigned int neq;		/* N egress Qs */
	unsigned int nethctrl;		/* N egress ETH or CTRL Qs */
	unsigned int niqflint;		/* N ingress Qs/w free list(s) & intr */
	unsigned int niq;		/* N ingress Qs */
	unsigned int tc;		/* PCI-E traffic class */
	unsigned int pmask;		/* port access rights mask */
	unsigned int nexactf;		/* N exact MPS filters */
	unsigned int r_caps;		/* read capabilities */
	unsigned int wx_caps;		/* write/execute capabilities */
};

struct pci_params {
	unsigned int vpd_cap_addr;
	unsigned char speed;
	unsigned char width;
};

struct devlog_params {
	u32 memtype;                    /* which memory (EDC0, EDC1, MC) */
	u32 start;                      /* start of log in firmware memory */
	u32 size;                       /* size of log */
};

/* Stores chip specific parameters */
struct arch_specific_params {
	u8 nchan;
	u8 pm_stats_cnt;
	u8 cng_ch_bits_log;		/* congestion channel map bits width */
	u16 mps_rplc_size;
	u16 vfcount;
	u32 sge_fl_db;
	u16 mps_tcam_size;
};

struct adapter_params {
	struct sge_params sge;
	struct tp_params  tp;
	struct vpd_params vpd;
	struct pf_resources pfres;
	struct pci_params pci;
	struct devlog_params devlog;
	enum pcie_memwin drv_memwin;

	unsigned int cim_la_size;

	unsigned int sf_size;             /* serial flash size in bytes */
	unsigned int sf_nsec;             /* # of flash sectors */

	unsigned int fw_vers;		  /* firmware version */
	unsigned int bs_vers;		  /* bootstrap version */
	unsigned int tp_vers;		  /* TP microcode version */
	unsigned int er_vers;		  /* expansion ROM version */
	unsigned int scfg_vers;		  /* Serial Configuration version */
	unsigned int vpd_vers;		  /* VPD Version */
	u8 api_vers[7];

	unsigned short mtus[NMTUS];
	unsigned short a_wnd[NCCTRL_WIN];
	unsigned short b_wnd[NCCTRL_WIN];

	unsigned char nports;             /* # of ethernet ports */
	unsigned char portvec;
	enum chip_type chip;               /* chip code */
	struct arch_specific_params arch;  /* chip specific params */
	unsigned char offload;
	unsigned char crypto;		/* HW capability for crypto */
	unsigned char ethofld;		/* QoS support */

	unsigned char bypass;
	unsigned char hash_filter;

	unsigned int ofldq_wr_cred;
	bool ulptx_memwrite_dsgl;          /* use of T5 DSGL allowed */

	unsigned int nsched_cls;          /* number of traffic classes */
	unsigned int max_ordird_qp;       /* Max read depth per RDMA QP */
	unsigned int max_ird_adapter;     /* Max read depth per adapter */
	bool fr_nsmr_tpte_wr_support;	  /* FW support for FR_NSMR_TPTE_WR */
	u8 fw_caps_support;		/* 32-bit Port Capabilities */
	bool filter2_wr_support;	/* FW support for FILTER2_WR */
	unsigned int viid_smt_extn_support:1; /* FW returns vin and smt index */

	/* MPS Buffer Group Map[per Port].  Bit i is set if buffer group i is
	 * used by the Port
	 */
	u8 mps_bg_map[MAX_NPORTS];	/* MPS Buffer Group Map */
	bool write_w_imm_support;       /* FW supports WRITE_WITH_IMMEDIATE */
	bool write_cmpl_support;        /* FW supports WRITE_CMPL */
};

/* State needed to monitor the forward progress of SGE Ingress DMA activities
 * and possible hangs.
 */
struct sge_idma_monitor_state {
	unsigned int idma_1s_thresh;	/* 1s threshold in Core Clock ticks */
	unsigned int idma_stalled[2];	/* synthesized stalled timers in HZ */
	unsigned int idma_state[2];	/* IDMA Hang detect state */
	unsigned int idma_qid[2];	/* IDMA Hung Ingress Queue ID */
	unsigned int idma_warn[2];	/* time to warning in HZ */
};

/* Firmware Mailbox Command/Reply log.  All values are in Host-Endian format.
 * The access and execute times are signed in order to accommodate negative
 * error returns.
 */
struct mbox_cmd {
	u64 cmd[MBOX_LEN / 8];		/* a Firmware Mailbox Command/Reply */
	u64 timestamp;			/* OS-dependent timestamp */
	u32 seqno;			/* sequence number */
	s16 access;			/* time (ms) to access mailbox */
	s16 execute;			/* time (ms) to execute */
};

struct mbox_cmd_log {
	unsigned int size;		/* number of entries in the log */
	unsigned int cursor;		/* next position in the log to write */
	u32 seqno;			/* next sequence number */
	/* variable length mailbox command log starts here */
};

/* Given a pointer to a Firmware Mailbox Command Log and a log entry index,
 * return a pointer to the specified entry.
 */
static inline struct mbox_cmd *mbox_cmd_log_entry(struct mbox_cmd_log *log,
						  unsigned int entry_idx)
{
	return &((struct mbox_cmd *)&(log)[1])[entry_idx];
}

#define FW_VERSION(chip) ( \
		FW_HDR_FW_VER_MAJOR_G(chip##FW_VERSION_MAJOR) | \
		FW_HDR_FW_VER_MINOR_G(chip##FW_VERSION_MINOR) | \
		FW_HDR_FW_VER_MICRO_G(chip##FW_VERSION_MICRO) | \
		FW_HDR_FW_VER_BUILD_G(chip##FW_VERSION_BUILD))
#define FW_INTFVER(chip, intf) (FW_HDR_INTFVER_##intf)

struct cxgb4_ethtool_lb_test {
	struct completion completion;
	int result;
	int loopback;
};

struct fw_info {
	u8 chip;
	char *fs_name;
	char *fw_mod_name;
	struct fw_hdr fw_hdr;
};

struct trace_params {
	u32 data[TRACE_LEN / 4];
	u32 mask[TRACE_LEN / 4];
	unsigned short snap_len;
	unsigned short min_len;
	unsigned char skip_ofst;
	unsigned char skip_len;
	unsigned char invert;
	unsigned char port;
};

struct cxgb4_fw_data {
	__be32 signature;
	__u8 reserved[4];
};

/* Firmware Port Capabilities types. */

typedef u16 fw_port_cap16_t;	/* 16-bit Port Capabilities integral value */
typedef u32 fw_port_cap32_t;	/* 32-bit Port Capabilities integral value */

enum fw_caps {
	FW_CAPS_UNKNOWN	= 0,	/* 0'ed out initial state */
	FW_CAPS16	= 1,	/* old Firmware: 16-bit Port Capabilities */
	FW_CAPS32	= 2,	/* new Firmware: 32-bit Port Capabilities */
};

struct link_config {
	fw_port_cap32_t pcaps;           /* link capabilities */
	fw_port_cap32_t def_acaps;       /* default advertised capabilities */
	fw_port_cap32_t acaps;           /* advertised capabilities */
	fw_port_cap32_t lpacaps;         /* peer advertised capabilities */

	fw_port_cap32_t speed_caps;      /* speed(s) user has requested */
	unsigned int   speed;            /* actual link speed (Mb/s) */

	enum cc_pause  requested_fc;     /* flow control user has requested */
	enum cc_pause  fc;               /* actual link flow control */
	enum cc_pause  advertised_fc;    /* actual advertised flow control */

	enum cc_fec    requested_fec;	 /* Forward Error Correction: */
	enum cc_fec    fec;		 /* requested and actual in use */

	unsigned char  autoneg;          /* autonegotiating? */

	unsigned char  link_ok;          /* link up? */
	unsigned char  link_down_rc;     /* link down reason */

	bool new_module;		 /* ->OS Transceiver Module inserted */
	bool redo_l1cfg;		 /* ->CC redo current "sticky" L1 CFG */
};

#define FW_LEN16(fw_struct) FW_CMD_LEN16_V(sizeof(fw_struct) / 16)

enum {
	MAX_ETH_QSETS = 32,           /* # of Ethernet Tx/Rx queue sets */
	MAX_OFLD_QSETS = 16,          /* # of offload Tx, iscsi Rx queue sets */
	MAX_CTRL_QUEUES = NCHAN,      /* # of control Tx queues */
};

enum {
	MAX_TXQ_ENTRIES      = 16384,
	MAX_CTRL_TXQ_ENTRIES = 1024,
	MAX_RSPQ_ENTRIES     = 16384,
	MAX_RX_BUFFERS       = 16384,
	MIN_TXQ_ENTRIES      = 32,
	MIN_CTRL_TXQ_ENTRIES = 32,
	MIN_RSPQ_ENTRIES     = 128,
	MIN_FL_ENTRIES       = 16
};

enum {
	MAX_TXQ_DESC_SIZE      = 64,
	MAX_RXQ_DESC_SIZE      = 128,
	MAX_FL_DESC_SIZE       = 8,
	MAX_CTRL_TXQ_DESC_SIZE = 64,
};

enum {
	INGQ_EXTRAS = 2,        /* firmware event queue and */
				/*   forwarded interrupts */
	MAX_INGQ = MAX_ETH_QSETS + INGQ_EXTRAS,
};

enum {
	PRIV_FLAG_PORT_TX_VM_BIT,
};

#define PRIV_FLAG_PORT_TX_VM		BIT(PRIV_FLAG_PORT_TX_VM_BIT)

#define PRIV_FLAGS_ADAP			0
#define PRIV_FLAGS_PORT			PRIV_FLAG_PORT_TX_VM

struct adapter;
struct sge_rspq;

#include "cxgb4_dcb.h"

#ifdef CONFIG_CHELSIO_T4_FCOE
#include "cxgb4_fcoe.h"
#endif /* CONFIG_CHELSIO_T4_FCOE */

struct port_info {
	struct adapter *adapter;
	u16    viid;
	int    xact_addr_filt;        /* index of exact MAC address filter */
	u16    rss_size;              /* size of VI's RSS table slice */
	s8     mdio_addr;
	enum fw_port_type port_type;
	u8     mod_type;
	u8     port_id;
	u8     tx_chan;
	u8     lport;                 /* associated offload logical port */
	u8     nqsets;                /* # of qsets */
	u8     first_qset;            /* index of first qset */
	u8     rss_mode;
	struct link_config link_cfg;
	u16   *rss;
	struct port_stats stats_base;
#ifdef CONFIG_CHELSIO_T4_DCB
	struct port_dcb_info dcb;     /* Data Center Bridging support */
#endif
#ifdef CONFIG_CHELSIO_T4_FCOE
	struct cxgb_fcoe fcoe;
#endif /* CONFIG_CHELSIO_T4_FCOE */
	bool rxtstamp;  /* Enable TS */
	struct hwtstamp_config tstamp_config;
	bool ptp_enable;
	struct sched_table *sched_tbl;
	u32 eth_flags;

	/* viid and smt fields either returned by fw
	 * or decoded by parsing viid by driver.
	 */
	u8 vin;
	u8 vivld;
	u8 smt_idx;
	u8 rx_cchan;

	bool tc_block_shared;

	/* Mirror VI information */
	u16 viid_mirror;
	u16 nmirrorqsets;
	u32 vi_mirror_count;
	struct mutex vi_mirror_mutex; /* Sync access to Mirror VI info */
	struct cxgb4_ethtool_lb_test ethtool_lb;
};

struct dentry;
struct work_struct;

enum {                                 /* adapter flags */
	CXGB4_FULL_INIT_DONE		= (1 << 0),
	CXGB4_DEV_ENABLED		= (1 << 1),
	CXGB4_USING_MSI			= (1 << 2),
	CXGB4_USING_MSIX		= (1 << 3),
	CXGB4_FW_OK			= (1 << 4),
	CXGB4_RSS_TNLALLLOOKUP		= (1 << 5),
	CXGB4_USING_SOFT_PARAMS		= (1 << 6),
	CXGB4_MASTER_PF			= (1 << 7),
	CXGB4_FW_OFLD_CONN		= (1 << 9),
	CXGB4_ROOT_NO_RELAXED_ORDERING	= (1 << 10),
	CXGB4_SHUTTING_DOWN		= (1 << 11),
	CXGB4_SGE_DBQ_TIMER		= (1 << 12),
};

enum {
	ULP_CRYPTO_LOOKASIDE = 1 << 0,
	ULP_CRYPTO_IPSEC_INLINE = 1 << 1,
	ULP_CRYPTO_KTLS_INLINE  = 1 << 3,
};

#define CXGB4_MIRROR_RXQ_DEFAULT_DESC_NUM 1024
#define CXGB4_MIRROR_RXQ_DEFAULT_DESC_SIZE 64
#define CXGB4_MIRROR_RXQ_DEFAULT_INTR_USEC 5
#define CXGB4_MIRROR_RXQ_DEFAULT_PKT_CNT 8

#define CXGB4_MIRROR_FLQ_DEFAULT_DESC_NUM 72

struct rx_sw_desc;

struct sge_fl {                     /* SGE free-buffer queue state */
	unsigned int avail;         /* # of available Rx buffers */
	unsigned int pend_cred;     /* new buffers since last FL DB ring */
	unsigned int cidx;          /* consumer index */
	unsigned int pidx;          /* producer index */
	unsigned long alloc_failed; /* # of times buffer allocation failed */
	unsigned long large_alloc_failed;
	unsigned long mapping_err;  /* # of RX Buffer DMA Mapping failures */
	unsigned long low;          /* # of times momentarily starving */
	unsigned long starving;
	/* RO fields */
	unsigned int cntxt_id;      /* SGE context id for the free list */
	unsigned int size;          /* capacity of free list */
	struct rx_sw_desc *sdesc;   /* address of SW Rx descriptor ring */
	__be64 *desc;               /* address of HW Rx descriptor ring */
	dma_addr_t addr;            /* bus address of HW ring start */
	void __iomem *bar2_addr;    /* address of BAR2 Queue registers */
	unsigned int bar2_qid;      /* Queue ID for BAR2 Queue registers */
};

/* A packet gather list */
struct pkt_gl {
	u64 sgetstamp;		    /* SGE Time Stamp for Ingress Packet */
	struct page_frag frags[MAX_SKB_FRAGS];
	void *va;                         /* virtual address of first byte */
	unsigned int nfrags;              /* # of fragments */
	unsigned int tot_len;             /* total length of fragments */
};

typedef int (*rspq_handler_t)(struct sge_rspq *q, const __be64 *rsp,
			      const struct pkt_gl *gl);
typedef void (*rspq_flush_handler_t)(struct sge_rspq *q);
/* LRO related declarations for ULD */
struct t4_lro_mgr {
#define MAX_LRO_SESSIONS		64
	u8 lro_session_cnt;         /* # of sessions to aggregate */
	unsigned long lro_pkts;     /* # of LRO super packets */
	unsigned long lro_merged;   /* # of wire packets merged by LRO */
	struct sk_buff_head lroq;   /* list of aggregated sessions */
};

struct sge_rspq {                   /* state for an SGE response queue */
	struct napi_struct napi;
	const __be64 *cur_desc;     /* current descriptor in queue */
	unsigned int cidx;          /* consumer index */
	u8 gen;                     /* current generation bit */
	u8 intr_params;             /* interrupt holdoff parameters */
	u8 next_intr_params;        /* holdoff params for next interrupt */
	u8 adaptive_rx;
	u8 pktcnt_idx;              /* interrupt packet threshold */
	u8 uld;                     /* ULD handling this queue */
	u8 idx;                     /* queue index within its group */
	int offset;                 /* offset into current Rx buffer */
	u16 cntxt_id;               /* SGE context id for the response q */
	u16 abs_id;                 /* absolute SGE id for the response q */
	__be64 *desc;               /* address of HW response ring */
	dma_addr_t phys_addr;       /* physical address of the ring */
	void __iomem *bar2_addr;    /* address of BAR2 Queue registers */
	unsigned int bar2_qid;      /* Queue ID for BAR2 Queue registers */
	unsigned int iqe_len;       /* entry size */
	unsigned int size;          /* capacity of response queue */
	struct adapter *adap;
	struct net_device *netdev;  /* associated net device */
	rspq_handler_t handler;
	rspq_flush_handler_t flush_handler;
	struct t4_lro_mgr lro_mgr;
};

struct sge_eth_stats {              /* Ethernet queue statistics */
	unsigned long pkts;         /* # of ethernet packets */
	unsigned long lro_pkts;     /* # of LRO super packets */
	unsigned long lro_merged;   /* # of wire packets merged by LRO */
	unsigned long rx_cso;       /* # of Rx checksum offloads */
	unsigned long vlan_ex;      /* # of Rx VLAN extractions */
	unsigned long rx_drops;     /* # of packets dropped due to no mem */
	unsigned long bad_rx_pkts;  /* # of packets with err_vec!=0 */
};

struct sge_eth_rxq {                /* SW Ethernet Rx queue */
	struct sge_rspq rspq;
	struct sge_fl fl;
	struct sge_eth_stats stats;
	struct msix_info *msix;
} ____cacheline_aligned_in_smp;

struct sge_ofld_stats {             /* offload queue statistics */
	unsigned long pkts;         /* # of packets */
	unsigned long imm;          /* # of immediate-data packets */
	unsigned long an;           /* # of asynchronous notifications */
	unsigned long nomem;        /* # of responses deferred due to no mem */
};

struct sge_ofld_rxq {               /* SW offload Rx queue */
	struct sge_rspq rspq;
	struct sge_fl fl;
	struct sge_ofld_stats stats;
	struct msix_info *msix;
} ____cacheline_aligned_in_smp;

struct tx_desc {
	__be64 flit[8];
};

struct ulptx_sgl;

struct tx_sw_desc {
	struct sk_buff *skb; /* SKB to free after getting completion */
	dma_addr_t addr[MAX_SKB_FRAGS + 1]; /* DMA mapped addresses */
};

struct sge_txq {
	unsigned int  in_use;       /* # of in-use Tx descriptors */
	unsigned int  q_type;	    /* Q type Eth/Ctrl/Ofld */
	unsigned int  size;         /* # of descriptors */
	unsigned int  cidx;         /* SW consumer index */
	unsigned int  pidx;         /* producer index */
	unsigned long stops;        /* # of times q has been stopped */
	unsigned long restarts;     /* # of queue restarts */
	unsigned int  cntxt_id;     /* SGE context id for the Tx q */
	struct tx_desc *desc;       /* address of HW Tx descriptor ring */
	struct tx_sw_desc *sdesc;   /* address of SW Tx descriptor ring */
	struct sge_qstat *stat;     /* queue status entry */
	dma_addr_t    phys_addr;    /* physical address of the ring */
	spinlock_t db_lock;
	int db_disabled;
	unsigned short db_pidx;
	unsigned short db_pidx_inc;
	void __iomem *bar2_addr;    /* address of BAR2 Queue registers */
	unsigned int bar2_qid;      /* Queue ID for BAR2 Queue registers */
};

struct sge_eth_txq {                /* state for an SGE Ethernet Tx queue */
	struct sge_txq q;
	struct netdev_queue *txq;   /* associated netdev TX queue */
#ifdef CONFIG_CHELSIO_T4_DCB
	u8 dcb_prio;		    /* DCB Priority bound to queue */
#endif
	u8 dbqt;                    /* SGE Doorbell Queue Timer in use */
	unsigned int dbqtimerix;    /* SGE Doorbell Queue Timer Index */
	unsigned long tso;          /* # of TSO requests */
	unsigned long uso;          /* # of USO requests */
	unsigned long tx_cso;       /* # of Tx checksum offloads */
	unsigned long vlan_ins;     /* # of Tx VLAN insertions */
	unsigned long mapping_err;  /* # of I/O MMU packet mapping errors */
} ____cacheline_aligned_in_smp;

struct sge_uld_txq {               /* state for an SGE offload Tx queue */
	struct sge_txq q;
	struct adapter *adap;
	struct sk_buff_head sendq;  /* list of backpressured packets */
	struct tasklet_struct qresume_tsk; /* restarts the queue */
	bool service_ofldq_running; /* service_ofldq() is processing sendq */
	u8 full;                    /* the Tx ring is full */
	unsigned long mapping_err;  /* # of I/O MMU packet mapping errors */
} ____cacheline_aligned_in_smp;

struct sge_ctrl_txq {               /* state for an SGE control Tx queue */
	struct sge_txq q;
	struct adapter *adap;
	struct sk_buff_head sendq;  /* list of backpressured packets */
	struct tasklet_struct qresume_tsk; /* restarts the queue */
	u8 full;                    /* the Tx ring is full */
} ____cacheline_aligned_in_smp;

struct sge_uld_rxq_info {
	char name[IFNAMSIZ];	/* name of ULD driver */
	struct sge_ofld_rxq *uldrxq; /* Rxq's for ULD */
	u16 *rspq_id;		/* response queue id's of rxq */
	u16 nrxq;		/* # of ingress uld queues */
	u16 nciq;		/* # of completion queues */
	u8 uld;			/* uld type */
};

struct sge_uld_txq_info {
	struct sge_uld_txq *uldtxq; /* Txq's for ULD */
	atomic_t users;		/* num users */
	u16 ntxq;		/* # of egress uld queues */
};

/* struct to maintain ULD list to reallocate ULD resources on hotplug */
struct cxgb4_uld_list {
	struct cxgb4_uld_info uld_info;
	struct list_head list_node;
	enum cxgb4_uld uld_type;
};

enum sge_eosw_state {
	CXGB4_EO_STATE_CLOSED = 0, /* Not ready to accept traffic */
	CXGB4_EO_STATE_FLOWC_OPEN_SEND, /* Send FLOWC open request */
	CXGB4_EO_STATE_FLOWC_OPEN_REPLY, /* Waiting for FLOWC open reply */
	CXGB4_EO_STATE_ACTIVE, /* Ready to accept traffic */
	CXGB4_EO_STATE_FLOWC_CLOSE_SEND, /* Send FLOWC close request */
	CXGB4_EO_STATE_FLOWC_CLOSE_REPLY, /* Waiting for FLOWC close reply */
};

struct sge_eosw_txq {
	spinlock_t lock; /* Per queue lock to synchronize completions */
	enum sge_eosw_state state; /* Current ETHOFLD State */
	struct tx_sw_desc *desc; /* Descriptor ring to hold packets */
	u32 ndesc; /* Number of descriptors */
	u32 pidx; /* Current Producer Index */
	u32 last_pidx; /* Last successfully transmitted Producer Index */
	u32 cidx; /* Current Consumer Index */
	u32 last_cidx; /* Last successfully reclaimed Consumer Index */
	u32 flowc_idx; /* Descriptor containing a FLOWC request */
	u32 inuse; /* Number of packets held in ring */

	u32 cred; /* Current available credits */
	u32 ncompl; /* # of completions posted */
	u32 last_compl; /* # of credits consumed since last completion req */

	u32 eotid; /* Index into EOTID table in software */
	u32 hwtid; /* Hardware EOTID index */

	u32 hwqid; /* Underlying hardware queue index */
	struct net_device *netdev; /* Pointer to netdevice */
	struct tasklet_struct qresume_tsk; /* Restarts the queue */
	struct completion completion; /* completion for FLOWC rendezvous */
};

struct sge_eohw_txq {
	spinlock_t lock; /* Per queue lock */
	struct sge_txq q; /* HW Txq */
	struct adapter *adap; /* Backpointer to adapter */
	unsigned long tso; /* # of TSO requests */
	unsigned long uso; /* # of USO requests */
	unsigned long tx_cso; /* # of Tx checksum offloads */
	unsigned long vlan_ins; /* # of Tx VLAN insertions */
	unsigned long mapping_err; /* # of I/O MMU packet mapping errors */
};

struct sge {
	struct sge_eth_txq ethtxq[MAX_ETH_QSETS];
	struct sge_eth_txq ptptxq;
	struct sge_ctrl_txq ctrlq[MAX_CTRL_QUEUES];

	struct sge_eth_rxq ethrxq[MAX_ETH_QSETS];
	struct sge_rspq fw_evtq ____cacheline_aligned_in_smp;
	struct sge_uld_rxq_info **uld_rxq_info;
	struct sge_uld_txq_info **uld_txq_info;

	struct sge_rspq intrq ____cacheline_aligned_in_smp;
	spinlock_t intrq_lock;

	struct sge_eohw_txq *eohw_txq;
	struct sge_ofld_rxq *eohw_rxq;

	struct sge_eth_rxq *mirror_rxq[NCHAN];

	u16 max_ethqsets;           /* # of available Ethernet queue sets */
	u16 ethqsets;               /* # of active Ethernet queue sets */
	u16 ethtxq_rover;           /* Tx queue to clean up next */
	u16 ofldqsets;              /* # of active ofld queue sets */
	u16 nqs_per_uld;	    /* # of Rx queues per ULD */
	u16 eoqsets;                /* # of ETHOFLD queues */
	u16 mirrorqsets;            /* # of Mirror queues */

	u16 timer_val[SGE_NTIMERS];
	u8 counter_val[SGE_NCOUNTERS];
	u16 dbqtimer_tick;
	u16 dbqtimer_val[SGE_NDBQTIMERS];
	u32 fl_pg_order;            /* large page allocation size */
	u32 stat_len;               /* length of status page at ring end */
	u32 pktshift;               /* padding between CPL & packet data */
	u32 fl_align;               /* response queue message alignment */
	u32 fl_starve_thres;        /* Free List starvation threshold */

	struct sge_idma_monitor_state idma_monitor;
	unsigned int egr_start;
	unsigned int egr_sz;
	unsigned int ingr_start;
	unsigned int ingr_sz;
	void **egr_map;    /* qid->queue egress queue map */
	struct sge_rspq **ingr_map; /* qid->queue ingress queue map */
	unsigned long *starving_fl;
	unsigned long *txq_maperr;
	unsigned long *blocked_fl;
	struct timer_list rx_timer; /* refills starving FLs */
	struct timer_list tx_timer; /* checks Tx queues */

	int fwevtq_msix_idx; /* Index to firmware event queue MSI-X info */
	int nd_msix_idx; /* Index to non-data interrupts MSI-X info */
};

#define for_each_ethrxq(sge, i) for (i = 0; i < (sge)->ethqsets; i++)
#define for_each_ofldtxq(sge, i) for (i = 0; i < (sge)->ofldqsets; i++)

struct l2t_data;

#ifdef CONFIG_PCI_IOV

/* T4 supports SRIOV on PF0-3 and T5 on PF0-7.  However, the Serial
 * Configuration initialization for T5 only has SR-IOV functionality enabled
 * on PF0-3 in order to simplify everything.
 */
#define NUM_OF_PF_WITH_SRIOV 4

#endif

struct doorbell_stats {
	u32 db_drop;
	u32 db_empty;
	u32 db_full;
};

struct hash_mac_addr {
	struct list_head list;
	u8 addr[ETH_ALEN];
	unsigned int iface_mac;
};

struct msix_bmap {
	unsigned long *msix_bmap;
	unsigned int mapsize;
	spinlock_t lock; /* lock for acquiring bitmap */
};

struct msix_info {
	unsigned short vec;
	char desc[IFNAMSIZ + 10];
	unsigned int idx;
	cpumask_var_t aff_mask;
};

struct vf_info {
	unsigned char vf_mac_addr[ETH_ALEN];
	unsigned int tx_rate;
	bool pf_set_mac;
	u16 vlan;
	int link_state;
};

enum {
	HMA_DMA_MAPPED_FLAG = 1
};

struct hma_data {
	unsigned char flags;
	struct sg_table *sgt;
	dma_addr_t *phy_addr;	/* physical address of the page */
};

struct mbox_list {
	struct list_head list;
};

#if IS_ENABLED(CONFIG_THERMAL)
struct ch_thermal {
	struct thermal_zone_device *tzdev;
	int trip_temp;
	int trip_type;
};
#endif

struct mps_entries_ref {
	struct list_head list;
	u8 addr[ETH_ALEN];
	u8 mask[ETH_ALEN];
	u16 idx;
	refcount_t refcnt;
};

struct cxgb4_ethtool_filter_info {
	u32 *loc_array; /* Array holding the actual TIDs set to filters */
	unsigned long *bmap; /* Bitmap for managing filters in use */
	u32 in_use; /* # of filters in use */
};

struct cxgb4_ethtool_filter {
	u32 nentries; /* Adapter wide number of supported filters */
	struct cxgb4_ethtool_filter_info *port; /* Per port entry */
};

struct adapter {
	void __iomem *regs;
	void __iomem *bar2;
	u32 t4_bar0;
	struct pci_dev *pdev;
	struct device *pdev_dev;
	const char *name;
	unsigned int mbox;
	unsigned int pf;
	unsigned int flags;
	unsigned int adap_idx;
	enum chip_type chip;
	u32 eth_flags;

	int msg_enable;
	__be16 vxlan_port;
	__be16 geneve_port;

	struct adapter_params params;
	struct cxgb4_virt_res vres;
	unsigned int swintr;

	/* MSI-X Info for NIC and OFLD queues */
	struct msix_info *msix_info;
	struct msix_bmap msix_bmap;

	struct doorbell_stats db_stats;
	struct sge sge;

	struct net_device *port[MAX_NPORTS];
	u8 chan_map[NCHAN];                   /* channel -> port map */

	struct vf_info *vfinfo;
	u8 num_vfs;

	u32 filter_mode;
	unsigned int l2t_start;
	unsigned int l2t_end;
	struct l2t_data *l2t;
	unsigned int clipt_start;
	unsigned int clipt_end;
	struct clip_tbl *clipt;
	unsigned int rawf_start;
	unsigned int rawf_cnt;
	struct smt_data *smt;
	struct cxgb4_uld_info *uld;
	void *uld_handle[CXGB4_ULD_MAX];
	unsigned int num_uld;
	unsigned int num_ofld_uld;
	struct list_head list_node;
	struct list_head rcu_node;
	struct list_head mac_hlist; /* list of MAC addresses in MPS Hash */
	struct list_head mps_ref;
	spinlock_t mps_ref_lock; /* lock for syncing mps ref/def activities */

	void *iscsi_ppm;

	struct tid_info tids;
	void **tid_release_head;
	spinlock_t tid_release_lock;
	struct workqueue_struct *workq;
	struct work_struct tid_release_task;
	struct work_struct db_full_task;
	struct work_struct db_drop_task;
	struct work_struct fatal_err_notify_task;
	bool tid_release_task_busy;

	/* lock for mailbox cmd list */
	spinlock_t mbox_lock;
	struct mbox_list mlist;

	/* support for mailbox command/reply logging */
#define T4_OS_LOG_MBOX_CMDS 256
	struct mbox_cmd_log *mbox_log;

	struct mutex uld_mutex;

	struct dentry *debugfs_root;
	bool use_bd;     /* Use SGE Back Door intfc for reading SGE Contexts */
	bool trace_rss;	/* 1 implies that different RSS flit per filter is
			 * used per filter else if 0 default RSS flit is
			 * used for all 4 filters.
			 */

	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_clock_info;
	struct sk_buff *ptp_tx_skb;
	/* ptp lock */
	spinlock_t ptp_lock;
	spinlock_t stats_lock;
	spinlock_t win0_lock ____cacheline_aligned_in_smp;

	/* TC u32 offload */
	struct cxgb4_tc_u32_table *tc_u32;
	struct chcr_ktls chcr_ktls;
	struct chcr_stats_debug chcr_stats;
#if IS_ENABLED(CONFIG_CHELSIO_TLS_DEVICE)
	struct ch_ktls_stats_debug ch_ktls_stats;
#endif
#if IS_ENABLED(CONFIG_CHELSIO_IPSEC_INLINE)
	struct ch_ipsec_stats_debug ch_ipsec_stats;
#endif

	/* TC flower offload */
	bool tc_flower_initialized;
	struct rhashtable flower_tbl;
	struct rhashtable_params flower_ht_params;
	struct timer_list flower_stats_timer;
	struct work_struct flower_stats_work;

	/* Ethtool Dump */
	struct ethtool_dump eth_dump;

	/* HMA */
	struct hma_data hma;

	struct srq_data *srq;

	/* Dump buffer for collecting logs in kdump kernel */
	struct vmcoredd_data vmcoredd;
#if IS_ENABLED(CONFIG_THERMAL)
	struct ch_thermal ch_thermal;
#endif

	/* TC MQPRIO offload */
	struct cxgb4_tc_mqprio *tc_mqprio;

	/* TC MATCHALL classifier offload */
	struct cxgb4_tc_matchall *tc_matchall;

	/* Ethtool n-tuple */
	struct cxgb4_ethtool_filter *ethtool_filters;
};

/* Support for "sched-class" command to allow a TX Scheduling Class to be
 * programmed with various parameters.
 */
struct ch_sched_params {
	u8   type;                     /* packet or flow */
	union {
		struct {
			u8   level;    /* scheduler hierarchy level */
			u8   mode;     /* per-class or per-flow */
			u8   rateunit; /* bit or packet rate */
			u8   ratemode; /* %port relative or kbps absolute */
			u8   channel;  /* scheduler channel [0..N] */
			u8   class;    /* scheduler class [0..N] */
			u32  minrate;  /* minimum rate */
			u32  maxrate;  /* maximum rate */
			u16  weight;   /* percent weight */
			u16  pktsize;  /* average packet size */
			u16  burstsize;  /* burst buffer size */
		} params;
	} u;
};

enum {
	SCHED_CLASS_TYPE_PACKET = 0,    /* class type */
};

enum {
	SCHED_CLASS_LEVEL_CL_RL = 0,    /* class rate limiter */
	SCHED_CLASS_LEVEL_CH_RL = 2,    /* channel rate limiter */
};

enum {
	SCHED_CLASS_MODE_CLASS = 0,     /* per-class scheduling */
	SCHED_CLASS_MODE_FLOW,          /* per-flow scheduling */
};

enum {
	SCHED_CLASS_RATEUNIT_BITS = 0,  /* bit rate scheduling */
};

enum {
	SCHED_CLASS_RATEMODE_ABS = 1,   /* Kb/s */
};

/* Support for "sched_queue" command to allow one or more NIC TX Queues
 * to be bound to a TX Scheduling Class.
 */
struct ch_sched_queue {
	s8   queue;    /* queue index */
	s8   class;    /* class index */
};

/* Support for "sched_flowc" command to allow one or more FLOWC
 * to be bound to a TX Scheduling Class.
 */
struct ch_sched_flowc {
	s32 tid;   /* TID to bind */
	s8  class; /* class index */
};

/* Defined bit width of user definable filter tuples
 */
#define ETHTYPE_BITWIDTH 16
#define FRAG_BITWIDTH 1
#define MACIDX_BITWIDTH 9
#define FCOE_BITWIDTH 1
#define IPORT_BITWIDTH 3
#define MATCHTYPE_BITWIDTH 3
#define PROTO_BITWIDTH 8
#define TOS_BITWIDTH 8
#define PF_BITWIDTH 8
#define VF_BITWIDTH 8
#define IVLAN_BITWIDTH 16
#define OVLAN_BITWIDTH 16
#define ENCAP_VNI_BITWIDTH 24

/* Filter matching rules.  These consist of a set of ingress packet field
 * (value, mask) tuples.  The associated ingress packet field matches the
 * tuple when ((field & mask) == value).  (Thus a wildcard "don't care" field
 * rule can be constructed by specifying a tuple of (0, 0).)  A filter rule
 * matches an ingress packet when all of the individual individual field
 * matching rules are true.
 *
 * Partial field masks are always valid, however, while it may be easy to
 * understand their meanings for some fields (e.g. IP address to match a
 * subnet), for others making sensible partial masks is less intuitive (e.g.
 * MPS match type) ...
 *
 * Most of the following data structures are modeled on T4 capabilities.
 * Drivers for earlier chips use the subsets which make sense for those chips.
 * We really need to come up with a hardware-independent mechanism to
 * represent hardware filter capabilities ...
 */
struct ch_filter_tuple {
	/* Compressed header matching field rules.  The TP_VLAN_PRI_MAP
	 * register selects which of these fields will participate in the
	 * filter match rules -- up to a maximum of 36 bits.  Because
	 * TP_VLAN_PRI_MAP is a global register, all filters must use the same
	 * set of fields.
	 */
	uint32_t ethtype:ETHTYPE_BITWIDTH;      /* Ethernet type */
	uint32_t frag:FRAG_BITWIDTH;            /* IP fragmentation header */
	uint32_t ivlan_vld:1;                   /* inner VLAN valid */
	uint32_t ovlan_vld:1;                   /* outer VLAN valid */
	uint32_t pfvf_vld:1;                    /* PF/VF valid */
	uint32_t encap_vld:1;			/* Encapsulation valid */
	uint32_t macidx:MACIDX_BITWIDTH;        /* exact match MAC index */
	uint32_t fcoe:FCOE_BITWIDTH;            /* FCoE packet */
	uint32_t iport:IPORT_BITWIDTH;          /* ingress port */
	uint32_t matchtype:MATCHTYPE_BITWIDTH;  /* MPS match type */
	uint32_t proto:PROTO_BITWIDTH;          /* protocol type */
	uint32_t tos:TOS_BITWIDTH;              /* TOS/Traffic Type */
	uint32_t pf:PF_BITWIDTH;                /* PCI-E PF ID */
	uint32_t vf:VF_BITWIDTH;                /* PCI-E VF ID */
	uint32_t ivlan:IVLAN_BITWIDTH;          /* inner VLAN */
	uint32_t ovlan:OVLAN_BITWIDTH;          /* outer VLAN */
	uint32_t vni:ENCAP_VNI_BITWIDTH;	/* VNI of tunnel */

	/* Uncompressed header matching field rules.  These are always
	 * available for field rules.
	 */
	uint8_t lip[16];        /* local IP address (IPv4 in [3:0]) */
	uint8_t fip[16];        /* foreign IP address (IPv4 in [3:0]) */
	uint16_t lport;         /* local port */
	uint16_t fport;         /* foreign port */
};

/* A filter ioctl command.
 */
struct ch_filter_specification {
	/* Administrative fields for filter.
	 */
	uint32_t hitcnts:1;     /* count filter hits in TCB */
	uint32_t prio:1;        /* filter has priority over active/server */

	/* Fundamental filter typing.  This is the one element of filter
	 * matching that doesn't exist as a (value, mask) tuple.
	 */
	uint32_t type:1;        /* 0 => IPv4, 1 => IPv6 */
	u32 hash:1;		/* 0 => wild-card, 1 => exact-match */

	/* Packet dispatch information.  Ingress packets which match the
	 * filter rules will be dropped, passed to the host or switched back
	 * out as egress packets.
	 */
	uint32_t action:2;      /* drop, pass, switch */

	uint32_t rpttid:1;      /* report TID in RSS hash field */

	uint32_t dirsteer:1;    /* 0 => RSS, 1 => steer to iq */
	uint32_t iq:10;         /* ingress queue */

	uint32_t maskhash:1;    /* dirsteer=0: store RSS hash in TCB */
	uint32_t dirsteerhash:1;/* dirsteer=1: 0 => TCB contains RSS hash */
				/*             1 => TCB contains IQ ID */

	/* Switch proxy/rewrite fields.  An ingress packet which matches a
	 * filter with "switch" set will be looped back out as an egress
	 * packet -- potentially with some Ethernet header rewriting.
	 */
	uint32_t eport:2;       /* egress port to switch packet out */
	uint32_t newdmac:1;     /* rewrite destination MAC address */
	uint32_t newsmac:1;     /* rewrite source MAC address */
	uint32_t newvlan:2;     /* rewrite VLAN Tag */
	uint32_t nat_mode:3;    /* specify NAT operation mode */
	uint8_t dmac[ETH_ALEN]; /* new destination MAC address */
	uint8_t smac[ETH_ALEN]; /* new source MAC address */
	uint16_t vlan;          /* VLAN Tag to insert */

	u8 nat_lip[16];		/* local IP to use after NAT'ing */
	u8 nat_fip[16];		/* foreign IP to use after NAT'ing */
	u16 nat_lport;		/* local port to use after NAT'ing */
	u16 nat_fport;		/* foreign port to use after NAT'ing */

	u32 tc_prio;		/* TC's filter priority index */
	u64 tc_cookie;		/* Unique cookie identifying TC rules */

	/* reservation for future additions */
	u8 rsvd[12];

	/* Filter rule value/mask pairs.
	 */
	struct ch_filter_tuple val;
	struct ch_filter_tuple mask;
};

enum {
	FILTER_PASS = 0,        /* default */
	FILTER_DROP,
	FILTER_SWITCH
};

enum {
	VLAN_NOCHANGE = 0,      /* default */
	VLAN_REMOVE,
	VLAN_INSERT,
	VLAN_REWRITE
};

enum {
	NAT_MODE_NONE = 0,	/* No NAT performed */
	NAT_MODE_DIP,		/* NAT on Dst IP */
	NAT_MODE_DIP_DP,	/* NAT on Dst IP, Dst Port */
	NAT_MODE_DIP_DP_SIP,	/* NAT on Dst IP, Dst Port and Src IP */
	NAT_MODE_DIP_DP_SP,	/* NAT on Dst IP, Dst Port and Src Port */
	NAT_MODE_SIP_SP,	/* NAT on Src IP and Src Port */
	NAT_MODE_DIP_SIP_SP,	/* NAT on Dst IP, Src IP and Src Port */
	NAT_MODE_ALL		/* NAT on entire 4-tuple */
};

#define CXGB4_FILTER_TYPE_MAX 2

/* Host shadow copy of ingress filter entry.  This is in host native format
 * and doesn't match the ordering or bit order, etc. of the hardware of the
 * firmware command.  The use of bit-field structure elements is purely to
 * remind ourselves of the field size limitations and save memory in the case
 * where the filter table is large.
 */
struct filter_entry {
	/* Administrative fields for filter. */
	u32 valid:1;            /* filter allocated and valid */
	u32 locked:1;           /* filter is administratively locked */

	u32 pending:1;          /* filter action is pending firmware reply */
	struct filter_ctx *ctx; /* Caller's completion hook */
	struct l2t_entry *l2t;  /* Layer Two Table entry for dmac */
	struct smt_entry *smt;  /* Source Mac Table entry for smac */
	struct net_device *dev; /* Associated net device */
	u32 tid;                /* This will store the actual tid */

	/* The filter itself.  Most of this is a straight copy of information
	 * provided by the extended ioctl().  Some fields are translated to
	 * internal forms -- for instance the Ingress Queue ID passed in from
	 * the ioctl() is translated into the Absolute Ingress Queue ID.
	 */
	struct ch_filter_specification fs;
};

static inline int is_offload(const struct adapter *adap)
{
	return adap->params.offload;
}

static inline int is_hashfilter(const struct adapter *adap)
{
	return adap->params.hash_filter;
}

static inline int is_pci_uld(const struct adapter *adap)
{
	return adap->params.crypto;
}

static inline int is_uld(const struct adapter *adap)
{
	return (adap->params.offload || adap->params.crypto);
}

static inline int is_ethofld(const struct adapter *adap)
{
	return adap->params.ethofld;
}

static inline u32 t4_read_reg(struct adapter *adap, u32 reg_addr)
{
	return readl(adap->regs + reg_addr);
}

static inline void t4_write_reg(struct adapter *adap, u32 reg_addr, u32 val)
{
	writel(val, adap->regs + reg_addr);
}

#ifndef readq
static inline u64 readq(const volatile void __iomem *addr)
{
	return readl(addr) + ((u64)readl(addr + 4) << 32);
}

static inline void writeq(u64 val, volatile void __iomem *addr)
{
	writel(val, addr);
	writel(val >> 32, addr + 4);
}
#endif

static inline u64 t4_read_reg64(struct adapter *adap, u32 reg_addr)
{
	return readq(adap->regs + reg_addr);
}

static inline void t4_write_reg64(struct adapter *adap, u32 reg_addr, u64 val)
{
	writeq(val, adap->regs + reg_addr);
}

/**
 * t4_set_hw_addr - store a port's MAC address in SW
 * @adapter: the adapter
 * @port_idx: the port index
 * @hw_addr: the Ethernet address
 *
 * Store the Ethernet address of the given port in SW.  Called by the common
 * code when it retrieves a port's Ethernet address from EEPROM.
 */
static inline void t4_set_hw_addr(struct adapter *adapter, int port_idx,
				  u8 hw_addr[])
{
	ether_addr_copy(adapter->port[port_idx]->dev_addr, hw_addr);
	ether_addr_copy(adapter->port[port_idx]->perm_addr, hw_addr);
}

/**
 * netdev2pinfo - return the port_info structure associated with a net_device
 * @dev: the netdev
 *
 * Return the struct port_info associated with a net_device
 */
static inline struct port_info *netdev2pinfo(const struct net_device *dev)
{
	return netdev_priv(dev);
}

/**
 * adap2pinfo - return the port_info of a port
 * @adap: the adapter
 * @idx: the port index
 *
 * Return the port_info structure for the port of the given index.
 */
static inline struct port_info *adap2pinfo(struct adapter *adap, int idx)
{
	return netdev_priv(adap->port[idx]);
}

/**
 * netdev2adap - return the adapter structure associated with a net_device
 * @dev: the netdev
 *
 * Return the struct adapter associated with a net_device
 */
static inline struct adapter *netdev2adap(const struct net_device *dev)
{
	return netdev2pinfo(dev)->adapter;
}

/* Return a version number to identify the type of adapter.  The scheme is:
 * - bits 0..9: chip version
 * - bits 10..15: chip revision
 * - bits 16..23: register dump version
 */
static inline unsigned int mk_adap_vers(struct adapter *ap)
{
	return CHELSIO_CHIP_VERSION(ap->params.chip) |
		(CHELSIO_CHIP_RELEASE(ap->params.chip) << 10) | (1 << 16);
}

/* Return a queue's interrupt hold-off time in us.  0 means no timer. */
static inline unsigned int qtimer_val(const struct adapter *adap,
				      const struct sge_rspq *q)
{
	unsigned int idx = q->intr_params >> 1;

	return idx < SGE_NTIMERS ? adap->sge.timer_val[idx] : 0;
}

/* driver name used for ethtool_drvinfo */
extern char cxgb4_driver_name[];

void t4_os_portmod_changed(struct adapter *adap, int port_id);
void t4_os_link_changed(struct adapter *adap, int port_id, int link_stat);

void t4_free_sge_resources(struct adapter *adap);
void t4_free_ofld_rxqs(struct adapter *adap, int n, struct sge_ofld_rxq *q);
irq_handler_t t4_intr_handler(struct adapter *adap);
netdev_tx_t t4_start_xmit(struct sk_buff *skb, struct net_device *dev);
int cxgb4_selftest_lb_pkt(struct net_device *netdev);
int t4_ethrx_handler(struct sge_rspq *q, const __be64 *rsp,
		     const struct pkt_gl *gl);
int t4_mgmt_tx(struct adapter *adap, struct sk_buff *skb);
int t4_ofld_send(struct adapter *adap, struct sk_buff *skb);
int t4_sge_alloc_rxq(struct adapter *adap, struct sge_rspq *iq, bool fwevtq,
		     struct net_device *dev, int intr_idx,
		     struct sge_fl *fl, rspq_handler_t hnd,
		     rspq_flush_handler_t flush_handler, int cong);
int t4_sge_alloc_eth_txq(struct adapter *adap, struct sge_eth_txq *txq,
			 struct net_device *dev, struct netdev_queue *netdevq,
			 unsigned int iqid, u8 dbqt);
int t4_sge_alloc_ctrl_txq(struct adapter *adap, struct sge_ctrl_txq *txq,
			  struct net_device *dev, unsigned int iqid,
			  unsigned int cmplqid);
int t4_sge_mod_ctrl_txq(struct adapter *adap, unsigned int eqid,
			unsigned int cmplqid);
int t4_sge_alloc_uld_txq(struct adapter *adap, struct sge_uld_txq *txq,
			 struct net_device *dev, unsigned int iqid,
			 unsigned int uld_type);
int t4_sge_alloc_ethofld_txq(struct adapter *adap, struct sge_eohw_txq *txq,
			     struct net_device *dev, u32 iqid);
void t4_sge_free_ethofld_txq(struct adapter *adap, struct sge_eohw_txq *txq);
irqreturn_t t4_sge_intr_msix(int irq, void *cookie);
int t4_sge_init(struct adapter *adap);
void t4_sge_start(struct adapter *adap);
void t4_sge_stop(struct adapter *adap);
int t4_sge_eth_txq_egress_update(struct adapter *adap, struct sge_eth_txq *q,
				 int maxreclaim);
void cxgb4_set_ethtool_ops(struct net_device *netdev);
int cxgb4_write_rss(const struct port_info *pi, const u16 *queues);
enum cpl_tx_tnl_lso_type cxgb_encap_offload_supported(struct sk_buff *skb);
extern int dbfifo_int_thresh;

#define for_each_port(adapter, iter) \
	for (iter = 0; iter < (adapter)->params.nports; ++iter)

static inline int is_bypass(struct adapter *adap)
{
	return adap->params.bypass;
}

static inline int is_bypass_device(int device)
{
	/* this should be set based upon device capabilities */
	switch (device) {
	case 0x440b:
	case 0x440c:
		return 1;
	default:
		return 0;
	}
}

static inline int is_10gbt_device(int device)
{
	/* this should be set based upon device capabilities */
	switch (device) {
	case 0x4409:
	case 0x4486:
		return 1;

	default:
		return 0;
	}
}

static inline unsigned int core_ticks_per_usec(const struct adapter *adap)
{
	return adap->params.vpd.cclk / 1000;
}

static inline unsigned int us_to_core_ticks(const struct adapter *adap,
					    unsigned int us)
{
	return (us * adap->params.vpd.cclk) / 1000;
}

static inline unsigned int core_ticks_to_us(const struct adapter *adapter,
					    unsigned int ticks)
{
	/* add Core Clock / 2 to round ticks to nearest uS */
	return ((ticks * 1000 + adapter->params.vpd.cclk/2) /
		adapter->params.vpd.cclk);
}

static inline unsigned int dack_ticks_to_usec(const struct adapter *adap,
					      unsigned int ticks)
{
	return (ticks << adap->params.tp.dack_re) / core_ticks_per_usec(adap);
}

void t4_set_reg_field(struct adapter *adap, unsigned int addr, u32 mask,
		      u32 val);

int t4_wr_mbox_meat_timeout(struct adapter *adap, int mbox, const void *cmd,
			    int size, void *rpl, bool sleep_ok, int timeout);
int t4_wr_mbox_meat(struct adapter *adap, int mbox, const void *cmd, int size,
		    void *rpl, bool sleep_ok);

static inline int t4_wr_mbox_timeout(struct adapter *adap, int mbox,
				     const void *cmd, int size, void *rpl,
				     int timeout)
{
	return t4_wr_mbox_meat_timeout(adap, mbox, cmd, size, rpl, true,
				       timeout);
}

static inline int t4_wr_mbox(struct adapter *adap, int mbox, const void *cmd,
			     int size, void *rpl)
{
	return t4_wr_mbox_meat(adap, mbox, cmd, size, rpl, true);
}

static inline int t4_wr_mbox_ns(struct adapter *adap, int mbox, const void *cmd,
				int size, void *rpl)
{
	return t4_wr_mbox_meat(adap, mbox, cmd, size, rpl, false);
}

/**
 *	hash_mac_addr - return the hash value of a MAC address
 *	@addr: the 48-bit Ethernet MAC address
 *
 *	Hashes a MAC address according to the hash function used by HW inexact
 *	(hash) address matching.
 */
static inline int hash_mac_addr(const u8 *addr)
{
	u32 a = ((u32)addr[0] << 16) | ((u32)addr[1] << 8) | addr[2];
	u32 b = ((u32)addr[3] << 16) | ((u32)addr[4] << 8) | addr[5];

	a ^= b;
	a ^= (a >> 12);
	a ^= (a >> 6);
	return a & 0x3f;
}

int cxgb4_set_rspq_intr_params(struct sge_rspq *q, unsigned int us,
			       unsigned int cnt);
static inline void init_rspq(struct adapter *adap, struct sge_rspq *q,
			     unsigned int us, unsigned int cnt,
			     unsigned int size, unsigned int iqe_size)
{
	q->adap = adap;
	cxgb4_set_rspq_intr_params(q, us, cnt);
	q->iqe_len = iqe_size;
	q->size = size;
}

/**
 *     t4_is_inserted_mod_type - is a plugged in Firmware Module Type
 *     @fw_mod_type: the Firmware Mofule Type
 *
 *     Return whether the Firmware Module Type represents a real Transceiver
 *     Module/Cable Module Type which has been inserted.
 */
static inline bool t4_is_inserted_mod_type(unsigned int fw_mod_type)
{
	return (fw_mod_type != FW_PORT_MOD_TYPE_NONE &&
		fw_mod_type != FW_PORT_MOD_TYPE_NOTSUPPORTED &&
		fw_mod_type != FW_PORT_MOD_TYPE_UNKNOWN &&
		fw_mod_type != FW_PORT_MOD_TYPE_ERROR);
}

void t4_write_indirect(struct adapter *adap, unsigned int addr_reg,
		       unsigned int data_reg, const u32 *vals,
		       unsigned int nregs, unsigned int start_idx);
void t4_read_indirect(struct adapter *adap, unsigned int addr_reg,
		      unsigned int data_reg, u32 *vals, unsigned int nregs,
		      unsigned int start_idx);
void t4_hw_pci_read_cfg4(struct adapter *adapter, int reg, u32 *val);

struct fw_filter_wr;

void t4_intr_enable(struct adapter *adapter);
void t4_intr_disable(struct adapter *adapter);
int t4_slow_intr_handler(struct adapter *adapter);

int t4_wait_dev_ready(void __iomem *regs);

fw_port_cap32_t t4_link_acaps(struct adapter *adapter, unsigned int port,
			      struct link_config *lc);
int t4_link_l1cfg_core(struct adapter *adap, unsigned int mbox,
		       unsigned int port, struct link_config *lc,
		       u8 sleep_ok, int timeout);

static inline int t4_link_l1cfg(struct adapter *adapter, unsigned int mbox,
				unsigned int port, struct link_config *lc)
{
	return t4_link_l1cfg_core(adapter, mbox, port, lc,
				  true, FW_CMD_MAX_TIMEOUT);
}

static inline int t4_link_l1cfg_ns(struct adapter *adapter, unsigned int mbox,
				   unsigned int port, struct link_config *lc)
{
	return t4_link_l1cfg_core(adapter, mbox, port, lc,
				  false, FW_CMD_MAX_TIMEOUT);
}

int t4_restart_aneg(struct adapter *adap, unsigned int mbox, unsigned int port);

u32 t4_read_pcie_cfg4(struct adapter *adap, int reg);
u32 t4_get_util_window(struct adapter *adap);
void t4_setup_memwin(struct adapter *adap, u32 memwin_base, u32 window);

int t4_memory_rw_init(struct adapter *adap, int win, int mtype, u32 *mem_off,
		      u32 *mem_base, u32 *mem_aperture);
void t4_memory_update_win(struct adapter *adap, int win, u32 addr);
void t4_memory_rw_residual(struct adapter *adap, u32 off, u32 addr, u8 *buf,
			   int dir);
#define T4_MEMORY_WRITE	0
#define T4_MEMORY_READ	1
int t4_memory_rw(struct adapter *adap, int win, int mtype, u32 addr, u32 len,
		 void *buf, int dir);
static inline int t4_memory_write(struct adapter *adap, int mtype, u32 addr,
				  u32 len, __be32 *buf)
{
	return t4_memory_rw(adap, 0, mtype, addr, len, buf, 0);
}

unsigned int t4_get_regs_len(struct adapter *adapter);
void t4_get_regs(struct adapter *adap, void *buf, size_t buf_size);

int t4_eeprom_ptov(unsigned int phys_addr, unsigned int fn, unsigned int sz);
int t4_seeprom_wp(struct adapter *adapter, bool enable);
int t4_get_raw_vpd_params(struct adapter *adapter, struct vpd_params *p);
int t4_get_vpd_params(struct adapter *adapter, struct vpd_params *p);
int t4_get_pfres(struct adapter *adapter);
int t4_read_flash(struct adapter *adapter, unsigned int addr,
		  unsigned int nwords, u32 *data, int byte_oriented);
int t4_load_fw(struct adapter *adapter, const u8 *fw_data, unsigned int size);
int t4_load_phy_fw(struct adapter *adap, int win,
		   int (*phy_fw_version)(const u8 *, size_t),
		   const u8 *phy_fw_data, size_t phy_fw_size);
int t4_phy_fw_ver(struct adapter *adap, int *phy_fw_ver);
int t4_fwcache(struct adapter *adap, enum fw_params_param_dev_fwcache op);
int t4_fw_upgrade(struct adapter *adap, unsigned int mbox,
		  const u8 *fw_data, unsigned int size, int force);
int t4_fl_pkt_align(struct adapter *adap);
unsigned int t4_flash_cfg_addr(struct adapter *adapter);
int t4_check_fw_version(struct adapter *adap);
int t4_load_cfg(struct adapter *adapter, const u8 *cfg_data, unsigned int size);
int t4_get_fw_version(struct adapter *adapter, u32 *vers);
int t4_get_bs_version(struct adapter *adapter, u32 *vers);
int t4_get_tp_version(struct adapter *adapter, u32 *vers);
int t4_get_exprom_version(struct adapter *adapter, u32 *vers);
int t4_get_scfg_version(struct adapter *adapter, u32 *vers);
int t4_get_vpd_version(struct adapter *adapter, u32 *vers);
int t4_get_version_info(struct adapter *adapter);
void t4_dump_version_info(struct adapter *adapter);
int t4_prep_fw(struct adapter *adap, struct fw_info *fw_info,
	       const u8 *fw_data, unsigned int fw_size,
	       struct fw_hdr *card_fw, enum dev_state state, int *reset);
int t4_prep_adapter(struct adapter *adapter);
int t4_shutdown_adapter(struct adapter *adapter);

enum t4_bar2_qtype { T4_BAR2_QTYPE_EGRESS, T4_BAR2_QTYPE_INGRESS };
int t4_bar2_sge_qregs(struct adapter *adapter,
		      unsigned int qid,
		      enum t4_bar2_qtype qtype,
		      int user,
		      u64 *pbar2_qoffset,
		      unsigned int *pbar2_qid);

unsigned int qtimer_val(const struct adapter *adap,
			const struct sge_rspq *q);

int t4_init_devlog_params(struct adapter *adapter);
int t4_init_sge_params(struct adapter *adapter);
int t4_init_tp_params(struct adapter *adap, bool sleep_ok);
int t4_filter_field_shift(const struct adapter *adap, int filter_sel);
int t4_init_rss_mode(struct adapter *adap, int mbox);
int t4_init_portinfo(struct port_info *pi, int mbox,
		     int port, int pf, int vf, u8 mac[]);
int t4_port_init(struct adapter *adap, int mbox, int pf, int vf);
int t4_init_port_mirror(struct port_info *pi, u8 mbox, u8 port, u8 pf, u8 vf,
			u16 *mirror_viid);
void t4_fatal_err(struct adapter *adapter);
unsigned int t4_chip_rss_size(struct adapter *adapter);
int t4_config_rss_range(struct adapter *adapter, int mbox, unsigned int viid,
			int start, int n, const u16 *rspq, unsigned int nrspq);
int t4_config_glbl_rss(struct adapter *adapter, int mbox, unsigned int mode,
		       unsigned int flags);
int t4_config_vi_rss(struct adapter *adapter, int mbox, unsigned int viid,
		     unsigned int flags, unsigned int defq);
int t4_read_rss(struct adapter *adapter, u16 *entries);
void t4_read_rss_key(struct adapter *adapter, u32 *key, bool sleep_ok);
void t4_write_rss_key(struct adapter *adap, const u32 *key, int idx,
		      bool sleep_ok);
void t4_read_rss_pf_config(struct adapter *adapter, unsigned int index,
			   u32 *valp, bool sleep_ok);
void t4_read_rss_vf_config(struct adapter *adapter, unsigned int index,
			   u32 *vfl, u32 *vfh, bool sleep_ok);
u32 t4_read_rss_pf_map(struct adapter *adapter, bool sleep_ok);
u32 t4_read_rss_pf_mask(struct adapter *adapter, bool sleep_ok);

unsigned int t4_get_mps_bg_map(struct adapter *adapter, int pidx);
unsigned int t4_get_tp_ch_map(struct adapter *adapter, int pidx);
void t4_pmtx_get_stats(struct adapter *adap, u32 cnt[], u64 cycles[]);
void t4_pmrx_get_stats(struct adapter *adap, u32 cnt[], u64 cycles[]);
int t4_read_cim_ibq(struct adapter *adap, unsigned int qid, u32 *data,
		    size_t n);
int t4_read_cim_obq(struct adapter *adap, unsigned int qid, u32 *data,
		    size_t n);
int t4_cim_read(struct adapter *adap, unsigned int addr, unsigned int n,
		unsigned int *valp);
int t4_cim_write(struct adapter *adap, unsigned int addr, unsigned int n,
		 const unsigned int *valp);
int t4_cim_read_la(struct adapter *adap, u32 *la_buf, unsigned int *wrptr);
void t4_cim_read_pif_la(struct adapter *adap, u32 *pif_req, u32 *pif_rsp,
			unsigned int *pif_req_wrptr,
			unsigned int *pif_rsp_wrptr);
void t4_cim_read_ma_la(struct adapter *adap, u32 *ma_req, u32 *ma_rsp);
void t4_read_cimq_cfg(struct adapter *adap, u16 *base, u16 *size, u16 *thres);
const char *t4_get_port_type_description(enum fw_port_type port_type);
void t4_get_port_stats(struct adapter *adap, int idx, struct port_stats *p);
void t4_get_port_stats_offset(struct adapter *adap, int idx,
			      struct port_stats *stats,
			      struct port_stats *offset);
void t4_get_lb_stats(struct adapter *adap, int idx, struct lb_port_stats *p);
void t4_read_mtu_tbl(struct adapter *adap, u16 *mtus, u8 *mtu_log);
void t4_read_cong_tbl(struct adapter *adap, u16 incr[NMTUS][NCCTRL_WIN]);
void t4_tp_wr_bits_indirect(struct adapter *adap, unsigned int addr,
			    unsigned int mask, unsigned int val);
void t4_tp_read_la(struct adapter *adap, u64 *la_buf, unsigned int *wrptr);
void t4_tp_get_err_stats(struct adapter *adap, struct tp_err_stats *st,
			 bool sleep_ok);
void t4_tp_get_cpl_stats(struct adapter *adap, struct tp_cpl_stats *st,
			 bool sleep_ok);
void t4_tp_get_rdma_stats(struct adapter *adap, struct tp_rdma_stats *st,
			  bool sleep_ok);
void t4_get_usm_stats(struct adapter *adap, struct tp_usm_stats *st,
		      bool sleep_ok);
void t4_tp_get_tcp_stats(struct adapter *adap, struct tp_tcp_stats *v4,
			 struct tp_tcp_stats *v6, bool sleep_ok);
void t4_get_fcoe_stats(struct adapter *adap, unsigned int idx,
		       struct tp_fcoe_stats *st, bool sleep_ok);
void t4_load_mtus(struct adapter *adap, const unsigned short *mtus,
		  const unsigned short *alpha, const unsigned short *beta);

void t4_ulprx_read_la(struct adapter *adap, u32 *la_buf);

void t4_get_chan_txrate(struct adapter *adap, u64 *nic_rate, u64 *ofld_rate);
void t4_mk_filtdelwr(unsigned int ftid, struct fw_filter_wr *wr, int qid);

void t4_wol_magic_enable(struct adapter *adap, unsigned int port,
			 const u8 *addr);
int t4_wol_pat_enable(struct adapter *adap, unsigned int port, unsigned int map,
		      u64 mask0, u64 mask1, unsigned int crc, bool enable);

int t4_fw_hello(struct adapter *adap, unsigned int mbox, unsigned int evt_mbox,
		enum dev_master master, enum dev_state *state);
int t4_fw_bye(struct adapter *adap, unsigned int mbox);
int t4_early_init(struct adapter *adap, unsigned int mbox);
int t4_fw_reset(struct adapter *adap, unsigned int mbox, int reset);
int t4_fixup_host_params(struct adapter *adap, unsigned int page_size,
			  unsigned int cache_line_size);
int t4_fw_initialize(struct adapter *adap, unsigned int mbox);
int t4_query_params(struct adapter *adap, unsigned int mbox, unsigned int pf,
		    unsigned int vf, unsigned int nparams, const u32 *params,
		    u32 *val);
int t4_query_params_ns(struct adapter *adap, unsigned int mbox, unsigned int pf,
		       unsigned int vf, unsigned int nparams, const u32 *params,
		       u32 *val);
int t4_query_params_rw(struct adapter *adap, unsigned int mbox, unsigned int pf,
		       unsigned int vf, unsigned int nparams, const u32 *params,
		       u32 *val, int rw, bool sleep_ok);
int t4_set_params_timeout(struct adapter *adap, unsigned int mbox,
			  unsigned int pf, unsigned int vf,
			  unsigned int nparams, const u32 *params,
			  const u32 *val, int timeout);
int t4_set_params(struct adapter *adap, unsigned int mbox, unsigned int pf,
		  unsigned int vf, unsigned int nparams, const u32 *params,
		  const u32 *val);
int t4_cfg_pfvf(struct adapter *adap, unsigned int mbox, unsigned int pf,
		unsigned int vf, unsigned int txq, unsigned int txq_eth_ctrl,
		unsigned int rxqi, unsigned int rxq, unsigned int tc,
		unsigned int vi, unsigned int cmask, unsigned int pmask,
		unsigned int nexact, unsigned int rcaps, unsigned int wxcaps);
int t4_alloc_vi(struct adapter *adap, unsigned int mbox, unsigned int port,
		unsigned int pf, unsigned int vf, unsigned int nmac, u8 *mac,
		unsigned int *rss_size, u8 *vivld, u8 *vin);
int t4_free_vi(struct adapter *adap, unsigned int mbox,
	       unsigned int pf, unsigned int vf,
	       unsigned int viid);
int t4_set_rxmode(struct adapter *adap, unsigned int mbox, unsigned int viid,
		  unsigned int viid_mirror, int mtu, int promisc, int all_multi,
		  int bcast, int vlanex, bool sleep_ok);
int t4_free_raw_mac_filt(struct adapter *adap, unsigned int viid,
			 const u8 *addr, const u8 *mask, unsigned int idx,
			 u8 lookup_type, u8 port_id, bool sleep_ok);
int t4_free_encap_mac_filt(struct adapter *adap, unsigned int viid, int idx,
			   bool sleep_ok);
int t4_alloc_encap_mac_filt(struct adapter *adap, unsigned int viid,
			    const u8 *addr, const u8 *mask, unsigned int vni,
			    unsigned int vni_mask, u8 dip_hit, u8 lookup_type,
			    bool sleep_ok);
int t4_alloc_raw_mac_filt(struct adapter *adap, unsigned int viid,
			  const u8 *addr, const u8 *mask, unsigned int idx,
			  u8 lookup_type, u8 port_id, bool sleep_ok);
int t4_alloc_mac_filt(struct adapter *adap, unsigned int mbox,
		      unsigned int viid, bool free, unsigned int naddr,
		      const u8 **addr, u16 *idx, u64 *hash, bool sleep_ok);
int t4_free_mac_filt(struct adapter *adap, unsigned int mbox,
		     unsigned int viid, unsigned int naddr,
		     const u8 **addr, bool sleep_ok);
int t4_change_mac(struct adapter *adap, unsigned int mbox, unsigned int viid,
		  int idx, const u8 *addr, bool persist, u8 *smt_idx);
int t4_set_addr_hash(struct adapter *adap, unsigned int mbox, unsigned int viid,
		     bool ucast, u64 vec, bool sleep_ok);
int t4_enable_vi_params(struct adapter *adap, unsigned int mbox,
			unsigned int viid, bool rx_en, bool tx_en, bool dcb_en);
int t4_enable_pi_params(struct adapter *adap, unsigned int mbox,
			struct port_info *pi,
			bool rx_en, bool tx_en, bool dcb_en);
int t4_enable_vi(struct adapter *adap, unsigned int mbox, unsigned int viid,
		 bool rx_en, bool tx_en);
int t4_identify_port(struct adapter *adap, unsigned int mbox, unsigned int viid,
		     unsigned int nblinks);
int t4_mdio_rd(struct adapter *adap, unsigned int mbox, unsigned int phy_addr,
	       unsigned int mmd, unsigned int reg, u16 *valp);
int t4_mdio_wr(struct adapter *adap, unsigned int mbox, unsigned int phy_addr,
	       unsigned int mmd, unsigned int reg, u16 val);
int t4_iq_stop(struct adapter *adap, unsigned int mbox, unsigned int pf,
	       unsigned int vf, unsigned int iqtype, unsigned int iqid,
	       unsigned int fl0id, unsigned int fl1id);
int t4_iq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
	       unsigned int vf, unsigned int iqtype, unsigned int iqid,
	       unsigned int fl0id, unsigned int fl1id);
int t4_eth_eq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
		   unsigned int vf, unsigned int eqid);
int t4_ctrl_eq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
		    unsigned int vf, unsigned int eqid);
int t4_ofld_eq_free(struct adapter *adap, unsigned int mbox, unsigned int pf,
		    unsigned int vf, unsigned int eqid);
int t4_sge_ctxt_flush(struct adapter *adap, unsigned int mbox, int ctxt_type);
int t4_read_sge_dbqtimers(struct adapter *adap, unsigned int ndbqtimers,
			  u16 *dbqtimers);
void t4_handle_get_port_info(struct port_info *pi, const __be64 *rpl);
int t4_update_port_info(struct port_info *pi);
int t4_get_link_params(struct port_info *pi, unsigned int *link_okp,
		       unsigned int *speedp, unsigned int *mtup);
int t4_handle_fw_rpl(struct adapter *adap, const __be64 *rpl);
void t4_db_full(struct adapter *adapter);
void t4_db_dropped(struct adapter *adapter);
int t4_set_trace_filter(struct adapter *adapter, const struct trace_params *tp,
			int filter_index, int enable);
void t4_get_trace_filter(struct adapter *adapter, struct trace_params *tp,
			 int filter_index, int *enabled);
int t4_fwaddrspace_write(struct adapter *adap, unsigned int mbox,
			 u32 addr, u32 val);
void t4_read_pace_tbl(struct adapter *adap, unsigned int pace_vals[NTX_SCHED]);
void t4_get_tx_sched(struct adapter *adap, unsigned int sched,
		     unsigned int *kbps, unsigned int *ipg, bool sleep_ok);
int t4_sge_ctxt_rd(struct adapter *adap, unsigned int mbox, unsigned int cid,
		   enum ctxt_type ctype, u32 *data);
int t4_sge_ctxt_rd_bd(struct adapter *adap, unsigned int cid,
		      enum ctxt_type ctype, u32 *data);
int t4_sched_params(struct adapter *adapter, u8 type, u8 level, u8 mode,
		    u8 rateunit, u8 ratemode, u8 channel, u8 class,
		    u32 minrate, u32 maxrate, u16 weight, u16 pktsize,
		    u16 burstsize);
void t4_sge_decode_idma_state(struct adapter *adapter, int state);
void t4_idma_monitor_init(struct adapter *adapter,
			  struct sge_idma_monitor_state *idma);
void t4_idma_monitor(struct adapter *adapter,
		     struct sge_idma_monitor_state *idma,
		     int hz, int ticks);
int t4_set_vf_mac_acl(struct adapter *adapter, unsigned int vf,
		      unsigned int naddr, u8 *addr);
void t4_tp_pio_read(struct adapter *adap, u32 *buff, u32 nregs,
		    u32 start_index, bool sleep_ok);
void t4_tp_tm_pio_read(struct adapter *adap, u32 *buff, u32 nregs,
		       u32 start_index, bool sleep_ok);
void t4_tp_mib_read(struct adapter *adap, u32 *buff, u32 nregs,
		    u32 start_index, bool sleep_ok);

void t4_uld_mem_free(struct adapter *adap);
int t4_uld_mem_alloc(struct adapter *adap);
void t4_uld_clean_up(struct adapter *adap);
void t4_register_netevent_notifier(void);
int t4_i2c_rd(struct adapter *adap, unsigned int mbox, int port,
	      unsigned int devid, unsigned int offset,
	      unsigned int len, u8 *buf);
int t4_load_boot(struct adapter *adap, u8 *boot_data,
		 unsigned int boot_addr, unsigned int size);
int t4_load_bootcfg(struct adapter *adap,
		    const u8 *cfg_data, unsigned int size);
void free_rspq_fl(struct adapter *adap, struct sge_rspq *rq, struct sge_fl *fl);
void free_tx_desc(struct adapter *adap, struct sge_txq *q,
		  unsigned int n, bool unmap);
void cxgb4_eosw_txq_free_desc(struct adapter *adap, struct sge_eosw_txq *txq,
			      u32 ndesc);
int cxgb4_ethofld_send_flowc(struct net_device *dev, u32 eotid, u32 tc);
void cxgb4_ethofld_restart(struct tasklet_struct *t);
int cxgb4_ethofld_rx_handler(struct sge_rspq *q, const __be64 *rsp,
			     const struct pkt_gl *si);
void free_txq(struct adapter *adap, struct sge_txq *q);
void cxgb4_reclaim_completed_tx(struct adapter *adap,
				struct sge_txq *q, bool unmap);
int cxgb4_map_skb(struct device *dev, const struct sk_buff *skb,
		  dma_addr_t *addr);
void cxgb4_inline_tx_skb(const struct sk_buff *skb, const struct sge_txq *q,
			 void *pos);
void cxgb4_write_sgl(const struct sk_buff *skb, struct sge_txq *q,
		     struct ulptx_sgl *sgl, u64 *end, unsigned int start,
		     const dma_addr_t *addr);
void cxgb4_write_partial_sgl(const struct sk_buff *skb, struct sge_txq *q,
			     struct ulptx_sgl *sgl, u64 *end,
			     const dma_addr_t *addr, u32 start, u32 send_len);
void cxgb4_ring_tx_db(struct adapter *adap, struct sge_txq *q, int n);
int t4_set_vlan_acl(struct adapter *adap, unsigned int mbox, unsigned int vf,
		    u16 vlan);
int cxgb4_dcb_enabled(const struct net_device *dev);

int cxgb4_thermal_init(struct adapter *adap);
int cxgb4_thermal_remove(struct adapter *adap);
int cxgb4_set_msix_aff(struct adapter *adap, unsigned short vec,
		       cpumask_var_t *aff_mask, int idx);
void cxgb4_clear_msix_aff(unsigned short vec, cpumask_var_t aff_mask);

int cxgb4_change_mac(struct port_info *pi, unsigned int viid,
		     int *tcam_idx, const u8 *addr,
		     bool persistent, u8 *smt_idx);

int cxgb4_alloc_mac_filt(struct adapter *adap, unsigned int viid,
			 bool free, unsigned int naddr,
			 const u8 **addr, u16 *idx,
			 u64 *hash, bool sleep_ok);
int cxgb4_free_mac_filt(struct adapter *adap, unsigned int viid,
			unsigned int naddr, const u8 **addr, bool sleep_ok);
int cxgb4_init_mps_ref_entries(struct adapter *adap);
void cxgb4_free_mps_ref_entries(struct adapter *adap);
int cxgb4_alloc_encap_mac_filt(struct adapter *adap, unsigned int viid,
			       const u8 *addr, const u8 *mask,
			       unsigned int vni, unsigned int vni_mask,
			       u8 dip_hit, u8 lookup_type, bool sleep_ok);
int cxgb4_free_encap_mac_filt(struct adapter *adap, unsigned int viid,
			      int idx, bool sleep_ok);
int cxgb4_free_raw_mac_filt(struct adapter *adap,
			    unsigned int viid,
			    const u8 *addr,
			    const u8 *mask,
			    unsigned int idx,
			    u8 lookup_type,
			    u8 port_id,
			    bool sleep_ok);
int cxgb4_alloc_raw_mac_filt(struct adapter *adap,
			     unsigned int viid,
			     const u8 *addr,
			     const u8 *mask,
			     unsigned int idx,
			     u8 lookup_type,
			     u8 port_id,
			     bool sleep_ok);
int cxgb4_update_mac_filt(struct port_info *pi, unsigned int viid,
			  int *tcam_idx, const u8 *addr,
			  bool persistent, u8 *smt_idx);
int cxgb4_get_msix_idx_from_bmap(struct adapter *adap);
void cxgb4_free_msix_idx_in_bmap(struct adapter *adap, u32 msix_idx);
int cxgb_open(struct net_device *dev);
int cxgb_close(struct net_device *dev);
void cxgb4_enable_rx(struct adapter *adap, struct sge_rspq *q);
void cxgb4_quiesce_rx(struct sge_rspq *q);
int cxgb4_port_mirror_alloc(struct net_device *dev);
void cxgb4_port_mirror_free(struct net_device *dev);
#if IS_ENABLED(CONFIG_CHELSIO_TLS_DEVICE)
int cxgb4_set_ktls_feature(struct adapter *adap, bool enable);
#endif
#endif /* __CXGB4_H__ */
