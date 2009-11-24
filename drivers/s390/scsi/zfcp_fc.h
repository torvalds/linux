/*
 * zfcp device driver
 *
 * Fibre Channel related definitions and inline functions for the zfcp
 * device driver
 *
 * Copyright IBM Corporation 2009
 */

#ifndef ZFCP_FC_H
#define ZFCP_FC_H

#include <scsi/fc/fc_els.h>
#include <scsi/fc/fc_fcp.h>
#include <scsi/fc/fc_ns.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_tcq.h>

#define ZFCP_FC_CT_SIZE_PAGE	  (PAGE_SIZE - sizeof(struct fc_ct_hdr))
#define ZFCP_FC_GPN_FT_ENT_PAGE	  (ZFCP_FC_CT_SIZE_PAGE \
					/ sizeof(struct fc_gpn_ft_resp))
#define ZFCP_FC_GPN_FT_NUM_BUFS	  4 /* memory pages */

#define ZFCP_FC_GPN_FT_MAX_SIZE	  (ZFCP_FC_GPN_FT_NUM_BUFS * PAGE_SIZE \
					- sizeof(struct fc_ct_hdr))
#define ZFCP_FC_GPN_FT_MAX_ENT	  (ZFCP_FC_GPN_FT_NUM_BUFS * \
					(ZFCP_FC_GPN_FT_ENT_PAGE + 1))

/**
 * struct zfcp_fc_gid_pn_req - container for ct header plus gid_pn request
 * @ct_hdr: FC GS common transport header
 * @gid_pn: GID_PN request
 */
struct zfcp_fc_gid_pn_req {
	struct fc_ct_hdr	ct_hdr;
	struct fc_ns_gid_pn	gid_pn;
} __packed;

/**
 * struct zfcp_fc_gid_pn_resp - container for ct header plus gid_pn response
 * @ct_hdr: FC GS common transport header
 * @gid_pn: GID_PN response
 */
struct zfcp_fc_gid_pn_resp {
	struct fc_ct_hdr	ct_hdr;
	struct fc_gid_pn_resp	gid_pn;
} __packed;

/**
 * struct zfcp_fc_gid_pn - everything required in zfcp for gid_pn request
 * @ct: data passed to zfcp_fsf for issuing fsf request
 * @sg_req: scatterlist entry for request data
 * @sg_resp: scatterlist entry for response data
 * @gid_pn_req: GID_PN request data
 * @gid_pn_resp: GID_PN response data
 */
struct zfcp_fc_gid_pn {
	struct zfcp_send_ct ct;
	struct scatterlist sg_req;
	struct scatterlist sg_resp;
	struct zfcp_fc_gid_pn_req gid_pn_req;
	struct zfcp_fc_gid_pn_resp gid_pn_resp;
	struct zfcp_port *port;
};

/**
 * struct zfcp_fc_gpn_ft - container for ct header plus gpn_ft request
 * @ct_hdr: FC GS common transport header
 * @gpn_ft: GPN_FT request
 */
struct zfcp_fc_gpn_ft_req {
	struct fc_ct_hdr	ct_hdr;
	struct fc_ns_gid_ft	gpn_ft;
} __packed;

/**
 * struct zfcp_fc_gpn_ft_resp - container for ct header plus gpn_ft response
 * @ct_hdr: FC GS common transport header
 * @gpn_ft: Array of gpn_ft response data to fill one memory page
 */
struct zfcp_fc_gpn_ft_resp {
	struct fc_ct_hdr	ct_hdr;
	struct fc_gpn_ft_resp	gpn_ft[ZFCP_FC_GPN_FT_ENT_PAGE];
} __packed;

/**
 * struct zfcp_fc_gpn_ft - zfcp data for gpn_ft request
 * @ct: data passed to zfcp_fsf for issuing fsf request
 * @sg_req: scatter list entry for gpn_ft request
 * @sg_resp: scatter list entries for gpn_ft responses (per memory page)
 */
struct zfcp_fc_gpn_ft {
	struct zfcp_send_ct ct;
	struct scatterlist sg_req;
	struct scatterlist sg_resp[ZFCP_FC_GPN_FT_NUM_BUFS];
};

/**
 * struct zfcp_fc_els_adisc - everything required in zfcp for issuing ELS ADISC
 * @els: data required for issuing els fsf command
 * @req: scatterlist entry for ELS ADISC request
 * @resp: scatterlist entry for ELS ADISC response
 * @adisc_req: ELS ADISC request data
 * @adisc_resp: ELS ADISC response data
 */
struct zfcp_fc_els_adisc {
	struct zfcp_send_els els;
	struct scatterlist req;
	struct scatterlist resp;
	struct fc_els_adisc adisc_req;
	struct fc_els_adisc adisc_resp;
};

/**
 * zfcp_fc_scsi_to_fcp - setup FCP command with data from scsi_cmnd
 * @fcp: fcp_cmnd to setup
 * @scsi: scsi_cmnd where to get LUN, task attributes/flags and CDB
 */
static inline
void zfcp_fc_scsi_to_fcp(struct fcp_cmnd *fcp, struct scsi_cmnd *scsi)
{
	char tag[2];

	int_to_scsilun(scsi->device->lun, (struct scsi_lun *) &fcp->fc_lun);

	if (scsi_populate_tag_msg(scsi, tag)) {
		switch (tag[0]) {
		case MSG_ORDERED_TAG:
			fcp->fc_pri_ta |= FCP_PTA_ORDERED;
			break;
		case MSG_SIMPLE_TAG:
			fcp->fc_pri_ta |= FCP_PTA_SIMPLE;
			break;
		};
	} else
		fcp->fc_pri_ta = FCP_PTA_SIMPLE;

	if (scsi->sc_data_direction == DMA_FROM_DEVICE)
		fcp->fc_flags |= FCP_CFL_RDDATA;
	if (scsi->sc_data_direction == DMA_TO_DEVICE)
		fcp->fc_flags |= FCP_CFL_WRDATA;

	memcpy(fcp->fc_cdb, scsi->cmnd, scsi->cmd_len);

	fcp->fc_dl = scsi_bufflen(scsi);
}

/**
 * zfcp_fc_fcp_tm - setup FCP command as task management command
 * @fcp: fcp_cmnd to setup
 * @dev: scsi_device where to send the task management command
 * @tm: task management flags to setup tm command
 */
static inline
void zfcp_fc_fcp_tm(struct fcp_cmnd *fcp, struct scsi_device *dev, u8 tm_flags)
{
	int_to_scsilun(dev->lun, (struct scsi_lun *) &fcp->fc_lun);
	fcp->fc_tm_flags |= tm_flags;
}

/**
 * zfcp_fc_evap_fcp_rsp - evaluate FCP RSP IU and update scsi_cmnd accordingly
 * @fcp_rsp: FCP RSP IU to evaluate
 * @scsi: SCSI command where to update status and sense buffer
 */
static inline
void zfcp_fc_eval_fcp_rsp(struct fcp_resp_with_ext *fcp_rsp,
			  struct scsi_cmnd *scsi)
{
	struct fcp_resp_rsp_info *rsp_info;
	char *sense;
	u32 sense_len, resid;
	u8 rsp_flags;

	set_msg_byte(scsi, COMMAND_COMPLETE);
	scsi->result |= fcp_rsp->resp.fr_status;

	rsp_flags = fcp_rsp->resp.fr_flags;

	if (unlikely(rsp_flags & FCP_RSP_LEN_VAL)) {
		rsp_info = (struct fcp_resp_rsp_info *) &fcp_rsp[1];
		if (rsp_info->rsp_code == FCP_TMF_CMPL)
			set_host_byte(scsi, DID_OK);
		else {
			set_host_byte(scsi, DID_ERROR);
			return;
		}
	}

	if (unlikely(rsp_flags & FCP_SNS_LEN_VAL)) {
		sense = (char *) &fcp_rsp[1];
		if (rsp_flags & FCP_RSP_LEN_VAL)
			sense += fcp_rsp->ext.fr_sns_len;
		sense_len = min(fcp_rsp->ext.fr_sns_len,
				(u32) SCSI_SENSE_BUFFERSIZE);
		memcpy(scsi->sense_buffer, sense, sense_len);
	}

	if (unlikely(rsp_flags & FCP_RESID_UNDER)) {
		resid = fcp_rsp->ext.fr_resid;
		scsi_set_resid(scsi, resid);
		if (scsi_bufflen(scsi) - resid < scsi->underflow &&
		     !(rsp_flags & FCP_SNS_LEN_VAL) &&
		     fcp_rsp->resp.fr_status == SAM_STAT_GOOD)
			set_host_byte(scsi, DID_ERROR);
	}
}

#endif
