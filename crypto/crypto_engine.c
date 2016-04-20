/*
 * Handle async block request by crypto hardware engine.
 *
 * Copyright (C) 2016 Linaro, Inc.
 *
 * Author: Baolin Wang <baolin.wang@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */

#include <linux/err.h>
#include <linux/delay.h>
#include "internal.h"

#define CRYPTO_ENGINE_MAX_QLEN 10

void crypto_finalize_request(struct crypto_engine *engine,
			     struct ablkcipher_request *req, int err);

/**
 * crypto_pump_requests - dequeue one request from engine queue to process
 * @engine: the hardware engine
 * @in_kthread: true if we are in the context of the request pump thread
 *
 * This function checks if there is any request in the engine queue that
 * needs processing and if so call out to the driver to initialize hardware
 * and handle each request.
 */
static void crypto_pump_requests(struct crypto_engine *engine,
				 bool in_kthread)
{
	struct crypto_async_request *async_req, *backlog;
	struct ablkcipher_request *req;
	unsigned long flags;
	bool was_busy = false;
	int ret;

	spin_lock_irqsave(&engine->queue_lock, flags);

	/* Make sure we are not already running a request */
	if (engine->cur_req)
		goto out;

	/* If another context is idling then defer */
	if (engine->idling) {
		queue_kthread_work(&engine->kworker, &engine->pump_requests);
		goto out;
	}

	/* Check if the engine queue is idle */
	if (!crypto_queue_len(&engine->queue) || !engine->running) {
		if (!engine->busy)
			goto out;

		/* Only do teardown in the thread */
		if (!in_kthread) {
			queue_kthread_work(&engine->kworker,
					   &engine->pump_requests);
			goto out;
		}

		engine->busy = false;
		engine->idling = true;
		spin_unlock_irqrestore(&engine->queue_lock, flags);

		if (engine->unprepare_crypt_hardware &&
		    engine->unprepare_crypt_hardware(engine))
			pr_err("failed to unprepare crypt hardware\n");

		spin_lock_irqsave(&engine->queue_lock, flags);
		engine->idling = false;
		goto out;
	}

	/* Get the fist request from the engine queue to handle */
	backlog = crypto_get_backlog(&engine->queue);
	async_req = crypto_dequeue_request(&engine->queue);
	if (!async_req)
		goto out;

	req = ablkcipher_request_cast(async_req);

	engine->cur_req = req;
	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);

	if (engine->busy)
		was_busy = true;
	else
		engine->busy = true;

	spin_unlock_irqrestore(&engine->queue_lock, flags);

	/* Until here we get the request need to be encrypted successfully */
	if (!was_busy && engine->prepare_crypt_hardware) {
		ret = engine->prepare_crypt_hardware(engine);
		if (ret) {
			pr_err("failed to prepare crypt hardware\n");
			goto req_err;
		}
	}

	if (engine->prepare_request) {
		ret = engine->prepare_request(engine, engine->cur_req);
		if (ret) {
			pr_err("failed to prepare request: %d\n", ret);
			goto req_err;
		}
		engine->cur_req_prepared = true;
	}

	ret = engine->crypt_one_request(engine, engine->cur_req);
	if (ret) {
		pr_err("failed to crypt one request from queue\n");
		goto req_err;
	}
	return;

req_err:
	crypto_finalize_request(engine, engine->cur_req, ret);
	return;

out:
	spin_unlock_irqrestore(&engine->queue_lock, flags);
}

static void crypto_pump_work(struct kthread_work *work)
{
	struct crypto_engine *engine =
		container_of(work, struct crypto_engine, pump_requests);

	crypto_pump_requests(engine, true);
}

/**
 * crypto_transfer_request - transfer the new request into the engine queue
 * @engine: the hardware engine
 * @req: the request need to be listed into the engine queue
 */
int crypto_transfer_request(struct crypto_engine *engine,
			    struct ablkcipher_request *req, bool need_pump)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&engine->queue_lock, flags);

	if (!engine->running) {
		spin_unlock_irqrestore(&engine->queue_lock, flags);
		return -ESHUTDOWN;
	}

	ret = ablkcipher_enqueue_request(&engine->queue, req);

	if (!engine->busy && need_pump)
		queue_kthread_work(&engine->kworker, &engine->pump_requests);

	spin_unlock_irqrestore(&engine->queue_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(crypto_transfer_request);

/**
 * crypto_transfer_request_to_engine - transfer one request to list into the
 * engine queue
 * @engine: the hardware engine
 * @req: the request need to be listed into the engine queue
 */
int crypto_transfer_request_to_engine(struct crypto_engine *engine,
				      struct ablkcipher_request *req)
{
	return crypto_transfer_request(engine, req, true);
}
EXPORT_SYMBOL_GPL(crypto_transfer_request_to_engine);

/**
 * crypto_finalize_request - finalize one request if the request is done
 * @engine: the hardware engine
 * @req: the request need to be finalized
 * @err: error number
 */
void crypto_finalize_request(struct crypto_engine *engine,
			     struct ablkcipher_request *req, int err)
{
	unsigned long flags;
	bool finalize_cur_req = false;
	int ret;

	spin_lock_irqsave(&engine->queue_lock, flags);
	if (engine->cur_req == req)
		finalize_cur_req = true;
	spin_unlock_irqrestore(&engine->queue_lock, flags);

	if (finalize_cur_req) {
		if (engine->cur_req_prepared && engine->unprepare_request) {
			ret = engine->unprepare_request(engine, req);
			if (ret)
				pr_err("failed to unprepare request\n");
		}

		spin_lock_irqsave(&engine->queue_lock, flags);
		engine->cur_req = NULL;
		engine->cur_req_prepared = false;
		spin_unlock_irqrestore(&engine->queue_lock, flags);
	}

	req->base.complete(&req->base, err);

	queue_kthread_work(&engine->kworker, &engine->pump_requests);
}
EXPORT_SYMBOL_GPL(crypto_finalize_request);

/**
 * crypto_engine_start - start the hardware engine
 * @engine: the hardware engine need to be started
 *
 * Return 0 on success, else on fail.
 */
int crypto_engine_start(struct crypto_engine *engine)
{
	unsigned long flags;

	spin_lock_irqsave(&engine->queue_lock, flags);

	if (engine->running || engine->busy) {
		spin_unlock_irqrestore(&engine->queue_lock, flags);
		return -EBUSY;
	}

	engine->running = true;
	spin_unlock_irqrestore(&engine->queue_lock, flags);

	queue_kthread_work(&engine->kworker, &engine->pump_requests);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_engine_start);

/**
 * crypto_engine_stop - stop the hardware engine
 * @engine: the hardware engine need to be stopped
 *
 * Return 0 on success, else on fail.
 */
int crypto_engine_stop(struct crypto_engine *engine)
{
	unsigned long flags;
	unsigned limit = 500;
	int ret = 0;

	spin_lock_irqsave(&engine->queue_lock, flags);

	/*
	 * If the engine queue is not empty or the engine is on busy state,
	 * we need to wait for a while to pump the requests of engine queue.
	 */
	while ((crypto_queue_len(&engine->queue) || engine->busy) && limit--) {
		spin_unlock_irqrestore(&engine->queue_lock, flags);
		msleep(20);
		spin_lock_irqsave(&engine->queue_lock, flags);
	}

	if (crypto_queue_len(&engine->queue) || engine->busy)
		ret = -EBUSY;
	else
		engine->running = false;

	spin_unlock_irqrestore(&engine->queue_lock, flags);

	if (ret)
		pr_warn("could not stop engine\n");

	return ret;
}
EXPORT_SYMBOL_GPL(crypto_engine_stop);

/**
 * crypto_engine_alloc_init - allocate crypto hardware engine structure and
 * initialize it.
 * @dev: the device attached with one hardware engine
 * @rt: whether this queue is set to run as a realtime task
 *
 * This must be called from context that can sleep.
 * Return: the crypto engine structure on success, else NULL.
 */
struct crypto_engine *crypto_engine_alloc_init(struct device *dev, bool rt)
{
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };
	struct crypto_engine *engine;

	if (!dev)
		return NULL;

	engine = devm_kzalloc(dev, sizeof(*engine), GFP_KERNEL);
	if (!engine)
		return NULL;

	engine->rt = rt;
	engine->running = false;
	engine->busy = false;
	engine->idling = false;
	engine->cur_req_prepared = false;
	engine->priv_data = dev;
	snprintf(engine->name, sizeof(engine->name),
		 "%s-engine", dev_name(dev));

	crypto_init_queue(&engine->queue, CRYPTO_ENGINE_MAX_QLEN);
	spin_lock_init(&engine->queue_lock);

	init_kthread_worker(&engine->kworker);
	engine->kworker_task = kthread_run(kthread_worker_fn,
					   &engine->kworker, "%s",
					   engine->name);
	if (IS_ERR(engine->kworker_task)) {
		dev_err(dev, "failed to create crypto request pump task\n");
		return NULL;
	}
	init_kthread_work(&engine->pump_requests, crypto_pump_work);

	if (engine->rt) {
		dev_info(dev, "will run requests pump with realtime priority\n");
		sched_setscheduler(engine->kworker_task, SCHED_FIFO, &param);
	}

	return engine;
}
EXPORT_SYMBOL_GPL(crypto_engine_alloc_init);

/**
 * crypto_engine_exit - free the resources of hardware engine when exit
 * @engine: the hardware engine need to be freed
 *
 * Return 0 for success.
 */
int crypto_engine_exit(struct crypto_engine *engine)
{
	int ret;

	ret = crypto_engine_stop(engine);
	if (ret)
		return ret;

	flush_kthread_worker(&engine->kworker);
	kthread_stop(engine->kworker_task);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_engine_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Crypto hardware engine framework");
