/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

/*
 * Node state machine functions for remote device node sm
 */

#ifndef __EFCT_DEVICE_H__
#define __EFCT_DEVICE_H__
void
efc_node_init_device(struct efc_node *node, bool send_plogi);
void
efc_process_prli_payload(struct efc_node *node,
			 void *prli);
void
efc_d_send_prli_rsp(struct efc_node *node, uint16_t ox_id);
void
efc_send_ls_acc_after_attach(struct efc_node *node,
			     struct fc_frame_header *hdr,
			     enum efc_node_send_ls_acc ls);
void
__efc_d_wait_loop(struct efc_sm_ctx *ctx,
		  enum efc_sm_event evt, void *arg);
void
__efc_d_wait_plogi_acc_cmpl(struct efc_sm_ctx *ctx,
			    enum efc_sm_event evt, void *arg);
void
__efc_d_init(struct efc_sm_ctx *ctx, enum efc_sm_event evt, void *arg);
void
__efc_d_wait_plogi_rsp(struct efc_sm_ctx *ctx,
		       enum efc_sm_event evt, void *arg);
void
__efc_d_wait_plogi_rsp_recvd_prli(struct efc_sm_ctx *ctx,
				  enum efc_sm_event evt, void *arg);
void
__efc_d_wait_domain_attach(struct efc_sm_ctx *ctx,
			   enum efc_sm_event evt, void *arg);
void
__efc_d_wait_topology_notify(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg);
void
__efc_d_wait_node_attach(struct efc_sm_ctx *ctx,
			 enum efc_sm_event evt, void *arg);
void
__efc_d_wait_attach_evt_shutdown(struct efc_sm_ctx *ctx,
				 enum efc_sm_event evt, void *arg);
void
__efc_d_initiate_shutdown(struct efc_sm_ctx *ctx,
			  enum efc_sm_event evt, void *arg);
void
__efc_d_port_logged_in(struct efc_sm_ctx *ctx,
		       enum efc_sm_event evt, void *arg);
void
__efc_d_wait_logo_acc_cmpl(struct efc_sm_ctx *ctx,
			   enum efc_sm_event evt, void *arg);
void
__efc_d_device_ready(struct efc_sm_ctx *ctx,
		     enum efc_sm_event evt, void *arg);
void
__efc_d_device_gone(struct efc_sm_ctx *ctx,
		    enum efc_sm_event evt, void *arg);
void
__efc_d_wait_adisc_rsp(struct efc_sm_ctx *ctx,
		       enum efc_sm_event evt, void *arg);
void
__efc_d_wait_logo_rsp(struct efc_sm_ctx *ctx,
		      enum efc_sm_event evt, void *arg);

#endif /* __EFCT_DEVICE_H__ */
