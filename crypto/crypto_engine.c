// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Handle async block request by crypto hardware engine.
 *
 * Copyright (C) 2016 Linaro, Inc.
 *
 * Author: Baolin Wang <baolin.wang@linaro.org>
 */

#include <crypto/internal/aead.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/engine.h>
#include <crypto/internal/hash.h>
#include <crypto/internal/kpp.h>
#include <crypto/internal/skcipher.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <uapi/linux/sched/types.h>
#include "internal.h"

#define CRYPTO_ENGINE_MAX_QLEN 10

/* Temporary algorithm flag used to indicate an updated driver. */
#define CRYPTO_ALG_ENGINE 0x200

struct crypto_engine_alg {
	struct crypto_alg base;
	struct crypto_engine_op op;
};

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

	/*
	 * If hardware cannot enqueue more requests
	 * and retry mechanism is not supported
	 * make sure we are completing the current request
	 */
	if (!engine->retry_support) {
		spin_lock_irqsave(&engine->queue_lock, flags);
		if (engine->cur_req == req) {
			engine->cur_req = NULL;
		}
		spin_unlock_irqrestore(&engine->queue_lock, flags);
	}

	lockdep_assert_in_softirq();
	crypto_request_complete(req, err);

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
	struct crypto_engine_alg *alg;
	struct crypto_engine_op *op;
	unsigned long flags;
	bool was_busy = false;
	int ret;

	spin_lock_irqsave(&engine->queue_lock, flags);

	/* Make sure we are not already running a request */
	if (!engine->retry_support && engine->cur_req)
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

start_request:
	/* Get the fist request from the engine queue to handle */
	backlog = crypto_get_backlog(&engine->queue);
	async_req = crypto_dequeue_request(&engine->queue);
	if (!async_req)
		goto out;

	/*
	 * If hardware doesn't support the retry mechanism,
	 * keep track of the request we are processing now.
	 * We'll need it on completion (crypto_finalize_request).
	 */
	if (!engine->retry_support)
		engine->cur_req = async_req;

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
			goto req_err_1;
		}
	}

	if (async_req->tfm->__crt_alg->cra_flags & CRYPTO_ALG_ENGINE) {
		alg = container_of(async_req->tfm->__crt_alg,
				   struct crypto_engine_alg, base);
		op = &alg->op;
	} else {
		dev_err(engine->dev, "failed to do request\n");
		ret = -EINVAL;
		goto req_err_1;
	}

	ret = op->do_one_request(engine, async_req);

	/* Request unsuccessfully executed by hardware */
	if (ret < 0) {
		/*
		 * If hardware queue is full (-ENOSPC), requeue request
		 * regardless of backlog flag.
		 * Otherwise, unprepare and complete the request.
		 */
		if (!engine->retry_support ||
		    (ret != -ENOSPC)) {
			dev_err(engine->dev,
				"Failed to do one request from queue: %d\n",
				ret);
			goto req_err_1;
		}
		spin_lock_irqsave(&engine->queue_lock, flags);
		/*
		 * If hardware was unable to execute request, enqueue it
		 * back in front of crypto-engine queue, to keep the order
		 * of requests.
		 */
		crypto_enqueue_request_head(&engine->queue, async_req);

		kthread_queue_work(engine->kworker, &engine->pump_requests);
		goto out;
	}

	goto retry;

req_err_1:
	crypto_request_complete(async_req, ret);

retry:
	if (backlog)
		crypto_request_complete(backlog, -EINPROGRESS);

	/* If retry mechanism is supported, send new requests to engine */
	if (engine->retry_support) {
		spin_lock_irqsave(&engine->queue_lock, flags);
		goto start_request;
	}
	return;

out:
	spin_unlock_irqrestore(&engine->queue_lock, flags);

	/*
	 * Batch requests is possible only if
	 * hardware can enqueue multiple requests
	 */
	if (engine->do_batch_requests) {
		ret = engine->do_batch_requests(engine);
		if (ret)
			dev_err(engine->dev, "failed to do batch requests: %d\n",
				ret);
	}

	return;
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
 * @need_pump: indicates whether queue the pump of request to kthread_work
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
 * crypto_transfer_kpp_request_to_engine - transfer one kpp_request to list
 * into the engine queue
 * @engine: the hardware engine
 * @req: the request need to be listed into the engine queue
 */
int crypto_transfer_kpp_request_to_engine(struct crypto_engine *engine,
					  struct kpp_request *req)
{
	return crypto_transfer_request_to_engine(engine, &req->base);
}
EXPORT_SYMBOL_GPL(crypto_transfer_kpp_request_to_engine);

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
 * crypto_finalize_kpp_request - finalize one kpp_request if the request is done
 * @engine: the hardware engine
 * @req: the request need to be finalized
 * @err: error number
 */
void crypto_finalize_kpp_request(struct crypto_engine *engine,
				 struct kpp_request *req, int err)
{
	return crypto_finalize_request(engine, &req->base, err);
}
EXPORT_SYMBOL_GPL(crypto_finalize_kpp_request);

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
 * crypto_engine_alloc_init_and_set - allocate crypto hardware engine structure
 * and initialize it by setting the maximum number of entries in the software
 * crypto-engine queue.
 * @dev: the device attached with one hardware engine
 * @retry_support: whether hardware has support for retry mechanism
 * @cbk_do_batch: pointer to a callback function to be invoked when executing
 *                a batch of requests.
 *                This has the form:
 *                callback(struct crypto_engine *engine)
 *                where:
 *                engine: the crypto engine structure.
 * @rt: whether this queue is set to run as a realtime task
 * @qlen: maximum size of the crypto-engine queue
 *
 * This must be called from context that can sleep.
 * Return: the crypto engine structure on success, else NULL.
 */
struct crypto_engine *crypto_engine_alloc_init_and_set(struct device *dev,
						       bool retry_support,
						       int (*cbk_do_batch)(struct crypto_engine *engine),
						       bool rt, int qlen)
{
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
	engine->retry_support = retry_support;
	engine->priv_data = dev;
	/*
	 * Batch requests is possible only if
	 * hardware has support for retry mechanism.
	 */
	engine->do_batch_requests = retry_support ? cbk_do_batch : NULL;

	snprintf(engine->name, sizeof(engine->name),
		 "%s-engine", dev_name(dev));

	crypto_init_queue(&engine->queue, qlen);
	spin_lock_init(&engine->queue_lock);

	engine->kworker = kthread_create_worker(0, "%s", engine->name);
	if (IS_ERR(engine->kworker)) {
		dev_err(dev, "failed to create crypto request pump task\n");
		return NULL;
	}
	kthread_init_work(&engine->pump_requests, crypto_pump_work);

	if (engine->rt) {
		dev_info(dev, "will run requests pump with realtime priority\n");
		sched_set_fifo(engine->kworker->task);
	}

	return engine;
}
EXPORT_SYMBOL_GPL(crypto_engine_alloc_init_and_set);

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
	return crypto_engine_alloc_init_and_set(dev, false, NULL, rt,
						CRYPTO_ENGINE_MAX_QLEN);
}
EXPORT_SYMBOL_GPL(crypto_engine_alloc_init);

/**
 * crypto_engine_exit - free the resources of hardware engine when exit
 * @engine: the hardware engine need to be freed
 */
void crypto_engine_exit(struct crypto_engine *engine)
{
	int ret;

	ret = crypto_engine_stop(engine);
	if (ret)
		return;

	kthread_destroy_worker(engine->kworker);
}
EXPORT_SYMBOL_GPL(crypto_engine_exit);

int crypto_engine_register_aead(struct aead_engine_alg *alg)
{
	if (!alg->op.do_one_request)
		return -EINVAL;

	alg->base.base.cra_flags |= CRYPTO_ALG_ENGINE;

	return crypto_register_aead(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_engine_register_aead);

void crypto_engine_unregister_aead(struct aead_engine_alg *alg)
{
	crypto_unregister_aead(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_engine_unregister_aead);

int crypto_engine_register_aeads(struct aead_engine_alg *algs, int count)
{
	int i, ret;

	for (i = 0; i < count; i++) {
		ret = crypto_engine_register_aead(&algs[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	crypto_engine_unregister_aeads(algs, i);

	return ret;
}
EXPORT_SYMBOL_GPL(crypto_engine_register_aeads);

void crypto_engine_unregister_aeads(struct aead_engine_alg *algs, int count)
{
	int i;

	for (i = count - 1; i >= 0; --i)
		crypto_engine_unregister_aead(&algs[i]);
}
EXPORT_SYMBOL_GPL(crypto_engine_unregister_aeads);

int crypto_engine_register_ahash(struct ahash_engine_alg *alg)
{
	if (!alg->op.do_one_request)
		return -EINVAL;

	alg->base.halg.base.cra_flags |= CRYPTO_ALG_ENGINE;

	return crypto_register_ahash(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_engine_register_ahash);

void crypto_engine_unregister_ahash(struct ahash_engine_alg *alg)
{
	crypto_unregister_ahash(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_engine_unregister_ahash);

int crypto_engine_register_ahashes(struct ahash_engine_alg *algs, int count)
{
	int i, ret;

	for (i = 0; i < count; i++) {
		ret = crypto_engine_register_ahash(&algs[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	crypto_engine_unregister_ahashes(algs, i);

	return ret;
}
EXPORT_SYMBOL_GPL(crypto_engine_register_ahashes);

void crypto_engine_unregister_ahashes(struct ahash_engine_alg *algs,
				      int count)
{
	int i;

	for (i = count - 1; i >= 0; --i)
		crypto_engine_unregister_ahash(&algs[i]);
}
EXPORT_SYMBOL_GPL(crypto_engine_unregister_ahashes);

int crypto_engine_register_akcipher(struct akcipher_engine_alg *alg)
{
	if (!alg->op.do_one_request)
		return -EINVAL;

	alg->base.base.cra_flags |= CRYPTO_ALG_ENGINE;

	return crypto_register_akcipher(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_engine_register_akcipher);

void crypto_engine_unregister_akcipher(struct akcipher_engine_alg *alg)
{
	crypto_unregister_akcipher(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_engine_unregister_akcipher);

int crypto_engine_register_kpp(struct kpp_engine_alg *alg)
{
	if (!alg->op.do_one_request)
		return -EINVAL;

	alg->base.base.cra_flags |= CRYPTO_ALG_ENGINE;

	return crypto_register_kpp(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_engine_register_kpp);

void crypto_engine_unregister_kpp(struct kpp_engine_alg *alg)
{
	crypto_unregister_kpp(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_engine_unregister_kpp);

int crypto_engine_register_skcipher(struct skcipher_engine_alg *alg)
{
	if (!alg->op.do_one_request)
		return -EINVAL;

	alg->base.base.cra_flags |= CRYPTO_ALG_ENGINE;

	return crypto_register_skcipher(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_engine_register_skcipher);

void crypto_engine_unregister_skcipher(struct skcipher_engine_alg *alg)
{
	return crypto_unregister_skcipher(&alg->base);
}
EXPORT_SYMBOL_GPL(crypto_engine_unregister_skcipher);

int crypto_engine_register_skciphers(struct skcipher_engine_alg *algs,
				     int count)
{
	int i, ret;

	for (i = 0; i < count; i++) {
		ret = crypto_engine_register_skcipher(&algs[i]);
		if (ret)
			goto err;
	}

	return 0;

err:
	crypto_engine_unregister_skciphers(algs, i);

	return ret;
}
EXPORT_SYMBOL_GPL(crypto_engine_register_skciphers);

void crypto_engine_unregister_skciphers(struct skcipher_engine_alg *algs,
					int count)
{
	int i;

	for (i = count - 1; i >= 0; --i)
		crypto_engine_unregister_skcipher(&algs[i]);
}
EXPORT_SYMBOL_GPL(crypto_engine_unregister_skciphers);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Crypto hardware engine framework");
