/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2024 Intel Corporation */

#ifndef _IDPF_VIRTCHNL_H_
#define _IDPF_VIRTCHNL_H_

#include "virtchnl2.h"

#define IDPF_VC_XN_DEFAULT_TIMEOUT_MSEC	(60 * 1000)
#define IDPF_VC_XN_IDX_M		GENMASK(7, 0)
#define IDPF_VC_XN_SALT_M		GENMASK(15, 8)
#define IDPF_VC_XN_RING_LEN		U8_MAX

/**
 * enum idpf_vc_xn_state - Virtchnl transaction status
 * @IDPF_VC_XN_IDLE: not expecting a reply, ready to be used
 * @IDPF_VC_XN_WAITING: expecting a reply, not yet received
 * @IDPF_VC_XN_COMPLETED_SUCCESS: a reply was expected and received, buffer
 *				  updated
 * @IDPF_VC_XN_COMPLETED_FAILED: a reply was expected and received, but there
 *				 was an error, buffer not updated
 * @IDPF_VC_XN_SHUTDOWN: transaction object cannot be used, VC torn down
 * @IDPF_VC_XN_ASYNC: transaction sent asynchronously and doesn't have the
 *		      return context; a callback may be provided to handle
 *		      return
 */
enum idpf_vc_xn_state {
	IDPF_VC_XN_IDLE = 1,
	IDPF_VC_XN_WAITING,
	IDPF_VC_XN_COMPLETED_SUCCESS,
	IDPF_VC_XN_COMPLETED_FAILED,
	IDPF_VC_XN_SHUTDOWN,
	IDPF_VC_XN_ASYNC,
};

struct idpf_vc_xn;
/* Callback for asynchronous messages */
typedef int (*async_vc_cb) (struct idpf_adapter *, struct idpf_vc_xn *,
			    const struct idpf_ctlq_msg *);

/**
 * struct idpf_vc_xn - Data structure representing virtchnl transactions
 * @completed: virtchnl event loop uses that to signal when a reply is
 *	       available, uses kernel completion API
 * @state: virtchnl event loop stores the data below, protected by the
 *	   completion's lock.
 * @reply_sz: Original size of reply, may be > reply_buf.iov_len; it will be
 *	      truncated on its way to the receiver thread according to
 *	      reply_buf.iov_len.
 * @reply: Reference to the buffer(s) where the reply data should be written
 *	   to. May be 0-length (then NULL address permitted) if the reply data
 *	   should be ignored.
 * @async_handler: if sent asynchronously, a callback can be provided to handle
 *		   the reply when it's received
 * @vc_op: corresponding opcode sent with this transaction
 * @idx: index used as retrieval on reply receive, used for cookie
 * @salt: changed every message to make unique, used for cookie
 */
struct idpf_vc_xn {
	struct completion completed;
	enum idpf_vc_xn_state state;
	size_t reply_sz;
	struct kvec reply;
	async_vc_cb async_handler;
	u32 vc_op;
	u8 idx;
	u8 salt;
};

/**
 * struct idpf_vc_xn_params - Parameters for executing transaction
 * @send_buf: kvec for send buffer
 * @recv_buf: kvec for recv buffer, may be NULL, must then have zero length
 * @timeout_ms: timeout to wait for reply
 * @async: send message asynchronously, will not wait on completion
 * @async_handler: If sent asynchronously, optional callback handler. The user
 *		   must be careful when using async handlers as the memory for
 *		   the recv_buf _cannot_ be on stack if this is async.
 * @vc_op: virtchnl op to send
 */
struct idpf_vc_xn_params {
	struct kvec send_buf;
	struct kvec recv_buf;
	int timeout_ms;
	bool async;
	async_vc_cb async_handler;
	u32 vc_op;
};

struct idpf_adapter;
struct idpf_netdev_priv;
struct idpf_vec_regs;
struct idpf_vport;
struct idpf_vport_max_q;
struct idpf_vport_user_config_data;

ssize_t idpf_vc_xn_exec(struct idpf_adapter *adapter,
			const struct idpf_vc_xn_params *params);
int idpf_init_dflt_mbx(struct idpf_adapter *adapter);
void idpf_deinit_dflt_mbx(struct idpf_adapter *adapter);
int idpf_vc_core_init(struct idpf_adapter *adapter);
void idpf_vc_core_deinit(struct idpf_adapter *adapter);

int idpf_get_reg_intr_vecs(struct idpf_vport *vport,
			   struct idpf_vec_regs *reg_vals);
int idpf_queue_reg_init(struct idpf_vport *vport);
int idpf_vport_queue_ids_init(struct idpf_vport *vport);

bool idpf_vport_is_cap_ena(struct idpf_vport *vport, u16 flag);
bool idpf_sideband_flow_type_ena(struct idpf_vport *vport, u32 flow_type);
bool idpf_sideband_action_ena(struct idpf_vport *vport,
			      struct ethtool_rx_flow_spec *fsp);
unsigned int idpf_fsteer_max_rules(struct idpf_vport *vport);

int idpf_recv_mb_msg(struct idpf_adapter *adapter);
int idpf_send_mb_msg(struct idpf_adapter *adapter, u32 op,
		     u16 msg_size, u8 *msg, u16 cookie);

struct idpf_queue_ptr {
	enum virtchnl2_queue_type	type;
	union {
		struct idpf_rx_queue		*rxq;
		struct idpf_tx_queue		*txq;
		struct idpf_buf_queue		*bufq;
		struct idpf_compl_queue		*complq;
	};
};

struct idpf_queue_set {
	struct idpf_vport		*vport;

	u32				num;
	struct idpf_queue_ptr		qs[] __counted_by(num);
};

struct idpf_queue_set *idpf_alloc_queue_set(struct idpf_vport *vport, u32 num);

int idpf_send_enable_queue_set_msg(const struct idpf_queue_set *qs);
int idpf_send_disable_queue_set_msg(const struct idpf_queue_set *qs);
int idpf_send_config_queue_set_msg(const struct idpf_queue_set *qs);

int idpf_send_disable_queues_msg(struct idpf_vport *vport);
int idpf_send_config_queues_msg(struct idpf_vport *vport);
int idpf_send_enable_queues_msg(struct idpf_vport *vport);

void idpf_vport_init(struct idpf_vport *vport, struct idpf_vport_max_q *max_q);
u32 idpf_get_vport_id(struct idpf_vport *vport);
int idpf_send_create_vport_msg(struct idpf_adapter *adapter,
			       struct idpf_vport_max_q *max_q);
int idpf_send_destroy_vport_msg(struct idpf_vport *vport);
int idpf_send_enable_vport_msg(struct idpf_vport *vport);
int idpf_send_disable_vport_msg(struct idpf_vport *vport);

int idpf_vport_adjust_qs(struct idpf_vport *vport);
int idpf_vport_alloc_max_qs(struct idpf_adapter *adapter,
			    struct idpf_vport_max_q *max_q);
void idpf_vport_dealloc_max_qs(struct idpf_adapter *adapter,
			       struct idpf_vport_max_q *max_q);
int idpf_send_add_queues_msg(const struct idpf_vport *vport, u16 num_tx_q,
			     u16 num_complq, u16 num_rx_q, u16 num_rx_bufq);
int idpf_send_delete_queues_msg(struct idpf_vport *vport);

int idpf_vport_alloc_vec_indexes(struct idpf_vport *vport);
int idpf_get_vec_ids(struct idpf_adapter *adapter,
		     u16 *vecids, int num_vecids,
		     struct virtchnl2_vector_chunks *chunks);
int idpf_send_alloc_vectors_msg(struct idpf_adapter *adapter, u16 num_vectors);
int idpf_send_dealloc_vectors_msg(struct idpf_adapter *adapter);
int idpf_send_map_unmap_queue_vector_msg(struct idpf_vport *vport, bool map);

int idpf_add_del_mac_filters(struct idpf_vport *vport,
			     struct idpf_netdev_priv *np,
			     bool add, bool async);
int idpf_set_promiscuous(struct idpf_adapter *adapter,
			 struct idpf_vport_user_config_data *config_data,
			 u32 vport_id);
int idpf_check_supported_desc_ids(struct idpf_vport *vport);
int idpf_send_get_rx_ptype_msg(struct idpf_vport *vport);
int idpf_send_ena_dis_loopback_msg(struct idpf_vport *vport);
int idpf_send_get_stats_msg(struct idpf_vport *vport);
int idpf_send_set_sriov_vfs_msg(struct idpf_adapter *adapter, u16 num_vfs);
int idpf_send_get_set_rss_key_msg(struct idpf_vport *vport, bool get);
int idpf_send_get_set_rss_lut_msg(struct idpf_vport *vport, bool get);
void idpf_vc_xn_shutdown(struct idpf_vc_xn_manager *vcxn_mngr);
int idpf_idc_rdma_vc_send_sync(struct iidc_rdma_core_dev_info *cdev_info,
			       u8 *send_msg, u16 msg_size,
			       u8 *recv_msg, u16 *recv_len);

#endif /* _IDPF_VIRTCHNL_H_ */
