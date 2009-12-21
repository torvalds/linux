/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <cs/bfa_debug.h>
#include <bfa_priv.h>
#include <log/bfa_log_hal.h>
#include <bfi/bfi_boot.h>
#include <bfi/bfi_cbreg.h>
#include <aen/bfa_aen_ioc.h>
#include <defs/bfa_defs_iocfc.h>
#include <defs/bfa_defs_pci.h>
#include "bfa_callback_priv.h"
#include "bfad_drv.h"

BFA_TRC_FILE(HAL, IOCFC);

/**
 * IOC local definitions
 */
#define BFA_IOCFC_TOV		5000	/* msecs */

enum {
	BFA_IOCFC_ACT_NONE	= 0,
	BFA_IOCFC_ACT_INIT	= 1,
	BFA_IOCFC_ACT_STOP	= 2,
	BFA_IOCFC_ACT_DISABLE	= 3,
};

/*
 * forward declarations
 */
static void bfa_iocfc_enable_cbfn(void *bfa_arg, enum bfa_status status);
static void bfa_iocfc_disable_cbfn(void *bfa_arg);
static void bfa_iocfc_hbfail_cbfn(void *bfa_arg);
static void bfa_iocfc_reset_cbfn(void *bfa_arg);
static void bfa_iocfc_stats_clear(void *bfa_arg);
static void bfa_iocfc_stats_swap(struct bfa_fw_stats_s *d,
			struct bfa_fw_stats_s *s);
static void bfa_iocfc_stats_clr_cb(void *bfa_arg, bfa_boolean_t complete);
static void bfa_iocfc_stats_clr_timeout(void *bfa_arg);
static void bfa_iocfc_stats_cb(void *bfa_arg, bfa_boolean_t complete);
static void bfa_iocfc_stats_timeout(void *bfa_arg);

static struct bfa_ioc_cbfn_s bfa_iocfc_cbfn;

/**
 *  bfa_ioc_pvt BFA IOC private functions
 */

static void
bfa_iocfc_cqs_sz(struct bfa_iocfc_cfg_s *cfg, u32 *dm_len)
{
	int             i, per_reqq_sz, per_rspq_sz;

	per_reqq_sz = BFA_ROUNDUP((cfg->drvcfg.num_reqq_elems * BFI_LMSG_SZ),
							BFA_DMA_ALIGN_SZ);
	per_rspq_sz = BFA_ROUNDUP((cfg->drvcfg.num_rspq_elems * BFI_LMSG_SZ),
							BFA_DMA_ALIGN_SZ);

	/*
	 * Calculate CQ size
	 */
	for (i = 0; i < cfg->fwcfg.num_cqs; i++) {
		*dm_len = *dm_len + per_reqq_sz;
		*dm_len = *dm_len + per_rspq_sz;
	}

	/*
	 * Calculate Shadow CI/PI size
	 */
	for (i = 0; i < cfg->fwcfg.num_cqs; i++)
		*dm_len += (2 * BFA_CACHELINE_SZ);
}

static void
bfa_iocfc_fw_cfg_sz(struct bfa_iocfc_cfg_s *cfg, u32 *dm_len)
{
	*dm_len +=
		BFA_ROUNDUP(sizeof(struct bfi_iocfc_cfg_s), BFA_CACHELINE_SZ);
	*dm_len +=
		BFA_ROUNDUP(sizeof(struct bfi_iocfc_cfgrsp_s),
			    BFA_CACHELINE_SZ);
	*dm_len += BFA_ROUNDUP(sizeof(struct bfa_fw_stats_s), BFA_CACHELINE_SZ);
}

/**
 * Use the Mailbox interface to send BFI_IOCFC_H2I_CFG_REQ
 */
static void
bfa_iocfc_send_cfg(void *bfa_arg)
{
	struct bfa_s *bfa = bfa_arg;
	struct bfa_iocfc_s *iocfc = &bfa->iocfc;
	struct bfi_iocfc_cfg_req_s cfg_req;
	struct bfi_iocfc_cfg_s *cfg_info = iocfc->cfginfo;
	struct bfa_iocfc_cfg_s  *cfg = &iocfc->cfg;
	int             i;

	bfa_assert(cfg->fwcfg.num_cqs <= BFI_IOC_MAX_CQS);
	bfa_trc(bfa, cfg->fwcfg.num_cqs);

	iocfc->cfgdone = BFA_FALSE;
	bfa_iocfc_reset_queues(bfa);

	/**
	 * initialize IOC configuration info
	 */
	cfg_info->endian_sig = BFI_IOC_ENDIAN_SIG;
	cfg_info->num_cqs = cfg->fwcfg.num_cqs;

	bfa_dma_be_addr_set(cfg_info->cfgrsp_addr, iocfc->cfgrsp_dma.pa);
	bfa_dma_be_addr_set(cfg_info->stats_addr, iocfc->stats_pa);

	/**
	 * dma map REQ and RSP circular queues and shadow pointers
	 */
	for (i = 0; i < cfg->fwcfg.num_cqs; i++) {
		bfa_dma_be_addr_set(cfg_info->req_cq_ba[i],
				       iocfc->req_cq_ba[i].pa);
		bfa_dma_be_addr_set(cfg_info->req_shadow_ci[i],
				       iocfc->req_cq_shadow_ci[i].pa);
		cfg_info->req_cq_elems[i] =
			bfa_os_htons(cfg->drvcfg.num_reqq_elems);

		bfa_dma_be_addr_set(cfg_info->rsp_cq_ba[i],
				       iocfc->rsp_cq_ba[i].pa);
		bfa_dma_be_addr_set(cfg_info->rsp_shadow_pi[i],
				       iocfc->rsp_cq_shadow_pi[i].pa);
		cfg_info->rsp_cq_elems[i] =
			bfa_os_htons(cfg->drvcfg.num_rspq_elems);
	}

	/**
	 * dma map IOC configuration itself
	 */
	bfi_h2i_set(cfg_req.mh, BFI_MC_IOCFC, BFI_IOCFC_H2I_CFG_REQ,
			bfa_lpuid(bfa));
	bfa_dma_be_addr_set(cfg_req.ioc_cfg_dma_addr, iocfc->cfg_info.pa);

	bfa_ioc_mbox_send(&bfa->ioc, &cfg_req,
			sizeof(struct bfi_iocfc_cfg_req_s));
}

static void
bfa_iocfc_init_mem(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		    struct bfa_pcidev_s *pcidev)
{
	struct bfa_iocfc_s	*iocfc = &bfa->iocfc;

	bfa->bfad = bfad;
	iocfc->bfa = bfa;
	iocfc->action = BFA_IOCFC_ACT_NONE;

	bfa_os_assign(iocfc->cfg, *cfg);

	/**
	 * Initialize chip specific handlers.
	 */
	if (bfa_ioc_devid(&bfa->ioc) == BFA_PCI_DEVICE_ID_CT) {
		iocfc->hwif.hw_reginit = bfa_hwct_reginit;
		iocfc->hwif.hw_rspq_ack = bfa_hwct_rspq_ack;
		iocfc->hwif.hw_msix_init = bfa_hwct_msix_init;
		iocfc->hwif.hw_msix_install = bfa_hwct_msix_install;
		iocfc->hwif.hw_msix_uninstall = bfa_hwct_msix_uninstall;
		iocfc->hwif.hw_isr_mode_set = bfa_hwct_isr_mode_set;
		iocfc->hwif.hw_msix_getvecs = bfa_hwct_msix_getvecs;
	} else {
		iocfc->hwif.hw_reginit = bfa_hwcb_reginit;
		iocfc->hwif.hw_rspq_ack = bfa_hwcb_rspq_ack;
		iocfc->hwif.hw_msix_init = bfa_hwcb_msix_init;
		iocfc->hwif.hw_msix_install = bfa_hwcb_msix_install;
		iocfc->hwif.hw_msix_uninstall = bfa_hwcb_msix_uninstall;
		iocfc->hwif.hw_isr_mode_set = bfa_hwcb_isr_mode_set;
		iocfc->hwif.hw_msix_getvecs = bfa_hwcb_msix_getvecs;
	}

	iocfc->hwif.hw_reginit(bfa);
	bfa->msix.nvecs = 0;
}

static void
bfa_iocfc_mem_claim(struct bfa_s *bfa, struct bfa_iocfc_cfg_s *cfg,
		      struct bfa_meminfo_s *meminfo)
{
	u8        *dm_kva;
	u64        dm_pa;
	int             i, per_reqq_sz, per_rspq_sz;
	struct bfa_iocfc_s  *iocfc = &bfa->iocfc;
	int		dbgsz;

	dm_kva = bfa_meminfo_dma_virt(meminfo);
	dm_pa = bfa_meminfo_dma_phys(meminfo);

	/*
	 * First allocate dma memory for IOC.
	 */
	bfa_ioc_mem_claim(&bfa->ioc, dm_kva, dm_pa);
	dm_kva += bfa_ioc_meminfo();
	dm_pa  += bfa_ioc_meminfo();

	/*
	 * Claim DMA-able memory for the request/response queues and for shadow
	 * ci/pi registers
	 */
	per_reqq_sz = BFA_ROUNDUP((cfg->drvcfg.num_reqq_elems * BFI_LMSG_SZ),
							BFA_DMA_ALIGN_SZ);
	per_rspq_sz = BFA_ROUNDUP((cfg->drvcfg.num_rspq_elems * BFI_LMSG_SZ),
							BFA_DMA_ALIGN_SZ);

	for (i = 0; i < cfg->fwcfg.num_cqs; i++) {
		iocfc->req_cq_ba[i].kva = dm_kva;
		iocfc->req_cq_ba[i].pa = dm_pa;
		bfa_os_memset(dm_kva, 0, per_reqq_sz);
		dm_kva += per_reqq_sz;
		dm_pa += per_reqq_sz;

		iocfc->rsp_cq_ba[i].kva = dm_kva;
		iocfc->rsp_cq_ba[i].pa = dm_pa;
		bfa_os_memset(dm_kva, 0, per_rspq_sz);
		dm_kva += per_rspq_sz;
		dm_pa += per_rspq_sz;
	}

	for (i = 0; i < cfg->fwcfg.num_cqs; i++) {
		iocfc->req_cq_shadow_ci[i].kva = dm_kva;
		iocfc->req_cq_shadow_ci[i].pa = dm_pa;
		dm_kva += BFA_CACHELINE_SZ;
		dm_pa += BFA_CACHELINE_SZ;

		iocfc->rsp_cq_shadow_pi[i].kva = dm_kva;
		iocfc->rsp_cq_shadow_pi[i].pa = dm_pa;
		dm_kva += BFA_CACHELINE_SZ;
		dm_pa += BFA_CACHELINE_SZ;
	}

	/*
	 * Claim DMA-able memory for the config info page
	 */
	bfa->iocfc.cfg_info.kva = dm_kva;
	bfa->iocfc.cfg_info.pa = dm_pa;
	bfa->iocfc.cfginfo = (struct bfi_iocfc_cfg_s *) dm_kva;
	dm_kva += BFA_ROUNDUP(sizeof(struct bfi_iocfc_cfg_s), BFA_CACHELINE_SZ);
	dm_pa += BFA_ROUNDUP(sizeof(struct bfi_iocfc_cfg_s), BFA_CACHELINE_SZ);

	/*
	 * Claim DMA-able memory for the config response
	 */
	bfa->iocfc.cfgrsp_dma.kva = dm_kva;
	bfa->iocfc.cfgrsp_dma.pa = dm_pa;
	bfa->iocfc.cfgrsp = (struct bfi_iocfc_cfgrsp_s *) dm_kva;

	dm_kva +=
		BFA_ROUNDUP(sizeof(struct bfi_iocfc_cfgrsp_s),
			    BFA_CACHELINE_SZ);
	dm_pa += BFA_ROUNDUP(sizeof(struct bfi_iocfc_cfgrsp_s),
			     BFA_CACHELINE_SZ);

	/*
	 * Claim DMA-able memory for iocfc stats
	 */
	bfa->iocfc.stats_kva = dm_kva;
	bfa->iocfc.stats_pa = dm_pa;
	bfa->iocfc.fw_stats = (struct bfa_fw_stats_s *) dm_kva;
	dm_kva += BFA_ROUNDUP(sizeof(struct bfa_fw_stats_s), BFA_CACHELINE_SZ);
	dm_pa += BFA_ROUNDUP(sizeof(struct bfa_fw_stats_s), BFA_CACHELINE_SZ);

	bfa_meminfo_dma_virt(meminfo) = dm_kva;
	bfa_meminfo_dma_phys(meminfo) = dm_pa;

	dbgsz = bfa_ioc_debug_trcsz(bfa_auto_recover);
	if (dbgsz > 0) {
		bfa_ioc_debug_memclaim(&bfa->ioc, bfa_meminfo_kva(meminfo));
		bfa_meminfo_kva(meminfo) += dbgsz;
	}
}

/**
 * BFA submodules initialization completion notification.
 */
static void
bfa_iocfc_initdone_submod(struct bfa_s *bfa)
{
	int             i;

	for (i = 0; hal_mods[i]; i++)
		hal_mods[i]->initdone(bfa);
}

/**
 * Start BFA submodules.
 */
static void
bfa_iocfc_start_submod(struct bfa_s *bfa)
{
	int             i;

	bfa->rme_process = BFA_TRUE;

	for (i = 0; hal_mods[i]; i++)
		hal_mods[i]->start(bfa);
}

/**
 * Disable BFA submodules.
 */
static void
bfa_iocfc_disable_submod(struct bfa_s *bfa)
{
	int             i;

	for (i = 0; hal_mods[i]; i++)
		hal_mods[i]->iocdisable(bfa);
}

static void
bfa_iocfc_init_cb(void *bfa_arg, bfa_boolean_t complete)
{
	struct bfa_s	*bfa = bfa_arg;

	if (complete) {
		if (bfa->iocfc.cfgdone)
			bfa_cb_init(bfa->bfad, BFA_STATUS_OK);
		else
			bfa_cb_init(bfa->bfad, BFA_STATUS_FAILED);
	} else
		bfa->iocfc.action = BFA_IOCFC_ACT_NONE;
}

static void
bfa_iocfc_stop_cb(void *bfa_arg, bfa_boolean_t compl)
{
	struct bfa_s  *bfa = bfa_arg;
	struct bfad_s *bfad = bfa->bfad;

	if (compl)
		complete(&bfad->comp);

	else
		bfa->iocfc.action = BFA_IOCFC_ACT_NONE;
}

static void
bfa_iocfc_disable_cb(void *bfa_arg, bfa_boolean_t compl)
{
	struct bfa_s  *bfa = bfa_arg;
	struct bfad_s *bfad = bfa->bfad;

	if (compl)
		complete(&bfad->disable_comp);
}

/**
 * Update BFA configuration from firmware configuration.
 */
static void
bfa_iocfc_cfgrsp(struct bfa_s *bfa)
{
	struct bfa_iocfc_s		*iocfc	 = &bfa->iocfc;
	struct bfi_iocfc_cfgrsp_s	*cfgrsp  = iocfc->cfgrsp;
	struct bfa_iocfc_fwcfg_s	*fwcfg   = &cfgrsp->fwcfg;
	struct bfi_iocfc_cfg_s 		*cfginfo = iocfc->cfginfo;

	fwcfg->num_cqs        = fwcfg->num_cqs;
	fwcfg->num_ioim_reqs  = bfa_os_ntohs(fwcfg->num_ioim_reqs);
	fwcfg->num_tskim_reqs = bfa_os_ntohs(fwcfg->num_tskim_reqs);
	fwcfg->num_fcxp_reqs  = bfa_os_ntohs(fwcfg->num_fcxp_reqs);
	fwcfg->num_uf_bufs    = bfa_os_ntohs(fwcfg->num_uf_bufs);
	fwcfg->num_rports     = bfa_os_ntohs(fwcfg->num_rports);

	cfginfo->intr_attr.coalesce = cfgrsp->intr_attr.coalesce;
	cfginfo->intr_attr.delay    = bfa_os_ntohs(cfgrsp->intr_attr.delay);
	cfginfo->intr_attr.latency  = bfa_os_ntohs(cfgrsp->intr_attr.latency);

	iocfc->cfgdone = BFA_TRUE;

	/**
	 * Configuration is complete - initialize/start submodules
	 */
	if (iocfc->action == BFA_IOCFC_ACT_INIT)
		bfa_cb_queue(bfa, &iocfc->init_hcb_qe, bfa_iocfc_init_cb, bfa);
	else
		bfa_iocfc_start_submod(bfa);
}

static void
bfa_iocfc_stats_clear(void *bfa_arg)
{
	struct bfa_s		*bfa = bfa_arg;
	struct bfa_iocfc_s	*iocfc = &bfa->iocfc;
	struct bfi_iocfc_stats_req_s stats_req;

	bfa_timer_start(bfa, &iocfc->stats_timer,
			    bfa_iocfc_stats_clr_timeout, bfa,
			    BFA_IOCFC_TOV);

	bfi_h2i_set(stats_req.mh, BFI_MC_IOCFC, BFI_IOCFC_H2I_CLEAR_STATS_REQ,
		bfa_lpuid(bfa));
	bfa_ioc_mbox_send(&bfa->ioc, &stats_req,
		sizeof(struct bfi_iocfc_stats_req_s));
}

static void
bfa_iocfc_stats_swap(struct bfa_fw_stats_s *d, struct bfa_fw_stats_s *s)
{
	u32       *dip = (u32 *) d;
	u32       *sip = (u32 *) s;
	int             i;

	for (i = 0; i < (sizeof(struct bfa_fw_stats_s) / sizeof(u32)); i++)
		dip[i] = bfa_os_ntohl(sip[i]);
}

static void
bfa_iocfc_stats_clr_cb(void *bfa_arg, bfa_boolean_t complete)
{
	struct bfa_s *bfa = bfa_arg;
	struct bfa_iocfc_s *iocfc = &bfa->iocfc;

	if (complete) {
		bfa_ioc_clr_stats(&bfa->ioc);
		iocfc->stats_cbfn(iocfc->stats_cbarg, iocfc->stats_status);
	} else {
		iocfc->stats_busy = BFA_FALSE;
		iocfc->stats_status = BFA_STATUS_OK;
	}
}

static void
bfa_iocfc_stats_clr_timeout(void *bfa_arg)
{
	struct bfa_s		*bfa = bfa_arg;
	struct bfa_iocfc_s	*iocfc = &bfa->iocfc;

	bfa_trc(bfa, 0);

	iocfc->stats_status = BFA_STATUS_ETIMER;
	bfa_cb_queue(bfa, &iocfc->stats_hcb_qe, bfa_iocfc_stats_clr_cb, bfa);
}

static void
bfa_iocfc_stats_cb(void *bfa_arg, bfa_boolean_t complete)
{
	struct bfa_s		*bfa = bfa_arg;
	struct bfa_iocfc_s	*iocfc = &bfa->iocfc;

	if (complete) {
		if (iocfc->stats_status == BFA_STATUS_OK) {
			bfa_os_memset(iocfc->stats_ret, 0,
				sizeof(*iocfc->stats_ret));
			bfa_iocfc_stats_swap(&iocfc->stats_ret->fw_stats,
				iocfc->fw_stats);
		}
		iocfc->stats_cbfn(iocfc->stats_cbarg, iocfc->stats_status);
	} else {
		iocfc->stats_busy = BFA_FALSE;
		iocfc->stats_status = BFA_STATUS_OK;
	}
}

static void
bfa_iocfc_stats_timeout(void *bfa_arg)
{
	struct bfa_s		*bfa = bfa_arg;
	struct bfa_iocfc_s	*iocfc = &bfa->iocfc;

	bfa_trc(bfa, 0);

	iocfc->stats_status = BFA_STATUS_ETIMER;
	bfa_cb_queue(bfa, &iocfc->stats_hcb_qe, bfa_iocfc_stats_cb, bfa);
}

static void
bfa_iocfc_stats_query(struct bfa_s *bfa)
{
	struct bfa_iocfc_s	*iocfc = &bfa->iocfc;
	struct bfi_iocfc_stats_req_s stats_req;

	bfa_timer_start(bfa, &iocfc->stats_timer,
			    bfa_iocfc_stats_timeout, bfa, BFA_IOCFC_TOV);

	bfi_h2i_set(stats_req.mh, BFI_MC_IOCFC, BFI_IOCFC_H2I_GET_STATS_REQ,
			bfa_lpuid(bfa));
	bfa_ioc_mbox_send(&bfa->ioc, &stats_req,
		sizeof(struct bfi_iocfc_stats_req_s));
}

void
bfa_iocfc_reset_queues(struct bfa_s *bfa)
{
	int             q;

	for (q = 0; q < BFI_IOC_MAX_CQS; q++) {
		bfa_reqq_ci(bfa, q) = 0;
		bfa_reqq_pi(bfa, q) = 0;
		bfa_rspq_ci(bfa, q) = 0;
		bfa_rspq_pi(bfa, q) = 0;
	}
}

/**
 * IOC enable request is complete
 */
static void
bfa_iocfc_enable_cbfn(void *bfa_arg, enum bfa_status status)
{
	struct bfa_s	*bfa = bfa_arg;

	if (status != BFA_STATUS_OK) {
		bfa_isr_disable(bfa);
		if (bfa->iocfc.action == BFA_IOCFC_ACT_INIT)
			bfa_cb_queue(bfa, &bfa->iocfc.init_hcb_qe,
				     bfa_iocfc_init_cb, bfa);
		return;
	}

	bfa_iocfc_initdone_submod(bfa);
	bfa_iocfc_send_cfg(bfa);
}

/**
 * IOC disable request is complete
 */
static void
bfa_iocfc_disable_cbfn(void *bfa_arg)
{
	struct bfa_s	*bfa = bfa_arg;

	bfa_isr_disable(bfa);
	bfa_iocfc_disable_submod(bfa);

	if (bfa->iocfc.action == BFA_IOCFC_ACT_STOP)
		bfa_cb_queue(bfa, &bfa->iocfc.stop_hcb_qe, bfa_iocfc_stop_cb,
			     bfa);
	else {
		bfa_assert(bfa->iocfc.action == BFA_IOCFC_ACT_DISABLE);
		bfa_cb_queue(bfa, &bfa->iocfc.dis_hcb_qe, bfa_iocfc_disable_cb,
			     bfa);
	}
}

/**
 * Notify sub-modules of hardware failure.
 */
static void
bfa_iocfc_hbfail_cbfn(void *bfa_arg)
{
	struct bfa_s	*bfa = bfa_arg;

	bfa->rme_process = BFA_FALSE;

	bfa_isr_disable(bfa);
	bfa_iocfc_disable_submod(bfa);

	if (bfa->iocfc.action == BFA_IOCFC_ACT_INIT)
		bfa_cb_queue(bfa, &bfa->iocfc.init_hcb_qe, bfa_iocfc_init_cb,
			     bfa);
}

/**
 * Actions on chip-reset completion.
 */
static void
bfa_iocfc_reset_cbfn(void *bfa_arg)
{
	struct bfa_s	*bfa = bfa_arg;

	bfa_iocfc_reset_queues(bfa);
	bfa_isr_enable(bfa);
}



/**
 *  bfa_ioc_public
 */

/**
 * Query IOC memory requirement information.
 */
void
bfa_iocfc_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *km_len,
		u32 *dm_len)
{
	/* dma memory for IOC */
	*dm_len += bfa_ioc_meminfo();

	bfa_iocfc_fw_cfg_sz(cfg, dm_len);
	bfa_iocfc_cqs_sz(cfg, dm_len);
	*km_len += bfa_ioc_debug_trcsz(bfa_auto_recover);
}

/**
 * Query IOC memory requirement information.
 */
void
bfa_iocfc_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		   struct bfa_meminfo_s *meminfo, struct bfa_pcidev_s *pcidev)
{
	int             i;

	bfa_iocfc_cbfn.enable_cbfn = bfa_iocfc_enable_cbfn;
	bfa_iocfc_cbfn.disable_cbfn = bfa_iocfc_disable_cbfn;
	bfa_iocfc_cbfn.hbfail_cbfn = bfa_iocfc_hbfail_cbfn;
	bfa_iocfc_cbfn.reset_cbfn = bfa_iocfc_reset_cbfn;

	bfa_ioc_attach(&bfa->ioc, bfa, &bfa_iocfc_cbfn, &bfa->timer_mod,
		bfa->trcmod, bfa->aen, bfa->logm);
	bfa_ioc_pci_init(&bfa->ioc, pcidev, BFI_MC_IOCFC);
	bfa_ioc_mbox_register(&bfa->ioc, bfa_mbox_isrs);

	/**
	 * Choose FC (ssid: 0x1C) v/s FCoE (ssid: 0x14) mode.
	 */
	if (0)
		bfa_ioc_set_fcmode(&bfa->ioc);

	bfa_iocfc_init_mem(bfa, bfad, cfg, pcidev);
	bfa_iocfc_mem_claim(bfa, cfg, meminfo);
	bfa_timer_init(&bfa->timer_mod);

	INIT_LIST_HEAD(&bfa->comp_q);
	for (i = 0; i < BFI_IOC_MAX_CQS; i++)
		INIT_LIST_HEAD(&bfa->reqq_waitq[i]);
}

/**
 * Query IOC memory requirement information.
 */
void
bfa_iocfc_detach(struct bfa_s *bfa)
{
	bfa_ioc_detach(&bfa->ioc);
}

/**
 * Query IOC memory requirement information.
 */
void
bfa_iocfc_init(struct bfa_s *bfa)
{
	bfa->iocfc.action = BFA_IOCFC_ACT_INIT;
	bfa_ioc_enable(&bfa->ioc);
	bfa_msix_install(bfa);
}

/**
 * IOC start called from bfa_start(). Called to start IOC operations
 * at driver instantiation for this instance.
 */
void
bfa_iocfc_start(struct bfa_s *bfa)
{
	if (bfa->iocfc.cfgdone)
		bfa_iocfc_start_submod(bfa);
}

/**
 * IOC stop called from bfa_stop(). Called only when driver is unloaded
 * for this instance.
 */
void
bfa_iocfc_stop(struct bfa_s *bfa)
{
	bfa->iocfc.action = BFA_IOCFC_ACT_STOP;

	bfa->rme_process = BFA_FALSE;
	bfa_ioc_disable(&bfa->ioc);
}

void
bfa_iocfc_isr(void *bfaarg, struct bfi_mbmsg_s *m)
{
	struct bfa_s		*bfa = bfaarg;
	struct bfa_iocfc_s	*iocfc = &bfa->iocfc;
	union bfi_iocfc_i2h_msg_u	*msg;

	msg = (union bfi_iocfc_i2h_msg_u *) m;
	bfa_trc(bfa, msg->mh.msg_id);

	switch (msg->mh.msg_id) {
	case BFI_IOCFC_I2H_CFG_REPLY:
		iocfc->cfg_reply = &msg->cfg_reply;
		bfa_iocfc_cfgrsp(bfa);
		break;

	case BFI_IOCFC_I2H_GET_STATS_RSP:
		if (iocfc->stats_busy == BFA_FALSE
		    || iocfc->stats_status == BFA_STATUS_ETIMER)
			break;

		bfa_timer_stop(&iocfc->stats_timer);
		iocfc->stats_status = BFA_STATUS_OK;
		bfa_cb_queue(bfa, &iocfc->stats_hcb_qe, bfa_iocfc_stats_cb,
			      bfa);
		break;
	case BFI_IOCFC_I2H_CLEAR_STATS_RSP:
		/*
		 * check for timer pop before processing the rsp
		 */
		if (iocfc->stats_busy == BFA_FALSE
		    || iocfc->stats_status == BFA_STATUS_ETIMER)
			break;

		bfa_timer_stop(&iocfc->stats_timer);
		iocfc->stats_status = BFA_STATUS_OK;
		bfa_cb_queue(bfa, &iocfc->stats_hcb_qe,
			      bfa_iocfc_stats_clr_cb, bfa);
		break;
	case BFI_IOCFC_I2H_UPDATEQ_RSP:
		iocfc->updateq_cbfn(iocfc->updateq_cbarg, BFA_STATUS_OK);
		break;
	default:
		bfa_assert(0);
	}
}

#ifndef BFA_BIOS_BUILD
void
bfa_adapter_get_attr(struct bfa_s *bfa, struct bfa_adapter_attr_s *ad_attr)
{
	bfa_ioc_get_adapter_attr(&bfa->ioc, ad_attr);
}

u64
bfa_adapter_get_id(struct bfa_s *bfa)
{
	return bfa_ioc_get_adid(&bfa->ioc);
}

void
bfa_iocfc_get_attr(struct bfa_s *bfa, struct bfa_iocfc_attr_s *attr)
{
	struct bfa_iocfc_s	*iocfc = &bfa->iocfc;

	attr->intr_attr = iocfc->cfginfo->intr_attr;
	attr->config	= iocfc->cfg;
}

bfa_status_t
bfa_iocfc_israttr_set(struct bfa_s *bfa, struct bfa_iocfc_intr_attr_s *attr)
{
	struct bfa_iocfc_s		*iocfc = &bfa->iocfc;
	struct bfi_iocfc_set_intr_req_s *m;

	iocfc->cfginfo->intr_attr = *attr;
	if (!bfa_iocfc_is_operational(bfa))
		return BFA_STATUS_OK;

	m = bfa_reqq_next(bfa, BFA_REQQ_IOC);
	if (!m)
		return BFA_STATUS_DEVBUSY;

	bfi_h2i_set(m->mh, BFI_MC_IOCFC, BFI_IOCFC_H2I_SET_INTR_REQ,
			bfa_lpuid(bfa));
	m->coalesce = attr->coalesce;
	m->delay    = bfa_os_htons(attr->delay);
	m->latency  = bfa_os_htons(attr->latency);

	bfa_trc(bfa, attr->delay);
	bfa_trc(bfa, attr->latency);

	bfa_reqq_produce(bfa, BFA_REQQ_IOC);
	return BFA_STATUS_OK;
}

void
bfa_iocfc_set_snsbase(struct bfa_s *bfa, u64 snsbase_pa)
{
	struct bfa_iocfc_s      *iocfc = &bfa->iocfc;

	iocfc->cfginfo->sense_buf_len = (BFI_IOIM_SNSLEN - 1);
	bfa_dma_be_addr_set(iocfc->cfginfo->ioim_snsbase, snsbase_pa);
}

bfa_status_t
bfa_iocfc_get_stats(struct bfa_s *bfa, struct bfa_iocfc_stats_s *stats,
		      bfa_cb_ioc_t cbfn, void *cbarg)
{
	struct bfa_iocfc_s	*iocfc = &bfa->iocfc;

	if (iocfc->stats_busy) {
		bfa_trc(bfa, iocfc->stats_busy);
		return BFA_STATUS_DEVBUSY;
	}

	iocfc->stats_busy = BFA_TRUE;
	iocfc->stats_ret = stats;
	iocfc->stats_cbfn = cbfn;
	iocfc->stats_cbarg = cbarg;

	bfa_iocfc_stats_query(bfa);

	return BFA_STATUS_OK;
}

bfa_status_t
bfa_iocfc_clear_stats(struct bfa_s *bfa, bfa_cb_ioc_t cbfn, void *cbarg)
{
	struct bfa_iocfc_s	*iocfc = &bfa->iocfc;

	if (iocfc->stats_busy) {
		bfa_trc(bfa, iocfc->stats_busy);
		return BFA_STATUS_DEVBUSY;
	}

	iocfc->stats_busy = BFA_TRUE;
	iocfc->stats_cbfn = cbfn;
	iocfc->stats_cbarg = cbarg;

	bfa_iocfc_stats_clear(bfa);
	return BFA_STATUS_OK;
}

/**
 * Enable IOC after it is disabled.
 */
void
bfa_iocfc_enable(struct bfa_s *bfa)
{
	bfa_plog_str(bfa->plog, BFA_PL_MID_HAL, BFA_PL_EID_MISC, 0,
		     "IOC Enable");
	bfa_ioc_enable(&bfa->ioc);
}

void
bfa_iocfc_disable(struct bfa_s *bfa)
{
	bfa_plog_str(bfa->plog, BFA_PL_MID_HAL, BFA_PL_EID_MISC, 0,
		     "IOC Disable");
	bfa->iocfc.action = BFA_IOCFC_ACT_DISABLE;

	bfa->rme_process = BFA_FALSE;
	bfa_ioc_disable(&bfa->ioc);
}


bfa_boolean_t
bfa_iocfc_is_operational(struct bfa_s *bfa)
{
	return bfa_ioc_is_operational(&bfa->ioc) && bfa->iocfc.cfgdone;
}

/**
 * Return boot target port wwns -- read from boot information in flash.
 */
void
bfa_iocfc_get_bootwwns(struct bfa_s *bfa, u8 *nwwns, wwn_t **wwns)
{
	struct bfa_iocfc_s		*iocfc	 = &bfa->iocfc;
	struct bfi_iocfc_cfgrsp_s	*cfgrsp  = iocfc->cfgrsp;

	*nwwns = cfgrsp->bootwwns.nwwns;
	*wwns = cfgrsp->bootwwns.wwn;
}

#endif


