/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014- QLogic Corporation.
 * All rights reserved
 * www.qlogic.com
 *
 * Linux driver for QLogic BR-series Fibre Channel Host Bus Adapter.
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

#include "bfad_drv.h"
#include "bfa_defs_svc.h"
#include "bfa_port.h"
#include "bfi.h"
#include "bfa_ioc.h"


BFA_TRC_FILE(CNA, PORT);

static void
bfa_port_stats_swap(struct bfa_port_s *port, union bfa_port_stats_u *stats)
{
	u32    *dip = (u32 *) stats;
	__be32    t0, t1;
	int	    i;

	for (i = 0; i < sizeof(union bfa_port_stats_u)/sizeof(u32);
		i += 2) {
		t0 = dip[i];
		t1 = dip[i + 1];
#ifdef __BIG_ENDIAN
		dip[i] = be32_to_cpu(t0);
		dip[i + 1] = be32_to_cpu(t1);
#else
		dip[i] = be32_to_cpu(t1);
		dip[i + 1] = be32_to_cpu(t0);
#endif
	}
}

/*
 * bfa_port_enable_isr()
 *
 *
 * @param[in] port - Pointer to the port module
 *            status - Return status from the f/w
 *
 * @return void
 */
static void
bfa_port_enable_isr(struct bfa_port_s *port, bfa_status_t status)
{
	bfa_trc(port, status);
	port->endis_pending = BFA_FALSE;
	port->endis_cbfn(port->endis_cbarg, status);
}

/*
 * bfa_port_disable_isr()
 *
 *
 * @param[in] port - Pointer to the port module
 *            status - Return status from the f/w
 *
 * @return void
 */
static void
bfa_port_disable_isr(struct bfa_port_s *port, bfa_status_t status)
{
	bfa_trc(port, status);
	port->endis_pending = BFA_FALSE;
	port->endis_cbfn(port->endis_cbarg, status);
}

/*
 * bfa_port_get_stats_isr()
 *
 *
 * @param[in] port - Pointer to the Port module
 *            status - Return status from the f/w
 *
 * @return void
 */
static void
bfa_port_get_stats_isr(struct bfa_port_s *port, bfa_status_t status)
{
	port->stats_status = status;
	port->stats_busy = BFA_FALSE;

	if (status == BFA_STATUS_OK) {
		struct timeval tv;

		memcpy(port->stats, port->stats_dma.kva,
		       sizeof(union bfa_port_stats_u));
		bfa_port_stats_swap(port, port->stats);

		do_gettimeofday(&tv);
		port->stats->fc.secs_reset = tv.tv_sec - port->stats_reset_time;
	}

	if (port->stats_cbfn) {
		port->stats_cbfn(port->stats_cbarg, status);
		port->stats_cbfn = NULL;
	}
}

/*
 * bfa_port_clear_stats_isr()
 *
 *
 * @param[in] port - Pointer to the Port module
 *            status - Return status from the f/w
 *
 * @return void
 */
static void
bfa_port_clear_stats_isr(struct bfa_port_s *port, bfa_status_t status)
{
	struct timeval tv;

	port->stats_status = status;
	port->stats_busy   = BFA_FALSE;

	/*
	* re-initialize time stamp for stats reset
	*/
	do_gettimeofday(&tv);
	port->stats_reset_time = tv.tv_sec;

	if (port->stats_cbfn) {
		port->stats_cbfn(port->stats_cbarg, status);
		port->stats_cbfn = NULL;
	}
}

/*
 * bfa_port_isr()
 *
 *
 * @param[in] Pointer to the Port module data structure.
 *
 * @return void
 */
static void
bfa_port_isr(void *cbarg, struct bfi_mbmsg_s *m)
{
	struct bfa_port_s *port = (struct bfa_port_s *) cbarg;
	union bfi_port_i2h_msg_u *i2hmsg;

	i2hmsg = (union bfi_port_i2h_msg_u *) m;
	bfa_trc(port, m->mh.msg_id);

	switch (m->mh.msg_id) {
	case BFI_PORT_I2H_ENABLE_RSP:
		if (port->endis_pending == BFA_FALSE)
			break;
		bfa_port_enable_isr(port, i2hmsg->enable_rsp.status);
		break;

	case BFI_PORT_I2H_DISABLE_RSP:
		if (port->endis_pending == BFA_FALSE)
			break;
		bfa_port_disable_isr(port, i2hmsg->disable_rsp.status);
		break;

	case BFI_PORT_I2H_GET_STATS_RSP:
		/* Stats busy flag is still set? (may be cmd timed out) */
		if (port->stats_busy == BFA_FALSE)
			break;
		bfa_port_get_stats_isr(port, i2hmsg->getstats_rsp.status);
		break;

	case BFI_PORT_I2H_CLEAR_STATS_RSP:
		if (port->stats_busy == BFA_FALSE)
			break;
		bfa_port_clear_stats_isr(port, i2hmsg->clearstats_rsp.status);
		break;

	default:
		WARN_ON(1);
	}
}

/*
 * bfa_port_meminfo()
 *
 *
 * @param[in] void
 *
 * @return Size of DMA region
 */
u32
bfa_port_meminfo(void)
{
	return BFA_ROUNDUP(sizeof(union bfa_port_stats_u), BFA_DMA_ALIGN_SZ);
}

/*
 * bfa_port_mem_claim()
 *
 *
 * @param[in] port Port module pointer
 *	      dma_kva Kernel Virtual Address of Port DMA Memory
 *	      dma_pa  Physical Address of Port DMA Memory
 *
 * @return void
 */
void
bfa_port_mem_claim(struct bfa_port_s *port, u8 *dma_kva, u64 dma_pa)
{
	port->stats_dma.kva = dma_kva;
	port->stats_dma.pa  = dma_pa;
}

/*
 * bfa_port_enable()
 *
 *   Send the Port enable request to the f/w
 *
 * @param[in] Pointer to the Port module data structure.
 *
 * @return Status
 */
bfa_status_t
bfa_port_enable(struct bfa_port_s *port, bfa_port_endis_cbfn_t cbfn,
		 void *cbarg)
{
	struct bfi_port_generic_req_s *m;

	/* If port is PBC disabled, return error */
	if (port->pbc_disabled) {
		bfa_trc(port, BFA_STATUS_PBC);
		return BFA_STATUS_PBC;
	}

	if (bfa_ioc_is_disabled(port->ioc)) {
		bfa_trc(port, BFA_STATUS_IOC_DISABLED);
		return BFA_STATUS_IOC_DISABLED;
	}

	if (!bfa_ioc_is_operational(port->ioc)) {
		bfa_trc(port, BFA_STATUS_IOC_FAILURE);
		return BFA_STATUS_IOC_FAILURE;
	}

	/* if port is d-port enabled, return error */
	if (port->dport_enabled) {
		bfa_trc(port, BFA_STATUS_DPORT_ERR);
		return BFA_STATUS_DPORT_ERR;
	}

	if (port->endis_pending) {
		bfa_trc(port, BFA_STATUS_DEVBUSY);
		return BFA_STATUS_DEVBUSY;
	}

	m = (struct bfi_port_generic_req_s *) port->endis_mb.msg;

	port->msgtag++;
	port->endis_cbfn    = cbfn;
	port->endis_cbarg   = cbarg;
	port->endis_pending = BFA_TRUE;

	bfi_h2i_set(m->mh, BFI_MC_PORT, BFI_PORT_H2I_ENABLE_REQ,
		    bfa_ioc_portid(port->ioc));
	bfa_ioc_mbox_queue(port->ioc, &port->endis_mb);

	return BFA_STATUS_OK;
}

/*
 * bfa_port_disable()
 *
 *   Send the Port disable request to the f/w
 *
 * @param[in] Pointer to the Port module data structure.
 *
 * @return Status
 */
bfa_status_t
bfa_port_disable(struct bfa_port_s *port, bfa_port_endis_cbfn_t cbfn,
		  void *cbarg)
{
	struct bfi_port_generic_req_s *m;

	/* If port is PBC disabled, return error */
	if (port->pbc_disabled) {
		bfa_trc(port, BFA_STATUS_PBC);
		return BFA_STATUS_PBC;
	}

	if (bfa_ioc_is_disabled(port->ioc)) {
		bfa_trc(port, BFA_STATUS_IOC_DISABLED);
		return BFA_STATUS_IOC_DISABLED;
	}

	if (!bfa_ioc_is_operational(port->ioc)) {
		bfa_trc(port, BFA_STATUS_IOC_FAILURE);
		return BFA_STATUS_IOC_FAILURE;
	}

	/* if port is d-port enabled, return error */
	if (port->dport_enabled) {
		bfa_trc(port, BFA_STATUS_DPORT_ERR);
		return BFA_STATUS_DPORT_ERR;
	}

	if (port->endis_pending) {
		bfa_trc(port, BFA_STATUS_DEVBUSY);
		return BFA_STATUS_DEVBUSY;
	}

	m = (struct bfi_port_generic_req_s *) port->endis_mb.msg;

	port->msgtag++;
	port->endis_cbfn    = cbfn;
	port->endis_cbarg   = cbarg;
	port->endis_pending = BFA_TRUE;

	bfi_h2i_set(m->mh, BFI_MC_PORT, BFI_PORT_H2I_DISABLE_REQ,
		    bfa_ioc_portid(port->ioc));
	bfa_ioc_mbox_queue(port->ioc, &port->endis_mb);

	return BFA_STATUS_OK;
}

/*
 * bfa_port_get_stats()
 *
 *   Send the request to the f/w to fetch Port statistics.
 *
 * @param[in] Pointer to the Port module data structure.
 *
 * @return Status
 */
bfa_status_t
bfa_port_get_stats(struct bfa_port_s *port, union bfa_port_stats_u *stats,
		    bfa_port_stats_cbfn_t cbfn, void *cbarg)
{
	struct bfi_port_get_stats_req_s *m;

	if (!bfa_ioc_is_operational(port->ioc)) {
		bfa_trc(port, BFA_STATUS_IOC_FAILURE);
		return BFA_STATUS_IOC_FAILURE;
	}

	if (port->stats_busy) {
		bfa_trc(port, BFA_STATUS_DEVBUSY);
		return BFA_STATUS_DEVBUSY;
	}

	m = (struct bfi_port_get_stats_req_s *) port->stats_mb.msg;

	port->stats	  = stats;
	port->stats_cbfn  = cbfn;
	port->stats_cbarg = cbarg;
	port->stats_busy  = BFA_TRUE;
	bfa_dma_be_addr_set(m->dma_addr, port->stats_dma.pa);

	bfi_h2i_set(m->mh, BFI_MC_PORT, BFI_PORT_H2I_GET_STATS_REQ,
		    bfa_ioc_portid(port->ioc));
	bfa_ioc_mbox_queue(port->ioc, &port->stats_mb);

	return BFA_STATUS_OK;
}

/*
 * bfa_port_clear_stats()
 *
 *
 * @param[in] Pointer to the Port module data structure.
 *
 * @return Status
 */
bfa_status_t
bfa_port_clear_stats(struct bfa_port_s *port, bfa_port_stats_cbfn_t cbfn,
		      void *cbarg)
{
	struct bfi_port_generic_req_s *m;

	if (!bfa_ioc_is_operational(port->ioc)) {
		bfa_trc(port, BFA_STATUS_IOC_FAILURE);
		return BFA_STATUS_IOC_FAILURE;
	}

	if (port->stats_busy) {
		bfa_trc(port, BFA_STATUS_DEVBUSY);
		return BFA_STATUS_DEVBUSY;
	}

	m = (struct bfi_port_generic_req_s *) port->stats_mb.msg;

	port->stats_cbfn  = cbfn;
	port->stats_cbarg = cbarg;
	port->stats_busy  = BFA_TRUE;

	bfi_h2i_set(m->mh, BFI_MC_PORT, BFI_PORT_H2I_CLEAR_STATS_REQ,
		    bfa_ioc_portid(port->ioc));
	bfa_ioc_mbox_queue(port->ioc, &port->stats_mb);

	return BFA_STATUS_OK;
}

/*
 * bfa_port_notify()
 *
 * Port module IOC event handler
 *
 * @param[in] Pointer to the Port module data structure.
 * @param[in] IOC event structure
 *
 * @return void
 */
void
bfa_port_notify(void *arg, enum bfa_ioc_event_e event)
{
	struct bfa_port_s *port = (struct bfa_port_s *) arg;

	switch (event) {
	case BFA_IOC_E_DISABLED:
	case BFA_IOC_E_FAILED:
		/* Fail any pending get_stats/clear_stats requests */
		if (port->stats_busy) {
			if (port->stats_cbfn)
				port->stats_cbfn(port->stats_cbarg,
						BFA_STATUS_FAILED);
			port->stats_cbfn = NULL;
			port->stats_busy = BFA_FALSE;
		}

		/* Clear any enable/disable is pending */
		if (port->endis_pending) {
			if (port->endis_cbfn)
				port->endis_cbfn(port->endis_cbarg,
						BFA_STATUS_FAILED);
			port->endis_cbfn = NULL;
			port->endis_pending = BFA_FALSE;
		}

		/* clear D-port mode */
		if (port->dport_enabled)
			bfa_port_set_dportenabled(port, BFA_FALSE);
		break;
	default:
		break;
	}
}

/*
 * bfa_port_attach()
 *
 *
 * @param[in] port - Pointer to the Port module data structure
 *            ioc  - Pointer to the ioc module data structure
 *            dev  - Pointer to the device driver module data structure
 *                   The device driver specific mbox ISR functions have
 *                   this pointer as one of the parameters.
 *            trcmod -
 *
 * @return void
 */
void
bfa_port_attach(struct bfa_port_s *port, struct bfa_ioc_s *ioc,
		 void *dev, struct bfa_trc_mod_s *trcmod)
{
	struct timeval tv;

	WARN_ON(!port);

	port->dev    = dev;
	port->ioc    = ioc;
	port->trcmod = trcmod;

	port->stats_busy = BFA_FALSE;
	port->endis_pending = BFA_FALSE;
	port->stats_cbfn = NULL;
	port->endis_cbfn = NULL;
	port->pbc_disabled = BFA_FALSE;
	port->dport_enabled = BFA_FALSE;

	bfa_ioc_mbox_regisr(port->ioc, BFI_MC_PORT, bfa_port_isr, port);
	bfa_q_qe_init(&port->ioc_notify);
	bfa_ioc_notify_init(&port->ioc_notify, bfa_port_notify, port);
	list_add_tail(&port->ioc_notify.qe, &port->ioc->notify_q);

	/*
	 * initialize time stamp for stats reset
	 */
	do_gettimeofday(&tv);
	port->stats_reset_time = tv.tv_sec;

	bfa_trc(port, 0);
}

/*
 * bfa_port_set_dportenabled();
 *
 * Port module- set pbc disabled flag
 *
 * @param[in] port - Pointer to the Port module data structure
 *
 * @return void
 */
void
bfa_port_set_dportenabled(struct bfa_port_s *port, bfa_boolean_t enabled)
{
	port->dport_enabled = enabled;
}

/*
 *	CEE module specific definitions
 */

/*
 * bfa_cee_get_attr_isr()
 *
 * @brief CEE ISR for get-attributes responses from f/w
 *
 * @param[in] cee - Pointer to the CEE module
 *		    status - Return status from the f/w
 *
 * @return void
 */
static void
bfa_cee_get_attr_isr(struct bfa_cee_s *cee, bfa_status_t status)
{
	struct bfa_cee_lldp_cfg_s *lldp_cfg = &cee->attr->lldp_remote;

	cee->get_attr_status = status;
	bfa_trc(cee, 0);
	if (status == BFA_STATUS_OK) {
		bfa_trc(cee, 0);
		memcpy(cee->attr, cee->attr_dma.kva,
			sizeof(struct bfa_cee_attr_s));
		lldp_cfg->time_to_live = be16_to_cpu(lldp_cfg->time_to_live);
		lldp_cfg->enabled_system_cap =
				be16_to_cpu(lldp_cfg->enabled_system_cap);
	}
	cee->get_attr_pending = BFA_FALSE;
	if (cee->cbfn.get_attr_cbfn) {
		bfa_trc(cee, 0);
		cee->cbfn.get_attr_cbfn(cee->cbfn.get_attr_cbarg, status);
	}
}

/*
 * bfa_cee_get_stats_isr()
 *
 * @brief CEE ISR for get-stats responses from f/w
 *
 * @param[in] cee - Pointer to the CEE module
 *	      status - Return status from the f/w
 *
 * @return void
 */
static void
bfa_cee_get_stats_isr(struct bfa_cee_s *cee, bfa_status_t status)
{
	u32 *buffer;
	int i;

	cee->get_stats_status = status;
	bfa_trc(cee, 0);
	if (status == BFA_STATUS_OK) {
		bfa_trc(cee, 0);
		memcpy(cee->stats, cee->stats_dma.kva,
			sizeof(struct bfa_cee_stats_s));
		/* swap the cee stats */
		buffer = (u32 *)cee->stats;
		for (i = 0; i < (sizeof(struct bfa_cee_stats_s) /
				 sizeof(u32)); i++)
			buffer[i] = cpu_to_be32(buffer[i]);
	}
	cee->get_stats_pending = BFA_FALSE;
	bfa_trc(cee, 0);
	if (cee->cbfn.get_stats_cbfn) {
		bfa_trc(cee, 0);
		cee->cbfn.get_stats_cbfn(cee->cbfn.get_stats_cbarg, status);
	}
}

/*
 * bfa_cee_reset_stats_isr()
 *
 * @brief CEE ISR for reset-stats responses from f/w
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

/*
 * bfa_cee_meminfo()
 *
 * @brief Returns the size of the DMA memory needed by CEE module
 *
 * @param[in] void
 *
 * @return Size of DMA region
 */
u32
bfa_cee_meminfo(void)
{
	return BFA_ROUNDUP(sizeof(struct bfa_cee_attr_s), BFA_DMA_ALIGN_SZ) +
		BFA_ROUNDUP(sizeof(struct bfa_cee_stats_s), BFA_DMA_ALIGN_SZ);
}

/*
 * bfa_cee_mem_claim()
 *
 * @brief Initialized CEE DMA Memory
 *
 * @param[in] cee CEE module pointer
 *            dma_kva Kernel Virtual Address of CEE DMA Memory
 *            dma_pa  Physical Address of CEE DMA Memory
 *
 * @return void
 */
void
bfa_cee_mem_claim(struct bfa_cee_s *cee, u8 *dma_kva, u64 dma_pa)
{
	cee->attr_dma.kva = dma_kva;
	cee->attr_dma.pa = dma_pa;
	cee->stats_dma.kva = dma_kva + BFA_ROUNDUP(
			     sizeof(struct bfa_cee_attr_s), BFA_DMA_ALIGN_SZ);
	cee->stats_dma.pa = dma_pa + BFA_ROUNDUP(
			     sizeof(struct bfa_cee_attr_s), BFA_DMA_ALIGN_SZ);
	cee->attr = (struct bfa_cee_attr_s *) dma_kva;
	cee->stats = (struct bfa_cee_stats_s *) (dma_kva + BFA_ROUNDUP(
			sizeof(struct bfa_cee_attr_s), BFA_DMA_ALIGN_SZ));
}

/*
 * bfa_cee_get_attr()
 *
 * @brief
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

	WARN_ON((cee == NULL) || (cee->ioc == NULL));
	bfa_trc(cee, 0);
	if (!bfa_ioc_is_operational(cee->ioc)) {
		bfa_trc(cee, 0);
		return BFA_STATUS_IOC_FAILURE;
	}
	if (cee->get_attr_pending == BFA_TRUE) {
		bfa_trc(cee, 0);
		return  BFA_STATUS_DEVBUSY;
	}
	cee->get_attr_pending = BFA_TRUE;
	cmd = (struct bfi_cee_get_req_s *) cee->get_cfg_mb.msg;
	cee->attr = attr;
	cee->cbfn.get_attr_cbfn = cbfn;
	cee->cbfn.get_attr_cbarg = cbarg;
	bfi_h2i_set(cmd->mh, BFI_MC_CEE, BFI_CEE_H2I_GET_CFG_REQ,
		bfa_ioc_portid(cee->ioc));
	bfa_dma_be_addr_set(cmd->dma_addr, cee->attr_dma.pa);
	bfa_ioc_mbox_queue(cee->ioc, &cee->get_cfg_mb);

	return BFA_STATUS_OK;
}

/*
 * bfa_cee_get_stats()
 *
 * @brief
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

	WARN_ON((cee == NULL) || (cee->ioc == NULL));

	if (!bfa_ioc_is_operational(cee->ioc)) {
		bfa_trc(cee, 0);
		return BFA_STATUS_IOC_FAILURE;
	}
	if (cee->get_stats_pending == BFA_TRUE) {
		bfa_trc(cee, 0);
		return  BFA_STATUS_DEVBUSY;
	}
	cee->get_stats_pending = BFA_TRUE;
	cmd = (struct bfi_cee_get_req_s *) cee->get_stats_mb.msg;
	cee->stats = stats;
	cee->cbfn.get_stats_cbfn = cbfn;
	cee->cbfn.get_stats_cbarg = cbarg;
	bfi_h2i_set(cmd->mh, BFI_MC_CEE, BFI_CEE_H2I_GET_STATS_REQ,
		bfa_ioc_portid(cee->ioc));
	bfa_dma_be_addr_set(cmd->dma_addr, cee->stats_dma.pa);
	bfa_ioc_mbox_queue(cee->ioc, &cee->get_stats_mb);

	return BFA_STATUS_OK;
}

/*
 * bfa_cee_reset_stats()
 *
 * @brief Clears CEE Stats in the f/w.
 *
 * @param[in] Pointer to the CEE module data structure.
 *
 * @return Status
 */

bfa_status_t
bfa_cee_reset_stats(struct bfa_cee_s *cee,
		    bfa_cee_reset_stats_cbfn_t cbfn, void *cbarg)
{
	struct bfi_cee_reset_stats_s *cmd;

	WARN_ON((cee == NULL) || (cee->ioc == NULL));
	if (!bfa_ioc_is_operational(cee->ioc)) {
		bfa_trc(cee, 0);
		return BFA_STATUS_IOC_FAILURE;
	}
	if (cee->reset_stats_pending == BFA_TRUE) {
		bfa_trc(cee, 0);
		return  BFA_STATUS_DEVBUSY;
	}
	cee->reset_stats_pending = BFA_TRUE;
	cmd = (struct bfi_cee_reset_stats_s *) cee->reset_stats_mb.msg;
	cee->cbfn.reset_stats_cbfn = cbfn;
	cee->cbfn.reset_stats_cbarg = cbarg;
	bfi_h2i_set(cmd->mh, BFI_MC_CEE, BFI_CEE_H2I_RESET_STATS,
		bfa_ioc_portid(cee->ioc));
	bfa_ioc_mbox_queue(cee->ioc, &cee->reset_stats_mb);

	return BFA_STATUS_OK;
}

/*
 * bfa_cee_isrs()
 *
 * @brief Handles Mail-box interrupts for CEE module.
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
	struct bfa_cee_s *cee = (struct bfa_cee_s *) cbarg;
	msg = (union bfi_cee_i2h_msg_u *) m;
	get_rsp = (struct bfi_cee_get_rsp_s *) m;
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
		WARN_ON(1);
	}
}

/*
 * bfa_cee_notify()
 *
 * @brief CEE module IOC event handler.
 *
 * @param[in] Pointer to the CEE module data structure.
 * @param[in] IOC event type
 *
 * @return void
 */

void
bfa_cee_notify(void *arg, enum bfa_ioc_event_e event)
{
	struct bfa_cee_s *cee = (struct bfa_cee_s *) arg;

	bfa_trc(cee, event);

	switch (event) {
	case BFA_IOC_E_DISABLED:
	case BFA_IOC_E_FAILED:
		if (cee->get_attr_pending == BFA_TRUE) {
			cee->get_attr_status = BFA_STATUS_FAILED;
			cee->get_attr_pending  = BFA_FALSE;
			if (cee->cbfn.get_attr_cbfn) {
				cee->cbfn.get_attr_cbfn(
					cee->cbfn.get_attr_cbarg,
					BFA_STATUS_FAILED);
			}
		}
		if (cee->get_stats_pending == BFA_TRUE) {
			cee->get_stats_status = BFA_STATUS_FAILED;
			cee->get_stats_pending  = BFA_FALSE;
			if (cee->cbfn.get_stats_cbfn) {
				cee->cbfn.get_stats_cbfn(
				cee->cbfn.get_stats_cbarg,
				BFA_STATUS_FAILED);
			}
		}
		if (cee->reset_stats_pending == BFA_TRUE) {
			cee->reset_stats_status = BFA_STATUS_FAILED;
			cee->reset_stats_pending  = BFA_FALSE;
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

/*
 * bfa_cee_attach()
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
bfa_cee_attach(struct bfa_cee_s *cee, struct bfa_ioc_s *ioc,
		void *dev)
{
	WARN_ON(cee == NULL);
	cee->dev = dev;
	cee->ioc = ioc;

	bfa_ioc_mbox_regisr(cee->ioc, BFI_MC_CEE, bfa_cee_isr, cee);
	bfa_q_qe_init(&cee->ioc_notify);
	bfa_ioc_notify_init(&cee->ioc_notify, bfa_cee_notify, cee);
	list_add_tail(&cee->ioc_notify.qe, &cee->ioc->notify_q);
}
