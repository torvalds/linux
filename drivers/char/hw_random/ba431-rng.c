// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Silex Insight

#include <linux/delay.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>

#define BA431_RESET_DELAY			1 /* usec */
#define BA431_RESET_READ_STATUS_TIMEOUT		1000 /* usec */
#define BA431_RESET_READ_STATUS_INTERVAL	10 /* usec */
#define BA431_READ_RETRY_INTERVAL		1 /* usec */

#define BA431_REG_CTRL				0x00
#define BA431_REG_FIFO_LEVEL			0x04
#define BA431_REG_STATUS			0x30
#define BA431_REG_FIFODATA			0x80

#define BA431_CTRL_ENABLE			BIT(0)
#define BA431_CTRL_SOFTRESET			BIT(8)

#define BA431_STATUS_STATE_MASK			(BIT(1) | BIT(2) | BIT(3))
#define BA431_STATUS_STATE_OFFSET		1

enum ba431_state {
	BA431_STATE_RESET,
	BA431_STATE_STARTUP,
	BA431_STATE_FIFOFULLON,
	BA431_STATE_FIFOFULLOFF,
	BA431_STATE_RUNNING,
	BA431_STATE_ERROR
};

struct ba431_trng {
	struct device *dev;
	void __iomem *base;
	struct hwrng rng;
	atomic_t reset_pending;
	struct work_struct reset_work;
};

static inline u32 ba431_trng_read_reg(struct ba431_trng *ba431, u32 reg)
{
	return ioread32(ba431->base + reg);
}

static inline void ba431_trng_write_reg(struct ba431_trng *ba431, u32 reg,
					u32 val)
{
	iowrite32(val, ba431->base + reg);
}

static inline enum ba431_state ba431_trng_get_state(struct ba431_trng *ba431)
{
	u32 status = ba431_trng_read_reg(ba431, BA431_REG_STATUS);

	return (status & BA431_STATUS_STATE_MASK) >> BA431_STATUS_STATE_OFFSET;
}

static int ba431_trng_is_in_error(struct ba431_trng *ba431)
{
	enum ba431_state state = ba431_trng_get_state(ba431);

	if ((state < BA431_STATE_STARTUP) ||
	    (state >= BA431_STATE_ERROR))
		return 1;

	return 0;
}

static int ba431_trng_reset(struct ba431_trng *ba431)
{
	int ret;

	/* Disable interrupts, random generation and enable the softreset */
	ba431_trng_write_reg(ba431, BA431_REG_CTRL, BA431_CTRL_SOFTRESET);
	udelay(BA431_RESET_DELAY);
	ba431_trng_write_reg(ba431, BA431_REG_CTRL, BA431_CTRL_ENABLE);

	/* Wait until the state changed */
	if (readx_poll_timeout(ba431_trng_is_in_error, ba431, ret, !ret,
			       BA431_RESET_READ_STATUS_INTERVAL,
			       BA431_RESET_READ_STATUS_TIMEOUT)) {
		dev_err(ba431->dev, "reset failed (state: %d)\n",
			ba431_trng_get_state(ba431));
		return -ETIMEDOUT;
	}

	dev_info(ba431->dev, "reset done\n");

	return 0;
}

static void ba431_trng_reset_work(struct work_struct *work)
{
	struct ba431_trng *ba431 = container_of(work, struct ba431_trng,
						reset_work);
	ba431_trng_reset(ba431);
	atomic_set(&ba431->reset_pending, 0);
}

static void ba431_trng_schedule_reset(struct ba431_trng *ba431)
{
	if (atomic_cmpxchg(&ba431->reset_pending, 0, 1))
		return;

	schedule_work(&ba431->reset_work);
}

static int ba431_trng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct ba431_trng *ba431 = container_of(rng, struct ba431_trng, rng);
	u32 *data = buf;
	unsigned int level, i;
	int n = 0;

	while (max > 0) {
		level = ba431_trng_read_reg(ba431, BA431_REG_FIFO_LEVEL);
		if (!level) {
			if (ba431_trng_is_in_error(ba431)) {
				ba431_trng_schedule_reset(ba431);
				break;
			}

			if (!wait)
				break;

			udelay(BA431_READ_RETRY_INTERVAL);
			continue;
		}

		i = level;
		do {
			data[n++] = ba431_trng_read_reg(ba431,
							BA431_REG_FIFODATA);
			max -= sizeof(*data);
		} while (--i && (max > 0));

		if (ba431_trng_is_in_error(ba431)) {
			n -= (level - i);
			ba431_trng_schedule_reset(ba431);
			break;
		}
	}

	n *= sizeof(data);
	return (n || !wait) ? n : -EIO;
}

static void ba431_trng_cleanup(struct hwrng *rng)
{
	struct ba431_trng *ba431 = container_of(rng, struct ba431_trng, rng);

	ba431_trng_write_reg(ba431, BA431_REG_CTRL, 0);
	cancel_work_sync(&ba431->reset_work);
}

static int ba431_trng_init(struct hwrng *rng)
{
	struct ba431_trng *ba431 = container_of(rng, struct ba431_trng, rng);

	return ba431_trng_reset(ba431);
}

static int ba431_trng_probe(struct platform_device *pdev)
{
	struct ba431_trng *ba431;
	int ret;

	ba431 = devm_kzalloc(&pdev->dev, sizeof(*ba431), GFP_KERNEL);
	if (!ba431)
		return -ENOMEM;

	ba431->dev = &pdev->dev;

	ba431->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ba431->base))
		return PTR_ERR(ba431->base);

	atomic_set(&ba431->reset_pending, 0);
	INIT_WORK(&ba431->reset_work, ba431_trng_reset_work);
	ba431->rng.name = pdev->name;
	ba431->rng.init = ba431_trng_init;
	ba431->rng.cleanup = ba431_trng_cleanup;
	ba431->rng.read = ba431_trng_read;

	platform_set_drvdata(pdev, ba431);

	ret = devm_hwrng_register(&pdev->dev, &ba431->rng);
	if (ret) {
		dev_err(&pdev->dev, "BA431 registration failed (%d)\n", ret);
		return ret;
	}

	dev_info(&pdev->dev, "BA431 TRNG registered\n");

	return 0;
}

static const struct of_device_id ba431_trng_dt_ids[] = {
	{ .compatible = "silex-insight,ba431-rng", .data = NULL },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ba431_trng_dt_ids);

static struct platform_driver ba431_trng_driver = {
	.driver = {
		.name = "ba431-rng",
		.of_match_table = ba431_trng_dt_ids,
	},
	.probe = ba431_trng_probe,
};

module_platform_driver(ba431_trng_driver);

MODULE_AUTHOR("Olivier Sobrie <olivier@sobrie.be>");
MODULE_DESCRIPTION("TRNG driver for Silex Insight BA431");
MODULE_LICENSE("GPL");
