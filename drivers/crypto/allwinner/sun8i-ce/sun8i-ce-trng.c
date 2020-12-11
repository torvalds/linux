// SPDX-License-Identifier: GPL-2.0
/*
 * sun8i-ce-trng.c - hardware cryptographic offloader for
 * Allwinner H3/A64/H5/H2+/H6/R40 SoC
 *
 * Copyright (C) 2015-2020 Corentin Labbe <clabbe@baylibre.com>
 *
 * This file handle the TRNG
 *
 * You could find a link for the datasheet in Documentation/arm/sunxi/README
 */
#include "sun8i-ce.h"
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <linux/hw_random.h>
/*
 * Note that according to the algorithm ID, 2 versions of the TRNG exists,
 * The first present in H3/H5/R40/A64 and the second present in H6.
 * This file adds support for both, but only the second is working
 * reliabily according to rngtest.
 **/

static int sun8i_ce_trng_read(struct hwrng *rng, void *data, size_t max, bool wait)
{
	struct sun8i_ce_dev *ce;
	dma_addr_t dma_dst;
	int err = 0;
	int flow = 3;
	unsigned int todo;
	struct sun8i_ce_flow *chan;
	struct ce_task *cet;
	u32 common;
	void *d;

	ce = container_of(rng, struct sun8i_ce_dev, trng);

	/* round the data length to a multiple of 32*/
	todo = max + 32;
	todo -= todo % 32;

	d = kzalloc(todo, GFP_KERNEL | GFP_DMA);
	if (!d)
		return -ENOMEM;

#ifdef CONFIG_CRYPTO_DEV_SUN8I_CE_DEBUG
	ce->hwrng_stat_req++;
	ce->hwrng_stat_bytes += todo;
#endif

	dma_dst = dma_map_single(ce->dev, d, todo, DMA_FROM_DEVICE);
	if (dma_mapping_error(ce->dev, dma_dst)) {
		dev_err(ce->dev, "Cannot DMA MAP DST\n");
		err = -EFAULT;
		goto err_dst;
	}

	err = pm_runtime_get_sync(ce->dev);
	if (err < 0) {
		pm_runtime_put_noidle(ce->dev);
		goto err_pm;
	}

	mutex_lock(&ce->rnglock);
	chan = &ce->chanlist[flow];

	cet = &chan->tl[0];
	memset(cet, 0, sizeof(struct ce_task));

	cet->t_id = cpu_to_le32(flow);
	common = ce->variant->trng | CE_COMM_INT;
	cet->t_common_ctl = cpu_to_le32(common);

	/* recent CE (H6) need length in bytes, in word otherwise */
	if (ce->variant->trng_t_dlen_in_bytes)
		cet->t_dlen = cpu_to_le32(todo);
	else
		cet->t_dlen = cpu_to_le32(todo / 4);

	cet->t_sym_ctl = 0;
	cet->t_asym_ctl = 0;

	cet->t_dst[0].addr = cpu_to_le32(dma_dst);
	cet->t_dst[0].len = cpu_to_le32(todo / 4);
	ce->chanlist[flow].timeout = todo;

	err = sun8i_ce_run_task(ce, 3, "TRNG");
	mutex_unlock(&ce->rnglock);

	pm_runtime_put(ce->dev);

err_pm:
	dma_unmap_single(ce->dev, dma_dst, todo, DMA_FROM_DEVICE);

	if (!err) {
		memcpy(data, d, max);
		err = max;
	}
	memzero_explicit(d, todo);
err_dst:
	kfree(d);
	return err;
}

int sun8i_ce_hwrng_register(struct sun8i_ce_dev *ce)
{
	int ret;

	if (ce->variant->trng == CE_ID_NOTSUPP) {
		dev_info(ce->dev, "TRNG not supported\n");
		return 0;
	}
	ce->trng.name = "sun8i Crypto Engine TRNG";
	ce->trng.read = sun8i_ce_trng_read;
	ce->trng.quality = 1000;

	ret = hwrng_register(&ce->trng);
	if (ret)
		dev_err(ce->dev, "Fail to register the TRNG\n");
	return ret;
}

void sun8i_ce_hwrng_unregister(struct sun8i_ce_dev *ce)
{
	if (ce->variant->trng == CE_ID_NOTSUPP)
		return;
	hwrng_unregister(&ce->trng);
}
