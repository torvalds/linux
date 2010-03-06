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
 *  port_api.c BFA FCS port
 */


#include <bfa.h>
#include <bfa_svc.h>
#include "fcs_lport.h"
#include "fcs_rport.h"
#include "lport_priv.h"
#include "fcs_trcmod.h"
#include "fcs_fcxp.h"
#include <fcs/bfa_fcs_fdmi.h>

BFA_TRC_FILE(FCS, FDMI);

#define BFA_FCS_FDMI_CMD_MAX_RETRIES 2

/*
 * forward declarations
 */
static void     bfa_fcs_port_fdmi_send_rhba(void *fdmi_cbarg,
					    struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_port_fdmi_send_rprt(void *fdmi_cbarg,
					    struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_port_fdmi_send_rpa(void *fdmi_cbarg,
					   struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_port_fdmi_rhba_response(void *fcsarg,
						struct bfa_fcxp_s *fcxp,
						void *cbarg,
						bfa_status_t req_status,
						u32 rsp_len,
						u32 resid_len,
						struct fchs_s *rsp_fchs);
static void     bfa_fcs_port_fdmi_rprt_response(void *fcsarg,
						struct bfa_fcxp_s *fcxp,
						void *cbarg,
						bfa_status_t req_status,
						u32 rsp_len,
						u32 resid_len,
						struct fchs_s *rsp_fchs);
static void     bfa_fcs_port_fdmi_rpa_response(void *fcsarg,
					       struct bfa_fcxp_s *fcxp,
					       void *cbarg,
					       bfa_status_t req_status,
					       u32 rsp_len,
					       u32 resid_len,
					       struct fchs_s *rsp_fchs);
static void     bfa_fcs_port_fdmi_timeout(void *arg);
static u16 bfa_fcs_port_fdmi_build_rhba_pyld(
			struct bfa_fcs_port_fdmi_s *fdmi, u8 *pyld);
static u16 bfa_fcs_port_fdmi_build_rprt_pyld(
			struct bfa_fcs_port_fdmi_s *fdmi, u8 *pyld);
static u16 bfa_fcs_port_fdmi_build_rpa_pyld(
			struct bfa_fcs_port_fdmi_s *fdmi, u8 *pyld);
static u16 bfa_fcs_port_fdmi_build_portattr_block(
			struct bfa_fcs_port_fdmi_s *fdmi, u8 *pyld);
static void bfa_fcs_fdmi_get_hbaattr(struct bfa_fcs_port_fdmi_s *fdmi,
			struct bfa_fcs_fdmi_hba_attr_s *hba_attr);
static void bfa_fcs_fdmi_get_portattr(struct bfa_fcs_port_fdmi_s *fdmi,
			struct bfa_fcs_fdmi_port_attr_s *port_attr);
/**
 *  fcs_fdmi_sm FCS FDMI state machine
 */

/**
 *  FDMI State Machine events
 */
enum port_fdmi_event {
	FDMISM_EVENT_PORT_ONLINE = 1,
	FDMISM_EVENT_PORT_OFFLINE = 2,
	FDMISM_EVENT_RSP_OK = 4,
	FDMISM_EVENT_RSP_ERROR = 5,
	FDMISM_EVENT_TIMEOUT = 6,
	FDMISM_EVENT_RHBA_SENT = 7,
	FDMISM_EVENT_RPRT_SENT = 8,
	FDMISM_EVENT_RPA_SENT = 9,
};

static void bfa_fcs_port_fdmi_sm_offline(struct bfa_fcs_port_fdmi_s *fdmi,
			enum port_fdmi_event event);
static void bfa_fcs_port_fdmi_sm_sending_rhba(struct bfa_fcs_port_fdmi_s *fdmi,
			enum port_fdmi_event event);
static void bfa_fcs_port_fdmi_sm_rhba(struct bfa_fcs_port_fdmi_s *fdmi,
			enum port_fdmi_event event);
static void bfa_fcs_port_fdmi_sm_rhba_retry(struct bfa_fcs_port_fdmi_s *fdmi,
			enum port_fdmi_event event);
static void bfa_fcs_port_fdmi_sm_sending_rprt(struct bfa_fcs_port_fdmi_s *fdmi,
			enum port_fdmi_event event);
static void bfa_fcs_port_fdmi_sm_rprt(struct bfa_fcs_port_fdmi_s *fdmi,
			enum port_fdmi_event event);
static void bfa_fcs_port_fdmi_sm_rprt_retry(struct bfa_fcs_port_fdmi_s *fdmi,
			enum port_fdmi_event event);
static void bfa_fcs_port_fdmi_sm_sending_rpa(struct bfa_fcs_port_fdmi_s *fdmi,
			enum port_fdmi_event event);
static void     bfa_fcs_port_fdmi_sm_rpa(struct bfa_fcs_port_fdmi_s *fdmi,
			enum port_fdmi_event event);
static void     bfa_fcs_port_fdmi_sm_rpa_retry(struct bfa_fcs_port_fdmi_s *fdmi,
			enum port_fdmi_event event);
static void     bfa_fcs_port_fdmi_sm_online(struct bfa_fcs_port_fdmi_s *fdmi,
			enum port_fdmi_event event);
static void	bfa_fcs_port_fdmi_sm_disabled(struct bfa_fcs_port_fdmi_s *fdmi,
			enum port_fdmi_event event);

/**
 * 		Start in offline state - awaiting MS to send start.
 */
static void
bfa_fcs_port_fdmi_sm_offline(struct bfa_fcs_port_fdmi_s *fdmi,
			     enum port_fdmi_event event)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	fdmi->retry_cnt = 0;

	switch (event) {
	case FDMISM_EVENT_PORT_ONLINE:
		if (port->vport) {
			/*
			 * For Vports, register a new port.
			 */
			bfa_sm_set_state(fdmi,
					 bfa_fcs_port_fdmi_sm_sending_rprt);
			bfa_fcs_port_fdmi_send_rprt(fdmi, NULL);
		} else {
			/*
			 * For a base port, we should first register the HBA
			 * atribute. The HBA attribute also contains the base
			 *  port registration.
			 */
			bfa_sm_set_state(fdmi,
					 bfa_fcs_port_fdmi_sm_sending_rhba);
			bfa_fcs_port_fdmi_send_rhba(fdmi, NULL);
		}
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_port_fdmi_sm_sending_rhba(struct bfa_fcs_port_fdmi_s *fdmi,
				  enum port_fdmi_event event)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_RHBA_SENT:
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_rhba);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(port),
				       &fdmi->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_port_fdmi_sm_rhba(struct bfa_fcs_port_fdmi_s *fdmi,
			  enum port_fdmi_event event)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_RSP_ERROR:
		/*
		 * if max retries have not been reached, start timer for a
		 * delayed retry
		 */
		if (fdmi->retry_cnt++ < BFA_FCS_FDMI_CMD_MAX_RETRIES) {
			bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_rhba_retry);
			bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(port),
					&fdmi->timer, bfa_fcs_port_fdmi_timeout,
					fdmi, BFA_FCS_RETRY_TIMEOUT);
		} else {
			/*
			 * set state to offline
			 */
			bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_offline);
		}
		break;

	case FDMISM_EVENT_RSP_OK:
		/*
		 * Initiate Register Port Attributes
		 */
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_sending_rpa);
		fdmi->retry_cnt = 0;
		bfa_fcs_port_fdmi_send_rpa(fdmi, NULL);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_fcxp_discard(fdmi->fcxp);
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_offline);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_port_fdmi_sm_rhba_retry(struct bfa_fcs_port_fdmi_s *fdmi,
				enum port_fdmi_event event)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_sending_rhba);
		bfa_fcs_port_fdmi_send_rhba(fdmi, NULL);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_offline);
		bfa_timer_stop(&fdmi->timer);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

/*
* RPRT : Register Port
 */
static void
bfa_fcs_port_fdmi_sm_sending_rprt(struct bfa_fcs_port_fdmi_s *fdmi,
				  enum port_fdmi_event event)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_RPRT_SENT:
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_rprt);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(port),
				       &fdmi->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_port_fdmi_sm_rprt(struct bfa_fcs_port_fdmi_s *fdmi,
			  enum port_fdmi_event event)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_RSP_ERROR:
		/*
		 * if max retries have not been reached, start timer for a
		 * delayed retry
		 */
		if (fdmi->retry_cnt++ < BFA_FCS_FDMI_CMD_MAX_RETRIES) {
			bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_rprt_retry);
			bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(port),
					&fdmi->timer, bfa_fcs_port_fdmi_timeout,
					fdmi, BFA_FCS_RETRY_TIMEOUT);

		} else {
			/*
			 * set state to offline
			 */
			bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_offline);
			fdmi->retry_cnt = 0;
		}
		break;

	case FDMISM_EVENT_RSP_OK:
		fdmi->retry_cnt = 0;
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_online);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_fcxp_discard(fdmi->fcxp);
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_offline);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_port_fdmi_sm_rprt_retry(struct bfa_fcs_port_fdmi_s *fdmi,
				enum port_fdmi_event event)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_sending_rprt);
		bfa_fcs_port_fdmi_send_rprt(fdmi, NULL);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_offline);
		bfa_timer_stop(&fdmi->timer);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

/*
 * Register Port Attributes
 */
static void
bfa_fcs_port_fdmi_sm_sending_rpa(struct bfa_fcs_port_fdmi_s *fdmi,
				 enum port_fdmi_event event)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_RPA_SENT:
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_rpa);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(port),
				       &fdmi->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_port_fdmi_sm_rpa(struct bfa_fcs_port_fdmi_s *fdmi,
			 enum port_fdmi_event event)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_RSP_ERROR:
		/*
		 * if max retries have not been reached, start timer for a
		 * delayed retry
		 */
		if (fdmi->retry_cnt++ < BFA_FCS_FDMI_CMD_MAX_RETRIES) {
			bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_rpa_retry);
			bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(port),
					&fdmi->timer, bfa_fcs_port_fdmi_timeout,
					fdmi, BFA_FCS_RETRY_TIMEOUT);
		} else {
			/*
			 * set state to offline
			 */
			bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_offline);
			fdmi->retry_cnt = 0;
		}
		break;

	case FDMISM_EVENT_RSP_OK:
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_online);
		fdmi->retry_cnt = 0;
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_fcxp_discard(fdmi->fcxp);
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_offline);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_port_fdmi_sm_rpa_retry(struct bfa_fcs_port_fdmi_s *fdmi,
			       enum port_fdmi_event event)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_sending_rpa);
		bfa_fcs_port_fdmi_send_rpa(fdmi, NULL);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_offline);
		bfa_timer_stop(&fdmi->timer);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_port_fdmi_sm_online(struct bfa_fcs_port_fdmi_s *fdmi,
			    enum port_fdmi_event event)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_offline);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

/**
 *  FDMI is disabled state.
 */
static void
bfa_fcs_port_fdmi_sm_disabled(struct bfa_fcs_port_fdmi_s *fdmi,
				enum port_fdmi_event event)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	/* No op State. It can only be enabled at Driver Init. */
}

/**
*   RHBA : Register HBA Attributes.
 */
static void
bfa_fcs_port_fdmi_send_rhba(void *fdmi_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_port_fdmi_s *fdmi = fdmi_cbarg;
	struct bfa_fcs_port_s *port = fdmi->ms->port;
	struct fchs_s          fchs;
	int             len, attr_len;
	struct bfa_fcxp_s *fcxp;
	u8        *pyld;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		bfa_fcxp_alloc_wait(port->fcs->bfa, &fdmi->fcxp_wqe,
				    bfa_fcs_port_fdmi_send_rhba, fdmi);
		return;
	}
	fdmi->fcxp = fcxp;

	pyld = bfa_fcxp_get_reqbuf(fcxp);
	bfa_os_memset(pyld, 0, FC_MAX_PDUSZ);

	len = fc_fdmi_reqhdr_build(&fchs, pyld, bfa_fcs_port_get_fcid(port),
				   FDMI_RHBA);

	attr_len = bfa_fcs_port_fdmi_build_rhba_pyld(fdmi,
			(u8 *) ((struct ct_hdr_s *) pyld + 1));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, (len + attr_len), &fchs,
		      bfa_fcs_port_fdmi_rhba_response, (void *)fdmi,
		      FC_MAX_PDUSZ, FC_RA_TOV);

	bfa_sm_send_event(fdmi, FDMISM_EVENT_RHBA_SENT);
}

static          u16
bfa_fcs_port_fdmi_build_rhba_pyld(struct bfa_fcs_port_fdmi_s *fdmi,
				  u8 *pyld)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;
	struct bfa_fcs_fdmi_hba_attr_s hba_attr;	/* @todo */
	struct bfa_fcs_fdmi_hba_attr_s *fcs_hba_attr = &hba_attr; /* @todo */
	struct fdmi_rhba_s    *rhba = (struct fdmi_rhba_s *) pyld;
	struct fdmi_attr_s    *attr;
	u8        *curr_ptr;
	u16        len, count;

	/*
	 * get hba attributes
	 */
	bfa_fcs_fdmi_get_hbaattr(fdmi, fcs_hba_attr);

	rhba->hba_id = bfa_fcs_port_get_pwwn(port);
	rhba->port_list.num_ports = bfa_os_htonl(1);
	rhba->port_list.port_entry = bfa_fcs_port_get_pwwn(port);

	len = sizeof(rhba->hba_id) + sizeof(rhba->port_list);

	count = 0;
	len += sizeof(rhba->hba_attr_blk.attr_count);

	/*
	 * fill out the invididual entries of the HBA attrib Block
	 */
	curr_ptr = (u8 *) &rhba->hba_attr_blk.hba_attr;

	/*
	 * Node Name
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = bfa_os_htons(FDMI_HBA_ATTRIB_NODENAME);
	attr->len = sizeof(wwn_t);
	memcpy(attr->value, &bfa_fcs_port_get_nwwn(port), attr->len);
	curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
	len += attr->len;
	count++;
	attr->len =
		bfa_os_htons(attr->len + sizeof(attr->type) +
			     sizeof(attr->len));

	/*
	 * Manufacturer
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = bfa_os_htons(FDMI_HBA_ATTRIB_MANUFACTURER);
	attr->len = (u16) strlen(fcs_hba_attr->manufacturer);
	memcpy(attr->value, fcs_hba_attr->manufacturer, attr->len);
	/* variable fields need to be 4 byte aligned */
	attr->len = fc_roundup(attr->len, sizeof(u32));
	curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
	len += attr->len;
	count++;
	attr->len =
		bfa_os_htons(attr->len + sizeof(attr->type) +
			     sizeof(attr->len));

	/*
	 * Serial Number
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = bfa_os_htons(FDMI_HBA_ATTRIB_SERIALNUM);
	attr->len = (u16) strlen(fcs_hba_attr->serial_num);
	memcpy(attr->value, fcs_hba_attr->serial_num, attr->len);
	/* variable fields need to be 4 byte aligned */
	attr->len = fc_roundup(attr->len, sizeof(u32));
	curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
	len += attr->len;
	count++;
	attr->len =
		bfa_os_htons(attr->len + sizeof(attr->type) +
			     sizeof(attr->len));

	/*
	 * Model
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = bfa_os_htons(FDMI_HBA_ATTRIB_MODEL);
	attr->len = (u16) strlen(fcs_hba_attr->model);
	memcpy(attr->value, fcs_hba_attr->model, attr->len);
	/* variable fields need to be 4 byte aligned */
	attr->len = fc_roundup(attr->len, sizeof(u32));
	curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
	len += attr->len;
	count++;
	attr->len =
		bfa_os_htons(attr->len + sizeof(attr->type) +
			     sizeof(attr->len));

	/*
	 * Model Desc
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = bfa_os_htons(FDMI_HBA_ATTRIB_MODEL_DESC);
	attr->len = (u16) strlen(fcs_hba_attr->model_desc);
	memcpy(attr->value, fcs_hba_attr->model_desc, attr->len);
	/* variable fields need to be 4 byte aligned */
	attr->len = fc_roundup(attr->len, sizeof(u32));
	curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
	len += attr->len;
	count++;
	attr->len =
		bfa_os_htons(attr->len + sizeof(attr->type) +
			     sizeof(attr->len));

	/*
	 * H/W Version
	 */
	if (fcs_hba_attr->hw_version[0] != '\0') {
		attr = (struct fdmi_attr_s *) curr_ptr;
		attr->type = bfa_os_htons(FDMI_HBA_ATTRIB_HW_VERSION);
		attr->len = (u16) strlen(fcs_hba_attr->hw_version);
		memcpy(attr->value, fcs_hba_attr->hw_version, attr->len);
		/* variable fields need to be 4 byte aligned */
		attr->len = fc_roundup(attr->len, sizeof(u32));
		curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
		len += attr->len;
		count++;
		attr->len =
			bfa_os_htons(attr->len + sizeof(attr->type) +
				     sizeof(attr->len));
	}

	/*
	 * Driver Version
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = bfa_os_htons(FDMI_HBA_ATTRIB_DRIVER_VERSION);
	attr->len = (u16) strlen(fcs_hba_attr->driver_version);
	memcpy(attr->value, fcs_hba_attr->driver_version, attr->len);
	/* variable fields need to be 4 byte aligned */
	attr->len = fc_roundup(attr->len, sizeof(u32));
	curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
	len += attr->len;;
	count++;
	attr->len =
		bfa_os_htons(attr->len + sizeof(attr->type) +
			     sizeof(attr->len));

	/*
	 * Option Rom Version
	 */
	if (fcs_hba_attr->option_rom_ver[0] != '\0') {
		attr = (struct fdmi_attr_s *) curr_ptr;
		attr->type = bfa_os_htons(FDMI_HBA_ATTRIB_ROM_VERSION);
		attr->len = (u16) strlen(fcs_hba_attr->option_rom_ver);
		memcpy(attr->value, fcs_hba_attr->option_rom_ver, attr->len);
		/* variable fields need to be 4 byte aligned */
		attr->len = fc_roundup(attr->len, sizeof(u32));
		curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
		len += attr->len;
		count++;
		attr->len =
			bfa_os_htons(attr->len + sizeof(attr->type) +
				     sizeof(attr->len));
	}

	/*
	 * f/w Version = driver version
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = bfa_os_htons(FDMI_HBA_ATTRIB_FW_VERSION);
	attr->len = (u16) strlen(fcs_hba_attr->driver_version);
	memcpy(attr->value, fcs_hba_attr->driver_version, attr->len);
	/* variable fields need to be 4 byte aligned */
	attr->len = fc_roundup(attr->len, sizeof(u32));
	curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
	len += attr->len;
	count++;
	attr->len =
		bfa_os_htons(attr->len + sizeof(attr->type) +
			     sizeof(attr->len));

	/*
	 * OS Name
	 */
	if (fcs_hba_attr->os_name[0] != '\0') {
		attr = (struct fdmi_attr_s *) curr_ptr;
		attr->type = bfa_os_htons(FDMI_HBA_ATTRIB_OS_NAME);
		attr->len = (u16) strlen(fcs_hba_attr->os_name);
		memcpy(attr->value, fcs_hba_attr->os_name, attr->len);
		/* variable fields need to be 4 byte aligned */
		attr->len = fc_roundup(attr->len, sizeof(u32));
		curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
		len += attr->len;
		count++;
		attr->len =
			bfa_os_htons(attr->len + sizeof(attr->type) +
				     sizeof(attr->len));
	}

	/*
	 * MAX_CT_PAYLOAD
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = bfa_os_htons(FDMI_HBA_ATTRIB_MAX_CT);
	attr->len = sizeof(fcs_hba_attr->max_ct_pyld);
	memcpy(attr->value, &fcs_hba_attr->max_ct_pyld, attr->len);
	len += attr->len;
	count++;
	attr->len =
		bfa_os_htons(attr->len + sizeof(attr->type) +
			     sizeof(attr->len));

	/*
	 * Update size of payload
	 */
	len += ((sizeof(attr->type) + sizeof(attr->len)) * count);

	rhba->hba_attr_blk.attr_count = bfa_os_htonl(count);
	return len;
}

static void
bfa_fcs_port_fdmi_rhba_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
				void *cbarg, bfa_status_t req_status,
				u32 rsp_len, u32 resid_len,
				struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_port_fdmi_s *fdmi = (struct bfa_fcs_port_fdmi_s *)cbarg;
	struct bfa_fcs_port_s *port = fdmi->ms->port;
	struct ct_hdr_s       *cthdr = NULL;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		bfa_sm_send_event(fdmi, FDMISM_EVENT_RSP_ERROR);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = bfa_os_ntohs(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		bfa_sm_send_event(fdmi, FDMISM_EVENT_RSP_OK);
		return;
	}

	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(fdmi, FDMISM_EVENT_RSP_ERROR);
}

/**
*   RPRT : Register Port
 */
static void
bfa_fcs_port_fdmi_send_rprt(void *fdmi_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_port_fdmi_s *fdmi = fdmi_cbarg;
	struct bfa_fcs_port_s *port = fdmi->ms->port;
	struct fchs_s          fchs;
	u16        len, attr_len;
	struct bfa_fcxp_s *fcxp;
	u8        *pyld;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		bfa_fcxp_alloc_wait(port->fcs->bfa, &fdmi->fcxp_wqe,
				    bfa_fcs_port_fdmi_send_rprt, fdmi);
		return;
	}
	fdmi->fcxp = fcxp;

	pyld = bfa_fcxp_get_reqbuf(fcxp);
	bfa_os_memset(pyld, 0, FC_MAX_PDUSZ);

	len = fc_fdmi_reqhdr_build(&fchs, pyld, bfa_fcs_port_get_fcid(port),
				   FDMI_RPRT);

	attr_len = bfa_fcs_port_fdmi_build_rprt_pyld(fdmi,
			(u8 *) ((struct ct_hdr_s *) pyld + 1));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len + attr_len, &fchs,
		      bfa_fcs_port_fdmi_rprt_response, (void *)fdmi,
		      FC_MAX_PDUSZ, FC_RA_TOV);

	bfa_sm_send_event(fdmi, FDMISM_EVENT_RPRT_SENT);
}

/**
 * This routine builds Port Attribute Block that used in RPA, RPRT commands.
 */
static          u16
bfa_fcs_port_fdmi_build_portattr_block(struct bfa_fcs_port_fdmi_s *fdmi,
				       u8 *pyld)
{
	struct bfa_fcs_fdmi_port_attr_s fcs_port_attr;
	struct fdmi_port_attr_s *port_attrib = (struct fdmi_port_attr_s *) pyld;
	struct fdmi_attr_s    *attr;
	u8        *curr_ptr;
	u16        len;
	u8         count = 0;

	/*
	 * get port attributes
	 */
	bfa_fcs_fdmi_get_portattr(fdmi, &fcs_port_attr);

	len = sizeof(port_attrib->attr_count);

	/*
	 * fill out the invididual entries
	 */
	curr_ptr = (u8 *) &port_attrib->port_attr;

	/*
	 * FC4 Types
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = bfa_os_htons(FDMI_PORT_ATTRIB_FC4_TYPES);
	attr->len = sizeof(fcs_port_attr.supp_fc4_types);
	memcpy(attr->value, fcs_port_attr.supp_fc4_types, attr->len);
	curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
	len += attr->len;
	++count;
	attr->len =
		bfa_os_htons(attr->len + sizeof(attr->type) +
			     sizeof(attr->len));

	/*
	 * Supported Speed
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = bfa_os_htons(FDMI_PORT_ATTRIB_SUPP_SPEED);
	attr->len = sizeof(fcs_port_attr.supp_speed);
	memcpy(attr->value, &fcs_port_attr.supp_speed, attr->len);
	curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
	len += attr->len;
	++count;
	attr->len =
		bfa_os_htons(attr->len + sizeof(attr->type) +
			     sizeof(attr->len));

	/*
	 * current Port Speed
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = bfa_os_htons(FDMI_PORT_ATTRIB_PORT_SPEED);
	attr->len = sizeof(fcs_port_attr.curr_speed);
	memcpy(attr->value, &fcs_port_attr.curr_speed, attr->len);
	curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
	len += attr->len;
	++count;
	attr->len =
		bfa_os_htons(attr->len + sizeof(attr->type) +
			     sizeof(attr->len));

	/*
	 * max frame size
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = bfa_os_htons(FDMI_PORT_ATTRIB_FRAME_SIZE);
	attr->len = sizeof(fcs_port_attr.max_frm_size);
	memcpy(attr->value, &fcs_port_attr.max_frm_size, attr->len);
	curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
	len += attr->len;
	++count;
	attr->len =
		bfa_os_htons(attr->len + sizeof(attr->type) +
			     sizeof(attr->len));

	/*
	 * OS Device Name
	 */
	if (fcs_port_attr.os_device_name[0] != '\0') {
		attr = (struct fdmi_attr_s *) curr_ptr;
		attr->type = bfa_os_htons(FDMI_PORT_ATTRIB_DEV_NAME);
		attr->len = (u16) strlen(fcs_port_attr.os_device_name);
		memcpy(attr->value, fcs_port_attr.os_device_name, attr->len);
		/* variable fields need to be 4 byte aligned */
		attr->len = fc_roundup(attr->len, sizeof(u32));
		curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
		len += attr->len;
		++count;
		attr->len =
			bfa_os_htons(attr->len + sizeof(attr->type) +
				     sizeof(attr->len));

	}
	/*
	 * Host Name
	 */
	if (fcs_port_attr.host_name[0] != '\0') {
		attr = (struct fdmi_attr_s *) curr_ptr;
		attr->type = bfa_os_htons(FDMI_PORT_ATTRIB_HOST_NAME);
		attr->len = (u16) strlen(fcs_port_attr.host_name);
		memcpy(attr->value, fcs_port_attr.host_name, attr->len);
		/* variable fields need to be 4 byte aligned */
		attr->len = fc_roundup(attr->len, sizeof(u32));
		curr_ptr += sizeof(attr->type) + sizeof(attr->len) + attr->len;
		len += attr->len;
		++count;
		attr->len =
			bfa_os_htons(attr->len + sizeof(attr->type) +
				     sizeof(attr->len));

	}

	/*
	 * Update size of payload
	 */
	port_attrib->attr_count = bfa_os_htonl(count);
	len += ((sizeof(attr->type) + sizeof(attr->len)) * count);
	return len;
}

static          u16
bfa_fcs_port_fdmi_build_rprt_pyld(struct bfa_fcs_port_fdmi_s *fdmi,
				  u8 *pyld)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;
	struct fdmi_rprt_s    *rprt = (struct fdmi_rprt_s *) pyld;
	u16        len;

	rprt->hba_id = bfa_fcs_port_get_pwwn(bfa_fcs_get_base_port(port->fcs));
	rprt->port_name = bfa_fcs_port_get_pwwn(port);

	len = bfa_fcs_port_fdmi_build_portattr_block(fdmi,
			(u8 *) &rprt->port_attr_blk);

	len += sizeof(rprt->hba_id) + sizeof(rprt->port_name);

	return len;
}

static void
bfa_fcs_port_fdmi_rprt_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
				void *cbarg, bfa_status_t req_status,
				u32 rsp_len, u32 resid_len,
				struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_port_fdmi_s *fdmi = (struct bfa_fcs_port_fdmi_s *)cbarg;
	struct bfa_fcs_port_s *port = fdmi->ms->port;
	struct ct_hdr_s       *cthdr = NULL;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		bfa_sm_send_event(fdmi, FDMISM_EVENT_RSP_ERROR);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = bfa_os_ntohs(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		bfa_sm_send_event(fdmi, FDMISM_EVENT_RSP_OK);
		return;
	}

	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(fdmi, FDMISM_EVENT_RSP_ERROR);
}

/**
*   RPA : Register Port Attributes.
 */
static void
bfa_fcs_port_fdmi_send_rpa(void *fdmi_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_port_fdmi_s *fdmi = fdmi_cbarg;
	struct bfa_fcs_port_s *port = fdmi->ms->port;
	struct fchs_s          fchs;
	u16        len, attr_len;
	struct bfa_fcxp_s *fcxp;
	u8        *pyld;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced : bfa_fcs_fcxp_alloc(port->fcs);
	if (!fcxp) {
		bfa_fcxp_alloc_wait(port->fcs->bfa, &fdmi->fcxp_wqe,
				    bfa_fcs_port_fdmi_send_rpa, fdmi);
		return;
	}
	fdmi->fcxp = fcxp;

	pyld = bfa_fcxp_get_reqbuf(fcxp);
	bfa_os_memset(pyld, 0, FC_MAX_PDUSZ);

	len = fc_fdmi_reqhdr_build(&fchs, pyld, bfa_fcs_port_get_fcid(port),
				   FDMI_RPA);

	attr_len = bfa_fcs_port_fdmi_build_rpa_pyld(fdmi,
			(u8 *) ((struct ct_hdr_s *) pyld + 1));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len + attr_len, &fchs,
		      bfa_fcs_port_fdmi_rpa_response, (void *)fdmi,
		      FC_MAX_PDUSZ, FC_RA_TOV);

	bfa_sm_send_event(fdmi, FDMISM_EVENT_RPA_SENT);
}

static          u16
bfa_fcs_port_fdmi_build_rpa_pyld(struct bfa_fcs_port_fdmi_s *fdmi,
				 u8 *pyld)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;
	struct fdmi_rpa_s     *rpa = (struct fdmi_rpa_s *) pyld;
	u16        len;

	rpa->port_name = bfa_fcs_port_get_pwwn(port);

	len = bfa_fcs_port_fdmi_build_portattr_block(fdmi,
			(u8 *) &rpa->port_attr_blk);

	len += sizeof(rpa->port_name);

	return len;
}

static void
bfa_fcs_port_fdmi_rpa_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
			       void *cbarg, bfa_status_t req_status,
			       u32 rsp_len, u32 resid_len,
			       struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_port_fdmi_s *fdmi = (struct bfa_fcs_port_fdmi_s *)cbarg;
	struct bfa_fcs_port_s *port = fdmi->ms->port;
	struct ct_hdr_s       *cthdr = NULL;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		bfa_sm_send_event(fdmi, FDMISM_EVENT_RSP_ERROR);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = bfa_os_ntohs(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		bfa_sm_send_event(fdmi, FDMISM_EVENT_RSP_OK);
		return;
	}

	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(fdmi, FDMISM_EVENT_RSP_ERROR);
}

static void
bfa_fcs_port_fdmi_timeout(void *arg)
{
	struct bfa_fcs_port_fdmi_s *fdmi = (struct bfa_fcs_port_fdmi_s *)arg;

	bfa_sm_send_event(fdmi, FDMISM_EVENT_TIMEOUT);
}

static void
bfa_fcs_fdmi_get_hbaattr(struct bfa_fcs_port_fdmi_s *fdmi,
			 struct bfa_fcs_fdmi_hba_attr_s *hba_attr)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;
	struct bfa_fcs_driver_info_s *driver_info = &port->fcs->driver_info;

	bfa_os_memset(hba_attr, 0, sizeof(struct bfa_fcs_fdmi_hba_attr_s));

	bfa_ioc_get_adapter_manufacturer(&port->fcs->bfa->ioc,
		hba_attr->manufacturer);
	bfa_ioc_get_adapter_serial_num(&port->fcs->bfa->ioc,
						hba_attr->serial_num);
	bfa_ioc_get_adapter_model(&port->fcs->bfa->ioc, hba_attr->model);
	bfa_ioc_get_adapter_model(&port->fcs->bfa->ioc, hba_attr->model_desc);
	bfa_ioc_get_pci_chip_rev(&port->fcs->bfa->ioc, hba_attr->hw_version);
	bfa_ioc_get_adapter_optrom_ver(&port->fcs->bfa->ioc,
		hba_attr->option_rom_ver);
	bfa_ioc_get_adapter_fw_ver(&port->fcs->bfa->ioc, hba_attr->fw_version);

	strncpy(hba_attr->driver_version, (char *)driver_info->version,
		sizeof(hba_attr->driver_version));

	strncpy(hba_attr->os_name, driver_info->host_os_name,
		sizeof(hba_attr->os_name));

	/*
	 * If there is a patch level, append it to the os name along with a
	 * separator
	 */
	if (driver_info->host_os_patch[0] != '\0') {
		strncat(hba_attr->os_name, BFA_FCS_PORT_SYMBNAME_SEPARATOR,
			sizeof(BFA_FCS_PORT_SYMBNAME_SEPARATOR));
		strncat(hba_attr->os_name, driver_info->host_os_patch,
			sizeof(driver_info->host_os_patch));
	}

	hba_attr->max_ct_pyld = bfa_os_htonl(FC_MAX_PDUSZ);

}

static void
bfa_fcs_fdmi_get_portattr(struct bfa_fcs_port_fdmi_s *fdmi,
			  struct bfa_fcs_fdmi_port_attr_s *port_attr)
{
	struct bfa_fcs_port_s *port = fdmi->ms->port;
	struct bfa_fcs_driver_info_s *driver_info = &port->fcs->driver_info;
	struct bfa_pport_attr_s pport_attr;

	bfa_os_memset(port_attr, 0, sizeof(struct bfa_fcs_fdmi_port_attr_s));

	/*
	 * get pport attributes from hal
	 */
	bfa_fcport_get_attr(port->fcs->bfa, &pport_attr);

	/*
	 * get FC4 type Bitmask
	 */
	fc_get_fc4type_bitmask(FC_TYPE_FCP, port_attr->supp_fc4_types);

	/*
	 * Supported Speeds
	 */
	port_attr->supp_speed = bfa_os_htonl(BFA_FCS_FDMI_SUPORTED_SPEEDS);

	/*
	 * Current Speed
	 */
	port_attr->curr_speed = bfa_os_htonl(pport_attr.speed);

	/*
	 * Max PDU Size.
	 */
	port_attr->max_frm_size = bfa_os_htonl(FC_MAX_PDUSZ);

	/*
	 * OS device Name
	 */
	strncpy(port_attr->os_device_name, (char *)driver_info->os_device_name,
		sizeof(port_attr->os_device_name));

	/*
	 * Host name
	 */
	strncpy(port_attr->host_name, (char *)driver_info->host_machine_name,
		sizeof(port_attr->host_name));

}


void
bfa_fcs_port_fdmi_init(struct bfa_fcs_port_ms_s *ms)
{
	struct bfa_fcs_port_fdmi_s *fdmi = &ms->fdmi;

	fdmi->ms = ms;
	if (ms->port->fcs->fdmi_enabled)
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_offline);
	else
		bfa_sm_set_state(fdmi, bfa_fcs_port_fdmi_sm_disabled);
}

void
bfa_fcs_port_fdmi_offline(struct bfa_fcs_port_ms_s *ms)
{
	struct bfa_fcs_port_fdmi_s *fdmi = &ms->fdmi;

	fdmi->ms = ms;
	bfa_sm_send_event(fdmi, FDMISM_EVENT_PORT_OFFLINE);
}

void
bfa_fcs_port_fdmi_online(struct bfa_fcs_port_ms_s *ms)
{
	struct bfa_fcs_port_fdmi_s *fdmi = &ms->fdmi;

	fdmi->ms = ms;
	bfa_sm_send_event(fdmi, FDMISM_EVENT_PORT_ONLINE);
}
