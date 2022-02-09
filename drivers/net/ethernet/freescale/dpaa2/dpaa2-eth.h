/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* Copyright 2014-2016 Freescale Semiconductor Inc.
 * Copyright 2016-2020 NXP
 */

#ifndef __DPAA2_ETH_H
#define __DPAA2_ETH_H

#include <linux/dcbnl.h>
#include <linux/netdevice.h>
#include <linux/if_vlan.h>
#include <linux/fsl/mc.h>
#include <linux/net_tstamp.h>
#include <net/devlink.h>

#include <soc/fsl/dpaa2-io.h>
#include <soc/fsl/dpaa2-fd.h>
#include "dpni.h"
#include "dpni-cmd.h"

#include "dpaa2-eth-trace.h"
#include "dpaa2-eth-debugfs.h"
#include "dpaa2-mac.h"

#define DPAA2_WRIOP_VERSION(x, y, z) ((x) << 10 | (y) << 5 | (z) << 0)

#define DPAA2_ETH_STORE_SIZE		16

/* Maximum number of scatter-gather entries in an ingress frame,
 * considering the maximum receive frame size is 64K
 */
#define DPAA2_ETH_MAX_SG_ENTRIES	((64 * 1024) / DPAA2_ETH_RX_BUF_SIZE)

/* Maximum acceptable MTU value. It is in direct relation with the hardware
 * enforced Max Frame Length (currently 10k).
 */
#define DPAA2_ETH_MFL			(10 * 1024)
#define DPAA2_ETH_MAX_MTU		(DPAA2_ETH_MFL - VLAN_ETH_HLEN)
/* Convert L3 MTU to L2 MFL */
#define DPAA2_ETH_L2_MAX_FRM(mtu)	((mtu) + VLAN_ETH_HLEN)

/* Set the taildrop threshold (in bytes) to allow the enqueue of a large
 * enough number of jumbo frames in the Rx queues (length of the current
 * frame is not taken into account when making the taildrop decision)
 */
#define DPAA2_ETH_FQ_TAILDROP_THRESH	(1024 * 1024)

/* Maximum burst size value for Tx shaping */
#define DPAA2_ETH_MAX_BURST_SIZE	0xF7FF

/* Maximum number of Tx confirmation frames to be processed
 * in a single NAPI call
 */
#define DPAA2_ETH_TXCONF_PER_NAPI	256

/* Buffer qouta per channel. We want to keep in check number of ingress frames
 * in flight: for small sized frames, congestion group taildrop may kick in
 * first; for large sizes, Rx FQ taildrop threshold will ensure only a
 * reasonable number of frames will be pending at any given time.
 * Ingress frame drop due to buffer pool depletion should be a corner case only
 */
#define DPAA2_ETH_NUM_BUFS		1280
#define DPAA2_ETH_REFILL_THRESH \
	(DPAA2_ETH_NUM_BUFS - DPAA2_ETH_BUFS_PER_CMD)

/* Congestion group taildrop threshold: number of frames allowed to accumulate
 * at any moment in a group of Rx queues belonging to the same traffic class.
 * Choose value such that we don't risk depleting the buffer pool before the
 * taildrop kicks in
 */
#define DPAA2_ETH_CG_TAILDROP_THRESH(priv)				\
	(1024 * dpaa2_eth_queue_count(priv) / dpaa2_eth_tc_count(priv))

/* Congestion group notification threshold: when this many frames accumulate
 * on the Rx queues belonging to the same TC, the MAC is instructed to send
 * PFC frames for that TC.
 * When number of pending frames drops below exit threshold transmission of
 * PFC frames is stopped.
 */
#define DPAA2_ETH_CN_THRESH_ENTRY(priv) \
	(DPAA2_ETH_CG_TAILDROP_THRESH(priv) / 2)
#define DPAA2_ETH_CN_THRESH_EXIT(priv) \
	(DPAA2_ETH_CN_THRESH_ENTRY(priv) * 3 / 4)

/* Maximum number of buffers that can be acquired/released through a single
 * QBMan command
 */
#define DPAA2_ETH_BUFS_PER_CMD		7

/* Hardware requires alignment for ingress/egress buffer addresses */
#define DPAA2_ETH_TX_BUF_ALIGN		64

#define DPAA2_ETH_RX_BUF_RAW_SIZE	PAGE_SIZE
#define DPAA2_ETH_RX_BUF_TAILROOM \
	SKB_DATA_ALIGN(sizeof(struct skb_shared_info))
#define DPAA2_ETH_RX_BUF_SIZE \
	(DPAA2_ETH_RX_BUF_RAW_SIZE - DPAA2_ETH_RX_BUF_TAILROOM)

/* Hardware annotation area in RX/TX buffers */
#define DPAA2_ETH_RX_HWA_SIZE		64
#define DPAA2_ETH_TX_HWA_SIZE		128

/* PTP nominal frequency 1GHz */
#define DPAA2_PTP_CLK_PERIOD_NS		1

/* Due to a limitation in WRIOP 1.0.0, the RX buffer data must be aligned
 * to 256B. For newer revisions, the requirement is only for 64B alignment
 */
#define DPAA2_ETH_RX_BUF_ALIGN_REV1	256
#define DPAA2_ETH_RX_BUF_ALIGN		64

/* We are accommodating a skb backpointer and some S/G info
 * in the frame's software annotation. The hardware
 * options are either 0 or 64, so we choose the latter.
 */
#define DPAA2_ETH_SWA_SIZE		64

/* We store different information in the software annotation area of a Tx frame
 * based on what type of frame it is
 */
enum dpaa2_eth_swa_type {
	DPAA2_ETH_SWA_SINGLE,
	DPAA2_ETH_SWA_SG,
	DPAA2_ETH_SWA_XDP,
	DPAA2_ETH_SWA_SW_TSO,
};

/* Must keep this struct smaller than DPAA2_ETH_SWA_SIZE */
struct dpaa2_eth_swa {
	enum dpaa2_eth_swa_type type;
	union {
		struct {
			struct sk_buff *skb;
			int sgt_size;
		} single;
		struct {
			struct sk_buff *skb;
			struct scatterlist *scl;
			int num_sg;
			int sgt_size;
		} sg;
		struct {
			int dma_size;
			struct xdp_frame *xdpf;
		} xdp;
		struct {
			struct sk_buff *skb;
			int num_sg;
			int sgt_size;
			int is_last_fd;
		} tso;
	};
};

/* Annotation valid bits in FD FRC */
#define DPAA2_FD_FRC_FASV		0x8000
#define DPAA2_FD_FRC_FAEADV		0x4000
#define DPAA2_FD_FRC_FAPRV		0x2000
#define DPAA2_FD_FRC_FAIADV		0x1000
#define DPAA2_FD_FRC_FASWOV		0x0800
#define DPAA2_FD_FRC_FAICFDV		0x0400

/* Error bits in FD CTRL */
#define DPAA2_FD_RX_ERR_MASK		(FD_CTRL_SBE | FD_CTRL_FAERR)
#define DPAA2_FD_TX_ERR_MASK		(FD_CTRL_UFD	| \
					 FD_CTRL_SBE	| \
					 FD_CTRL_FSE	| \
					 FD_CTRL_FAERR)

/* Annotation bits in FD CTRL */
#define DPAA2_FD_CTRL_ASAL		0x00020000	/* ASAL = 128B */

/* Frame annotation status */
struct dpaa2_fas {
	u8 reserved;
	u8 ppid;
	__le16 ifpid;
	__le32 status;
};

/* Frame annotation status word is located in the first 8 bytes
 * of the buffer's hardware annoatation area
 */
#define DPAA2_FAS_OFFSET		0
#define DPAA2_FAS_SIZE			(sizeof(struct dpaa2_fas))

/* Timestamp is located in the next 8 bytes of the buffer's
 * hardware annotation area
 */
#define DPAA2_TS_OFFSET			0x8

/* Frame annotation parse results */
struct dpaa2_fapr {
	/* 64-bit word 1 */
	__le32 faf_lo;
	__le16 faf_ext;
	__le16 nxt_hdr;
	/* 64-bit word 2 */
	__le64 faf_hi;
	/* 64-bit word 3 */
	u8 last_ethertype_offset;
	u8 vlan_tci_offset_n;
	u8 vlan_tci_offset_1;
	u8 llc_snap_offset;
	u8 eth_offset;
	u8 ip1_pid_offset;
	u8 shim_offset_2;
	u8 shim_offset_1;
	/* 64-bit word 4 */
	u8 l5_offset;
	u8 l4_offset;
	u8 gre_offset;
	u8 l3_offset_n;
	u8 l3_offset_1;
	u8 mpls_offset_n;
	u8 mpls_offset_1;
	u8 pppoe_offset;
	/* 64-bit word 5 */
	__le16 running_sum;
	__le16 gross_running_sum;
	u8 ipv6_frag_offset;
	u8 nxt_hdr_offset;
	u8 routing_hdr_offset_2;
	u8 routing_hdr_offset_1;
	/* 64-bit word 6 */
	u8 reserved[5]; /* Soft-parsing context */
	u8 ip_proto_offset_n;
	u8 nxt_hdr_frag_offset;
	u8 parse_error_code;
};

#define DPAA2_FAPR_OFFSET		0x10
#define DPAA2_FAPR_SIZE			sizeof((struct dpaa2_fapr))

/* Frame annotation egress action descriptor */
#define DPAA2_FAEAD_OFFSET		0x58

struct dpaa2_faead {
	__le32 conf_fqid;
	__le32 ctrl;
};

#define DPAA2_FAEAD_A2V			0x20000000
#define DPAA2_FAEAD_A4V			0x08000000
#define DPAA2_FAEAD_UPDV		0x00001000
#define DPAA2_FAEAD_EBDDV		0x00002000
#define DPAA2_FAEAD_UPD			0x00000010

struct ptp_tstamp {
	u16 sec_msb;
	u32 sec_lsb;
	u32 nsec;
};

static inline void ns_to_ptp_tstamp(struct ptp_tstamp *tstamp, u64 ns)
{
	u64 sec, nsec;

	sec = ns;
	nsec = do_div(sec, 1000000000);

	tstamp->sec_lsb = sec & 0xFFFFFFFF;
	tstamp->sec_msb = (sec >> 32) & 0xFFFF;
	tstamp->nsec = nsec;
}

/* Accessors for the hardware annotation fields that we use */
static inline void *dpaa2_get_hwa(void *buf_addr, bool swa)
{
	return buf_addr + (swa ? DPAA2_ETH_SWA_SIZE : 0);
}

static inline struct dpaa2_fas *dpaa2_get_fas(void *buf_addr, bool swa)
{
	return dpaa2_get_hwa(buf_addr, swa) + DPAA2_FAS_OFFSET;
}

static inline __le64 *dpaa2_get_ts(void *buf_addr, bool swa)
{
	return dpaa2_get_hwa(buf_addr, swa) + DPAA2_TS_OFFSET;
}

static inline struct dpaa2_fapr *dpaa2_get_fapr(void *buf_addr, bool swa)
{
	return dpaa2_get_hwa(buf_addr, swa) + DPAA2_FAPR_OFFSET;
}

static inline struct dpaa2_faead *dpaa2_get_faead(void *buf_addr, bool swa)
{
	return dpaa2_get_hwa(buf_addr, swa) + DPAA2_FAEAD_OFFSET;
}

/* Error and status bits in the frame annotation status word */
/* Debug frame, otherwise supposed to be discarded */
#define DPAA2_FAS_DISC			0x80000000
/* MACSEC frame */
#define DPAA2_FAS_MS			0x40000000
#define DPAA2_FAS_PTP			0x08000000
/* Ethernet multicast frame */
#define DPAA2_FAS_MC			0x04000000
/* Ethernet broadcast frame */
#define DPAA2_FAS_BC			0x02000000
#define DPAA2_FAS_KSE			0x00040000
#define DPAA2_FAS_EOFHE			0x00020000
#define DPAA2_FAS_MNLE			0x00010000
#define DPAA2_FAS_TIDE			0x00008000
#define DPAA2_FAS_PIEE			0x00004000
/* Frame length error */
#define DPAA2_FAS_FLE			0x00002000
/* Frame physical error */
#define DPAA2_FAS_FPE			0x00001000
#define DPAA2_FAS_PTE			0x00000080
#define DPAA2_FAS_ISP			0x00000040
#define DPAA2_FAS_PHE			0x00000020
#define DPAA2_FAS_BLE			0x00000010
/* L3 csum validation performed */
#define DPAA2_FAS_L3CV			0x00000008
/* L3 csum error */
#define DPAA2_FAS_L3CE			0x00000004
/* L4 csum validation performed */
#define DPAA2_FAS_L4CV			0x00000002
/* L4 csum error */
#define DPAA2_FAS_L4CE			0x00000001
/* Possible errors on the ingress path */
#define DPAA2_FAS_RX_ERR_MASK		(DPAA2_FAS_KSE		| \
					 DPAA2_FAS_EOFHE	| \
					 DPAA2_FAS_MNLE		| \
					 DPAA2_FAS_TIDE		| \
					 DPAA2_FAS_PIEE		| \
					 DPAA2_FAS_FLE		| \
					 DPAA2_FAS_FPE		| \
					 DPAA2_FAS_PTE		| \
					 DPAA2_FAS_ISP		| \
					 DPAA2_FAS_PHE		| \
					 DPAA2_FAS_BLE		| \
					 DPAA2_FAS_L3CE		| \
					 DPAA2_FAS_L4CE)

/* Time in milliseconds between link state updates */
#define DPAA2_ETH_LINK_STATE_REFRESH	1000

/* Number of times to retry a frame enqueue before giving up.
 * Value determined empirically, in order to minimize the number
 * of frames dropped on Tx
 */
#define DPAA2_ETH_ENQUEUE_RETRIES	10

/* Number of times to retry DPIO portal operations while waiting
 * for portal to finish executing current command and become
 * available. We want to avoid being stuck in a while loop in case
 * hardware becomes unresponsive, but not give up too easily if
 * the portal really is busy for valid reasons
 */
#define DPAA2_ETH_SWP_BUSY_RETRIES	1000

/* Driver statistics, other than those in struct rtnl_link_stats64.
 * These are usually collected per-CPU and aggregated by ethtool.
 */
struct dpaa2_eth_drv_stats {
	__u64	tx_conf_frames;
	__u64	tx_conf_bytes;
	__u64	tx_sg_frames;
	__u64	tx_sg_bytes;
	__u64	tx_tso_frames;
	__u64	tx_tso_bytes;
	__u64	rx_sg_frames;
	__u64	rx_sg_bytes;
	/* Linear skbs sent as a S/G FD due to insufficient headroom */
	__u64	tx_converted_sg_frames;
	__u64	tx_converted_sg_bytes;
	/* Enqueues retried due to portal busy */
	__u64	tx_portal_busy;
};

/* Per-FQ statistics */
struct dpaa2_eth_fq_stats {
	/* Number of frames received on this queue */
	__u64 frames;
};

/* Per-channel statistics */
struct dpaa2_eth_ch_stats {
	/* Volatile dequeues retried due to portal busy */
	__u64 dequeue_portal_busy;
	/* Pull errors */
	__u64 pull_err;
	/* Number of CDANs; useful to estimate avg NAPI len */
	__u64 cdan;
	/* XDP counters */
	__u64 xdp_drop;
	__u64 xdp_tx;
	__u64 xdp_tx_err;
	__u64 xdp_redirect;
	/* Must be last, does not show up in ethtool stats */
	__u64 frames;
	__u64 frames_per_cdan;
	__u64 bytes_per_cdan;
};

#define DPAA2_ETH_CH_STATS	7

/* Maximum number of queues associated with a DPNI */
#define DPAA2_ETH_MAX_TCS		8
#define DPAA2_ETH_MAX_RX_QUEUES_PER_TC	16
#define DPAA2_ETH_MAX_RX_QUEUES		\
	(DPAA2_ETH_MAX_RX_QUEUES_PER_TC * DPAA2_ETH_MAX_TCS)
#define DPAA2_ETH_MAX_TX_QUEUES		16
#define DPAA2_ETH_MAX_RX_ERR_QUEUES	1
#define DPAA2_ETH_MAX_QUEUES		(DPAA2_ETH_MAX_RX_QUEUES + \
					DPAA2_ETH_MAX_TX_QUEUES + \
					DPAA2_ETH_MAX_RX_ERR_QUEUES)
#define DPAA2_ETH_MAX_NETDEV_QUEUES	\
	(DPAA2_ETH_MAX_TX_QUEUES * DPAA2_ETH_MAX_TCS)

#define DPAA2_ETH_MAX_DPCONS		16

enum dpaa2_eth_fq_type {
	DPAA2_RX_FQ = 0,
	DPAA2_TX_CONF_FQ,
	DPAA2_RX_ERR_FQ
};

struct dpaa2_eth_priv;

struct dpaa2_eth_xdp_fds {
	struct dpaa2_fd fds[DEV_MAP_BULK_SIZE];
	ssize_t num;
};

struct dpaa2_eth_fq {
	u32 fqid;
	u32 tx_qdbin;
	u32 tx_fqid[DPAA2_ETH_MAX_TCS];
	u16 flowid;
	u8 tc;
	int target_cpu;
	u32 dq_frames;
	u32 dq_bytes;
	struct dpaa2_eth_channel *channel;
	enum dpaa2_eth_fq_type type;

	void (*consume)(struct dpaa2_eth_priv *priv,
			struct dpaa2_eth_channel *ch,
			const struct dpaa2_fd *fd,
			struct dpaa2_eth_fq *fq);
	struct dpaa2_eth_fq_stats stats;

	struct dpaa2_eth_xdp_fds xdp_redirect_fds;
	struct dpaa2_eth_xdp_fds xdp_tx_fds;
};

struct dpaa2_eth_ch_xdp {
	struct bpf_prog *prog;
	unsigned int res;
};

struct dpaa2_eth_channel {
	struct dpaa2_io_notification_ctx nctx;
	struct fsl_mc_device *dpcon;
	int dpcon_id;
	int ch_id;
	struct napi_struct napi;
	struct dpaa2_io *dpio;
	struct dpaa2_io_store *store;
	struct dpaa2_eth_priv *priv;
	int buf_count;
	struct dpaa2_eth_ch_stats stats;
	struct dpaa2_eth_ch_xdp xdp;
	struct xdp_rxq_info xdp_rxq;
	struct list_head *rx_list;

	/* Buffers to be recycled back in the buffer pool */
	u64 recycled_bufs[DPAA2_ETH_BUFS_PER_CMD];
	int recycled_bufs_cnt;
};

struct dpaa2_eth_dist_fields {
	u64 rxnfc_field;
	enum net_prot cls_prot;
	int cls_field;
	int size;
	u64 id;
};

struct dpaa2_eth_cls_rule {
	struct ethtool_rx_flow_spec fs;
	u8 in_use;
};

#define DPAA2_ETH_SGT_CACHE_SIZE	256
struct dpaa2_eth_sgt_cache {
	void *buf[DPAA2_ETH_SGT_CACHE_SIZE];
	u16 count;
};

struct dpaa2_eth_trap_item {
	void *trap_ctx;
};

struct dpaa2_eth_trap_data {
	struct dpaa2_eth_trap_item *trap_items_arr;
	struct dpaa2_eth_priv *priv;
};

#define DPAA2_ETH_SG_ENTRIES_MAX	(PAGE_SIZE / sizeof(struct scatterlist))

#define DPAA2_ETH_DEFAULT_COPYBREAK	512

#define DPAA2_ETH_ENQUEUE_MAX_FDS	200
struct dpaa2_eth_fds {
	struct dpaa2_fd array[DPAA2_ETH_ENQUEUE_MAX_FDS];
};

/* Driver private data */
struct dpaa2_eth_priv {
	struct net_device *net_dev;

	u8 num_fqs;
	struct dpaa2_eth_fq fq[DPAA2_ETH_MAX_QUEUES];
	int (*enqueue)(struct dpaa2_eth_priv *priv,
		       struct dpaa2_eth_fq *fq,
		       struct dpaa2_fd *fd, u8 prio,
		       u32 num_frames,
		       int *frames_enqueued);

	u8 num_channels;
	struct dpaa2_eth_channel *channel[DPAA2_ETH_MAX_DPCONS];
	struct dpaa2_eth_sgt_cache __percpu *sgt_cache;

	struct dpni_attr dpni_attrs;
	u16 dpni_ver_major;
	u16 dpni_ver_minor;
	u16 tx_data_offset;

	struct fsl_mc_device *dpbp_dev;
	u16 rx_buf_size;
	u16 bpid;
	struct iommu_domain *iommu_domain;

	enum hwtstamp_tx_types tx_tstamp_type;	/* Tx timestamping type */
	bool rx_tstamp;				/* Rx timestamping enabled */

	u16 tx_qdid;
	struct fsl_mc_io *mc_io;
	/* Cores which have an affine DPIO/DPCON.
	 * This is the cpu set on which Rx and Tx conf frames are processed
	 */
	struct cpumask dpio_cpumask;

	/* Standard statistics */
	struct rtnl_link_stats64 __percpu *percpu_stats;
	/* Extra stats, in addition to the ones known by the kernel */
	struct dpaa2_eth_drv_stats __percpu *percpu_extras;

	u16 mc_token;
	u8 rx_fqtd_enabled;
	u8 rx_cgtd_enabled;

	struct dpni_link_state link_state;
	bool do_link_poll;
	struct task_struct *poll_thread;

	/* enabled ethtool hashing bits */
	u64 rx_hash_fields;
	u64 rx_cls_fields;
	struct dpaa2_eth_cls_rule *cls_rules;
	u8 rx_cls_enabled;
	u8 vlan_cls_enabled;
	u8 pfc_enabled;
#ifdef CONFIG_FSL_DPAA2_ETH_DCB
	u8 dcbx_mode;
	struct ieee_pfc pfc;
#endif
	struct bpf_prog *xdp_prog;
#ifdef CONFIG_DEBUG_FS
	struct dpaa2_debugfs dbg;
#endif

	struct dpaa2_mac *mac;
	struct workqueue_struct	*dpaa2_ptp_wq;
	struct work_struct	tx_onestep_tstamp;
	struct sk_buff_head	tx_skbs;
	/* The one-step timestamping configuration on hardware
	 * registers could only be done when no one-step
	 * timestamping frames are in flight. So we use a mutex
	 * lock here to make sure the lock is released by last
	 * one-step timestamping packet through TX confirmation
	 * queue before transmit current packet.
	 */
	struct mutex		onestep_tstamp_lock;
	struct devlink *devlink;
	struct dpaa2_eth_trap_data *trap_data;
	struct devlink_port devlink_port;

	u32 rx_copybreak;

	struct dpaa2_eth_fds __percpu *fd;
};

struct dpaa2_eth_devlink_priv {
	struct dpaa2_eth_priv *dpaa2_priv;
};

#define TX_TSTAMP		0x1
#define TX_TSTAMP_ONESTEP_SYNC	0x2

#define DPAA2_RXH_SUPPORTED	(RXH_L2DA | RXH_VLAN | RXH_L3_PROTO \
				| RXH_IP_SRC | RXH_IP_DST | RXH_L4_B_0_1 \
				| RXH_L4_B_2_3)

/* default Rx hash options, set during probing */
#define DPAA2_RXH_DEFAULT	(RXH_L3_PROTO | RXH_IP_SRC | RXH_IP_DST | \
				 RXH_L4_B_0_1 | RXH_L4_B_2_3)

#define dpaa2_eth_hash_enabled(priv)	\
	((priv)->dpni_attrs.num_queues > 1)

/* Required by struct dpni_rx_tc_dist_cfg::key_cfg_iova */
#define DPAA2_CLASSIFIER_DMA_SIZE 256

extern const struct ethtool_ops dpaa2_ethtool_ops;
extern int dpaa2_phc_index;
extern struct ptp_qoriq *dpaa2_ptp;

static inline int dpaa2_eth_cmp_dpni_ver(struct dpaa2_eth_priv *priv,
					 u16 ver_major, u16 ver_minor)
{
	if (priv->dpni_ver_major == ver_major)
		return priv->dpni_ver_minor - ver_minor;
	return priv->dpni_ver_major - ver_major;
}

/* Minimum firmware version that supports a more flexible API
 * for configuring the Rx flow hash key
 */
#define DPNI_RX_DIST_KEY_VER_MAJOR	7
#define DPNI_RX_DIST_KEY_VER_MINOR	5

#define dpaa2_eth_has_legacy_dist(priv)					\
	(dpaa2_eth_cmp_dpni_ver((priv), DPNI_RX_DIST_KEY_VER_MAJOR,	\
				DPNI_RX_DIST_KEY_VER_MINOR) < 0)

#define dpaa2_eth_fs_enabled(priv)	\
	(!((priv)->dpni_attrs.options & DPNI_OPT_NO_FS))

#define dpaa2_eth_fs_mask_enabled(priv)	\
	((priv)->dpni_attrs.options & DPNI_OPT_HAS_KEY_MASKING)

#define dpaa2_eth_fs_count(priv)        \
	((priv)->dpni_attrs.fs_entries)

#define dpaa2_eth_tc_count(priv)	\
	((priv)->dpni_attrs.num_tcs)

/* We have exactly one {Rx, Tx conf} queue per channel */
#define dpaa2_eth_queue_count(priv)     \
	((priv)->num_channels)

enum dpaa2_eth_rx_dist {
	DPAA2_ETH_RX_DIST_HASH,
	DPAA2_ETH_RX_DIST_CLS
};

/* Unique IDs for the supported Rx classification header fields */
#define DPAA2_ETH_DIST_ETHDST		BIT(0)
#define DPAA2_ETH_DIST_ETHSRC		BIT(1)
#define DPAA2_ETH_DIST_ETHTYPE		BIT(2)
#define DPAA2_ETH_DIST_VLAN		BIT(3)
#define DPAA2_ETH_DIST_IPSRC		BIT(4)
#define DPAA2_ETH_DIST_IPDST		BIT(5)
#define DPAA2_ETH_DIST_IPPROTO		BIT(6)
#define DPAA2_ETH_DIST_L4SRC		BIT(7)
#define DPAA2_ETH_DIST_L4DST		BIT(8)
#define DPAA2_ETH_DIST_ALL		(~0ULL)

#define DPNI_PAUSE_VER_MAJOR		7
#define DPNI_PAUSE_VER_MINOR		13
#define dpaa2_eth_has_pause_support(priv)			\
	(dpaa2_eth_cmp_dpni_ver((priv), DPNI_PAUSE_VER_MAJOR,	\
				DPNI_PAUSE_VER_MINOR) >= 0)

static inline bool dpaa2_eth_tx_pause_enabled(u64 link_options)
{
	return !!(link_options & DPNI_LINK_OPT_PAUSE) ^
	       !!(link_options & DPNI_LINK_OPT_ASYM_PAUSE);
}

static inline bool dpaa2_eth_rx_pause_enabled(u64 link_options)
{
	return !!(link_options & DPNI_LINK_OPT_PAUSE);
}

static inline unsigned int dpaa2_eth_needed_headroom(struct sk_buff *skb)
{
	unsigned int headroom = DPAA2_ETH_SWA_SIZE;

	/* If we don't have an skb (e.g. XDP buffer), we only need space for
	 * the software annotation area
	 */
	if (!skb)
		return headroom;

	/* For non-linear skbs we have no headroom requirement, as we build a
	 * SG frame with a newly allocated SGT buffer
	 */
	if (skb_is_nonlinear(skb))
		return 0;

	/* If we have Tx timestamping, need 128B hardware annotation */
	if (skb->cb[0])
		headroom += DPAA2_ETH_TX_HWA_SIZE;

	return headroom;
}

/* Extra headroom space requested to hardware, in order to make sure there's
 * no realloc'ing in forwarding scenarios
 */
static inline unsigned int dpaa2_eth_rx_head_room(struct dpaa2_eth_priv *priv)
{
	return priv->tx_data_offset - DPAA2_ETH_RX_HWA_SIZE;
}

static inline bool dpaa2_eth_is_type_phy(struct dpaa2_eth_priv *priv)
{
	if (priv->mac &&
	    (priv->mac->attr.link_type == DPMAC_LINK_TYPE_PHY ||
	     priv->mac->attr.link_type == DPMAC_LINK_TYPE_BACKPLANE))
		return true;

	return false;
}

static inline bool dpaa2_eth_has_mac(struct dpaa2_eth_priv *priv)
{
	return priv->mac ? true : false;
}

int dpaa2_eth_set_hash(struct net_device *net_dev, u64 flags);
int dpaa2_eth_set_cls(struct net_device *net_dev, u64 key);
int dpaa2_eth_cls_key_size(u64 key);
int dpaa2_eth_cls_fld_off(int prot, int field);
void dpaa2_eth_cls_trim_rule(void *key_mem, u64 fields);

void dpaa2_eth_set_rx_taildrop(struct dpaa2_eth_priv *priv,
			       bool tx_pause, bool pfc);

extern const struct dcbnl_rtnl_ops dpaa2_eth_dcbnl_ops;

int dpaa2_eth_dl_alloc(struct dpaa2_eth_priv *priv);
void dpaa2_eth_dl_free(struct dpaa2_eth_priv *priv);

void dpaa2_eth_dl_register(struct dpaa2_eth_priv *priv);
void dpaa2_eth_dl_unregister(struct dpaa2_eth_priv *priv);

int dpaa2_eth_dl_port_add(struct dpaa2_eth_priv *priv);
void dpaa2_eth_dl_port_del(struct dpaa2_eth_priv *priv);

int dpaa2_eth_dl_traps_register(struct dpaa2_eth_priv *priv);
void dpaa2_eth_dl_traps_unregister(struct dpaa2_eth_priv *priv);

struct dpaa2_eth_trap_item *dpaa2_eth_dl_get_trap(struct dpaa2_eth_priv *priv,
						  struct dpaa2_fapr *fapr);
#endif	/* __DPAA2_H */
