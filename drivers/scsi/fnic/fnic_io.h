/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */
#ifndef _FNIC_IO_H_
#define _FNIC_IO_H_

#include <scsi/fc/fc_fcp.h>
#include "fnic_fdls.h"

#define FNIC_DFLT_SG_DESC_CNT  32
#define FNIC_MAX_SG_DESC_CNT        256     /* Maximum descriptors per sgl */
#define FNIC_SG_DESC_ALIGN          16      /* Descriptor address alignment */

struct host_sg_desc {
	__le64 addr;
	__le32 len;
	u32 _resvd;
};

struct fnic_dflt_sgl_list {
	struct host_sg_desc sg_desc[FNIC_DFLT_SG_DESC_CNT];
};

struct fnic_sgl_list {
	struct host_sg_desc sg_desc[FNIC_MAX_SG_DESC_CNT];
};

enum fnic_sgl_list_type {
	FNIC_SGL_CACHE_DFLT = 0,  /* cache with default size sgl */
	FNIC_SGL_CACHE_MAX,       /* cache with max size sgl */
	FNIC_SGL_NUM_CACHES       /* number of sgl caches */
};

enum fnic_ioreq_state {
	FNIC_IOREQ_NOT_INITED = 0,
	FNIC_IOREQ_CMD_PENDING,
	FNIC_IOREQ_ABTS_PENDING,
	FNIC_IOREQ_ABTS_COMPLETE,
	FNIC_IOREQ_CMD_COMPLETE,
};

struct fnic_io_req {
	struct fnic_iport_s *iport;
	struct fnic_tport_s *tport;
	struct host_sg_desc *sgl_list; /* sgl list */
	void *sgl_list_alloc; /* sgl list address used for free */
	dma_addr_t sense_buf_pa; /* dma address for sense buffer*/
	dma_addr_t sgl_list_pa;	/* dma address for sgl list */
	u16 sgl_cnt;
	u8 sgl_type; /* device DMA descriptor list type */
	u8 io_completed:1; /* set to 1 when fw completes IO */
	u32 port_id; /* remote port DID */
	unsigned long start_time; /* in jiffies */
	struct completion *abts_done; /* completion for abts */
	struct completion *dr_done; /* completion for device reset */
	unsigned int tag;
	struct scsi_cmnd *sc; /* midlayer's cmd pointer */
};
#endif /* _FNIC_IO_H_ */
