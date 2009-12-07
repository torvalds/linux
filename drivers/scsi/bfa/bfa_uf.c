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

/**
 *  bfa_uf.c BFA unsolicited frame receive implementation
 */

#include <bfa.h>
#include <bfa_svc.h>
#include <bfi/bfi_uf.h>
#include <cs/bfa_debug.h>

BFA_TRC_FILE(HAL, UF);
BFA_MODULE(uf);

/*
 *****************************************************************************
 * Internal functions
 *****************************************************************************
 */
static void
__bfa_cb_uf_recv(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_uf_s   *uf = cbarg;
	struct bfa_uf_mod_s *ufm = BFA_UF_MOD(uf->bfa);

	if (complete)
		ufm->ufrecv(ufm->cbarg, uf);
}

static void
claim_uf_pbs(struct bfa_uf_mod_s *ufm, struct bfa_meminfo_s *mi)
{
	u32        uf_pb_tot_sz;

	ufm->uf_pbs_kva = (struct bfa_uf_buf_s *) bfa_meminfo_dma_virt(mi);
	ufm->uf_pbs_pa = bfa_meminfo_dma_phys(mi);
	uf_pb_tot_sz = BFA_ROUNDUP((sizeof(struct bfa_uf_buf_s) * ufm->num_ufs),
							BFA_DMA_ALIGN_SZ);

	bfa_meminfo_dma_virt(mi) += uf_pb_tot_sz;
	bfa_meminfo_dma_phys(mi) += uf_pb_tot_sz;

	bfa_os_memset((void *)ufm->uf_pbs_kva, 0, uf_pb_tot_sz);
}

static void
claim_uf_post_msgs(struct bfa_uf_mod_s *ufm, struct bfa_meminfo_s *mi)
{
	struct bfi_uf_buf_post_s *uf_bp_msg;
	struct bfi_sge_s      *sge;
	union bfi_addr_u      sga_zero = { {0} };
	u16        i;
	u16        buf_len;

	ufm->uf_buf_posts = (struct bfi_uf_buf_post_s *) bfa_meminfo_kva(mi);
	uf_bp_msg = ufm->uf_buf_posts;

	for (i = 0, uf_bp_msg = ufm->uf_buf_posts; i < ufm->num_ufs;
	     i++, uf_bp_msg++) {
		bfa_os_memset(uf_bp_msg, 0, sizeof(struct bfi_uf_buf_post_s));

		uf_bp_msg->buf_tag = i;
		buf_len = sizeof(struct bfa_uf_buf_s);
		uf_bp_msg->buf_len = bfa_os_htons(buf_len);
		bfi_h2i_set(uf_bp_msg->mh, BFI_MC_UF, BFI_UF_H2I_BUF_POST,
			    bfa_lpuid(ufm->bfa));

		sge = uf_bp_msg->sge;
		sge[0].sg_len = buf_len;
		sge[0].flags = BFI_SGE_DATA_LAST;
		bfa_dma_addr_set(sge[0].sga, ufm_pbs_pa(ufm, i));
		bfa_sge_to_be(sge);

		sge[1].sg_len = buf_len;
		sge[1].flags = BFI_SGE_PGDLEN;
		sge[1].sga = sga_zero;
		bfa_sge_to_be(&sge[1]);
	}

	/**
	 * advance pointer beyond consumed memory
	 */
	bfa_meminfo_kva(mi) = (u8 *) uf_bp_msg;
}

static void
claim_ufs(struct bfa_uf_mod_s *ufm, struct bfa_meminfo_s *mi)
{
	u16        i;
	struct bfa_uf_s   *uf;

	/*
	 * Claim block of memory for UF list
	 */
	ufm->uf_list = (struct bfa_uf_s *) bfa_meminfo_kva(mi);

	/*
	 * Initialize UFs and queue it in UF free queue
	 */
	for (i = 0, uf = ufm->uf_list; i < ufm->num_ufs; i++, uf++) {
		bfa_os_memset(uf, 0, sizeof(struct bfa_uf_s));
		uf->bfa = ufm->bfa;
		uf->uf_tag = i;
		uf->pb_len = sizeof(struct bfa_uf_buf_s);
		uf->buf_kva = (void *)&ufm->uf_pbs_kva[i];
		uf->buf_pa = ufm_pbs_pa(ufm, i);
		list_add_tail(&uf->qe, &ufm->uf_free_q);
	}

	/**
	 * advance memory pointer
	 */
	bfa_meminfo_kva(mi) = (u8 *) uf;
}

static void
uf_mem_claim(struct bfa_uf_mod_s *ufm, struct bfa_meminfo_s *mi)
{
	claim_uf_pbs(ufm, mi);
	claim_ufs(ufm, mi);
	claim_uf_post_msgs(ufm, mi);
}

static void
bfa_uf_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *ndm_len, u32 *dm_len)
{
	u32        num_ufs = cfg->fwcfg.num_uf_bufs;

	/*
	 * dma-able memory for UF posted bufs
	 */
	*dm_len += BFA_ROUNDUP((sizeof(struct bfa_uf_buf_s) * num_ufs),
							BFA_DMA_ALIGN_SZ);

	/*
	 * kernel Virtual memory for UFs and UF buf post msg copies
	 */
	*ndm_len += sizeof(struct bfa_uf_s) * num_ufs;
	*ndm_len += sizeof(struct bfi_uf_buf_post_s) * num_ufs;
}

static void
bfa_uf_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		  struct bfa_meminfo_s *meminfo, struct bfa_pcidev_s *pcidev)
{
	struct bfa_uf_mod_s *ufm = BFA_UF_MOD(bfa);

	bfa_os_memset(ufm, 0, sizeof(struct bfa_uf_mod_s));
	ufm->bfa = bfa;
	ufm->num_ufs = cfg->fwcfg.num_uf_bufs;
	INIT_LIST_HEAD(&ufm->uf_free_q);
	INIT_LIST_HEAD(&ufm->uf_posted_q);

	uf_mem_claim(ufm, meminfo);
}

static void
bfa_uf_initdone(struct bfa_s *bfa)
{
}

static void
bfa_uf_detach(struct bfa_s *bfa)
{
}

static struct bfa_uf_s *
bfa_uf_get(struct bfa_uf_mod_s *uf_mod)
{
	struct bfa_uf_s   *uf;

	bfa_q_deq(&uf_mod->uf_free_q, &uf);
	return (uf);
}

static void
bfa_uf_put(struct bfa_uf_mod_s *uf_mod, struct bfa_uf_s *uf)
{
	list_add_tail(&uf->qe, &uf_mod->uf_free_q);
}

static bfa_status_t
bfa_uf_post(struct bfa_uf_mod_s *ufm, struct bfa_uf_s *uf)
{
	struct bfi_uf_buf_post_s *uf_post_msg;

	uf_post_msg = bfa_reqq_next(ufm->bfa, BFA_REQQ_FCXP);
	if (!uf_post_msg)
		return BFA_STATUS_FAILED;

	bfa_os_memcpy(uf_post_msg, &ufm->uf_buf_posts[uf->uf_tag],
		      sizeof(struct bfi_uf_buf_post_s));
	bfa_reqq_produce(ufm->bfa, BFA_REQQ_FCXP);

	bfa_trc(ufm->bfa, uf->uf_tag);

	list_add_tail(&uf->qe, &ufm->uf_posted_q);
	return BFA_STATUS_OK;
}

static void
bfa_uf_post_all(struct bfa_uf_mod_s *uf_mod)
{
	struct bfa_uf_s   *uf;

	while ((uf = bfa_uf_get(uf_mod)) != NULL) {
		if (bfa_uf_post(uf_mod, uf) != BFA_STATUS_OK)
			break;
	}
}

static void
uf_recv(struct bfa_s *bfa, struct bfi_uf_frm_rcvd_s *m)
{
	struct bfa_uf_mod_s *ufm = BFA_UF_MOD(bfa);
	u16        uf_tag = m->buf_tag;
	struct bfa_uf_buf_s *uf_buf = &ufm->uf_pbs_kva[uf_tag];
	struct bfa_uf_s   *uf = &ufm->uf_list[uf_tag];
	u8        *buf = &uf_buf->d[0];
	struct fchs_s         *fchs;

	m->frm_len = bfa_os_ntohs(m->frm_len);
	m->xfr_len = bfa_os_ntohs(m->xfr_len);

	fchs = (struct fchs_s *) uf_buf;

	list_del(&uf->qe);	/* dequeue from posted queue */

	uf->data_ptr = buf;
	uf->data_len = m->xfr_len;

	bfa_assert(uf->data_len >= sizeof(struct fchs_s));

	if (uf->data_len == sizeof(struct fchs_s)) {
		bfa_plog_fchdr(bfa->plog, BFA_PL_MID_HAL_UF, BFA_PL_EID_RX,
			       uf->data_len, (struct fchs_s *) buf);
	} else {
		u32        pld_w0 = *((u32 *) (buf + sizeof(struct fchs_s)));
		bfa_plog_fchdr_and_pl(bfa->plog, BFA_PL_MID_HAL_UF,
				      BFA_PL_EID_RX, uf->data_len,
				      (struct fchs_s *) buf, pld_w0);
	}

	bfa_cb_queue(bfa, &uf->hcb_qe, __bfa_cb_uf_recv, uf);
}

static void
bfa_uf_stop(struct bfa_s *bfa)
{
}

static void
bfa_uf_iocdisable(struct bfa_s *bfa)
{
	struct bfa_uf_mod_s *ufm = BFA_UF_MOD(bfa);
	struct bfa_uf_s   *uf;
	struct list_head        *qe, *qen;

	list_for_each_safe(qe, qen, &ufm->uf_posted_q) {
		uf = (struct bfa_uf_s *) qe;
		list_del(&uf->qe);
		bfa_uf_put(ufm, uf);
	}
}

static void
bfa_uf_start(struct bfa_s *bfa)
{
	bfa_uf_post_all(BFA_UF_MOD(bfa));
}



/**
 *  bfa_uf_api
 */

/**
 * 		Register handler for all unsolicted recieve frames.
 *
 * @param[in]	bfa		BFA instance
 * @param[in]	ufrecv	receive handler function
 * @param[in]	cbarg	receive handler arg
 */
void
bfa_uf_recv_register(struct bfa_s *bfa, bfa_cb_uf_recv_t ufrecv, void *cbarg)
{
	struct bfa_uf_mod_s *ufm = BFA_UF_MOD(bfa);

	ufm->ufrecv = ufrecv;
	ufm->cbarg = cbarg;
}

/**
 * 		Free an unsolicited frame back to BFA.
 *
 * @param[in]		uf		unsolicited frame to be freed
 *
 * @return None
 */
void
bfa_uf_free(struct bfa_uf_s *uf)
{
	bfa_uf_put(BFA_UF_MOD(uf->bfa), uf);
	bfa_uf_post_all(BFA_UF_MOD(uf->bfa));
}



/**
 *  uf_pub BFA uf module public functions
 */

void
bfa_uf_isr(struct bfa_s *bfa, struct bfi_msg_s *msg)
{
	bfa_trc(bfa, msg->mhdr.msg_id);

	switch (msg->mhdr.msg_id) {
	case BFI_UF_I2H_FRM_RCVD:
		uf_recv(bfa, (struct bfi_uf_frm_rcvd_s *) msg);
		break;

	default:
		bfa_trc(bfa, msg->mhdr.msg_id);
		bfa_assert(0);
	}
}


