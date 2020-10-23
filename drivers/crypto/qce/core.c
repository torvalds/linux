// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <crypto/algapi.h>
#include <crypto/internal/hash.h>
#include <crypto/sha.h>

#include "core.h"
#include "cipher.h"
#include "sha.h"

#define QCE_MAJOR_VERSION5	0x05
#define QCE_QUEUE_LENGTH	1

static const struct qce_algo_ops *qce_ops[] = {
#ifdef CONFIG_CRYPTO_DEV_QCE_SKCIPHER
	&skcipher_ops,
#endif
#ifdef CONFIG_CRYPTO_DEV_QCE_SHA
	&ahash_ops,
#endif
};

static void qce_unregister_algs(struct qce_device *qce)
{
	const struct qce_algo_ops *ops;
	int i;

	for (i = 0; i < ARRAY_SIZE(qce_ops); i++) {
		ops = qce_ops[i];
		ops->unregister_algs(qce);
	}
}

static int qce_register_algs(struct qce_device *qce)
{
	const struct qce_algo_ops *ops;
	int i, ret = -ENODEV;

	for (i = 0; i < ARRAY_SIZE(qce_ops); i++) {
		ops = qce_ops[i];
		ret = ops->register_algs(qce);
		if (ret)
			break;
	}

	return ret;
}

static int qce_handle_request(struct crypto_async_request *async_req)
{
	int ret = -EINVAL, i;
	const struct qce_algo_ops *ops;
	u32 type = crypto_tfm_alg_type(async_req->tfm);

	for (i = 0; i < ARRAY_SIZE(qce_ops); i++) {
		ops = qce_ops[i];
		if (type != ops->type)
			continue;
		ret = ops->async_req_handle(async_req);
		break;
	}

	return ret;
}

static int qce_handle_queue(struct qce_device *qce,
			    struct crypto_async_request *req)
{
	struct crypto_async_request *async_req, *backlog;
	unsigned long flags;
	int ret = 0, err;

	spin_lock_irqsave(&qce->lock, flags);

	if (req)
		ret = crypto_enqueue_request(&qce->queue, req);

	/* busy, do not dequeue request */
	if (qce->req) {
		spin_unlock_irqrestore(&qce->lock, flags);
		return ret;
	}

	backlog = crypto_get_backlog(&qce->queue);
	async_req = crypto_dequeue_request(&qce->queue);
	if (async_req)
		qce->req = async_req;

	spin_unlock_irqrestore(&qce->lock, flags);

	if (!async_req)
		return ret;

	if (backlog) {
		spin_lock_bh(&qce->lock);
		backlog->complete(backlog, -EINPROGRESS);
		spin_unlock_bh(&qce->lock);
	}

	err = qce_handle_request(async_req);
	if (err) {
		qce->result = err;
		tasklet_schedule(&qce->done_tasklet);
	}

	return ret;
}

static void qce_tasklet_req_done(unsigned long data)
{
	struct qce_device *qce = (struct qce_device *)data;
	struct crypto_async_request *req;
	unsigned long flags;

	spin_lock_irqsave(&qce->lock, flags);
	req = qce->req;
	qce->req = NULL;
	spin_unlock_irqrestore(&qce->lock, flags);

	if (req)
		req->complete(req, qce->result);

	qce_handle_queue(qce, NULL);
}

static int qce_async_request_enqueue(struct qce_device *qce,
				     struct crypto_async_request *req)
{
	return qce_handle_queue(qce, req);
}

static void qce_async_request_done(struct qce_device *qce, int ret)
{
	qce->result = ret;
	tasklet_schedule(&qce->done_tasklet);
}

static int qce_check_version(struct qce_device *qce)
{
	u32 major, minor, step;

	qce_get_version(qce, &major, &minor, &step);

	/*
	 * the driver does not support v5 with minor 0 because it has special
	 * alignment requirements.
	 */
	if (major != QCE_MAJOR_VERSION5 || minor == 0)
		return -ENODEV;

	qce->burst_size = QCE_BAM_BURST_SIZE;
	qce->pipe_pair_id = 1;

	dev_dbg(qce->dev, "Crypto device found, version %d.%d.%d\n",
		major, minor, step);

	return 0;
}

static int qce_crypto_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct qce_device *qce;
	int ret;

	qce = devm_kzalloc(dev, sizeof(*qce), GFP_KERNEL);
	if (!qce)
		return -ENOMEM;

	qce->dev = dev;
	platform_set_drvdata(pdev, qce);

	qce->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(qce->base))
		return PTR_ERR(qce->base);

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret < 0)
		return ret;

	qce->core = devm_clk_get(qce->dev, "core");
	if (IS_ERR(qce->core))
		return PTR_ERR(qce->core);

	qce->iface = devm_clk_get(qce->dev, "iface");
	if (IS_ERR(qce->iface))
		return PTR_ERR(qce->iface);

	qce->bus = devm_clk_get(qce->dev, "bus");
	if (IS_ERR(qce->bus))
		return PTR_ERR(qce->bus);

	ret = clk_prepare_enable(qce->core);
	if (ret)
		return ret;

	ret = clk_prepare_enable(qce->iface);
	if (ret)
		goto err_clks_core;

	ret = clk_prepare_enable(qce->bus);
	if (ret)
		goto err_clks_iface;

	ret = qce_dma_request(qce->dev, &qce->dma);
	if (ret)
		goto err_clks;

	ret = qce_check_version(qce);
	if (ret)
		goto err_clks;

	spin_lock_init(&qce->lock);
	tasklet_init(&qce->done_tasklet, qce_tasklet_req_done,
		     (unsigned long)qce);
	crypto_init_queue(&qce->queue, QCE_QUEUE_LENGTH);

	qce->async_req_enqueue = qce_async_request_enqueue;
	qce->async_req_done = qce_async_request_done;

	ret = qce_register_algs(qce);
	if (ret)
		goto err_dma;

	return 0;

err_dma:
	qce_dma_release(&qce->dma);
err_clks:
	clk_disable_unprepare(qce->bus);
err_clks_iface:
	clk_disable_unprepare(qce->iface);
err_clks_core:
	clk_disable_unprepare(qce->core);
	return ret;
}

static int qce_crypto_remove(struct platform_device *pdev)
{
	struct qce_device *qce = platform_get_drvdata(pdev);

	tasklet_kill(&qce->done_tasklet);
	qce_unregister_algs(qce);
	qce_dma_release(&qce->dma);
	clk_disable_unprepare(qce->bus);
	clk_disable_unprepare(qce->iface);
	clk_disable_unprepare(qce->core);
	return 0;
}

static const struct of_device_id qce_crypto_of_match[] = {
	{ .compatible = "qcom,crypto-v5.1", },
	{}
};
MODULE_DEVICE_TABLE(of, qce_crypto_of_match);

static struct platform_driver qce_crypto_driver = {
	.probe = qce_crypto_probe,
	.remove = qce_crypto_remove,
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = qce_crypto_of_match,
	},
};
module_platform_driver(qce_crypto_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Qualcomm crypto engine driver");
MODULE_ALIAS("platform:" KBUILD_MODNAME);
MODULE_AUTHOR("The Linux Foundation");
