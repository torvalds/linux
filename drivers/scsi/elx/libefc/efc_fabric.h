/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

/*
 * Declarations for the interface exported by efc_fabric
 */

#ifndef __EFCT_FABRIC_H__
#define __EFCT_FABRIC_H__
#include "scsi/fc/fc_els.h"
#include "scsi/fc/fc_fs.h"
#include "scsi/fc/fc_ns.h"

void
__efc_fabric_init(struct efc_sm_ctx *ctx,
		  enum efc_sm_event evt, void *arg);
void
__efc_fabric_flogi_wait_rsp(struct efc_sm_ctx *ctx,
			    enum efc_sm_event evt, void *arg);
void
__efc_fabric_domain_attach_wait(struct efc_sm_ctx *ctx,
				enum efc_sm_event evt, void *arg);
void
__efc_fabric_wait_domain_attach(struct efc_sm_ctx *ctx,
				enum efc_sm_event evt, void *arg);

void
__efc_vport_fabric_init(struct efc_sm_ctx *ctx,
			enum efc_sm_event evt, void *arg);
void
__efc_fabric_fdisc_wait_rsp(struct efc_sm_ctx *ctx,
			    enum efc_sm_event evt, void *arg);
void
__efc_fabric_wait_nport_attach(struct efc_sm_ctx *ctx,
			       enum efc_sm_event evt, void *arg);

void
__efc_ns_init(struct efc_sm_ctx *ctx, enum efc_sm_event evt, void *arg);
void
__efc_ns_plogi_wait_rsp(struct efc_sm_ctx *ctx,
			enum efc_sm_event evt, void *arg);
void
__efc_ns_rftid_wait_rsp(struct efc_sm_ctx *ctx,
			enum efc_sm_event evt, void *arg);
void
__efc_ns_rffid_wait_rsp(struct efc_sm_ctx *ctx,
			enum efc_sm_event evt, void *arg);
void
__efc_ns_wait_node_attach(struct efc_sm_ctx *ctx,
			  enum efc_sm_event evt, void *arg);
void
__efc_fabric_wait_attach_evt_shutdown(struct efc_sm_ctx *ctx,
				      enum efc_sm_event evt, void *arg);
void
__efc_ns_logo_wait_rsp(struct efc_sm_ctx *ctx,
		       enum efc_sm_event, void *arg);
void
__efc_ns_gidpt_wait_rsp(struct efc_sm_ctx *ctx,
			enum efc_sm_event evt, void *arg);
void
__efc_ns_idle(struct efc_sm_ctx *ctx, enum efc_sm_event evt, void *arg);
void
__efc_ns_gidpt_delay(struct efc_sm_ctx *ctx,
		     enum efc_sm_event evt, void *arg);
void
__efc_fabctl_init(struct efc_sm_ctx *ctx,
		  enum efc_sm_event evt, void *arg);
void
__efc_fabctl_wait_node_attach(struct efc_sm_ctx *ctx,
			      enum efc_sm_event evt, void *arg);
void
__efc_fabctl_wait_scr_rsp(struct efc_sm_ctx *ctx,
			  enum efc_sm_event evt, void *arg);
void
__efc_fabctl_ready(struct efc_sm_ctx *ctx,
		   enum efc_sm_event evt, void *arg);
void
__efc_fabctl_wait_ls_acc_cmpl(struct efc_sm_ctx *ctx,
			      enum efc_sm_event evt, void *arg);
void
__efc_fabric_idle(struct efc_sm_ctx *ctx,
		  enum efc_sm_event evt, void *arg);

void
__efc_p2p_rnode_init(struct efc_sm_ctx *ctx,
		     enum efc_sm_event evt, void *arg);
void
__efc_p2p_domain_attach_wait(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg);
void
__efc_p2p_wait_flogi_acc_cmpl(struct efc_sm_ctx *ctx,
			      enum efc_sm_event evt, void *arg);
void
__efc_p2p_wait_plogi_rsp(struct efc_sm_ctx *ctx,
			 enum efc_sm_event evt, void *arg);
void
__efc_p2p_wait_plogi_rsp_recvd_prli(struct efc_sm_ctx *ctx,
				    enum efc_sm_event evt, void *arg);
void
__efc_p2p_wait_domain_attach(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg);
void
__efc_p2p_wait_node_attach(struct efc_sm_ctx *ctx,
			   enum efc_sm_event evt, void *arg);

int
efc_p2p_setup(struct efc_nport *nport);
void
efc_fabric_set_topology(struct efc_node *node,
			enum efc_nport_topology topology);
void efc_fabric_notify_topology(struct efc_node *node);

#endif /* __EFCT_FABRIC_H__ */
