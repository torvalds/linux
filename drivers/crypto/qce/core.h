/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 */

#ifndef _CORE_H_
#define _CORE_H_

#include "dma.h"

/**
 * struct qce_device - crypto engine device structure
 * @queue: crypto request queue
 * @lock: the lock protects queue and req
 * @done_tasklet: done tasklet object
 * @req: current active request
 * @result: result of current transform
 * @base: virtual IO base
 * @dev: pointer to device structure
 * @core: core device clock
 * @iface: interface clock
 * @bus: bus clock
 * @dma: pointer to dma data
 * @burst_size: the crypto burst size
 * @pipe_pair_id: which pipe pair id the device using
 * @async_req_enqueue: invoked by every algorithm to enqueue a request
 * @async_req_done: invoked by every algorithm to finish its request
 */
struct qce_device {
	struct crypto_queue queue;
	spinlock_t lock;
	struct tasklet_struct done_tasklet;
	struct crypto_async_request *req;
	int result;
	void __iomem *base;
	struct device *dev;
	struct clk *core, *iface, *bus;
	struct qce_dma_data dma;
	int burst_size;
	unsigned int pipe_pair_id;
	int (*async_req_enqueue)(struct qce_device *qce,
				 struct crypto_async_request *req);
	void (*async_req_done)(struct qce_device *qce, int ret);
};

/**
 * struct qce_algo_ops - algorithm operations per crypto type
 * @type: should be CRYPTO_ALG_TYPE_XXX
 * @register_algs: invoked by core to register the algorithms
 * @unregister_algs: invoked by core to unregister the algorithms
 * @async_req_handle: invoked by core to handle enqueued request
 */
struct qce_algo_ops {
	u32 type;
	int (*register_algs)(struct qce_device *qce);
	void (*unregister_algs)(struct qce_device *qce);
	int (*async_req_handle)(struct crypto_async_request *async_req);
};

#endif /* _CORE_H_ */
