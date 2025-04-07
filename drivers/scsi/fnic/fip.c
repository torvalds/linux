// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */
#include "fnic.h"
#include "fip.h"
#include <linux/etherdevice.h>

#define FIP_FNIC_RESET_WAIT_COUNT 15

/**
 * fnic_fcoe_reset_vlans - Free up the list of discovered vlans
 * @fnic: Handle to fnic driver instance
 */
void fnic_fcoe_reset_vlans(struct fnic *fnic)
{
	unsigned long flags;
	struct fcoe_vlan *vlan, *next;

	spin_lock_irqsave(&fnic->vlans_lock, flags);
	if (!list_empty(&fnic->vlan_list)) {
		list_for_each_entry_safe(vlan, next, &fnic->vlan_list, list) {
			list_del(&vlan->list);
			kfree(vlan);
		}
	}

	spin_unlock_irqrestore(&fnic->vlans_lock, flags);
	FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "Reset vlan complete\n");
}

/**
 * fnic_fcoe_send_vlan_req - Send FIP vlan request to all FCFs MAC
 * @fnic: Handle to fnic driver instance
 */
void fnic_fcoe_send_vlan_req(struct fnic *fnic)
{
	uint8_t *frame;
	struct fnic_iport_s *iport = &fnic->iport;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	u64 vlan_tov;
	struct fip_vlan_req *pvlan_req;
	uint16_t frame_size = sizeof(struct fip_vlan_req);

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FIP_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
		     "Failed to allocate frame to send VLAN req");
		return;
	}

	fnic_fcoe_reset_vlans(fnic);

	fnic->set_vlan(fnic, 0);
	FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "set vlan done\n");

	FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "got MAC 0x%x:%x:%x:%x:%x:%x\n", iport->hwmac[0],
		     iport->hwmac[1], iport->hwmac[2], iport->hwmac[3],
		     iport->hwmac[4], iport->hwmac[5]);

	pvlan_req = (struct fip_vlan_req *) frame;
	*pvlan_req = (struct fip_vlan_req) {
		.eth = {.h_dest = FCOE_ALL_FCFS_MAC,
			.h_proto = cpu_to_be16(ETH_P_FIP)},
		.fip = {.fip_ver = FIP_VER_ENCAPS(FIP_VER),
			.fip_op = cpu_to_be16(FIP_OP_VLAN),
			.fip_subcode = FIP_SC_REQ,
			.fip_dl_len = cpu_to_be16(FIP_VLAN_REQ_LEN)},
		.mac_desc = {.fd_desc = {.fip_dtype = FIP_DT_MAC,
						.fip_dlen = 2}}
	};

	memcpy(pvlan_req->eth.h_source, iport->hwmac, ETH_ALEN);
	memcpy(pvlan_req->mac_desc.fd_mac, iport->hwmac, ETH_ALEN);

	atomic64_inc(&fnic_stats->vlan_stats.vlan_disc_reqs);

	iport->fip.state = FDLS_FIP_VLAN_DISCOVERY_STARTED;

	FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "Send VLAN req\n");
	fnic_send_fip_frame(iport, frame, frame_size);

	vlan_tov = jiffies + msecs_to_jiffies(FCOE_CTLR_FIPVLAN_TOV);
	mod_timer(&fnic->retry_fip_timer, round_jiffies(vlan_tov));
	FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "fip timer set\n");
}

/**
 * fnic_fcoe_process_vlan_resp - Processes the vlan response from one FCF and
 * populates VLAN list.
 * @fnic: Handle to fnic driver instance
 * @fiph: Received FIP frame
 *
 * Will wait for responses from multiple FCFs until timeout.
 */
void fnic_fcoe_process_vlan_resp(struct fnic *fnic, struct fip_header *fiph)
{
	struct fip_vlan_notif *vlan_notif = (struct fip_vlan_notif *)fiph;

	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	u16 vid;
	int num_vlan = 0;
	int cur_desc, desc_len;
	struct fcoe_vlan *vlan;
	struct fip_vlan_desc *vlan_desc;
	unsigned long flags;

	FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "fnic 0x%p got vlan resp\n", fnic);

	desc_len = be16_to_cpu(vlan_notif->fip.fip_dl_len);
	FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "desc_len %d\n", desc_len);

	spin_lock_irqsave(&fnic->vlans_lock, flags);

	cur_desc = 0;
	while (desc_len > 0) {
		vlan_desc =
		    (struct fip_vlan_desc *)(((char *)vlan_notif->vlans_desc)
					       + cur_desc * 4);

		if (vlan_desc->fd_desc.fip_dtype == FIP_DT_VLAN) {
			if (vlan_desc->fd_desc.fip_dlen != 1) {
				FNIC_FIP_DBG(KERN_INFO, fnic->host,
					     fnic->fnic_num,
					     "Invalid descriptor length(%x) in VLan response\n",
					     vlan_desc->fd_desc.fip_dlen);

			}
			num_vlan++;
			vid = be16_to_cpu(vlan_desc->fd_vlan);
			FNIC_FIP_DBG(KERN_INFO, fnic->host,
				     fnic->fnic_num,
				     "process_vlan_resp: FIP VLAN %d\n", vid);
			vlan = kzalloc(sizeof(*vlan), GFP_KERNEL);

			if (!vlan) {
				/* retry from timer */
				FNIC_FIP_DBG(KERN_INFO, fnic->host,
					     fnic->fnic_num,
					     "Mem Alloc failure\n");
				spin_unlock_irqrestore(&fnic->vlans_lock,
						       flags);
				goto out;
			}
			vlan->vid = vid & 0x0fff;
			vlan->state = FIP_VLAN_AVAIL;
			list_add_tail(&vlan->list, &fnic->vlan_list);
			break;
		}
		FNIC_FIP_DBG(KERN_INFO, fnic->host,
			     fnic->fnic_num,
			     "Invalid descriptor type(%x) in VLan response\n",
			     vlan_desc->fd_desc.fip_dtype);
		/*
		 * Note : received a type=2 descriptor here i.e. FIP
		 * MAC Address Descriptor
		 */
		cur_desc += vlan_desc->fd_desc.fip_dlen;
		desc_len -= vlan_desc->fd_desc.fip_dlen;
	}

	/* any VLAN descriptors present ? */
	if (num_vlan == 0) {
		atomic64_inc(&fnic_stats->vlan_stats.resp_withno_vlanID);
		FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "fnic 0x%p No VLAN descriptors in FIP VLAN response\n",
			     fnic);
	}

	spin_unlock_irqrestore(&fnic->vlans_lock, flags);

 out:
	return;
}

/**
 * fnic_fcoe_start_fcf_discovery - Start FIP FCF discovery in a selected vlan
 * @fnic: Handle to fnic driver instance
 */
void fnic_fcoe_start_fcf_discovery(struct fnic *fnic)
{
	uint8_t *frame;
	struct fnic_iport_s *iport = &fnic->iport;
	u64 fcs_tov;
	struct fip_discovery *pdisc_sol;
	uint16_t frame_size = sizeof(struct fip_discovery);

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FIP_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
		     "Failed to allocate frame to start FCF discovery");
		return;
	}

	memset(iport->selected_fcf.fcf_mac, 0, ETH_ALEN);

	pdisc_sol = (struct fip_discovery *) frame;
	*pdisc_sol = (struct fip_discovery) {
		.eth = {.h_dest = FCOE_ALL_FCFS_MAC,
			.h_proto = cpu_to_be16(ETH_P_FIP)},
		.fip = {
			.fip_ver = FIP_VER_ENCAPS(FIP_VER), .fip_op = cpu_to_be16(FIP_OP_DISC),
			.fip_subcode = FIP_SC_REQ, .fip_dl_len = cpu_to_be16(FIP_DISC_SOL_LEN),
			.fip_flags = cpu_to_be16(FIP_FL_FPMA)},
		.mac_desc = {.fd_desc = {.fip_dtype = FIP_DT_MAC, .fip_dlen = 2}},
		.name_desc = {.fd_desc = {.fip_dtype = FIP_DT_NAME, .fip_dlen = 3}},
		.fcoe_desc = {.fd_desc = {.fip_dtype = FIP_DT_FCOE_SIZE, .fip_dlen = 1},
			      .fd_size = cpu_to_be16(FCOE_MAX_SIZE)}
	};

	memcpy(pdisc_sol->eth.h_source, iport->hwmac, ETH_ALEN);
	memcpy(pdisc_sol->mac_desc.fd_mac, iport->hwmac, ETH_ALEN);
	iport->selected_fcf.fcf_priority = 0xFF;

	FNIC_STD_SET_NODE_NAME(&pdisc_sol->name_desc.fd_wwn, iport->wwnn);

	FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "Start FCF discovery\n");
	fnic_send_fip_frame(iport, frame, frame_size);

	iport->fip.state = FDLS_FIP_FCF_DISCOVERY_STARTED;

	fcs_tov = jiffies + msecs_to_jiffies(FCOE_CTLR_FCS_TOV);
	mod_timer(&fnic->retry_fip_timer, round_jiffies(fcs_tov));
}

/**
 * fnic_fcoe_fip_discovery_resp - Processes FCF advertisements.
 * @fnic: Handle to fnic driver instance
 * @fiph: Received frame
 *
 * FCF advertisements can be:
 * solicited - Sent in response of a discover FCF FIP request
 * Store the information of the FCF with highest priority.
 * Wait until timeout in case of multiple FCFs.
 *
 * unsolicited - Sent periodically by the FCF for keep alive.
 * If FLOGI is in progress or completed and the advertisement is
 * received by our selected FCF, refresh the keep alive timer.
 */
void fnic_fcoe_fip_discovery_resp(struct fnic *fnic, struct fip_header *fiph)
{
	struct fnic_iport_s *iport = &fnic->iport;
	struct fip_disc_adv *disc_adv = (struct fip_disc_adv *)fiph;
	u64 fcs_ka_tov;
	u64 tov;
	int fka_has_changed;

	switch (iport->fip.state) {
	case FDLS_FIP_FCF_DISCOVERY_STARTED:
		if (be16_to_cpu(disc_adv->fip.fip_flags) & FIP_FL_SOL) {
			FNIC_FIP_DBG(KERN_INFO, fnic->host,
				     fnic->fnic_num,
				     "fnic 0x%p Solicited adv\n", fnic);

			if ((disc_adv->prio_desc.fd_pri <
			     iport->selected_fcf.fcf_priority)
			    && (be16_to_cpu(disc_adv->fip.fip_flags) & FIP_FL_AVAIL)) {

				FNIC_FIP_DBG(KERN_INFO, fnic->host,
					     fnic->fnic_num,
					     "fnic 0x%p FCF Available\n", fnic);
				memcpy(iport->selected_fcf.fcf_mac,
				       disc_adv->mac_desc.fd_mac, ETH_ALEN);
				iport->selected_fcf.fcf_priority =
				    disc_adv->prio_desc.fd_pri;
				iport->selected_fcf.fka_adv_period =
				    be32_to_cpu(disc_adv->fka_adv_desc.fd_fka_period);
				FNIC_FIP_DBG(KERN_INFO, fnic->host,
					     fnic->fnic_num, "adv time %d",
					     iport->selected_fcf.fka_adv_period);
				iport->selected_fcf.ka_disabled =
				    (disc_adv->fka_adv_desc.fd_flags & 1);
			}
		}
		break;
	case FDLS_FIP_FLOGI_STARTED:
	case FDLS_FIP_FLOGI_COMPLETE:
		if (!(be16_to_cpu(disc_adv->fip.fip_flags) & FIP_FL_SOL)) {
			/* same fcf */
			if (memcmp
			    (iport->selected_fcf.fcf_mac,
			     disc_adv->mac_desc.fd_mac, ETH_ALEN) == 0) {
				if (iport->selected_fcf.fka_adv_period !=
				    be32_to_cpu(disc_adv->fka_adv_desc.fd_fka_period)) {
					iport->selected_fcf.fka_adv_period =
					    be32_to_cpu(disc_adv->fka_adv_desc.fd_fka_period);
					FNIC_FIP_DBG(KERN_INFO,
						     fnic->host,
						     fnic->fnic_num,
						     "change fka to %d",
						     iport->selected_fcf.fka_adv_period);
				}

				fka_has_changed =
				    (iport->selected_fcf.ka_disabled == 1)
				    && ((disc_adv->fka_adv_desc.fd_flags & 1) ==
					0);

				iport->selected_fcf.ka_disabled =
				    (disc_adv->fka_adv_desc.fd_flags & 1);
				if (!((iport->selected_fcf.ka_disabled)
				      || (iport->selected_fcf.fka_adv_period ==
					  0))) {

					fcs_ka_tov = jiffies
					    + 3
					    *
					    msecs_to_jiffies(iport->selected_fcf.fka_adv_period);
					mod_timer(&fnic->fcs_ka_timer,
						  round_jiffies(fcs_ka_tov));
				} else {
					if (timer_pending(&fnic->fcs_ka_timer))
						timer_delete_sync(&fnic->fcs_ka_timer);
				}

				if (fka_has_changed) {
					if (iport->selected_fcf.fka_adv_period != 0) {
						tov =
						 jiffies +
						 msecs_to_jiffies(
							 iport->selected_fcf.fka_adv_period);
						mod_timer(&fnic->enode_ka_timer,
							  round_jiffies(tov));

						tov =
						    jiffies +
						    msecs_to_jiffies
						    (FIP_VN_KA_PERIOD);
						mod_timer(&fnic->vn_ka_timer,
							  round_jiffies(tov));
					}
				}
			}
		}
		break;
	default:
		break;
	}			/* end switch */
}

/**
 * fnic_fcoe_start_flogi - Send FIP FLOGI to the selected FCF
 * @fnic: Handle to fnic driver instance
 */
void fnic_fcoe_start_flogi(struct fnic *fnic)
{
	uint8_t *frame;
	struct fnic_iport_s *iport = &fnic->iport;
	struct fip_flogi *pflogi_req;
	u64 flogi_tov;
	uint16_t oxid;
	uint16_t frame_size = sizeof(struct fip_flogi);

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FIP_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
		     "Failed to allocate frame to start FIP FLOGI");
		return;
	}

	pflogi_req = (struct fip_flogi *) frame;
	*pflogi_req = (struct fip_flogi) {
		.eth = {
			.h_proto = cpu_to_be16(ETH_P_FIP)},
		.fip = {
			.fip_ver = FIP_VER_ENCAPS(FIP_VER),
			.fip_op = cpu_to_be16(FIP_OP_LS),
			.fip_subcode = FIP_SC_REQ,
			.fip_dl_len = cpu_to_be16(FIP_FLOGI_LEN),
			.fip_flags = cpu_to_be16(FIP_FL_FPMA)},
		.flogi_desc = {
				.fd_desc = {.fip_dtype = FIP_DT_FLOGI, .fip_dlen = 36},
			       .flogi = {
					 .fchdr = {
						   .fh_r_ctl = FC_RCTL_ELS_REQ,
						   .fh_d_id = {0xFF, 0xFF, 0xFE},
						   .fh_type = FC_TYPE_ELS,
						   .fh_f_ctl = {FNIC_ELS_REQ_FCTL, 0, 0},
						   .fh_rx_id = cpu_to_be16(FNIC_UNASSIGNED_RXID)},
					 .els = {
						 .fl_cmd = ELS_FLOGI,
						 .fl_csp = {
							    .sp_hi_ver =
							    FNIC_FC_PH_VER_HI,
							    .sp_lo_ver =
							    FNIC_FC_PH_VER_LO,
							    .sp_bb_cred =
							    cpu_to_be16
							    (FNIC_FC_B2B_CREDIT),
							    .sp_bb_data =
							    cpu_to_be16
							    (FNIC_FC_B2B_RDF_SZ)},
						 .fl_cssp[2].cp_class =
						 cpu_to_be16(FC_CPC_VALID | FC_CPC_SEQ)
						},
					}
			},
		.mac_desc = {.fd_desc = {.fip_dtype = FIP_DT_MAC, .fip_dlen = 2}}
	};

	memcpy(pflogi_req->eth.h_source, iport->hwmac, ETH_ALEN);
	if (iport->usefip)
		memcpy(pflogi_req->eth.h_dest, iport->selected_fcf.fcf_mac,
		       ETH_ALEN);

	oxid = fdls_alloc_oxid(iport, FNIC_FRAME_TYPE_FABRIC_FLOGI,
		&iport->active_oxid_fabric_req);
	if (oxid == FNIC_UNASSIGNED_OXID) {
		FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "Failed to allocate OXID to send FIP FLOGI");
		mempool_free(frame, fnic->frame_pool);
		return;
	}
	FNIC_STD_SET_OX_ID(pflogi_req->flogi_desc.flogi.fchdr, oxid);

	FNIC_STD_SET_NPORT_NAME(&pflogi_req->flogi_desc.flogi.els.fl_wwpn,
			iport->wwpn);
	FNIC_STD_SET_NODE_NAME(&pflogi_req->flogi_desc.flogi.els.fl_wwnn,
			iport->wwnn);

	FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "FIP start FLOGI\n");
	fnic_send_fip_frame(iport, frame, frame_size);
	iport->fip.flogi_retry++;

	iport->fip.state = FDLS_FIP_FLOGI_STARTED;
	flogi_tov = jiffies + msecs_to_jiffies(fnic->config.flogi_timeout);
	mod_timer(&fnic->retry_fip_timer, round_jiffies(flogi_tov));
}

/**
 * fnic_fcoe_process_flogi_resp - Processes FLOGI response from FCF.
 * @fnic: Handle to fnic driver instance
 * @fiph: Received frame
 *
 * If successful save assigned fc_id and MAC, program firmware
 * and start fdls discovery, else restart vlan discovery.
 */
void fnic_fcoe_process_flogi_resp(struct fnic *fnic, struct fip_header *fiph)
{
	struct fnic_iport_s *iport = &fnic->iport;
	struct fip_flogi_rsp *flogi_rsp = (struct fip_flogi_rsp *)fiph;
	int desc_len;
	uint32_t s_id;
	int frame_type;
	uint16_t oxid;

	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	struct fc_frame_header *fchdr = &flogi_rsp->rsp_desc.flogi.fchdr;

	FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "fnic 0x%p FIP FLOGI rsp\n", fnic);
	desc_len = be16_to_cpu(flogi_rsp->fip.fip_dl_len);
	if (desc_len != 38) {
		FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "Invalid Descriptor List len (%x). Dropping frame\n",
			     desc_len);
		return;
	}

	if (!((flogi_rsp->rsp_desc.fd_desc.fip_dtype == 7)
	      && (flogi_rsp->rsp_desc.fd_desc.fip_dlen == 36))
	    || !((flogi_rsp->mac_desc.fd_desc.fip_dtype == 2)
		 && (flogi_rsp->mac_desc.fd_desc.fip_dlen == 2))) {
		FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "Dropping frame invalid type and len mix\n");
		return;
	}

	frame_type = fnic_fdls_validate_and_get_frame_type(iport, fchdr);

	s_id = ntoh24(fchdr->fh_s_id);
	if ((fchdr->fh_f_ctl[0] != 0x98)
	    || (fchdr->fh_r_ctl != 0x23)
	    || (s_id != FC_FID_FLOGI)
	    || (frame_type != FNIC_FABRIC_FLOGI_RSP)
	    || (fchdr->fh_type != 0x01)) {
		FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "Dropping invalid frame: s_id %x F %x R %x t %x OX_ID %x\n",
			     s_id, fchdr->fh_f_ctl[0], fchdr->fh_r_ctl,
			     fchdr->fh_type, FNIC_STD_GET_OX_ID(fchdr));
		return;
	}

	if (iport->fip.state == FDLS_FIP_FLOGI_STARTED) {
		FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "fnic 0x%p rsp for pending FLOGI\n", fnic);

		oxid = FNIC_STD_GET_OX_ID(fchdr);
		fdls_free_oxid(iport, oxid, &iport->active_oxid_fabric_req);
		timer_delete_sync(&fnic->retry_fip_timer);

		if ((be16_to_cpu(flogi_rsp->fip.fip_dl_len) == FIP_FLOGI_LEN)
		    && (flogi_rsp->rsp_desc.flogi.els.fl_cmd == ELS_LS_ACC)) {

			FNIC_FIP_DBG(KERN_INFO, fnic->host,
				     fnic->fnic_num,
				     "fnic 0x%p FLOGI success\n", fnic);
			memcpy(iport->fpma, flogi_rsp->mac_desc.fd_mac, ETH_ALEN);
			iport->fcid =
			    ntoh24(flogi_rsp->rsp_desc.flogi.fchdr.fh_d_id);

			iport->r_a_tov =
			    be32_to_cpu(flogi_rsp->rsp_desc.flogi.els.fl_csp.sp_r_a_tov);
			iport->e_d_tov =
			    be32_to_cpu(flogi_rsp->rsp_desc.flogi.els.fl_csp.sp_e_d_tov);
			memcpy(fnic->iport.fcfmac, iport->selected_fcf.fcf_mac,
			       ETH_ALEN);
			vnic_dev_add_addr(fnic->vdev, flogi_rsp->mac_desc.fd_mac);

			if (fnic_fdls_register_portid(iport, iport->fcid, NULL)
			    != 0) {
				FNIC_FIP_DBG(KERN_INFO, fnic->host,
					     fnic->fnic_num,
					     "fnic 0x%p flogi registration failed\n",
					     fnic);
				return;
			}

			iport->fip.state = FDLS_FIP_FLOGI_COMPLETE;
			iport->state = FNIC_IPORT_STATE_FABRIC_DISC;
			FNIC_FIP_DBG(KERN_INFO, fnic->host,
				     fnic->fnic_num, "iport->state:%d\n",
				     iport->state);
			fnic_fdls_disc_start(iport);
			if (!((iport->selected_fcf.ka_disabled)
			      || (iport->selected_fcf.fka_adv_period == 0))) {
				u64 tov;

				tov = jiffies
				    +
				    msecs_to_jiffies(iport->selected_fcf.fka_adv_period);
				mod_timer(&fnic->enode_ka_timer,
					  round_jiffies(tov));

				tov =
				    jiffies +
				    msecs_to_jiffies(FIP_VN_KA_PERIOD);
				mod_timer(&fnic->vn_ka_timer,
					  round_jiffies(tov));

			}
		} else {
			/*
			 * If there's FLOGI rejects - clear all
			 * fcf's & restart from scratch
			 */
			atomic64_inc(&fnic_stats->vlan_stats.flogi_rejects);
			/* start FCoE VLAN discovery */
			fnic_fcoe_send_vlan_req(fnic);

			iport->fip.state = FDLS_FIP_VLAN_DISCOVERY_STARTED;
		}
	}
}

/**
 * fnic_common_fip_cleanup - Clean up FCF info and timers in case of
 * link down/CVL
 * @fnic: Handle to fnic driver instance
 */
void fnic_common_fip_cleanup(struct fnic *fnic)
{

	struct fnic_iport_s *iport = &fnic->iport;

	if (!iport->usefip)
		return;
	FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "fnic 0x%p fip cleanup\n", fnic);

	iport->fip.state = FDLS_FIP_INIT;

	timer_delete_sync(&fnic->retry_fip_timer);
	timer_delete_sync(&fnic->fcs_ka_timer);
	timer_delete_sync(&fnic->enode_ka_timer);
	timer_delete_sync(&fnic->vn_ka_timer);

	if (!is_zero_ether_addr(iport->fpma))
		vnic_dev_del_addr(fnic->vdev, iport->fpma);

	memset(iport->fpma, 0, ETH_ALEN);
	iport->fcid = 0;
	iport->r_a_tov = 0;
	iport->e_d_tov = 0;
	memset(fnic->iport.fcfmac, 0, ETH_ALEN);
	memset(iport->selected_fcf.fcf_mac, 0, ETH_ALEN);
	iport->selected_fcf.fcf_priority = 0;
	iport->selected_fcf.fka_adv_period = 0;
	iport->selected_fcf.ka_disabled = 0;

	fnic_fcoe_reset_vlans(fnic);
}

/**
 * fnic_fcoe_process_cvl - Processes Clear Virtual Link from FCF.
 * @fnic: Handle to fnic driver instance
 * @fiph: Received frame
 *
 * Verify that cvl is received from our current FCF for our assigned MAC
 * and clean up and restart the vlan discovery.
 */
void fnic_fcoe_process_cvl(struct fnic *fnic, struct fip_header *fiph)
{
	struct fnic_iport_s *iport = &fnic->iport;
	struct fip_cvl *cvl_msg = (struct fip_cvl *)fiph;
	int i;
	int found = false;
	int max_count = 0;

	FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "fnic 0x%p clear virtual link handler\n", fnic);

	if (!((cvl_msg->fcf_mac_desc.fd_desc.fip_dtype == 2)
	      && (cvl_msg->fcf_mac_desc.fd_desc.fip_dlen == 2))
	    || !((cvl_msg->name_desc.fd_desc.fip_dtype == 4)
		 && (cvl_msg->name_desc.fd_desc.fip_dlen == 3))) {

		FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "invalid mix: ft %x fl %x ndt %x ndl %x",
			     cvl_msg->fcf_mac_desc.fd_desc.fip_dtype,
			     cvl_msg->fcf_mac_desc.fd_desc.fip_dlen,
				 cvl_msg->name_desc.fd_desc.fip_dtype,
			     cvl_msg->name_desc.fd_desc.fip_dlen);
	}

	if (memcmp
	    (iport->selected_fcf.fcf_mac, cvl_msg->fcf_mac_desc.fd_mac, ETH_ALEN)
	    == 0) {
		for (i = 0; i < ((be16_to_cpu(fiph->fip_dl_len) / 5) - 1); i++) {
			if (!((cvl_msg->vn_ports_desc[i].fd_desc.fip_dtype == 11)
			      && (cvl_msg->vn_ports_desc[i].fd_desc.fip_dlen == 5))) {

				FNIC_FIP_DBG(KERN_INFO, fnic->host,
					     fnic->fnic_num,
					     "Invalid type and len mix type: %d len: %d\n",
					     cvl_msg->vn_ports_desc[i].fd_desc.fip_dtype,
					     cvl_msg->vn_ports_desc[i].fd_desc.fip_dlen);
			}
			if (memcmp
			    (iport->fpma, cvl_msg->vn_ports_desc[i].fd_mac,
			     ETH_ALEN) == 0) {
				found = true;
				break;
			}
		}
		if (!found)
			return;
		fnic_common_fip_cleanup(fnic);

		while (fnic->reset_in_progress == IN_PROGRESS) {
			spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);
			wait_for_completion_timeout(&fnic->reset_completion_wait,
							msecs_to_jiffies(5000));
			spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
			max_count++;
			if (max_count >= FIP_FNIC_RESET_WAIT_COUNT) {
				FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
					 "Rthr waited too long. Skipping handle link event %p\n",
					 fnic);
				return;
			}
			FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
				 "fnic reset in progress. Link event needs to wait %p",
				 fnic);
		}
		fnic->reset_in_progress = IN_PROGRESS;
		fnic_fdls_link_down(iport);
		fnic->reset_in_progress = NOT_IN_PROGRESS;
		complete(&fnic->reset_completion_wait);
		fnic_fcoe_send_vlan_req(fnic);
	}
}

/**
 * fdls_fip_recv_frame - Demultiplexer for FIP frames
 * @fnic: Handle to fnic driver instance
 * @frame: Received ethernet frame
 */
int fdls_fip_recv_frame(struct fnic *fnic, void *frame)
{
	struct ethhdr *eth = (struct ethhdr *)frame;
	struct fip_header *fiph;
	u16 op;
	u8 sub;
	int len = 2048;

	if (be16_to_cpu(eth->h_proto) == ETH_P_FIP) {
		fiph = (struct fip_header *)(eth + 1);
		op = be16_to_cpu(fiph->fip_op);
		sub = fiph->fip_subcode;

		fnic_debug_dump_fip_frame(fnic, eth, len, "Incoming");

		if (op == FIP_OP_DISC && sub == FIP_SC_REP)
			fnic_fcoe_fip_discovery_resp(fnic, fiph);
		else if (op == FIP_OP_VLAN && sub == FIP_SC_REP)
			fnic_fcoe_process_vlan_resp(fnic, fiph);
		else if (op == FIP_OP_CTRL && sub == FIP_SC_REP)
			fnic_fcoe_process_cvl(fnic, fiph);
		else if (op == FIP_OP_LS && sub == FIP_SC_REP)
			fnic_fcoe_process_flogi_resp(fnic, fiph);

		/* Return true if the frame was a FIP frame */
		return true;
	}

	FNIC_FCS_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		"Not a FIP Frame");
	return false;
}

void fnic_work_on_fip_timer(struct work_struct *work)
{
	struct fnic *fnic = container_of(work, struct fnic, fip_timer_work);
	struct fnic_iport_s *iport = &fnic->iport;

	FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "FIP timeout\n");

	if (iport->fip.state == FDLS_FIP_VLAN_DISCOVERY_STARTED) {
		fnic_vlan_discovery_timeout(fnic);
	} else if (iport->fip.state == FDLS_FIP_FCF_DISCOVERY_STARTED) {
		u8 zmac[ETH_ALEN] = { 0, 0, 0, 0, 0, 0 };

		FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "FCF Discovery timeout\n");
		if (memcmp(iport->selected_fcf.fcf_mac, zmac, ETH_ALEN) != 0) {

			if (iport->flags & FNIC_FIRST_LINK_UP) {
				fnic_scsi_fcpio_reset(iport->fnic);
				iport->flags &= ~FNIC_FIRST_LINK_UP;
			}

			fnic_fcoe_start_flogi(fnic);
			if (!((iport->selected_fcf.ka_disabled)
			      || (iport->selected_fcf.fka_adv_period == 0))) {
				u64 fcf_tov;

				fcf_tov = jiffies
				    + 3
				    *
				    msecs_to_jiffies(iport->selected_fcf.fka_adv_period);
				mod_timer(&fnic->fcs_ka_timer,
					  round_jiffies(fcf_tov));
			}
		} else {
			FNIC_FIP_DBG(KERN_INFO, fnic->host,
				     fnic->fnic_num, "FCF Discovery timeout\n");
			fnic_vlan_discovery_timeout(fnic);
		}
	} else if (iport->fip.state == FDLS_FIP_FLOGI_STARTED) {
		fdls_schedule_oxid_free(iport, &iport->active_oxid_fabric_req);
		FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
			     "FLOGI timeout\n");
		if (iport->fip.flogi_retry < fnic->config.flogi_retries)
			fnic_fcoe_start_flogi(fnic);
		else
			fnic_vlan_discovery_timeout(fnic);
	}
}

/**
 * fnic_handle_fip_timer - Timeout handler for FIP discover phase.
 * @t: Handle to the timer list
 *
 * Based on the current state, start next phase or restart discovery.
 */
void fnic_handle_fip_timer(struct timer_list *t)
{
	struct fnic *fnic = from_timer(fnic, t, retry_fip_timer);

	INIT_WORK(&fnic->fip_timer_work, fnic_work_on_fip_timer);
	queue_work(fnic_fip_queue, &fnic->fip_timer_work);
}

/**
 * fnic_handle_enode_ka_timer - FIP node keep alive.
 * @t: Handle to the timer list
 */
void fnic_handle_enode_ka_timer(struct timer_list *t)
{
	uint8_t *frame;
	struct fnic *fnic = from_timer(fnic, t, enode_ka_timer);

	struct fnic_iport_s *iport = &fnic->iport;
	struct fip_enode_ka *penode_ka;
	u64 enode_ka_tov;
	uint16_t frame_size = sizeof(struct fip_enode_ka);

	if (iport->fip.state != FDLS_FIP_FLOGI_COMPLETE)
		return;

	if ((iport->selected_fcf.ka_disabled)
	    || (iport->selected_fcf.fka_adv_period == 0)) {
		return;
	}

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FIP_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
		     "Failed to allocate frame to send enode ka");
		return;
	}

	penode_ka = (struct fip_enode_ka *) frame;
	*penode_ka = (struct fip_enode_ka) {
		.eth = {
			.h_proto = cpu_to_be16(ETH_P_FIP)},
		.fip = {
			.fip_ver = FIP_VER_ENCAPS(FIP_VER),
			.fip_op = cpu_to_be16(FIP_OP_CTRL),
			.fip_subcode = FIP_SC_REQ,
			.fip_dl_len = cpu_to_be16(FIP_ENODE_KA_LEN)},
		.mac_desc = {.fd_desc = {.fip_dtype = FIP_DT_MAC, .fip_dlen = 2}}
	};

	memcpy(penode_ka->eth.h_source, iport->hwmac, ETH_ALEN);
	memcpy(penode_ka->eth.h_dest, iport->selected_fcf.fcf_mac, ETH_ALEN);
	memcpy(penode_ka->mac_desc.fd_mac, iport->hwmac, ETH_ALEN);

	FNIC_FIP_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
		     "Handle enode KA timer\n");
	fnic_send_fip_frame(iport, frame, frame_size);
	enode_ka_tov = jiffies
	    + msecs_to_jiffies(iport->selected_fcf.fka_adv_period);
	mod_timer(&fnic->enode_ka_timer, round_jiffies(enode_ka_tov));
}

/**
 * fnic_handle_vn_ka_timer - FIP virtual port keep alive.
 * @t: Handle to the timer list
 */
void fnic_handle_vn_ka_timer(struct timer_list *t)
{
	uint8_t *frame;
	struct fnic *fnic = from_timer(fnic, t, vn_ka_timer);

	struct fnic_iport_s *iport = &fnic->iport;
	struct fip_vn_port_ka *pvn_port_ka;
	u64 vn_ka_tov;
	uint8_t fcid[3];
	uint16_t frame_size = sizeof(struct fip_vn_port_ka);

	if (iport->fip.state != FDLS_FIP_FLOGI_COMPLETE)
		return;

	if ((iport->selected_fcf.ka_disabled)
	    || (iport->selected_fcf.fka_adv_period == 0)) {
		return;
	}

	frame = fdls_alloc_frame(iport);
	if (frame == NULL) {
		FNIC_FIP_DBG(KERN_ERR, fnic->host, fnic->fnic_num,
		     "Failed to allocate frame to send vn ka");
		return;
	}

	pvn_port_ka = (struct fip_vn_port_ka *) frame;
	*pvn_port_ka = (struct fip_vn_port_ka) {
		.eth = {
			.h_proto = cpu_to_be16(ETH_P_FIP)},
		.fip = {
			.fip_ver = FIP_VER_ENCAPS(FIP_VER),
			.fip_op = cpu_to_be16(FIP_OP_CTRL),
			.fip_subcode = FIP_SC_REQ,
			.fip_dl_len = cpu_to_be16(FIP_VN_KA_LEN)},
		.mac_desc = {.fd_desc = {.fip_dtype = FIP_DT_MAC, .fip_dlen = 2}},
		.vn_port_desc = {.fd_desc = {.fip_dtype = FIP_DT_VN_ID, .fip_dlen = 5}}
	};

	memcpy(pvn_port_ka->eth.h_source, iport->fpma, ETH_ALEN);
	memcpy(pvn_port_ka->eth.h_dest, iport->selected_fcf.fcf_mac, ETH_ALEN);
	memcpy(pvn_port_ka->mac_desc.fd_mac, iport->hwmac, ETH_ALEN);
	memcpy(pvn_port_ka->vn_port_desc.fd_mac, iport->fpma, ETH_ALEN);
	hton24(fcid, iport->fcid);
	memcpy(pvn_port_ka->vn_port_desc.fd_fc_id, fcid, 3);
	FNIC_STD_SET_NPORT_NAME(&pvn_port_ka->vn_port_desc.fd_wwpn, iport->wwpn);

	FNIC_FIP_DBG(KERN_DEBUG, fnic->host, fnic->fnic_num,
		     "Handle vnport KA timer\n");
	fnic_send_fip_frame(iport, frame, frame_size);
	vn_ka_tov = jiffies + msecs_to_jiffies(FIP_VN_KA_PERIOD);
	mod_timer(&fnic->vn_ka_timer, round_jiffies(vn_ka_tov));
}

/**
 * fnic_vlan_discovery_timeout - Handle vlan discovery timeout
 * @fnic: Handle to fnic driver instance
 *
 * End of VLAN discovery or FCF discovery time window.
 * Start the FCF discovery if VLAN was never used.
 */
void fnic_vlan_discovery_timeout(struct fnic *fnic)
{
	struct fcoe_vlan *vlan;
	struct fnic_iport_s *iport = &fnic->iport;
	struct fnic_stats *fnic_stats = &fnic->fnic_stats;
	unsigned long flags;

	spin_lock_irqsave(&fnic->fnic_lock, flags);
	if (fnic->stop_rx_link_events) {
		spin_unlock_irqrestore(&fnic->fnic_lock, flags);
		return;
	}
	spin_unlock_irqrestore(&fnic->fnic_lock, flags);

	if (!iport->usefip)
		return;

	spin_lock_irqsave(&fnic->vlans_lock, flags);
	if (list_empty(&fnic->vlan_list)) {
		/* no vlans available, try again */
		spin_unlock_irqrestore(&fnic->vlans_lock, flags);
		fnic_fcoe_send_vlan_req(fnic);
		return;
	}

	vlan = list_first_entry(&fnic->vlan_list, struct fcoe_vlan, list);

	if (vlan->state == FIP_VLAN_SENT) {
		if (vlan->sol_count >= FCOE_CTLR_MAX_SOL) {
			/*
			 * no response on this vlan, remove  from the list.
			 * Try the next vlan
			 */
			list_del(&vlan->list);
			kfree(vlan);
			vlan = NULL;
			if (list_empty(&fnic->vlan_list)) {
				/* we exhausted all vlans, restart vlan disc */
				spin_unlock_irqrestore(&fnic->vlans_lock,
						       flags);
				fnic_fcoe_send_vlan_req(fnic);
				return;
			}
			/* check the next vlan */
			vlan =
			    list_first_entry(&fnic->vlan_list, struct fcoe_vlan,
					     list);

			fnic->set_vlan(fnic, vlan->vid);
			vlan->state = FIP_VLAN_SENT;	/* sent now */

		}
		atomic64_inc(&fnic_stats->vlan_stats.sol_expiry_count);

	} else {
		fnic->set_vlan(fnic, vlan->vid);
		vlan->state = FIP_VLAN_SENT;	/* sent now */
	}
	vlan->sol_count++;
	spin_unlock_irqrestore(&fnic->vlans_lock, flags);
	fnic_fcoe_start_fcf_discovery(fnic);
}

/**
 * fnic_work_on_fcs_ka_timer - Handle work on FCS keep alive timer.
 * @work: the work queue to be serviced
 *
 * Finish handling fcs_ka_timer in process context.
 * Clean up, bring the link down, and restart all FIP discovery.
 */
void fnic_work_on_fcs_ka_timer(struct work_struct *work)
{
	struct fnic
	*fnic = container_of(work, struct fnic, fip_timer_work);
	struct fnic_iport_s *iport = &fnic->iport;

	FNIC_FIP_DBG(KERN_INFO, fnic->host, fnic->fnic_num,
		     "fnic 0x%p fcs ka timeout\n", fnic);

	fnic_common_fip_cleanup(fnic);
	spin_lock_irqsave(&fnic->fnic_lock, fnic->lock_flags);
	fnic_fdls_link_down(iport);
	iport->state = FNIC_IPORT_STATE_FIP;
	spin_unlock_irqrestore(&fnic->fnic_lock, fnic->lock_flags);

	fnic_fcoe_send_vlan_req(fnic);
}

/**
 * fnic_handle_fcs_ka_timer - Handle FCS keep alive timer.
 * @t: Handle to the timer list
 *
 * No keep alives received from FCF. Clean up, bring the link down
 * and restart all the FIP discovery.
 */
void fnic_handle_fcs_ka_timer(struct timer_list *t)
{
	struct fnic *fnic = from_timer(fnic, t, fcs_ka_timer);

	INIT_WORK(&fnic->fip_timer_work, fnic_work_on_fcs_ka_timer);
	queue_work(fnic_fip_queue, &fnic->fip_timer_work);
}
