// SPDX-License-Identifier: GPL-2.0
/*
 * sl3516-ce-rng.c - hardware cryptographic offloader for SL3516 SoC.
 *
 * Copyright (C) 2021 Corentin Labbe <clabbe@baylibre.com>
 *
 * This file handle the RNG found in the SL3516 crypto engine
 */
#include "sl3516-ce.h"
#include <linux/pm_runtime.h>
#include <linux/hw_random.h>

static int sl3516_ce_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct sl3516_ce_dev *ce;
	u32 *data = buf;
	size_t read = 0;
	int err;

	ce = container_of(rng, struct sl3516_ce_dev, trng);

#ifdef CONFIG_CRYPTO_DEV_SL3516_DEBUG
	ce->hwrng_stat_req++;
	ce->hwrng_stat_bytes += max;
#endif

	err = pm_runtime_get_sync(ce->dev);
	if (err < 0) {
		pm_runtime_put_noidle(ce->dev);
		return err;
	}

	while (read < max) {
		*data = readl(ce->base + IPSEC_RAND_NUM_REG);
		data++;
		read += 4;
	}

	pm_runtime_put(ce->dev);

	return read;
}

int sl3516_ce_rng_register(struct sl3516_ce_dev *ce)
{
	int ret;

	ce->trng.name = "SL3516 Crypto Engine RNG";
	ce->trng.read = sl3516_ce_rng_read;
	ce->trng.quality = 700;

	ret = hwrng_register(&ce->trng);
	if (ret)
		dev_err(ce->dev, "Fail to register the RNG\n");
	return ret;
}

void sl3516_ce_rng_unregister(struct sl3516_ce_dev *ce)
{
	hwrng_unregister(&ce->trng);
}
