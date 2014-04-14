/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
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

#include "bfad_drv.h"
#include "bfad_im.h"
#include "bfa_fcs.h"
#include "bfa_fcbuild.h"
#include "bfa_fc.h"

BFA_TRC_FILE(FCS, PORT);

/*
 * ALPA to LIXA bitmap mapping
 *
 * ALPA 0x00 (Word 0, Bit 30) is invalid for N_Ports. Also Word 0 Bit 31
 * is for L_bit (login required) and is filled as ALPA 0x00 here.
 */
static const u8 loop_alpa_map[] = {
	0x00, 0x00, 0x01, 0x02, 0x04, 0x08, 0x0F, 0x10, /* Word 0 Bits 31..24 */
	0x17, 0x18, 0x1B, 0x1D, 0x1E, 0x1F, 0x23, 0x25, /* Word 0 Bits 23..16 */
	0x26, 0x27, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, /* Word 0 Bits 15..08 */
	0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x39, 0x3A, /* Word 0 Bits 07..00 */

	0x3C, 0x43, 0x45, 0x46, 0x47, 0x49, 0x4A, 0x4B, /* Word 1 Bits 31..24 */
	0x4C, 0x4D, 0x4E, 0x51, 0x52, 0x53, 0x54, 0x55, /* Word 1 Bits 23..16 */
	0x56, 0x59, 0x5A, 0x5C, 0x63, 0x65, 0x66, 0x67, /* Word 1 Bits 15..08 */
	0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x71, 0x72, /* Word 1 Bits 07..00 */

	0x73, 0x74, 0x75, 0x76, 0x79, 0x7A, 0x7C, 0x80, /* Word 2 Bits 31..24 */
	0x81, 0x82, 0x84, 0x88, 0x8F, 0x90, 0x97, 0x98, /* Word 2 Bits 23..16 */
	0x9B, 0x9D, 0x9E, 0x9F, 0xA3, 0xA5, 0xA6, 0xA7, /* Word 2 Bits 15..08 */
	0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xB1, 0xB2, /* Word 2 Bits 07..00 */

	0xB3, 0xB4, 0xB5, 0xB6, 0xB9, 0xBA, 0xBC, 0xC3, /* Word 3 Bits 31..24 */
	0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, /* Word 3 Bits 23..16 */
	0xCE, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD9, /* Word 3 Bits 15..08 */
	0xDA, 0xDC, 0xE0, 0xE1, 0xE2, 0xE4, 0xE8, 0xEF, /* Word 3 Bits 07..00 */
};

static void     bfa_fcs_lport_send_ls_rjt(struct bfa_fcs_lport_s *port,
					 struct fchs_s *rx_fchs, u8 reason_code,
					 u8 reason_code_expl);
static void     bfa_fcs_lport_plogi(struct bfa_fcs_lport_s *port,
			struct fchs_s *rx_fchs, struct fc_logi_s *plogi);
static void     bfa_fcs_lport_online_actions(struct bfa_fcs_lport_s *port);
static void     bfa_fcs_lport_offline_actions(struct bfa_fcs_lport_s *port);
static void     bfa_fcs_lport_unknown_init(struct bfa_fcs_lport_s *port);
static void     bfa_fcs_lport_unknown_online(struct bfa_fcs_lport_s *port);
static void     bfa_fcs_lport_unknown_offline(struct bfa_fcs_lport_s *port);
static void     bfa_fcs_lport_deleted(struct bfa_fcs_lport_s *port);
static void     bfa_fcs_lport_echo(struct bfa_fcs_lport_s *port,
			struct fchs_s *rx_fchs,
			struct fc_echo_s *echo, u16 len);
static void     bfa_fcs_lport_rnid(struct bfa_fcs_lport_s *port,
			struct fchs_s *rx_fchs,
			struct fc_rnid_cmd_s *rnid, u16 len);
static void     bfa_fs_port_get_gen_topo_data(struct bfa_fcs_lport_s *port,
			struct fc_rnid_general_topology_data_s *gen_topo_data);

static void	bfa_fcs_lport_fab_init(struct bfa_fcs_lport_s *port);
static void	bfa_fcs_lport_fab_online(struct bfa_fcs_lport_s *port);
static void	bfa_fcs_lport_fab_offline(struct bfa_fcs_lport_s *port);

static void	bfa_fcs_lport_n2n_init(struct bfa_fcs_lport_s *port);
static void	bfa_fcs_lport_n2n_online(struct bfa_fcs_lport_s *port);
static void	bfa_fcs_lport_n2n_offline(struct bfa_fcs_lport_s *port);

static void	bfa_fcs_lport_loop_init(struct bfa_fcs_lport_s *port);
static void	bfa_fcs_lport_loop_online(struct bfa_fcs_lport_s *port);
static void	bfa_fcs_lport_loop_offline(struct bfa_fcs_lport_s *port);

static struct {
	void		(*init) (struct bfa_fcs_lport_s *port);
	void		(*online) (struct bfa_fcs_lport_s *port);
	void		(*offline) (struct bfa_fcs_lport_s *port);
} __port_action[] = {
	{
	bfa_fcs_lport_unknown_init, bfa_fcs_lport_unknown_online,
			bfa_fcs_lport_unknown_offline}, {
	bfa_fcs_lport_fab_init, bfa_fcs_lport_fab_online,
			bfa_fcs_lport_fab_offline}, {
	bfa_fcs_lport_n2n_init, bfa_fcs_lport_n2n_online,
			bfa_fcs_lport_n2n_offline}, {
	bfa_fcs_lport_loop_init, bfa_fcs_lport_loop_online,
			bfa_fcs_lport_loop_offline},
	};

/*
 *  fcs_port_sm FCS logical port state machine
 */

enum bfa_fcs_lport_event {
	BFA_FCS_PORT_SM_CREATE = 1,
	BFA_FCS_PORT_SM_ONLINE = 2,
	BFA_FCS_PORT_SM_OFFLINE = 3,
	BFA_FCS_PORT_SM_DELETE = 4,
	BFA_FCS_PORT_SM_DELRPORT = 5,
	BFA_FCS_PORT_SM_STOP = 6,
};

static void     bfa_fcs_lport_sm_uninit(struct bfa_fcs_lport_s *port,
					enum bfa_fcs_lport_event event);
static void     bfa_fcs_lport_sm_init(struct bfa_fcs_lport_s *port,
					enum bfa_fcs_lport_event event);
static void     bfa_fcs_lport_sm_online(struct bfa_fcs_lport_s *port,
					enum bfa_fcs_lport_event event);
static void     bfa_fcs_lport_sm_offline(struct bfa_fcs_lport_s *port,
					enum bfa_fcs_lport_event event);
static void     bfa_fcs_lport_sm_deleting(struct bfa_fcs_lport_s *port,
					enum bfa_fcs_lport_event event);
static void	bfa_fcs_lport_sm_stopping(struct bfa_fcs_lport_s *port,
					enum bfa_fcs_lport_event event);

static void
bfa_fcs_lport_sm_uninit(
	struct bfa_fcs_lport_s *port,
	enum bfa_fcs_lport_event event)
{
	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case BFA_FCS_PORT_SM_CREATE:
		bfa_sm_set_state(port, bfa_fcs_lport_sm_init);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_lport_sm_init(struct bfa_fcs_lport_s *port,
			enum bfa_fcs_lport_event event)
{
	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case BFA_FCS_PORT_SM_ONLINE:
		bfa_sm_set_state(port, bfa_fcs_lport_sm_online);
		bfa_fcs_lport_online_actions(port);
		break;

	case BFA_FCS_PORT_SM_DELETE:
		bfa_sm_set_state(port, bfa_fcs_lport_sm_uninit);
		bfa_fcs_lport_deleted(port);
		break;

	case BFA_FCS_PORT_SM_STOP:
		/* If vport - send completion call back */
		if (port->vport)
			bfa_fcs_vport_stop_comp(port->vport);
		else
			bfa_wc_down(&(port->fabric->stop_wc));
		break;

	case BFA_FCS_PORT_SM_OFFLINE:
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_lport_sm_online(
	struct bfa_fcs_lport_s *port,
	enum bfa_fcs_lport_event event)
{
	struct bfa_fcs_rport_s *rport;
	struct list_head		*qe, *qen;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case BFA_FCS_PORT_SM_OFFLINE:
		bfa_sm_set_state(port, bfa_fcs_lport_sm_offline);
		bfa_fcs_lport_offline_actions(port);
		break;

	case BFA_FCS_PORT_SM_STOP:
		__port_action[port->fabric->fab_type].offline(port);

		if (port->num_rports == 0) {
			bfa_sm_set_state(port, bfa_fcs_lport_sm_init);
			/* If vport - send completion call back */
			if (port->vport)
				bfa_fcs_vport_stop_comp(port->vport);
			else
				bfa_wc_down(&(port->fabric->stop_wc));
		} else {
			bfa_sm_set_state(port, bfa_fcs_lport_sm_stopping);
			list_for_each_safe(qe, qen, &port->rport_q) {
				rport = (struct bfa_fcs_rport_s *) qe;
				bfa_sm_send_event(rport, RPSM_EVENT_DELETE);
			}
		}
		break;

	case BFA_FCS_PORT_SM_DELETE:

		__port_action[port->fabric->fab_type].offline(port);

		if (port->num_rports == 0) {
			bfa_sm_set_state(port, bfa_fcs_lport_sm_uninit);
			bfa_fcs_lport_deleted(port);
		} else {
			bfa_sm_set_state(port, bfa_fcs_lport_sm_deleting);
			list_for_each_safe(qe, qen, &port->rport_q) {
				rport = (struct bfa_fcs_rport_s *) qe;
				bfa_sm_send_event(rport, RPSM_EVENT_DELETE);
			}
		}
		break;

	case BFA_FCS_PORT_SM_DELRPORT:
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_lport_sm_offline(
	struct bfa_fcs_lport_s *port,
	enum bfa_fcs_lport_event event)
{
	struct bfa_fcs_rport_s *rport;
	struct list_head		*qe, *qen;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case BFA_FCS_PORT_SM_ONLINE:
		bfa_sm_set_state(port, bfa_fcs_lport_sm_online);
		bfa_fcs_lport_online_actions(port);
		break;

	case BFA_FCS_PORT_SM_STOP:
		if (port->num_rports == 0) {
			bfa_sm_set_state(port, bfa_fcs_lport_sm_init);
			/* If vport - send completion call back */
			if (port->vport)
				bfa_fcs_vport_stop_comp(port->vport);
			else
				bfa_wc_down(&(port->fabric->stop_wc));
		} else {
			bfa_sm_set_state(port, bfa_fcs_lport_sm_stopping);
			list_for_each_safe(qe, qen, &port->rport_q) {
				rport = (struct bfa_fcs_rport_s *) qe;
				bfa_sm_send_event(rport, RPSM_EVENT_DELETE);
			}
		}
		break;

	case BFA_FCS_PORT_SM_DELETE:
		if (port->num_rports == 0) {
			bfa_sm_set_state(port, bfa_fcs_lport_sm_uninit);
			bfa_fcs_lport_deleted(port);
		} else {
			bfa_sm_set_state(port, bfa_fcs_lport_sm_deleting);
			list_for_each_safe(qe, qen, &port->rport_q) {
				rport = (struct bfa_fcs_rport_s *) qe;
				bfa_sm_send_event(rport, RPSM_EVENT_DELETE);
			}
		}
		break;

	case BFA_FCS_PORT_SM_DELRPORT:
	case BFA_FCS_PORT_SM_OFFLINE:
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_lport_sm_stopping(struct bfa_fcs_lport_s *port,
			  enum bfa_fcs_lport_event event)
{
	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case BFA_FCS_PORT_SM_DELRPORT:
		if (port->num_rports == 0) {
			bfa_sm_set_state(port, bfa_fcs_lport_sm_init);
			/* If vport - send completion call back */
			if (port->vport)
				bfa_fcs_vport_stop_comp(port->vport);
			else
				bfa_wc_down(&(port->fabric->stop_wc));
		}
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_lport_sm_deleting(
	struct bfa_fcs_lport_s *port,
	enum bfa_fcs_lport_event event)
{
	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case BFA_FCS_PORT_SM_DELRPORT:
		if (port->num_rports == 0) {
			bfa_sm_set_state(port, bfa_fcs_lport_sm_uninit);
			bfa_fcs_lport_deleted(port);
		}
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

/*
 *  fcs_port_pvt
 */

/*
 * Send AEN notification
 */
static void
bfa_fcs_lport_aen_post(struct bfa_fcs_lport_s *port,
			enum bfa_lport_aen_event event)
{
	struct bfad_s *bfad = (struct bfad_s *)port->fabric->fcs->bfad;
	struct bfa_aen_entry_s  *aen_entry;

	bfad_get_aen_entry(bfad, aen_entry);
	if (!aen_entry)
		return;

	aen_entry->aen_data.lport.vf_id = port->fabric->vf_id;
	aen_entry->aen_data.lport.roles = port->port_cfg.roles;
	aen_entry->aen_data.lport.ppwwn = bfa_fcs_lport_get_pwwn(
					bfa_fcs_get_base_port(port->fcs));
	aen_entry->aen_data.lport.lpwwn = bfa_fcs_lport_get_pwwn(port);

	/* Send the AEN notification */
	bfad_im_post_vendor_event(aen_entry, bfad, ++port->fcs->fcs_aen_seq,
				  BFA_AEN_CAT_LPORT, event);
}

/*
 * Send a LS reject
 */
static void
bfa_fcs_lport_send_ls_rjt(struct bfa_fcs_lport_s *port, struct fchs_s *rx_fchs,
			 u8 reason_code, u8 reason_code_expl)
{
	struct fchs_s	fchs;
	struct bfa_fcxp_s *fcxp;
	struct bfa_rport_s *bfa_rport = NULL;
	int		len;

	bfa_trc(port->fcs, rx_fchs->d_id);
	bfa_trc(port->fcs, rx_fchs->s_id);

	fcxp = bfa_fcs_fcxp_alloc(port->fcs, BFA_FALSE);
	if (!fcxp)
		return;

	len = fc_ls_rjt_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			      rx_fchs->s_id, bfa_fcs_lport_get_fcid(port),
			      rx_fchs->ox_id, reason_code, reason_code_expl);

	bfa_fcxp_send(fcxp, bfa_rport, port->fabric->vf_id, port->lp_tag,
			  BFA_FALSE, FC_CLASS_3, len, &fchs, NULL, NULL,
			  FC_MAX_PDUSZ, 0);
}

/*
 * Send a FCCT Reject
 */
static void
bfa_fcs_lport_send_fcgs_rjt(struct bfa_fcs_lport_s *port,
	struct fchs_s *rx_fchs, u8 reason_code, u8 reason_code_expl)
{
	struct fchs_s   fchs;
	struct bfa_fcxp_s *fcxp;
	struct bfa_rport_s *bfa_rport = NULL;
	int             len;
	struct ct_hdr_s *rx_cthdr = (struct ct_hdr_s *)(rx_fchs + 1);
	struct ct_hdr_s *ct_hdr;

	bfa_trc(port->fcs, rx_fchs->d_id);
	bfa_trc(port->fcs, rx_fchs->s_id);

	fcxp = bfa_fcs_fcxp_alloc(port->fcs, BFA_FALSE);
	if (!fcxp)
		return;

	ct_hdr = bfa_fcxp_get_reqbuf(fcxp);
	ct_hdr->gs_type = rx_cthdr->gs_type;
	ct_hdr->gs_sub_type = rx_cthdr->gs_sub_type;

	len = fc_gs_rjt_build(&fchs, ct_hdr, rx_fchs->s_id,
			bfa_fcs_lport_get_fcid(port),
			rx_fchs->ox_id, reason_code, reason_code_expl);

	bfa_fcxp_send(fcxp, bfa_rport, port->fabric->vf_id, port->lp_tag,
			BFA_FALSE, FC_CLASS_3, len, &fchs, NULL, NULL,
			FC_MAX_PDUSZ, 0);
}

/*
 * Process incoming plogi from a remote port.
 */
static void
bfa_fcs_lport_plogi(struct bfa_fcs_lport_s *port,
		struct fchs_s *rx_fchs, struct fc_logi_s *plogi)
{
	struct bfa_fcs_rport_s *rport;

	bfa_trc(port->fcs, rx_fchs->d_id);
	bfa_trc(port->fcs, rx_fchs->s_id);

	/*
	 * If min cfg mode is enabled, drop any incoming PLOGIs
	 */
	if (__fcs_min_cfg(port->fcs)) {
		bfa_trc(port->fcs, rx_fchs->s_id);
		return;
	}

	if (fc_plogi_parse(rx_fchs) != FC_PARSE_OK) {
		bfa_trc(port->fcs, rx_fchs->s_id);
		/*
		 * send a LS reject
		 */
		bfa_fcs_lport_send_ls_rjt(port, rx_fchs,
					FC_LS_RJT_RSN_PROTOCOL_ERROR,
					FC_LS_RJT_EXP_SPARMS_ERR_OPTIONS);
		return;
	}

	/*
	 * Direct Attach P2P mode : verify address assigned by the r-port.
	 */
	if ((!bfa_fcs_fabric_is_switched(port->fabric)) &&
		(memcmp((void *)&bfa_fcs_lport_get_pwwn(port),
			   (void *)&plogi->port_name, sizeof(wwn_t)) < 0)) {
		if (BFA_FCS_PID_IS_WKA(rx_fchs->d_id)) {
			/* Address assigned to us cannot be a WKA */
			bfa_fcs_lport_send_ls_rjt(port, rx_fchs,
					FC_LS_RJT_RSN_PROTOCOL_ERROR,
					FC_LS_RJT_EXP_INVALID_NPORT_ID);
			return;
		}
		port->pid  = rx_fchs->d_id;
		bfa_lps_set_n2n_pid(port->fabric->lps, rx_fchs->d_id);
	}

	/*
	 * First, check if we know the device by pwwn.
	 */
	rport = bfa_fcs_lport_get_rport_by_pwwn(port, plogi->port_name);
	if (rport) {
		/*
		 * Direct Attach P2P mode : handle address assigned by r-port.
		 */
		if ((!bfa_fcs_fabric_is_switched(port->fabric)) &&
			(memcmp((void *)&bfa_fcs_lport_get_pwwn(port),
			(void *)&plogi->port_name, sizeof(wwn_t)) < 0)) {
			port->pid  = rx_fchs->d_id;
			bfa_lps_set_n2n_pid(port->fabric->lps, rx_fchs->d_id);
			rport->pid = rx_fchs->s_id;
		}
		bfa_fcs_rport_plogi(rport, rx_fchs, plogi);
		return;
	}

	/*
	 * Next, lookup rport by PID.
	 */
	rport = bfa_fcs_lport_get_rport_by_pid(port, rx_fchs->s_id);
	if (!rport) {
		/*
		 * Inbound PLOGI from a new device.
		 */
		bfa_fcs_rport_plogi_create(port, rx_fchs, plogi);
		return;
	}

	/*
	 * Rport is known only by PID.
	 */
	if (rport->pwwn) {
		/*
		 * This is a different device with the same pid. Old device
		 * disappeared. Send implicit LOGO to old device.
		 */
		WARN_ON(rport->pwwn == plogi->port_name);
		bfa_sm_send_event(rport, RPSM_EVENT_LOGO_IMP);

		/*
		 * Inbound PLOGI from a new device (with old PID).
		 */
		bfa_fcs_rport_plogi_create(port, rx_fchs, plogi);
		return;
	}

	/*
	 * PLOGI crossing each other.
	 */
	WARN_ON(rport->pwwn != WWN_NULL);
	bfa_fcs_rport_plogi(rport, rx_fchs, plogi);
}

/*
 * Process incoming ECHO.
 * Since it does not require a login, it is processed here.
 */
static void
bfa_fcs_lport_echo(struct bfa_fcs_lport_s *port, struct fchs_s *rx_fchs,
		struct fc_echo_s *echo, u16 rx_len)
{
	struct fchs_s		fchs;
	struct bfa_fcxp_s	*fcxp;
	struct bfa_rport_s	*bfa_rport = NULL;
	int			len, pyld_len;

	bfa_trc(port->fcs, rx_fchs->s_id);
	bfa_trc(port->fcs, rx_fchs->d_id);

	fcxp = bfa_fcs_fcxp_alloc(port->fcs, BFA_FALSE);
	if (!fcxp)
		return;

	len = fc_ls_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				rx_fchs->s_id, bfa_fcs_lport_get_fcid(port),
				rx_fchs->ox_id);

	/*
	 * Copy the payload (if any) from the echo frame
	 */
	pyld_len = rx_len - sizeof(struct fchs_s);
	bfa_trc(port->fcs, rx_len);
	bfa_trc(port->fcs, pyld_len);

	if (pyld_len > len)
		memcpy(((u8 *) bfa_fcxp_get_reqbuf(fcxp)) +
			sizeof(struct fc_echo_s), (echo + 1),
			(pyld_len - sizeof(struct fc_echo_s)));

	bfa_fcxp_send(fcxp, bfa_rport, port->fabric->vf_id, port->lp_tag,
			BFA_FALSE, FC_CLASS_3, pyld_len, &fchs, NULL, NULL,
			FC_MAX_PDUSZ, 0);
}

/*
 * Process incoming RNID.
 * Since it does not require a login, it is processed here.
 */
static void
bfa_fcs_lport_rnid(struct bfa_fcs_lport_s *port, struct fchs_s *rx_fchs,
		struct fc_rnid_cmd_s *rnid, u16 rx_len)
{
	struct fc_rnid_common_id_data_s common_id_data;
	struct fc_rnid_general_topology_data_s gen_topo_data;
	struct fchs_s	fchs;
	struct bfa_fcxp_s *fcxp;
	struct bfa_rport_s *bfa_rport = NULL;
	u16	len;
	u32	data_format;

	bfa_trc(port->fcs, rx_fchs->s_id);
	bfa_trc(port->fcs, rx_fchs->d_id);
	bfa_trc(port->fcs, rx_len);

	fcxp = bfa_fcs_fcxp_alloc(port->fcs, BFA_FALSE);
	if (!fcxp)
		return;

	/*
	 * Check Node Indentification Data Format
	 * We only support General Topology Discovery Format.
	 * For any other requested Data Formats, we return Common Node Id Data
	 * only, as per FC-LS.
	 */
	bfa_trc(port->fcs, rnid->node_id_data_format);
	if (rnid->node_id_data_format == RNID_NODEID_DATA_FORMAT_DISCOVERY) {
		data_format = RNID_NODEID_DATA_FORMAT_DISCOVERY;
		/*
		 * Get General topology data for this port
		 */
		bfa_fs_port_get_gen_topo_data(port, &gen_topo_data);
	} else {
		data_format = RNID_NODEID_DATA_FORMAT_COMMON;
	}

	/*
	 * Copy the Node Id Info
	 */
	common_id_data.port_name = bfa_fcs_lport_get_pwwn(port);
	common_id_data.node_name = bfa_fcs_lport_get_nwwn(port);

	len = fc_rnid_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				rx_fchs->s_id, bfa_fcs_lport_get_fcid(port),
				rx_fchs->ox_id, data_format, &common_id_data,
				&gen_topo_data);

	bfa_fcxp_send(fcxp, bfa_rport, port->fabric->vf_id, port->lp_tag,
			BFA_FALSE, FC_CLASS_3, len, &fchs, NULL, NULL,
			FC_MAX_PDUSZ, 0);
}

/*
 *  Fill out General Topolpgy Discovery Data for RNID ELS.
 */
static void
bfa_fs_port_get_gen_topo_data(struct bfa_fcs_lport_s *port,
			struct fc_rnid_general_topology_data_s *gen_topo_data)
{
	memset(gen_topo_data, 0,
		      sizeof(struct fc_rnid_general_topology_data_s));

	gen_topo_data->asso_type = cpu_to_be32(RNID_ASSOCIATED_TYPE_HOST);
	gen_topo_data->phy_port_num = 0;	/* @todo */
	gen_topo_data->num_attached_nodes = cpu_to_be32(1);
}

static void
bfa_fcs_lport_online_actions(struct bfa_fcs_lport_s *port)
{
	struct bfad_s *bfad = (struct bfad_s *)port->fcs->bfad;
	char	lpwwn_buf[BFA_STRING_32];

	bfa_trc(port->fcs, port->fabric->oper_type);

	__port_action[port->fabric->fab_type].init(port);
	__port_action[port->fabric->fab_type].online(port);

	wwn2str(lpwwn_buf, bfa_fcs_lport_get_pwwn(port));
	BFA_LOG(KERN_WARNING, bfad, bfa_log_level,
		"Logical port online: WWN = %s Role = %s\n",
		lpwwn_buf, "Initiator");
	bfa_fcs_lport_aen_post(port, BFA_LPORT_AEN_ONLINE);

	bfad->bfad_flags |= BFAD_PORT_ONLINE;
}

static void
bfa_fcs_lport_offline_actions(struct bfa_fcs_lport_s *port)
{
	struct list_head	*qe, *qen;
	struct bfa_fcs_rport_s *rport;
	struct bfad_s *bfad = (struct bfad_s *)port->fcs->bfad;
	char    lpwwn_buf[BFA_STRING_32];

	bfa_trc(port->fcs, port->fabric->oper_type);

	__port_action[port->fabric->fab_type].offline(port);

	wwn2str(lpwwn_buf, bfa_fcs_lport_get_pwwn(port));
	if (bfa_sm_cmp_state(port->fabric,
			bfa_fcs_fabric_sm_online) == BFA_TRUE) {
		BFA_LOG(KERN_WARNING, bfad, bfa_log_level,
		"Logical port lost fabric connectivity: WWN = %s Role = %s\n",
		lpwwn_buf, "Initiator");
		bfa_fcs_lport_aen_post(port, BFA_LPORT_AEN_DISCONNECT);
	} else {
		BFA_LOG(KERN_WARNING, bfad, bfa_log_level,
		"Logical port taken offline: WWN = %s Role = %s\n",
		lpwwn_buf, "Initiator");
		bfa_fcs_lport_aen_post(port, BFA_LPORT_AEN_OFFLINE);
	}

	list_for_each_safe(qe, qen, &port->rport_q) {
		rport = (struct bfa_fcs_rport_s *) qe;
		bfa_sm_send_event(rport, RPSM_EVENT_LOGO_IMP);
	}
}

static void
bfa_fcs_lport_unknown_init(struct bfa_fcs_lport_s *port)
{
	WARN_ON(1);
}

static void
bfa_fcs_lport_unknown_online(struct bfa_fcs_lport_s *port)
{
	WARN_ON(1);
}

static void
bfa_fcs_lport_unknown_offline(struct bfa_fcs_lport_s *port)
{
	WARN_ON(1);
}

static void
bfa_fcs_lport_abts_acc(struct bfa_fcs_lport_s *port, struct fchs_s *rx_fchs)
{
	struct fchs_s fchs;
	struct bfa_fcxp_s *fcxp;
	int		len;

	bfa_trc(port->fcs, rx_fchs->d_id);
	bfa_trc(port->fcs, rx_fchs->s_id);

	fcxp = bfa_fcs_fcxp_alloc(port->fcs, BFA_FALSE);
	if (!fcxp)
		return;

	len = fc_ba_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			rx_fchs->s_id, bfa_fcs_lport_get_fcid(port),
			rx_fchs->ox_id, 0);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag,
			  BFA_FALSE, FC_CLASS_3, len, &fchs, NULL, NULL,
			  FC_MAX_PDUSZ, 0);
}
static void
bfa_fcs_lport_deleted(struct bfa_fcs_lport_s *port)
{
	struct bfad_s *bfad = (struct bfad_s *)port->fcs->bfad;
	char    lpwwn_buf[BFA_STRING_32];

	wwn2str(lpwwn_buf, bfa_fcs_lport_get_pwwn(port));
	BFA_LOG(KERN_INFO, bfad, bfa_log_level,
		"Logical port deleted: WWN = %s Role = %s\n",
		lpwwn_buf, "Initiator");
	bfa_fcs_lport_aen_post(port, BFA_LPORT_AEN_DELETE);

	/* Base port will be deleted by the OS driver */
	if (port->vport)
		bfa_fcs_vport_delete_comp(port->vport);
	else
		bfa_wc_down(&port->fabric->wc);
}


/*
 * Unsolicited frame receive handling.
 */
void
bfa_fcs_lport_uf_recv(struct bfa_fcs_lport_s *lport,
			struct fchs_s *fchs, u16 len)
{
	u32	pid = fchs->s_id;
	struct bfa_fcs_rport_s *rport = NULL;
	struct fc_els_cmd_s *els_cmd = (struct fc_els_cmd_s *) (fchs + 1);

	bfa_stats(lport, uf_recvs);
	bfa_trc(lport->fcs, fchs->type);

	if (!bfa_fcs_lport_is_online(lport)) {
		bfa_stats(lport, uf_recv_drops);
		return;
	}

	/*
	 * First, handle ELSs that donot require a login.
	 */
	/*
	 * Handle PLOGI first
	 */
	if ((fchs->type == FC_TYPE_ELS) &&
		(els_cmd->els_code == FC_ELS_PLOGI)) {
		bfa_fcs_lport_plogi(lport, fchs, (struct fc_logi_s *) els_cmd);
		return;
	}

	/*
	 * Handle ECHO separately.
	 */
	if ((fchs->type == FC_TYPE_ELS) && (els_cmd->els_code == FC_ELS_ECHO)) {
		bfa_fcs_lport_echo(lport, fchs,
				(struct fc_echo_s *)els_cmd, len);
		return;
	}

	/*
	 * Handle RNID separately.
	 */
	if ((fchs->type == FC_TYPE_ELS) && (els_cmd->els_code == FC_ELS_RNID)) {
		bfa_fcs_lport_rnid(lport, fchs,
			(struct fc_rnid_cmd_s *) els_cmd, len);
		return;
	}

	if (fchs->type == FC_TYPE_BLS) {
		if ((fchs->routing == FC_RTG_BASIC_LINK) &&
				(fchs->cat_info == FC_CAT_ABTS))
			bfa_fcs_lport_abts_acc(lport, fchs);
		return;
	}

	if (fchs->type == FC_TYPE_SERVICES) {
		/*
		 * Unhandled FC-GS frames. Send a FC-CT Reject
		 */
		bfa_fcs_lport_send_fcgs_rjt(lport, fchs, CT_RSN_NOT_SUPP,
				CT_NS_EXP_NOADDITIONAL);
		return;
	}

	/*
	 * look for a matching remote port ID
	 */
	rport = bfa_fcs_lport_get_rport_by_pid(lport, pid);
	if (rport) {
		bfa_trc(rport->fcs, fchs->s_id);
		bfa_trc(rport->fcs, fchs->d_id);
		bfa_trc(rport->fcs, fchs->type);

		bfa_fcs_rport_uf_recv(rport, fchs, len);
		return;
	}

	/*
	 * Only handles ELS frames for now.
	 */
	if (fchs->type != FC_TYPE_ELS) {
		bfa_trc(lport->fcs, fchs->s_id);
		bfa_trc(lport->fcs, fchs->d_id);
		/* ignore type FC_TYPE_FC_FSS */
		if (fchs->type != FC_TYPE_FC_FSS)
			bfa_sm_fault(lport->fcs, fchs->type);
		return;
	}

	bfa_trc(lport->fcs, els_cmd->els_code);
	if (els_cmd->els_code == FC_ELS_RSCN) {
		bfa_fcs_lport_scn_process_rscn(lport, fchs, len);
		return;
	}

	if (els_cmd->els_code == FC_ELS_LOGO) {
		/*
		 * @todo Handle LOGO frames received.
		 */
		return;
	}

	if (els_cmd->els_code == FC_ELS_PRLI) {
		/*
		 * @todo Handle PRLI frames received.
		 */
		return;
	}

	/*
	 * Unhandled ELS frames. Send a LS_RJT.
	 */
	bfa_fcs_lport_send_ls_rjt(lport, fchs, FC_LS_RJT_RSN_CMD_NOT_SUPP,
				 FC_LS_RJT_EXP_NO_ADDL_INFO);

}

/*
 *   PID based Lookup for a R-Port in the Port R-Port Queue
 */
struct bfa_fcs_rport_s *
bfa_fcs_lport_get_rport_by_pid(struct bfa_fcs_lport_s *port, u32 pid)
{
	struct bfa_fcs_rport_s *rport;
	struct list_head	*qe;

	list_for_each(qe, &port->rport_q) {
		rport = (struct bfa_fcs_rport_s *) qe;
		if (rport->pid == pid)
			return rport;
	}

	bfa_trc(port->fcs, pid);
	return NULL;
}

/*
 * OLD_PID based Lookup for a R-Port in the Port R-Port Queue
 */
struct bfa_fcs_rport_s *
bfa_fcs_lport_get_rport_by_old_pid(struct bfa_fcs_lport_s *port, u32 pid)
{
	struct bfa_fcs_rport_s *rport;
	struct list_head	*qe;

	list_for_each(qe, &port->rport_q) {
		rport = (struct bfa_fcs_rport_s *) qe;
		if (rport->old_pid == pid)
			return rport;
	}

	bfa_trc(port->fcs, pid);
	return NULL;
}

/*
 *   PWWN based Lookup for a R-Port in the Port R-Port Queue
 */
struct bfa_fcs_rport_s *
bfa_fcs_lport_get_rport_by_pwwn(struct bfa_fcs_lport_s *port, wwn_t pwwn)
{
	struct bfa_fcs_rport_s *rport;
	struct list_head	*qe;

	list_for_each(qe, &port->rport_q) {
		rport = (struct bfa_fcs_rport_s *) qe;
		if (wwn_is_equal(rport->pwwn, pwwn))
			return rport;
	}

	bfa_trc(port->fcs, pwwn);
	return NULL;
}

/*
 *   NWWN based Lookup for a R-Port in the Port R-Port Queue
 */
struct bfa_fcs_rport_s *
bfa_fcs_lport_get_rport_by_nwwn(struct bfa_fcs_lport_s *port, wwn_t nwwn)
{
	struct bfa_fcs_rport_s *rport;
	struct list_head	*qe;

	list_for_each(qe, &port->rport_q) {
		rport = (struct bfa_fcs_rport_s *) qe;
		if (wwn_is_equal(rport->nwwn, nwwn))
			return rport;
	}

	bfa_trc(port->fcs, nwwn);
	return NULL;
}

/*
 * PWWN & PID based Lookup for a R-Port in the Port R-Port Queue
 */
struct bfa_fcs_rport_s *
bfa_fcs_lport_get_rport_by_qualifier(struct bfa_fcs_lport_s *port,
				     wwn_t pwwn, u32 pid)
{
	struct bfa_fcs_rport_s *rport;
	struct list_head	*qe;

	list_for_each(qe, &port->rport_q) {
		rport = (struct bfa_fcs_rport_s *) qe;
		if (wwn_is_equal(rport->pwwn, pwwn) && rport->pid == pid)
			return rport;
	}

	bfa_trc(port->fcs, pwwn);
	return NULL;
}

/*
 * Called by rport module when new rports are discovered.
 */
void
bfa_fcs_lport_add_rport(
	struct bfa_fcs_lport_s *port,
	struct bfa_fcs_rport_s *rport)
{
	list_add_tail(&rport->qe, &port->rport_q);
	port->num_rports++;
}

/*
 * Called by rport module to when rports are deleted.
 */
void
bfa_fcs_lport_del_rport(
	struct bfa_fcs_lport_s *port,
	struct bfa_fcs_rport_s *rport)
{
	WARN_ON(!bfa_q_is_on_q(&port->rport_q, rport));
	list_del(&rport->qe);
	port->num_rports--;

	bfa_sm_send_event(port, BFA_FCS_PORT_SM_DELRPORT);
}

/*
 * Called by fabric for base port when fabric login is complete.
 * Called by vport for virtual ports when FDISC is complete.
 */
void
bfa_fcs_lport_online(struct bfa_fcs_lport_s *port)
{
	bfa_sm_send_event(port, BFA_FCS_PORT_SM_ONLINE);
}

/*
 * Called by fabric for base port when fabric goes offline.
 * Called by vport for virtual ports when virtual port becomes offline.
 */
void
bfa_fcs_lport_offline(struct bfa_fcs_lport_s *port)
{
	bfa_sm_send_event(port, BFA_FCS_PORT_SM_OFFLINE);
}

/*
 * Called by fabric for base port and by vport for virtual ports
 * when target mode driver is unloaded.
 */
void
bfa_fcs_lport_stop(struct bfa_fcs_lport_s *port)
{
	bfa_sm_send_event(port, BFA_FCS_PORT_SM_STOP);
}

/*
 * Called by fabric to delete base lport and associated resources.
 *
 * Called by vport to delete lport and associated resources. Should call
 * bfa_fcs_vport_delete_comp() for vports on completion.
 */
void
bfa_fcs_lport_delete(struct bfa_fcs_lport_s *port)
{
	bfa_sm_send_event(port, BFA_FCS_PORT_SM_DELETE);
}

/*
 * Return TRUE if port is online, else return FALSE
 */
bfa_boolean_t
bfa_fcs_lport_is_online(struct bfa_fcs_lport_s *port)
{
	return bfa_sm_cmp_state(port, bfa_fcs_lport_sm_online);
}

/*
  * Attach time initialization of logical ports.
 */
void
bfa_fcs_lport_attach(struct bfa_fcs_lport_s *lport, struct bfa_fcs_s *fcs,
		   u16 vf_id, struct bfa_fcs_vport_s *vport)
{
	lport->fcs = fcs;
	lport->fabric = bfa_fcs_vf_lookup(fcs, vf_id);
	lport->vport = vport;
	lport->lp_tag = (vport) ? vport->lps->bfa_tag :
				  lport->fabric->lps->bfa_tag;

	INIT_LIST_HEAD(&lport->rport_q);
	lport->num_rports = 0;
}

/*
 * Logical port initialization of base or virtual port.
 * Called by fabric for base port or by vport for virtual ports.
 */

void
bfa_fcs_lport_init(struct bfa_fcs_lport_s *lport,
	struct bfa_lport_cfg_s *port_cfg)
{
	struct bfa_fcs_vport_s *vport = lport->vport;
	struct bfad_s *bfad = (struct bfad_s *)lport->fcs->bfad;
	char    lpwwn_buf[BFA_STRING_32];

	lport->port_cfg = *port_cfg;

	lport->bfad_port = bfa_fcb_lport_new(lport->fcs->bfad, lport,
					lport->port_cfg.roles,
					lport->fabric->vf_drv,
					vport ? vport->vport_drv : NULL);

	wwn2str(lpwwn_buf, bfa_fcs_lport_get_pwwn(lport));
	BFA_LOG(KERN_INFO, bfad, bfa_log_level,
		"New logical port created: WWN = %s Role = %s\n",
		lpwwn_buf, "Initiator");
	bfa_fcs_lport_aen_post(lport, BFA_LPORT_AEN_NEW);

	bfa_sm_set_state(lport, bfa_fcs_lport_sm_uninit);
	bfa_sm_send_event(lport, BFA_FCS_PORT_SM_CREATE);
}

void
bfa_fcs_lport_set_symname(struct bfa_fcs_lport_s *port,
				char *symname)
{
	strcpy(port->port_cfg.sym_name.symname, symname);

	if (bfa_sm_cmp_state(port, bfa_fcs_lport_sm_online))
		bfa_fcs_lport_ns_util_send_rspn_id(
			BFA_FCS_GET_NS_FROM_PORT(port), NULL);
}

/*
 *  fcs_lport_api
 */

void
bfa_fcs_lport_get_attr(
	struct bfa_fcs_lport_s *port,
	struct bfa_lport_attr_s *port_attr)
{
	if (bfa_sm_cmp_state(port, bfa_fcs_lport_sm_online))
		port_attr->pid = port->pid;
	else
		port_attr->pid = 0;

	port_attr->port_cfg = port->port_cfg;

	if (port->fabric) {
		port_attr->port_type = port->fabric->oper_type;
		port_attr->loopback = bfa_sm_cmp_state(port->fabric,
				bfa_fcs_fabric_sm_loopback);
		port_attr->authfail =
			bfa_sm_cmp_state(port->fabric,
				bfa_fcs_fabric_sm_auth_failed);
		port_attr->fabric_name  = bfa_fcs_lport_get_fabric_name(port);
		memcpy(port_attr->fabric_ip_addr,
			bfa_fcs_lport_get_fabric_ipaddr(port),
			BFA_FCS_FABRIC_IPADDR_SZ);

		if (port->vport != NULL) {
			port_attr->port_type = BFA_PORT_TYPE_VPORT;
			port_attr->fpma_mac =
				port->vport->lps->lp_mac;
		} else {
			port_attr->fpma_mac =
				port->fabric->lps->lp_mac;
		}
	} else {
		port_attr->port_type = BFA_PORT_TYPE_UNKNOWN;
		port_attr->state = BFA_LPORT_UNINIT;
	}
}

/*
 *  bfa_fcs_lport_fab port fab functions
 */

/*
 *   Called by port to initialize fabric services of the base port.
 */
static void
bfa_fcs_lport_fab_init(struct bfa_fcs_lport_s *port)
{
	bfa_fcs_lport_ns_init(port);
	bfa_fcs_lport_scn_init(port);
	bfa_fcs_lport_ms_init(port);
}

/*
 *   Called by port to notify transition to online state.
 */
static void
bfa_fcs_lport_fab_online(struct bfa_fcs_lport_s *port)
{
	bfa_fcs_lport_ns_online(port);
	bfa_fcs_lport_fab_scn_online(port);
}

/*
 *   Called by port to notify transition to offline state.
 */
static void
bfa_fcs_lport_fab_offline(struct bfa_fcs_lport_s *port)
{
	bfa_fcs_lport_ns_offline(port);
	bfa_fcs_lport_scn_offline(port);
	bfa_fcs_lport_ms_offline(port);
}

/*
 *  bfa_fcs_lport_n2n  functions
 */

/*
 *   Called by fcs/port to initialize N2N topology.
 */
static void
bfa_fcs_lport_n2n_init(struct bfa_fcs_lport_s *port)
{
}

/*
 *   Called by fcs/port to notify transition to online state.
 */
static void
bfa_fcs_lport_n2n_online(struct bfa_fcs_lport_s *port)
{
	struct bfa_fcs_lport_n2n_s *n2n_port = &port->port_topo.pn2n;
	struct bfa_lport_cfg_s *pcfg = &port->port_cfg;
	struct bfa_fcs_rport_s *rport;

	bfa_trc(port->fcs, pcfg->pwwn);

	/*
	 * If our PWWN is > than that of the r-port, we have to initiate PLOGI
	 * and assign an Address. if not, we need to wait for its PLOGI.
	 *
	 * If our PWWN is < than that of the remote port, it will send a PLOGI
	 * with the PIDs assigned. The rport state machine take care of this
	 * incoming PLOGI.
	 */
	if (memcmp
	    ((void *)&pcfg->pwwn, (void *)&n2n_port->rem_port_wwn,
	     sizeof(wwn_t)) > 0) {
		port->pid = N2N_LOCAL_PID;
		bfa_lps_set_n2n_pid(port->fabric->lps, N2N_LOCAL_PID);
		/*
		 * First, check if we know the device by pwwn.
		 */
		rport = bfa_fcs_lport_get_rport_by_pwwn(port,
							n2n_port->rem_port_wwn);
		if (rport) {
			bfa_trc(port->fcs, rport->pid);
			bfa_trc(port->fcs, rport->pwwn);
			rport->pid = N2N_REMOTE_PID;
			bfa_sm_send_event(rport, RPSM_EVENT_PLOGI_SEND);
			return;
		}

		/*
		 * In n2n there can be only one rport. Delete the old one
		 * whose pid should be zero, because it is offline.
		 */
		if (port->num_rports > 0) {
			rport = bfa_fcs_lport_get_rport_by_pid(port, 0);
			WARN_ON(rport == NULL);
			if (rport) {
				bfa_trc(port->fcs, rport->pwwn);
				bfa_sm_send_event(rport, RPSM_EVENT_DELETE);
			}
		}
		bfa_fcs_rport_create(port, N2N_REMOTE_PID);
	}
}

/*
 *   Called by fcs/port to notify transition to offline state.
 */
static void
bfa_fcs_lport_n2n_offline(struct bfa_fcs_lport_s *port)
{
	struct bfa_fcs_lport_n2n_s *n2n_port = &port->port_topo.pn2n;

	bfa_trc(port->fcs, port->pid);
	port->pid = 0;
	n2n_port->rem_port_wwn = 0;
	n2n_port->reply_oxid = 0;
}

void
bfa_fcport_get_loop_attr(struct bfa_fcs_lport_s *port)
{
	int i = 0, j = 0, bit = 0, alpa_bit = 0;
	u8 k = 0;
	struct bfa_fcport_s *fcport = BFA_FCPORT_MOD(port->fcs->bfa);

	port->port_topo.ploop.alpabm_valid = fcport->alpabm_valid;
	port->pid = fcport->myalpa;
	port->pid = bfa_hton3b(port->pid);

	for (i = 0; i < (FC_ALPA_MAX / 8); i++) {
		for (j = 0, alpa_bit = 0; j < 8; j++, alpa_bit++) {
			bfa_trc(port->fcs->bfa, fcport->alpabm.alpa_bm[i]);
			bit = (fcport->alpabm.alpa_bm[i] & (1 << (7 - j)));
			if (bit) {
				port->port_topo.ploop.alpa_pos_map[k] =
					loop_alpa_map[(i * 8) + alpa_bit];
				k++;
				bfa_trc(port->fcs->bfa, k);
				bfa_trc(port->fcs->bfa,
					 port->port_topo.ploop.alpa_pos_map[k]);
			}
		}
	}
	port->port_topo.ploop.num_alpa = k;
}

/*
 * Called by fcs/port to initialize Loop topology.
 */
static void
bfa_fcs_lport_loop_init(struct bfa_fcs_lport_s *port)
{
}

/*
 * Called by fcs/port to notify transition to online state.
 */
static void
bfa_fcs_lport_loop_online(struct bfa_fcs_lport_s *port)
{
	u8 num_alpa = 0, alpabm_valid = 0;
	struct bfa_fcs_rport_s *rport;
	u8 *alpa_map = NULL;
	int i = 0;
	u32 pid;

	bfa_fcport_get_loop_attr(port);

	num_alpa = port->port_topo.ploop.num_alpa;
	alpabm_valid = port->port_topo.ploop.alpabm_valid;
	alpa_map = port->port_topo.ploop.alpa_pos_map;

	bfa_trc(port->fcs->bfa, port->pid);
	bfa_trc(port->fcs->bfa, num_alpa);
	if (alpabm_valid == 1) {
		for (i = 0; i < num_alpa; i++) {
			bfa_trc(port->fcs->bfa, alpa_map[i]);
			if (alpa_map[i] != bfa_hton3b(port->pid)) {
				pid = alpa_map[i];
				bfa_trc(port->fcs->bfa, pid);
				rport = bfa_fcs_lport_get_rport_by_pid(port,
						bfa_hton3b(pid));
				if (!rport)
					rport = bfa_fcs_rport_create(port,
						bfa_hton3b(pid));
			}
		}
	} else {
		for (i = 0; i < MAX_ALPA_COUNT; i++) {
			if (alpa_map[i] != port->pid) {
				pid = loop_alpa_map[i];
				bfa_trc(port->fcs->bfa, pid);
				rport = bfa_fcs_lport_get_rport_by_pid(port,
						bfa_hton3b(pid));
				if (!rport)
					rport = bfa_fcs_rport_create(port,
						bfa_hton3b(pid));
			}
		}
	}
}

/*
 * Called by fcs/port to notify transition to offline state.
 */
static void
bfa_fcs_lport_loop_offline(struct bfa_fcs_lport_s *port)
{
}

#define BFA_FCS_FDMI_CMD_MAX_RETRIES 2

/*
 * forward declarations
 */
static void     bfa_fcs_lport_fdmi_send_rhba(void *fdmi_cbarg,
					    struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_lport_fdmi_send_rprt(void *fdmi_cbarg,
					    struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_lport_fdmi_send_rpa(void *fdmi_cbarg,
					   struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_lport_fdmi_rhba_response(void *fcsarg,
						struct bfa_fcxp_s *fcxp,
						void *cbarg,
						bfa_status_t req_status,
						u32 rsp_len,
						u32 resid_len,
						struct fchs_s *rsp_fchs);
static void     bfa_fcs_lport_fdmi_rprt_response(void *fcsarg,
						struct bfa_fcxp_s *fcxp,
						void *cbarg,
						bfa_status_t req_status,
						u32 rsp_len,
						u32 resid_len,
						struct fchs_s *rsp_fchs);
static void     bfa_fcs_lport_fdmi_rpa_response(void *fcsarg,
					       struct bfa_fcxp_s *fcxp,
					       void *cbarg,
					       bfa_status_t req_status,
					       u32 rsp_len,
					       u32 resid_len,
					       struct fchs_s *rsp_fchs);
static void     bfa_fcs_lport_fdmi_timeout(void *arg);
static u16 bfa_fcs_lport_fdmi_build_rhba_pyld(struct bfa_fcs_lport_fdmi_s *fdmi,
						  u8 *pyld);
static u16 bfa_fcs_lport_fdmi_build_rprt_pyld(struct bfa_fcs_lport_fdmi_s *fdmi,
						  u8 *pyld);
static u16 bfa_fcs_lport_fdmi_build_rpa_pyld(struct bfa_fcs_lport_fdmi_s *fdmi,
						 u8 *pyld);
static u16 bfa_fcs_lport_fdmi_build_portattr_block(struct bfa_fcs_lport_fdmi_s *
						       fdmi, u8 *pyld);
static void	bfa_fcs_fdmi_get_hbaattr(struct bfa_fcs_lport_fdmi_s *fdmi,
				 struct bfa_fcs_fdmi_hba_attr_s *hba_attr);
static void	bfa_fcs_fdmi_get_portattr(struct bfa_fcs_lport_fdmi_s *fdmi,
				  struct bfa_fcs_fdmi_port_attr_s *port_attr);
u32	bfa_fcs_fdmi_convert_speed(enum bfa_port_speed pport_speed);

/*
 *  fcs_fdmi_sm FCS FDMI state machine
 */

/*
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

static void     bfa_fcs_lport_fdmi_sm_offline(struct bfa_fcs_lport_fdmi_s *fdmi,
					     enum port_fdmi_event event);
static void     bfa_fcs_lport_fdmi_sm_sending_rhba(
				struct bfa_fcs_lport_fdmi_s *fdmi,
				enum port_fdmi_event event);
static void     bfa_fcs_lport_fdmi_sm_rhba(struct bfa_fcs_lport_fdmi_s *fdmi,
					  enum port_fdmi_event event);
static void     bfa_fcs_lport_fdmi_sm_rhba_retry(
				struct bfa_fcs_lport_fdmi_s *fdmi,
				enum port_fdmi_event event);
static void     bfa_fcs_lport_fdmi_sm_sending_rprt(
				struct bfa_fcs_lport_fdmi_s *fdmi,
				enum port_fdmi_event event);
static void     bfa_fcs_lport_fdmi_sm_rprt(struct bfa_fcs_lport_fdmi_s *fdmi,
					  enum port_fdmi_event event);
static void     bfa_fcs_lport_fdmi_sm_rprt_retry(
				struct bfa_fcs_lport_fdmi_s *fdmi,
				enum port_fdmi_event event);
static void     bfa_fcs_lport_fdmi_sm_sending_rpa(
				struct bfa_fcs_lport_fdmi_s *fdmi,
				enum port_fdmi_event event);
static void     bfa_fcs_lport_fdmi_sm_rpa(struct bfa_fcs_lport_fdmi_s *fdmi,
					 enum port_fdmi_event event);
static void     bfa_fcs_lport_fdmi_sm_rpa_retry(
				struct bfa_fcs_lport_fdmi_s *fdmi,
				enum port_fdmi_event event);
static void     bfa_fcs_lport_fdmi_sm_online(struct bfa_fcs_lport_fdmi_s *fdmi,
					    enum port_fdmi_event event);
static void     bfa_fcs_lport_fdmi_sm_disabled(
				struct bfa_fcs_lport_fdmi_s *fdmi,
				enum port_fdmi_event event);
/*
 *	Start in offline state - awaiting MS to send start.
 */
static void
bfa_fcs_lport_fdmi_sm_offline(struct bfa_fcs_lport_fdmi_s *fdmi,
			     enum port_fdmi_event event)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;

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
					 bfa_fcs_lport_fdmi_sm_sending_rprt);
			bfa_fcs_lport_fdmi_send_rprt(fdmi, NULL);
		} else {
			/*
			 * For a base port, we should first register the HBA
			 * attribute. The HBA attribute also contains the base
			 *  port registration.
			 */
			bfa_sm_set_state(fdmi,
					 bfa_fcs_lport_fdmi_sm_sending_rhba);
			bfa_fcs_lport_fdmi_send_rhba(fdmi, NULL);
		}
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_lport_fdmi_sm_sending_rhba(struct bfa_fcs_lport_fdmi_s *fdmi,
				  enum port_fdmi_event event)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_RHBA_SENT:
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_rhba);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(port),
					   &fdmi->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_lport_fdmi_sm_rhba(struct bfa_fcs_lport_fdmi_s *fdmi,
			enum port_fdmi_event event)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_RSP_ERROR:
		/*
		 * if max retries have not been reached, start timer for a
		 * delayed retry
		 */
		if (fdmi->retry_cnt++ < BFA_FCS_FDMI_CMD_MAX_RETRIES) {
			bfa_sm_set_state(fdmi,
					bfa_fcs_lport_fdmi_sm_rhba_retry);
			bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(port),
					    &fdmi->timer,
					    bfa_fcs_lport_fdmi_timeout, fdmi,
					    BFA_FCS_RETRY_TIMEOUT);
		} else {
			/*
			 * set state to offline
			 */
			bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_offline);
		}
		break;

	case FDMISM_EVENT_RSP_OK:
		/*
		 * Initiate Register Port Attributes
		 */
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_sending_rpa);
		fdmi->retry_cnt = 0;
		bfa_fcs_lport_fdmi_send_rpa(fdmi, NULL);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_fcxp_discard(fdmi->fcxp);
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_offline);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_lport_fdmi_sm_rhba_retry(struct bfa_fcs_lport_fdmi_s *fdmi,
				enum port_fdmi_event event)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_sending_rhba);
		bfa_fcs_lport_fdmi_send_rhba(fdmi, NULL);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_offline);
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
bfa_fcs_lport_fdmi_sm_sending_rprt(struct bfa_fcs_lport_fdmi_s *fdmi,
				  enum port_fdmi_event event)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_RPRT_SENT:
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_rprt);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(port),
					   &fdmi->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_lport_fdmi_sm_rprt(struct bfa_fcs_lport_fdmi_s *fdmi,
			enum port_fdmi_event event)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_RSP_ERROR:
		/*
		 * if max retries have not been reached, start timer for a
		 * delayed retry
		 */
		if (fdmi->retry_cnt++ < BFA_FCS_FDMI_CMD_MAX_RETRIES) {
			bfa_sm_set_state(fdmi,
					bfa_fcs_lport_fdmi_sm_rprt_retry);
			bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(port),
					    &fdmi->timer,
					    bfa_fcs_lport_fdmi_timeout, fdmi,
					    BFA_FCS_RETRY_TIMEOUT);

		} else {
			/*
			 * set state to offline
			 */
			bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_offline);
			fdmi->retry_cnt = 0;
		}
		break;

	case FDMISM_EVENT_RSP_OK:
		fdmi->retry_cnt = 0;
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_online);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_fcxp_discard(fdmi->fcxp);
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_offline);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_lport_fdmi_sm_rprt_retry(struct bfa_fcs_lport_fdmi_s *fdmi,
				enum port_fdmi_event event)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_sending_rprt);
		bfa_fcs_lport_fdmi_send_rprt(fdmi, NULL);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_offline);
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
bfa_fcs_lport_fdmi_sm_sending_rpa(struct bfa_fcs_lport_fdmi_s *fdmi,
				 enum port_fdmi_event event)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_RPA_SENT:
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_rpa);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(port),
					   &fdmi->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_lport_fdmi_sm_rpa(struct bfa_fcs_lport_fdmi_s *fdmi,
			enum port_fdmi_event event)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_RSP_ERROR:
		/*
		 * if max retries have not been reached, start timer for a
		 * delayed retry
		 */
		if (fdmi->retry_cnt++ < BFA_FCS_FDMI_CMD_MAX_RETRIES) {
			bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_rpa_retry);
			bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(port),
					    &fdmi->timer,
					    bfa_fcs_lport_fdmi_timeout, fdmi,
					    BFA_FCS_RETRY_TIMEOUT);
		} else {
			/*
			 * set state to offline
			 */
			bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_offline);
			fdmi->retry_cnt = 0;
		}
		break;

	case FDMISM_EVENT_RSP_OK:
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_online);
		fdmi->retry_cnt = 0;
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_fcxp_discard(fdmi->fcxp);
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_offline);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_lport_fdmi_sm_rpa_retry(struct bfa_fcs_lport_fdmi_s *fdmi,
			       enum port_fdmi_event event)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_sending_rpa);
		bfa_fcs_lport_fdmi_send_rpa(fdmi, NULL);
		break;

	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_offline);
		bfa_timer_stop(&fdmi->timer);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_lport_fdmi_sm_online(struct bfa_fcs_lport_fdmi_s *fdmi,
				enum port_fdmi_event event)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	switch (event) {
	case FDMISM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_offline);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}
/*
 *  FDMI is disabled state.
 */
static void
bfa_fcs_lport_fdmi_sm_disabled(struct bfa_fcs_lport_fdmi_s *fdmi,
			     enum port_fdmi_event event)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;

	bfa_trc(port->fcs, port->port_cfg.pwwn);
	bfa_trc(port->fcs, event);

	/* No op State. It can only be enabled at Driver Init. */
}

/*
*  RHBA : Register HBA Attributes.
 */
static void
bfa_fcs_lport_fdmi_send_rhba(void *fdmi_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_lport_fdmi_s *fdmi = fdmi_cbarg;
	struct bfa_fcs_lport_s *port = fdmi->ms->port;
	struct fchs_s fchs;
	int             len, attr_len;
	struct bfa_fcxp_s *fcxp;
	u8        *pyld;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &fdmi->fcxp_wqe,
				bfa_fcs_lport_fdmi_send_rhba, fdmi, BFA_TRUE);
		return;
	}
	fdmi->fcxp = fcxp;

	pyld = bfa_fcxp_get_reqbuf(fcxp);
	memset(pyld, 0, FC_MAX_PDUSZ);

	len = fc_fdmi_reqhdr_build(&fchs, pyld, bfa_fcs_lport_get_fcid(port),
				   FDMI_RHBA);

	attr_len =
		bfa_fcs_lport_fdmi_build_rhba_pyld(fdmi,
					  (u8 *) ((struct ct_hdr_s *) pyld
						       + 1));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			  FC_CLASS_3, (len + attr_len), &fchs,
			  bfa_fcs_lport_fdmi_rhba_response, (void *)fdmi,
			  FC_MAX_PDUSZ, FC_FCCT_TOV);

	bfa_sm_send_event(fdmi, FDMISM_EVENT_RHBA_SENT);
}

static          u16
bfa_fcs_lport_fdmi_build_rhba_pyld(struct bfa_fcs_lport_fdmi_s *fdmi, u8 *pyld)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;
	struct bfa_fcs_fdmi_hba_attr_s hba_attr;
	struct bfa_fcs_fdmi_hba_attr_s *fcs_hba_attr = &hba_attr;
	struct fdmi_rhba_s *rhba = (struct fdmi_rhba_s *) pyld;
	struct fdmi_attr_s *attr;
	u8        *curr_ptr;
	u16        len, count;
	u16	templen;

	/*
	 * get hba attributes
	 */
	bfa_fcs_fdmi_get_hbaattr(fdmi, fcs_hba_attr);

	rhba->hba_id = bfa_fcs_lport_get_pwwn(port);
	rhba->port_list.num_ports = cpu_to_be32(1);
	rhba->port_list.port_entry = bfa_fcs_lport_get_pwwn(port);

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
	attr->type = cpu_to_be16(FDMI_HBA_ATTRIB_NODENAME);
	templen = sizeof(wwn_t);
	memcpy(attr->value, &bfa_fcs_lport_get_nwwn(port), templen);
	curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
	len += templen;
	count++;
	attr->len = cpu_to_be16(templen + sizeof(attr->type) +
			     sizeof(templen));

	/*
	 * Manufacturer
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = cpu_to_be16(FDMI_HBA_ATTRIB_MANUFACTURER);
	templen = (u16) strlen(fcs_hba_attr->manufacturer);
	memcpy(attr->value, fcs_hba_attr->manufacturer, templen);
	templen = fc_roundup(templen, sizeof(u32));
	curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
	len += templen;
	count++;
	attr->len = cpu_to_be16(templen + sizeof(attr->type) +
			     sizeof(templen));

	/*
	 * Serial Number
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = cpu_to_be16(FDMI_HBA_ATTRIB_SERIALNUM);
	templen = (u16) strlen(fcs_hba_attr->serial_num);
	memcpy(attr->value, fcs_hba_attr->serial_num, templen);
	templen = fc_roundup(templen, sizeof(u32));
	curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
	len += templen;
	count++;
	attr->len = cpu_to_be16(templen + sizeof(attr->type) +
			     sizeof(templen));

	/*
	 * Model
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = cpu_to_be16(FDMI_HBA_ATTRIB_MODEL);
	templen = (u16) strlen(fcs_hba_attr->model);
	memcpy(attr->value, fcs_hba_attr->model, templen);
	templen = fc_roundup(templen, sizeof(u32));
	curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
	len += templen;
	count++;
	attr->len = cpu_to_be16(templen + sizeof(attr->type) +
			     sizeof(templen));

	/*
	 * Model Desc
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = cpu_to_be16(FDMI_HBA_ATTRIB_MODEL_DESC);
	templen = (u16) strlen(fcs_hba_attr->model_desc);
	memcpy(attr->value, fcs_hba_attr->model_desc, templen);
	templen = fc_roundup(templen, sizeof(u32));
	curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
	len += templen;
	count++;
	attr->len = cpu_to_be16(templen + sizeof(attr->type) +
			     sizeof(templen));

	/*
	 * H/W Version
	 */
	if (fcs_hba_attr->hw_version[0] != '\0') {
		attr = (struct fdmi_attr_s *) curr_ptr;
		attr->type = cpu_to_be16(FDMI_HBA_ATTRIB_HW_VERSION);
		templen = (u16) strlen(fcs_hba_attr->hw_version);
		memcpy(attr->value, fcs_hba_attr->hw_version, templen);
		templen = fc_roundup(templen, sizeof(u32));
		curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
		len += templen;
		count++;
		attr->len = cpu_to_be16(templen + sizeof(attr->type) +
					 sizeof(templen));
	}

	/*
	 * Driver Version
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = cpu_to_be16(FDMI_HBA_ATTRIB_DRIVER_VERSION);
	templen = (u16) strlen(fcs_hba_attr->driver_version);
	memcpy(attr->value, fcs_hba_attr->driver_version, templen);
	templen = fc_roundup(templen, sizeof(u32));
	curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
	len += templen;
	count++;
	attr->len = cpu_to_be16(templen + sizeof(attr->type) +
			     sizeof(templen));

	/*
	 * Option Rom Version
	 */
	if (fcs_hba_attr->option_rom_ver[0] != '\0') {
		attr = (struct fdmi_attr_s *) curr_ptr;
		attr->type = cpu_to_be16(FDMI_HBA_ATTRIB_ROM_VERSION);
		templen = (u16) strlen(fcs_hba_attr->option_rom_ver);
		memcpy(attr->value, fcs_hba_attr->option_rom_ver, templen);
		templen = fc_roundup(templen, sizeof(u32));
		curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
		len += templen;
		count++;
		attr->len = cpu_to_be16(templen + sizeof(attr->type) +
					 sizeof(templen));
	}

	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = cpu_to_be16(FDMI_HBA_ATTRIB_FW_VERSION);
	templen = (u16) strlen(fcs_hba_attr->fw_version);
	memcpy(attr->value, fcs_hba_attr->fw_version, templen);
	templen = fc_roundup(templen, sizeof(u32));
	curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
	len += templen;
	count++;
	attr->len = cpu_to_be16(templen + sizeof(attr->type) +
			     sizeof(templen));

	/*
	 * OS Name
	 */
	if (fcs_hba_attr->os_name[0] != '\0') {
		attr = (struct fdmi_attr_s *) curr_ptr;
		attr->type = cpu_to_be16(FDMI_HBA_ATTRIB_OS_NAME);
		templen = (u16) strlen(fcs_hba_attr->os_name);
		memcpy(attr->value, fcs_hba_attr->os_name, templen);
		templen = fc_roundup(templen, sizeof(u32));
		curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
		len += templen;
		count++;
		attr->len = cpu_to_be16(templen + sizeof(attr->type) +
					sizeof(templen));
	}

	/*
	 * MAX_CT_PAYLOAD
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = cpu_to_be16(FDMI_HBA_ATTRIB_MAX_CT);
	templen = sizeof(fcs_hba_attr->max_ct_pyld);
	memcpy(attr->value, &fcs_hba_attr->max_ct_pyld, templen);
	len += templen;
	count++;
	attr->len = cpu_to_be16(templen + sizeof(attr->type) +
			     sizeof(templen));

	/*
	 * Update size of payload
	 */
	len += ((sizeof(attr->type) + sizeof(attr->len)) * count);

	rhba->hba_attr_blk.attr_count = cpu_to_be32(count);
	return len;
}

static void
bfa_fcs_lport_fdmi_rhba_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
				void *cbarg, bfa_status_t req_status,
				u32 rsp_len, u32 resid_len,
				struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_lport_fdmi_s *fdmi =
				(struct bfa_fcs_lport_fdmi_s *) cbarg;
	struct bfa_fcs_lport_s *port = fdmi->ms->port;
	struct ct_hdr_s *cthdr = NULL;

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
	cthdr->cmd_rsp_code = be16_to_cpu(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		bfa_sm_send_event(fdmi, FDMISM_EVENT_RSP_OK);
		return;
	}

	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(fdmi, FDMISM_EVENT_RSP_ERROR);
}

/*
*  RPRT : Register Port
 */
static void
bfa_fcs_lport_fdmi_send_rprt(void *fdmi_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_lport_fdmi_s *fdmi = fdmi_cbarg;
	struct bfa_fcs_lport_s *port = fdmi->ms->port;
	struct fchs_s fchs;
	u16        len, attr_len;
	struct bfa_fcxp_s *fcxp;
	u8        *pyld;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &fdmi->fcxp_wqe,
				bfa_fcs_lport_fdmi_send_rprt, fdmi, BFA_TRUE);
		return;
	}
	fdmi->fcxp = fcxp;

	pyld = bfa_fcxp_get_reqbuf(fcxp);
	memset(pyld, 0, FC_MAX_PDUSZ);

	len = fc_fdmi_reqhdr_build(&fchs, pyld, bfa_fcs_lport_get_fcid(port),
				   FDMI_RPRT);

	attr_len =
		bfa_fcs_lport_fdmi_build_rprt_pyld(fdmi,
					  (u8 *) ((struct ct_hdr_s *) pyld
						       + 1));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			  FC_CLASS_3, len + attr_len, &fchs,
			  bfa_fcs_lport_fdmi_rprt_response, (void *)fdmi,
			  FC_MAX_PDUSZ, FC_FCCT_TOV);

	bfa_sm_send_event(fdmi, FDMISM_EVENT_RPRT_SENT);
}

/*
 * This routine builds Port Attribute Block that used in RPA, RPRT commands.
 */
static          u16
bfa_fcs_lport_fdmi_build_portattr_block(struct bfa_fcs_lport_fdmi_s *fdmi,
				       u8 *pyld)
{
	struct bfa_fcs_fdmi_port_attr_s fcs_port_attr;
	struct fdmi_port_attr_s *port_attrib = (struct fdmi_port_attr_s *) pyld;
	struct fdmi_attr_s *attr;
	u8        *curr_ptr;
	u16        len;
	u8	count = 0;
	u16	templen;

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
	attr->type = cpu_to_be16(FDMI_PORT_ATTRIB_FC4_TYPES);
	templen = sizeof(fcs_port_attr.supp_fc4_types);
	memcpy(attr->value, fcs_port_attr.supp_fc4_types, templen);
	curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
	len += templen;
	++count;
	attr->len =
		cpu_to_be16(templen + sizeof(attr->type) +
			     sizeof(templen));

	/*
	 * Supported Speed
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = cpu_to_be16(FDMI_PORT_ATTRIB_SUPP_SPEED);
	templen = sizeof(fcs_port_attr.supp_speed);
	memcpy(attr->value, &fcs_port_attr.supp_speed, templen);
	curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
	len += templen;
	++count;
	attr->len =
		cpu_to_be16(templen + sizeof(attr->type) +
			     sizeof(templen));

	/*
	 * current Port Speed
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = cpu_to_be16(FDMI_PORT_ATTRIB_PORT_SPEED);
	templen = sizeof(fcs_port_attr.curr_speed);
	memcpy(attr->value, &fcs_port_attr.curr_speed, templen);
	curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
	len += templen;
	++count;
	attr->len = cpu_to_be16(templen + sizeof(attr->type) +
			     sizeof(templen));

	/*
	 * max frame size
	 */
	attr = (struct fdmi_attr_s *) curr_ptr;
	attr->type = cpu_to_be16(FDMI_PORT_ATTRIB_FRAME_SIZE);
	templen = sizeof(fcs_port_attr.max_frm_size);
	memcpy(attr->value, &fcs_port_attr.max_frm_size, templen);
	curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
	len += templen;
	++count;
	attr->len = cpu_to_be16(templen + sizeof(attr->type) +
			     sizeof(templen));

	/*
	 * OS Device Name
	 */
	if (fcs_port_attr.os_device_name[0] != '\0') {
		attr = (struct fdmi_attr_s *) curr_ptr;
		attr->type = cpu_to_be16(FDMI_PORT_ATTRIB_DEV_NAME);
		templen = (u16) strlen(fcs_port_attr.os_device_name);
		memcpy(attr->value, fcs_port_attr.os_device_name, templen);
		templen = fc_roundup(templen, sizeof(u32));
		curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
		len += templen;
		++count;
		attr->len = cpu_to_be16(templen + sizeof(attr->type) +
					sizeof(templen));
	}
	/*
	 * Host Name
	 */
	if (fcs_port_attr.host_name[0] != '\0') {
		attr = (struct fdmi_attr_s *) curr_ptr;
		attr->type = cpu_to_be16(FDMI_PORT_ATTRIB_HOST_NAME);
		templen = (u16) strlen(fcs_port_attr.host_name);
		memcpy(attr->value, fcs_port_attr.host_name, templen);
		templen = fc_roundup(templen, sizeof(u32));
		curr_ptr += sizeof(attr->type) + sizeof(templen) + templen;
		len += templen;
		++count;
		attr->len = cpu_to_be16(templen + sizeof(attr->type) +
				sizeof(templen));
	}

	/*
	 * Update size of payload
	 */
	port_attrib->attr_count = cpu_to_be32(count);
	len += ((sizeof(attr->type) + sizeof(attr->len)) * count);
	return len;
}

static          u16
bfa_fcs_lport_fdmi_build_rprt_pyld(struct bfa_fcs_lport_fdmi_s *fdmi, u8 *pyld)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;
	struct fdmi_rprt_s *rprt = (struct fdmi_rprt_s *) pyld;
	u16        len;

	rprt->hba_id = bfa_fcs_lport_get_pwwn(bfa_fcs_get_base_port(port->fcs));
	rprt->port_name = bfa_fcs_lport_get_pwwn(port);

	len = bfa_fcs_lport_fdmi_build_portattr_block(fdmi,
				(u8 *) &rprt->port_attr_blk);

	len += sizeof(rprt->hba_id) + sizeof(rprt->port_name);

	return len;
}

static void
bfa_fcs_lport_fdmi_rprt_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
				void *cbarg, bfa_status_t req_status,
				u32 rsp_len, u32 resid_len,
				struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_lport_fdmi_s *fdmi =
			(struct bfa_fcs_lport_fdmi_s *) cbarg;
	struct bfa_fcs_lport_s *port = fdmi->ms->port;
	struct ct_hdr_s *cthdr = NULL;

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
	cthdr->cmd_rsp_code = be16_to_cpu(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		bfa_sm_send_event(fdmi, FDMISM_EVENT_RSP_OK);
		return;
	}

	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(fdmi, FDMISM_EVENT_RSP_ERROR);
}

/*
*  RPA : Register Port Attributes.
 */
static void
bfa_fcs_lport_fdmi_send_rpa(void *fdmi_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_lport_fdmi_s *fdmi = fdmi_cbarg;
	struct bfa_fcs_lport_s *port = fdmi->ms->port;
	struct fchs_s fchs;
	u16        len, attr_len;
	struct bfa_fcxp_s *fcxp;
	u8        *pyld;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &fdmi->fcxp_wqe,
				bfa_fcs_lport_fdmi_send_rpa, fdmi, BFA_TRUE);
		return;
	}
	fdmi->fcxp = fcxp;

	pyld = bfa_fcxp_get_reqbuf(fcxp);
	memset(pyld, 0, FC_MAX_PDUSZ);

	len = fc_fdmi_reqhdr_build(&fchs, pyld, bfa_fcs_lport_get_fcid(port),
				   FDMI_RPA);

	attr_len = bfa_fcs_lport_fdmi_build_rpa_pyld(fdmi,
				(u8 *) ((struct ct_hdr_s *) pyld + 1));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			  FC_CLASS_3, len + attr_len, &fchs,
			  bfa_fcs_lport_fdmi_rpa_response, (void *)fdmi,
			  FC_MAX_PDUSZ, FC_FCCT_TOV);

	bfa_sm_send_event(fdmi, FDMISM_EVENT_RPA_SENT);
}

static          u16
bfa_fcs_lport_fdmi_build_rpa_pyld(struct bfa_fcs_lport_fdmi_s *fdmi, u8 *pyld)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;
	struct fdmi_rpa_s *rpa = (struct fdmi_rpa_s *) pyld;
	u16        len;

	rpa->port_name = bfa_fcs_lport_get_pwwn(port);

	len = bfa_fcs_lport_fdmi_build_portattr_block(fdmi,
				(u8 *) &rpa->port_attr_blk);

	len += sizeof(rpa->port_name);

	return len;
}

static void
bfa_fcs_lport_fdmi_rpa_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
			void *cbarg, bfa_status_t req_status, u32 rsp_len,
			u32 resid_len, struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_lport_fdmi_s *fdmi =
				(struct bfa_fcs_lport_fdmi_s *) cbarg;
	struct bfa_fcs_lport_s *port = fdmi->ms->port;
	struct ct_hdr_s *cthdr = NULL;

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
	cthdr->cmd_rsp_code = be16_to_cpu(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		bfa_sm_send_event(fdmi, FDMISM_EVENT_RSP_OK);
		return;
	}

	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(fdmi, FDMISM_EVENT_RSP_ERROR);
}

static void
bfa_fcs_lport_fdmi_timeout(void *arg)
{
	struct bfa_fcs_lport_fdmi_s *fdmi = (struct bfa_fcs_lport_fdmi_s *) arg;

	bfa_sm_send_event(fdmi, FDMISM_EVENT_TIMEOUT);
}

static void
bfa_fcs_fdmi_get_hbaattr(struct bfa_fcs_lport_fdmi_s *fdmi,
			 struct bfa_fcs_fdmi_hba_attr_s *hba_attr)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;
	struct bfa_fcs_driver_info_s  *driver_info = &port->fcs->driver_info;
	struct bfa_fcs_fdmi_port_attr_s fcs_port_attr;

	memset(hba_attr, 0, sizeof(struct bfa_fcs_fdmi_hba_attr_s));

	bfa_ioc_get_adapter_manufacturer(&port->fcs->bfa->ioc,
					hba_attr->manufacturer);
	bfa_ioc_get_adapter_serial_num(&port->fcs->bfa->ioc,
					hba_attr->serial_num);
	bfa_ioc_get_adapter_model(&port->fcs->bfa->ioc,
					hba_attr->model);
	bfa_ioc_get_adapter_model(&port->fcs->bfa->ioc,
					hba_attr->model_desc);
	bfa_ioc_get_pci_chip_rev(&port->fcs->bfa->ioc,
					hba_attr->hw_version);
	bfa_ioc_get_adapter_optrom_ver(&port->fcs->bfa->ioc,
					hba_attr->option_rom_ver);
	bfa_ioc_get_adapter_fw_ver(&port->fcs->bfa->ioc,
					hba_attr->fw_version);

	strncpy(hba_attr->driver_version, (char *)driver_info->version,
		sizeof(hba_attr->driver_version));

	strncpy(hba_attr->os_name, driver_info->host_os_name,
		sizeof(hba_attr->os_name));

	/*
	 * If there is a patch level, append it
	 * to the os name along with a separator
	 */
	if (driver_info->host_os_patch[0] != '\0') {
		strncat(hba_attr->os_name, BFA_FCS_PORT_SYMBNAME_SEPARATOR,
			sizeof(BFA_FCS_PORT_SYMBNAME_SEPARATOR));
		strncat(hba_attr->os_name, driver_info->host_os_patch,
				sizeof(driver_info->host_os_patch));
	}

	/* Retrieve the max frame size from the port attr */
	bfa_fcs_fdmi_get_portattr(fdmi, &fcs_port_attr);
	hba_attr->max_ct_pyld = fcs_port_attr.max_frm_size;
}

static void
bfa_fcs_fdmi_get_portattr(struct bfa_fcs_lport_fdmi_s *fdmi,
			  struct bfa_fcs_fdmi_port_attr_s *port_attr)
{
	struct bfa_fcs_lport_s *port = fdmi->ms->port;
	struct bfa_fcs_driver_info_s  *driver_info = &port->fcs->driver_info;
	struct bfa_port_attr_s pport_attr;

	memset(port_attr, 0, sizeof(struct bfa_fcs_fdmi_port_attr_s));

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
	switch (pport_attr.speed_supported) {
	case BFA_PORT_SPEED_16GBPS:
		port_attr->supp_speed =
			cpu_to_be32(BFA_FCS_FDMI_SUPP_SPEEDS_16G);
		break;

	case BFA_PORT_SPEED_10GBPS:
		port_attr->supp_speed =
			cpu_to_be32(BFA_FCS_FDMI_SUPP_SPEEDS_10G);
		break;

	case BFA_PORT_SPEED_8GBPS:
		port_attr->supp_speed =
			cpu_to_be32(BFA_FCS_FDMI_SUPP_SPEEDS_8G);
		break;

	case BFA_PORT_SPEED_4GBPS:
		port_attr->supp_speed =
			cpu_to_be32(BFA_FCS_FDMI_SUPP_SPEEDS_4G);
		break;

	default:
		bfa_sm_fault(port->fcs, pport_attr.speed_supported);
	}

	/*
	 * Current Speed
	 */
	port_attr->curr_speed = cpu_to_be32(
				bfa_fcs_fdmi_convert_speed(pport_attr.speed));

	/*
	 * Max PDU Size.
	 */
	port_attr->max_frm_size = cpu_to_be32(pport_attr.pport_cfg.maxfrsize);

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

/*
 * Convert BFA speed to FDMI format.
 */
u32
bfa_fcs_fdmi_convert_speed(bfa_port_speed_t pport_speed)
{
	u32	ret;

	switch (pport_speed) {
	case BFA_PORT_SPEED_1GBPS:
	case BFA_PORT_SPEED_2GBPS:
		ret = pport_speed;
		break;

	case BFA_PORT_SPEED_4GBPS:
		ret = FDMI_TRANS_SPEED_4G;
		break;

	case BFA_PORT_SPEED_8GBPS:
		ret = FDMI_TRANS_SPEED_8G;
		break;

	case BFA_PORT_SPEED_10GBPS:
		ret = FDMI_TRANS_SPEED_10G;
		break;

	case BFA_PORT_SPEED_16GBPS:
		ret = FDMI_TRANS_SPEED_16G;
		break;

	default:
		ret = FDMI_TRANS_SPEED_UNKNOWN;
	}
	return ret;
}

void
bfa_fcs_lport_fdmi_init(struct bfa_fcs_lport_ms_s *ms)
{
	struct bfa_fcs_lport_fdmi_s *fdmi = &ms->fdmi;

	fdmi->ms = ms;
	if (ms->port->fcs->fdmi_enabled)
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_offline);
	else
		bfa_sm_set_state(fdmi, bfa_fcs_lport_fdmi_sm_disabled);
}

void
bfa_fcs_lport_fdmi_offline(struct bfa_fcs_lport_ms_s *ms)
{
	struct bfa_fcs_lport_fdmi_s *fdmi = &ms->fdmi;

	fdmi->ms = ms;
	bfa_sm_send_event(fdmi, FDMISM_EVENT_PORT_OFFLINE);
}

void
bfa_fcs_lport_fdmi_online(struct bfa_fcs_lport_ms_s *ms)
{
	struct bfa_fcs_lport_fdmi_s *fdmi = &ms->fdmi;

	fdmi->ms = ms;
	bfa_sm_send_event(fdmi, FDMISM_EVENT_PORT_ONLINE);
}

#define BFA_FCS_MS_CMD_MAX_RETRIES  2

/*
 * forward declarations
 */
static void     bfa_fcs_lport_ms_send_plogi(void *ms_cbarg,
					   struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_lport_ms_timeout(void *arg);
static void     bfa_fcs_lport_ms_plogi_response(void *fcsarg,
					       struct bfa_fcxp_s *fcxp,
					       void *cbarg,
					       bfa_status_t req_status,
					       u32 rsp_len,
					       u32 resid_len,
					       struct fchs_s *rsp_fchs);

static void	bfa_fcs_lport_ms_send_gmal(void *ms_cbarg,
					struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_lport_ms_gmal_response(void *fcsarg,
					       struct bfa_fcxp_s *fcxp,
					       void *cbarg,
					       bfa_status_t req_status,
					       u32 rsp_len,
					       u32 resid_len,
					       struct fchs_s *rsp_fchs);
static void	bfa_fcs_lport_ms_send_gfn(void *ms_cbarg,
					struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_lport_ms_gfn_response(void *fcsarg,
					       struct bfa_fcxp_s *fcxp,
					       void *cbarg,
					       bfa_status_t req_status,
					       u32 rsp_len,
					       u32 resid_len,
					       struct fchs_s *rsp_fchs);
/*
 *  fcs_ms_sm FCS MS state machine
 */

/*
 *  MS State Machine events
 */
enum port_ms_event {
	MSSM_EVENT_PORT_ONLINE = 1,
	MSSM_EVENT_PORT_OFFLINE = 2,
	MSSM_EVENT_RSP_OK = 3,
	MSSM_EVENT_RSP_ERROR = 4,
	MSSM_EVENT_TIMEOUT = 5,
	MSSM_EVENT_FCXP_SENT = 6,
	MSSM_EVENT_PORT_FABRIC_RSCN = 7
};

static void     bfa_fcs_lport_ms_sm_offline(struct bfa_fcs_lport_ms_s *ms,
					   enum port_ms_event event);
static void     bfa_fcs_lport_ms_sm_plogi_sending(struct bfa_fcs_lport_ms_s *ms,
						 enum port_ms_event event);
static void     bfa_fcs_lport_ms_sm_plogi(struct bfa_fcs_lport_ms_s *ms,
					 enum port_ms_event event);
static void     bfa_fcs_lport_ms_sm_plogi_retry(struct bfa_fcs_lport_ms_s *ms,
					       enum port_ms_event event);
static void     bfa_fcs_lport_ms_sm_gmal_sending(struct bfa_fcs_lport_ms_s *ms,
						 enum port_ms_event event);
static void     bfa_fcs_lport_ms_sm_gmal(struct bfa_fcs_lport_ms_s *ms,
					 enum port_ms_event event);
static void     bfa_fcs_lport_ms_sm_gmal_retry(struct bfa_fcs_lport_ms_s *ms,
					       enum port_ms_event event);
static void     bfa_fcs_lport_ms_sm_gfn_sending(struct bfa_fcs_lport_ms_s *ms,
						 enum port_ms_event event);
static void     bfa_fcs_lport_ms_sm_gfn(struct bfa_fcs_lport_ms_s *ms,
					 enum port_ms_event event);
static void     bfa_fcs_lport_ms_sm_gfn_retry(struct bfa_fcs_lport_ms_s *ms,
					       enum port_ms_event event);
static void     bfa_fcs_lport_ms_sm_online(struct bfa_fcs_lport_ms_s *ms,
					  enum port_ms_event event);
/*
 *	Start in offline state - awaiting NS to send start.
 */
static void
bfa_fcs_lport_ms_sm_offline(struct bfa_fcs_lport_ms_s *ms,
				enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_PORT_ONLINE:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_plogi_sending);
		bfa_fcs_lport_ms_send_plogi(ms, NULL);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		break;

	default:
		bfa_sm_fault(ms->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ms_sm_plogi_sending(struct bfa_fcs_lport_ms_s *ms,
				enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_FCXP_SENT:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_plogi);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ms->port),
					   &ms->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(ms->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ms_sm_plogi(struct bfa_fcs_lport_ms_s *ms,
			enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_RSP_ERROR:
		/*
		 * Start timer for a delayed retry
		 */
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_plogi_retry);
		ms->port->stats.ms_retries++;
		bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ms->port),
				    &ms->timer, bfa_fcs_lport_ms_timeout, ms,
				    BFA_FCS_RETRY_TIMEOUT);
		break;

	case MSSM_EVENT_RSP_OK:
		/*
		 * since plogi is done, now invoke MS related sub-modules
		 */
		bfa_fcs_lport_fdmi_online(ms);

		/*
		 * if this is a Vport, go to online state.
		 */
		if (ms->port->vport) {
			bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_online);
			break;
		}

		/*
		 * For a base port we need to get the
		 * switch's IP address.
		 */
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_gmal_sending);
		bfa_fcs_lport_ms_send_gmal(ms, NULL);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_offline);
		bfa_fcxp_discard(ms->fcxp);
		break;

	default:
		bfa_sm_fault(ms->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ms_sm_plogi_retry(struct bfa_fcs_lport_ms_s *ms,
			enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_plogi_sending);
		bfa_fcs_lport_ms_send_plogi(ms, NULL);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_offline);
		bfa_timer_stop(&ms->timer);
		break;

	default:
		bfa_sm_fault(ms->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ms_sm_online(struct bfa_fcs_lport_ms_s *ms,
			enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_offline);
		break;

	case MSSM_EVENT_PORT_FABRIC_RSCN:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_gfn_sending);
		ms->retry_cnt = 0;
		bfa_fcs_lport_ms_send_gfn(ms, NULL);
		break;

	default:
		bfa_sm_fault(ms->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ms_sm_gmal_sending(struct bfa_fcs_lport_ms_s *ms,
				enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_FCXP_SENT:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_gmal);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ms->port),
					   &ms->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(ms->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ms_sm_gmal(struct bfa_fcs_lport_ms_s *ms,
				enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_RSP_ERROR:
		/*
		 * Start timer for a delayed retry
		 */
		if (ms->retry_cnt++ < BFA_FCS_MS_CMD_MAX_RETRIES) {
			bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_gmal_retry);
			ms->port->stats.ms_retries++;
			bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ms->port),
				&ms->timer, bfa_fcs_lport_ms_timeout, ms,
				BFA_FCS_RETRY_TIMEOUT);
		} else {
			bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_gfn_sending);
			bfa_fcs_lport_ms_send_gfn(ms, NULL);
			ms->retry_cnt = 0;
		}
		break;

	case MSSM_EVENT_RSP_OK:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_gfn_sending);
		bfa_fcs_lport_ms_send_gfn(ms, NULL);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_offline);
		bfa_fcxp_discard(ms->fcxp);
		break;

	default:
		bfa_sm_fault(ms->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ms_sm_gmal_retry(struct bfa_fcs_lport_ms_s *ms,
				enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_gmal_sending);
		bfa_fcs_lport_ms_send_gmal(ms, NULL);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_offline);
		bfa_timer_stop(&ms->timer);
		break;

	default:
		bfa_sm_fault(ms->port->fcs, event);
	}
}
/*
 *  ms_pvt MS local functions
 */

static void
bfa_fcs_lport_ms_send_gmal(void *ms_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_lport_ms_s *ms = ms_cbarg;
	bfa_fcs_lport_t *port = ms->port;
	struct fchs_s	fchs;
	int		len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(port->fcs, port->pid);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &ms->fcxp_wqe,
				bfa_fcs_lport_ms_send_gmal, ms, BFA_TRUE);
		return;
	}
	ms->fcxp = fcxp;

	len = fc_gmal_req_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			     bfa_fcs_lport_get_fcid(port),
				 port->fabric->lps->pr_nwwn);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			  FC_CLASS_3, len, &fchs,
			  bfa_fcs_lport_ms_gmal_response, (void *)ms,
			  FC_MAX_PDUSZ, FC_FCCT_TOV);

	bfa_sm_send_event(ms, MSSM_EVENT_FCXP_SENT);
}

static void
bfa_fcs_lport_ms_gmal_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
				void *cbarg, bfa_status_t req_status,
				u32 rsp_len, u32 resid_len,
				struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_lport_ms_s *ms = (struct bfa_fcs_lport_ms_s *) cbarg;
	bfa_fcs_lport_t *port = ms->port;
	struct ct_hdr_s		*cthdr = NULL;
	struct fcgs_gmal_resp_s *gmal_resp;
	struct fcgs_gmal_entry_s *gmal_entry;
	u32		num_entries;
	u8			*rsp_str;

	bfa_trc(port->fcs, req_status);
	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = be16_to_cpu(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		gmal_resp = (struct fcgs_gmal_resp_s *)(cthdr + 1);

		num_entries = be32_to_cpu(gmal_resp->ms_len);
		if (num_entries == 0) {
			bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
			return;
		}
		/*
		* The response could contain multiple Entries.
		* Entries for SNMP interface, etc.
		* We look for the entry with a telnet prefix.
		* First "http://" entry refers to IP addr
		*/

		gmal_entry = (struct fcgs_gmal_entry_s *)gmal_resp->ms_ma;
		while (num_entries > 0) {
			if (strncmp(gmal_entry->prefix,
				CT_GMAL_RESP_PREFIX_HTTP,
				sizeof(gmal_entry->prefix)) == 0) {

				/*
				* if the IP address is terminating with a '/',
				* remove it.
				* Byte 0 consists of the length of the string.
				*/
				rsp_str = &(gmal_entry->prefix[0]);
				if (rsp_str[gmal_entry->len-1] == '/')
					rsp_str[gmal_entry->len-1] = 0;

				/* copy IP Address to fabric */
				strncpy(bfa_fcs_lport_get_fabric_ipaddr(port),
					gmal_entry->ip_addr,
					BFA_FCS_FABRIC_IPADDR_SZ);
				break;
			} else {
				--num_entries;
				++gmal_entry;
			}
		}

		bfa_sm_send_event(ms, MSSM_EVENT_RSP_OK);
		return;
	}

	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
}

static void
bfa_fcs_lport_ms_sm_gfn_sending(struct bfa_fcs_lport_ms_s *ms,
			enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_FCXP_SENT:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_gfn);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ms->port),
					   &ms->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(ms->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ms_sm_gfn(struct bfa_fcs_lport_ms_s *ms,
			enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_RSP_ERROR:
		/*
		 * Start timer for a delayed retry
		 */
		if (ms->retry_cnt++ < BFA_FCS_MS_CMD_MAX_RETRIES) {
			bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_gfn_retry);
			ms->port->stats.ms_retries++;
			bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ms->port),
				&ms->timer, bfa_fcs_lport_ms_timeout, ms,
				BFA_FCS_RETRY_TIMEOUT);
		} else {
			bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_online);
			ms->retry_cnt = 0;
		}
		break;

	case MSSM_EVENT_RSP_OK:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_online);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_offline);
		bfa_fcxp_discard(ms->fcxp);
		break;

	default:
		bfa_sm_fault(ms->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ms_sm_gfn_retry(struct bfa_fcs_lport_ms_s *ms,
				enum port_ms_event event)
{
	bfa_trc(ms->port->fcs, ms->port->port_cfg.pwwn);
	bfa_trc(ms->port->fcs, event);

	switch (event) {
	case MSSM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_gfn_sending);
		bfa_fcs_lport_ms_send_gfn(ms, NULL);
		break;

	case MSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_offline);
		bfa_timer_stop(&ms->timer);
		break;

	default:
		bfa_sm_fault(ms->port->fcs, event);
	}
}
/*
 *  ms_pvt MS local functions
 */

static void
bfa_fcs_lport_ms_send_gfn(void *ms_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_lport_ms_s *ms = ms_cbarg;
	bfa_fcs_lport_t *port = ms->port;
	struct fchs_s		fchs;
	int			len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(port->fcs, port->pid);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &ms->fcxp_wqe,
				bfa_fcs_lport_ms_send_gfn, ms, BFA_TRUE);
		return;
	}
	ms->fcxp = fcxp;

	len = fc_gfn_req_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			     bfa_fcs_lport_get_fcid(port),
				 port->fabric->lps->pr_nwwn);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			  FC_CLASS_3, len, &fchs,
			  bfa_fcs_lport_ms_gfn_response, (void *)ms,
			  FC_MAX_PDUSZ, FC_FCCT_TOV);

	bfa_sm_send_event(ms, MSSM_EVENT_FCXP_SENT);
}

static void
bfa_fcs_lport_ms_gfn_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
			void *cbarg, bfa_status_t req_status, u32 rsp_len,
			u32 resid_len, struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_lport_ms_s *ms = (struct bfa_fcs_lport_ms_s *) cbarg;
	bfa_fcs_lport_t *port = ms->port;
	struct ct_hdr_s	*cthdr = NULL;
	wwn_t	       *gfn_resp;

	bfa_trc(port->fcs, req_status);
	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = be16_to_cpu(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		gfn_resp = (wwn_t *)(cthdr + 1);
		/* check if it has actually changed */
		if ((memcmp((void *)&bfa_fcs_lport_get_fabric_name(port),
				gfn_resp, sizeof(wwn_t)) != 0)) {
			bfa_fcs_fabric_set_fabric_name(port->fabric, *gfn_resp);
		}
		bfa_sm_send_event(ms, MSSM_EVENT_RSP_OK);
		return;
	}

	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
}

/*
 *  ms_pvt MS local functions
 */

static void
bfa_fcs_lport_ms_send_plogi(void *ms_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_lport_ms_s *ms = ms_cbarg;
	struct bfa_fcs_lport_s *port = ms->port;
	struct fchs_s	fchs;
	int	len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(port->fcs, port->pid);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		port->stats.ms_plogi_alloc_wait++;
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &ms->fcxp_wqe,
				bfa_fcs_lport_ms_send_plogi, ms, BFA_TRUE);
		return;
	}
	ms->fcxp = fcxp;

	len = fc_plogi_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			     bfa_hton3b(FC_MGMT_SERVER),
			     bfa_fcs_lport_get_fcid(port), 0,
			     port->port_cfg.pwwn, port->port_cfg.nwwn,
			     bfa_fcport_get_maxfrsize(port->fcs->bfa),
			     bfa_fcport_get_rx_bbcredit(port->fcs->bfa));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			  FC_CLASS_3, len, &fchs,
			  bfa_fcs_lport_ms_plogi_response, (void *)ms,
			  FC_MAX_PDUSZ, FC_ELS_TOV);

	port->stats.ms_plogi_sent++;
	bfa_sm_send_event(ms, MSSM_EVENT_FCXP_SENT);
}

static void
bfa_fcs_lport_ms_plogi_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
			void *cbarg, bfa_status_t req_status,
			u32 rsp_len, u32 resid_len, struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_lport_ms_s *ms = (struct bfa_fcs_lport_ms_s *) cbarg;
	struct bfa_fcs_lport_s *port = ms->port;
	struct fc_els_cmd_s *els_cmd;
	struct fc_ls_rjt_s *ls_rjt;

	bfa_trc(port->fcs, req_status);
	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		port->stats.ms_plogi_rsp_err++;
		bfa_trc(port->fcs, req_status);
		bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
		return;
	}

	els_cmd = (struct fc_els_cmd_s *) BFA_FCXP_RSP_PLD(fcxp);

	switch (els_cmd->els_code) {

	case FC_ELS_ACC:
		if (rsp_len < sizeof(struct fc_logi_s)) {
			bfa_trc(port->fcs, rsp_len);
			port->stats.ms_plogi_acc_err++;
			bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
			break;
		}
		port->stats.ms_plogi_accepts++;
		bfa_sm_send_event(ms, MSSM_EVENT_RSP_OK);
		break;

	case FC_ELS_LS_RJT:
		ls_rjt = (struct fc_ls_rjt_s *) BFA_FCXP_RSP_PLD(fcxp);

		bfa_trc(port->fcs, ls_rjt->reason_code);
		bfa_trc(port->fcs, ls_rjt->reason_code_expl);

		port->stats.ms_rejects++;
		bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
		break;

	default:
		port->stats.ms_plogi_unknown_rsp++;
		bfa_trc(port->fcs, els_cmd->els_code);
		bfa_sm_send_event(ms, MSSM_EVENT_RSP_ERROR);
	}
}

static void
bfa_fcs_lport_ms_timeout(void *arg)
{
	struct bfa_fcs_lport_ms_s *ms = (struct bfa_fcs_lport_ms_s *) arg;

	ms->port->stats.ms_timeouts++;
	bfa_sm_send_event(ms, MSSM_EVENT_TIMEOUT);
}


void
bfa_fcs_lport_ms_init(struct bfa_fcs_lport_s *port)
{
	struct bfa_fcs_lport_ms_s *ms = BFA_FCS_GET_MS_FROM_PORT(port);

	ms->port = port;
	bfa_sm_set_state(ms, bfa_fcs_lport_ms_sm_offline);

	/*
	 * Invoke init routines of sub modules.
	 */
	bfa_fcs_lport_fdmi_init(ms);
}

void
bfa_fcs_lport_ms_offline(struct bfa_fcs_lport_s *port)
{
	struct bfa_fcs_lport_ms_s *ms = BFA_FCS_GET_MS_FROM_PORT(port);

	ms->port = port;
	bfa_sm_send_event(ms, MSSM_EVENT_PORT_OFFLINE);
	bfa_fcs_lport_fdmi_offline(ms);
}

void
bfa_fcs_lport_ms_online(struct bfa_fcs_lport_s *port)
{
	struct bfa_fcs_lport_ms_s *ms = BFA_FCS_GET_MS_FROM_PORT(port);

	ms->port = port;
	bfa_sm_send_event(ms, MSSM_EVENT_PORT_ONLINE);
}
void
bfa_fcs_lport_ms_fabric_rscn(struct bfa_fcs_lport_s *port)
{
	struct bfa_fcs_lport_ms_s *ms = BFA_FCS_GET_MS_FROM_PORT(port);

	/* todo.  Handle this only  when in Online state */
	if (bfa_sm_cmp_state(ms, bfa_fcs_lport_ms_sm_online))
		bfa_sm_send_event(ms, MSSM_EVENT_PORT_FABRIC_RSCN);
}

/*
 * @page ns_sm_info VPORT NS State Machine
 *
 * @section ns_sm_interactions VPORT NS State Machine Interactions
 *
 * @section ns_sm VPORT NS State Machine
 * img ns_sm.jpg
 */

/*
 * forward declarations
 */
static void     bfa_fcs_lport_ns_send_plogi(void *ns_cbarg,
					   struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_lport_ns_send_rspn_id(void *ns_cbarg,
					     struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_lport_ns_send_rft_id(void *ns_cbarg,
					    struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_lport_ns_send_rff_id(void *ns_cbarg,
					    struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_lport_ns_send_gid_ft(void *ns_cbarg,
					    struct bfa_fcxp_s *fcxp_alloced);
static void	bfa_fcs_lport_ns_send_rnn_id(void *ns_cbarg,
					struct bfa_fcxp_s *fcxp_alloced);
static void	bfa_fcs_lport_ns_send_rsnn_nn(void *ns_cbarg,
					struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_lport_ns_timeout(void *arg);
static void     bfa_fcs_lport_ns_plogi_response(void *fcsarg,
					       struct bfa_fcxp_s *fcxp,
					       void *cbarg,
					       bfa_status_t req_status,
					       u32 rsp_len,
					       u32 resid_len,
					       struct fchs_s *rsp_fchs);
static void     bfa_fcs_lport_ns_rspn_id_response(void *fcsarg,
						 struct bfa_fcxp_s *fcxp,
						 void *cbarg,
						 bfa_status_t req_status,
						 u32 rsp_len,
						 u32 resid_len,
						 struct fchs_s *rsp_fchs);
static void     bfa_fcs_lport_ns_rft_id_response(void *fcsarg,
						struct bfa_fcxp_s *fcxp,
						void *cbarg,
						bfa_status_t req_status,
						u32 rsp_len,
						u32 resid_len,
						struct fchs_s *rsp_fchs);
static void     bfa_fcs_lport_ns_rff_id_response(void *fcsarg,
						struct bfa_fcxp_s *fcxp,
						void *cbarg,
						bfa_status_t req_status,
						u32 rsp_len,
						u32 resid_len,
						struct fchs_s *rsp_fchs);
static void     bfa_fcs_lport_ns_gid_ft_response(void *fcsarg,
						struct bfa_fcxp_s *fcxp,
						void *cbarg,
						bfa_status_t req_status,
						u32 rsp_len,
						u32 resid_len,
						struct fchs_s *rsp_fchs);
static void     bfa_fcs_lport_ns_rnn_id_response(void *fcsarg,
						struct bfa_fcxp_s *fcxp,
						void *cbarg,
						bfa_status_t req_status,
						u32 rsp_len,
						u32 resid_len,
						struct fchs_s *rsp_fchs);
static void     bfa_fcs_lport_ns_rsnn_nn_response(void *fcsarg,
						struct bfa_fcxp_s *fcxp,
						void *cbarg,
						bfa_status_t req_status,
						u32 rsp_len,
						u32 resid_len,
						struct fchs_s *rsp_fchs);
static void     bfa_fcs_lport_ns_process_gidft_pids(
				struct bfa_fcs_lport_s *port,
				u32 *pid_buf, u32 n_pids);

static void bfa_fcs_lport_ns_boot_target_disc(bfa_fcs_lport_t *port);
/*
 *  fcs_ns_sm FCS nameserver interface state machine
 */

/*
 * VPort NS State Machine events
 */
enum vport_ns_event {
	NSSM_EVENT_PORT_ONLINE = 1,
	NSSM_EVENT_PORT_OFFLINE = 2,
	NSSM_EVENT_PLOGI_SENT = 3,
	NSSM_EVENT_RSP_OK = 4,
	NSSM_EVENT_RSP_ERROR = 5,
	NSSM_EVENT_TIMEOUT = 6,
	NSSM_EVENT_NS_QUERY = 7,
	NSSM_EVENT_RSPNID_SENT = 8,
	NSSM_EVENT_RFTID_SENT = 9,
	NSSM_EVENT_RFFID_SENT = 10,
	NSSM_EVENT_GIDFT_SENT = 11,
	NSSM_EVENT_RNNID_SENT = 12,
	NSSM_EVENT_RSNN_NN_SENT = 13,
};

static void     bfa_fcs_lport_ns_sm_offline(struct bfa_fcs_lport_ns_s *ns,
					   enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_plogi_sending(struct bfa_fcs_lport_ns_s *ns,
						 enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_plogi(struct bfa_fcs_lport_ns_s *ns,
					 enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_plogi_retry(struct bfa_fcs_lport_ns_s *ns,
					       enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_sending_rspn_id(
					struct bfa_fcs_lport_ns_s *ns,
					enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_rspn_id(struct bfa_fcs_lport_ns_s *ns,
					   enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_rspn_id_retry(struct bfa_fcs_lport_ns_s *ns,
						 enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_sending_rft_id(
					struct bfa_fcs_lport_ns_s *ns,
					enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_rft_id_retry(struct bfa_fcs_lport_ns_s *ns,
						enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_rft_id(struct bfa_fcs_lport_ns_s *ns,
					  enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_sending_rff_id(
					struct bfa_fcs_lport_ns_s *ns,
					enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_rff_id_retry(struct bfa_fcs_lport_ns_s *ns,
						enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_rff_id(struct bfa_fcs_lport_ns_s *ns,
					  enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_sending_gid_ft(
					struct bfa_fcs_lport_ns_s *ns,
					enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_gid_ft(struct bfa_fcs_lport_ns_s *ns,
					  enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_gid_ft_retry(struct bfa_fcs_lport_ns_s *ns,
						enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_online(struct bfa_fcs_lport_ns_s *ns,
					  enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_sending_rnn_id(
					struct bfa_fcs_lport_ns_s *ns,
					enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_rnn_id(struct bfa_fcs_lport_ns_s *ns,
					enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_rnn_id_retry(struct bfa_fcs_lport_ns_s *ns,
						enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_sending_rsnn_nn(
					struct bfa_fcs_lport_ns_s *ns,
					enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_rsnn_nn(struct bfa_fcs_lport_ns_s *ns,
						enum vport_ns_event event);
static void     bfa_fcs_lport_ns_sm_rsnn_nn_retry(
					struct bfa_fcs_lport_ns_s *ns,
					enum vport_ns_event event);
/*
 *	Start in offline state - awaiting linkup
 */
static void
bfa_fcs_lport_ns_sm_offline(struct bfa_fcs_lport_ns_s *ns,
			enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_PORT_ONLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_plogi_sending);
		bfa_fcs_lport_ns_send_plogi(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_plogi_sending(struct bfa_fcs_lport_ns_s *ns,
			enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_PLOGI_SENT:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_plogi);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
					   &ns->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_plogi(struct bfa_fcs_lport_ns_s *ns,
			enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RSP_ERROR:
		/*
		 * Start timer for a delayed retry
		 */
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_plogi_retry);
		ns->port->stats.ns_retries++;
		bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
				    &ns->timer, bfa_fcs_lport_ns_timeout, ns,
				    BFA_FCS_RETRY_TIMEOUT);
		break;

	case NSSM_EVENT_RSP_OK:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_sending_rnn_id);
		ns->num_rnnid_retries = 0;
		bfa_fcs_lport_ns_send_rnn_id(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_fcxp_discard(ns->fcxp);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_plogi_retry(struct bfa_fcs_lport_ns_s *ns,
				enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_plogi_sending);
		bfa_fcs_lport_ns_send_plogi(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_timer_stop(&ns->timer);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_sending_rnn_id(struct bfa_fcs_lport_ns_s *ns,
					enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RNNID_SENT:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_rnn_id);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
						&ns->fcxp_wqe);
		break;
	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_rnn_id(struct bfa_fcs_lport_ns_s *ns,
				enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RSP_OK:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_sending_rsnn_nn);
		ns->num_rnnid_retries = 0;
		ns->num_rsnn_nn_retries = 0;
		bfa_fcs_lport_ns_send_rsnn_nn(ns, NULL);
		break;

	case NSSM_EVENT_RSP_ERROR:
		if (ns->num_rnnid_retries < BFA_FCS_MAX_NS_RETRIES) {
			bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_rnn_id_retry);
			ns->port->stats.ns_retries++;
			ns->num_rnnid_retries++;
			bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
				&ns->timer, bfa_fcs_lport_ns_timeout, ns,
				BFA_FCS_RETRY_TIMEOUT);
		} else {
			bfa_sm_set_state(ns,
				bfa_fcs_lport_ns_sm_sending_rspn_id);
			bfa_fcs_lport_ns_send_rspn_id(ns, NULL);
		}
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_fcxp_discard(ns->fcxp);
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_rnn_id_retry(struct bfa_fcs_lport_ns_s *ns,
				enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_TIMEOUT:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_sending_rnn_id);
		bfa_fcs_lport_ns_send_rnn_id(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_timer_stop(&ns->timer);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_sending_rsnn_nn(struct bfa_fcs_lport_ns_s *ns,
					enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RSNN_NN_SENT:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_rsnn_nn);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
			&ns->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_rsnn_nn(struct bfa_fcs_lport_ns_s *ns,
				enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RSP_OK:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_sending_rspn_id);
		ns->num_rsnn_nn_retries = 0;
		bfa_fcs_lport_ns_send_rspn_id(ns, NULL);
		break;

	case NSSM_EVENT_RSP_ERROR:
		if (ns->num_rsnn_nn_retries < BFA_FCS_MAX_NS_RETRIES) {
			bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_rsnn_nn_retry);
			ns->port->stats.ns_retries++;
			ns->num_rsnn_nn_retries++;
			bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
					&ns->timer, bfa_fcs_lport_ns_timeout,
					ns, BFA_FCS_RETRY_TIMEOUT);
		} else {
			bfa_sm_set_state(ns,
				bfa_fcs_lport_ns_sm_sending_rspn_id);
			bfa_fcs_lport_ns_send_rspn_id(ns, NULL);
		}
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_fcxp_discard(ns->fcxp);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_rsnn_nn_retry(struct bfa_fcs_lport_ns_s *ns,
					enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_TIMEOUT:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_sending_rsnn_nn);
		bfa_fcs_lport_ns_send_rsnn_nn(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_timer_stop(&ns->timer);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_sending_rspn_id(struct bfa_fcs_lport_ns_s *ns,
				   enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RSPNID_SENT:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_rspn_id);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
					   &ns->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_rspn_id(struct bfa_fcs_lport_ns_s *ns,
			enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RSP_ERROR:
		/*
		 * Start timer for a delayed retry
		 */
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_rspn_id_retry);
		ns->port->stats.ns_retries++;
		bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
				    &ns->timer, bfa_fcs_lport_ns_timeout, ns,
				    BFA_FCS_RETRY_TIMEOUT);
		break;

	case NSSM_EVENT_RSP_OK:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_sending_rft_id);
		bfa_fcs_lport_ns_send_rft_id(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_fcxp_discard(ns->fcxp);
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_rspn_id_retry(struct bfa_fcs_lport_ns_s *ns,
				enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_TIMEOUT:
		/*
		 * Retry Timer Expired. Re-send
		 */
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_sending_rspn_id);
		bfa_fcs_lport_ns_send_rspn_id(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_timer_stop(&ns->timer);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_sending_rft_id(struct bfa_fcs_lport_ns_s *ns,
				  enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RFTID_SENT:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_rft_id);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
					   &ns->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_rft_id(struct bfa_fcs_lport_ns_s *ns,
			enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RSP_OK:
		/* Now move to register FC4 Features */
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_sending_rff_id);
		bfa_fcs_lport_ns_send_rff_id(ns, NULL);
		break;

	case NSSM_EVENT_RSP_ERROR:
		/*
		 * Start timer for a delayed retry
		 */
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_rft_id_retry);
		ns->port->stats.ns_retries++;
		bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
				    &ns->timer, bfa_fcs_lport_ns_timeout, ns,
				    BFA_FCS_RETRY_TIMEOUT);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_fcxp_discard(ns->fcxp);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_rft_id_retry(struct bfa_fcs_lport_ns_s *ns,
				enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_TIMEOUT:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_sending_rft_id);
		bfa_fcs_lport_ns_send_rft_id(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_timer_stop(&ns->timer);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_sending_rff_id(struct bfa_fcs_lport_ns_s *ns,
				  enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RFFID_SENT:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_rff_id);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
					   &ns->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_rff_id(struct bfa_fcs_lport_ns_s *ns,
			enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RSP_OK:

		/*
		 * If min cfg mode is enabled, we donot initiate rport
		 * discovery with the fabric. Instead, we will retrieve the
		 * boot targets from HAL/FW.
		 */
		if (__fcs_min_cfg(ns->port->fcs)) {
			bfa_fcs_lport_ns_boot_target_disc(ns->port);
			bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_online);
			return;
		}

		/*
		 * If the port role is Initiator Mode issue NS query.
		 * If it is Target Mode, skip this and go to online.
		 */
		if (BFA_FCS_VPORT_IS_INITIATOR_MODE(ns->port)) {
			bfa_sm_set_state(ns,
				bfa_fcs_lport_ns_sm_sending_gid_ft);
			bfa_fcs_lport_ns_send_gid_ft(ns, NULL);
		}
		/*
		 * kick off mgmt srvr state machine
		 */
		bfa_fcs_lport_ms_online(ns->port);
		break;

	case NSSM_EVENT_RSP_ERROR:
		/*
		 * Start timer for a delayed retry
		 */
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_rff_id_retry);
		ns->port->stats.ns_retries++;
		bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
				    &ns->timer, bfa_fcs_lport_ns_timeout, ns,
				    BFA_FCS_RETRY_TIMEOUT);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_fcxp_discard(ns->fcxp);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_rff_id_retry(struct bfa_fcs_lport_ns_s *ns,
				enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_TIMEOUT:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_sending_rff_id);
		bfa_fcs_lport_ns_send_rff_id(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_timer_stop(&ns->timer);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}
static void
bfa_fcs_lport_ns_sm_sending_gid_ft(struct bfa_fcs_lport_ns_s *ns,
				  enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_GIDFT_SENT:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_gid_ft);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_fcxp_walloc_cancel(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
					   &ns->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_gid_ft(struct bfa_fcs_lport_ns_s *ns,
			enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_RSP_OK:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_online);
		break;

	case NSSM_EVENT_RSP_ERROR:
		/*
		 * TBD: for certain reject codes, we don't need to retry
		 */
		/*
		 * Start timer for a delayed retry
		 */
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_gid_ft_retry);
		ns->port->stats.ns_retries++;
		bfa_timer_start(BFA_FCS_GET_HAL_FROM_PORT(ns->port),
				    &ns->timer, bfa_fcs_lport_ns_timeout, ns,
				    BFA_FCS_RETRY_TIMEOUT);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_fcxp_discard(ns->fcxp);
		break;

	case  NSSM_EVENT_NS_QUERY:
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_gid_ft_retry(struct bfa_fcs_lport_ns_s *ns,
				enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_TIMEOUT:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_sending_gid_ft);
		bfa_fcs_lport_ns_send_gid_ft(ns, NULL);
		break;

	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		bfa_timer_stop(&ns->timer);
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}

static void
bfa_fcs_lport_ns_sm_online(struct bfa_fcs_lport_ns_s *ns,
			enum vport_ns_event event)
{
	bfa_trc(ns->port->fcs, ns->port->port_cfg.pwwn);
	bfa_trc(ns->port->fcs, event);

	switch (event) {
	case NSSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
		break;

	case NSSM_EVENT_NS_QUERY:
		/*
		 * If the port role is Initiator Mode issue NS query.
		 * If it is Target Mode, skip this and go to online.
		 */
		if (BFA_FCS_VPORT_IS_INITIATOR_MODE(ns->port)) {
			bfa_sm_set_state(ns,
				bfa_fcs_lport_ns_sm_sending_gid_ft);
			bfa_fcs_lport_ns_send_gid_ft(ns, NULL);
		};
		break;

	default:
		bfa_sm_fault(ns->port->fcs, event);
	}
}



/*
 *  ns_pvt Nameserver local functions
 */

static void
bfa_fcs_lport_ns_send_plogi(void *ns_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_lport_ns_s *ns = ns_cbarg;
	struct bfa_fcs_lport_s *port = ns->port;
	struct fchs_s fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(port->fcs, port->pid);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		port->stats.ns_plogi_alloc_wait++;
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &ns->fcxp_wqe,
				bfa_fcs_lport_ns_send_plogi, ns, BFA_TRUE);
		return;
	}
	ns->fcxp = fcxp;

	len = fc_plogi_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			     bfa_hton3b(FC_NAME_SERVER),
			     bfa_fcs_lport_get_fcid(port), 0,
			     port->port_cfg.pwwn, port->port_cfg.nwwn,
			     bfa_fcport_get_maxfrsize(port->fcs->bfa),
			     bfa_fcport_get_rx_bbcredit(port->fcs->bfa));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			  FC_CLASS_3, len, &fchs,
			  bfa_fcs_lport_ns_plogi_response, (void *)ns,
			  FC_MAX_PDUSZ, FC_ELS_TOV);
	port->stats.ns_plogi_sent++;

	bfa_sm_send_event(ns, NSSM_EVENT_PLOGI_SENT);
}

static void
bfa_fcs_lport_ns_plogi_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
			void *cbarg, bfa_status_t req_status, u32 rsp_len,
		       u32 resid_len, struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_lport_ns_s *ns = (struct bfa_fcs_lport_ns_s *) cbarg;
	struct bfa_fcs_lport_s *port = ns->port;
	/* struct fc_logi_s *plogi_resp; */
	struct fc_els_cmd_s *els_cmd;
	struct fc_ls_rjt_s *ls_rjt;

	bfa_trc(port->fcs, req_status);
	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		port->stats.ns_plogi_rsp_err++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		return;
	}

	els_cmd = (struct fc_els_cmd_s *) BFA_FCXP_RSP_PLD(fcxp);

	switch (els_cmd->els_code) {

	case FC_ELS_ACC:
		if (rsp_len < sizeof(struct fc_logi_s)) {
			bfa_trc(port->fcs, rsp_len);
			port->stats.ns_plogi_acc_err++;
			bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
			break;
		}
		port->stats.ns_plogi_accepts++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
		break;

	case FC_ELS_LS_RJT:
		ls_rjt = (struct fc_ls_rjt_s *) BFA_FCXP_RSP_PLD(fcxp);

		bfa_trc(port->fcs, ls_rjt->reason_code);
		bfa_trc(port->fcs, ls_rjt->reason_code_expl);

		port->stats.ns_rejects++;

		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		break;

	default:
		port->stats.ns_plogi_unknown_rsp++;
		bfa_trc(port->fcs, els_cmd->els_code);
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
	}
}

/*
 * Register node name for port_id
 */
static void
bfa_fcs_lport_ns_send_rnn_id(void *ns_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_lport_ns_s *ns = ns_cbarg;
	struct bfa_fcs_lport_s *port = ns->port;
	struct fchs_s  fchs;
	int	len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced :
			bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		port->stats.ns_rnnid_alloc_wait++;
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &ns->fcxp_wqe,
				bfa_fcs_lport_ns_send_rnn_id, ns, BFA_TRUE);
		return;
	}

	ns->fcxp = fcxp;

	len = fc_rnnid_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				bfa_fcs_lport_get_fcid(port),
				bfa_fcs_lport_get_fcid(port),
				bfa_fcs_lport_get_nwwn(port));

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			  FC_CLASS_3, len, &fchs,
			  bfa_fcs_lport_ns_rnn_id_response, (void *)ns,
			  FC_MAX_PDUSZ, FC_FCCT_TOV);

	port->stats.ns_rnnid_sent++;
	bfa_sm_send_event(ns, NSSM_EVENT_RNNID_SENT);
}

static void
bfa_fcs_lport_ns_rnn_id_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
				void *cbarg, bfa_status_t req_status,
				u32 rsp_len, u32 resid_len,
				struct fchs_s *rsp_fchs)

{
	struct bfa_fcs_lport_ns_s *ns = (struct bfa_fcs_lport_ns_s *) cbarg;
	struct bfa_fcs_lport_s *port = ns->port;
	struct ct_hdr_s	*cthdr = NULL;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		port->stats.ns_rnnid_rsp_err++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = be16_to_cpu(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		port->stats.ns_rnnid_accepts++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
		return;
	}

	port->stats.ns_rnnid_rejects++;
	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
}

/*
 * Register the symbolic node name for a given node name.
 */
static void
bfa_fcs_lport_ns_send_rsnn_nn(void *ns_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_lport_ns_s *ns = ns_cbarg;
	struct bfa_fcs_lport_s *port = ns->port;
	struct fchs_s  fchs;
	int     len;
	struct bfa_fcxp_s *fcxp;
	u8 *nsymbl;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced :
			bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		port->stats.ns_rsnn_nn_alloc_wait++;
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &ns->fcxp_wqe,
				bfa_fcs_lport_ns_send_rsnn_nn, ns, BFA_TRUE);
		return;
	}
	ns->fcxp = fcxp;

	nsymbl = (u8 *) &(bfa_fcs_lport_get_nsym_name(
					bfa_fcs_get_base_port(port->fcs)));

	len = fc_rsnn_nn_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				bfa_fcs_lport_get_fcid(port),
				bfa_fcs_lport_get_nwwn(port), nsymbl);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			  FC_CLASS_3, len, &fchs,
			  bfa_fcs_lport_ns_rsnn_nn_response, (void *)ns,
			  FC_MAX_PDUSZ, FC_FCCT_TOV);

	port->stats.ns_rsnn_nn_sent++;

	bfa_sm_send_event(ns, NSSM_EVENT_RSNN_NN_SENT);
}

static void
bfa_fcs_lport_ns_rsnn_nn_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
				void *cbarg, bfa_status_t req_status,
				u32 rsp_len, u32 resid_len,
				struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_lport_ns_s *ns = (struct bfa_fcs_lport_ns_s *) cbarg;
	struct bfa_fcs_lport_s *port = ns->port;
	struct ct_hdr_s	*cthdr = NULL;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		port->stats.ns_rsnn_nn_rsp_err++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = be16_to_cpu(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		port->stats.ns_rsnn_nn_accepts++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
		return;
	}

	port->stats.ns_rsnn_nn_rejects++;
	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
}

/*
 * Register the symbolic port name.
 */
static void
bfa_fcs_lport_ns_send_rspn_id(void *ns_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_lport_ns_s *ns = ns_cbarg;
	struct bfa_fcs_lport_s *port = ns->port;
	struct fchs_s fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;
	u8         symbl[256];
	u8         *psymbl = &symbl[0];

	memset(symbl, 0, sizeof(symbl));

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		port->stats.ns_rspnid_alloc_wait++;
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &ns->fcxp_wqe,
				bfa_fcs_lport_ns_send_rspn_id, ns, BFA_TRUE);
		return;
	}
	ns->fcxp = fcxp;

	/*
	 * for V-Port, form a Port Symbolic Name
	 */
	if (port->vport) {
		/*
		 * For Vports, we append the vport's port symbolic name
		 * to that of the base port.
		 */

		strncpy((char *)psymbl,
			(char *) &
			(bfa_fcs_lport_get_psym_name
			 (bfa_fcs_get_base_port(port->fcs))),
			strlen((char *) &
			       bfa_fcs_lport_get_psym_name(bfa_fcs_get_base_port
							  (port->fcs))));

		/* Ensure we have a null terminating string. */
		((char *)psymbl)[strlen((char *) &
			bfa_fcs_lport_get_psym_name(bfa_fcs_get_base_port
						(port->fcs)))] = 0;
		strncat((char *)psymbl,
			(char *) &(bfa_fcs_lport_get_psym_name(port)),
		strlen((char *) &bfa_fcs_lport_get_psym_name(port)));
	} else {
		psymbl = (u8 *) &(bfa_fcs_lport_get_psym_name(port));
	}

	len = fc_rspnid_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			      bfa_fcs_lport_get_fcid(port), 0, psymbl);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			  FC_CLASS_3, len, &fchs,
			  bfa_fcs_lport_ns_rspn_id_response, (void *)ns,
			  FC_MAX_PDUSZ, FC_FCCT_TOV);

	port->stats.ns_rspnid_sent++;

	bfa_sm_send_event(ns, NSSM_EVENT_RSPNID_SENT);
}

static void
bfa_fcs_lport_ns_rspn_id_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
				 void *cbarg, bfa_status_t req_status,
				 u32 rsp_len, u32 resid_len,
				 struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_lport_ns_s *ns = (struct bfa_fcs_lport_ns_s *) cbarg;
	struct bfa_fcs_lport_s *port = ns->port;
	struct ct_hdr_s *cthdr = NULL;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		port->stats.ns_rspnid_rsp_err++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = be16_to_cpu(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		port->stats.ns_rspnid_accepts++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
		return;
	}

	port->stats.ns_rspnid_rejects++;
	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
}

/*
 * Register FC4-Types
 */
static void
bfa_fcs_lport_ns_send_rft_id(void *ns_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_lport_ns_s *ns = ns_cbarg;
	struct bfa_fcs_lport_s *port = ns->port;
	struct fchs_s fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		port->stats.ns_rftid_alloc_wait++;
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &ns->fcxp_wqe,
				bfa_fcs_lport_ns_send_rft_id, ns, BFA_TRUE);
		return;
	}
	ns->fcxp = fcxp;

	len = fc_rftid_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
		     bfa_fcs_lport_get_fcid(port), 0, port->port_cfg.roles);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			  FC_CLASS_3, len, &fchs,
			  bfa_fcs_lport_ns_rft_id_response, (void *)ns,
			  FC_MAX_PDUSZ, FC_FCCT_TOV);

	port->stats.ns_rftid_sent++;
	bfa_sm_send_event(ns, NSSM_EVENT_RFTID_SENT);
}

static void
bfa_fcs_lport_ns_rft_id_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
				void *cbarg, bfa_status_t req_status,
				u32 rsp_len, u32 resid_len,
				struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_lport_ns_s *ns = (struct bfa_fcs_lport_ns_s *) cbarg;
	struct bfa_fcs_lport_s *port = ns->port;
	struct ct_hdr_s *cthdr = NULL;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		port->stats.ns_rftid_rsp_err++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = be16_to_cpu(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		port->stats.ns_rftid_accepts++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
		return;
	}

	port->stats.ns_rftid_rejects++;
	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);
	bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
}

/*
 * Register FC4-Features : Should be done after RFT_ID
 */
static void
bfa_fcs_lport_ns_send_rff_id(void *ns_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_lport_ns_s *ns = ns_cbarg;
	struct bfa_fcs_lport_s *port = ns->port;
	struct fchs_s fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;
	u8			fc4_ftrs = 0;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		port->stats.ns_rffid_alloc_wait++;
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &ns->fcxp_wqe,
				bfa_fcs_lport_ns_send_rff_id, ns, BFA_TRUE);
		return;
	}
	ns->fcxp = fcxp;

	if (BFA_FCS_VPORT_IS_INITIATOR_MODE(ns->port))
		fc4_ftrs = FC_GS_FCP_FC4_FEATURE_INITIATOR;

	len = fc_rffid_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			     bfa_fcs_lport_get_fcid(port), 0,
				 FC_TYPE_FCP, fc4_ftrs);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			  FC_CLASS_3, len, &fchs,
			  bfa_fcs_lport_ns_rff_id_response, (void *)ns,
			  FC_MAX_PDUSZ, FC_FCCT_TOV);

	port->stats.ns_rffid_sent++;
	bfa_sm_send_event(ns, NSSM_EVENT_RFFID_SENT);
}

static void
bfa_fcs_lport_ns_rff_id_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
				void *cbarg, bfa_status_t req_status,
				u32 rsp_len, u32 resid_len,
				struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_lport_ns_s *ns = (struct bfa_fcs_lport_ns_s *) cbarg;
	struct bfa_fcs_lport_s *port = ns->port;
	struct ct_hdr_s *cthdr = NULL;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		port->stats.ns_rffid_rsp_err++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = be16_to_cpu(cthdr->cmd_rsp_code);

	if (cthdr->cmd_rsp_code == CT_RSP_ACCEPT) {
		port->stats.ns_rffid_accepts++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
		return;
	}

	port->stats.ns_rffid_rejects++;
	bfa_trc(port->fcs, cthdr->reason_code);
	bfa_trc(port->fcs, cthdr->exp_code);

	if (cthdr->reason_code == CT_RSN_NOT_SUPP) {
		/* if this command is not supported, we don't retry */
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
	} else
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
}
/*
 * Query Fabric for FC4-Types Devices.
 *
* TBD : Need to use a local (FCS private) response buffer, since the response
 * can be larger than 2K.
 */
static void
bfa_fcs_lport_ns_send_gid_ft(void *ns_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_lport_ns_s *ns = ns_cbarg;
	struct bfa_fcs_lport_s *port = ns->port;
	struct fchs_s fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(port->fcs, port->pid);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		port->stats.ns_gidft_alloc_wait++;
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &ns->fcxp_wqe,
				bfa_fcs_lport_ns_send_gid_ft, ns, BFA_TRUE);
		return;
	}
	ns->fcxp = fcxp;

	/*
	 * This query is only initiated for FCP initiator mode.
	 */
	len = fc_gid_ft_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			      ns->port->pid, FC_TYPE_FCP);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			  FC_CLASS_3, len, &fchs,
			  bfa_fcs_lport_ns_gid_ft_response, (void *)ns,
			  bfa_fcxp_get_maxrsp(port->fcs->bfa), FC_FCCT_TOV);

	port->stats.ns_gidft_sent++;

	bfa_sm_send_event(ns, NSSM_EVENT_GIDFT_SENT);
}

static void
bfa_fcs_lport_ns_gid_ft_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
				void *cbarg, bfa_status_t req_status,
				u32 rsp_len, u32 resid_len,
				struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_lport_ns_s *ns = (struct bfa_fcs_lport_ns_s *) cbarg;
	struct bfa_fcs_lport_s *port = ns->port;
	struct ct_hdr_s *cthdr = NULL;
	u32        n_pids;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		port->stats.ns_gidft_rsp_err++;
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		return;
	}

	if (resid_len != 0) {
		/*
		 * TBD : we will need to allocate a larger buffer & retry the
		 * command
		 */
		bfa_trc(port->fcs, rsp_len);
		bfa_trc(port->fcs, resid_len);
		return;
	}

	cthdr = (struct ct_hdr_s *) BFA_FCXP_RSP_PLD(fcxp);
	cthdr->cmd_rsp_code = be16_to_cpu(cthdr->cmd_rsp_code);

	switch (cthdr->cmd_rsp_code) {

	case CT_RSP_ACCEPT:

		port->stats.ns_gidft_accepts++;
		n_pids = (fc_get_ctresp_pyld_len(rsp_len) / sizeof(u32));
		bfa_trc(port->fcs, n_pids);
		bfa_fcs_lport_ns_process_gidft_pids(port,
						   (u32 *) (cthdr + 1),
						   n_pids);
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
		break;

	case CT_RSP_REJECT:

		/*
		 * Check the reason code  & explanation.
		 * There may not have been any FC4 devices in the fabric
		 */
		port->stats.ns_gidft_rejects++;
		bfa_trc(port->fcs, cthdr->reason_code);
		bfa_trc(port->fcs, cthdr->exp_code);

		if ((cthdr->reason_code == CT_RSN_UNABLE_TO_PERF)
		    && (cthdr->exp_code == CT_NS_EXP_FT_NOT_REG)) {

			bfa_sm_send_event(ns, NSSM_EVENT_RSP_OK);
		} else {
			/*
			 * for all other errors, retry
			 */
			bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
		}
		break;

	default:
		port->stats.ns_gidft_unknown_rsp++;
		bfa_trc(port->fcs, cthdr->cmd_rsp_code);
		bfa_sm_send_event(ns, NSSM_EVENT_RSP_ERROR);
	}
}

/*
 *     This routine will be called by bfa_timer on timer timeouts.
 *
 *	param[in]	port - pointer to bfa_fcs_lport_t.
 *
 *	return
 *		void
 *
 *	Special Considerations:
 *
 *	note
 */
static void
bfa_fcs_lport_ns_timeout(void *arg)
{
	struct bfa_fcs_lport_ns_s *ns = (struct bfa_fcs_lport_ns_s *) arg;

	ns->port->stats.ns_timeouts++;
	bfa_sm_send_event(ns, NSSM_EVENT_TIMEOUT);
}

/*
 * Process the PID list in GID_FT response
 */
static void
bfa_fcs_lport_ns_process_gidft_pids(struct bfa_fcs_lport_s *port, u32 *pid_buf,
				   u32 n_pids)
{
	struct fcgs_gidft_resp_s *gidft_entry;
	struct bfa_fcs_rport_s *rport;
	u32        ii;
	struct bfa_fcs_fabric_s *fabric = port->fabric;
	struct bfa_fcs_vport_s *vport;
	struct list_head *qe;
	u8 found = 0;

	for (ii = 0; ii < n_pids; ii++) {
		gidft_entry = (struct fcgs_gidft_resp_s *) &pid_buf[ii];

		if (gidft_entry->pid == port->pid)
			continue;

		/*
		 * Ignore PID if it is of base port
		 * (Avoid vports discovering base port as remote port)
		 */
		if (gidft_entry->pid == fabric->bport.pid)
			continue;

		/*
		 * Ignore PID if it is of vport created on the same base port
		 * (Avoid vport discovering every other vport created on the
		 * same port as remote port)
		 */
		list_for_each(qe, &fabric->vport_q) {
			vport = (struct bfa_fcs_vport_s *) qe;
			if (vport->lport.pid == gidft_entry->pid)
				found = 1;
		}

		if (found) {
			found = 0;
			continue;
		}

		/*
		 * Check if this rport already exists
		 */
		rport = bfa_fcs_lport_get_rport_by_pid(port, gidft_entry->pid);
		if (rport == NULL) {
			/*
			 * this is a new device. create rport
			 */
			rport = bfa_fcs_rport_create(port, gidft_entry->pid);
		} else {
			/*
			 * this rport already exists
			 */
			bfa_fcs_rport_scn(rport);
		}

		bfa_trc(port->fcs, gidft_entry->pid);

		/*
		 * if the last entry bit is set, bail out.
		 */
		if (gidft_entry->last)
			return;
	}
}

/*
 *  fcs_ns_public FCS nameserver public interfaces
 */

/*
 * Functions called by port/fab.
 * These will send relevant Events to the ns state machine.
 */
void
bfa_fcs_lport_ns_init(struct bfa_fcs_lport_s *port)
{
	struct bfa_fcs_lport_ns_s *ns = BFA_FCS_GET_NS_FROM_PORT(port);

	ns->port = port;
	bfa_sm_set_state(ns, bfa_fcs_lport_ns_sm_offline);
}

void
bfa_fcs_lport_ns_offline(struct bfa_fcs_lport_s *port)
{
	struct bfa_fcs_lport_ns_s *ns = BFA_FCS_GET_NS_FROM_PORT(port);

	ns->port = port;
	bfa_sm_send_event(ns, NSSM_EVENT_PORT_OFFLINE);
}

void
bfa_fcs_lport_ns_online(struct bfa_fcs_lport_s *port)
{
	struct bfa_fcs_lport_ns_s *ns = BFA_FCS_GET_NS_FROM_PORT(port);

	ns->port = port;
	bfa_sm_send_event(ns, NSSM_EVENT_PORT_ONLINE);
}

void
bfa_fcs_lport_ns_query(struct bfa_fcs_lport_s *port)
{
	struct bfa_fcs_lport_ns_s *ns = BFA_FCS_GET_NS_FROM_PORT(port);

	bfa_trc(port->fcs, port->pid);
	if (bfa_sm_cmp_state(ns, bfa_fcs_lport_ns_sm_online))
		bfa_sm_send_event(ns, NSSM_EVENT_NS_QUERY);
}

static void
bfa_fcs_lport_ns_boot_target_disc(bfa_fcs_lport_t *port)
{

	struct bfa_fcs_rport_s *rport;
	u8 nwwns;
	wwn_t  wwns[BFA_PREBOOT_BOOTLUN_MAX];
	int ii;

	bfa_iocfc_get_bootwwns(port->fcs->bfa, &nwwns, wwns);

	for (ii = 0 ; ii < nwwns; ++ii) {
		rport = bfa_fcs_rport_create_by_wwn(port, wwns[ii]);
		WARN_ON(!rport);
	}
}

void
bfa_fcs_lport_ns_util_send_rspn_id(void *cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_lport_ns_s *ns = cbarg;
	struct bfa_fcs_lport_s *port = ns->port;
	struct fchs_s fchs;
	struct bfa_fcxp_s *fcxp;
	u8 symbl[256];
	u8 *psymbl = &symbl[0];
	int len;

	/* Avoid sending RSPN in the following states. */
	if (bfa_sm_cmp_state(ns, bfa_fcs_lport_ns_sm_offline) ||
	    bfa_sm_cmp_state(ns, bfa_fcs_lport_ns_sm_plogi_sending) ||
	    bfa_sm_cmp_state(ns, bfa_fcs_lport_ns_sm_plogi) ||
	    bfa_sm_cmp_state(ns, bfa_fcs_lport_ns_sm_plogi_retry) ||
	    bfa_sm_cmp_state(ns, bfa_fcs_lport_ns_sm_rspn_id_retry))
		return;

	memset(symbl, 0, sizeof(symbl));
	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_FALSE);
	if (!fcxp) {
		port->stats.ns_rspnid_alloc_wait++;
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &ns->fcxp_wqe,
			bfa_fcs_lport_ns_util_send_rspn_id, ns, BFA_FALSE);
		return;
	}

	ns->fcxp = fcxp;

	if (port->vport) {
		/*
		 * For Vports, we append the vport's port symbolic name
		 * to that of the base port.
		 */
		strncpy((char *)psymbl, (char *)&(bfa_fcs_lport_get_psym_name
			(bfa_fcs_get_base_port(port->fcs))),
			strlen((char *)&bfa_fcs_lport_get_psym_name(
			bfa_fcs_get_base_port(port->fcs))));

		/* Ensure we have a null terminating string. */
		((char *)psymbl)[strlen((char *)&bfa_fcs_lport_get_psym_name(
		 bfa_fcs_get_base_port(port->fcs)))] = 0;

		strncat((char *)psymbl,
			(char *)&(bfa_fcs_lport_get_psym_name(port)),
			strlen((char *)&bfa_fcs_lport_get_psym_name(port)));
	}

	len = fc_rspnid_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			      bfa_fcs_lport_get_fcid(port), 0, psymbl);

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
		      FC_CLASS_3, len, &fchs, NULL, NULL, FC_MAX_PDUSZ, 0);

	port->stats.ns_rspnid_sent++;
}

/*
 * FCS SCN
 */

#define FC_QOS_RSCN_EVENT		0x0c
#define FC_FABRIC_NAME_RSCN_EVENT	0x0d

/*
 * forward declarations
 */
static void     bfa_fcs_lport_scn_send_scr(void *scn_cbarg,
					  struct bfa_fcxp_s *fcxp_alloced);
static void     bfa_fcs_lport_scn_scr_response(void *fcsarg,
					      struct bfa_fcxp_s *fcxp,
					      void *cbarg,
					      bfa_status_t req_status,
					      u32 rsp_len,
					      u32 resid_len,
					      struct fchs_s *rsp_fchs);
static void     bfa_fcs_lport_scn_send_ls_acc(struct bfa_fcs_lport_s *port,
					     struct fchs_s *rx_fchs);
static void     bfa_fcs_lport_scn_timeout(void *arg);

/*
 *  fcs_scm_sm FCS SCN state machine
 */

/*
 * VPort SCN State Machine events
 */
enum port_scn_event {
	SCNSM_EVENT_PORT_ONLINE = 1,
	SCNSM_EVENT_PORT_OFFLINE = 2,
	SCNSM_EVENT_RSP_OK = 3,
	SCNSM_EVENT_RSP_ERROR = 4,
	SCNSM_EVENT_TIMEOUT = 5,
	SCNSM_EVENT_SCR_SENT = 6,
};

static void     bfa_fcs_lport_scn_sm_offline(struct bfa_fcs_lport_scn_s *scn,
					    enum port_scn_event event);
static void     bfa_fcs_lport_scn_sm_sending_scr(
					struct bfa_fcs_lport_scn_s *scn,
					enum port_scn_event event);
static void     bfa_fcs_lport_scn_sm_scr(struct bfa_fcs_lport_scn_s *scn,
					enum port_scn_event event);
static void     bfa_fcs_lport_scn_sm_scr_retry(struct bfa_fcs_lport_scn_s *scn,
					      enum port_scn_event event);
static void     bfa_fcs_lport_scn_sm_online(struct bfa_fcs_lport_scn_s *scn,
					   enum port_scn_event event);

/*
 *	Starting state - awaiting link up.
 */
static void
bfa_fcs_lport_scn_sm_offline(struct bfa_fcs_lport_scn_s *scn,
			enum port_scn_event event)
{
	switch (event) {
	case SCNSM_EVENT_PORT_ONLINE:
		bfa_sm_set_state(scn, bfa_fcs_lport_scn_sm_sending_scr);
		bfa_fcs_lport_scn_send_scr(scn, NULL);
		break;

	case SCNSM_EVENT_PORT_OFFLINE:
		break;

	default:
		bfa_sm_fault(scn->port->fcs, event);
	}
}

static void
bfa_fcs_lport_scn_sm_sending_scr(struct bfa_fcs_lport_scn_s *scn,
				enum port_scn_event event)
{
	switch (event) {
	case SCNSM_EVENT_SCR_SENT:
		bfa_sm_set_state(scn, bfa_fcs_lport_scn_sm_scr);
		break;

	case SCNSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(scn, bfa_fcs_lport_scn_sm_offline);
		bfa_fcxp_walloc_cancel(scn->port->fcs->bfa, &scn->fcxp_wqe);
		break;

	default:
		bfa_sm_fault(scn->port->fcs, event);
	}
}

static void
bfa_fcs_lport_scn_sm_scr(struct bfa_fcs_lport_scn_s *scn,
			enum port_scn_event event)
{
	struct bfa_fcs_lport_s *port = scn->port;

	switch (event) {
	case SCNSM_EVENT_RSP_OK:
		bfa_sm_set_state(scn, bfa_fcs_lport_scn_sm_online);
		break;

	case SCNSM_EVENT_RSP_ERROR:
		bfa_sm_set_state(scn, bfa_fcs_lport_scn_sm_scr_retry);
		bfa_timer_start(port->fcs->bfa, &scn->timer,
				    bfa_fcs_lport_scn_timeout, scn,
				    BFA_FCS_RETRY_TIMEOUT);
		break;

	case SCNSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(scn, bfa_fcs_lport_scn_sm_offline);
		bfa_fcxp_discard(scn->fcxp);
		break;

	default:
		bfa_sm_fault(port->fcs, event);
	}
}

static void
bfa_fcs_lport_scn_sm_scr_retry(struct bfa_fcs_lport_scn_s *scn,
				enum port_scn_event event)
{
	switch (event) {
	case SCNSM_EVENT_TIMEOUT:
		bfa_sm_set_state(scn, bfa_fcs_lport_scn_sm_sending_scr);
		bfa_fcs_lport_scn_send_scr(scn, NULL);
		break;

	case SCNSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(scn, bfa_fcs_lport_scn_sm_offline);
		bfa_timer_stop(&scn->timer);
		break;

	default:
		bfa_sm_fault(scn->port->fcs, event);
	}
}

static void
bfa_fcs_lport_scn_sm_online(struct bfa_fcs_lport_scn_s *scn,
			enum port_scn_event event)
{
	switch (event) {
	case SCNSM_EVENT_PORT_OFFLINE:
		bfa_sm_set_state(scn, bfa_fcs_lport_scn_sm_offline);
		break;

	default:
		bfa_sm_fault(scn->port->fcs, event);
	}
}



/*
 *  fcs_scn_private FCS SCN private functions
 */

/*
 * This routine will be called to send a SCR command.
 */
static void
bfa_fcs_lport_scn_send_scr(void *scn_cbarg, struct bfa_fcxp_s *fcxp_alloced)
{
	struct bfa_fcs_lport_scn_s *scn = scn_cbarg;
	struct bfa_fcs_lport_s *port = scn->port;
	struct fchs_s fchs;
	int             len;
	struct bfa_fcxp_s *fcxp;

	bfa_trc(port->fcs, port->pid);
	bfa_trc(port->fcs, port->port_cfg.pwwn);

	fcxp = fcxp_alloced ? fcxp_alloced :
	       bfa_fcs_fcxp_alloc(port->fcs, BFA_TRUE);
	if (!fcxp) {
		bfa_fcs_fcxp_alloc_wait(port->fcs->bfa, &scn->fcxp_wqe,
				bfa_fcs_lport_scn_send_scr, scn, BFA_TRUE);
		return;
	}
	scn->fcxp = fcxp;

	/* Handle VU registrations for Base port only */
	if ((!port->vport) && bfa_ioc_get_fcmode(&port->fcs->bfa->ioc)) {
		len = fc_scr_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				port->fabric->lps->brcd_switch,
				port->pid, 0);
	} else {
	    len = fc_scr_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
				    BFA_FALSE,
				    port->pid, 0);
	}

	bfa_fcxp_send(fcxp, NULL, port->fabric->vf_id, port->lp_tag, BFA_FALSE,
			  FC_CLASS_3, len, &fchs,
			  bfa_fcs_lport_scn_scr_response,
			  (void *)scn, FC_MAX_PDUSZ, FC_ELS_TOV);

	bfa_sm_send_event(scn, SCNSM_EVENT_SCR_SENT);
}

static void
bfa_fcs_lport_scn_scr_response(void *fcsarg, struct bfa_fcxp_s *fcxp,
			void *cbarg, bfa_status_t req_status, u32 rsp_len,
			      u32 resid_len, struct fchs_s *rsp_fchs)
{
	struct bfa_fcs_lport_scn_s *scn = (struct bfa_fcs_lport_scn_s *) cbarg;
	struct bfa_fcs_lport_s *port = scn->port;
	struct fc_els_cmd_s *els_cmd;
	struct fc_ls_rjt_s *ls_rjt;

	bfa_trc(port->fcs, port->port_cfg.pwwn);

	/*
	 * Sanity Checks
	 */
	if (req_status != BFA_STATUS_OK) {
		bfa_trc(port->fcs, req_status);
		bfa_sm_send_event(scn, SCNSM_EVENT_RSP_ERROR);
		return;
	}

	els_cmd = (struct fc_els_cmd_s *) BFA_FCXP_RSP_PLD(fcxp);

	switch (els_cmd->els_code) {

	case FC_ELS_ACC:
		bfa_sm_send_event(scn, SCNSM_EVENT_RSP_OK);
		break;

	case FC_ELS_LS_RJT:

		ls_rjt = (struct fc_ls_rjt_s *) BFA_FCXP_RSP_PLD(fcxp);

		bfa_trc(port->fcs, ls_rjt->reason_code);
		bfa_trc(port->fcs, ls_rjt->reason_code_expl);

		bfa_sm_send_event(scn, SCNSM_EVENT_RSP_ERROR);
		break;

	default:
		bfa_sm_send_event(scn, SCNSM_EVENT_RSP_ERROR);
	}
}

/*
 * Send a LS Accept
 */
static void
bfa_fcs_lport_scn_send_ls_acc(struct bfa_fcs_lport_s *port,
				struct fchs_s *rx_fchs)
{
	struct fchs_s fchs;
	struct bfa_fcxp_s *fcxp;
	struct bfa_rport_s *bfa_rport = NULL;
	int             len;

	bfa_trc(port->fcs, rx_fchs->s_id);

	fcxp = bfa_fcs_fcxp_alloc(port->fcs, BFA_FALSE);
	if (!fcxp)
		return;

	len = fc_ls_acc_build(&fchs, bfa_fcxp_get_reqbuf(fcxp),
			      rx_fchs->s_id, bfa_fcs_lport_get_fcid(port),
			      rx_fchs->ox_id);

	bfa_fcxp_send(fcxp, bfa_rport, port->fabric->vf_id, port->lp_tag,
			  BFA_FALSE, FC_CLASS_3, len, &fchs, NULL, NULL,
			  FC_MAX_PDUSZ, 0);
}

/*
 *     This routine will be called by bfa_timer on timer timeouts.
 *
 *	param[in]	vport		- pointer to bfa_fcs_lport_t.
 *	param[out]	vport_status	- pointer to return vport status in
 *
 *	return
 *		void
 *
 *	Special Considerations:
 *
 *	note
 */
static void
bfa_fcs_lport_scn_timeout(void *arg)
{
	struct bfa_fcs_lport_scn_s *scn = (struct bfa_fcs_lport_scn_s *) arg;

	bfa_sm_send_event(scn, SCNSM_EVENT_TIMEOUT);
}



/*
 *  fcs_scn_public FCS state change notification public interfaces
 */

/*
 * Functions called by port/fab
 */
void
bfa_fcs_lport_scn_init(struct bfa_fcs_lport_s *port)
{
	struct bfa_fcs_lport_scn_s *scn = BFA_FCS_GET_SCN_FROM_PORT(port);

	scn->port = port;
	bfa_sm_set_state(scn, bfa_fcs_lport_scn_sm_offline);
}

void
bfa_fcs_lport_scn_offline(struct bfa_fcs_lport_s *port)
{
	struct bfa_fcs_lport_scn_s *scn = BFA_FCS_GET_SCN_FROM_PORT(port);

	scn->port = port;
	bfa_sm_send_event(scn, SCNSM_EVENT_PORT_OFFLINE);
}

void
bfa_fcs_lport_fab_scn_online(struct bfa_fcs_lport_s *port)
{
	struct bfa_fcs_lport_scn_s *scn = BFA_FCS_GET_SCN_FROM_PORT(port);

	scn->port = port;
	bfa_sm_send_event(scn, SCNSM_EVENT_PORT_ONLINE);
}

static void
bfa_fcs_lport_scn_portid_rscn(struct bfa_fcs_lport_s *port, u32 rpid)
{
	struct bfa_fcs_rport_s *rport;
	struct bfa_fcs_fabric_s *fabric = port->fabric;
	struct bfa_fcs_vport_s *vport;
	struct list_head *qe;

	bfa_trc(port->fcs, rpid);

	/*
	 * Ignore PID if it is of base port or of vports created on the
	 * same base port. It is to avoid vports discovering base port or
	 * other vports created on same base port as remote port
	 */
	if (rpid == fabric->bport.pid)
		return;

	list_for_each(qe, &fabric->vport_q) {
		vport = (struct bfa_fcs_vport_s *) qe;
		if (vport->lport.pid == rpid)
			return;
	}
	/*
	 * If this is an unknown device, then it just came online.
	 * Otherwise let rport handle the RSCN event.
	 */
	rport = bfa_fcs_lport_get_rport_by_pid(port, rpid);
	if (!rport)
		rport = bfa_fcs_lport_get_rport_by_old_pid(port, rpid);

	if (rport == NULL) {
		/*
		 * If min cfg mode is enabled, we donot need to
		 * discover any new rports.
		 */
		if (!__fcs_min_cfg(port->fcs))
			rport = bfa_fcs_rport_create(port, rpid);
	} else
		bfa_fcs_rport_scn(rport);
}

/*
 * rscn format based PID comparison
 */
#define __fc_pid_match(__c0, __c1, __fmt)		\
	(((__fmt) == FC_RSCN_FORMAT_FABRIC) ||		\
	 (((__fmt) == FC_RSCN_FORMAT_DOMAIN) &&		\
	  ((__c0)[0] == (__c1)[0])) ||				\
	 (((__fmt) == FC_RSCN_FORMAT_AREA) &&		\
	  ((__c0)[0] == (__c1)[0]) &&				\
	  ((__c0)[1] == (__c1)[1])))

static void
bfa_fcs_lport_scn_multiport_rscn(struct bfa_fcs_lport_s *port,
				enum fc_rscn_format format,
				u32 rscn_pid)
{
	struct bfa_fcs_rport_s *rport;
	struct list_head        *qe, *qe_next;
	u8        *c0, *c1;

	bfa_trc(port->fcs, format);
	bfa_trc(port->fcs, rscn_pid);

	c0 = (u8 *) &rscn_pid;

	list_for_each_safe(qe, qe_next, &port->rport_q) {
		rport = (struct bfa_fcs_rport_s *) qe;
		c1 = (u8 *) &rport->pid;
		if (__fc_pid_match(c0, c1, format))
			bfa_fcs_rport_scn(rport);
	}
}


void
bfa_fcs_lport_scn_process_rscn(struct bfa_fcs_lport_s *port,
			struct fchs_s *fchs, u32 len)
{
	struct fc_rscn_pl_s *rscn = (struct fc_rscn_pl_s *) (fchs + 1);
	int             num_entries;
	u32        rscn_pid;
	bfa_boolean_t   nsquery = BFA_FALSE, found;
	int             i = 0, j;

	num_entries =
		(be16_to_cpu(rscn->payldlen) -
		 sizeof(u32)) / sizeof(rscn->event[0]);

	bfa_trc(port->fcs, num_entries);

	port->stats.num_rscn++;

	bfa_fcs_lport_scn_send_ls_acc(port, fchs);

	for (i = 0; i < num_entries; i++) {
		rscn_pid = rscn->event[i].portid;

		bfa_trc(port->fcs, rscn->event[i].format);
		bfa_trc(port->fcs, rscn_pid);

		/* check for duplicate entries in the list */
		found = BFA_FALSE;
		for (j = 0; j < i; j++) {
			if (rscn->event[j].portid == rscn_pid) {
				found = BFA_TRUE;
				break;
			}
		}

		/* if found in down the list, pid has been already processed */
		if (found) {
			bfa_trc(port->fcs, rscn_pid);
			continue;
		}

		switch (rscn->event[i].format) {
		case FC_RSCN_FORMAT_PORTID:
			if (rscn->event[i].qualifier == FC_QOS_RSCN_EVENT) {
				/*
				 * Ignore this event.
				 * f/w would have processed it
				 */
				bfa_trc(port->fcs, rscn_pid);
			} else {
				port->stats.num_portid_rscn++;
				bfa_fcs_lport_scn_portid_rscn(port, rscn_pid);
			}
		break;

		case FC_RSCN_FORMAT_FABRIC:
			if (rscn->event[i].qualifier ==
					FC_FABRIC_NAME_RSCN_EVENT) {
				bfa_fcs_lport_ms_fabric_rscn(port);
				break;
			}
			/* !!!!!!!!! Fall Through !!!!!!!!!!!!! */

		case FC_RSCN_FORMAT_AREA:
		case FC_RSCN_FORMAT_DOMAIN:
			nsquery = BFA_TRUE;
			bfa_fcs_lport_scn_multiport_rscn(port,
							rscn->event[i].format,
							rscn_pid);
			break;


		default:
			WARN_ON(1);
			nsquery = BFA_TRUE;
		}
	}

	/*
	 * If any of area, domain or fabric RSCN is received, do a fresh
	 * discovery to find new devices.
	 */
	if (nsquery)
		bfa_fcs_lport_ns_query(port);
}

/*
 * BFA FCS port
 */
/*
 *  fcs_port_api BFA FCS port API
 */
struct bfa_fcs_lport_s *
bfa_fcs_get_base_port(struct bfa_fcs_s *fcs)
{
	return &fcs->fabric.bport;
}

wwn_t
bfa_fcs_lport_get_rport(struct bfa_fcs_lport_s *port, wwn_t wwn, int index,
		int nrports, bfa_boolean_t bwwn)
{
	struct list_head	*qh, *qe;
	struct bfa_fcs_rport_s *rport = NULL;
	int	i;
	struct bfa_fcs_s	*fcs;

	if (port == NULL || nrports == 0)
		return (wwn_t) 0;

	fcs = port->fcs;
	bfa_trc(fcs, (u32) nrports);

	i = 0;
	qh = &port->rport_q;
	qe = bfa_q_first(qh);

	while ((qe != qh) && (i < nrports)) {
		rport = (struct bfa_fcs_rport_s *) qe;
		if (bfa_ntoh3b(rport->pid) > 0xFFF000) {
			qe = bfa_q_next(qe);
			bfa_trc(fcs, (u32) rport->pwwn);
			bfa_trc(fcs, rport->pid);
			bfa_trc(fcs, i);
			continue;
		}

		if (bwwn) {
			if (!memcmp(&wwn, &rport->pwwn, 8))
				break;
		} else {
			if (i == index)
				break;
		}

		i++;
		qe = bfa_q_next(qe);
	}

	bfa_trc(fcs, i);
	if (rport)
		return rport->pwwn;
	else
		return (wwn_t) 0;
}

void
bfa_fcs_lport_get_rport_quals(struct bfa_fcs_lport_s *port,
		struct bfa_rport_qualifier_s rports[], int *nrports)
{
	struct list_head	*qh, *qe;
	struct bfa_fcs_rport_s *rport = NULL;
	int	i;
	struct bfa_fcs_s	*fcs;

	if (port == NULL || rports == NULL || *nrports == 0)
		return;

	fcs = port->fcs;
	bfa_trc(fcs, (u32) *nrports);

	i = 0;
	qh = &port->rport_q;
	qe = bfa_q_first(qh);

	while ((qe != qh) && (i < *nrports)) {
		rport = (struct bfa_fcs_rport_s *) qe;
		if (bfa_ntoh3b(rport->pid) > 0xFFF000) {
			qe = bfa_q_next(qe);
			bfa_trc(fcs, (u32) rport->pwwn);
			bfa_trc(fcs, rport->pid);
			bfa_trc(fcs, i);
			continue;
		}

		if (!rport->pwwn && !rport->pid) {
			qe = bfa_q_next(qe);
			continue;
		}

		rports[i].pwwn = rport->pwwn;
		rports[i].pid = rport->pid;

		i++;
		qe = bfa_q_next(qe);
	}

	bfa_trc(fcs, i);
	*nrports = i;
}

/*
 * Iterate's through all the rport's in the given port to
 * determine the maximum operating speed.
 *
 * !!!! To be used in TRL Functionality only !!!!
 */
bfa_port_speed_t
bfa_fcs_lport_get_rport_max_speed(bfa_fcs_lport_t *port)
{
	struct list_head *qh, *qe;
	struct bfa_fcs_rport_s *rport = NULL;
	struct bfa_fcs_s	*fcs;
	bfa_port_speed_t max_speed = 0;
	struct bfa_port_attr_s port_attr;
	bfa_port_speed_t port_speed, rport_speed;
	bfa_boolean_t trl_enabled = bfa_fcport_is_ratelim(port->fcs->bfa);


	if (port == NULL)
		return 0;

	fcs = port->fcs;

	/* Get Physical port's current speed */
	bfa_fcport_get_attr(port->fcs->bfa, &port_attr);
	port_speed = port_attr.speed;
	bfa_trc(fcs, port_speed);

	qh = &port->rport_q;
	qe = bfa_q_first(qh);

	while (qe != qh) {
		rport = (struct bfa_fcs_rport_s *) qe;
		if ((bfa_ntoh3b(rport->pid) > 0xFFF000) ||
			(bfa_fcs_rport_get_state(rport) == BFA_RPORT_OFFLINE) ||
			(rport->scsi_function != BFA_RPORT_TARGET)) {
			qe = bfa_q_next(qe);
			continue;
		}

		rport_speed = rport->rpf.rpsc_speed;
		if ((trl_enabled) && (rport_speed ==
			BFA_PORT_SPEED_UNKNOWN)) {
			/* Use default ratelim speed setting */
			rport_speed =
				bfa_fcport_get_ratelim_speed(port->fcs->bfa);
		}

		if (rport_speed > max_speed)
			max_speed = rport_speed;

		qe = bfa_q_next(qe);
	}

	if (max_speed > port_speed)
		max_speed = port_speed;

	bfa_trc(fcs, max_speed);
	return max_speed;
}

struct bfa_fcs_lport_s *
bfa_fcs_lookup_port(struct bfa_fcs_s *fcs, u16 vf_id, wwn_t lpwwn)
{
	struct bfa_fcs_vport_s *vport;
	bfa_fcs_vf_t   *vf;

	WARN_ON(fcs == NULL);

	vf = bfa_fcs_vf_lookup(fcs, vf_id);
	if (vf == NULL) {
		bfa_trc(fcs, vf_id);
		return NULL;
	}

	if (!lpwwn || (vf->bport.port_cfg.pwwn == lpwwn))
		return &vf->bport;

	vport = bfa_fcs_fabric_vport_lookup(vf, lpwwn);
	if (vport)
		return &vport->lport;

	return NULL;
}

/*
 *  API corresponding to NPIV_VPORT_GETINFO.
 */
void
bfa_fcs_lport_get_info(struct bfa_fcs_lport_s *port,
	 struct bfa_lport_info_s *port_info)
{

	bfa_trc(port->fcs, port->fabric->fabric_name);

	if (port->vport == NULL) {
		/*
		 * This is a Physical port
		 */
		port_info->port_type = BFA_LPORT_TYPE_PHYSICAL;

		/*
		 * @todo : need to fix the state & reason
		 */
		port_info->port_state = 0;
		port_info->offline_reason = 0;

		port_info->port_wwn = bfa_fcs_lport_get_pwwn(port);
		port_info->node_wwn = bfa_fcs_lport_get_nwwn(port);

		port_info->max_vports_supp =
			bfa_lps_get_max_vport(port->fcs->bfa);
		port_info->num_vports_inuse =
			port->fabric->num_vports;
		port_info->max_rports_supp = BFA_FCS_MAX_RPORTS_SUPP;
		port_info->num_rports_inuse = port->num_rports;
	} else {
		/*
		 * This is a virtual port
		 */
		port_info->port_type = BFA_LPORT_TYPE_VIRTUAL;

		/*
		 * @todo : need to fix the state & reason
		 */
		port_info->port_state = 0;
		port_info->offline_reason = 0;

		port_info->port_wwn = bfa_fcs_lport_get_pwwn(port);
		port_info->node_wwn = bfa_fcs_lport_get_nwwn(port);
	}
}

void
bfa_fcs_lport_get_stats(struct bfa_fcs_lport_s *fcs_port,
	 struct bfa_lport_stats_s *port_stats)
{
	*port_stats = fcs_port->stats;
}

void
bfa_fcs_lport_clear_stats(struct bfa_fcs_lport_s *fcs_port)
{
	memset(&fcs_port->stats, 0, sizeof(struct bfa_lport_stats_s));
}

/*
 * Let new loop map create missing rports
 */
void
bfa_fcs_lport_lip_scn_online(struct bfa_fcs_lport_s *port)
{
	bfa_fcs_lport_loop_online(port);
}

/*
 * FCS virtual port state machine
 */

#define __vport_fcs(__vp)       ((__vp)->lport.fcs)
#define __vport_pwwn(__vp)      ((__vp)->lport.port_cfg.pwwn)
#define __vport_nwwn(__vp)      ((__vp)->lport.port_cfg.nwwn)
#define __vport_bfa(__vp)       ((__vp)->lport.fcs->bfa)
#define __vport_fcid(__vp)      ((__vp)->lport.pid)
#define __vport_fabric(__vp)    ((__vp)->lport.fabric)
#define __vport_vfid(__vp)      ((__vp)->lport.fabric->vf_id)

#define BFA_FCS_VPORT_MAX_RETRIES  5
/*
 * Forward declarations
 */
static void     bfa_fcs_vport_do_fdisc(struct bfa_fcs_vport_s *vport);
static void     bfa_fcs_vport_timeout(void *vport_arg);
static void     bfa_fcs_vport_do_logo(struct bfa_fcs_vport_s *vport);
static void     bfa_fcs_vport_free(struct bfa_fcs_vport_s *vport);

/*
 *  fcs_vport_sm FCS virtual port state machine
 */

/*
 * VPort State Machine events
 */
enum bfa_fcs_vport_event {
	BFA_FCS_VPORT_SM_CREATE = 1,	/*  vport create event */
	BFA_FCS_VPORT_SM_DELETE = 2,	/*  vport delete event */
	BFA_FCS_VPORT_SM_START = 3,	/*  vport start request */
	BFA_FCS_VPORT_SM_STOP = 4,	/*  stop: unsupported */
	BFA_FCS_VPORT_SM_ONLINE = 5,	/*  fabric online */
	BFA_FCS_VPORT_SM_OFFLINE = 6,	/*  fabric offline event */
	BFA_FCS_VPORT_SM_FRMSENT = 7,	/*  fdisc/logo sent events */
	BFA_FCS_VPORT_SM_RSP_OK = 8,	/*  good response */
	BFA_FCS_VPORT_SM_RSP_ERROR = 9,	/*  error/bad response */
	BFA_FCS_VPORT_SM_TIMEOUT = 10,	/*  delay timer event */
	BFA_FCS_VPORT_SM_DELCOMP = 11,	/*  lport delete completion */
	BFA_FCS_VPORT_SM_RSP_DUP_WWN = 12,	/*  Dup wnn error*/
	BFA_FCS_VPORT_SM_RSP_FAILED = 13,	/*  non-retryable failure */
	BFA_FCS_VPORT_SM_STOPCOMP = 14,	/* vport delete completion */
};

static void     bfa_fcs_vport_sm_uninit(struct bfa_fcs_vport_s *vport,
					enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_created(struct bfa_fcs_vport_s *vport,
					 enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_offline(struct bfa_fcs_vport_s *vport,
					 enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_fdisc(struct bfa_fcs_vport_s *vport,
				       enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_fdisc_retry(struct bfa_fcs_vport_s *vport,
					     enum bfa_fcs_vport_event event);
static void	bfa_fcs_vport_sm_fdisc_rsp_wait(struct bfa_fcs_vport_s *vport,
					enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_online(struct bfa_fcs_vport_s *vport,
					enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_deleting(struct bfa_fcs_vport_s *vport,
					  enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_cleanup(struct bfa_fcs_vport_s *vport,
					 enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_logo(struct bfa_fcs_vport_s *vport,
				      enum bfa_fcs_vport_event event);
static void     bfa_fcs_vport_sm_error(struct bfa_fcs_vport_s *vport,
				      enum bfa_fcs_vport_event event);
static void	bfa_fcs_vport_sm_stopping(struct bfa_fcs_vport_s *vport,
					enum bfa_fcs_vport_event event);
static void	bfa_fcs_vport_sm_logo_for_stop(struct bfa_fcs_vport_s *vport,
					enum bfa_fcs_vport_event event);

static struct bfa_sm_table_s  vport_sm_table[] = {
	{BFA_SM(bfa_fcs_vport_sm_uninit), BFA_FCS_VPORT_UNINIT},
	{BFA_SM(bfa_fcs_vport_sm_created), BFA_FCS_VPORT_CREATED},
	{BFA_SM(bfa_fcs_vport_sm_offline), BFA_FCS_VPORT_OFFLINE},
	{BFA_SM(bfa_fcs_vport_sm_fdisc), BFA_FCS_VPORT_FDISC},
	{BFA_SM(bfa_fcs_vport_sm_fdisc_retry), BFA_FCS_VPORT_FDISC_RETRY},
	{BFA_SM(bfa_fcs_vport_sm_fdisc_rsp_wait), BFA_FCS_VPORT_FDISC_RSP_WAIT},
	{BFA_SM(bfa_fcs_vport_sm_online), BFA_FCS_VPORT_ONLINE},
	{BFA_SM(bfa_fcs_vport_sm_deleting), BFA_FCS_VPORT_DELETING},
	{BFA_SM(bfa_fcs_vport_sm_cleanup), BFA_FCS_VPORT_CLEANUP},
	{BFA_SM(bfa_fcs_vport_sm_logo), BFA_FCS_VPORT_LOGO},
	{BFA_SM(bfa_fcs_vport_sm_error), BFA_FCS_VPORT_ERROR}
};

/*
 * Beginning state.
 */
static void
bfa_fcs_vport_sm_uninit(struct bfa_fcs_vport_s *vport,
			enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_CREATE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_created);
		bfa_fcs_fabric_addvport(__vport_fabric(vport), vport);
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/*
 * Created state - a start event is required to start up the state machine.
 */
static void
bfa_fcs_vport_sm_created(struct bfa_fcs_vport_s *vport,
			enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_START:
		if (bfa_sm_cmp_state(__vport_fabric(vport),
					bfa_fcs_fabric_sm_online)
		    && bfa_fcs_fabric_npiv_capable(__vport_fabric(vport))) {
			bfa_sm_set_state(vport, bfa_fcs_vport_sm_fdisc);
			bfa_fcs_vport_do_fdisc(vport);
		} else {
			/*
			 * Fabric is offline or not NPIV capable, stay in
			 * offline state.
			 */
			vport->vport_stats.fab_no_npiv++;
			bfa_sm_set_state(vport, bfa_fcs_vport_sm_offline);
		}
		break;

	case BFA_FCS_VPORT_SM_DELETE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_cleanup);
		bfa_fcs_lport_delete(&vport->lport);
		break;

	case BFA_FCS_VPORT_SM_ONLINE:
	case BFA_FCS_VPORT_SM_OFFLINE:
		/*
		 * Ignore ONLINE/OFFLINE events from fabric
		 * till vport is started.
		 */
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/*
 * Offline state - awaiting ONLINE event from fabric SM.
 */
static void
bfa_fcs_vport_sm_offline(struct bfa_fcs_vport_s *vport,
			enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_DELETE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_cleanup);
		bfa_fcs_lport_delete(&vport->lport);
		break;

	case BFA_FCS_VPORT_SM_ONLINE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_fdisc);
		vport->fdisc_retries = 0;
		bfa_fcs_vport_do_fdisc(vport);
		break;

	case BFA_FCS_VPORT_SM_STOP:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_cleanup);
		bfa_sm_send_event(&vport->lport, BFA_FCS_PORT_SM_STOP);
		break;

	case BFA_FCS_VPORT_SM_OFFLINE:
		/*
		 * This can happen if the vport couldn't be initialzied
		 * due the fact that the npiv was not enabled on the switch.
		 * In that case we will put the vport in offline state.
		 * However, the link can go down and cause the this event to
		 * be sent when we are already offline. Ignore it.
		 */
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}


/*
 * FDISC is sent and awaiting reply from fabric.
 */
static void
bfa_fcs_vport_sm_fdisc(struct bfa_fcs_vport_s *vport,
			enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_DELETE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_fdisc_rsp_wait);
		break;

	case BFA_FCS_VPORT_SM_OFFLINE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_offline);
		bfa_sm_send_event(vport->lps, BFA_LPS_SM_OFFLINE);
		break;

	case BFA_FCS_VPORT_SM_RSP_OK:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_online);
		bfa_fcs_lport_online(&vport->lport);
		break;

	case BFA_FCS_VPORT_SM_RSP_ERROR:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_fdisc_retry);
		bfa_timer_start(__vport_bfa(vport), &vport->timer,
				    bfa_fcs_vport_timeout, vport,
				    BFA_FCS_RETRY_TIMEOUT);
		break;

	case BFA_FCS_VPORT_SM_RSP_FAILED:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_offline);
		break;

	case BFA_FCS_VPORT_SM_RSP_DUP_WWN:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_error);
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/*
 * FDISC attempt failed - a timer is active to retry FDISC.
 */
static void
bfa_fcs_vport_sm_fdisc_retry(struct bfa_fcs_vport_s *vport,
			     enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_DELETE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_cleanup);
		bfa_timer_stop(&vport->timer);
		bfa_fcs_lport_delete(&vport->lport);
		break;

	case BFA_FCS_VPORT_SM_OFFLINE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_offline);
		bfa_timer_stop(&vport->timer);
		break;

	case BFA_FCS_VPORT_SM_TIMEOUT:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_fdisc);
		vport->vport_stats.fdisc_retries++;
		vport->fdisc_retries++;
		bfa_fcs_vport_do_fdisc(vport);
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/*
 * FDISC is in progress and we got a vport delete request -
 * this is a wait state while we wait for fdisc response and
 * we will transition to the appropriate state - on rsp status.
 */
static void
bfa_fcs_vport_sm_fdisc_rsp_wait(struct bfa_fcs_vport_s *vport,
				enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_RSP_OK:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_deleting);
		bfa_fcs_lport_delete(&vport->lport);
		break;

	case BFA_FCS_VPORT_SM_DELETE:
		break;

	case BFA_FCS_VPORT_SM_OFFLINE:
	case BFA_FCS_VPORT_SM_RSP_ERROR:
	case BFA_FCS_VPORT_SM_RSP_FAILED:
	case BFA_FCS_VPORT_SM_RSP_DUP_WWN:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_cleanup);
		bfa_sm_send_event(vport->lps, BFA_LPS_SM_OFFLINE);
		bfa_fcs_lport_delete(&vport->lport);
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/*
 * Vport is online (FDISC is complete).
 */
static void
bfa_fcs_vport_sm_online(struct bfa_fcs_vport_s *vport,
			enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_DELETE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_deleting);
		bfa_fcs_lport_delete(&vport->lport);
		break;

	case BFA_FCS_VPORT_SM_STOP:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_stopping);
		bfa_sm_send_event(&vport->lport, BFA_FCS_PORT_SM_STOP);
		break;

	case BFA_FCS_VPORT_SM_OFFLINE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_offline);
		bfa_sm_send_event(vport->lps, BFA_LPS_SM_OFFLINE);
		bfa_fcs_lport_offline(&vport->lport);
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/*
 * Vport is being stopped - awaiting lport stop completion to send
 * LOGO to fabric.
 */
static void
bfa_fcs_vport_sm_stopping(struct bfa_fcs_vport_s *vport,
			  enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_STOPCOMP:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_logo_for_stop);
		bfa_fcs_vport_do_logo(vport);
		break;

	case BFA_FCS_VPORT_SM_OFFLINE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_cleanup);
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/*
 * Vport is being deleted - awaiting lport delete completion to send
 * LOGO to fabric.
 */
static void
bfa_fcs_vport_sm_deleting(struct bfa_fcs_vport_s *vport,
			enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_DELETE:
		break;

	case BFA_FCS_VPORT_SM_DELCOMP:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_logo);
		bfa_fcs_vport_do_logo(vport);
		break;

	case BFA_FCS_VPORT_SM_OFFLINE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_cleanup);
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/*
 * Error State.
 * This state will be set when the Vport Creation fails due
 * to errors like Dup WWN. In this state only operation allowed
 * is a Vport Delete.
 */
static void
bfa_fcs_vport_sm_error(struct bfa_fcs_vport_s *vport,
			enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_DELETE:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_cleanup);
		bfa_fcs_lport_delete(&vport->lport);
		break;

	default:
		bfa_trc(__vport_fcs(vport), event);
	}
}

/*
 * Lport cleanup is in progress since vport is being deleted. Fabric is
 * offline, so no LOGO is needed to complete vport deletion.
 */
static void
bfa_fcs_vport_sm_cleanup(struct bfa_fcs_vport_s *vport,
			enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_DELCOMP:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_uninit);
		bfa_fcs_vport_free(vport);
		break;

	case BFA_FCS_VPORT_SM_STOPCOMP:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_created);
		break;

	case BFA_FCS_VPORT_SM_DELETE:
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/*
 * LOGO is sent to fabric. Vport stop is in progress. Lport stop cleanup
 * is done.
 */
static void
bfa_fcs_vport_sm_logo_for_stop(struct bfa_fcs_vport_s *vport,
			       enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_OFFLINE:
		bfa_sm_send_event(vport->lps, BFA_LPS_SM_OFFLINE);
		/*
		 * !!! fall through !!!
		 */

	case BFA_FCS_VPORT_SM_RSP_OK:
	case BFA_FCS_VPORT_SM_RSP_ERROR:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_created);
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}

/*
 * LOGO is sent to fabric. Vport delete is in progress. Lport delete cleanup
 * is done.
 */
static void
bfa_fcs_vport_sm_logo(struct bfa_fcs_vport_s *vport,
			enum bfa_fcs_vport_event event)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), event);

	switch (event) {
	case BFA_FCS_VPORT_SM_OFFLINE:
		bfa_sm_send_event(vport->lps, BFA_LPS_SM_OFFLINE);
		/*
		 * !!! fall through !!!
		 */

	case BFA_FCS_VPORT_SM_RSP_OK:
	case BFA_FCS_VPORT_SM_RSP_ERROR:
		bfa_sm_set_state(vport, bfa_fcs_vport_sm_uninit);
		bfa_fcs_vport_free(vport);
		break;

	case BFA_FCS_VPORT_SM_DELETE:
		break;

	default:
		bfa_sm_fault(__vport_fcs(vport), event);
	}
}



/*
 *  fcs_vport_private FCS virtual port private functions
 */
/*
 * Send AEN notification
 */
static void
bfa_fcs_vport_aen_post(struct bfa_fcs_lport_s *port,
		       enum bfa_lport_aen_event event)
{
	struct bfad_s *bfad = (struct bfad_s *)port->fabric->fcs->bfad;
	struct bfa_aen_entry_s  *aen_entry;

	bfad_get_aen_entry(bfad, aen_entry);
	if (!aen_entry)
		return;

	aen_entry->aen_data.lport.vf_id = port->fabric->vf_id;
	aen_entry->aen_data.lport.roles = port->port_cfg.roles;
	aen_entry->aen_data.lport.ppwwn = bfa_fcs_lport_get_pwwn(
					bfa_fcs_get_base_port(port->fcs));
	aen_entry->aen_data.lport.lpwwn = bfa_fcs_lport_get_pwwn(port);

	/* Send the AEN notification */
	bfad_im_post_vendor_event(aen_entry, bfad, ++port->fcs->fcs_aen_seq,
				  BFA_AEN_CAT_LPORT, event);
}

/*
 * This routine will be called to send a FDISC command.
 */
static void
bfa_fcs_vport_do_fdisc(struct bfa_fcs_vport_s *vport)
{
	bfa_lps_fdisc(vport->lps, vport,
		bfa_fcport_get_maxfrsize(__vport_bfa(vport)),
		__vport_pwwn(vport), __vport_nwwn(vport));
	vport->vport_stats.fdisc_sent++;
}

static void
bfa_fcs_vport_fdisc_rejected(struct bfa_fcs_vport_s *vport)
{
	u8		lsrjt_rsn = vport->lps->lsrjt_rsn;
	u8		lsrjt_expl = vport->lps->lsrjt_expl;

	bfa_trc(__vport_fcs(vport), lsrjt_rsn);
	bfa_trc(__vport_fcs(vport), lsrjt_expl);

	/* For certain reason codes, we don't want to retry. */
	switch (vport->lps->lsrjt_expl) {
	case FC_LS_RJT_EXP_INV_PORT_NAME: /* by brocade */
	case FC_LS_RJT_EXP_INVALID_NPORT_ID: /* by Cisco */
		if (vport->fdisc_retries < BFA_FCS_VPORT_MAX_RETRIES)
			bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_ERROR);
		else {
			bfa_fcs_vport_aen_post(&vport->lport,
					BFA_LPORT_AEN_NPIV_DUP_WWN);
			bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_DUP_WWN);
		}
		break;

	case FC_LS_RJT_EXP_INSUFF_RES:
		/*
		 * This means max logins per port/switch setting on the
		 * switch was exceeded.
		 */
		if (vport->fdisc_retries < BFA_FCS_VPORT_MAX_RETRIES)
			bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_ERROR);
		else {
			bfa_fcs_vport_aen_post(&vport->lport,
					BFA_LPORT_AEN_NPIV_FABRIC_MAX);
			bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_FAILED);
		}
		break;

	default:
		if (vport->fdisc_retries == 0)
			bfa_fcs_vport_aen_post(&vport->lport,
					BFA_LPORT_AEN_NPIV_UNKNOWN);
		bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_ERROR);
	}
}

/*
 *	Called to send a logout to the fabric. Used when a V-Port is
 *	deleted/stopped.
 */
static void
bfa_fcs_vport_do_logo(struct bfa_fcs_vport_s *vport)
{
	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));

	vport->vport_stats.logo_sent++;
	bfa_lps_fdisclogo(vport->lps);
}


/*
 *     This routine will be called by bfa_timer on timer timeouts.
 *
 *	param[in]	vport		- pointer to bfa_fcs_vport_t.
 *	param[out]	vport_status	- pointer to return vport status in
 *
 *	return
 *		void
 *
 *	Special Considerations:
 *
 *	note
 */
static void
bfa_fcs_vport_timeout(void *vport_arg)
{
	struct bfa_fcs_vport_s *vport = (struct bfa_fcs_vport_s *) vport_arg;

	vport->vport_stats.fdisc_timeouts++;
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_TIMEOUT);
}

static void
bfa_fcs_vport_free(struct bfa_fcs_vport_s *vport)
{
	struct bfad_vport_s *vport_drv =
			(struct bfad_vport_s *)vport->vport_drv;

	bfa_fcs_fabric_delvport(__vport_fabric(vport), vport);
	bfa_lps_delete(vport->lps);

	if (vport_drv->comp_del) {
		complete(vport_drv->comp_del);
		return;
	}

	/*
	 * We queue the vport delete work to the IM work_q from here.
	 * The memory for the bfad_vport_s is freed from the FC function
	 * template vport_delete entry point.
	 */
	bfad_im_port_delete(vport_drv->drv_port.bfad, &vport_drv->drv_port);
}

/*
 *  fcs_vport_public FCS virtual port public interfaces
 */

/*
 * Online notification from fabric SM.
 */
void
bfa_fcs_vport_online(struct bfa_fcs_vport_s *vport)
{
	vport->vport_stats.fab_online++;
	if (bfa_fcs_fabric_npiv_capable(__vport_fabric(vport)))
		bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_ONLINE);
	else
		vport->vport_stats.fab_no_npiv++;
}

/*
 * Offline notification from fabric SM.
 */
void
bfa_fcs_vport_offline(struct bfa_fcs_vport_s *vport)
{
	vport->vport_stats.fab_offline++;
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_OFFLINE);
}

/*
 * Cleanup notification from fabric SM on link timer expiry.
 */
void
bfa_fcs_vport_cleanup(struct bfa_fcs_vport_s *vport)
{
	vport->vport_stats.fab_cleanup++;
}

/*
 * Stop notification from fabric SM. To be invoked from within FCS.
 */
void
bfa_fcs_vport_fcs_stop(struct bfa_fcs_vport_s *vport)
{
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_STOP);
}

/*
 * delete notification from fabric SM. To be invoked from within FCS.
 */
void
bfa_fcs_vport_fcs_delete(struct bfa_fcs_vport_s *vport)
{
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_DELETE);
}

/*
 * Stop completion callback from associated lport
 */
void
bfa_fcs_vport_stop_comp(struct bfa_fcs_vport_s *vport)
{
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_STOPCOMP);
}

/*
 * Delete completion callback from associated lport
 */
void
bfa_fcs_vport_delete_comp(struct bfa_fcs_vport_s *vport)
{
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_DELCOMP);
}



/*
 *  fcs_vport_api Virtual port API
 */

/*
 *	Use this function to instantiate a new FCS vport object. This
 *	function will not trigger any HW initialization process (which will be
 *	done in vport_start() call)
 *
 *	param[in] vport	-		pointer to bfa_fcs_vport_t. This space
 *					needs to be allocated by the driver.
 *	param[in] fcs		-	FCS instance
 *	param[in] vport_cfg	-	vport configuration
 *	param[in] vf_id		-	VF_ID if vport is created within a VF.
 *					FC_VF_ID_NULL to specify base fabric.
 *	param[in] vport_drv	-	Opaque handle back to the driver's vport
 *					structure
 *
 *	retval BFA_STATUS_OK - on success.
 *	retval BFA_STATUS_FAILED - on failure.
 */
bfa_status_t
bfa_fcs_vport_create(struct bfa_fcs_vport_s *vport, struct bfa_fcs_s *fcs,
		u16 vf_id, struct bfa_lport_cfg_s *vport_cfg,
		struct bfad_vport_s *vport_drv)
{
	if (vport_cfg->pwwn == 0)
		return BFA_STATUS_INVALID_WWN;

	if (bfa_fcs_lport_get_pwwn(&fcs->fabric.bport) == vport_cfg->pwwn)
		return BFA_STATUS_VPORT_WWN_BP;

	if (bfa_fcs_vport_lookup(fcs, vf_id, vport_cfg->pwwn) != NULL)
		return BFA_STATUS_VPORT_EXISTS;

	if (fcs->fabric.num_vports ==
			bfa_lps_get_max_vport(fcs->bfa))
		return BFA_STATUS_VPORT_MAX;

	vport->lps = bfa_lps_alloc(fcs->bfa);
	if (!vport->lps)
		return BFA_STATUS_VPORT_MAX;

	vport->vport_drv = vport_drv;
	vport_cfg->preboot_vp = BFA_FALSE;

	bfa_sm_set_state(vport, bfa_fcs_vport_sm_uninit);
	bfa_fcs_lport_attach(&vport->lport, fcs, vf_id, vport);
	bfa_fcs_lport_init(&vport->lport, vport_cfg);
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_CREATE);

	return BFA_STATUS_OK;
}

/*
 *	Use this function to instantiate a new FCS PBC vport object. This
 *	function will not trigger any HW initialization process (which will be
 *	done in vport_start() call)
 *
 *	param[in] vport	-	pointer to bfa_fcs_vport_t. This space
 *				needs to be allocated by the driver.
 *	param[in] fcs	-	FCS instance
 *	param[in] vport_cfg	-	vport configuration
 *	param[in] vf_id		-	VF_ID if vport is created within a VF.
 *					FC_VF_ID_NULL to specify base fabric.
 *	param[in] vport_drv	-	Opaque handle back to the driver's vport
 *					structure
 *
 *	retval BFA_STATUS_OK - on success.
 *	retval BFA_STATUS_FAILED - on failure.
 */
bfa_status_t
bfa_fcs_pbc_vport_create(struct bfa_fcs_vport_s *vport, struct bfa_fcs_s *fcs,
			u16 vf_id, struct bfa_lport_cfg_s *vport_cfg,
			struct bfad_vport_s *vport_drv)
{
	bfa_status_t rc;

	rc = bfa_fcs_vport_create(vport, fcs, vf_id, vport_cfg, vport_drv);
	vport->lport.port_cfg.preboot_vp = BFA_TRUE;

	return rc;
}

/*
 *	Use this function to findout if this is a pbc vport or not.
 *
 * @param[in] vport - pointer to bfa_fcs_vport_t.
 *
 * @returns None
 */
bfa_boolean_t
bfa_fcs_is_pbc_vport(struct bfa_fcs_vport_s *vport)
{

	if (vport && (vport->lport.port_cfg.preboot_vp == BFA_TRUE))
		return BFA_TRUE;
	else
		return BFA_FALSE;

}

/*
 * Use this function initialize the vport.
 *
 * @param[in] vport - pointer to bfa_fcs_vport_t.
 *
 * @returns None
 */
bfa_status_t
bfa_fcs_vport_start(struct bfa_fcs_vport_s *vport)
{
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_START);

	return BFA_STATUS_OK;
}

/*
 *	Use this function quiese the vport object. This function will return
 *	immediately, when the vport is actually stopped, the
 *	bfa_drv_vport_stop_cb() will be called.
 *
 *	param[in] vport - pointer to bfa_fcs_vport_t.
 *
 *	return None
 */
bfa_status_t
bfa_fcs_vport_stop(struct bfa_fcs_vport_s *vport)
{
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_STOP);

	return BFA_STATUS_OK;
}

/*
 *	Use this function to delete a vport object. Fabric object should
 *	be stopped before this function call.
 *
 *	!!!!!!! Donot invoke this from within FCS  !!!!!!!
 *
 *	param[in] vport - pointer to bfa_fcs_vport_t.
 *
 *	return     None
 */
bfa_status_t
bfa_fcs_vport_delete(struct bfa_fcs_vport_s *vport)
{

	if (vport->lport.port_cfg.preboot_vp)
		return BFA_STATUS_PBC;

	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_DELETE);

	return BFA_STATUS_OK;
}

/*
 *	Use this function to get vport's current status info.
 *
 *	param[in] vport		pointer to bfa_fcs_vport_t.
 *	param[out] attr		pointer to return vport attributes
 *
 *	return None
 */
void
bfa_fcs_vport_get_attr(struct bfa_fcs_vport_s *vport,
			struct bfa_vport_attr_s *attr)
{
	if (vport == NULL || attr == NULL)
		return;

	memset(attr, 0, sizeof(struct bfa_vport_attr_s));

	bfa_fcs_lport_get_attr(&vport->lport, &attr->port_attr);
	attr->vport_state = bfa_sm_to_state(vport_sm_table, vport->sm);
}


/*
 *	Lookup a virtual port. Excludes base port from lookup.
 */
struct bfa_fcs_vport_s *
bfa_fcs_vport_lookup(struct bfa_fcs_s *fcs, u16 vf_id, wwn_t vpwwn)
{
	struct bfa_fcs_vport_s *vport;
	struct bfa_fcs_fabric_s *fabric;

	bfa_trc(fcs, vf_id);
	bfa_trc(fcs, vpwwn);

	fabric = bfa_fcs_vf_lookup(fcs, vf_id);
	if (!fabric) {
		bfa_trc(fcs, vf_id);
		return NULL;
	}

	vport = bfa_fcs_fabric_vport_lookup(fabric, vpwwn);
	return vport;
}

/*
 * FDISC Response
 */
void
bfa_cb_lps_fdisc_comp(void *bfad, void *uarg, bfa_status_t status)
{
	struct bfa_fcs_vport_s *vport = uarg;

	bfa_trc(__vport_fcs(vport), __vport_pwwn(vport));
	bfa_trc(__vport_fcs(vport), status);

	switch (status) {
	case BFA_STATUS_OK:
		/*
		 * Initialize the V-Port fields
		 */
		__vport_fcid(vport) = vport->lps->lp_pid;
		vport->vport_stats.fdisc_accepts++;
		bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_OK);
		break;

	case BFA_STATUS_INVALID_MAC:
		/* Only for CNA */
		vport->vport_stats.fdisc_acc_bad++;
		bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_ERROR);

		break;

	case BFA_STATUS_EPROTOCOL:
		switch (vport->lps->ext_status) {
		case BFA_EPROTO_BAD_ACCEPT:
			vport->vport_stats.fdisc_acc_bad++;
			break;

		case BFA_EPROTO_UNKNOWN_RSP:
			vport->vport_stats.fdisc_unknown_rsp++;
			break;

		default:
			break;
		}

		bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_ERROR);
		break;

	case BFA_STATUS_FABRIC_RJT:
		vport->vport_stats.fdisc_rejects++;
		bfa_fcs_vport_fdisc_rejected(vport);
		break;

	default:
		vport->vport_stats.fdisc_rsp_err++;
		bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_ERROR);
	}
}

/*
 * LOGO response
 */
void
bfa_cb_lps_fdisclogo_comp(void *bfad, void *uarg)
{
	struct bfa_fcs_vport_s *vport = uarg;
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_RSP_OK);
}

/*
 * Received clear virtual link
 */
void
bfa_cb_lps_cvl_event(void *bfad, void *uarg)
{
	struct bfa_fcs_vport_s *vport = uarg;

	/* Send an Offline followed by an ONLINE */
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_OFFLINE);
	bfa_sm_send_event(vport, BFA_FCS_VPORT_SM_ONLINE);
}
