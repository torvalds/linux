/*
 * Copyright (c) 2005 Cisco Systems.  All rights reserved.
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

#ifndef IB_SRP_H
#define IB_SRP_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>

#include <scsi/scsi_host.h>
#include <scsi/scsi_cmnd.h>

#include <rdma/ib_verbs.h>
#include <rdma/ib_sa.h>
#include <rdma/ib_cm.h>
#include <rdma/ib_fmr_pool.h>

enum {
	SRP_PATH_REC_TIMEOUT_MS	= 1000,
	SRP_ABORT_TIMEOUT_MS	= 5000,

	SRP_PORT_REDIRECT	= 1,
	SRP_DLID_REDIRECT	= 2,
	SRP_STALE_CONN		= 3,

	SRP_DEF_SG_TABLESIZE	= 12,

	SRP_DEFAULT_QUEUE_SIZE	= 1 << 6,
	SRP_RSP_SQ_SIZE		= 1,
	SRP_TSK_MGMT_SQ_SIZE	= 1,
	SRP_DEFAULT_CMD_SQ_SIZE = SRP_DEFAULT_QUEUE_SIZE - SRP_RSP_SQ_SIZE -
				  SRP_TSK_MGMT_SQ_SIZE,

	SRP_TAG_NO_REQ		= ~0U,
	SRP_TAG_TSK_MGMT	= 1U << 31,

	SRP_MAX_PAGES_PER_MR	= 512,

	LOCAL_INV_WR_ID_MASK	= 1,
	FAST_REG_WR_ID_MASK	= 2,

	SRP_LAST_WR_ID		= 0xfffffffcU,
};

enum srp_target_state {
	SRP_TARGET_SCANNING,
	SRP_TARGET_LIVE,
	SRP_TARGET_REMOVED,
};

enum srp_iu_type {
	SRP_IU_CMD,
	SRP_IU_TSK_MGMT,
	SRP_IU_RSP,
};

/*
 * @mr_page_mask: HCA memory registration page mask.
 * @mr_page_size: HCA memory registration page size.
 * @mr_max_size: Maximum size in bytes of a single FMR / FR registration
 *   request.
 */
struct srp_device {
	struct list_head	dev_list;
	struct ib_device       *dev;
	struct ib_pd	       *pd;
	struct ib_mr	       *global_mr;
	u64			mr_page_mask;
	int			mr_page_size;
	int			mr_max_size;
	int			max_pages_per_mr;
	bool			has_fmr;
	bool			has_fr;
	bool			use_fmr;
	bool			use_fast_reg;
};

struct srp_host {
	struct srp_device      *srp_dev;
	u8			port;
	struct device		dev;
	struct list_head	target_list;
	spinlock_t		target_lock;
	struct completion	released;
	struct list_head	list;
	struct mutex		add_target_mutex;
};

struct srp_request {
	struct scsi_cmnd       *scmnd;
	struct srp_iu	       *cmd;
	union {
		struct ib_pool_fmr **fmr_list;
		struct srp_fr_desc **fr_list;
	};
	u64		       *map_page;
	struct srp_direct_buf  *indirect_desc;
	dma_addr_t		indirect_dma_addr;
	short			nmdesc;
};

/**
 * struct srp_rdma_ch
 * @comp_vector: Completion vector used by this RDMA channel.
 */
struct srp_rdma_ch {
	/* These are RW in the hot path, and commonly used together */
	struct list_head	free_tx;
	spinlock_t		lock;
	s32			req_lim;

	/* These are read-only in the hot path */
	struct srp_target_port *target ____cacheline_aligned_in_smp;
	struct ib_cq	       *send_cq;
	struct ib_cq	       *recv_cq;
	struct ib_qp	       *qp;
	union {
		struct ib_fmr_pool     *fmr_pool;
		struct srp_fr_pool     *fr_pool;
	};

	/* Everything above this point is used in the hot path of
	 * command processing. Try to keep them packed into cachelines.
	 */

	struct completion	done;
	int			status;

	struct ib_sa_path_rec	path;
	struct ib_sa_query     *path_query;
	int			path_query_id;

	struct ib_cm_id	       *cm_id;
	struct srp_iu	      **tx_ring;
	struct srp_iu	      **rx_ring;
	struct srp_request     *req_ring;
	int			max_ti_iu_len;
	int			comp_vector;

	struct completion	tsk_mgmt_done;
	u8			tsk_mgmt_status;
	bool			connected;
};

/**
 * struct srp_target_port
 * @comp_vector: Completion vector used by the first RDMA channel created for
 *   this target port.
 */
struct srp_target_port {
	/* read and written in the hot path */
	spinlock_t		lock;

	/* read only in the hot path */
	struct ib_mr		*global_mr;
	struct srp_rdma_ch	*ch;
	u32			ch_count;
	u32			lkey;
	enum srp_target_state	state;
	unsigned int		max_iu_len;
	unsigned int		cmd_sg_cnt;
	unsigned int		indirect_size;
	bool			allow_ext_sg;

	/* other member variables */
	union ib_gid		sgid;
	__be64			id_ext;
	__be64			ioc_guid;
	__be64			service_id;
	__be64			initiator_ext;
	u16			io_class;
	struct srp_host	       *srp_host;
	struct Scsi_Host       *scsi_host;
	struct srp_rport       *rport;
	char			target_name[32];
	unsigned int		scsi_id;
	unsigned int		sg_tablesize;
	int			queue_size;
	int			req_ring_size;
	int			comp_vector;
	int			tl_retry_count;

	union ib_gid		orig_dgid;
	__be16			pkey;

	u32			rq_tmo_jiffies;

	int			zero_req_lim;

	struct work_struct	tl_err_work;
	struct work_struct	remove_work;

	struct list_head	list;
	bool			qp_in_error;
};

struct srp_iu {
	struct list_head	list;
	u64			dma;
	void		       *buf;
	size_t			size;
	enum dma_data_direction	direction;
};

/**
 * struct srp_fr_desc - fast registration work request arguments
 * @entry: Entry in srp_fr_pool.free_list.
 * @mr:    Memory region.
 * @frpl:  Fast registration page list.
 */
struct srp_fr_desc {
	struct list_head		entry;
	struct ib_mr			*mr;
	struct ib_fast_reg_page_list	*frpl;
};

/**
 * struct srp_fr_pool - pool of fast registration descriptors
 *
 * An entry is available for allocation if and only if it occurs in @free_list.
 *
 * @size:      Number of descriptors in this pool.
 * @max_page_list_len: Maximum fast registration work request page list length.
 * @lock:      Protects free_list.
 * @free_list: List of free descriptors.
 * @desc:      Fast registration descriptor pool.
 */
struct srp_fr_pool {
	int			size;
	int			max_page_list_len;
	spinlock_t		lock;
	struct list_head	free_list;
	struct srp_fr_desc	desc[0];
};

/**
 * struct srp_map_state - per-request DMA memory mapping state
 * @desc:	    Pointer to the element of the SRP buffer descriptor array
 *		    that is being filled in.
 * @pages:	    Array with DMA addresses of pages being considered for
 *		    memory registration.
 * @base_dma_addr:  DMA address of the first page that has not yet been mapped.
 * @dma_len:	    Number of bytes that will be registered with the next
 *		    FMR or FR memory registration call.
 * @total_len:	    Total number of bytes in the sg-list being mapped.
 * @npages:	    Number of page addresses in the pages[] array.
 * @nmdesc:	    Number of FMR or FR memory descriptors used for mapping.
 * @ndesc:	    Number of SRP buffer descriptors that have been filled in.
 */
struct srp_map_state {
	union {
		struct {
			struct ib_pool_fmr **next;
			struct ib_pool_fmr **end;
		} fmr;
		struct {
			struct srp_fr_desc **next;
			struct srp_fr_desc **end;
		} fr;
		struct {
			void		   **next;
			void		   **end;
		} gen;
	};
	struct srp_direct_buf  *desc;
	u64		       *pages;
	dma_addr_t		base_dma_addr;
	u32			dma_len;
	u32			total_len;
	unsigned int		npages;
	unsigned int		nmdesc;
	unsigned int		ndesc;
};

#endif /* IB_SRP_H */
