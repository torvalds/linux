/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014- QLogic Corporation.
 * All rights reserved
 * www.qlogic.com
 *
 * Linux driver for QLogic BR-series Fibre Channel Host Bus Adapter.
 */

#ifndef __BFA_PORT_H__
#define __BFA_PORT_H__

#include "bfa_defs_svc.h"
#include "bfa_ioc.h"
#include "bfa_cs.h"

typedef void (*bfa_port_stats_cbfn_t) (void *dev, bfa_status_t status);
typedef void (*bfa_port_endis_cbfn_t) (void *dev, bfa_status_t status);

struct bfa_port_s {
	void				*dev;
	struct bfa_ioc_s		*ioc;
	struct bfa_trc_mod_s		*trcmod;
	u32			msgtag;
	bfa_boolean_t			stats_busy;
	struct bfa_mbox_cmd_s		stats_mb;
	bfa_port_stats_cbfn_t		stats_cbfn;
	void				*stats_cbarg;
	bfa_status_t			stats_status;
	time64_t			stats_reset_time;
	union bfa_port_stats_u		*stats;
	struct bfa_dma_s		stats_dma;
	bfa_boolean_t			endis_pending;
	struct bfa_mbox_cmd_s		endis_mb;
	bfa_port_endis_cbfn_t		endis_cbfn;
	void				*endis_cbarg;
	bfa_status_t			endis_status;
	struct bfa_ioc_notify_s		ioc_notify;
	bfa_boolean_t			pbc_disabled;
	bfa_boolean_t			dport_enabled;
	struct bfa_mem_dma_s		port_dma;
};

#define BFA_MEM_PORT_DMA(__bfa)		(&((__bfa)->modules.port.port_dma))

void	     bfa_port_attach(struct bfa_port_s *port, struct bfa_ioc_s *ioc,
				void *dev, struct bfa_trc_mod_s *trcmod);
void	bfa_port_notify(void *arg, enum bfa_ioc_event_e event);

bfa_status_t bfa_port_get_stats(struct bfa_port_s *port,
				 union bfa_port_stats_u *stats,
				 bfa_port_stats_cbfn_t cbfn, void *cbarg);
bfa_status_t bfa_port_clear_stats(struct bfa_port_s *port,
				   bfa_port_stats_cbfn_t cbfn, void *cbarg);
bfa_status_t bfa_port_enable(struct bfa_port_s *port,
			      bfa_port_endis_cbfn_t cbfn, void *cbarg);
bfa_status_t bfa_port_disable(struct bfa_port_s *port,
			       bfa_port_endis_cbfn_t cbfn, void *cbarg);
u32     bfa_port_meminfo(void);
void	     bfa_port_mem_claim(struct bfa_port_s *port,
				 u8 *dma_kva, u64 dma_pa);
void	bfa_port_set_dportenabled(struct bfa_port_s *port,
				  bfa_boolean_t enabled);

/*
 * CEE declaration
 */
typedef void (*bfa_cee_get_attr_cbfn_t) (void *dev, bfa_status_t status);
typedef void (*bfa_cee_get_stats_cbfn_t) (void *dev, bfa_status_t status);
typedef void (*bfa_cee_reset_stats_cbfn_t) (void *dev, bfa_status_t status);

struct bfa_cee_cbfn_s {
	bfa_cee_get_attr_cbfn_t		get_attr_cbfn;
	void				*get_attr_cbarg;
	bfa_cee_get_stats_cbfn_t	get_stats_cbfn;
	void				*get_stats_cbarg;
	bfa_cee_reset_stats_cbfn_t	reset_stats_cbfn;
	void				*reset_stats_cbarg;
};

struct bfa_cee_s {
	void *dev;
	bfa_boolean_t		get_attr_pending;
	bfa_boolean_t		get_stats_pending;
	bfa_boolean_t		reset_stats_pending;
	bfa_status_t		get_attr_status;
	bfa_status_t		get_stats_status;
	bfa_status_t		reset_stats_status;
	struct bfa_cee_cbfn_s	cbfn;
	struct bfa_ioc_notify_s	ioc_notify;
	struct bfa_trc_mod_s	*trcmod;
	struct bfa_cee_attr_s	*attr;
	struct bfa_cee_stats_s	*stats;
	struct bfa_dma_s	attr_dma;
	struct bfa_dma_s	stats_dma;
	struct bfa_ioc_s	*ioc;
	struct bfa_mbox_cmd_s	get_cfg_mb;
	struct bfa_mbox_cmd_s	get_stats_mb;
	struct bfa_mbox_cmd_s	reset_stats_mb;
	struct bfa_mem_dma_s	cee_dma;
};

#define BFA_MEM_CEE_DMA(__bfa)	(&((__bfa)->modules.cee.cee_dma))

u32	bfa_cee_meminfo(void);
void	bfa_cee_mem_claim(struct bfa_cee_s *cee, u8 *dma_kva, u64 dma_pa);
void	bfa_cee_attach(struct bfa_cee_s *cee,
			struct bfa_ioc_s *ioc, void *dev);
bfa_status_t	bfa_cee_get_attr(struct bfa_cee_s *cee,
				struct bfa_cee_attr_s *attr,
				bfa_cee_get_attr_cbfn_t cbfn, void *cbarg);
bfa_status_t	bfa_cee_get_stats(struct bfa_cee_s *cee,
				struct bfa_cee_stats_s *stats,
				bfa_cee_get_stats_cbfn_t cbfn, void *cbarg);
bfa_status_t	bfa_cee_reset_stats(struct bfa_cee_s *cee,
				bfa_cee_reset_stats_cbfn_t cbfn, void *cbarg);

#endif	/* __BFA_PORT_H__ */
