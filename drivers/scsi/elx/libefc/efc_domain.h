/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

/*
 * Declare driver's domain handler exported interface
 */

#ifndef __EFCT_DOMAIN_H__
#define __EFCT_DOMAIN_H__

struct efc_domain *
efc_domain_alloc(struct efc *efc, uint64_t fcf_wwn);
void
efc_domain_free(struct efc_domain *domain);

void
__efc_domain_init(struct efc_sm_ctx *ctx, enum efc_sm_event evt, void *arg);
void
__efc_domain_wait_alloc(struct efc_sm_ctx *ctx,	enum efc_sm_event evt,
			void *arg);
void
__efc_domain_allocated(struct efc_sm_ctx *ctx, enum efc_sm_event evt,
		       void *arg);
void
__efc_domain_wait_attach(struct efc_sm_ctx *ctx, enum efc_sm_event evt,
			 void *arg);
void
__efc_domain_ready(struct efc_sm_ctx *ctx, enum efc_sm_event evt, void *arg);
void
__efc_domain_wait_nports_free(struct efc_sm_ctx *ctx, enum efc_sm_event evt,
			      void *arg);
void
__efc_domain_wait_shutdown(struct efc_sm_ctx *ctx, enum efc_sm_event evt,
			   void *arg);
void
__efc_domain_wait_domain_lost(struct efc_sm_ctx *ctx, enum efc_sm_event evt,
			      void *arg);
void
efc_domain_attach(struct efc_domain *domain, u32 s_id);
int
efc_domain_post_event(struct efc_domain *domain, enum efc_sm_event event,
		      void *arg);
void
__efc_domain_attach_internal(struct efc_domain *domain, u32 s_id);

int
efc_domain_dispatch_frame(void *arg, struct efc_hw_sequence *seq);
void
efc_node_dispatch_frame(void *arg, struct efc_hw_sequence *seq);

#endif /* __EFCT_DOMAIN_H__ */
