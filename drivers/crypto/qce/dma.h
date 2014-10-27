/*
 * Copyright (c) 2011-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _DMA_H_
#define _DMA_H_

#include <linux/dmaengine.h>

/* maximum data transfer block size between BAM and CE */
#define QCE_BAM_BURST_SIZE		64

#define QCE_AUTHIV_REGS_CNT		16
#define QCE_AUTH_BYTECOUNT_REGS_CNT	4
#define QCE_CNTRIV_REGS_CNT		4

struct qce_result_dump {
	u32 auth_iv[QCE_AUTHIV_REGS_CNT];
	u32 auth_byte_count[QCE_AUTH_BYTECOUNT_REGS_CNT];
	u32 encr_cntr_iv[QCE_CNTRIV_REGS_CNT];
	u32 status;
	u32 status2;
};

#define QCE_IGNORE_BUF_SZ	(2 * QCE_BAM_BURST_SIZE)
#define QCE_RESULT_BUF_SZ	\
		ALIGN(sizeof(struct qce_result_dump), QCE_BAM_BURST_SIZE)

struct qce_dma_data {
	struct dma_chan *txchan;
	struct dma_chan *rxchan;
	struct qce_result_dump *result_buf;
	void *ignore_buf;
};

int qce_dma_request(struct device *dev, struct qce_dma_data *dma);
void qce_dma_release(struct qce_dma_data *dma);
int qce_dma_prep_sgs(struct qce_dma_data *dma, struct scatterlist *sg_in,
		     int in_ents, struct scatterlist *sg_out, int out_ents,
		     dma_async_tx_callback cb, void *cb_param);
void qce_dma_issue_pending(struct qce_dma_data *dma);
int qce_dma_terminate_all(struct qce_dma_data *dma);
int qce_countsg(struct scatterlist *sg_list, int nbytes, bool *chained);
void qce_unmapsg(struct device *dev, struct scatterlist *sg, int nents,
		 enum dma_data_direction dir, bool chained);
int qce_mapsg(struct device *dev, struct scatterlist *sg, int nents,
	      enum dma_data_direction dir, bool chained);
struct scatterlist *
qce_sgtable_add(struct sg_table *sgt, struct scatterlist *sg_add);

#endif /* _DMA_H_ */
