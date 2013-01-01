/* bnx2x_sriov.h: Broadcom Everest network driver.
 *
 * Copyright 2009-2012 Broadcom Corporation
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

/* The bnx2x device structure holds vfdb structure described below.
 * The VF array is indexed by the relative vfid.
 */
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

/* vf queue (used both for rx or tx) */
struct bnx2x_vf_queue {
	struct eth_context		*cxt;

	/* MACs object */
	struct bnx2x_vlan_mac_obj	mac_obj;

	/* VLANs object */
	struct bnx2x_vlan_mac_obj	vlan_obj;
	atomic_t vlan_count;		/* 0 means vlan-0 is set  ~ untagged */

	/* Queue Slow-path State object */
	struct bnx2x_queue_sp_obj	sp_obj;

	u32 cid;
	u16 index;
	u16 sb_idx;
};

/* struct bnx2x_vfop_qctor_params - prepare queue construction parameters:
 * q-init, q-setup and SB index
 */
struct bnx2x_vfop_qctor_params {
	struct bnx2x_queue_state_params		qstate;
	struct bnx2x_queue_setup_params		prep_qsetup;
};

/* VFOP parameters (one copy per VF) */
union bnx2x_vfop_params {
	struct bnx2x_vlan_mac_ramrod_params	vlan_mac;
	struct bnx2x_rx_mode_ramrod_params	rx_mode;
	struct bnx2x_mcast_ramrod_params	mcast;
	struct bnx2x_config_rss_params		rss;
	struct bnx2x_vfop_qctor_params		qctor;
};

/* forward */
struct bnx2x_virtf;

/* VFOP definitions */
typedef void (*vfop_handler_t)(struct bnx2x *bp, struct bnx2x_virtf *vf);

/* VFOP queue filters command additional arguments */
struct bnx2x_vfop_filter {
	struct list_head link;
	int type;
#define BNX2X_VFOP_FILTER_MAC	1
#define BNX2X_VFOP_FILTER_VLAN	2

	bool add;
	u8 *mac;
	u16 vid;
};

struct bnx2x_vfop_filters {
	int add_cnt;
	struct list_head head;
	struct bnx2x_vfop_filter filters[];
};

/* transient list allocated, built and saved until its
 * passed to the SP-VERBs layer.
 */
struct bnx2x_vfop_args_mcast {
	int mc_num;
	struct bnx2x_mcast_list_elem *mc;
};

struct bnx2x_vfop_args_qctor {
	int	qid;
	u16	sb_idx;
};

struct bnx2x_vfop_args_qdtor {
	int	qid;
	struct eth_context *cxt;
};

struct bnx2x_vfop_args_defvlan {
	int	qid;
	bool	enable;
	u16	vid;
	u8	prio;
};

struct bnx2x_vfop_args_qx {
	int	qid;
	bool	en_add;
};

struct bnx2x_vfop_args_filters {
	struct bnx2x_vfop_filters *multi_filter;
	atomic_t *credit;	/* non NULL means 'don't consume credit' */
};

union bnx2x_vfop_args {
	struct bnx2x_vfop_args_mcast	mc_list;
	struct bnx2x_vfop_args_qctor	qctor;
	struct bnx2x_vfop_args_qdtor	qdtor;
	struct bnx2x_vfop_args_defvlan	defvlan;
	struct bnx2x_vfop_args_qx	qx;
	struct bnx2x_vfop_args_filters	filters;
};

struct bnx2x_vfop {
	struct list_head link;
	int			rc;		/* return code */
	int			state;		/* next state */
	union bnx2x_vfop_args	args;		/* extra arguments */
	union bnx2x_vfop_params *op_p;		/* ramrod params */

	/* state machine callbacks */
	vfop_handler_t transition;
	vfop_handler_t done;
};

/* vf context */
struct bnx2x_virtf {
	u16 cfg_flags;
#define VF_CFG_STATS		0x0001
#define VF_CFG_FW_FC		0x0002
#define VF_CFG_TPA		0x0004
#define VF_CFG_INT_SIMD		0x0008
#define VF_CACHE_LINE		0x0010

	u8 state;
#define VF_FREE		0	/* VF ready to be acquired holds no resc */
#define VF_ACQUIRED	1	/* VF aquired, but not initalized */
#define VF_ENABLED	2	/* VF Enabled */
#define VF_RESET	3	/* VF FLR'd, pending cleanup */

	/* non 0 during flr cleanup */
	u8 flr_clnup_stage;
#define VF_FLR_CLN	1	/* reclaim resources and do 'final cleanup'
				 * sans the end-wait
				 */
#define VF_FLR_ACK	2	/* ACK flr notification */
#define VF_FLR_EPILOG	3	/* wait for VF remnants to dissipate in the HW
				 * ~ final cleanup' end wait
				 */

	/* dma */
	dma_addr_t fw_stat_map;		/* valid iff VF_CFG_STATS */
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

	u8 sb_count;	/* actual number of SBs */
	u8 igu_base_id;	/* base igu status block id */

	struct bnx2x_vf_queue	*vfqs;
#define bnx2x_vfq(vf, nr, var)	((vf)->vfqs[(nr)].var)

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
	struct bnx2x_mcast_obj		mcast_obj;

	/* RSS configuration object */
	struct bnx2x_rss_config_obj     rss_conf_obj;

	/* slow-path operations */
	atomic_t			op_in_progress;
	int				op_rc;
	bool				op_wait_blocking;
	struct list_head		op_list_head;
	union bnx2x_vfop_params		op_params;
	struct mutex			op_mutex; /* one vfop at a time mutex */
	enum channel_tlvs		op_current;
};

#define BNX2X_NR_VIRTFN(bp)	((bp)->vfdb->sriov.nr_virtfn)

#define for_each_vf(bp, var) \
		for ((var) = 0; (var) < BNX2X_NR_VIRTFN(bp); (var)++)

#define HW_VF_HANDLE(bp, abs_vfid) \
	(u16)(BP_ABS_FUNC((bp)) | (1<<3) |  ((u16)(abs_vfid) << 4))

#define FW_PF_MAX_HANDLE	8

#define FW_VF_HANDLE(abs_vfid)	\
	(abs_vfid + FW_PF_MAX_HANDLE)

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

	u8 flags;
#define VF_MSG_INPROCESS	0x1	/* failsafe - the FW should prevent
					 * more then one pending msg
					 */
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

	struct hw_dma		sp_dma;
#define bnx2x_vf_sp(bp, vf, field) ((bp)->vfdb->sp_dma.addr +		\
		(vf)->index * sizeof(struct bnx2x_vf_sp) +		\
		offsetof(struct bnx2x_vf_sp, field))
#define bnx2x_vf_sp_map(bp, vf, field) ((bp)->vfdb->sp_dma.mapping +	\
		(vf)->index * sizeof(struct bnx2x_vf_sp) +		\
		offsetof(struct bnx2x_vf_sp, field))

#define FLRD_VFS_DWORDS (BNX2X_MAX_NUM_OF_VFS / 32)
	u32 flrd_vfs[FLRD_VFS_DWORDS];
};

/* queue access */
static inline struct bnx2x_vf_queue *vfq_get(struct bnx2x_virtf *vf, u8 index)
{
	return &(vf->vfqs[index]);
}

static inline u8 vf_igu_sb(struct bnx2x_virtf *vf, u16 sb_idx)
{
	return vf->igu_base_id + sb_idx;
}

/* global iov routines */
int bnx2x_iov_init_ilt(struct bnx2x *bp, u16 line);
int bnx2x_iov_init_one(struct bnx2x *bp, int int_mode_param, int num_vfs_param);
void bnx2x_iov_remove_one(struct bnx2x *bp);
void bnx2x_iov_free_mem(struct bnx2x *bp);
int bnx2x_iov_alloc_mem(struct bnx2x *bp);
int bnx2x_iov_nic_init(struct bnx2x *bp);
void bnx2x_iov_init_dq(struct bnx2x *bp);
void bnx2x_iov_init_dmae(struct bnx2x *bp);
void bnx2x_iov_set_queue_sp_obj(struct bnx2x *bp, int vf_cid,
				struct bnx2x_queue_sp_obj **q_obj);
void bnx2x_iov_sp_event(struct bnx2x *bp, int vf_cid, bool queue_work);
int bnx2x_iov_eq_sp_event(struct bnx2x *bp, union event_ring_elem *elem);
void bnx2x_iov_sp_task(struct bnx2x *bp);
/* global vf mailbox routines */
void bnx2x_vf_mbx(struct bnx2x *bp, struct vf_pf_event_data *vfpf_event);
void bnx2x_vf_enable_mbx(struct bnx2x *bp, u8 abs_vfid);
static inline struct bnx2x_vfop *bnx2x_vfop_cur(struct bnx2x *bp,
						struct bnx2x_virtf *vf)
{
	WARN(!mutex_is_locked(&vf->op_mutex), "about to access vf op linked list but mutex was not locked!");
	WARN_ON(list_empty(&vf->op_list_head));
	return list_first_entry(&vf->op_list_head, struct bnx2x_vfop, link);
}

int bnx2x_vf_idx_by_abs_fid(struct bnx2x *bp, u16 abs_vfid);
/* VF FLR helpers */
int bnx2x_vf_flr_clnup_epilog(struct bnx2x *bp, u8 abs_vfid);
void bnx2x_vf_enable_access(struct bnx2x *bp, u8 abs_vfid);
void bnx2x_add_tlv(struct bnx2x *bp, void *tlvs_list, u16 offset, u16 type,
		   u16 length);
void bnx2x_vfpf_prep(struct bnx2x *bp, struct vfpf_first_tlv *first_tlv,
		     u16 type, u16 length);
void bnx2x_dp_tlv_list(struct bnx2x *bp, void *tlvs_list);

bool bnx2x_tlv_supported(u16 tlvtype);

#endif /* bnx2x_sriov.h */
