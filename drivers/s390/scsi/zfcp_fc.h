/*
 * zfcp device driver
 *
 * Fibre Channel related definitions and inline functions for the zfcp
 * device driver
 *
 * Copyright IBM Corp. 2009, 2017
 */

#ifndef ZFCP_FC_H
#define ZFCP_FC_H

#include <scsi/fc/fc_els.h>
#include <scsi/fc/fc_fcp.h>
#include <scsi/fc/fc_ns.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_tcq.h>
#include "zfcp_fsf.h"

#define ZFCP_FC_CT_SIZE_PAGE	  (PAGE_SIZE - sizeof(struct fc_ct_hdr))
#define ZFCP_FC_GPN_FT_ENT_PAGE	  (ZFCP_FC_CT_SIZE_PAGE \
					/ sizeof(struct fc_gpn_ft_resp))
#define ZFCP_FC_GPN_FT_NUM_BUFS	  4 /* memory pages */

#define ZFCP_FC_GPN_FT_MAX_SIZE	  (ZFCP_FC_GPN_FT_NUM_BUFS * PAGE_SIZE \
					- sizeof(struct fc_ct_hdr))
#define ZFCP_FC_GPN_FT_MAX_ENT	  (ZFCP_FC_GPN_FT_NUM_BUFS * \
					(ZFCP_FC_GPN_FT_ENT_PAGE + 1))

#define ZFCP_FC_CTELS_TMO	(2 * FC_DEF_R_A_TOV / 1000)

/**
 * struct zfcp_fc_event - FC HBAAPI event for internal queueing from irq context
 * @code: Event code
 * @data: Event data
 * @list: list_head for zfcp_fc_events list
 */
struct zfcp_fc_event {
	enum fc_host_event_code code;
	u32 data;
	struct list_head list;
};

/**
 * struct zfcp_fc_events - Infrastructure for posting FC events from irq context
 * @list: List for queueing of events from irq context to workqueue
 * @list_lock: Lock for event list
 * @work: work_struct for forwarding events in workqueue
*/
struct zfcp_fc_events {
	struct list_head list;
	spinlock_t list_lock;
	struct work_struct work;
};

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
 * struct zfcp_fc_gid_pn_rsp - container for ct header plus gid_pn response
 * @ct_hdr: FC GS common transport header
 * @gid_pn: GID_PN response
 */
struct zfcp_fc_gid_pn_rsp {
	struct fc_ct_hdr	ct_hdr;
	struct fc_gid_pn_resp	gid_pn;
} __packed;

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
 * struct zfcp_fc_gspn_req - container for ct header plus GSPN_ID request
 * @ct_hdr: FC GS common transport header
 * @gspn: GSPN_ID request
 */
struct zfcp_fc_gspn_req {
	struct fc_ct_hdr	ct_hdr;
	struct fc_gid_pn_resp	gspn;
} __packed;

/**
 * struct zfcp_fc_gspn_rsp - container for ct header plus GSPN_ID response
 * @ct_hdr: FC GS common transport header
 * @gspn: GSPN_ID response
 * @name: The name string of the GSPN_ID response
 */
struct zfcp_fc_gspn_rsp {
	struct fc_ct_hdr	ct_hdr;
	struct fc_gspn_resp	gspn;
	char			name[FC_SYMBOLIC_NAME_SIZE];
} __packed;

/**
 * struct zfcp_fc_rspn_req - container for ct header plus RSPN_ID request
 * @ct_hdr: FC GS common transport header
 * @rspn: RSPN_ID request
 * @name: The name string of the RSPN_ID request
 */
struct zfcp_fc_rspn_req {
	struct fc_ct_hdr	ct_hdr;
	struct fc_ns_rspn	rspn;
	char			name[FC_SYMBOLIC_NAME_SIZE];
} __packed;

/**
 * struct zfcp_fc_req - Container for FC ELS and CT requests sent from zfcp
 * @ct_els: data required for issuing fsf command
 * @sg_req: scatterlist entry for request data
 * @sg_rsp: scatterlist entry for response data
 * @u: request specific data
 */
struct zfcp_fc_req {
	struct zfcp_fsf_ct_els				ct_els;
	struct scatterlist				sg_req;
	struct scatterlist				sg_rsp;
	union {
		struct {
			struct fc_els_adisc		req;
			struct fc_els_adisc		rsp;
		} adisc;
		struct {
			struct zfcp_fc_gid_pn_req	req;
			struct zfcp_fc_gid_pn_rsp	rsp;
		} gid_pn;
		struct {
			struct scatterlist sg_rsp2[ZFCP_FC_GPN_FT_NUM_BUFS - 1];
			struct zfcp_fc_gpn_ft_req	req;
		} gpn_ft;
		struct {
			struct zfcp_fc_gspn_req		req;
			struct zfcp_fc_gspn_rsp		rsp;
		} gspn;
		struct {
			struct zfcp_fc_rspn_req		req;
			struct fc_ct_hdr		rsp;
		} rspn;
	} u;
};

/**
 * enum zfcp_fc_wka_status - FC WKA port status in zfcp
 * @ZFCP_FC_WKA_PORT_OFFLINE: Port is closed and not in use
 * @ZFCP_FC_WKA_PORT_CLOSING: The FSF "close port" request is pending
 * @ZFCP_FC_WKA_PORT_OPENING: The FSF "open port" request is pending
 * @ZFCP_FC_WKA_PORT_ONLINE: The port is open and the port handle is valid
 */
enum zfcp_fc_wka_status {
	ZFCP_FC_WKA_PORT_OFFLINE,
	ZFCP_FC_WKA_PORT_CLOSING,
	ZFCP_FC_WKA_PORT_OPENING,
	ZFCP_FC_WKA_PORT_ONLINE,
};

/**
 * struct zfcp_fc_wka_port - representation of well-known-address (WKA) FC port
 * @adapter: Pointer to adapter structure this WKA port belongs to
 * @completion_wq: Wait for completion of open/close command
 * @status: Current status of WKA port
 * @refcount: Reference count to keep port open as long as it is in use
 * @d_id: FC destination id or well-known-address
 * @handle: FSF handle for the open WKA port
 * @mutex: Mutex used during opening/closing state changes
 * @work: For delaying the closing of the WKA port
 */
struct zfcp_fc_wka_port {
	struct zfcp_adapter	*adapter;
	wait_queue_head_t	completion_wq;
	enum zfcp_fc_wka_status	status;
	atomic_t		refcount;
	u32			d_id;
	u32			handle;
	struct mutex		mutex;
	struct delayed_work	work;
};

/**
 * struct zfcp_fc_wka_ports - Data structures for FC generic services
 * @ms: FC Management service
 * @ts: FC time service
 * @ds: FC directory service
 * @as: FC alias service
 */
struct zfcp_fc_wka_ports {
	struct zfcp_fc_wka_port ms;
	struct zfcp_fc_wka_port ts;
	struct zfcp_fc_wka_port ds;
	struct zfcp_fc_wka_port as;
};

/**
 * zfcp_fc_scsi_to_fcp - setup FCP command with data from scsi_cmnd
 * @fcp: fcp_cmnd to setup
 * @scsi: scsi_cmnd where to get LUN, task attributes/flags and CDB
 * @tm: task management flags to setup task management command
 */
static inline
void zfcp_fc_scsi_to_fcp(struct fcp_cmnd *fcp, struct scsi_cmnd *scsi,
			 u8 tm_flags)
{
	u32 datalen;

	int_to_scsilun(scsi->device->lun, (struct scsi_lun *) &fcp->fc_lun);

	if (unlikely(tm_flags)) {
		fcp->fc_tm_flags = tm_flags;
		return;
	}

	fcp->fc_pri_ta = FCP_PTA_SIMPLE;

	if (scsi->sc_data_direction == DMA_FROM_DEVICE)
		fcp->fc_flags |= FCP_CFL_RDDATA;
	if (scsi->sc_data_direction == DMA_TO_DEVICE)
		fcp->fc_flags |= FCP_CFL_WRDATA;

	memcpy(fcp->fc_cdb, scsi->cmnd, scsi->cmd_len);

	datalen = scsi_bufflen(scsi);
	fcp->fc_dl = cpu_to_be32(datalen);

	if (scsi_get_prot_type(scsi) == SCSI_PROT_DIF_TYPE1) {
		datalen += datalen / scsi->device->sector_size * 8;
		fcp->fc_dl = cpu_to_be32(datalen);
	}
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
			sense += be32_to_cpu(fcp_rsp->ext.fr_rsp_len);
		sense_len = min_t(u32, be32_to_cpu(fcp_rsp->ext.fr_sns_len),
				  SCSI_SENSE_BUFFERSIZE);
		memcpy(scsi->sense_buffer, sense, sense_len);
	}

	if (unlikely(rsp_flags & FCP_RESID_UNDER)) {
		resid = be32_to_cpu(fcp_rsp->ext.fr_resid);
		scsi_set_resid(scsi, resid);
		if (scsi_bufflen(scsi) - resid < scsi->underflow &&
		     !(rsp_flags & FCP_SNS_LEN_VAL) &&
		     fcp_rsp->resp.fr_status == SAM_STAT_GOOD)
			set_host_byte(scsi, DID_ERROR);
	} else if (unlikely(rsp_flags & FCP_RESID_OVER)) {
		/* FCP_DL was not sufficient for SCSI data length */
		if (fcp_rsp->resp.fr_status == SAM_STAT_GOOD)
			set_host_byte(scsi, DID_ERROR);
	}
}

#endif
