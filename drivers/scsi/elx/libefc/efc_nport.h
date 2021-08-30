/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

/**
 * EFC FC port (NPORT) exported declarations
 *
 */

#ifndef __EFC_NPORT_H__
#define __EFC_NPORT_H__

struct efc_nport *
efc_nport_find(struct efc_domain *domain, u32 d_id);
struct efc_nport *
efc_nport_alloc(struct efc_domain *domain, uint64_t wwpn, uint64_t wwnn,
		u32 fc_id, bool enable_ini, bool enable_tgt);
void
efc_nport_free(struct efc_nport *nport);
int
efc_nport_attach(struct efc_nport *nport, u32 fc_id);

void
__efc_nport_allocated(struct efc_sm_ctx *ctx,
		      enum efc_sm_event evt, void *arg);
void
__efc_nport_wait_shutdown(struct efc_sm_ctx *ctx,
			  enum efc_sm_event evt, void *arg);
void
__efc_nport_wait_port_free(struct efc_sm_ctx *ctx,
			   enum efc_sm_event evt, void *arg);
void
__efc_nport_vport_init(struct efc_sm_ctx *ctx,
		       enum efc_sm_event evt, void *arg);
void
__efc_nport_vport_wait_alloc(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg);
void
__efc_nport_vport_allocated(struct efc_sm_ctx *ctx,
			    enum efc_sm_event evt, void *arg);
void
__efc_nport_attached(struct efc_sm_ctx *ctx,
		     enum efc_sm_event evt, void *arg);

int
efc_vport_start(struct efc_domain *domain);

#endif /* __EFC_NPORT_H__ */
