// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
/*
 * Crypto driver for NVIDIA Security Engine in Tegra Chips
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>

#include <crypto/engine.h>

#include "tegra-se.h"

static struct host1x_bo *tegra_se_cmdbuf_get(struct host1x_bo *host_bo)
{
	struct tegra_se_cmdbuf *cmdbuf = container_of(host_bo, struct tegra_se_cmdbuf, bo);

	kref_get(&cmdbuf->ref);

	return host_bo;
}

static void tegra_se_cmdbuf_release(struct kref *ref)
{
	struct tegra_se_cmdbuf *cmdbuf = container_of(ref, struct tegra_se_cmdbuf, ref);

	dma_free_attrs(cmdbuf->dev, cmdbuf->size, cmdbuf->addr,
		       cmdbuf->iova, 0);

	kfree(cmdbuf);
}

static void tegra_se_cmdbuf_put(struct host1x_bo *host_bo)
{
	struct tegra_se_cmdbuf *cmdbuf = container_of(host_bo, struct tegra_se_cmdbuf, bo);

	kref_put(&cmdbuf->ref, tegra_se_cmdbuf_release);
}

static struct host1x_bo_mapping *
tegra_se_cmdbuf_pin(struct device *dev, struct host1x_bo *bo, enum dma_data_direction direction)
{
	struct tegra_se_cmdbuf *cmdbuf = container_of(bo, struct tegra_se_cmdbuf, bo);
	struct host1x_bo_mapping *map;
	int err;

	map = kzalloc(sizeof(*map), GFP_KERNEL);
	if (!map)
		return ERR_PTR(-ENOMEM);

	kref_init(&map->ref);
	map->bo = host1x_bo_get(bo);
	map->direction = direction;
	map->dev = dev;

	map->sgt = kzalloc(sizeof(*map->sgt), GFP_KERNEL);
	if (!map->sgt) {
		err = -ENOMEM;
		goto free;
	}

	err = dma_get_sgtable(dev, map->sgt, cmdbuf->addr,
			      cmdbuf->iova, cmdbuf->words * 4);
	if (err)
		goto free_sgt;

	err = dma_map_sgtable(dev, map->sgt, direction, 0);
	if (err)
		goto free_sgt;

	map->phys = sg_dma_address(map->sgt->sgl);
	map->size = cmdbuf->words * 4;
	map->chunks = err;

	return map;

free_sgt:
	sg_free_table(map->sgt);
	kfree(map->sgt);
free:
	kfree(map);
	return ERR_PTR(err);
}

static void tegra_se_cmdbuf_unpin(struct host1x_bo_mapping *map)
{
	if (!map)
		return;

	dma_unmap_sgtable(map->dev, map->sgt, map->direction, 0);
	sg_free_table(map->sgt);
	kfree(map->sgt);
	host1x_bo_put(map->bo);

	kfree(map);
}

static void *tegra_se_cmdbuf_mmap(struct host1x_bo *host_bo)
{
	struct tegra_se_cmdbuf *cmdbuf = container_of(host_bo, struct tegra_se_cmdbuf, bo);

	return cmdbuf->addr;
}

static void tegra_se_cmdbuf_munmap(struct host1x_bo *host_bo, void *addr)
{
}

static const struct host1x_bo_ops tegra_se_cmdbuf_ops = {
	.get = tegra_se_cmdbuf_get,
	.put = tegra_se_cmdbuf_put,
	.pin = tegra_se_cmdbuf_pin,
	.unpin = tegra_se_cmdbuf_unpin,
	.mmap = tegra_se_cmdbuf_mmap,
	.munmap = tegra_se_cmdbuf_munmap,
};

static struct tegra_se_cmdbuf *tegra_se_host1x_bo_alloc(struct tegra_se *se, ssize_t size)
{
	struct tegra_se_cmdbuf *cmdbuf;
	struct device *dev = se->dev->parent;

	cmdbuf = kzalloc(sizeof(*cmdbuf), GFP_KERNEL);
	if (!cmdbuf)
		return NULL;

	cmdbuf->addr = dma_alloc_attrs(dev, size, &cmdbuf->iova,
				       GFP_KERNEL, 0);
	if (!cmdbuf->addr)
		return NULL;

	cmdbuf->size = size;
	cmdbuf->dev  = dev;

	host1x_bo_init(&cmdbuf->bo, &tegra_se_cmdbuf_ops);
	kref_init(&cmdbuf->ref);

	return cmdbuf;
}

int tegra_se_host1x_submit(struct tegra_se *se, u32 size)
{
	struct host1x_job *job;
	int ret;

	job = host1x_job_alloc(se->channel, 1, 0, true);
	if (!job) {
		dev_err(se->dev, "failed to allocate host1x job\n");
		return -ENOMEM;
	}

	job->syncpt = host1x_syncpt_get(se->syncpt);
	job->syncpt_incrs = 1;
	job->client = &se->client;
	job->class = se->client.class;
	job->serialize = true;
	job->engine_fallback_streamid = se->stream_id;
	job->engine_streamid_offset = SE_STREAM_ID;

	se->cmdbuf->words = size;

	host1x_job_add_gather(job, &se->cmdbuf->bo, size, 0);

	ret = host1x_job_pin(job, se->dev);
	if (ret) {
		dev_err(se->dev, "failed to pin host1x job\n");
		goto job_put;
	}

	ret = host1x_job_submit(job);
	if (ret) {
		dev_err(se->dev, "failed to submit host1x job\n");
		goto job_unpin;
	}

	ret = host1x_syncpt_wait(job->syncpt, job->syncpt_end,
				 MAX_SCHEDULE_TIMEOUT, NULL);
	if (ret) {
		dev_err(se->dev, "host1x job timed out\n");
		return ret;
	}

	host1x_job_put(job);
	return 0;

job_unpin:
	host1x_job_unpin(job);
job_put:
	host1x_job_put(job);

	return ret;
}

static int tegra_se_client_init(struct host1x_client *client)
{
	struct tegra_se *se = container_of(client, struct tegra_se, client);
	int ret;

	se->channel = host1x_channel_request(&se->client);
	if (!se->channel) {
		dev_err(se->dev, "host1x channel map failed\n");
		return -ENODEV;
	}

	se->syncpt = host1x_syncpt_request(&se->client, 0);
	if (!se->syncpt) {
		dev_err(se->dev, "host1x syncpt allocation failed\n");
		ret = -EINVAL;
		goto channel_put;
	}

	se->syncpt_id =  host1x_syncpt_id(se->syncpt);

	se->cmdbuf = tegra_se_host1x_bo_alloc(se, SZ_4K);
	if (!se->cmdbuf) {
		ret = -ENOMEM;
		goto syncpt_put;
	}

	ret = se->hw->init_alg(se);
	if (ret) {
		dev_err(se->dev, "failed to register algorithms\n");
		goto cmdbuf_put;
	}

	return 0;

cmdbuf_put:
	tegra_se_cmdbuf_put(&se->cmdbuf->bo);
syncpt_put:
	host1x_syncpt_put(se->syncpt);
channel_put:
	host1x_channel_put(se->channel);

	return ret;
}

static int tegra_se_client_deinit(struct host1x_client *client)
{
	struct tegra_se *se = container_of(client, struct tegra_se, client);

	se->hw->deinit_alg(se);
	tegra_se_cmdbuf_put(&se->cmdbuf->bo);
	host1x_syncpt_put(se->syncpt);
	host1x_channel_put(se->channel);

	return 0;
}

static const struct host1x_client_ops tegra_se_client_ops = {
	.init = tegra_se_client_init,
	.exit = tegra_se_client_deinit,
};

static int tegra_se_host1x_register(struct tegra_se *se)
{
	INIT_LIST_HEAD(&se->client.list);
	se->client.dev = se->dev;
	se->client.ops = &tegra_se_client_ops;
	se->client.class = se->hw->host1x_class;
	se->client.num_syncpts = 1;

	host1x_client_register(&se->client);

	return 0;
}

static int tegra_se_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra_se *se;
	int ret;

	se = devm_kzalloc(dev, sizeof(*se), GFP_KERNEL);
	if (!se)
		return -ENOMEM;

	se->dev = dev;
	se->owner = TEGRA_GPSE_ID;
	se->hw = device_get_match_data(&pdev->dev);

	se->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(se->base))
		return PTR_ERR(se->base);

	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(39));
	platform_set_drvdata(pdev, se);

	se->clk = devm_clk_get_enabled(se->dev, NULL);
	if (IS_ERR(se->clk))
		return dev_err_probe(dev, PTR_ERR(se->clk),
				"failed to enable clocks\n");

	if (!tegra_dev_iommu_get_stream_id(dev, &se->stream_id))
		return dev_err_probe(dev, -ENODEV,
				"failed to get IOMMU stream ID\n");

	writel(se->stream_id, se->base + SE_STREAM_ID);

	se->engine = crypto_engine_alloc_init(dev, 0);
	if (!se->engine)
		return dev_err_probe(dev, -ENOMEM, "failed to init crypto engine\n");

	ret = crypto_engine_start(se->engine);
	if (ret) {
		crypto_engine_exit(se->engine);
		return dev_err_probe(dev, ret, "failed to start crypto engine\n");
	}

	ret = tegra_se_host1x_register(se);
	if (ret) {
		crypto_engine_stop(se->engine);
		crypto_engine_exit(se->engine);
		return dev_err_probe(dev, ret, "failed to init host1x params\n");
	}

	return 0;
}

static void tegra_se_remove(struct platform_device *pdev)
{
	struct tegra_se *se = platform_get_drvdata(pdev);

	crypto_engine_stop(se->engine);
	crypto_engine_exit(se->engine);
	iommu_fwspec_free(se->dev);
	host1x_client_unregister(&se->client);
}

static const struct tegra_se_regs tegra234_aes1_regs = {
	.config = SE_AES1_CFG,
	.op = SE_AES1_OPERATION,
	.last_blk = SE_AES1_LAST_BLOCK,
	.linear_ctr = SE_AES1_LINEAR_CTR,
	.aad_len = SE_AES1_AAD_LEN,
	.cryp_msg_len = SE_AES1_CRYPTO_MSG_LEN,
	.manifest = SE_AES1_KEYMANIFEST,
	.key_addr = SE_AES1_KEY_ADDR,
	.key_data = SE_AES1_KEY_DATA,
	.key_dst = SE_AES1_KEY_DST,
	.result = SE_AES1_CMAC_RESULT,
};

static const struct tegra_se_regs tegra234_hash_regs = {
	.config = SE_SHA_CFG,
	.op = SE_SHA_OPERATION,
	.manifest = SE_SHA_KEYMANIFEST,
	.key_addr = SE_SHA_KEY_ADDR,
	.key_data = SE_SHA_KEY_DATA,
	.key_dst = SE_SHA_KEY_DST,
	.result = SE_SHA_HASH_RESULT,
};

static const struct tegra_se_hw tegra234_aes_hw = {
	.regs = &tegra234_aes1_regs,
	.kac_ver = 1,
	.host1x_class = 0x3b,
	.init_alg = tegra_init_aes,
	.deinit_alg = tegra_deinit_aes,
};

static const struct tegra_se_hw tegra234_hash_hw = {
	.regs = &tegra234_hash_regs,
	.kac_ver = 1,
	.host1x_class = 0x3d,
	.init_alg = tegra_init_hash,
	.deinit_alg = tegra_deinit_hash,
};

static const struct of_device_id tegra_se_of_match[] = {
	{
		.compatible = "nvidia,tegra234-se-aes",
		.data = &tegra234_aes_hw
	}, {
		.compatible = "nvidia,tegra234-se-hash",
		.data = &tegra234_hash_hw,
	},
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_se_of_match);

static struct platform_driver tegra_se_driver = {
	.driver = {
		.name	= "tegra-se",
		.of_match_table = tegra_se_of_match,
	},
	.probe		= tegra_se_probe,
	.remove_new	= tegra_se_remove,
};

static int tegra_se_host1x_probe(struct host1x_device *dev)
{
	return host1x_device_init(dev);
}

static int tegra_se_host1x_remove(struct host1x_device *dev)
{
	host1x_device_exit(dev);

	return 0;
}

static struct host1x_driver tegra_se_host1x_driver = {
	.driver = {
		.name = "tegra-se-host1x",
	},
	.probe = tegra_se_host1x_probe,
	.remove = tegra_se_host1x_remove,
	.subdevs = tegra_se_of_match,
};

static int __init tegra_se_module_init(void)
{
	int ret;

	ret = host1x_driver_register(&tegra_se_host1x_driver);
	if (ret)
		return ret;

	return platform_driver_register(&tegra_se_driver);
}

static void __exit tegra_se_module_exit(void)
{
	host1x_driver_unregister(&tegra_se_host1x_driver);
	platform_driver_unregister(&tegra_se_driver);
}

module_init(tegra_se_module_init);
module_exit(tegra_se_module_exit);

MODULE_DESCRIPTION("NVIDIA Tegra Security Engine Driver");
MODULE_AUTHOR("Akhil R <akhilrajeev@nvidia.com>");
MODULE_LICENSE("GPL");
