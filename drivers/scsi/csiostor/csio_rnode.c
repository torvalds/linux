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

#include <linux/string.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/fc/fc_els.h>
#include <scsi/fc/fc_fs.h>

#include "csio_hw.h"
#include "csio_lnode.h"
#include "csio_rnode.h"

static int csio_rnode_init(struct csio_rnode *, struct csio_lnode *);
static void csio_rnode_exit(struct csio_rnode *);

/* Static machine forward declarations */
static void csio_rns_uninit(struct csio_rnode *, enum csio_rn_ev);
static void csio_rns_ready(struct csio_rnode *, enum csio_rn_ev);
static void csio_rns_offline(struct csio_rnode *, enum csio_rn_ev);
static void csio_rns_disappeared(struct csio_rnode *, enum csio_rn_ev);

/* RNF event mapping */
static enum csio_rn_ev fwevt_to_rnevt[] = {
	CSIO_RNFE_NONE,		/* None */
	CSIO_RNFE_LOGGED_IN,	/* PLOGI_ACC_RCVD  */
	CSIO_RNFE_NONE,		/* PLOGI_RJT_RCVD  */
	CSIO_RNFE_PLOGI_RECV,	/* PLOGI_RCVD	   */
	CSIO_RNFE_LOGO_RECV,	/* PLOGO_RCVD	   */
	CSIO_RNFE_PRLI_DONE,	/* PRLI_ACC_RCVD   */
	CSIO_RNFE_NONE,		/* PRLI_RJT_RCVD   */
	CSIO_RNFE_PRLI_RECV,	/* PRLI_RCVD	   */
	CSIO_RNFE_PRLO_RECV,	/* PRLO_RCVD	   */
	CSIO_RNFE_NONE,		/* NPORT_ID_CHGD   */
	CSIO_RNFE_LOGO_RECV,	/* FLOGO_RCVD	   */
	CSIO_RNFE_NONE,		/* CLR_VIRT_LNK_RCVD */
	CSIO_RNFE_LOGGED_IN,	/* FLOGI_ACC_RCVD   */
	CSIO_RNFE_NONE,		/* FLOGI_RJT_RCVD   */
	CSIO_RNFE_LOGGED_IN,	/* FDISC_ACC_RCVD   */
	CSIO_RNFE_NONE,		/* FDISC_RJT_RCVD   */
	CSIO_RNFE_NONE,		/* FLOGI_TMO_MAX_RETRY */
	CSIO_RNFE_NONE,		/* IMPL_LOGO_ADISC_ACC */
	CSIO_RNFE_NONE,		/* IMPL_LOGO_ADISC_RJT */
	CSIO_RNFE_NONE,		/* IMPL_LOGO_ADISC_CNFLT */
	CSIO_RNFE_NONE,		/* PRLI_TMO		*/
	CSIO_RNFE_NONE,		/* ADISC_TMO		*/
	CSIO_RNFE_NAME_MISSING,	/* RSCN_DEV_LOST  */
	CSIO_RNFE_NONE,		/* SCR_ACC_RCVD	*/
	CSIO_RNFE_NONE,		/* ADISC_RJT_RCVD */
	CSIO_RNFE_NONE,		/* LOGO_SNT */
	CSIO_RNFE_LOGO_RECV,	/* PROTO_ERR_IMPL_LOGO */
};

#define CSIO_FWE_TO_RNFE(_evt)	((_evt > PROTO_ERR_IMPL_LOGO) ?		\
						CSIO_RNFE_NONE :	\
						fwevt_to_rnevt[_evt])
int
csio_is_rnode_ready(struct csio_rnode *rn)
{
	return csio_match_state(rn, csio_rns_ready);
}

static int
csio_is_rnode_uninit(struct csio_rnode *rn)
{
	return csio_match_state(rn, csio_rns_uninit);
}

static int
csio_is_rnode_wka(uint8_t rport_type)
{
	if ((rport_type == FLOGI_VFPORT) ||
	    (rport_type == FDISC_VFPORT) ||
	    (rport_type == NS_VNPORT) ||
	    (rport_type == FDMI_VNPORT))
		return 1;

	return 0;
}

/*
 * csio_rn_lookup - Finds the rnode with the given flowid
 * @ln - lnode
 * @flowid - flowid.
 *
 * Does the rnode lookup on the given lnode and flowid.If no matching entry
 * found, NULL is returned.
 */
static struct csio_rnode *
csio_rn_lookup(struct csio_lnode *ln, uint32_t flowid)
{
	struct csio_rnode *rnhead = (struct csio_rnode *) &ln->rnhead;
	struct list_head *tmp;
	struct csio_rnode *rn;

	list_for_each(tmp, &rnhead->sm.sm_list) {
		rn = (struct csio_rnode *) tmp;
		if (rn->flowid == flowid)
			return rn;
	}

	return NULL;
}

/*
 * csio_rn_lookup_wwpn - Finds the rnode with the given wwpn
 * @ln: lnode
 * @wwpn: wwpn
 *
 * Does the rnode lookup on the given lnode and wwpn. If no matching entry
 * found, NULL is returned.
 */
static struct csio_rnode *
csio_rn_lookup_wwpn(struct csio_lnode *ln, uint8_t *wwpn)
{
	struct csio_rnode *rnhead = (struct csio_rnode *) &ln->rnhead;
	struct list_head *tmp;
	struct csio_rnode *rn;

	list_for_each(tmp, &rnhead->sm.sm_list) {
		rn = (struct csio_rnode *) tmp;
		if (!memcmp(csio_rn_wwpn(rn), wwpn, 8))
			return rn;
	}

	return NULL;
}

/**
 * csio_rnode_lookup_portid - Finds the rnode with the given portid
 * @ln:		lnode
 * @portid:	port id
 *
 * Lookup the rnode list for a given portid. If no matching entry
 * found, NULL is returned.
 */
struct csio_rnode *
csio_rnode_lookup_portid(struct csio_lnode *ln, uint32_t portid)
{
	struct csio_rnode *rnhead = (struct csio_rnode *) &ln->rnhead;
	struct list_head *tmp;
	struct csio_rnode *rn;

	list_for_each(tmp, &rnhead->sm.sm_list) {
		rn = (struct csio_rnode *) tmp;
		if (rn->nport_id == portid)
			return rn;
	}

	return NULL;
}

static int
csio_rn_dup_flowid(struct csio_lnode *ln, uint32_t rdev_flowid,
		    uint32_t *vnp_flowid)
{
	struct csio_rnode *rnhead;
	struct list_head *tmp, *tmp1;
	struct csio_rnode *rn;
	struct csio_lnode *ln_tmp;
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	list_for_each(tmp1, &hw->sln_head) {
		ln_tmp = (struct csio_lnode *) tmp1;
		if (ln_tmp == ln)
			continue;

		rnhead = (struct csio_rnode *)&ln_tmp->rnhead;
		list_for_each(tmp, &rnhead->sm.sm_list) {

			rn = (struct csio_rnode *) tmp;
			if (csio_is_rnode_ready(rn)) {
				if (rn->flowid == rdev_flowid) {
					*vnp_flowid = csio_ln_flowid(ln_tmp);
					return 1;
				}
			}
		}
	}

	return 0;
}

static struct csio_rnode *
csio_alloc_rnode(struct csio_lnode *ln)
{
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	struct csio_rnode *rn = mempool_alloc(hw->rnode_mempool, GFP_ATOMIC);
	if (!rn)
		goto err;

	memset(rn, 0, sizeof(struct csio_rnode));
	if (csio_rnode_init(rn, ln))
		goto err_free;

	CSIO_INC_STATS(ln, n_rnode_alloc);

	return rn;

err_free:
	mempool_free(rn, hw->rnode_mempool);
err:
	CSIO_INC_STATS(ln, n_rnode_nomem);
	return NULL;
}

static void
csio_free_rnode(struct csio_rnode *rn)
{
	struct csio_hw *hw = csio_lnode_to_hw(csio_rnode_to_lnode(rn));

	csio_rnode_exit(rn);
	CSIO_INC_STATS(rn->lnp, n_rnode_free);
	mempool_free(rn, hw->rnode_mempool);
}

/*
 * csio_get_rnode - Gets rnode with the given flowid
 * @ln - lnode
 * @flowid - flow id.
 *
 * Does the rnode lookup on the given lnode and flowid. If no matching
 * rnode found, then new rnode with given npid is allocated and returned.
 */
static struct csio_rnode *
csio_get_rnode(struct csio_lnode *ln, uint32_t flowid)
{
	struct csio_rnode *rn;

	rn = csio_rn_lookup(ln, flowid);
	if (!rn) {
		rn = csio_alloc_rnode(ln);
		if (!rn)
			return NULL;

		rn->flowid = flowid;
	}

	return rn;
}

/*
 * csio_put_rnode - Frees the given rnode
 * @ln - lnode
 * @flowid - flow id.
 *
 * Does the rnode lookup on the given lnode and flowid. If no matching
 * rnode found, then new rnode with given npid is allocated and returned.
 */
void
csio_put_rnode(struct csio_lnode *ln, struct csio_rnode *rn)
{
	CSIO_DB_ASSERT(csio_is_rnode_uninit(rn) != 0);
	csio_free_rnode(rn);
}

/*
 * csio_confirm_rnode - confirms rnode based on wwpn.
 * @ln: lnode
 * @rdev_flowid: remote device flowid
 * @rdevp: remote device params
 * This routines searches other rnode in list having same wwpn of new rnode.
 * If there is a match, then matched rnode is returned and otherwise new rnode
 * is returned.
 * returns rnode.
 */
struct csio_rnode *
csio_confirm_rnode(struct csio_lnode *ln, uint32_t rdev_flowid,
		   struct fcoe_rdev_entry *rdevp)
{
	uint8_t rport_type;
	struct csio_rnode *rn, *match_rn;
	uint32_t vnp_flowid = 0;
	__be32 *port_id;

	port_id = (__be32 *)&rdevp->r_id[0];
	rport_type =
		FW_RDEV_WR_RPORT_TYPE_GET(rdevp->rd_xfer_rdy_to_rport_type);

	/* Drop rdev event for cntrl port */
	if (rport_type == FAB_CTLR_VNPORT) {
		csio_ln_dbg(ln,
			    "Unhandled rport_type:%d recv in rdev evt "
			    "ssni:x%x\n", rport_type, rdev_flowid);
		return NULL;
	}

	/* Lookup on flowid */
	rn = csio_rn_lookup(ln, rdev_flowid);
	if (!rn) {

		/* Drop events with duplicate flowid */
		if (csio_rn_dup_flowid(ln, rdev_flowid, &vnp_flowid)) {
			csio_ln_warn(ln,
				     "ssni:%x already active on vnpi:%x",
				     rdev_flowid, vnp_flowid);
			return NULL;
		}

		/* Lookup on wwpn for NPORTs */
		rn = csio_rn_lookup_wwpn(ln, rdevp->wwpn);
		if (!rn)
			goto alloc_rnode;

	} else {
		/* Lookup well-known ports with nport id */
		if (csio_is_rnode_wka(rport_type)) {
			match_rn = csio_rnode_lookup_portid(ln,
				      ((ntohl(*port_id) >> 8) & CSIO_DID_MASK));
			if (match_rn == NULL) {
				csio_rn_flowid(rn) = CSIO_INVALID_IDX;
				goto alloc_rnode;
			}

			/*
			 * Now compare the wwpn to confirm that
			 * same port relogged in. If so update the matched rn.
			 * Else, go ahead and alloc a new rnode.
			 */
			if (!memcmp(csio_rn_wwpn(match_rn), rdevp->wwpn, 8)) {
				if (rn == match_rn)
					goto found_rnode;
				csio_ln_dbg(ln,
					    "nport_id:x%x and wwpn:%llx"
					    " match for ssni:x%x\n",
					    rn->nport_id,
					    wwn_to_u64(rdevp->wwpn),
					    rdev_flowid);
				if (csio_is_rnode_ready(rn)) {
					csio_ln_warn(ln,
						     "rnode is already"
						     "active ssni:x%x\n",
						     rdev_flowid);
					CSIO_ASSERT(0);
				}
				csio_rn_flowid(rn) = CSIO_INVALID_IDX;
				rn = match_rn;

				/* Update rn */
				goto found_rnode;
			}
			csio_rn_flowid(rn) = CSIO_INVALID_IDX;
			goto alloc_rnode;
		}

		/* wwpn match */
		if (!memcmp(csio_rn_wwpn(rn), rdevp->wwpn, 8))
			goto found_rnode;

		/* Search for rnode that have same wwpn */
		match_rn = csio_rn_lookup_wwpn(ln, rdevp->wwpn);
		if (match_rn != NULL) {
			csio_ln_dbg(ln,
				"ssni:x%x changed for rport name(wwpn):%llx "
				"did:x%x\n", rdev_flowid,
				wwn_to_u64(rdevp->wwpn),
				match_rn->nport_id);
			csio_rn_flowid(rn) = CSIO_INVALID_IDX;
			rn = match_rn;
		} else {
			csio_ln_dbg(ln,
				"rnode wwpn mismatch found ssni:x%x "
				"name(wwpn):%llx\n",
				rdev_flowid,
				wwn_to_u64(csio_rn_wwpn(rn)));
			if (csio_is_rnode_ready(rn)) {
				csio_ln_warn(ln,
					     "rnode is already active "
					     "wwpn:%llx ssni:x%x\n",
					     wwn_to_u64(csio_rn_wwpn(rn)),
					     rdev_flowid);
				CSIO_ASSERT(0);
			}
			csio_rn_flowid(rn) = CSIO_INVALID_IDX;
			goto alloc_rnode;
		}
	}

found_rnode:
	csio_ln_dbg(ln, "found rnode:%p ssni:x%x name(wwpn):%llx\n",
		rn, rdev_flowid, wwn_to_u64(rdevp->wwpn));

	/* Update flowid */
	csio_rn_flowid(rn) = rdev_flowid;

	/* update rdev entry */
	rn->rdev_entry = rdevp;
	CSIO_INC_STATS(ln, n_rnode_match);
	return rn;

alloc_rnode:
	rn = csio_get_rnode(ln, rdev_flowid);
	if (!rn)
		return NULL;

	csio_ln_dbg(ln, "alloc rnode:%p ssni:x%x name(wwpn):%llx\n",
		rn, rdev_flowid, wwn_to_u64(rdevp->wwpn));

	/* update rdev entry */
	rn->rdev_entry = rdevp;
	return rn;
}

/*
 * csio_rn_verify_rparams - verify rparams.
 * @ln: lnode
 * @rn: rnode
 * @rdevp: remote device params
 * returns success if rparams are verified.
 */
static int
csio_rn_verify_rparams(struct csio_lnode *ln, struct csio_rnode *rn,
			struct fcoe_rdev_entry *rdevp)
{
	uint8_t null[8];
	uint8_t rport_type;
	uint8_t fc_class;
	__be32 *did;

	did = (__be32 *) &rdevp->r_id[0];
	rport_type =
		FW_RDEV_WR_RPORT_TYPE_GET(rdevp->rd_xfer_rdy_to_rport_type);
	switch (rport_type) {
	case FLOGI_VFPORT:
		rn->role = CSIO_RNFR_FABRIC;
		if (((ntohl(*did) >> 8) & CSIO_DID_MASK) != FC_FID_FLOGI) {
			csio_ln_err(ln, "ssni:x%x invalid fabric portid\n",
				csio_rn_flowid(rn));
			return -EINVAL;
		}
		/* NPIV support */
		if (FW_RDEV_WR_NPIV_GET(rdevp->vft_to_qos))
			ln->flags |= CSIO_LNF_NPIVSUPP;

		break;

	case NS_VNPORT:
		rn->role = CSIO_RNFR_NS;
		if (((ntohl(*did) >> 8) & CSIO_DID_MASK) != FC_FID_DIR_SERV) {
			csio_ln_err(ln, "ssni:x%x invalid fabric portid\n",
				csio_rn_flowid(rn));
			return -EINVAL;
		}
		break;

	case REG_FC4_VNPORT:
	case REG_VNPORT:
		rn->role = CSIO_RNFR_NPORT;
		if (rdevp->event_cause == PRLI_ACC_RCVD ||
			rdevp->event_cause == PRLI_RCVD) {
			if (FW_RDEV_WR_TASK_RETRY_ID_GET(
							rdevp->enh_disc_to_tgt))
				rn->fcp_flags |= FCP_SPPF_OVLY_ALLOW;

			if (FW_RDEV_WR_RETRY_GET(rdevp->enh_disc_to_tgt))
				rn->fcp_flags |= FCP_SPPF_RETRY;

			if (FW_RDEV_WR_CONF_CMPL_GET(rdevp->enh_disc_to_tgt))
				rn->fcp_flags |= FCP_SPPF_CONF_COMPL;

			if (FW_RDEV_WR_TGT_GET(rdevp->enh_disc_to_tgt))
				rn->role |= CSIO_RNFR_TARGET;

			if (FW_RDEV_WR_INI_GET(rdevp->enh_disc_to_tgt))
				rn->role |= CSIO_RNFR_INITIATOR;
		}

		break;

	case FDMI_VNPORT:
	case FAB_CTLR_VNPORT:
		rn->role = 0;
		break;

	default:
		csio_ln_err(ln, "ssni:x%x invalid rport type recv x%x\n",
			csio_rn_flowid(rn), rport_type);
		return -EINVAL;
	}

	/* validate wwpn/wwnn for Name server/remote port */
	if (rport_type == REG_VNPORT || rport_type == NS_VNPORT) {
		memset(null, 0, 8);
		if (!memcmp(rdevp->wwnn, null, 8)) {
			csio_ln_err(ln,
				    "ssni:x%x invalid wwnn received from"
				    " rport did:x%x\n",
				    csio_rn_flowid(rn),
				    (ntohl(*did) & CSIO_DID_MASK));
			return -EINVAL;
		}

		if (!memcmp(rdevp->wwpn, null, 8)) {
			csio_ln_err(ln,
				    "ssni:x%x invalid wwpn received from"
				    " rport did:x%x\n",
				    csio_rn_flowid(rn),
				    (ntohl(*did) & CSIO_DID_MASK));
			return -EINVAL;
		}

	}

	/* Copy wwnn, wwpn and nport id */
	rn->nport_id = (ntohl(*did) >> 8) & CSIO_DID_MASK;
	memcpy(csio_rn_wwnn(rn), rdevp->wwnn, 8);
	memcpy(csio_rn_wwpn(rn), rdevp->wwpn, 8);
	rn->rn_sparm.csp.sp_bb_data = rdevp->rcv_fr_sz;
	fc_class = FW_RDEV_WR_CLASS_GET(rdevp->vft_to_qos);
	rn->rn_sparm.clsp[fc_class - 1].cp_class = htons(FC_CPC_VALID);

	return 0;
}

static void
__csio_reg_rnode(struct csio_rnode *rn)
{
	struct csio_lnode *ln = csio_rnode_to_lnode(rn);
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	spin_unlock_irq(&hw->lock);
	csio_reg_rnode(rn);
	spin_lock_irq(&hw->lock);

	if (rn->role & CSIO_RNFR_TARGET)
		ln->n_scsi_tgts++;

	if (rn->nport_id == FC_FID_MGMT_SERV)
		csio_ln_fdmi_start(ln, (void *) rn);
}

static void
__csio_unreg_rnode(struct csio_rnode *rn)
{
	struct csio_lnode *ln = csio_rnode_to_lnode(rn);
	struct csio_hw *hw = csio_lnode_to_hw(ln);
	LIST_HEAD(tmp_q);
	int cmpl = 0;

	if (!list_empty(&rn->host_cmpl_q)) {
		csio_dbg(hw, "Returning completion queue I/Os\n");
		list_splice_tail_init(&rn->host_cmpl_q, &tmp_q);
		cmpl = 1;
	}

	if (rn->role & CSIO_RNFR_TARGET) {
		ln->n_scsi_tgts--;
		ln->last_scan_ntgts--;
	}

	spin_unlock_irq(&hw->lock);
	csio_unreg_rnode(rn);
	spin_lock_irq(&hw->lock);

	/* Cleanup I/Os that were waiting for rnode to unregister */
	if (cmpl)
		csio_scsi_cleanup_io_q(csio_hw_to_scsim(hw), &tmp_q);

}

/*****************************************************************************/
/* START: Rnode SM                                                           */
/*****************************************************************************/

/*
 * csio_rns_uninit -
 * @rn - rnode
 * @evt - SM event.
 *
 */
static void
csio_rns_uninit(struct csio_rnode *rn, enum csio_rn_ev evt)
{
	struct csio_lnode *ln = csio_rnode_to_lnode(rn);
	int ret = 0;

	CSIO_INC_STATS(rn, n_evt_sm[evt]);

	switch (evt) {
	case CSIO_RNFE_LOGGED_IN:
	case CSIO_RNFE_PLOGI_RECV:
		ret = csio_rn_verify_rparams(ln, rn, rn->rdev_entry);
		if (!ret) {
			csio_set_state(&rn->sm, csio_rns_ready);
			__csio_reg_rnode(rn);
		} else {
			CSIO_INC_STATS(rn, n_err_inval);
		}
		break;
	case CSIO_RNFE_LOGO_RECV:
		csio_ln_dbg(ln,
			    "ssni:x%x Ignoring event %d recv "
			    "in rn state[uninit]\n", csio_rn_flowid(rn), evt);
		CSIO_INC_STATS(rn, n_evt_drop);
		break;
	default:
		csio_ln_dbg(ln,
			    "ssni:x%x unexp event %d recv "
			    "in rn state[uninit]\n", csio_rn_flowid(rn), evt);
		CSIO_INC_STATS(rn, n_evt_unexp);
		break;
	}
}

/*
 * csio_rns_ready -
 * @rn - rnode
 * @evt - SM event.
 *
 */
static void
csio_rns_ready(struct csio_rnode *rn, enum csio_rn_ev evt)
{
	struct csio_lnode *ln = csio_rnode_to_lnode(rn);
	int ret = 0;

	CSIO_INC_STATS(rn, n_evt_sm[evt]);

	switch (evt) {
	case CSIO_RNFE_LOGGED_IN:
	case CSIO_RNFE_PLOGI_RECV:
		csio_ln_dbg(ln,
			"ssni:x%x Ignoring event %d recv from did:x%x "
			"in rn state[ready]\n", csio_rn_flowid(rn), evt,
			rn->nport_id);
		CSIO_INC_STATS(rn, n_evt_drop);
		break;

	case CSIO_RNFE_PRLI_DONE:
	case CSIO_RNFE_PRLI_RECV:
		ret = csio_rn_verify_rparams(ln, rn, rn->rdev_entry);
		if (!ret)
			__csio_reg_rnode(rn);
		else
			CSIO_INC_STATS(rn, n_err_inval);

		break;
	case CSIO_RNFE_DOWN:
		csio_set_state(&rn->sm, csio_rns_offline);
		__csio_unreg_rnode(rn);

		/* FW expected to internally aborted outstanding SCSI WRs
		 * and return all SCSI WRs to host with status "ABORTED".
		 */
		break;

	case CSIO_RNFE_LOGO_RECV:
		csio_set_state(&rn->sm, csio_rns_offline);

		__csio_unreg_rnode(rn);

		/* FW expected to internally aborted outstanding SCSI WRs
		 * and return all SCSI WRs to host with status "ABORTED".
		 */
		break;

	case CSIO_RNFE_CLOSE:
		/*
		 * Each rnode receives CLOSE event when driver is removed or
		 * device is reset
		 * Note: All outstanding IOs on remote port need to returned
		 * to uppper layer with appropriate error before sending
		 * CLOSE event
		 */
		csio_set_state(&rn->sm, csio_rns_uninit);
		__csio_unreg_rnode(rn);
		break;

	case CSIO_RNFE_NAME_MISSING:
		csio_set_state(&rn->sm, csio_rns_disappeared);
		__csio_unreg_rnode(rn);

		/*
		 * FW expected to internally aborted outstanding SCSI WRs
		 * and return all SCSI WRs to host with status "ABORTED".
		 */

		break;

	default:
		csio_ln_dbg(ln,
			"ssni:x%x unexp event %d recv from did:x%x "
			"in rn state[uninit]\n", csio_rn_flowid(rn), evt,
			rn->nport_id);
		CSIO_INC_STATS(rn, n_evt_unexp);
		break;
	}
}

/*
 * csio_rns_offline -
 * @rn - rnode
 * @evt - SM event.
 *
 */
static void
csio_rns_offline(struct csio_rnode *rn, enum csio_rn_ev evt)
{
	struct csio_lnode *ln = csio_rnode_to_lnode(rn);
	int ret = 0;

	CSIO_INC_STATS(rn, n_evt_sm[evt]);

	switch (evt) {
	case CSIO_RNFE_LOGGED_IN:
	case CSIO_RNFE_PLOGI_RECV:
		ret = csio_rn_verify_rparams(ln, rn, rn->rdev_entry);
		if (!ret) {
			csio_set_state(&rn->sm, csio_rns_ready);
			__csio_reg_rnode(rn);
		} else {
			CSIO_INC_STATS(rn, n_err_inval);
			csio_post_event(&rn->sm, CSIO_RNFE_CLOSE);
		}
		break;

	case CSIO_RNFE_DOWN:
		csio_ln_dbg(ln,
			"ssni:x%x Ignoring event %d recv from did:x%x "
			"in rn state[offline]\n", csio_rn_flowid(rn), evt,
			rn->nport_id);
		CSIO_INC_STATS(rn, n_evt_drop);
		break;

	case CSIO_RNFE_CLOSE:
		/* Each rnode receives CLOSE event when driver is removed or
		 * device is reset
		 * Note: All outstanding IOs on remote port need to returned
		 * to uppper layer with appropriate error before sending
		 * CLOSE event
		 */
		csio_set_state(&rn->sm, csio_rns_uninit);
		break;

	case CSIO_RNFE_NAME_MISSING:
		csio_set_state(&rn->sm, csio_rns_disappeared);
		break;

	default:
		csio_ln_dbg(ln,
			"ssni:x%x unexp event %d recv from did:x%x "
			"in rn state[offline]\n", csio_rn_flowid(rn), evt,
			rn->nport_id);
		CSIO_INC_STATS(rn, n_evt_unexp);
		break;
	}
}

/*
 * csio_rns_disappeared -
 * @rn - rnode
 * @evt - SM event.
 *
 */
static void
csio_rns_disappeared(struct csio_rnode *rn, enum csio_rn_ev evt)
{
	struct csio_lnode *ln = csio_rnode_to_lnode(rn);
	int ret = 0;

	CSIO_INC_STATS(rn, n_evt_sm[evt]);

	switch (evt) {
	case CSIO_RNFE_LOGGED_IN:
	case CSIO_RNFE_PLOGI_RECV:
		ret = csio_rn_verify_rparams(ln, rn, rn->rdev_entry);
		if (!ret) {
			csio_set_state(&rn->sm, csio_rns_ready);
			__csio_reg_rnode(rn);
		} else {
			CSIO_INC_STATS(rn, n_err_inval);
			csio_post_event(&rn->sm, CSIO_RNFE_CLOSE);
		}
		break;

	case CSIO_RNFE_CLOSE:
		/* Each rnode receives CLOSE event when driver is removed or
		 * device is reset.
		 * Note: All outstanding IOs on remote port need to returned
		 * to uppper layer with appropriate error before sending
		 * CLOSE event
		 */
		csio_set_state(&rn->sm, csio_rns_uninit);
		break;

	case CSIO_RNFE_DOWN:
	case CSIO_RNFE_NAME_MISSING:
		csio_ln_dbg(ln,
			"ssni:x%x Ignoring event %d recv from did x%x"
			"in rn state[disappeared]\n", csio_rn_flowid(rn),
			evt, rn->nport_id);
		break;

	default:
		csio_ln_dbg(ln,
			"ssni:x%x unexp event %d recv from did x%x"
			"in rn state[disappeared]\n", csio_rn_flowid(rn),
			evt, rn->nport_id);
		CSIO_INC_STATS(rn, n_evt_unexp);
		break;
	}
}

/*****************************************************************************/
/* END: Rnode SM                                                             */
/*****************************************************************************/

/*
 * csio_rnode_devloss_handler - Device loss event handler
 * @rn: rnode
 *
 * Post event to close rnode SM and free rnode.
 */
void
csio_rnode_devloss_handler(struct csio_rnode *rn)
{
	struct csio_lnode *ln = csio_rnode_to_lnode(rn);

	/* ignore if same rnode came back as online */
	if (csio_is_rnode_ready(rn))
		return;

	csio_post_event(&rn->sm, CSIO_RNFE_CLOSE);

	/* Free rn if in uninit state */
	if (csio_is_rnode_uninit(rn))
		csio_put_rnode(ln, rn);
}

/**
 * csio_rnode_fwevt_handler - Event handler for firmware rnode events.
 * @rn:		rnode
 *
 */
void
csio_rnode_fwevt_handler(struct csio_rnode *rn, uint8_t fwevt)
{
	struct csio_lnode *ln = csio_rnode_to_lnode(rn);
	enum csio_rn_ev evt;

	evt = CSIO_FWE_TO_RNFE(fwevt);
	if (!evt) {
		csio_ln_err(ln, "ssni:x%x Unhandled FW Rdev event: %d\n",
			    csio_rn_flowid(rn), fwevt);
		CSIO_INC_STATS(rn, n_evt_unexp);
		return;
	}
	CSIO_INC_STATS(rn, n_evt_fw[fwevt]);

	/* Track previous & current events for debugging */
	rn->prev_evt = rn->cur_evt;
	rn->cur_evt = fwevt;

	/* Post event to rnode SM */
	csio_post_event(&rn->sm, evt);

	/* Free rn if in uninit state */
	if (csio_is_rnode_uninit(rn))
		csio_put_rnode(ln, rn);
}

/*
 * csio_rnode_init - Initialize rnode.
 * @rn: RNode
 * @ln: Associated lnode
 *
 * Caller is responsible for holding the lock. The lock is required
 * to be held for inserting the rnode in ln->rnhead list.
 */
static int
csio_rnode_init(struct csio_rnode *rn, struct csio_lnode *ln)
{
	csio_rnode_to_lnode(rn) = ln;
	csio_init_state(&rn->sm, csio_rns_uninit);
	INIT_LIST_HEAD(&rn->host_cmpl_q);
	csio_rn_flowid(rn) = CSIO_INVALID_IDX;

	/* Add rnode to list of lnodes->rnhead */
	list_add_tail(&rn->sm.sm_list, &ln->rnhead);

	return 0;
}

static void
csio_rnode_exit(struct csio_rnode *rn)
{
	list_del_init(&rn->sm.sm_list);
	CSIO_DB_ASSERT(list_empty(&rn->host_cmpl_q));
}
