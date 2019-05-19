// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * APM X-Gene SoC RNG Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Author: Rameshwar Prasad Sahu <rsahu@apm.com>
 *	   Shamal Winchurkar <swinchurkar@apm.com>
 *	   Feng Kan <fkan@apm.com>
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/hw_random.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/timer.h>

#define RNG_MAX_DATUM			4
#define MAX_TRY				100
#define XGENE_RNG_RETRY_COUNT		20
#define XGENE_RNG_RETRY_INTERVAL	10

/* RNG  Registers */
#define RNG_INOUT_0			0x00
#define RNG_INTR_STS_ACK		0x10
#define RNG_CONTROL			0x14
#define RNG_CONFIG			0x18
#define RNG_ALARMCNT			0x1c
#define RNG_FROENABLE			0x20
#define RNG_FRODETUNE			0x24
#define RNG_ALARMMASK			0x28
#define RNG_ALARMSTOP			0x2c
#define RNG_OPTIONS			0x78
#define RNG_EIP_REV			0x7c

#define MONOBIT_FAIL_MASK		BIT(7)
#define POKER_FAIL_MASK			BIT(6)
#define LONG_RUN_FAIL_MASK		BIT(5)
#define RUN_FAIL_MASK			BIT(4)
#define NOISE_FAIL_MASK			BIT(3)
#define STUCK_OUT_MASK			BIT(2)
#define SHUTDOWN_OFLO_MASK		BIT(1)
#define READY_MASK			BIT(0)

#define MAJOR_HW_REV_RD(src)		(((src) & 0x0f000000) >> 24)
#define MINOR_HW_REV_RD(src)		(((src) & 0x00f00000) >> 20)
#define HW_PATCH_LEVEL_RD(src)		(((src) & 0x000f0000) >> 16)
#define MAX_REFILL_CYCLES_SET(dst, src) \
			((dst & ~0xffff0000) | (((u32)src << 16) & 0xffff0000))
#define MIN_REFILL_CYCLES_SET(dst, src) \
			((dst & ~0x000000ff) | (((u32)src) & 0x000000ff))
#define ALARM_THRESHOLD_SET(dst, src) \
			((dst & ~0x000000ff) | (((u32)src) & 0x000000ff))
#define ENABLE_RNG_SET(dst, src) \
			((dst & ~BIT(10)) | (((u32)src << 10) & BIT(10)))
#define REGSPEC_TEST_MODE_SET(dst, src) \
			((dst & ~BIT(8)) | (((u32)src << 8) & BIT(8)))
#define MONOBIT_FAIL_MASK_SET(dst, src) \
			((dst & ~BIT(7)) | (((u32)src << 7) & BIT(7)))
#define POKER_FAIL_MASK_SET(dst, src) \
			((dst & ~BIT(6)) | (((u32)src << 6) & BIT(6)))
#define LONG_RUN_FAIL_MASK_SET(dst, src) \
			((dst & ~BIT(5)) | (((u32)src << 5) & BIT(5)))
#define RUN_FAIL_MASK_SET(dst, src) \
			((dst & ~BIT(4)) | (((u32)src << 4) & BIT(4)))
#define NOISE_FAIL_MASK_SET(dst, src) \
			((dst & ~BIT(3)) | (((u32)src << 3) & BIT(3)))
#define STUCK_OUT_MASK_SET(dst, src) \
			((dst & ~BIT(2)) | (((u32)src << 2) & BIT(2)))
#define SHUTDOWN_OFLO_MASK_SET(dst, src) \
			((dst & ~BIT(1)) | (((u32)src << 1) & BIT(1)))

struct xgene_rng_dev {
	u32 irq;
	void  __iomem *csr_base;
	u32 revision;
	u32 datum_size;
	u32 failure_cnt;	/* Failure count last minute */
	unsigned long failure_ts;/* First failure timestamp */
	struct timer_list failure_timer;
	struct device *dev;
	struct clk *clk;
};

static void xgene_rng_expired_timer(struct timer_list *t)
{
	struct xgene_rng_dev *ctx = from_timer(ctx, t, failure_timer);

	/* Clear failure counter as timer expired */
	disable_irq(ctx->irq);
	ctx->failure_cnt = 0;
	del_timer(&ctx->failure_timer);
	enable_irq(ctx->irq);
}

static void xgene_rng_start_timer(struct xgene_rng_dev *ctx)
{
	ctx->failure_timer.expires = jiffies + 120 * HZ;
	add_timer(&ctx->failure_timer);
}

/*
 * Initialize or reinit free running oscillators (FROs)
 */
static void xgene_rng_init_fro(struct xgene_rng_dev *ctx, u32 fro_val)
{
	writel(fro_val, ctx->csr_base + RNG_FRODETUNE);
	writel(0x00000000, ctx->csr_base + RNG_ALARMMASK);
	writel(0x00000000, ctx->csr_base + RNG_ALARMSTOP);
	writel(0xFFFFFFFF, ctx->csr_base + RNG_FROENABLE);
}

static void xgene_rng_chk_overflow(struct xgene_rng_dev *ctx)
{
	u32 val;

	val = readl(ctx->csr_base + RNG_INTR_STS_ACK);
	if (val & MONOBIT_FAIL_MASK)
		/*
		 * LFSR detected an out-of-bounds number of 1s after
		 * checking 20,000 bits (test T1 as specified in the
		 * AIS-31 standard)
		 */
		dev_err(ctx->dev, "test monobit failure error 0x%08X\n", val);
	if (val & POKER_FAIL_MASK)
		/*
		 * LFSR detected an out-of-bounds value in at least one
		 * of the 16 poker_count_X counters or an out of bounds sum
		 * of squares value after checking 20,000 bits (test T2 as
		 * specified in the AIS-31 standard)
		 */
		dev_err(ctx->dev, "test poker failure error 0x%08X\n", val);
	if (val & LONG_RUN_FAIL_MASK)
		/*
		 * LFSR detected a sequence of 34 identical bits
		 * (test T4 as specified in the AIS-31 standard)
		 */
		dev_err(ctx->dev, "test long run failure error 0x%08X\n", val);
	if (val & RUN_FAIL_MASK)
		/*
		 * LFSR detected an outof-bounds value for at least one
		 * of the running counters after checking 20,000 bits
		 * (test T3 as specified in the AIS-31 standard)
		 */
		dev_err(ctx->dev, "test run failure error 0x%08X\n", val);
	if (val & NOISE_FAIL_MASK)
		/* LFSR detected a sequence of 48 identical bits */
		dev_err(ctx->dev, "noise failure error 0x%08X\n", val);
	if (val & STUCK_OUT_MASK)
		/*
		 * Detected output data registers generated same value twice
		 * in a row
		 */
		dev_err(ctx->dev, "stuck out failure error 0x%08X\n", val);

	if (val & SHUTDOWN_OFLO_MASK) {
		u32 frostopped;

		/* FROs shut down after a second error event. Try recover. */
		if (++ctx->failure_cnt == 1) {
			/* 1st time, just recover */
			ctx->failure_ts = jiffies;
			frostopped = readl(ctx->csr_base + RNG_ALARMSTOP);
			xgene_rng_init_fro(ctx, frostopped);

			/*
			 * We must start a timer to clear out this error
			 * in case the system timer wrap around
			 */
			xgene_rng_start_timer(ctx);
		} else {
			/* 2nd time failure in lesser than 1 minute? */
			if (time_after(ctx->failure_ts + 60 * HZ, jiffies)) {
				dev_err(ctx->dev,
					"FRO shutdown failure error 0x%08X\n",
					val);
			} else {
				/* 2nd time failure after 1 minutes, recover */
				ctx->failure_ts = jiffies;
				ctx->failure_cnt = 1;
				/*
				 * We must start a timer to clear out this
				 * error in case the system timer wrap
				 * around
				 */
				xgene_rng_start_timer(ctx);
			}
			frostopped = readl(ctx->csr_base + RNG_ALARMSTOP);
			xgene_rng_init_fro(ctx, frostopped);
		}
	}
	/* Clear them all */
	writel(val, ctx->csr_base + RNG_INTR_STS_ACK);
}

static irqreturn_t xgene_rng_irq_handler(int irq, void *id)
{
	struct xgene_rng_dev *ctx = (struct xgene_rng_dev *) id;

	/* RNG Alarm Counter overflow */
	xgene_rng_chk_overflow(ctx);

	return IRQ_HANDLED;
}

static int xgene_rng_data_present(struct hwrng *rng, int wait)
{
	struct xgene_rng_dev *ctx = (struct xgene_rng_dev *) rng->priv;
	u32 i, val = 0;

	for (i = 0; i < XGENE_RNG_RETRY_COUNT; i++) {
		val = readl(ctx->csr_base + RNG_INTR_STS_ACK);
		if ((val & READY_MASK) || !wait)
			break;
		udelay(XGENE_RNG_RETRY_INTERVAL);
	}

	return (val & READY_MASK);
}

static int xgene_rng_data_read(struct hwrng *rng, u32 *data)
{
	struct xgene_rng_dev *ctx = (struct xgene_rng_dev *) rng->priv;
	int i;

	for (i = 0; i < ctx->datum_size; i++)
		data[i] = readl(ctx->csr_base + RNG_INOUT_0 + i * 4);

	/* Clear ready bit to start next transaction */
	writel(READY_MASK, ctx->csr_base + RNG_INTR_STS_ACK);

	return ctx->datum_size << 2;
}

static void xgene_rng_init_internal(struct xgene_rng_dev *ctx)
{
	u32 val;

	writel(0x00000000, ctx->csr_base + RNG_CONTROL);

	val = MAX_REFILL_CYCLES_SET(0, 10);
	val = MIN_REFILL_CYCLES_SET(val, 10);
	writel(val, ctx->csr_base + RNG_CONFIG);

	val = ALARM_THRESHOLD_SET(0, 0xFF);
	writel(val, ctx->csr_base + RNG_ALARMCNT);

	xgene_rng_init_fro(ctx, 0);

	writel(MONOBIT_FAIL_MASK |
		POKER_FAIL_MASK	|
		LONG_RUN_FAIL_MASK |
		RUN_FAIL_MASK |
		NOISE_FAIL_MASK |
		STUCK_OUT_MASK |
		SHUTDOWN_OFLO_MASK |
		READY_MASK, ctx->csr_base + RNG_INTR_STS_ACK);

	val = ENABLE_RNG_SET(0, 1);
	val = MONOBIT_FAIL_MASK_SET(val, 1);
	val = POKER_FAIL_MASK_SET(val, 1);
	val = LONG_RUN_FAIL_MASK_SET(val, 1);
	val = RUN_FAIL_MASK_SET(val, 1);
	val = NOISE_FAIL_MASK_SET(val, 1);
	val = STUCK_OUT_MASK_SET(val, 1);
	val = SHUTDOWN_OFLO_MASK_SET(val, 1);
	writel(val, ctx->csr_base + RNG_CONTROL);
}

static int xgene_rng_init(struct hwrng *rng)
{
	struct xgene_rng_dev *ctx = (struct xgene_rng_dev *) rng->priv;

	ctx->failure_cnt = 0;
	timer_setup(&ctx->failure_timer, xgene_rng_expired_timer, 0);

	ctx->revision = readl(ctx->csr_base + RNG_EIP_REV);

	dev_dbg(ctx->dev, "Rev %d.%d.%d\n",
		MAJOR_HW_REV_RD(ctx->revision),
		MINOR_HW_REV_RD(ctx->revision),
		HW_PATCH_LEVEL_RD(ctx->revision));

	dev_dbg(ctx->dev, "Options 0x%08X",
		readl(ctx->csr_base + RNG_OPTIONS));

	xgene_rng_init_internal(ctx);

	ctx->datum_size = RNG_MAX_DATUM;

	return 0;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id xgene_rng_acpi_match[] = {
	{ "APMC0D18", },
	{ }
};
MODULE_DEVICE_TABLE(acpi, xgene_rng_acpi_match);
#endif

static struct hwrng xgene_rng_func = {
	.name		= "xgene-rng",
	.init		= xgene_rng_init,
	.data_present	= xgene_rng_data_present,
	.data_read	= xgene_rng_data_read,
};

static int xgene_rng_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct xgene_rng_dev *ctx;
	int rc = 0;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = &pdev->dev;
	platform_set_drvdata(pdev, ctx);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	ctx->csr_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ctx->csr_base))
		return PTR_ERR(ctx->csr_base);

	rc = platform_get_irq(pdev, 0);
	if (rc < 0) {
		dev_err(&pdev->dev, "No IRQ resource\n");
		return rc;
	}
	ctx->irq = rc;

	dev_dbg(&pdev->dev, "APM X-Gene RNG BASE %p ALARM IRQ %d",
		ctx->csr_base, ctx->irq);

	rc = devm_request_irq(&pdev->dev, ctx->irq, xgene_rng_irq_handler, 0,
				dev_name(&pdev->dev), ctx);
	if (rc) {
		dev_err(&pdev->dev, "Could not request RNG alarm IRQ\n");
		return rc;
	}

	/* Enable IP clock */
	ctx->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(ctx->clk)) {
		dev_warn(&pdev->dev, "Couldn't get the clock for RNG\n");
	} else {
		rc = clk_prepare_enable(ctx->clk);
		if (rc) {
			dev_warn(&pdev->dev,
				 "clock prepare enable failed for RNG");
			return rc;
		}
	}

	xgene_rng_func.priv = (unsigned long) ctx;

	rc = hwrng_register(&xgene_rng_func);
	if (rc) {
		dev_err(&pdev->dev, "RNG registering failed error %d\n", rc);
		if (!IS_ERR(ctx->clk))
			clk_disable_unprepare(ctx->clk);
		return rc;
	}

	rc = device_init_wakeup(&pdev->dev, 1);
	if (rc) {
		dev_err(&pdev->dev, "RNG device_init_wakeup failed error %d\n",
			rc);
		if (!IS_ERR(ctx->clk))
			clk_disable_unprepare(ctx->clk);
		hwrng_unregister(&xgene_rng_func);
		return rc;
	}

	return 0;
}

static int xgene_rng_remove(struct platform_device *pdev)
{
	struct xgene_rng_dev *ctx = platform_get_drvdata(pdev);
	int rc;

	rc = device_init_wakeup(&pdev->dev, 0);
	if (rc)
		dev_err(&pdev->dev, "RNG init wakeup failed error %d\n", rc);
	if (!IS_ERR(ctx->clk))
		clk_disable_unprepare(ctx->clk);
	hwrng_unregister(&xgene_rng_func);

	return rc;
}

static const struct of_device_id xgene_rng_of_match[] = {
	{ .compatible = "apm,xgene-rng" },
	{ }
};

MODULE_DEVICE_TABLE(of, xgene_rng_of_match);

static struct platform_driver xgene_rng_driver = {
	.probe = xgene_rng_probe,
	.remove	= xgene_rng_remove,
	.driver = {
		.name		= "xgene-rng",
		.of_match_table = xgene_rng_of_match,
		.acpi_match_table = ACPI_PTR(xgene_rng_acpi_match),
	},
};

module_platform_driver(xgene_rng_driver);
MODULE_DESCRIPTION("APM X-Gene RNG driver");
MODULE_LICENSE("GPL");
