/*
 * This file is part of the Chelsio FCoE driver for Linux.
 *
 * Copyright (c) 2008-2012 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/mm.h>
#include <linux/jiffies.h>
#include <scsi/fc/fc_fs.h>

#include "csio_init.h"

static void
csio_vport_set_state(struct csio_lnode *ln);

/*
 * csio_reg_rnode - Register a remote port with FC transport.
 * @rn: Rnode representing remote port.
 *
 * Call fc_remote_port_add() to register this remote port with FC transport.
 * If remote port is Initiator OR Target OR both, change the role appropriately.
 *
 */
void
csio_reg_rnode(struct csio_rnode *rn)
{
	struct csio_lnode *ln		= csio_rnode_to_lnode(rn);
	struct Scsi_Host *shost		= csio_ln_to_shost(ln);
	struct fc_rport_identifiers ids;
	struct fc_rport  *rport;
	struct csio_service_parms *sp;

	ids.node_name	= wwn_to_u64(csio_rn_wwnn(rn));
	ids.port_name	= wwn_to_u64(csio_rn_wwpn(rn));
	ids.port_id	= rn->nport_id;
	ids.roles	= FC_RPORT_ROLE_UNKNOWN;

	if (rn->role & CSIO_RNFR_INITIATOR || rn->role & CSIO_RNFR_TARGET) {
		rport = rn->rport;
		CSIO_ASSERT(rport != NULL);
		goto update_role;
	}

	rn->rport = fc_remote_port_add(shost, 0, &ids);
	if (!rn->rport) {
		csio_ln_err(ln, "Failed to register rport = 0x%x.\n",
					rn->nport_id);
		return;
	}

	ln->num_reg_rnodes++;
	rport = rn->rport;
	spin_lock_irq(shost->host_lock);
	*((struct csio_rnode **)rport->dd_data) = rn;
	spin_unlock_irq(shost->host_lock);

	sp = &rn->rn_sparm;
	rport->maxframe_size = ntohs(sp->csp.sp_bb_data);
	if (ntohs(sp->clsp[2].cp_class) & FC_CPC_VALID)
		rport->supported_classes = FC_COS_CLASS3;
	else
		rport->supported_classes = FC_COS_UNSPECIFIED;
update_role:
	if (rn->role & CSIO_RNFR_INITIATOR)
		ids.roles |= FC_RPORT_ROLE_FCP_INITIATOR;
	if (rn->role & CSIO_RNFR_TARGET)
		ids.roles |= FC_RPORT_ROLE_FCP_TARGET;

	if (ids.roles != FC_RPORT_ROLE_UNKNOWN)
		fc_remote_port_rolechg(rport, ids.roles);

	rn->scsi_id = rport->scsi_target_id;

	csio_ln_dbg(ln, "Remote port x%x role 0x%x registered\n",
		rn->nport_id, ids.roles);
}

/*
 * csio_unreg_rnode - Unregister a remote port with FC transport.
 * @rn: Rnode representing remote port.
 *
 * Call fc_remote_port_delete() to unregister this remote port with FC
 * transport.
 *
 */
void
csio_unreg_rnode(struct csio_rnode *rn)
{
	struct csio_lnode *ln = csio_rnode_to_lnode(rn);
	struct fc_rport *rport = rn->rport;

	rn->role &= ~(CSIO_RNFR_INITIATOR | CSIO_RNFR_TARGET);
	fc_remote_port_delete(rport);
	ln->num_reg_rnodes--;

	csio_ln_dbg(ln, "Remote port x%x un-registered\n", rn->nport_id);
}

/*
 * csio_lnode_async_event - Async events from local port.
 * @ln: lnode representing local port.
 *
 * Async events from local node that FC transport/SCSI ML
 * should be made aware of (Eg: RSCN).
 */
void
csio_lnode_async_event(struct csio_lnode *ln, enum csio_ln_fc_evt fc_evt)
{
	switch (fc_evt) {
	case CSIO_LN_FC_RSCN:
		/* Get payload of rscn from ln */
		/* For each RSCN entry */
			/*
			 * fc_host_post_event(shost,
			 *		      fc_get_event_number(),
			 *		      FCH_EVT_RSCN,
			 *		      rscn_entry);
			 */
		break;
	case CSIO_LN_FC_LINKUP:
		/* send fc_host_post_event */
		/* set vport state */
		if (csio_is_npiv_ln(ln))
			csio_vport_set_state(ln);

		break;
	case CSIO_LN_FC_LINKDOWN:
		/* send fc_host_post_event */
		/* set vport state */
		if (csio_is_npiv_ln(ln))
			csio_vport_set_state(ln);

		break;
	case CSIO_LN_FC_ATTRIB_UPDATE:
		csio_fchost_attr_init(ln);
		break;
	default:
		break;
	}
}

/*
 * csio_fchost_attr_init - Initialize FC transport attributes
 * @ln: Lnode.
 *
 */
void
csio_fchost_attr_init(struct csio_lnode *ln)
{
	struct Scsi_Host  *shost = csio_ln_to_shost(ln);

	fc_host_node_name(shost) = wwn_to_u64(csio_ln_wwnn(ln));
	fc_host_port_name(shost) = wwn_to_u64(csio_ln_wwpn(ln));

	fc_host_supported_classes(shost) = FC_COS_CLASS3;
	fc_host_max_npiv_vports(shost) =
			(csio_lnode_to_hw(ln))->fres_info.max_vnps;
	fc_host_supported_speeds(shost) = FC_PORTSPEED_10GBIT |
		FC_PORTSPEED_1GBIT;

	fc_host_maxframe_size(shost) = ntohs(ln->ln_sparm.csp.sp_bb_data);
	memset(fc_host_supported_fc4s(shost), 0,
		sizeof(fc_host_supported_fc4s(shost)));
	fc_host_supported_fc4s(shost)[7] = 1;

	memset(fc_host_active_fc4s(shost), 0,
		sizeof(fc_host_active_fc4s(shost)));
	fc_host_active_fc4s(shost)[7] = 1;
}

/*
 * csio_get_host_port_id - sysfs entries for nport_id is
 * populated/cached from this function
 */
static void
csio_get_host_port_id(struct Scsi_Host *shost)
{
	struct csio_lnode *ln	= shost_priv(shost);
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	spin_lock_irq(&hw->lock);
	fc_host_port_id(shost) = ln->nport_id;
	spin_unlock_irq(&hw->lock);
}

/*
 * csio_get_port_type - Return FC local port type.
 * @shost: scsi host.
 *
 */
static void
csio_get_host_port_type(struct Scsi_Host *shost)
{
	struct csio_lnode *ln = shost_priv(shost);
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	spin_lock_irq(&hw->lock);
	if (csio_is_npiv_ln(ln))
		fc_host_port_type(shost) = FC_PORTTYPE_NPIV;
	else
		fc_host_port_type(shost) = FC_PORTTYPE_NPORT;
	spin_unlock_irq(&hw->lock);
}

/*
 * csio_get_port_state - Return FC local port state.
 * @shost: scsi host.
 *
 */
static void
csio_get_host_port_state(struct Scsi_Host *shost)
{
	struct csio_lnode *ln = shost_priv(shost);
	struct csio_hw *hw = csio_lnode_to_hw(ln);
	char state[16];

	spin_lock_irq(&hw->lock);

	csio_lnode_state_to_str(ln, state);
	if (!strcmp(state, "READY"))
		fc_host_port_state(shost) = FC_PORTSTATE_ONLINE;
	else if (!strcmp(state, "OFFLINE"))
		fc_host_port_state(shost) = FC_PORTSTATE_LINKDOWN;
	else
		fc_host_port_state(shost) = FC_PORTSTATE_UNKNOWN;

	spin_unlock_irq(&hw->lock);
}

/*
 * csio_get_host_speed - Return link speed to FC transport.
 * @shost: scsi host.
 *
 */
static void
csio_get_host_speed(struct Scsi_Host *shost)
{
	struct csio_lnode *ln = shost_priv(shost);
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	spin_lock_irq(&hw->lock);
	switch (hw->pport[ln->portid].link_speed) {
	case FW_PORT_CAP_SPEED_1G:
		fc_host_speed(shost) = FC_PORTSPEED_1GBIT;
		break;
	case FW_PORT_CAP_SPEED_10G:
		fc_host_speed(shost) = FC_PORTSPEED_10GBIT;
		break;
	default:
		fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;
		break;
	}
	spin_unlock_irq(&hw->lock);
}

/*
 * csio_get_host_fabric_name - Return fabric name
 * @shost: scsi host.
 *
 */
static void
csio_get_host_fabric_name(struct Scsi_Host *shost)
{
	struct csio_lnode *ln = shost_priv(shost);
	struct csio_rnode *rn = NULL;
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	spin_lock_irq(&hw->lock);
	rn = csio_rnode_lookup_portid(ln, FC_FID_FLOGI);
	if (rn)
		fc_host_fabric_name(shost) = wwn_to_u64(csio_rn_wwnn(rn));
	else
		fc_host_fabric_name(shost) = 0;
	spin_unlock_irq(&hw->lock);
}

/*
 * csio_get_host_speed - Return FC transport statistics.
 * @ln: Lnode.
 *
 */
static struct fc_host_statistics *
csio_get_stats(struct Scsi_Host *shost)
{
	struct csio_lnode *ln = shost_priv(shost);
	struct csio_hw *hw = csio_lnode_to_hw(ln);
	struct fc_host_statistics *fhs = &ln->fch_stats;
	struct fw_fcoe_port_stats fcoe_port_stats;
	uint64_t seconds;

	memset(&fcoe_port_stats, 0, sizeof(struct fw_fcoe_port_stats));
	csio_get_phy_port_stats(hw, ln->portid, &fcoe_port_stats);

	fhs->tx_frames  += (be64_to_cpu(fcoe_port_stats.tx_bcast_frames) +
			    be64_to_cpu(fcoe_port_stats.tx_mcast_frames) +
			    be64_to_cpu(fcoe_port_stats.tx_ucast_frames) +
			    be64_to_cpu(fcoe_port_stats.tx_offload_frames));
	fhs->tx_words  += (be64_to_cpu(fcoe_port_stats.tx_bcast_bytes) +
			   be64_to_cpu(fcoe_port_stats.tx_mcast_bytes) +
			   be64_to_cpu(fcoe_port_stats.tx_ucast_bytes) +
			   be64_to_cpu(fcoe_port_stats.tx_offload_bytes)) /
							CSIO_WORD_TO_BYTE;
	fhs->rx_frames += (be64_to_cpu(fcoe_port_stats.rx_bcast_frames) +
			   be64_to_cpu(fcoe_port_stats.rx_mcast_frames) +
			   be64_to_cpu(fcoe_port_stats.rx_ucast_frames));
	fhs->rx_words += (be64_to_cpu(fcoe_port_stats.rx_bcast_bytes) +
			  be64_to_cpu(fcoe_port_stats.rx_mcast_bytes) +
			  be64_to_cpu(fcoe_port_stats.rx_ucast_bytes)) /
							CSIO_WORD_TO_BYTE;
	fhs->error_frames += be64_to_cpu(fcoe_port_stats.rx_err_frames);
	fhs->fcp_input_requests +=  ln->stats.n_input_requests;
	fhs->fcp_output_requests +=  ln->stats.n_output_requests;
	fhs->fcp_control_requests +=  ln->stats.n_control_requests;
	fhs->fcp_input_megabytes +=  ln->stats.n_input_bytes >> 20;
	fhs->fcp_output_megabytes +=  ln->stats.n_output_bytes >> 20;
	fhs->link_failure_count = ln->stats.n_link_down;
	/* Reset stats for the device */
	seconds = jiffies_to_msecs(jiffies) - hw->stats.n_reset_start;
	do_div(seconds, 1000);
	fhs->seconds_since_last_reset = seconds;

	return fhs;
}

/*
 * csio_set_rport_loss_tmo - Set the rport dev loss timeout
 * @rport: fc rport.
 * @timeout: new value for dev loss tmo.
 *
 * If timeout is non zero set the dev_loss_tmo to timeout, else set
 * dev_loss_tmo to one.
 */
static void
csio_set_rport_loss_tmo(struct fc_rport *rport, uint32_t timeout)
{
	if (timeout)
		rport->dev_loss_tmo = timeout;
	else
		rport->dev_loss_tmo = 1;
}

static void
csio_vport_set_state(struct csio_lnode *ln)
{
	struct fc_vport *fc_vport = ln->fc_vport;
	struct csio_lnode  *pln = ln->pln;
	char state[16];

	/* Set fc vport state based on phyiscal lnode */
	csio_lnode_state_to_str(pln, state);
	if (strcmp(state, "READY")) {
		fc_vport_set_state(fc_vport, FC_VPORT_LINKDOWN);
		return;
	}

	if (!(pln->flags & CSIO_LNF_NPIVSUPP)) {
		fc_vport_set_state(fc_vport, FC_VPORT_NO_FABRIC_SUPP);
		return;
	}

	/* Set fc vport state based on virtual lnode */
	csio_lnode_state_to_str(ln, state);
	if (strcmp(state, "READY")) {
		fc_vport_set_state(fc_vport, FC_VPORT_LINKDOWN);
		return;
	}
	fc_vport_set_state(fc_vport, FC_VPORT_ACTIVE);
}

static int
csio_fcoe_alloc_vnp(struct csio_hw *hw, struct csio_lnode *ln)
{
	struct csio_lnode *pln;
	struct csio_mb  *mbp;
	struct fw_fcoe_vnp_cmd *rsp;
	int ret = 0;
	int retry = 0;

	/* Issue VNP cmd to alloc vport */
	/* Allocate Mbox request */
	spin_lock_irq(&hw->lock);
	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		ret = -ENOMEM;
		goto out;
	}

	pln = ln->pln;
	ln->fcf_flowid = pln->fcf_flowid;
	ln->portid = pln->portid;

	csio_fcoe_vnp_alloc_init_mb(ln, mbp, CSIO_MB_DEFAULT_TMO,
				    pln->fcf_flowid, pln->vnp_flowid, 0,
				    csio_ln_wwnn(ln), csio_ln_wwpn(ln), NULL);

	for (retry = 0; retry < 3; retry++) {
		/* FW is expected to complete vnp cmd in immediate mode
		 * without much delay.
		 * Otherwise, there will be increase in IO latency since HW
		 * lock is held till completion of vnp mbox cmd.
		 */
		ret = csio_mb_issue(hw, mbp);
		if (ret != -EBUSY)
			break;

		/* Retry if mbox returns busy */
		spin_unlock_irq(&hw->lock);
		msleep(2000);
		spin_lock_irq(&hw->lock);
	}

	if (ret) {
		csio_ln_err(ln, "Failed to issue mbox FCoE VNP command\n");
		goto out_free;
	}

	/* Process Mbox response of VNP command */
	rsp = (struct fw_fcoe_vnp_cmd *)(mbp->mb);
	if (FW_CMD_RETVAL_G(ntohl(rsp->alloc_to_len16)) != FW_SUCCESS) {
		csio_ln_err(ln, "FCOE VNP ALLOC cmd returned 0x%x!\n",
			    FW_CMD_RETVAL_G(ntohl(rsp->alloc_to_len16)));
		ret = -EINVAL;
		goto out_free;
	}

	ln->vnp_flowid = FW_FCOE_VNP_CMD_VNPI_GET(
				ntohl(rsp->gen_wwn_to_vnpi));
	memcpy(csio_ln_wwnn(ln), rsp->vnport_wwnn, 8);
	memcpy(csio_ln_wwpn(ln), rsp->vnport_wwpn, 8);

	csio_ln_dbg(ln, "FCOE VNPI: 0x%x\n", ln->vnp_flowid);
	csio_ln_dbg(ln, "\tWWNN: %x%x%x%x%x%x%x%x\n",
		    ln->ln_sparm.wwnn[0], ln->ln_sparm.wwnn[1],
		    ln->ln_sparm.wwnn[2], ln->ln_sparm.wwnn[3],
		    ln->ln_sparm.wwnn[4], ln->ln_sparm.wwnn[5],
		    ln->ln_sparm.wwnn[6], ln->ln_sparm.wwnn[7]);
	csio_ln_dbg(ln, "\tWWPN: %x%x%x%x%x%x%x%x\n",
		    ln->ln_sparm.wwpn[0], ln->ln_sparm.wwpn[1],
		    ln->ln_sparm.wwpn[2], ln->ln_sparm.wwpn[3],
		    ln->ln_sparm.wwpn[4], ln->ln_sparm.wwpn[5],
		    ln->ln_sparm.wwpn[6], ln->ln_sparm.wwpn[7]);

out_free:
	mempool_free(mbp, hw->mb_mempool);
out:
	spin_unlock_irq(&hw->lock);
	return ret;
}

static int
csio_fcoe_free_vnp(struct csio_hw *hw, struct csio_lnode *ln)
{
	struct csio_lnode *pln;
	struct csio_mb  *mbp;
	struct fw_fcoe_vnp_cmd *rsp;
	int ret = 0;
	int retry = 0;

	/* Issue VNP cmd to free vport */
	/* Allocate Mbox request */

	spin_lock_irq(&hw->lock);
	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		ret = -ENOMEM;
		goto out;
	}

	pln = ln->pln;

	csio_fcoe_vnp_free_init_mb(ln, mbp, CSIO_MB_DEFAULT_TMO,
				   ln->fcf_flowid, ln->vnp_flowid,
				   NULL);

	for (retry = 0; retry < 3; retry++) {
		ret = csio_mb_issue(hw, mbp);
		if (ret != -EBUSY)
			break;

		/* Retry if mbox returns busy */
		spin_unlock_irq(&hw->lock);
		msleep(2000);
		spin_lock_irq(&hw->lock);
	}

	if (ret) {
		csio_ln_err(ln, "Failed to issue mbox FCoE VNP command\n");
		goto out_free;
	}

	/* Process Mbox response of VNP command */
	rsp = (struct fw_fcoe_vnp_cmd *)(mbp->mb);
	if (FW_CMD_RETVAL_G(ntohl(rsp->alloc_to_len16)) != FW_SUCCESS) {
		csio_ln_err(ln, "FCOE VNP FREE cmd returned 0x%x!\n",
			    FW_CMD_RETVAL_G(ntohl(rsp->alloc_to_len16)));
		ret = -EINVAL;
	}

out_free:
	mempool_free(mbp, hw->mb_mempool);
out:
	spin_unlock_irq(&hw->lock);
	return ret;
}

static int
csio_vport_create(struct fc_vport *fc_vport, bool disable)
{
	struct Scsi_Host *shost = fc_vport->shost;
	struct csio_lnode *pln = shost_priv(shost);
	struct csio_lnode *ln = NULL;
	struct csio_hw *hw = csio_lnode_to_hw(pln);
	uint8_t wwn[8];
	int ret = -1;

	ln = csio_shost_init(hw, &fc_vport->dev, false, pln);
	if (!ln)
		goto error;

	if (fc_vport->node_name != 0) {
		u64_to_wwn(fc_vport->node_name, wwn);

		if (!CSIO_VALID_WWN(wwn)) {
			csio_ln_err(ln,
				    "vport create failed. Invalid wwnn\n");
			goto error;
		}
		memcpy(csio_ln_wwnn(ln), wwn, 8);
	}

	if (fc_vport->port_name != 0) {
		u64_to_wwn(fc_vport->port_name, wwn);

		if (!CSIO_VALID_WWN(wwn)) {
			csio_ln_err(ln,
				    "vport create failed. Invalid wwpn\n");
			goto error;
		}

		if (csio_lnode_lookup_by_wwpn(hw, wwn)) {
			csio_ln_err(ln,
			    "vport create failed. wwpn already exists\n");
			goto error;
		}
		memcpy(csio_ln_wwpn(ln), wwn, 8);
	}

	fc_vport_set_state(fc_vport, FC_VPORT_INITIALIZING);

	if (csio_fcoe_alloc_vnp(hw, ln))
		goto error;

	*(struct csio_lnode **)fc_vport->dd_data = ln;
	ln->fc_vport = fc_vport;
	if (!fc_vport->node_name)
		fc_vport->node_name = wwn_to_u64(csio_ln_wwnn(ln));
	if (!fc_vport->port_name)
		fc_vport->port_name = wwn_to_u64(csio_ln_wwpn(ln));
	csio_fchost_attr_init(ln);
	return 0;
error:
	if (ln)
		csio_shost_exit(ln);

	return ret;
}

static int
csio_vport_delete(struct fc_vport *fc_vport)
{
	struct csio_lnode *ln = *(struct csio_lnode **)fc_vport->dd_data;
	struct Scsi_Host *shost = csio_ln_to_shost(ln);
	struct csio_hw *hw = csio_lnode_to_hw(ln);
	int rmv;

	spin_lock_irq(&hw->lock);
	rmv = csio_is_hw_removing(hw);
	spin_unlock_irq(&hw->lock);

	if (rmv) {
		csio_shost_exit(ln);
		return 0;
	}

	/* Quiesce ios and send remove event to lnode */
	scsi_block_requests(shost);
	spin_lock_irq(&hw->lock);
	csio_scsim_cleanup_io_lnode(csio_hw_to_scsim(hw), ln);
	csio_lnode_close(ln);
	spin_unlock_irq(&hw->lock);
	scsi_unblock_requests(shost);

	/* Free vnp */
	if (fc_vport->vport_state !=  FC_VPORT_DISABLED)
		csio_fcoe_free_vnp(hw, ln);

	csio_shost_exit(ln);
	return 0;
}

static int
csio_vport_disable(struct fc_vport *fc_vport, bool disable)
{
	struct csio_lnode *ln = *(struct csio_lnode **)fc_vport->dd_data;
	struct Scsi_Host *shost = csio_ln_to_shost(ln);
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	/* disable vport */
	if (disable) {
		/* Quiesce ios and send stop event to lnode */
		scsi_block_requests(shost);
		spin_lock_irq(&hw->lock);
		csio_scsim_cleanup_io_lnode(csio_hw_to_scsim(hw), ln);
		csio_lnode_stop(ln);
		spin_unlock_irq(&hw->lock);
		scsi_unblock_requests(shost);

		/* Free vnp */
		csio_fcoe_free_vnp(hw, ln);
		fc_vport_set_state(fc_vport, FC_VPORT_DISABLED);
		csio_ln_err(ln, "vport disabled\n");
		return 0;
	} else {
		/* enable vport */
		fc_vport_set_state(fc_vport, FC_VPORT_INITIALIZING);
		if (csio_fcoe_alloc_vnp(hw, ln)) {
			csio_ln_err(ln, "vport enabled failed.\n");
			return -1;
		}
		csio_ln_err(ln, "vport enabled\n");
		return 0;
	}
}

static void
csio_dev_loss_tmo_callbk(struct fc_rport *rport)
{
	struct csio_rnode *rn;
	struct csio_hw *hw;
	struct csio_lnode *ln;

	rn = *((struct csio_rnode **)rport->dd_data);
	ln = csio_rnode_to_lnode(rn);
	hw = csio_lnode_to_hw(ln);

	spin_lock_irq(&hw->lock);

	/* return if driver is being removed or same rnode comes back online */
	if (csio_is_hw_removing(hw) || csio_is_rnode_ready(rn))
		goto out;

	csio_ln_dbg(ln, "devloss timeout on rnode:%p portid:x%x flowid:x%x\n",
		    rn, rn->nport_id, csio_rn_flowid(rn));

	CSIO_INC_STATS(ln, n_dev_loss_tmo);

	/*
	 * enqueue devloss event to event worker thread to serialize all
	 * rnode events.
	 */
	if (csio_enqueue_evt(hw, CSIO_EVT_DEV_LOSS, &rn, sizeof(rn))) {
		CSIO_INC_STATS(hw, n_evt_drop);
		goto out;
	}

	if (!(hw->flags & CSIO_HWF_FWEVT_PENDING)) {
		hw->flags |= CSIO_HWF_FWEVT_PENDING;
		spin_unlock_irq(&hw->lock);
		schedule_work(&hw->evtq_work);
		return;
	}

out:
	spin_unlock_irq(&hw->lock);
}

/* FC transport functions template - Physical port */
struct fc_function_template csio_fc_transport_funcs = {
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_fc4s = 1,
	.show_host_maxframe_size = 1,

	.get_host_port_id = csio_get_host_port_id,
	.show_host_port_id = 1,

	.get_host_port_type = csio_get_host_port_type,
	.show_host_port_type = 1,

	.get_host_port_state = csio_get_host_port_state,
	.show_host_port_state = 1,

	.show_host_active_fc4s = 1,
	.get_host_speed = csio_get_host_speed,
	.show_host_speed = 1,
	.get_host_fabric_name = csio_get_host_fabric_name,
	.show_host_fabric_name = 1,

	.get_fc_host_stats = csio_get_stats,

	.dd_fcrport_size = sizeof(struct csio_rnode *),
	.show_rport_maxframe_size = 1,
	.show_rport_supported_classes = 1,

	.set_rport_dev_loss_tmo = csio_set_rport_loss_tmo,
	.show_rport_dev_loss_tmo = 1,

	.show_starget_port_id = 1,
	.show_starget_node_name = 1,
	.show_starget_port_name = 1,

	.dev_loss_tmo_callbk = csio_dev_loss_tmo_callbk,
	.dd_fcvport_size = sizeof(struct csio_lnode *),

	.vport_create = csio_vport_create,
	.vport_disable = csio_vport_disable,
	.vport_delete = csio_vport_delete,
};

/* FC transport functions template - Virtual  port */
struct fc_function_template csio_fc_transport_vport_funcs = {
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_fc4s = 1,
	.show_host_maxframe_size = 1,

	.get_host_port_id = csio_get_host_port_id,
	.show_host_port_id = 1,

	.get_host_port_type = csio_get_host_port_type,
	.show_host_port_type = 1,

	.get_host_port_state = csio_get_host_port_state,
	.show_host_port_state = 1,
	.show_host_active_fc4s = 1,

	.get_host_speed = csio_get_host_speed,
	.show_host_speed = 1,

	.get_host_fabric_name = csio_get_host_fabric_name,
	.show_host_fabric_name = 1,

	.get_fc_host_stats = csio_get_stats,

	.dd_fcrport_size = sizeof(struct csio_rnode *),
	.show_rport_maxframe_size = 1,
	.show_rport_supported_classes = 1,

	.set_rport_dev_loss_tmo = csio_set_rport_loss_tmo,
	.show_rport_dev_loss_tmo = 1,

	.show_starget_port_id = 1,
	.show_starget_node_name = 1,
	.show_starget_port_name = 1,

	.dev_loss_tmo_callbk = csio_dev_loss_tmo_callbk,

};
