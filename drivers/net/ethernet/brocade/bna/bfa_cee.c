/*
 * Linux network driver for Brocade Converged Network Adapter.
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
/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 */

#include "bfa_cee.h"
#include "bfi_cna.h"
#include "bfa_ioc.h"

static void bfa_cee_format_lldp_cfg(struct bfa_cee_lldp_cfg *lldp_cfg);
static void bfa_cee_format_cee_cfg(void *buffer);

static void
bfa_cee_format_cee_cfg(void *buffer)
{
	struct bfa_cee_attr *cee_cfg = buffer;
	bfa_cee_format_lldp_cfg(&cee_cfg->lldp_remote);
}

static void
bfa_cee_stats_swap(struct bfa_cee_stats *stats)
{
	u32 *buffer = (u32 *)stats;
	int i;

	for (i = 0; i < (sizeof(struct bfa_cee_stats) / sizeof(u32));
		i++) {
		buffer[i] = ntohl(buffer[i]);
	}
}

static void
bfa_cee_format_lldp_cfg(struct bfa_cee_lldp_cfg *lldp_cfg)
{
	lldp_cfg->time_to_live =
			ntohs(lldp_cfg->time_to_live);
	lldp_cfg->enabled_system_cap =
			ntohs(lldp_cfg->enabled_system_cap);
}

/**
 * bfa_cee_attr_meminfo()
 *
 * @brief Returns the size of the DMA memory needed by CEE attributes
 *
 * @param[in] void
 *
 * @return Size of DMA region
 */
static u32
bfa_cee_attr_meminfo(void)
{
	return roundup(sizeof(struct bfa_cee_attr), BFA_DMA_ALIGN_SZ);
}
/**
 * bfa_cee_stats_meminfo()
 *
 * @brief Returns the size of the DMA memory needed by CEE stats
 *
 * @param[in] void
 *
 * @return Size of DMA region
 */
static u32
bfa_cee_stats_meminfo(void)
{
	return roundup(sizeof(struct bfa_cee_stats), BFA_DMA_ALIGN_SZ);
}

/**
 * bfa_cee_get_attr_isr()
 *
 * @brief CEE ISR for get-attributes responses from f/w
 *
 * @param[in] cee - Pointer to the CEE module
 *            status - Return status from the f/w
 *
 * @return void
 */
static void
bfa_cee_get_attr_isr(struct bfa_cee *cee, enum bfa_status status)
{
	cee->get_attr_status = status;
	if (status == BFA_STATUS_OK) {
		memcpy(cee->attr, cee->attr_dma.kva,
		    sizeof(struct bfa_cee_attr));
		bfa_cee_format_cee_cfg(cee->attr);
	}
	cee->get_attr_pending = false;
	if (cee->cbfn.get_attr_cbfn)
		cee->cbfn.get_attr_cbfn(cee->cbfn.get_attr_cbarg, status);
}

/**
 * bfa_cee_get_attr_isr()
 *
 * @brief CEE ISR for get-stats responses from f/w
 *
 * @param[in] cee - Pointer to the CEE module
 *            status - Return status from the f/w
 *
 * @return void
 */
static void
bfa_cee_get_stats_isr(struct bfa_cee *cee, enum bfa_status status)
{
	cee->get_stats_status = status;
	if (status == BFA_STATUS_OK) {
		memcpy(cee->stats, cee->stats_dma.kva,
			sizeof(struct bfa_cee_stats));
		bfa_cee_stats_swap(cee->stats);
	}
	cee->get_stats_pending = false;
	if (cee->cbfn.get_stats_cbfn)
		cee->cbfn.get_stats_cbfn(cee->cbfn.get_stats_cbarg, status);
}

/**
 * bfa_cee_get_attr_isr()
 *
 * @brief CEE ISR for reset-stats responses from f/w
 *
 * @param[in] cee - Pointer to the CEE module
 *            status - Return status from the f/w
 *
 * @return void
 */
static void
bfa_cee_reset_stats_isr(struct bfa_cee *cee, enum bfa_status status)
{
	cee->reset_stats_status = status;
	cee->reset_stats_pending = false;
	if (cee->cbfn.reset_stats_cbfn)
		cee->cbfn.reset_stats_cbfn(cee->cbfn.reset_stats_cbarg, status);
}
/**
 * bfa_nw_cee_meminfo()
 *
 * @brief Returns the size of the DMA memory needed by CEE module
 *
 * @param[in] void
 *
 * @return Size of DMA region
 */
u32
bfa_nw_cee_meminfo(void)
{
	return bfa_cee_attr_meminfo() + bfa_cee_stats_meminfo();
}

/**
 * bfa_nw_cee_mem_claim()
 *
 * @brief Initialized CEE DMA Memory
 *
 * @param[in] cee CEE module pointer
 *	      dma_kva Kernel Virtual Address of CEE DMA Memory
 *	      dma_pa  Physical Address of CEE DMA Memory
 *
 * @return void
 */
void
bfa_nw_cee_mem_claim(struct bfa_cee *cee, u8 *dma_kva, u64 dma_pa)
{
	cee->attr_dma.kva = dma_kva;
	cee->attr_dma.pa = dma_pa;
	cee->stats_dma.kva = dma_kva + bfa_cee_attr_meminfo();
	cee->stats_dma.pa = dma_pa + bfa_cee_attr_meminfo();
	cee->attr = (struct bfa_cee_attr *) dma_kva;
	cee->stats = (struct bfa_cee_stats *)
		(dma_kva + bfa_cee_attr_meminfo());
}

/**
 * bfa_cee_get_attr()
 *
 * @brief	Send the request to the f/w to fetch CEE attributes.
 *
 * @param[in]	Pointer to the CEE module data structure.
 *
 * @return	Status
 */
enum bfa_status
bfa_nw_cee_get_attr(struct bfa_cee *cee, struct bfa_cee_attr *attr,
		    bfa_cee_get_attr_cbfn_t cbfn, void *cbarg)
{
	struct bfi_cee_get_req *cmd;

	BUG_ON(!((cee != NULL) && (cee->ioc != NULL)));
	if (!bfa_nw_ioc_is_operational(cee->ioc))
		return BFA_STATUS_IOC_FAILURE;

	if (cee->get_attr_pending)
		return  BFA_STATUS_DEVBUSY;

	cee->get_attr_pending = true;
	cmd = (struct bfi_cee_get_req *) cee->get_cfg_mb.msg;
	cee->attr = attr;
	cee->cbfn.get_attr_cbfn = cbfn;
	cee->cbfn.get_attr_cbarg = cbarg;
	bfi_h2i_set(cmd->mh, BFI_MC_CEE, BFI_CEE_H2I_GET_CFG_REQ,
		    bfa_ioc_portid(cee->ioc));
	bfa_dma_be_addr_set(cmd->dma_addr, cee->attr_dma.pa);
	bfa_nw_ioc_mbox_queue(cee->ioc, &cee->get_cfg_mb, NULL, NULL);

	return BFA_STATUS_OK;
}

/**
 * bfa_cee_isrs()
 *
 * @brief Handles Mail-box interrupts for CEE module.
 *
 * @param[in] Pointer to the CEE module data structure.
 *
 * @return void
 */

static void
bfa_cee_isr(void *cbarg, struct bfi_mbmsg *m)
{
	union bfi_cee_i2h_msg_u *msg;
	struct bfi_cee_get_rsp *get_rsp;
	struct bfa_cee *cee = (struct bfa_cee *) cbarg;
	msg = (union bfi_cee_i2h_msg_u *) m;
	get_rsp = (struct bfi_cee_get_rsp *) m;
	switch (msg->mh.msg_id) {
	case BFI_CEE_I2H_GET_CFG_RSP:
		bfa_cee_get_attr_isr(cee, get_rsp->cmd_status);
		break;
	case BFI_CEE_I2H_GET_STATS_RSP:
		bfa_cee_get_stats_isr(cee, get_rsp->cmd_status);
		break;
	case BFI_CEE_I2H_RESET_STATS_RSP:
		bfa_cee_reset_stats_isr(cee, get_rsp->cmd_status);
		break;
	default:
		BUG_ON(1);
	}
}

/**
 * bfa_cee_notify()
 *
 * @brief CEE module heart-beat failure handler.
 * @brief CEE module IOC event handler.
 *
 * @param[in] IOC event type
 *
 * @return void
 */

static void
bfa_cee_notify(void *arg, enum bfa_ioc_event event)
{
	struct bfa_cee *cee;
	cee = (struct bfa_cee *) arg;

	switch (event) {
	case BFA_IOC_E_DISABLED:
	case BFA_IOC_E_FAILED:
		if (cee->get_attr_pending) {
			cee->get_attr_status = BFA_STATUS_FAILED;
			cee->get_attr_pending  = false;
			if (cee->cbfn.get_attr_cbfn) {
				cee->cbfn.get_attr_cbfn(
					cee->cbfn.get_attr_cbarg,
					BFA_STATUS_FAILED);
			}
		}
		if (cee->get_stats_pending) {
			cee->get_stats_status = BFA_STATUS_FAILED;
			cee->get_stats_pending  = false;
			if (cee->cbfn.get_stats_cbfn) {
				cee->cbfn.get_stats_cbfn(
					cee->cbfn.get_stats_cbarg,
					BFA_STATUS_FAILED);
			}
		}
		if (cee->reset_stats_pending) {
			cee->reset_stats_status = BFA_STATUS_FAILED;
			cee->reset_stats_pending  = false;
			if (cee->cbfn.reset_stats_cbfn) {
				cee->cbfn.reset_stats_cbfn(
					cee->cbfn.reset_stats_cbarg,
					BFA_STATUS_FAILED);
			}
		}
		break;

	default:
		break;
	}
}

/**
 * bfa_nw_cee_attach()
 *
 * @brief CEE module-attach API
 *
 * @param[in] cee - Pointer to the CEE module data structure
 *            ioc - Pointer to the ioc module data structure
 *            dev - Pointer to the device driver module data structure
 *                  The device driver specific mbox ISR functions have
 *                  this pointer as one of the parameters.
 *
 * @return void
 */
void
bfa_nw_cee_attach(struct bfa_cee *cee, struct bfa_ioc *ioc,
		void *dev)
{
	BUG_ON(!(cee != NULL));
	cee->dev = dev;
	cee->ioc = ioc;

	bfa_nw_ioc_mbox_regisr(cee->ioc, BFI_MC_CEE, bfa_cee_isr, cee);
	bfa_q_qe_init(&cee->ioc_notify);
	bfa_ioc_notify_init(&cee->ioc_notify, bfa_cee_notify, cee);
	bfa_nw_ioc_notify_register(cee->ioc, &cee->ioc_notify);
}
