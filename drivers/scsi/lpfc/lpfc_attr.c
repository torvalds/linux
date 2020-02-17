/*******************************************************************
 * This file is part of the Emulex Linux Device Driver for         *
 * Fibre Channel Host Bus Adapters.                                *
 * Copyright (C) 2017-2019 Broadcom. All Rights Reserved. The term *
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.  *
 * Copyright (C) 2004-2016 Emulex.  All rights reserved.           *
 * EMULEX and SLI are trademarks of Emulex.                        *
 * www.broadcom.com                                                *
 * Portions Copyright (C) 2004-2005 Christoph Hellwig              *
 *                                                                 *
 * This program is free software; you can redistribute it and/or   *
 * modify it under the terms of version 2 of the GNU General       *
 * Public License as published by the Free Software Foundation.    *
 * This program is distributed in the hope that it will be useful. *
 * ALL EXPRESS OR IMPLIED CONDITIONS, REPRESENTATIONS AND          *
 * WARRANTIES, INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,  *
 * FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE      *
 * DISCLAIMED, EXCEPT TO THE EXTENT THAT SUCH DISCLAIMERS ARE HELD *
 * TO BE LEGALLY INVALID.  See the GNU General Public License for  *
 * more details, a copy of which can be found in the file COPYING  *
 * included with this package.                                     *
 *******************************************************************/

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/aer.h>
#include <linux/gfp.h>
#include <linux/kernel.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/fc/fc_fs.h>

#include <linux/nvme-fc-driver.h>

#include "lpfc_hw4.h"
#include "lpfc_hw.h"
#include "lpfc_sli.h"
#include "lpfc_sli4.h"
#include "lpfc_nl.h"
#include "lpfc_disc.h"
#include "lpfc.h"
#include "lpfc_scsi.h"
#include "lpfc_nvme.h"
#include "lpfc_nvmet.h"
#include "lpfc_logmsg.h"
#include "lpfc_version.h"
#include "lpfc_compat.h"
#include "lpfc_crtn.h"
#include "lpfc_vport.h"
#include "lpfc_attr.h"

#define LPFC_DEF_DEVLOSS_TMO	30
#define LPFC_MIN_DEVLOSS_TMO	1
#define LPFC_MAX_DEVLOSS_TMO	255

#define LPFC_DEF_MRQ_POST	512
#define LPFC_MIN_MRQ_POST	512
#define LPFC_MAX_MRQ_POST	2048

/*
 * Write key size should be multiple of 4. If write key is changed
 * make sure that library write key is also changed.
 */
#define LPFC_REG_WRITE_KEY_SIZE	4
#define LPFC_REG_WRITE_KEY	"EMLX"

const char *const trunk_errmsg[] = {	/* map errcode */
	"",	/* There is no such error code at index 0*/
	"link negotiated speed does not match existing"
		" trunk - link was \"low\" speed",
	"link negotiated speed does not match"
		" existing trunk - link was \"middle\" speed",
	"link negotiated speed does not match existing"
		" trunk - link was \"high\" speed",
	"Attached to non-trunking port - F_Port",
	"Attached to non-trunking port - N_Port",
	"FLOGI response timeout",
	"non-FLOGI frame received",
	"Invalid FLOGI response",
	"Trunking initialization protocol",
	"Trunk peer device mismatch",
};

/**
 * lpfc_jedec_to_ascii - Hex to ascii convertor according to JEDEC rules
 * @incr: integer to convert.
 * @hdw: ascii string holding converted integer plus a string terminator.
 *
 * Description:
 * JEDEC Joint Electron Device Engineering Council.
 * Convert a 32 bit integer composed of 8 nibbles into an 8 byte ascii
 * character string. The string is then terminated with a NULL in byte 9.
 * Hex 0-9 becomes ascii '0' to '9'.
 * Hex a-f becomes ascii '=' to 'B' capital B.
 *
 * Notes:
 * Coded for 32 bit integers only.
 **/
static void
lpfc_jedec_to_ascii(int incr, char hdw[])
{
	int i, j;
	for (i = 0; i < 8; i++) {
		j = (incr & 0xf);
		if (j <= 9)
			hdw[7 - i] = 0x30 +  j;
		 else
			hdw[7 - i] = 0x61 + j - 10;
		incr = (incr >> 4);
	}
	hdw[8] = 0;
	return;
}

/**
 * lpfc_drvr_version_show - Return the Emulex driver string with version number
 * @dev: class unused variable.
 * @attr: device attribute, not used.
 * @buf: on return contains the module description text.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_drvr_version_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	return scnprintf(buf, PAGE_SIZE, LPFC_MODULE_DESC "\n");
}

/**
 * lpfc_enable_fip_show - Return the fip mode of the HBA
 * @dev: class unused variable.
 * @attr: device attribute, not used.
 * @buf: on return contains the module description text.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_enable_fip_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	if (phba->hba_flag & HBA_FIP_SUPPORT)
		return scnprintf(buf, PAGE_SIZE, "1\n");
	else
		return scnprintf(buf, PAGE_SIZE, "0\n");
}

static ssize_t
lpfc_nvme_info_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = shost_priv(shost);
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_nvmet_tgtport *tgtp;
	struct nvme_fc_local_port *localport;
	struct lpfc_nvme_lport *lport;
	struct lpfc_nvme_rport *rport;
	struct lpfc_nodelist *ndlp;
	struct nvme_fc_remote_port *nrport;
	struct lpfc_fc4_ctrl_stat *cstat;
	uint64_t data1, data2, data3;
	uint64_t totin, totout, tot;
	char *statep;
	int i;
	int len = 0;
	char tmp[LPFC_MAX_NVME_INFO_TMP_LEN] = {0};

	if (!(vport->cfg_enable_fc4_type & LPFC_ENABLE_NVME)) {
		len = scnprintf(buf, PAGE_SIZE, "NVME Disabled\n");
		return len;
	}
	if (phba->nvmet_support) {
		if (!phba->targetport) {
			len = scnprintf(buf, PAGE_SIZE,
					"NVME Target: x%llx is not allocated\n",
					wwn_to_u64(vport->fc_portname.u.wwn));
			return len;
		}
		/* Port state is only one of two values for now. */
		if (phba->targetport->port_id)
			statep = "REGISTERED";
		else
			statep = "INIT";
		scnprintf(tmp, sizeof(tmp),
			  "NVME Target Enabled  State %s\n",
			  statep);
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto buffer_done;

		scnprintf(tmp, sizeof(tmp),
			  "%s%d WWPN x%llx WWNN x%llx DID x%06x\n",
			  "NVME Target: lpfc",
			  phba->brd_no,
			  wwn_to_u64(vport->fc_portname.u.wwn),
			  wwn_to_u64(vport->fc_nodename.u.wwn),
			  phba->targetport->port_id);
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto buffer_done;

		if (strlcat(buf, "\nNVME Target: Statistics\n", PAGE_SIZE)
		    >= PAGE_SIZE)
			goto buffer_done;

		tgtp = (struct lpfc_nvmet_tgtport *)phba->targetport->private;
		scnprintf(tmp, sizeof(tmp),
			  "LS: Rcv %08x Drop %08x Abort %08x\n",
			  atomic_read(&tgtp->rcv_ls_req_in),
			  atomic_read(&tgtp->rcv_ls_req_drop),
			  atomic_read(&tgtp->xmt_ls_abort));
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto buffer_done;

		if (atomic_read(&tgtp->rcv_ls_req_in) !=
		    atomic_read(&tgtp->rcv_ls_req_out)) {
			scnprintf(tmp, sizeof(tmp),
				  "Rcv LS: in %08x != out %08x\n",
				  atomic_read(&tgtp->rcv_ls_req_in),
				  atomic_read(&tgtp->rcv_ls_req_out));
			if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
				goto buffer_done;
		}

		scnprintf(tmp, sizeof(tmp),
			  "LS: Xmt %08x Drop %08x Cmpl %08x\n",
			  atomic_read(&tgtp->xmt_ls_rsp),
			  atomic_read(&tgtp->xmt_ls_drop),
			  atomic_read(&tgtp->xmt_ls_rsp_cmpl));
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto buffer_done;

		scnprintf(tmp, sizeof(tmp),
			  "LS: RSP Abort %08x xb %08x Err %08x\n",
			  atomic_read(&tgtp->xmt_ls_rsp_aborted),
			  atomic_read(&tgtp->xmt_ls_rsp_xb_set),
			  atomic_read(&tgtp->xmt_ls_rsp_error));
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto buffer_done;

		scnprintf(tmp, sizeof(tmp),
			  "FCP: Rcv %08x Defer %08x Release %08x "
			  "Drop %08x\n",
			  atomic_read(&tgtp->rcv_fcp_cmd_in),
			  atomic_read(&tgtp->rcv_fcp_cmd_defer),
			  atomic_read(&tgtp->xmt_fcp_release),
			  atomic_read(&tgtp->rcv_fcp_cmd_drop));
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto buffer_done;

		if (atomic_read(&tgtp->rcv_fcp_cmd_in) !=
		    atomic_read(&tgtp->rcv_fcp_cmd_out)) {
			scnprintf(tmp, sizeof(tmp),
				  "Rcv FCP: in %08x != out %08x\n",
				  atomic_read(&tgtp->rcv_fcp_cmd_in),
				  atomic_read(&tgtp->rcv_fcp_cmd_out));
			if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
				goto buffer_done;
		}

		scnprintf(tmp, sizeof(tmp),
			  "FCP Rsp: RD %08x rsp %08x WR %08x rsp %08x "
			  "drop %08x\n",
			  atomic_read(&tgtp->xmt_fcp_read),
			  atomic_read(&tgtp->xmt_fcp_read_rsp),
			  atomic_read(&tgtp->xmt_fcp_write),
			  atomic_read(&tgtp->xmt_fcp_rsp),
			  atomic_read(&tgtp->xmt_fcp_drop));
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto buffer_done;

		scnprintf(tmp, sizeof(tmp),
			  "FCP Rsp Cmpl: %08x err %08x drop %08x\n",
			  atomic_read(&tgtp->xmt_fcp_rsp_cmpl),
			  atomic_read(&tgtp->xmt_fcp_rsp_error),
			  atomic_read(&tgtp->xmt_fcp_rsp_drop));
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto buffer_done;

		scnprintf(tmp, sizeof(tmp),
			  "FCP Rsp Abort: %08x xb %08x xricqe  %08x\n",
			  atomic_read(&tgtp->xmt_fcp_rsp_aborted),
			  atomic_read(&tgtp->xmt_fcp_rsp_xb_set),
			  atomic_read(&tgtp->xmt_fcp_xri_abort_cqe));
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto buffer_done;

		scnprintf(tmp, sizeof(tmp),
			  "ABORT: Xmt %08x Cmpl %08x\n",
			  atomic_read(&tgtp->xmt_fcp_abort),
			  atomic_read(&tgtp->xmt_fcp_abort_cmpl));
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto buffer_done;

		scnprintf(tmp, sizeof(tmp),
			  "ABORT: Sol %08x  Usol %08x Err %08x Cmpl %08x\n",
			  atomic_read(&tgtp->xmt_abort_sol),
			  atomic_read(&tgtp->xmt_abort_unsol),
			  atomic_read(&tgtp->xmt_abort_rsp),
			  atomic_read(&tgtp->xmt_abort_rsp_error));
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto buffer_done;

		scnprintf(tmp, sizeof(tmp),
			  "DELAY: ctx %08x  fod %08x wqfull %08x\n",
			  atomic_read(&tgtp->defer_ctx),
			  atomic_read(&tgtp->defer_fod),
			  atomic_read(&tgtp->defer_wqfull));
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto buffer_done;

		/* Calculate outstanding IOs */
		tot = atomic_read(&tgtp->rcv_fcp_cmd_drop);
		tot += atomic_read(&tgtp->xmt_fcp_release);
		tot = atomic_read(&tgtp->rcv_fcp_cmd_in) - tot;

		scnprintf(tmp, sizeof(tmp),
			  "IO_CTX: %08x  WAIT: cur %08x tot %08x\n"
			  "CTX Outstanding %08llx\n\n",
			  phba->sli4_hba.nvmet_xri_cnt,
			  phba->sli4_hba.nvmet_io_wait_cnt,
			  phba->sli4_hba.nvmet_io_wait_total,
			  tot);
		strlcat(buf, tmp, PAGE_SIZE);
		goto buffer_done;
	}

	localport = vport->localport;
	if (!localport) {
		len = scnprintf(buf, PAGE_SIZE,
				"NVME Initiator x%llx is not allocated\n",
				wwn_to_u64(vport->fc_portname.u.wwn));
		return len;
	}
	lport = (struct lpfc_nvme_lport *)localport->private;
	if (strlcat(buf, "\nNVME Initiator Enabled\n", PAGE_SIZE) >= PAGE_SIZE)
		goto buffer_done;

	scnprintf(tmp, sizeof(tmp),
		  "XRI Dist lpfc%d Total %d IO %d ELS %d\n",
		  phba->brd_no,
		  phba->sli4_hba.max_cfg_param.max_xri,
		  phba->sli4_hba.io_xri_max,
		  lpfc_sli4_get_els_iocb_cnt(phba));
	if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
		goto buffer_done;

	/* Port state is only one of two values for now. */
	if (localport->port_id)
		statep = "ONLINE";
	else
		statep = "UNKNOWN ";

	scnprintf(tmp, sizeof(tmp),
		  "%s%d WWPN x%llx WWNN x%llx DID x%06x %s\n",
		  "NVME LPORT lpfc",
		  phba->brd_no,
		  wwn_to_u64(vport->fc_portname.u.wwn),
		  wwn_to_u64(vport->fc_nodename.u.wwn),
		  localport->port_id, statep);
	if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
		goto buffer_done;

	spin_lock_irq(shost->host_lock);

	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
		nrport = NULL;
		spin_lock(&vport->phba->hbalock);
		rport = lpfc_ndlp_get_nrport(ndlp);
		if (rport)
			nrport = rport->remoteport;
		spin_unlock(&vport->phba->hbalock);
		if (!nrport)
			continue;

		/* Port state is only one of two values for now. */
		switch (nrport->port_state) {
		case FC_OBJSTATE_ONLINE:
			statep = "ONLINE";
			break;
		case FC_OBJSTATE_UNKNOWN:
			statep = "UNKNOWN ";
			break;
		default:
			statep = "UNSUPPORTED";
			break;
		}

		/* Tab in to show lport ownership. */
		if (strlcat(buf, "NVME RPORT       ", PAGE_SIZE) >= PAGE_SIZE)
			goto unlock_buf_done;
		if (phba->brd_no >= 10) {
			if (strlcat(buf, " ", PAGE_SIZE) >= PAGE_SIZE)
				goto unlock_buf_done;
		}

		scnprintf(tmp, sizeof(tmp), "WWPN x%llx ",
			  nrport->port_name);
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto unlock_buf_done;

		scnprintf(tmp, sizeof(tmp), "WWNN x%llx ",
			  nrport->node_name);
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto unlock_buf_done;

		scnprintf(tmp, sizeof(tmp), "DID x%06x ",
			  nrport->port_id);
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto unlock_buf_done;

		/* An NVME rport can have multiple roles. */
		if (nrport->port_role & FC_PORT_ROLE_NVME_INITIATOR) {
			if (strlcat(buf, "INITIATOR ", PAGE_SIZE) >= PAGE_SIZE)
				goto unlock_buf_done;
		}
		if (nrport->port_role & FC_PORT_ROLE_NVME_TARGET) {
			if (strlcat(buf, "TARGET ", PAGE_SIZE) >= PAGE_SIZE)
				goto unlock_buf_done;
		}
		if (nrport->port_role & FC_PORT_ROLE_NVME_DISCOVERY) {
			if (strlcat(buf, "DISCSRVC ", PAGE_SIZE) >= PAGE_SIZE)
				goto unlock_buf_done;
		}
		if (nrport->port_role & ~(FC_PORT_ROLE_NVME_INITIATOR |
					  FC_PORT_ROLE_NVME_TARGET |
					  FC_PORT_ROLE_NVME_DISCOVERY)) {
			scnprintf(tmp, sizeof(tmp), "UNKNOWN ROLE x%x",
				  nrport->port_role);
			if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
				goto unlock_buf_done;
		}

		scnprintf(tmp, sizeof(tmp), "%s\n", statep);
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto unlock_buf_done;
	}
	spin_unlock_irq(shost->host_lock);

	if (!lport)
		goto buffer_done;

	if (strlcat(buf, "\nNVME Statistics\n", PAGE_SIZE) >= PAGE_SIZE)
		goto buffer_done;

	scnprintf(tmp, sizeof(tmp),
		  "LS: Xmt %010x Cmpl %010x Abort %08x\n",
		  atomic_read(&lport->fc4NvmeLsRequests),
		  atomic_read(&lport->fc4NvmeLsCmpls),
		  atomic_read(&lport->xmt_ls_abort));
	if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
		goto buffer_done;

	scnprintf(tmp, sizeof(tmp),
		  "LS XMIT: Err %08x  CMPL: xb %08x Err %08x\n",
		  atomic_read(&lport->xmt_ls_err),
		  atomic_read(&lport->cmpl_ls_xb),
		  atomic_read(&lport->cmpl_ls_err));
	if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
		goto buffer_done;

	totin = 0;
	totout = 0;
	for (i = 0; i < phba->cfg_hdw_queue; i++) {
		cstat = &phba->sli4_hba.hdwq[i].nvme_cstat;
		tot = cstat->io_cmpls;
		totin += tot;
		data1 = cstat->input_requests;
		data2 = cstat->output_requests;
		data3 = cstat->control_requests;
		totout += (data1 + data2 + data3);
	}
	scnprintf(tmp, sizeof(tmp),
		  "Total FCP Cmpl %016llx Issue %016llx "
		  "OutIO %016llx\n",
		  totin, totout, totout - totin);
	if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
		goto buffer_done;

	scnprintf(tmp, sizeof(tmp),
		  "\tabort %08x noxri %08x nondlp %08x qdepth %08x "
		  "wqerr %08x err %08x\n",
		  atomic_read(&lport->xmt_fcp_abort),
		  atomic_read(&lport->xmt_fcp_noxri),
		  atomic_read(&lport->xmt_fcp_bad_ndlp),
		  atomic_read(&lport->xmt_fcp_qdepth),
		  atomic_read(&lport->xmt_fcp_err),
		  atomic_read(&lport->xmt_fcp_wqerr));
	if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
		goto buffer_done;

	scnprintf(tmp, sizeof(tmp),
		  "FCP CMPL: xb %08x Err %08x\n",
		  atomic_read(&lport->cmpl_fcp_xb),
		  atomic_read(&lport->cmpl_fcp_err));
	strlcat(buf, tmp, PAGE_SIZE);

	/* host_lock is already unlocked. */
	goto buffer_done;

 unlock_buf_done:
	spin_unlock_irq(shost->host_lock);

 buffer_done:
	len = strnlen(buf, PAGE_SIZE);

	if (unlikely(len >= (PAGE_SIZE - 1))) {
		lpfc_printf_log(phba, KERN_INFO, LOG_NVME,
				"6314 Catching potential buffer "
				"overflow > PAGE_SIZE = %lu bytes\n",
				PAGE_SIZE);
		strlcpy(buf + PAGE_SIZE - 1 -
			strnlen(LPFC_NVME_INFO_MORE_STR, PAGE_SIZE - 1),
			LPFC_NVME_INFO_MORE_STR,
			strnlen(LPFC_NVME_INFO_MORE_STR, PAGE_SIZE - 1)
			+ 1);
	}

	return len;
}

static ssize_t
lpfc_scsi_stat_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = shost_priv(shost);
	struct lpfc_hba *phba = vport->phba;
	int len;
	struct lpfc_fc4_ctrl_stat *cstat;
	u64 data1, data2, data3;
	u64 tot, totin, totout;
	int i;
	char tmp[LPFC_MAX_SCSI_INFO_TMP_LEN] = {0};

	if (!(vport->cfg_enable_fc4_type & LPFC_ENABLE_FCP) ||
	    (phba->sli_rev != LPFC_SLI_REV4))
		return 0;

	scnprintf(buf, PAGE_SIZE, "SCSI HDWQ Statistics\n");

	totin = 0;
	totout = 0;
	for (i = 0; i < phba->cfg_hdw_queue; i++) {
		cstat = &phba->sli4_hba.hdwq[i].scsi_cstat;
		tot = cstat->io_cmpls;
		totin += tot;
		data1 = cstat->input_requests;
		data2 = cstat->output_requests;
		data3 = cstat->control_requests;
		totout += (data1 + data2 + data3);

		scnprintf(tmp, sizeof(tmp), "HDWQ (%d): Rd %016llx Wr %016llx "
			  "IO %016llx ", i, data1, data2, data3);
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto buffer_done;

		scnprintf(tmp, sizeof(tmp), "Cmpl %016llx OutIO %016llx\n",
			  tot, ((data1 + data2 + data3) - tot));
		if (strlcat(buf, tmp, PAGE_SIZE) >= PAGE_SIZE)
			goto buffer_done;
	}
	scnprintf(tmp, sizeof(tmp), "Total FCP Cmpl %016llx Issue %016llx "
		  "OutIO %016llx\n", totin, totout, totout - totin);
	strlcat(buf, tmp, PAGE_SIZE);

buffer_done:
	len = strnlen(buf, PAGE_SIZE);

	return len;
}

static ssize_t
lpfc_bg_info_show(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	if (phba->cfg_enable_bg) {
		if (phba->sli3_options & LPFC_SLI3_BG_ENABLED)
			return scnprintf(buf, PAGE_SIZE,
					"BlockGuard Enabled\n");
		else
			return scnprintf(buf, PAGE_SIZE,
					"BlockGuard Not Supported\n");
	} else
		return scnprintf(buf, PAGE_SIZE,
					"BlockGuard Disabled\n");
}

static ssize_t
lpfc_bg_guard_err_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%llu\n",
			(unsigned long long)phba->bg_guard_err_cnt);
}

static ssize_t
lpfc_bg_apptag_err_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%llu\n",
			(unsigned long long)phba->bg_apptag_err_cnt);
}

static ssize_t
lpfc_bg_reftag_err_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%llu\n",
			(unsigned long long)phba->bg_reftag_err_cnt);
}

/**
 * lpfc_info_show - Return some pci info about the host in ascii
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the formatted text from lpfc_info().
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_info_show(struct device *dev, struct device_attribute *attr,
	       char *buf)
{
	struct Scsi_Host *host = class_to_shost(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n", lpfc_info(host));
}

/**
 * lpfc_serialnum_show - Return the hba serial number in ascii
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the formatted text serial number.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_serialnum_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%s\n", phba->SerialNumber);
}

/**
 * lpfc_temp_sensor_show - Return the temperature sensor level
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the formatted support level.
 *
 * Description:
 * Returns a number indicating the temperature sensor level currently
 * supported, zero or one in ascii.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_temp_sensor_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	return scnprintf(buf, PAGE_SIZE, "%d\n", phba->temp_sensor_support);
}

/**
 * lpfc_modeldesc_show - Return the model description of the hba
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the scsi vpd model description.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_modeldesc_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%s\n", phba->ModelDesc);
}

/**
 * lpfc_modelname_show - Return the model name of the hba
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the scsi vpd model name.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_modelname_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%s\n", phba->ModelName);
}

/**
 * lpfc_programtype_show - Return the program type of the hba
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the scsi vpd program type.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_programtype_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%s\n", phba->ProgramType);
}

/**
 * lpfc_mlomgmt_show - Return the Menlo Maintenance sli flag
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the Menlo Maintenance sli flag.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_mlomgmt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
		(phba->sli.sli_flag & LPFC_MENLO_MAINT));
}

/**
 * lpfc_vportnum_show - Return the port number in ascii of the hba
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains scsi vpd program type.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_vportnum_show(struct device *dev, struct device_attribute *attr,
		   char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%s\n", phba->Port);
}

/**
 * lpfc_fwrev_show - Return the firmware rev running in the hba
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the scsi vpd program type.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_fwrev_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t if_type;
	uint8_t sli_family;
	char fwrev[FW_REV_STR_SIZE];
	int len;

	lpfc_decode_firmware_rev(phba, fwrev, 1);
	if_type = phba->sli4_hba.pc_sli4_params.if_type;
	sli_family = phba->sli4_hba.pc_sli4_params.sli_family;

	if (phba->sli_rev < LPFC_SLI_REV4)
		len = scnprintf(buf, PAGE_SIZE, "%s, sli-%d\n",
			       fwrev, phba->sli_rev);
	else
		len = scnprintf(buf, PAGE_SIZE, "%s, sli-%d:%d:%x\n",
			       fwrev, phba->sli_rev, if_type, sli_family);

	return len;
}

/**
 * lpfc_hdw_show - Return the jedec information about the hba
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the scsi vpd program type.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_hdw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char hdw[9];
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	lpfc_vpd_t *vp = &phba->vpd;

	lpfc_jedec_to_ascii(vp->rev.biuRev, hdw);
	return scnprintf(buf, PAGE_SIZE, "%s %08x %08x\n", hdw,
			 vp->rev.smRev, vp->rev.smFwRev);
}

/**
 * lpfc_option_rom_version_show - Return the adapter ROM FCode version
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the ROM and FCode ascii strings.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_option_rom_version_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	char fwrev[FW_REV_STR_SIZE];

	if (phba->sli_rev < LPFC_SLI_REV4)
		return scnprintf(buf, PAGE_SIZE, "%s\n",
				phba->OptionROMVersion);

	lpfc_decode_firmware_rev(phba, fwrev, 1);
	return scnprintf(buf, PAGE_SIZE, "%s\n", fwrev);
}

/**
 * lpfc_state_show - Return the link state of the port
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains text describing the state of the link.
 *
 * Notes:
 * The switch statement has no default so zero will be returned.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_link_state_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int  len = 0;

	switch (phba->link_state) {
	case LPFC_LINK_UNKNOWN:
	case LPFC_WARM_START:
	case LPFC_INIT_START:
	case LPFC_INIT_MBX_CMDS:
	case LPFC_LINK_DOWN:
	case LPFC_HBA_ERROR:
		if (phba->hba_flag & LINK_DISABLED)
			len += scnprintf(buf + len, PAGE_SIZE-len,
				"Link Down - User disabled\n");
		else
			len += scnprintf(buf + len, PAGE_SIZE-len,
				"Link Down\n");
		break;
	case LPFC_LINK_UP:
	case LPFC_CLEAR_LA:
	case LPFC_HBA_READY:
		len += scnprintf(buf + len, PAGE_SIZE-len, "Link Up - ");

		switch (vport->port_state) {
		case LPFC_LOCAL_CFG_LINK:
			len += scnprintf(buf + len, PAGE_SIZE-len,
					"Configuring Link\n");
			break;
		case LPFC_FDISC:
		case LPFC_FLOGI:
		case LPFC_FABRIC_CFG_LINK:
		case LPFC_NS_REG:
		case LPFC_NS_QRY:
		case LPFC_BUILD_DISC_LIST:
		case LPFC_DISC_AUTH:
			len += scnprintf(buf + len, PAGE_SIZE - len,
					"Discovery\n");
			break;
		case LPFC_VPORT_READY:
			len += scnprintf(buf + len, PAGE_SIZE - len,
					"Ready\n");
			break;

		case LPFC_VPORT_FAILED:
			len += scnprintf(buf + len, PAGE_SIZE - len,
					"Failed\n");
			break;

		case LPFC_VPORT_UNKNOWN:
			len += scnprintf(buf + len, PAGE_SIZE - len,
					"Unknown\n");
			break;
		}
		if (phba->sli.sli_flag & LPFC_MENLO_MAINT)
			len += scnprintf(buf + len, PAGE_SIZE-len,
					"   Menlo Maint Mode\n");
		else if (phba->fc_topology == LPFC_TOPOLOGY_LOOP) {
			if (vport->fc_flag & FC_PUBLIC_LOOP)
				len += scnprintf(buf + len, PAGE_SIZE-len,
						"   Public Loop\n");
			else
				len += scnprintf(buf + len, PAGE_SIZE-len,
						"   Private Loop\n");
		} else {
			if (vport->fc_flag & FC_FABRIC)
				len += scnprintf(buf + len, PAGE_SIZE-len,
						"   Fabric\n");
			else
				len += scnprintf(buf + len, PAGE_SIZE-len,
						"   Point-2-Point\n");
		}
	}

	if ((phba->sli_rev == LPFC_SLI_REV4) &&
	    ((bf_get(lpfc_sli_intf_if_type,
	     &phba->sli4_hba.sli_intf) ==
	     LPFC_SLI_INTF_IF_TYPE_6))) {
		struct lpfc_trunk_link link = phba->trunk_link;

		if (bf_get(lpfc_conf_trunk_port0, &phba->sli4_hba))
			len += scnprintf(buf + len, PAGE_SIZE - len,
				"Trunk port 0: Link %s %s\n",
				(link.link0.state == LPFC_LINK_UP) ?
				 "Up" : "Down. ",
				trunk_errmsg[link.link0.fault]);

		if (bf_get(lpfc_conf_trunk_port1, &phba->sli4_hba))
			len += scnprintf(buf + len, PAGE_SIZE - len,
				"Trunk port 1: Link %s %s\n",
				(link.link1.state == LPFC_LINK_UP) ?
				 "Up" : "Down. ",
				trunk_errmsg[link.link1.fault]);

		if (bf_get(lpfc_conf_trunk_port2, &phba->sli4_hba))
			len += scnprintf(buf + len, PAGE_SIZE - len,
				"Trunk port 2: Link %s %s\n",
				(link.link2.state == LPFC_LINK_UP) ?
				 "Up" : "Down. ",
				trunk_errmsg[link.link2.fault]);

		if (bf_get(lpfc_conf_trunk_port3, &phba->sli4_hba))
			len += scnprintf(buf + len, PAGE_SIZE - len,
				"Trunk port 3: Link %s %s\n",
				(link.link3.state == LPFC_LINK_UP) ?
				 "Up" : "Down. ",
				trunk_errmsg[link.link3.fault]);

	}

	return len;
}

/**
 * lpfc_sli4_protocol_show - Return the fip mode of the HBA
 * @dev: class unused variable.
 * @attr: device attribute, not used.
 * @buf: on return contains the module description text.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_sli4_protocol_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba *phba = vport->phba;

	if (phba->sli_rev < LPFC_SLI_REV4)
		return scnprintf(buf, PAGE_SIZE, "fc\n");

	if (phba->sli4_hba.lnk_info.lnk_dv == LPFC_LNK_DAT_VAL) {
		if (phba->sli4_hba.lnk_info.lnk_tp == LPFC_LNK_TYPE_GE)
			return scnprintf(buf, PAGE_SIZE, "fcoe\n");
		if (phba->sli4_hba.lnk_info.lnk_tp == LPFC_LNK_TYPE_FC)
			return scnprintf(buf, PAGE_SIZE, "fc\n");
	}
	return scnprintf(buf, PAGE_SIZE, "unknown\n");
}

/**
 * lpfc_oas_supported_show - Return whether or not Optimized Access Storage
 *			    (OAS) is supported.
 * @dev: class unused variable.
 * @attr: device attribute, not used.
 * @buf: on return contains the module description text.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_oas_supported_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_hba *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			phba->sli4_hba.pc_sli4_params.oas_supported);
}

/**
 * lpfc_link_state_store - Transition the link_state on an HBA port
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: one or more lpfc_polling_flags values.
 * @count: not used.
 *
 * Returns:
 * -EINVAL if the buffer is not "up" or "down"
 * return from link state change function if non-zero
 * length of the buf on success
 **/
static ssize_t
lpfc_link_state_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	int status = -EINVAL;

	if ((strncmp(buf, "up", sizeof("up") - 1) == 0) &&
			(phba->link_state == LPFC_LINK_DOWN))
		status = phba->lpfc_hba_init_link(phba, MBX_NOWAIT);
	else if ((strncmp(buf, "down", sizeof("down") - 1) == 0) &&
			(phba->link_state >= LPFC_LINK_UP))
		status = phba->lpfc_hba_down_link(phba, MBX_NOWAIT);

	if (status == 0)
		return strlen(buf);
	else
		return status;
}

/**
 * lpfc_num_discovered_ports_show - Return sum of mapped and unmapped vports
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the sum of fc mapped and unmapped.
 *
 * Description:
 * Returns the ascii text number of the sum of the fc mapped and unmapped
 * vport counts.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_num_discovered_ports_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			vport->fc_map_cnt + vport->fc_unmap_cnt);
}

/**
 * lpfc_issue_lip - Misnomer, name carried over from long ago
 * @shost: Scsi_Host pointer.
 *
 * Description:
 * Bring the link down gracefully then re-init the link. The firmware will
 * re-init the fiber channel interface as required. Does not issue a LIP.
 *
 * Returns:
 * -EPERM port offline or management commands are being blocked
 * -ENOMEM cannot allocate memory for the mailbox command
 * -EIO error sending the mailbox command
 * zero for success
 **/
static int
lpfc_issue_lip(struct Scsi_Host *shost)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	LPFC_MBOXQ_t *pmboxq;
	int mbxstatus = MBXERR_ERROR;

	/*
	 * If the link is offline, disabled or BLOCK_MGMT_IO
	 * it doesn't make any sense to allow issue_lip
	 */
	if ((vport->fc_flag & FC_OFFLINE_MODE) ||
	    (phba->hba_flag & LINK_DISABLED) ||
	    (phba->sli.sli_flag & LPFC_BLOCK_MGMT_IO))
		return -EPERM;

	pmboxq = mempool_alloc(phba->mbox_mem_pool,GFP_KERNEL);

	if (!pmboxq)
		return -ENOMEM;

	memset((void *)pmboxq, 0, sizeof (LPFC_MBOXQ_t));
	pmboxq->u.mb.mbxCommand = MBX_DOWN_LINK;
	pmboxq->u.mb.mbxOwner = OWN_HOST;

	mbxstatus = lpfc_sli_issue_mbox_wait(phba, pmboxq, LPFC_MBOX_TMO * 2);

	if ((mbxstatus == MBX_SUCCESS) &&
	    (pmboxq->u.mb.mbxStatus == 0 ||
	     pmboxq->u.mb.mbxStatus == MBXERR_LINK_DOWN)) {
		memset((void *)pmboxq, 0, sizeof (LPFC_MBOXQ_t));
		lpfc_init_link(phba, pmboxq, phba->cfg_topology,
			       phba->cfg_link_speed);
		mbxstatus = lpfc_sli_issue_mbox_wait(phba, pmboxq,
						     phba->fc_ratov * 2);
		if ((mbxstatus == MBX_SUCCESS) &&
		    (pmboxq->u.mb.mbxStatus == MBXERR_SEC_NO_PERMISSION))
			lpfc_printf_log(phba, KERN_ERR, LOG_MBOX | LOG_SLI,
					"2859 SLI authentication is required "
					"for INIT_LINK but has not done yet\n");
	}

	lpfc_set_loopback_flag(phba);
	if (mbxstatus != MBX_TIMEOUT)
		mempool_free(pmboxq, phba->mbox_mem_pool);

	if (mbxstatus == MBXERR_ERROR)
		return -EIO;

	return 0;
}

int
lpfc_emptyq_wait(struct lpfc_hba *phba, struct list_head *q, spinlock_t *lock)
{
	int cnt = 0;

	spin_lock_irq(lock);
	while (!list_empty(q)) {
		spin_unlock_irq(lock);
		msleep(20);
		if (cnt++ > 250) {  /* 5 secs */
			lpfc_printf_log(phba, KERN_WARNING, LOG_INIT,
					"0466 %s %s\n",
					"Outstanding IO when ",
					"bringing Adapter offline\n");
				return 0;
		}
		spin_lock_irq(lock);
	}
	spin_unlock_irq(lock);
	return 1;
}

/**
 * lpfc_do_offline - Issues a mailbox command to bring the link down
 * @phba: lpfc_hba pointer.
 * @type: LPFC_EVT_OFFLINE, LPFC_EVT_WARM_START, LPFC_EVT_KILL.
 *
 * Notes:
 * Assumes any error from lpfc_do_offline() will be negative.
 * Can wait up to 5 seconds for the port ring buffers count
 * to reach zero, prints a warning if it is not zero and continues.
 * lpfc_workq_post_event() returns a non-zero return code if call fails.
 *
 * Returns:
 * -EIO error posting the event
 * zero for success
 **/
static int
lpfc_do_offline(struct lpfc_hba *phba, uint32_t type)
{
	struct completion online_compl;
	struct lpfc_queue *qp = NULL;
	struct lpfc_sli_ring *pring;
	struct lpfc_sli *psli;
	int status = 0;
	int i;
	int rc;

	init_completion(&online_compl);
	rc = lpfc_workq_post_event(phba, &status, &online_compl,
			      LPFC_EVT_OFFLINE_PREP);
	if (rc == 0)
		return -ENOMEM;

	wait_for_completion(&online_compl);

	if (status != 0)
		return -EIO;

	psli = &phba->sli;

	/*
	 * If freeing the queues have already started, don't access them.
	 * Otherwise set FREE_WAIT to indicate that queues are being used
	 * to hold the freeing process until we finish.
	 */
	spin_lock_irq(&phba->hbalock);
	if (!(psli->sli_flag & LPFC_QUEUE_FREE_INIT)) {
		psli->sli_flag |= LPFC_QUEUE_FREE_WAIT;
	} else {
		spin_unlock_irq(&phba->hbalock);
		goto skip_wait;
	}
	spin_unlock_irq(&phba->hbalock);

	/* Wait a little for things to settle down, but not
	 * long enough for dev loss timeout to expire.
	 */
	if (phba->sli_rev != LPFC_SLI_REV4) {
		for (i = 0; i < psli->num_rings; i++) {
			pring = &psli->sli3_ring[i];
			if (!lpfc_emptyq_wait(phba, &pring->txcmplq,
					      &phba->hbalock))
				goto out;
		}
	} else {
		list_for_each_entry(qp, &phba->sli4_hba.lpfc_wq_list, wq_list) {
			pring = qp->pring;
			if (!pring)
				continue;
			if (!lpfc_emptyq_wait(phba, &pring->txcmplq,
					      &pring->ring_lock))
				goto out;
		}
	}
out:
	spin_lock_irq(&phba->hbalock);
	psli->sli_flag &= ~LPFC_QUEUE_FREE_WAIT;
	spin_unlock_irq(&phba->hbalock);

skip_wait:
	init_completion(&online_compl);
	rc = lpfc_workq_post_event(phba, &status, &online_compl, type);
	if (rc == 0)
		return -ENOMEM;

	wait_for_completion(&online_compl);

	if (status != 0)
		return -EIO;

	return 0;
}

/**
 * lpfc_reset_pci_bus - resets PCI bridge controller's secondary bus of an HBA
 * @phba: lpfc_hba pointer.
 *
 * Description:
 * Issues a PCI secondary bus reset for the phba->pcidev.
 *
 * Notes:
 * First walks the bus_list to ensure only PCI devices with Emulex
 * vendor id, device ids that support hot reset, only one occurrence
 * of function 0, and all ports on the bus are in offline mode to ensure the
 * hot reset only affects one valid HBA.
 *
 * Returns:
 * -ENOTSUPP, cfg_enable_hba_reset must be of value 2
 * -ENODEV,   NULL ptr to pcidev
 * -EBADSLT,  detected invalid device
 * -EBUSY,    port is not in offline state
 *      0,    successful
 */
static int
lpfc_reset_pci_bus(struct lpfc_hba *phba)
{
	struct pci_dev *pdev = phba->pcidev;
	struct Scsi_Host *shost = NULL;
	struct lpfc_hba *phba_other = NULL;
	struct pci_dev *ptr = NULL;
	int res;

	if (phba->cfg_enable_hba_reset != 2)
		return -ENOTSUPP;

	if (!pdev) {
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT, "8345 pdev NULL!\n");
		return -ENODEV;
	}

	res = lpfc_check_pci_resettable(phba);
	if (res)
		return res;

	/* Walk the list of devices on the pci_dev's bus */
	list_for_each_entry(ptr, &pdev->bus->devices, bus_list) {
		/* Check port is offline */
		shost = pci_get_drvdata(ptr);
		if (shost) {
			phba_other =
				((struct lpfc_vport *)shost->hostdata)->phba;
			if (!(phba_other->pport->fc_flag & FC_OFFLINE_MODE)) {
				lpfc_printf_log(phba_other, KERN_INFO, LOG_INIT,
						"8349 WWPN = 0x%02x%02x%02x%02x"
						"%02x%02x%02x%02x is not "
						"offline!\n",
						phba_other->wwpn[0],
						phba_other->wwpn[1],
						phba_other->wwpn[2],
						phba_other->wwpn[3],
						phba_other->wwpn[4],
						phba_other->wwpn[5],
						phba_other->wwpn[6],
						phba_other->wwpn[7]);
				return -EBUSY;
			}
		}
	}

	/* Issue PCI bus reset */
	res = pci_reset_bus(pdev);
	if (res) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"8350 PCI reset bus failed: %d\n", res);
	}

	return res;
}

/**
 * lpfc_selective_reset - Offline then onlines the port
 * @phba: lpfc_hba pointer.
 *
 * Description:
 * If the port is configured to allow a reset then the hba is brought
 * offline then online.
 *
 * Notes:
 * Assumes any error from lpfc_do_offline() will be negative.
 * Do not make this function static.
 *
 * Returns:
 * lpfc_do_offline() return code if not zero
 * -EIO reset not configured or error posting the event
 * zero for success
 **/
int
lpfc_selective_reset(struct lpfc_hba *phba)
{
	struct completion online_compl;
	int status = 0;
	int rc;

	if (!phba->cfg_enable_hba_reset)
		return -EACCES;

	if (!(phba->pport->fc_flag & FC_OFFLINE_MODE)) {
		status = lpfc_do_offline(phba, LPFC_EVT_OFFLINE);

		if (status != 0)
			return status;
	}

	init_completion(&online_compl);
	rc = lpfc_workq_post_event(phba, &status, &online_compl,
			      LPFC_EVT_ONLINE);
	if (rc == 0)
		return -ENOMEM;

	wait_for_completion(&online_compl);

	if (status != 0)
		return -EIO;

	return 0;
}

/**
 * lpfc_issue_reset - Selectively resets an adapter
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: containing the string "selective".
 * @count: unused variable.
 *
 * Description:
 * If the buf contains the string "selective" then lpfc_selective_reset()
 * is called to perform the reset.
 *
 * Notes:
 * Assumes any error from lpfc_selective_reset() will be negative.
 * If lpfc_selective_reset() returns zero then the length of the buffer
 * is returned which indicates success
 *
 * Returns:
 * -EINVAL if the buffer does not contain the string "selective"
 * length of buf if lpfc-selective_reset() if the call succeeds
 * return value of lpfc_selective_reset() if the call fails
**/
static ssize_t
lpfc_issue_reset(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int status = -EINVAL;

	if (!phba->cfg_enable_hba_reset)
		return -EACCES;

	if (strncmp(buf, "selective", sizeof("selective") - 1) == 0)
		status = phba->lpfc_selective_reset(phba);

	if (status == 0)
		return strlen(buf);
	else
		return status;
}

/**
 * lpfc_sli4_pdev_status_reg_wait - Wait for pdev status register for readyness
 * @phba: lpfc_hba pointer.
 *
 * Description:
 * SLI4 interface type-2 device to wait on the sliport status register for
 * the readyness after performing a firmware reset.
 *
 * Returns:
 * zero for success, -EPERM when port does not have privilege to perform the
 * reset, -EIO when port timeout from recovering from the reset.
 *
 * Note:
 * As the caller will interpret the return code by value, be careful in making
 * change or addition to return codes.
 **/
int
lpfc_sli4_pdev_status_reg_wait(struct lpfc_hba *phba)
{
	struct lpfc_register portstat_reg = {0};
	int i;

	msleep(100);
	if (lpfc_readl(phba->sli4_hba.u.if_type2.STATUSregaddr,
		       &portstat_reg.word0))
		return -EIO;

	/* verify if privileged for the request operation */
	if (!bf_get(lpfc_sliport_status_rn, &portstat_reg) &&
	    !bf_get(lpfc_sliport_status_err, &portstat_reg))
		return -EPERM;

	/* wait for the SLI port firmware ready after firmware reset */
	for (i = 0; i < LPFC_FW_RESET_MAXIMUM_WAIT_10MS_CNT; i++) {
		msleep(10);
		if (lpfc_readl(phba->sli4_hba.u.if_type2.STATUSregaddr,
			       &portstat_reg.word0))
			continue;
		if (!bf_get(lpfc_sliport_status_err, &portstat_reg))
			continue;
		if (!bf_get(lpfc_sliport_status_rn, &portstat_reg))
			continue;
		if (!bf_get(lpfc_sliport_status_rdy, &portstat_reg))
			continue;
		break;
	}

	if (i < LPFC_FW_RESET_MAXIMUM_WAIT_10MS_CNT)
		return 0;
	else
		return -EIO;
}

/**
 * lpfc_sli4_pdev_reg_request - Request physical dev to perform a register acc
 * @phba: lpfc_hba pointer.
 *
 * Description:
 * Request SLI4 interface type-2 device to perform a physical register set
 * access.
 *
 * Returns:
 * zero for success
 **/
static ssize_t
lpfc_sli4_pdev_reg_request(struct lpfc_hba *phba, uint32_t opcode)
{
	struct completion online_compl;
	struct pci_dev *pdev = phba->pcidev;
	uint32_t before_fc_flag;
	uint32_t sriov_nr_virtfn;
	uint32_t reg_val;
	int status = 0, rc = 0;
	int job_posted = 1, sriov_err;

	if (!phba->cfg_enable_hba_reset)
		return -EACCES;

	if ((phba->sli_rev < LPFC_SLI_REV4) ||
	    (bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf) <
	     LPFC_SLI_INTF_IF_TYPE_2))
		return -EPERM;

	/* Keep state if we need to restore back */
	before_fc_flag = phba->pport->fc_flag;
	sriov_nr_virtfn = phba->cfg_sriov_nr_virtfn;

	/* Disable SR-IOV virtual functions if enabled */
	if (phba->cfg_sriov_nr_virtfn) {
		pci_disable_sriov(pdev);
		phba->cfg_sriov_nr_virtfn = 0;
	}

	if (opcode == LPFC_FW_DUMP)
		phba->hba_flag |= HBA_FW_DUMP_OP;

	status = lpfc_do_offline(phba, LPFC_EVT_OFFLINE);

	if (status != 0) {
		phba->hba_flag &= ~HBA_FW_DUMP_OP;
		return status;
	}

	/* wait for the device to be quiesced before firmware reset */
	msleep(100);

	reg_val = readl(phba->sli4_hba.conf_regs_memmap_p +
			LPFC_CTL_PDEV_CTL_OFFSET);

	if (opcode == LPFC_FW_DUMP)
		reg_val |= LPFC_FW_DUMP_REQUEST;
	else if (opcode == LPFC_FW_RESET)
		reg_val |= LPFC_CTL_PDEV_CTL_FRST;
	else if (opcode == LPFC_DV_RESET)
		reg_val |= LPFC_CTL_PDEV_CTL_DRST;

	writel(reg_val, phba->sli4_hba.conf_regs_memmap_p +
	       LPFC_CTL_PDEV_CTL_OFFSET);
	/* flush */
	readl(phba->sli4_hba.conf_regs_memmap_p + LPFC_CTL_PDEV_CTL_OFFSET);

	/* delay driver action following IF_TYPE_2 reset */
	rc = lpfc_sli4_pdev_status_reg_wait(phba);

	if (rc == -EPERM) {
		/* no privilege for reset */
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"3150 No privilege to perform the requested "
				"access: x%x\n", reg_val);
	} else if (rc == -EIO) {
		/* reset failed, there is nothing more we can do */
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI,
				"3153 Fail to perform the requested "
				"access: x%x\n", reg_val);
		return rc;
	}

	/* keep the original port state */
	if (before_fc_flag & FC_OFFLINE_MODE)
		goto out;

	init_completion(&online_compl);
	job_posted = lpfc_workq_post_event(phba, &status, &online_compl,
					   LPFC_EVT_ONLINE);
	if (!job_posted)
		goto out;

	wait_for_completion(&online_compl);

out:
	/* in any case, restore the virtual functions enabled as before */
	if (sriov_nr_virtfn) {
		sriov_err =
			lpfc_sli_probe_sriov_nr_virtfn(phba, sriov_nr_virtfn);
		if (!sriov_err)
			phba->cfg_sriov_nr_virtfn = sriov_nr_virtfn;
	}

	/* return proper error code */
	if (!rc) {
		if (!job_posted)
			rc = -ENOMEM;
		else if (status)
			rc = -EIO;
	}
	return rc;
}

/**
 * lpfc_nport_evt_cnt_show - Return the number of nport events
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the ascii number of nport events.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_nport_evt_cnt_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%d\n", phba->nport_event_cnt);
}

static int
lpfc_set_trunking(struct lpfc_hba *phba, char *buff_out)
{
	LPFC_MBOXQ_t *mbox = NULL;
	unsigned long val = 0;
	char *pval = NULL;
	int rc = 0;

	if (!strncmp("enable", buff_out,
				 strlen("enable"))) {
		pval = buff_out + strlen("enable") + 1;
		rc = kstrtoul(pval, 0, &val);
		if (rc)
			return rc; /* Invalid  number */
	} else if (!strncmp("disable", buff_out,
				 strlen("disable"))) {
		val = 0;
	} else {
		return -EINVAL;  /* Invalid command */
	}

	switch (val) {
	case 0:
		val = 0x0; /* Disable */
		break;
	case 2:
		val = 0x1; /* Enable two port trunk */
		break;
	case 4:
		val = 0x2; /* Enable four port trunk */
		break;
	default:
		return -EINVAL;
	}

	lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
			"0070 Set trunk mode with val %ld ", val);

	mbox = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!mbox)
		return -ENOMEM;

	lpfc_sli4_config(phba, mbox, LPFC_MBOX_SUBSYSTEM_FCOE,
			 LPFC_MBOX_OPCODE_FCOE_FC_SET_TRUNK_MODE,
			 12, LPFC_SLI4_MBX_EMBED);

	bf_set(lpfc_mbx_set_trunk_mode,
	       &mbox->u.mqe.un.set_trunk_mode,
	       val);
	rc = lpfc_sli_issue_mbox(phba, mbox, MBX_POLL);
	if (rc)
		lpfc_printf_log(phba, KERN_ERR, LOG_MBOX,
				"0071 Set trunk mode failed with status: %d",
				rc);
	if (rc != MBX_TIMEOUT)
		mempool_free(mbox, phba->mbox_mem_pool);

	return 0;
}

/**
 * lpfc_board_mode_show - Return the state of the board
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the state of the adapter.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_board_mode_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	char  * state;

	if (phba->link_state == LPFC_HBA_ERROR)
		state = "error";
	else if (phba->link_state == LPFC_WARM_START)
		state = "warm start";
	else if (phba->link_state == LPFC_INIT_START)
		state = "offline";
	else
		state = "online";

	return scnprintf(buf, PAGE_SIZE, "%s\n", state);
}

/**
 * lpfc_board_mode_store - Puts the hba in online, offline, warm or error state
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: containing one of the strings "online", "offline", "warm" or "error".
 * @count: unused variable.
 *
 * Returns:
 * -EACCES if enable hba reset not enabled
 * -EINVAL if the buffer does not contain a valid string (see above)
 * -EIO if lpfc_workq_post_event() or lpfc_do_offline() fails
 * buf length greater than zero indicates success
 **/
static ssize_t
lpfc_board_mode_store(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct completion online_compl;
	char *board_mode_str = NULL;
	int status = 0;
	int rc;

	if (!phba->cfg_enable_hba_reset) {
		status = -EACCES;
		goto board_mode_out;
	}

	lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
			 "3050 lpfc_board_mode set to %s\n", buf);

	init_completion(&online_compl);

	if(strncmp(buf, "online", sizeof("online") - 1) == 0) {
		rc = lpfc_workq_post_event(phba, &status, &online_compl,
				      LPFC_EVT_ONLINE);
		if (rc == 0) {
			status = -ENOMEM;
			goto board_mode_out;
		}
		wait_for_completion(&online_compl);
		if (status)
			status = -EIO;
	} else if (strncmp(buf, "offline", sizeof("offline") - 1) == 0)
		status = lpfc_do_offline(phba, LPFC_EVT_OFFLINE);
	else if (strncmp(buf, "warm", sizeof("warm") - 1) == 0)
		if (phba->sli_rev == LPFC_SLI_REV4)
			status = -EINVAL;
		else
			status = lpfc_do_offline(phba, LPFC_EVT_WARM_START);
	else if (strncmp(buf, "error", sizeof("error") - 1) == 0)
		if (phba->sli_rev == LPFC_SLI_REV4)
			status = -EINVAL;
		else
			status = lpfc_do_offline(phba, LPFC_EVT_KILL);
	else if (strncmp(buf, "dump", sizeof("dump") - 1) == 0)
		status = lpfc_sli4_pdev_reg_request(phba, LPFC_FW_DUMP);
	else if (strncmp(buf, "fw_reset", sizeof("fw_reset") - 1) == 0)
		status = lpfc_sli4_pdev_reg_request(phba, LPFC_FW_RESET);
	else if (strncmp(buf, "dv_reset", sizeof("dv_reset") - 1) == 0)
		status = lpfc_sli4_pdev_reg_request(phba, LPFC_DV_RESET);
	else if (strncmp(buf, "pci_bus_reset", sizeof("pci_bus_reset") - 1)
		 == 0)
		status = lpfc_reset_pci_bus(phba);
	else if (strncmp(buf, "trunk", sizeof("trunk") - 1) == 0)
		status = lpfc_set_trunking(phba, (char *)buf + sizeof("trunk"));
	else
		status = -EINVAL;

board_mode_out:
	if (!status)
		return strlen(buf);
	else {
		board_mode_str = strchr(buf, '\n');
		if (board_mode_str)
			*board_mode_str = '\0';
		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				 "3097 Failed \"%s\", status(%d), "
				 "fc_flag(x%x)\n",
				 buf, status, phba->pport->fc_flag);
		return status;
	}
}

/**
 * lpfc_get_hba_info - Return various bits of informaton about the adapter
 * @phba: pointer to the adapter structure.
 * @mxri: max xri count.
 * @axri: available xri count.
 * @mrpi: max rpi count.
 * @arpi: available rpi count.
 * @mvpi: max vpi count.
 * @avpi: available vpi count.
 *
 * Description:
 * If an integer pointer for an count is not null then the value for the
 * count is returned.
 *
 * Returns:
 * zero on error
 * one for success
 **/
static int
lpfc_get_hba_info(struct lpfc_hba *phba,
		  uint32_t *mxri, uint32_t *axri,
		  uint32_t *mrpi, uint32_t *arpi,
		  uint32_t *mvpi, uint32_t *avpi)
{
	struct lpfc_mbx_read_config *rd_config;
	LPFC_MBOXQ_t *pmboxq;
	MAILBOX_t *pmb;
	int rc = 0;
	uint32_t max_vpi;

	/*
	 * prevent udev from issuing mailbox commands until the port is
	 * configured.
	 */
	if (phba->link_state < LPFC_LINK_DOWN ||
	    !phba->mbox_mem_pool ||
	    (phba->sli.sli_flag & LPFC_SLI_ACTIVE) == 0)
		return 0;

	if (phba->sli.sli_flag & LPFC_BLOCK_MGMT_IO)
		return 0;

	pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmboxq)
		return 0;
	memset(pmboxq, 0, sizeof (LPFC_MBOXQ_t));

	pmb = &pmboxq->u.mb;
	pmb->mbxCommand = MBX_READ_CONFIG;
	pmb->mbxOwner = OWN_HOST;
	pmboxq->ctx_buf = NULL;

	if (phba->pport->fc_flag & FC_OFFLINE_MODE)
		rc = MBX_NOT_FINISHED;
	else
		rc = lpfc_sli_issue_mbox_wait(phba, pmboxq, phba->fc_ratov * 2);

	if (rc != MBX_SUCCESS) {
		if (rc != MBX_TIMEOUT)
			mempool_free(pmboxq, phba->mbox_mem_pool);
		return 0;
	}

	if (phba->sli_rev == LPFC_SLI_REV4) {
		rd_config = &pmboxq->u.mqe.un.rd_config;
		if (mrpi)
			*mrpi = bf_get(lpfc_mbx_rd_conf_rpi_count, rd_config);
		if (arpi)
			*arpi = bf_get(lpfc_mbx_rd_conf_rpi_count, rd_config) -
					phba->sli4_hba.max_cfg_param.rpi_used;
		if (mxri)
			*mxri = bf_get(lpfc_mbx_rd_conf_xri_count, rd_config);
		if (axri)
			*axri = bf_get(lpfc_mbx_rd_conf_xri_count, rd_config) -
					phba->sli4_hba.max_cfg_param.xri_used;

		/* Account for differences with SLI-3.  Get vpi count from
		 * mailbox data and subtract one for max vpi value.
		 */
		max_vpi = (bf_get(lpfc_mbx_rd_conf_vpi_count, rd_config) > 0) ?
			(bf_get(lpfc_mbx_rd_conf_vpi_count, rd_config) - 1) : 0;

		/* Limit the max we support */
		if (max_vpi > LPFC_MAX_VPI)
			max_vpi = LPFC_MAX_VPI;
		if (mvpi)
			*mvpi = max_vpi;
		if (avpi)
			*avpi = max_vpi - phba->sli4_hba.max_cfg_param.vpi_used;
	} else {
		if (mrpi)
			*mrpi = pmb->un.varRdConfig.max_rpi;
		if (arpi)
			*arpi = pmb->un.varRdConfig.avail_rpi;
		if (mxri)
			*mxri = pmb->un.varRdConfig.max_xri;
		if (axri)
			*axri = pmb->un.varRdConfig.avail_xri;
		if (mvpi)
			*mvpi = pmb->un.varRdConfig.max_vpi;
		if (avpi) {
			/* avail_vpi is only valid if link is up and ready */
			if (phba->link_state == LPFC_HBA_READY)
				*avpi = pmb->un.varRdConfig.avail_vpi;
			else
				*avpi = pmb->un.varRdConfig.max_vpi;
		}
	}

	mempool_free(pmboxq, phba->mbox_mem_pool);
	return 1;
}

/**
 * lpfc_max_rpi_show - Return maximum rpi
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the maximum rpi count in decimal or "Unknown".
 *
 * Description:
 * Calls lpfc_get_hba_info() asking for just the mrpi count.
 * If lpfc_get_hba_info() returns zero (failure) the buffer text is set
 * to "Unknown" and the buffer length is returned, therefore the caller
 * must check for "Unknown" in the buffer to detect a failure.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_max_rpi_show(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t cnt;

	if (lpfc_get_hba_info(phba, NULL, NULL, &cnt, NULL, NULL, NULL))
		return scnprintf(buf, PAGE_SIZE, "%d\n", cnt);
	return scnprintf(buf, PAGE_SIZE, "Unknown\n");
}

/**
 * lpfc_used_rpi_show - Return maximum rpi minus available rpi
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: containing the used rpi count in decimal or "Unknown".
 *
 * Description:
 * Calls lpfc_get_hba_info() asking for just the mrpi and arpi counts.
 * If lpfc_get_hba_info() returns zero (failure) the buffer text is set
 * to "Unknown" and the buffer length is returned, therefore the caller
 * must check for "Unknown" in the buffer to detect a failure.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_used_rpi_show(struct device *dev, struct device_attribute *attr,
		   char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t cnt, acnt;

	if (lpfc_get_hba_info(phba, NULL, NULL, &cnt, &acnt, NULL, NULL))
		return scnprintf(buf, PAGE_SIZE, "%d\n", (cnt - acnt));
	return scnprintf(buf, PAGE_SIZE, "Unknown\n");
}

/**
 * lpfc_max_xri_show - Return maximum xri
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the maximum xri count in decimal or "Unknown".
 *
 * Description:
 * Calls lpfc_get_hba_info() asking for just the mrpi count.
 * If lpfc_get_hba_info() returns zero (failure) the buffer text is set
 * to "Unknown" and the buffer length is returned, therefore the caller
 * must check for "Unknown" in the buffer to detect a failure.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_max_xri_show(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t cnt;

	if (lpfc_get_hba_info(phba, &cnt, NULL, NULL, NULL, NULL, NULL))
		return scnprintf(buf, PAGE_SIZE, "%d\n", cnt);
	return scnprintf(buf, PAGE_SIZE, "Unknown\n");
}

/**
 * lpfc_used_xri_show - Return maximum xpi minus the available xpi
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the used xri count in decimal or "Unknown".
 *
 * Description:
 * Calls lpfc_get_hba_info() asking for just the mxri and axri counts.
 * If lpfc_get_hba_info() returns zero (failure) the buffer text is set
 * to "Unknown" and the buffer length is returned, therefore the caller
 * must check for "Unknown" in the buffer to detect a failure.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_used_xri_show(struct device *dev, struct device_attribute *attr,
		   char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t cnt, acnt;

	if (lpfc_get_hba_info(phba, &cnt, &acnt, NULL, NULL, NULL, NULL))
		return scnprintf(buf, PAGE_SIZE, "%d\n", (cnt - acnt));
	return scnprintf(buf, PAGE_SIZE, "Unknown\n");
}

/**
 * lpfc_max_vpi_show - Return maximum vpi
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the maximum vpi count in decimal or "Unknown".
 *
 * Description:
 * Calls lpfc_get_hba_info() asking for just the mvpi count.
 * If lpfc_get_hba_info() returns zero (failure) the buffer text is set
 * to "Unknown" and the buffer length is returned, therefore the caller
 * must check for "Unknown" in the buffer to detect a failure.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_max_vpi_show(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t cnt;

	if (lpfc_get_hba_info(phba, NULL, NULL, NULL, NULL, &cnt, NULL))
		return scnprintf(buf, PAGE_SIZE, "%d\n", cnt);
	return scnprintf(buf, PAGE_SIZE, "Unknown\n");
}

/**
 * lpfc_used_vpi_show - Return maximum vpi minus the available vpi
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the used vpi count in decimal or "Unknown".
 *
 * Description:
 * Calls lpfc_get_hba_info() asking for just the mvpi and avpi counts.
 * If lpfc_get_hba_info() returns zero (failure) the buffer text is set
 * to "Unknown" and the buffer length is returned, therefore the caller
 * must check for "Unknown" in the buffer to detect a failure.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_used_vpi_show(struct device *dev, struct device_attribute *attr,
		   char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t cnt, acnt;

	if (lpfc_get_hba_info(phba, NULL, NULL, NULL, NULL, &cnt, &acnt))
		return scnprintf(buf, PAGE_SIZE, "%d\n", (cnt - acnt));
	return scnprintf(buf, PAGE_SIZE, "Unknown\n");
}

/**
 * lpfc_npiv_info_show - Return text about NPIV support for the adapter
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: text that must be interpreted to determine if npiv is supported.
 *
 * Description:
 * Buffer will contain text indicating npiv is not suppoerted on the port,
 * the port is an NPIV physical port, or it is an npiv virtual port with
 * the id of the vport.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_npiv_info_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	if (!(phba->max_vpi))
		return scnprintf(buf, PAGE_SIZE, "NPIV Not Supported\n");
	if (vport->port_type == LPFC_PHYSICAL_PORT)
		return scnprintf(buf, PAGE_SIZE, "NPIV Physical\n");
	return scnprintf(buf, PAGE_SIZE, "NPIV Virtual (VPI %d)\n", vport->vpi);
}

/**
 * lpfc_poll_show - Return text about poll support for the adapter
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the cfg_poll in hex.
 *
 * Notes:
 * cfg_poll should be a lpfc_polling_flags type.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_poll_show(struct device *dev, struct device_attribute *attr,
	       char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%#x\n", phba->cfg_poll);
}

/**
 * lpfc_poll_store - Set the value of cfg_poll for the adapter
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: one or more lpfc_polling_flags values.
 * @count: not used.
 *
 * Notes:
 * buf contents converted to integer and checked for a valid value.
 *
 * Returns:
 * -EINVAL if the buffer connot be converted or is out of range
 * length of the buf on success
 **/
static ssize_t
lpfc_poll_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	uint32_t creg_val;
	uint32_t old_val;
	int val=0;

	if (!isdigit(buf[0]))
		return -EINVAL;

	if (sscanf(buf, "%i", &val) != 1)
		return -EINVAL;

	if ((val & 0x3) != val)
		return -EINVAL;

	if (phba->sli_rev == LPFC_SLI_REV4)
		val = 0;

	lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
		"3051 lpfc_poll changed from %d to %d\n",
		phba->cfg_poll, val);

	spin_lock_irq(&phba->hbalock);

	old_val = phba->cfg_poll;

	if (val & ENABLE_FCP_RING_POLLING) {
		if ((val & DISABLE_FCP_RING_INT) &&
		    !(old_val & DISABLE_FCP_RING_INT)) {
			if (lpfc_readl(phba->HCregaddr, &creg_val)) {
				spin_unlock_irq(&phba->hbalock);
				return -EINVAL;
			}
			creg_val &= ~(HC_R0INT_ENA << LPFC_FCP_RING);
			writel(creg_val, phba->HCregaddr);
			readl(phba->HCregaddr); /* flush */

			lpfc_poll_start_timer(phba);
		}
	} else if (val != 0x0) {
		spin_unlock_irq(&phba->hbalock);
		return -EINVAL;
	}

	if (!(val & DISABLE_FCP_RING_INT) &&
	    (old_val & DISABLE_FCP_RING_INT))
	{
		spin_unlock_irq(&phba->hbalock);
		del_timer(&phba->fcp_poll_timer);
		spin_lock_irq(&phba->hbalock);
		if (lpfc_readl(phba->HCregaddr, &creg_val)) {
			spin_unlock_irq(&phba->hbalock);
			return -EINVAL;
		}
		creg_val |= (HC_R0INT_ENA << LPFC_FCP_RING);
		writel(creg_val, phba->HCregaddr);
		readl(phba->HCregaddr); /* flush */
	}

	phba->cfg_poll = val;

	spin_unlock_irq(&phba->hbalock);

	return strlen(buf);
}

/**
 * lpfc_fips_level_show - Return the current FIPS level for the HBA
 * @dev: class unused variable.
 * @attr: device attribute, not used.
 * @buf: on return contains the module description text.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_fips_level_show(struct device *dev,  struct device_attribute *attr,
		     char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%d\n", phba->fips_level);
}

/**
 * lpfc_fips_rev_show - Return the FIPS Spec revision for the HBA
 * @dev: class unused variable.
 * @attr: device attribute, not used.
 * @buf: on return contains the module description text.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_fips_rev_show(struct device *dev,  struct device_attribute *attr,
		   char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%d\n", phba->fips_spec_rev);
}

/**
 * lpfc_dss_show - Return the current state of dss and the configured state
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the formatted text.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_dss_show(struct device *dev, struct device_attribute *attr,
	      char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%s - %sOperational\n",
			(phba->cfg_enable_dss) ? "Enabled" : "Disabled",
			(phba->sli3_options & LPFC_SLI3_DSS_ENABLED) ?
				"" : "Not ");
}

/**
 * lpfc_sriov_hw_max_virtfn_show - Return maximum number of virtual functions
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the formatted support level.
 *
 * Description:
 * Returns the maximum number of virtual functions a physical function can
 * support, 0 will be returned if called on virtual function.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_sriov_hw_max_virtfn_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	uint16_t max_nr_virtfn;

	max_nr_virtfn = lpfc_sli_sriov_nr_virtfn_get(phba);
	return scnprintf(buf, PAGE_SIZE, "%d\n", max_nr_virtfn);
}

static inline bool lpfc_rangecheck(uint val, uint min, uint max)
{
	return val >= min && val <= max;
}

/**
 * lpfc_enable_bbcr_set: Sets an attribute value.
 * @phba: pointer the the adapter structure.
 * @val: integer attribute value.
 *
 * Description:
 * Validates the min and max values then sets the
 * adapter config field if in the valid range. prints error message
 * and does not set the parameter if invalid.
 *
 * Returns:
 * zero on success
 * -EINVAL if val is invalid
 */
static ssize_t
lpfc_enable_bbcr_set(struct lpfc_hba *phba, uint val)
{
	if (lpfc_rangecheck(val, 0, 1) && phba->sli_rev == LPFC_SLI_REV4) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3068 %s_enable_bbcr changed from %d to %d\n",
				LPFC_DRIVER_NAME, phba->cfg_enable_bbcr, val);
		phba->cfg_enable_bbcr = val;
		return 0;
	}
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"0451 %s_enable_bbcr cannot set to %d, range is 0, 1\n",
			LPFC_DRIVER_NAME, val);
	return -EINVAL;
}

/**
 * lpfc_param_show - Return a cfg attribute value in decimal
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_show.
 *
 * lpfc_##attr##_show: Return the decimal value of an adapters cfg_xxx field.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the attribute value in decimal.
 *
 * Returns: size of formatted string.
 **/
#define lpfc_param_show(attr)	\
static ssize_t \
lpfc_##attr##_show(struct device *dev, struct device_attribute *attr, \
		   char *buf) \
{ \
	struct Scsi_Host  *shost = class_to_shost(dev);\
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;\
	struct lpfc_hba   *phba = vport->phba;\
	return scnprintf(buf, PAGE_SIZE, "%d\n",\
			phba->cfg_##attr);\
}

/**
 * lpfc_param_hex_show - Return a cfg attribute value in hex
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_show
 *
 * lpfc_##attr##_show: Return the hex value of an adapters cfg_xxx field.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the attribute value in hexadecimal.
 *
 * Returns: size of formatted string.
 **/
#define lpfc_param_hex_show(attr)	\
static ssize_t \
lpfc_##attr##_show(struct device *dev, struct device_attribute *attr, \
		   char *buf) \
{ \
	struct Scsi_Host  *shost = class_to_shost(dev);\
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;\
	struct lpfc_hba   *phba = vport->phba;\
	uint val = 0;\
	val = phba->cfg_##attr;\
	return scnprintf(buf, PAGE_SIZE, "%#x\n",\
			phba->cfg_##attr);\
}

/**
 * lpfc_param_init - Initializes a cfg attribute
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_init. The macro also
 * takes a default argument, a minimum and maximum argument.
 *
 * lpfc_##attr##_init: Initializes an attribute.
 * @phba: pointer the the adapter structure.
 * @val: integer attribute value.
 *
 * Validates the min and max values then sets the adapter config field
 * accordingly, or uses the default if out of range and prints an error message.
 *
 * Returns:
 * zero on success
 * -EINVAL if default used
 **/
#define lpfc_param_init(attr, default, minval, maxval)	\
static int \
lpfc_##attr##_init(struct lpfc_hba *phba, uint val) \
{ \
	if (lpfc_rangecheck(val, minval, maxval)) {\
		phba->cfg_##attr = val;\
		return 0;\
	}\
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT, \
			"0449 lpfc_"#attr" attribute cannot be set to %d, "\
			"allowed range is ["#minval", "#maxval"]\n", val); \
	phba->cfg_##attr = default;\
	return -EINVAL;\
}

/**
 * lpfc_param_set - Set a cfg attribute value
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_set
 *
 * lpfc_##attr##_set: Sets an attribute value.
 * @phba: pointer the the adapter structure.
 * @val: integer attribute value.
 *
 * Description:
 * Validates the min and max values then sets the
 * adapter config field if in the valid range. prints error message
 * and does not set the parameter if invalid.
 *
 * Returns:
 * zero on success
 * -EINVAL if val is invalid
 **/
#define lpfc_param_set(attr, default, minval, maxval)	\
static int \
lpfc_##attr##_set(struct lpfc_hba *phba, uint val) \
{ \
	if (lpfc_rangecheck(val, minval, maxval)) {\
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT, \
			"3052 lpfc_" #attr " changed from %d to %d\n", \
			phba->cfg_##attr, val); \
		phba->cfg_##attr = val;\
		return 0;\
	}\
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT, \
			"0450 lpfc_"#attr" attribute cannot be set to %d, "\
			"allowed range is ["#minval", "#maxval"]\n", val); \
	return -EINVAL;\
}

/**
 * lpfc_param_store - Set a vport attribute value
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_store.
 *
 * lpfc_##attr##_store: Set an sttribute value.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: contains the attribute value in ascii.
 * @count: not used.
 *
 * Description:
 * Convert the ascii text number to an integer, then
 * use the lpfc_##attr##_set function to set the value.
 *
 * Returns:
 * -EINVAL if val is invalid or lpfc_##attr##_set() fails
 * length of buffer upon success.
 **/
#define lpfc_param_store(attr)	\
static ssize_t \
lpfc_##attr##_store(struct device *dev, struct device_attribute *attr, \
		    const char *buf, size_t count) \
{ \
	struct Scsi_Host  *shost = class_to_shost(dev);\
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;\
	struct lpfc_hba   *phba = vport->phba;\
	uint val = 0;\
	if (!isdigit(buf[0]))\
		return -EINVAL;\
	if (sscanf(buf, "%i", &val) != 1)\
		return -EINVAL;\
	if (lpfc_##attr##_set(phba, val) == 0) \
		return strlen(buf);\
	else \
		return -EINVAL;\
}

/**
 * lpfc_vport_param_show - Return decimal formatted cfg attribute value
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_show
 *
 * lpfc_##attr##_show: prints the attribute value in decimal.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the attribute value in decimal.
 *
 * Returns: length of formatted string.
 **/
#define lpfc_vport_param_show(attr)	\
static ssize_t \
lpfc_##attr##_show(struct device *dev, struct device_attribute *attr, \
		   char *buf) \
{ \
	struct Scsi_Host  *shost = class_to_shost(dev);\
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;\
	return scnprintf(buf, PAGE_SIZE, "%d\n", vport->cfg_##attr);\
}

/**
 * lpfc_vport_param_hex_show - Return hex formatted attribute value
 *
 * Description:
 * Macro that given an attr e.g.
 * hba_queue_depth expands into a function with the name
 * lpfc_hba_queue_depth_show
 *
 * lpfc_##attr##_show: prints the attribute value in hexadecimal.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the attribute value in hexadecimal.
 *
 * Returns: length of formatted string.
 **/
#define lpfc_vport_param_hex_show(attr)	\
static ssize_t \
lpfc_##attr##_show(struct device *dev, struct device_attribute *attr, \
		   char *buf) \
{ \
	struct Scsi_Host  *shost = class_to_shost(dev);\
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;\
	return scnprintf(buf, PAGE_SIZE, "%#x\n", vport->cfg_##attr);\
}

/**
 * lpfc_vport_param_init - Initialize a vport cfg attribute
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_init. The macro also
 * takes a default argument, a minimum and maximum argument.
 *
 * lpfc_##attr##_init: validates the min and max values then sets the
 * adapter config field accordingly, or uses the default if out of range
 * and prints an error message.
 * @phba: pointer the the adapter structure.
 * @val: integer attribute value.
 *
 * Returns:
 * zero on success
 * -EINVAL if default used
 **/
#define lpfc_vport_param_init(attr, default, minval, maxval)	\
static int \
lpfc_##attr##_init(struct lpfc_vport *vport, uint val) \
{ \
	if (lpfc_rangecheck(val, minval, maxval)) {\
		vport->cfg_##attr = val;\
		return 0;\
	}\
	lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT, \
			 "0423 lpfc_"#attr" attribute cannot be set to %d, "\
			 "allowed range is ["#minval", "#maxval"]\n", val); \
	vport->cfg_##attr = default;\
	return -EINVAL;\
}

/**
 * lpfc_vport_param_set - Set a vport cfg attribute
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth expands
 * into a function with the name lpfc_hba_queue_depth_set
 *
 * lpfc_##attr##_set: validates the min and max values then sets the
 * adapter config field if in the valid range. prints error message
 * and does not set the parameter if invalid.
 * @phba: pointer the the adapter structure.
 * @val:	integer attribute value.
 *
 * Returns:
 * zero on success
 * -EINVAL if val is invalid
 **/
#define lpfc_vport_param_set(attr, default, minval, maxval)	\
static int \
lpfc_##attr##_set(struct lpfc_vport *vport, uint val) \
{ \
	if (lpfc_rangecheck(val, minval, maxval)) {\
		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT, \
			"3053 lpfc_" #attr \
			" changed from %d (x%x) to %d (x%x)\n", \
			vport->cfg_##attr, vport->cfg_##attr, \
			val, val); \
		vport->cfg_##attr = val;\
		return 0;\
	}\
	lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT, \
			 "0424 lpfc_"#attr" attribute cannot be set to %d, "\
			 "allowed range is ["#minval", "#maxval"]\n", val); \
	return -EINVAL;\
}

/**
 * lpfc_vport_param_store - Set a vport attribute
 *
 * Description:
 * Macro that given an attr e.g. hba_queue_depth
 * expands into a function with the name lpfc_hba_queue_depth_store
 *
 * lpfc_##attr##_store: convert the ascii text number to an integer, then
 * use the lpfc_##attr##_set function to set the value.
 * @cdev: class device that is converted into a Scsi_host.
 * @buf:	contains the attribute value in decimal.
 * @count: not used.
 *
 * Returns:
 * -EINVAL if val is invalid or lpfc_##attr##_set() fails
 * length of buffer upon success.
 **/
#define lpfc_vport_param_store(attr)	\
static ssize_t \
lpfc_##attr##_store(struct device *dev, struct device_attribute *attr, \
		    const char *buf, size_t count) \
{ \
	struct Scsi_Host  *shost = class_to_shost(dev);\
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;\
	uint val = 0;\
	if (!isdigit(buf[0]))\
		return -EINVAL;\
	if (sscanf(buf, "%i", &val) != 1)\
		return -EINVAL;\
	if (lpfc_##attr##_set(vport, val) == 0) \
		return strlen(buf);\
	else \
		return -EINVAL;\
}


static DEVICE_ATTR(nvme_info, 0444, lpfc_nvme_info_show, NULL);
static DEVICE_ATTR(scsi_stat, 0444, lpfc_scsi_stat_show, NULL);
static DEVICE_ATTR(bg_info, S_IRUGO, lpfc_bg_info_show, NULL);
static DEVICE_ATTR(bg_guard_err, S_IRUGO, lpfc_bg_guard_err_show, NULL);
static DEVICE_ATTR(bg_apptag_err, S_IRUGO, lpfc_bg_apptag_err_show, NULL);
static DEVICE_ATTR(bg_reftag_err, S_IRUGO, lpfc_bg_reftag_err_show, NULL);
static DEVICE_ATTR(info, S_IRUGO, lpfc_info_show, NULL);
static DEVICE_ATTR(serialnum, S_IRUGO, lpfc_serialnum_show, NULL);
static DEVICE_ATTR(modeldesc, S_IRUGO, lpfc_modeldesc_show, NULL);
static DEVICE_ATTR(modelname, S_IRUGO, lpfc_modelname_show, NULL);
static DEVICE_ATTR(programtype, S_IRUGO, lpfc_programtype_show, NULL);
static DEVICE_ATTR(portnum, S_IRUGO, lpfc_vportnum_show, NULL);
static DEVICE_ATTR(fwrev, S_IRUGO, lpfc_fwrev_show, NULL);
static DEVICE_ATTR(hdw, S_IRUGO, lpfc_hdw_show, NULL);
static DEVICE_ATTR(link_state, S_IRUGO | S_IWUSR, lpfc_link_state_show,
		lpfc_link_state_store);
static DEVICE_ATTR(option_rom_version, S_IRUGO,
		   lpfc_option_rom_version_show, NULL);
static DEVICE_ATTR(num_discovered_ports, S_IRUGO,
		   lpfc_num_discovered_ports_show, NULL);
static DEVICE_ATTR(menlo_mgmt_mode, S_IRUGO, lpfc_mlomgmt_show, NULL);
static DEVICE_ATTR(nport_evt_cnt, S_IRUGO, lpfc_nport_evt_cnt_show, NULL);
static DEVICE_ATTR_RO(lpfc_drvr_version);
static DEVICE_ATTR_RO(lpfc_enable_fip);
static DEVICE_ATTR(board_mode, S_IRUGO | S_IWUSR,
		   lpfc_board_mode_show, lpfc_board_mode_store);
static DEVICE_ATTR(issue_reset, S_IWUSR, NULL, lpfc_issue_reset);
static DEVICE_ATTR(max_vpi, S_IRUGO, lpfc_max_vpi_show, NULL);
static DEVICE_ATTR(used_vpi, S_IRUGO, lpfc_used_vpi_show, NULL);
static DEVICE_ATTR(max_rpi, S_IRUGO, lpfc_max_rpi_show, NULL);
static DEVICE_ATTR(used_rpi, S_IRUGO, lpfc_used_rpi_show, NULL);
static DEVICE_ATTR(max_xri, S_IRUGO, lpfc_max_xri_show, NULL);
static DEVICE_ATTR(used_xri, S_IRUGO, lpfc_used_xri_show, NULL);
static DEVICE_ATTR(npiv_info, S_IRUGO, lpfc_npiv_info_show, NULL);
static DEVICE_ATTR_RO(lpfc_temp_sensor);
static DEVICE_ATTR_RO(lpfc_fips_level);
static DEVICE_ATTR_RO(lpfc_fips_rev);
static DEVICE_ATTR_RO(lpfc_dss);
static DEVICE_ATTR_RO(lpfc_sriov_hw_max_virtfn);
static DEVICE_ATTR(protocol, S_IRUGO, lpfc_sli4_protocol_show, NULL);
static DEVICE_ATTR(lpfc_xlane_supported, S_IRUGO, lpfc_oas_supported_show,
		   NULL);

static char *lpfc_soft_wwn_key = "C99G71SL8032A";
#define WWN_SZ 8
/**
 * lpfc_wwn_set - Convert string to the 8 byte WWN value.
 * @buf: WWN string.
 * @cnt: Length of string.
 * @wwn: Array to receive converted wwn value.
 *
 * Returns:
 * -EINVAL if the buffer does not contain a valid wwn
 * 0 success
 **/
static size_t
lpfc_wwn_set(const char *buf, size_t cnt, char wwn[])
{
	unsigned int i, j;

	/* Count may include a LF at end of string */
	if (buf[cnt-1] == '\n')
		cnt--;

	if ((cnt < 16) || (cnt > 18) || ((cnt == 17) && (*buf++ != 'x')) ||
	    ((cnt == 18) && ((*buf++ != '0') || (*buf++ != 'x'))))
		return -EINVAL;

	memset(wwn, 0, WWN_SZ);

	/* Validate and store the new name */
	for (i = 0, j = 0; i < 16; i++) {
		if ((*buf >= 'a') && (*buf <= 'f'))
			j = ((j << 4) | ((*buf++ - 'a') + 10));
		else if ((*buf >= 'A') && (*buf <= 'F'))
			j = ((j << 4) | ((*buf++ - 'A') + 10));
		else if ((*buf >= '0') && (*buf <= '9'))
			j = ((j << 4) | (*buf++ - '0'));
		else
			return -EINVAL;
		if (i % 2) {
			wwn[i/2] = j & 0xff;
			j = 0;
		}
	}
	return 0;
}
/**
 * lpfc_soft_wwn_enable_store - Allows setting of the wwn if the key is valid
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: containing the string lpfc_soft_wwn_key.
 * @count: must be size of lpfc_soft_wwn_key.
 *
 * Returns:
 * -EINVAL if the buffer does not contain lpfc_soft_wwn_key
 * length of buf indicates success
 **/
static ssize_t
lpfc_soft_wwn_enable_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	unsigned int cnt = count;
	uint8_t vvvl = vport->fc_sparam.cmn.valid_vendor_ver_level;
	u32 *fawwpn_key = (uint32_t *)&vport->fc_sparam.un.vendorVersion[0];

	/*
	 * We're doing a simple sanity check for soft_wwpn setting.
	 * We require that the user write a specific key to enable
	 * the soft_wwpn attribute to be settable. Once the attribute
	 * is written, the enable key resets. If further updates are
	 * desired, the key must be written again to re-enable the
	 * attribute.
	 *
	 * The "key" is not secret - it is a hardcoded string shown
	 * here. The intent is to protect against the random user or
	 * application that is just writing attributes.
	 */
	if (vvvl == 1 && cpu_to_be32(*fawwpn_key) == FAPWWN_KEY_VENDOR) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				 "0051 "LPFC_DRIVER_NAME" soft wwpn can not"
				 " be enabled: fawwpn is enabled\n");
		return -EINVAL;
	}

	/* count may include a LF at end of string */
	if (buf[cnt-1] == '\n')
		cnt--;

	if ((cnt != strlen(lpfc_soft_wwn_key)) ||
	    (strncmp(buf, lpfc_soft_wwn_key, strlen(lpfc_soft_wwn_key)) != 0))
		return -EINVAL;

	phba->soft_wwn_enable = 1;

	dev_printk(KERN_WARNING, &phba->pcidev->dev,
		   "lpfc%d: soft_wwpn assignment has been enabled.\n",
		   phba->brd_no);
	dev_printk(KERN_WARNING, &phba->pcidev->dev,
		   "  The soft_wwpn feature is not supported by Broadcom.");

	return count;
}
static DEVICE_ATTR_WO(lpfc_soft_wwn_enable);

/**
 * lpfc_soft_wwpn_show - Return the cfg soft ww port name of the adapter
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the wwpn in hexadecimal.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_soft_wwpn_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n",
			(unsigned long long)phba->cfg_soft_wwpn);
}

/**
 * lpfc_soft_wwpn_store - Set the ww port name of the adapter
 * @dev class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: contains the wwpn in hexadecimal.
 * @count: number of wwpn bytes in buf
 *
 * Returns:
 * -EACCES hba reset not enabled, adapter over temp
 * -EINVAL soft wwn not enabled, count is invalid, invalid wwpn byte invalid
 * -EIO error taking adapter offline or online
 * value of count on success
 **/
static ssize_t
lpfc_soft_wwpn_store(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct completion online_compl;
	int stat1 = 0, stat2 = 0;
	unsigned int cnt = count;
	u8 wwpn[WWN_SZ];
	int rc;

	if (!phba->cfg_enable_hba_reset)
		return -EACCES;
	spin_lock_irq(&phba->hbalock);
	if (phba->over_temp_state == HBA_OVER_TEMP) {
		spin_unlock_irq(&phba->hbalock);
		return -EACCES;
	}
	spin_unlock_irq(&phba->hbalock);
	/* count may include a LF at end of string */
	if (buf[cnt-1] == '\n')
		cnt--;

	if (!phba->soft_wwn_enable)
		return -EINVAL;

	/* lock setting wwpn, wwnn down */
	phba->soft_wwn_enable = 0;

	rc = lpfc_wwn_set(buf, cnt, wwpn);
	if (rc) {
		/* not able to set wwpn, unlock it */
		phba->soft_wwn_enable = 1;
		return rc;
	}

	phba->cfg_soft_wwpn = wwn_to_u64(wwpn);
	fc_host_port_name(shost) = phba->cfg_soft_wwpn;
	if (phba->cfg_soft_wwnn)
		fc_host_node_name(shost) = phba->cfg_soft_wwnn;

	dev_printk(KERN_NOTICE, &phba->pcidev->dev,
		   "lpfc%d: Reinitializing to use soft_wwpn\n", phba->brd_no);

	stat1 = lpfc_do_offline(phba, LPFC_EVT_OFFLINE);
	if (stat1)
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0463 lpfc_soft_wwpn attribute set failed to "
				"reinit adapter - %d\n", stat1);
	init_completion(&online_compl);
	rc = lpfc_workq_post_event(phba, &stat2, &online_compl,
				   LPFC_EVT_ONLINE);
	if (rc == 0)
		return -ENOMEM;

	wait_for_completion(&online_compl);
	if (stat2)
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0464 lpfc_soft_wwpn attribute set failed to "
				"reinit adapter - %d\n", stat2);
	return (stat1 || stat2) ? -EIO : count;
}
static DEVICE_ATTR_RW(lpfc_soft_wwpn);

/**
 * lpfc_soft_wwnn_show - Return the cfg soft ww node name for the adapter
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: on return contains the wwnn in hexadecimal.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_soft_wwnn_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	return scnprintf(buf, PAGE_SIZE, "0x%llx\n",
			(unsigned long long)phba->cfg_soft_wwnn);
}

/**
 * lpfc_soft_wwnn_store - sets the ww node name of the adapter
 * @cdev: class device that is converted into a Scsi_host.
 * @buf: contains the ww node name in hexadecimal.
 * @count: number of wwnn bytes in buf.
 *
 * Returns:
 * -EINVAL soft wwn not enabled, count is invalid, invalid wwnn byte invalid
 * value of count on success
 **/
static ssize_t
lpfc_soft_wwnn_store(struct device *dev, struct device_attribute *attr,
		     const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	unsigned int cnt = count;
	u8 wwnn[WWN_SZ];
	int rc;

	/* count may include a LF at end of string */
	if (buf[cnt-1] == '\n')
		cnt--;

	if (!phba->soft_wwn_enable)
		return -EINVAL;

	rc = lpfc_wwn_set(buf, cnt, wwnn);
	if (rc) {
		/* Allow wwnn to be set many times, as long as the enable
		 * is set. However, once the wwpn is set, everything locks.
		 */
		return rc;
	}

	phba->cfg_soft_wwnn = wwn_to_u64(wwnn);

	dev_printk(KERN_NOTICE, &phba->pcidev->dev,
		   "lpfc%d: soft_wwnn set. Value will take effect upon "
		   "setting of the soft_wwpn\n", phba->brd_no);

	return count;
}
static DEVICE_ATTR_RW(lpfc_soft_wwnn);

/**
 * lpfc_oas_tgt_show - Return wwpn of target whose luns maybe enabled for
 *		      Optimized Access Storage (OAS) operations.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: buffer for passing information.
 *
 * Returns:
 * value of count
 **/
static ssize_t
lpfc_oas_tgt_show(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n",
			wwn_to_u64(phba->cfg_oas_tgt_wwpn));
}

/**
 * lpfc_oas_tgt_store - Store wwpn of target whose luns maybe enabled for
 *		      Optimized Access Storage (OAS) operations.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: buffer for passing information.
 * @count: Size of the data buffer.
 *
 * Returns:
 * -EINVAL count is invalid, invalid wwpn byte invalid
 * -EPERM oas is not supported by hba
 * value of count on success
 **/
static ssize_t
lpfc_oas_tgt_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	unsigned int cnt = count;
	uint8_t wwpn[WWN_SZ];
	int rc;

	if (!phba->cfg_fof)
		return -EPERM;

	/* count may include a LF at end of string */
	if (buf[cnt-1] == '\n')
		cnt--;

	rc = lpfc_wwn_set(buf, cnt, wwpn);
	if (rc)
		return rc;

	memcpy(phba->cfg_oas_tgt_wwpn, wwpn, (8 * sizeof(uint8_t)));
	memcpy(phba->sli4_hba.oas_next_tgt_wwpn, wwpn, (8 * sizeof(uint8_t)));
	if (wwn_to_u64(wwpn) == 0)
		phba->cfg_oas_flags |= OAS_FIND_ANY_TARGET;
	else
		phba->cfg_oas_flags &= ~OAS_FIND_ANY_TARGET;
	phba->cfg_oas_flags &= ~OAS_LUN_VALID;
	phba->sli4_hba.oas_next_lun = FIND_FIRST_OAS_LUN;
	return count;
}
static DEVICE_ATTR(lpfc_xlane_tgt, S_IRUGO | S_IWUSR,
		   lpfc_oas_tgt_show, lpfc_oas_tgt_store);

/**
 * lpfc_oas_priority_show - Return wwpn of target whose luns maybe enabled for
 *		      Optimized Access Storage (OAS) operations.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: buffer for passing information.
 *
 * Returns:
 * value of count
 **/
static ssize_t
lpfc_oas_priority_show(struct device *dev, struct device_attribute *attr,
		       char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	return scnprintf(buf, PAGE_SIZE, "%d\n", phba->cfg_oas_priority);
}

/**
 * lpfc_oas_priority_store - Store wwpn of target whose luns maybe enabled for
 *		      Optimized Access Storage (OAS) operations.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: buffer for passing information.
 * @count: Size of the data buffer.
 *
 * Returns:
 * -EINVAL count is invalid, invalid wwpn byte invalid
 * -EPERM oas is not supported by hba
 * value of count on success
 **/
static ssize_t
lpfc_oas_priority_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	unsigned int cnt = count;
	unsigned long val;
	int ret;

	if (!phba->cfg_fof)
		return -EPERM;

	/* count may include a LF at end of string */
	if (buf[cnt-1] == '\n')
		cnt--;

	ret = kstrtoul(buf, 0, &val);
	if (ret || (val > 0x7f))
		return -EINVAL;

	if (val)
		phba->cfg_oas_priority = (uint8_t)val;
	else
		phba->cfg_oas_priority = phba->cfg_XLanePriority;
	return count;
}
static DEVICE_ATTR(lpfc_xlane_priority, S_IRUGO | S_IWUSR,
		   lpfc_oas_priority_show, lpfc_oas_priority_store);

/**
 * lpfc_oas_vpt_show - Return wwpn of vport whose targets maybe enabled
 *		      for Optimized Access Storage (OAS) operations.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: buffer for passing information.
 *
 * Returns:
 * value of count on success
 **/
static ssize_t
lpfc_oas_vpt_show(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n",
			wwn_to_u64(phba->cfg_oas_vpt_wwpn));
}

/**
 * lpfc_oas_vpt_store - Store wwpn of vport whose targets maybe enabled
 *		      for Optimized Access Storage (OAS) operations.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: buffer for passing information.
 * @count: Size of the data buffer.
 *
 * Returns:
 * -EINVAL count is invalid, invalid wwpn byte invalid
 * -EPERM oas is not supported by hba
 * value of count on success
 **/
static ssize_t
lpfc_oas_vpt_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	unsigned int cnt = count;
	uint8_t wwpn[WWN_SZ];
	int rc;

	if (!phba->cfg_fof)
		return -EPERM;

	/* count may include a LF at end of string */
	if (buf[cnt-1] == '\n')
		cnt--;

	rc = lpfc_wwn_set(buf, cnt, wwpn);
	if (rc)
		return rc;

	memcpy(phba->cfg_oas_vpt_wwpn, wwpn, (8 * sizeof(uint8_t)));
	memcpy(phba->sli4_hba.oas_next_vpt_wwpn, wwpn, (8 * sizeof(uint8_t)));
	if (wwn_to_u64(wwpn) == 0)
		phba->cfg_oas_flags |= OAS_FIND_ANY_VPORT;
	else
		phba->cfg_oas_flags &= ~OAS_FIND_ANY_VPORT;
	phba->cfg_oas_flags &= ~OAS_LUN_VALID;
	if (phba->cfg_oas_priority == 0)
		phba->cfg_oas_priority = phba->cfg_XLanePriority;
	phba->sli4_hba.oas_next_lun = FIND_FIRST_OAS_LUN;
	return count;
}
static DEVICE_ATTR(lpfc_xlane_vpt, S_IRUGO | S_IWUSR,
		   lpfc_oas_vpt_show, lpfc_oas_vpt_store);

/**
 * lpfc_oas_lun_state_show - Return the current state (enabled or disabled)
 *			    of whether luns will be enabled or disabled
 *			    for Optimized Access Storage (OAS) operations.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: buffer for passing information.
 *
 * Returns:
 * size of formatted string.
 **/
static ssize_t
lpfc_oas_lun_state_show(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	return scnprintf(buf, PAGE_SIZE, "%d\n", phba->cfg_oas_lun_state);
}

/**
 * lpfc_oas_lun_state_store - Store the state (enabled or disabled)
 *			    of whether luns will be enabled or disabled
 *			    for Optimized Access Storage (OAS) operations.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: buffer for passing information.
 * @count: Size of the data buffer.
 *
 * Returns:
 * -EINVAL count is invalid, invalid wwpn byte invalid
 * -EPERM oas is not supported by hba
 * value of count on success
 **/
static ssize_t
lpfc_oas_lun_state_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	int val = 0;

	if (!phba->cfg_fof)
		return -EPERM;

	if (!isdigit(buf[0]))
		return -EINVAL;

	if (sscanf(buf, "%i", &val) != 1)
		return -EINVAL;

	if ((val != 0) && (val != 1))
		return -EINVAL;

	phba->cfg_oas_lun_state = val;
	return strlen(buf);
}
static DEVICE_ATTR(lpfc_xlane_lun_state, S_IRUGO | S_IWUSR,
		   lpfc_oas_lun_state_show, lpfc_oas_lun_state_store);

/**
 * lpfc_oas_lun_status_show - Return the status of the Optimized Access
 *                          Storage (OAS) lun returned by the
 *                          lpfc_oas_lun_show function.
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: buffer for passing information.
 *
 * Returns:
 * size of formatted string.
 **/
static ssize_t
lpfc_oas_lun_status_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	if (!(phba->cfg_oas_flags & OAS_LUN_VALID))
		return -EFAULT;

	return scnprintf(buf, PAGE_SIZE, "%d\n", phba->cfg_oas_lun_status);
}
static DEVICE_ATTR(lpfc_xlane_lun_status, S_IRUGO,
		   lpfc_oas_lun_status_show, NULL);


/**
 * lpfc_oas_lun_state_set - enable or disable a lun for Optimized Access Storage
 *			   (OAS) operations.
 * @phba: lpfc_hba pointer.
 * @ndlp: pointer to fcp target node.
 * @lun: the fc lun for setting oas state.
 * @oas_state: the oas state to be set to the lun.
 *
 * Returns:
 * SUCCESS : 0
 * -EPERM OAS is not enabled or not supported by this port.
 *
 */
static size_t
lpfc_oas_lun_state_set(struct lpfc_hba *phba, uint8_t vpt_wwpn[],
		       uint8_t tgt_wwpn[], uint64_t lun,
		       uint32_t oas_state, uint8_t pri)
{

	int rc = 0;

	if (!phba->cfg_fof)
		return -EPERM;

	if (oas_state) {
		if (!lpfc_enable_oas_lun(phba, (struct lpfc_name *)vpt_wwpn,
					 (struct lpfc_name *)tgt_wwpn,
					 lun, pri))
			rc = -ENOMEM;
	} else {
		lpfc_disable_oas_lun(phba, (struct lpfc_name *)vpt_wwpn,
				     (struct lpfc_name *)tgt_wwpn, lun, pri);
	}
	return rc;

}

/**
 * lpfc_oas_lun_get_next - get the next lun that has been enabled for Optimized
 *			  Access Storage (OAS) operations.
 * @phba: lpfc_hba pointer.
 * @vpt_wwpn: wwpn of the vport associated with the returned lun
 * @tgt_wwpn: wwpn of the target associated with the returned lun
 * @lun_status: status of the lun returned lun
 *
 * Returns the first or next lun enabled for OAS operations for the vport/target
 * specified.  If a lun is found, its vport wwpn, target wwpn and status is
 * returned.  If the lun is not found, NOT_OAS_ENABLED_LUN is returned.
 *
 * Return:
 * lun that is OAS enabled for the vport/target
 * NOT_OAS_ENABLED_LUN when no oas enabled lun found.
 */
static uint64_t
lpfc_oas_lun_get_next(struct lpfc_hba *phba, uint8_t vpt_wwpn[],
		      uint8_t tgt_wwpn[], uint32_t *lun_status,
		      uint32_t *lun_pri)
{
	uint64_t found_lun;

	if (unlikely(!phba) || !vpt_wwpn || !tgt_wwpn)
		return NOT_OAS_ENABLED_LUN;
	if (lpfc_find_next_oas_lun(phba, (struct lpfc_name *)
				   phba->sli4_hba.oas_next_vpt_wwpn,
				   (struct lpfc_name *)
				   phba->sli4_hba.oas_next_tgt_wwpn,
				   &phba->sli4_hba.oas_next_lun,
				   (struct lpfc_name *)vpt_wwpn,
				   (struct lpfc_name *)tgt_wwpn,
				   &found_lun, lun_status, lun_pri))
		return found_lun;
	else
		return NOT_OAS_ENABLED_LUN;
}

/**
 * lpfc_oas_lun_state_change - enable/disable a lun for OAS operations
 * @phba: lpfc_hba pointer.
 * @vpt_wwpn: vport wwpn by reference.
 * @tgt_wwpn: target wwpn by reference.
 * @lun: the fc lun for setting oas state.
 * @oas_state: the oas state to be set to the oas_lun.
 *
 * This routine enables (OAS_LUN_ENABLE) or disables (OAS_LUN_DISABLE)
 * a lun for OAS operations.
 *
 * Return:
 * SUCCESS: 0
 * -ENOMEM: failed to enable an lun for OAS operations
 * -EPERM: OAS is not enabled
 */
static ssize_t
lpfc_oas_lun_state_change(struct lpfc_hba *phba, uint8_t vpt_wwpn[],
			  uint8_t tgt_wwpn[], uint64_t lun,
			  uint32_t oas_state, uint8_t pri)
{

	int rc;

	rc = lpfc_oas_lun_state_set(phba, vpt_wwpn, tgt_wwpn, lun,
				    oas_state, pri);
	return rc;
}

/**
 * lpfc_oas_lun_show - Return oas enabled luns from a chosen target
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: buffer for passing information.
 *
 * This routine returns a lun enabled for OAS each time the function
 * is called.
 *
 * Returns:
 * SUCCESS: size of formatted string.
 * -EFAULT: target or vport wwpn was not set properly.
 * -EPERM: oas is not enabled.
 **/
static ssize_t
lpfc_oas_lun_show(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	uint64_t oas_lun;
	int len = 0;

	if (!phba->cfg_fof)
		return -EPERM;

	if (wwn_to_u64(phba->cfg_oas_vpt_wwpn) == 0)
		if (!(phba->cfg_oas_flags & OAS_FIND_ANY_VPORT))
			return -EFAULT;

	if (wwn_to_u64(phba->cfg_oas_tgt_wwpn) == 0)
		if (!(phba->cfg_oas_flags & OAS_FIND_ANY_TARGET))
			return -EFAULT;

	oas_lun = lpfc_oas_lun_get_next(phba, phba->cfg_oas_vpt_wwpn,
					phba->cfg_oas_tgt_wwpn,
					&phba->cfg_oas_lun_status,
					&phba->cfg_oas_priority);
	if (oas_lun != NOT_OAS_ENABLED_LUN)
		phba->cfg_oas_flags |= OAS_LUN_VALID;

	len += scnprintf(buf + len, PAGE_SIZE-len, "0x%llx", oas_lun);

	return len;
}

/**
 * lpfc_oas_lun_store - Sets the OAS state for lun
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: buffer for passing information.
 *
 * This function sets the OAS state for lun.  Before this function is called,
 * the vport wwpn, target wwpn, and oas state need to be set.
 *
 * Returns:
 * SUCCESS: size of formatted string.
 * -EFAULT: target or vport wwpn was not set properly.
 * -EPERM: oas is not enabled.
 * size of formatted string.
 **/
static ssize_t
lpfc_oas_lun_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_hba *phba = ((struct lpfc_vport *)shost->hostdata)->phba;
	uint64_t scsi_lun;
	uint32_t pri;
	ssize_t rc;

	if (!phba->cfg_fof)
		return -EPERM;

	if (wwn_to_u64(phba->cfg_oas_vpt_wwpn) == 0)
		return -EFAULT;

	if (wwn_to_u64(phba->cfg_oas_tgt_wwpn) == 0)
		return -EFAULT;

	if (!isdigit(buf[0]))
		return -EINVAL;

	if (sscanf(buf, "0x%llx", &scsi_lun) != 1)
		return -EINVAL;

	pri = phba->cfg_oas_priority;
	if (pri == 0)
		pri = phba->cfg_XLanePriority;

	lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
			"3372 Try to set vport 0x%llx target 0x%llx lun:0x%llx "
			"priority 0x%x with oas state %d\n",
			wwn_to_u64(phba->cfg_oas_vpt_wwpn),
			wwn_to_u64(phba->cfg_oas_tgt_wwpn), scsi_lun,
			pri, phba->cfg_oas_lun_state);

	rc = lpfc_oas_lun_state_change(phba, phba->cfg_oas_vpt_wwpn,
				       phba->cfg_oas_tgt_wwpn, scsi_lun,
				       phba->cfg_oas_lun_state, pri);
	if (rc)
		return rc;

	return count;
}
static DEVICE_ATTR(lpfc_xlane_lun, S_IRUGO | S_IWUSR,
		   lpfc_oas_lun_show, lpfc_oas_lun_store);

int lpfc_enable_nvmet_cnt;
unsigned long long lpfc_enable_nvmet[LPFC_NVMET_MAX_PORTS] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
module_param_array(lpfc_enable_nvmet, ullong, &lpfc_enable_nvmet_cnt, 0444);
MODULE_PARM_DESC(lpfc_enable_nvmet, "Enable HBA port(s) WWPN as a NVME Target");

static int lpfc_poll = 0;
module_param(lpfc_poll, int, S_IRUGO);
MODULE_PARM_DESC(lpfc_poll, "FCP ring polling mode control:"
		 " 0 - none,"
		 " 1 - poll with interrupts enabled"
		 " 3 - poll and disable FCP ring interrupts");

static DEVICE_ATTR_RW(lpfc_poll);

int lpfc_no_hba_reset_cnt;
unsigned long lpfc_no_hba_reset[MAX_HBAS_NO_RESET] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
module_param_array(lpfc_no_hba_reset, ulong, &lpfc_no_hba_reset_cnt, 0444);
MODULE_PARM_DESC(lpfc_no_hba_reset, "WWPN of HBAs that should not be reset");

LPFC_ATTR(sli_mode, 0, 0, 3,
	"SLI mode selector:"
	" 0 - auto (SLI-3 if supported),"
	" 2 - select SLI-2 even on SLI-3 capable HBAs,"
	" 3 - select SLI-3");

LPFC_ATTR_R(enable_npiv, 1, 0, 1,
	"Enable NPIV functionality");

LPFC_ATTR_R(fcf_failover_policy, 1, 1, 2,
	"FCF Fast failover=1 Priority failover=2");

/*
# lpfc_enable_rrq: Track XRI/OXID reuse after IO failures
#	0x0 = disabled, XRI/OXID use not tracked.
#	0x1 = XRI/OXID reuse is timed with ratov, RRQ sent.
#	0x2 = XRI/OXID reuse is timed with ratov, No RRQ sent.
*/
LPFC_ATTR_R(enable_rrq, 2, 0, 2,
	"Enable RRQ functionality");

/*
# lpfc_suppress_link_up:  Bring link up at initialization
#            0x0  = bring link up (issue MBX_INIT_LINK)
#            0x1  = do NOT bring link up at initialization(MBX_INIT_LINK)
#            0x2  = never bring up link
# Default value is 0.
*/
LPFC_ATTR_R(suppress_link_up, LPFC_INITIALIZE_LINK, LPFC_INITIALIZE_LINK,
		LPFC_DELAY_INIT_LINK_INDEFINITELY,
		"Suppress Link Up at initialization");

static ssize_t
lpfc_pls_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_hba   *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 phba->sli4_hba.pc_sli4_params.pls);
}
static DEVICE_ATTR(pls, 0444,
			 lpfc_pls_show, NULL);

static ssize_t
lpfc_pt_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_hba   *phba = ((struct lpfc_vport *)shost->hostdata)->phba;

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 (phba->hba_flag & HBA_PERSISTENT_TOPO) ? 1 : 0);
}
static DEVICE_ATTR(pt, 0444,
			 lpfc_pt_show, NULL);

/*
# lpfc_cnt: Number of IOCBs allocated for ELS, CT, and ABTS
#       1 - (1024)
#       2 - (2048)
#       3 - (3072)
#       4 - (4096)
#       5 - (5120)
*/
static ssize_t
lpfc_iocb_hw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_hba   *phba = ((struct lpfc_vport *) shost->hostdata)->phba;

	return scnprintf(buf, PAGE_SIZE, "%d\n", phba->iocb_max);
}

static DEVICE_ATTR(iocb_hw, S_IRUGO,
			 lpfc_iocb_hw_show, NULL);
static ssize_t
lpfc_txq_hw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_hba   *phba = ((struct lpfc_vport *) shost->hostdata)->phba;
	struct lpfc_sli_ring *pring = lpfc_phba_elsring(phba);

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			pring ? pring->txq_max : 0);
}

static DEVICE_ATTR(txq_hw, S_IRUGO,
			 lpfc_txq_hw_show, NULL);
static ssize_t
lpfc_txcmplq_hw_show(struct device *dev, struct device_attribute *attr,
 char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_hba   *phba = ((struct lpfc_vport *) shost->hostdata)->phba;
	struct lpfc_sli_ring *pring = lpfc_phba_elsring(phba);

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			pring ? pring->txcmplq_max : 0);
}

static DEVICE_ATTR(txcmplq_hw, S_IRUGO,
			 lpfc_txcmplq_hw_show, NULL);

/*
# lpfc_nodev_tmo: If set, it will hold all I/O errors on devices that disappear
# until the timer expires. Value range is [0,255]. Default value is 30.
*/
static int lpfc_nodev_tmo = LPFC_DEF_DEVLOSS_TMO;
static int lpfc_devloss_tmo = LPFC_DEF_DEVLOSS_TMO;
module_param(lpfc_nodev_tmo, int, 0);
MODULE_PARM_DESC(lpfc_nodev_tmo,
		 "Seconds driver will hold I/O waiting "
		 "for a device to come back");

/**
 * lpfc_nodev_tmo_show - Return the hba dev loss timeout value
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains the dev loss timeout in decimal.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_nodev_tmo_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;

	return scnprintf(buf, PAGE_SIZE, "%d\n",	vport->cfg_devloss_tmo);
}

/**
 * lpfc_nodev_tmo_init - Set the hba nodev timeout value
 * @vport: lpfc vport structure pointer.
 * @val: contains the nodev timeout value.
 *
 * Description:
 * If the devloss tmo is already set then nodev tmo is set to devloss tmo,
 * a kernel error message is printed and zero is returned.
 * Else if val is in range then nodev tmo and devloss tmo are set to val.
 * Otherwise nodev tmo is set to the default value.
 *
 * Returns:
 * zero if already set or if val is in range
 * -EINVAL val out of range
 **/
static int
lpfc_nodev_tmo_init(struct lpfc_vport *vport, int val)
{
	if (vport->cfg_devloss_tmo != LPFC_DEF_DEVLOSS_TMO) {
		vport->cfg_nodev_tmo = vport->cfg_devloss_tmo;
		if (val != LPFC_DEF_DEVLOSS_TMO)
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
					 "0407 Ignoring lpfc_nodev_tmo module "
					 "parameter because lpfc_devloss_tmo "
					 "is set.\n");
		return 0;
	}

	if (val >= LPFC_MIN_DEVLOSS_TMO && val <= LPFC_MAX_DEVLOSS_TMO) {
		vport->cfg_nodev_tmo = val;
		vport->cfg_devloss_tmo = val;
		return 0;
	}
	lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
			 "0400 lpfc_nodev_tmo attribute cannot be set to"
			 " %d, allowed range is [%d, %d]\n",
			 val, LPFC_MIN_DEVLOSS_TMO, LPFC_MAX_DEVLOSS_TMO);
	vport->cfg_nodev_tmo = LPFC_DEF_DEVLOSS_TMO;
	return -EINVAL;
}

/**
 * lpfc_update_rport_devloss_tmo - Update dev loss tmo value
 * @vport: lpfc vport structure pointer.
 *
 * Description:
 * Update all the ndlp's dev loss tmo with the vport devloss tmo value.
 **/
static void
lpfc_update_rport_devloss_tmo(struct lpfc_vport *vport)
{
	struct Scsi_Host  *shost;
	struct lpfc_nodelist  *ndlp;
#if (IS_ENABLED(CONFIG_NVME_FC))
	struct lpfc_nvme_rport *rport;
	struct nvme_fc_remote_port *remoteport = NULL;
#endif

	shost = lpfc_shost_from_vport(vport);
	spin_lock_irq(shost->host_lock);
	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
		if (!NLP_CHK_NODE_ACT(ndlp))
			continue;
		if (ndlp->rport)
			ndlp->rport->dev_loss_tmo = vport->cfg_devloss_tmo;
#if (IS_ENABLED(CONFIG_NVME_FC))
		spin_lock(&vport->phba->hbalock);
		rport = lpfc_ndlp_get_nrport(ndlp);
		if (rport)
			remoteport = rport->remoteport;
		spin_unlock(&vport->phba->hbalock);
		if (rport && remoteport)
			nvme_fc_set_remoteport_devloss(remoteport,
						       vport->cfg_devloss_tmo);
#endif
	}
	spin_unlock_irq(shost->host_lock);
}

/**
 * lpfc_nodev_tmo_set - Set the vport nodev tmo and devloss tmo values
 * @vport: lpfc vport structure pointer.
 * @val: contains the tmo value.
 *
 * Description:
 * If the devloss tmo is already set or the vport dev loss tmo has changed
 * then a kernel error message is printed and zero is returned.
 * Else if val is in range then nodev tmo and devloss tmo are set to val.
 * Otherwise nodev tmo is set to the default value.
 *
 * Returns:
 * zero if already set or if val is in range
 * -EINVAL val out of range
 **/
static int
lpfc_nodev_tmo_set(struct lpfc_vport *vport, int val)
{
	if (vport->dev_loss_tmo_changed ||
	    (lpfc_devloss_tmo != LPFC_DEF_DEVLOSS_TMO)) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				 "0401 Ignoring change to lpfc_nodev_tmo "
				 "because lpfc_devloss_tmo is set.\n");
		return 0;
	}
	if (val >= LPFC_MIN_DEVLOSS_TMO && val <= LPFC_MAX_DEVLOSS_TMO) {
		vport->cfg_nodev_tmo = val;
		vport->cfg_devloss_tmo = val;
		/*
		 * For compat: set the fc_host dev loss so new rports
		 * will get the value.
		 */
		fc_host_dev_loss_tmo(lpfc_shost_from_vport(vport)) = val;
		lpfc_update_rport_devloss_tmo(vport);
		return 0;
	}
	lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
			 "0403 lpfc_nodev_tmo attribute cannot be set to "
			 "%d, allowed range is [%d, %d]\n",
			 val, LPFC_MIN_DEVLOSS_TMO, LPFC_MAX_DEVLOSS_TMO);
	return -EINVAL;
}

lpfc_vport_param_store(nodev_tmo)

static DEVICE_ATTR_RW(lpfc_nodev_tmo);

/*
# lpfc_devloss_tmo: If set, it will hold all I/O errors on devices that
# disappear until the timer expires. Value range is [0,255]. Default
# value is 30.
*/
module_param(lpfc_devloss_tmo, int, S_IRUGO);
MODULE_PARM_DESC(lpfc_devloss_tmo,
		 "Seconds driver will hold I/O waiting "
		 "for a device to come back");
lpfc_vport_param_init(devloss_tmo, LPFC_DEF_DEVLOSS_TMO,
		      LPFC_MIN_DEVLOSS_TMO, LPFC_MAX_DEVLOSS_TMO)
lpfc_vport_param_show(devloss_tmo)

/**
 * lpfc_devloss_tmo_set - Sets vport nodev tmo, devloss tmo values, changed bit
 * @vport: lpfc vport structure pointer.
 * @val: contains the tmo value.
 *
 * Description:
 * If val is in a valid range then set the vport nodev tmo,
 * devloss tmo, also set the vport dev loss tmo changed flag.
 * Else a kernel error message is printed.
 *
 * Returns:
 * zero if val is in range
 * -EINVAL val out of range
 **/
static int
lpfc_devloss_tmo_set(struct lpfc_vport *vport, int val)
{
	if (val >= LPFC_MIN_DEVLOSS_TMO && val <= LPFC_MAX_DEVLOSS_TMO) {
		vport->cfg_nodev_tmo = val;
		vport->cfg_devloss_tmo = val;
		vport->dev_loss_tmo_changed = 1;
		fc_host_dev_loss_tmo(lpfc_shost_from_vport(vport)) = val;
		lpfc_update_rport_devloss_tmo(vport);
		return 0;
	}

	lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
			 "0404 lpfc_devloss_tmo attribute cannot be set to "
			 "%d, allowed range is [%d, %d]\n",
			 val, LPFC_MIN_DEVLOSS_TMO, LPFC_MAX_DEVLOSS_TMO);
	return -EINVAL;
}

lpfc_vport_param_store(devloss_tmo)
static DEVICE_ATTR_RW(lpfc_devloss_tmo);

/*
 * lpfc_suppress_rsp: Enable suppress rsp feature is firmware supports it
 * lpfc_suppress_rsp = 0  Disable
 * lpfc_suppress_rsp = 1  Enable (default)
 *
 */
LPFC_ATTR_R(suppress_rsp, 1, 0, 1,
	    "Enable suppress rsp feature is firmware supports it");

/*
 * lpfc_nvmet_mrq: Specify number of RQ pairs for processing NVMET cmds
 * lpfc_nvmet_mrq = 0  driver will calcualte optimal number of RQ pairs
 * lpfc_nvmet_mrq = 1  use a single RQ pair
 * lpfc_nvmet_mrq >= 2  use specified RQ pairs for MRQ
 *
 */
LPFC_ATTR_R(nvmet_mrq,
	    LPFC_NVMET_MRQ_AUTO, LPFC_NVMET_MRQ_AUTO, LPFC_NVMET_MRQ_MAX,
	    "Specify number of RQ pairs for processing NVMET cmds");

/*
 * lpfc_nvmet_mrq_post: Specify number of RQ buffer to initially post
 * to each NVMET RQ. Range 64 to 2048, default is 512.
 */
LPFC_ATTR_R(nvmet_mrq_post,
	    LPFC_NVMET_RQE_DEF_POST, LPFC_NVMET_RQE_MIN_POST,
	    LPFC_NVMET_RQE_DEF_COUNT,
	    "Specify number of RQ buffers to initially post");

/*
 * lpfc_enable_fc4_type: Defines what FC4 types are supported.
 * Supported Values:  1 - register just FCP
 *                    3 - register both FCP and NVME
 * Supported values are [1,3]. Default value is 3
 */
LPFC_ATTR_R(enable_fc4_type, LPFC_ENABLE_BOTH,
	    LPFC_ENABLE_FCP, LPFC_ENABLE_BOTH,
	    "Enable FC4 Protocol support - FCP / NVME");

/*
# lpfc_log_verbose: Only turn this flag on if you are willing to risk being
# deluged with LOTS of information.
# You can set a bit mask to record specific types of verbose messages:
# See lpfc_logmsh.h for definitions.
*/
LPFC_VPORT_ATTR_HEX_RW(log_verbose, 0x0, 0x0, 0xffffffff,
		       "Verbose logging bit-mask");

/*
# lpfc_enable_da_id: This turns on the DA_ID CT command that deregisters
# objects that have been registered with the nameserver after login.
*/
LPFC_VPORT_ATTR_R(enable_da_id, 1, 0, 1,
		  "Deregister nameserver objects before LOGO");

/*
# lun_queue_depth:  This parameter is used to limit the number of outstanding
# commands per FCP LUN. Value range is [1,512]. Default value is 30.
# If this parameter value is greater than 1/8th the maximum number of exchanges
# supported by the HBA port, then the lun queue depth will be reduced to
# 1/8th the maximum number of exchanges.
*/
LPFC_VPORT_ATTR_R(lun_queue_depth, 30, 1, 512,
		  "Max number of FCP commands we can queue to a specific LUN");

/*
# tgt_queue_depth:  This parameter is used to limit the number of outstanding
# commands per target port. Value range is [10,65535]. Default value is 65535.
*/
static uint lpfc_tgt_queue_depth = LPFC_MAX_TGT_QDEPTH;
module_param(lpfc_tgt_queue_depth, uint, 0444);
MODULE_PARM_DESC(lpfc_tgt_queue_depth, "Set max Target queue depth");
lpfc_vport_param_show(tgt_queue_depth);
lpfc_vport_param_init(tgt_queue_depth, LPFC_MAX_TGT_QDEPTH,
		      LPFC_MIN_TGT_QDEPTH, LPFC_MAX_TGT_QDEPTH);

/**
 * lpfc_tgt_queue_depth_store: Sets an attribute value.
 * @phba: pointer the the adapter structure.
 * @val: integer attribute value.
 *
 * Description: Sets the parameter to the new value.
 *
 * Returns:
 * zero on success
 * -EINVAL if val is invalid
 */
static int
lpfc_tgt_queue_depth_set(struct lpfc_vport *vport, uint val)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp;

	if (!lpfc_rangecheck(val, LPFC_MIN_TGT_QDEPTH, LPFC_MAX_TGT_QDEPTH))
		return -EINVAL;

	if (val == vport->cfg_tgt_queue_depth)
		return 0;

	spin_lock_irq(shost->host_lock);
	vport->cfg_tgt_queue_depth = val;

	/* Next loop thru nodelist and change cmd_qdepth */
	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp)
		ndlp->cmd_qdepth = vport->cfg_tgt_queue_depth;

	spin_unlock_irq(shost->host_lock);
	return 0;
}

lpfc_vport_param_store(tgt_queue_depth);
static DEVICE_ATTR_RW(lpfc_tgt_queue_depth);

/*
# hba_queue_depth:  This parameter is used to limit the number of outstanding
# commands per lpfc HBA. Value range is [32,8192]. If this parameter
# value is greater than the maximum number of exchanges supported by the HBA,
# then maximum number of exchanges supported by the HBA is used to determine
# the hba_queue_depth.
*/
LPFC_ATTR_R(hba_queue_depth, 8192, 32, 8192,
	    "Max number of FCP commands we can queue to a lpfc HBA");

/*
# peer_port_login:  This parameter allows/prevents logins
# between peer ports hosted on the same physical port.
# When this parameter is set 0 peer ports of same physical port
# are not allowed to login to each other.
# When this parameter is set 1 peer ports of same physical port
# are allowed to login to each other.
# Default value of this parameter is 0.
*/
LPFC_VPORT_ATTR_R(peer_port_login, 0, 0, 1,
		  "Allow peer ports on the same physical port to login to each "
		  "other.");

/*
# restrict_login:  This parameter allows/prevents logins
# between Virtual Ports and remote initiators.
# When this parameter is not set (0) Virtual Ports will accept PLOGIs from
# other initiators and will attempt to PLOGI all remote ports.
# When this parameter is set (1) Virtual Ports will reject PLOGIs from
# remote ports and will not attempt to PLOGI to other initiators.
# This parameter does not restrict to the physical port.
# This parameter does not restrict logins to Fabric resident remote ports.
# Default value of this parameter is 1.
*/
static int lpfc_restrict_login = 1;
module_param(lpfc_restrict_login, int, S_IRUGO);
MODULE_PARM_DESC(lpfc_restrict_login,
		 "Restrict virtual ports login to remote initiators.");
lpfc_vport_param_show(restrict_login);

/**
 * lpfc_restrict_login_init - Set the vport restrict login flag
 * @vport: lpfc vport structure pointer.
 * @val: contains the restrict login value.
 *
 * Description:
 * If val is not in a valid range then log a kernel error message and set
 * the vport restrict login to one.
 * If the port type is physical clear the restrict login flag and return.
 * Else set the restrict login flag to val.
 *
 * Returns:
 * zero if val is in range
 * -EINVAL val out of range
 **/
static int
lpfc_restrict_login_init(struct lpfc_vport *vport, int val)
{
	if (val < 0 || val > 1) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				 "0422 lpfc_restrict_login attribute cannot "
				 "be set to %d, allowed range is [0, 1]\n",
				 val);
		vport->cfg_restrict_login = 1;
		return -EINVAL;
	}
	if (vport->port_type == LPFC_PHYSICAL_PORT) {
		vport->cfg_restrict_login = 0;
		return 0;
	}
	vport->cfg_restrict_login = val;
	return 0;
}

/**
 * lpfc_restrict_login_set - Set the vport restrict login flag
 * @vport: lpfc vport structure pointer.
 * @val: contains the restrict login value.
 *
 * Description:
 * If val is not in a valid range then log a kernel error message and set
 * the vport restrict login to one.
 * If the port type is physical and the val is not zero log a kernel
 * error message, clear the restrict login flag and return zero.
 * Else set the restrict login flag to val.
 *
 * Returns:
 * zero if val is in range
 * -EINVAL val out of range
 **/
static int
lpfc_restrict_login_set(struct lpfc_vport *vport, int val)
{
	if (val < 0 || val > 1) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				 "0425 lpfc_restrict_login attribute cannot "
				 "be set to %d, allowed range is [0, 1]\n",
				 val);
		vport->cfg_restrict_login = 1;
		return -EINVAL;
	}
	if (vport->port_type == LPFC_PHYSICAL_PORT && val != 0) {
		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				 "0468 lpfc_restrict_login must be 0 for "
				 "Physical ports.\n");
		vport->cfg_restrict_login = 0;
		return 0;
	}
	vport->cfg_restrict_login = val;
	return 0;
}
lpfc_vport_param_store(restrict_login);
static DEVICE_ATTR_RW(lpfc_restrict_login);

/*
# Some disk devices have a "select ID" or "select Target" capability.
# From a protocol standpoint "select ID" usually means select the
# Fibre channel "ALPA".  In the FC-AL Profile there is an "informative
# annex" which contains a table that maps a "select ID" (a number
# between 0 and 7F) to an ALPA.  By default, for compatibility with
# older drivers, the lpfc driver scans this table from low ALPA to high
# ALPA.
#
# Turning on the scan-down variable (on  = 1, off = 0) will
# cause the lpfc driver to use an inverted table, effectively
# scanning ALPAs from high to low. Value range is [0,1]. Default value is 1.
#
# (Note: This "select ID" functionality is a LOOP ONLY characteristic
# and will not work across a fabric. Also this parameter will take
# effect only in the case when ALPA map is not available.)
*/
LPFC_VPORT_ATTR_R(scan_down, 1, 0, 1,
		  "Start scanning for devices from highest ALPA to lowest");

/*
# lpfc_topology:  link topology for init link
#            0x0  = attempt loop mode then point-to-point
#            0x01 = internal loopback mode
#            0x02 = attempt point-to-point mode only
#            0x04 = attempt loop mode only
#            0x06 = attempt point-to-point mode then loop
# Set point-to-point mode if you want to run as an N_Port.
# Set loop mode if you want to run as an NL_Port. Value range is [0,0x6].
# Default value is 0.
*/
LPFC_ATTR(topology, 0, 0, 6,
	"Select Fibre Channel topology");

/**
 * lpfc_topology_set - Set the adapters topology field
 * @phba: lpfc_hba pointer.
 * @val: topology value.
 *
 * Description:
 * If val is in a valid range then set the adapter's topology field and
 * issue a lip; if the lip fails reset the topology to the old value.
 *
 * If the value is not in range log a kernel error message and return an error.
 *
 * Returns:
 * zero if val is in range and lip okay
 * non-zero return value from lpfc_issue_lip()
 * -EINVAL val out of range
 **/
static ssize_t
lpfc_topology_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int val = 0;
	int nolip = 0;
	const char *val_buf = buf;
	int err;
	uint32_t prev_val;

	if (!strncmp(buf, "nolip ", strlen("nolip "))) {
		nolip = 1;
		val_buf = &buf[strlen("nolip ")];
	}

	if (!isdigit(val_buf[0]))
		return -EINVAL;
	if (sscanf(val_buf, "%i", &val) != 1)
		return -EINVAL;

	if (val >= 0 && val <= 6) {
		prev_val = phba->cfg_topology;
		if (phba->cfg_link_speed == LPFC_USER_LINK_SPEED_16G &&
			val == 4) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				"3113 Loop mode not supported at speed %d\n",
				val);
			return -EINVAL;
		}
		/*
		 * The 'topology' is not a configurable parameter if :
		 *   - persistent topology enabled
		 *   - G7/G6 with no private loop support
		 */

		if ((phba->hba_flag & HBA_PERSISTENT_TOPO ||
		     (!phba->sli4_hba.pc_sli4_params.pls &&
		     (phba->pcidev->device == PCI_DEVICE_ID_LANCER_G6_FC ||
		     phba->pcidev->device == PCI_DEVICE_ID_LANCER_G7_FC))) &&
		    val == 4) {
			lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
				"3114 Loop mode not supported\n");
			return -EINVAL;
		}
		phba->cfg_topology = val;
		if (nolip)
			return strlen(buf);

		lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
			"3054 lpfc_topology changed from %d to %d\n",
			prev_val, val);
		if (prev_val != val && phba->sli_rev == LPFC_SLI_REV4)
			phba->fc_topology_changed = 1;
		err = lpfc_issue_lip(lpfc_shost_from_vport(phba->pport));
		if (err) {
			phba->cfg_topology = prev_val;
			return -EINVAL;
		} else
			return strlen(buf);
	}
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
		"%d:0467 lpfc_topology attribute cannot be set to %d, "
		"allowed range is [0, 6]\n",
		phba->brd_no, val);
	return -EINVAL;
}

lpfc_param_show(topology)
static DEVICE_ATTR_RW(lpfc_topology);

/**
 * lpfc_static_vport_show: Read callback function for
 *   lpfc_static_vport sysfs file.
 * @dev: Pointer to class device object.
 * @attr: device attribute structure.
 * @buf: Data buffer.
 *
 * This function is the read call back function for
 * lpfc_static_vport sysfs file. The lpfc_static_vport
 * sysfs file report the mageability of the vport.
 **/
static ssize_t
lpfc_static_vport_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	if (vport->vport_flag & STATIC_VPORT)
		sprintf(buf, "1\n");
	else
		sprintf(buf, "0\n");

	return strlen(buf);
}

/*
 * Sysfs attribute to control the statistical data collection.
 */
static DEVICE_ATTR_RO(lpfc_static_vport);

/**
 * lpfc_stat_data_ctrl_store - write call back for lpfc_stat_data_ctrl sysfs file
 * @dev: Pointer to class device.
 * @buf: Data buffer.
 * @count: Size of the data buffer.
 *
 * This function get called when a user write to the lpfc_stat_data_ctrl
 * sysfs file. This function parse the command written to the sysfs file
 * and take appropriate action. These commands are used for controlling
 * driver statistical data collection.
 * Following are the command this function handles.
 *
 *    setbucket <bucket_type> <base> <step>
 *			       = Set the latency buckets.
 *    destroybucket            = destroy all the buckets.
 *    start                    = start data collection
 *    stop                     = stop data collection
 *    reset                    = reset the collected data
 **/
static ssize_t
lpfc_stat_data_ctrl_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
#define LPFC_MAX_DATA_CTRL_LEN 1024
	static char bucket_data[LPFC_MAX_DATA_CTRL_LEN];
	unsigned long i;
	char *str_ptr, *token;
	struct lpfc_vport **vports;
	struct Scsi_Host *v_shost;
	char *bucket_type_str, *base_str, *step_str;
	unsigned long base, step, bucket_type;

	if (!strncmp(buf, "setbucket", strlen("setbucket"))) {
		if (strlen(buf) > (LPFC_MAX_DATA_CTRL_LEN - 1))
			return -EINVAL;

		strncpy(bucket_data, buf, LPFC_MAX_DATA_CTRL_LEN);
		str_ptr = &bucket_data[0];
		/* Ignore this token - this is command token */
		token = strsep(&str_ptr, "\t ");
		if (!token)
			return -EINVAL;

		bucket_type_str = strsep(&str_ptr, "\t ");
		if (!bucket_type_str)
			return -EINVAL;

		if (!strncmp(bucket_type_str, "linear", strlen("linear")))
			bucket_type = LPFC_LINEAR_BUCKET;
		else if (!strncmp(bucket_type_str, "power2", strlen("power2")))
			bucket_type = LPFC_POWER2_BUCKET;
		else
			return -EINVAL;

		base_str = strsep(&str_ptr, "\t ");
		if (!base_str)
			return -EINVAL;
		base = simple_strtoul(base_str, NULL, 0);

		step_str = strsep(&str_ptr, "\t ");
		if (!step_str)
			return -EINVAL;
		step = simple_strtoul(step_str, NULL, 0);
		if (!step)
			return -EINVAL;

		/* Block the data collection for every vport */
		vports = lpfc_create_vport_work_array(phba);
		if (vports == NULL)
			return -ENOMEM;

		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			v_shost = lpfc_shost_from_vport(vports[i]);
			spin_lock_irq(v_shost->host_lock);
			/* Block and reset data collection */
			vports[i]->stat_data_blocked = 1;
			if (vports[i]->stat_data_enabled)
				lpfc_vport_reset_stat_data(vports[i]);
			spin_unlock_irq(v_shost->host_lock);
		}

		/* Set the bucket attributes */
		phba->bucket_type = bucket_type;
		phba->bucket_base = base;
		phba->bucket_step = step;

		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			v_shost = lpfc_shost_from_vport(vports[i]);

			/* Unblock data collection */
			spin_lock_irq(v_shost->host_lock);
			vports[i]->stat_data_blocked = 0;
			spin_unlock_irq(v_shost->host_lock);
		}
		lpfc_destroy_vport_work_array(phba, vports);
		return strlen(buf);
	}

	if (!strncmp(buf, "destroybucket", strlen("destroybucket"))) {
		vports = lpfc_create_vport_work_array(phba);
		if (vports == NULL)
			return -ENOMEM;

		for (i = 0; i <= phba->max_vports && vports[i] != NULL; i++) {
			v_shost = lpfc_shost_from_vport(vports[i]);
			spin_lock_irq(shost->host_lock);
			vports[i]->stat_data_blocked = 1;
			lpfc_free_bucket(vport);
			vport->stat_data_enabled = 0;
			vports[i]->stat_data_blocked = 0;
			spin_unlock_irq(shost->host_lock);
		}
		lpfc_destroy_vport_work_array(phba, vports);
		phba->bucket_type = LPFC_NO_BUCKET;
		phba->bucket_base = 0;
		phba->bucket_step = 0;
		return strlen(buf);
	}

	if (!strncmp(buf, "start", strlen("start"))) {
		/* If no buckets configured return error */
		if (phba->bucket_type == LPFC_NO_BUCKET)
			return -EINVAL;
		spin_lock_irq(shost->host_lock);
		if (vport->stat_data_enabled) {
			spin_unlock_irq(shost->host_lock);
			return strlen(buf);
		}
		lpfc_alloc_bucket(vport);
		vport->stat_data_enabled = 1;
		spin_unlock_irq(shost->host_lock);
		return strlen(buf);
	}

	if (!strncmp(buf, "stop", strlen("stop"))) {
		spin_lock_irq(shost->host_lock);
		if (vport->stat_data_enabled == 0) {
			spin_unlock_irq(shost->host_lock);
			return strlen(buf);
		}
		lpfc_free_bucket(vport);
		vport->stat_data_enabled = 0;
		spin_unlock_irq(shost->host_lock);
		return strlen(buf);
	}

	if (!strncmp(buf, "reset", strlen("reset"))) {
		if ((phba->bucket_type == LPFC_NO_BUCKET)
			|| !vport->stat_data_enabled)
			return strlen(buf);
		spin_lock_irq(shost->host_lock);
		vport->stat_data_blocked = 1;
		lpfc_vport_reset_stat_data(vport);
		vport->stat_data_blocked = 0;
		spin_unlock_irq(shost->host_lock);
		return strlen(buf);
	}
	return -EINVAL;
}


/**
 * lpfc_stat_data_ctrl_show - Read function for lpfc_stat_data_ctrl sysfs file
 * @dev: Pointer to class device object.
 * @buf: Data buffer.
 *
 * This function is the read call back function for
 * lpfc_stat_data_ctrl sysfs file. This function report the
 * current statistical data collection state.
 **/
static ssize_t
lpfc_stat_data_ctrl_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int index = 0;
	int i;
	char *bucket_type;
	unsigned long bucket_value;

	switch (phba->bucket_type) {
	case LPFC_LINEAR_BUCKET:
		bucket_type = "linear";
		break;
	case LPFC_POWER2_BUCKET:
		bucket_type = "power2";
		break;
	default:
		bucket_type = "No Bucket";
		break;
	}

	sprintf(&buf[index], "Statistical Data enabled :%d, "
		"blocked :%d, Bucket type :%s, Bucket base :%d,"
		" Bucket step :%d\nLatency Ranges :",
		vport->stat_data_enabled, vport->stat_data_blocked,
		bucket_type, phba->bucket_base, phba->bucket_step);
	index = strlen(buf);
	if (phba->bucket_type != LPFC_NO_BUCKET) {
		for (i = 0; i < LPFC_MAX_BUCKET_COUNT; i++) {
			if (phba->bucket_type == LPFC_LINEAR_BUCKET)
				bucket_value = phba->bucket_base +
					phba->bucket_step * i;
			else
				bucket_value = phba->bucket_base +
				(1 << i) * phba->bucket_step;

			if (index + 10 > PAGE_SIZE)
				break;
			sprintf(&buf[index], "%08ld ", bucket_value);
			index = strlen(buf);
		}
	}
	sprintf(&buf[index], "\n");
	return strlen(buf);
}

/*
 * Sysfs attribute to control the statistical data collection.
 */
static DEVICE_ATTR_RW(lpfc_stat_data_ctrl);

/*
 * lpfc_drvr_stat_data: sysfs attr to get driver statistical data.
 */

/*
 * Each Bucket takes 11 characters and 1 new line + 17 bytes WWN
 * for each target.
 */
#define STAT_DATA_SIZE_PER_TARGET(NUM_BUCKETS) ((NUM_BUCKETS) * 11 + 18)
#define MAX_STAT_DATA_SIZE_PER_TARGET \
	STAT_DATA_SIZE_PER_TARGET(LPFC_MAX_BUCKET_COUNT)


/**
 * sysfs_drvr_stat_data_read - Read function for lpfc_drvr_stat_data attribute
 * @filp: sysfs file
 * @kobj: Pointer to the kernel object
 * @bin_attr: Attribute object
 * @buff: Buffer pointer
 * @off: File offset
 * @count: Buffer size
 *
 * This function is the read call back function for lpfc_drvr_stat_data
 * sysfs file. This function export the statistical data to user
 * applications.
 **/
static ssize_t
sysfs_drvr_stat_data_read(struct file *filp, struct kobject *kobj,
		struct bin_attribute *bin_attr,
		char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device,
		kobj);
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int i = 0, index = 0;
	unsigned long nport_index;
	struct lpfc_nodelist *ndlp = NULL;
	nport_index = (unsigned long)off /
		MAX_STAT_DATA_SIZE_PER_TARGET;

	if (!vport->stat_data_enabled || vport->stat_data_blocked
		|| (phba->bucket_type == LPFC_NO_BUCKET))
		return 0;

	spin_lock_irq(shost->host_lock);
	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
		if (!NLP_CHK_NODE_ACT(ndlp) || !ndlp->lat_data)
			continue;

		if (nport_index > 0) {
			nport_index--;
			continue;
		}

		if ((index + MAX_STAT_DATA_SIZE_PER_TARGET)
			> count)
			break;

		if (!ndlp->lat_data)
			continue;

		/* Print the WWN */
		sprintf(&buf[index], "%02x%02x%02x%02x%02x%02x%02x%02x:",
			ndlp->nlp_portname.u.wwn[0],
			ndlp->nlp_portname.u.wwn[1],
			ndlp->nlp_portname.u.wwn[2],
			ndlp->nlp_portname.u.wwn[3],
			ndlp->nlp_portname.u.wwn[4],
			ndlp->nlp_portname.u.wwn[5],
			ndlp->nlp_portname.u.wwn[6],
			ndlp->nlp_portname.u.wwn[7]);

		index = strlen(buf);

		for (i = 0; i < LPFC_MAX_BUCKET_COUNT; i++) {
			sprintf(&buf[index], "%010u,",
				ndlp->lat_data[i].cmd_count);
			index = strlen(buf);
		}
		sprintf(&buf[index], "\n");
		index = strlen(buf);
	}
	spin_unlock_irq(shost->host_lock);
	return index;
}

static struct bin_attribute sysfs_drvr_stat_data_attr = {
	.attr = {
		.name = "lpfc_drvr_stat_data",
		.mode = S_IRUSR,
	},
	.size = LPFC_MAX_TARGET * MAX_STAT_DATA_SIZE_PER_TARGET,
	.read = sysfs_drvr_stat_data_read,
	.write = NULL,
};

/*
# lpfc_link_speed: Link speed selection for initializing the Fibre Channel
# connection.
# Value range is [0,16]. Default value is 0.
*/
/**
 * lpfc_link_speed_set - Set the adapters link speed
 * @phba: lpfc_hba pointer.
 * @val: link speed value.
 *
 * Description:
 * If val is in a valid range then set the adapter's link speed field and
 * issue a lip; if the lip fails reset the link speed to the old value.
 *
 * Notes:
 * If the value is not in range log a kernel error message and return an error.
 *
 * Returns:
 * zero if val is in range and lip okay.
 * non-zero return value from lpfc_issue_lip()
 * -EINVAL val out of range
 **/
static ssize_t
lpfc_link_speed_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int val = LPFC_USER_LINK_SPEED_AUTO;
	int nolip = 0;
	const char *val_buf = buf;
	int err;
	uint32_t prev_val, if_type;

	if_type = bf_get(lpfc_sli_intf_if_type, &phba->sli4_hba.sli_intf);
	if (if_type >= LPFC_SLI_INTF_IF_TYPE_2 &&
	    phba->hba_flag & HBA_FORCED_LINK_SPEED)
		return -EPERM;

	if (!strncmp(buf, "nolip ", strlen("nolip "))) {
		nolip = 1;
		val_buf = &buf[strlen("nolip ")];
	}

	if (!isdigit(val_buf[0]))
		return -EINVAL;
	if (sscanf(val_buf, "%i", &val) != 1)
		return -EINVAL;

	lpfc_printf_vlog(vport, KERN_ERR, LOG_INIT,
		"3055 lpfc_link_speed changed from %d to %d %s\n",
		phba->cfg_link_speed, val, nolip ? "(nolip)" : "(lip)");

	if (((val == LPFC_USER_LINK_SPEED_1G) && !(phba->lmt & LMT_1Gb)) ||
	    ((val == LPFC_USER_LINK_SPEED_2G) && !(phba->lmt & LMT_2Gb)) ||
	    ((val == LPFC_USER_LINK_SPEED_4G) && !(phba->lmt & LMT_4Gb)) ||
	    ((val == LPFC_USER_LINK_SPEED_8G) && !(phba->lmt & LMT_8Gb)) ||
	    ((val == LPFC_USER_LINK_SPEED_10G) && !(phba->lmt & LMT_10Gb)) ||
	    ((val == LPFC_USER_LINK_SPEED_16G) && !(phba->lmt & LMT_16Gb)) ||
	    ((val == LPFC_USER_LINK_SPEED_32G) && !(phba->lmt & LMT_32Gb)) ||
	    ((val == LPFC_USER_LINK_SPEED_64G) && !(phba->lmt & LMT_64Gb))) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"2879 lpfc_link_speed attribute cannot be set "
				"to %d. Speed is not supported by this port.\n",
				val);
		return -EINVAL;
	}
	if (val >= LPFC_USER_LINK_SPEED_16G &&
	    phba->fc_topology == LPFC_TOPOLOGY_LOOP) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3112 lpfc_link_speed attribute cannot be set "
				"to %d. Speed is not supported in loop mode.\n",
				val);
		return -EINVAL;
	}

	switch (val) {
	case LPFC_USER_LINK_SPEED_AUTO:
	case LPFC_USER_LINK_SPEED_1G:
	case LPFC_USER_LINK_SPEED_2G:
	case LPFC_USER_LINK_SPEED_4G:
	case LPFC_USER_LINK_SPEED_8G:
	case LPFC_USER_LINK_SPEED_16G:
	case LPFC_USER_LINK_SPEED_32G:
	case LPFC_USER_LINK_SPEED_64G:
		prev_val = phba->cfg_link_speed;
		phba->cfg_link_speed = val;
		if (nolip)
			return strlen(buf);

		err = lpfc_issue_lip(lpfc_shost_from_vport(phba->pport));
		if (err) {
			phba->cfg_link_speed = prev_val;
			return -EINVAL;
		}
		return strlen(buf);
	default:
		break;
	}

	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"0469 lpfc_link_speed attribute cannot be set to %d, "
			"allowed values are [%s]\n",
			val, LPFC_LINK_SPEED_STRING);
	return -EINVAL;

}

static int lpfc_link_speed = 0;
module_param(lpfc_link_speed, int, S_IRUGO);
MODULE_PARM_DESC(lpfc_link_speed, "Select link speed");
lpfc_param_show(link_speed)

/**
 * lpfc_link_speed_init - Set the adapters link speed
 * @phba: lpfc_hba pointer.
 * @val: link speed value.
 *
 * Description:
 * If val is in a valid range then set the adapter's link speed field.
 *
 * Notes:
 * If the value is not in range log a kernel error message, clear the link
 * speed and return an error.
 *
 * Returns:
 * zero if val saved.
 * -EINVAL val out of range
 **/
static int
lpfc_link_speed_init(struct lpfc_hba *phba, int val)
{
	if (val >= LPFC_USER_LINK_SPEED_16G && phba->cfg_topology == 4) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"3111 lpfc_link_speed of %d cannot "
			"support loop mode, setting topology to default.\n",
			 val);
		phba->cfg_topology = 0;
	}

	switch (val) {
	case LPFC_USER_LINK_SPEED_AUTO:
	case LPFC_USER_LINK_SPEED_1G:
	case LPFC_USER_LINK_SPEED_2G:
	case LPFC_USER_LINK_SPEED_4G:
	case LPFC_USER_LINK_SPEED_8G:
	case LPFC_USER_LINK_SPEED_16G:
	case LPFC_USER_LINK_SPEED_32G:
	case LPFC_USER_LINK_SPEED_64G:
		phba->cfg_link_speed = val;
		return 0;
	default:
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"0405 lpfc_link_speed attribute cannot "
				"be set to %d, allowed values are "
				"["LPFC_LINK_SPEED_STRING"]\n", val);
		phba->cfg_link_speed = LPFC_USER_LINK_SPEED_AUTO;
		return -EINVAL;
	}
}

static DEVICE_ATTR_RW(lpfc_link_speed);

/*
# lpfc_aer_support: Support PCIe device Advanced Error Reporting (AER)
#       0  = aer disabled or not supported
#       1  = aer supported and enabled (default)
# Value range is [0,1]. Default value is 1.
*/
LPFC_ATTR(aer_support, 1, 0, 1,
	"Enable PCIe device AER support");
lpfc_param_show(aer_support)

/**
 * lpfc_aer_support_store - Set the adapter for aer support
 *
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: containing enable or disable aer flag.
 * @count: unused variable.
 *
 * Description:
 * If the val is 1 and currently the device's AER capability was not
 * enabled, invoke the kernel's enable AER helper routine, trying to
 * enable the device's AER capability. If the helper routine enabling
 * AER returns success, update the device's cfg_aer_support flag to
 * indicate AER is supported by the device; otherwise, if the device
 * AER capability is already enabled to support AER, then do nothing.
 *
 * If the val is 0 and currently the device's AER support was enabled,
 * invoke the kernel's disable AER helper routine. After that, update
 * the device's cfg_aer_support flag to indicate AER is not supported
 * by the device; otherwise, if the device AER capability is already
 * disabled from supporting AER, then do nothing.
 *
 * Returns:
 * length of the buf on success if val is in range the intended mode
 * is supported.
 * -EINVAL if val out of range or intended mode is not supported.
 **/
static ssize_t
lpfc_aer_support_store(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	int val = 0, rc = -EINVAL;

	if (!isdigit(buf[0]))
		return -EINVAL;
	if (sscanf(buf, "%i", &val) != 1)
		return -EINVAL;

	switch (val) {
	case 0:
		if (phba->hba_flag & HBA_AER_ENABLED) {
			rc = pci_disable_pcie_error_reporting(phba->pcidev);
			if (!rc) {
				spin_lock_irq(&phba->hbalock);
				phba->hba_flag &= ~HBA_AER_ENABLED;
				spin_unlock_irq(&phba->hbalock);
				phba->cfg_aer_support = 0;
				rc = strlen(buf);
			} else
				rc = -EPERM;
		} else {
			phba->cfg_aer_support = 0;
			rc = strlen(buf);
		}
		break;
	case 1:
		if (!(phba->hba_flag & HBA_AER_ENABLED)) {
			rc = pci_enable_pcie_error_reporting(phba->pcidev);
			if (!rc) {
				spin_lock_irq(&phba->hbalock);
				phba->hba_flag |= HBA_AER_ENABLED;
				spin_unlock_irq(&phba->hbalock);
				phba->cfg_aer_support = 1;
				rc = strlen(buf);
			} else
				 rc = -EPERM;
		} else {
			phba->cfg_aer_support = 1;
			rc = strlen(buf);
		}
		break;
	default:
		rc = -EINVAL;
		break;
	}
	return rc;
}

static DEVICE_ATTR_RW(lpfc_aer_support);

/**
 * lpfc_aer_cleanup_state - Clean up aer state to the aer enabled device
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: containing flag 1 for aer cleanup state.
 * @count: unused variable.
 *
 * Description:
 * If the @buf contains 1 and the device currently has the AER support
 * enabled, then invokes the kernel AER helper routine
 * pci_cleanup_aer_uncorrect_error_status to clean up the uncorrectable
 * error status register.
 *
 * Notes:
 *
 * Returns:
 * -EINVAL if the buf does not contain the 1 or the device is not currently
 * enabled with the AER support.
 **/
static ssize_t
lpfc_aer_cleanup_state(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int val, rc = -1;

	if (!isdigit(buf[0]))
		return -EINVAL;
	if (sscanf(buf, "%i", &val) != 1)
		return -EINVAL;
	if (val != 1)
		return -EINVAL;

	if (phba->hba_flag & HBA_AER_ENABLED)
		rc = pci_cleanup_aer_uncorrect_error_status(phba->pcidev);

	if (rc == 0)
		return strlen(buf);
	else
		return -EPERM;
}

static DEVICE_ATTR(lpfc_aer_state_cleanup, S_IWUSR, NULL,
		   lpfc_aer_cleanup_state);

/**
 * lpfc_sriov_nr_virtfn_store - Enable the adapter for sr-iov virtual functions
 *
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: containing the string the number of vfs to be enabled.
 * @count: unused variable.
 *
 * Description:
 * When this api is called either through user sysfs, the driver shall
 * try to enable or disable SR-IOV virtual functions according to the
 * following:
 *
 * If zero virtual function has been enabled to the physical function,
 * the driver shall invoke the pci enable virtual function api trying
 * to enable the virtual functions. If the nr_vfn provided is greater
 * than the maximum supported, the maximum virtual function number will
 * be used for invoking the api; otherwise, the nr_vfn provided shall
 * be used for invoking the api. If the api call returned success, the
 * actual number of virtual functions enabled will be set to the driver
 * cfg_sriov_nr_virtfn; otherwise, -EINVAL shall be returned and driver
 * cfg_sriov_nr_virtfn remains zero.
 *
 * If none-zero virtual functions have already been enabled to the
 * physical function, as reflected by the driver's cfg_sriov_nr_virtfn,
 * -EINVAL will be returned and the driver does nothing;
 *
 * If the nr_vfn provided is zero and none-zero virtual functions have
 * been enabled, as indicated by the driver's cfg_sriov_nr_virtfn, the
 * disabling virtual function api shall be invoded to disable all the
 * virtual functions and driver's cfg_sriov_nr_virtfn shall be set to
 * zero. Otherwise, if zero virtual function has been enabled, do
 * nothing.
 *
 * Returns:
 * length of the buf on success if val is in range the intended mode
 * is supported.
 * -EINVAL if val out of range or intended mode is not supported.
 **/
static ssize_t
lpfc_sriov_nr_virtfn_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	struct pci_dev *pdev = phba->pcidev;
	int val = 0, rc = -EINVAL;

	/* Sanity check on user data */
	if (!isdigit(buf[0]))
		return -EINVAL;
	if (sscanf(buf, "%i", &val) != 1)
		return -EINVAL;
	if (val < 0)
		return -EINVAL;

	/* Request disabling virtual functions */
	if (val == 0) {
		if (phba->cfg_sriov_nr_virtfn > 0) {
			pci_disable_sriov(pdev);
			phba->cfg_sriov_nr_virtfn = 0;
		}
		return strlen(buf);
	}

	/* Request enabling virtual functions */
	if (phba->cfg_sriov_nr_virtfn > 0) {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3018 There are %d virtual functions "
				"enabled on physical function.\n",
				phba->cfg_sriov_nr_virtfn);
		return -EEXIST;
	}

	if (val <= LPFC_MAX_VFN_PER_PFN)
		phba->cfg_sriov_nr_virtfn = val;
	else {
		lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
				"3019 Enabling %d virtual functions is not "
				"allowed.\n", val);
		return -EINVAL;
	}

	rc = lpfc_sli_probe_sriov_nr_virtfn(phba, phba->cfg_sriov_nr_virtfn);
	if (rc) {
		phba->cfg_sriov_nr_virtfn = 0;
		rc = -EPERM;
	} else
		rc = strlen(buf);

	return rc;
}

LPFC_ATTR(sriov_nr_virtfn, LPFC_DEF_VFN_PER_PFN, 0, LPFC_MAX_VFN_PER_PFN,
	"Enable PCIe device SR-IOV virtual fn");

lpfc_param_show(sriov_nr_virtfn)
static DEVICE_ATTR_RW(lpfc_sriov_nr_virtfn);

/**
 * lpfc_request_firmware_store - Request for Linux generic firmware upgrade
 *
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: containing the string the number of vfs to be enabled.
 * @count: unused variable.
 *
 * Description:
 *
 * Returns:
 * length of the buf on success if val is in range the intended mode
 * is supported.
 * -EINVAL if val out of range or intended mode is not supported.
 **/
static ssize_t
lpfc_request_firmware_upgrade_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	int val = 0, rc = -EINVAL;

	/* Sanity check on user data */
	if (!isdigit(buf[0]))
		return -EINVAL;
	if (sscanf(buf, "%i", &val) != 1)
		return -EINVAL;
	if (val != 1)
		return -EINVAL;

	rc = lpfc_sli4_request_firmware_update(phba, RUN_FW_UPGRADE);
	if (rc)
		rc = -EPERM;
	else
		rc = strlen(buf);
	return rc;
}

static int lpfc_req_fw_upgrade;
module_param(lpfc_req_fw_upgrade, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(lpfc_req_fw_upgrade, "Enable Linux generic firmware upgrade");
lpfc_param_show(request_firmware_upgrade)

/**
 * lpfc_request_firmware_upgrade_init - Enable initial linux generic fw upgrade
 * @phba: lpfc_hba pointer.
 * @val: 0 or 1.
 *
 * Description:
 * Set the initial Linux generic firmware upgrade enable or disable flag.
 *
 * Returns:
 * zero if val saved.
 * -EINVAL val out of range
 **/
static int
lpfc_request_firmware_upgrade_init(struct lpfc_hba *phba, int val)
{
	if (val >= 0 && val <= 1) {
		phba->cfg_request_firmware_upgrade = val;
		return 0;
	}
	return -EINVAL;
}
static DEVICE_ATTR(lpfc_req_fw_upgrade, S_IRUGO | S_IWUSR,
		   lpfc_request_firmware_upgrade_show,
		   lpfc_request_firmware_upgrade_store);

/**
 * lpfc_force_rscn_store
 *
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: unused string
 * @count: unused variable.
 *
 * Description:
 * Force the switch to send a RSCN to all other NPorts in our zone
 * If we are direct connect pt2pt, build the RSCN command ourself
 * and send to the other NPort. Not supported for private loop.
 *
 * Returns:
 * 0      - on success
 * -EIO   - if command is not sent
 **/
static ssize_t
lpfc_force_rscn_store(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	int i;

	i = lpfc_issue_els_rscn(vport, 0);
	if (i)
		return -EIO;
	return strlen(buf);
}

/*
 * lpfc_force_rscn: Force an RSCN to be sent to all remote NPorts
 * connected to  the HBA.
 *
 * Value range is any ascii value
 */
static int lpfc_force_rscn;
module_param(lpfc_force_rscn, int, 0644);
MODULE_PARM_DESC(lpfc_force_rscn,
		 "Force an RSCN to be sent to all remote NPorts");
lpfc_param_show(force_rscn)

/**
 * lpfc_force_rscn_init - Force an RSCN to be sent to all remote NPorts
 * @phba: lpfc_hba pointer.
 * @val: unused value.
 *
 * Returns:
 * zero if val saved.
 **/
static int
lpfc_force_rscn_init(struct lpfc_hba *phba, int val)
{
	return 0;
}
static DEVICE_ATTR_RW(lpfc_force_rscn);

/**
 * lpfc_fcp_imax_store
 *
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: string with the number of fast-path FCP interrupts per second.
 * @count: unused variable.
 *
 * Description:
 * If val is in a valid range [636,651042], then set the adapter's
 * maximum number of fast-path FCP interrupts per second.
 *
 * Returns:
 * length of the buf on success if val is in range the intended mode
 * is supported.
 * -EINVAL if val out of range or intended mode is not supported.
 **/
static ssize_t
lpfc_fcp_imax_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_eq_intr_info *eqi;
	uint32_t usdelay;
	int val = 0, i;

	/* fcp_imax is only valid for SLI4 */
	if (phba->sli_rev != LPFC_SLI_REV4)
		return -EINVAL;

	/* Sanity check on user data */
	if (!isdigit(buf[0]))
		return -EINVAL;
	if (sscanf(buf, "%i", &val) != 1)
		return -EINVAL;

	/*
	 * Value range for the HBA is [5000,5000000]
	 * The value for each EQ depends on how many EQs are configured.
	 * Allow value == 0
	 */
	if (val && (val < LPFC_MIN_IMAX || val > LPFC_MAX_IMAX))
		return -EINVAL;

	phba->cfg_auto_imax = (val) ? 0 : 1;
	if (phba->cfg_fcp_imax && !val) {
		queue_delayed_work(phba->wq, &phba->eq_delay_work,
				   msecs_to_jiffies(LPFC_EQ_DELAY_MSECS));

		for_each_present_cpu(i) {
			eqi = per_cpu_ptr(phba->sli4_hba.eq_info, i);
			eqi->icnt = 0;
		}
	}

	phba->cfg_fcp_imax = (uint32_t)val;

	if (phba->cfg_fcp_imax)
		usdelay = LPFC_SEC_TO_USEC / phba->cfg_fcp_imax;
	else
		usdelay = 0;

	for (i = 0; i < phba->cfg_irq_chann; i += LPFC_MAX_EQ_DELAY_EQID_CNT)
		lpfc_modify_hba_eq_delay(phba, i, LPFC_MAX_EQ_DELAY_EQID_CNT,
					 usdelay);

	return strlen(buf);
}

/*
# lpfc_fcp_imax: The maximum number of fast-path FCP interrupts per second
# for the HBA.
#
# Value range is [5,000 to 5,000,000]. Default value is 50,000.
*/
static int lpfc_fcp_imax = LPFC_DEF_IMAX;
module_param(lpfc_fcp_imax, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(lpfc_fcp_imax,
	    "Set the maximum number of FCP interrupts per second per HBA");
lpfc_param_show(fcp_imax)

/**
 * lpfc_fcp_imax_init - Set the initial sr-iov virtual function enable
 * @phba: lpfc_hba pointer.
 * @val: link speed value.
 *
 * Description:
 * If val is in a valid range [636,651042], then initialize the adapter's
 * maximum number of fast-path FCP interrupts per second.
 *
 * Returns:
 * zero if val saved.
 * -EINVAL val out of range
 **/
static int
lpfc_fcp_imax_init(struct lpfc_hba *phba, int val)
{
	if (phba->sli_rev != LPFC_SLI_REV4) {
		phba->cfg_fcp_imax = 0;
		return 0;
	}

	if ((val >= LPFC_MIN_IMAX && val <= LPFC_MAX_IMAX) ||
	    (val == 0)) {
		phba->cfg_fcp_imax = val;
		return 0;
	}

	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"3016 lpfc_fcp_imax: %d out of range, using default\n",
			val);
	phba->cfg_fcp_imax = LPFC_DEF_IMAX;

	return 0;
}

static DEVICE_ATTR_RW(lpfc_fcp_imax);

/**
 * lpfc_cq_max_proc_limit_store
 *
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: string with the cq max processing limit of cqes
 * @count: unused variable.
 *
 * Description:
 * If val is in a valid range, then set value on each cq
 *
 * Returns:
 * The length of the buf: if successful
 * -ERANGE: if val is not in the valid range
 * -EINVAL: if bad value format or intended mode is not supported.
 **/
static ssize_t
lpfc_cq_max_proc_limit_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_hba *phba = vport->phba;
	struct lpfc_queue *eq, *cq;
	unsigned long val;
	int i;

	/* cq_max_proc_limit is only valid for SLI4 */
	if (phba->sli_rev != LPFC_SLI_REV4)
		return -EINVAL;

	/* Sanity check on user data */
	if (!isdigit(buf[0]))
		return -EINVAL;
	if (kstrtoul(buf, 0, &val))
		return -EINVAL;

	if (val < LPFC_CQ_MIN_PROC_LIMIT || val > LPFC_CQ_MAX_PROC_LIMIT)
		return -ERANGE;

	phba->cfg_cq_max_proc_limit = (uint32_t)val;

	/* set the values on the cq's */
	for (i = 0; i < phba->cfg_irq_chann; i++) {
		/* Get the EQ corresponding to the IRQ vector */
		eq = phba->sli4_hba.hba_eq_hdl[i].eq;
		if (!eq)
			continue;

		list_for_each_entry(cq, &eq->child_list, list)
			cq->max_proc_limit = min(phba->cfg_cq_max_proc_limit,
						 cq->entry_count);
	}

	return strlen(buf);
}

/*
 * lpfc_cq_max_proc_limit: The maximum number CQE entries processed in an
 *   itteration of CQ processing.
 */
static int lpfc_cq_max_proc_limit = LPFC_CQ_DEF_MAX_PROC_LIMIT;
module_param(lpfc_cq_max_proc_limit, int, 0644);
MODULE_PARM_DESC(lpfc_cq_max_proc_limit,
	    "Set the maximum number CQEs processed in an iteration of "
	    "CQ processing");
lpfc_param_show(cq_max_proc_limit)

/*
 * lpfc_cq_poll_threshold: Set the threshold of CQE completions in a
 *   single handler call which should request a polled completion rather
 *   than re-enabling interrupts.
 */
LPFC_ATTR_RW(cq_poll_threshold, LPFC_CQ_DEF_THRESHOLD_TO_POLL,
	     LPFC_CQ_MIN_THRESHOLD_TO_POLL,
	     LPFC_CQ_MAX_THRESHOLD_TO_POLL,
	     "CQE Processing Threshold to enable Polling");

/**
 * lpfc_cq_max_proc_limit_init - Set the initial cq max_proc_limit
 * @phba: lpfc_hba pointer.
 * @val: entry limit
 *
 * Description:
 * If val is in a valid range, then initialize the adapter's maximum
 * value.
 *
 * Returns:
 *  Always returns 0 for success, even if value not always set to
 *  requested value. If value out of range or not supported, will fall
 *  back to default.
 **/
static int
lpfc_cq_max_proc_limit_init(struct lpfc_hba *phba, int val)
{
	phba->cfg_cq_max_proc_limit = LPFC_CQ_DEF_MAX_PROC_LIMIT;

	if (phba->sli_rev != LPFC_SLI_REV4)
		return 0;

	if (val >= LPFC_CQ_MIN_PROC_LIMIT && val <= LPFC_CQ_MAX_PROC_LIMIT) {
		phba->cfg_cq_max_proc_limit = val;
		return 0;
	}

	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"0371 "LPFC_DRIVER_NAME"_cq_max_proc_limit: "
			"%d out of range, using default\n",
			phba->cfg_cq_max_proc_limit);

	return 0;
}

static DEVICE_ATTR_RW(lpfc_cq_max_proc_limit);

/**
 * lpfc_state_show - Display current driver CPU affinity
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains text describing the state of the link.
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_fcp_cpu_map_show(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_vector_map_info *cpup;
	int  len = 0;

	if ((phba->sli_rev != LPFC_SLI_REV4) ||
	    (phba->intr_type != MSIX))
		return len;

	switch (phba->cfg_fcp_cpu_map) {
	case 0:
		len += scnprintf(buf + len, PAGE_SIZE-len,
				"fcp_cpu_map: No mapping (%d)\n",
				phba->cfg_fcp_cpu_map);
		return len;
	case 1:
		len += scnprintf(buf + len, PAGE_SIZE-len,
				"fcp_cpu_map: HBA centric mapping (%d): "
				"%d of %d CPUs online from %d possible CPUs\n",
				phba->cfg_fcp_cpu_map, num_online_cpus(),
				num_present_cpus(),
				phba->sli4_hba.num_possible_cpu);
		break;
	}

	while (phba->sli4_hba.curr_disp_cpu <
	       phba->sli4_hba.num_possible_cpu) {
		cpup = &phba->sli4_hba.cpu_map[phba->sli4_hba.curr_disp_cpu];

		if (!cpu_present(phba->sli4_hba.curr_disp_cpu))
			len += scnprintf(buf + len, PAGE_SIZE - len,
					"CPU %02d not present\n",
					phba->sli4_hba.curr_disp_cpu);
		else if (cpup->eq == LPFC_VECTOR_MAP_EMPTY) {
			if (cpup->hdwq == LPFC_VECTOR_MAP_EMPTY)
				len += scnprintf(
					buf + len, PAGE_SIZE - len,
					"CPU %02d hdwq None "
					"physid %d coreid %d ht %d ua %d\n",
					phba->sli4_hba.curr_disp_cpu,
					cpup->phys_id, cpup->core_id,
					(cpup->flag & LPFC_CPU_MAP_HYPER),
					(cpup->flag & LPFC_CPU_MAP_UNASSIGN));
			else
				len += scnprintf(
					buf + len, PAGE_SIZE - len,
					"CPU %02d EQ None hdwq %04d "
					"physid %d coreid %d ht %d ua %d\n",
					phba->sli4_hba.curr_disp_cpu,
					cpup->hdwq, cpup->phys_id,
					cpup->core_id,
					(cpup->flag & LPFC_CPU_MAP_HYPER),
					(cpup->flag & LPFC_CPU_MAP_UNASSIGN));
		} else {
			if (cpup->hdwq == LPFC_VECTOR_MAP_EMPTY)
				len += scnprintf(
					buf + len, PAGE_SIZE - len,
					"CPU %02d hdwq None "
					"physid %d coreid %d ht %d ua %d IRQ %d\n",
					phba->sli4_hba.curr_disp_cpu,
					cpup->phys_id,
					cpup->core_id,
					(cpup->flag & LPFC_CPU_MAP_HYPER),
					(cpup->flag & LPFC_CPU_MAP_UNASSIGN),
					lpfc_get_irq(cpup->eq));
			else
				len += scnprintf(
					buf + len, PAGE_SIZE - len,
					"CPU %02d EQ %04d hdwq %04d "
					"physid %d coreid %d ht %d ua %d IRQ %d\n",
					phba->sli4_hba.curr_disp_cpu,
					cpup->eq, cpup->hdwq, cpup->phys_id,
					cpup->core_id,
					(cpup->flag & LPFC_CPU_MAP_HYPER),
					(cpup->flag & LPFC_CPU_MAP_UNASSIGN),
					lpfc_get_irq(cpup->eq));
		}

		phba->sli4_hba.curr_disp_cpu++;

		/* display max number of CPUs keeping some margin */
		if (phba->sli4_hba.curr_disp_cpu <
				phba->sli4_hba.num_possible_cpu &&
				(len >= (PAGE_SIZE - 64))) {
			len += scnprintf(buf + len,
					PAGE_SIZE - len, "more...\n");
			break;
		}
	}

	if (phba->sli4_hba.curr_disp_cpu == phba->sli4_hba.num_possible_cpu)
		phba->sli4_hba.curr_disp_cpu = 0;

	return len;
}

/**
 * lpfc_fcp_cpu_map_store - Change CPU affinity of driver vectors
 * @dev: class device that is converted into a Scsi_host.
 * @attr: device attribute, not used.
 * @buf: one or more lpfc_polling_flags values.
 * @count: not used.
 *
 * Returns:
 * -EINVAL  - Not implemented yet.
 **/
static ssize_t
lpfc_fcp_cpu_map_store(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	int status = -EINVAL;
	return status;
}

/*
# lpfc_fcp_cpu_map: Defines how to map CPUs to IRQ vectors
# for the HBA.
#
# Value range is [0 to 1]. Default value is LPFC_HBA_CPU_MAP (1).
#	0 - Do not affinitze IRQ vectors
#	1 - Affintize HBA vectors with respect to each HBA
#	    (start with CPU0 for each HBA)
# This also defines how Hardware Queues are mapped to specific CPUs.
*/
static int lpfc_fcp_cpu_map = LPFC_HBA_CPU_MAP;
module_param(lpfc_fcp_cpu_map, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(lpfc_fcp_cpu_map,
		 "Defines how to map CPUs to IRQ vectors per HBA");

/**
 * lpfc_fcp_cpu_map_init - Set the initial sr-iov virtual function enable
 * @phba: lpfc_hba pointer.
 * @val: link speed value.
 *
 * Description:
 * If val is in a valid range [0-2], then affinitze the adapter's
 * MSIX vectors.
 *
 * Returns:
 * zero if val saved.
 * -EINVAL val out of range
 **/
static int
lpfc_fcp_cpu_map_init(struct lpfc_hba *phba, int val)
{
	if (phba->sli_rev != LPFC_SLI_REV4) {
		phba->cfg_fcp_cpu_map = 0;
		return 0;
	}

	if (val >= LPFC_MIN_CPU_MAP && val <= LPFC_MAX_CPU_MAP) {
		phba->cfg_fcp_cpu_map = val;
		return 0;
	}

	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"3326 lpfc_fcp_cpu_map: %d out of range, using "
			"default\n", val);
	phba->cfg_fcp_cpu_map = LPFC_HBA_CPU_MAP;

	return 0;
}

static DEVICE_ATTR_RW(lpfc_fcp_cpu_map);

/*
# lpfc_fcp_class:  Determines FC class to use for the FCP protocol.
# Value range is [2,3]. Default value is 3.
*/
LPFC_VPORT_ATTR_R(fcp_class, 3, 2, 3,
		  "Select Fibre Channel class of service for FCP sequences");

/*
# lpfc_use_adisc: Use ADISC for FCP rediscovery instead of PLOGI. Value range
# is [0,1]. Default value is 0.
*/
LPFC_VPORT_ATTR_RW(use_adisc, 0, 0, 1,
		   "Use ADISC on rediscovery to authenticate FCP devices");

/*
# lpfc_first_burst_size: First burst size to use on the NPorts
# that support first burst.
# Value range is [0,65536]. Default value is 0.
*/
LPFC_VPORT_ATTR_RW(first_burst_size, 0, 0, 65536,
		   "First burst size for Targets that support first burst");

/*
* lpfc_nvmet_fb_size: NVME Target mode supported first burst size.
* When the driver is configured as an NVME target, this value is
* communicated to the NVME initiator in the PRLI response.  It is
* used only when the lpfc_nvme_enable_fb and lpfc_nvmet_support
* parameters are set and the target is sending the PRLI RSP.
* Parameter supported on physical port only - no NPIV support.
* Value range is [0,65536]. Default value is 0.
*/
LPFC_ATTR_RW(nvmet_fb_size, 0, 0, 65536,
	     "NVME Target mode first burst size in 512B increments.");

/*
 * lpfc_nvme_enable_fb: Enable NVME first burst on I and T functions.
 * For the Initiator (I), enabling this parameter means that an NVMET
 * PRLI response with FBA enabled and an FB_SIZE set to a nonzero value will be
 * processed by the initiator for subsequent NVME FCP IO.
 * Currently, this feature is not supported on the NVME target
 * Value range is [0,1]. Default value is 0 (disabled).
 */
LPFC_ATTR_RW(nvme_enable_fb, 0, 0, 1,
	     "Enable First Burst feature for NVME Initiator.");

/*
# lpfc_max_scsicmpl_time: Use scsi command completion time to control I/O queue
# depth. Default value is 0. When the value of this parameter is zero the
# SCSI command completion time is not used for controlling I/O queue depth. When
# the parameter is set to a non-zero value, the I/O queue depth is controlled
# to limit the I/O completion time to the parameter value.
# The value is set in milliseconds.
*/
LPFC_VPORT_ATTR(max_scsicmpl_time, 0, 0, 60000,
	"Use command completion time to control queue depth");

lpfc_vport_param_show(max_scsicmpl_time);
static int
lpfc_max_scsicmpl_time_set(struct lpfc_vport *vport, int val)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	struct lpfc_nodelist *ndlp, *next_ndlp;

	if (val == vport->cfg_max_scsicmpl_time)
		return 0;
	if ((val < 0) || (val > 60000))
		return -EINVAL;
	vport->cfg_max_scsicmpl_time = val;

	spin_lock_irq(shost->host_lock);
	list_for_each_entry_safe(ndlp, next_ndlp, &vport->fc_nodes, nlp_listp) {
		if (!NLP_CHK_NODE_ACT(ndlp))
			continue;
		if (ndlp->nlp_state == NLP_STE_UNUSED_NODE)
			continue;
		ndlp->cmd_qdepth = vport->cfg_tgt_queue_depth;
	}
	spin_unlock_irq(shost->host_lock);
	return 0;
}
lpfc_vport_param_store(max_scsicmpl_time);
static DEVICE_ATTR_RW(lpfc_max_scsicmpl_time);

/*
# lpfc_ack0: Use ACK0, instead of ACK1 for class 2 acknowledgement. Value
# range is [0,1]. Default value is 0.
*/
LPFC_ATTR_R(ack0, 0, 0, 1, "Enable ACK0 support");

/*
# lpfc_xri_rebalancing: enable or disable XRI rebalancing feature
# range is [0,1]. Default value is 1.
*/
LPFC_ATTR_R(xri_rebalancing, 1, 0, 1, "Enable/Disable XRI rebalancing");

/*
 * lpfc_io_sched: Determine scheduling algrithmn for issuing FCP cmds
 * range is [0,1]. Default value is 0.
 * For [0], FCP commands are issued to Work Queues based on upper layer
 * hardware queue index.
 * For [1], FCP commands are issued to a Work Queue associated with the
 *          current CPU.
 *
 * LPFC_FCP_SCHED_BY_HDWQ == 0
 * LPFC_FCP_SCHED_BY_CPU == 1
 *
 * The driver dynamically sets this to 1 (BY_CPU) if it's able to set up cpu
 * affinity for FCP/NVME I/Os through Work Queues associated with the current
 * CPU. Otherwise, the default 0 (Round Robin) scheduling of FCP/NVME I/Os
 * through WQs will be used.
 */
LPFC_ATTR_RW(fcp_io_sched, LPFC_FCP_SCHED_BY_CPU,
	     LPFC_FCP_SCHED_BY_HDWQ,
	     LPFC_FCP_SCHED_BY_CPU,
	     "Determine scheduling algorithm for "
	     "issuing commands [0] - Hardware Queue, [1] - Current CPU");

/*
 * lpfc_ns_query: Determine algrithmn for NameServer queries after RSCN
 * range is [0,1]. Default value is 0.
 * For [0], GID_FT is used for NameServer queries after RSCN (default)
 * For [1], GID_PT is used for NameServer queries after RSCN
 *
 */
LPFC_ATTR_RW(ns_query, LPFC_NS_QUERY_GID_FT,
	     LPFC_NS_QUERY_GID_FT, LPFC_NS_QUERY_GID_PT,
	     "Determine algorithm NameServer queries after RSCN "
	     "[0] - GID_FT, [1] - GID_PT");

/*
# lpfc_fcp2_no_tgt_reset: Determine bus reset behavior
# range is [0,1]. Default value is 0.
# For [0], bus reset issues target reset to ALL devices
# For [1], bus reset issues target reset to non-FCP2 devices
*/
LPFC_ATTR_RW(fcp2_no_tgt_reset, 0, 0, 1, "Determine bus reset behavior for "
	     "FCP2 devices [0] - issue tgt reset, [1] - no tgt reset");


/*
# lpfc_cr_delay & lpfc_cr_count: Default values for I/O colaesing
# cr_delay (msec) or cr_count outstanding commands. cr_delay can take
# value [0,63]. cr_count can take value [1,255]. Default value of cr_delay
# is 0. Default value of cr_count is 1. The cr_count feature is disabled if
# cr_delay is set to 0.
*/
LPFC_ATTR_RW(cr_delay, 0, 0, 63, "A count of milliseconds after which an "
		"interrupt response is generated");

LPFC_ATTR_RW(cr_count, 1, 1, 255, "A count of I/O completions after which an "
		"interrupt response is generated");

/*
# lpfc_multi_ring_support:  Determines how many rings to spread available
# cmd/rsp IOCB entries across.
# Value range is [1,2]. Default value is 1.
*/
LPFC_ATTR_R(multi_ring_support, 1, 1, 2, "Determines number of primary "
		"SLI rings to spread IOCB entries across");

/*
# lpfc_multi_ring_rctl:  If lpfc_multi_ring_support is enabled, this
# identifies what rctl value to configure the additional ring for.
# Value range is [1,0xff]. Default value is 4 (Unsolicated Data).
*/
LPFC_ATTR_R(multi_ring_rctl, FC_RCTL_DD_UNSOL_DATA, 1,
	     255, "Identifies RCTL for additional ring configuration");

/*
# lpfc_multi_ring_type:  If lpfc_multi_ring_support is enabled, this
# identifies what type value to configure the additional ring for.
# Value range is [1,0xff]. Default value is 5 (LLC/SNAP).
*/
LPFC_ATTR_R(multi_ring_type, FC_TYPE_IP, 1,
	     255, "Identifies TYPE for additional ring configuration");

/*
# lpfc_enable_SmartSAN: Sets up FDMI support for SmartSAN
#       0  = SmartSAN functionality disabled (default)
#       1  = SmartSAN functionality enabled
# This parameter will override the value of lpfc_fdmi_on module parameter.
# Value range is [0,1]. Default value is 0.
*/
LPFC_ATTR_R(enable_SmartSAN, 0, 0, 1, "Enable SmartSAN functionality");

/*
# lpfc_fdmi_on: Controls FDMI support.
#       0       No FDMI support
#       1       Traditional FDMI support (default)
# Traditional FDMI support means the driver will assume FDMI-2 support;
# however, if that fails, it will fallback to FDMI-1.
# If lpfc_enable_SmartSAN is set to 1, the driver ignores lpfc_fdmi_on.
# If lpfc_enable_SmartSAN is set 0, the driver uses the current value of
# lpfc_fdmi_on.
# Value range [0,1]. Default value is 1.
*/
LPFC_ATTR_R(fdmi_on, 1, 0, 1, "Enable FDMI support");

/*
# Specifies the maximum number of ELS cmds we can have outstanding (for
# discovery). Value range is [1,64]. Default value = 32.
*/
LPFC_VPORT_ATTR(discovery_threads, 32, 1, 64, "Maximum number of ELS commands "
		 "during discovery");

/*
# lpfc_max_luns: maximum allowed LUN ID. This is the highest LUN ID that
#    will be scanned by the SCSI midlayer when sequential scanning is
#    used; and is also the highest LUN ID allowed when the SCSI midlayer
#    parses REPORT_LUN responses. The lpfc driver has no LUN count or
#    LUN ID limit, but the SCSI midlayer requires this field for the uses
#    above. The lpfc driver limits the default value to 255 for two reasons.
#    As it bounds the sequential scan loop, scanning for thousands of luns
#    on a target can take minutes of wall clock time.  Additionally,
#    there are FC targets, such as JBODs, that only recognize 8-bits of
#    LUN ID. When they receive a value greater than 8 bits, they chop off
#    the high order bits. In other words, they see LUN IDs 0, 256, 512,
#    and so on all as LUN ID 0. This causes the linux kernel, which sees
#    valid responses at each of the LUN IDs, to believe there are multiple
#    devices present, when in fact, there is only 1.
#    A customer that is aware of their target behaviors, and the results as
#    indicated above, is welcome to increase the lpfc_max_luns value.
#    As mentioned, this value is not used by the lpfc driver, only the
#    SCSI midlayer.
# Value range is [0,65535]. Default value is 255.
# NOTE: The SCSI layer might probe all allowed LUN on some old targets.
*/
LPFC_VPORT_ULL_ATTR_R(max_luns, 255, 0, 65535, "Maximum allowed LUN ID");

/*
# lpfc_poll_tmo: .Milliseconds driver will wait between polling FCP ring.
# Value range is [1,255], default value is 10.
*/
LPFC_ATTR_RW(poll_tmo, 10, 1, 255,
	     "Milliseconds driver will wait between polling FCP ring");

/*
# lpfc_task_mgmt_tmo: Maximum time to wait for task management commands
# to complete in seconds. Value range is [5,180], default value is 60.
*/
LPFC_ATTR_RW(task_mgmt_tmo, 60, 5, 180,
	     "Maximum time to wait for task management commands to complete");
/*
# lpfc_use_msi: Use MSI (Message Signaled Interrupts) in systems that
#		support this feature
#       0  = MSI disabled
#       1  = MSI enabled
#       2  = MSI-X enabled (default)
# Value range is [0,2]. Default value is 2.
*/
LPFC_ATTR_R(use_msi, 2, 0, 2, "Use Message Signaled Interrupts (1) or "
	    "MSI-X (2), if possible");

/*
 * lpfc_nvme_oas: Use the oas bit when sending NVME/NVMET IOs
 *
 *      0  = NVME OAS disabled
 *      1  = NVME OAS enabled
 *
 * Value range is [0,1]. Default value is 0.
 */
LPFC_ATTR_RW(nvme_oas, 0, 0, 1,
	     "Use OAS bit on NVME IOs");

/*
 * lpfc_nvme_embed_cmd: Use the oas bit when sending NVME/NVMET IOs
 *
 *      0  = Put NVME Command in SGL
 *      1  = Embed NVME Command in WQE (unless G7)
 *      2 =  Embed NVME Command in WQE (force)
 *
 * Value range is [0,2]. Default value is 1.
 */
LPFC_ATTR_RW(nvme_embed_cmd, 1, 0, 2,
	     "Embed NVME Command in WQE");

/*
 * lpfc_fcp_mq_threshold: Set the maximum number of Hardware Queues
 * the driver will advertise it supports to the SCSI layer.
 *
 *      0    = Set nr_hw_queues by the number of CPUs or HW queues.
 *      1,256 = Manually specify nr_hw_queue value to be advertised,
 *
 * Value range is [0,256]. Default value is 8.
 */
LPFC_ATTR_R(fcp_mq_threshold, LPFC_FCP_MQ_THRESHOLD_DEF,
	    LPFC_FCP_MQ_THRESHOLD_MIN, LPFC_FCP_MQ_THRESHOLD_MAX,
	    "Set the number of SCSI Queues advertised");

/*
 * lpfc_hdw_queue: Set the number of Hardware Queues the driver
 * will advertise it supports to the NVME and  SCSI layers. This also
 * will map to the number of CQ/WQ pairs the driver will create.
 *
 * The NVME Layer will try to create this many, plus 1 administrative
 * hardware queue. The administrative queue will always map to WQ 0
 * A hardware IO queue maps (qidx) to a specific driver CQ/WQ.
 *
 *      0    = Configure the number of hdw queues to the number of active CPUs.
 *      1,256 = Manually specify how many hdw queues to use.
 *
 * Value range is [0,256]. Default value is 0.
 */
LPFC_ATTR_R(hdw_queue,
	    LPFC_HBA_HDWQ_DEF,
	    LPFC_HBA_HDWQ_MIN, LPFC_HBA_HDWQ_MAX,
	    "Set the number of I/O Hardware Queues");

static inline void
lpfc_assign_default_irq_numa(struct lpfc_hba *phba)
{
#if IS_ENABLED(CONFIG_X86)
	/* If AMD architecture, then default is LPFC_IRQ_CHANN_NUMA */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
		phba->cfg_irq_numa = 1;
	else
		phba->cfg_irq_numa = 0;
#else
	phba->cfg_irq_numa = 0;
#endif
}

/*
 * lpfc_irq_chann: Set the number of IRQ vectors that are available
 * for Hardware Queues to utilize.  This also will map to the number
 * of EQ / MSI-X vectors the driver will create. This should never be
 * more than the number of Hardware Queues
 *
 *	0		= Configure number of IRQ Channels to:
 *			  if AMD architecture, number of CPUs on HBA's NUMA node
 *			  otherwise, number of active CPUs.
 *	[1,256]		= Manually specify how many IRQ Channels to use.
 *
 * Value range is [0,256]. Default value is [0].
 */
static uint lpfc_irq_chann = LPFC_IRQ_CHANN_DEF;
module_param(lpfc_irq_chann, uint, 0444);
MODULE_PARM_DESC(lpfc_irq_chann, "Set number of interrupt vectors to allocate");

/* lpfc_irq_chann_init - Set the hba irq_chann initial value
 * @phba: lpfc_hba pointer.
 * @val: contains the initial value
 *
 * Description:
 * Validates the initial value is within range and assigns it to the
 * adapter. If not in range, an error message is posted and the
 * default value is assigned.
 *
 * Returns:
 * zero if value is in range and is set
 * -EINVAL if value was out of range
 **/
static int
lpfc_irq_chann_init(struct lpfc_hba *phba, uint32_t val)
{
	const struct cpumask *numa_mask;

	if (phba->cfg_use_msi != 2) {
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"8532 use_msi = %u ignoring cfg_irq_numa\n",
				phba->cfg_use_msi);
		phba->cfg_irq_numa = 0;
		phba->cfg_irq_chann = LPFC_IRQ_CHANN_MIN;
		return 0;
	}

	/* Check if default setting was passed */
	if (val == LPFC_IRQ_CHANN_DEF)
		lpfc_assign_default_irq_numa(phba);

	if (phba->cfg_irq_numa) {
		numa_mask = &phba->sli4_hba.numa_mask;

		if (cpumask_empty(numa_mask)) {
			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"8533 Could not identify NUMA node, "
					"ignoring cfg_irq_numa\n");
			phba->cfg_irq_numa = 0;
			phba->cfg_irq_chann = LPFC_IRQ_CHANN_MIN;
		} else {
			phba->cfg_irq_chann = cpumask_weight(numa_mask);
			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"8543 lpfc_irq_chann set to %u "
					"(numa)\n", phba->cfg_irq_chann);
		}
	} else {
		if (val > LPFC_IRQ_CHANN_MAX) {
			lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
					"8545 lpfc_irq_chann attribute cannot "
					"be set to %u, allowed range is "
					"[%u,%u]\n",
					val,
					LPFC_IRQ_CHANN_MIN,
					LPFC_IRQ_CHANN_MAX);
			phba->cfg_irq_chann = LPFC_IRQ_CHANN_MIN;
			return -EINVAL;
		}
		phba->cfg_irq_chann = val;
	}

	return 0;
}

/**
 * lpfc_irq_chann_show - Display value of irq_chann
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains a string with the list sizes
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_irq_chann_show(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_hba *phba = vport->phba;

	return scnprintf(buf, PAGE_SIZE, "%u\n", phba->cfg_irq_chann);
}

static DEVICE_ATTR_RO(lpfc_irq_chann);

/*
# lpfc_enable_hba_reset: Allow or prevent HBA resets to the hardware.
#       0  = HBA resets disabled
#       1  = HBA resets enabled (default)
#       2  = HBA reset via PCI bus reset enabled
# Value range is [0,2]. Default value is 1.
*/
LPFC_ATTR_RW(enable_hba_reset, 1, 0, 2, "Enable HBA resets from the driver.");

/*
# lpfc_enable_hba_heartbeat: Disable HBA heartbeat timer..
#       0  = HBA Heartbeat disabled
#       1  = HBA Heartbeat enabled (default)
# Value range is [0,1]. Default value is 1.
*/
LPFC_ATTR_R(enable_hba_heartbeat, 0, 0, 1, "Enable HBA Heartbeat.");

/*
# lpfc_EnableXLane: Enable Express Lane Feature
#      0x0   Express Lane Feature disabled
#      0x1   Express Lane Feature enabled
# Value range is [0,1]. Default value is 0.
*/
LPFC_ATTR_R(EnableXLane, 0, 0, 1, "Enable Express Lane Feature.");

/*
# lpfc_XLanePriority:  Define CS_CTL priority for Express Lane Feature
#       0x0 - 0x7f  = CS_CTL field in FC header (high 7 bits)
# Value range is [0x0,0x7f]. Default value is 0
*/
LPFC_ATTR_RW(XLanePriority, 0, 0x0, 0x7f, "CS_CTL for Express Lane Feature.");

/*
# lpfc_enable_bg: Enable BlockGuard (Emulex's Implementation of T10-DIF)
#       0  = BlockGuard disabled (default)
#       1  = BlockGuard enabled
# Value range is [0,1]. Default value is 0.
*/
LPFC_ATTR_R(enable_bg, 0, 0, 1, "Enable BlockGuard Support");

/*
# lpfc_prot_mask: i
#	- Bit mask of host protection capabilities used to register with the
#	  SCSI mid-layer
# 	- Only meaningful if BG is turned on (lpfc_enable_bg=1).
#	- Allows you to ultimately specify which profiles to use
#	- Default will result in registering capabilities for all profiles.
#	- SHOST_DIF_TYPE1_PROTECTION	1
#		HBA supports T10 DIF Type 1: HBA to Target Type 1 Protection
#	- SHOST_DIX_TYPE0_PROTECTION	8
#		HBA supports DIX Type 0: Host to HBA protection only
#	- SHOST_DIX_TYPE1_PROTECTION	16
#		HBA supports DIX Type 1: Host to HBA  Type 1 protection
#
*/
LPFC_ATTR(prot_mask,
	(SHOST_DIF_TYPE1_PROTECTION |
	SHOST_DIX_TYPE0_PROTECTION |
	SHOST_DIX_TYPE1_PROTECTION),
	0,
	(SHOST_DIF_TYPE1_PROTECTION |
	SHOST_DIX_TYPE0_PROTECTION |
	SHOST_DIX_TYPE1_PROTECTION),
	"T10-DIF host protection capabilities mask");

/*
# lpfc_prot_guard: i
#	- Bit mask of protection guard types to register with the SCSI mid-layer
#	- Guard types are currently either 1) T10-DIF CRC 2) IP checksum
#	- Allows you to ultimately specify which profiles to use
#	- Default will result in registering capabilities for all guard types
#
*/
LPFC_ATTR(prot_guard,
	SHOST_DIX_GUARD_IP, SHOST_DIX_GUARD_CRC, SHOST_DIX_GUARD_IP,
	"T10-DIF host protection guard type");

/*
 * Delay initial NPort discovery when Clean Address bit is cleared in
 * FLOGI/FDISC accept and FCID/Fabric name/Fabric portname is changed.
 * This parameter can have value 0 or 1.
 * When this parameter is set to 0, no delay is added to the initial
 * discovery.
 * When this parameter is set to non-zero value, initial Nport discovery is
 * delayed by ra_tov seconds when Clean Address bit is cleared in FLOGI/FDISC
 * accept and FCID/Fabric name/Fabric portname is changed.
 * Driver always delay Nport discovery for subsequent FLOGI/FDISC completion
 * when Clean Address bit is cleared in FLOGI/FDISC
 * accept and FCID/Fabric name/Fabric portname is changed.
 * Default value is 0.
 */
LPFC_ATTR(delay_discovery, 0, 0, 1,
	"Delay NPort discovery when Clean Address bit is cleared.");

/*
 * lpfc_sg_seg_cnt - Initial Maximum DMA Segment Count
 * This value can be set to values between 64 and 4096. The default value
 * is 64, but may be increased to allow for larger Max I/O sizes. The scsi
 * and nvme layers will allow I/O sizes up to (MAX_SEG_COUNT * SEG_SIZE).
 * Because of the additional overhead involved in setting up T10-DIF,
 * this parameter will be limited to 128 if BlockGuard is enabled under SLI4
 * and will be limited to 512 if BlockGuard is enabled under SLI3.
 */
static uint lpfc_sg_seg_cnt = LPFC_DEFAULT_SG_SEG_CNT;
module_param(lpfc_sg_seg_cnt, uint, 0444);
MODULE_PARM_DESC(lpfc_sg_seg_cnt, "Max Scatter Gather Segment Count");

/**
 * lpfc_sg_seg_cnt_show - Display the scatter/gather list sizes
 *    configured for the adapter
 * @dev: class converted to a Scsi_host structure.
 * @attr: device attribute, not used.
 * @buf: on return contains a string with the list sizes
 *
 * Returns: size of formatted string.
 **/
static ssize_t
lpfc_sg_seg_cnt_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	int len;

	len = scnprintf(buf, PAGE_SIZE, "SGL sz: %d  total SGEs: %d\n",
		       phba->cfg_sg_dma_buf_size, phba->cfg_total_seg_cnt);

	len += scnprintf(buf + len, PAGE_SIZE, "Cfg: %d  SCSI: %d  NVME: %d\n",
			phba->cfg_sg_seg_cnt, phba->cfg_scsi_seg_cnt,
			phba->cfg_nvme_seg_cnt);
	return len;
}

static DEVICE_ATTR_RO(lpfc_sg_seg_cnt);

/**
 * lpfc_sg_seg_cnt_init - Set the hba sg_seg_cnt initial value
 * @phba: lpfc_hba pointer.
 * @val: contains the initial value
 *
 * Description:
 * Validates the initial value is within range and assigns it to the
 * adapter. If not in range, an error message is posted and the
 * default value is assigned.
 *
 * Returns:
 * zero if value is in range and is set
 * -EINVAL if value was out of range
 **/
static int
lpfc_sg_seg_cnt_init(struct lpfc_hba *phba, int val)
{
	if (val >= LPFC_MIN_SG_SEG_CNT && val <= LPFC_MAX_SG_SEG_CNT) {
		phba->cfg_sg_seg_cnt = val;
		return 0;
	}
	lpfc_printf_log(phba, KERN_ERR, LOG_INIT,
			"0409 "LPFC_DRIVER_NAME"_sg_seg_cnt attribute cannot "
			"be set to %d, allowed range is [%d, %d]\n",
			val, LPFC_MIN_SG_SEG_CNT, LPFC_MAX_SG_SEG_CNT);
	phba->cfg_sg_seg_cnt = LPFC_DEFAULT_SG_SEG_CNT;
	return -EINVAL;
}

/*
 * lpfc_enable_mds_diags: Enable MDS Diagnostics
 *       0  = MDS Diagnostics disabled (default)
 *       1  = MDS Diagnostics enabled
 * Value range is [0,1]. Default value is 0.
 */
LPFC_ATTR_RW(enable_mds_diags, 0, 0, 1, "Enable MDS Diagnostics");

/*
 * lpfc_ras_fwlog_buffsize: Firmware logging host buffer size
 *	0 = Disable firmware logging (default)
 *	[1-4] = Multiple of 1/4th Mb of host memory for FW logging
 * Value range [0..4]. Default value is 0
 */
LPFC_ATTR(ras_fwlog_buffsize, 0, 0, 4, "Host memory for FW logging");
lpfc_param_show(ras_fwlog_buffsize);

static ssize_t
lpfc_ras_fwlog_buffsize_set(struct lpfc_hba  *phba, uint val)
{
	int ret = 0;
	enum ras_state state;

	if (!lpfc_rangecheck(val, 0, 4))
		return -EINVAL;

	if (phba->cfg_ras_fwlog_buffsize == val)
		return 0;

	if (phba->cfg_ras_fwlog_func != PCI_FUNC(phba->pcidev->devfn))
		return -EINVAL;

	spin_lock_irq(&phba->hbalock);
	state = phba->ras_fwlog.state;
	spin_unlock_irq(&phba->hbalock);

	if (state == REG_INPROGRESS) {
		lpfc_printf_log(phba, KERN_ERR, LOG_SLI, "6147 RAS Logging "
				"registration is in progress\n");
		return -EBUSY;
	}

	/* For disable logging: stop the logs and free the DMA.
	 * For ras_fwlog_buffsize size change we still need to free and
	 * reallocate the DMA in lpfc_sli4_ras_fwlog_init.
	 */
	phba->cfg_ras_fwlog_buffsize = val;
	if (state == ACTIVE) {
		lpfc_ras_stop_fwlog(phba);
		lpfc_sli4_ras_dma_free(phba);
	}

	lpfc_sli4_ras_init(phba);
	if (phba->ras_fwlog.ras_enabled)
		ret = lpfc_sli4_ras_fwlog_init(phba, phba->cfg_ras_fwlog_level,
					       LPFC_RAS_ENABLE_LOGGING);
	return ret;
}

lpfc_param_store(ras_fwlog_buffsize);
static DEVICE_ATTR_RW(lpfc_ras_fwlog_buffsize);

/*
 * lpfc_ras_fwlog_level: Firmware logging verbosity level
 * Valid only if firmware logging is enabled
 * 0(Least Verbosity) 4 (most verbosity)
 * Value range is [0..4]. Default value is 0
 */
LPFC_ATTR_RW(ras_fwlog_level, 0, 0, 4, "Firmware Logging Level");

/*
 * lpfc_ras_fwlog_func: Firmware logging enabled on function number
 * Default function which has RAS support : 0
 * Value Range is [0..7].
 * FW logging is a global action and enablement is via a specific
 * port.
 */
LPFC_ATTR_RW(ras_fwlog_func, 0, 0, 7, "Firmware Logging Enabled on Function");

/*
 * lpfc_enable_bbcr: Enable BB Credit Recovery
 *       0  = BB Credit Recovery disabled
 *       1  = BB Credit Recovery enabled (default)
 * Value range is [0,1]. Default value is 1.
 */
LPFC_BBCR_ATTR_RW(enable_bbcr, 1, 0, 1, "Enable BBC Recovery");

/*
 * lpfc_enable_dpp: Enable DPP on G7
 *       0  = DPP on G7 disabled
 *       1  = DPP on G7 enabled (default)
 * Value range is [0,1]. Default value is 1.
 */
LPFC_ATTR_RW(enable_dpp, 1, 0, 1, "Enable Direct Packet Push");

struct device_attribute *lpfc_hba_attrs[] = {
	&dev_attr_nvme_info,
	&dev_attr_scsi_stat,
	&dev_attr_bg_info,
	&dev_attr_bg_guard_err,
	&dev_attr_bg_apptag_err,
	&dev_attr_bg_reftag_err,
	&dev_attr_info,
	&dev_attr_serialnum,
	&dev_attr_modeldesc,
	&dev_attr_modelname,
	&dev_attr_programtype,
	&dev_attr_portnum,
	&dev_attr_fwrev,
	&dev_attr_hdw,
	&dev_attr_option_rom_version,
	&dev_attr_link_state,
	&dev_attr_num_discovered_ports,
	&dev_attr_menlo_mgmt_mode,
	&dev_attr_lpfc_drvr_version,
	&dev_attr_lpfc_enable_fip,
	&dev_attr_lpfc_temp_sensor,
	&dev_attr_lpfc_log_verbose,
	&dev_attr_lpfc_lun_queue_depth,
	&dev_attr_lpfc_tgt_queue_depth,
	&dev_attr_lpfc_hba_queue_depth,
	&dev_attr_lpfc_peer_port_login,
	&dev_attr_lpfc_nodev_tmo,
	&dev_attr_lpfc_devloss_tmo,
	&dev_attr_lpfc_enable_fc4_type,
	&dev_attr_lpfc_fcp_class,
	&dev_attr_lpfc_use_adisc,
	&dev_attr_lpfc_first_burst_size,
	&dev_attr_lpfc_ack0,
	&dev_attr_lpfc_xri_rebalancing,
	&dev_attr_lpfc_topology,
	&dev_attr_lpfc_scan_down,
	&dev_attr_lpfc_link_speed,
	&dev_attr_lpfc_fcp_io_sched,
	&dev_attr_lpfc_ns_query,
	&dev_attr_lpfc_fcp2_no_tgt_reset,
	&dev_attr_lpfc_cr_delay,
	&dev_attr_lpfc_cr_count,
	&dev_attr_lpfc_multi_ring_support,
	&dev_attr_lpfc_multi_ring_rctl,
	&dev_attr_lpfc_multi_ring_type,
	&dev_attr_lpfc_fdmi_on,
	&dev_attr_lpfc_enable_SmartSAN,
	&dev_attr_lpfc_max_luns,
	&dev_attr_lpfc_enable_npiv,
	&dev_attr_lpfc_fcf_failover_policy,
	&dev_attr_lpfc_enable_rrq,
	&dev_attr_nport_evt_cnt,
	&dev_attr_board_mode,
	&dev_attr_max_vpi,
	&dev_attr_used_vpi,
	&dev_attr_max_rpi,
	&dev_attr_used_rpi,
	&dev_attr_max_xri,
	&dev_attr_used_xri,
	&dev_attr_npiv_info,
	&dev_attr_issue_reset,
	&dev_attr_lpfc_poll,
	&dev_attr_lpfc_poll_tmo,
	&dev_attr_lpfc_task_mgmt_tmo,
	&dev_attr_lpfc_use_msi,
	&dev_attr_lpfc_nvme_oas,
	&dev_attr_lpfc_nvme_embed_cmd,
	&dev_attr_lpfc_fcp_imax,
	&dev_attr_lpfc_force_rscn,
	&dev_attr_lpfc_cq_poll_threshold,
	&dev_attr_lpfc_cq_max_proc_limit,
	&dev_attr_lpfc_fcp_cpu_map,
	&dev_attr_lpfc_fcp_mq_threshold,
	&dev_attr_lpfc_hdw_queue,
	&dev_attr_lpfc_irq_chann,
	&dev_attr_lpfc_suppress_rsp,
	&dev_attr_lpfc_nvmet_mrq,
	&dev_attr_lpfc_nvmet_mrq_post,
	&dev_attr_lpfc_nvme_enable_fb,
	&dev_attr_lpfc_nvmet_fb_size,
	&dev_attr_lpfc_enable_bg,
	&dev_attr_lpfc_soft_wwnn,
	&dev_attr_lpfc_soft_wwpn,
	&dev_attr_lpfc_soft_wwn_enable,
	&dev_attr_lpfc_enable_hba_reset,
	&dev_attr_lpfc_enable_hba_heartbeat,
	&dev_attr_lpfc_EnableXLane,
	&dev_attr_lpfc_XLanePriority,
	&dev_attr_lpfc_xlane_lun,
	&dev_attr_lpfc_xlane_tgt,
	&dev_attr_lpfc_xlane_vpt,
	&dev_attr_lpfc_xlane_lun_state,
	&dev_attr_lpfc_xlane_lun_status,
	&dev_attr_lpfc_xlane_priority,
	&dev_attr_lpfc_sg_seg_cnt,
	&dev_attr_lpfc_max_scsicmpl_time,
	&dev_attr_lpfc_stat_data_ctrl,
	&dev_attr_lpfc_aer_support,
	&dev_attr_lpfc_aer_state_cleanup,
	&dev_attr_lpfc_sriov_nr_virtfn,
	&dev_attr_lpfc_req_fw_upgrade,
	&dev_attr_lpfc_suppress_link_up,
	&dev_attr_iocb_hw,
	&dev_attr_pls,
	&dev_attr_pt,
	&dev_attr_txq_hw,
	&dev_attr_txcmplq_hw,
	&dev_attr_lpfc_fips_level,
	&dev_attr_lpfc_fips_rev,
	&dev_attr_lpfc_dss,
	&dev_attr_lpfc_sriov_hw_max_virtfn,
	&dev_attr_protocol,
	&dev_attr_lpfc_xlane_supported,
	&dev_attr_lpfc_enable_mds_diags,
	&dev_attr_lpfc_ras_fwlog_buffsize,
	&dev_attr_lpfc_ras_fwlog_level,
	&dev_attr_lpfc_ras_fwlog_func,
	&dev_attr_lpfc_enable_bbcr,
	&dev_attr_lpfc_enable_dpp,
	NULL,
};

struct device_attribute *lpfc_vport_attrs[] = {
	&dev_attr_info,
	&dev_attr_link_state,
	&dev_attr_num_discovered_ports,
	&dev_attr_lpfc_drvr_version,
	&dev_attr_lpfc_log_verbose,
	&dev_attr_lpfc_lun_queue_depth,
	&dev_attr_lpfc_tgt_queue_depth,
	&dev_attr_lpfc_nodev_tmo,
	&dev_attr_lpfc_devloss_tmo,
	&dev_attr_lpfc_hba_queue_depth,
	&dev_attr_lpfc_peer_port_login,
	&dev_attr_lpfc_restrict_login,
	&dev_attr_lpfc_fcp_class,
	&dev_attr_lpfc_use_adisc,
	&dev_attr_lpfc_first_burst_size,
	&dev_attr_lpfc_max_luns,
	&dev_attr_nport_evt_cnt,
	&dev_attr_npiv_info,
	&dev_attr_lpfc_enable_da_id,
	&dev_attr_lpfc_max_scsicmpl_time,
	&dev_attr_lpfc_stat_data_ctrl,
	&dev_attr_lpfc_static_vport,
	&dev_attr_lpfc_fips_level,
	&dev_attr_lpfc_fips_rev,
	NULL,
};

/**
 * sysfs_ctlreg_write - Write method for writing to ctlreg
 * @filp: open sysfs file
 * @kobj: kernel kobject that contains the kernel class device.
 * @bin_attr: kernel attributes passed to us.
 * @buf: contains the data to be written to the adapter IOREG space.
 * @off: offset into buffer to beginning of data.
 * @count: bytes to transfer.
 *
 * Description:
 * Accessed via /sys/class/scsi_host/hostxxx/ctlreg.
 * Uses the adapter io control registers to send buf contents to the adapter.
 *
 * Returns:
 * -ERANGE off and count combo out of range
 * -EINVAL off, count or buff address invalid
 * -EPERM adapter is offline
 * value of count, buf contents written
 **/
static ssize_t
sysfs_ctlreg_write(struct file *filp, struct kobject *kobj,
		   struct bin_attribute *bin_attr,
		   char *buf, loff_t off, size_t count)
{
	size_t buf_off;
	struct device *dev = container_of(kobj, struct device, kobj);
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	if (phba->sli_rev >= LPFC_SLI_REV4)
		return -EPERM;

	if ((off + count) > FF_REG_AREA_SIZE)
		return -ERANGE;

	if (count <= LPFC_REG_WRITE_KEY_SIZE)
		return 0;

	if (off % 4 || count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	/* This is to protect HBA registers from accidental writes. */
	if (memcmp(buf, LPFC_REG_WRITE_KEY, LPFC_REG_WRITE_KEY_SIZE))
		return -EINVAL;

	if (!(vport->fc_flag & FC_OFFLINE_MODE))
		return -EPERM;

	spin_lock_irq(&phba->hbalock);
	for (buf_off = 0; buf_off < count - LPFC_REG_WRITE_KEY_SIZE;
			buf_off += sizeof(uint32_t))
		writel(*((uint32_t *)(buf + buf_off + LPFC_REG_WRITE_KEY_SIZE)),
		       phba->ctrl_regs_memmap_p + off + buf_off);

	spin_unlock_irq(&phba->hbalock);

	return count;
}

/**
 * sysfs_ctlreg_read - Read method for reading from ctlreg
 * @filp: open sysfs file
 * @kobj: kernel kobject that contains the kernel class device.
 * @bin_attr: kernel attributes passed to us.
 * @buf: if successful contains the data from the adapter IOREG space.
 * @off: offset into buffer to beginning of data.
 * @count: bytes to transfer.
 *
 * Description:
 * Accessed via /sys/class/scsi_host/hostxxx/ctlreg.
 * Uses the adapter io control registers to read data into buf.
 *
 * Returns:
 * -ERANGE off and count combo out of range
 * -EINVAL off, count or buff address invalid
 * value of count, buf contents read
 **/
static ssize_t
sysfs_ctlreg_read(struct file *filp, struct kobject *kobj,
		  struct bin_attribute *bin_attr,
		  char *buf, loff_t off, size_t count)
{
	size_t buf_off;
	uint32_t * tmp_ptr;
	struct device *dev = container_of(kobj, struct device, kobj);
	struct Scsi_Host  *shost = class_to_shost(dev);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	if (phba->sli_rev >= LPFC_SLI_REV4)
		return -EPERM;

	if (off > FF_REG_AREA_SIZE)
		return -ERANGE;

	if ((off + count) > FF_REG_AREA_SIZE)
		count = FF_REG_AREA_SIZE - off;

	if (count == 0) return 0;

	if (off % 4 || count % 4 || (unsigned long)buf % 4)
		return -EINVAL;

	spin_lock_irq(&phba->hbalock);

	for (buf_off = 0; buf_off < count; buf_off += sizeof(uint32_t)) {
		tmp_ptr = (uint32_t *)(buf + buf_off);
		*tmp_ptr = readl(phba->ctrl_regs_memmap_p + off + buf_off);
	}

	spin_unlock_irq(&phba->hbalock);

	return count;
}

static struct bin_attribute sysfs_ctlreg_attr = {
	.attr = {
		.name = "ctlreg",
		.mode = S_IRUSR | S_IWUSR,
	},
	.size = 256,
	.read = sysfs_ctlreg_read,
	.write = sysfs_ctlreg_write,
};

/**
 * sysfs_mbox_write - Write method for writing information via mbox
 * @filp: open sysfs file
 * @kobj: kernel kobject that contains the kernel class device.
 * @bin_attr: kernel attributes passed to us.
 * @buf: contains the data to be written to sysfs mbox.
 * @off: offset into buffer to beginning of data.
 * @count: bytes to transfer.
 *
 * Description:
 * Deprecated function. All mailbox access from user space is performed via the
 * bsg interface.
 *
 * Returns:
 * -EPERM operation not permitted
 **/
static ssize_t
sysfs_mbox_write(struct file *filp, struct kobject *kobj,
		 struct bin_attribute *bin_attr,
		 char *buf, loff_t off, size_t count)
{
	return -EPERM;
}

/**
 * sysfs_mbox_read - Read method for reading information via mbox
 * @filp: open sysfs file
 * @kobj: kernel kobject that contains the kernel class device.
 * @bin_attr: kernel attributes passed to us.
 * @buf: contains the data to be read from sysfs mbox.
 * @off: offset into buffer to beginning of data.
 * @count: bytes to transfer.
 *
 * Description:
 * Deprecated function. All mailbox access from user space is performed via the
 * bsg interface.
 *
 * Returns:
 * -EPERM operation not permitted
 **/
static ssize_t
sysfs_mbox_read(struct file *filp, struct kobject *kobj,
		struct bin_attribute *bin_attr,
		char *buf, loff_t off, size_t count)
{
	return -EPERM;
}

static struct bin_attribute sysfs_mbox_attr = {
	.attr = {
		.name = "mbox",
		.mode = S_IRUSR | S_IWUSR,
	},
	.size = MAILBOX_SYSFS_MAX,
	.read = sysfs_mbox_read,
	.write = sysfs_mbox_write,
};

/**
 * lpfc_alloc_sysfs_attr - Creates the ctlreg and mbox entries
 * @vport: address of lpfc vport structure.
 *
 * Return codes:
 * zero on success
 * error return code from sysfs_create_bin_file()
 **/
int
lpfc_alloc_sysfs_attr(struct lpfc_vport *vport)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	int error;

	error = sysfs_create_bin_file(&shost->shost_dev.kobj,
				      &sysfs_drvr_stat_data_attr);

	/* Virtual ports do not need ctrl_reg and mbox */
	if (error || vport->port_type == LPFC_NPIV_PORT)
		goto out;

	error = sysfs_create_bin_file(&shost->shost_dev.kobj,
				      &sysfs_ctlreg_attr);
	if (error)
		goto out_remove_stat_attr;

	error = sysfs_create_bin_file(&shost->shost_dev.kobj,
				      &sysfs_mbox_attr);
	if (error)
		goto out_remove_ctlreg_attr;

	return 0;
out_remove_ctlreg_attr:
	sysfs_remove_bin_file(&shost->shost_dev.kobj, &sysfs_ctlreg_attr);
out_remove_stat_attr:
	sysfs_remove_bin_file(&shost->shost_dev.kobj,
			&sysfs_drvr_stat_data_attr);
out:
	return error;
}

/**
 * lpfc_free_sysfs_attr - Removes the ctlreg and mbox entries
 * @vport: address of lpfc vport structure.
 **/
void
lpfc_free_sysfs_attr(struct lpfc_vport *vport)
{
	struct Scsi_Host *shost = lpfc_shost_from_vport(vport);
	sysfs_remove_bin_file(&shost->shost_dev.kobj,
		&sysfs_drvr_stat_data_attr);
	/* Virtual ports do not need ctrl_reg and mbox */
	if (vport->port_type == LPFC_NPIV_PORT)
		return;
	sysfs_remove_bin_file(&shost->shost_dev.kobj, &sysfs_mbox_attr);
	sysfs_remove_bin_file(&shost->shost_dev.kobj, &sysfs_ctlreg_attr);
}

/*
 * Dynamic FC Host Attributes Support
 */

/**
 * lpfc_get_host_symbolic_name - Copy symbolic name into the scsi host
 * @shost: kernel scsi host pointer.
 **/
static void
lpfc_get_host_symbolic_name(struct Scsi_Host *shost)
{
	struct lpfc_vport *vport = (struct lpfc_vport *)shost->hostdata;

	lpfc_vport_symbolic_node_name(vport, fc_host_symbolic_name(shost),
				      sizeof fc_host_symbolic_name(shost));
}

/**
 * lpfc_get_host_port_id - Copy the vport DID into the scsi host port id
 * @shost: kernel scsi host pointer.
 **/
static void
lpfc_get_host_port_id(struct Scsi_Host *shost)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;

	/* note: fc_myDID already in cpu endianness */
	fc_host_port_id(shost) = vport->fc_myDID;
}

/**
 * lpfc_get_host_port_type - Set the value of the scsi host port type
 * @shost: kernel scsi host pointer.
 **/
static void
lpfc_get_host_port_type(struct Scsi_Host *shost)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	spin_lock_irq(shost->host_lock);

	if (vport->port_type == LPFC_NPIV_PORT) {
		fc_host_port_type(shost) = FC_PORTTYPE_NPIV;
	} else if (lpfc_is_link_up(phba)) {
		if (phba->fc_topology == LPFC_TOPOLOGY_LOOP) {
			if (vport->fc_flag & FC_PUBLIC_LOOP)
				fc_host_port_type(shost) = FC_PORTTYPE_NLPORT;
			else
				fc_host_port_type(shost) = FC_PORTTYPE_LPORT;
		} else {
			if (vport->fc_flag & FC_FABRIC)
				fc_host_port_type(shost) = FC_PORTTYPE_NPORT;
			else
				fc_host_port_type(shost) = FC_PORTTYPE_PTP;
		}
	} else
		fc_host_port_type(shost) = FC_PORTTYPE_UNKNOWN;

	spin_unlock_irq(shost->host_lock);
}

/**
 * lpfc_get_host_port_state - Set the value of the scsi host port state
 * @shost: kernel scsi host pointer.
 **/
static void
lpfc_get_host_port_state(struct Scsi_Host *shost)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	spin_lock_irq(shost->host_lock);

	if (vport->fc_flag & FC_OFFLINE_MODE)
		fc_host_port_state(shost) = FC_PORTSTATE_OFFLINE;
	else {
		switch (phba->link_state) {
		case LPFC_LINK_UNKNOWN:
		case LPFC_LINK_DOWN:
			fc_host_port_state(shost) = FC_PORTSTATE_LINKDOWN;
			break;
		case LPFC_LINK_UP:
		case LPFC_CLEAR_LA:
		case LPFC_HBA_READY:
			/* Links up, reports port state accordingly */
			if (vport->port_state < LPFC_VPORT_READY)
				fc_host_port_state(shost) =
							FC_PORTSTATE_BYPASSED;
			else
				fc_host_port_state(shost) =
							FC_PORTSTATE_ONLINE;
			break;
		case LPFC_HBA_ERROR:
			fc_host_port_state(shost) = FC_PORTSTATE_ERROR;
			break;
		default:
			fc_host_port_state(shost) = FC_PORTSTATE_UNKNOWN;
			break;
		}
	}

	spin_unlock_irq(shost->host_lock);
}

/**
 * lpfc_get_host_speed - Set the value of the scsi host speed
 * @shost: kernel scsi host pointer.
 **/
static void
lpfc_get_host_speed(struct Scsi_Host *shost)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;

	spin_lock_irq(shost->host_lock);

	if ((lpfc_is_link_up(phba)) && (!(phba->hba_flag & HBA_FCOE_MODE))) {
		switch(phba->fc_linkspeed) {
		case LPFC_LINK_SPEED_1GHZ:
			fc_host_speed(shost) = FC_PORTSPEED_1GBIT;
			break;
		case LPFC_LINK_SPEED_2GHZ:
			fc_host_speed(shost) = FC_PORTSPEED_2GBIT;
			break;
		case LPFC_LINK_SPEED_4GHZ:
			fc_host_speed(shost) = FC_PORTSPEED_4GBIT;
			break;
		case LPFC_LINK_SPEED_8GHZ:
			fc_host_speed(shost) = FC_PORTSPEED_8GBIT;
			break;
		case LPFC_LINK_SPEED_10GHZ:
			fc_host_speed(shost) = FC_PORTSPEED_10GBIT;
			break;
		case LPFC_LINK_SPEED_16GHZ:
			fc_host_speed(shost) = FC_PORTSPEED_16GBIT;
			break;
		case LPFC_LINK_SPEED_32GHZ:
			fc_host_speed(shost) = FC_PORTSPEED_32GBIT;
			break;
		case LPFC_LINK_SPEED_64GHZ:
			fc_host_speed(shost) = FC_PORTSPEED_64GBIT;
			break;
		case LPFC_LINK_SPEED_128GHZ:
			fc_host_speed(shost) = FC_PORTSPEED_128GBIT;
			break;
		default:
			fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;
			break;
		}
	} else if (lpfc_is_link_up(phba) && (phba->hba_flag & HBA_FCOE_MODE)) {
		switch (phba->fc_linkspeed) {
		case LPFC_ASYNC_LINK_SPEED_10GBPS:
			fc_host_speed(shost) = FC_PORTSPEED_10GBIT;
			break;
		case LPFC_ASYNC_LINK_SPEED_25GBPS:
			fc_host_speed(shost) = FC_PORTSPEED_25GBIT;
			break;
		case LPFC_ASYNC_LINK_SPEED_40GBPS:
			fc_host_speed(shost) = FC_PORTSPEED_40GBIT;
			break;
		case LPFC_ASYNC_LINK_SPEED_100GBPS:
			fc_host_speed(shost) = FC_PORTSPEED_100GBIT;
			break;
		default:
			fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;
			break;
		}
	} else
		fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;

	spin_unlock_irq(shost->host_lock);
}

/**
 * lpfc_get_host_fabric_name - Set the value of the scsi host fabric name
 * @shost: kernel scsi host pointer.
 **/
static void
lpfc_get_host_fabric_name (struct Scsi_Host *shost)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	u64 node_name;

	spin_lock_irq(shost->host_lock);

	if ((vport->port_state > LPFC_FLOGI) &&
	    ((vport->fc_flag & FC_FABRIC) ||
	     ((phba->fc_topology == LPFC_TOPOLOGY_LOOP) &&
	      (vport->fc_flag & FC_PUBLIC_LOOP))))
		node_name = wwn_to_u64(phba->fc_fabparam.nodeName.u.wwn);
	else
		/* fabric is local port if there is no F/FL_Port */
		node_name = 0;

	spin_unlock_irq(shost->host_lock);

	fc_host_fabric_name(shost) = node_name;
}

/**
 * lpfc_get_stats - Return statistical information about the adapter
 * @shost: kernel scsi host pointer.
 *
 * Notes:
 * NULL on error for link down, no mbox pool, sli2 active,
 * management not allowed, memory allocation error, or mbox error.
 *
 * Returns:
 * NULL for error
 * address of the adapter host statistics
 **/
static struct fc_host_statistics *
lpfc_get_stats(struct Scsi_Host *shost)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_sli   *psli = &phba->sli;
	struct fc_host_statistics *hs = &phba->link_stats;
	struct lpfc_lnk_stat * lso = &psli->lnk_stat_offsets;
	LPFC_MBOXQ_t *pmboxq;
	MAILBOX_t *pmb;
	int rc = 0;

	/*
	 * prevent udev from issuing mailbox commands until the port is
	 * configured.
	 */
	if (phba->link_state < LPFC_LINK_DOWN ||
	    !phba->mbox_mem_pool ||
	    (phba->sli.sli_flag & LPFC_SLI_ACTIVE) == 0)
		return NULL;

	if (phba->sli.sli_flag & LPFC_BLOCK_MGMT_IO)
		return NULL;

	pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmboxq)
		return NULL;
	memset(pmboxq, 0, sizeof (LPFC_MBOXQ_t));

	pmb = &pmboxq->u.mb;
	pmb->mbxCommand = MBX_READ_STATUS;
	pmb->mbxOwner = OWN_HOST;
	pmboxq->ctx_buf = NULL;
	pmboxq->vport = vport;

	if (vport->fc_flag & FC_OFFLINE_MODE)
		rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_POLL);
	else
		rc = lpfc_sli_issue_mbox_wait(phba, pmboxq, phba->fc_ratov * 2);

	if (rc != MBX_SUCCESS) {
		if (rc != MBX_TIMEOUT)
			mempool_free(pmboxq, phba->mbox_mem_pool);
		return NULL;
	}

	memset(hs, 0, sizeof (struct fc_host_statistics));

	hs->tx_frames = pmb->un.varRdStatus.xmitFrameCnt;
	/*
	 * The MBX_READ_STATUS returns tx_k_bytes which has to
	 * converted to words
	 */
	hs->tx_words = (uint64_t)
			((uint64_t)pmb->un.varRdStatus.xmitByteCnt
			* (uint64_t)256);
	hs->rx_frames = pmb->un.varRdStatus.rcvFrameCnt;
	hs->rx_words = (uint64_t)
			((uint64_t)pmb->un.varRdStatus.rcvByteCnt
			 * (uint64_t)256);

	memset(pmboxq, 0, sizeof (LPFC_MBOXQ_t));
	pmb->mbxCommand = MBX_READ_LNK_STAT;
	pmb->mbxOwner = OWN_HOST;
	pmboxq->ctx_buf = NULL;
	pmboxq->vport = vport;

	if (vport->fc_flag & FC_OFFLINE_MODE)
		rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_POLL);
	else
		rc = lpfc_sli_issue_mbox_wait(phba, pmboxq, phba->fc_ratov * 2);

	if (rc != MBX_SUCCESS) {
		if (rc != MBX_TIMEOUT)
			mempool_free(pmboxq, phba->mbox_mem_pool);
		return NULL;
	}

	hs->link_failure_count = pmb->un.varRdLnk.linkFailureCnt;
	hs->loss_of_sync_count = pmb->un.varRdLnk.lossSyncCnt;
	hs->loss_of_signal_count = pmb->un.varRdLnk.lossSignalCnt;
	hs->prim_seq_protocol_err_count = pmb->un.varRdLnk.primSeqErrCnt;
	hs->invalid_tx_word_count = pmb->un.varRdLnk.invalidXmitWord;
	hs->invalid_crc_count = pmb->un.varRdLnk.crcCnt;
	hs->error_frames = pmb->un.varRdLnk.crcCnt;

	hs->link_failure_count -= lso->link_failure_count;
	hs->loss_of_sync_count -= lso->loss_of_sync_count;
	hs->loss_of_signal_count -= lso->loss_of_signal_count;
	hs->prim_seq_protocol_err_count -= lso->prim_seq_protocol_err_count;
	hs->invalid_tx_word_count -= lso->invalid_tx_word_count;
	hs->invalid_crc_count -= lso->invalid_crc_count;
	hs->error_frames -= lso->error_frames;

	if (phba->hba_flag & HBA_FCOE_MODE) {
		hs->lip_count = -1;
		hs->nos_count = (phba->link_events >> 1);
		hs->nos_count -= lso->link_events;
	} else if (phba->fc_topology == LPFC_TOPOLOGY_LOOP) {
		hs->lip_count = (phba->fc_eventTag >> 1);
		hs->lip_count -= lso->link_events;
		hs->nos_count = -1;
	} else {
		hs->lip_count = -1;
		hs->nos_count = (phba->fc_eventTag >> 1);
		hs->nos_count -= lso->link_events;
	}

	hs->dumped_frames = -1;

	hs->seconds_since_last_reset = ktime_get_seconds() - psli->stats_start;

	mempool_free(pmboxq, phba->mbox_mem_pool);

	return hs;
}

/**
 * lpfc_reset_stats - Copy the adapter link stats information
 * @shost: kernel scsi host pointer.
 **/
static void
lpfc_reset_stats(struct Scsi_Host *shost)
{
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_hba   *phba = vport->phba;
	struct lpfc_sli   *psli = &phba->sli;
	struct lpfc_lnk_stat *lso = &psli->lnk_stat_offsets;
	LPFC_MBOXQ_t *pmboxq;
	MAILBOX_t *pmb;
	int rc = 0;

	if (phba->sli.sli_flag & LPFC_BLOCK_MGMT_IO)
		return;

	pmboxq = mempool_alloc(phba->mbox_mem_pool, GFP_KERNEL);
	if (!pmboxq)
		return;
	memset(pmboxq, 0, sizeof(LPFC_MBOXQ_t));

	pmb = &pmboxq->u.mb;
	pmb->mbxCommand = MBX_READ_STATUS;
	pmb->mbxOwner = OWN_HOST;
	pmb->un.varWords[0] = 0x1; /* reset request */
	pmboxq->ctx_buf = NULL;
	pmboxq->vport = vport;

	if ((vport->fc_flag & FC_OFFLINE_MODE) ||
		(!(psli->sli_flag & LPFC_SLI_ACTIVE)))
		rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_POLL);
	else
		rc = lpfc_sli_issue_mbox_wait(phba, pmboxq, phba->fc_ratov * 2);

	if (rc != MBX_SUCCESS) {
		if (rc != MBX_TIMEOUT)
			mempool_free(pmboxq, phba->mbox_mem_pool);
		return;
	}

	memset(pmboxq, 0, sizeof(LPFC_MBOXQ_t));
	pmb->mbxCommand = MBX_READ_LNK_STAT;
	pmb->mbxOwner = OWN_HOST;
	pmboxq->ctx_buf = NULL;
	pmboxq->vport = vport;

	if ((vport->fc_flag & FC_OFFLINE_MODE) ||
	    (!(psli->sli_flag & LPFC_SLI_ACTIVE)))
		rc = lpfc_sli_issue_mbox(phba, pmboxq, MBX_POLL);
	else
		rc = lpfc_sli_issue_mbox_wait(phba, pmboxq, phba->fc_ratov * 2);

	if (rc != MBX_SUCCESS) {
		if (rc != MBX_TIMEOUT)
			mempool_free( pmboxq, phba->mbox_mem_pool);
		return;
	}

	lso->link_failure_count = pmb->un.varRdLnk.linkFailureCnt;
	lso->loss_of_sync_count = pmb->un.varRdLnk.lossSyncCnt;
	lso->loss_of_signal_count = pmb->un.varRdLnk.lossSignalCnt;
	lso->prim_seq_protocol_err_count = pmb->un.varRdLnk.primSeqErrCnt;
	lso->invalid_tx_word_count = pmb->un.varRdLnk.invalidXmitWord;
	lso->invalid_crc_count = pmb->un.varRdLnk.crcCnt;
	lso->error_frames = pmb->un.varRdLnk.crcCnt;
	if (phba->hba_flag & HBA_FCOE_MODE)
		lso->link_events = (phba->link_events >> 1);
	else
		lso->link_events = (phba->fc_eventTag >> 1);

	psli->stats_start = ktime_get_seconds();

	mempool_free(pmboxq, phba->mbox_mem_pool);

	return;
}

/*
 * The LPFC driver treats linkdown handling as target loss events so there
 * are no sysfs handlers for link_down_tmo.
 */

/**
 * lpfc_get_node_by_target - Return the nodelist for a target
 * @starget: kernel scsi target pointer.
 *
 * Returns:
 * address of the node list if found
 * NULL target not found
 **/
static struct lpfc_nodelist *
lpfc_get_node_by_target(struct scsi_target *starget)
{
	struct Scsi_Host  *shost = dev_to_shost(starget->dev.parent);
	struct lpfc_vport *vport = (struct lpfc_vport *) shost->hostdata;
	struct lpfc_nodelist *ndlp;

	spin_lock_irq(shost->host_lock);
	/* Search for this, mapped, target ID */
	list_for_each_entry(ndlp, &vport->fc_nodes, nlp_listp) {
		if (NLP_CHK_NODE_ACT(ndlp) &&
		    ndlp->nlp_state == NLP_STE_MAPPED_NODE &&
		    starget->id == ndlp->nlp_sid) {
			spin_unlock_irq(shost->host_lock);
			return ndlp;
		}
	}
	spin_unlock_irq(shost->host_lock);
	return NULL;
}

/**
 * lpfc_get_starget_port_id - Set the target port id to the ndlp DID or -1
 * @starget: kernel scsi target pointer.
 **/
static void
lpfc_get_starget_port_id(struct scsi_target *starget)
{
	struct lpfc_nodelist *ndlp = lpfc_get_node_by_target(starget);

	fc_starget_port_id(starget) = ndlp ? ndlp->nlp_DID : -1;
}

/**
 * lpfc_get_starget_node_name - Set the target node name
 * @starget: kernel scsi target pointer.
 *
 * Description: Set the target node name to the ndlp node name wwn or zero.
 **/
static void
lpfc_get_starget_node_name(struct scsi_target *starget)
{
	struct lpfc_nodelist *ndlp = lpfc_get_node_by_target(starget);

	fc_starget_node_name(starget) =
		ndlp ? wwn_to_u64(ndlp->nlp_nodename.u.wwn) : 0;
}

/**
 * lpfc_get_starget_port_name - Set the target port name
 * @starget: kernel scsi target pointer.
 *
 * Description:  set the target port name to the ndlp port name wwn or zero.
 **/
static void
lpfc_get_starget_port_name(struct scsi_target *starget)
{
	struct lpfc_nodelist *ndlp = lpfc_get_node_by_target(starget);

	fc_starget_port_name(starget) =
		ndlp ? wwn_to_u64(ndlp->nlp_portname.u.wwn) : 0;
}

/**
 * lpfc_set_rport_loss_tmo - Set the rport dev loss tmo
 * @rport: fc rport address.
 * @timeout: new value for dev loss tmo.
 *
 * Description:
 * If timeout is non zero set the dev_loss_tmo to timeout, else set
 * dev_loss_tmo to one.
 **/
static void
lpfc_set_rport_loss_tmo(struct fc_rport *rport, uint32_t timeout)
{
	struct lpfc_rport_data *rdata = rport->dd_data;
	struct lpfc_nodelist *ndlp = rdata->pnode;
#if (IS_ENABLED(CONFIG_NVME_FC))
	struct lpfc_nvme_rport *nrport = NULL;
#endif

	if (timeout)
		rport->dev_loss_tmo = timeout;
	else
		rport->dev_loss_tmo = 1;

	if (!ndlp || !NLP_CHK_NODE_ACT(ndlp)) {
		dev_info(&rport->dev, "Cannot find remote node to "
				      "set rport dev loss tmo, port_id x%x\n",
				      rport->port_id);
		return;
	}

#if (IS_ENABLED(CONFIG_NVME_FC))
	nrport = lpfc_ndlp_get_nrport(ndlp);

	if (nrport && nrport->remoteport)
		nvme_fc_set_remoteport_devloss(nrport->remoteport,
					       rport->dev_loss_tmo);
#endif
}

/**
 * lpfc_rport_show_function - Return rport target information
 *
 * Description:
 * Macro that uses field to generate a function with the name lpfc_show_rport_
 *
 * lpfc_show_rport_##field: returns the bytes formatted in buf
 * @cdev: class converted to an fc_rport.
 * @buf: on return contains the target_field or zero.
 *
 * Returns: size of formatted string.
 **/
#define lpfc_rport_show_function(field, format_string, sz, cast)	\
static ssize_t								\
lpfc_show_rport_##field (struct device *dev,				\
			 struct device_attribute *attr,			\
			 char *buf)					\
{									\
	struct fc_rport *rport = transport_class_to_rport(dev);		\
	struct lpfc_rport_data *rdata = rport->hostdata;		\
	return scnprintf(buf, sz, format_string,			\
		(rdata->target) ? cast rdata->target->field : 0);	\
}

#define lpfc_rport_rd_attr(field, format_string, sz)			\
	lpfc_rport_show_function(field, format_string, sz, )		\
static FC_RPORT_ATTR(field, S_IRUGO, lpfc_show_rport_##field, NULL)

/**
 * lpfc_set_vport_symbolic_name - Set the vport's symbolic name
 * @fc_vport: The fc_vport who's symbolic name has been changed.
 *
 * Description:
 * This function is called by the transport after the @fc_vport's symbolic name
 * has been changed. This function re-registers the symbolic name with the
 * switch to propagate the change into the fabric if the vport is active.
 **/
static void
lpfc_set_vport_symbolic_name(struct fc_vport *fc_vport)
{
	struct lpfc_vport *vport = *(struct lpfc_vport **)fc_vport->dd_data;

	if (vport->port_state == LPFC_VPORT_READY)
		lpfc_ns_cmd(vport, SLI_CTNS_RSPN_ID, 0, 0);
}

/**
 * lpfc_hba_log_verbose_init - Set hba's log verbose level
 * @phba: Pointer to lpfc_hba struct.
 *
 * This function is called by the lpfc_get_cfgparam() routine to set the
 * module lpfc_log_verbose into the @phba cfg_log_verbose for use with
 * log message according to the module's lpfc_log_verbose parameter setting
 * before hba port or vport created.
 **/
static void
lpfc_hba_log_verbose_init(struct lpfc_hba *phba, uint32_t verbose)
{
	phba->cfg_log_verbose = verbose;
}

struct fc_function_template lpfc_transport_functions = {
	/* fixed attributes the driver supports */
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_fc4s = 1,
	.show_host_supported_speeds = 1,
	.show_host_maxframe_size = 1,

	.get_host_symbolic_name = lpfc_get_host_symbolic_name,
	.show_host_symbolic_name = 1,

	/* dynamic attributes the driver supports */
	.get_host_port_id = lpfc_get_host_port_id,
	.show_host_port_id = 1,

	.get_host_port_type = lpfc_get_host_port_type,
	.show_host_port_type = 1,

	.get_host_port_state = lpfc_get_host_port_state,
	.show_host_port_state = 1,

	/* active_fc4s is shown but doesn't change (thus no get function) */
	.show_host_active_fc4s = 1,

	.get_host_speed = lpfc_get_host_speed,
	.show_host_speed = 1,

	.get_host_fabric_name = lpfc_get_host_fabric_name,
	.show_host_fabric_name = 1,

	/*
	 * The LPFC driver treats linkdown handling as target loss events
	 * so there are no sysfs handlers for link_down_tmo.
	 */

	.get_fc_host_stats = lpfc_get_stats,
	.reset_fc_host_stats = lpfc_reset_stats,

	.dd_fcrport_size = sizeof(struct lpfc_rport_data),
	.show_rport_maxframe_size = 1,
	.show_rport_supported_classes = 1,

	.set_rport_dev_loss_tmo = lpfc_set_rport_loss_tmo,
	.show_rport_dev_loss_tmo = 1,

	.get_starget_port_id  = lpfc_get_starget_port_id,
	.show_starget_port_id = 1,

	.get_starget_node_name = lpfc_get_starget_node_name,
	.show_starget_node_name = 1,

	.get_starget_port_name = lpfc_get_starget_port_name,
	.show_starget_port_name = 1,

	.issue_fc_host_lip = lpfc_issue_lip,
	.dev_loss_tmo_callbk = lpfc_dev_loss_tmo_callbk,
	.terminate_rport_io = lpfc_terminate_rport_io,

	.dd_fcvport_size = sizeof(struct lpfc_vport *),

	.vport_disable = lpfc_vport_disable,

	.set_vport_symbolic_name = lpfc_set_vport_symbolic_name,

	.bsg_request = lpfc_bsg_request,
	.bsg_timeout = lpfc_bsg_timeout,
};

struct fc_function_template lpfc_vport_transport_functions = {
	/* fixed attributes the driver supports */
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_fc4s = 1,
	.show_host_supported_speeds = 1,
	.show_host_maxframe_size = 1,

	.get_host_symbolic_name = lpfc_get_host_symbolic_name,
	.show_host_symbolic_name = 1,

	/* dynamic attributes the driver supports */
	.get_host_port_id = lpfc_get_host_port_id,
	.show_host_port_id = 1,

	.get_host_port_type = lpfc_get_host_port_type,
	.show_host_port_type = 1,

	.get_host_port_state = lpfc_get_host_port_state,
	.show_host_port_state = 1,

	/* active_fc4s is shown but doesn't change (thus no get function) */
	.show_host_active_fc4s = 1,

	.get_host_speed = lpfc_get_host_speed,
	.show_host_speed = 1,

	.get_host_fabric_name = lpfc_get_host_fabric_name,
	.show_host_fabric_name = 1,

	/*
	 * The LPFC driver treats linkdown handling as target loss events
	 * so there are no sysfs handlers for link_down_tmo.
	 */

	.get_fc_host_stats = lpfc_get_stats,
	.reset_fc_host_stats = lpfc_reset_stats,

	.dd_fcrport_size = sizeof(struct lpfc_rport_data),
	.show_rport_maxframe_size = 1,
	.show_rport_supported_classes = 1,

	.set_rport_dev_loss_tmo = lpfc_set_rport_loss_tmo,
	.show_rport_dev_loss_tmo = 1,

	.get_starget_port_id  = lpfc_get_starget_port_id,
	.show_starget_port_id = 1,

	.get_starget_node_name = lpfc_get_starget_node_name,
	.show_starget_node_name = 1,

	.get_starget_port_name = lpfc_get_starget_port_name,
	.show_starget_port_name = 1,

	.dev_loss_tmo_callbk = lpfc_dev_loss_tmo_callbk,
	.terminate_rport_io = lpfc_terminate_rport_io,

	.vport_disable = lpfc_vport_disable,

	.set_vport_symbolic_name = lpfc_set_vport_symbolic_name,
};

/**
 * lpfc_get_hba_function_mode - Used to determine the HBA function in FCoE
 * Mode
 * @phba: lpfc_hba pointer.
 **/
static void
lpfc_get_hba_function_mode(struct lpfc_hba *phba)
{
	/* If the adapter supports FCoE mode */
	switch (phba->pcidev->device) {
	case PCI_DEVICE_ID_SKYHAWK:
	case PCI_DEVICE_ID_SKYHAWK_VF:
	case PCI_DEVICE_ID_LANCER_FCOE:
	case PCI_DEVICE_ID_LANCER_FCOE_VF:
	case PCI_DEVICE_ID_ZEPHYR_DCSP:
	case PCI_DEVICE_ID_HORNET:
	case PCI_DEVICE_ID_TIGERSHARK:
	case PCI_DEVICE_ID_TOMCAT:
		phba->hba_flag |= HBA_FCOE_MODE;
		break;
	default:
	/* for others, clear the flag */
		phba->hba_flag &= ~HBA_FCOE_MODE;
	}
}

/**
 * lpfc_get_cfgparam - Used during probe_one to init the adapter structure
 * @phba: lpfc_hba pointer.
 **/
void
lpfc_get_cfgparam(struct lpfc_hba *phba)
{
	lpfc_hba_log_verbose_init(phba, lpfc_log_verbose);
	lpfc_fcp_io_sched_init(phba, lpfc_fcp_io_sched);
	lpfc_ns_query_init(phba, lpfc_ns_query);
	lpfc_fcp2_no_tgt_reset_init(phba, lpfc_fcp2_no_tgt_reset);
	lpfc_cr_delay_init(phba, lpfc_cr_delay);
	lpfc_cr_count_init(phba, lpfc_cr_count);
	lpfc_multi_ring_support_init(phba, lpfc_multi_ring_support);
	lpfc_multi_ring_rctl_init(phba, lpfc_multi_ring_rctl);
	lpfc_multi_ring_type_init(phba, lpfc_multi_ring_type);
	lpfc_ack0_init(phba, lpfc_ack0);
	lpfc_xri_rebalancing_init(phba, lpfc_xri_rebalancing);
	lpfc_topology_init(phba, lpfc_topology);
	lpfc_link_speed_init(phba, lpfc_link_speed);
	lpfc_poll_tmo_init(phba, lpfc_poll_tmo);
	lpfc_task_mgmt_tmo_init(phba, lpfc_task_mgmt_tmo);
	lpfc_enable_npiv_init(phba, lpfc_enable_npiv);
	lpfc_fcf_failover_policy_init(phba, lpfc_fcf_failover_policy);
	lpfc_enable_rrq_init(phba, lpfc_enable_rrq);
	lpfc_fdmi_on_init(phba, lpfc_fdmi_on);
	lpfc_enable_SmartSAN_init(phba, lpfc_enable_SmartSAN);
	lpfc_use_msi_init(phba, lpfc_use_msi);
	lpfc_nvme_oas_init(phba, lpfc_nvme_oas);
	lpfc_nvme_embed_cmd_init(phba, lpfc_nvme_embed_cmd);
	lpfc_fcp_imax_init(phba, lpfc_fcp_imax);
	lpfc_force_rscn_init(phba, lpfc_force_rscn);
	lpfc_cq_poll_threshold_init(phba, lpfc_cq_poll_threshold);
	lpfc_cq_max_proc_limit_init(phba, lpfc_cq_max_proc_limit);
	lpfc_fcp_cpu_map_init(phba, lpfc_fcp_cpu_map);
	lpfc_enable_hba_reset_init(phba, lpfc_enable_hba_reset);
	lpfc_enable_hba_heartbeat_init(phba, lpfc_enable_hba_heartbeat);

	lpfc_EnableXLane_init(phba, lpfc_EnableXLane);
	if (phba->sli_rev != LPFC_SLI_REV4)
		phba->cfg_EnableXLane = 0;
	lpfc_XLanePriority_init(phba, lpfc_XLanePriority);

	memset(phba->cfg_oas_tgt_wwpn, 0, (8 * sizeof(uint8_t)));
	memset(phba->cfg_oas_vpt_wwpn, 0, (8 * sizeof(uint8_t)));
	phba->cfg_oas_lun_state = 0;
	phba->cfg_oas_lun_status = 0;
	phba->cfg_oas_flags = 0;
	phba->cfg_oas_priority = 0;
	lpfc_enable_bg_init(phba, lpfc_enable_bg);
	lpfc_prot_mask_init(phba, lpfc_prot_mask);
	lpfc_prot_guard_init(phba, lpfc_prot_guard);
	if (phba->sli_rev == LPFC_SLI_REV4)
		phba->cfg_poll = 0;
	else
		phba->cfg_poll = lpfc_poll;

	/* Get the function mode */
	lpfc_get_hba_function_mode(phba);

	/* BlockGuard allowed for FC only. */
	if (phba->cfg_enable_bg && phba->hba_flag & HBA_FCOE_MODE) {
		lpfc_printf_log(phba, KERN_INFO, LOG_INIT,
				"0581 BlockGuard feature not supported\n");
		/* If set, clear the BlockGuard support param */
		phba->cfg_enable_bg = 0;
	} else if (phba->cfg_enable_bg) {
		phba->sli3_options |= LPFC_SLI3_BG_ENABLED;
	}

	lpfc_suppress_rsp_init(phba, lpfc_suppress_rsp);

	lpfc_enable_fc4_type_init(phba, lpfc_enable_fc4_type);
	lpfc_nvmet_mrq_init(phba, lpfc_nvmet_mrq);
	lpfc_nvmet_mrq_post_init(phba, lpfc_nvmet_mrq_post);

	/* Initialize first burst. Target vs Initiator are different. */
	lpfc_nvme_enable_fb_init(phba, lpfc_nvme_enable_fb);
	lpfc_nvmet_fb_size_init(phba, lpfc_nvmet_fb_size);
	lpfc_fcp_mq_threshold_init(phba, lpfc_fcp_mq_threshold);
	lpfc_hdw_queue_init(phba, lpfc_hdw_queue);
	lpfc_irq_chann_init(phba, lpfc_irq_chann);
	lpfc_enable_bbcr_init(phba, lpfc_enable_bbcr);
	lpfc_enable_dpp_init(phba, lpfc_enable_dpp);

	if (phba->sli_rev != LPFC_SLI_REV4) {
		/* NVME only supported on SLI4 */
		phba->nvmet_support = 0;
		phba->cfg_nvmet_mrq = 0;
		phba->cfg_enable_fc4_type = LPFC_ENABLE_FCP;
		phba->cfg_enable_bbcr = 0;
		phba->cfg_xri_rebalancing = 0;
	} else {
		/* We MUST have FCP support */
		if (!(phba->cfg_enable_fc4_type & LPFC_ENABLE_FCP))
			phba->cfg_enable_fc4_type |= LPFC_ENABLE_FCP;
	}

	phba->cfg_auto_imax = (phba->cfg_fcp_imax) ? 0 : 1;

	phba->cfg_enable_pbde = 0;

	/* A value of 0 means use the number of CPUs found in the system */
	if (phba->cfg_hdw_queue == 0)
		phba->cfg_hdw_queue = phba->sli4_hba.num_present_cpu;
	if (phba->cfg_irq_chann == 0)
		phba->cfg_irq_chann = phba->sli4_hba.num_present_cpu;
	if (phba->cfg_irq_chann > phba->cfg_hdw_queue)
		phba->cfg_irq_chann = phba->cfg_hdw_queue;

	phba->cfg_soft_wwnn = 0L;
	phba->cfg_soft_wwpn = 0L;
	lpfc_sg_seg_cnt_init(phba, lpfc_sg_seg_cnt);
	lpfc_hba_queue_depth_init(phba, lpfc_hba_queue_depth);
	lpfc_aer_support_init(phba, lpfc_aer_support);
	lpfc_sriov_nr_virtfn_init(phba, lpfc_sriov_nr_virtfn);
	lpfc_request_firmware_upgrade_init(phba, lpfc_req_fw_upgrade);
	lpfc_suppress_link_up_init(phba, lpfc_suppress_link_up);
	lpfc_delay_discovery_init(phba, lpfc_delay_discovery);
	lpfc_sli_mode_init(phba, lpfc_sli_mode);
	phba->cfg_enable_dss = 1;
	lpfc_enable_mds_diags_init(phba, lpfc_enable_mds_diags);
	lpfc_ras_fwlog_buffsize_init(phba, lpfc_ras_fwlog_buffsize);
	lpfc_ras_fwlog_level_init(phba, lpfc_ras_fwlog_level);
	lpfc_ras_fwlog_func_init(phba, lpfc_ras_fwlog_func);

	return;
}

/**
 * lpfc_nvme_mod_param_dep - Adjust module parameter value based on
 * dependencies between protocols and roles.
 * @phba: lpfc_hba pointer.
 **/
void
lpfc_nvme_mod_param_dep(struct lpfc_hba *phba)
{
	if (phba->cfg_hdw_queue > phba->sli4_hba.num_present_cpu)
		phba->cfg_hdw_queue = phba->sli4_hba.num_present_cpu;
	if (phba->cfg_irq_chann > phba->sli4_hba.num_present_cpu)
		phba->cfg_irq_chann = phba->sli4_hba.num_present_cpu;
	if (phba->cfg_irq_chann > phba->cfg_hdw_queue)
		phba->cfg_irq_chann = phba->cfg_hdw_queue;

	if (phba->cfg_enable_fc4_type & LPFC_ENABLE_NVME &&
	    phba->nvmet_support) {
		phba->cfg_enable_fc4_type &= ~LPFC_ENABLE_FCP;

		lpfc_printf_log(phba, KERN_INFO, LOG_NVME_DISC,
				"6013 %s x%x fb_size x%x, fb_max x%x\n",
				"NVME Target PRLI ACC enable_fb ",
				phba->cfg_nvme_enable_fb,
				phba->cfg_nvmet_fb_size,
				LPFC_NVMET_FB_SZ_MAX);

		if (phba->cfg_nvme_enable_fb == 0)
			phba->cfg_nvmet_fb_size = 0;
		else {
			if (phba->cfg_nvmet_fb_size > LPFC_NVMET_FB_SZ_MAX)
				phba->cfg_nvmet_fb_size = LPFC_NVMET_FB_SZ_MAX;
		}

		if (!phba->cfg_nvmet_mrq)
			phba->cfg_nvmet_mrq = phba->cfg_hdw_queue;

		/* Adjust lpfc_nvmet_mrq to avoid running out of WQE slots */
		if (phba->cfg_nvmet_mrq > phba->cfg_hdw_queue) {
			phba->cfg_nvmet_mrq = phba->cfg_hdw_queue;
			lpfc_printf_log(phba, KERN_ERR, LOG_NVME_DISC,
					"6018 Adjust lpfc_nvmet_mrq to %d\n",
					phba->cfg_nvmet_mrq);
		}
		if (phba->cfg_nvmet_mrq > LPFC_NVMET_MRQ_MAX)
			phba->cfg_nvmet_mrq = LPFC_NVMET_MRQ_MAX;

	} else {
		/* Not NVME Target mode.  Turn off Target parameters. */
		phba->nvmet_support = 0;
		phba->cfg_nvmet_mrq = 0;
		phba->cfg_nvmet_fb_size = 0;
	}
}

/**
 * lpfc_get_vport_cfgparam - Used during port create, init the vport structure
 * @vport: lpfc_vport pointer.
 **/
void
lpfc_get_vport_cfgparam(struct lpfc_vport *vport)
{
	lpfc_log_verbose_init(vport, lpfc_log_verbose);
	lpfc_lun_queue_depth_init(vport, lpfc_lun_queue_depth);
	lpfc_tgt_queue_depth_init(vport, lpfc_tgt_queue_depth);
	lpfc_devloss_tmo_init(vport, lpfc_devloss_tmo);
	lpfc_nodev_tmo_init(vport, lpfc_nodev_tmo);
	lpfc_peer_port_login_init(vport, lpfc_peer_port_login);
	lpfc_restrict_login_init(vport, lpfc_restrict_login);
	lpfc_fcp_class_init(vport, lpfc_fcp_class);
	lpfc_use_adisc_init(vport, lpfc_use_adisc);
	lpfc_first_burst_size_init(vport, lpfc_first_burst_size);
	lpfc_max_scsicmpl_time_init(vport, lpfc_max_scsicmpl_time);
	lpfc_discovery_threads_init(vport, lpfc_discovery_threads);
	lpfc_max_luns_init(vport, lpfc_max_luns);
	lpfc_scan_down_init(vport, lpfc_scan_down);
	lpfc_enable_da_id_init(vport, lpfc_enable_da_id);
	return;
}
