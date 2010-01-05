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

#include <bfa.h>
#include <log/bfa_log_hal.h>

BFA_TRC_FILE(HAL, FCPIM);
BFA_MODULE(fcpim);

/**
 *  hal_fcpim_mod BFA FCP Initiator Mode module
 */

/**
 * 		Compute and return memory needed by FCP(im) module.
 */
static void
bfa_fcpim_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *km_len,
		u32 *dm_len)
{
	bfa_itnim_meminfo(cfg, km_len, dm_len);

	/**
	 * IO memory
	 */
	if (cfg->fwcfg.num_ioim_reqs < BFA_IOIM_MIN)
		cfg->fwcfg.num_ioim_reqs = BFA_IOIM_MIN;
	else if (cfg->fwcfg.num_ioim_reqs > BFA_IOIM_MAX)
		cfg->fwcfg.num_ioim_reqs = BFA_IOIM_MAX;

	*km_len += cfg->fwcfg.num_ioim_reqs *
	  (sizeof(struct bfa_ioim_s) + sizeof(struct bfa_ioim_sp_s));

	*dm_len += cfg->fwcfg.num_ioim_reqs * BFI_IOIM_SNSLEN;

	/**
	 * task management command memory
	 */
	if (cfg->fwcfg.num_tskim_reqs < BFA_TSKIM_MIN)
		cfg->fwcfg.num_tskim_reqs = BFA_TSKIM_MIN;
	*km_len += cfg->fwcfg.num_tskim_reqs * sizeof(struct bfa_tskim_s);
}


static void
bfa_fcpim_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		     struct bfa_meminfo_s *meminfo, struct bfa_pcidev_s *pcidev)
{
	struct bfa_fcpim_mod_s *fcpim = BFA_FCPIM_MOD(bfa);

	bfa_trc(bfa, cfg->drvcfg.path_tov);
	bfa_trc(bfa, cfg->fwcfg.num_rports);
	bfa_trc(bfa, cfg->fwcfg.num_ioim_reqs);
	bfa_trc(bfa, cfg->fwcfg.num_tskim_reqs);

	fcpim->bfa            = bfa;
	fcpim->num_itnims     = cfg->fwcfg.num_rports;
	fcpim->num_ioim_reqs  = cfg->fwcfg.num_ioim_reqs;
	fcpim->num_tskim_reqs = cfg->fwcfg.num_tskim_reqs;
	fcpim->path_tov       = cfg->drvcfg.path_tov;
	fcpim->delay_comp	  = cfg->drvcfg.delay_comp;

	bfa_itnim_attach(fcpim, meminfo);
	bfa_tskim_attach(fcpim, meminfo);
	bfa_ioim_attach(fcpim, meminfo);
}

static void
bfa_fcpim_initdone(struct bfa_s *bfa)
{
}

static void
bfa_fcpim_detach(struct bfa_s *bfa)
{
	struct bfa_fcpim_mod_s *fcpim = BFA_FCPIM_MOD(bfa);

	bfa_ioim_detach(fcpim);
	bfa_tskim_detach(fcpim);
}

static void
bfa_fcpim_start(struct bfa_s *bfa)
{
}

static void
bfa_fcpim_stop(struct bfa_s *bfa)
{
}

static void
bfa_fcpim_iocdisable(struct bfa_s *bfa)
{
	struct bfa_fcpim_mod_s *fcpim = BFA_FCPIM_MOD(bfa);
	struct bfa_itnim_s *itnim;
	struct list_head        *qe, *qen;

	list_for_each_safe(qe, qen, &fcpim->itnim_q) {
		itnim = (struct bfa_itnim_s *) qe;
		bfa_itnim_iocdisable(itnim);
	}
}

void
bfa_fcpim_path_tov_set(struct bfa_s *bfa, u16 path_tov)
{
	struct bfa_fcpim_mod_s *fcpim = BFA_FCPIM_MOD(bfa);

	fcpim->path_tov = path_tov * 1000;
	if (fcpim->path_tov > BFA_FCPIM_PATHTOV_MAX)
		fcpim->path_tov = BFA_FCPIM_PATHTOV_MAX;
}

u16
bfa_fcpim_path_tov_get(struct bfa_s *bfa)
{
	struct bfa_fcpim_mod_s *fcpim = BFA_FCPIM_MOD(bfa);

	return fcpim->path_tov / 1000;
}

bfa_status_t
bfa_fcpim_get_modstats(struct bfa_s *bfa, struct bfa_fcpim_stats_s *modstats)
{
	struct bfa_fcpim_mod_s *fcpim = BFA_FCPIM_MOD(bfa);

	*modstats = fcpim->stats;

	return BFA_STATUS_OK;
}

bfa_status_t
bfa_fcpim_clr_modstats(struct bfa_s *bfa)
{
	struct bfa_fcpim_mod_s *fcpim = BFA_FCPIM_MOD(bfa);

	memset(&fcpim->stats, 0, sizeof(struct bfa_fcpim_stats_s));

	return BFA_STATUS_OK;
}

void
bfa_fcpim_qdepth_set(struct bfa_s *bfa, u16 q_depth)
{
	struct bfa_fcpim_mod_s *fcpim = BFA_FCPIM_MOD(bfa);

	bfa_assert(q_depth <= BFA_IOCFC_QDEPTH_MAX);

	fcpim->q_depth = q_depth;
}

u16
bfa_fcpim_qdepth_get(struct bfa_s *bfa)
{
	struct bfa_fcpim_mod_s *fcpim = BFA_FCPIM_MOD(bfa);

	return fcpim->q_depth;
}


