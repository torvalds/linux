/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linux network driver for QLogic BR-series Converged Network Adapter.
 */
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014-2015 QLogic Corporation
 * All rights reserved
 * www.qlogic.com
 */

#ifndef __BFA_CEE_H__
#define __BFA_CEE_H__

#include "bfa_defs_cna.h"
#include "bfa_ioc.h"

typedef void (*bfa_cee_get_attr_cbfn_t) (void *dev, enum bfa_status status);
typedef void (*bfa_cee_get_stats_cbfn_t) (void *dev, enum bfa_status status);
typedef void (*bfa_cee_reset_stats_cbfn_t) (void *dev, enum bfa_status status);

struct bfa_cee_cbfn {
	bfa_cee_get_attr_cbfn_t    get_attr_cbfn;
	void *get_attr_cbarg;
	bfa_cee_get_stats_cbfn_t   get_stats_cbfn;
	void *get_stats_cbarg;
	bfa_cee_reset_stats_cbfn_t reset_stats_cbfn;
	void *reset_stats_cbarg;
};

struct bfa_cee {
	void *dev;
	bool get_attr_pending;
	bool get_stats_pending;
	bool reset_stats_pending;
	enum bfa_status get_attr_status;
	enum bfa_status get_stats_status;
	enum bfa_status reset_stats_status;
	struct bfa_cee_cbfn cbfn;
	struct bfa_ioc_notify ioc_notify;
	struct bfa_cee_attr *attr;
	struct bfa_cee_stats *stats;
	struct bfa_dma attr_dma;
	struct bfa_dma stats_dma;
	struct bfa_ioc *ioc;
	struct bfa_mbox_cmd get_cfg_mb;
	struct bfa_mbox_cmd get_stats_mb;
	struct bfa_mbox_cmd reset_stats_mb;
};

u32 bfa_nw_cee_meminfo(void);
void bfa_nw_cee_mem_claim(struct bfa_cee *cee, u8 *dma_kva,
	u64 dma_pa);
void bfa_nw_cee_attach(struct bfa_cee *cee, struct bfa_ioc *ioc, void *dev);
enum bfa_status bfa_nw_cee_get_attr(struct bfa_cee *cee,
				struct bfa_cee_attr *attr,
				bfa_cee_get_attr_cbfn_t cbfn, void *cbarg);
#endif /* __BFA_CEE_H__ */
