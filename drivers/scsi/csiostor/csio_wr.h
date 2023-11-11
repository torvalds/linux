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

#ifndef __CSIO_WR_H__
#define __CSIO_WR_H__

#include <linux/cache.h>

#include "csio_defs.h"
#include "t4fw_api.h"
#include "t4fw_api_stor.h"

/*
 * SGE register field values.
 */
#define X_INGPCIEBOUNDARY_32B		0
#define X_INGPCIEBOUNDARY_64B		1
#define X_INGPCIEBOUNDARY_128B		2
#define X_INGPCIEBOUNDARY_256B		3
#define X_INGPCIEBOUNDARY_512B		4
#define X_INGPCIEBOUNDARY_1024B		5
#define X_INGPCIEBOUNDARY_2048B		6
#define X_INGPCIEBOUNDARY_4096B		7

/* GTS register */
#define X_TIMERREG_COUNTER0		0
#define X_TIMERREG_COUNTER1		1
#define X_TIMERREG_COUNTER2		2
#define X_TIMERREG_COUNTER3		3
#define X_TIMERREG_COUNTER4		4
#define X_TIMERREG_COUNTER5		5
#define X_TIMERREG_RESTART_COUNTER	6
#define X_TIMERREG_UPDATE_CIDX		7

/*
 * Egress Context field values
 */
#define X_FETCHBURSTMIN_16B		0
#define X_FETCHBURSTMIN_32B		1
#define X_FETCHBURSTMIN_64B		2
#define X_FETCHBURSTMIN_128B		3

#define X_FETCHBURSTMAX_64B		0
#define X_FETCHBURSTMAX_128B		1
#define X_FETCHBURSTMAX_256B		2
#define X_FETCHBURSTMAX_512B		3

#define X_HOSTFCMODE_NONE		0
#define X_HOSTFCMODE_INGRESS_QUEUE	1
#define X_HOSTFCMODE_STATUS_PAGE	2
#define X_HOSTFCMODE_BOTH		3

/*
 * Ingress Context field values
 */
#define X_UPDATESCHEDULING_TIMER	0
#define X_UPDATESCHEDULING_COUNTER_OPTTIMER	1

#define X_UPDATEDELIVERY_NONE		0
#define X_UPDATEDELIVERY_INTERRUPT	1
#define X_UPDATEDELIVERY_STATUS_PAGE	2
#define X_UPDATEDELIVERY_BOTH		3

#define X_INTERRUPTDESTINATION_PCIE	0
#define X_INTERRUPTDESTINATION_IQ	1

#define X_RSPD_TYPE_FLBUF		0
#define X_RSPD_TYPE_CPL			1
#define X_RSPD_TYPE_INTR		2

/* WR status is at the same position as retval in a CMD header */
#define csio_wr_status(_wr)		\
		(FW_CMD_RETVAL_G(ntohl(((struct fw_cmd_hdr *)(_wr))->lo)))

struct csio_hw;

extern int csio_intr_coalesce_cnt;
extern int csio_intr_coalesce_time;

/* Ingress queue params */
struct csio_iq_params {

	uint8_t		iq_start:1;
	uint8_t		iq_stop:1;
	uint8_t		pfn:3;

	uint8_t		vfn;

	uint16_t	physiqid;
	uint16_t	iqid;

	uint16_t	fl0id;
	uint16_t	fl1id;

	uint8_t		viid;

	uint8_t		type;
	uint8_t		iqasynch;
	uint8_t		reserved4;

	uint8_t		iqandst;
	uint8_t		iqanus;
	uint8_t		iqanud;

	uint16_t	iqandstindex;

	uint8_t		iqdroprss;
	uint8_t		iqpciech;
	uint8_t		iqdcaen;

	uint8_t		iqdcacpu;
	uint8_t		iqintcntthresh;
	uint8_t		iqo;

	uint8_t		iqcprio;
	uint8_t		iqesize;

	uint16_t	iqsize;

	uint64_t	iqaddr;

	uint8_t		iqflintiqhsen;
	uint8_t		reserved5;
	uint8_t		iqflintcongen;
	uint8_t		iqflintcngchmap;

	uint32_t	reserved6;

	uint8_t		fl0hostfcmode;
	uint8_t		fl0cprio;
	uint8_t		fl0paden;
	uint8_t		fl0packen;
	uint8_t		fl0congen;
	uint8_t		fl0dcaen;

	uint8_t		fl0dcacpu;
	uint8_t		fl0fbmin;

	uint8_t		fl0fbmax;
	uint8_t		fl0cidxfthresho;
	uint8_t		fl0cidxfthresh;

	uint16_t	fl0size;

	uint64_t	fl0addr;

	uint64_t	reserved7;

	uint8_t		fl1hostfcmode;
	uint8_t		fl1cprio;
	uint8_t		fl1paden;
	uint8_t		fl1packen;
	uint8_t		fl1congen;
	uint8_t		fl1dcaen;

	uint8_t		fl1dcacpu;
	uint8_t		fl1fbmin;

	uint8_t		fl1fbmax;
	uint8_t		fl1cidxfthresho;
	uint8_t		fl1cidxfthresh;

	uint16_t	fl1size;

	uint64_t	fl1addr;
};

/* Egress queue params */
struct csio_eq_params {

	uint8_t		pfn;
	uint8_t		vfn;

	uint8_t		eqstart:1;
	uint8_t		eqstop:1;

	uint16_t        physeqid;
	uint32_t	eqid;

	uint8_t		hostfcmode:2;
	uint8_t		cprio:1;
	uint8_t		pciechn:3;

	uint16_t	iqid;

	uint8_t		dcaen:1;
	uint8_t		dcacpu:5;

	uint8_t		fbmin:3;
	uint8_t		fbmax:3;

	uint8_t		cidxfthresho:1;
	uint8_t		cidxfthresh:3;

	uint16_t	eqsize;

	uint64_t	eqaddr;
};

struct csio_dma_buf {
	struct list_head	list;
	void			*vaddr;		/* Virtual address */
	dma_addr_t		paddr;		/* Physical address */
	uint32_t		len;		/* Buffer size */
};

/* Generic I/O request structure */
struct csio_ioreq {
	struct csio_sm		sm;		/* SM, List
						 * should be the first member
						 */
	int			iq_idx;		/* Ingress queue index */
	int			eq_idx;		/* Egress queue index */
	uint32_t		nsge;		/* Number of SG elements */
	uint32_t		tmo;		/* Driver timeout */
	uint32_t		datadir;	/* Data direction */
	struct csio_dma_buf	dma_buf;	/* Req/resp DMA buffers */
	uint16_t		wr_status;	/* WR completion status */
	int16_t			drv_status;	/* Driver internal status */
	struct csio_lnode	*lnode;		/* Owner lnode */
	struct csio_rnode	*rnode;		/* Src/destination rnode */
	void (*io_cbfn) (struct csio_hw *, struct csio_ioreq *);
						/* completion callback */
	void			*scratch1;	/* Scratch area 1.
						 */
	void			*scratch2;	/* Scratch area 2. */
	struct list_head	gen_list;	/* Any list associated with
						 * this ioreq.
						 */
	uint64_t		fw_handle;	/* Unique handle passed
						 * to FW
						 */
	uint8_t			dcopy;		/* Data copy required */
	uint8_t			reserved1;
	uint16_t		reserved2;
	struct completion	cmplobj;	/* ioreq completion object */
} ____cacheline_aligned_in_smp;

/*
 * Egress status page for egress cidx updates
 */
struct csio_qstatus_page {
	__be32 qid;
	__be16 cidx;
	__be16 pidx;
};


enum {
	CSIO_MAX_FLBUF_PER_IQWR = 4,
	CSIO_QCREDIT_SZ  = 64,			/* pidx/cidx increments
						 * in bytes
						 */
	CSIO_MAX_QID = 0xFFFF,
	CSIO_MAX_IQ = 128,

	CSIO_SGE_NTIMERS = 6,
	CSIO_SGE_NCOUNTERS = 4,
	CSIO_SGE_FL_SIZE_REGS = 16,
};

/* Defines for type */
enum {
	CSIO_EGRESS	= 1,
	CSIO_INGRESS	= 2,
	CSIO_FREELIST	= 3,
};

/*
 * Structure for footer (last 2 flits) of Ingress Queue Entry.
 */
struct csio_iqwr_footer {
	__be32			hdrbuflen_pidx;
	__be32			pldbuflen_qid;
	union {
		u8		type_gen;
		__be64		last_flit;
	} u;
};

#define IQWRF_NEWBUF		(1 << 31)
#define IQWRF_LEN_GET(x)	(((x) >> 0) & 0x7fffffffU)
#define IQWRF_GEN_SHIFT		7
#define IQWRF_TYPE_GET(x)	(((x) >> 4) & 0x3U)


/*
 * WR pair:
 * ========
 * A WR can start towards the end of a queue, and then continue at the
 * beginning, since the queue is considered to be circular. This will
 * require a pair of address/len to be passed back to the caller -
 * hence the Work request pair structure.
 */
struct csio_wr_pair {
	void			*addr1;
	uint32_t		size1;
	void			*addr2;
	uint32_t		size2;
};

/*
 * The following structure is used by ingress processing to return the
 * free list buffers to consumers.
 */
struct csio_fl_dma_buf {
	struct csio_dma_buf	flbufs[CSIO_MAX_FLBUF_PER_IQWR];
						/* Freelist DMA buffers */
	int			offset;		/* Offset within the
						 * first FL buf.
						 */
	uint32_t		totlen;		/* Total length */
	uint8_t			defer_free;	/* Free of buffer can
						 * deferred
						 */
};

/* Data-types */
typedef void (*iq_handler_t)(struct csio_hw *, void *, uint32_t,
			     struct csio_fl_dma_buf *, void *);

struct csio_iq {
	uint16_t		iqid;		/* Queue ID */
	uint16_t		physiqid;	/* Physical Queue ID */
	uint16_t		genbit;		/* Generation bit,
						 * initially set to 1
						 */
	int			flq_idx;	/* Freelist queue index */
	iq_handler_t		iq_intx_handler; /* IQ INTx handler routine */
};

struct csio_eq {
	uint16_t		eqid;		/* Qid */
	uint16_t		physeqid;	/* Physical Queue ID */
	uint8_t			wrap[512];	/* Temp area for q-wrap around*/
};

struct csio_fl {
	uint16_t		flid;		/* Qid */
	uint16_t		packen;		/* Packing enabled? */
	int			offset;		/* Offset within FL buf */
	int			sreg;		/* Size register */
	struct csio_dma_buf	*bufs;		/* Free list buffer ptr array
						 * indexed using flq->cidx/pidx
						 */
};

struct csio_qstats {
	uint32_t	n_tot_reqs;		/* Total no. of Requests */
	uint32_t	n_tot_rsps;		/* Total no. of responses */
	uint32_t	n_qwrap;		/* Queue wraps */
	uint32_t	n_eq_wr_split;		/* Number of split EQ WRs */
	uint32_t	n_qentry;		/* Queue entry */
	uint32_t	n_qempty;		/* Queue empty */
	uint32_t	n_qfull;		/* Queue fulls */
	uint32_t	n_rsp_unknown;		/* Unknown response type */
	uint32_t	n_stray_comp;		/* Stray completion intr */
	uint32_t	n_flq_refill;		/* Number of FL refills */
};

/* Queue metadata */
struct csio_q {
	uint16_t		type;		/* Type: Ingress/Egress/FL */
	uint16_t		pidx;		/* producer index */
	uint16_t		cidx;		/* consumer index */
	uint16_t		inc_idx;	/* Incremental index */
	uint32_t		wr_sz;		/* Size of all WRs in this q
						 * if fixed
						 */
	void			*vstart;	/* Base virtual address
						 * of queue
						 */
	void			*vwrap;		/* Virtual end address to
						 * wrap around at
						 */
	uint32_t		credits;	/* Size of queue in credits */
	void			*owner;		/* Owner */
	union {					/* Queue contexts */
		struct csio_iq	iq;
		struct csio_eq	eq;
		struct csio_fl	fl;
	} un;

	dma_addr_t		pstart;		/* Base physical address of
						 * queue
						 */
	uint32_t		portid;		/* PCIE Channel */
	uint32_t		size;		/* Size of queue in bytes */
	struct csio_qstats	stats;		/* Statistics */
} ____cacheline_aligned_in_smp;

struct csio_sge {
	uint32_t	csio_fl_align;		/* Calculated and cached
						 * for fast path
						 */
	uint32_t	sge_control;		/* padding, boundaries,
						 * lengths, etc.
						 */
	uint32_t	sge_host_page_size;	/* Host page size */
	uint32_t	sge_fl_buf_size[CSIO_SGE_FL_SIZE_REGS];
						/* free list buffer sizes */
	uint16_t	timer_val[CSIO_SGE_NTIMERS];
	uint8_t		counter_val[CSIO_SGE_NCOUNTERS];
};

/* Work request module */
struct csio_wrm {
	int			num_q;		/* Number of queues */
	struct csio_q		**q_arr;	/* Array of queue pointers
						 * allocated dynamically
						 * based on configured values
						 */
	uint32_t		fw_iq_start;	/* Start ID of IQ for this fn*/
	uint32_t		fw_eq_start;	/* Start ID of EQ for this fn*/
	struct csio_q		*intr_map[CSIO_MAX_IQ];
						/* IQ-id to IQ map table. */
	int			free_qidx;	/* queue idx of free queue */
	struct csio_sge		sge;		/* SGE params */
};

#define csio_get_q(__hw, __idx)		((__hw)->wrm.q_arr[__idx])
#define	csio_q_type(__hw, __idx)	((__hw)->wrm.q_arr[(__idx)]->type)
#define	csio_q_pidx(__hw, __idx)	((__hw)->wrm.q_arr[(__idx)]->pidx)
#define	csio_q_cidx(__hw, __idx)	((__hw)->wrm.q_arr[(__idx)]->cidx)
#define	csio_q_inc_idx(__hw, __idx)	((__hw)->wrm.q_arr[(__idx)]->inc_idx)
#define	csio_q_vstart(__hw, __idx)	((__hw)->wrm.q_arr[(__idx)]->vstart)
#define	csio_q_pstart(__hw, __idx)	((__hw)->wrm.q_arr[(__idx)]->pstart)
#define	csio_q_size(__hw, __idx)	((__hw)->wrm.q_arr[(__idx)]->size)
#define	csio_q_credits(__hw, __idx)	((__hw)->wrm.q_arr[(__idx)]->credits)
#define	csio_q_portid(__hw, __idx)	((__hw)->wrm.q_arr[(__idx)]->portid)
#define	csio_q_wr_sz(__hw, __idx)	((__hw)->wrm.q_arr[(__idx)]->wr_sz)
#define	csio_q_iqid(__hw, __idx)	((__hw)->wrm.q_arr[(__idx)]->un.iq.iqid)
#define csio_q_physiqid(__hw, __idx)					\
				((__hw)->wrm.q_arr[(__idx)]->un.iq.physiqid)
#define csio_q_iq_flq_idx(__hw, __idx)					\
				((__hw)->wrm.q_arr[(__idx)]->un.iq.flq_idx)
#define	csio_q_eqid(__hw, __idx)	((__hw)->wrm.q_arr[(__idx)]->un.eq.eqid)
#define	csio_q_flid(__hw, __idx)	((__hw)->wrm.q_arr[(__idx)]->un.fl.flid)

#define csio_q_physeqid(__hw, __idx)					\
				((__hw)->wrm.q_arr[(__idx)]->un.eq.physeqid)
#define csio_iq_has_fl(__iq)		((__iq)->un.iq.flq_idx != -1)

#define csio_q_iq_to_flid(__hw, __iq_idx)				\
	csio_q_flid((__hw), (__hw)->wrm.q_arr[(__iq_qidx)]->un.iq.flq_idx)
#define csio_q_set_intr_map(__hw, __iq_idx, __rel_iq_id)		\
		(__hw)->wrm.intr_map[__rel_iq_id] = csio_get_q(__hw, __iq_idx)
#define	csio_q_eq_wrap(__hw, __idx)	((__hw)->wrm.q_arr[(__idx)]->un.eq.wrap)

struct csio_mb;

int csio_wr_alloc_q(struct csio_hw *, uint32_t, uint32_t,
		    uint16_t, void *, uint32_t, int, iq_handler_t);
int csio_wr_iq_create(struct csio_hw *, void *, int,
				uint32_t, uint8_t, bool,
				void (*)(struct csio_hw *, struct csio_mb *));
int csio_wr_eq_create(struct csio_hw *, void *, int, int, uint8_t,
				void (*)(struct csio_hw *, struct csio_mb *));
int csio_wr_destroy_queues(struct csio_hw *, bool cmd);


int csio_wr_get(struct csio_hw *, int, uint32_t,
			  struct csio_wr_pair *);
void csio_wr_copy_to_wrp(void *, struct csio_wr_pair *, uint32_t, uint32_t);
int csio_wr_issue(struct csio_hw *, int, bool);
int csio_wr_process_iq(struct csio_hw *, struct csio_q *,
				 void (*)(struct csio_hw *, void *,
					  uint32_t, struct csio_fl_dma_buf *,
					  void *),
				 void *);
int csio_wr_process_iq_idx(struct csio_hw *, int,
				 void (*)(struct csio_hw *, void *,
					  uint32_t, struct csio_fl_dma_buf *,
					  void *),
				 void *);

void csio_wr_sge_init(struct csio_hw *);
int csio_wrm_init(struct csio_wrm *, struct csio_hw *);
void csio_wrm_exit(struct csio_wrm *, struct csio_hw *);

#endif /* ifndef __CSIO_WR_H__ */
