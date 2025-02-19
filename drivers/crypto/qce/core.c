// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2010-2014, The Linux Foundation. All rights reserved.
 */

#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/interconnect.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <crypto/algapi.h>
#include <crypto/internal/hash.h>

#include "core.h"
#include "cipher.h"
#include "sha.h"
#include "aead.h"

#define QCE_MAJOR_VERSION5	0x05
#define QCE_QUEUE_LENGTH	1

#define QCE_DEFAULT_MEM_BANDWIDTH	393600

static const struct qce_algo_ops *qce_ops[] = {
#ifdef CONFIG_CRYPTO_DEV_QCE_SKCIPHER
	&skcipher_ops,
#endif
#ifdef CONFIG_CRYPTO_DEV_QCE_SHA
	&ahash_ops,
#endif
#ifdef CONFIG_CRYPTO_DEV_QCE_AEAD
	&aead_ops,
#endif
};

static void qce_unregister_algs(void *data)
{
	const struct qce_algo_ops *ops;
	struct qce_device *qce = data;
	int i;

	for (i = 0; i < ARRAY_SIZE(qce_ops); i++) {
		ops = qce_ops[i];
		ops->unregister_algs(qce);
	}
}

static int devm_qce_register_algs(struct qce_device *qce)
{
	const struct qce_algo_ops *ops;
	int i, j, ret = -ENODEV;

	for (i = 0; i < ARRAY_SIZE(qce_ops); i++) {
		ops = qce_ops[i];
		ret = ops->register_algs(qce);
		if (ret) {
			for (j = i - 1; j >= 0; j--)
				ops->unregister_algs(qce);
			return ret;
		}
	}

	return devm_add_action_or_reset(qce->dev, qce_unregister_algs, qce);
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
	int ret = 0, err;

	scoped_guard(mutex, &qce->lock) {
		if (req)
			ret = crypto_enqueue_request(&qce->queue, req);

		/* busy, do not dequeue request */
		if (qce->req)
			return ret;

		backlog = crypto_get_backlog(&qce->queue);
		async_req = crypto_dequeue_request(&qce->queue);
		if (async_req)
			qce->req = async_req;
	}

	if (!async_req)
		return ret;

	if (backlog) {
		scoped_guard(mutex, &qce->lock)
			crypto_request_complete(backlog, -EINPROGRESS);
	}

	err = qce_handle_request(async_req);
	if (err) {
		qce->result = err;
		schedule_work(&qce->done_work);
	}

	return ret;
}

static void qce_req_done_work(struct work_struct *work)
{
	struct qce_device *qce = container_of(work, struct qce_device,
					      done_work);
	struct crypto_async_request *req;

	scoped_guard(mutex, &qce->lock) {
		req = qce->req;
		qce->req = NULL;
	}

	if (req)
		crypto_request_complete(req, qce->result);

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
	schedule_work(&qce->done_work);
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

	/*
	 * Rx and tx pipes are treated as a pair inside CE.
	 * Pipe pair number depends on the actual BAM dma pipe
	 * that is used for transfers. The BAM dma pipes are passed
	 * from the device tree and used to derive the pipe pair
	 * id in the CE driver as follows.
	 * 	BAM dma pipes(rx, tx)		CE pipe pair id
	 *		0,1				0
	 *		2,3				1
	 *		4,5				2
	 *		6,7				3
	 *		...
	 */
	qce->pipe_pair_id = qce->dma.rxchan->chan_id >> 1;

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

	qce->core = devm_clk_get_optional_enabled(qce->dev, "core");
	if (IS_ERR(qce->core))
		return PTR_ERR(qce->core);

	qce->iface = devm_clk_get_optional_enabled(qce->dev, "iface");
	if (IS_ERR(qce->iface))
		return PTR_ERR(qce->iface);

	qce->bus = devm_clk_get_optional_enabled(qce->dev, "bus");
	if (IS_ERR(qce->bus))
		return PTR_ERR(qce->bus);

	qce->mem_path = devm_of_icc_get(qce->dev, "memory");
	if (IS_ERR(qce->mem_path))
		return PTR_ERR(qce->mem_path);

	ret = icc_set_bw(qce->mem_path, QCE_DEFAULT_MEM_BANDWIDTH, QCE_DEFAULT_MEM_BANDWIDTH);
	if (ret)
		return ret;

	ret = devm_qce_dma_request(qce->dev, &qce->dma);
	if (ret)
		return ret;

	ret = qce_check_version(qce);
	if (ret)
		return ret;

	ret = devm_mutex_init(qce->dev, &qce->lock);
	if (ret)
		return ret;

	INIT_WORK(&qce->done_work, qce_req_done_work);
	crypto_init_queue(&qce->queue, QCE_QUEUE_LENGTH);

	qce->async_req_enqueue = qce_async_request_enqueue;
	qce->async_req_done = qce_async_request_done;

	return devm_qce_register_algs(qce);
}

static const struct of_device_id qce_crypto_of_match[] = {
	{ .compatible = "qcom,crypto-v5.1", },
	{ .compatible = "qcom,crypto-v5.4", },
	{ .compatible = "qcom,qce", },
	{}
};
MODULE_DEVICE_TABLE(of, qce_crypto_of_match);

static struct platform_driver qce_crypto_driver = {
	.probe = qce_crypto_probe,
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
