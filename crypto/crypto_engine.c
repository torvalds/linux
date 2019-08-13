// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Handle async block request by crypto hardware engine.
 *
 * Copyright (C) 2016 Linaro, Inc.
 *
 * Author: Baolin Wang <baolin.wang@linaro.org>
 */

#include <linux/err.h>
#include <linux/delay.h>
#include <crypto/engine.h>
#include <uapi/linux/sched/types.h>
#include "internal.h"

#define CRYPTO_ENGINE_MAX_QLEN 10

/**
 * crypto_finalize_request - finalize one request if the request is done
 * @engine: the hardware engine
 * @req: the request need to be finalized
 * @err: error number
 */
static void crypto_finalize_request(struct crypto_engine *engine,
			     struct crypto_async_request *req, int err)
{
	unsigned long flags;
	bool finalize_cur_req = false;
	int ret;
	struct crypto_engine_ctx *enginectx;

	spin_lock_irqsave(&engine->queue_lock, flags);
	if (engine->cur_req == req)
		finalize_cur_req = true;
	spin_unlock_irqrestore(&engine->queue_lock, flags);

	if (finalize_cur_req) {
		enginectx = crypto_tfm_ctx(req->tfm);
		if (engine->cur_req_prepared &&
		    enginectx->op.unprepare_request) {
			ret = enginectx->op.unprepare_request(engine, req);
			if (ret)
				dev_err(engine->dev, "failed to unprepare request\n");
		}
		spin_lock_irqsave(&engine->queue_lock, flags);
		engine->cur_req = NULL;
		engine->cur_req_prepared = false;
		spin_unlock_irqrestore(&engine->queue_lock, flags);
	}

	req->complete(req, err);

	kthread_queue_work(engine->kworker, &engine->pump_requests);
}

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
	unsigned long flags;
	bool was_busy = false;
	int ret;
	struct crypto_engine_ctx *enginectx;

	spin_lock_irqsave(&engine->queue_lock, flags);

	/* Make sure we are not already running a request */
	if (engine->cur_req)
		goto out;

	/* If another context is idling then defer */
	if (engine->idling) {
		kthread_queue_work(engine->kworker, &engine->pump_requests);
		goto out;
	}

	/* Check if the engine queue is idle */
	if (!crypto_queue_len(&engine->queue) || !engine->running) {
		if (!engine->busy)
			goto out;

		/* Only do teardown in the thread */
		if (!in_kthread) {
			kthread_queue_work(engine->kworker,
					   &engine->pump_requests);
			goto out;
		}

		engine->busy = false;
		engine->idling = true;
		spin_unlock_irqrestore(&engine->queue_lock, flags);

		if (engine->unprepare_crypt_hardware &&
		    engine->unprepare_crypt_hardware(engine))
			dev_err(engine->dev, "failed to unprepare crypt hardware\n");

		spin_lock_irqsave(&engine->queue_lock, flags);
		engine->idling = false;
		goto out;
	}

	/* Get the fist request from the engine queue to handle */
	backlog = crypto_get_backlog(&engine->queue);
	async_req = crypto_dequeue_request(&engine->queue);
	if (!async_req)
		goto out;

	engine->cur_req = async_req;
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
			dev_err(engine->dev, "failed to prepare crypt hardware\n");
			goto req_err;
		}
	}

	enginectx = crypto_tfm_ctx(async_req->tfm);

	if (enginectx->op.prepare_request) {
		ret = enginectx->op.prepare_request(engine, async_req);
		if (ret) {
			dev_err(engine->dev, "failed to prepare request: %d\n",
				ret);
			goto req_err;
		}
		engine->cur_req_prepared = true;
	}
	if (!enginectx->op.do_one_request) {
		dev_err(engine->dev, "failed to do request\n");
		ret = -EINVAL;
		goto req_err;
	}
	ret = enginectx->op.do_one_request(engine, async_req);
	if (ret) {
		dev_err(engine->dev, "Failed to do one request from queue: %d\n", ret);
		goto req_err;
	}
	return;

req_err:
	crypto_finalize_request(engine, async_req, ret);
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
static int crypto_transfer_request(struct crypto_engine *engine,
				   struct crypto_async_request *req,
				   bool need_pump)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&engine->queue_lock, flags);

	if (!engine->running) {
		spin_unlock_irqrestore(&engine->queue_lock, flags);
		return -ESHUTDOWN;
	}

	ret = crypto_enqueue_request(&engine->queue, req);

	if (!engine->busy && need_pump)
		kthread_queue_work(engine->kworker, &engine->pump_requests);

	spin_unlock_irqrestore(&engine->queue_lock, flags);
	return ret;
}

/**
 * crypto_transfer_request_to_engine - transfer one request to list
 * into the engine queue
 * @engine: the hardware engine
 * @req: the request need to be listed into the engine queue
 */
static int crypto_transfer_request_to_engine(struct crypto_engine *engine,
					     struct crypto_async_request *req)
{
	return crypto_transfer_request(engine, req, true);
}

/**
 * crypto_transfer_ablkcipher_request_to_engine - transfer one ablkcipher_request
 * to list into the engine queue
 * @engine: the hardware engine
 * @req: the request need to be listed into the engine queue
 * TODO: Remove this function when skcipher conversion is finished
 */
int crypto_transfer_ablkcipher_request_to_engine(struct crypto_engine *engine,
						 struct ablkcipher_request *req)
{
	return crypto_transfer_request_to_engine(engine, &req->base);
}
EXPORT_SYMBOL_GPL(crypto_transfer_ablkcipher_request_to_engine);

/**
 * crypto_transfer_aead_request_to_engine - transfer one aead_request
 * to list into the engine queue
 * @engine: the hardware engine
 * @req: the request need to be listed into the engine queue
 */
int crypto_transfer_aead_request_to_engine(struct crypto_engine *engine,
					   struct aead_request *req)
{
	return crypto_transfer_request_to_engine(engine, &req->base);
}
EXPORT_SYMBOL_GPL(crypto_transfer_aead_request_to_engine);

/**
 * crypto_transfer_akcipher_request_to_engine - transfer one akcipher_request
 * to list into the engine queue
 * @engine: the hardware engine
 * @req: the request need to be listed into the engine queue
 */
int crypto_transfer_akcipher_request_to_engine(struct crypto_engine *engine,
					       struct akcipher_request *req)
{
	return crypto_transfer_request_to_engine(engine, &req->base);
}
EXPORT_SYMBOL_GPL(crypto_transfer_akcipher_request_to_engine);

/**
 * crypto_transfer_hash_request_to_engine - transfer one ahash_request
 * to list into the engine queue
 * @engine: the hardware engine
 * @req: the request need to be listed into the engine queue
 */
int crypto_transfer_hash_request_to_engine(struct crypto_engine *engine,
					   struct ahash_request *req)
{
	return crypto_transfer_request_to_engine(engine, &req->base);
}
EXPORT_SYMBOL_GPL(crypto_transfer_hash_request_to_engine);

/**
 * crypto_transfer_skcipher_request_to_engine - transfer one skcipher_request
 * to list into the engine queue
 * @engine: the hardware engine
 * @req: the request need to be listed into the engine queue
 */
int crypto_transfer_skcipher_request_to_engine(struct crypto_engine *engine,
					       struct skcipher_request *req)
{
	return crypto_transfer_request_to_engine(engine, &req->base);
}
EXPORT_SYMBOL_GPL(crypto_transfer_skcipher_request_to_engine);

/**
 * crypto_finalize_ablkcipher_request - finalize one ablkcipher_request if
 * the request is done
 * @engine: the hardware engine
 * @req: the request need to be finalized
 * @err: error number
 * TODO: Remove this function when skcipher conversion is finished
 */
void crypto_finalize_ablkcipher_request(struct crypto_engine *engine,
					struct ablkcipher_request *req, int err)
{
	return crypto_finalize_request(engine, &req->base, err);
}
EXPORT_SYMBOL_GPL(crypto_finalize_ablkcipher_request);

/**
 * crypto_finalize_aead_request - finalize one aead_request if
 * the request is done
 * @engine: the hardware engine
 * @req: the request need to be finalized
 * @err: error number
 */
void crypto_finalize_aead_request(struct crypto_engine *engine,
				  struct aead_request *req, int err)
{
	return crypto_finalize_request(engine, &req->base, err);
}
EXPORT_SYMBOL_GPL(crypto_finalize_aead_request);

/**
 * crypto_finalize_akcipher_request - finalize one akcipher_request if
 * the request is done
 * @engine: the hardware engine
 * @req: the request need to be finalized
 * @err: error number
 */
void crypto_finalize_akcipher_request(struct crypto_engine *engine,
				      struct akcipher_request *req, int err)
{
	return crypto_finalize_request(engine, &req->base, err);
}
EXPORT_SYMBOL_GPL(crypto_finalize_akcipher_request);

/**
 * crypto_finalize_hash_request - finalize one ahash_request if
 * the request is done
 * @engine: the hardware engine
 * @req: the request need to be finalized
 * @err: error number
 */
void crypto_finalize_hash_request(struct crypto_engine *engine,
				  struct ahash_request *req, int err)
{
	return crypto_finalize_request(engine, &req->base, err);
}
EXPORT_SYMBOL_GPL(crypto_finalize_hash_request);

/**
 * crypto_finalize_skcipher_request - finalize one skcipher_request if
 * the request is done
 * @engine: the hardware engine
 * @req: the request need to be finalized
 * @err: error number
 */
void crypto_finalize_skcipher_request(struct crypto_engine *engine,
				      struct skcipher_request *req, int err)
{
	return crypto_finalize_request(engine, &req->base, err);
}
EXPORT_SYMBOL_GPL(crypto_finalize_skcipher_request);

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

	kthread_queue_work(engine->kworker, &engine->pump_requests);

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
	unsigned int limit = 500;
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
		dev_warn(engine->dev, "could not stop engine\n");

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

	engine->dev = dev;
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

	engine->kworker = kthread_create_worker(0, "%s", engine->name);
	if (IS_ERR(engine->kworker)) {
		dev_err(dev, "failed to create crypto request pump task\n");
		return NULL;
	}
	kthread_init_work(&engine->pump_requests, crypto_pump_work);

	if (engine->rt) {
		dev_info(dev, "will run requests pump with realtime priority\n");
		sched_setscheduler(engine->kworker->task, SCHED_FIFO, &param);
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

	kthread_destroy_worker(engine->kworker);

	return 0;
}
EXPORT_SYMBOL_GPL(crypto_engine_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Crypto hardware engine framework");
