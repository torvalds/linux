/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2014 Cisco Systems, Inc.  All rights reserved. */

#ifndef _SNIC_IO_H
#define _SNIC_IO_H

#define SNIC_DFLT_SG_DESC_CNT	32	/* Default descriptors for sgl */
#define SNIC_MAX_SG_DESC_CNT	60	/* Max descriptor for sgl */
#define SNIC_SG_DESC_ALIGN	16	/* Descriptor address alignment */

/* SG descriptor for snic */
struct snic_sg_desc {
	__le64 addr;
	__le32 len;
	u32 _resvd;
};

struct snic_dflt_sgl {
	struct snic_sg_desc sg_desc[SNIC_DFLT_SG_DESC_CNT];
};

struct snic_max_sgl {
	struct snic_sg_desc sg_desc[SNIC_MAX_SG_DESC_CNT];
};

enum snic_req_cache_type {
	SNIC_REQ_CACHE_DFLT_SGL = 0,	/* cache with default size sgl */
	SNIC_REQ_CACHE_MAX_SGL,		/* cache with max size sgl */
	SNIC_REQ_TM_CACHE,		/* cache for task mgmt reqs contains
					   snic_host_req objects only*/
	SNIC_REQ_MAX_CACHES		/* number of sgl caches */
};

/* Per IO internal state */
struct snic_internal_io_state {
	char	*rqi;
	u64	flags;
	u32	state;
	u32	abts_status;	/* Abort completion status */
	u32	lr_status;	/* device reset completion status */
};

/* IO state machine */
enum snic_ioreq_state {
	SNIC_IOREQ_NOT_INITED = 0,
	SNIC_IOREQ_PENDING,
	SNIC_IOREQ_ABTS_PENDING,
	SNIC_IOREQ_ABTS_COMPLETE,
	SNIC_IOREQ_LR_PENDING,
	SNIC_IOREQ_LR_COMPLETE,
	SNIC_IOREQ_COMPLETE,
};

struct snic;
struct snic_host_req;

/*
 * snic_req_info : Contains info about IO, one per scsi command.
 * Notes: Make sure that the structure is aligned to 16 B
 * this helps in easy access to snic_req_info from snic_host_req
 */
struct snic_req_info {
	struct list_head list;
	struct snic_host_req *req;
	u64	start_time;		/* start time in jiffies */
	u16	rq_pool_type;		/* noticion of request pool type */
	u16	req_len;		/* buf len passing to fw (req + sgl)*/
	u32	tgt_id;

	u32	tm_tag;
	u8	io_cmpl:1;		/* sets to 1 when fw completes IO */
	u8	resvd[3];
	struct scsi_cmnd *sc;		/* Associated scsi cmd */
	struct snic	*snic;		/* Associated snic */
	ulong	sge_va;			/* Pointer to Resp Buffer */
	u64	snsbuf_va;

	struct snic_host_req *abort_req;
	struct completion *abts_done;

	struct snic_host_req *dr_req;
	struct completion *dr_done;
};


#define rqi_to_req(rqi)	\
	((struct snic_host_req *) (((struct snic_req_info *)rqi)->req))

#define req_to_rqi(req)	\
	((struct snic_req_info *) (((struct snic_host_req *)req)->hdr.init_ctx))

#define req_to_sgl(req)	\
	((struct snic_sg_desc *) (((struct snic_host_req *)req)+1))

struct snic_req_info *
snic_req_init(struct snic *, int sg_cnt);
void snic_req_free(struct snic *, struct snic_req_info *);
void snic_calc_io_process_time(struct snic *, struct snic_req_info *);
void snic_pci_unmap_rsp_buf(struct snic *, struct snic_req_info *);
struct snic_host_req *
snic_abort_req_init(struct snic *, struct snic_req_info *);
struct snic_host_req *
snic_dr_req_init(struct snic *, struct snic_req_info *);
#endif /* _SNIC_IO_H */
