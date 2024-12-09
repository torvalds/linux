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
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/utsname.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport_fc.h>
#include <linux/unaligned.h>
#include <scsi/fc/fc_els.h>
#include <scsi/fc/fc_fs.h>
#include <scsi/fc/fc_gs.h>
#include <scsi/fc/fc_ms.h>

#include "csio_hw.h"
#include "csio_mb.h"
#include "csio_lnode.h"
#include "csio_rnode.h"

int csio_fcoe_rnodes = 1024;
int csio_fdmi_enable = 1;

#define PORT_ID_PTR(_x)         ((uint8_t *)(&_x) + 1)

/* Lnode SM declarations */
static void csio_lns_uninit(struct csio_lnode *, enum csio_ln_ev);
static void csio_lns_online(struct csio_lnode *, enum csio_ln_ev);
static void csio_lns_ready(struct csio_lnode *, enum csio_ln_ev);
static void csio_lns_offline(struct csio_lnode *, enum csio_ln_ev);

static int csio_ln_mgmt_submit_req(struct csio_ioreq *,
		void (*io_cbfn) (struct csio_hw *, struct csio_ioreq *),
		enum fcoe_cmn_type, struct csio_dma_buf *, uint32_t);

/* LN event mapping */
static enum csio_ln_ev fwevt_to_lnevt[] = {
	CSIO_LNE_NONE,		/* None */
	CSIO_LNE_NONE,		/* PLOGI_ACC_RCVD  */
	CSIO_LNE_NONE,		/* PLOGI_RJT_RCVD  */
	CSIO_LNE_NONE,		/* PLOGI_RCVD	   */
	CSIO_LNE_NONE,		/* PLOGO_RCVD	   */
	CSIO_LNE_NONE,		/* PRLI_ACC_RCVD   */
	CSIO_LNE_NONE,		/* PRLI_RJT_RCVD   */
	CSIO_LNE_NONE,		/* PRLI_RCVD	   */
	CSIO_LNE_NONE,		/* PRLO_RCVD	   */
	CSIO_LNE_NONE,		/* NPORT_ID_CHGD   */
	CSIO_LNE_LOGO,		/* FLOGO_RCVD	   */
	CSIO_LNE_LOGO,		/* CLR_VIRT_LNK_RCVD */
	CSIO_LNE_FAB_INIT_DONE,/* FLOGI_ACC_RCVD   */
	CSIO_LNE_NONE,		/* FLOGI_RJT_RCVD   */
	CSIO_LNE_FAB_INIT_DONE,/* FDISC_ACC_RCVD   */
	CSIO_LNE_NONE,		/* FDISC_RJT_RCVD   */
	CSIO_LNE_NONE,		/* FLOGI_TMO_MAX_RETRY */
	CSIO_LNE_NONE,		/* IMPL_LOGO_ADISC_ACC */
	CSIO_LNE_NONE,		/* IMPL_LOGO_ADISC_RJT */
	CSIO_LNE_NONE,		/* IMPL_LOGO_ADISC_CNFLT */
	CSIO_LNE_NONE,		/* PRLI_TMO		*/
	CSIO_LNE_NONE,		/* ADISC_TMO		*/
	CSIO_LNE_NONE,		/* RSCN_DEV_LOST */
	CSIO_LNE_NONE,		/* SCR_ACC_RCVD */
	CSIO_LNE_NONE,		/* ADISC_RJT_RCVD */
	CSIO_LNE_NONE,		/* LOGO_SNT */
	CSIO_LNE_NONE,		/* PROTO_ERR_IMPL_LOGO */
};

#define CSIO_FWE_TO_LNE(_evt)	((_evt > PROTO_ERR_IMPL_LOGO) ?		\
						CSIO_LNE_NONE :	\
						fwevt_to_lnevt[_evt])

#define csio_ct_rsp(cp)		(((struct fc_ct_hdr *)cp)->ct_cmd)
#define csio_ct_reason(cp)	(((struct fc_ct_hdr *)cp)->ct_reason)
#define csio_ct_expl(cp)	(((struct fc_ct_hdr *)cp)->ct_explan)
#define csio_ct_get_pld(cp)	((void *)(((uint8_t *)cp) + FC_CT_HDR_LEN))

/*
 * csio_ln_match_by_portid - lookup lnode using given portid.
 * @hw: HW module
 * @portid: port-id.
 *
 * If found, returns lnode matching given portid otherwise returns NULL.
 */
static struct csio_lnode *
csio_ln_lookup_by_portid(struct csio_hw *hw, uint8_t portid)
{
	struct csio_lnode *ln;
	struct list_head *tmp;

	/* Match siblings lnode with portid */
	list_for_each(tmp, &hw->sln_head) {
		ln = (struct csio_lnode *) tmp;
		if (ln->portid == portid)
			return ln;
	}

	return NULL;
}

/*
 * csio_ln_lookup_by_vnpi - Lookup lnode using given vnp id.
 * @hw - HW module
 * @vnpi - vnp index.
 * Returns - If found, returns lnode matching given vnp id
 * otherwise returns NULL.
 */
static struct csio_lnode *
csio_ln_lookup_by_vnpi(struct csio_hw *hw, uint32_t vnp_id)
{
	struct list_head *tmp1, *tmp2;
	struct csio_lnode *sln = NULL, *cln = NULL;

	if (list_empty(&hw->sln_head)) {
		CSIO_INC_STATS(hw, n_lnlkup_miss);
		return NULL;
	}
	/* Traverse sibling lnodes */
	list_for_each(tmp1, &hw->sln_head) {
		sln = (struct csio_lnode *) tmp1;

		/* Match sibling lnode */
		if (sln->vnp_flowid == vnp_id)
			return sln;

		if (list_empty(&sln->cln_head))
			continue;

		/* Traverse children lnodes */
		list_for_each(tmp2, &sln->cln_head) {
			cln = (struct csio_lnode *) tmp2;

			if (cln->vnp_flowid == vnp_id)
				return cln;
		}
	}
	CSIO_INC_STATS(hw, n_lnlkup_miss);
	return NULL;
}

/**
 * csio_lnode_lookup_by_wwpn - Lookup lnode using given wwpn.
 * @hw:		HW module.
 * @wwpn:	WWPN.
 *
 * If found, returns lnode matching given wwpn, returns NULL otherwise.
 */
struct csio_lnode *
csio_lnode_lookup_by_wwpn(struct csio_hw *hw, uint8_t *wwpn)
{
	struct list_head *tmp1, *tmp2;
	struct csio_lnode *sln = NULL, *cln = NULL;

	if (list_empty(&hw->sln_head)) {
		CSIO_INC_STATS(hw, n_lnlkup_miss);
		return NULL;
	}
	/* Traverse sibling lnodes */
	list_for_each(tmp1, &hw->sln_head) {
		sln = (struct csio_lnode *) tmp1;

		/* Match sibling lnode */
		if (!memcmp(csio_ln_wwpn(sln), wwpn, 8))
			return sln;

		if (list_empty(&sln->cln_head))
			continue;

		/* Traverse children lnodes */
		list_for_each(tmp2, &sln->cln_head) {
			cln = (struct csio_lnode *) tmp2;

			if (!memcmp(csio_ln_wwpn(cln), wwpn, 8))
				return cln;
		}
	}
	return NULL;
}

/* FDMI */
static void
csio_fill_ct_iu(void *buf, uint8_t type, uint8_t sub_type, uint16_t op)
{
	struct fc_ct_hdr *cmd = (struct fc_ct_hdr *)buf;
	cmd->ct_rev = FC_CT_REV;
	cmd->ct_fs_type = type;
	cmd->ct_fs_subtype = sub_type;
	cmd->ct_cmd = htons(op);
}

static int
csio_hostname(uint8_t *buf, size_t buf_len)
{
	if (snprintf(buf, buf_len, "%s", init_utsname()->nodename) > 0)
		return 0;
	return -1;
}

static int
csio_osname(uint8_t *buf, size_t buf_len)
{
	if (snprintf(buf, buf_len, "%s %s %s",
		     init_utsname()->sysname,
		     init_utsname()->release,
		     init_utsname()->version) > 0)
		return 0;

	return -1;
}

static inline void
csio_append_attrib(uint8_t **ptr, uint16_t type, void *val, size_t val_len)
{
	uint16_t len;
	struct fc_fdmi_attr_entry *ae = (struct fc_fdmi_attr_entry *)*ptr;

	if (WARN_ON(val_len > U16_MAX))
		return;

	len = val_len;

	ae->type = htons(type);
	len += 4;		/* includes attribute type and length */
	len = (len + 3) & ~3;	/* should be multiple of 4 bytes */
	ae->len = htons(len);
	memcpy(ae->value, val, val_len);
	if (len > val_len)
		memset(ae->value + val_len, 0, len - val_len);
	*ptr += len;
}

/*
 * csio_ln_fdmi_done - FDMI registeration completion
 * @hw: HW context
 * @fdmi_req: fdmi request
 */
static void
csio_ln_fdmi_done(struct csio_hw *hw, struct csio_ioreq *fdmi_req)
{
	void *cmd;
	struct csio_lnode *ln = fdmi_req->lnode;

	if (fdmi_req->wr_status != FW_SUCCESS) {
		csio_ln_dbg(ln, "WR error:%x in processing fdmi rpa cmd\n",
			    fdmi_req->wr_status);
		CSIO_INC_STATS(ln, n_fdmi_err);
	}

	cmd = fdmi_req->dma_buf.vaddr;
	if (ntohs(csio_ct_rsp(cmd)) != FC_FS_ACC) {
		csio_ln_dbg(ln, "fdmi rpa cmd rejected reason %x expl %x\n",
			    csio_ct_reason(cmd), csio_ct_expl(cmd));
	}
}

/*
 * csio_ln_fdmi_rhba_cbfn - RHBA completion
 * @hw: HW context
 * @fdmi_req: fdmi request
 */
static void
csio_ln_fdmi_rhba_cbfn(struct csio_hw *hw, struct csio_ioreq *fdmi_req)
{
	void *cmd;
	uint8_t *pld;
	uint32_t len = 0;
	__be32 val;
	__be16 mfs;
	uint32_t numattrs = 0;
	struct csio_lnode *ln = fdmi_req->lnode;
	struct fs_fdmi_attrs *attrib_blk;
	struct fc_fdmi_port_name *port_name;
	uint8_t buf[64];
	uint8_t *fc4_type;
	unsigned long flags;

	if (fdmi_req->wr_status != FW_SUCCESS) {
		csio_ln_dbg(ln, "WR error:%x in processing fdmi rhba cmd\n",
			    fdmi_req->wr_status);
		CSIO_INC_STATS(ln, n_fdmi_err);
	}

	cmd = fdmi_req->dma_buf.vaddr;
	if (ntohs(csio_ct_rsp(cmd)) != FC_FS_ACC) {
		csio_ln_dbg(ln, "fdmi rhba cmd rejected reason %x expl %x\n",
			    csio_ct_reason(cmd), csio_ct_expl(cmd));
	}

	if (!csio_is_rnode_ready(fdmi_req->rnode)) {
		CSIO_INC_STATS(ln, n_fdmi_err);
		return;
	}

	/* Prepare CT hdr for RPA cmd */
	memset(cmd, 0, FC_CT_HDR_LEN);
	csio_fill_ct_iu(cmd, FC_FST_MGMT, FC_FDMI_SUBTYPE, FC_FDMI_RPA);

	/* Prepare RPA payload */
	pld = (uint8_t *)csio_ct_get_pld(cmd);
	port_name = (struct fc_fdmi_port_name *)pld;
	memcpy(&port_name->portname, csio_ln_wwpn(ln), 8);
	pld += sizeof(*port_name);

	/* Start appending Port attributes */
	attrib_blk = (struct fs_fdmi_attrs *)pld;
	attrib_blk->numattrs = 0;
	len += sizeof(attrib_blk->numattrs);
	pld += sizeof(attrib_blk->numattrs);

	fc4_type = &buf[0];
	memset(fc4_type, 0, FC_FDMI_PORT_ATTR_FC4TYPES_LEN);
	fc4_type[2] = 1;
	fc4_type[7] = 1;
	csio_append_attrib(&pld, FC_FDMI_PORT_ATTR_FC4TYPES,
			   fc4_type, FC_FDMI_PORT_ATTR_FC4TYPES_LEN);
	numattrs++;
	val = htonl(FC_PORTSPEED_1GBIT | FC_PORTSPEED_10GBIT);
	csio_append_attrib(&pld, FC_FDMI_PORT_ATTR_SUPPORTEDSPEED,
			   &val,
			   FC_FDMI_PORT_ATTR_SUPPORTEDSPEED_LEN);
	numattrs++;

	if (hw->pport[ln->portid].link_speed == FW_PORT_CAP_SPEED_1G)
		val = htonl(FC_PORTSPEED_1GBIT);
	else if (hw->pport[ln->portid].link_speed == FW_PORT_CAP_SPEED_10G)
		val = htonl(FC_PORTSPEED_10GBIT);
	else if (hw->pport[ln->portid].link_speed == FW_PORT_CAP32_SPEED_25G)
		val = htonl(FC_PORTSPEED_25GBIT);
	else if (hw->pport[ln->portid].link_speed == FW_PORT_CAP32_SPEED_40G)
		val = htonl(FC_PORTSPEED_40GBIT);
	else if (hw->pport[ln->portid].link_speed == FW_PORT_CAP32_SPEED_50G)
		val = htonl(FC_PORTSPEED_50GBIT);
	else if (hw->pport[ln->portid].link_speed == FW_PORT_CAP32_SPEED_100G)
		val = htonl(FC_PORTSPEED_100GBIT);
	else
		val = htonl(CSIO_HBA_PORTSPEED_UNKNOWN);
	csio_append_attrib(&pld, FC_FDMI_PORT_ATTR_CURRENTPORTSPEED,
			   &val, FC_FDMI_PORT_ATTR_CURRENTPORTSPEED_LEN);
	numattrs++;

	mfs = ln->ln_sparm.csp.sp_bb_data;
	csio_append_attrib(&pld, FC_FDMI_PORT_ATTR_MAXFRAMESIZE,
			   &mfs, sizeof(mfs));
	numattrs++;

	strcpy(buf, "csiostor");
	csio_append_attrib(&pld, FC_FDMI_PORT_ATTR_OSDEVICENAME, buf,
			   strlen(buf));
	numattrs++;

	if (!csio_hostname(buf, sizeof(buf))) {
		csio_append_attrib(&pld, FC_FDMI_PORT_ATTR_HOSTNAME,
				   buf, strlen(buf));
		numattrs++;
	}
	attrib_blk->numattrs = htonl(numattrs);
	len = (uint32_t)(pld - (uint8_t *)cmd);

	/* Submit FDMI RPA request */
	spin_lock_irqsave(&hw->lock, flags);
	if (csio_ln_mgmt_submit_req(fdmi_req, csio_ln_fdmi_done,
				FCOE_CT, &fdmi_req->dma_buf, len)) {
		CSIO_INC_STATS(ln, n_fdmi_err);
		csio_ln_dbg(ln, "Failed to issue fdmi rpa req\n");
	}
	spin_unlock_irqrestore(&hw->lock, flags);
}

/*
 * csio_ln_fdmi_dprt_cbfn - DPRT completion
 * @hw: HW context
 * @fdmi_req: fdmi request
 */
static void
csio_ln_fdmi_dprt_cbfn(struct csio_hw *hw, struct csio_ioreq *fdmi_req)
{
	void *cmd;
	uint8_t *pld;
	uint32_t len = 0;
	uint32_t numattrs = 0;
	__be32  maxpayload = htonl(65536);
	struct fc_fdmi_hba_identifier *hbaid;
	struct csio_lnode *ln = fdmi_req->lnode;
	struct fc_fdmi_rpl *reg_pl;
	struct fs_fdmi_attrs *attrib_blk;
	uint8_t buf[64];
	unsigned long flags;

	if (fdmi_req->wr_status != FW_SUCCESS) {
		csio_ln_dbg(ln, "WR error:%x in processing fdmi dprt cmd\n",
			    fdmi_req->wr_status);
		CSIO_INC_STATS(ln, n_fdmi_err);
	}

	if (!csio_is_rnode_ready(fdmi_req->rnode)) {
		CSIO_INC_STATS(ln, n_fdmi_err);
		return;
	}
	cmd = fdmi_req->dma_buf.vaddr;
	if (ntohs(csio_ct_rsp(cmd)) != FC_FS_ACC) {
		csio_ln_dbg(ln, "fdmi dprt cmd rejected reason %x expl %x\n",
			    csio_ct_reason(cmd), csio_ct_expl(cmd));
	}

	/* Prepare CT hdr for RHBA cmd */
	memset(cmd, 0, FC_CT_HDR_LEN);
	csio_fill_ct_iu(cmd, FC_FST_MGMT, FC_FDMI_SUBTYPE, FC_FDMI_RHBA);
	len = FC_CT_HDR_LEN;

	/* Prepare RHBA payload */
	pld = (uint8_t *)csio_ct_get_pld(cmd);
	hbaid = (struct fc_fdmi_hba_identifier *)pld;
	memcpy(&hbaid->id, csio_ln_wwpn(ln), 8); /* HBA identifer */
	pld += sizeof(*hbaid);

	/* Register one port per hba */
	reg_pl = (struct fc_fdmi_rpl *)pld;
	reg_pl->numport = htonl(1);
	memcpy(&reg_pl->port[0].portname, csio_ln_wwpn(ln), 8);
	pld += sizeof(*reg_pl);

	/* Start appending HBA attributes hba */
	attrib_blk = (struct fs_fdmi_attrs *)pld;
	attrib_blk->numattrs = 0;
	len += sizeof(attrib_blk->numattrs);
	pld += sizeof(attrib_blk->numattrs);

	csio_append_attrib(&pld, FC_FDMI_HBA_ATTR_NODENAME, csio_ln_wwnn(ln),
			   FC_FDMI_HBA_ATTR_NODENAME_LEN);
	numattrs++;

	memset(buf, 0, sizeof(buf));

	strcpy(buf, "Chelsio Communications");
	csio_append_attrib(&pld, FC_FDMI_HBA_ATTR_MANUFACTURER, buf,
			   strlen(buf));
	numattrs++;
	csio_append_attrib(&pld, FC_FDMI_HBA_ATTR_SERIALNUMBER,
			   hw->vpd.sn, sizeof(hw->vpd.sn));
	numattrs++;
	csio_append_attrib(&pld, FC_FDMI_HBA_ATTR_MODEL, hw->vpd.id,
			   sizeof(hw->vpd.id));
	numattrs++;
	csio_append_attrib(&pld, FC_FDMI_HBA_ATTR_MODELDESCRIPTION,
			   hw->model_desc, strlen(hw->model_desc));
	numattrs++;
	csio_append_attrib(&pld, FC_FDMI_HBA_ATTR_HARDWAREVERSION,
			   hw->hw_ver, sizeof(hw->hw_ver));
	numattrs++;
	csio_append_attrib(&pld, FC_FDMI_HBA_ATTR_FIRMWAREVERSION,
			   hw->fwrev_str, strlen(hw->fwrev_str));
	numattrs++;

	if (!csio_osname(buf, sizeof(buf))) {
		csio_append_attrib(&pld, FC_FDMI_HBA_ATTR_OSNAMEVERSION,
				   buf, strlen(buf));
		numattrs++;
	}

	csio_append_attrib(&pld, FC_FDMI_HBA_ATTR_MAXCTPAYLOAD,
			   &maxpayload, FC_FDMI_HBA_ATTR_MAXCTPAYLOAD_LEN);
	len = (uint32_t)(pld - (uint8_t *)cmd);
	numattrs++;
	attrib_blk->numattrs = htonl(numattrs);

	/* Submit FDMI RHBA request */
	spin_lock_irqsave(&hw->lock, flags);
	if (csio_ln_mgmt_submit_req(fdmi_req, csio_ln_fdmi_rhba_cbfn,
				FCOE_CT, &fdmi_req->dma_buf, len)) {
		CSIO_INC_STATS(ln, n_fdmi_err);
		csio_ln_dbg(ln, "Failed to issue fdmi rhba req\n");
	}
	spin_unlock_irqrestore(&hw->lock, flags);
}

/*
 * csio_ln_fdmi_dhba_cbfn - DHBA completion
 * @hw: HW context
 * @fdmi_req: fdmi request
 */
static void
csio_ln_fdmi_dhba_cbfn(struct csio_hw *hw, struct csio_ioreq *fdmi_req)
{
	struct csio_lnode *ln = fdmi_req->lnode;
	void *cmd;
	struct fc_fdmi_port_name *port_name;
	uint32_t len;
	unsigned long flags;

	if (fdmi_req->wr_status != FW_SUCCESS) {
		csio_ln_dbg(ln, "WR error:%x in processing fdmi dhba cmd\n",
			    fdmi_req->wr_status);
		CSIO_INC_STATS(ln, n_fdmi_err);
	}

	if (!csio_is_rnode_ready(fdmi_req->rnode)) {
		CSIO_INC_STATS(ln, n_fdmi_err);
		return;
	}
	cmd = fdmi_req->dma_buf.vaddr;
	if (ntohs(csio_ct_rsp(cmd)) != FC_FS_ACC) {
		csio_ln_dbg(ln, "fdmi dhba cmd rejected reason %x expl %x\n",
			    csio_ct_reason(cmd), csio_ct_expl(cmd));
	}

	/* Send FDMI cmd to de-register any Port attributes if registered
	 * before
	 */

	/* Prepare FDMI DPRT cmd */
	memset(cmd, 0, FC_CT_HDR_LEN);
	csio_fill_ct_iu(cmd, FC_FST_MGMT, FC_FDMI_SUBTYPE, FC_FDMI_DPRT);
	len = FC_CT_HDR_LEN;
	port_name = (struct fc_fdmi_port_name *)csio_ct_get_pld(cmd);
	memcpy(&port_name->portname, csio_ln_wwpn(ln), 8);
	len += sizeof(*port_name);

	/* Submit FDMI request */
	spin_lock_irqsave(&hw->lock, flags);
	if (csio_ln_mgmt_submit_req(fdmi_req, csio_ln_fdmi_dprt_cbfn,
				FCOE_CT, &fdmi_req->dma_buf, len)) {
		CSIO_INC_STATS(ln, n_fdmi_err);
		csio_ln_dbg(ln, "Failed to issue fdmi dprt req\n");
	}
	spin_unlock_irqrestore(&hw->lock, flags);
}

/**
 * csio_ln_fdmi_start - Start an FDMI request.
 * @ln:		lnode
 * @context:	session context
 *
 * Issued with lock held.
 */
int
csio_ln_fdmi_start(struct csio_lnode *ln, void *context)
{
	struct csio_ioreq *fdmi_req;
	struct csio_rnode *fdmi_rn = (struct csio_rnode *)context;
	void *cmd;
	struct fc_fdmi_hba_identifier *hbaid;
	uint32_t len;

	if (!(ln->flags & CSIO_LNF_FDMI_ENABLE))
		return -EPROTONOSUPPORT;

	if (!csio_is_rnode_ready(fdmi_rn))
		CSIO_INC_STATS(ln, n_fdmi_err);

	/* Send FDMI cmd to de-register any HBA attributes if registered
	 * before
	 */

	fdmi_req = ln->mgmt_req;
	fdmi_req->lnode = ln;
	fdmi_req->rnode = fdmi_rn;

	/* Prepare FDMI DHBA cmd */
	cmd = fdmi_req->dma_buf.vaddr;
	memset(cmd, 0, FC_CT_HDR_LEN);
	csio_fill_ct_iu(cmd, FC_FST_MGMT, FC_FDMI_SUBTYPE, FC_FDMI_DHBA);
	len = FC_CT_HDR_LEN;

	hbaid = (struct fc_fdmi_hba_identifier *)csio_ct_get_pld(cmd);
	memcpy(&hbaid->id, csio_ln_wwpn(ln), 8);
	len += sizeof(*hbaid);

	/* Submit FDMI request */
	if (csio_ln_mgmt_submit_req(fdmi_req, csio_ln_fdmi_dhba_cbfn,
					FCOE_CT, &fdmi_req->dma_buf, len)) {
		CSIO_INC_STATS(ln, n_fdmi_err);
		csio_ln_dbg(ln, "Failed to issue fdmi dhba req\n");
	}

	return 0;
}

/*
 * csio_ln_vnp_read_cbfn - vnp read completion handler.
 * @hw: HW lnode
 * @cbfn: Completion handler.
 *
 * Reads vnp response and updates ln parameters.
 */
static void
csio_ln_vnp_read_cbfn(struct csio_hw *hw, struct csio_mb *mbp)
{
	struct csio_lnode *ln = ((struct csio_lnode *)mbp->priv);
	struct fw_fcoe_vnp_cmd *rsp = (struct fw_fcoe_vnp_cmd *)(mbp->mb);
	struct fc_els_csp *csp;
	struct fc_els_cssp *clsp;
	enum fw_retval retval;
	__be32 nport_id = 0;

	retval = FW_CMD_RETVAL_G(ntohl(rsp->alloc_to_len16));
	if (retval != FW_SUCCESS) {
		csio_err(hw, "FCOE VNP read cmd returned error:0x%x\n", retval);
		mempool_free(mbp, hw->mb_mempool);
		return;
	}

	spin_lock_irq(&hw->lock);

	memcpy(ln->mac, rsp->vnport_mac, sizeof(ln->mac));
	memcpy(&nport_id, &rsp->vnport_mac[3], sizeof(uint8_t)*3);
	ln->nport_id = ntohl(nport_id);
	ln->nport_id = ln->nport_id >> 8;

	/* Update WWNs */
	/*
	 * This may look like a duplication of what csio_fcoe_enable_link()
	 * does, but is absolutely necessary if the vnpi changes between
	 * a FCOE LINK UP and FCOE LINK DOWN.
	 */
	memcpy(csio_ln_wwnn(ln), rsp->vnport_wwnn, 8);
	memcpy(csio_ln_wwpn(ln), rsp->vnport_wwpn, 8);

	/* Copy common sparam */
	csp = (struct fc_els_csp *)rsp->cmn_srv_parms;
	ln->ln_sparm.csp.sp_hi_ver = csp->sp_hi_ver;
	ln->ln_sparm.csp.sp_lo_ver = csp->sp_lo_ver;
	ln->ln_sparm.csp.sp_bb_cred = csp->sp_bb_cred;
	ln->ln_sparm.csp.sp_features = csp->sp_features;
	ln->ln_sparm.csp.sp_bb_data = csp->sp_bb_data;
	ln->ln_sparm.csp.sp_r_a_tov = csp->sp_r_a_tov;
	ln->ln_sparm.csp.sp_e_d_tov = csp->sp_e_d_tov;

	/* Copy word 0 & word 1 of class sparam */
	clsp = (struct fc_els_cssp *)rsp->clsp_word_0_1;
	ln->ln_sparm.clsp[2].cp_class = clsp->cp_class;
	ln->ln_sparm.clsp[2].cp_init = clsp->cp_init;
	ln->ln_sparm.clsp[2].cp_recip = clsp->cp_recip;
	ln->ln_sparm.clsp[2].cp_rdfs = clsp->cp_rdfs;

	spin_unlock_irq(&hw->lock);

	mempool_free(mbp, hw->mb_mempool);

	/* Send an event to update local attribs */
	csio_lnode_async_event(ln, CSIO_LN_FC_ATTRIB_UPDATE);
}

/*
 * csio_ln_vnp_read - Read vnp params.
 * @ln: lnode
 * @cbfn: Completion handler.
 *
 * Issued with lock held.
 */
static int
csio_ln_vnp_read(struct csio_lnode *ln,
		void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct csio_hw *hw = ln->hwp;
	struct csio_mb  *mbp;

	/* Allocate Mbox request */
	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		return -ENOMEM;
	}

	/* Prepare VNP Command */
	csio_fcoe_vnp_read_init_mb(ln, mbp,
				    CSIO_MB_DEFAULT_TMO,
				    ln->fcf_flowid,
				    ln->vnp_flowid,
				    cbfn);

	/* Issue MBOX cmd */
	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "Failed to issue mbox FCoE VNP command\n");
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	return 0;
}

/*
 * csio_fcoe_enable_link - Enable fcoe link.
 * @ln: lnode
 * @enable: enable/disable
 * Issued with lock held.
 * Issues mbox cmd to bring up FCOE link on port associated with given ln.
 */
static int
csio_fcoe_enable_link(struct csio_lnode *ln, bool enable)
{
	struct csio_hw *hw = ln->hwp;
	struct csio_mb  *mbp;
	enum fw_retval retval;
	uint8_t portid;
	uint8_t sub_op;
	struct fw_fcoe_link_cmd *lcmd;
	int i;

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		return -ENOMEM;
	}

	portid = ln->portid;
	sub_op = enable ? FCOE_LINK_UP : FCOE_LINK_DOWN;

	csio_dbg(hw, "bringing FCOE LINK %s on Port:%d\n",
		 sub_op ? "UP" : "DOWN", portid);

	csio_write_fcoe_link_cond_init_mb(ln, mbp, CSIO_MB_DEFAULT_TMO,
					  portid, sub_op, 0, 0, 0, NULL);

	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "failed to issue FCOE LINK cmd on port[%d]\n",
			portid);
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	retval = csio_mb_fw_retval(mbp);
	if (retval != FW_SUCCESS) {
		csio_err(hw,
			 "FCOE LINK %s cmd on port[%d] failed with "
			 "ret:x%x\n", sub_op ? "UP" : "DOWN", portid, retval);
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	if (!enable)
		goto out;

	lcmd = (struct fw_fcoe_link_cmd *)mbp->mb;

	memcpy(csio_ln_wwnn(ln), lcmd->vnport_wwnn, 8);
	memcpy(csio_ln_wwpn(ln), lcmd->vnport_wwpn, 8);

	for (i = 0; i < CSIO_MAX_PPORTS; i++)
		if (hw->pport[i].portid == portid)
			memcpy(hw->pport[i].mac, lcmd->phy_mac, 6);

out:
	mempool_free(mbp, hw->mb_mempool);
	return 0;
}

/*
 * csio_ln_read_fcf_cbfn - Read fcf parameters
 * @ln: lnode
 *
 * read fcf response and Update ln fcf information.
 */
static void
csio_ln_read_fcf_cbfn(struct csio_hw *hw, struct csio_mb *mbp)
{
	struct csio_lnode *ln = (struct csio_lnode *)mbp->priv;
	struct csio_fcf_info	*fcf_info;
	struct fw_fcoe_fcf_cmd *rsp =
				(struct fw_fcoe_fcf_cmd *)(mbp->mb);
	enum fw_retval retval;

	retval = FW_CMD_RETVAL_G(ntohl(rsp->retval_len16));
	if (retval != FW_SUCCESS) {
		csio_ln_err(ln, "FCOE FCF cmd failed with ret x%x\n",
				retval);
		mempool_free(mbp, hw->mb_mempool);
		return;
	}

	spin_lock_irq(&hw->lock);
	fcf_info = ln->fcfinfo;
	fcf_info->priority = FW_FCOE_FCF_CMD_PRIORITY_GET(
					ntohs(rsp->priority_pkd));
	fcf_info->vf_id = ntohs(rsp->vf_id);
	fcf_info->vlan_id = rsp->vlan_id;
	fcf_info->max_fcoe_size = ntohs(rsp->max_fcoe_size);
	fcf_info->fka_adv = be32_to_cpu(rsp->fka_adv);
	fcf_info->fcfi = FW_FCOE_FCF_CMD_FCFI_GET(ntohl(rsp->op_to_fcfi));
	fcf_info->fpma = FW_FCOE_FCF_CMD_FPMA_GET(rsp->fpma_to_portid);
	fcf_info->spma = FW_FCOE_FCF_CMD_SPMA_GET(rsp->fpma_to_portid);
	fcf_info->login = FW_FCOE_FCF_CMD_LOGIN_GET(rsp->fpma_to_portid);
	fcf_info->portid = FW_FCOE_FCF_CMD_PORTID_GET(rsp->fpma_to_portid);
	memcpy(fcf_info->fc_map, rsp->fc_map, sizeof(fcf_info->fc_map));
	memcpy(fcf_info->mac, rsp->mac, sizeof(fcf_info->mac));
	memcpy(fcf_info->name_id, rsp->name_id, sizeof(fcf_info->name_id));
	memcpy(fcf_info->fabric, rsp->fabric, sizeof(fcf_info->fabric));
	memcpy(fcf_info->spma_mac, rsp->spma_mac, sizeof(fcf_info->spma_mac));

	spin_unlock_irq(&hw->lock);

	mempool_free(mbp, hw->mb_mempool);
}

/*
 * csio_ln_read_fcf_entry - Read fcf entry.
 * @ln: lnode
 * @cbfn: Completion handler.
 *
 * Issued with lock held.
 */
static int
csio_ln_read_fcf_entry(struct csio_lnode *ln,
			void (*cbfn) (struct csio_hw *, struct csio_mb *))
{
	struct csio_hw *hw = ln->hwp;
	struct csio_mb  *mbp;

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		CSIO_INC_STATS(hw, n_err_nomem);
		return -ENOMEM;
	}

	/* Get FCoE FCF information */
	csio_fcoe_read_fcf_init_mb(ln, mbp, CSIO_MB_DEFAULT_TMO,
				      ln->portid, ln->fcf_flowid, cbfn);

	if (csio_mb_issue(hw, mbp)) {
		csio_err(hw, "failed to issue FCOE FCF cmd\n");
		mempool_free(mbp, hw->mb_mempool);
		return -EINVAL;
	}

	return 0;
}

/*
 * csio_handle_link_up - Logical Linkup event.
 * @hw - HW module.
 * @portid - Physical port number
 * @fcfi - FCF index.
 * @vnpi - VNP index.
 * Returns - none.
 *
 * This event is received from FW, when virtual link is established between
 * Physical port[ENode] and FCF. If its new vnpi, then local node object is
 * created on this FCF and set to [ONLINE] state.
 * Lnode waits for FW_RDEV_CMD event to be received indicating that
 * Fabric login is completed and lnode moves to [READY] state.
 *
 * This called with hw lock held
 */
static void
csio_handle_link_up(struct csio_hw *hw, uint8_t portid, uint32_t fcfi,
		    uint32_t vnpi)
{
	struct csio_lnode *ln = NULL;

	/* Lookup lnode based on vnpi */
	ln = csio_ln_lookup_by_vnpi(hw, vnpi);
	if (!ln) {
		/* Pick lnode based on portid */
		ln = csio_ln_lookup_by_portid(hw, portid);
		if (!ln) {
			csio_err(hw, "failed to lookup fcoe lnode on port:%d\n",
				portid);
			CSIO_DB_ASSERT(0);
			return;
		}

		/* Check if lnode has valid vnp flowid */
		if (ln->vnp_flowid != CSIO_INVALID_IDX) {
			/* New VN-Port */
			spin_unlock_irq(&hw->lock);
			csio_lnode_alloc(hw);
			spin_lock_irq(&hw->lock);
			if (!ln) {
				csio_err(hw,
					 "failed to allocate fcoe lnode"
					 "for port:%d vnpi:x%x\n",
					 portid, vnpi);
				CSIO_DB_ASSERT(0);
				return;
			}
			ln->portid = portid;
		}
		ln->vnp_flowid = vnpi;
		ln->dev_num &= ~0xFFFF;
		ln->dev_num |= vnpi;
	}

	/*Initialize fcfi */
	ln->fcf_flowid = fcfi;

	csio_info(hw, "Port:%d - FCOE LINK UP\n", portid);

	CSIO_INC_STATS(ln, n_link_up);

	/* Send LINKUP event to SM */
	csio_post_event(&ln->sm, CSIO_LNE_LINKUP);
}

/*
 * csio_post_event_rns
 * @ln - FCOE lnode
 * @evt - Given rnode event
 * Returns - none
 *
 * Posts given rnode event to all FCOE rnodes connected with given Lnode.
 * This routine is invoked when lnode receives LINK_DOWN/DOWN_LINK/CLOSE
 * event.
 *
 * This called with hw lock held
 */
static void
csio_post_event_rns(struct csio_lnode *ln, enum csio_rn_ev evt)
{
	struct csio_rnode *rnhead = (struct csio_rnode *) &ln->rnhead;
	struct list_head *tmp, *next;
	struct csio_rnode *rn;

	list_for_each_safe(tmp, next, &rnhead->sm.sm_list) {
		rn = (struct csio_rnode *) tmp;
		csio_post_event(&rn->sm, evt);
	}
}

/*
 * csio_cleanup_rns
 * @ln - FCOE lnode
 * Returns - none
 *
 * Frees all FCOE rnodes connected with given Lnode.
 *
 * This called with hw lock held
 */
static void
csio_cleanup_rns(struct csio_lnode *ln)
{
	struct csio_rnode *rnhead = (struct csio_rnode *) &ln->rnhead;
	struct list_head *tmp, *next_rn;
	struct csio_rnode *rn;

	list_for_each_safe(tmp, next_rn, &rnhead->sm.sm_list) {
		rn = (struct csio_rnode *) tmp;
		csio_put_rnode(ln, rn);
	}

}

/*
 * csio_post_event_lns
 * @ln - FCOE lnode
 * @evt - Given lnode event
 * Returns - none
 *
 * Posts given lnode event to all FCOE lnodes connected with given Lnode.
 * This routine is invoked when lnode receives LINK_DOWN/DOWN_LINK/CLOSE
 * event.
 *
 * This called with hw lock held
 */
static void
csio_post_event_lns(struct csio_lnode *ln, enum csio_ln_ev evt)
{
	struct list_head *tmp;
	struct csio_lnode *cln, *sln;

	/* If NPIV lnode, send evt only to that and return */
	if (csio_is_npiv_ln(ln)) {
		csio_post_event(&ln->sm, evt);
		return;
	}

	sln = ln;
	/* Traverse children lnodes list and send evt */
	list_for_each(tmp, &sln->cln_head) {
		cln = (struct csio_lnode *) tmp;
		csio_post_event(&cln->sm, evt);
	}

	/* Send evt to parent lnode */
	csio_post_event(&ln->sm, evt);
}

/*
 * csio_ln_down - Lcoal nport is down
 * @ln - FCOE Lnode
 * Returns - none
 *
 * Sends LINK_DOWN events to Lnode and its associated NPIVs lnodes.
 *
 * This called with hw lock held
 */
static void
csio_ln_down(struct csio_lnode *ln)
{
	csio_post_event_lns(ln, CSIO_LNE_LINK_DOWN);
}

/*
 * csio_handle_link_down - Logical Linkdown event.
 * @hw - HW module.
 * @portid - Physical port number
 * @fcfi - FCF index.
 * @vnpi - VNP index.
 * Returns - none
 *
 * This event is received from FW, when virtual link goes down between
 * Physical port[ENode] and FCF. Lnode and its associated NPIVs lnode hosted on
 * this vnpi[VN-Port] will be de-instantiated.
 *
 * This called with hw lock held
 */
static void
csio_handle_link_down(struct csio_hw *hw, uint8_t portid, uint32_t fcfi,
		      uint32_t vnpi)
{
	struct csio_fcf_info *fp;
	struct csio_lnode *ln;

	/* Lookup lnode based on vnpi */
	ln = csio_ln_lookup_by_vnpi(hw, vnpi);
	if (ln) {
		fp = ln->fcfinfo;
		CSIO_INC_STATS(ln, n_link_down);

		/*Warn if linkdown received if lnode is not in ready state */
		if (!csio_is_lnode_ready(ln)) {
			csio_ln_warn(ln,
				"warn: FCOE link is already in offline "
				"Ignoring Fcoe linkdown event on portid %d\n",
				 portid);
			CSIO_INC_STATS(ln, n_evt_drop);
			return;
		}

		/* Verify portid */
		if (fp->portid != portid) {
			csio_ln_warn(ln,
				"warn: FCOE linkdown recv with "
				"invalid port %d\n", portid);
			CSIO_INC_STATS(ln, n_evt_drop);
			return;
		}

		/* verify fcfi */
		if (ln->fcf_flowid != fcfi) {
			csio_ln_warn(ln,
				"warn: FCOE linkdown recv with "
				"invalid fcfi x%x\n", fcfi);
			CSIO_INC_STATS(ln, n_evt_drop);
			return;
		}

		csio_info(hw, "Port:%d - FCOE LINK DOWN\n", portid);

		/* Send LINK_DOWN event to lnode s/m */
		csio_ln_down(ln);

		return;
	} else {
		csio_warn(hw,
			  "warn: FCOE linkdown recv with invalid vnpi x%x\n",
			  vnpi);
		CSIO_INC_STATS(hw, n_evt_drop);
	}
}

/*
 * csio_is_lnode_ready - Checks FCOE lnode is in ready state.
 * @ln: Lnode module
 *
 * Returns True if FCOE lnode is in ready state.
 */
int
csio_is_lnode_ready(struct csio_lnode *ln)
{
	return (csio_get_state(ln) == csio_lns_ready);
}

/*****************************************************************************/
/* START: Lnode SM                                                           */
/*****************************************************************************/
/*
 * csio_lns_uninit - The request in uninit state.
 * @ln - FCOE lnode.
 * @evt - Event to be processed.
 *
 * Process the given lnode event which is currently in "uninit" state.
 * Invoked with HW lock held.
 * Return - none.
 */
static void
csio_lns_uninit(struct csio_lnode *ln, enum csio_ln_ev evt)
{
	struct csio_hw *hw = csio_lnode_to_hw(ln);
	struct csio_lnode *rln = hw->rln;
	int rv;

	CSIO_INC_STATS(ln, n_evt_sm[evt]);
	switch (evt) {
	case CSIO_LNE_LINKUP:
		csio_set_state(&ln->sm, csio_lns_online);
		/* Read FCF only for physical lnode */
		if (csio_is_phys_ln(ln)) {
			rv = csio_ln_read_fcf_entry(ln,
					csio_ln_read_fcf_cbfn);
			if (rv != 0) {
				/* TODO: Send HW RESET event */
				CSIO_INC_STATS(ln, n_err);
				break;
			}

			/* Add FCF record */
			list_add_tail(&ln->fcfinfo->list, &rln->fcf_lsthead);
		}

		rv = csio_ln_vnp_read(ln, csio_ln_vnp_read_cbfn);
		if (rv != 0) {
			/* TODO: Send HW RESET event */
			CSIO_INC_STATS(ln, n_err);
		}
		break;

	case CSIO_LNE_DOWN_LINK:
		break;

	default:
		csio_ln_dbg(ln,
			    "unexp ln event %d recv from did:x%x in "
			    "ln state[uninit].\n", evt, ln->nport_id);
		CSIO_INC_STATS(ln, n_evt_unexp);
		break;
	} /* switch event */
}

/*
 * csio_lns_online - The request in online state.
 * @ln - FCOE lnode.
 * @evt - Event to be processed.
 *
 * Process the given lnode event which is currently in "online" state.
 * Invoked with HW lock held.
 * Return - none.
 */
static void
csio_lns_online(struct csio_lnode *ln, enum csio_ln_ev evt)
{
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	CSIO_INC_STATS(ln, n_evt_sm[evt]);
	switch (evt) {
	case CSIO_LNE_LINKUP:
		csio_ln_warn(ln,
			     "warn: FCOE link is up already "
			     "Ignoring linkup on port:%d\n", ln->portid);
		CSIO_INC_STATS(ln, n_evt_drop);
		break;

	case CSIO_LNE_FAB_INIT_DONE:
		csio_set_state(&ln->sm, csio_lns_ready);

		spin_unlock_irq(&hw->lock);
		csio_lnode_async_event(ln, CSIO_LN_FC_LINKUP);
		spin_lock_irq(&hw->lock);

		break;

	case CSIO_LNE_LINK_DOWN:
	case CSIO_LNE_DOWN_LINK:
		csio_set_state(&ln->sm, csio_lns_uninit);
		if (csio_is_phys_ln(ln)) {
			/* Remove FCF entry */
			list_del_init(&ln->fcfinfo->list);
		}
		break;

	default:
		csio_ln_dbg(ln,
			    "unexp ln event %d recv from did:x%x in "
			    "ln state[uninit].\n", evt, ln->nport_id);
		CSIO_INC_STATS(ln, n_evt_unexp);

		break;
	} /* switch event */
}

/*
 * csio_lns_ready - The request in ready state.
 * @ln - FCOE lnode.
 * @evt - Event to be processed.
 *
 * Process the given lnode event which is currently in "ready" state.
 * Invoked with HW lock held.
 * Return - none.
 */
static void
csio_lns_ready(struct csio_lnode *ln, enum csio_ln_ev evt)
{
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	CSIO_INC_STATS(ln, n_evt_sm[evt]);
	switch (evt) {
	case CSIO_LNE_FAB_INIT_DONE:
		csio_ln_dbg(ln,
			    "ignoring event %d recv from did x%x"
			    "in ln state[ready].\n", evt, ln->nport_id);
		CSIO_INC_STATS(ln, n_evt_drop);
		break;

	case CSIO_LNE_LINK_DOWN:
		csio_set_state(&ln->sm, csio_lns_offline);
		csio_post_event_rns(ln, CSIO_RNFE_DOWN);

		spin_unlock_irq(&hw->lock);
		csio_lnode_async_event(ln, CSIO_LN_FC_LINKDOWN);
		spin_lock_irq(&hw->lock);

		if (csio_is_phys_ln(ln)) {
			/* Remove FCF entry */
			list_del_init(&ln->fcfinfo->list);
		}
		break;

	case CSIO_LNE_DOWN_LINK:
		csio_set_state(&ln->sm, csio_lns_offline);
		csio_post_event_rns(ln, CSIO_RNFE_DOWN);

		/* Host need to issue aborts in case if FW has not returned
		 * WRs with status "ABORTED"
		 */
		spin_unlock_irq(&hw->lock);
		csio_lnode_async_event(ln, CSIO_LN_FC_LINKDOWN);
		spin_lock_irq(&hw->lock);

		if (csio_is_phys_ln(ln)) {
			/* Remove FCF entry */
			list_del_init(&ln->fcfinfo->list);
		}
		break;

	case CSIO_LNE_CLOSE:
		csio_set_state(&ln->sm, csio_lns_uninit);
		csio_post_event_rns(ln, CSIO_RNFE_CLOSE);
		break;

	case CSIO_LNE_LOGO:
		csio_set_state(&ln->sm, csio_lns_offline);
		csio_post_event_rns(ln, CSIO_RNFE_DOWN);
		break;

	default:
		csio_ln_dbg(ln,
			    "unexp ln event %d recv from did:x%x in "
			    "ln state[uninit].\n", evt, ln->nport_id);
		CSIO_INC_STATS(ln, n_evt_unexp);
		CSIO_DB_ASSERT(0);
		break;
	} /* switch event */
}

/*
 * csio_lns_offline - The request in offline state.
 * @ln - FCOE lnode.
 * @evt - Event to be processed.
 *
 * Process the given lnode event which is currently in "offline" state.
 * Invoked with HW lock held.
 * Return - none.
 */
static void
csio_lns_offline(struct csio_lnode *ln, enum csio_ln_ev evt)
{
	struct csio_hw *hw = csio_lnode_to_hw(ln);
	struct csio_lnode *rln = hw->rln;
	int rv;

	CSIO_INC_STATS(ln, n_evt_sm[evt]);
	switch (evt) {
	case CSIO_LNE_LINKUP:
		csio_set_state(&ln->sm, csio_lns_online);
		/* Read FCF only for physical lnode */
		if (csio_is_phys_ln(ln)) {
			rv = csio_ln_read_fcf_entry(ln,
					csio_ln_read_fcf_cbfn);
			if (rv != 0) {
				/* TODO: Send HW RESET event */
				CSIO_INC_STATS(ln, n_err);
				break;
			}

			/* Add FCF record */
			list_add_tail(&ln->fcfinfo->list, &rln->fcf_lsthead);
		}

		rv = csio_ln_vnp_read(ln, csio_ln_vnp_read_cbfn);
		if (rv != 0) {
			/* TODO: Send HW RESET event */
			CSIO_INC_STATS(ln, n_err);
		}
		break;

	case CSIO_LNE_LINK_DOWN:
	case CSIO_LNE_DOWN_LINK:
	case CSIO_LNE_LOGO:
		csio_ln_dbg(ln,
			    "ignoring event %d recv from did x%x"
			    "in ln state[offline].\n", evt, ln->nport_id);
		CSIO_INC_STATS(ln, n_evt_drop);
		break;

	case CSIO_LNE_CLOSE:
		csio_set_state(&ln->sm, csio_lns_uninit);
		csio_post_event_rns(ln, CSIO_RNFE_CLOSE);
		break;

	default:
		csio_ln_dbg(ln,
			    "unexp ln event %d recv from did:x%x in "
			    "ln state[offline]\n", evt, ln->nport_id);
		CSIO_INC_STATS(ln, n_evt_unexp);
		CSIO_DB_ASSERT(0);
		break;
	} /* switch event */
}

/*****************************************************************************/
/* END: Lnode SM                                                             */
/*****************************************************************************/

static void
csio_free_fcfinfo(struct kref *kref)
{
	struct csio_fcf_info *fcfinfo = container_of(kref,
						struct csio_fcf_info, kref);
	kfree(fcfinfo);
}

/* Helper routines for attributes  */
/*
 * csio_lnode_state_to_str - Get current state of FCOE lnode.
 * @ln - lnode
 * @str - state of lnode.
 *
 */
void
csio_lnode_state_to_str(struct csio_lnode *ln, int8_t *str)
{
	if (csio_get_state(ln) == csio_lns_uninit) {
		strcpy(str, "UNINIT");
		return;
	}
	if (csio_get_state(ln) == csio_lns_ready) {
		strcpy(str, "READY");
		return;
	}
	if (csio_get_state(ln) == csio_lns_offline) {
		strcpy(str, "OFFLINE");
		return;
	}
	strcpy(str, "UNKNOWN");
} /* csio_lnode_state_to_str */


int
csio_get_phy_port_stats(struct csio_hw *hw, uint8_t portid,
			struct fw_fcoe_port_stats *port_stats)
{
	struct csio_mb  *mbp;
	struct fw_fcoe_port_cmd_params portparams;
	enum fw_retval retval;
	int idx;

	mbp = mempool_alloc(hw->mb_mempool, GFP_ATOMIC);
	if (!mbp) {
		csio_err(hw, "FCoE FCF PARAMS command out of memory!\n");
		return -EINVAL;
	}
	portparams.portid = portid;

	for (idx = 1; idx <= 3; idx++) {
		portparams.idx = (idx-1)*6 + 1;
		portparams.nstats = 6;
		if (idx == 3)
			portparams.nstats = 4;
		csio_fcoe_read_portparams_init_mb(hw, mbp, CSIO_MB_DEFAULT_TMO,
							&portparams, NULL);
		if (csio_mb_issue(hw, mbp)) {
			csio_err(hw, "Issue of FCoE port params failed!\n");
			mempool_free(mbp, hw->mb_mempool);
			return -EINVAL;
		}
		csio_mb_process_portparams_rsp(hw, mbp, &retval,
						&portparams, port_stats);
	}

	mempool_free(mbp, hw->mb_mempool);
	return 0;
}

/*
 * csio_ln_mgmt_wr_handler -Mgmt Work Request handler.
 * @wr - WR.
 * @len - WR len.
 * This handler is invoked when an outstanding mgmt WR is completed.
 * Its invoked in the context of FW event worker thread for every
 * mgmt event received.
 * Return - none.
 */

static void
csio_ln_mgmt_wr_handler(struct csio_hw *hw, void *wr, uint32_t len)
{
	struct csio_mgmtm *mgmtm = csio_hw_to_mgmtm(hw);
	struct csio_ioreq *io_req = NULL;
	struct fw_fcoe_els_ct_wr *wr_cmd;


	wr_cmd = (struct fw_fcoe_els_ct_wr *) wr;

	if (len < sizeof(struct fw_fcoe_els_ct_wr)) {
		csio_err(mgmtm->hw,
			 "Invalid ELS CT WR length recvd, len:%x\n", len);
		mgmtm->stats.n_err++;
		return;
	}

	io_req = (struct csio_ioreq *) ((uintptr_t) wr_cmd->cookie);
	io_req->wr_status = csio_wr_status(wr_cmd);

	/* lookup ioreq exists in our active Q */
	spin_lock_irq(&hw->lock);
	if (csio_mgmt_req_lookup(mgmtm, io_req) != 0) {
		csio_err(mgmtm->hw,
			"Error- Invalid IO handle recv in WR. handle: %p\n",
			io_req);
		mgmtm->stats.n_err++;
		spin_unlock_irq(&hw->lock);
		return;
	}

	mgmtm = csio_hw_to_mgmtm(hw);

	/* Dequeue from active queue */
	list_del_init(&io_req->sm.sm_list);
	mgmtm->stats.n_active--;
	spin_unlock_irq(&hw->lock);

	/* io_req will be freed by completion handler */
	if (io_req->io_cbfn)
		io_req->io_cbfn(hw, io_req);
}

/**
 * csio_fcoe_fwevt_handler - Event handler for Firmware FCoE events.
 * @hw:		HW module
 * @cpl_op:	CPL opcode
 * @cmd:	FW cmd/WR.
 *
 * Process received FCoE cmd/WR event from FW.
 */
void
csio_fcoe_fwevt_handler(struct csio_hw *hw, __u8 cpl_op, __be64 *cmd)
{
	struct csio_lnode *ln;
	struct csio_rnode *rn;
	uint8_t portid, opcode = *(uint8_t *)cmd;
	struct fw_fcoe_link_cmd *lcmd;
	struct fw_wr_hdr *wr;
	struct fw_rdev_wr *rdev_wr;
	enum fw_fcoe_link_status lstatus;
	uint32_t fcfi, rdev_flowid, vnpi;
	enum csio_ln_ev evt;

	if (cpl_op == CPL_FW6_MSG && opcode == FW_FCOE_LINK_CMD) {

		lcmd = (struct fw_fcoe_link_cmd *)cmd;
		lstatus = lcmd->lstatus;
		portid = FW_FCOE_LINK_CMD_PORTID_GET(
					ntohl(lcmd->op_to_portid));
		fcfi = FW_FCOE_LINK_CMD_FCFI_GET(ntohl(lcmd->sub_opcode_fcfi));
		vnpi = FW_FCOE_LINK_CMD_VNPI_GET(ntohl(lcmd->vnpi_pkd));

		if (lstatus == FCOE_LINKUP) {

			/* HW lock here */
			spin_lock_irq(&hw->lock);
			csio_handle_link_up(hw, portid, fcfi, vnpi);
			spin_unlock_irq(&hw->lock);
			/* HW un lock here */

		} else if (lstatus == FCOE_LINKDOWN) {

			/* HW lock here */
			spin_lock_irq(&hw->lock);
			csio_handle_link_down(hw, portid, fcfi, vnpi);
			spin_unlock_irq(&hw->lock);
			/* HW un lock here */
		} else {
			csio_warn(hw, "Unexpected FCOE LINK status:0x%x\n",
				  lcmd->lstatus);
			CSIO_INC_STATS(hw, n_cpl_unexp);
		}
	} else if (cpl_op == CPL_FW6_PLD) {
		wr = (struct fw_wr_hdr *) (cmd + 4);
		if (FW_WR_OP_G(be32_to_cpu(wr->hi))
			== FW_RDEV_WR) {

			rdev_wr = (struct fw_rdev_wr *) (cmd + 4);

			rdev_flowid = FW_RDEV_WR_FLOWID_GET(
					ntohl(rdev_wr->alloc_to_len16));
			vnpi = FW_RDEV_WR_ASSOC_FLOWID_GET(
				    ntohl(rdev_wr->flags_to_assoc_flowid));

			csio_dbg(hw,
				"FW_RDEV_WR: flowid:x%x ev_cause:x%x "
				"vnpi:0x%x\n", rdev_flowid,
				rdev_wr->event_cause, vnpi);

			if (rdev_wr->protocol != PROT_FCOE) {
				csio_err(hw,
					"FW_RDEV_WR: invalid proto:x%x "
					"received with flowid:x%x\n",
					rdev_wr->protocol,
					rdev_flowid);
				CSIO_INC_STATS(hw, n_evt_drop);
				return;
			}

			/* HW lock here */
			spin_lock_irq(&hw->lock);
			ln = csio_ln_lookup_by_vnpi(hw, vnpi);
			if (!ln) {
				csio_err(hw,
					"FW_DEV_WR: invalid vnpi:x%x received "
					"with flowid:x%x\n", vnpi, rdev_flowid);
				CSIO_INC_STATS(hw, n_evt_drop);
				goto out_pld;
			}

			rn = csio_confirm_rnode(ln, rdev_flowid,
					&rdev_wr->u.fcoe_rdev);
			if (!rn) {
				csio_ln_dbg(ln,
					"Failed to confirm rnode "
					"for flowid:x%x\n", rdev_flowid);
				CSIO_INC_STATS(hw, n_evt_drop);
				goto out_pld;
			}

			/* save previous event for debugging */
			ln->prev_evt = ln->cur_evt;
			ln->cur_evt = rdev_wr->event_cause;
			CSIO_INC_STATS(ln, n_evt_fw[rdev_wr->event_cause]);

			/* Translate all the fabric events to lnode SM events */
			evt = CSIO_FWE_TO_LNE(rdev_wr->event_cause);
			if (evt) {
				csio_ln_dbg(ln,
					"Posting event to lnode event:%d "
					"cause:%d flowid:x%x\n", evt,
					rdev_wr->event_cause, rdev_flowid);
				csio_post_event(&ln->sm, evt);
			}

			/* Handover event to rn SM here. */
			csio_rnode_fwevt_handler(rn, rdev_wr->event_cause);
out_pld:
			spin_unlock_irq(&hw->lock);
			return;
		} else {
			csio_warn(hw, "unexpected WR op(0x%x) recv\n",
				  FW_WR_OP_G(be32_to_cpu((wr->hi))));
			CSIO_INC_STATS(hw, n_cpl_unexp);
		}
	} else if (cpl_op == CPL_FW6_MSG) {
		wr = (struct fw_wr_hdr *) (cmd);
		if (FW_WR_OP_G(be32_to_cpu(wr->hi)) == FW_FCOE_ELS_CT_WR) {
			csio_ln_mgmt_wr_handler(hw, wr,
					sizeof(struct fw_fcoe_els_ct_wr));
		} else {
			csio_warn(hw, "unexpected WR op(0x%x) recv\n",
				  FW_WR_OP_G(be32_to_cpu((wr->hi))));
			CSIO_INC_STATS(hw, n_cpl_unexp);
		}
	} else {
		csio_warn(hw, "unexpected CPL op(0x%x) recv\n", opcode);
		CSIO_INC_STATS(hw, n_cpl_unexp);
	}
}

/**
 * csio_lnode_start - Kickstart lnode discovery.
 * @ln:		lnode
 *
 * This routine kickstarts the discovery by issuing an FCOE_LINK (up) command.
 */
int
csio_lnode_start(struct csio_lnode *ln)
{
	int rv = 0;
	if (csio_is_phys_ln(ln) && !(ln->flags & CSIO_LNF_LINK_ENABLE)) {
		rv = csio_fcoe_enable_link(ln, 1);
		ln->flags |= CSIO_LNF_LINK_ENABLE;
	}

	return rv;
}

/**
 * csio_lnode_stop - Stop the lnode.
 * @ln:		lnode
 *
 * This routine is invoked by HW module to stop lnode and its associated NPIV
 * lnodes.
 */
void
csio_lnode_stop(struct csio_lnode *ln)
{
	csio_post_event_lns(ln, CSIO_LNE_DOWN_LINK);
	if (csio_is_phys_ln(ln) && (ln->flags & CSIO_LNF_LINK_ENABLE)) {
		csio_fcoe_enable_link(ln, 0);
		ln->flags &= ~CSIO_LNF_LINK_ENABLE;
	}
	csio_ln_dbg(ln, "stopping ln :%p\n", ln);
}

/**
 * csio_lnode_close - Close an lnode.
 * @ln:		lnode
 *
 * This routine is invoked by HW module to close an lnode and its
 * associated NPIV lnodes. Lnode and its associated NPIV lnodes are
 * set to uninitialized state.
 */
void
csio_lnode_close(struct csio_lnode *ln)
{
	csio_post_event_lns(ln, CSIO_LNE_CLOSE);
	if (csio_is_phys_ln(ln))
		ln->vnp_flowid = CSIO_INVALID_IDX;

	csio_ln_dbg(ln, "closed ln :%p\n", ln);
}

/*
 * csio_ln_prep_ecwr - Prepare ELS/CT WR.
 * @io_req - IO request.
 * @wr_len - WR len
 * @immd_len - WR immediate data
 * @sub_op - Sub opcode
 * @sid - source portid.
 * @did - destination portid
 * @flow_id - flowid
 * @fw_wr - ELS/CT WR to be prepared.
 * Returns: 0 - on success
 */
static int
csio_ln_prep_ecwr(struct csio_ioreq *io_req, uint32_t wr_len,
		      uint32_t immd_len, uint8_t sub_op, uint32_t sid,
		      uint32_t did, uint32_t flow_id, uint8_t *fw_wr)
{
	struct fw_fcoe_els_ct_wr *wr;
	__be32 port_id;

	wr  = (struct fw_fcoe_els_ct_wr *)fw_wr;
	wr->op_immdlen = cpu_to_be32(FW_WR_OP_V(FW_FCOE_ELS_CT_WR) |
				     FW_FCOE_ELS_CT_WR_IMMDLEN(immd_len));

	wr_len =  DIV_ROUND_UP(wr_len, 16);
	wr->flowid_len16 = cpu_to_be32(FW_WR_FLOWID_V(flow_id) |
				       FW_WR_LEN16_V(wr_len));
	wr->els_ct_type = sub_op;
	wr->ctl_pri = 0;
	wr->cp_en_class = 0;
	wr->cookie = io_req->fw_handle;
	wr->iqid = cpu_to_be16(csio_q_physiqid(
					io_req->lnode->hwp, io_req->iq_idx));
	wr->fl_to_sp =  FW_FCOE_ELS_CT_WR_SP(1);
	wr->tmo_val = (uint8_t) io_req->tmo;
	port_id = htonl(sid);
	memcpy(wr->l_id, PORT_ID_PTR(port_id), 3);
	port_id = htonl(did);
	memcpy(wr->r_id, PORT_ID_PTR(port_id), 3);

	/* Prepare RSP SGL */
	wr->rsp_dmalen = cpu_to_be32(io_req->dma_buf.len);
	wr->rsp_dmaaddr = cpu_to_be64(io_req->dma_buf.paddr);
	return 0;
}

/*
 * csio_ln_mgmt_submit_wr - Post elsct work request.
 * @mgmtm - mgmtm
 * @io_req - io request.
 * @sub_op - ELS or CT request type
 * @pld - Dma Payload buffer
 * @pld_len - Payload len
 * Prepares ELSCT Work request and sents it to FW.
 * Returns: 0 - on success
 */
static int
csio_ln_mgmt_submit_wr(struct csio_mgmtm *mgmtm, struct csio_ioreq *io_req,
		uint8_t sub_op, struct csio_dma_buf *pld,
		uint32_t pld_len)
{
	struct csio_wr_pair wrp;
	struct csio_lnode *ln = io_req->lnode;
	struct csio_rnode *rn = io_req->rnode;
	struct	csio_hw	*hw = mgmtm->hw;
	uint8_t fw_wr[64];
	struct ulptx_sgl dsgl;
	uint32_t wr_size = 0;
	uint8_t im_len = 0;
	uint32_t wr_off = 0;

	int ret = 0;

	/* Calculate WR Size for this ELS REQ */
	wr_size = sizeof(struct fw_fcoe_els_ct_wr);

	/* Send as immediate data if pld < 256 */
	if (pld_len < 256) {
		wr_size += ALIGN(pld_len, 8);
		im_len = (uint8_t)pld_len;
	} else
		wr_size += sizeof(struct ulptx_sgl);

	/* Roundup WR size in units of 16 bytes */
	wr_size = ALIGN(wr_size, 16);

	/* Get WR to send ELS REQ */
	ret = csio_wr_get(hw, mgmtm->eq_idx, wr_size, &wrp);
	if (ret != 0) {
		csio_err(hw, "Failed to get WR for ec_req %p ret:%d\n",
			io_req, ret);
		return ret;
	}

	/* Prepare Generic WR used by all ELS/CT cmd */
	csio_ln_prep_ecwr(io_req, wr_size, im_len, sub_op,
				ln->nport_id, rn->nport_id,
				csio_rn_flowid(rn),
				&fw_wr[0]);

	/* Copy ELS/CT WR CMD */
	csio_wr_copy_to_wrp(&fw_wr[0], &wrp, wr_off,
			sizeof(struct fw_fcoe_els_ct_wr));
	wr_off += sizeof(struct fw_fcoe_els_ct_wr);

	/* Copy payload to Immediate section of WR */
	if (im_len)
		csio_wr_copy_to_wrp(pld->vaddr, &wrp, wr_off, im_len);
	else {
		/* Program DSGL to dma payload */
		dsgl.cmd_nsge = htonl(ULPTX_CMD_V(ULP_TX_SC_DSGL) |
					ULPTX_MORE_F | ULPTX_NSGE_V(1));
		dsgl.len0 = cpu_to_be32(pld_len);
		dsgl.addr0 = cpu_to_be64(pld->paddr);
		csio_wr_copy_to_wrp(&dsgl, &wrp, ALIGN(wr_off, 8),
				   sizeof(struct ulptx_sgl));
	}

	/* Issue work request to xmit ELS/CT req to FW */
	csio_wr_issue(mgmtm->hw, mgmtm->eq_idx, false);
	return ret;
}

/*
 * csio_ln_mgmt_submit_req - Submit FCOE Mgmt request.
 * @io_req - IO Request
 * @io_cbfn - Completion handler.
 * @req_type - ELS or CT request type
 * @pld - Dma Payload buffer
 * @pld_len - Payload len
 *
 *
 * This API used submit managment ELS/CT request.
 * This called with hw lock held
 * Returns: 0 - on success
 *	    -ENOMEM	- on error.
 */
static int
csio_ln_mgmt_submit_req(struct csio_ioreq *io_req,
		void (*io_cbfn) (struct csio_hw *, struct csio_ioreq *),
		enum fcoe_cmn_type req_type, struct csio_dma_buf *pld,
		uint32_t pld_len)
{
	struct csio_hw *hw = csio_lnode_to_hw(io_req->lnode);
	struct csio_mgmtm *mgmtm = csio_hw_to_mgmtm(hw);
	int rv;

	BUG_ON(pld_len > pld->len);

	io_req->io_cbfn = io_cbfn;	/* Upper layer callback handler */
	io_req->fw_handle = (uintptr_t) (io_req);
	io_req->eq_idx = mgmtm->eq_idx;
	io_req->iq_idx = mgmtm->iq_idx;

	rv = csio_ln_mgmt_submit_wr(mgmtm, io_req, req_type, pld, pld_len);
	if (rv == 0) {
		list_add_tail(&io_req->sm.sm_list, &mgmtm->active_q);
		mgmtm->stats.n_active++;
	}
	return rv;
}

/*
 * csio_ln_fdmi_init - FDMI Init entry point.
 * @ln: lnode
 */
static int
csio_ln_fdmi_init(struct csio_lnode *ln)
{
	struct csio_hw *hw = csio_lnode_to_hw(ln);
	struct csio_dma_buf	*dma_buf;

	/* Allocate MGMT request required for FDMI */
	ln->mgmt_req = kzalloc(sizeof(struct csio_ioreq), GFP_KERNEL);
	if (!ln->mgmt_req) {
		csio_ln_err(ln, "Failed to alloc ioreq for FDMI\n");
		CSIO_INC_STATS(hw, n_err_nomem);
		return -ENOMEM;
	}

	/* Allocate Dma buffers for FDMI response Payload */
	dma_buf = &ln->mgmt_req->dma_buf;
	dma_buf->len = 2048;
	dma_buf->vaddr = dma_alloc_coherent(&hw->pdev->dev, dma_buf->len,
						&dma_buf->paddr, GFP_KERNEL);
	if (!dma_buf->vaddr) {
		csio_err(hw, "Failed to alloc DMA buffer for FDMI!\n");
		kfree(ln->mgmt_req);
		ln->mgmt_req = NULL;
		return -ENOMEM;
	}

	ln->flags |= CSIO_LNF_FDMI_ENABLE;
	return 0;
}

/*
 * csio_ln_fdmi_exit - FDMI exit entry point.
 * @ln: lnode
 */
static int
csio_ln_fdmi_exit(struct csio_lnode *ln)
{
	struct csio_dma_buf *dma_buf;
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	if (!ln->mgmt_req)
		return 0;

	dma_buf = &ln->mgmt_req->dma_buf;
	if (dma_buf->vaddr)
		dma_free_coherent(&hw->pdev->dev, dma_buf->len, dma_buf->vaddr,
				    dma_buf->paddr);

	kfree(ln->mgmt_req);
	return 0;
}

int
csio_scan_done(struct csio_lnode *ln, unsigned long ticks,
		unsigned long time, unsigned long max_scan_ticks,
		unsigned long delta_scan_ticks)
{
	int rv = 0;

	if (time >= max_scan_ticks)
		return 1;

	if (!ln->tgt_scan_tick)
		ln->tgt_scan_tick = ticks;

	if (((ticks - ln->tgt_scan_tick) >= delta_scan_ticks)) {
		if (!ln->last_scan_ntgts)
			ln->last_scan_ntgts = ln->n_scsi_tgts;
		else {
			if (ln->last_scan_ntgts == ln->n_scsi_tgts)
				return 1;

			ln->last_scan_ntgts = ln->n_scsi_tgts;
		}
		ln->tgt_scan_tick = ticks;
	}
	return rv;
}

/*
 * csio_notify_lnodes:
 * @hw: HW module
 * @note: Notification
 *
 * Called from the HW SM to fan out notifications to the
 * Lnode SM. Since the HW SM is entered with lock held,
 * there is no need to hold locks here.
 *
 */
void
csio_notify_lnodes(struct csio_hw *hw, enum csio_ln_notify note)
{
	struct list_head *tmp;
	struct csio_lnode *ln;

	csio_dbg(hw, "Notifying all nodes of event %d\n", note);

	/* Traverse children lnodes list and send evt */
	list_for_each(tmp, &hw->sln_head) {
		ln = (struct csio_lnode *) tmp;

		switch (note) {
		case CSIO_LN_NOTIFY_HWREADY:
			csio_lnode_start(ln);
			break;

		case CSIO_LN_NOTIFY_HWRESET:
		case CSIO_LN_NOTIFY_HWREMOVE:
			csio_lnode_close(ln);
			break;

		case CSIO_LN_NOTIFY_HWSTOP:
			csio_lnode_stop(ln);
			break;

		default:
			break;

		}
	}
}

/*
 * csio_disable_lnodes:
 * @hw: HW module
 * @portid:port id
 * @disable: disable/enable flag.
 * If disable=1, disables all lnode hosted on given physical port.
 * otherwise enables all the lnodes on given phsysical port.
 * This routine need to called with hw lock held.
 */
void
csio_disable_lnodes(struct csio_hw *hw, uint8_t portid, bool disable)
{
	struct list_head *tmp;
	struct csio_lnode *ln;

	csio_dbg(hw, "Notifying event to all nodes of port:%d\n", portid);

	/* Traverse sibling lnodes list and send evt */
	list_for_each(tmp, &hw->sln_head) {
		ln = (struct csio_lnode *) tmp;
		if (ln->portid != portid)
			continue;

		if (disable)
			csio_lnode_stop(ln);
		else
			csio_lnode_start(ln);
	}
}

/*
 * csio_ln_init - Initialize an lnode.
 * @ln:		lnode
 *
 */
static int
csio_ln_init(struct csio_lnode *ln)
{
	int rv = -EINVAL;
	struct csio_lnode *pln;
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	csio_init_state(&ln->sm, csio_lns_uninit);
	ln->vnp_flowid = CSIO_INVALID_IDX;
	ln->fcf_flowid = CSIO_INVALID_IDX;

	if (csio_is_root_ln(ln)) {

		/* This is the lnode used during initialization */

		ln->fcfinfo = kzalloc(sizeof(struct csio_fcf_info), GFP_KERNEL);
		if (!ln->fcfinfo) {
			csio_ln_err(ln, "Failed to alloc FCF record\n");
			CSIO_INC_STATS(hw, n_err_nomem);
			goto err;
		}

		INIT_LIST_HEAD(&ln->fcf_lsthead);
		kref_init(&ln->fcfinfo->kref);

		if (csio_fdmi_enable && csio_ln_fdmi_init(ln))
			goto err;

	} else { /* Either a non-root physical or a virtual lnode */

		/*
		 * THe rest is common for non-root physical and NPIV lnodes.
		 * Just get references to all other modules
		 */

		if (csio_is_npiv_ln(ln)) {
			/* NPIV */
			pln = csio_parent_lnode(ln);
			kref_get(&pln->fcfinfo->kref);
			ln->fcfinfo = pln->fcfinfo;
		} else {
			/* Another non-root physical lnode (FCF) */
			ln->fcfinfo = kzalloc(sizeof(struct csio_fcf_info),
								GFP_KERNEL);
			if (!ln->fcfinfo) {
				csio_ln_err(ln, "Failed to alloc FCF info\n");
				CSIO_INC_STATS(hw, n_err_nomem);
				goto err;
			}

			kref_init(&ln->fcfinfo->kref);

			if (csio_fdmi_enable && csio_ln_fdmi_init(ln))
				goto err;
		}

	} /* if (!csio_is_root_ln(ln)) */

	return 0;
err:
	return rv;
}

static void
csio_ln_exit(struct csio_lnode *ln)
{
	struct csio_lnode *pln;

	csio_cleanup_rns(ln);
	if (csio_is_npiv_ln(ln)) {
		pln = csio_parent_lnode(ln);
		kref_put(&pln->fcfinfo->kref, csio_free_fcfinfo);
	} else {
		kref_put(&ln->fcfinfo->kref, csio_free_fcfinfo);
		if (csio_fdmi_enable)
			csio_ln_fdmi_exit(ln);
	}
	ln->fcfinfo = NULL;
}

/*
 * csio_lnode_init - Initialize the members of an lnode.
 * @ln:		lnode
 */
int
csio_lnode_init(struct csio_lnode *ln, struct csio_hw *hw,
		struct csio_lnode *pln)
{
	int rv = -EINVAL;

	/* Link this lnode to hw */
	csio_lnode_to_hw(ln)	= hw;

	/* Link child to parent if child lnode */
	if (pln)
		ln->pln = pln;
	else
		ln->pln = NULL;

	/* Initialize scsi_tgt and timers to zero */
	ln->n_scsi_tgts = 0;
	ln->last_scan_ntgts = 0;
	ln->tgt_scan_tick = 0;

	/* Initialize rnode list */
	INIT_LIST_HEAD(&ln->rnhead);
	INIT_LIST_HEAD(&ln->cln_head);

	/* Initialize log level for debug */
	ln->params.log_level	= hw->params.log_level;

	if (csio_ln_init(ln))
		goto err;

	/* Add lnode to list of sibling or children lnodes */
	spin_lock_irq(&hw->lock);
	list_add_tail(&ln->sm.sm_list, pln ? &pln->cln_head : &hw->sln_head);
	if (pln)
		pln->num_vports++;
	spin_unlock_irq(&hw->lock);

	hw->num_lns++;

	return 0;
err:
	csio_lnode_to_hw(ln) = NULL;
	return rv;
}

/**
 * csio_lnode_exit - De-instantiate an lnode.
 * @ln:		lnode
 *
 */
void
csio_lnode_exit(struct csio_lnode *ln)
{
	struct csio_hw *hw = csio_lnode_to_hw(ln);

	csio_ln_exit(ln);

	/* Remove this lnode from hw->sln_head */
	spin_lock_irq(&hw->lock);

	list_del_init(&ln->sm.sm_list);

	/* If it is children lnode, decrement the
	 * counter in its parent lnode
	 */
	if (ln->pln)
		ln->pln->num_vports--;

	/* Update root lnode pointer */
	if (list_empty(&hw->sln_head))
		hw->rln = NULL;
	else
		hw->rln = (struct csio_lnode *)csio_list_next(&hw->sln_head);

	spin_unlock_irq(&hw->lock);

	csio_lnode_to_hw(ln)	= NULL;
	hw->num_lns--;
}
