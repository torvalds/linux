/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007 Cisco Systems.  All rights reserved.
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

#ifndef MLX4_H
#define MLX4_H

#include <infiniband/endian.h>
#include <stddef.h>

#include <infiniband/driver.h>
#include <infiniband/udma_barrier.h>
#include <infiniband/verbs.h>

#define MLX4_PORTS_NUM 2

#define PFX		"mlx4: "

enum {
	MLX4_STAT_RATE_OFFSET		= 5
};

enum {
	MLX4_QP_TABLE_BITS		= 8,
	MLX4_QP_TABLE_SIZE		= 1 << MLX4_QP_TABLE_BITS,
	MLX4_QP_TABLE_MASK		= MLX4_QP_TABLE_SIZE - 1
};

#define MLX4_REMOTE_SRQN_FLAGS(wr) htobe32(wr->qp_type.xrc.remote_srqn << 8)

enum {
	MLX4_XSRQ_TABLE_BITS = 8,
	MLX4_XSRQ_TABLE_SIZE = 1 << MLX4_XSRQ_TABLE_BITS,
	MLX4_XSRQ_TABLE_MASK = MLX4_XSRQ_TABLE_SIZE - 1
};

struct mlx4_xsrq_table {
	struct {
		struct mlx4_srq **table;
		int		  refcnt;
	} xsrq_table[MLX4_XSRQ_TABLE_SIZE];

	pthread_mutex_t		  mutex;
	int			  num_xsrq;
	int			  shift;
	int			  mask;
};

enum {
	MLX4_XRC_QPN_BIT     = (1 << 23)
};

enum mlx4_db_type {
	MLX4_DB_TYPE_CQ,
	MLX4_DB_TYPE_RQ,
	MLX4_NUM_DB_TYPE
};

enum {
	MLX4_OPCODE_NOP			= 0x00,
	MLX4_OPCODE_SEND_INVAL		= 0x01,
	MLX4_OPCODE_RDMA_WRITE		= 0x08,
	MLX4_OPCODE_RDMA_WRITE_IMM	= 0x09,
	MLX4_OPCODE_SEND		= 0x0a,
	MLX4_OPCODE_SEND_IMM		= 0x0b,
	MLX4_OPCODE_LSO			= 0x0e,
	MLX4_OPCODE_RDMA_READ		= 0x10,
	MLX4_OPCODE_ATOMIC_CS		= 0x11,
	MLX4_OPCODE_ATOMIC_FA		= 0x12,
	MLX4_OPCODE_MASKED_ATOMIC_CS	= 0x14,
	MLX4_OPCODE_MASKED_ATOMIC_FA	= 0x15,
	MLX4_OPCODE_BIND_MW		= 0x18,
	MLX4_OPCODE_FMR			= 0x19,
	MLX4_OPCODE_LOCAL_INVAL		= 0x1b,
	MLX4_OPCODE_CONFIG_CMD		= 0x1f,

	MLX4_RECV_OPCODE_RDMA_WRITE_IMM	= 0x00,
	MLX4_RECV_OPCODE_SEND		= 0x01,
	MLX4_RECV_OPCODE_SEND_IMM	= 0x02,
	MLX4_RECV_OPCODE_SEND_INVAL	= 0x03,

	MLX4_CQE_OPCODE_ERROR		= 0x1e,
	MLX4_CQE_OPCODE_RESIZE		= 0x16,
};

struct mlx4_device {
	struct verbs_device		verbs_dev;
	int				page_size;
	int				abi_version;
};

struct mlx4_db_page;

struct mlx4_context {
	struct ibv_context		ibv_ctx;

	void			       *uar;
	pthread_spinlock_t		uar_lock;

	void			       *bf_page;
	int				bf_buf_size;
	int				bf_offset;
	pthread_spinlock_t		bf_lock;

	struct {
		struct mlx4_qp	      **table;
		int			refcnt;
	}				qp_table[MLX4_QP_TABLE_SIZE];
	pthread_mutex_t			qp_table_mutex;
	int				num_qps;
	int				qp_table_shift;
	int				qp_table_mask;
	int				max_qp_wr;
	int				max_sge;

	struct mlx4_db_page	       *db_list[MLX4_NUM_DB_TYPE];
	pthread_mutex_t			db_list_mutex;
	int				cqe_size;
	struct mlx4_xsrq_table		xsrq_table;
	struct {
		uint8_t                 valid;
		uint8_t                 link_layer;
		enum ibv_port_cap_flags caps;
	} port_query_cache[MLX4_PORTS_NUM];
	struct {
		uint64_t                offset;
		uint8_t                 offset_valid;
	} core_clock;
	void			       *hca_core_clock;
};

struct mlx4_buf {
	void			       *buf;
	size_t				length;
};

struct mlx4_pd {
	struct ibv_pd			ibv_pd;
	uint32_t			pdn;
};

enum {
	MLX4_CQ_FLAGS_RX_CSUM_VALID = 1 << 0,
	MLX4_CQ_FLAGS_EXTENDED = 1 << 1,
	MLX4_CQ_FLAGS_SINGLE_THREADED = 1 << 2,
};

struct mlx4_cq {
	struct ibv_cq_ex		ibv_cq;
	struct mlx4_buf			buf;
	struct mlx4_buf			resize_buf;
	pthread_spinlock_t		lock;
	uint32_t			cqn;
	uint32_t			cons_index;
	uint32_t		       *set_ci_db;
	uint32_t		       *arm_db;
	int				arm_sn;
	int				cqe_size;
	struct mlx4_qp			*cur_qp;
	struct mlx4_cqe			*cqe;
	uint32_t			flags;
};

struct mlx4_srq {
	struct verbs_srq		verbs_srq;
	struct mlx4_buf			buf;
	pthread_spinlock_t		lock;
	uint64_t		       *wrid;
	uint32_t			srqn;
	int				max;
	int				max_gs;
	int				wqe_shift;
	int				head;
	int				tail;
	uint32_t		       *db;
	uint16_t			counter;
	uint8_t				ext_srq;
};

struct mlx4_wq {
	uint64_t		       *wrid;
	pthread_spinlock_t		lock;
	int				wqe_cnt;
	int				max_post;
	unsigned			head;
	unsigned			tail;
	int				max_gs;
	int				wqe_shift;
	int				offset;
};

struct mlx4_qp {
	struct verbs_qp			verbs_qp;
	struct mlx4_buf			buf;
	int				max_inline_data;
	int				buf_size;

	uint32_t			doorbell_qpn;
	uint32_t			sq_signal_bits;
	int				sq_spare_wqes;
	struct mlx4_wq			sq;

	uint32_t		       *db;
	struct mlx4_wq			rq;

	uint8_t				link_layer;
	uint32_t			qp_cap_cache;
};

struct mlx4_av {
	uint32_t			port_pd;
	uint8_t				reserved1;
	uint8_t				g_slid;
	uint16_t			dlid;
	uint8_t				reserved2;
	uint8_t				gid_index;
	uint8_t				stat_rate;
	uint8_t				hop_limit;
	uint32_t			sl_tclass_flowlabel;
	uint8_t				dgid[16];
};

struct mlx4_ah {
	struct ibv_ah			ibv_ah;
	struct mlx4_av			av;
	uint16_t			vlan;
	uint8_t				mac[6];
};

enum {
	MLX4_CSUM_SUPPORT_UD_OVER_IB	= (1 <<  0),
	MLX4_CSUM_SUPPORT_RAW_OVER_ETH	= (1 <<  1),
	/* Only report rx checksum when the validation is valid */
	MLX4_RX_CSUM_VALID		= (1 <<  16),
};

enum mlx4_cqe_status {
	MLX4_CQE_STATUS_TCP_UDP_CSUM_OK	= (1 <<  2),
	MLX4_CQE_STATUS_IPV4_PKT	= (1 << 22),
	MLX4_CQE_STATUS_IP_HDR_CSUM_OK	= (1 << 28),
	MLX4_CQE_STATUS_IPV4_CSUM_OK	= MLX4_CQE_STATUS_IPV4_PKT |
					MLX4_CQE_STATUS_IP_HDR_CSUM_OK |
					MLX4_CQE_STATUS_TCP_UDP_CSUM_OK
};

struct mlx4_cqe {
	uint32_t	vlan_my_qpn;
	uint32_t	immed_rss_invalid;
	uint32_t	g_mlpath_rqpn;
	union {
		struct {
			uint16_t	sl_vid;
			uint16_t	rlid;
		};
		uint32_t ts_47_16;
	};
	uint32_t	status;
	uint32_t	byte_cnt;
	uint16_t	wqe_index;
	uint16_t	checksum;
	uint8_t		reserved3;
	uint8_t		ts_15_8;
	uint8_t		ts_7_0;
	uint8_t		owner_sr_opcode;
};

static inline unsigned long align(unsigned long val, unsigned long align)
{
	return (val + align - 1) & ~(align - 1);
}
int align_queue_size(int req);

#define to_mxxx(xxx, type)						\
	((struct mlx4_##type *)					\
	 ((void *) ib##xxx - offsetof(struct mlx4_##type, ibv_##xxx)))

static inline struct mlx4_device *to_mdev(struct ibv_device *ibdev)
{
	/* ibv_device is first field of verbs_device
	 * see try_driver() in libibverbs.
	 */
	return container_of(ibdev, struct mlx4_device, verbs_dev);
}

static inline struct mlx4_context *to_mctx(struct ibv_context *ibctx)
{
	return to_mxxx(ctx, context);
}

static inline struct mlx4_pd *to_mpd(struct ibv_pd *ibpd)
{
	return to_mxxx(pd, pd);
}

static inline struct mlx4_cq *to_mcq(struct ibv_cq *ibcq)
{
	return to_mxxx(cq, cq);
}

static inline struct mlx4_srq *to_msrq(struct ibv_srq *ibsrq)
{
	return container_of(container_of(ibsrq, struct verbs_srq, srq),
			    struct mlx4_srq, verbs_srq);
}

static inline struct mlx4_qp *to_mqp(struct ibv_qp *ibqp)
{
	return container_of(container_of(ibqp, struct verbs_qp, qp),
			    struct mlx4_qp, verbs_qp);
}

static inline struct mlx4_ah *to_mah(struct ibv_ah *ibah)
{
	return to_mxxx(ah, ah);
}

static inline void mlx4_update_cons_index(struct mlx4_cq *cq)
{
	*cq->set_ci_db = htobe32(cq->cons_index & 0xffffff);
}

int mlx4_alloc_buf(struct mlx4_buf *buf, size_t size, int page_size);
void mlx4_free_buf(struct mlx4_buf *buf);

uint32_t *mlx4_alloc_db(struct mlx4_context *context, enum mlx4_db_type type);
void mlx4_free_db(struct mlx4_context *context, enum mlx4_db_type type, uint32_t *db);

int mlx4_query_device(struct ibv_context *context,
		       struct ibv_device_attr *attr);
int mlx4_query_device_ex(struct ibv_context *context,
			 const struct ibv_query_device_ex_input *input,
			 struct ibv_device_attr_ex *attr,
			 size_t attr_size);
int mlx4_query_port(struct ibv_context *context, uint8_t port,
		     struct ibv_port_attr *attr);
int mlx4_query_rt_values(struct ibv_context *context,
			 struct ibv_values_ex *values);
struct ibv_pd *mlx4_alloc_pd(struct ibv_context *context);
int mlx4_free_pd(struct ibv_pd *pd);
struct ibv_xrcd *mlx4_open_xrcd(struct ibv_context *context,
				struct ibv_xrcd_init_attr *attr);
int mlx4_close_xrcd(struct ibv_xrcd *xrcd);

struct ibv_mr *mlx4_reg_mr(struct ibv_pd *pd, void *addr,
			    size_t length, int access);
int mlx4_rereg_mr(struct ibv_mr *mr, int flags, struct ibv_pd *pd,
		  void *addr, size_t length, int access);
int mlx4_dereg_mr(struct ibv_mr *mr);

struct ibv_mw *mlx4_alloc_mw(struct ibv_pd *pd, enum ibv_mw_type type);
int mlx4_dealloc_mw(struct ibv_mw *mw);
int mlx4_bind_mw(struct ibv_qp *qp, struct ibv_mw *mw,
		 struct ibv_mw_bind *mw_bind);

struct ibv_cq *mlx4_create_cq(struct ibv_context *context, int cqe,
			       struct ibv_comp_channel *channel,
			       int comp_vector);
struct ibv_cq_ex *mlx4_create_cq_ex(struct ibv_context *context,
				    struct ibv_cq_init_attr_ex *cq_attr);
void mlx4_cq_fill_pfns(struct mlx4_cq *cq, const struct ibv_cq_init_attr_ex *cq_attr);
int mlx4_alloc_cq_buf(struct mlx4_device *dev, struct mlx4_buf *buf, int nent,
		      int entry_size);
int mlx4_resize_cq(struct ibv_cq *cq, int cqe);
int mlx4_destroy_cq(struct ibv_cq *cq);
int mlx4_poll_cq(struct ibv_cq *cq, int ne, struct ibv_wc *wc);
int mlx4_arm_cq(struct ibv_cq *cq, int solicited);
void mlx4_cq_event(struct ibv_cq *cq);
void __mlx4_cq_clean(struct mlx4_cq *cq, uint32_t qpn, struct mlx4_srq *srq);
void mlx4_cq_clean(struct mlx4_cq *cq, uint32_t qpn, struct mlx4_srq *srq);
int mlx4_get_outstanding_cqes(struct mlx4_cq *cq);
void mlx4_cq_resize_copy_cqes(struct mlx4_cq *cq, void *buf, int new_cqe);

struct ibv_srq *mlx4_create_srq(struct ibv_pd *pd,
				 struct ibv_srq_init_attr *attr);
struct ibv_srq *mlx4_create_srq_ex(struct ibv_context *context,
				   struct ibv_srq_init_attr_ex *attr_ex);
struct ibv_srq *mlx4_create_xrc_srq(struct ibv_context *context,
				    struct ibv_srq_init_attr_ex *attr_ex);
int mlx4_modify_srq(struct ibv_srq *srq,
		     struct ibv_srq_attr *attr,
		     int mask);
int mlx4_query_srq(struct ibv_srq *srq,
			   struct ibv_srq_attr *attr);
int mlx4_destroy_srq(struct ibv_srq *srq);
int mlx4_destroy_xrc_srq(struct ibv_srq *srq);
int mlx4_alloc_srq_buf(struct ibv_pd *pd, struct ibv_srq_attr *attr,
			struct mlx4_srq *srq);
void mlx4_init_xsrq_table(struct mlx4_xsrq_table *xsrq_table, int size);
struct mlx4_srq *mlx4_find_xsrq(struct mlx4_xsrq_table *xsrq_table, uint32_t srqn);
int mlx4_store_xsrq(struct mlx4_xsrq_table *xsrq_table, uint32_t srqn,
		    struct mlx4_srq *srq);
void mlx4_clear_xsrq(struct mlx4_xsrq_table *xsrq_table, uint32_t srqn);
void mlx4_free_srq_wqe(struct mlx4_srq *srq, int ind);
int mlx4_post_srq_recv(struct ibv_srq *ibsrq,
		       struct ibv_recv_wr *wr,
		       struct ibv_recv_wr **bad_wr);

struct ibv_qp *mlx4_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *attr);
struct ibv_qp *mlx4_create_qp_ex(struct ibv_context *context,
				 struct ibv_qp_init_attr_ex *attr);
struct ibv_qp *mlx4_open_qp(struct ibv_context *context, struct ibv_qp_open_attr *attr);
int mlx4_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		   int attr_mask,
		   struct ibv_qp_init_attr *init_attr);
int mlx4_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		    int attr_mask);
int mlx4_destroy_qp(struct ibv_qp *qp);
void mlx4_init_qp_indices(struct mlx4_qp *qp);
void mlx4_qp_init_sq_ownership(struct mlx4_qp *qp);
int mlx4_post_send(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
			  struct ibv_send_wr **bad_wr);
int mlx4_post_recv(struct ibv_qp *ibqp, struct ibv_recv_wr *wr,
			  struct ibv_recv_wr **bad_wr);
void mlx4_calc_sq_wqe_size(struct ibv_qp_cap *cap, enum ibv_qp_type type,
			   struct mlx4_qp *qp);
int mlx4_alloc_qp_buf(struct ibv_context *context, struct ibv_qp_cap *cap,
		       enum ibv_qp_type type, struct mlx4_qp *qp);
void mlx4_set_sq_sizes(struct mlx4_qp *qp, struct ibv_qp_cap *cap,
		       enum ibv_qp_type type);
struct mlx4_qp *mlx4_find_qp(struct mlx4_context *ctx, uint32_t qpn);
int mlx4_store_qp(struct mlx4_context *ctx, uint32_t qpn, struct mlx4_qp *qp);
void mlx4_clear_qp(struct mlx4_context *ctx, uint32_t qpn);
struct ibv_ah *mlx4_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr);
int mlx4_destroy_ah(struct ibv_ah *ah);
int mlx4_alloc_av(struct mlx4_pd *pd, struct ibv_ah_attr *attr,
		   struct mlx4_ah *ah);
void mlx4_free_av(struct mlx4_ah *ah);

#endif /* MLX4_H */
