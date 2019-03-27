/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006 Cisco Systems, Inc.  All rights reserved.
 * Copyright (c) 2005 PathScale, Inc.  All rights reserved.
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

#ifndef INFINIBAND_DRIVER_H
#define INFINIBAND_DRIVER_H

#include <infiniband/verbs.h>
#include <infiniband/kern-abi.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else /* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif /* __cplusplus */

/*
 * Extension that low-level drivers should add to their .so filename
 * (probably via libtool "-release" option).  For example a low-level
 * driver named "libfoo" should build a plug-in named "libfoo-rdmav2.so".
 */
#define IBV_DEVICE_LIBRARY_EXTENSION rdmav2

struct verbs_device;

enum verbs_xrcd_mask {
	VERBS_XRCD_HANDLE	= 1 << 0,
	VERBS_XRCD_RESERVED	= 1 << 1
};

struct verbs_xrcd {
	struct ibv_xrcd		xrcd;
	uint32_t		comp_mask;
	uint32_t		handle;
};

enum verbs_srq_mask {
	VERBS_SRQ_TYPE		= 1 << 0,
	VERBS_SRQ_XRCD		= 1 << 1,
	VERBS_SRQ_CQ		= 1 << 2,
	VERBS_SRQ_NUM		= 1 << 3,
	VERBS_SRQ_RESERVED	= 1 << 4
};

struct verbs_srq {
	struct ibv_srq		srq;
	uint32_t		comp_mask;
	enum ibv_srq_type	srq_type;
	struct verbs_xrcd      *xrcd;
	struct ibv_cq	       *cq;
	uint32_t		srq_num;
};

enum verbs_qp_mask {
	VERBS_QP_XRCD		= 1 << 0,
	VERBS_QP_RESERVED	= 1 << 1
};

enum ibv_gid_type {
	IBV_GID_TYPE_IB_ROCE_V1,
	IBV_GID_TYPE_ROCE_V2,
};

struct verbs_qp {
	struct ibv_qp		qp;
	uint32_t		comp_mask;
	struct verbs_xrcd       *xrcd;
};

/* Must change the PRIVATE IBVERBS_PRIVATE_ symbol if this is changed */
struct verbs_device_ops {
	/* Old interface, do not use in new code. */
	struct ibv_context *(*alloc_context)(struct ibv_device *device,
					     int cmd_fd);
	void (*free_context)(struct ibv_context *context);

	/* New interface */
	int (*init_context)(struct verbs_device *device,
			    struct ibv_context *ctx, int cmd_fd);
	void (*uninit_context)(struct verbs_device *device,
			       struct ibv_context *ctx);
};

/* Must change the PRIVATE IBVERBS_PRIVATE_ symbol if this is changed */
struct verbs_device {
	struct ibv_device device; /* Must be first */
	const struct verbs_device_ops *ops;
	size_t	sz;
	size_t	size_of_context;
};

static inline struct verbs_device *
verbs_get_device(const struct ibv_device *dev)
{
	return container_of(dev, struct verbs_device, device);
}

typedef struct verbs_device *(*verbs_driver_init_func)(const char *uverbs_sys_path,
						       int abi_version);
void verbs_register_driver(const char *name, verbs_driver_init_func init_func);
void verbs_init_cq(struct ibv_cq *cq, struct ibv_context *context,
		       struct ibv_comp_channel *channel,
		       void *cq_context);

int ibv_cmd_get_context(struct ibv_context *context, struct ibv_get_context *cmd,
			size_t cmd_size, struct ibv_get_context_resp *resp,
			size_t resp_size);
int ibv_cmd_query_device(struct ibv_context *context,
			 struct ibv_device_attr *device_attr,
			 uint64_t *raw_fw_ver,
			 struct ibv_query_device *cmd, size_t cmd_size);
int ibv_cmd_query_device_ex(struct ibv_context *context,
			    const struct ibv_query_device_ex_input *input,
			    struct ibv_device_attr_ex *attr, size_t attr_size,
			    uint64_t *raw_fw_ver,
			    struct ibv_query_device_ex *cmd,
			    size_t cmd_core_size,
			    size_t cmd_size,
			    struct ibv_query_device_resp_ex *resp,
			    size_t resp_core_size,
			    size_t resp_size);
int ibv_cmd_query_port(struct ibv_context *context, uint8_t port_num,
		       struct ibv_port_attr *port_attr,
		       struct ibv_query_port *cmd, size_t cmd_size);
int ibv_cmd_alloc_pd(struct ibv_context *context, struct ibv_pd *pd,
		     struct ibv_alloc_pd *cmd, size_t cmd_size,
		     struct ibv_alloc_pd_resp *resp, size_t resp_size);
int ibv_cmd_dealloc_pd(struct ibv_pd *pd);
int ibv_cmd_open_xrcd(struct ibv_context *context, struct verbs_xrcd *xrcd,
		      int vxrcd_size,
		      struct ibv_xrcd_init_attr *attr,
		      struct ibv_open_xrcd *cmd, size_t cmd_size,
		      struct ibv_open_xrcd_resp *resp, size_t resp_size);
int ibv_cmd_close_xrcd(struct verbs_xrcd *xrcd);
#define IBV_CMD_REG_MR_HAS_RESP_PARAMS
int ibv_cmd_reg_mr(struct ibv_pd *pd, void *addr, size_t length,
		   uint64_t hca_va, int access,
		   struct ibv_mr *mr, struct ibv_reg_mr *cmd,
		   size_t cmd_size,
		   struct ibv_reg_mr_resp *resp, size_t resp_size);
int ibv_cmd_rereg_mr(struct ibv_mr *mr, uint32_t flags, void *addr,
		     size_t length, uint64_t hca_va, int access,
		     struct ibv_pd *pd, struct ibv_rereg_mr *cmd,
		     size_t cmd_sz, struct ibv_rereg_mr_resp *resp,
		     size_t resp_sz);
int ibv_cmd_dereg_mr(struct ibv_mr *mr);
int ibv_cmd_alloc_mw(struct ibv_pd *pd, enum ibv_mw_type type,
		     struct ibv_mw *mw, struct ibv_alloc_mw *cmd,
		     size_t cmd_size,
		     struct ibv_alloc_mw_resp *resp, size_t resp_size);
int ibv_cmd_dealloc_mw(struct ibv_mw *mw,
		       struct ibv_dealloc_mw *cmd, size_t cmd_size);
int ibv_cmd_create_cq(struct ibv_context *context, int cqe,
		      struct ibv_comp_channel *channel,
		      int comp_vector, struct ibv_cq *cq,
		      struct ibv_create_cq *cmd, size_t cmd_size,
		      struct ibv_create_cq_resp *resp, size_t resp_size);
int ibv_cmd_create_cq_ex(struct ibv_context *context,
			 struct ibv_cq_init_attr_ex *cq_attr,
			 struct ibv_cq_ex *cq,
			 struct ibv_create_cq_ex *cmd,
			 size_t cmd_core_size,
			 size_t cmd_size,
			 struct ibv_create_cq_resp_ex *resp,
			 size_t resp_core_size,
			 size_t resp_size);
int ibv_cmd_poll_cq(struct ibv_cq *cq, int ne, struct ibv_wc *wc);
int ibv_cmd_req_notify_cq(struct ibv_cq *cq, int solicited_only);
#define IBV_CMD_RESIZE_CQ_HAS_RESP_PARAMS
int ibv_cmd_resize_cq(struct ibv_cq *cq, int cqe,
		      struct ibv_resize_cq *cmd, size_t cmd_size,
		      struct ibv_resize_cq_resp *resp, size_t resp_size);
int ibv_cmd_destroy_cq(struct ibv_cq *cq);

int ibv_cmd_create_srq(struct ibv_pd *pd,
		       struct ibv_srq *srq, struct ibv_srq_init_attr *attr,
		       struct ibv_create_srq *cmd, size_t cmd_size,
		       struct ibv_create_srq_resp *resp, size_t resp_size);
int ibv_cmd_create_srq_ex(struct ibv_context *context,
			  struct verbs_srq *srq, int vsrq_sz,
			  struct ibv_srq_init_attr_ex *attr_ex,
			  struct ibv_create_xsrq *cmd, size_t cmd_size,
			  struct ibv_create_srq_resp *resp, size_t resp_size);
int ibv_cmd_modify_srq(struct ibv_srq *srq,
		       struct ibv_srq_attr *srq_attr,
		       int srq_attr_mask,
		       struct ibv_modify_srq *cmd, size_t cmd_size);
int ibv_cmd_query_srq(struct ibv_srq *srq,
		      struct ibv_srq_attr *srq_attr,
		      struct ibv_query_srq *cmd, size_t cmd_size);
int ibv_cmd_destroy_srq(struct ibv_srq *srq);

int ibv_cmd_create_qp(struct ibv_pd *pd,
		      struct ibv_qp *qp, struct ibv_qp_init_attr *attr,
		      struct ibv_create_qp *cmd, size_t cmd_size,
		      struct ibv_create_qp_resp *resp, size_t resp_size);
int ibv_cmd_create_qp_ex(struct ibv_context *context,
			 struct verbs_qp *qp, int vqp_sz,
			 struct ibv_qp_init_attr_ex *attr_ex,
			 struct ibv_create_qp *cmd, size_t cmd_size,
			 struct ibv_create_qp_resp *resp, size_t resp_size);
int ibv_cmd_create_qp_ex2(struct ibv_context *context,
			  struct verbs_qp *qp, int vqp_sz,
			  struct ibv_qp_init_attr_ex *qp_attr,
			  struct ibv_create_qp_ex *cmd,
			  size_t cmd_core_size,
			  size_t cmd_size,
			  struct ibv_create_qp_resp_ex *resp,
			  size_t resp_core_size,
			  size_t resp_size);
int ibv_cmd_open_qp(struct ibv_context *context,
		    struct verbs_qp *qp,  int vqp_sz,
		    struct ibv_qp_open_attr *attr,
		    struct ibv_open_qp *cmd, size_t cmd_size,
		    struct ibv_create_qp_resp *resp, size_t resp_size);
int ibv_cmd_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *qp_attr,
		     int attr_mask,
		     struct ibv_qp_init_attr *qp_init_attr,
		     struct ibv_query_qp *cmd, size_t cmd_size);
int ibv_cmd_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		      int attr_mask,
		      struct ibv_modify_qp *cmd, size_t cmd_size);
int ibv_cmd_modify_qp_ex(struct ibv_qp *qp, struct ibv_qp_attr *attr,
			 int attr_mask, struct ibv_modify_qp_ex *cmd,
			 size_t cmd_core_size, size_t cmd_size,
			 struct ibv_modify_qp_resp_ex *resp,
			 size_t resp_core_size, size_t resp_size);
int ibv_cmd_destroy_qp(struct ibv_qp *qp);
int ibv_cmd_post_send(struct ibv_qp *ibqp, struct ibv_send_wr *wr,
		      struct ibv_send_wr **bad_wr);
int ibv_cmd_post_recv(struct ibv_qp *ibqp, struct ibv_recv_wr *wr,
		      struct ibv_recv_wr **bad_wr);
int ibv_cmd_post_srq_recv(struct ibv_srq *srq, struct ibv_recv_wr *wr,
			  struct ibv_recv_wr **bad_wr);
int ibv_cmd_create_ah(struct ibv_pd *pd, struct ibv_ah *ah,
		      struct ibv_ah_attr *attr,
		      struct ibv_create_ah_resp *resp,
		      size_t resp_size);
int ibv_cmd_destroy_ah(struct ibv_ah *ah);
int ibv_cmd_attach_mcast(struct ibv_qp *qp, const union ibv_gid *gid, uint16_t lid);
int ibv_cmd_detach_mcast(struct ibv_qp *qp, const union ibv_gid *gid, uint16_t lid);

struct ibv_flow *ibv_cmd_create_flow(struct ibv_qp *qp,
				     struct ibv_flow_attr *flow_attr);
int ibv_cmd_destroy_flow(struct ibv_flow *flow_id);
int ibv_cmd_create_wq(struct ibv_context *context,
		      struct ibv_wq_init_attr *wq_init_attr,
		      struct ibv_wq *wq,
		      struct ibv_create_wq *cmd,
		      size_t cmd_core_size,
		      size_t cmd_size,
		      struct ibv_create_wq_resp *resp,
		      size_t resp_core_size,
		      size_t resp_size);

int ibv_cmd_modify_wq(struct ibv_wq *wq, struct ibv_wq_attr *attr,
		      struct ibv_modify_wq *cmd, size_t cmd_core_size,
		      size_t cmd_size);
int ibv_cmd_destroy_wq(struct ibv_wq *wq);
int ibv_cmd_create_rwq_ind_table(struct ibv_context *context,
				 struct ibv_rwq_ind_table_init_attr *init_attr,
				 struct ibv_rwq_ind_table *rwq_ind_table,
				 struct ibv_create_rwq_ind_table *cmd,
				 size_t cmd_core_size,
				 size_t cmd_size,
				 struct ibv_create_rwq_ind_table_resp *resp,
				 size_t resp_core_size,
				 size_t resp_size);
int ibv_cmd_destroy_rwq_ind_table(struct ibv_rwq_ind_table *rwq_ind_table);
int ibv_dontfork_range(void *base, size_t size);
int ibv_dofork_range(void *base, size_t size);

/*
 * sysfs helper functions
 */
const char *ibv_get_sysfs_path(void);

int ibv_read_sysfs_file(const char *dir, const char *file,
			char *buf, size_t size);

static inline int verbs_get_srq_num(struct ibv_srq *srq, uint32_t *srq_num)
{
	struct verbs_srq *vsrq = container_of(srq, struct verbs_srq, srq);
	if (vsrq->comp_mask & VERBS_SRQ_NUM) {
		*srq_num = vsrq->srq_num;
		return 0;
	}
	return ENOSYS;
}

int ibv_query_gid_type(struct ibv_context *context, uint8_t port_num,
		       unsigned int index, enum ibv_gid_type *type);
#endif /* INFINIBAND_DRIVER_H */
