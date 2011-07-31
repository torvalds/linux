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

BFA_TRC_FILE(HAL, SGPG);
BFA_MODULE(sgpg);

/**
 *  bfa_sgpg_mod BFA SGPG Mode module
 */

/**
 * Compute and return memory needed by FCP(im) module.
 */
static void
bfa_sgpg_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *km_len,
		u32 *dm_len)
{
	if (cfg->drvcfg.num_sgpgs < BFA_SGPG_MIN)
		cfg->drvcfg.num_sgpgs = BFA_SGPG_MIN;

	*km_len += (cfg->drvcfg.num_sgpgs + 1) * sizeof(struct bfa_sgpg_s);
	*dm_len += (cfg->drvcfg.num_sgpgs + 1) * sizeof(struct bfi_sgpg_s);
}


static void
bfa_sgpg_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		    struct bfa_meminfo_s *minfo, struct bfa_pcidev_s *pcidev)
{
	struct bfa_sgpg_mod_s	*mod = BFA_SGPG_MOD(bfa);
	int				i;
	struct bfa_sgpg_s		*hsgpg;
	struct bfi_sgpg_s 	*sgpg;
	u64		align_len;

	union {
		u64        pa;
		union bfi_addr_u      addr;
	} sgpg_pa;

	INIT_LIST_HEAD(&mod->sgpg_q);
	INIT_LIST_HEAD(&mod->sgpg_wait_q);

	bfa_trc(bfa, cfg->drvcfg.num_sgpgs);

	mod->num_sgpgs = cfg->drvcfg.num_sgpgs;
	mod->sgpg_arr_pa = bfa_meminfo_dma_phys(minfo);
	align_len = (BFA_SGPG_ROUNDUP(mod->sgpg_arr_pa) - mod->sgpg_arr_pa);
	mod->sgpg_arr_pa += align_len;
	mod->hsgpg_arr = (struct bfa_sgpg_s *) (bfa_meminfo_kva(minfo) +
						align_len);
	mod->sgpg_arr = (struct bfi_sgpg_s *) (bfa_meminfo_dma_virt(minfo) +
						align_len);

	hsgpg = mod->hsgpg_arr;
	sgpg = mod->sgpg_arr;
	sgpg_pa.pa = mod->sgpg_arr_pa;
	mod->free_sgpgs = mod->num_sgpgs;

	bfa_assert(!(sgpg_pa.pa & (sizeof(struct bfi_sgpg_s) - 1)));

	for (i = 0; i < mod->num_sgpgs; i++) {
		bfa_os_memset(hsgpg, 0, sizeof(*hsgpg));
		bfa_os_memset(sgpg, 0, sizeof(*sgpg));

		hsgpg->sgpg = sgpg;
		hsgpg->sgpg_pa = sgpg_pa.addr;
		list_add_tail(&hsgpg->qe, &mod->sgpg_q);

		hsgpg++;
		sgpg++;
		sgpg_pa.pa += sizeof(struct bfi_sgpg_s);
	}

	bfa_meminfo_kva(minfo) = (u8 *) hsgpg;
	bfa_meminfo_dma_virt(minfo) = (u8 *) sgpg;
	bfa_meminfo_dma_phys(minfo) = sgpg_pa.pa;
}

static void
bfa_sgpg_detach(struct bfa_s *bfa)
{
}

static void
bfa_sgpg_start(struct bfa_s *bfa)
{
}

static void
bfa_sgpg_stop(struct bfa_s *bfa)
{
}

static void
bfa_sgpg_iocdisable(struct bfa_s *bfa)
{
}



/**
 *  bfa_sgpg_public BFA SGPG public functions
 */

bfa_status_t
bfa_sgpg_malloc(struct bfa_s *bfa, struct list_head *sgpg_q, int nsgpgs)
{
	struct bfa_sgpg_mod_s *mod = BFA_SGPG_MOD(bfa);
	struct bfa_sgpg_s *hsgpg;
	int             i;

	bfa_trc_fp(bfa, nsgpgs);

	if (mod->free_sgpgs < nsgpgs)
		return BFA_STATUS_ENOMEM;

	for (i = 0; i < nsgpgs; i++) {
		bfa_q_deq(&mod->sgpg_q, &hsgpg);
		bfa_assert(hsgpg);
		list_add_tail(&hsgpg->qe, sgpg_q);
	}

	mod->free_sgpgs -= nsgpgs;
	return BFA_STATUS_OK;
}

void
bfa_sgpg_mfree(struct bfa_s *bfa, struct list_head *sgpg_q, int nsgpg)
{
	struct bfa_sgpg_mod_s *mod = BFA_SGPG_MOD(bfa);
	struct bfa_sgpg_wqe_s *wqe;

	bfa_trc_fp(bfa, nsgpg);

	mod->free_sgpgs += nsgpg;
	bfa_assert(mod->free_sgpgs <= mod->num_sgpgs);

	list_splice_tail_init(sgpg_q, &mod->sgpg_q);

	if (list_empty(&mod->sgpg_wait_q))
		return;

	/**
	 * satisfy as many waiting requests as possible
	 */
	do {
		wqe = bfa_q_first(&mod->sgpg_wait_q);
		if (mod->free_sgpgs < wqe->nsgpg)
			nsgpg = mod->free_sgpgs;
		else
			nsgpg = wqe->nsgpg;
		bfa_sgpg_malloc(bfa, &wqe->sgpg_q, nsgpg);
		wqe->nsgpg -= nsgpg;
		if (wqe->nsgpg == 0) {
			list_del(&wqe->qe);
			wqe->cbfn(wqe->cbarg);
		}
	} while (mod->free_sgpgs && !list_empty(&mod->sgpg_wait_q));
}

void
bfa_sgpg_wait(struct bfa_s *bfa, struct bfa_sgpg_wqe_s *wqe, int nsgpg)
{
	struct bfa_sgpg_mod_s *mod = BFA_SGPG_MOD(bfa);

	bfa_assert(nsgpg > 0);
	bfa_assert(nsgpg > mod->free_sgpgs);

	wqe->nsgpg_total = wqe->nsgpg = nsgpg;

	/**
	 * allocate any left to this one first
	 */
	if (mod->free_sgpgs) {
		/**
		 * no one else is waiting for SGPG
		 */
		bfa_assert(list_empty(&mod->sgpg_wait_q));
		list_splice_tail_init(&mod->sgpg_q, &wqe->sgpg_q);
		wqe->nsgpg -= mod->free_sgpgs;
		mod->free_sgpgs = 0;
	}

	list_add_tail(&wqe->qe, &mod->sgpg_wait_q);
}

void
bfa_sgpg_wcancel(struct bfa_s *bfa, struct bfa_sgpg_wqe_s *wqe)
{
	struct bfa_sgpg_mod_s *mod = BFA_SGPG_MOD(bfa);

	bfa_assert(bfa_q_is_on_q(&mod->sgpg_wait_q, wqe));
	list_del(&wqe->qe);

	if (wqe->nsgpg_total != wqe->nsgpg)
		bfa_sgpg_mfree(bfa, &wqe->sgpg_q,
				   wqe->nsgpg_total - wqe->nsgpg);
}

void
bfa_sgpg_winit(struct bfa_sgpg_wqe_s *wqe, void (*cbfn) (void *cbarg),
		   void *cbarg)
{
	INIT_LIST_HEAD(&wqe->sgpg_q);
	wqe->cbfn = cbfn;
	wqe->cbarg = cbarg;
}


