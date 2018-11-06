/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
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

#ifndef MLX5_IB_H
#define MLX5_IB_H

#include <linux/kernel.h>
#include <linux/sched.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_smi.h>
#include <linux/mlx5/driver.h>
#include <linux/mlx5/cq.h>
#include <linux/mlx5/fs.h>
#include <linux/mlx5/qp.h>
#include <linux/mlx5/srq.h>
#include <linux/mlx5/fs.h>
#include <linux/types.h>
#include <linux/mlx5/transobj.h>
#include <rdma/ib_user_verbs.h>
#include <rdma/mlx5-abi.h>
#include <rdma/uverbs_ioctl.h>
#include <rdma/mlx5_user_ioctl_cmds.h>

#define mlx5_ib_dbg(_dev, format, arg...)                                      \
	dev_dbg(&(_dev)->ib_dev.dev, "%s:%d:(pid %d): " format, __func__,      \
		__LINE__, current->pid, ##arg)

#define mlx5_ib_err(_dev, format, arg...)                                      \
	dev_err(&(_dev)->ib_dev.dev, "%s:%d:(pid %d): " format, __func__,      \
		__LINE__, current->pid, ##arg)

#define mlx5_ib_warn(_dev, format, arg...)                                     \
	dev_warn(&(_dev)->ib_dev.dev, "%s:%d:(pid %d): " format, __func__,     \
		 __LINE__, current->pid, ##arg)

#define field_avail(type, fld, sz) (offsetof(type, fld) +		\
				    sizeof(((type *)0)->fld) <= (sz))
#define MLX5_IB_DEFAULT_UIDX 0xffffff
#define MLX5_USER_ASSIGNED_UIDX_MASK __mlx5_mask(qpc, user_index)

#define MLX5_MKEY_PAGE_SHIFT_MASK __mlx5_mask(mkc, log_page_size)

enum {
	MLX5_IB_MMAP_CMD_SHIFT	= 8,
	MLX5_IB_MMAP_CMD_MASK	= 0xff,
};

enum {
	MLX5_RES_SCAT_DATA32_CQE	= 0x1,
	MLX5_RES_SCAT_DATA64_CQE	= 0x2,
	MLX5_REQ_SCAT_DATA32_CQE	= 0x11,
	MLX5_REQ_SCAT_DATA64_CQE	= 0x22,
};

enum mlx5_ib_mad_ifc_flags {
	MLX5_MAD_IFC_IGNORE_MKEY	= 1,
	MLX5_MAD_IFC_IGNORE_BKEY	= 2,
	MLX5_MAD_IFC_NET_VIEW		= 4,
};

enum {
	MLX5_CROSS_CHANNEL_BFREG         = 0,
};

enum {
	MLX5_CQE_VERSION_V0,
	MLX5_CQE_VERSION_V1,
};

enum {
	MLX5_TM_MAX_RNDV_MSG_SIZE	= 64,
	MLX5_TM_MAX_SGE			= 1,
};

enum {
	MLX5_IB_INVALID_UAR_INDEX	= BIT(31),
	MLX5_IB_INVALID_BFREG		= BIT(31),
};

enum {
	MLX5_MAX_MEMIC_PAGES = 0x100,
	MLX5_MEMIC_ALLOC_SIZE_MASK = 0x3f,
};

enum {
	MLX5_MEMIC_BASE_ALIGN	= 6,
	MLX5_MEMIC_BASE_SIZE	= 1 << MLX5_MEMIC_BASE_ALIGN,
};

struct mlx5_ib_ucontext {
	struct ib_ucontext	ibucontext;
	struct list_head	db_page_list;

	/* protect doorbell record alloc/free
	 */
	struct mutex		db_page_mutex;
	struct mlx5_bfreg_info	bfregi;
	u8			cqe_version;
	/* Transport Domain number */
	u32			tdn;

	u64			lib_caps;
	DECLARE_BITMAP(dm_pages, MLX5_MAX_MEMIC_PAGES);
	u16			devx_uid;
	/* For RoCE LAG TX affinity */
	atomic_t		tx_port_affinity;
};

static inline struct mlx5_ib_ucontext *to_mucontext(struct ib_ucontext *ibucontext)
{
	return container_of(ibucontext, struct mlx5_ib_ucontext, ibucontext);
}

struct mlx5_ib_pd {
	struct ib_pd		ibpd;
	u32			pdn;
	u16			uid;
};

enum {
	MLX5_IB_FLOW_ACTION_MODIFY_HEADER,
	MLX5_IB_FLOW_ACTION_PACKET_REFORMAT,
	MLX5_IB_FLOW_ACTION_DECAP,
};

#define MLX5_IB_FLOW_MCAST_PRIO		(MLX5_BY_PASS_NUM_PRIOS - 1)
#define MLX5_IB_FLOW_LAST_PRIO		(MLX5_BY_PASS_NUM_REGULAR_PRIOS - 1)
#if (MLX5_IB_FLOW_LAST_PRIO <= 0)
#error "Invalid number of bypass priorities"
#endif
#define MLX5_IB_FLOW_LEFTOVERS_PRIO	(MLX5_IB_FLOW_MCAST_PRIO + 1)

#define MLX5_IB_NUM_FLOW_FT		(MLX5_IB_FLOW_LEFTOVERS_PRIO + 1)
#define MLX5_IB_NUM_SNIFFER_FTS		2
#define MLX5_IB_NUM_EGRESS_FTS		1
struct mlx5_ib_flow_prio {
	struct mlx5_flow_table		*flow_table;
	unsigned int			refcount;
};

struct mlx5_ib_flow_handler {
	struct list_head		list;
	struct ib_flow			ibflow;
	struct mlx5_ib_flow_prio	*prio;
	struct mlx5_flow_handle		*rule;
	struct ib_counters		*ibcounters;
	struct mlx5_ib_dev		*dev;
	struct mlx5_ib_flow_matcher	*flow_matcher;
};

struct mlx5_ib_flow_matcher {
	struct mlx5_ib_match_params matcher_mask;
	int			mask_len;
	enum mlx5_ib_flow_type	flow_type;
	enum mlx5_flow_namespace_type ns_type;
	u16			priority;
	struct mlx5_core_dev	*mdev;
	atomic_t		usecnt;
	u8			match_criteria_enable;
};

struct mlx5_ib_flow_db {
	struct mlx5_ib_flow_prio	prios[MLX5_IB_NUM_FLOW_FT];
	struct mlx5_ib_flow_prio	egress_prios[MLX5_IB_NUM_FLOW_FT];
	struct mlx5_ib_flow_prio	sniffer[MLX5_IB_NUM_SNIFFER_FTS];
	struct mlx5_ib_flow_prio	egress[MLX5_IB_NUM_EGRESS_FTS];
	struct mlx5_flow_table		*lag_demux_ft;
	/* Protect flow steering bypass flow tables
	 * when add/del flow rules.
	 * only single add/removal of flow steering rule could be done
	 * simultaneously.
	 */
	struct mutex			lock;
};

/* Use macros here so that don't have to duplicate
 * enum ib_send_flags and enum ib_qp_type for low-level driver
 */

#define MLX5_IB_SEND_UMR_ENABLE_MR	       (IB_SEND_RESERVED_START << 0)
#define MLX5_IB_SEND_UMR_DISABLE_MR	       (IB_SEND_RESERVED_START << 1)
#define MLX5_IB_SEND_UMR_FAIL_IF_FREE	       (IB_SEND_RESERVED_START << 2)
#define MLX5_IB_SEND_UMR_UPDATE_XLT	       (IB_SEND_RESERVED_START << 3)
#define MLX5_IB_SEND_UMR_UPDATE_TRANSLATION    (IB_SEND_RESERVED_START << 4)
#define MLX5_IB_SEND_UMR_UPDATE_PD_ACCESS       IB_SEND_RESERVED_END

#define MLX5_IB_QPT_REG_UMR	IB_QPT_RESERVED1
/*
 * IB_QPT_GSI creates the software wrapper around GSI, and MLX5_IB_QPT_HW_GSI
 * creates the actual hardware QP.
 */
#define MLX5_IB_QPT_HW_GSI	IB_QPT_RESERVED2
#define MLX5_IB_QPT_DCI		IB_QPT_RESERVED3
#define MLX5_IB_QPT_DCT		IB_QPT_RESERVED4
#define MLX5_IB_WR_UMR		IB_WR_RESERVED1

#define MLX5_IB_UMR_OCTOWORD	       16
#define MLX5_IB_UMR_XLT_ALIGNMENT      64

#define MLX5_IB_UPD_XLT_ZAP	      BIT(0)
#define MLX5_IB_UPD_XLT_ENABLE	      BIT(1)
#define MLX5_IB_UPD_XLT_ATOMIC	      BIT(2)
#define MLX5_IB_UPD_XLT_ADDR	      BIT(3)
#define MLX5_IB_UPD_XLT_PD	      BIT(4)
#define MLX5_IB_UPD_XLT_ACCESS	      BIT(5)
#define MLX5_IB_UPD_XLT_INDIRECT      BIT(6)

/* Private QP creation flags to be passed in ib_qp_init_attr.create_flags.
 *
 * These flags are intended for internal use by the mlx5_ib driver, and they
 * rely on the range reserved for that use in the ib_qp_create_flags enum.
 */

/* Create a UD QP whose source QP number is 1 */
static inline enum ib_qp_create_flags mlx5_ib_create_qp_sqpn_qp1(void)
{
	return IB_QP_CREATE_RESERVED_START;
}

struct wr_list {
	u16	opcode;
	u16	next;
};

enum mlx5_ib_rq_flags {
	MLX5_IB_RQ_CVLAN_STRIPPING	= 1 << 0,
	MLX5_IB_RQ_PCI_WRITE_END_PADDING	= 1 << 1,
};

struct mlx5_ib_wq {
	u64		       *wrid;
	u32		       *wr_data;
	struct wr_list	       *w_list;
	unsigned	       *wqe_head;
	u16		        unsig_count;

	/* serialize post to the work queue
	 */
	spinlock_t		lock;
	int			wqe_cnt;
	int			max_post;
	int			max_gs;
	int			offset;
	int			wqe_shift;
	unsigned		head;
	unsigned		tail;
	u16			cur_post;
	u16			last_poll;
	void		       *qend;
};

enum mlx5_ib_wq_flags {
	MLX5_IB_WQ_FLAGS_DELAY_DROP = 0x1,
	MLX5_IB_WQ_FLAGS_STRIDING_RQ = 0x2,
};

#define MLX5_MIN_SINGLE_WQE_LOG_NUM_STRIDES 9
#define MLX5_MAX_SINGLE_WQE_LOG_NUM_STRIDES 16
#define MLX5_MIN_SINGLE_STRIDE_LOG_NUM_BYTES 6
#define MLX5_MAX_SINGLE_STRIDE_LOG_NUM_BYTES 13

struct mlx5_ib_rwq {
	struct ib_wq		ibwq;
	struct mlx5_core_qp	core_qp;
	u32			rq_num_pas;
	u32			log_rq_stride;
	u32			log_rq_size;
	u32			rq_page_offset;
	u32			log_page_size;
	u32			log_num_strides;
	u32			two_byte_shift_en;
	u32			single_stride_log_num_of_bytes;
	struct ib_umem		*umem;
	size_t			buf_size;
	unsigned int		page_shift;
	int			create_type;
	struct mlx5_db		db;
	u32			user_index;
	u32			wqe_count;
	u32			wqe_shift;
	int			wq_sig;
	u32			create_flags; /* Use enum mlx5_ib_wq_flags */
};

enum {
	MLX5_QP_USER,
	MLX5_QP_KERNEL,
	MLX5_QP_EMPTY
};

enum {
	MLX5_WQ_USER,
	MLX5_WQ_KERNEL
};

struct mlx5_ib_rwq_ind_table {
	struct ib_rwq_ind_table ib_rwq_ind_tbl;
	u32			rqtn;
	u16			uid;
};

struct mlx5_ib_ubuffer {
	struct ib_umem	       *umem;
	int			buf_size;
	u64			buf_addr;
};

struct mlx5_ib_qp_base {
	struct mlx5_ib_qp	*container_mibqp;
	struct mlx5_core_qp	mqp;
	struct mlx5_ib_ubuffer	ubuffer;
};

struct mlx5_ib_qp_trans {
	struct mlx5_ib_qp_base	base;
	u16			xrcdn;
	u8			alt_port;
	u8			atomic_rd_en;
	u8			resp_depth;
};

struct mlx5_ib_rss_qp {
	u32	tirn;
};

struct mlx5_ib_rq {
	struct mlx5_ib_qp_base base;
	struct mlx5_ib_wq	*rq;
	struct mlx5_ib_ubuffer	ubuffer;
	struct mlx5_db		*doorbell;
	u32			tirn;
	u8			state;
	u32			flags;
};

struct mlx5_ib_sq {
	struct mlx5_ib_qp_base base;
	struct mlx5_ib_wq	*sq;
	struct mlx5_ib_ubuffer  ubuffer;
	struct mlx5_db		*doorbell;
	struct mlx5_flow_handle	*flow_rule;
	u32			tisn;
	u8			state;
};

struct mlx5_ib_raw_packet_qp {
	struct mlx5_ib_sq sq;
	struct mlx5_ib_rq rq;
};

struct mlx5_bf {
	int			buf_size;
	unsigned long		offset;
	struct mlx5_sq_bfreg   *bfreg;
};

struct mlx5_ib_dct {
	struct mlx5_core_dct    mdct;
	u32                     *in;
};

struct mlx5_ib_qp {
	struct ib_qp		ibqp;
	union {
		struct mlx5_ib_qp_trans trans_qp;
		struct mlx5_ib_raw_packet_qp raw_packet_qp;
		struct mlx5_ib_rss_qp rss_qp;
		struct mlx5_ib_dct dct;
	};
	struct mlx5_frag_buf	buf;

	struct mlx5_db		db;
	struct mlx5_ib_wq	rq;

	u8			sq_signal_bits;
	u8			next_fence;
	struct mlx5_ib_wq	sq;

	/* serialize qp state modifications
	 */
	struct mutex		mutex;
	u32			flags;
	u8			port;
	u8			state;
	int			wq_sig;
	int			scat_cqe;
	int			max_inline_data;
	struct mlx5_bf	        bf;
	int			has_rq;

	/* only for user space QPs. For kernel
	 * we have it from the bf object
	 */
	int			bfregn;

	int			create_type;

	/* Store signature errors */
	bool			signature_en;

	struct list_head	qps_list;
	struct list_head	cq_recv_list;
	struct list_head	cq_send_list;
	struct mlx5_rate_limit	rl;
	u32                     underlay_qpn;
	u32			flags_en;
	/* storage for qp sub type when core qp type is IB_QPT_DRIVER */
	enum ib_qp_type		qp_sub_type;
};

struct mlx5_ib_cq_buf {
	struct mlx5_frag_buf_ctrl fbc;
	struct mlx5_frag_buf    frag_buf;
	struct ib_umem		*umem;
	int			cqe_size;
	int			nent;
};

enum mlx5_ib_qp_flags {
	MLX5_IB_QP_LSO                          = IB_QP_CREATE_IPOIB_UD_LSO,
	MLX5_IB_QP_BLOCK_MULTICAST_LOOPBACK     = IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK,
	MLX5_IB_QP_CROSS_CHANNEL            = IB_QP_CREATE_CROSS_CHANNEL,
	MLX5_IB_QP_MANAGED_SEND             = IB_QP_CREATE_MANAGED_SEND,
	MLX5_IB_QP_MANAGED_RECV             = IB_QP_CREATE_MANAGED_RECV,
	MLX5_IB_QP_SIGNATURE_HANDLING           = 1 << 5,
	/* QP uses 1 as its source QP number */
	MLX5_IB_QP_SQPN_QP1			= 1 << 6,
	MLX5_IB_QP_CAP_SCATTER_FCS		= 1 << 7,
	MLX5_IB_QP_RSS				= 1 << 8,
	MLX5_IB_QP_CVLAN_STRIPPING		= 1 << 9,
	MLX5_IB_QP_UNDERLAY			= 1 << 10,
	MLX5_IB_QP_PCI_WRITE_END_PADDING	= 1 << 11,
	MLX5_IB_QP_TUNNEL_OFFLOAD		= 1 << 12,
};

struct mlx5_umr_wr {
	struct ib_send_wr		wr;
	u64				virt_addr;
	u64				offset;
	struct ib_pd		       *pd;
	unsigned int			page_shift;
	unsigned int			xlt_size;
	u64				length;
	int				access_flags;
	u32				mkey;
};

static inline const struct mlx5_umr_wr *umr_wr(const struct ib_send_wr *wr)
{
	return container_of(wr, struct mlx5_umr_wr, wr);
}

struct mlx5_shared_mr_info {
	int mr_id;
	struct ib_umem		*umem;
};

enum mlx5_ib_cq_pr_flags {
	MLX5_IB_CQ_PR_FLAGS_CQE_128_PAD	= 1 << 0,
};

struct mlx5_ib_cq {
	struct ib_cq		ibcq;
	struct mlx5_core_cq	mcq;
	struct mlx5_ib_cq_buf	buf;
	struct mlx5_db		db;

	/* serialize access to the CQ
	 */
	spinlock_t		lock;

	/* protect resize cq
	 */
	struct mutex		resize_mutex;
	struct mlx5_ib_cq_buf  *resize_buf;
	struct ib_umem	       *resize_umem;
	int			cqe_size;
	struct list_head	list_send_qp;
	struct list_head	list_recv_qp;
	u32			create_flags;
	struct list_head	wc_list;
	enum ib_cq_notify_flags notify_flags;
	struct work_struct	notify_work;
	u16			private_flags; /* Use mlx5_ib_cq_pr_flags */
};

struct mlx5_ib_wc {
	struct ib_wc wc;
	struct list_head list;
};

struct mlx5_ib_srq {
	struct ib_srq		ibsrq;
	struct mlx5_core_srq	msrq;
	struct mlx5_frag_buf	buf;
	struct mlx5_db		db;
	u64		       *wrid;
	/* protect SRQ hanlding
	 */
	spinlock_t		lock;
	int			head;
	int			tail;
	u16			wqe_ctr;
	struct ib_umem	       *umem;
	/* serialize arming a SRQ
	 */
	struct mutex		mutex;
	int			wq_sig;
};

struct mlx5_ib_xrcd {
	struct ib_xrcd		ibxrcd;
	u32			xrcdn;
	u16			uid;
};

enum mlx5_ib_mtt_access_flags {
	MLX5_IB_MTT_READ  = (1 << 0),
	MLX5_IB_MTT_WRITE = (1 << 1),
};

struct mlx5_ib_dm {
	struct ib_dm		ibdm;
	phys_addr_t		dev_addr;
};

#define MLX5_IB_MTT_PRESENT (MLX5_IB_MTT_READ | MLX5_IB_MTT_WRITE)

#define MLX5_IB_DM_ALLOWED_ACCESS (IB_ACCESS_LOCAL_WRITE   |\
				   IB_ACCESS_REMOTE_WRITE  |\
				   IB_ACCESS_REMOTE_READ   |\
				   IB_ACCESS_REMOTE_ATOMIC |\
				   IB_ZERO_BASED)

struct mlx5_ib_mr {
	struct ib_mr		ibmr;
	void			*descs;
	dma_addr_t		desc_map;
	int			ndescs;
	int			max_descs;
	int			desc_size;
	int			access_mode;
	struct mlx5_core_mkey	mmkey;
	struct ib_umem	       *umem;
	struct mlx5_shared_mr_info	*smr_info;
	struct list_head	list;
	int			order;
	bool			allocated_from_cache;
	int			npages;
	struct mlx5_ib_dev     *dev;
	u32 out[MLX5_ST_SZ_DW(create_mkey_out)];
	struct mlx5_core_sig_ctx    *sig;
	int			live;
	void			*descs_alloc;
	int			access_flags; /* Needed for rereg MR */

	struct mlx5_ib_mr      *parent;
	atomic_t		num_leaf_free;
	wait_queue_head_t       q_leaf_free;
};

struct mlx5_ib_mw {
	struct ib_mw		ibmw;
	struct mlx5_core_mkey	mmkey;
	int			ndescs;
};

struct mlx5_ib_umr_context {
	struct ib_cqe		cqe;
	enum ib_wc_status	status;
	struct completion	done;
};

struct umr_common {
	struct ib_pd	*pd;
	struct ib_cq	*cq;
	struct ib_qp	*qp;
	/* control access to UMR QP
	 */
	struct semaphore	sem;
};

enum {
	MLX5_FMR_INVALID,
	MLX5_FMR_VALID,
	MLX5_FMR_BUSY,
};

struct mlx5_cache_ent {
	struct list_head	head;
	/* sync access to the cahce entry
	 */
	spinlock_t		lock;


	struct dentry	       *dir;
	char                    name[4];
	u32                     order;
	u32			xlt;
	u32			access_mode;
	u32			page;

	u32			size;
	u32                     cur;
	u32                     miss;
	u32			limit;

	struct dentry          *fsize;
	struct dentry          *fcur;
	struct dentry          *fmiss;
	struct dentry          *flimit;

	struct mlx5_ib_dev     *dev;
	struct work_struct	work;
	struct delayed_work	dwork;
	int			pending;
	struct completion	compl;
};

struct mlx5_mr_cache {
	struct workqueue_struct *wq;
	struct mlx5_cache_ent	ent[MAX_MR_CACHE_ENTRIES];
	int			stopped;
	struct dentry		*root;
	unsigned long		last_add;
};

struct mlx5_ib_gsi_qp;

struct mlx5_ib_port_resources {
	struct mlx5_ib_resources *devr;
	struct mlx5_ib_gsi_qp *gsi;
	struct work_struct pkey_change_work;
};

struct mlx5_ib_resources {
	struct ib_cq	*c0;
	struct ib_xrcd	*x0;
	struct ib_xrcd	*x1;
	struct ib_pd	*p0;
	struct ib_srq	*s0;
	struct ib_srq	*s1;
	struct mlx5_ib_port_resources ports[2];
	/* Protects changes to the port resources */
	struct mutex	mutex;
};

struct mlx5_ib_counters {
	const char **names;
	size_t *offsets;
	u32 num_q_counters;
	u32 num_cong_counters;
	u32 num_ext_ppcnt_counters;
	u16 set_id;
	bool set_id_valid;
};

struct mlx5_ib_multiport_info;

struct mlx5_ib_multiport {
	struct mlx5_ib_multiport_info *mpi;
	/* To be held when accessing the multiport info */
	spinlock_t mpi_lock;
};

struct mlx5_ib_port {
	struct mlx5_ib_counters cnts;
	struct mlx5_ib_multiport mp;
	struct mlx5_ib_dbg_cc_params	*dbg_cc_params;
};

struct mlx5_roce {
	/* Protect mlx5_ib_get_netdev from invoking dev_hold() with a NULL
	 * netdev pointer
	 */
	rwlock_t		netdev_lock;
	struct net_device	*netdev;
	struct notifier_block	nb;
	atomic_t		tx_port_affinity;
	enum ib_port_state last_port_state;
	struct mlx5_ib_dev	*dev;
	u8			native_port_num;
};

struct mlx5_ib_dbg_param {
	int			offset;
	struct mlx5_ib_dev	*dev;
	struct dentry		*dentry;
	u8			port_num;
};

enum mlx5_ib_dbg_cc_types {
	MLX5_IB_DBG_CC_RP_CLAMP_TGT_RATE,
	MLX5_IB_DBG_CC_RP_CLAMP_TGT_RATE_ATI,
	MLX5_IB_DBG_CC_RP_TIME_RESET,
	MLX5_IB_DBG_CC_RP_BYTE_RESET,
	MLX5_IB_DBG_CC_RP_THRESHOLD,
	MLX5_IB_DBG_CC_RP_AI_RATE,
	MLX5_IB_DBG_CC_RP_HAI_RATE,
	MLX5_IB_DBG_CC_RP_MIN_DEC_FAC,
	MLX5_IB_DBG_CC_RP_MIN_RATE,
	MLX5_IB_DBG_CC_RP_RATE_TO_SET_ON_FIRST_CNP,
	MLX5_IB_DBG_CC_RP_DCE_TCP_G,
	MLX5_IB_DBG_CC_RP_DCE_TCP_RTT,
	MLX5_IB_DBG_CC_RP_RATE_REDUCE_MONITOR_PERIOD,
	MLX5_IB_DBG_CC_RP_INITIAL_ALPHA_VALUE,
	MLX5_IB_DBG_CC_RP_GD,
	MLX5_IB_DBG_CC_NP_CNP_DSCP,
	MLX5_IB_DBG_CC_NP_CNP_PRIO_MODE,
	MLX5_IB_DBG_CC_NP_CNP_PRIO,
	MLX5_IB_DBG_CC_MAX,
};

struct mlx5_ib_dbg_cc_params {
	struct dentry			*root;
	struct mlx5_ib_dbg_param	params[MLX5_IB_DBG_CC_MAX];
};

enum {
	MLX5_MAX_DELAY_DROP_TIMEOUT_MS = 100,
};

struct mlx5_ib_dbg_delay_drop {
	struct dentry		*dir_debugfs;
	struct dentry		*rqs_cnt_debugfs;
	struct dentry		*events_cnt_debugfs;
	struct dentry		*timeout_debugfs;
};

struct mlx5_ib_delay_drop {
	struct mlx5_ib_dev     *dev;
	struct work_struct	delay_drop_work;
	/* serialize setting of delay drop */
	struct mutex		lock;
	u32			timeout;
	bool			activate;
	atomic_t		events_cnt;
	atomic_t		rqs_cnt;
	struct mlx5_ib_dbg_delay_drop *dbg;
};

enum mlx5_ib_stages {
	MLX5_IB_STAGE_INIT,
	MLX5_IB_STAGE_FLOW_DB,
	MLX5_IB_STAGE_CAPS,
	MLX5_IB_STAGE_NON_DEFAULT_CB,
	MLX5_IB_STAGE_ROCE,
	MLX5_IB_STAGE_DEVICE_RESOURCES,
	MLX5_IB_STAGE_ODP,
	MLX5_IB_STAGE_COUNTERS,
	MLX5_IB_STAGE_CONG_DEBUGFS,
	MLX5_IB_STAGE_UAR,
	MLX5_IB_STAGE_BFREG,
	MLX5_IB_STAGE_PRE_IB_REG_UMR,
	MLX5_IB_STAGE_SPECS,
	MLX5_IB_STAGE_IB_REG,
	MLX5_IB_STAGE_POST_IB_REG_UMR,
	MLX5_IB_STAGE_DELAY_DROP,
	MLX5_IB_STAGE_CLASS_ATTR,
	MLX5_IB_STAGE_REP_REG,
	MLX5_IB_STAGE_MAX,
};

struct mlx5_ib_stage {
	int (*init)(struct mlx5_ib_dev *dev);
	void (*cleanup)(struct mlx5_ib_dev *dev);
};

#define STAGE_CREATE(_stage, _init, _cleanup) \
	.stage[_stage] = {.init = _init, .cleanup = _cleanup}

struct mlx5_ib_profile {
	struct mlx5_ib_stage stage[MLX5_IB_STAGE_MAX];
};

struct mlx5_ib_multiport_info {
	struct list_head list;
	struct mlx5_ib_dev *ibdev;
	struct mlx5_core_dev *mdev;
	struct completion unref_comp;
	u64 sys_image_guid;
	u32 mdev_refcnt;
	bool is_master;
	bool unaffiliate;
};

struct mlx5_ib_flow_action {
	struct ib_flow_action		ib_action;
	union {
		struct {
			u64			    ib_flags;
			struct mlx5_accel_esp_xfrm *ctx;
		} esp_aes_gcm;
		struct {
			struct mlx5_ib_dev *dev;
			u32 sub_type;
			u32 action_id;
		} flow_action_raw;
	};
};

struct mlx5_memic {
	struct mlx5_core_dev *dev;
	spinlock_t		memic_lock;
	DECLARE_BITMAP(memic_alloc_pages, MLX5_MAX_MEMIC_PAGES);
};

struct mlx5_read_counters_attr {
	struct mlx5_fc *hw_cntrs_hndl;
	u64 *out;
	u32 flags;
};

enum mlx5_ib_counters_type {
	MLX5_IB_COUNTERS_FLOW,
};

struct mlx5_ib_mcounters {
	struct ib_counters ibcntrs;
	enum mlx5_ib_counters_type type;
	/* number of counters supported for this counters type */
	u32 counters_num;
	struct mlx5_fc *hw_cntrs_hndl;
	/* read function for this counters type */
	int (*read_counters)(struct ib_device *ibdev,
			     struct mlx5_read_counters_attr *read_attr);
	/* max index set as part of create_flow */
	u32 cntrs_max_index;
	/* number of counters data entries (<description,index> pair) */
	u32 ncounters;
	/* counters data array for descriptions and indexes */
	struct mlx5_ib_flow_counters_desc *counters_data;
	/* protects access to mcounters internal data */
	struct mutex mcntrs_mutex;
};

static inline struct mlx5_ib_mcounters *
to_mcounters(struct ib_counters *ibcntrs)
{
	return container_of(ibcntrs, struct mlx5_ib_mcounters, ibcntrs);
}

int parse_flow_flow_action(struct mlx5_ib_flow_action *maction,
			   bool is_egress,
			   struct mlx5_flow_act *action);
struct mlx5_ib_lb_state {
	/* protect the user_td */
	struct mutex		mutex;
	u32			user_td;
	int			qps;
	bool			enabled;
};

struct mlx5_ib_dev {
	struct ib_device		ib_dev;
	const struct uverbs_object_tree_def *driver_trees[7];
	struct mlx5_core_dev		*mdev;
	struct mlx5_roce		roce[MLX5_MAX_PORTS];
	int				num_ports;
	/* serialize update of capability mask
	 */
	struct mutex			cap_mask_mutex;
	bool				ib_active;
	struct umr_common		umrc;
	/* sync used page count stats
	 */
	struct mlx5_ib_resources	devr;
	struct mlx5_mr_cache		cache;
	struct timer_list		delay_timer;
	/* Prevents soft lock on massive reg MRs */
	struct mutex			slow_path_mutex;
	int				fill_delay;
#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
	struct ib_odp_caps	odp_caps;
	u64			odp_max_size;
	/*
	 * Sleepable RCU that prevents destruction of MRs while they are still
	 * being used by a page fault handler.
	 */
	struct srcu_struct      mr_srcu;
	u32			null_mkey;
#endif
	struct mlx5_ib_flow_db	*flow_db;
	/* protect resources needed as part of reset flow */
	spinlock_t		reset_flow_resource_lock;
	struct list_head	qp_list;
	/* Array with num_ports elements */
	struct mlx5_ib_port	*port;
	struct mlx5_sq_bfreg	bfreg;
	struct mlx5_sq_bfreg	fp_bfreg;
	struct mlx5_ib_delay_drop	delay_drop;
	const struct mlx5_ib_profile	*profile;
	struct mlx5_eswitch_rep		*rep;

	struct mlx5_ib_lb_state		lb;
	u8			umr_fence;
	struct list_head	ib_dev_list;
	u64			sys_image_guid;
	struct mlx5_memic	memic;
	u16			devx_whitelist_uid;
};

static inline struct mlx5_ib_cq *to_mibcq(struct mlx5_core_cq *mcq)
{
	return container_of(mcq, struct mlx5_ib_cq, mcq);
}

static inline struct mlx5_ib_xrcd *to_mxrcd(struct ib_xrcd *ibxrcd)
{
	return container_of(ibxrcd, struct mlx5_ib_xrcd, ibxrcd);
}

static inline struct mlx5_ib_dev *to_mdev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct mlx5_ib_dev, ib_dev);
}

static inline struct mlx5_ib_cq *to_mcq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct mlx5_ib_cq, ibcq);
}

static inline struct mlx5_ib_qp *to_mibqp(struct mlx5_core_qp *mqp)
{
	return container_of(mqp, struct mlx5_ib_qp_base, mqp)->container_mibqp;
}

static inline struct mlx5_ib_rwq *to_mibrwq(struct mlx5_core_qp *core_qp)
{
	return container_of(core_qp, struct mlx5_ib_rwq, core_qp);
}

static inline struct mlx5_ib_mr *to_mibmr(struct mlx5_core_mkey *mmkey)
{
	return container_of(mmkey, struct mlx5_ib_mr, mmkey);
}

static inline struct mlx5_ib_pd *to_mpd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct mlx5_ib_pd, ibpd);
}

static inline struct mlx5_ib_srq *to_msrq(struct ib_srq *ibsrq)
{
	return container_of(ibsrq, struct mlx5_ib_srq, ibsrq);
}

static inline struct mlx5_ib_qp *to_mqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct mlx5_ib_qp, ibqp);
}

static inline struct mlx5_ib_rwq *to_mrwq(struct ib_wq *ibwq)
{
	return container_of(ibwq, struct mlx5_ib_rwq, ibwq);
}

static inline struct mlx5_ib_rwq_ind_table *to_mrwq_ind_table(struct ib_rwq_ind_table *ib_rwq_ind_tbl)
{
	return container_of(ib_rwq_ind_tbl, struct mlx5_ib_rwq_ind_table, ib_rwq_ind_tbl);
}

static inline struct mlx5_ib_srq *to_mibsrq(struct mlx5_core_srq *msrq)
{
	return container_of(msrq, struct mlx5_ib_srq, msrq);
}

static inline struct mlx5_ib_dm *to_mdm(struct ib_dm *ibdm)
{
	return container_of(ibdm, struct mlx5_ib_dm, ibdm);
}

static inline struct mlx5_ib_mr *to_mmr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct mlx5_ib_mr, ibmr);
}

static inline struct mlx5_ib_mw *to_mmw(struct ib_mw *ibmw)
{
	return container_of(ibmw, struct mlx5_ib_mw, ibmw);
}

static inline struct mlx5_ib_flow_action *
to_mflow_act(struct ib_flow_action *ibact)
{
	return container_of(ibact, struct mlx5_ib_flow_action, ib_action);
}

int mlx5_ib_db_map_user(struct mlx5_ib_ucontext *context, unsigned long virt,
			struct mlx5_db *db);
void mlx5_ib_db_unmap_user(struct mlx5_ib_ucontext *context, struct mlx5_db *db);
void __mlx5_ib_cq_clean(struct mlx5_ib_cq *cq, u32 qpn, struct mlx5_ib_srq *srq);
void mlx5_ib_cq_clean(struct mlx5_ib_cq *cq, u32 qpn, struct mlx5_ib_srq *srq);
void mlx5_ib_free_srq_wqe(struct mlx5_ib_srq *srq, int wqe_index);
int mlx5_MAD_IFC(struct mlx5_ib_dev *dev, int ignore_mkey, int ignore_bkey,
		 u8 port, const struct ib_wc *in_wc, const struct ib_grh *in_grh,
		 const void *in_mad, void *response_mad);
struct ib_ah *mlx5_ib_create_ah(struct ib_pd *pd, struct rdma_ah_attr *ah_attr,
				struct ib_udata *udata);
int mlx5_ib_query_ah(struct ib_ah *ibah, struct rdma_ah_attr *ah_attr);
int mlx5_ib_destroy_ah(struct ib_ah *ah);
struct ib_srq *mlx5_ib_create_srq(struct ib_pd *pd,
				  struct ib_srq_init_attr *init_attr,
				  struct ib_udata *udata);
int mlx5_ib_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		       enum ib_srq_attr_mask attr_mask, struct ib_udata *udata);
int mlx5_ib_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *srq_attr);
int mlx5_ib_destroy_srq(struct ib_srq *srq);
int mlx5_ib_post_srq_recv(struct ib_srq *ibsrq, const struct ib_recv_wr *wr,
			  const struct ib_recv_wr **bad_wr);
int mlx5_ib_enable_lb(struct mlx5_ib_dev *dev, bool td, bool qp);
void mlx5_ib_disable_lb(struct mlx5_ib_dev *dev, bool td, bool qp);
struct ib_qp *mlx5_ib_create_qp(struct ib_pd *pd,
				struct ib_qp_init_attr *init_attr,
				struct ib_udata *udata);
int mlx5_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		      int attr_mask, struct ib_udata *udata);
int mlx5_ib_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr, int qp_attr_mask,
		     struct ib_qp_init_attr *qp_init_attr);
int mlx5_ib_destroy_qp(struct ib_qp *qp);
void mlx5_ib_drain_sq(struct ib_qp *qp);
void mlx5_ib_drain_rq(struct ib_qp *qp);
int mlx5_ib_post_send(struct ib_qp *ibqp, const struct ib_send_wr *wr,
		      const struct ib_send_wr **bad_wr);
int mlx5_ib_post_recv(struct ib_qp *ibqp, const struct ib_recv_wr *wr,
		      const struct ib_recv_wr **bad_wr);
void *mlx5_get_send_wqe(struct mlx5_ib_qp *qp, int n);
int mlx5_ib_read_user_wqe(struct mlx5_ib_qp *qp, int send, int wqe_index,
			  void *buffer, u32 length,
			  struct mlx5_ib_qp_base *base);
struct ib_cq *mlx5_ib_create_cq(struct ib_device *ibdev,
				const struct ib_cq_init_attr *attr,
				struct ib_ucontext *context,
				struct ib_udata *udata);
int mlx5_ib_destroy_cq(struct ib_cq *cq);
int mlx5_ib_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc);
int mlx5_ib_arm_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags);
int mlx5_ib_modify_cq(struct ib_cq *cq, u16 cq_count, u16 cq_period);
int mlx5_ib_resize_cq(struct ib_cq *ibcq, int entries, struct ib_udata *udata);
struct ib_mr *mlx5_ib_get_dma_mr(struct ib_pd *pd, int acc);
struct ib_mr *mlx5_ib_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				  u64 virt_addr, int access_flags,
				  struct ib_udata *udata);
struct ib_mw *mlx5_ib_alloc_mw(struct ib_pd *pd, enum ib_mw_type type,
			       struct ib_udata *udata);
int mlx5_ib_dealloc_mw(struct ib_mw *mw);
int mlx5_ib_update_xlt(struct mlx5_ib_mr *mr, u64 idx, int npages,
		       int page_shift, int flags);
struct mlx5_ib_mr *mlx5_ib_alloc_implicit_mr(struct mlx5_ib_pd *pd,
					     int access_flags);
void mlx5_ib_free_implicit_mr(struct mlx5_ib_mr *mr);
int mlx5_ib_rereg_user_mr(struct ib_mr *ib_mr, int flags, u64 start,
			  u64 length, u64 virt_addr, int access_flags,
			  struct ib_pd *pd, struct ib_udata *udata);
int mlx5_ib_dereg_mr(struct ib_mr *ibmr);
struct ib_mr *mlx5_ib_alloc_mr(struct ib_pd *pd,
			       enum ib_mr_type mr_type,
			       u32 max_num_sg);
int mlx5_ib_map_mr_sg(struct ib_mr *ibmr, struct scatterlist *sg, int sg_nents,
		      unsigned int *sg_offset);
int mlx5_ib_process_mad(struct ib_device *ibdev, int mad_flags, u8 port_num,
			const struct ib_wc *in_wc, const struct ib_grh *in_grh,
			const struct ib_mad_hdr *in, size_t in_mad_size,
			struct ib_mad_hdr *out, size_t *out_mad_size,
			u16 *out_mad_pkey_index);
struct ib_xrcd *mlx5_ib_alloc_xrcd(struct ib_device *ibdev,
					  struct ib_ucontext *context,
					  struct ib_udata *udata);
int mlx5_ib_dealloc_xrcd(struct ib_xrcd *xrcd);
int mlx5_ib_get_buf_offset(u64 addr, int page_shift, u32 *offset);
int mlx5_query_ext_port_caps(struct mlx5_ib_dev *dev, u8 port);
int mlx5_query_mad_ifc_smp_attr_node_info(struct ib_device *ibdev,
					  struct ib_smp *out_mad);
int mlx5_query_mad_ifc_system_image_guid(struct ib_device *ibdev,
					 __be64 *sys_image_guid);
int mlx5_query_mad_ifc_max_pkeys(struct ib_device *ibdev,
				 u16 *max_pkeys);
int mlx5_query_mad_ifc_vendor_id(struct ib_device *ibdev,
				 u32 *vendor_id);
int mlx5_query_mad_ifc_node_desc(struct mlx5_ib_dev *dev, char *node_desc);
int mlx5_query_mad_ifc_node_guid(struct mlx5_ib_dev *dev, __be64 *node_guid);
int mlx5_query_mad_ifc_pkey(struct ib_device *ibdev, u8 port, u16 index,
			    u16 *pkey);
int mlx5_query_mad_ifc_gids(struct ib_device *ibdev, u8 port, int index,
			    union ib_gid *gid);
int mlx5_query_mad_ifc_port(struct ib_device *ibdev, u8 port,
			    struct ib_port_attr *props);
int mlx5_ib_query_port(struct ib_device *ibdev, u8 port,
		       struct ib_port_attr *props);
int mlx5_ib_init_fmr(struct mlx5_ib_dev *dev);
void mlx5_ib_cleanup_fmr(struct mlx5_ib_dev *dev);
void mlx5_ib_cont_pages(struct ib_umem *umem, u64 addr,
			unsigned long max_page_shift,
			int *count, int *shift,
			int *ncont, int *order);
void __mlx5_ib_populate_pas(struct mlx5_ib_dev *dev, struct ib_umem *umem,
			    int page_shift, size_t offset, size_t num_pages,
			    __be64 *pas, int access_flags);
void mlx5_ib_populate_pas(struct mlx5_ib_dev *dev, struct ib_umem *umem,
			  int page_shift, __be64 *pas, int access_flags);
void mlx5_ib_copy_pas(u64 *old, u64 *new, int step, int num);
int mlx5_ib_get_cqe_size(struct ib_cq *ibcq);
int mlx5_mr_cache_init(struct mlx5_ib_dev *dev);
int mlx5_mr_cache_cleanup(struct mlx5_ib_dev *dev);

struct mlx5_ib_mr *mlx5_mr_cache_alloc(struct mlx5_ib_dev *dev, int entry);
void mlx5_mr_cache_free(struct mlx5_ib_dev *dev, struct mlx5_ib_mr *mr);
int mlx5_ib_check_mr_status(struct ib_mr *ibmr, u32 check_mask,
			    struct ib_mr_status *mr_status);
struct ib_wq *mlx5_ib_create_wq(struct ib_pd *pd,
				struct ib_wq_init_attr *init_attr,
				struct ib_udata *udata);
int mlx5_ib_destroy_wq(struct ib_wq *wq);
int mlx5_ib_modify_wq(struct ib_wq *wq, struct ib_wq_attr *wq_attr,
		      u32 wq_attr_mask, struct ib_udata *udata);
struct ib_rwq_ind_table *mlx5_ib_create_rwq_ind_table(struct ib_device *device,
						      struct ib_rwq_ind_table_init_attr *init_attr,
						      struct ib_udata *udata);
int mlx5_ib_destroy_rwq_ind_table(struct ib_rwq_ind_table *wq_ind_table);
bool mlx5_ib_dc_atomic_is_supported(struct mlx5_ib_dev *dev);
struct ib_dm *mlx5_ib_alloc_dm(struct ib_device *ibdev,
			       struct ib_ucontext *context,
			       struct ib_dm_alloc_attr *attr,
			       struct uverbs_attr_bundle *attrs);
int mlx5_ib_dealloc_dm(struct ib_dm *ibdm);
struct ib_mr *mlx5_ib_reg_dm_mr(struct ib_pd *pd, struct ib_dm *dm,
				struct ib_dm_mr_attr *attr,
				struct uverbs_attr_bundle *attrs);

#ifdef CONFIG_INFINIBAND_ON_DEMAND_PAGING
void mlx5_ib_internal_fill_odp_caps(struct mlx5_ib_dev *dev);
void mlx5_ib_pfault(struct mlx5_core_dev *mdev, void *context,
		    struct mlx5_pagefault *pfault);
int mlx5_ib_odp_init_one(struct mlx5_ib_dev *ibdev);
int __init mlx5_ib_odp_init(void);
void mlx5_ib_odp_cleanup(void);
void mlx5_ib_invalidate_range(struct ib_umem_odp *umem_odp, unsigned long start,
			      unsigned long end);
void mlx5_odp_init_mr_cache_entry(struct mlx5_cache_ent *ent);
void mlx5_odp_populate_klm(struct mlx5_klm *pklm, size_t offset,
			   size_t nentries, struct mlx5_ib_mr *mr, int flags);
#else /* CONFIG_INFINIBAND_ON_DEMAND_PAGING */
static inline void mlx5_ib_internal_fill_odp_caps(struct mlx5_ib_dev *dev)
{
	return;
}

static inline int mlx5_ib_odp_init_one(struct mlx5_ib_dev *ibdev) { return 0; }
static inline int mlx5_ib_odp_init(void) { return 0; }
static inline void mlx5_ib_odp_cleanup(void)				    {}
static inline void mlx5_odp_init_mr_cache_entry(struct mlx5_cache_ent *ent) {}
static inline void mlx5_odp_populate_klm(struct mlx5_klm *pklm, size_t offset,
					 size_t nentries, struct mlx5_ib_mr *mr,
					 int flags) {}

#endif /* CONFIG_INFINIBAND_ON_DEMAND_PAGING */

/* Needed for rep profile */
int mlx5_ib_stage_init_init(struct mlx5_ib_dev *dev);
void mlx5_ib_stage_init_cleanup(struct mlx5_ib_dev *dev);
int mlx5_ib_stage_rep_flow_db_init(struct mlx5_ib_dev *dev);
int mlx5_ib_stage_caps_init(struct mlx5_ib_dev *dev);
int mlx5_ib_stage_rep_non_default_cb(struct mlx5_ib_dev *dev);
int mlx5_ib_stage_rep_roce_init(struct mlx5_ib_dev *dev);
void mlx5_ib_stage_rep_roce_cleanup(struct mlx5_ib_dev *dev);
int mlx5_ib_stage_dev_res_init(struct mlx5_ib_dev *dev);
void mlx5_ib_stage_dev_res_cleanup(struct mlx5_ib_dev *dev);
int mlx5_ib_stage_counters_init(struct mlx5_ib_dev *dev);
void mlx5_ib_stage_counters_cleanup(struct mlx5_ib_dev *dev);
int mlx5_ib_stage_bfrag_init(struct mlx5_ib_dev *dev);
void mlx5_ib_stage_bfrag_cleanup(struct mlx5_ib_dev *dev);
void mlx5_ib_stage_pre_ib_reg_umr_cleanup(struct mlx5_ib_dev *dev);
int mlx5_ib_stage_ib_reg_init(struct mlx5_ib_dev *dev);
void mlx5_ib_stage_ib_reg_cleanup(struct mlx5_ib_dev *dev);
int mlx5_ib_stage_post_ib_reg_umr_init(struct mlx5_ib_dev *dev);
void __mlx5_ib_remove(struct mlx5_ib_dev *dev,
		      const struct mlx5_ib_profile *profile,
		      int stage);
void *__mlx5_ib_add(struct mlx5_ib_dev *dev,
		    const struct mlx5_ib_profile *profile);

int mlx5_ib_get_vf_config(struct ib_device *device, int vf,
			  u8 port, struct ifla_vf_info *info);
int mlx5_ib_set_vf_link_state(struct ib_device *device, int vf,
			      u8 port, int state);
int mlx5_ib_get_vf_stats(struct ib_device *device, int vf,
			 u8 port, struct ifla_vf_stats *stats);
int mlx5_ib_set_vf_guid(struct ib_device *device, int vf, u8 port,
			u64 guid, int type);

__be16 mlx5_get_roce_udp_sport(struct mlx5_ib_dev *dev,
			       const struct ib_gid_attr *attr);

void mlx5_ib_cleanup_cong_debugfs(struct mlx5_ib_dev *dev, u8 port_num);
int mlx5_ib_init_cong_debugfs(struct mlx5_ib_dev *dev, u8 port_num);

/* GSI QP helper functions */
struct ib_qp *mlx5_ib_gsi_create_qp(struct ib_pd *pd,
				    struct ib_qp_init_attr *init_attr);
int mlx5_ib_gsi_destroy_qp(struct ib_qp *qp);
int mlx5_ib_gsi_modify_qp(struct ib_qp *qp, struct ib_qp_attr *attr,
			  int attr_mask);
int mlx5_ib_gsi_query_qp(struct ib_qp *qp, struct ib_qp_attr *qp_attr,
			 int qp_attr_mask,
			 struct ib_qp_init_attr *qp_init_attr);
int mlx5_ib_gsi_post_send(struct ib_qp *qp, const struct ib_send_wr *wr,
			  const struct ib_send_wr **bad_wr);
int mlx5_ib_gsi_post_recv(struct ib_qp *qp, const struct ib_recv_wr *wr,
			  const struct ib_recv_wr **bad_wr);
void mlx5_ib_gsi_pkey_change(struct mlx5_ib_gsi_qp *gsi);

int mlx5_ib_generate_wc(struct ib_cq *ibcq, struct ib_wc *wc);

void mlx5_ib_free_bfreg(struct mlx5_ib_dev *dev, struct mlx5_bfreg_info *bfregi,
			int bfregn);
struct mlx5_ib_dev *mlx5_ib_get_ibdev_from_mpi(struct mlx5_ib_multiport_info *mpi);
struct mlx5_core_dev *mlx5_ib_get_native_port_mdev(struct mlx5_ib_dev *dev,
						   u8 ib_port_num,
						   u8 *native_port_num);
void mlx5_ib_put_native_port_mdev(struct mlx5_ib_dev *dev,
				  u8 port_num);

#if IS_ENABLED(CONFIG_INFINIBAND_USER_ACCESS)
int mlx5_ib_devx_create(struct mlx5_ib_dev *dev);
void mlx5_ib_devx_destroy(struct mlx5_ib_dev *dev, u16 uid);
const struct uverbs_object_tree_def *mlx5_ib_get_devx_tree(void);
struct mlx5_ib_flow_handler *mlx5_ib_raw_fs_rule_add(
	struct mlx5_ib_dev *dev, struct mlx5_ib_flow_matcher *fs_matcher,
	struct mlx5_flow_act *flow_act, void *cmd_in, int inlen,
	int dest_id, int dest_type);
bool mlx5_ib_devx_is_flow_dest(void *obj, int *dest_id, int *dest_type);
int mlx5_ib_get_flow_trees(const struct uverbs_object_tree_def **root);
void mlx5_ib_destroy_flow_action_raw(struct mlx5_ib_flow_action *maction);
#else
static inline int
mlx5_ib_devx_create(struct mlx5_ib_dev *dev) { return -EOPNOTSUPP; };
static inline void mlx5_ib_devx_destroy(struct mlx5_ib_dev *dev, u16 uid) {}
static inline const struct uverbs_object_tree_def *
mlx5_ib_get_devx_tree(void) { return NULL; }
static inline bool mlx5_ib_devx_is_flow_dest(void *obj, int *dest_id,
					     int *dest_type)
{
	return false;
}
static inline int
mlx5_ib_get_flow_trees(const struct uverbs_object_tree_def **root)
{
	return 0;
}
static inline void
mlx5_ib_destroy_flow_action_raw(struct mlx5_ib_flow_action *maction)
{
	return;
};
#endif
static inline void init_query_mad(struct ib_smp *mad)
{
	mad->base_version  = 1;
	mad->mgmt_class    = IB_MGMT_CLASS_SUBN_LID_ROUTED;
	mad->class_version = 1;
	mad->method	   = IB_MGMT_METHOD_GET;
}

static inline u8 convert_access(int acc)
{
	return (acc & IB_ACCESS_REMOTE_ATOMIC ? MLX5_PERM_ATOMIC       : 0) |
	       (acc & IB_ACCESS_REMOTE_WRITE  ? MLX5_PERM_REMOTE_WRITE : 0) |
	       (acc & IB_ACCESS_REMOTE_READ   ? MLX5_PERM_REMOTE_READ  : 0) |
	       (acc & IB_ACCESS_LOCAL_WRITE   ? MLX5_PERM_LOCAL_WRITE  : 0) |
	       MLX5_PERM_LOCAL_READ;
}

static inline int is_qp1(enum ib_qp_type qp_type)
{
	return qp_type == MLX5_IB_QPT_HW_GSI;
}

#define MLX5_MAX_UMR_SHIFT 16
#define MLX5_MAX_UMR_PAGES (1 << MLX5_MAX_UMR_SHIFT)

static inline u32 check_cq_create_flags(u32 flags)
{
	/*
	 * It returns non-zero value for unsupported CQ
	 * create flags, otherwise it returns zero.
	 */
	return (flags & ~(IB_UVERBS_CQ_FLAGS_IGNORE_OVERRUN |
			  IB_UVERBS_CQ_FLAGS_TIMESTAMP_COMPLETION));
}

static inline int verify_assign_uidx(u8 cqe_version, u32 cmd_uidx,
				     u32 *user_index)
{
	if (cqe_version) {
		if ((cmd_uidx == MLX5_IB_DEFAULT_UIDX) ||
		    (cmd_uidx & ~MLX5_USER_ASSIGNED_UIDX_MASK))
			return -EINVAL;
		*user_index = cmd_uidx;
	} else {
		*user_index = MLX5_IB_DEFAULT_UIDX;
	}

	return 0;
}

static inline int get_qp_user_index(struct mlx5_ib_ucontext *ucontext,
				    struct mlx5_ib_create_qp *ucmd,
				    int inlen,
				    u32 *user_index)
{
	u8 cqe_version = ucontext->cqe_version;

	if (field_avail(struct mlx5_ib_create_qp, uidx, inlen) &&
	    !cqe_version && (ucmd->uidx == MLX5_IB_DEFAULT_UIDX))
		return 0;

	if (!!(field_avail(struct mlx5_ib_create_qp, uidx, inlen) !=
	       !!cqe_version))
		return -EINVAL;

	return verify_assign_uidx(cqe_version, ucmd->uidx, user_index);
}

static inline int get_srq_user_index(struct mlx5_ib_ucontext *ucontext,
				     struct mlx5_ib_create_srq *ucmd,
				     int inlen,
				     u32 *user_index)
{
	u8 cqe_version = ucontext->cqe_version;

	if (field_avail(struct mlx5_ib_create_srq, uidx, inlen) &&
	    !cqe_version && (ucmd->uidx == MLX5_IB_DEFAULT_UIDX))
		return 0;

	if (!!(field_avail(struct mlx5_ib_create_srq, uidx, inlen) !=
	       !!cqe_version))
		return -EINVAL;

	return verify_assign_uidx(cqe_version, ucmd->uidx, user_index);
}

static inline int get_uars_per_sys_page(struct mlx5_ib_dev *dev, bool lib_support)
{
	return lib_support && MLX5_CAP_GEN(dev->mdev, uar_4k) ?
				MLX5_UARS_IN_PAGE : 1;
}

static inline int get_num_static_uars(struct mlx5_ib_dev *dev,
				      struct mlx5_bfreg_info *bfregi)
{
	return get_uars_per_sys_page(dev, bfregi->lib_uar_4k) * bfregi->num_static_sys_pages;
}

unsigned long mlx5_ib_get_xlt_emergency_page(void);
void mlx5_ib_put_xlt_emergency_page(void);

int bfregn_to_uar_index(struct mlx5_ib_dev *dev,
			struct mlx5_bfreg_info *bfregi, u32 bfregn,
			bool dyn_bfreg);
#endif /* MLX5_IB_H */
