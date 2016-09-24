/*
 * Copyright 2014 Cisco Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
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

#ifndef __SNIC_FWINT_H
#define __SNIC_FWINT_H

#define SNIC_CDB_LEN	32	/* SCSI CDB size 32, can be used for 16 bytes */
#define LUN_ADDR_LEN	8

/*
 * Command entry type
 */
enum snic_io_type {
	/*
	 * Initiator request types
	 */
	SNIC_REQ_REPORT_TGTS = 0x2,	/* Report Targets */
	SNIC_REQ_ICMND,			/* Initiator command for SCSI IO */
	SNIC_REQ_ITMF,			/* Initiator command for Task Mgmt */
	SNIC_REQ_HBA_RESET,		/* SNIC Reset */
	SNIC_REQ_EXCH_VER,		/* Exchange Version Information */
	SNIC_REQ_TGT_INFO,		/* Backend/Target Information */
	SNIC_REQ_BOOT_LUNS,

	/*
	 * Response type
	 */
	SNIC_RSP_REPORT_TGTS_CMPL = 0x12,/* Report Targets Completion */
	SNIC_RSP_ICMND_CMPL,		/* SCSI IO Completion */
	SNIC_RSP_ITMF_CMPL,		/* Task Management Completion */
	SNIC_RSP_HBA_RESET_CMPL,	/* SNIC Reset Completion */
	SNIC_RSP_EXCH_VER_CMPL,		/* Exchange Version Completion*/
	SNIC_RSP_BOOT_LUNS_CMPL,

	/*
	 * Misc Request types
	 */
	SNIC_MSG_ACK = 0x80,		/* Ack: snic_notify_msg */
	SNIC_MSG_ASYNC_EVNOTIFY,	/* Asynchronous Event Notification */
}; /* end of enum snic_io_type */


/*
 * Header status codes from firmware
 */
enum snic_io_status {
	SNIC_STAT_IO_SUCCESS = 0,	/* request was successful */

	/*
	 * If a request to the fw is rejected, the original request header
	 * will be returned with the status set to one of the following:
	 */
	SNIC_STAT_INVALID_HDR,	/* header contains invalid data */
	SNIC_STAT_OUT_OF_RES,	/* out of resources to complete request */
	SNIC_STAT_INVALID_PARM,	/* some parameter in request is not valid */
	SNIC_STAT_REQ_NOT_SUP,	/* req type is not supported */
	SNIC_STAT_IO_NOT_FOUND,	/* requested IO was not found */

	/*
	 * Once a request is processed, the fw will usually return
	 * a cmpl message type. In cases where errors occurred,
	 * the header status would be filled in with one of the following:
	 */
	SNIC_STAT_ABORTED,		/* req was aborted */
	SNIC_STAT_TIMEOUT,		/* req was timed out */
	SNIC_STAT_SGL_INVALID,		/* req was aborted due to sgl error */
	SNIC_STAT_DATA_CNT_MISMATCH,	/*recv/sent more/less data than expec */
	SNIC_STAT_FW_ERR,		/* req was terminated due to fw error */
	SNIC_STAT_ITMF_REJECT,		/* itmf req was rejected by target */
	SNIC_STAT_ITMF_FAIL,		/* itmf req was failed */
	SNIC_STAT_ITMF_INCORRECT_LUN,	/* itmf req has incorrect LUN id*/
	SNIC_STAT_CMND_REJECT,		/* req was invalid and rejected */
	SNIC_STAT_DEV_OFFLINE,		/* req sent to offline device */
	SNIC_STAT_NO_BOOTLUN,
	SNIC_STAT_SCSI_ERR,		/* SCSI error returned by Target. */
	SNIC_STAT_NOT_READY,		/* sNIC Subsystem is not ready */
	SNIC_STAT_FATAL_ERROR,		/* sNIC is in unrecoverable state */
}; /* end of enum snic_io_status */

/*
 * snic_io_hdr : host <--> firmware
 *
 * for any other message that will be queued to firmware should
 *  have the following request header
 */
struct snic_io_hdr {
	__le32	hid;
	__le32	cmnd_id;	/* tag here */
	ulong	init_ctx;	/* initiator context */
	u8	type;		/* request/response type */
	u8	status;		/* header status entry */
	u8	protocol;	/* Protocol specific, may needed for RoCE*/
	u8	flags;
	__le16	sg_cnt;
	u16	resvd;
};

/* auxillary funciton for encoding the snic_io_hdr */
static inline void
snic_io_hdr_enc(struct snic_io_hdr *hdr, u8 typ, u8 status, u32 id, u32 hid,
		u16 sg_cnt, ulong ctx)
{
	hdr->type = typ;
	hdr->status = status;
	hdr->protocol = 0;
	hdr->hid = cpu_to_le32(hid);
	hdr->cmnd_id = cpu_to_le32(id);
	hdr->sg_cnt = cpu_to_le16(sg_cnt);
	hdr->init_ctx = ctx;
	hdr->flags = 0;
}

/* auxillary funciton for decoding the snic_io_hdr */
static inline void
snic_io_hdr_dec(struct snic_io_hdr *hdr, u8 *typ, u8 *stat, u32 *cmnd_id,
		u32 *hid, ulong *ctx)
{
	*typ = hdr->type;
	*stat = hdr->status;
	*hid = le32_to_cpu(hdr->hid);
	*cmnd_id = le32_to_cpu(hdr->cmnd_id);
	*ctx = hdr->init_ctx;
}

/*
 * snic_host_info: host -> firmware
 *
 * Used for sending host information to firmware, and request fw version
 */
struct snic_exch_ver_req {
	__le32	drvr_ver;	/* for debugging, when fw dump captured */
	__le32	os_type;	/* for OS specific features */
};

/*
 * os_type flags
 * Bit 0-7 : OS information
 * Bit 8-31: Feature/Capability Information
 */
#define SNIC_OS_LINUX	0x1
#define SNIC_OS_WIN	0x2
#define SNIC_OS_ESX	0x3

/*
 * HBA Capabilities
 * Bit 1: Reserved.
 * Bit 2: Dynamic Discovery of LUNs.
 * Bit 3: Async event notifications on on tgt online/offline events.
 * Bit 4: IO timeout support in FW.
 * Bit 5-31: Reserved.
 */
#define SNIC_HBA_CAP_DDL	0x02	/* Supports Dynamic Discovery of LUNs */
#define SNIC_HBA_CAP_AEN	0x04	/* Supports Async Event Noitifcation */
#define SNIC_HBA_CAP_TMO	0x08	/* Supports IO timeout in FW */

/*
 * snic_exch_ver_rsp : firmware -> host
 *
 * Used by firmware to send response to version request
 */
struct snic_exch_ver_rsp {
	__le32	version;
	__le32	hid;
	__le32	max_concur_ios;		/* max concurrent ios */
	__le32	max_sgs_per_cmd;	/* max sgls per IO */
	__le32	max_io_sz;		/* max io size supported */
	__le32	hba_cap;		/* hba capabilities */
	__le32	max_tgts;		/* max tgts supported */
	__le16	io_timeout;		/* FW extended timeout */
	u16	rsvd;
};


/*
 * snic_report_tgts : host -> firmware request
 *
 * Used by the host to request list of targets
 */
struct snic_report_tgts {
	__le16	sg_cnt;
	__le16	flags;		/* specific flags from fw */
	u8	_resvd[4];
	__le64	sg_addr;	/* Points to SGL */
	__le64	sense_addr;
};

enum snic_type {
	SNIC_NONE = 0x0,
	SNIC_DAS,
	SNIC_SAN,
};


/* Report Target Response */
enum snic_tgt_type {
	SNIC_TGT_NONE = 0x0,
	SNIC_TGT_DAS,	/* DAS Target */
	SNIC_TGT_SAN,	/* SAN Target */
};

/* target id format */
struct snic_tgt_id {
	__le32	tgt_id;		/* target id */
	__le16	tgt_type;	/* tgt type */
	__le16	vnic_id;	/* corresponding vnic id */
};

/*
 * snic_report_tgts_cmpl : firmware -> host response
 *
 * Used by firmware to send response to Report Targets request
 */
struct snic_report_tgts_cmpl {
	__le32	tgt_cnt;	/* Number of Targets accessible */
	u32	_resvd;
};

/*
 * Command flags
 *
 * Bit 0: Read flags
 * Bit 1: Write flag
 * Bit 2: ESGL - sg/esg array contains extended sg
 *	  ESGE - is a host buffer contains sg elements
 * Bit 3-4: Task Attributes
 *		00b - simple
 *		01b - head of queue
 *		10b - ordered
 * Bit 5-7: Priority - future use
 * Bit 8-15: Reserved
 */

#define SNIC_ICMND_WR		0x01	/* write command */
#define SNIC_ICMND_RD		0x02	/* read command */
#define SNIC_ICMND_ESGL		0x04	/* SGE/ESGE array contains valid data*/

/*
 * Priority/Task Attribute settings
 */
#define SNIC_ICMND_TSK_SHIFT		2	/* task attr starts at bit 2 */
#define SNIC_ICMND_TSK_MASK(x)		((x>>SNIC_ICMND_TSK_SHIFT) & ~(0xffff))
#define SNIC_ICMND_TSK_SIMPLE		0	/* simple task attr */
#define SNIC_ICMND_TSK_HEAD_OF_QUEUE	1	/* head of qeuue task attr */
#define SNIC_ICMND_TSK_ORDERED		2	/* ordered task attr */

#define SNIC_ICMND_PRI_SHIFT		5	/* prio val starts at bit 5 */

/*
 * snic_icmnd : host-> firmware request
 *
 * used for sending out an initiator SCSI 16/32-byte command
 */
struct snic_icmnd {
	__le16	sg_cnt;		/* Number of SG Elements */
	__le16	flags;		/* flags */
	__le32	sense_len;	/* Sense buffer length */
	__le64	tgt_id;		/* Destination Target ID */
	__le64	lun_id;		/* Destination LUN ID */
	u8	cdb_len;
	u8	_resvd;
	__le16	time_out;	/* ms time for Res allocations fw to handle io*/
	__le32	data_len;	/* Total number of bytes to be transferred */
	u8	cdb[SNIC_CDB_LEN];
	__le64	sg_addr;	/* Points to SG List */
	__le64	sense_addr;	/* Sense buffer address */
};


/* Response flags */
/* Bit 0: Under run
 * Bit 1: Over Run
 * Bit 2-7: Reserved
 */
#define SNIC_ICMND_CMPL_UNDR_RUN	0x01	/* resid under and valid */
#define SNIC_ICMND_CMPL_OVER_RUN	0x02	/* resid over and valid */

/*
 * snic_icmnd_cmpl: firmware -> host response
 *
 * Used for sending the host a response to an icmnd (initiator command)
 */
struct snic_icmnd_cmpl {
	u8	scsi_status;	/* value as per SAM */
	u8	flags;
	__le16	sense_len;	/* Sense Length */
	__le32	resid;		/* Residue : # bytes under or over run */
};

/*
 * snic_itmf: host->firmware request
 *
 * used for requesting the firmware to abort a request and/or send out
 * a task management function
 *
 * the req_id field is valid in case of abort task and clear task
 */
struct snic_itmf {
	u8	tm_type;	/* SCSI Task Management request */
	u8	resvd;
	__le16	flags;		/* flags */
	__le32	req_id;		/* Command id of snic req to be aborted */
	__le64	tgt_id;		/* Target ID */
	__le64	lun_id;		/* Destination LUN ID */
	__le16	timeout;	/* in sec */
};

/*
 * Task Management Request
 */
enum snic_itmf_tm_type {
	SNIC_ITMF_ABTS_TASK = 0x01,	/* Abort Task */
	SNIC_ITMF_ABTS_TASK_SET,	/* Abort Task Set */
	SNIC_ITMF_CLR_TASK,		/* Clear Task */
	SNIC_ITMF_CLR_TASKSET,		/* Clear Task Set */
	SNIC_ITMF_LUN_RESET,		/* Lun Reset */
	SNIC_ITMF_ABTS_TASK_TERM,	/* Supported for SAN Targets */
};

/*
 * snic_itmf_cmpl: firmware -> host resposne
 *
 * used for sending the host a response for a itmf request
 */
struct snic_itmf_cmpl {
	__le32	nterminated;	/* # IOs terminated as a result of tmf */
	u8	flags;		/* flags */
	u8	_resvd[3];
};

/*
 * itmfl_cmpl flags
 * Bit 0 : 1 - Num terminated field valid
 * Bit 1 - 7 : Reserved
 */
#define SNIC_NUM_TERM_VALID	0x01	/* Number of IOs terminated */

/*
 * snic_hba_reset: host -> firmware request
 *
 * used for requesting firmware to reset snic
 */
struct snic_hba_reset {
	__le16	flags;		/* flags */
	u8	_resvd[6];
};

/*
 * snic_hba_reset_cmpl: firmware -> host response
 *
 * Used by firmware to respond to the host's hba reset request
 */
struct snic_hba_reset_cmpl {
	u8	flags;		/* flags : more info needs to be added*/
	u8	_resvd[7];
};

/*
 * snic_notify_msg: firmware -> host response
 *
 * Used by firmware to notify host of the last work queue entry received
 */
struct snic_notify_msg {
	__le32	wqe_num;	/* wq entry number */
	u8	flags;		/* flags, macros */
	u8	_resvd[4];
};


#define SNIC_EVDATA_LEN		24	/* in bytes */
/* snic_async_evnotify: firmware -> host notification
 *
 * Used by firmware to notify the host about configuration/state changes
 */
struct snic_async_evnotify {
	u8	FLS_EVENT_DESC;
	u8	vnic;			/* vnic id */
	u8	_resvd[2];
	__le32	ev_id;			/* Event ID */
	u8	ev_data[SNIC_EVDATA_LEN]; /* Event Data */
	u8	_resvd2[4];
};

/* async event flags */
enum snic_ev_type {
	SNIC_EV_TGT_OFFLINE = 0x01, /* Target Offline, PL contains TGT ID */
	SNIC_EV_TGT_ONLINE,	/* Target Online, PL contains TGT ID */
	SNIC_EV_LUN_OFFLINE,	/* LUN Offline, PL contains LUN ID */
	SNIC_EV_LUN_ONLINE,	/* LUN Online, PL contains LUN ID */
	SNIC_EV_CONF_CHG,	/* Dev Config/Attr Change Event */
	SNIC_EV_TGT_ADDED,	/* Target Added */
	SNIC_EV_TGT_DELTD,	/* Target Del'd, PL contains TGT ID */
	SNIC_EV_LUN_ADDED,	/* LUN Added */
	SNIC_EV_LUN_DELTD,	/* LUN Del'd, PL cont. TGT & LUN ID */

	SNIC_EV_DISC_CMPL = 0x10, /* Discovery Completed Event */
};


#define SNIC_HOST_REQ_LEN	128	/*Exp length of host req, wq desc sz*/
/* Payload 88 bytes = 128 - 24 - 16 */
#define SNIC_HOST_REQ_PAYLOAD	((int)(SNIC_HOST_REQ_LEN -		\
					sizeof(struct snic_io_hdr) -	\
					(2 * sizeof(u64)) - sizeof(ulong)))

/*
 * snic_host_req: host -> firmware request
 *
 * Basic structure for all snic requests that are sent from the host to
 * firmware. They are 128 bytes in size.
 */
struct snic_host_req {
	u64	ctrl_data[2];	/*16 bytes - Control Data */
	struct snic_io_hdr hdr;
	union {
		/*
		 * Entry specific space, last byte contains color
		 */
		u8	buf[SNIC_HOST_REQ_PAYLOAD];

		/*
		 * Exchange firmware version
		 */
		struct snic_exch_ver_req	exch_ver;

		/* report targets */
		struct snic_report_tgts		rpt_tgts;

		/* io request */
		struct snic_icmnd		icmnd;

		/* task management request */
		struct snic_itmf		itmf;

		/* hba reset */
		struct snic_hba_reset		reset;
	} u;

	ulong req_pa;
}; /* end of snic_host_req structure */


#define SNIC_FW_REQ_LEN		64 /* Expected length of fw req */
struct snic_fw_req {
	struct snic_io_hdr hdr;
	union {
		/*
		 * Entry specific space, last byte contains color
		 */
		u8	buf[SNIC_FW_REQ_LEN - sizeof(struct snic_io_hdr)];

		/* Exchange Version Response */
		struct snic_exch_ver_rsp	exch_ver_cmpl;

		/* Report Targets Response */
		struct snic_report_tgts_cmpl	rpt_tgts_cmpl;

		/* scsi response */
		struct snic_icmnd_cmpl		icmnd_cmpl;

		/* task management response */
		struct snic_itmf_cmpl		itmf_cmpl;

		/* hba reset response */
		struct snic_hba_reset_cmpl	reset_cmpl;

		/* notify message */
		struct snic_notify_msg		ack;

		/* async notification event */
		struct snic_async_evnotify	async_ev;

	} u;
}; /* end of snic_fw_req structure */

/*
 * Auxillary macro to verify specific snic req/cmpl structures
 * to ensure that it will be aligned to 64 bit, and not using
 * color bit field
 */
#define VERIFY_REQ_SZ(x)
#define VERIFY_CMPL_SZ(x)

/*
 * Access routines to encode and decode the color bit, which is the most
 * significant bit of the structure.
 */
static inline void
snic_color_enc(struct snic_fw_req *req, u8 color)
{
	u8 *c = ((u8 *) req) + sizeof(struct snic_fw_req) - 1;

	if (color)
		*c |= 0x80;
	else
		*c &= ~0x80;
}

static inline void
snic_color_dec(struct snic_fw_req *req, u8 *color)
{
	u8 *c = ((u8 *) req) + sizeof(struct snic_fw_req) - 1;

	*color = *c >> 7;

	/* Make sure color bit is read from desc *before* other fields
	 * are read from desc. Hardware guarantees color bit is last
	 * bit (byte) written. Adding the rmb() prevents the compiler
	 * and/or CPU from reordering the reads which would potentially
	 * result in reading stale values.
	 */
	rmb();
}
#endif /* end of __SNIC_FWINT_H */
