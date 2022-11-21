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

#ifndef __CSIO_SCSI_H__
#define __CSIO_SCSI_H__

#include <linux/spinlock_types.h>
#include <linux/completion.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_tcq.h>
#include <scsi/fc/fc_fcp.h>

#include "csio_defs.h"
#include "csio_wr.h"

extern struct scsi_host_template csio_fcoe_shost_template;
extern struct scsi_host_template csio_fcoe_shost_vport_template;

extern int csio_scsi_eqsize;
extern int csio_scsi_iqlen;
extern int csio_scsi_ioreqs;
extern uint32_t csio_max_scan_tmo;
extern uint32_t csio_delta_scan_tmo;
extern int csio_lun_qdepth;

/*
 **************************** NOTE *******************************
 * How do we calculate MAX FCoE SCSI SGEs? Here is the math:
 * Max Egress WR size = 512 bytes
 * One SCSI egress WR has the following fixed no of bytes:
 *      48 (sizeof(struct fw_scsi_write[read]_wr)) - FW WR
 *    + 32 (sizeof(struct fc_fcp_cmnd)) - Immediate FCP_CMD
 *    ------
 *      80
 *    ------
 * That leaves us with 512 - 96 = 432 bytes for data SGE. Using
 * struct ulptx_sgl header for the SGE consumes:
 *	- 4 bytes for cmnd_sge.
 *	- 12 bytes for the first SGL.
 * That leaves us with 416 bytes for the remaining SGE pairs. Which is
 * is 416 / 24 (size(struct ulptx_sge_pair)) = 17 SGE pairs,
 * or 34 SGEs. Adding the first SGE fetches us 35 SGEs.
 */
#define CSIO_SCSI_MAX_SGE		35
#define CSIO_SCSI_ABRT_TMO_MS		60000
#define CSIO_SCSI_LUNRST_TMO_MS		60000
#define CSIO_SCSI_TM_POLL_MS		2000	/* should be less than
						 * all TM timeouts.
						 */
#define CSIO_SCSI_IQ_WRSZ		128
#define CSIO_SCSI_IQSIZE		(csio_scsi_iqlen * CSIO_SCSI_IQ_WRSZ)

#define	CSIO_MAX_SNS_LEN		128
#define	CSIO_SCSI_RSP_LEN	(FCP_RESP_WITH_EXT + 4 + CSIO_MAX_SNS_LEN)

/* Reference to scsi_cmnd */
#define csio_scsi_cmnd(req)		((req)->scratch1)

struct csio_scsi_stats {
	uint64_t		n_tot_success;	/* Total number of good I/Os */
	uint32_t		n_rn_nr_error;	/* No. of remote-node-not-
						 * ready errors
						 */
	uint32_t		n_hw_nr_error;	/* No. of hw-module-not-
						 * ready errors
						 */
	uint32_t		n_dmamap_error;	/* No. of DMA map erros */
	uint32_t		n_unsupp_sge_error; /* No. of too-many-SGes
						     * errors.
						     */
	uint32_t		n_no_req_error;	/* No. of Out-of-ioreqs error */
	uint32_t		n_busy_error;	/* No. of -EBUSY errors */
	uint32_t		n_hosterror;	/* No. of FW_HOSTERROR I/O */
	uint32_t		n_rsperror;	/* No. of response errors */
	uint32_t		n_autosense;	/* No. of auto sense replies */
	uint32_t		n_ovflerror;	/* No. of overflow errors */
	uint32_t		n_unflerror;	/* No. of underflow errors */
	uint32_t		n_rdev_nr_error;/* No. of rdev not
						 * ready errors
						 */
	uint32_t		n_rdev_lost_error;/* No. of rdev lost errors */
	uint32_t		n_rdev_logo_error;/* No. of rdev logo errors */
	uint32_t		n_link_down_error;/* No. of link down errors */
	uint32_t		n_no_xchg_error; /* No. no exchange error */
	uint32_t		n_unknown_error;/* No. of unhandled errors */
	uint32_t		n_aborted;	/* No. of aborted I/Os */
	uint32_t		n_abrt_timedout; /* No. of abort timedouts */
	uint32_t		n_abrt_fail;	/* No. of abort failures */
	uint32_t		n_abrt_dups;	/* No. of duplicate aborts */
	uint32_t		n_abrt_race_comp; /* No. of aborts that raced
						   * with completions.
						   */
	uint32_t		n_abrt_busy_error;/* No. of abort failures
						   * due to -EBUSY.
						   */
	uint32_t		n_closed;	/* No. of closed I/Os */
	uint32_t		n_cls_busy_error; /* No. of close failures
						   * due to -EBUSY.
						   */
	uint32_t		n_active;	/* No. of IOs in active_q */
	uint32_t		n_tm_active;	/* No. of TMs in active_q */
	uint32_t		n_wcbfn;	/* No. of I/Os in worker
						 * cbfn q
						 */
	uint32_t		n_free_ioreq;	/* No. of freelist entries */
	uint32_t		n_free_ddp;	/* No. of DDP freelist */
	uint32_t		n_unaligned;	/* No. of Unaligned SGls */
	uint32_t		n_inval_cplop;	/* No. invalid CPL op's in IQ */
	uint32_t		n_inval_scsiop;	/* No. invalid scsi op's in IQ*/
};

struct csio_scsim {
	struct csio_hw		*hw;		/* Pointer to HW moduel */
	uint8_t			max_sge;	/* Max SGE */
	uint8_t			proto_cmd_len;	/* Proto specific SCSI
						 * cmd length
						 */
	uint16_t		proto_rsp_len;	/* Proto specific SCSI
						 * response length
						 */
	spinlock_t		freelist_lock;	/* Lock for ioreq freelist */
	struct list_head	active_q;	/* Outstanding SCSI I/Os */
	struct list_head	ioreq_freelist;	/* Free list of ioreq's */
	struct list_head	ddp_freelist;	/* DDP descriptor freelist */
	struct csio_scsi_stats	stats;		/* This module's statistics */
};

/* State machine defines */
enum csio_scsi_ev {
	CSIO_SCSIE_START_IO = 1,		/* Start a regular SCSI IO */
	CSIO_SCSIE_START_TM,			/* Start a TM IO */
	CSIO_SCSIE_COMPLETED,			/* IO Completed */
	CSIO_SCSIE_ABORT,			/* Abort IO */
	CSIO_SCSIE_ABORTED,			/* IO Aborted */
	CSIO_SCSIE_CLOSE,			/* Close exchange */
	CSIO_SCSIE_CLOSED,			/* Exchange closed */
	CSIO_SCSIE_DRVCLEANUP,			/* Driver wants to manually
						 * cleanup this I/O.
						 */
};

enum csio_scsi_lev {
	CSIO_LEV_ALL = 1,
	CSIO_LEV_LNODE,
	CSIO_LEV_RNODE,
	CSIO_LEV_LUN,
};

struct csio_scsi_level_data {
	enum csio_scsi_lev	level;
	struct csio_rnode	*rnode;
	struct csio_lnode	*lnode;
	uint64_t		oslun;
};

struct csio_cmd_priv {
	uint8_t fc_tm_flags;	/* task management flags */
	uint16_t wr_status;
};

static inline struct csio_cmd_priv *csio_priv(struct scsi_cmnd *cmd)
{
	return scsi_cmd_priv(cmd);
}

static inline struct csio_ioreq *
csio_get_scsi_ioreq(struct csio_scsim *scm)
{
	struct csio_sm *req;

	if (likely(!list_empty(&scm->ioreq_freelist))) {
		req = list_first_entry(&scm->ioreq_freelist,
				       struct csio_sm, sm_list);
		list_del_init(&req->sm_list);
		CSIO_DEC_STATS(scm, n_free_ioreq);
		return (struct csio_ioreq *)req;
	} else
		return NULL;
}

static inline void
csio_put_scsi_ioreq(struct csio_scsim *scm, struct csio_ioreq *ioreq)
{
	list_add_tail(&ioreq->sm.sm_list, &scm->ioreq_freelist);
	CSIO_INC_STATS(scm, n_free_ioreq);
}

static inline void
csio_put_scsi_ioreq_list(struct csio_scsim *scm, struct list_head *reqlist,
			 int n)
{
	list_splice_init(reqlist, &scm->ioreq_freelist);
	scm->stats.n_free_ioreq += n;
}

static inline struct csio_dma_buf *
csio_get_scsi_ddp(struct csio_scsim *scm)
{
	struct csio_dma_buf *ddp;

	if (likely(!list_empty(&scm->ddp_freelist))) {
		ddp = list_first_entry(&scm->ddp_freelist,
				       struct csio_dma_buf, list);
		list_del_init(&ddp->list);
		CSIO_DEC_STATS(scm, n_free_ddp);
		return ddp;
	} else
		return NULL;
}

static inline void
csio_put_scsi_ddp(struct csio_scsim *scm, struct csio_dma_buf *ddp)
{
	list_add_tail(&ddp->list, &scm->ddp_freelist);
	CSIO_INC_STATS(scm, n_free_ddp);
}

static inline void
csio_put_scsi_ddp_list(struct csio_scsim *scm, struct list_head *reqlist,
			 int n)
{
	list_splice_tail_init(reqlist, &scm->ddp_freelist);
	scm->stats.n_free_ddp += n;
}

static inline void
csio_scsi_completed(struct csio_ioreq *ioreq, struct list_head *cbfn_q)
{
	csio_post_event(&ioreq->sm, CSIO_SCSIE_COMPLETED);
	if (csio_list_deleted(&ioreq->sm.sm_list))
		list_add_tail(&ioreq->sm.sm_list, cbfn_q);
}

static inline void
csio_scsi_aborted(struct csio_ioreq *ioreq, struct list_head *cbfn_q)
{
	csio_post_event(&ioreq->sm, CSIO_SCSIE_ABORTED);
	list_add_tail(&ioreq->sm.sm_list, cbfn_q);
}

static inline void
csio_scsi_closed(struct csio_ioreq *ioreq, struct list_head *cbfn_q)
{
	csio_post_event(&ioreq->sm, CSIO_SCSIE_CLOSED);
	list_add_tail(&ioreq->sm.sm_list, cbfn_q);
}

static inline void
csio_scsi_drvcleanup(struct csio_ioreq *ioreq)
{
	csio_post_event(&ioreq->sm, CSIO_SCSIE_DRVCLEANUP);
}

/*
 * csio_scsi_start_io - Kick starts the IO SM.
 * @req: io request SM.
 *
 * needs to be called with lock held.
 */
static inline int
csio_scsi_start_io(struct csio_ioreq *ioreq)
{
	csio_post_event(&ioreq->sm, CSIO_SCSIE_START_IO);
	return ioreq->drv_status;
}

/*
 * csio_scsi_start_tm - Kicks off the Task management IO SM.
 * @req: io request SM.
 *
 * needs to be called with lock held.
 */
static inline int
csio_scsi_start_tm(struct csio_ioreq *ioreq)
{
	csio_post_event(&ioreq->sm, CSIO_SCSIE_START_TM);
	return ioreq->drv_status;
}

/*
 * csio_scsi_abort - Abort an IO request
 * @req: io request SM.
 *
 * needs to be called with lock held.
 */
static inline int
csio_scsi_abort(struct csio_ioreq *ioreq)
{
	csio_post_event(&ioreq->sm, CSIO_SCSIE_ABORT);
	return ioreq->drv_status;
}

/*
 * csio_scsi_close - Close an IO request
 * @req: io request SM.
 *
 * needs to be called with lock held.
 */
static inline int
csio_scsi_close(struct csio_ioreq *ioreq)
{
	csio_post_event(&ioreq->sm, CSIO_SCSIE_CLOSE);
	return ioreq->drv_status;
}

void csio_scsi_cleanup_io_q(struct csio_scsim *, struct list_head *);
int csio_scsim_cleanup_io(struct csio_scsim *, bool abort);
int csio_scsim_cleanup_io_lnode(struct csio_scsim *,
					  struct csio_lnode *);
struct csio_ioreq *csio_scsi_cmpl_handler(struct csio_hw *, void *, uint32_t,
					  struct csio_fl_dma_buf *,
					  void *, uint8_t **);
int csio_scsi_qconfig(struct csio_hw *);
int csio_scsim_init(struct csio_scsim *, struct csio_hw *);
void csio_scsim_exit(struct csio_scsim *);

#endif /* __CSIO_SCSI_H__ */
