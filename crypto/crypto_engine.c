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
#include <crypto/engine.h>
#include <crypto/internal/hash.h>
#include <uapi/linux/sched/types.h>
#include "internal.h"

#define CRYPTO_ENGINE_MAX_QLEN 10

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
	struct ahash_request *hreq;
	struct ablkcipher_request *breq;
	unsigned long flags;
	bool was_busy = false;
	int ret, rtype;

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

	rtype = crypto_tfm_alg_type(engine->cur_req->tfm);
	/* Until here we get the request need to be encrypted successfully */
	if (!was_busy && engine->prepare_crypt_hardware) {
		ret = engine->prepare_crypt_hardware(engine);
		if (ret) {
			dev_err(engine->dev, "failed to prepare crypt hardware\n");
			goto req_err;
		}
	}

	switch (rtype) {
	case CRYPTO_ALG_TYPE_AHASH:
		hreq = ahash_request_cast(engine->cur_req);
		if (engine->prepare_hash_request) {
			ret = engine->prepare_hash_request(engine, hreq);
			if (ret) {
				dev_err(engine->dev, "failed to prepare request: %d\n",
					ret);
				goto req_err;
			}
			engine->cur_req_prepared = true;
		}
		ret = engine->hash_one_request(engine, hreq);
		if (ret) {
			dev_err(engine->dev, "failed to hash one request from queue\n");
			goto req_err;
		}
		return;
	case CRYPTO_ALG_TYPE_ABLKCIPHER:
		breq = ablkcipher_request_cast(engine->cur_req);
		if (engine->prepare_cipher_request) {
			ret = engine->prepare_cipher_request(engine, breq);
			if (ret) {
				dev_err(engine->dev, "failed to prepare request: %d\n",
					ret);
				goto req_err;
			}
			engine->cur_req_prepared = true;
		}
		ret = engine->cipher_one_request(engine, breq);
		if (ret) {
			dev_err(engine->dev, "failed to cipher one request from queue\n");
			goto req_err;
		}
		return;
	default:
		dev_err(engine->dev, "failed to prepare request of unknown type\n");
		return;
	}

req_err:
	switch (rtype) {
	case CRYPTO_ALG_TYPE_AHASH:
		hreq = ahash_request_cast(engine->cur_req);
		crypto_finalize_hash_request(engine, hreq, ret);
		break;
	case CRYPTO_ALG_TYPE_ABLKCIPHER:
		breq = ablkcipher_request_cast(engine->cur_req);
		crypto_finalize_cipher_request(engine, breq, ret);
		break;
	}
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
 * crypto_transfer_cipher_request - transfer the new request into the
 * enginequeue
 * @engine: the hardware engine
 * @req: the request need to be listed into the engine queue
 */
int crypto_transfer_cipher_request(struct crypto_engine *engine,
				   struct ablkcipher_request *req,
				   bool need_pump)
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
		kthread_queue_work(engine->kworker, &engine->pump_requests);

	spin_unlock_irqrestore(&engine->queue_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(crypto_transfer_cipher_request);

/**
 * crypto_transfer_cipher_request_to_engine - transfer one request to list
 * into the engine queue
 * @engine: the hardware engine
 * @req: the request need to be listed into the engine queue
 */
int crypto_transfer_cipher_request_to_engine(struct crypto_engine *engine,
					     struct ablkcipher_request *req)
{
	return crypto_transfer_cipher_request(engine, req, true);
}
EXPORT_SYMBOL_GPL(crypto_transfer_cipher_request_to_engine);

/**
 * crypto_transfer_hash_request - transfer the new request into the
 * enginequeue
 * @engine: the hardware engine
 * @req: the request need to be listed into the engine queue
 */
int crypto_transfer_hash_request(struct crypto_engine *engine,
				 struct ahash_request *req, bool need_pump)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&engine->queue_lock, flags);

	if (!engine->running) {
		spin_unlock_irqrestore(&engine->queue_lock, flags);
		return -ESHUTDOWN;
	}

	ret = ahash_enqueue_request(&engine->queue, req);

	if (!engine->busy && need_pump)
		kthread_queue_work(engine->kworker, &engine->pump_requests);

	spin_unlock_irqrestore(&engine->queue_lock, flags);
	return ret;
}
EXPORT_SYMBOL_GPL(crypto_transfer_hash_request);

/**
 * crypto_transfer_hash_request_to_engine - transfer one request to list
 * into the engine queue
 * @engine: the hardware engine
 * @req: the request need to be listed into the engine queue
 */
int crypto_transfer_hash_request_to_engine(struct crypto_engine *engine,
					   struct ahash_request *req)
{
	return crypto_transfer_hash_request(engine, req, true);
}
EXPORT_SYMBOL_GPL(crypto_transfer_hash_request_to_engine);

/**
 * crypto_finalize_cipher_request - finalize one request if the request is done
 * @engine: the hardware engine
 * @req: the request need to be finalized
 * @err: error number
 */
void crypto_finalize_cipher_request(struct crypto_engine *engine,
				    struct ablkcipher_request *req, int err)
{
	unsigned long flags;
	bool finalize_cur_req = false;
	int ret;

	spin_lock_irqsave(&engine->queue_lock, flags);
	if (engine->cur_req == &req->base)
		finalize_cur_req = true;
	spin_unlock_irqrestore(&engine->queue_lock, flags);

	if (finalize_cur_req) {
		if (engine->cur_req_prepared &&
		    engine->unprepare_cipher_request) {
			ret = engine->unprepare_cipher_request(engine, req);
			if (ret)
				dev_err(engine->dev, "failed to unprepare request\n");
		}
		spin_lock_irqsave(&engine->queue_lock, flags);
		engine->cur_req = NULL;
		engine->cur_req_prepared = false;
		spin_unlock_irqrestore(&engine->queue_lock, flags);
	}

	req->base.complete(&req->base, err);

	kthread_queue_work(engine->kworker, &engine->pump_requests);
}
EXPORT_SYMBOL_GPL(crypto_finalize_cipher_request);

/**
 * crypto_finalize_hash_request - finalize one request if the request is done
 * @engine: the hardware engine
 * @req: the request need to be finalized
 * @err: error number
 */
void crypto_finalize_hash_request(struct crypto_engine *engine,
				  struct ahash_request *req, int err)
{
	unsigned long flags;
	bool finalize_cur_req = false;
	int ret;

	spin_lock_irqsave(&engine->queue_lock, flags);
	if (engine->cur_req == &req->base)
		finalize_cur_req = true;
	spin_unlock_irqrestore(&engine->queue_lock, flags);

	if (finalize_cur_req) {
		if (engine->cur_req_prepared &&
		    engine->unprepare_hash_request) {
			ret = engine->unprepare_hash_request(engine, req);
			if (ret)
				dev_err(engine->dev, "failed to unprepare request\n");
		}
		spin_lock_irqsave(&engine->queue_lock, flags);
		engine->cur_req = NULL;
		engine->cur_req_prepared = false;
		spin_unlock_irqrestore(&engine->queue_lock, flags);
	}

	req->base.complete(&req->base, err);

	kthread_queue_work(engine->kworker, &engine->pump_requests);
}
EXPORT_SYMBOL_GPL(crypto_finalize_hash_request);

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
