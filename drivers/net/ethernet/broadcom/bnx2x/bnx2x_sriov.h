/* bnx2x_sriov.h: Broadcom Everest network driver.
 *
 * Copyright 2009-2013 Broadcom Corporation
 *
 * Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2, available
 * at http://www.gnu.org/licenses/old-licenses/gpl-2.0.html (the "GPL").
 *
 * Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a
 * license other than the GPL, without Broadcom's express prior written
 * consent.
 *
 * Maintained by: Eilon Greenstein <eilong@broadcom.com>
 * Written by: Shmulik Ravid <shmulikr@broadcom.com>
 *	       Ariel Elior <ariele@broadcom.com>
 */
#ifndef BNX2X_SRIOV_H
#define BNX2X_SRIOV_H

#include "bnx2x_vfpf.h"
#include "bnx2x.h"

enum sample_bulletin_result {
	   PFVF_BULLETIN_UNCHANGED,
	   PFVF_BULLETIN_UPDATED,
	   PFVF_BULLETIN_CRC_ERR
};

#ifdef CONFIG_BNX2X_SRIOV

extern struct workqueue_struct *bnx2x_iov_wq;

/* The bnx2x device structure holds vfdb structure described below.
 * The VF array is indexed by the relative vfid.
 */
#define BNX2X_VF_MAX_QUEUES		16
#define BNX2X_VF_MAX_TPA_AGG_QUEUES	8

struct bnx2x_sriov {
	u32 first_vf_in_pf;

	/* standard SRIOV capability fields, mostly for debugging */
	int pos;		/* capability position */
	int nres;		/* number of resources */
	u32 cap;		/* SR-IOV Capabilities */
	u16 ctrl;		/* SR-IOV Control */
	u16 total;		/* total VFs associated with the PF */
	u16 initial;		/* initial VFs associated with the PF */
	u16 nr_virtfn;		/* number of VFs available */
	u16 offset;		/* first VF Routing ID offset */
	u16 stride;		/* following VF stride */
	u32 pgsz;		/* page size for BAR alignment */
	u8 link;		/* Function Dependency Link */
};

/* bars */
struct bnx2x_vf_bar {
	u64 bar;
	u32 size;
};

struct bnx2x_vf_bar_info {
	struct bnx2x_vf_bar bars[PCI_SRIOV_NUM_BARS];
	u8 nr_bars;
};

/* vf queue (used both for rx or tx) */
struct bnx2x_vf_queue {
	struct eth_context		*cxt;

	/* MACs object */
	struct bnx2x_vlan_mac_obj	mac_obj;

	/* VLANs object */
	struct bnx2x_vlan_mac_obj	vlan_obj;
	atomic_t vlan_count;		/* 0 means vlan-0 is set  ~ untagged */
	unsigned long accept_flags;	/* last accept flags configured */

	/* Queue Slow-path State object */
	struct bnx2x_queue_sp_obj	sp_obj;

	u32 cid;
	u16 index;
	u16 sb_idx;
	bool is_leading;
	bool sp_initialized;
};

/* struct bnx2x_vf_queue_construct_params - prepare queue construction
 * parameters: q-init, q-setup and SB index
 */
struct bnx2x_vf_queue_construct_params {
	struct bnx2x_queue_state_params		qstate;
	struct bnx2x_queue_setup_params		prep_qsetup;
};

/* forward */
struct bnx2x_virtf;

/* VFOP definitions */

struct bnx2x_vf_mac_vlan_filter {
	int type;
#define BNX2X_VF_FILTER_MAC	1
#define BNX2X_VF_FILTER_VLAN	2

	bool add;
	u8 *mac;
	u16 vid;
};

struct bnx2x_vf_mac_vlan_filters {
	int count;
	struct bnx2x_vf_mac_vlan_filter filters[];
};

/* vf context */
struct bnx2x_virtf {
	u16 cfg_flags;
#define VF_CFG_STATS		0x0001
#define VF_CFG_FW_FC		0x0002
#define VF_CFG_TPA		0x0004
#define VF_CFG_INT_SIMD		0x0008
#define VF_CACHE_LINE		0x0010
#define VF_CFG_VLAN		0x0020
#define VF_CFG_STATS_COALESCE	0x0040

	u8 state;
#define VF_FREE		0	/* VF ready to be acquired holds no resc */
#define VF_ACQUIRED	1	/* VF acquired, but not initialized */
#define VF_ENABLED	2	/* VF Enabled */
#define VF_RESET	3	/* VF FLR'd, pending cleanup */

	bool flr_clnup_stage;	/* true during flr cleanup */

	/* dma */
	dma_addr_t fw_stat_map;		/* valid iff VF_CFG_STATS */
	u16 stats_stride;
	dma_addr_t spq_map;
	dma_addr_t bulletin_map;

	/* Allocated resources counters. Before the VF is acquired, the
	 * counters hold the following values:
	 *
	 * - xxq_count = 0 as the queues memory is not allocated yet.
	 *
	 * - sb_count  = The number of status blocks configured for this VF in
	 *		 the IGU CAM. Initially read during probe.
	 *
	 * - xx_rules_count = The number of rules statically and equally
	 *		      allocated for each VF, during PF load.
	 */
	struct vf_pf_resc_request	alloc_resc;
#define vf_rxq_count(vf)		((vf)->alloc_resc.num_rxqs)
#define vf_txq_count(vf)		((vf)->alloc_resc.num_txqs)
#define vf_sb_count(vf)			((vf)->alloc_resc.num_sbs)
#define vf_mac_rules_cnt(vf)		((vf)->alloc_resc.num_mac_filters)
#define vf_vlan_rules_cnt(vf)		((vf)->alloc_resc.num_vlan_filters)
#define vf_mc_rules_cnt(vf)		((vf)->alloc_resc.num_mc_filters)
	/* Hide a single vlan filter credit for the hypervisor */
#define vf_vlan_rules_visible_cnt(vf)	(vf_vlan_rules_cnt(vf) - 1)

	u8 sb_count;	/* actual number of SBs */
	u8 igu_base_id;	/* base igu status block id */

	struct bnx2x_vf_queue	*vfqs;
#define LEADING_IDX			0
#define bnx2x_vfq_is_leading(vfq)	((vfq)->index == LEADING_IDX)
#define bnx2x_vfq(vf, nr, var)		((vf)->vfqs[(nr)].var)
#define bnx2x_leading_vfq(vf, var)	((vf)->vfqs[LEADING_IDX].var)

	u8 index;	/* index in the vf array */
	u8 abs_vfid;
	u8 sp_cl_id;
	u32 error;	/* 0 means all's-well */

	/* BDF */
	unsigned int bus;
	unsigned int devfn;

	/* bars */
	struct bnx2x_vf_bar bars[PCI_SRIOV_NUM_BARS];

	/* set-mac ramrod state 1-pending, 0-done */
	unsigned long	filter_state;

	/* leading rss client id ~~ the client id of the first rxq, must be
	 * set for each txq.
	 */
	int leading_rss;

	/* MCAST object */
	int mcast_list_len;
	struct bnx2x_mcast_obj		mcast_obj;

	/* RSS configuration object */
	struct bnx2x_rss_config_obj     rss_conf_obj;

	/* slow-path operations */
	struct mutex			op_mutex; /* one vfop at a time mutex */
	enum channel_tlvs		op_current;
};

#define BNX2X_NR_VIRTFN(bp)	((bp)->vfdb->sriov.nr_virtfn)

#define for_each_vf(bp, var) \
		for ((var) = 0; (var) < BNX2X_NR_VIRTFN(bp); (var)++)

#define for_each_vfq(vf, var) \
		for ((var) = 0; (var) < vf_rxq_count(vf); (var)++)

#define for_each_vf_sb(vf, var) \
		for ((var) = 0; (var) < vf_sb_count(vf); (var)++)

#define is_vf_multi(vf)	(vf_rxq_count(vf) > 1)

#define HW_VF_HANDLE(bp, abs_vfid) \
	(u16)(BP_ABS_FUNC((bp)) | (1<<3) |  ((u16)(abs_vfid) << 4))

#define FW_PF_MAX_HANDLE	8

#define FW_VF_HANDLE(abs_vfid)	\
	(abs_vfid + FW_PF_MAX_HANDLE)

/* locking and unlocking the channel mutex */
void bnx2x_lock_vf_pf_channel(struct bnx2x *bp, struct bnx2x_virtf *vf,
			      enum channel_tlvs tlv);

void bnx2x_unlock_vf_pf_channel(struct bnx2x *bp, struct bnx2x_virtf *vf,
				enum channel_tlvs expected_tlv);

/* VF mail box (aka vf-pf channel) */

/* a container for the bi-directional vf<-->pf messages.
 *  The actual response will be placed according to the offset parameter
 *  provided in the request
 */

#define MBX_MSG_ALIGN	8
#define MBX_MSG_ALIGNED_SIZE	(roundup(sizeof(struct bnx2x_vf_mbx_msg), \
				MBX_MSG_ALIGN))

struct bnx2x_vf_mbx_msg {
	union vfpf_tlvs req;
	union pfvf_tlvs resp;
};

struct bnx2x_vf_mbx {
	struct bnx2x_vf_mbx_msg *msg;
	dma_addr_t msg_mapping;

	/* VF GPA address */
	u32 vf_addr_lo;
	u32 vf_addr_hi;

	struct vfpf_first_tlv first_tlv;	/* saved VF request header */
};

struct bnx2x_vf_sp {
	union {
		struct eth_classify_rules_ramrod_data	e2;
	} mac_rdata;

	union {
		struct eth_classify_rules_ramrod_data	e2;
	} vlan_rdata;

	union {
		struct eth_filter_rules_ramrod_data	e2;
	} rx_mode_rdata;

	union {
		struct eth_multicast_rules_ramrod_data  e2;
	} mcast_rdata;

	union {
		struct client_init_ramrod_data  init_data;
		struct client_update_ramrod_data update_data;
	} q_data;

	union {
		struct eth_rss_update_ramrod_data e2;
	} rss_rdata;
};

struct hw_dma {
	void *addr;
	dma_addr_t mapping;
	size_t size;
};

struct bnx2x_vfdb {
#define BP_VFDB(bp)		((bp)->vfdb)
	/* vf array */
	struct bnx2x_virtf	*vfs;
#define BP_VF(bp, idx)		(&((bp)->vfdb->vfs[(idx)]))
#define bnx2x_vf(bp, idx, var)	((bp)->vfdb->vfs[(idx)].var)

	/* queue array - for all vfs */
	struct bnx2x_vf_queue *vfqs;

	/* vf HW contexts */
	struct hw_dma		context[BNX2X_VF_CIDS/ILT_PAGE_CIDS];
#define	BP_VF_CXT_PAGE(bp, i)	(&(bp)->vfdb->context[(i)])

	/* SR-IOV information */
	struct bnx2x_sriov	sriov;
	struct hw_dma		mbx_dma;
#define BP_VF_MBX_DMA(bp)	(&((bp)->vfdb->mbx_dma))
	struct bnx2x_vf_mbx	mbxs[BNX2X_MAX_NUM_OF_VFS];
#define BP_VF_MBX(bp, vfid)	(&((bp)->vfdb->mbxs[(vfid)]))

	struct hw_dma		bulletin_dma;
#define BP_VF_BULLETIN_DMA(bp)	(&((bp)->vfdb->bulletin_dma))
#define	BP_VF_BULLETIN(bp, vf) \
	(((struct pf_vf_bulletin_content *)(BP_VF_BULLETIN_DMA(bp)->addr)) \
	 + (vf))

	struct hw_dma		sp_dma;
#define bnx2x_vf_sp(bp, vf, field) ((bp)->vfdb->sp_dma.addr +		\
		(vf)->index * sizeof(struct bnx2x_vf_sp) +		\
		offsetof(struct bnx2x_vf_sp, field))
#define bnx2x_vf_sp_map(bp, vf, field) ((bp)->vfdb->sp_dma.mapping +	\
		(vf)->index * sizeof(struct bnx2x_vf_sp) +		\
		offsetof(struct bnx2x_vf_sp, field))

#define FLRD_VFS_DWORDS (BNX2X_MAX_NUM_OF_VFS / 32)
	u32 flrd_vfs[FLRD_VFS_DWORDS];

	/* the number of msix vectors belonging to this PF designated for VFs */
	u16 vf_sbs_pool;
	u16 first_vf_igu_entry;

	/* sp_rtnl synchronization */
	struct mutex			event_mutex;
	u64				event_occur;
};

/* queue access */
static inline struct bnx2x_vf_queue *vfq_get(struct bnx2x_virtf *vf, u8 index)
{
	return &(vf->vfqs[index]);
}

/* FW ids */
static inline u8 vf_igu_sb(struct bnx2x_virtf *vf, u16 sb_idx)
{
	return vf->igu_base_id + sb_idx;
}

static inline u8 vf_hc_qzone(struct bnx2x_virtf *vf, u16 sb_idx)
{
	return vf_igu_sb(vf, sb_idx);
}

static u8 vfq_cl_id(struct bnx2x_virtf *vf, struct bnx2x_vf_queue *q)
{
	return vf->igu_base_id + q->index;
}

static inline u8 vfq_stat_id(struct bnx2x_virtf *vf, struct bnx2x_vf_queue *q)
{
	if (vf->cfg_flags & VF_CFG_STATS_COALESCE)
		return vf->leading_rss;
	else
		return vfq_cl_id(vf, q);
}

static inline u8 vfq_qzone_id(struct bnx2x_virtf *vf, struct bnx2x_vf_queue *q)
{
	return vfq_cl_id(vf, q);
}

/* global iov routines */
int bnx2x_iov_init_ilt(struct bnx2x *bp, u16 line);
int bnx2x_iov_init_one(struct bnx2x *bp, int int_mode_param, int num_vfs_param);
void bnx2x_iov_remove_one(struct bnx2x *bp);
void bnx2x_iov_free_mem(struct bnx2x *bp);
int bnx2x_iov_alloc_mem(struct bnx2x *bp);
int bnx2x_iov_nic_init(struct bnx2x *bp);
int bnx2x_iov_chip_cleanup(struct bnx2x *bp);
void bnx2x_iov_init_dq(struct bnx2x *bp);
void bnx2x_iov_init_dmae(struct bnx2x *bp);
void bnx2x_iov_set_queue_sp_obj(struct bnx2x *bp, int vf_cid,
				struct bnx2x_queue_sp_obj **q_obj);
int bnx2x_iov_eq_sp_event(struct bnx2x *bp, union event_ring_elem *elem);
void bnx2x_iov_adjust_stats_req(struct bnx2x *bp);
void bnx2x_iov_storm_stats_update(struct bnx2x *bp);
/* global vf mailbox routines */
void bnx2x_vf_mbx(struct bnx2x *bp);
void bnx2x_vf_mbx_schedule(struct bnx2x *bp,
			   struct vf_pf_event_data *vfpf_event);
void bnx2x_vf_enable_mbx(struct bnx2x *bp, u8 abs_vfid);

/* CORE VF API */
typedef u8 bnx2x_mac_addr_t[ETH_ALEN];

/* acquire */
int bnx2x_vf_acquire(struct bnx2x *bp, struct bnx2x_virtf *vf,
		     struct vf_pf_resc_request *resc);
/* init */
int bnx2x_vf_init(struct bnx2x *bp, struct bnx2x_virtf *vf,
		  dma_addr_t *sb_map);

/* VFOP queue construction helpers */
void bnx2x_vfop_qctor_dump_tx(struct bnx2x *bp, struct bnx2x_virtf *vf,
			    struct bnx2x_queue_init_params *init_params,
			    struct bnx2x_queue_setup_params *setup_params,
			    u16 q_idx, u16 sb_idx);

void bnx2x_vfop_qctor_dump_rx(struct bnx2x *bp, struct bnx2x_virtf *vf,
			    struct bnx2x_queue_init_params *init_params,
			    struct bnx2x_queue_setup_params *setup_params,
			    u16 q_idx, u16 sb_idx);

void bnx2x_vfop_qctor_prep(struct bnx2x *bp,
			   struct bnx2x_virtf *vf,
			   struct bnx2x_vf_queue *q,
			   struct bnx2x_vf_queue_construct_params *p,
			   unsigned long q_type);

int bnx2x_vf_mac_vlan_config_list(struct bnx2x *bp, struct bnx2x_virtf *vf,
				  struct bnx2x_vf_mac_vlan_filters *filters,
				  int qid, bool drv_only);

int bnx2x_vf_queue_setup(struct bnx2x *bp, struct bnx2x_virtf *vf, int qid,
			 struct bnx2x_vf_queue_construct_params *qctor);

int bnx2x_vf_queue_teardown(struct bnx2x *bp, struct bnx2x_virtf *vf, int qid);

int bnx2x_vf_mcast(struct bnx2x *bp, struct bnx2x_virtf *vf,
		   bnx2x_mac_addr_t *mcasts, int mc_num, bool drv_only);

int bnx2x_vf_rxmode(struct bnx2x *bp, struct bnx2x_virtf *vf,
		    int qid, unsigned long accept_flags);

int bnx2x_vf_close(struct bnx2x *bp, struct bnx2x_virtf *vf);

int bnx2x_vf_free(struct bnx2x *bp, struct bnx2x_virtf *vf);

int bnx2x_vf_rss_update(struct bnx2x *bp, struct bnx2x_virtf *vf,
			struct bnx2x_config_rss_params *rss);

int bnx2x_vf_tpa_update(struct bnx2x *bp, struct bnx2x_virtf *vf,
			struct vfpf_tpa_tlv *tlv,
			struct bnx2x_queue_update_tpa_params *params);

/* VF release ~ VF close + VF release-resources
 *
 * Release is the ultimate SW shutdown and is called whenever an
 * irrecoverable error is encountered.
 */
int bnx2x_vf_release(struct bnx2x *bp, struct bnx2x_virtf *vf);
int bnx2x_vf_idx_by_abs_fid(struct bnx2x *bp, u16 abs_vfid);
u8 bnx2x_vf_max_queue_cnt(struct bnx2x *bp, struct bnx2x_virtf *vf);

/* FLR routines */

/* VF FLR helpers */
int bnx2x_vf_flr_clnup_epilog(struct bnx2x *bp, u8 abs_vfid);
void bnx2x_vf_enable_access(struct bnx2x *bp, u8 abs_vfid);

/* Handles an FLR (or VF_DISABLE) notification form the MCP */
void bnx2x_vf_handle_flr_event(struct bnx2x *bp);

bool bnx2x_tlv_supported(u16 tlvtype);

u32 bnx2x_crc_vf_bulletin(struct bnx2x *bp,
			  struct pf_vf_bulletin_content *bulletin);
int bnx2x_post_vf_bulletin(struct bnx2x *bp, int vf);

enum sample_bulletin_result bnx2x_sample_bulletin(struct bnx2x *bp);

/* VF side vfpf channel functions */
int bnx2x_vfpf_acquire(struct bnx2x *bp, u8 tx_count, u8 rx_count);
int bnx2x_vfpf_release(struct bnx2x *bp);
int bnx2x_vfpf_release(struct bnx2x *bp);
int bnx2x_vfpf_init(struct bnx2x *bp);
void bnx2x_vfpf_close_vf(struct bnx2x *bp);
int bnx2x_vfpf_setup_q(struct bnx2x *bp, struct bnx2x_fastpath *fp,
		       bool is_leading);
int bnx2x_vfpf_config_mac(struct bnx2x *bp, u8 *addr, u8 vf_qid, bool set);
int bnx2x_vfpf_config_rss(struct bnx2x *bp,
			  struct bnx2x_config_rss_params *params);
int bnx2x_vfpf_set_mcast(struct net_device *dev);
int bnx2x_vfpf_storm_rx_mode(struct bnx2x *bp);

static inline void bnx2x_vf_fill_fw_str(struct bnx2x *bp, char *buf,
					size_t buf_len)
{
	strlcpy(buf, bp->acquire_resp.pfdev_info.fw_ver, buf_len);
}

static inline int bnx2x_vf_ustorm_prods_offset(struct bnx2x *bp,
					       struct bnx2x_fastpath *fp)
{
	return PXP_VF_ADDR_USDM_QUEUES_START +
		bp->acquire_resp.resc.hw_qid[fp->index] *
		sizeof(struct ustorm_queue_zone_data);
}

enum sample_bulletin_result bnx2x_sample_bulletin(struct bnx2x *bp);
void bnx2x_timer_sriov(struct bnx2x *bp);
void __iomem *bnx2x_vf_doorbells(struct bnx2x *bp);
void bnx2x_vf_pci_dealloc(struct bnx2x *bp);
int bnx2x_vf_pci_alloc(struct bnx2x *bp);
int bnx2x_enable_sriov(struct bnx2x *bp);
void bnx2x_disable_sriov(struct bnx2x *bp);
static inline int bnx2x_vf_headroom(struct bnx2x *bp)
{
	return bp->vfdb->sriov.nr_virtfn * BNX2X_CIDS_PER_VF;
}
void bnx2x_pf_set_vfs_vlan(struct bnx2x *bp);
int bnx2x_sriov_configure(struct pci_dev *dev, int num_vfs);
void bnx2x_iov_channel_down(struct bnx2x *bp);

void bnx2x_iov_task(struct work_struct *work);

void bnx2x_schedule_iov_task(struct bnx2x *bp, enum bnx2x_iov_flag flag);

#else /* CONFIG_BNX2X_SRIOV */

static inline void bnx2x_iov_set_queue_sp_obj(struct bnx2x *bp, int vf_cid,
				struct bnx2x_queue_sp_obj **q_obj) {}
static inline void bnx2x_vf_handle_flr_event(struct bnx2x *bp) {}
static inline int bnx2x_iov_eq_sp_event(struct bnx2x *bp,
					union event_ring_elem *elem) {return 1; }
static inline void bnx2x_vf_mbx(struct bnx2x *bp) {}
static inline void bnx2x_vf_mbx_schedule(struct bnx2x *bp,
					 struct vf_pf_event_data *vfpf_event) {}
static inline int bnx2x_iov_init_ilt(struct bnx2x *bp, u16 line) {return line; }
static inline void bnx2x_iov_init_dq(struct bnx2x *bp) {}
static inline int bnx2x_iov_alloc_mem(struct bnx2x *bp) {return 0; }
static inline void bnx2x_iov_free_mem(struct bnx2x *bp) {}
static inline int bnx2x_iov_chip_cleanup(struct bnx2x *bp) {return 0; }
static inline void bnx2x_iov_init_dmae(struct bnx2x *bp) {}
static inline int bnx2x_iov_init_one(struct bnx2x *bp, int int_mode_param,
				     int num_vfs_param) {return 0; }
static inline void bnx2x_iov_remove_one(struct bnx2x *bp) {}
static inline int bnx2x_enable_sriov(struct bnx2x *bp) {return 0; }
static inline void bnx2x_disable_sriov(struct bnx2x *bp) {}
static inline int bnx2x_vfpf_acquire(struct bnx2x *bp,
				     u8 tx_count, u8 rx_count) {return 0; }
static inline int bnx2x_vfpf_release(struct bnx2x *bp) {return 0; }
static inline int bnx2x_vfpf_init(struct bnx2x *bp) {return 0; }
static inline void bnx2x_vfpf_close_vf(struct bnx2x *bp) {}
static inline int bnx2x_vfpf_setup_q(struct bnx2x *bp, struct bnx2x_fastpath *fp, bool is_leading) {return 0; }
static inline int bnx2x_vfpf_config_mac(struct bnx2x *bp, u8 *addr,
					u8 vf_qid, bool set) {return 0; }
static inline int bnx2x_vfpf_config_rss(struct bnx2x *bp,
					struct bnx2x_config_rss_params *params) {return 0; }
static inline int bnx2x_vfpf_set_mcast(struct net_device *dev) {return 0; }
static inline int bnx2x_vfpf_storm_rx_mode(struct bnx2x *bp) {return 0; }
static inline int bnx2x_iov_nic_init(struct bnx2x *bp) {return 0; }
static inline int bnx2x_vf_headroom(struct bnx2x *bp) {return 0; }
static inline void bnx2x_iov_adjust_stats_req(struct bnx2x *bp) {}
static inline void bnx2x_vf_fill_fw_str(struct bnx2x *bp, char *buf,
					size_t buf_len) {}
static inline int bnx2x_vf_ustorm_prods_offset(struct bnx2x *bp,
					       struct bnx2x_fastpath *fp) {return 0; }
static inline enum sample_bulletin_result bnx2x_sample_bulletin(struct bnx2x *bp)
{
	return PFVF_BULLETIN_UNCHANGED;
}
static inline void bnx2x_timer_sriov(struct bnx2x *bp) {}

static inline void __iomem *bnx2x_vf_doorbells(struct bnx2x *bp)
{
	return NULL;
}

static inline void bnx2x_vf_pci_dealloc(struct bnx2 *bp) {return 0; }
static inline int bnx2x_vf_pci_alloc(struct bnx2x *bp) {return 0; }
static inline void bnx2x_pf_set_vfs_vlan(struct bnx2x *bp) {}
static inline int bnx2x_sriov_configure(struct pci_dev *dev, int num_vfs) {return 0; }
static inline void bnx2x_iov_channel_down(struct bnx2x *bp) {}

static inline void bnx2x_iov_task(struct work_struct *work) {}
static inline void bnx2x_schedule_iov_task(struct bnx2x *bp, enum bnx2x_iov_flag flag) {}

#endif /* CONFIG_BNX2X_SRIOV */
#endif /* bnx2x_sriov.h */
