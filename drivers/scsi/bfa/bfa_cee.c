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

#include <defs/bfa_defs_cee.h>
#include <cs/bfa_trc.h>
#include <cs/bfa_log.h>
#include <cs/bfa_debug.h>
#include <cee/bfa_cee.h>
#include <bfi/bfi_cee.h>
#include <bfi/bfi.h>
#include <bfa_ioc.h>
#include <cna/bfa_cna_trcmod.h>

BFA_TRC_FILE(CNA, CEE);

#define bfa_ioc_portid(__ioc) ((__ioc)->port_id)
#define bfa_lpuid(__arg) bfa_ioc_portid(&(__arg)->ioc)

static void     bfa_cee_format_lldp_cfg(struct bfa_cee_lldp_cfg_s *lldp_cfg);
static void     bfa_cee_format_dcbcx_stats(struct bfa_cee_dcbx_stats_s
					   *dcbcx_stats);
static void     bfa_cee_format_lldp_stats(struct bfa_cee_lldp_stats_s
					  *lldp_stats);
static void     bfa_cee_format_cfg_stats(struct bfa_cee_cfg_stats_s *cfg_stats);
static void     bfa_cee_format_cee_cfg(void *buffer);
static void     bfa_cee_format_cee_stats(void *buffer);

static void
bfa_cee_format_cee_stats(void *buffer)
{
	struct bfa_cee_stats_s *cee_stats = buffer;
	bfa_cee_format_dcbcx_stats(&cee_stats->dcbx_stats);
	bfa_cee_format_lldp_stats(&cee_stats->lldp_stats);
	bfa_cee_format_cfg_stats(&cee_stats->cfg_stats);
}

static void
bfa_cee_format_cee_cfg(void *buffer)
{
	struct bfa_cee_attr_s *cee_cfg = buffer;
	bfa_cee_format_lldp_cfg(&cee_cfg->lldp_remote);
}

static void
bfa_cee_format_dcbcx_stats(struct bfa_cee_dcbx_stats_s *dcbcx_stats)
{
	dcbcx_stats->subtlvs_unrecognized =
		bfa_os_ntohl(dcbcx_stats->subtlvs_unrecognized);
	dcbcx_stats->negotiation_failed =
		bfa_os_ntohl(dcbcx_stats->negotiation_failed);
	dcbcx_stats->remote_cfg_changed =
		bfa_os_ntohl(dcbcx_stats->remote_cfg_changed);
	dcbcx_stats->tlvs_received = bfa_os_ntohl(dcbcx_stats->tlvs_received);
	dcbcx_stats->tlvs_invalid = bfa_os_ntohl(dcbcx_stats->tlvs_invalid);
	dcbcx_stats->seqno = bfa_os_ntohl(dcbcx_stats->seqno);
	dcbcx_stats->ackno = bfa_os_ntohl(dcbcx_stats->ackno);
	dcbcx_stats->recvd_seqno = bfa_os_ntohl(dcbcx_stats->recvd_seqno);
	dcbcx_stats->recvd_ackno = bfa_os_ntohl(dcbcx_stats->recvd_ackno);
}

static void
bfa_cee_format_lldp_stats(struct bfa_cee_lldp_stats_s *lldp_stats)
{
	lldp_stats->frames_transmitted =
		bfa_os_ntohl(lldp_stats->frames_transmitted);
	lldp_stats->frames_aged_out = bfa_os_ntohl(lldp_stats->frames_aged_out);
	lldp_stats->frames_discarded =
		bfa_os_ntohl(lldp_stats->frames_discarded);
	lldp_stats->frames_in_error = bfa_os_ntohl(lldp_stats->frames_in_error);
	lldp_stats->frames_rcvd = bfa_os_ntohl(lldp_stats->frames_rcvd);
	lldp_stats->tlvs_discarded = bfa_os_ntohl(lldp_stats->tlvs_discarded);
	lldp_stats->tlvs_unrecognized =
		bfa_os_ntohl(lldp_stats->tlvs_unrecognized);
}

static void
bfa_cee_format_cfg_stats(struct bfa_cee_cfg_stats_s *cfg_stats)
{
	cfg_stats->cee_status_down = bfa_os_ntohl(cfg_stats->cee_status_down);
	cfg_stats->cee_status_up = bfa_os_ntohl(cfg_stats->cee_status_up);
	cfg_stats->cee_hw_cfg_changed =
		bfa_os_ntohl(cfg_stats->cee_hw_cfg_changed);
	cfg_stats->recvd_invalid_cfg =
		bfa_os_ntohl(cfg_stats->recvd_invalid_cfg);
}

static void
bfa_cee_format_lldp_cfg(struct bfa_cee_lldp_cfg_s *lldp_cfg)
{
	lldp_cfg->time_to_interval = bfa_os_ntohs(lldp_cfg->time_to_interval);
	lldp_cfg->enabled_system_cap =
		bfa_os_ntohs(lldp_cfg->enabled_system_cap);
}

/**
 * bfa_cee_attr_meminfo()
 *
 *
 * @param[in] void
 *
 * @return Size of DMA region
 */
static          u32
bfa_cee_attr_meminfo(void)
{
	return BFA_ROUNDUP(sizeof(struct bfa_cee_attr_s), BFA_DMA_ALIGN_SZ);
}

/**
 * bfa_cee_stats_meminfo()
 *
 *
 * @param[in] void
 *
 * @return Size of DMA region
 */
static          u32
bfa_cee_stats_meminfo(void)
{
	return BFA_ROUNDUP(sizeof(struct bfa_cee_stats_s), BFA_DMA_ALIGN_SZ);
}

/**
 * bfa_cee_get_attr_isr()
 *
 *
 * @param[in] cee - Pointer to the CEE module
 *            status - Return status from the f/w
 *
 * @return void
 */
static void
bfa_cee_get_attr_isr(struct bfa_cee_s *cee, bfa_status_t status)
{
	cee->get_attr_status = status;
	bfa_trc(cee, 0);
	if (status == BFA_STATUS_OK) {
		bfa_trc(cee, 0);
		/*
		 * The requested data has been copied to the DMA area, *process
		 * it.
		 */
		memcpy(cee->attr, cee->attr_dma.kva,
		       sizeof(struct bfa_cee_attr_s));
		bfa_cee_format_cee_cfg(cee->attr);
	}
	cee->get_attr_pending = BFA_FALSE;
	if (cee->cbfn.get_attr_cbfn) {
		bfa_trc(cee, 0);
		cee->cbfn.get_attr_cbfn(cee->cbfn.get_attr_cbarg, status);
	}
	bfa_trc(cee, 0);
}

/**
 * bfa_cee_get_attr_isr()
 *
 *
 * @param[in] cee - Pointer to the CEE module
 *            status - Return status from the f/w
 *
 * @return void
 */
static void
bfa_cee_get_stats_isr(struct bfa_cee_s *cee, bfa_status_t status)
{
	cee->get_stats_status = status;
	bfa_trc(cee, 0);
	if (status == BFA_STATUS_OK) {
		bfa_trc(cee, 0);
		/*
		 * The requested data has been copied to the DMA area, process
		 * it.
		 */
		memcpy(cee->stats, cee->stats_dma.kva,
		       sizeof(struct bfa_cee_stats_s));
		bfa_cee_format_cee_stats(cee->stats);
	}
	cee->get_stats_pending = BFA_FALSE;
	bfa_trc(cee, 0);
	if (cee->cbfn.get_stats_cbfn) {
		bfa_trc(cee, 0);
		cee->cbfn.get_stats_cbfn(cee->cbfn.get_stats_cbarg, status);
	}
	bfa_trc(cee, 0);
}

/**
 * bfa_cee_get_attr_isr()
 *
 *
 * @param[in] cee - Pointer to the CEE module
 *            status - Return status from the f/w
 *
 * @return void
 */
static void
bfa_cee_reset_stats_isr(struct bfa_cee_s *cee, bfa_status_t status)
{
	cee->reset_stats_status = status;
	cee->reset_stats_pending = BFA_FALSE;
	if (cee->cbfn.reset_stats_cbfn)
		cee->cbfn.reset_stats_cbfn(cee->cbfn.reset_stats_cbarg, status);
}

/**
 * bfa_cee_meminfo()
 *
 *
 * @param[in] void
 *
 * @return Size of DMA region
 */
u32
bfa_cee_meminfo(void)
{
	return bfa_cee_attr_meminfo() + bfa_cee_stats_meminfo();
}

/**
 * bfa_cee_mem_claim()
 *
 *
 * @param[in] cee CEE module pointer
 * 	      dma_kva Kernel Virtual Address of CEE DMA Memory
 * 	      dma_pa  Physical Address of CEE DMA Memory
 *
 * @return void
 */
void
bfa_cee_mem_claim(struct bfa_cee_s *cee, u8 *dma_kva, u64 dma_pa)
{
	cee->attr_dma.kva = dma_kva;
	cee->attr_dma.pa = dma_pa;
	cee->stats_dma.kva = dma_kva + bfa_cee_attr_meminfo();
	cee->stats_dma.pa = dma_pa + bfa_cee_attr_meminfo();
	cee->attr = (struct bfa_cee_attr_s *)dma_kva;
	cee->stats =
		(struct bfa_cee_stats_s *)(dma_kva + bfa_cee_attr_meminfo());
}

/**
 * bfa_cee_get_attr()
 *
 *   Send the request to the f/w to fetch CEE attributes.
 *
 * @param[in] Pointer to the CEE module data structure.
 *
 * @return Status
 */

bfa_status_t
bfa_cee_get_attr(struct bfa_cee_s *cee, struct bfa_cee_attr_s *attr,
		 bfa_cee_get_attr_cbfn_t cbfn, void *cbarg)
{
	struct bfi_cee_get_req_s *cmd;

	bfa_assert((cee != NULL) && (cee->ioc != NULL));
	bfa_trc(cee, 0);
	if (!bfa_ioc_is_operational(cee->ioc)) {
		bfa_trc(cee, 0);
		return BFA_STATUS_IOC_FAILURE;
	}
	if (cee->get_attr_pending == BFA_TRUE) {
		bfa_trc(cee, 0);
		return BFA_STATUS_DEVBUSY;
	}
	cee->get_attr_pending = BFA_TRUE;
	cmd = (struct bfi_cee_get_req_s *)cee->get_cfg_mb.msg;
	cee->attr = attr;
	cee->cbfn.get_attr_cbfn = cbfn;
	cee->cbfn.get_attr_cbarg = cbarg;
	bfi_h2i_set(cmd->mh, BFI_MC_CEE, BFI_CEE_H2I_GET_CFG_REQ,
		    bfa_ioc_portid(cee->ioc));
	bfa_dma_be_addr_set(cmd->dma_addr, cee->attr_dma.pa);
	bfa_ioc_mbox_queue(cee->ioc, &cee->get_cfg_mb);
	bfa_trc(cee, 0);

	return BFA_STATUS_OK;
}

/**
 * bfa_cee_get_stats()
 *
 *   Send the request to the f/w to fetch CEE statistics.
 *
 * @param[in] Pointer to the CEE module data structure.
 *
 * @return Status
 */

bfa_status_t
bfa_cee_get_stats(struct bfa_cee_s *cee, struct bfa_cee_stats_s *stats,
		  bfa_cee_get_stats_cbfn_t cbfn, void *cbarg)
{
	struct bfi_cee_get_req_s *cmd;

	bfa_assert((cee != NULL) && (cee->ioc != NULL));

	if (!bfa_ioc_is_operational(cee->ioc)) {
		bfa_trc(cee, 0);
		return BFA_STATUS_IOC_FAILURE;
	}
	if (cee->get_stats_pending == BFA_TRUE) {
		bfa_trc(cee, 0);
		return BFA_STATUS_DEVBUSY;
	}
	cee->get_stats_pending = BFA_TRUE;
	cmd = (struct bfi_cee_get_req_s *)cee->get_stats_mb.msg;
	cee->stats = stats;
	cee->cbfn.get_stats_cbfn = cbfn;
	cee->cbfn.get_stats_cbarg = cbarg;
	bfi_h2i_set(cmd->mh, BFI_MC_CEE, BFI_CEE_H2I_GET_STATS_REQ,
		    bfa_ioc_portid(cee->ioc));
	bfa_dma_be_addr_set(cmd->dma_addr, cee->stats_dma.pa);
	bfa_ioc_mbox_queue(cee->ioc, &cee->get_stats_mb);
	bfa_trc(cee, 0);

	return BFA_STATUS_OK;
}

/**
 * bfa_cee_reset_stats()
 *
 *
 * @param[in] Pointer to the CEE module data structure.
 *
 * @return Status
 */

bfa_status_t
bfa_cee_reset_stats(struct bfa_cee_s *cee, bfa_cee_reset_stats_cbfn_t cbfn,
		    void *cbarg)
{
	struct bfi_cee_reset_stats_s *cmd;

	bfa_assert((cee != NULL) && (cee->ioc != NULL));
	if (!bfa_ioc_is_operational(cee->ioc)) {
		bfa_trc(cee, 0);
		return BFA_STATUS_IOC_FAILURE;
	}
	if (cee->reset_stats_pending == BFA_TRUE) {
		bfa_trc(cee, 0);
		return BFA_STATUS_DEVBUSY;
	}
	cee->reset_stats_pending = BFA_TRUE;
	cmd = (struct bfi_cee_reset_stats_s *)cee->reset_stats_mb.msg;
	cee->cbfn.reset_stats_cbfn = cbfn;
	cee->cbfn.reset_stats_cbarg = cbarg;
	bfi_h2i_set(cmd->mh, BFI_MC_CEE, BFI_CEE_H2I_RESET_STATS,
		    bfa_ioc_portid(cee->ioc));
	bfa_ioc_mbox_queue(cee->ioc, &cee->reset_stats_mb);
	bfa_trc(cee, 0);
	return BFA_STATUS_OK;
}

/**
 * bfa_cee_isrs()
 *
 *
 * @param[in] Pointer to the CEE module data structure.
 *
 * @return void
 */

void
bfa_cee_isr(void *cbarg, struct bfi_mbmsg_s *m)
{
	union bfi_cee_i2h_msg_u *msg;
	struct bfi_cee_get_rsp_s *get_rsp;
	struct bfa_cee_s *cee = (struct bfa_cee_s *)cbarg;
	msg = (union bfi_cee_i2h_msg_u *)m;
	get_rsp = (struct bfi_cee_get_rsp_s *)m;
	bfa_trc(cee, msg->mh.msg_id);
	switch (msg->mh.msg_id) {
	case BFI_CEE_I2H_GET_CFG_RSP:
		bfa_trc(cee, get_rsp->cmd_status);
		bfa_cee_get_attr_isr(cee, get_rsp->cmd_status);
		break;
	case BFI_CEE_I2H_GET_STATS_RSP:
		bfa_cee_get_stats_isr(cee, get_rsp->cmd_status);
		break;
	case BFI_CEE_I2H_RESET_STATS_RSP:
		bfa_cee_reset_stats_isr(cee, get_rsp->cmd_status);
		break;
	default:
		bfa_assert(0);
	}
}

/**
 * bfa_cee_hbfail()
 *
 *
 * @param[in] Pointer to the CEE module data structure.
 *
 * @return void
 */

void
bfa_cee_hbfail(void *arg)
{
	struct bfa_cee_s *cee;
	cee = (struct bfa_cee_s *)arg;

	if (cee->get_attr_pending == BFA_TRUE) {
		cee->get_attr_status = BFA_STATUS_FAILED;
		cee->get_attr_pending = BFA_FALSE;
		if (cee->cbfn.get_attr_cbfn) {
			cee->cbfn.get_attr_cbfn(cee->cbfn.get_attr_cbarg,
						BFA_STATUS_FAILED);
		}
	}
	if (cee->get_stats_pending == BFA_TRUE) {
		cee->get_stats_status = BFA_STATUS_FAILED;
		cee->get_stats_pending = BFA_FALSE;
		if (cee->cbfn.get_stats_cbfn) {
			cee->cbfn.get_stats_cbfn(cee->cbfn.get_stats_cbarg,
						 BFA_STATUS_FAILED);
		}
	}
	if (cee->reset_stats_pending == BFA_TRUE) {
		cee->reset_stats_status = BFA_STATUS_FAILED;
		cee->reset_stats_pending = BFA_FALSE;
		if (cee->cbfn.reset_stats_cbfn) {
			cee->cbfn.reset_stats_cbfn(cee->cbfn.reset_stats_cbarg,
						   BFA_STATUS_FAILED);
		}
	}
}

/**
 * bfa_cee_attach()
 *
 *
 * @param[in] cee - Pointer to the CEE module data structure
 *            ioc - Pointer to the ioc module data structure
 *            dev - Pointer to the device driver module data structure
 *                  The device driver specific mbox ISR functions have
 *                  this pointer as one of the parameters.
 *            trcmod -
 *            logmod -
 *
 * @return void
 */
void
bfa_cee_attach(struct bfa_cee_s *cee, struct bfa_ioc_s *ioc, void *dev,
	       struct bfa_trc_mod_s *trcmod, struct bfa_log_mod_s *logmod)
{
	bfa_assert(cee != NULL);
	cee->dev = dev;
	cee->trcmod = trcmod;
	cee->logmod = logmod;
	cee->ioc = ioc;

	bfa_ioc_mbox_regisr(cee->ioc, BFI_MC_CEE, bfa_cee_isr, cee);
	bfa_ioc_hbfail_init(&cee->hbfail, bfa_cee_hbfail, cee);
	bfa_ioc_hbfail_register(cee->ioc, &cee->hbfail);
	bfa_trc(cee, 0);
}

/**
 * bfa_cee_detach()
 *
 *
 * @param[in] cee - Pointer to the CEE module data structure
 *
 * @return void
 */
void
bfa_cee_detach(struct bfa_cee_s *cee)
{
	/*
	 * For now, just check if there is some ioctl pending and mark that as
	 * failed?
	 */
	/* bfa_cee_hbfail(cee); */
}
