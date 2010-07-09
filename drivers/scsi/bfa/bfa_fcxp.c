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
#include <bfi/bfi_uf.h>
#include <cs/bfa_debug.h>

BFA_TRC_FILE(HAL, FCXP);
BFA_MODULE(fcxp);

/**
 * forward declarations
 */
static void     __bfa_fcxp_send_cbfn(void *cbarg, bfa_boolean_t complete);
static void     hal_fcxp_rx_plog(struct bfa_s *bfa, struct bfa_fcxp_s *fcxp,
				 struct bfi_fcxp_send_rsp_s *fcxp_rsp);
static void     hal_fcxp_tx_plog(struct bfa_s *bfa, u32 reqlen,
				 struct bfa_fcxp_s *fcxp, struct fchs_s *fchs);
static void	bfa_fcxp_qresume(void *cbarg);
static void	bfa_fcxp_queue(struct bfa_fcxp_s *fcxp,
			       struct bfi_fcxp_send_req_s *send_req);

/**
 *  fcxp_pvt BFA FCXP private functions
 */

static void
claim_fcxp_req_rsp_mem(struct bfa_fcxp_mod_s *mod, struct bfa_meminfo_s *mi)
{
	u8        *dm_kva = NULL;
	u64        dm_pa;
	u32        buf_pool_sz;

	dm_kva = bfa_meminfo_dma_virt(mi);
	dm_pa = bfa_meminfo_dma_phys(mi);

	buf_pool_sz = mod->req_pld_sz * mod->num_fcxps;

	/*
	 * Initialize the fcxp req payload list
	 */
	mod->req_pld_list_kva = dm_kva;
	mod->req_pld_list_pa = dm_pa;
	dm_kva += buf_pool_sz;
	dm_pa += buf_pool_sz;
	bfa_os_memset(mod->req_pld_list_kva, 0, buf_pool_sz);

	/*
	 * Initialize the fcxp rsp payload list
	 */
	buf_pool_sz = mod->rsp_pld_sz * mod->num_fcxps;
	mod->rsp_pld_list_kva = dm_kva;
	mod->rsp_pld_list_pa = dm_pa;
	dm_kva += buf_pool_sz;
	dm_pa += buf_pool_sz;
	bfa_os_memset(mod->rsp_pld_list_kva, 0, buf_pool_sz);

	bfa_meminfo_dma_virt(mi) = dm_kva;
	bfa_meminfo_dma_phys(mi) = dm_pa;
}

static void
claim_fcxps_mem(struct bfa_fcxp_mod_s *mod, struct bfa_meminfo_s *mi)
{
	u16        i;
	struct bfa_fcxp_s *fcxp;

	fcxp = (struct bfa_fcxp_s *) bfa_meminfo_kva(mi);
	bfa_os_memset(fcxp, 0, sizeof(struct bfa_fcxp_s) * mod->num_fcxps);

	INIT_LIST_HEAD(&mod->fcxp_free_q);
	INIT_LIST_HEAD(&mod->fcxp_active_q);

	mod->fcxp_list = fcxp;

	for (i = 0; i < mod->num_fcxps; i++) {
		fcxp->fcxp_mod = mod;
		fcxp->fcxp_tag = i;

		list_add_tail(&fcxp->qe, &mod->fcxp_free_q);
		bfa_reqq_winit(&fcxp->reqq_wqe, bfa_fcxp_qresume, fcxp);
		fcxp->reqq_waiting = BFA_FALSE;

		fcxp = fcxp + 1;
	}

	bfa_meminfo_kva(mi) = (void *)fcxp;
}

static void
bfa_fcxp_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *ndm_len,
		u32 *dm_len)
{
	u16        num_fcxp_reqs = cfg->fwcfg.num_fcxp_reqs;

	if (num_fcxp_reqs == 0)
		return;

	/*
	 * Account for req/rsp payload
	 */
	*dm_len += BFA_FCXP_MAX_IBUF_SZ * num_fcxp_reqs;
	if (cfg->drvcfg.min_cfg)
		*dm_len += BFA_FCXP_MAX_IBUF_SZ * num_fcxp_reqs;
	else
		*dm_len += BFA_FCXP_MAX_LBUF_SZ * num_fcxp_reqs;

	/*
	 * Account for fcxp structs
	 */
	*ndm_len += sizeof(struct bfa_fcxp_s) * num_fcxp_reqs;
}

static void
bfa_fcxp_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		    struct bfa_meminfo_s *meminfo, struct bfa_pcidev_s *pcidev)
{
	struct bfa_fcxp_mod_s *mod = BFA_FCXP_MOD(bfa);

	bfa_os_memset(mod, 0, sizeof(struct bfa_fcxp_mod_s));
	mod->bfa = bfa;
	mod->num_fcxps = cfg->fwcfg.num_fcxp_reqs;

	/**
	 * Initialize FCXP request and response payload sizes.
	 */
	mod->req_pld_sz = mod->rsp_pld_sz = BFA_FCXP_MAX_IBUF_SZ;
	if (!cfg->drvcfg.min_cfg)
		mod->rsp_pld_sz = BFA_FCXP_MAX_LBUF_SZ;

	INIT_LIST_HEAD(&mod->wait_q);

	claim_fcxp_req_rsp_mem(mod, meminfo);
	claim_fcxps_mem(mod, meminfo);
}

static void
bfa_fcxp_detach(struct bfa_s *bfa)
{
}

static void
bfa_fcxp_start(struct bfa_s *bfa)
{
}

static void
bfa_fcxp_stop(struct bfa_s *bfa)
{
}

static void
bfa_fcxp_iocdisable(struct bfa_s *bfa)
{
	struct bfa_fcxp_mod_s *mod = BFA_FCXP_MOD(bfa);
	struct bfa_fcxp_s *fcxp;
	struct list_head        *qe, *qen;

	list_for_each_safe(qe, qen, &mod->fcxp_active_q) {
		fcxp = (struct bfa_fcxp_s *) qe;
		if (fcxp->caller == NULL) {
			fcxp->send_cbfn(fcxp->caller, fcxp, fcxp->send_cbarg,
					BFA_STATUS_IOC_FAILURE, 0, 0, NULL);
			bfa_fcxp_free(fcxp);
		} else {
			fcxp->rsp_status = BFA_STATUS_IOC_FAILURE;
			bfa_cb_queue(bfa, &fcxp->hcb_qe,
				      __bfa_fcxp_send_cbfn, fcxp);
		}
	}
}

static struct bfa_fcxp_s *
bfa_fcxp_get(struct bfa_fcxp_mod_s *fm)
{
	struct bfa_fcxp_s *fcxp;

	bfa_q_deq(&fm->fcxp_free_q, &fcxp);

	if (fcxp)
		list_add_tail(&fcxp->qe, &fm->fcxp_active_q);

	return fcxp;
}

static void
bfa_fcxp_put(struct bfa_fcxp_s *fcxp)
{
	struct bfa_fcxp_mod_s *mod = fcxp->fcxp_mod;
	struct bfa_fcxp_wqe_s *wqe;

	bfa_q_deq(&mod->wait_q, &wqe);
	if (wqe) {
		bfa_trc(mod->bfa, fcxp->fcxp_tag);
		wqe->alloc_cbfn(wqe->alloc_cbarg, fcxp);
		return;
	}

	bfa_assert(bfa_q_is_on_q(&mod->fcxp_active_q, fcxp));
	list_del(&fcxp->qe);
	list_add_tail(&fcxp->qe, &mod->fcxp_free_q);
}

static void
bfa_fcxp_null_comp(void *bfad_fcxp, struct bfa_fcxp_s *fcxp, void *cbarg,
		       bfa_status_t req_status, u32 rsp_len,
		       u32 resid_len, struct fchs_s *rsp_fchs)
{
	/* discarded fcxp completion */
}

static void
__bfa_fcxp_send_cbfn(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_fcxp_s *fcxp = cbarg;

	if (complete) {
		fcxp->send_cbfn(fcxp->caller, fcxp, fcxp->send_cbarg,
				fcxp->rsp_status, fcxp->rsp_len,
				fcxp->residue_len, &fcxp->rsp_fchs);
	} else {
		bfa_fcxp_free(fcxp);
	}
}

static void
hal_fcxp_send_comp(struct bfa_s *bfa, struct bfi_fcxp_send_rsp_s *fcxp_rsp)
{
	struct bfa_fcxp_mod_s	*mod = BFA_FCXP_MOD(bfa);
	struct bfa_fcxp_s 	*fcxp;
	u16		fcxp_tag = bfa_os_ntohs(fcxp_rsp->fcxp_tag);

	bfa_trc(bfa, fcxp_tag);

	fcxp_rsp->rsp_len = bfa_os_ntohl(fcxp_rsp->rsp_len);

	/**
	 * @todo f/w should not set residue to non-0 when everything
	 *       is received.
	 */
	if (fcxp_rsp->req_status == BFA_STATUS_OK)
		fcxp_rsp->residue_len = 0;
	else
		fcxp_rsp->residue_len = bfa_os_ntohl(fcxp_rsp->residue_len);

	fcxp = BFA_FCXP_FROM_TAG(mod, fcxp_tag);

	bfa_assert(fcxp->send_cbfn != NULL);

	hal_fcxp_rx_plog(mod->bfa, fcxp, fcxp_rsp);

	if (fcxp->send_cbfn != NULL) {
		if (fcxp->caller == NULL) {
			bfa_trc(mod->bfa, fcxp->fcxp_tag);

			fcxp->send_cbfn(fcxp->caller, fcxp, fcxp->send_cbarg,
					fcxp_rsp->req_status, fcxp_rsp->rsp_len,
					fcxp_rsp->residue_len, &fcxp_rsp->fchs);
			/*
			 * fcxp automatically freed on return from the callback
			 */
			bfa_fcxp_free(fcxp);
		} else {
			bfa_trc(mod->bfa, fcxp->fcxp_tag);
			fcxp->rsp_status = fcxp_rsp->req_status;
			fcxp->rsp_len = fcxp_rsp->rsp_len;
			fcxp->residue_len = fcxp_rsp->residue_len;
			fcxp->rsp_fchs = fcxp_rsp->fchs;

			bfa_cb_queue(bfa, &fcxp->hcb_qe,
				      __bfa_fcxp_send_cbfn, fcxp);
		}
	} else {
		bfa_trc(bfa, fcxp_tag);
	}
}

static void
hal_fcxp_set_local_sges(struct bfi_sge_s *sge, u32 reqlen, u64 req_pa)
{
	union bfi_addr_u      sga_zero = { {0} };

	sge->sg_len = reqlen;
	sge->flags = BFI_SGE_DATA_LAST;
	bfa_dma_addr_set(sge[0].sga, req_pa);
	bfa_sge_to_be(sge);
	sge++;

	sge->sga = sga_zero;
	sge->sg_len = reqlen;
	sge->flags = BFI_SGE_PGDLEN;
	bfa_sge_to_be(sge);
}

static void
hal_fcxp_tx_plog(struct bfa_s *bfa, u32 reqlen, struct bfa_fcxp_s *fcxp,
		 struct fchs_s *fchs)
{
	/*
	 * TODO: TX ox_id
	 */
	if (reqlen > 0) {
		if (fcxp->use_ireqbuf) {
			u32        pld_w0 =
				*((u32 *) BFA_FCXP_REQ_PLD(fcxp));

			bfa_plog_fchdr_and_pl(bfa->plog, BFA_PL_MID_HAL_FCXP,
				BFA_PL_EID_TX,
				reqlen + sizeof(struct fchs_s), fchs, pld_w0);
		} else {
			bfa_plog_fchdr(bfa->plog, BFA_PL_MID_HAL_FCXP,
				BFA_PL_EID_TX, reqlen + sizeof(struct fchs_s),
				fchs);
		}
	} else {
		bfa_plog_fchdr(bfa->plog, BFA_PL_MID_HAL_FCXP, BFA_PL_EID_TX,
			       reqlen + sizeof(struct fchs_s), fchs);
	}
}

static void
hal_fcxp_rx_plog(struct bfa_s *bfa, struct bfa_fcxp_s *fcxp,
		 struct bfi_fcxp_send_rsp_s *fcxp_rsp)
{
	if (fcxp_rsp->rsp_len > 0) {
		if (fcxp->use_irspbuf) {
			u32        pld_w0 =
				*((u32 *) BFA_FCXP_RSP_PLD(fcxp));

			bfa_plog_fchdr_and_pl(bfa->plog, BFA_PL_MID_HAL_FCXP,
					      BFA_PL_EID_RX,
					      (u16) fcxp_rsp->rsp_len,
					      &fcxp_rsp->fchs, pld_w0);
		} else {
			bfa_plog_fchdr(bfa->plog, BFA_PL_MID_HAL_FCXP,
				       BFA_PL_EID_RX,
				       (u16) fcxp_rsp->rsp_len,
				       &fcxp_rsp->fchs);
		}
	} else {
		bfa_plog_fchdr(bfa->plog, BFA_PL_MID_HAL_FCXP, BFA_PL_EID_RX,
			       (u16) fcxp_rsp->rsp_len, &fcxp_rsp->fchs);
	}
}

/**
 * Handler to resume sending fcxp when space in available in cpe queue.
 */
static void
bfa_fcxp_qresume(void *cbarg)
{
	struct bfa_fcxp_s		*fcxp = cbarg;
	struct bfa_s			*bfa = fcxp->fcxp_mod->bfa;
	struct bfi_fcxp_send_req_s	*send_req;

	fcxp->reqq_waiting = BFA_FALSE;
	send_req = bfa_reqq_next(bfa, BFA_REQQ_FCXP);
	bfa_fcxp_queue(fcxp, send_req);
}

/**
 * Queue fcxp send request to foimrware.
 */
static void
bfa_fcxp_queue(struct bfa_fcxp_s *fcxp, struct bfi_fcxp_send_req_s *send_req)
{
	struct bfa_s      		*bfa = fcxp->fcxp_mod->bfa;
	struct bfa_fcxp_req_info_s	*reqi = &fcxp->req_info;
	struct bfa_fcxp_rsp_info_s	*rspi = &fcxp->rsp_info;
	struct bfa_rport_s		*rport = reqi->bfa_rport;

	bfi_h2i_set(send_req->mh, BFI_MC_FCXP, BFI_FCXP_H2I_SEND_REQ,
			bfa_lpuid(bfa));

	send_req->fcxp_tag = bfa_os_htons(fcxp->fcxp_tag);
	if (rport) {
		send_req->rport_fw_hndl = rport->fw_handle;
		send_req->max_frmsz = bfa_os_htons(rport->rport_info.max_frmsz);
		if (send_req->max_frmsz == 0)
			send_req->max_frmsz = bfa_os_htons(FC_MAX_PDUSZ);
	} else {
		send_req->rport_fw_hndl = 0;
		send_req->max_frmsz = bfa_os_htons(FC_MAX_PDUSZ);
	}

	send_req->vf_id = bfa_os_htons(reqi->vf_id);
	send_req->lp_tag = reqi->lp_tag;
	send_req->class = reqi->class;
	send_req->rsp_timeout = rspi->rsp_timeout;
	send_req->cts = reqi->cts;
	send_req->fchs = reqi->fchs;

	send_req->req_len = bfa_os_htonl(reqi->req_tot_len);
	send_req->rsp_maxlen = bfa_os_htonl(rspi->rsp_maxlen);

	/*
	 * setup req sgles
	 */
	if (fcxp->use_ireqbuf == 1) {
		hal_fcxp_set_local_sges(send_req->req_sge, reqi->req_tot_len,
					BFA_FCXP_REQ_PLD_PA(fcxp));
	} else {
		if (fcxp->nreq_sgles > 0) {
			bfa_assert(fcxp->nreq_sgles == 1);
			hal_fcxp_set_local_sges(send_req->req_sge,
						reqi->req_tot_len,
						fcxp->req_sga_cbfn(fcxp->caller,
								   0));
		} else {
			bfa_assert(reqi->req_tot_len == 0);
			hal_fcxp_set_local_sges(send_req->rsp_sge, 0, 0);
		}
	}

	/*
	 * setup rsp sgles
	 */
	if (fcxp->use_irspbuf == 1) {
		bfa_assert(rspi->rsp_maxlen <= BFA_FCXP_MAX_LBUF_SZ);

		hal_fcxp_set_local_sges(send_req->rsp_sge, rspi->rsp_maxlen,
					BFA_FCXP_RSP_PLD_PA(fcxp));

	} else {
		if (fcxp->nrsp_sgles > 0) {
			bfa_assert(fcxp->nrsp_sgles == 1);
			hal_fcxp_set_local_sges(send_req->rsp_sge,
						rspi->rsp_maxlen,
						fcxp->rsp_sga_cbfn(fcxp->caller,
								   0));
		} else {
			bfa_assert(rspi->rsp_maxlen == 0);
			hal_fcxp_set_local_sges(send_req->rsp_sge, 0, 0);
		}
	}

	hal_fcxp_tx_plog(bfa, reqi->req_tot_len, fcxp, &reqi->fchs);

	bfa_reqq_produce(bfa, BFA_REQQ_FCXP);

	bfa_trc(bfa, bfa_reqq_pi(bfa, BFA_REQQ_FCXP));
	bfa_trc(bfa, bfa_reqq_ci(bfa, BFA_REQQ_FCXP));
}


/**
 *  hal_fcxp_api BFA FCXP API
 */

/**
 * Allocate an FCXP instance to send a response or to send a request
 * that has a response. Request/response buffers are allocated by caller.
 *
 * @param[in]	bfa		BFA bfa instance
 * @param[in]	nreq_sgles	Number of SG elements required for request
 * 				buffer. 0, if fcxp internal buffers are	used.
 * 				Use bfa_fcxp_get_reqbuf() to get the
 * 				internal req buffer.
 * @param[in]	req_sgles	SG elements describing request buffer. Will be
 * 				copied in by BFA and hence can be freed on
 * 				return from this function.
 * @param[in]	get_req_sga	function ptr to be called to get a request SG
 * 				Address (given the sge index).
 * @param[in]	get_req_sglen	function ptr to be called to get a request SG
 * 				len (given the sge index).
 * @param[in]	get_rsp_sga	function ptr to be called to get a response SG
 * 				Address (given the sge index).
 * @param[in]	get_rsp_sglen	function ptr to be called to get a response SG
 * 				len (given the sge index).
 *
 * @return FCXP instance. NULL on failure.
 */
struct bfa_fcxp_s *
bfa_fcxp_alloc(void *caller, struct bfa_s *bfa, int nreq_sgles,
			int nrsp_sgles, bfa_fcxp_get_sgaddr_t req_sga_cbfn,
			bfa_fcxp_get_sglen_t req_sglen_cbfn,
			bfa_fcxp_get_sgaddr_t rsp_sga_cbfn,
			bfa_fcxp_get_sglen_t rsp_sglen_cbfn)
{
	struct bfa_fcxp_s *fcxp = NULL;
	u32        nreq_sgpg, nrsp_sgpg;

	bfa_assert(bfa != NULL);

	fcxp = bfa_fcxp_get(BFA_FCXP_MOD(bfa));
	if (fcxp == NULL)
		return NULL;

	bfa_trc(bfa, fcxp->fcxp_tag);

	fcxp->caller = caller;

	if (nreq_sgles == 0) {
		fcxp->use_ireqbuf = 1;
	} else {
		bfa_assert(req_sga_cbfn != NULL);
		bfa_assert(req_sglen_cbfn != NULL);

		fcxp->use_ireqbuf = 0;
		fcxp->req_sga_cbfn = req_sga_cbfn;
		fcxp->req_sglen_cbfn = req_sglen_cbfn;

		fcxp->nreq_sgles = nreq_sgles;

		/*
		 * alloc required sgpgs
		 */
		if (nreq_sgles > BFI_SGE_INLINE) {
			nreq_sgpg = BFA_SGPG_NPAGE(nreq_sgles);

			if (bfa_sgpg_malloc(bfa, &fcxp->req_sgpg_q, nreq_sgpg)
			    != BFA_STATUS_OK) {
				/*
				 * TODO
				 */
			}
		}
	}

	if (nrsp_sgles == 0) {
		fcxp->use_irspbuf = 1;
	} else {
		bfa_assert(rsp_sga_cbfn != NULL);
		bfa_assert(rsp_sglen_cbfn != NULL);

		fcxp->use_irspbuf = 0;
		fcxp->rsp_sga_cbfn = rsp_sga_cbfn;
		fcxp->rsp_sglen_cbfn = rsp_sglen_cbfn;

		fcxp->nrsp_sgles = nrsp_sgles;
		/*
		 * alloc required sgpgs
		 */
		if (nrsp_sgles > BFI_SGE_INLINE) {
			nrsp_sgpg = BFA_SGPG_NPAGE(nreq_sgles);

			if (bfa_sgpg_malloc
			    (bfa, &fcxp->rsp_sgpg_q, nrsp_sgpg)
			    != BFA_STATUS_OK) {
				/* bfa_sgpg_wait(bfa, &fcxp->rsp_sgpg_wqe,
				nrsp_sgpg); */
				/*
				 * TODO
				 */
			}
		}
	}

	return fcxp;
}

/**
 * Get the internal request buffer pointer
 *
 * @param[in]	fcxp	BFA fcxp pointer
 *
 * @return 		pointer to the internal request buffer
 */
void *
bfa_fcxp_get_reqbuf(struct bfa_fcxp_s *fcxp)
{
	struct bfa_fcxp_mod_s *mod = fcxp->fcxp_mod;
	void	*reqbuf;

	bfa_assert(fcxp->use_ireqbuf == 1);
	reqbuf = ((u8 *)mod->req_pld_list_kva) +
			fcxp->fcxp_tag * mod->req_pld_sz;
	return reqbuf;
}

u32
bfa_fcxp_get_reqbufsz(struct bfa_fcxp_s *fcxp)
{
	struct bfa_fcxp_mod_s *mod = fcxp->fcxp_mod;

	return mod->req_pld_sz;
}

/**
 * Get the internal response buffer pointer
 *
 * @param[in]	fcxp	BFA fcxp pointer
 *
 * @return		pointer to the internal request buffer
 */
void *
bfa_fcxp_get_rspbuf(struct bfa_fcxp_s *fcxp)
{
	struct bfa_fcxp_mod_s *mod = fcxp->fcxp_mod;
	void	*rspbuf;

	bfa_assert(fcxp->use_irspbuf == 1);

	rspbuf = ((u8 *)mod->rsp_pld_list_kva) +
			fcxp->fcxp_tag * mod->rsp_pld_sz;
	return rspbuf;
}

/**
 * 		Free the BFA FCXP
 *
 * @param[in]	fcxp			BFA fcxp pointer
 *
 * @return 		void
 */
void
bfa_fcxp_free(struct bfa_fcxp_s *fcxp)
{
	struct bfa_fcxp_mod_s *mod = fcxp->fcxp_mod;

	bfa_assert(fcxp != NULL);
	bfa_trc(mod->bfa, fcxp->fcxp_tag);
	bfa_fcxp_put(fcxp);
}

/**
 * Send a FCXP request
 *
 * @param[in]	fcxp	BFA fcxp pointer
 * @param[in]	rport	BFA rport pointer. Could be left NULL for WKA rports
 * @param[in]	vf_id	virtual Fabric ID
 * @param[in]	lp_tag  lport tag
 * @param[in]	cts	use Continous sequence
 * @param[in]	cos	fc Class of Service
 * @param[in]	reqlen	request length, does not include FCHS length
 * @param[in]	fchs	fc Header Pointer. The header content will be copied
 *			in by BFA.
 *
 * @param[in]	cbfn	call back function to be called on receiving
 * 								the response
 * @param[in]	cbarg	arg for cbfn
 * @param[in]	rsp_timeout
 *			response timeout
 *
 * @return		bfa_status_t
 */
void
bfa_fcxp_send(struct bfa_fcxp_s *fcxp, struct bfa_rport_s *rport,
		u16 vf_id, u8 lp_tag, bfa_boolean_t cts, enum fc_cos cos,
		u32 reqlen, struct fchs_s *fchs, bfa_cb_fcxp_send_t cbfn,
		void *cbarg, u32 rsp_maxlen, u8 rsp_timeout)
{
	struct bfa_s			*bfa  = fcxp->fcxp_mod->bfa;
	struct bfa_fcxp_req_info_s	*reqi = &fcxp->req_info;
	struct bfa_fcxp_rsp_info_s	*rspi = &fcxp->rsp_info;
	struct bfi_fcxp_send_req_s	*send_req;

	bfa_trc(bfa, fcxp->fcxp_tag);

	/**
	 * setup request/response info
	 */
	reqi->bfa_rport = rport;
	reqi->vf_id = vf_id;
	reqi->lp_tag = lp_tag;
	reqi->class = cos;
	rspi->rsp_timeout = rsp_timeout;
	reqi->cts = cts;
	reqi->fchs = *fchs;
	reqi->req_tot_len = reqlen;
	rspi->rsp_maxlen = rsp_maxlen;
	fcxp->send_cbfn = cbfn ? cbfn : bfa_fcxp_null_comp;
	fcxp->send_cbarg = cbarg;

	/**
	 * If no room in CPE queue, wait for space in request queue
	 */
	send_req = bfa_reqq_next(bfa, BFA_REQQ_FCXP);
	if (!send_req) {
		bfa_trc(bfa, fcxp->fcxp_tag);
		fcxp->reqq_waiting = BFA_TRUE;
		bfa_reqq_wait(bfa, BFA_REQQ_FCXP, &fcxp->reqq_wqe);
		return;
	}

	bfa_fcxp_queue(fcxp, send_req);
}

/**
 * Abort a BFA FCXP
 *
 * @param[in]	fcxp	BFA fcxp pointer
 *
 * @return 		void
 */
bfa_status_t
bfa_fcxp_abort(struct bfa_fcxp_s *fcxp)
{
	bfa_assert(0);
	return BFA_STATUS_OK;
}

void
bfa_fcxp_alloc_wait(struct bfa_s *bfa, struct bfa_fcxp_wqe_s *wqe,
			bfa_fcxp_alloc_cbfn_t alloc_cbfn, void *alloc_cbarg)
{
	struct bfa_fcxp_mod_s *mod = BFA_FCXP_MOD(bfa);

	bfa_assert(list_empty(&mod->fcxp_free_q));

	wqe->alloc_cbfn = alloc_cbfn;
	wqe->alloc_cbarg = alloc_cbarg;
	list_add_tail(&wqe->qe, &mod->wait_q);
}

void
bfa_fcxp_walloc_cancel(struct bfa_s *bfa, struct bfa_fcxp_wqe_s *wqe)
{
	struct bfa_fcxp_mod_s *mod = BFA_FCXP_MOD(bfa);

	bfa_assert(bfa_q_is_on_q(&mod->wait_q, wqe));
	list_del(&wqe->qe);
}

void
bfa_fcxp_discard(struct bfa_fcxp_s *fcxp)
{
	/**
	 * If waiting for room in request queue, cancel reqq wait
	 * and free fcxp.
	 */
	if (fcxp->reqq_waiting) {
		fcxp->reqq_waiting = BFA_FALSE;
		bfa_reqq_wcancel(&fcxp->reqq_wqe);
		bfa_fcxp_free(fcxp);
		return;
	}

	fcxp->send_cbfn = bfa_fcxp_null_comp;
}



/**
 *  hal_fcxp_public BFA FCXP public functions
 */

void
bfa_fcxp_isr(struct bfa_s *bfa, struct bfi_msg_s *msg)
{
	switch (msg->mhdr.msg_id) {
	case BFI_FCXP_I2H_SEND_RSP:
		hal_fcxp_send_comp(bfa, (struct bfi_fcxp_send_rsp_s *) msg);
		break;

	default:
		bfa_trc(bfa, msg->mhdr.msg_id);
		bfa_assert(0);
	}
}

u32
bfa_fcxp_get_maxrsp(struct bfa_s *bfa)
{
	struct bfa_fcxp_mod_s *mod = BFA_FCXP_MOD(bfa);

	return mod->rsp_pld_sz;
}


