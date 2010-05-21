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

BFA_TRC_FILE(HAL, FCPORT);
BFA_MODULE(fcport);

/*
 * The port is considered disabled if corresponding physical port or IOC are
 * disabled explicitly
 */
#define BFA_PORT_IS_DISABLED(bfa) \
	((bfa_fcport_is_disabled(bfa) == BFA_TRUE) || \
	(bfa_ioc_is_disabled(&bfa->ioc) == BFA_TRUE))

/*
 * forward declarations
 */
static bfa_boolean_t bfa_fcport_send_enable(struct bfa_fcport_s *fcport);
static bfa_boolean_t bfa_fcport_send_disable(struct bfa_fcport_s *fcport);
static void     bfa_fcport_update_linkinfo(struct bfa_fcport_s *fcport);
static void     bfa_fcport_reset_linkinfo(struct bfa_fcport_s *fcport);
static void     bfa_fcport_set_wwns(struct bfa_fcport_s *fcport);
static void     __bfa_cb_fcport_event(void *cbarg, bfa_boolean_t complete);
static void     bfa_fcport_callback(struct bfa_fcport_s *fcport,
				enum bfa_pport_linkstate event);
static void     bfa_fcport_queue_cb(struct bfa_fcport_ln_s *ln,
				enum bfa_pport_linkstate event);
static void     __bfa_cb_fcport_stats_clr(void *cbarg, bfa_boolean_t complete);
static void     bfa_fcport_stats_get_timeout(void *cbarg);
static void     bfa_fcport_stats_clr_timeout(void *cbarg);

/**
 *  bfa_pport_private
 */

/**
 * BFA port state machine events
 */
enum bfa_fcport_sm_event {
	BFA_FCPORT_SM_START = 1,	/*  start port state machine */
	BFA_FCPORT_SM_STOP = 2,	/*  stop port state machine */
	BFA_FCPORT_SM_ENABLE = 3,	/*  enable port */
	BFA_FCPORT_SM_DISABLE = 4,	/*  disable port state machine */
	BFA_FCPORT_SM_FWRSP = 5,	/*  firmware enable/disable rsp */
	BFA_FCPORT_SM_LINKUP = 6,	/*  firmware linkup event */
	BFA_FCPORT_SM_LINKDOWN = 7,	/*  firmware linkup down */
	BFA_FCPORT_SM_QRESUME = 8,	/*  CQ space available */
	BFA_FCPORT_SM_HWFAIL = 9,	/*  IOC h/w failure */
};

/**
 * BFA port link notification state machine events
 */

enum bfa_fcport_ln_sm_event {
	BFA_FCPORT_LN_SM_LINKUP         = 1,    /*  linkup event */
	BFA_FCPORT_LN_SM_LINKDOWN       = 2,    /*  linkdown event */
	BFA_FCPORT_LN_SM_NOTIFICATION   = 3     /*  done notification */
};

static void     bfa_fcport_sm_uninit(struct bfa_fcport_s *fcport,
					enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_enabling_qwait(struct bfa_fcport_s *fcport,
						enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_enabling(struct bfa_fcport_s *fcport,
						enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_linkdown(struct bfa_fcport_s *fcport,
						enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_linkup(struct bfa_fcport_s *fcport,
						enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_disabling(struct bfa_fcport_s *fcport,
						enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_disabling_qwait(struct bfa_fcport_s *fcport,
						enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_disabled(struct bfa_fcport_s *fcport,
						enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_stopped(struct bfa_fcport_s *fcport,
					 enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_iocdown(struct bfa_fcport_s *fcport,
					 enum bfa_fcport_sm_event event);
static void     bfa_fcport_sm_iocfail(struct bfa_fcport_s *fcport,
					 enum bfa_fcport_sm_event event);

static void     bfa_fcport_ln_sm_dn(struct bfa_fcport_ln_s *ln,
					 enum bfa_fcport_ln_sm_event event);
static void     bfa_fcport_ln_sm_dn_nf(struct bfa_fcport_ln_s *ln,
					 enum bfa_fcport_ln_sm_event event);
static void     bfa_fcport_ln_sm_dn_up_nf(struct bfa_fcport_ln_s *ln,
					 enum bfa_fcport_ln_sm_event event);
static void     bfa_fcport_ln_sm_up(struct bfa_fcport_ln_s *ln,
					 enum bfa_fcport_ln_sm_event event);
static void     bfa_fcport_ln_sm_up_nf(struct bfa_fcport_ln_s *ln,
					 enum bfa_fcport_ln_sm_event event);
static void     bfa_fcport_ln_sm_up_dn_nf(struct bfa_fcport_ln_s *ln,
					 enum bfa_fcport_ln_sm_event event);
static void     bfa_fcport_ln_sm_up_dn_up_nf(struct bfa_fcport_ln_s *ln,
					 enum bfa_fcport_ln_sm_event event);

static struct bfa_sm_table_s hal_pport_sm_table[] = {
	{BFA_SM(bfa_fcport_sm_uninit), BFA_PPORT_ST_UNINIT},
	{BFA_SM(bfa_fcport_sm_enabling_qwait), BFA_PPORT_ST_ENABLING_QWAIT},
	{BFA_SM(bfa_fcport_sm_enabling), BFA_PPORT_ST_ENABLING},
	{BFA_SM(bfa_fcport_sm_linkdown), BFA_PPORT_ST_LINKDOWN},
	{BFA_SM(bfa_fcport_sm_linkup), BFA_PPORT_ST_LINKUP},
	{BFA_SM(bfa_fcport_sm_disabling_qwait), BFA_PPORT_ST_DISABLING_QWAIT},
	{BFA_SM(bfa_fcport_sm_disabling), BFA_PPORT_ST_DISABLING},
	{BFA_SM(bfa_fcport_sm_disabled), BFA_PPORT_ST_DISABLED},
	{BFA_SM(bfa_fcport_sm_stopped), BFA_PPORT_ST_STOPPED},
	{BFA_SM(bfa_fcport_sm_iocdown), BFA_PPORT_ST_IOCDOWN},
	{BFA_SM(bfa_fcport_sm_iocfail), BFA_PPORT_ST_IOCDOWN},
};

static void
bfa_fcport_aen_post(struct bfa_fcport_s *fcport, enum bfa_port_aen_event event)
{
	union bfa_aen_data_u aen_data;
	struct bfa_log_mod_s *logmod = fcport->bfa->logm;
	wwn_t           pwwn = fcport->pwwn;
	char            pwwn_ptr[BFA_STRING_32];

	memset(&aen_data, 0, sizeof(aen_data));
	wwn2str(pwwn_ptr, pwwn);
	bfa_log(logmod, BFA_LOG_CREATE_ID(BFA_AEN_CAT_PORT, event), pwwn_ptr);

	aen_data.port.ioc_type = bfa_get_type(fcport->bfa);
	aen_data.port.pwwn = pwwn;
}

static void
bfa_fcport_sm_uninit(struct bfa_fcport_s *fcport,
			enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_START:
		/**
		 * Start event after IOC is configured and BFA is started.
		 */
		if (bfa_fcport_send_enable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling);
		else
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling_qwait);
		break;

	case BFA_FCPORT_SM_ENABLE:
		/**
		 * Port is persistently configured to be in enabled state. Do
		 * not change state. Port enabling is done when START event is
		 * received.
		 */
		break;

	case BFA_FCPORT_SM_DISABLE:
		/**
		 * If a port is persistently configured to be disabled, the
		 * first event will a port disable request.
		 */
		bfa_sm_set_state(fcport, bfa_fcport_sm_disabled);
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocdown);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_enabling_qwait(struct bfa_fcport_s *fcport,
			    enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_QRESUME:
		bfa_sm_set_state(fcport, bfa_fcport_sm_enabling);
		bfa_fcport_send_enable(fcport);
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_reqq_wcancel(&fcport->reqq_wait);
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		break;

	case BFA_FCPORT_SM_ENABLE:
		/**
		 * Already enable is in progress.
		 */
		break;

	case BFA_FCPORT_SM_DISABLE:
		/**
		 * Just send disable request to firmware when room becomes
		 * available in request queue.
		 */
		bfa_sm_set_state(fcport, bfa_fcport_sm_disabled);
		bfa_reqq_wcancel(&fcport->reqq_wait);
		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_DISABLE, 0, "Port Disable");
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISABLE);
		break;

	case BFA_FCPORT_SM_LINKUP:
	case BFA_FCPORT_SM_LINKDOWN:
		/**
		 * Possible to get link events when doing back-to-back
		 * enable/disables.
		 */
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_reqq_wcancel(&fcport->reqq_wait);
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocdown);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_enabling(struct bfa_fcport_s *fcport,
		enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_FWRSP:
	case BFA_FCPORT_SM_LINKDOWN:
		bfa_sm_set_state(fcport, bfa_fcport_sm_linkdown);
		break;

	case BFA_FCPORT_SM_LINKUP:
		bfa_fcport_update_linkinfo(fcport);
		bfa_sm_set_state(fcport, bfa_fcport_sm_linkup);

		bfa_assert(fcport->event_cbfn);
		bfa_fcport_callback(fcport, BFA_PPORT_LINKUP);
		break;

	case BFA_FCPORT_SM_ENABLE:
		/**
		 * Already being enabled.
		 */
		break;

	case BFA_FCPORT_SM_DISABLE:
		if (bfa_fcport_send_disable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_disabling);
		else
			bfa_sm_set_state(fcport, bfa_fcport_sm_disabling_qwait);

		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_DISABLE, 0, "Port Disable");
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISABLE);
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocdown);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_linkdown(struct bfa_fcport_s *fcport,
			enum bfa_fcport_sm_event event)
{
	struct bfi_fcport_event_s *pevent = fcport->event_arg.i2hmsg.event;
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_LINKUP:
		bfa_fcport_update_linkinfo(fcport);
		bfa_sm_set_state(fcport, bfa_fcport_sm_linkup);
		bfa_assert(fcport->event_cbfn);
		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_ST_CHANGE, 0, "Port Linkup");

		if (!bfa_ioc_get_fcmode(&fcport->bfa->ioc)) {

			bfa_trc(fcport->bfa, pevent->link_state.fcf.fipenabled);
			bfa_trc(fcport->bfa, pevent->link_state.fcf.fipfailed);

			if (pevent->link_state.fcf.fipfailed)
				bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
					BFA_PL_EID_FIP_FCF_DISC, 0,
					"FIP FCF Discovery Failed");
			else
				bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
					BFA_PL_EID_FIP_FCF_DISC, 0,
					"FIP FCF Discovered");
		}

		bfa_fcport_callback(fcport, BFA_PPORT_LINKUP);
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_ONLINE);
		/**
		 * If QoS is enabled and it is not online,
		 * Send a separate event.
		 */
		if ((fcport->cfg.qos_enabled)
		    && (bfa_os_ntohl(fcport->qos_attr.state) != BFA_QOS_ONLINE))
			bfa_fcport_aen_post(fcport, BFA_PORT_AEN_QOS_NEG);

		break;

	case BFA_FCPORT_SM_LINKDOWN:
		/**
		 * Possible to get link down event.
		 */
		break;

	case BFA_FCPORT_SM_ENABLE:
		/**
		 * Already enabled.
		 */
		break;

	case BFA_FCPORT_SM_DISABLE:
		if (bfa_fcport_send_disable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_disabling);
		else
			bfa_sm_set_state(fcport, bfa_fcport_sm_disabling_qwait);

		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_DISABLE, 0, "Port Disable");
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISABLE);
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocdown);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_linkup(struct bfa_fcport_s *fcport,
			enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_ENABLE:
		/**
		 * Already enabled.
		 */
		break;

	case BFA_FCPORT_SM_DISABLE:
		if (bfa_fcport_send_disable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_disabling);
		else
			bfa_sm_set_state(fcport, bfa_fcport_sm_disabling_qwait);

		bfa_fcport_reset_linkinfo(fcport);
		bfa_fcport_callback(fcport, BFA_PPORT_LINKDOWN);
		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_DISABLE, 0, "Port Disable");
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_OFFLINE);
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISABLE);
		break;

	case BFA_FCPORT_SM_LINKDOWN:
		bfa_sm_set_state(fcport, bfa_fcport_sm_linkdown);
		bfa_fcport_reset_linkinfo(fcport);
		bfa_fcport_callback(fcport, BFA_PPORT_LINKDOWN);
		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_ST_CHANGE, 0, "Port Linkdown");
		if (BFA_PORT_IS_DISABLED(fcport->bfa))
			bfa_fcport_aen_post(fcport, BFA_PORT_AEN_OFFLINE);
		else
			bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISCONNECT);
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		bfa_fcport_reset_linkinfo(fcport);
		if (BFA_PORT_IS_DISABLED(fcport->bfa))
			bfa_fcport_aen_post(fcport, BFA_PORT_AEN_OFFLINE);
		else
			bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISCONNECT);
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocdown);
		bfa_fcport_reset_linkinfo(fcport);
		bfa_fcport_callback(fcport, BFA_PPORT_LINKDOWN);
		if (BFA_PORT_IS_DISABLED(fcport->bfa))
			bfa_fcport_aen_post(fcport, BFA_PORT_AEN_OFFLINE);
		else
			bfa_fcport_aen_post(fcport, BFA_PORT_AEN_DISCONNECT);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_disabling_qwait(struct bfa_fcport_s *fcport,
			     enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_QRESUME:
		bfa_sm_set_state(fcport, bfa_fcport_sm_disabling);
		bfa_fcport_send_disable(fcport);
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		bfa_reqq_wcancel(&fcport->reqq_wait);
		break;

	case BFA_FCPORT_SM_DISABLE:
		/**
		 * Already being disabled.
		 */
		break;

	case BFA_FCPORT_SM_LINKUP:
	case BFA_FCPORT_SM_LINKDOWN:
		/**
		 * Possible to get link events when doing back-to-back
		 * enable/disables.
		 */
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocfail);
		bfa_reqq_wcancel(&fcport->reqq_wait);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_disabling(struct bfa_fcport_s *fcport,
			enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_FWRSP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_disabled);
		break;

	case BFA_FCPORT_SM_DISABLE:
		/**
		 * Already being disabled.
		 */
		break;

	case BFA_FCPORT_SM_ENABLE:
		if (bfa_fcport_send_enable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling);
		else
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling_qwait);

		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_ENABLE, 0, "Port Enable");
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_ENABLE);
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		break;

	case BFA_FCPORT_SM_LINKUP:
	case BFA_FCPORT_SM_LINKDOWN:
		/**
		 * Possible to get link events when doing back-to-back
		 * enable/disables.
		 */
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocfail);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_disabled(struct bfa_fcport_s *fcport,
			enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_START:
		/**
		 * Ignore start event for a port that is disabled.
		 */
		break;

	case BFA_FCPORT_SM_STOP:
		bfa_sm_set_state(fcport, bfa_fcport_sm_stopped);
		break;

	case BFA_FCPORT_SM_ENABLE:
		if (bfa_fcport_send_enable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling);
		else
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling_qwait);

		bfa_plog_str(fcport->bfa->plog, BFA_PL_MID_HAL,
			     BFA_PL_EID_PORT_ENABLE, 0, "Port Enable");
		bfa_fcport_aen_post(fcport, BFA_PORT_AEN_ENABLE);
		break;

	case BFA_FCPORT_SM_DISABLE:
		/**
		 * Already disabled.
		 */
		break;

	case BFA_FCPORT_SM_HWFAIL:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocfail);
		break;

	default:
		bfa_sm_fault(fcport->bfa, event);
	}
}

static void
bfa_fcport_sm_stopped(struct bfa_fcport_s *fcport,
			enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_START:
		if (bfa_fcport_send_enable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling);
		else
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling_qwait);
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
bfa_fcport_sm_iocdown(struct bfa_fcport_s *fcport,
			enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_START:
		if (bfa_fcport_send_enable(fcport))
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling);
		else
			bfa_sm_set_state(fcport, bfa_fcport_sm_enabling_qwait);
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
bfa_fcport_sm_iocfail(struct bfa_fcport_s *fcport,
			enum bfa_fcport_sm_event event)
{
	bfa_trc(fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_SM_START:
		bfa_sm_set_state(fcport, bfa_fcport_sm_disabled);
		break;

	case BFA_FCPORT_SM_ENABLE:
		bfa_sm_set_state(fcport, bfa_fcport_sm_iocdown);
		break;

	default:
		/**
		 * Ignore all events.
		 */
		;
	}
}

/**
 * Link state is down
 */
static void
bfa_fcport_ln_sm_dn(struct bfa_fcport_ln_s *ln,
		enum bfa_fcport_ln_sm_event event)
{
	bfa_trc(ln->fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_LN_SM_LINKUP:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_up_nf);
		bfa_fcport_queue_cb(ln, BFA_PPORT_LINKUP);
		break;

	default:
		bfa_sm_fault(ln->fcport->bfa, event);
	}
}

/**
 * Link state is waiting for down notification
 */
static void
bfa_fcport_ln_sm_dn_nf(struct bfa_fcport_ln_s *ln,
		enum bfa_fcport_ln_sm_event event)
{
	bfa_trc(ln->fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_LN_SM_LINKUP:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_dn_up_nf);
		break;

	case BFA_FCPORT_LN_SM_NOTIFICATION:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_dn);
		break;

	default:
		bfa_sm_fault(ln->fcport->bfa, event);
	}
}

/**
 * Link state is waiting for down notification and there is a pending up
 */
static void
bfa_fcport_ln_sm_dn_up_nf(struct bfa_fcport_ln_s *ln,
		enum bfa_fcport_ln_sm_event event)
{
	bfa_trc(ln->fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_LN_SM_LINKDOWN:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_dn_nf);
		break;

	case BFA_FCPORT_LN_SM_NOTIFICATION:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_up_nf);
		bfa_fcport_queue_cb(ln, BFA_PPORT_LINKUP);
		break;

	default:
		bfa_sm_fault(ln->fcport->bfa, event);
	}
}

/**
 * Link state is up
 */
static void
bfa_fcport_ln_sm_up(struct bfa_fcport_ln_s *ln,
		enum bfa_fcport_ln_sm_event event)
{
	bfa_trc(ln->fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_LN_SM_LINKDOWN:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_dn_nf);
		bfa_fcport_queue_cb(ln, BFA_PPORT_LINKDOWN);
		break;

	default:
		bfa_sm_fault(ln->fcport->bfa, event);
	}
}

/**
 * Link state is waiting for up notification
 */
static void
bfa_fcport_ln_sm_up_nf(struct bfa_fcport_ln_s *ln,
		enum bfa_fcport_ln_sm_event event)
{
	bfa_trc(ln->fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_LN_SM_LINKDOWN:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_up_dn_nf);
		break;

	case BFA_FCPORT_LN_SM_NOTIFICATION:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_up);
		break;

	default:
		bfa_sm_fault(ln->fcport->bfa, event);
	}
}

/**
 * Link state is waiting for up notification and there is a pending down
 */
static void
bfa_fcport_ln_sm_up_dn_nf(struct bfa_fcport_ln_s *ln,
		enum bfa_fcport_ln_sm_event event)
{
	bfa_trc(ln->fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_LN_SM_LINKUP:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_up_dn_up_nf);
		break;

	case BFA_FCPORT_LN_SM_NOTIFICATION:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_dn_nf);
		bfa_fcport_queue_cb(ln, BFA_PPORT_LINKDOWN);
		break;

	default:
		bfa_sm_fault(ln->fcport->bfa, event);
	}
}

/**
 * Link state is waiting for up notification and there are pending down and up
 */
static void
bfa_fcport_ln_sm_up_dn_up_nf(struct bfa_fcport_ln_s *ln,
			enum bfa_fcport_ln_sm_event event)
{
	bfa_trc(ln->fcport->bfa, event);

	switch (event) {
	case BFA_FCPORT_LN_SM_LINKDOWN:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_up_dn_nf);
		break;

	case BFA_FCPORT_LN_SM_NOTIFICATION:
		bfa_sm_set_state(ln, bfa_fcport_ln_sm_dn_up_nf);
		bfa_fcport_queue_cb(ln, BFA_PPORT_LINKDOWN);
		break;

	default:
		bfa_sm_fault(ln->fcport->bfa, event);
	}
}

/**
 *  bfa_pport_private
 */

static void
__bfa_cb_fcport_event(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_fcport_ln_s *ln = cbarg;

	if (complete)
		ln->fcport->event_cbfn(ln->fcport->event_cbarg, ln->ln_event);
	else
		bfa_sm_send_event(ln, BFA_FCPORT_LN_SM_NOTIFICATION);
}

static void
bfa_fcport_callback(struct bfa_fcport_s *fcport, enum bfa_pport_linkstate event)
{
	if (fcport->bfa->fcs) {
		fcport->event_cbfn(fcport->event_cbarg, event);
		return;
	}

	switch (event) {
	case BFA_PPORT_LINKUP:
		bfa_sm_send_event(&fcport->ln, BFA_FCPORT_LN_SM_LINKUP);
		break;
	case BFA_PPORT_LINKDOWN:
		bfa_sm_send_event(&fcport->ln, BFA_FCPORT_LN_SM_LINKDOWN);
		break;
	default:
		bfa_assert(0);
	}
}

static void
bfa_fcport_queue_cb(struct bfa_fcport_ln_s *ln, enum bfa_pport_linkstate event)
{
	ln->ln_event = event;
	bfa_cb_queue(ln->fcport->bfa, &ln->ln_qe, __bfa_cb_fcport_event, ln);
}

#define FCPORT_STATS_DMA_SZ (BFA_ROUNDUP(sizeof(union bfa_fcport_stats_u), \
							BFA_CACHELINE_SZ))

static void
bfa_fcport_meminfo(struct bfa_iocfc_cfg_s *cfg, u32 *ndm_len,
		  u32 *dm_len)
{
	*dm_len += FCPORT_STATS_DMA_SZ;
}

static void
bfa_fcport_qresume(void *cbarg)
{
	struct bfa_fcport_s *fcport = cbarg;

	bfa_sm_send_event(fcport, BFA_FCPORT_SM_QRESUME);
}

static void
bfa_fcport_mem_claim(struct bfa_fcport_s *fcport, struct bfa_meminfo_s *meminfo)
{
	u8        *dm_kva;
	u64        dm_pa;

	dm_kva = bfa_meminfo_dma_virt(meminfo);
	dm_pa = bfa_meminfo_dma_phys(meminfo);

	fcport->stats_kva = dm_kva;
	fcport->stats_pa = dm_pa;
	fcport->stats = (union bfa_fcport_stats_u *)dm_kva;

	dm_kva += FCPORT_STATS_DMA_SZ;
	dm_pa += FCPORT_STATS_DMA_SZ;

	bfa_meminfo_dma_virt(meminfo) = dm_kva;
	bfa_meminfo_dma_phys(meminfo) = dm_pa;
}

/**
 * Memory initialization.
 */
static void
bfa_fcport_attach(struct bfa_s *bfa, void *bfad, struct bfa_iocfc_cfg_s *cfg,
		 struct bfa_meminfo_s *meminfo, struct bfa_pcidev_s *pcidev)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);
	struct bfa_pport_cfg_s *port_cfg = &fcport->cfg;
	struct bfa_fcport_ln_s *ln = &fcport->ln;

	bfa_os_memset(fcport, 0, sizeof(struct bfa_fcport_s));
	fcport->bfa = bfa;
	ln->fcport = fcport;

	bfa_fcport_mem_claim(fcport, meminfo);

	bfa_sm_set_state(fcport, bfa_fcport_sm_uninit);
	bfa_sm_set_state(ln, bfa_fcport_ln_sm_dn);

	/**
	 * initialize and set default configuration
	 */
	port_cfg->topology = BFA_PPORT_TOPOLOGY_P2P;
	port_cfg->speed = BFA_PPORT_SPEED_AUTO;
	port_cfg->trunked = BFA_FALSE;
	port_cfg->maxfrsize = 0;

	port_cfg->trl_def_speed = BFA_PPORT_SPEED_1GBPS;

	bfa_reqq_winit(&fcport->reqq_wait, bfa_fcport_qresume, fcport);
}

static void
bfa_fcport_initdone(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	/**
	 * Initialize port attributes from IOC hardware data.
	 */
	bfa_fcport_set_wwns(fcport);
	if (fcport->cfg.maxfrsize == 0)
		fcport->cfg.maxfrsize = bfa_ioc_maxfrsize(&bfa->ioc);
	fcport->cfg.rx_bbcredit = bfa_ioc_rx_bbcredit(&bfa->ioc);
	fcport->speed_sup = bfa_ioc_speed_sup(&bfa->ioc);

	bfa_assert(fcport->cfg.maxfrsize);
	bfa_assert(fcport->cfg.rx_bbcredit);
	bfa_assert(fcport->speed_sup);
}

static void
bfa_fcport_detach(struct bfa_s *bfa)
{
}

/**
 * Called when IOC is ready.
 */
static void
bfa_fcport_start(struct bfa_s *bfa)
{
	bfa_sm_send_event(BFA_FCPORT_MOD(bfa), BFA_FCPORT_SM_START);
}

/**
 * Called before IOC is stopped.
 */
static void
bfa_fcport_stop(struct bfa_s *bfa)
{
	bfa_sm_send_event(BFA_FCPORT_MOD(bfa), BFA_FCPORT_SM_STOP);
}

/**
 * Called when IOC failure is detected.
 */
static void
bfa_fcport_iocdisable(struct bfa_s *bfa)
{
	bfa_sm_send_event(BFA_FCPORT_MOD(bfa), BFA_FCPORT_SM_HWFAIL);
}

static void
bfa_fcport_update_linkinfo(struct bfa_fcport_s *fcport)
{
	struct bfi_fcport_event_s *pevent = fcport->event_arg.i2hmsg.event;

	fcport->speed = pevent->link_state.speed;
	fcport->topology = pevent->link_state.topology;

	if (fcport->topology == BFA_PPORT_TOPOLOGY_LOOP)
		fcport->myalpa =
			pevent->link_state.tl.loop_info.myalpa;

	/*
	 * QoS Details
	 */
	bfa_os_assign(fcport->qos_attr, pevent->link_state.qos_attr);
	bfa_os_assign(fcport->qos_vc_attr, pevent->link_state.qos_vc_attr);

	bfa_trc(fcport->bfa, fcport->speed);
	bfa_trc(fcport->bfa, fcport->topology);
}

static void
bfa_fcport_reset_linkinfo(struct bfa_fcport_s *fcport)
{
	fcport->speed = BFA_PPORT_SPEED_UNKNOWN;
	fcport->topology = BFA_PPORT_TOPOLOGY_NONE;
}

/**
 * Send port enable message to firmware.
 */
static          bfa_boolean_t
bfa_fcport_send_enable(struct bfa_fcport_s *fcport)
{
	struct bfi_fcport_enable_req_s *m;

	/**
	 * Increment message tag before queue check, so that responses to old
	 * requests are discarded.
	 */
	fcport->msgtag++;

	/**
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(fcport->bfa, BFA_REQQ_PORT);
	if (!m) {
		bfa_reqq_wait(fcport->bfa, BFA_REQQ_PORT,
							&fcport->reqq_wait);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_FCPORT, BFI_FCPORT_H2I_ENABLE_REQ,
				bfa_lpuid(fcport->bfa));
	m->nwwn = fcport->nwwn;
	m->pwwn = fcport->pwwn;
	m->port_cfg = fcport->cfg;
	m->msgtag = fcport->msgtag;
	m->port_cfg.maxfrsize = bfa_os_htons(fcport->cfg.maxfrsize);
	bfa_dma_be_addr_set(m->stats_dma_addr, fcport->stats_pa);
	bfa_trc(fcport->bfa, m->stats_dma_addr.a32.addr_lo);
	bfa_trc(fcport->bfa, m->stats_dma_addr.a32.addr_hi);

	/**
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(fcport->bfa, BFA_REQQ_PORT);
	return BFA_TRUE;
}

/**
 * Send port disable message to firmware.
 */
static          bfa_boolean_t
bfa_fcport_send_disable(struct bfa_fcport_s *fcport)
{
	struct bfi_fcport_req_s *m;

	/**
	 * Increment message tag before queue check, so that responses to old
	 * requests are discarded.
	 */
	fcport->msgtag++;

	/**
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(fcport->bfa, BFA_REQQ_PORT);
	if (!m) {
		bfa_reqq_wait(fcport->bfa, BFA_REQQ_PORT,
							&fcport->reqq_wait);
		return BFA_FALSE;
	}

	bfi_h2i_set(m->mh, BFI_MC_FCPORT, BFI_FCPORT_H2I_DISABLE_REQ,
			bfa_lpuid(fcport->bfa));
	m->msgtag = fcport->msgtag;

	/**
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(fcport->bfa, BFA_REQQ_PORT);

	return BFA_TRUE;
}

static void
bfa_fcport_set_wwns(struct bfa_fcport_s *fcport)
{
	fcport->pwwn = bfa_ioc_get_pwwn(&fcport->bfa->ioc);
	fcport->nwwn = bfa_ioc_get_nwwn(&fcport->bfa->ioc);

	bfa_trc(fcport->bfa, fcport->pwwn);
	bfa_trc(fcport->bfa, fcport->nwwn);
}

static void
bfa_fcport_send_txcredit(void *port_cbarg)
{

	struct bfa_fcport_s *fcport = port_cbarg;
	struct bfi_fcport_set_svc_params_req_s *m;

	/**
	 * check for room in queue to send request now
	 */
	m = bfa_reqq_next(fcport->bfa, BFA_REQQ_PORT);
	if (!m) {
		bfa_trc(fcport->bfa, fcport->cfg.tx_bbcredit);
		return;
	}

	bfi_h2i_set(m->mh, BFI_MC_FCPORT, BFI_FCPORT_H2I_SET_SVC_PARAMS_REQ,
			bfa_lpuid(fcport->bfa));
	m->tx_bbcredit = bfa_os_htons((u16) fcport->cfg.tx_bbcredit);

	/**
	 * queue I/O message to firmware
	 */
	bfa_reqq_produce(fcport->bfa, BFA_REQQ_PORT);
}

static void
bfa_fcport_qos_stats_swap(struct bfa_qos_stats_s *d,
	struct bfa_qos_stats_s *s)
{
	u32     *dip = (u32 *) d;
	u32     *sip = (u32 *) s;
	int             i;

	/* Now swap the 32 bit fields */
	for (i = 0; i < (sizeof(struct bfa_qos_stats_s)/sizeof(u32)); ++i)
		dip[i] = bfa_os_ntohl(sip[i]);
}

static void
bfa_fcport_fcoe_stats_swap(struct bfa_fcoe_stats_s *d,
	struct bfa_fcoe_stats_s *s)
{
	u32     *dip = (u32 *) d;
	u32     *sip = (u32 *) s;
	int             i;

	for (i = 0; i < ((sizeof(struct bfa_fcoe_stats_s))/sizeof(u32));
		i = i + 2) {
#ifdef __BIGENDIAN
		dip[i] = bfa_os_ntohl(sip[i]);
		dip[i + 1] = bfa_os_ntohl(sip[i + 1]);
#else
		dip[i] = bfa_os_ntohl(sip[i + 1]);
		dip[i + 1] = bfa_os_ntohl(sip[i]);
#endif
	}
}

static void
__bfa_cb_fcport_stats_get(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_fcport_s *fcport = cbarg;

	if (complete) {
		if (fcport->stats_status == BFA_STATUS_OK) {

			/* Swap FC QoS or FCoE stats */
			if (bfa_ioc_get_fcmode(&fcport->bfa->ioc))
				bfa_fcport_qos_stats_swap(
					&fcport->stats_ret->fcqos,
					&fcport->stats->fcqos);
			else
				bfa_fcport_fcoe_stats_swap(
					&fcport->stats_ret->fcoe,
					&fcport->stats->fcoe);
		}
		fcport->stats_cbfn(fcport->stats_cbarg, fcport->stats_status);
	} else {
		fcport->stats_busy = BFA_FALSE;
		fcport->stats_status = BFA_STATUS_OK;
	}
}

static void
bfa_fcport_stats_get_timeout(void *cbarg)
{
	struct bfa_fcport_s *fcport = (struct bfa_fcport_s *) cbarg;

	bfa_trc(fcport->bfa, fcport->stats_qfull);

	if (fcport->stats_qfull) {
		bfa_reqq_wcancel(&fcport->stats_reqq_wait);
		fcport->stats_qfull = BFA_FALSE;
	}

	fcport->stats_status = BFA_STATUS_ETIMER;
	bfa_cb_queue(fcport->bfa, &fcport->hcb_qe, __bfa_cb_fcport_stats_get,
		fcport);
}

static void
bfa_fcport_send_stats_get(void *cbarg)
{
	struct bfa_fcport_s *fcport = (struct bfa_fcport_s *) cbarg;
	struct bfi_fcport_req_s *msg;

	msg = bfa_reqq_next(fcport->bfa, BFA_REQQ_PORT);

	if (!msg) {
		fcport->stats_qfull = BFA_TRUE;
		bfa_reqq_winit(&fcport->stats_reqq_wait,
				bfa_fcport_send_stats_get, fcport);
		bfa_reqq_wait(fcport->bfa, BFA_REQQ_PORT,
				&fcport->stats_reqq_wait);
		return;
	}
	fcport->stats_qfull = BFA_FALSE;

	bfa_os_memset(msg, 0, sizeof(struct bfi_fcport_req_s));
	bfi_h2i_set(msg->mh, BFI_MC_FCPORT, BFI_FCPORT_H2I_STATS_GET_REQ,
		bfa_lpuid(fcport->bfa));
	bfa_reqq_produce(fcport->bfa, BFA_REQQ_PORT);
}

static void
__bfa_cb_fcport_stats_clr(void *cbarg, bfa_boolean_t complete)
{
	struct bfa_fcport_s *fcport = cbarg;

	if (complete) {
		fcport->stats_cbfn(fcport->stats_cbarg, fcport->stats_status);
	} else {
		fcport->stats_busy = BFA_FALSE;
		fcport->stats_status = BFA_STATUS_OK;
	}
}

static void
bfa_fcport_stats_clr_timeout(void *cbarg)
{
	struct bfa_fcport_s *fcport = (struct bfa_fcport_s *) cbarg;

	bfa_trc(fcport->bfa, fcport->stats_qfull);

	if (fcport->stats_qfull) {
		bfa_reqq_wcancel(&fcport->stats_reqq_wait);
		fcport->stats_qfull = BFA_FALSE;
	}

	fcport->stats_status = BFA_STATUS_ETIMER;
	bfa_cb_queue(fcport->bfa, &fcport->hcb_qe,
			__bfa_cb_fcport_stats_clr, fcport);
}

static void
bfa_fcport_send_stats_clear(void *cbarg)
{
	struct bfa_fcport_s *fcport = (struct bfa_fcport_s *) cbarg;
	struct bfi_fcport_req_s *msg;

	msg = bfa_reqq_next(fcport->bfa, BFA_REQQ_PORT);

	if (!msg) {
		fcport->stats_qfull = BFA_TRUE;
		bfa_reqq_winit(&fcport->stats_reqq_wait,
				bfa_fcport_send_stats_clear, fcport);
		bfa_reqq_wait(fcport->bfa, BFA_REQQ_PORT,
				&fcport->stats_reqq_wait);
		return;
	}
	fcport->stats_qfull = BFA_FALSE;

	bfa_os_memset(msg, 0, sizeof(struct bfi_fcport_req_s));
	bfi_h2i_set(msg->mh, BFI_MC_FCPORT, BFI_FCPORT_H2I_STATS_CLEAR_REQ,
			bfa_lpuid(fcport->bfa));
	bfa_reqq_produce(fcport->bfa, BFA_REQQ_PORT);
}

/**
 *  bfa_pport_public
 */

/**
 * Firmware message handler.
 */
void
bfa_fcport_isr(struct bfa_s *bfa, struct bfi_msg_s *msg)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);
	union bfi_fcport_i2h_msg_u i2hmsg;

	i2hmsg.msg = msg;
	fcport->event_arg.i2hmsg = i2hmsg;

	switch (msg->mhdr.msg_id) {
	case BFI_FCPORT_I2H_ENABLE_RSP:
		if (fcport->msgtag == i2hmsg.penable_rsp->msgtag)
			bfa_sm_send_event(fcport, BFA_FCPORT_SM_FWRSP);
		break;

	case BFI_FCPORT_I2H_DISABLE_RSP:
		if (fcport->msgtag == i2hmsg.penable_rsp->msgtag)
			bfa_sm_send_event(fcport, BFA_FCPORT_SM_FWRSP);
		break;

	case BFI_FCPORT_I2H_EVENT:
		switch (i2hmsg.event->link_state.linkstate) {
		case BFA_PPORT_LINKUP:
			bfa_sm_send_event(fcport, BFA_FCPORT_SM_LINKUP);
			break;
		case BFA_PPORT_LINKDOWN:
			bfa_sm_send_event(fcport, BFA_FCPORT_SM_LINKDOWN);
			break;
		case BFA_PPORT_TRUNK_LINKDOWN:
			/** todo: event notification */
			break;
		}
		break;

	case BFI_FCPORT_I2H_STATS_GET_RSP:
		/*
		 * check for timer pop before processing the rsp
		 */
		if (fcport->stats_busy == BFA_FALSE ||
			fcport->stats_status == BFA_STATUS_ETIMER)
			break;

		bfa_timer_stop(&fcport->timer);
		fcport->stats_status = i2hmsg.pstatsget_rsp->status;
		bfa_cb_queue(fcport->bfa, &fcport->hcb_qe,
				__bfa_cb_fcport_stats_get, fcport);
		break;

	case BFI_FCPORT_I2H_STATS_CLEAR_RSP:
		/*
		 * check for timer pop before processing the rsp
		 */
		if (fcport->stats_busy == BFA_FALSE ||
			fcport->stats_status == BFA_STATUS_ETIMER)
			break;

		bfa_timer_stop(&fcport->timer);
		fcport->stats_status = BFA_STATUS_OK;
		bfa_cb_queue(fcport->bfa, &fcport->hcb_qe,
				__bfa_cb_fcport_stats_clr, fcport);
		break;

	default:
		bfa_assert(0);
	break;
	}
}

/**
 *  bfa_pport_api
 */

/**
 * Registered callback for port events.
 */
void
bfa_fcport_event_register(struct bfa_s *bfa,
			 void (*cbfn) (void *cbarg, bfa_pport_event_t event),
			 void *cbarg)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	fcport->event_cbfn = cbfn;
	fcport->event_cbarg = cbarg;
}

bfa_status_t
bfa_fcport_enable(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	if (fcport->diag_busy)
		return BFA_STATUS_DIAG_BUSY;
	else if (bfa_sm_cmp_state
		 (BFA_FCPORT_MOD(bfa), bfa_fcport_sm_disabling_qwait))
		return BFA_STATUS_DEVBUSY;

	bfa_sm_send_event(BFA_FCPORT_MOD(bfa), BFA_FCPORT_SM_ENABLE);
	return BFA_STATUS_OK;
}

bfa_status_t
bfa_fcport_disable(struct bfa_s *bfa)
{
	bfa_sm_send_event(BFA_FCPORT_MOD(bfa), BFA_FCPORT_SM_DISABLE);
	return BFA_STATUS_OK;
}

/**
 * Configure port speed.
 */
bfa_status_t
bfa_fcport_cfg_speed(struct bfa_s *bfa, enum bfa_pport_speed speed)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, speed);

	if ((speed != BFA_PPORT_SPEED_AUTO) && (speed > fcport->speed_sup)) {
		bfa_trc(bfa, fcport->speed_sup);
		return BFA_STATUS_UNSUPP_SPEED;
	}

	fcport->cfg.speed = speed;

	return BFA_STATUS_OK;
}

/**
 * Get current speed.
 */
enum bfa_pport_speed
bfa_fcport_get_speed(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return fcport->speed;
}

/**
 * Configure port topology.
 */
bfa_status_t
bfa_fcport_cfg_topology(struct bfa_s *bfa, enum bfa_pport_topology topology)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, topology);
	bfa_trc(bfa, fcport->cfg.topology);

	switch (topology) {
	case BFA_PPORT_TOPOLOGY_P2P:
	case BFA_PPORT_TOPOLOGY_LOOP:
	case BFA_PPORT_TOPOLOGY_AUTO:
		break;

	default:
		return BFA_STATUS_EINVAL;
	}

	fcport->cfg.topology = topology;
	return BFA_STATUS_OK;
}

/**
 * Get current topology.
 */
enum bfa_pport_topology
bfa_fcport_get_topology(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return fcport->topology;
}

bfa_status_t
bfa_fcport_cfg_hardalpa(struct bfa_s *bfa, u8 alpa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, alpa);
	bfa_trc(bfa, fcport->cfg.cfg_hardalpa);
	bfa_trc(bfa, fcport->cfg.hardalpa);

	fcport->cfg.cfg_hardalpa = BFA_TRUE;
	fcport->cfg.hardalpa = alpa;

	return BFA_STATUS_OK;
}

bfa_status_t
bfa_fcport_clr_hardalpa(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, fcport->cfg.cfg_hardalpa);
	bfa_trc(bfa, fcport->cfg.hardalpa);

	fcport->cfg.cfg_hardalpa = BFA_FALSE;
	return BFA_STATUS_OK;
}

bfa_boolean_t
bfa_fcport_get_hardalpa(struct bfa_s *bfa, u8 *alpa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	*alpa = fcport->cfg.hardalpa;
	return fcport->cfg.cfg_hardalpa;
}

u8
bfa_fcport_get_myalpa(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return fcport->myalpa;
}

bfa_status_t
bfa_fcport_cfg_maxfrsize(struct bfa_s *bfa, u16 maxfrsize)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, maxfrsize);
	bfa_trc(bfa, fcport->cfg.maxfrsize);

	/*
	 * with in range
	 */
	if ((maxfrsize > FC_MAX_PDUSZ) || (maxfrsize < FC_MIN_PDUSZ))
		return BFA_STATUS_INVLD_DFSZ;

	/*
	 * power of 2, if not the max frame size of 2112
	 */
	if ((maxfrsize != FC_MAX_PDUSZ) && (maxfrsize & (maxfrsize - 1)))
		return BFA_STATUS_INVLD_DFSZ;

	fcport->cfg.maxfrsize = maxfrsize;
	return BFA_STATUS_OK;
}

u16
bfa_fcport_get_maxfrsize(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return fcport->cfg.maxfrsize;
}

u32
bfa_fcport_mypid(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return fcport->mypid;
}

u8
bfa_fcport_get_rx_bbcredit(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return fcport->cfg.rx_bbcredit;
}

void
bfa_fcport_set_tx_bbcredit(struct bfa_s *bfa, u16 tx_bbcredit)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	fcport->cfg.tx_bbcredit = (u8) tx_bbcredit;
	bfa_fcport_send_txcredit(fcport);
}

/**
 * Get port attributes.
 */

wwn_t
bfa_fcport_get_wwn(struct bfa_s *bfa, bfa_boolean_t node)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);
	if (node)
		return fcport->nwwn;
	else
		return fcport->pwwn;
}

void
bfa_fcport_get_attr(struct bfa_s *bfa, struct bfa_pport_attr_s *attr)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_os_memset(attr, 0, sizeof(struct bfa_pport_attr_s));

	attr->nwwn = fcport->nwwn;
	attr->pwwn = fcport->pwwn;

	bfa_os_memcpy(&attr->pport_cfg, &fcport->cfg,
		      sizeof(struct bfa_pport_cfg_s));
	/*
	 * speed attributes
	 */
	attr->pport_cfg.speed = fcport->cfg.speed;
	attr->speed_supported = fcport->speed_sup;
	attr->speed = fcport->speed;
	attr->cos_supported = FC_CLASS_3;

	/*
	 * topology attributes
	 */
	attr->pport_cfg.topology = fcport->cfg.topology;
	attr->topology = fcport->topology;

	/*
	 * beacon attributes
	 */
	attr->beacon = fcport->beacon;
	attr->link_e2e_beacon = fcport->link_e2e_beacon;
	attr->plog_enabled = bfa_plog_get_setting(fcport->bfa->plog);

	attr->pport_cfg.path_tov = bfa_fcpim_path_tov_get(bfa);
	attr->pport_cfg.q_depth = bfa_fcpim_qdepth_get(bfa);
	attr->port_state = bfa_sm_to_state(hal_pport_sm_table, fcport->sm);
	if (bfa_ioc_is_disabled(&fcport->bfa->ioc))
		attr->port_state = BFA_PPORT_ST_IOCDIS;
	else if (bfa_ioc_fw_mismatch(&fcport->bfa->ioc))
		attr->port_state = BFA_PPORT_ST_FWMISMATCH;
}

#define BFA_FCPORT_STATS_TOV	1000

/**
 * Fetch port attributes (FCQoS or FCoE).
 */
bfa_status_t
bfa_fcport_get_stats(struct bfa_s *bfa, union bfa_fcport_stats_u *stats,
		    bfa_cb_pport_t cbfn, void *cbarg)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	if (fcport->stats_busy) {
		bfa_trc(bfa, fcport->stats_busy);
		return BFA_STATUS_DEVBUSY;
	}

	fcport->stats_busy  = BFA_TRUE;
	fcport->stats_ret   = stats;
	fcport->stats_cbfn  = cbfn;
	fcport->stats_cbarg = cbarg;

	bfa_fcport_send_stats_get(fcport);

	bfa_timer_start(bfa, &fcport->timer, bfa_fcport_stats_get_timeout,
		fcport, BFA_FCPORT_STATS_TOV);
	return BFA_STATUS_OK;
}

/**
 * Reset port statistics (FCQoS or FCoE).
 */
bfa_status_t
bfa_fcport_clear_stats(struct bfa_s *bfa, bfa_cb_pport_t cbfn, void *cbarg)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	if (fcport->stats_busy) {
		bfa_trc(bfa, fcport->stats_busy);
		return BFA_STATUS_DEVBUSY;
	}

	fcport->stats_busy = BFA_TRUE;
	fcport->stats_cbfn = cbfn;
	fcport->stats_cbarg = cbarg;

	bfa_fcport_send_stats_clear(fcport);

	bfa_timer_start(bfa, &fcport->timer, bfa_fcport_stats_clr_timeout,
			fcport, BFA_FCPORT_STATS_TOV);
	return BFA_STATUS_OK;
}

/**
 * Fetch FCQoS port statistics
 */
bfa_status_t
bfa_fcport_get_qos_stats(struct bfa_s *bfa, union bfa_fcport_stats_u *stats,
	bfa_cb_pport_t cbfn, void *cbarg)
{
	/* Meaningful only for FC mode */
	bfa_assert(bfa_ioc_get_fcmode(&bfa->ioc));

	return bfa_fcport_get_stats(bfa, stats, cbfn, cbarg);
}

/**
 * Reset FCoE port statistics
 */
bfa_status_t
bfa_fcport_clear_qos_stats(struct bfa_s *bfa, bfa_cb_pport_t cbfn, void *cbarg)
{
	/* Meaningful only for FC mode */
	bfa_assert(bfa_ioc_get_fcmode(&bfa->ioc));

	return bfa_fcport_clear_stats(bfa, cbfn, cbarg);
}

/**
 * Fetch FCQoS port statistics
 */
bfa_status_t
bfa_fcport_get_fcoe_stats(struct bfa_s *bfa, union bfa_fcport_stats_u *stats,
	bfa_cb_pport_t cbfn, void *cbarg)
{
	/* Meaningful only for FCoE mode */
	bfa_assert(!bfa_ioc_get_fcmode(&bfa->ioc));

	return bfa_fcport_get_stats(bfa, stats, cbfn, cbarg);
}

/**
 * Reset FCoE port statistics
 */
bfa_status_t
bfa_fcport_clear_fcoe_stats(struct bfa_s *bfa, bfa_cb_pport_t cbfn, void *cbarg)
{
	/* Meaningful only for FCoE mode */
	bfa_assert(!bfa_ioc_get_fcmode(&bfa->ioc));

	return bfa_fcport_clear_stats(bfa, cbfn, cbarg);
}

bfa_status_t
bfa_fcport_trunk_enable(struct bfa_s *bfa, u8 bitmap)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, bitmap);
	bfa_trc(bfa, fcport->cfg.trunked);
	bfa_trc(bfa, fcport->cfg.trunk_ports);

	if (!bitmap || (bitmap & (bitmap - 1)))
		return BFA_STATUS_EINVAL;

	fcport->cfg.trunked = BFA_TRUE;
	fcport->cfg.trunk_ports = bitmap;

	return BFA_STATUS_OK;
}

void
bfa_fcport_qos_get_attr(struct bfa_s *bfa, struct bfa_qos_attr_s *qos_attr)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	qos_attr->state = bfa_os_ntohl(fcport->qos_attr.state);
	qos_attr->total_bb_cr = bfa_os_ntohl(fcport->qos_attr.total_bb_cr);
}

void
bfa_fcport_qos_get_vc_attr(struct bfa_s *bfa,
			  struct bfa_qos_vc_attr_s *qos_vc_attr)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);
	struct bfa_qos_vc_attr_s *bfa_vc_attr = &fcport->qos_vc_attr;
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
 * Fetch port attributes.
 */
bfa_status_t
bfa_fcport_trunk_disable(struct bfa_s *bfa)
{
	return BFA_STATUS_OK;
}

bfa_boolean_t
bfa_fcport_trunk_query(struct bfa_s *bfa, u32 *bitmap)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	*bitmap = fcport->cfg.trunk_ports;
	return fcport->cfg.trunked;
}

bfa_boolean_t
bfa_fcport_is_disabled(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return bfa_sm_to_state(hal_pport_sm_table, fcport->sm) ==
		BFA_PPORT_ST_DISABLED;

}

bfa_boolean_t
bfa_fcport_is_ratelim(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	return fcport->cfg.ratelimit ? BFA_TRUE : BFA_FALSE;

}

void
bfa_fcport_cfg_qos(struct bfa_s *bfa, bfa_boolean_t on_off)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);
	enum bfa_ioc_type_e ioc_type = bfa_get_type(bfa);

	bfa_trc(bfa, on_off);
	bfa_trc(bfa, fcport->cfg.qos_enabled);

	bfa_trc(bfa, ioc_type);

	if (ioc_type == BFA_IOC_TYPE_FC)
		fcport->cfg.qos_enabled = on_off;
}

void
bfa_fcport_cfg_ratelim(struct bfa_s *bfa, bfa_boolean_t on_off)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, on_off);
	bfa_trc(bfa, fcport->cfg.ratelimit);

	fcport->cfg.ratelimit = on_off;
	if (fcport->cfg.trl_def_speed == BFA_PPORT_SPEED_UNKNOWN)
		fcport->cfg.trl_def_speed = BFA_PPORT_SPEED_1GBPS;
}

/**
 * Configure default minimum ratelim speed
 */
bfa_status_t
bfa_fcport_cfg_ratelim_speed(struct bfa_s *bfa, enum bfa_pport_speed speed)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, speed);

	/*
	 * Auto and speeds greater than the supported speed, are invalid
	 */
	if ((speed == BFA_PPORT_SPEED_AUTO) || (speed > fcport->speed_sup)) {
		bfa_trc(bfa, fcport->speed_sup);
		return BFA_STATUS_UNSUPP_SPEED;
	}

	fcport->cfg.trl_def_speed = speed;

	return BFA_STATUS_OK;
}

/**
 * Get default minimum ratelim speed
 */
enum bfa_pport_speed
bfa_fcport_get_ratelim_speed(struct bfa_s *bfa)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, fcport->cfg.trl_def_speed);
	return fcport->cfg.trl_def_speed;

}

void
bfa_fcport_busy(struct bfa_s *bfa, bfa_boolean_t status)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, status);
	bfa_trc(bfa, fcport->diag_busy);

	fcport->diag_busy = status;
}

void
bfa_fcport_beacon(struct bfa_s *bfa, bfa_boolean_t beacon,
		 bfa_boolean_t link_e2e_beacon)
{
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(bfa);

	bfa_trc(bfa, beacon);
	bfa_trc(bfa, link_e2e_beacon);
	bfa_trc(bfa, fcport->beacon);
	bfa_trc(bfa, fcport->link_e2e_beacon);

	fcport->beacon = beacon;
	fcport->link_e2e_beacon = link_e2e_beacon;
}

bfa_boolean_t
bfa_fcport_is_linkup(struct bfa_s *bfa)
{
	return bfa_sm_cmp_state(BFA_FCPORT_MOD(bfa), bfa_fcport_sm_linkup);
}


