/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#if !defined(__EFC_NODE_H__)
#define __EFC_NODE_H__
#include "scsi/fc/fc_ns.h"

#define EFC_NODEDB_PAUSE_FABRIC_LOGIN	(1 << 0)
#define EFC_NODEDB_PAUSE_NAMESERVER	(1 << 1)
#define EFC_NODEDB_PAUSE_NEW_NODES	(1 << 2)

#define MAX_ACC_REJECT_PAYLOAD	sizeof(struct fc_els_ls_rjt)

#define scsi_io_printf(io, fmt, ...) \
	efc_log_debug(io->efc, "[%s] [%04x][i:%04x t:%04x h:%04x]" fmt, \
	io->node->display_name, io->instance_index, io->init_task_tag, \
	io->tgt_task_tag, io->hw_tag, ##__VA_ARGS__)

static inline void
efc_node_evt_set(struct efc_sm_ctx *ctx, enum efc_sm_event evt,
		 const char *handler)
{
	struct efc_node *node = ctx->app;

	if (evt == EFC_EVT_ENTER) {
		strscpy_pad(node->current_state_name, handler,
			    sizeof(node->current_state_name));
	} else if (evt == EFC_EVT_EXIT) {
		memcpy(node->prev_state_name, node->current_state_name,
		       sizeof(node->prev_state_name));
		strscpy_pad(node->current_state_name, "invalid",
			    sizeof(node->current_state_name));
	}
	node->prev_evt = node->current_evt;
	node->current_evt = evt;
}

/**
 * hold frames in pending frame list
 *
 * Unsolicited receive frames are held on the node pending frame list,
 * rather than being processed.
 */

static inline void
efc_node_hold_frames(struct efc_node *node)
{
	node->hold_frames = true;
}

/**
 * accept frames
 *
 * Unsolicited receive frames processed rather than being held on the node
 * pending frame list.
 */

static inline void
efc_node_accept_frames(struct efc_node *node)
{
	node->hold_frames = false;
}

/*
 * Node initiator/target enable defines
 * All combinations of the SLI port (nport) initiator/target enable,
 * and remote node initiator/target enable are enumerated.
 * ex: EFC_NODE_ENABLE_T_TO_IT decodes to target mode is enabled on SLI port
 * and I+T is enabled on remote node.
 */
enum efc_node_enable {
	EFC_NODE_ENABLE_x_TO_x,
	EFC_NODE_ENABLE_x_TO_T,
	EFC_NODE_ENABLE_x_TO_I,
	EFC_NODE_ENABLE_x_TO_IT,
	EFC_NODE_ENABLE_T_TO_x,
	EFC_NODE_ENABLE_T_TO_T,
	EFC_NODE_ENABLE_T_TO_I,
	EFC_NODE_ENABLE_T_TO_IT,
	EFC_NODE_ENABLE_I_TO_x,
	EFC_NODE_ENABLE_I_TO_T,
	EFC_NODE_ENABLE_I_TO_I,
	EFC_NODE_ENABLE_I_TO_IT,
	EFC_NODE_ENABLE_IT_TO_x,
	EFC_NODE_ENABLE_IT_TO_T,
	EFC_NODE_ENABLE_IT_TO_I,
	EFC_NODE_ENABLE_IT_TO_IT,
};

static inline enum efc_node_enable
efc_node_get_enable(struct efc_node *node)
{
	u32 retval = 0;

	if (node->nport->enable_ini)
		retval |= (1U << 3);
	if (node->nport->enable_tgt)
		retval |= (1U << 2);
	if (node->init)
		retval |= (1U << 1);
	if (node->targ)
		retval |= (1U << 0);
	return (enum efc_node_enable)retval;
}

int
efc_node_check_els_req(struct efc_sm_ctx *ctx,
		       enum efc_sm_event evt, void *arg,
		       u8 cmd, void (*efc_node_common_func)(const char *,
		       struct efc_sm_ctx *, enum efc_sm_event, void *),
		       const char *funcname);
int
efc_node_check_ns_req(struct efc_sm_ctx *ctx,
		      enum efc_sm_event evt, void *arg,
		      u16 cmd, void (*efc_node_common_func)(const char *,
		      struct efc_sm_ctx *, enum efc_sm_event, void *),
		      const char *funcname);
int
efc_node_attach(struct efc_node *node);
struct efc_node *
efc_node_alloc(struct efc_nport *nport, u32 port_id,
	       bool init, bool targ);
void
efc_node_free(struct efc_node *efc);
void
efc_node_update_display_name(struct efc_node *node);
void efc_node_post_event(struct efc_node *node, enum efc_sm_event evt,
			 void *arg);

void
__efc_node_shutdown(struct efc_sm_ctx *ctx,
		    enum efc_sm_event evt, void *arg);
void
__efc_node_wait_node_free(struct efc_sm_ctx *ctx,
			  enum efc_sm_event evt, void *arg);
void
__efc_node_wait_els_shutdown(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg);
void
__efc_node_wait_ios_shutdown(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg);
void
efc_node_save_sparms(struct efc_node *node, void *payload);
void
efc_node_transition(struct efc_node *node,
		    void (*state)(struct efc_sm_ctx *, enum efc_sm_event,
				  void *), void *data);
void
__efc_node_common(const char *funcname, struct efc_sm_ctx *ctx,
		  enum efc_sm_event evt, void *arg);

void
efc_node_initiate_cleanup(struct efc_node *node);

void
efc_node_build_eui_name(char *buf, u32 buf_len, uint64_t eui_name);

void
efc_node_pause(struct efc_node *node,
	       void (*state)(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg));
void
__efc_node_paused(struct efc_sm_ctx *ctx,
		  enum efc_sm_event evt, void *arg);
int
efc_node_active_ios_empty(struct efc_node *node);
void
efc_node_send_ls_io_cleanup(struct efc_node *node);

int
efc_els_io_list_empty(struct efc_node *node, struct list_head *list);

void
efc_process_node_pending(struct efc_node *domain);

u64 efc_node_get_wwnn(struct efc_node *node);
struct efc_node *
efc_node_find(struct efc_nport *nport, u32 id);
void
efc_node_post_els_resp(struct efc_node *node, u32 evt, void *arg);
void
efc_node_recv_els_frame(struct efc_node *node, struct efc_hw_sequence *s);
void
efc_node_recv_ct_frame(struct efc_node *node, struct efc_hw_sequence *seq);
void
efc_node_recv_fcp_cmd(struct efc_node *node, struct efc_hw_sequence *seq);

#endif /* __EFC_NODE_H__ */
