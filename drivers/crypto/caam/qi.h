/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Public definitions for the CAAM/QI (Queue Interface) backend.
 *
 * Copyright 2013-2016 Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 */

#ifndef __QI_H__
#define __QI_H__

#include <soc/fsl/qman.h>
#include "compat.h"
#include "desc.h"
#include "desc_constr.h"

/*
 * CAAM hardware constructs a job descriptor which points to a shared descriptor
 * (as pointed by context_a of to-CAAM FQ).
 * When the job descriptor is executed by DECO, the whole job descriptor
 * together with shared descriptor gets loaded in DECO buffer, which is
 * 64 words (each 32-bit) long.
 *
 * The job descriptor constructed by CAAM hardware has the following layout:
 *
 *	HEADER		(1 word)
 *	Shdesc ptr	(1 or 2 words)
 *	SEQ_OUT_PTR	(1 word)
 *	Out ptr		(1 or 2 words)
 *	Out length	(1 word)
 *	SEQ_IN_PTR	(1 word)
 *	In ptr		(1 or 2 words)
 *	In length	(1 word)
 *
 * The shdesc ptr is used to fetch shared descriptor contents into DECO buffer.
 *
 * Apart from shdesc contents, the total number of words that get loaded in DECO
 * buffer are '8' or '11'. The remaining words in DECO buffer can be used for
 * storing shared descriptor.
 */
#define MAX_SDLEN	((CAAM_DESC_BYTES_MAX - DESC_JOB_IO_LEN) / CAAM_CMD_SZ)

/* Length of a single buffer in the QI driver memory cache */
#define CAAM_QI_MEMCACHE_SIZE	768

extern bool caam_congested __read_mostly;

/*
 * This is the request structure the driver application should fill while
 * submitting a job to driver.
 */
struct caam_drv_req;

/*
 * caam_qi_cbk - application's callback function invoked by the driver when the
 *               request has been successfully processed.
 * @drv_req: original request that was submitted
 * @status: completion status of request (0 - success, non-zero - error code)
 */
typedef void (*caam_qi_cbk)(struct caam_drv_req *drv_req, u32 status);

enum optype {
	ENCRYPT,
	DECRYPT,
	NUM_OP
};

/**
 * caam_drv_ctx - CAAM/QI backend driver context
 *
 * The jobs are processed by the driver against a driver context.
 * With every cryptographic context, a driver context is attached.
 * The driver context contains data for private use by driver.
 * For the applications, this is an opaque structure.
 *
 * @prehdr: preheader placed before shrd desc
 * @sh_desc: shared descriptor
 * @context_a: shared descriptor dma address
 * @req_fq: to-CAAM request frame queue
 * @rsp_fq: from-CAAM response frame queue
 * @cpu: cpu on which to receive CAAM response
 * @op_type: operation type
 * @qidev: device pointer for CAAM/QI backend
 */
struct caam_drv_ctx {
	u32 prehdr[2];
	u32 sh_desc[MAX_SDLEN];
	dma_addr_t context_a;
	struct qman_fq *req_fq;
	struct qman_fq *rsp_fq;
	int cpu;
	enum optype op_type;
	struct device *qidev;
} ____cacheline_aligned;

/**
 * caam_drv_req - The request structure the driver application should fill while
 *                submitting a job to driver.
 * @fd_sgt: QMan S/G pointing to output (fd_sgt[0]) and input (fd_sgt[1])
 *          buffers.
 * @cbk: callback function to invoke when job is completed
 * @app_ctx: arbitrary context attached with request by the application
 *
 * The fields mentioned below should not be used by application.
 * These are for private use by driver.
 *
 * @hdr__: linked list header to maintain list of outstanding requests to CAAM
 * @hwaddr: DMA address for the S/G table.
 */
struct caam_drv_req {
	struct qm_sg_entry fd_sgt[2];
	struct caam_drv_ctx *drv_ctx;
	caam_qi_cbk cbk;
	void *app_ctx;
} ____cacheline_aligned;

/**
 * caam_drv_ctx_init - Initialise a CAAM/QI driver context
 *
 * A CAAM/QI driver context must be attached with each cryptographic context.
 * This function allocates memory for CAAM/QI context and returns a handle to
 * the application. This handle must be submitted along with each enqueue
 * request to the driver by the application.
 *
 * @cpu: CPU where the application prefers to the driver to receive CAAM
 *       responses. The request completion callback would be issued from this
 *       CPU.
 * @sh_desc: shared descriptor pointer to be attached with CAAM/QI driver
 *           context.
 *
 * Returns a driver context on success or negative error code on failure.
 */
struct caam_drv_ctx *caam_drv_ctx_init(struct device *qidev, int *cpu,
				       u32 *sh_desc);

/**
 * caam_qi_enqueue - Submit a request to QI backend driver.
 *
 * The request structure must be properly filled as described above.
 *
 * @qidev: device pointer for QI backend
 * @req: CAAM QI request structure
 *
 * Returns 0 on success or negative error code on failure.
 */
int caam_qi_enqueue(struct device *qidev, struct caam_drv_req *req);

/**
 * caam_drv_ctx_busy - Check if there are too many jobs pending with CAAM
 *		       or too many CAAM responses are pending to be processed.
 * @drv_ctx: driver context for which job is to be submitted
 *
 * Returns caam congestion status 'true/false'
 */
bool caam_drv_ctx_busy(struct caam_drv_ctx *drv_ctx);

/**
 * caam_drv_ctx_update - Update QI driver context
 *
 * Invoked when shared descriptor is required to be change in driver context.
 *
 * @drv_ctx: driver context to be updated
 * @sh_desc: new shared descriptor pointer to be updated in QI driver context
 *
 * Returns 0 on success or negative error code on failure.
 */
int caam_drv_ctx_update(struct caam_drv_ctx *drv_ctx, u32 *sh_desc);

/**
 * caam_drv_ctx_rel - Release a QI driver context
 * @drv_ctx: context to be released
 */
void caam_drv_ctx_rel(struct caam_drv_ctx *drv_ctx);

int caam_qi_init(struct platform_device *pdev);
void caam_qi_shutdown(struct device *dev);

/**
 * qi_cache_alloc - Allocate buffers from CAAM-QI cache
 *
 * Invoked when a user of the CAAM-QI (i.e. caamalg-qi) needs data which has
 * to be allocated on the hotpath. Instead of using malloc, one can use the
 * services of the CAAM QI memory cache (backed by kmem_cache). The buffers
 * will have a size of 256B, which is sufficient for hosting 16 SG entries.
 *
 * @flags: flags that would be used for the equivalent malloc(..) call
 *
 * Returns a pointer to a retrieved buffer on success or NULL on failure.
 */
void *qi_cache_alloc(gfp_t flags);

/**
 * qi_cache_free - Frees buffers allocated from CAAM-QI cache
 *
 * Invoked when a user of the CAAM-QI (i.e. caamalg-qi) no longer needs
 * the buffer previously allocated by a qi_cache_alloc call.
 * No checking is being done, the call is a passthrough call to
 * kmem_cache_free(...)
 *
 * @obj: object previously allocated using qi_cache_alloc()
 */
void qi_cache_free(void *obj);

#endif /* __QI_H__ */
