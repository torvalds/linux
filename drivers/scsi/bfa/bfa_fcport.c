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
#include <bfa_svc.h>
#include <bfi/bfi_pport.h>
#include <cs/bfa_debug.h>
#include <aen/bfa_aen.h>
#include <cs/bfa_plog.h>
#include <aen/bfa_aen_port.h>

BFA_TRC_FILE(HAL, PPORT);
BFA_MODULE(pport);

#define bfa_pport_callback(__pport, __event) do {			\
	if ((__pport)->bfa->fcs) {      \
		(__pport)->event_cbfn((__pport)->event_cbarg, (__event));      \
	} else {							\
		(__pport)->hcb_event = (__event);      \
		bfa_cb_queue((__pport)->bfa, &(__pport)->hcb_qe,	\
		__bfa_cb_port_event, (__pport));      \
	}								\
} while (0)

/*
 * The port is considered disabled if corresponding physical port or IOC are
 * disabled explicitly
 */
#define BFA_PORT_IS_DISABLED(bfa) \
	((bfa_pport_is_disabled(bfa) == BFA_TRUE) || \
	(bfa_ioc_is_disabled(&bfa->ioc) == BFA_TRUE))

/*
 * forward declarations
 */
static bfa_boolean_t bfa_pport_send_enable(struct bfa_pport_s *port);
static bfa_boolean_t bfa_pport_send_disable(struct bfa_pport_s *port);
static void     bfa_pport_update_linkinfo(struct bfa_pport_s *pport);
static void     bfa_pport_reset_linkinfo(struct bfa_pport_s *pport);
static void     bfa_pport_set_wwns(struct bfa_pport_s *port);
static void     __bfa_cb_port_event(void *cbarg, bfa_boolean_t complete);
static void     __bfa_cb_port_stats(void *cbarg, bfa_boolean_t complete);
static void     __bfa_cb_port_stats_clr(void *cbarg, bfa_boolean_t complete);
static void     bfa_port_stats_timeout(void *cbarg);
static void     bfa_port_stats_clr_timeout(void *cbarg);

/**
 *  bfa_pport_private
 */

/**
 * BFA port state machine events
 */
enum bfa_pport_sm_event {
	BFA_PPORT_SM_START = 1,	/*  start port state machine */
	BFA_PPORT_SM_STOP = 2,	/*  stop port state machine */
	BFA_PPORT_SM_ENABLE = 3,	/*  enable port */
	BFA_PPORT_SM_DISABLE = 4,	/*  disable port state machine */
	BFA_PPORT_SM_FWRSP = 5,	/*  firmware enable/disable rsp */
	BFA_PPORT_SM_LINKUP = 6,	/*  firmware linkup event */
	BFA_PPORT_SM_LINKDOWN = 7,	/*  firmware linkup down */
	BFA_PPORT_SM_QRESUME = 8,	/*  CQ space available */
	BFA_PPORT_SM_HWFAIL = 9,	/*  IOC h/w failure */
};

static void     bfa_pport_sm_uninit(struct bfa_pport_s *pport,
				    enum bfa_pport_sm_event event);
static void     bfa_pport_sm_enabling_qwait(struct bfa_pport_s *pport,
					    enum bfa_pport_sm_event event);
static void     bfa_pport_sm_enabling(struct bfa_pport_s *pport,
				      enum bfa_pport_sm_event event);
static void     bfa_pport_sm_linkdown(struct bfa_pport_s *pport,
				      enum bfa_pport_sm_event event);
static void     bfa_pport_sm_linkup(struct bfa_pport_s *pport,
				    enum bfa_pport_sm_event event);
static void     bfa_pport_sm_disabling(struct bfa_pport_s *pport,
				       enum bfa_pport_sm_event event);
static void     bfa_pport_sm_disabling_qwait(struct bfa_pport_s *pport,
					     enum bfa_pport_sm_event event);
static void     bfa_pport_sm_disabled(struct bfa_pport_s *pport,
				      enum bfa_pport_sm_event event);
static void     bfa_pport_sm_stopped(struct bfa_pport_s *pport,
				     enum bfa_pport_sm_event event);
static void     bfa_pport_sm_iocdown(struct bfa_pport_s *pport,
				     enum bfa_pport_sm_event event);
static void     bfa_pport_sm_iocfail(struct bfa_pport_s *pport,
				     enum bfa_pport_sm_event event);

static struct bfa_sm_table_s hal_pport_sm_table[] = {
	{BFA_SM(bfa_pport_sm_uninit), BFA_PPORT_ST_UNINIT},
	{BFA_SM(bfa_pport_sm_enabling_qwait), BFA_PPORT_ST_ENABLING_QWAIT},
	{BFA_SM(bfa_pport_sm_enabling), BFA_PPORT_ST_ENABLING},
	{BFA_SM(bfa_pport_sm_linkdown), BFA_PPORT_ST_LINKDOWN},
	{BFA_SM(bfa_pport_sm_linkup), BFA_PPORT_ST_LINKUP},
	{BFA_SM(bfa_pport_sm_disabling_qwait),
	 BFA_PPORT_ST_DISABLING_QWAIT},
	{BFA_SM(bfa_pport_sm_disabling), BFA_PPORT_ST_DISABLING},
	{BFA_SM(bfa_pport_sm_disabled), BFA_PPORT_ST_DISABLED},
	{BFA_SM(bfa_pport_sm_stopped), BFA_PPORT_ST_STOPPED},
	{BFA_SM(bfa_pport_sm_iocdown), BFA_PPORT_ST_IOCDOWN},
	{BFA_SM(bfa_pport_sm_iocfail), BFA_PPORT_ST_IOCDOWN},
};

static void
bfa_pport_aen_post(struct bfa_pport_s *pport, enum bfa_port_aen_event event)
{
	union bfa_aen_data_u aen_data;
	struct bfa_log_mod_s *logmod = pport->bfa->logm;
	wwn_t           pwwn = pport->pwwn;
	char            pwwn_ptr[BFA_STRING_32];
	struct bfa_ioc_attr_s ioc_attr;

	wwn2str(pwwn_ptr, pwwn);
	switch (event) {
	case BFA_PORT_AEN_ONLINE:
		bfa_log(logmod, BFA_AEN_PORT_ONLINE, pwwn_ptr);
		break;
	case BFA_PORT_AEN_OFFLINE:
		bfa_log(logmod, BFA_AEN_PORT_OFFLINE, pwwn_ptr);
		break;
	case BFA_PORT_AEN_ENABLE:
		bfa_log(logmod, BFA_AEN_PORT_ENABLE, pwwn_ptr);
		break;
	case BFA_PORT_AEN_DISABLE:
		bfa_log(logmod, BFA_AEN_PORT_DISABLE, pwwn_ptr);
		break;
	case BFA_PORT_AEN_DISCONNECT:
		bfa_log(logmod, BFA_AEN_PORT_DISCONNECT, pwwn_ptr);
		break;
	case BFA_PORT_AEN_QOS_NEG:
		bfa_log(logmod, BFA_AEN_PORT_QOS_NEG, pwwn_ptr);
		break;
	default:
		break;
	}

	bfa_ioc_get_attr(&pport->bfa->ioc, &ioc_attr);
	aen_data.port.ioc_type = ioc_attr.ioc_type;
	aen_data.port.pwwn = pwwn;
}

static void
bfa_pport_sm_uninit(struct bfa_pport_s *pport, enum bfa_pport_sm_event event)
{
	bfa_trc(pport->bfa, event);

	switch (event) {
	case BFA_PPORT_SM_START:
		/**
		 * Start event after IOC is configured and BFA is started.
		 */
		if (bfa_pport_send_enable(pport))
			bfa_sm_set_state(pport, bfa_pport_sm_enabling);
		else
			bfa_sm_set_state(pport, bfa_pport_sm_enabling_qwait);
		break;

	case BFA_PPORT_SM_ENABLE:
		/**
		 * Port is persistently configured to be in enabled state. Do
		 * not change state. Port enabling is done when START event is
		 * received.
		 */
		break;

	case BFA_PPORT_SM_DISABLE:
		/**
		 * If a port is persistently configured to be disabled, the
		 * first event will a port disable request.
		 */
		bfa_sm_set_state(pport, bfa_pport_sm_disabled);
		break;

	case BFA_PPORT_SM_HWFAIL:
		bfa_sm_set_state(pport, bfa_pport_sm_iocdown);
		break;

	default:
		bfa_sm_fault(pport->bfa, event);
	}
}

static void
bfa_pport_sm_enabling_qwait(struct bfa_pport_s *pport,
			    enum bfa_pport_sm_event event)
{
	bfa_trc(pport->bfa, event);

	switch (event) {
	case BFA_PPORT_SM_QRESUME:
		bfa_sm_set_state(pport, bfa_pport_sm_enabling);
		bfa_pport_send_enable(pport);
		break;

	case BFA_PPORT_SM_STOP:
		bfa_reqq_wcancel(&pport->reqq_wait);
		bfa_sm_set_state(pport, bfa_pport_sm_stopped);
		break;

	case BFA_PPORT_SM_ENABLE:
		/**
		 * Already enable is in progress.
		 */
		break;

	case BFA_PPORT_SM_DISABLE:
		/**
		 * Just send disable request to firmware when room becomes
		 * available in request queue.
		 */
		bfa_sm_set_state(pport, bfa_pport_sm_disabled);
		bfa_reqq_wcancel(&pport->reqq_wait);
		bfa_plog_str(pport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_DISABLE, 0, "Port Disable");
		bfa_pport_aen_post(pport, BFA_PORT_AEN_DISABLE);
		break;

	case BFA_PPORT_SM_LINKUP:
	case BFA_PPORT_SM_LINKDOWN:
		/**
		 * Possible to get link events when doing back-to-back
		 * enable/disables.
		 */
		break;

	case BFA_PPORT_SM_HWFAIL:
		bfa_reqq_wcancel(&pport->reqq_wait);
		bfa_sm_set_state(pport, bfa_pport_sm_iocdown);
		break;

	default:
		bfa_sm_fault(pport->bfa, event);
	}
}

static void
bfa_pport_sm_enabling(struct bfa_pport_s *pport, enum bfa_pport_sm_event event)
{
	bfa_trc(pport->bfa, event);

	switch (event) {
	case BFA_PPORT_SM_FWRSP:
	case BFA_PPORT_SM_LINKDOWN:
		bfa_sm_set_state(pport, bfa_pport_sm_linkdown);
		break;

	case BFA_PPORT_SM_LINKUP:
		bfa_pport_update_linkinfo(pport);
		bfa_sm_set_state(pport, bfa_pport_sm_linkup);

		bfa_assert(pport->event_cbfn);
		bfa_pport_callback(pport, BFA_PPORT_LINKUP);
		break;

	case BFA_PPORT_SM_ENABLE:
		/**
		 * Already being enabled.
		 */
		break;

	case BFA_PPORT_SM_DISABLE:
		if (bfa_pport_send_disable(pport))
			bfa_sm_set_state(pport, bfa_pport_sm_disabling);
		else
			bfa_sm_set_state(pport, bfa_pport_sm_disabling_qwait);

		bfa_plog_str(pport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_DISABLE, 0, "Port Disable");
		bfa_pport_aen_post(pport, BFA_PORT_AEN_DISABLE);
		break;

	case BFA_PPORT_SM_STOP:
		bfa_sm_set_state(pport, bfa_pport_sm_stopped);
		break;

	case BFA_PPORT_SM_HWFAIL:
		bfa_sm_set_state(pport, bfa_pport_sm_iocdown);
		break;

	default:
		bfa_sm_fault(pport->bfa, event);
	}
}

static void
bfa_pport_sm_linkdown(struct bfa_pport_s *pport, enum bfa_pport_sm_event event)
{
	bfa_trc(pport->bfa, event);

	switch (event) {
	case BFA_PPORT_SM_LINKUP:
		bfa_pport_update_linkinfo(pport);
		bfa_sm_set_state(pport, bfa_pport_sm_linkup);
		bfa_assert(pport->event_cbfn);
		bfa_plog_str(pport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_ST_CHANGE, 0, "Port Linkup");
		bfa_pport_callback(pport, BFA_PPORT_LINKUP);
		bfa_pport_aen_post(pport, BFA_PORT_AEN_ONLINE);
		/**
		 * If QoS is enabled and it is not online,
		 * Send a separate event.
		 */
		if ((pport->cfg.qos_enabled)
		    && (bfa_os_ntohl(pport->qos_attr.state) != BFA_QOS_ONLINE))
			bfa_pport_aen_post(pport, BFA_PORT_AEN_QOS_NEG);

		break;

	case BFA_PPORT_SM_LINKDOWN:
		/**
		 * Possible to get link down event.
		 */
		break;

	case BFA_PPORT_SM_ENABLE:
		/**
		 * Already enabled.
		 */
		break;

	case BFA_PPORT_SM_DISABLE:
		if (bfa_pport_send_disable(pport))
			bfa_sm_set_state(pport, bfa_pport_sm_disabling);
		else
			bfa_sm_set_state(pport, bfa_pport_sm_disabling_qwait);

		bfa_plog_str(pport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_DISABLE, 0, "Port Disable");
		bfa_pport_aen_post(pport, BFA_PORT_AEN_DISABLE);
		break;

	case BFA_PPORT_SM_STOP:
		bfa_sm_set_state(pport, bfa_pport_sm_stopped);
		break;

	case BFA_PPORT_SM_HWFAIL:
		bfa_sm_set_state(pport, bfa_pport_sm_iocdown);
		break;

	default:
		bfa_sm_fault(pport->bfa, event);
	}
}

static void
bfa_pport_sm_linkup(struct bfa_pport_s *pport, enum bfa_pport_sm_event event)
{
	bfa_trc(pport->bfa, event);

	switch (event) {
	case BFA_PPORT_SM_ENABLE:
		/**
		 * Already enabled.
		 */
		break;

	case BFA_PPORT_SM_DISABLE:
		if (bfa_pport_send_disable(pport))
			bfa_sm_set_state(pport, bfa_pport_sm_disabling);
		else
			bfa_sm_set_state(pport, bfa_pport_sm_disabling_qwait);

		bfa_pport_reset_linkinfo(pport);
		bfa_pport_callback(pport, BFA_PPORT_LINKDOWN);
		bfa_plog_str(pport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_DISABLE, 0, "Port Disable");
		bfa_pport_aen_post(pport, BFA_PORT_AEN_OFFLINE);
		bfa_pport_aen_post(pport, BFA_PORT_AEN_DISABLE);
		break;

	case BFA_PPORT_SM_LINKDOWN:
		bfa_sm_set_state(pport, bfa_pport_sm_linkdown);
		bfa_pport_reset_linkinfo(pport);
		bfa_pport_callback(pport, BFA_PPORT_LINKDOWN);
		bfa_plog_str(pport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_ST_CHANGE, 0, "Port Linkdown");
		if (BFA_PORT_IS_DISABLED(pport->bfa)) {
			bfa_pport_aen_post(pport, BFA_PORT_AEN_OFFLINE);
		} else {
			bfa_pport_aen_post(pport, BFA_PORT_AEN_DISCONNECT);
		}
		break;

	case BFA_PPORT_SM_STOP:
		bfa_sm_set_state(pport, bfa_pport_sm_stopped);
		bfa_pport_reset_linkinfo(pport);
		if (BFA_PORT_IS_DISABLED(pport->bfa)) {
			bfa_pport_aen_post(pport, BFA_PORT_AEN_OFFLINE);
		} else {
			bfa_pport_aen_post(pport, BFA_PORT_AEN_DISCONNECT);
		}
		break;

	case BFA_PPORT_SM_HWFAIL:
		bfa_sm_set_state(pport, bfa_pport_sm_iocdown);
		bfa_pport_reset_linkinfo(pport);
		bfa_pport_callback(pport, BFA_PPORT_LINKDOWN);
		if (BFA_PORT_IS_DISABLED(pport->bfa)) {
			bfa_pport_aen_post(pport, BFA_PORT_AEN_OFFLINE);
		} else {
			bfa_pport_aen_post(pport, BFA_PORT_AEN_DISCONNECT);
		}
		break;

	default:
		bfa_sm_fault(pport->bfa, event);
	}
}

static void
bfa_pport_sm_disabling_qwait(struct bfa_pport_s *pport,
			     enum bfa_pport_sm_event event)
{
	bfa_trc(pport->bfa, event);

	switch (event) {
	case BFA_PPORT_SM_QRESUME:
		bfa_sm_set_state(pport, bfa_pport_sm_disabling);
		bfa_pport_send_disable(pport);
		break;

	case BFA_PPORT_SM_STOP:
		bfa_sm_set_state(pport, bfa_pport_sm_stopped);
		bfa_reqq_wcancel(&pport->reqq_wait);
		break;

	case BFA_PPORT_SM_DISABLE:
		/**
		 * Already being disabled.
		 */
		break;

	case BFA_PPORT_SM_LINKUP:
	case BFA_PPORT_SM_LINKDOWN:
		/**
		 * Possible to get link events when doing back-to-back
		 * enable/disables.
		 */
		break;

	case BFA_PPORT_SM_HWFAIL:
		bfa_sm_set_state(pport, bfa_pport_sm_iocfail);
		bfa_reqq_wcancel(&pport->reqq_wait);
		break;

	default:
		bfa_sm_fault(pport->bfa, event);
	}
}

static void
bfa_pport_sm_disabling(struct bfa_pport_s *pport, enum bfa_pport_sm_event event)
{
	bfa_trc(pport->bfa, event);

	switch (event) {
	case BFA_PPORT_SM_FWRSP:
		bfa_sm_set_state(pport, bfa_pport_sm_disabled);
		break;

	case BFA_PPORT_SM_DISABLE:
		/**
		 * Already being disabled.
		 */
		break;

	case BFA_PPORT_SM_ENABLE:
		if (bfa_pport_send_enable(pport))
			bfa_sm_set_state(pport, bfa_pport_sm_enabling);
		else
			bfa_sm_set_state(pport, bfa_pport_sm_enabling_qwait);

		bfa_plog_str(pport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_ENABLE, 0, "Port Enable");
		bfa_pport_aen_post(pport, BFA_PORT_AEN_ENABLE);
		break;

	case BFA_PPORT_SM_STOP:
		bfa_sm_set_state(pport, bfa_pport_sm_stopped);
		break;

	case BFA_PPORT_SM_LINKUP:
	case BFA_PPORT_SM_LINKDOWN:
		/**
		 * Possible to get link events when doing back-to-back
		 * enable/disables.
		 */
		break;

	case BFA_PPORT_SM_HWFAIL:
		bfa_sm_set_state(pport, bfa_pport_sm_iocfail);
		break;

	default:
		bfa_sm_fault(pport->bfa, event);
	}
}

static void
bfa_pport_sm_disabled(struct bfa_pport_s *pport, enum bfa_pport_sm_event event)
{
	bfa_trc(pport->bfa, event);

	switch (event) {
	case BFA_PPORT_SM_START:
		/**
		 * Ignore start event for a port that is disabled.
		 */
		break;

	case BFA_PPORT_SM_STOP:
		bfa_sm_set_state(pport, bfa_pport_sm_stopped);
		break;

	case BFA_PPORT_SM_ENABLE:
		if (bfa_pport_send_enable(pport))
			bfa_sm_set_state(pport, bfa_pport_sm_enabling);
		else
			bfa_sm_set_state(pport, bfa_pport_sm_enabling_qwait);

		bfa_plog_str(pport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_ENABLE, 0, "Port Enable");
		bfa_pport_aen_post(pport, BFA_PORT_AEN_ENABLE);
		break;

	case BFA_PPORT_SM_DISABLE:
		/**
		 * Already disabled.
		 */
		break;

	case BFA_PPORT_SM_HWFAIL:
		bfa_sm_set_state(pport, bfa_pport_sm_iocfail);
		break;

	default:
		bfa_sm_fault(pport->bfa, event);
	}
}

static void
bfa_pport_sm_stopped(struct bfa_pport_s *pport, enum bfa_pport_sm_event event)
{
	bfa_trc(pport->bfa, event);

	switch (event) {
	case BFA_PPORT_SM_START:
		if (bfa_pport_send_enable(pport))
			bfa_sm_set_state(pport, bfa_pport_sm_enabling);
		else
			bfa_sm_set_state(pport, bfa_pport_sm_enabling_qwait);
		break;

	default:
		/**
		 * Ignore all other events.
		 */
		;
	}
}

/**
 * Port is enabled. IOC is down/failed.
 */
static void
bfa_pport_sm_iocdown(struct bfa_pport_s *pport, enum bfa_pport_sm_event event)
{
	bfa_trc(pport->bfa, event);

	switch (event) {
	case BFA_PPORT_SM_START:
		if (bfa_pport_send_enable(pport))
			bfa_sm_set_state(pport, bfa_pport_sm_enabling);
		else
			bfa_sm_set_state(pport, bfa_pport_sm_enabling_qwait);
		break;

	default:
		/**
		 * Ignore all events.
		 */
		;
	}
}

/**
 * Port is disabled. IOC is down/failed.
 */
static void
bfa_pport_sm_iocfail(struct bfa_pport_s *pport, enum bfa_pport_sm_event event)
{
	bfa_trc(pport->bfa, event);

	switch (event) {
	case BFA_PPORT_SM_START:
		bfa_sm_set_state(pport, bfa_pport_sm_disabled);
		break;

	case BFA_PPORT_SM_ENABLE:
		bfa_sm_set_state(pport, bfa_pport_sm_iocdown);
		break;

	default:
		/**
		 * Ignore all events.
		 */
		;
	}
}



/**
 *  bfa_pport_private
 */

static void
__bfa_cb_port_event(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_pport_s *pport = cbarg;

	if (complete)
		pport->event_cbfn(pport->event_cbarg, pport->hcb_event);
}

#define PPORT_STATS_DMA_SZ (BFA_ROUNDUP(sizeof(union bfa_pport_stats_u), \
							BFA_CACHELINE_SZ))

static void
bfa_pport_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *ndm_len,
		  u32 *dm_len)
{
	*dm_len += PPORT_STATS_DMA_SZ;
}

static void
bfa_pport_qresume(void *cbarg)
{
	struct bfa_pport_s *port = cbarg;

	bfa_sm_send_event(port, BFA_PPORT_SM_QRESUME);
}

static void
bfa_pport_mem_claim(struct bfa_pport_s *pport, struct bfa_meminfo_s *meminfo)
{
	u8        *dm_kva;
	u64        dm_pa;

	dm_kva = bfa_meminfo_dma_virt(meminfo);
	dm_pa = bfa_meminfo_dma_phys(meminfo);

	pport->stats_kva = dm_kva;
	pport->stats_pa = dm_pa;
	pport->stats = (union bfa_pport_stats_u *)dm_kva;

	dm_kva += PPORT_STATS_DMA_SZ;
	dm_pa += PPORT_STATS_DMA_SZ;

	bfa_meminfo_dma_virt(meminfo) = dm_kva;
	bfa_meminfo_dma_phys(meminfo) = dm_pa;
}

/**
 * Memory initialization.
 */
static void
bfa_pport_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		 struct bfa_meminfo_s *meminfo, struct bfa_pcidev_s *pcidev)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);
	struct bfa_pport_cfg_s *port_cfg = &pport->cfg;

	bfa_os_memset(pport, 0, sizeof(struct bfa_pport_s));
	pport->bfa = bfa;

	bfa_pport_mem_claim(pport, meminfo);

	bfa_sm_set_state(pport, bfa_pport_sm_uninit);

	/**
	 * initialize and set default configuration
	 */
	port_cfg->topology = BFA_PPORT_TOPOLOGY_P2P;
	port_cfg->speed = BFA_PPORT_SPEED_AUTO;
	port_cfg->trunked = BFA_FALSE;
	port_cfg->maxfrsize = 0;

	port_cfg->trl_def_speed = BFA_PPORT_SPEED_1GBPS;

	bfa_reqq_winit(&pport->reqq_wait, bfa_pport_qresume, pport);
}

static void
bfa_pport_initdone(struct bfa_s *bfa)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	/**
	 * Initialize port attributes from IOC hardware data.
	 */
	bfa_pport_set_wwns(pport);
	if (pport->cfg.maxfrsize == 0)
		pport->cfg.maxfrsize = bfa_ioc_maxfrsize(&bfa->ioc);
	pport->cfg.rx_bbcredit = bfa_ioc_rx_bbcredit(&bfa->ioc);
	pport->speed_sup = bfa_ioc_speed_sup(&bfa->ioc);

	bfa_assert(pport->cfg.maxfrsize);
	bfa_assert(pport->cfg.rx_bbcredit);
	bfa_assert(pport->speed_sup);
}

static void
bfa_pport_detach(struct bfa_s *bfa)
{
}

/**
 * Called when IOC is ready.
 */
static void
bfa_pport_start(struct bfa_s *bfa)
{
	bfa_sm_send_event(BFA_PORT_MOD(bfa), BFA_PPORT_SM_START);
}

/**
 * Called before IOC is stopped.
 */
static void
bfa_pport_stop(struct bfa_s *bfa)
{
	bfa_sm_send_event(BFA_PORT_MOD(bfa), BFA_PPORT_SM_STOP);
}

/**
 * Called when IOC failure is detected.
 */
static void
bfa_pport_iocdisable(struct bfa_s *bfa)
{
	bfa_sm_send_event(BFA_PORT_MOD(bfa), BFA_PPORT_SM_HWFAIL);
}

static void
bfa_pport_update_linkinfo(struct bfa_pport_s *pport)
{
	struct bfi_pport_event_s *pevent = pport->event_arg.i2hmsg.event;

	pport->speed = pevent->link_state.speed;
	pport->topology = pevent->link_state.topology;

	if (pport->topology == BFA_PPORT_TOPOLOGY_LOOP)
		pport->myalpa = pevent->link_state.tl.loop_info.myalpa;

	/*
	 * QoS Details
	 */
	bfa_os_assign(pport->qos_attr, pevent->link_state.qos_attr);
	bfa_os_assign(pport->qos_vc_attr, pevent->link_state.qos_vc_attr);

	bfa_trc(pport->bfa, pport->speed);
	bfa_trc(pport->bfa, pport->topology);
}

static void
bfa_pport_reset_linkinfo(struct bfa_pport_s *pport)
{
	pport->speed = BFA_PPORT_SPEED_UNKNOWN;
	pport->topology = BFA_PPORT_TOPOLOGY_NONE;
}

/**
 * Send port enable message to firmware.
 */
static          bfa_boolean_t
bfa_pport_send_enable(struct bfa_pport_s *port)
{
	struct bfi_pport_enable_req_s *m;

	/**
	 * Increment message tag before queue check, so that responses to old
	 * requests are discarded.
	 */
	port->msgtag++;

	/**
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(port->bfa, BFA_REQQ_PORT);
	if (!m) {
		bfa_reqq_wait(port->bfa, BFA_REQQ_PORT, &port->reqq_wait);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_FC_PORT, BFI_PPORT_H2I_ENABLE_REQ,
		    bfa_lpuid(port->bfa));
	m->nwwn = port->nwwn;
	m->pwwn = port->pwwn;
	m->port_cfg = port->cfg;
	m->msgtag = port->msgtag;
	m->port_cfg.maxfrsize = bfa_os_htons(port->cfg.maxfrsize);
	bfa_dma_be_addr_set(m->stats_dma_addr, port->stats_pa);
	bfa_trc(port->bfa, m->stats_dma_addr.a32.addr_lo);
	bfa_trc(port->bfa, m->stats_dma_addr.a32.addr_hi);

	/**
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(port->bfa, BFA_REQQ_PORT);
	return BFA_TRUE;
}

/**
 * Send port disable message to firmware.
 */
static          bfa_boolean_t
bfa_pport_send_disable(struct bfa_pport_s *port)
{
	bfi_pport_disable_req_t *m;

	/**
	 * Increment message tag before queue check, so that responses to old
	 * requests are discarded.
	 */
	port->msgtag++;

	/**
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(port->bfa, BFA_REQQ_PORT);
	if (!m) {
		bfa_reqq_wait(port->bfa, BFA_REQQ_PORT, &port->reqq_wait);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_FC_PORT, BFI_PPORT_H2I_DISABLE_REQ,
		    bfa_lpuid(port->bfa));
	m->msgtag = port->msgtag;

	/**
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(port->bfa, BFA_REQQ_PORT);

	return BFA_TRUE;
}

static void
bfa_pport_set_wwns(struct bfa_pport_s *port)
{
	port->pwwn = bfa_ioc_get_pwwn(&port->bfa->ioc);
	port->nwwn = bfa_ioc_get_nwwn(&port->bfa->ioc);

	bfa_trc(port->bfa, port->pwwn);
	bfa_trc(port->bfa, port->nwwn);
}

static void
bfa_port_send_txcredit(void *port_cbarg)
{

	struct bfa_pport_s *port = port_cbarg;
	struct bfi_pport_set_svc_params_req_s *m;

	/**
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(port->bfa, BFA_REQQ_PORT);
	if (!m) {
		bfa_trc(port->bfa, port->cfg.tx_bbcredit);
		return;
	}

	bfi_h2i_set(m->mh, BFI_MC_FC_PORT, BFI_PPORT_H2I_SET_SVC_PARAMS_REQ,
		    bfa_lpuid(port->bfa));
	m->tx_bbcredit = bfa_os_htons((u16) port->cfg.tx_bbcredit);

	/**
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(port->bfa, BFA_REQQ_PORT);
}



/**
 *  bfa_pport_public
 */

/**
 * Firmware message handler.
 */
void
bfa_pport_isr(struct bfa_s *bfa, struct bfi_msg_s *msg)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);
	union bfi_pport_i2h_msg_u i2hmsg;

	i2hmsg.msg = msg;
	pport->event_arg.i2hmsg = i2hmsg;

	switch (msg->mhdr.msg_id) {
	case BFI_PPORT_I2H_ENABLE_RSP:
		if (pport->msgtag == i2hmsg.enable_rsp->msgtag)
			bfa_sm_send_event(pport, BFA_PPORT_SM_FWRSP);
		break;

	case BFI_PPORT_I2H_DISABLE_RSP:
		if (pport->msgtag == i2hmsg.enable_rsp->msgtag)
			bfa_sm_send_event(pport, BFA_PPORT_SM_FWRSP);
		break;

	case BFI_PPORT_I2H_EVENT:
		switch (i2hmsg.event->link_state.linkstate) {
		case BFA_PPORT_LINKUP:
			bfa_sm_send_event(pport, BFA_PPORT_SM_LINKUP);
			break;
		case BFA_PPORT_LINKDOWN:
			bfa_sm_send_event(pport, BFA_PPORT_SM_LINKDOWN);
			break;
		case BFA_PPORT_TRUNK_LINKDOWN:
			/** todo: event notification */
			break;
		}
		break;

	case BFI_PPORT_I2H_GET_STATS_RSP:
	case BFI_PPORT_I2H_GET_QOS_STATS_RSP:
		/*
		 * check for timer pop before processing the rsp
		 */
		if (pport->stats_busy == BFA_FALSE
		    || pport->stats_status == BFA_STATUS_ETIMER)
			break;

		bfa_timer_stop(&pport->timer);
		pport->stats_status = i2hmsg.getstats_rsp->status;
		bfa_cb_queue(pport->bfa, &pport->hcb_qe, __bfa_cb_port_stats,
			     pport);
		break;
	case BFI_PPORT_I2H_CLEAR_STATS_RSP:
	case BFI_PPORT_I2H_CLEAR_QOS_STATS_RSP:
		/*
		 * check for timer pop before processing the rsp
		 */
		if (pport->stats_busy == BFA_FALSE
		    || pport->stats_status == BFA_STATUS_ETIMER)
			break;

		bfa_timer_stop(&pport->timer);
		pport->stats_status = BFA_STATUS_OK;
		bfa_cb_queue(pport->bfa, &pport->hcb_qe,
			     __bfa_cb_port_stats_clr, pport);
		break;

	default:
		bfa_assert(0);
	}
}



/**
 *  bfa_pport_api
 */

/**
 * Registered callback for port events.
 */
void
bfa_pport_event_register(struct bfa_s *bfa,
			 void (*cbfn) (void *cbarg, bfa_pport_event_t event),
			 void *cbarg)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	pport->event_cbfn = cbfn;
	pport->event_cbarg = cbarg;
}

bfa_status_t
bfa_pport_enable(struct bfa_s *bfa)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	if (pport->diag_busy)
		return (BFA_STATUS_DIAG_BUSY);
	else if (bfa_sm_cmp_state
		 (BFA_PORT_MOD(bfa), bfa_pport_sm_disabling_qwait))
		return (BFA_STATUS_DEVBUSY);

	bfa_sm_send_event(BFA_PORT_MOD(bfa), BFA_PPORT_SM_ENABLE);
	return BFA_STATUS_OK;
}

bfa_status_t
bfa_pport_disable(struct bfa_s *bfa)
{
	bfa_sm_send_event(BFA_PORT_MOD(bfa), BFA_PPORT_SM_DISABLE);
	return BFA_STATUS_OK;
}

/**
 * Configure port speed.
 */
bfa_status_t
bfa_pport_cfg_speed(struct bfa_s *bfa, enum bfa_pport_speed speed)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	bfa_trc(bfa, speed);

	if ((speed != BFA_PPORT_SPEED_AUTO) && (speed > pport->speed_sup)) {
		bfa_trc(bfa, pport->speed_sup);
		return BFA_STATUS_UNSUPP_SPEED;
	}

	pport->cfg.speed = speed;

	return (BFA_STATUS_OK);
}

/**
 * Get current speed.
 */
enum bfa_pport_speed
bfa_pport_get_speed(struct bfa_s *bfa)
{
	struct bfa_pport_s *port = BFA_PORT_MOD(bfa);

	return port->speed;
}

/**
 * Configure port topology.
 */
bfa_status_t
bfa_pport_cfg_topology(struct bfa_s *bfa, enum bfa_pport_topology topology)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	bfa_trc(bfa, topology);
	bfa_trc(bfa, pport->cfg.topology);

	switch (topology) {
	case BFA_PPORT_TOPOLOGY_P2P:
	case BFA_PPORT_TOPOLOGY_LOOP:
	case BFA_PPORT_TOPOLOGY_AUTO:
		break;

	default:
		return BFA_STATUS_EINVAL;
	}

	pport->cfg.topology = topology;
	return (BFA_STATUS_OK);
}

/**
 * Get current topology.
 */
enum bfa_pport_topology
bfa_pport_get_topology(struct bfa_s *bfa)
{
	struct bfa_pport_s *port = BFA_PORT_MOD(bfa);

	return port->topology;
}

bfa_status_t
bfa_pport_cfg_hardalpa(struct bfa_s *bfa, u8 alpa)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	bfa_trc(bfa, alpa);
	bfa_trc(bfa, pport->cfg.cfg_hardalpa);
	bfa_trc(bfa, pport->cfg.hardalpa);

	pport->cfg.cfg_hardalpa = BFA_TRUE;
	pport->cfg.hardalpa = alpa;

	return (BFA_STATUS_OK);
}

bfa_status_t
bfa_pport_clr_hardalpa(struct bfa_s *bfa)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	bfa_trc(bfa, pport->cfg.cfg_hardalpa);
	bfa_trc(bfa, pport->cfg.hardalpa);

	pport->cfg.cfg_hardalpa = BFA_FALSE;
	return (BFA_STATUS_OK);
}

bfa_boolean_t
bfa_pport_get_hardalpa(struct bfa_s *bfa, u8 *alpa)
{
	struct bfa_pport_s *port = BFA_PORT_MOD(bfa);

	*alpa = port->cfg.hardalpa;
	return port->cfg.cfg_hardalpa;
}

u8
bfa_pport_get_myalpa(struct bfa_s *bfa)
{
	struct bfa_pport_s *port = BFA_PORT_MOD(bfa);

	return port->myalpa;
}

bfa_status_t
bfa_pport_cfg_maxfrsize(struct bfa_s *bfa, u16 maxfrsize)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	bfa_trc(bfa, maxfrsize);
	bfa_trc(bfa, pport->cfg.maxfrsize);

	/*
	 * with in range
	 */
	if ((maxfrsize > FC_MAX_PDUSZ) || (maxfrsize < FC_MIN_PDUSZ))
		return (BFA_STATUS_INVLD_DFSZ);

	/*
	 * power of 2, if not the max frame size of 2112
	 */
	if ((maxfrsize != FC_MAX_PDUSZ) && (maxfrsize & (maxfrsize - 1)))
		return (BFA_STATUS_INVLD_DFSZ);

	pport->cfg.maxfrsize = maxfrsize;
	return (BFA_STATUS_OK);
}

u16
bfa_pport_get_maxfrsize(struct bfa_s *bfa)
{
	struct bfa_pport_s *port = BFA_PORT_MOD(bfa);

	return port->cfg.maxfrsize;
}

u32
bfa_pport_mypid(struct bfa_s *bfa)
{
	struct bfa_pport_s *port = BFA_PORT_MOD(bfa);

	return port->mypid;
}

u8
bfa_pport_get_rx_bbcredit(struct bfa_s *bfa)
{
	struct bfa_pport_s *port = BFA_PORT_MOD(bfa);

	return port->cfg.rx_bbcredit;
}

void
bfa_pport_set_tx_bbcredit(struct bfa_s *bfa, u16 tx_bbcredit)
{
	struct bfa_pport_s *port = BFA_PORT_MOD(bfa);

	port->cfg.tx_bbcredit = (u8) tx_bbcredit;
	bfa_port_send_txcredit(port);
}

/**
 * Get port attributes.
 */

wwn_t
bfa_pport_get_wwn(struct bfa_s *bfa, bfa_boolean_t node)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);
	if (node)
		return pport->nwwn;
	else
		return pport->pwwn;
}

void
bfa_pport_get_attr(struct bfa_s *bfa, struct bfa_pport_attr_s *attr)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	bfa_os_memset(attr, 0, sizeof(struct bfa_pport_attr_s));

	attr->nwwn = pport->nwwn;
	attr->pwwn = pport->pwwn;

	bfa_os_memcpy(&attr->pport_cfg, &pport->cfg,
		      sizeof(struct bfa_pport_cfg_s));
	/*
	 * speed attributes
	 */
	attr->pport_cfg.speed = pport->cfg.speed;
	attr->speed_supported = pport->speed_sup;
	attr->speed = pport->speed;
	attr->cos_supported = FC_CLASS_3;

	/*
	 * topology attributes
	 */
	attr->pport_cfg.topology = pport->cfg.topology;
	attr->topology = pport->topology;

	/*
	 * beacon attributes
	 */
	attr->beacon = pport->beacon;
	attr->link_e2e_beacon = pport->link_e2e_beacon;
	attr->plog_enabled = bfa_plog_get_setting(pport->bfa->plog);

	attr->pport_cfg.path_tov = bfa_fcpim_path_tov_get(bfa);
	attr->pport_cfg.q_depth = bfa_fcpim_qdepth_get(bfa);
	attr->port_state = bfa_sm_to_state(hal_pport_sm_table, pport->sm);
	if (bfa_ioc_is_disabled(&pport->bfa->ioc))
		attr->port_state = BFA_PPORT_ST_IOCDIS;
	else if (bfa_ioc_fw_mismatch(&pport->bfa->ioc))
		attr->port_state = BFA_PPORT_ST_FWMISMATCH;
}

static void
bfa_port_stats_query(void *cbarg)
{
	struct bfa_pport_s *port = (struct bfa_pport_s *)cbarg;
	bfi_pport_get_stats_req_t *msg;

	msg = bfa_reqq_next(port->bfa, BFA_REQQ_PORT);

	if (!msg) {
		port->stats_qfull = BFA_TRUE;
		bfa_reqq_winit(&port->stats_reqq_wait, bfa_port_stats_query,
			       port);
		bfa_reqq_wait(port->bfa, BFA_REQQ_PORT, &port->stats_reqq_wait);
		return;
	}
	port->stats_qfull = BFA_FALSE;

	bfa_os_memset(msg, 0, sizeof(bfi_pport_get_stats_req_t));
	bfi_h2i_set(msg->mh, BFI_MC_FC_PORT, BFI_PPORT_H2I_GET_STATS_REQ,
		    bfa_lpuid(port->bfa));
	bfa_reqq_produce(port->bfa, BFA_REQQ_PORT);

	return;
}

static void
bfa_port_stats_clear(void *cbarg)
{
	struct bfa_pport_s *port = (struct bfa_pport_s *)cbarg;
	bfi_pport_clear_stats_req_t *msg;

	msg = bfa_reqq_next(port->bfa, BFA_REQQ_PORT);

	if (!msg) {
		port->stats_qfull = BFA_TRUE;
		bfa_reqq_winit(&port->stats_reqq_wait, bfa_port_stats_clear,
			       port);
		bfa_reqq_wait(port->bfa, BFA_REQQ_PORT, &port->stats_reqq_wait);
		return;
	}
	port->stats_qfull = BFA_FALSE;

	bfa_os_memset(msg, 0, sizeof(bfi_pport_clear_stats_req_t));
	bfi_h2i_set(msg->mh, BFI_MC_FC_PORT, BFI_PPORT_H2I_CLEAR_STATS_REQ,
		    bfa_lpuid(port->bfa));
	bfa_reqq_produce(port->bfa, BFA_REQQ_PORT);
	return;
}

static void
bfa_port_qos_stats_clear(void *cbarg)
{
	struct bfa_pport_s *port = (struct bfa_pport_s *)cbarg;
	bfi_pport_clear_qos_stats_req_t *msg;

	msg = bfa_reqq_next(port->bfa, BFA_REQQ_PORT);

	if (!msg) {
		port->stats_qfull = BFA_TRUE;
		bfa_reqq_winit(&port->stats_reqq_wait, bfa_port_qos_stats_clear,
			       port);
		bfa_reqq_wait(port->bfa, BFA_REQQ_PORT, &port->stats_reqq_wait);
		return;
	}
	port->stats_qfull = BFA_FALSE;

	bfa_os_memset(msg, 0, sizeof(bfi_pport_clear_qos_stats_req_t));
	bfi_h2i_set(msg->mh, BFI_MC_FC_PORT, BFI_PPORT_H2I_CLEAR_QOS_STATS_REQ,
		    bfa_lpuid(port->bfa));
	bfa_reqq_produce(port->bfa, BFA_REQQ_PORT);
	return;
}

static void
bfa_pport_stats_swap(union bfa_pport_stats_u *d, union bfa_pport_stats_u *s)
{
	u32       *dip = (u32 *) d;
	u32       *sip = (u32 *) s;
	int             i;

	/*
	 * Do 64 bit fields swap first
	 */
	for (i = 0;
	     i <
	     ((sizeof(union bfa_pport_stats_u) -
	       sizeof(struct bfa_qos_stats_s)) / sizeof(u32)); i = i + 2) {
#ifdef __BIGENDIAN
		dip[i] = bfa_os_ntohl(sip[i]);
		dip[i + 1] = bfa_os_ntohl(sip[i + 1]);
#else
		dip[i] = bfa_os_ntohl(sip[i + 1]);
		dip[i + 1] = bfa_os_ntohl(sip[i]);
#endif
	}

	/*
	 * Now swap the 32 bit fields
	 */
	for (; i < (sizeof(union bfa_pport_stats_u) / sizeof(u32)); ++i)
		dip[i] = bfa_os_ntohl(sip[i]);
}

static void
__bfa_cb_port_stats_clr(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_pport_s *port = cbarg;

	if (complete) {
		port->stats_cbfn(port->stats_cbarg, port->stats_status);
	} else {
		port->stats_busy = BFA_FALSE;
		port->stats_status = BFA_STATUS_OK;
	}
}

static void
bfa_port_stats_clr_timeout(void *cbarg)
{
	struct bfa_pport_s *port = (struct bfa_pport_s *)cbarg;

	bfa_trc(port->bfa, port->stats_qfull);

	if (port->stats_qfull) {
		bfa_reqq_wcancel(&port->stats_reqq_wait);
		port->stats_qfull = BFA_FALSE;
	}

	port->stats_status = BFA_STATUS_ETIMER;
	bfa_cb_queue(port->bfa, &port->hcb_qe, __bfa_cb_port_stats_clr, port);
}

static void
__bfa_cb_port_stats(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_pport_s *port = cbarg;

	if (complete) {
		if (port->stats_status == BFA_STATUS_OK)
			bfa_pport_stats_swap(port->stats_ret, port->stats);
		port->stats_cbfn(port->stats_cbarg, port->stats_status);
	} else {
		port->stats_busy = BFA_FALSE;
		port->stats_status = BFA_STATUS_OK;
	}
}

static void
bfa_port_stats_timeout(void *cbarg)
{
	struct bfa_pport_s *port = (struct bfa_pport_s *)cbarg;

	bfa_trc(port->bfa, port->stats_qfull);

	if (port->stats_qfull) {
		bfa_reqq_wcancel(&port->stats_reqq_wait);
		port->stats_qfull = BFA_FALSE;
	}

	port->stats_status = BFA_STATUS_ETIMER;
	bfa_cb_queue(port->bfa, &port->hcb_qe, __bfa_cb_port_stats, port);
}

#define BFA_PORT_STATS_TOV	1000

/**
 * Fetch port attributes.
 */
bfa_status_t
bfa_pport_get_stats(struct bfa_s *bfa, union bfa_pport_stats_u *stats,
		    bfa_cb_pport_t cbfn, void *cbarg)
{
	struct bfa_pport_s *port = BFA_PORT_MOD(bfa);

	if (port->stats_busy) {
		bfa_trc(bfa, port->stats_busy);
		return (BFA_STATUS_DEVBUSY);
	}

	port->stats_busy = BFA_TRUE;
	port->stats_ret = stats;
	port->stats_cbfn = cbfn;
	port->stats_cbarg = cbarg;

	bfa_port_stats_query(port);

	bfa_timer_start(bfa, &port->timer, bfa_port_stats_timeout, port,
			BFA_PORT_STATS_TOV);
	return (BFA_STATUS_OK);
}

bfa_status_t
bfa_pport_clear_stats(struct bfa_s *bfa, bfa_cb_pport_t cbfn, void *cbarg)
{
	struct bfa_pport_s *port = BFA_PORT_MOD(bfa);

	if (port->stats_busy) {
		bfa_trc(bfa, port->stats_busy);
		return (BFA_STATUS_DEVBUSY);
	}

	port->stats_busy = BFA_TRUE;
	port->stats_cbfn = cbfn;
	port->stats_cbarg = cbarg;

	bfa_port_stats_clear(port);

	bfa_timer_start(bfa, &port->timer, bfa_port_stats_clr_timeout, port,
			BFA_PORT_STATS_TOV);
	return (BFA_STATUS_OK);
}

bfa_status_t
bfa_pport_trunk_enable(struct bfa_s *bfa, u8 bitmap)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	bfa_trc(bfa, bitmap);
	bfa_trc(bfa, pport->cfg.trunked);
	bfa_trc(bfa, pport->cfg.trunk_ports);

	if (!bitmap || (bitmap & (bitmap - 1)))
		return BFA_STATUS_EINVAL;

	pport->cfg.trunked = BFA_TRUE;
	pport->cfg.trunk_ports = bitmap;

	return BFA_STATUS_OK;
}

void
bfa_pport_qos_get_attr(struct bfa_s *bfa, struct bfa_qos_attr_s *qos_attr)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	qos_attr->state = bfa_os_ntohl(pport->qos_attr.state);
	qos_attr->total_bb_cr = bfa_os_ntohl(pport->qos_attr.total_bb_cr);
}

void
bfa_pport_qos_get_vc_attr(struct bfa_s *bfa,
			  struct bfa_qos_vc_attr_s *qos_vc_attr)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);
	struct bfa_qos_vc_attr_s *bfa_vc_attr = &pport->qos_vc_attr;
	u32        i = 0;

	qos_vc_attr->total_vc_count = bfa_os_ntohs(bfa_vc_attr->total_vc_count);
	qos_vc_attr->shared_credit = bfa_os_ntohs(bfa_vc_attr->shared_credit);
	qos_vc_attr->elp_opmode_flags =
		bfa_os_ntohl(bfa_vc_attr->elp_opmode_flags);

	/*
	 * Individual VC info
	 */
	while (i < qos_vc_attr->total_vc_count) {
		qos_vc_attr->vc_info[i].vc_credit =
			bfa_vc_attr->vc_info[i].vc_credit;
		qos_vc_attr->vc_info[i].borrow_credit =
			bfa_vc_attr->vc_info[i].borrow_credit;
		qos_vc_attr->vc_info[i].priority =
			bfa_vc_attr->vc_info[i].priority;
		++i;
	}
}

/**
 * Fetch QoS Stats.
 */
bfa_status_t
bfa_pport_get_qos_stats(struct bfa_s *bfa, union bfa_pport_stats_u *stats,
			bfa_cb_pport_t cbfn, void *cbarg)
{
	/*
	 * QoS stats is embedded in port stats
	 */
	return (bfa_pport_get_stats(bfa, stats, cbfn, cbarg));
}

bfa_status_t
bfa_pport_clear_qos_stats(struct bfa_s *bfa, bfa_cb_pport_t cbfn, void *cbarg)
{
	struct bfa_pport_s *port = BFA_PORT_MOD(bfa);

	if (port->stats_busy) {
		bfa_trc(bfa, port->stats_busy);
		return (BFA_STATUS_DEVBUSY);
	}

	port->stats_busy = BFA_TRUE;
	port->stats_cbfn = cbfn;
	port->stats_cbarg = cbarg;

	bfa_port_qos_stats_clear(port);

	bfa_timer_start(bfa, &port->timer, bfa_port_stats_clr_timeout, port,
			BFA_PORT_STATS_TOV);
	return (BFA_STATUS_OK);
}

/**
 * Fetch port attributes.
 */
bfa_status_t
bfa_pport_trunk_disable(struct bfa_s *bfa)
{
	return (BFA_STATUS_OK);
}

bfa_boolean_t
bfa_pport_trunk_query(struct bfa_s *bfa, u32 *bitmap)
{
	struct bfa_pport_s *port = BFA_PORT_MOD(bfa);

	*bitmap = port->cfg.trunk_ports;
	return port->cfg.trunked;
}

bfa_boolean_t
bfa_pport_is_disabled(struct bfa_s *bfa)
{
	struct bfa_pport_s *port = BFA_PORT_MOD(bfa);

	return (bfa_sm_to_state(hal_pport_sm_table, port->sm) ==
		BFA_PPORT_ST_DISABLED);

}

bfa_boolean_t
bfa_pport_is_ratelim(struct bfa_s *bfa)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

return (pport->cfg.ratelimit ? BFA_TRUE : BFA_FALSE);

}

void
bfa_pport_cfg_qos(struct bfa_s *bfa, bfa_boolean_t on_off)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	bfa_trc(bfa, on_off);
	bfa_trc(bfa, pport->cfg.qos_enabled);

	pport->cfg.qos_enabled = on_off;
}

void
bfa_pport_cfg_ratelim(struct bfa_s *bfa, bfa_boolean_t on_off)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	bfa_trc(bfa, on_off);
	bfa_trc(bfa, pport->cfg.ratelimit);

	pport->cfg.ratelimit = on_off;
	if (pport->cfg.trl_def_speed == BFA_PPORT_SPEED_UNKNOWN)
		pport->cfg.trl_def_speed = BFA_PPORT_SPEED_1GBPS;
}

/**
 * Configure default minimum ratelim speed
 */
bfa_status_t
bfa_pport_cfg_ratelim_speed(struct bfa_s *bfa, enum bfa_pport_speed speed)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	bfa_trc(bfa, speed);

	/*
	 * Auto and speeds greater than the supported speed, are invalid
	 */
	if ((speed == BFA_PPORT_SPEED_AUTO) || (speed > pport->speed_sup)) {
		bfa_trc(bfa, pport->speed_sup);
		return BFA_STATUS_UNSUPP_SPEED;
	}

	pport->cfg.trl_def_speed = speed;

	return (BFA_STATUS_OK);
}

/**
 * Get default minimum ratelim speed
 */
enum bfa_pport_speed
bfa_pport_get_ratelim_speed(struct bfa_s *bfa)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	bfa_trc(bfa, pport->cfg.trl_def_speed);
	return (pport->cfg.trl_def_speed);

}

void
bfa_pport_busy(struct bfa_s *bfa, bfa_boolean_t status)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	bfa_trc(bfa, status);
	bfa_trc(bfa, pport->diag_busy);

	pport->diag_busy = status;
}

void
bfa_pport_beacon(struct bfa_s *bfa, bfa_boolean_t beacon,
		 bfa_boolean_t link_e2e_beacon)
{
	struct bfa_pport_s *pport = BFA_PORT_MOD(bfa);

	bfa_trc(bfa, beacon);
	bfa_trc(bfa, link_e2e_beacon);
	bfa_trc(bfa, pport->beacon);
	bfa_trc(bfa, pport->link_e2e_beacon);

	pport->beacon = beacon;
	pport->link_e2e_beacon = link_e2e_beacon;
}

bfa_boolean_t
bfa_pport_is_linkup(struct bfa_s *bfa)
{
	return bfa_sm_cmp_state(BFA_PORT_MOD(bfa), bfa_pport_sm_linkup);
}


