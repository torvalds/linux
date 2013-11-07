/*
 * Copyright (c) 2006, 2007 Cisco Systems.  All rights reserved.
 * Copyright (c) 2007, 2008 Mellanox Technologies. All rights reserved.
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

#ifndef MLX4_IB_H
#define MLX4_IB_H

#include <linux/compiler.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/idr.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_umem.h>
#include <rdma/ib_mad.h>
#include <rdma/ib_sa.h>

#include <linux/mlx4/device.h>
#include <linux/mlx4/doorbell.h>

#define MLX4_IB_DRV_NAME	"mlx4_ib"

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt)	"<" MLX4_IB_DRV_NAME "> %s: " fmt, __func__

#define mlx4_ib_warn(ibdev, format, arg...) \
	dev_warn((ibdev)->dma_device, MLX4_IB_DRV_NAME ": " format, ## arg)

enum {
	MLX4_IB_SQ_MIN_WQE_SHIFT = 6,
	MLX4_IB_MAX_HEADROOM	 = 2048
};

#define MLX4_IB_SQ_HEADROOM(shift)	((MLX4_IB_MAX_HEADROOM >> (shift)) + 1)
#define MLX4_IB_SQ_MAX_SPARE		(MLX4_IB_SQ_HEADROOM(MLX4_IB_SQ_MIN_WQE_SHIFT))

/*module param to indicate if SM assigns the alias_GUID*/
extern int mlx4_ib_sm_guid_assign;

struct mlx4_ib_ucontext {
	struct ib_ucontext	ibucontext;
	struct mlx4_uar		uar;
	struct list_head	db_page_list;
	struct mutex		db_page_mutex;
};

struct mlx4_ib_pd {
	struct ib_pd		ibpd;
	u32			pdn;
};

struct mlx4_ib_xrcd {
	struct ib_xrcd		ibxrcd;
	u32			xrcdn;
	struct ib_pd	       *pd;
	struct ib_cq	       *cq;
};

struct mlx4_ib_cq_buf {
	struct mlx4_buf		buf;
	struct mlx4_mtt		mtt;
	int			entry_size;
};

struct mlx4_ib_cq_resize {
	struct mlx4_ib_cq_buf	buf;
	int			cqe;
};

struct mlx4_ib_cq {
	struct ib_cq		ibcq;
	struct mlx4_cq		mcq;
	struct mlx4_ib_cq_buf	buf;
	struct mlx4_ib_cq_resize *resize_buf;
	struct mlx4_db		db;
	spinlock_t		lock;
	struct mutex		resize_mutex;
	struct ib_umem	       *umem;
	struct ib_umem	       *resize_umem;
};

struct mlx4_ib_mr {
	struct ib_mr		ibmr;
	struct mlx4_mr		mmr;
	struct ib_umem	       *umem;
};

struct mlx4_ib_mw {
	struct ib_mw		ibmw;
	struct mlx4_mw		mmw;
};

struct mlx4_ib_fast_reg_page_list {
	struct ib_fast_reg_page_list	ibfrpl;
	__be64			       *mapped_page_list;
	dma_addr_t			map;
};

struct mlx4_ib_fmr {
	struct ib_fmr           ibfmr;
	struct mlx4_fmr         mfmr;
};

struct mlx4_ib_flow {
	struct ib_flow ibflow;
	/* translating DMFS verbs sniffer rule to FW API requires two reg IDs */
	u64 reg_id[2];
};

struct mlx4_ib_wq {
	u64		       *wrid;
	spinlock_t		lock;
	int			wqe_cnt;
	int			max_post;
	int			max_gs;
	int			offset;
	int			wqe_shift;
	unsigned		head;
	unsigned		tail;
};

enum mlx4_ib_qp_flags {
	MLX4_IB_QP_LSO = IB_QP_CREATE_IPOIB_UD_LSO,
	MLX4_IB_QP_BLOCK_MULTICAST_LOOPBACK = IB_QP_CREATE_BLOCK_MULTICAST_LOOPBACK,
	MLX4_IB_SRIOV_TUNNEL_QP = 1 << 30,
	MLX4_IB_SRIOV_SQP = 1 << 31,
};

struct mlx4_ib_gid_entry {
	struct list_head	list;
	union ib_gid		gid;
	int			added;
	u8			port;
};

enum mlx4_ib_qp_type {
	/*
	 * IB_QPT_SMI and IB_QPT_GSI have to be the first two entries
	 * here (and in that order) since the MAD layer uses them as
	 * indices into a 2-entry table.
	 */
	MLX4_IB_QPT_SMI = IB_QPT_SMI,
	MLX4_IB_QPT_GSI = IB_QPT_GSI,

	MLX4_IB_QPT_RC = IB_QPT_RC,
	MLX4_IB_QPT_UC = IB_QPT_UC,
	MLX4_IB_QPT_UD = IB_QPT_UD,
	MLX4_IB_QPT_RAW_IPV6 = IB_QPT_RAW_IPV6,
	MLX4_IB_QPT_RAW_ETHERTYPE = IB_QPT_RAW_ETHERTYPE,
	MLX4_IB_QPT_RAW_PACKET = IB_QPT_RAW_PACKET,
	MLX4_IB_QPT_XRC_INI = IB_QPT_XRC_INI,
	MLX4_IB_QPT_XRC_TGT = IB_QPT_XRC_TGT,

	MLX4_IB_QPT_PROXY_SMI_OWNER	= 1 << 16,
	MLX4_IB_QPT_PROXY_SMI		= 1 << 17,
	MLX4_IB_QPT_PROXY_GSI		= 1 << 18,
	MLX4_IB_QPT_TUN_SMI_OWNER	= 1 << 19,
	MLX4_IB_QPT_TUN_SMI		= 1 << 20,
	MLX4_IB_QPT_TUN_GSI		= 1 << 21,
};

#define MLX4_IB_QPT_ANY_SRIOV	(MLX4_IB_QPT_PROXY_SMI_OWNER | \
	MLX4_IB_QPT_PROXY_SMI | MLX4_IB_QPT_PROXY_GSI | MLX4_IB_QPT_TUN_SMI_OWNER | \
	MLX4_IB_QPT_TUN_SMI | MLX4_IB_QPT_TUN_GSI)

enum mlx4_ib_mad_ifc_flags {
	MLX4_MAD_IFC_IGNORE_MKEY	= 1,
	MLX4_MAD_IFC_IGNORE_BKEY	= 2,
	MLX4_MAD_IFC_IGNORE_KEYS	= (MLX4_MAD_IFC_IGNORE_MKEY |
					   MLX4_MAD_IFC_IGNORE_BKEY),
	MLX4_MAD_IFC_NET_VIEW		= 4,
};

enum {
	MLX4_NUM_TUNNEL_BUFS		= 256,
};

struct mlx4_ib_tunnel_header {
	struct mlx4_av av;
	__be32 remote_qpn;
	__be32 qkey;
	__be16 vlan;
	u8 mac[6];
	__be16 pkey_index;
	u8 reserved[6];
};

struct mlx4_ib_buf {
	void *addr;
	dma_addr_t map;
};

struct mlx4_rcv_tunnel_hdr {
	__be32 flags_src_qp; /* flags[6:5] is defined for VLANs:
			      * 0x0 - no vlan was in the packet
			      * 0x01 - C-VLAN was in the packet */
	u8 g_ml_path; /* gid bit stands for ipv6/4 header in RoCE */
	u8 reserved;
	__be16 pkey_index;
	__be16 sl_vid;
	__be16 slid_mac_47_32;
	__be32 mac_31_0;
};

struct mlx4_ib_proxy_sqp_hdr {
	struct ib_grh grh;
	struct mlx4_rcv_tunnel_hdr tun;
}  __packed;

struct mlx4_ib_qp {
	struct ib_qp		ibqp;
	struct mlx4_qp		mqp;
	struct mlx4_buf		buf;

	struct mlx4_db		db;
	struct mlx4_ib_wq	rq;

	u32			doorbell_qpn;
	__be32			sq_signal_bits;
	unsigned		sq_next_wqe;
	int			sq_max_wqes_per_wr;
	int			sq_spare_wqes;
	struct mlx4_ib_wq	sq;

	enum mlx4_ib_qp_type	mlx4_ib_qp_type;
	struct ib_umem	       *umem;
	struct mlx4_mtt		mtt;
	int			buf_size;
	struct mutex		mutex;
	u16			xrcdn;
	u32			flags;
	u8			port;
	u8			alt_port;
	u8			atomic_rd_en;
	u8			resp_depth;
	u8			sq_no_prefetch;
	u8			state;
	int			mlx_type;
	struct list_head	gid_list;
	struct list_head	steering_rules;
	struct mlx4_ib_buf	*sqp_proxy_rcv;

};

struct mlx4_ib_srq {
	struct ib_srq		ibsrq;
	struct mlx4_srq		msrq;
	struct mlx4_buf		buf;
	struct mlx4_db		db;
	u64		       *wrid;
	spinlock_t		lock;
	int			head;
	int			tail;
	u16			wqe_ctr;
	struct ib_umem	       *umem;
	struct mlx4_mtt		mtt;
	struct mutex		mutex;
};

struct mlx4_ib_ah {
	struct ib_ah		ibah;
	union mlx4_ext_av       av;
};

/****************************************/
/* alias guid support */
/****************************************/
#define NUM_PORT_ALIAS_GUID		2
#define NUM_ALIAS_GUID_IN_REC		8
#define NUM_ALIAS_GUID_REC_IN_PORT	16
#define GUID_REC_SIZE			8
#define NUM_ALIAS_GUID_PER_PORT		128
#define MLX4_NOT_SET_GUID		(0x00LL)
#define MLX4_GUID_FOR_DELETE_VAL	(~(0x00LL))

enum mlx4_guid_alias_rec_status {
	MLX4_GUID_INFO_STATUS_IDLE,
	MLX4_GUID_INFO_STATUS_SET,
	MLX4_GUID_INFO_STATUS_PENDING,
};

enum mlx4_guid_alias_rec_ownership {
	MLX4_GUID_DRIVER_ASSIGN,
	MLX4_GUID_SYSADMIN_ASSIGN,
	MLX4_GUID_NONE_ASSIGN, /*init state of each record*/
};

enum mlx4_guid_alias_rec_method {
	MLX4_GUID_INFO_RECORD_SET	= IB_MGMT_METHOD_SET,
	MLX4_GUID_INFO_RECORD_DELETE	= IB_SA_METHOD_DELETE,
};

struct mlx4_sriov_alias_guid_info_rec_det {
	u8 all_recs[GUID_REC_SIZE * NUM_ALIAS_GUID_IN_REC];
	ib_sa_comp_mask guid_indexes; /*indicates what from the 8 records are valid*/
	enum mlx4_guid_alias_rec_status status; /*indicates the administraively status of the record.*/
	u8 method; /*set or delete*/
	enum mlx4_guid_alias_rec_ownership ownership; /*indicates who assign that alias_guid record*/
};

struct mlx4_sriov_alias_guid_port_rec_det {
	struct mlx4_sriov_alias_guid_info_rec_det all_rec_per_port[NUM_ALIAS_GUID_REC_IN_PORT];
	struct workqueue_struct *wq;
	struct delayed_work alias_guid_work;
	u8 port;
	struct mlx4_sriov_alias_guid *parent;
	struct list_head cb_list;
};

struct mlx4_sriov_alias_guid {
	struct mlx4_sriov_alias_guid_port_rec_det ports_guid[MLX4_MAX_PORTS];
	spinlock_t ag_work_lock;
	struct ib_sa_client *sa_client;
};

struct mlx4_ib_demux_work {
	struct work_struct	work;
	struct mlx4_ib_dev     *dev;
	int			slave;
	int			do_init;
	u8			port;

};

struct mlx4_ib_tun_tx_buf {
	struct mlx4_ib_buf buf;
	struct ib_ah *ah;
};

struct mlx4_ib_demux_pv_qp {
	struct ib_qp *qp;
	enum ib_qp_type proxy_qpt;
	struct mlx4_ib_buf *ring;
	struct mlx4_ib_tun_tx_buf *tx_ring;
	spinlock_t tx_lock;
	unsigned tx_ix_head;
	unsigned tx_ix_tail;
};

enum mlx4_ib_demux_pv_state {
	DEMUX_PV_STATE_DOWN,
	DEMUX_PV_STATE_STARTING,
	DEMUX_PV_STATE_ACTIVE,
	DEMUX_PV_STATE_DOWNING,
};

struct mlx4_ib_demux_pv_ctx {
	int port;
	int slave;
	enum mlx4_ib_demux_pv_state state;
	int has_smi;
	struct ib_device *ib_dev;
	struct ib_cq *cq;
	struct ib_pd *pd;
	struct ib_mr *mr;
	struct work_struct work;
	struct workqueue_struct *wq;
	struct mlx4_ib_demux_pv_qp qp[2];
};

struct mlx4_ib_demux_ctx {
	struct ib_device *ib_dev;
	int port;
	struct workqueue_struct *wq;
	struct workqueue_struct *ud_wq;
	spinlock_t ud_lock;
	__be64 subnet_prefix;
	__be64 guid_cache[128];
	struct mlx4_ib_dev *dev;
	/* the following lock protects both mcg_table and mcg_mgid0_list */
	struct mutex		mcg_table_lock;
	struct rb_root		mcg_table;
	struct list_head	mcg_mgid0_list;
	struct workqueue_struct	*mcg_wq;
	struct mlx4_ib_demux_pv_ctx **tun;
	atomic_t tid;
	int    flushing; /* flushing the work queue */
};

struct mlx4_ib_sriov {
	struct mlx4_ib_demux_ctx demux[MLX4_MAX_PORTS];
	struct mlx4_ib_demux_pv_ctx *sqps[MLX4_MAX_PORTS];
	/* when using this spinlock you should use "irq" because
	 * it may be called from interrupt context.*/
	spinlock_t going_down_lock;
	int is_going_down;

	struct mlx4_sriov_alias_guid alias_guid;

	/* CM paravirtualization fields */
	struct list_head cm_list;
	spinlock_t id_map_lock;
	struct rb_root sl_id_map;
	struct idr pv_id_table;
};

struct mlx4_ib_iboe {
	spinlock_t		lock;
	struct net_device      *netdevs[MLX4_MAX_PORTS];
	struct notifier_block 	nb;
	union ib_gid		gid_table[MLX4_MAX_PORTS][128];
};

struct pkey_mgt {
	u8			virt2phys_pkey[MLX4_MFUNC_MAX][MLX4_MAX_PORTS][MLX4_MAX_PORT_PKEYS];
	u16			phys_pkey_cache[MLX4_MAX_PORTS][MLX4_MAX_PORT_PKEYS];
	struct list_head	pkey_port_list[MLX4_MFUNC_MAX];
	struct kobject	       *device_parent[MLX4_MFUNC_MAX];
};

struct mlx4_ib_iov_sysfs_attr {
	void *ctx;
	struct kobject *kobj;
	unsigned long data;
	u32 entry_num;
	char name[15];
	struct device_attribute dentry;
	struct device *dev;
};

struct mlx4_ib_iov_sysfs_attr_ar {
	struct mlx4_ib_iov_sysfs_attr dentries[3 * NUM_ALIAS_GUID_PER_PORT + 1];
};

struct mlx4_ib_iov_port {
	char name[100];
	u8 num;
	struct mlx4_ib_dev *dev;
	struct list_head list;
	struct mlx4_ib_iov_sysfs_attr_ar *dentr_ar;
	struct ib_port_attr attr;
	struct kobject	*cur_port;
	struct kobject	*admin_alias_parent;
	struct kobject	*gids_parent;
	struct kobject	*pkeys_parent;
	struct kobject	*mcgs_parent;
	struct mlx4_ib_iov_sysfs_attr mcg_dentry;
};

struct mlx4_ib_dev {
	struct ib_device	ib_dev;
	struct mlx4_dev	       *dev;
	int			num_ports;
	void __iomem	       *uar_map;

	struct mlx4_uar		priv_uar;
	u32			priv_pdn;
	MLX4_DECLARE_DOORBELL_LOCK(uar_lock);

	struct ib_mad_agent    *send_agent[MLX4_MAX_PORTS][2];
	struct ib_ah	       *sm_ah[MLX4_MAX_PORTS];
	spinlock_t		sm_lock;
	struct mlx4_ib_sriov	sriov;

	struct mutex		cap_mask_mutex;
	bool			ib_active;
	struct mlx4_ib_iboe	iboe;
	int			counters[MLX4_MAX_PORTS];
	int		       *eq_table;
	int			eq_added;
	struct kobject	       *iov_parent;
	struct kobject	       *ports_parent;
	struct kobject	       *dev_ports_parent[MLX4_MFUNC_MAX];
	struct mlx4_ib_iov_port	iov_ports[MLX4_MAX_PORTS];
	struct pkey_mgt		pkeys;
	int steering_support;
};

struct ib_event_work {
	struct work_struct	work;
	struct mlx4_ib_dev	*ib_dev;
	struct mlx4_eqe		ib_eqe;
};

struct mlx4_ib_qp_tunnel_init_attr {
	struct ib_qp_init_attr init_attr;
	int slave;
	enum ib_qp_type proxy_qp_type;
	u8 port;
};

static inline struct mlx4_ib_dev *to_mdev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct mlx4_ib_dev, ib_dev);
}

static inline struct mlx4_ib_ucontext *to_mucontext(struct ib_ucontext *ibucontext)
{
	return container_of(ibucontext, struct mlx4_ib_ucontext, ibucontext);
}

static inline struct mlx4_ib_pd *to_mpd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct mlx4_ib_pd, ibpd);
}

static inline struct mlx4_ib_xrcd *to_mxrcd(struct ib_xrcd *ibxrcd)
{
	return container_of(ibxrcd, struct mlx4_ib_xrcd, ibxrcd);
}

static inline struct mlx4_ib_cq *to_mcq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct mlx4_ib_cq, ibcq);
}

static inline struct mlx4_ib_cq *to_mibcq(struct mlx4_cq *mcq)
{
	return container_of(mcq, struct mlx4_ib_cq, mcq);
}

static inline struct mlx4_ib_mr *to_mmr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct mlx4_ib_mr, ibmr);
}

static inline struct mlx4_ib_mw *to_mmw(struct ib_mw *ibmw)
{
	return container_of(ibmw, struct mlx4_ib_mw, ibmw);
}

static inline struct mlx4_ib_fast_reg_page_list *to_mfrpl(struct ib_fast_reg_page_list *ibfrpl)
{
	return container_of(ibfrpl, struct mlx4_ib_fast_reg_page_list, ibfrpl);
}

static inline struct mlx4_ib_fmr *to_mfmr(struct ib_fmr *ibfmr)
{
	return container_of(ibfmr, struct mlx4_ib_fmr, ibfmr);
}

static inline struct mlx4_ib_flow *to_mflow(struct ib_flow *ibflow)
{
	return container_of(ibflow, struct mlx4_ib_flow, ibflow);
}

static inline struct mlx4_ib_qp *to_mqp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct mlx4_ib_qp, ibqp);
}

static inline struct mlx4_ib_qp *to_mibqp(struct mlx4_qp *mqp)
{
	return container_of(mqp, struct mlx4_ib_qp, mqp);
}

static inline struct mlx4_ib_srq *to_msrq(struct ib_srq *ibsrq)
{
	return container_of(ibsrq, struct mlx4_ib_srq, ibsrq);
}

static inline struct mlx4_ib_srq *to_mibsrq(struct mlx4_srq *msrq)
{
	return container_of(msrq, struct mlx4_ib_srq, msrq);
}

static inline struct mlx4_ib_ah *to_mah(struct ib_ah *ibah)
{
	return container_of(ibah, struct mlx4_ib_ah, ibah);
}

int mlx4_ib_init_sriov(struct mlx4_ib_dev *dev);
void mlx4_ib_close_sriov(struct mlx4_ib_dev *dev);

int mlx4_ib_db_map_user(struct mlx4_ib_ucontext *context, unsigned long virt,
			struct mlx4_db *db);
void mlx4_ib_db_unmap_user(struct mlx4_ib_ucontext *context, struct mlx4_db *db);

struct ib_mr *mlx4_ib_get_dma_mr(struct ib_pd *pd, int acc);
int mlx4_ib_umem_write_mtt(struct mlx4_ib_dev *dev, struct mlx4_mtt *mtt,
			   struct ib_umem *umem);
struct ib_mr *mlx4_ib_reg_user_mr(struct ib_pd *pd, u64 start, u64 length,
				  u64 virt_addr, int access_flags,
				  struct ib_udata *udata);
int mlx4_ib_dereg_mr(struct ib_mr *mr);
struct ib_mw *mlx4_ib_alloc_mw(struct ib_pd *pd, enum ib_mw_type type);
int mlx4_ib_bind_mw(struct ib_qp *qp, struct ib_mw *mw,
		    struct ib_mw_bind *mw_bind);
int mlx4_ib_dealloc_mw(struct ib_mw *mw);
struct ib_mr *mlx4_ib_alloc_fast_reg_mr(struct ib_pd *pd,
					int max_page_list_len);
struct ib_fast_reg_page_list *mlx4_ib_alloc_fast_reg_page_list(struct ib_device *ibdev,
							       int page_list_len);
void mlx4_ib_free_fast_reg_page_list(struct ib_fast_reg_page_list *page_list);

int mlx4_ib_modify_cq(struct ib_cq *cq, u16 cq_count, u16 cq_period);
int mlx4_ib_resize_cq(struct ib_cq *ibcq, int entries, struct ib_udata *udata);
struct ib_cq *mlx4_ib_create_cq(struct ib_device *ibdev, int entries, int vector,
				struct ib_ucontext *context,
				struct ib_udata *udata);
int mlx4_ib_destroy_cq(struct ib_cq *cq);
int mlx4_ib_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc);
int mlx4_ib_arm_cq(struct ib_cq *cq, enum ib_cq_notify_flags flags);
void __mlx4_ib_cq_clean(struct mlx4_ib_cq *cq, u32 qpn, struct mlx4_ib_srq *srq);
void mlx4_ib_cq_clean(struct mlx4_ib_cq *cq, u32 qpn, struct mlx4_ib_srq *srq);

struct ib_ah *mlx4_ib_create_ah(struct ib_pd *pd, struct ib_ah_attr *ah_attr);
int mlx4_ib_query_ah(struct ib_ah *ibah, struct ib_ah_attr *ah_attr);
int mlx4_ib_destroy_ah(struct ib_ah *ah);

struct ib_srq *mlx4_ib_create_srq(struct ib_pd *pd,
				  struct ib_srq_init_attr *init_attr,
				  struct ib_udata *udata);
int mlx4_ib_modify_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr,
		       enum ib_srq_attr_mask attr_mask, struct ib_udata *udata);
int mlx4_ib_query_srq(struct ib_srq *srq, struct ib_srq_attr *srq_attr);
int mlx4_ib_destroy_srq(struct ib_srq *srq);
void mlx4_ib_free_srq_wqe(struct mlx4_ib_srq *srq, int wqe_index);
int mlx4_ib_post_srq_recv(struct ib_srq *ibsrq, struct ib_recv_wr *wr,
			  struct ib_recv_wr **bad_wr);

struct ib_qp *mlx4_ib_create_qp(struct ib_pd *pd,
				struct ib_qp_init_attr *init_attr,
				struct ib_udata *udata);
int mlx4_ib_destroy_qp(struct ib_qp *qp);
int mlx4_ib_modify_qp(struct ib_qp *ibqp, struct ib_qp_attr *attr,
		      int attr_mask, struct ib_udata *udata);
int mlx4_ib_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr, int qp_attr_mask,
		     struct ib_qp_init_attr *qp_init_attr);
int mlx4_ib_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
		      struct ib_send_wr **bad_wr);
int mlx4_ib_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *wr,
		      struct ib_recv_wr **bad_wr);

int mlx4_MAD_IFC(struct mlx4_ib_dev *dev, int mad_ifc_flags,
		 int port, struct ib_wc *in_wc, struct ib_grh *in_grh,
		 void *in_mad, void *response_mad);
int mlx4_ib_process_mad(struct ib_device *ibdev, int mad_flags,	u8 port_num,
			struct ib_wc *in_wc, struct ib_grh *in_grh,
			struct ib_mad *in_mad, struct ib_mad *out_mad);
int mlx4_ib_mad_init(struct mlx4_ib_dev *dev);
void mlx4_ib_mad_cleanup(struct mlx4_ib_dev *dev);

struct ib_fmr *mlx4_ib_fmr_alloc(struct ib_pd *pd, int mr_access_flags,
				  struct ib_fmr_attr *fmr_attr);
int mlx4_ib_map_phys_fmr(struct ib_fmr *ibfmr, u64 *page_list, int npages,
			 u64 iova);
int mlx4_ib_unmap_fmr(struct list_head *fmr_list);
int mlx4_ib_fmr_dealloc(struct ib_fmr *fmr);
int __mlx4_ib_query_port(struct ib_device *ibdev, u8 port,
			 struct ib_port_attr *props, int netw_view);
int __mlx4_ib_query_pkey(struct ib_device *ibdev, u8 port, u16 index,
			 u16 *pkey, int netw_view);

int __mlx4_ib_query_gid(struct ib_device *ibdev, u8 port, int index,
			union ib_gid *gid, int netw_view);

int mlx4_ib_resolve_grh(struct mlx4_ib_dev *dev, const struct ib_ah_attr *ah_attr,
			u8 *mac, int *is_mcast, u8 port);

static inline bool mlx4_ib_ah_grh_present(struct mlx4_ib_ah *ah)
{
	u8 port = be32_to_cpu(ah->av.ib.port_pd) >> 24 & 3;

	if (rdma_port_get_link_layer(ah->ibah.device, port) == IB_LINK_LAYER_ETHERNET)
		return true;

	return !!(ah->av.ib.g_slid & 0x80);
}

int mlx4_ib_mcg_port_init(struct mlx4_ib_demux_ctx *ctx);
void mlx4_ib_mcg_port_cleanup(struct mlx4_ib_demux_ctx *ctx, int destroy_wq);
void clean_vf_mcast(struct mlx4_ib_demux_ctx *ctx, int slave);
int mlx4_ib_mcg_init(void);
void mlx4_ib_mcg_destroy(void);

int mlx4_ib_find_real_gid(struct ib_device *ibdev, u8 port, __be64 guid);

int mlx4_ib_mcg_multiplex_handler(struct ib_device *ibdev, int port, int slave,
				  struct ib_sa_mad *sa_mad);
int mlx4_ib_mcg_demux_handler(struct ib_device *ibdev, int port, int slave,
			      struct ib_sa_mad *mad);

int mlx4_ib_add_mc(struct mlx4_ib_dev *mdev, struct mlx4_ib_qp *mqp,
		   union ib_gid *gid);

void mlx4_ib_dispatch_event(struct mlx4_ib_dev *dev, u8 port_num,
			    enum ib_event_type type);

void mlx4_ib_tunnels_update_work(struct work_struct *work);

int mlx4_ib_send_to_slave(struct mlx4_ib_dev *dev, int slave, u8 port,
			  enum ib_qp_type qpt, struct ib_wc *wc,
			  struct ib_grh *grh, struct ib_mad *mad);
int mlx4_ib_send_to_wire(struct mlx4_ib_dev *dev, int slave, u8 port,
			 enum ib_qp_type dest_qpt, u16 pkey_index, u32 remote_qpn,
			 u32 qkey, struct ib_ah_attr *attr, struct ib_mad *mad);
__be64 mlx4_ib_get_new_demux_tid(struct mlx4_ib_demux_ctx *ctx);

int mlx4_ib_demux_cm_handler(struct ib_device *ibdev, int port, int *slave,
		struct ib_mad *mad);

int mlx4_ib_multiplex_cm_handler(struct ib_device *ibdev, int port, int slave_id,
		struct ib_mad *mad);

void mlx4_ib_cm_paravirt_init(struct mlx4_ib_dev *dev);
void mlx4_ib_cm_paravirt_clean(struct mlx4_ib_dev *dev, int slave_id);

/* alias guid support */
void mlx4_ib_init_alias_guid_work(struct mlx4_ib_dev *dev, int port);
int mlx4_ib_init_alias_guid_service(struct mlx4_ib_dev *dev);
void mlx4_ib_destroy_alias_guid_service(struct mlx4_ib_dev *dev);
void mlx4_ib_invalidate_all_guid_record(struct mlx4_ib_dev *dev, int port);

void mlx4_ib_notify_slaves_on_guid_change(struct mlx4_ib_dev *dev,
					  int block_num,
					  u8 port_num, u8 *p_data);

void mlx4_ib_update_cache_on_guid_change(struct mlx4_ib_dev *dev,
					 int block_num, u8 port_num,
					 u8 *p_data);

int add_sysfs_port_mcg_attr(struct mlx4_ib_dev *device, int port_num,
			    struct attribute *attr);
void del_sysfs_port_mcg_attr(struct mlx4_ib_dev *device, int port_num,
			     struct attribute *attr);
ib_sa_comp_mask mlx4_ib_get_aguid_comp_mask_from_ix(int index);

int mlx4_ib_device_register_sysfs(struct mlx4_ib_dev *device) ;

void mlx4_ib_device_unregister_sysfs(struct mlx4_ib_dev *device);

__be64 mlx4_ib_gen_node_guid(void);


#endif /* MLX4_IB_H */
