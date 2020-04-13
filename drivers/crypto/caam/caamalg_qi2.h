/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright 2015-2016 Freescale Semiconductor Inc.
 * Copyright 2017-2018 NXP
 */

#ifndef _CAAMALG_QI2_H_
#define _CAAMALG_QI2_H_

#include <soc/fsl/dpaa2-io.h>
#include <soc/fsl/dpaa2-fd.h>
#include <linux/threads.h>
#include <linux/netdevice.h>
#include "dpseci.h"
#include "desc_constr.h"

#define DPAA2_CAAM_STORE_SIZE	16
/* NAPI weight *must* be a multiple of the store size. */
#define DPAA2_CAAM_NAPI_WEIGHT	512

/* The congestion entrance threshold was chosen so that on LS2088
 * we support the maximum throughput for the available memory
 */
#define DPAA2_SEC_CONG_ENTRY_THRESH	(128 * 1024 * 1024)
#define DPAA2_SEC_CONG_EXIT_THRESH	(DPAA2_SEC_CONG_ENTRY_THRESH * 9 / 10)

/**
 * dpaa2_caam_priv - driver private data
 * @dpseci_id: DPSECI object unique ID
 * @major_ver: DPSECI major version
 * @minor_ver: DPSECI minor version
 * @dpseci_attr: DPSECI attributes
 * @sec_attr: SEC engine attributes
 * @rx_queue_attr: array of Rx queue attributes
 * @tx_queue_attr: array of Tx queue attributes
 * @cscn_mem: pointer to memory region containing the congestion SCN
 *	it's size is larger than to accommodate alignment
 * @cscn_mem_aligned: pointer to congestion SCN; it is computed as
 *	PTR_ALIGN(cscn_mem, DPAA2_CSCN_ALIGN)
 * @cscn_dma: dma address used by the QMAN to write CSCN messages
 * @dev: device associated with the DPSECI object
 * @mc_io: pointer to MC portal's I/O object
 * @domain: IOMMU domain
 * @ppriv: per CPU pointers to privata data
 */
struct dpaa2_caam_priv {
	int dpsec_id;

	u16 major_ver;
	u16 minor_ver;

	struct dpseci_attr dpseci_attr;
	struct dpseci_sec_attr sec_attr;
	struct dpseci_rx_queue_attr rx_queue_attr[DPSECI_MAX_QUEUE_NUM];
	struct dpseci_tx_queue_attr tx_queue_attr[DPSECI_MAX_QUEUE_NUM];
	int num_pairs;

	/* congestion */
	void *cscn_mem;
	void *cscn_mem_aligned;
	dma_addr_t cscn_dma;

	struct device *dev;
	struct fsl_mc_io *mc_io;
	struct iommu_domain *domain;

	struct dpaa2_caam_priv_per_cpu __percpu *ppriv;
	struct dentry *dfs_root;
};

/**
 * dpaa2_caam_priv_per_cpu - per CPU private data
 * @napi: napi structure
 * @net_dev: netdev used by napi
 * @req_fqid: (virtual) request (Tx / enqueue) FQID
 * @rsp_fqid: (virtual) response (Rx / dequeue) FQID
 * @prio: internal queue number - index for dpaa2_caam_priv.*_queue_attr
 * @nctx: notification context of response FQ
 * @store: where dequeued frames are stored
 * @priv: backpointer to dpaa2_caam_priv
 * @dpio: portal used for data path operations
 */
struct dpaa2_caam_priv_per_cpu {
	struct napi_struct napi;
	struct net_device net_dev;
	int req_fqid;
	int rsp_fqid;
	int prio;
	struct dpaa2_io_notification_ctx nctx;
	struct dpaa2_io_store *store;
	struct dpaa2_caam_priv *priv;
	struct dpaa2_io *dpio;
};

/* Length of a single buffer in the QI driver memory cache */
#define CAAM_QI_MEMCACHE_SIZE	512

/*
 * aead_edesc - s/w-extended aead descriptor
 * @src_nents: number of segments in input scatterlist
 * @dst_nents: number of segments in output scatterlist
 * @iv_dma: dma address of iv for checking continuity and link table
 * @qm_sg_bytes: length of dma mapped h/w link table
 * @qm_sg_dma: bus physical mapped address of h/w link table
 * @assoclen: associated data length, in CAAM endianness
 * @assoclen_dma: bus physical mapped address of req->assoclen
 * @sgt: the h/w link table, followed by IV
 */
struct aead_edesc {
	int src_nents;
	int dst_nents;
	dma_addr_t iv_dma;
	int qm_sg_bytes;
	dma_addr_t qm_sg_dma;
	unsigned int assoclen;
	dma_addr_t assoclen_dma;
	struct dpaa2_sg_entry sgt[];
};

/*
 * skcipher_edesc - s/w-extended skcipher descriptor
 * @src_nents: number of segments in input scatterlist
 * @dst_nents: number of segments in output scatterlist
 * @iv_dma: dma address of iv for checking continuity and link table
 * @qm_sg_bytes: length of dma mapped qm_sg space
 * @qm_sg_dma: I/O virtual address of h/w link table
 * @sgt: the h/w link table, followed by IV
 */
struct skcipher_edesc {
	int src_nents;
	int dst_nents;
	dma_addr_t iv_dma;
	int qm_sg_bytes;
	dma_addr_t qm_sg_dma;
	struct dpaa2_sg_entry sgt[];
};

/*
 * ahash_edesc - s/w-extended ahash descriptor
 * @qm_sg_dma: I/O virtual address of h/w link table
 * @src_nents: number of segments in input scatterlist
 * @qm_sg_bytes: length of dma mapped qm_sg space
 * @sgt: pointer to h/w link table
 */
struct ahash_edesc {
	dma_addr_t qm_sg_dma;
	int src_nents;
	int qm_sg_bytes;
	struct dpaa2_sg_entry sgt[];
};

/**
 * caam_flc - Flow Context (FLC)
 * @flc: Flow Context options
 * @sh_desc: Shared Descriptor
 */
struct caam_flc {
	u32 flc[16];
	u32 sh_desc[MAX_SDLEN];
} ____cacheline_aligned;

enum optype {
	ENCRYPT = 0,
	DECRYPT,
	NUM_OP
};

/**
 * caam_request - the request structure the driver application should fill while
 *                submitting a job to driver.
 * @fd_flt: Frame list table defining input and output
 *          fd_flt[0] - FLE pointing to output buffer
 *          fd_flt[1] - FLE pointing to input buffer
 * @fd_flt_dma: DMA address for the frame list table
 * @flc: Flow Context
 * @flc_dma: I/O virtual address of Flow Context
 * @cbk: Callback function to invoke when job is completed
 * @ctx: arbit context attached with request by the application
 * @edesc: extended descriptor; points to one of {skcipher,aead}_edesc
 */
struct caam_request {
	struct dpaa2_fl_entry fd_flt[2];
	dma_addr_t fd_flt_dma;
	struct caam_flc *flc;
	dma_addr_t flc_dma;
	void (*cbk)(void *ctx, u32 err);
	void *ctx;
	void *edesc;
};

/**
 * dpaa2_caam_enqueue() - enqueue a crypto request
 * @dev: device associated with the DPSECI object
 * @req: pointer to caam_request
 */
int dpaa2_caam_enqueue(struct device *dev, struct caam_request *req);

#endif	/* _CAAMALG_QI2_H_ */
