// SPDX-License-Identifier: GPL-2.0
// KUnit tests for the SPI core DMA mapping error paths.
//
// A mapping error must clear all SG tables and *_sg_mapped flags while
// cur_{tx,rx}_dma_dev identify the devices used for the attempted mapping.
// Zero-length transfers make sg_alloc_table() fail with -EINVAL, providing
// deterministic failure injection without test hooks.

#include <kunit/device.h>
#include <kunit/test.h>
#include <linux/dma-mapping.h>
#include <linux/limits.h>
#include <linux/spi/spi.h>

#include "../internals.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

#define SPI_DMA_TEST_LEN		256
#define SPI_DMA_TEST_XFERS		2

struct spi_dma_test_ctx {
	struct spi_controller	*ctlr;
	struct spi_device	*spi;
	struct device		*dma_dev;
	struct device		*stale_dma_dev;
	struct spi_transfer	xfer[SPI_DMA_TEST_XFERS];
	struct spi_message	msg;
	void			*buf[SPI_DMA_TEST_XFERS * 2];
};

static bool spi_dma_test_can_dma(struct spi_controller *ctlr,
				 struct spi_device *spi,
				 struct spi_transfer *xfer)
{
	/* Opt every transfer into the core DMA mapping path. */
	return true;
}

/*
 * A bare controller is sufficient because the mapping helpers do not
 * dereference ctlr->dev. With dma_tx and dma_rx unset, both directions use
 * dma_map_dev, so the controller need not be registered.
 */
static struct spi_dma_test_ctx *spi_dma_test_ctx_new(struct kunit *test)
{
	struct spi_dma_test_ctx *ctx;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	ctx->dma_dev = kunit_device_register(test, "spi-dma-error-path");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->dma_dev);
	ctx->stale_dma_dev =
		kunit_device_register(test, "spi-dma-stale-device");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->stale_dma_dev);

	/* Keep both devices valid if an assertion aborts the test. */
	KUNIT_ASSERT_EQ(test, 0,
			dma_coerce_mask_and_coherent(ctx->dma_dev,
						     DMA_BIT_MASK(64)));
	KUNIT_ASSERT_EQ(test, 0,
			dma_coerce_mask_and_coherent(ctx->stale_dma_dev,
						     DMA_BIT_MASK(64)));

	ctx->ctlr = kunit_kzalloc(test, sizeof(*ctx->ctlr), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->ctlr);

	ctx->spi = kunit_kzalloc(test, sizeof(*ctx->spi), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->spi);

	ctx->ctlr->can_dma = spi_dma_test_can_dma;
	ctx->ctlr->dma_map_dev = ctx->dma_dev;
	/* Normally initialized by spi_register_controller(). */
	ctx->ctlr->max_dma_len = INT_MAX;

	ctx->spi->controller = ctx->ctlr;
	spi_message_init(&ctx->msg);
	ctx->msg.spi = ctx->spi;

	return ctx;
}

static void *spi_dma_test_buf(struct kunit *test, struct spi_dma_test_ctx *ctx,
			      unsigned int slot)
{
	KUNIT_ASSERT_LT(test, slot, ARRAY_SIZE(ctx->buf));

	ctx->buf[slot] = kunit_kzalloc(test, SPI_DMA_TEST_LEN, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx->buf[slot]);

	return ctx->buf[slot];
}

/*
 * Emulate DMA devices retained from an earlier message. Using valid devices
 * also lets the unfixed path reach the assertions instead of dereferencing
 * NULL during cleanup.
 */
static void spi_dma_test_pin_stale_dma_devs(struct spi_dma_test_ctx *ctx)
{
	ctx->ctlr->cur_tx_dma_dev = ctx->stale_dma_dev;
	ctx->ctlr->cur_rx_dma_dev = ctx->stale_dma_dev;
}

static void spi_dma_test_assert_dma_devs_published(struct kunit *test,
						   struct spi_dma_test_ctx *ctx)
{
	KUNIT_ASSERT_PTR_EQ(test, ctx->ctlr->cur_tx_dma_dev, ctx->dma_dev);
	KUNIT_ASSERT_PTR_EQ(test, ctx->ctlr->cur_rx_dma_dev, ctx->dma_dev);
}

static void spi_dma_test_assert_nothing_mapped(struct kunit *test,
					       struct spi_dma_test_ctx *ctx,
					       unsigned int nr_xfers)
{
	unsigned int i;

	for (i = 0; i < nr_xfers; i++) {
		KUNIT_ASSERT_FALSE_MSG(test, ctx->xfer[i].tx_sg_mapped,
				       "xfer[%u] still claims a TX mapping after __spi_map_msg() failed",
				       i);
		KUNIT_ASSERT_FALSE_MSG(test, ctx->xfer[i].rx_sg_mapped,
				       "xfer[%u] still claims an RX mapping after __spi_map_msg() failed",
				       i);
		KUNIT_EXPECT_NULL(test, ctx->xfer[i].tx_sg.sgl);
		KUNIT_EXPECT_EQ(test, ctx->xfer[i].tx_sg.orig_nents, 0U);
		KUNIT_EXPECT_EQ(test, ctx->xfer[i].tx_sg.nents, 0U);
		KUNIT_EXPECT_NULL(test, ctx->xfer[i].rx_sg.sgl);
		KUNIT_EXPECT_EQ(test, ctx->xfer[i].rx_sg.orig_nents, 0U);
		KUNIT_EXPECT_EQ(test, ctx->xfer[i].rx_sg.nents, 0U);
	}
}

/*
 * xfer0 maps TX and RX; zero-length xfer1 then fails its TX mapping.
 * The failure must unwind xfer0 and update cur_*_dma_dev.
 */
static void spi_dma_later_tx_fail_rolls_back_earlier(struct kunit *test)
{
	struct spi_dma_test_ctx *ctx = spi_dma_test_ctx_new(test);
	int ret;

	ctx->xfer[0].tx_buf = spi_dma_test_buf(test, ctx, 0);
	ctx->xfer[0].rx_buf = spi_dma_test_buf(test, ctx, 1);
	ctx->xfer[0].len = SPI_DMA_TEST_LEN;

	ctx->xfer[1].tx_buf = spi_dma_test_buf(test, ctx, 2);
	ctx->xfer[1].rx_buf = NULL;
	ctx->xfer[1].len = 0;			/* forces -EINVAL */

	spi_message_add_tail(&ctx->xfer[0], &ctx->msg);
	spi_message_add_tail(&ctx->xfer[1], &ctx->msg);

	spi_dma_test_pin_stale_dma_devs(ctx);

	ret = __spi_map_msg(ctx->ctlr, &ctx->msg);
	KUNIT_ASSERT_EQ(test, ret, -EINVAL);

	spi_dma_test_assert_dma_devs_published(test, ctx);
	spi_dma_test_assert_nothing_mapped(test, ctx, SPI_DMA_TEST_XFERS);

	KUNIT_EXPECT_EQ(test, 0, __spi_unmap_msg(ctx->ctlr, &ctx->msg));
}

/*
 * xfer0 maps TX and RX; zero-length RX-only xfer1 then fails.
 * The failure must unwind xfer0 without leaving either mapping flag set.
 */
static void spi_dma_later_rx_fail_rolls_back_earlier(struct kunit *test)
{
	struct spi_dma_test_ctx *ctx = spi_dma_test_ctx_new(test);
	int ret;

	ctx->xfer[0].tx_buf = spi_dma_test_buf(test, ctx, 0);
	ctx->xfer[0].rx_buf = spi_dma_test_buf(test, ctx, 1);
	ctx->xfer[0].len = SPI_DMA_TEST_LEN;

	ctx->xfer[1].tx_buf = NULL;
	ctx->xfer[1].rx_buf = spi_dma_test_buf(test, ctx, 2);
	ctx->xfer[1].len = 0;			/* forces -EINVAL */

	spi_message_add_tail(&ctx->xfer[0], &ctx->msg);
	spi_message_add_tail(&ctx->xfer[1], &ctx->msg);

	spi_dma_test_pin_stale_dma_devs(ctx);

	ret = __spi_map_msg(ctx->ctlr, &ctx->msg);
	KUNIT_ASSERT_EQ(test, ret, -EINVAL);

	spi_dma_test_assert_dma_devs_published(test, ctx);
	spi_dma_test_assert_nothing_mapped(test, ctx, SPI_DMA_TEST_XFERS);

	KUNIT_EXPECT_EQ(test, 0, __spi_unmap_msg(ctx->ctlr, &ctx->msg));
}

/* Ensure the error unwind does not affect successful mappings. */
static void spi_dma_map_success_publishes_dma_devs(struct kunit *test)
{
	struct spi_dma_test_ctx *ctx = spi_dma_test_ctx_new(test);
	int ret;

	ctx->xfer[0].tx_buf = spi_dma_test_buf(test, ctx, 0);
	ctx->xfer[0].rx_buf = spi_dma_test_buf(test, ctx, 1);
	ctx->xfer[0].len = SPI_DMA_TEST_LEN;

	spi_message_add_tail(&ctx->xfer[0], &ctx->msg);

	ret = __spi_map_msg(ctx->ctlr, &ctx->msg);
	KUNIT_ASSERT_EQ(test, ret, 0);

	KUNIT_EXPECT_TRUE(test, ctx->xfer[0].tx_sg_mapped);
	KUNIT_EXPECT_TRUE(test, ctx->xfer[0].rx_sg_mapped);
	KUNIT_EXPECT_PTR_EQ(test, ctx->ctlr->cur_tx_dma_dev, ctx->dma_dev);
	KUNIT_EXPECT_PTR_EQ(test, ctx->ctlr->cur_rx_dma_dev, ctx->dma_dev);

	KUNIT_EXPECT_EQ(test, 0, __spi_unmap_msg(ctx->ctlr, &ctx->msg));

	KUNIT_EXPECT_FALSE(test, ctx->xfer[0].tx_sg_mapped);
	KUNIT_EXPECT_FALSE(test, ctx->xfer[0].rx_sg_mapped);
	KUNIT_EXPECT_NULL(test, ctx->xfer[0].tx_sg.sgl);
	KUNIT_EXPECT_NULL(test, ctx->xfer[0].rx_sg.sgl);
}

/* A transfer without buffers requires no DMA mapping. */
static void spi_dma_map_nothing_is_success(struct kunit *test)
{
	struct spi_dma_test_ctx *ctx = spi_dma_test_ctx_new(test);
	int ret;

	ctx->xfer[0].tx_buf = NULL;
	ctx->xfer[0].rx_buf = NULL;
	ctx->xfer[0].len = SPI_DMA_TEST_LEN;

	spi_message_add_tail(&ctx->xfer[0], &ctx->msg);

	ret = __spi_map_msg(ctx->ctlr, &ctx->msg);
	KUNIT_EXPECT_EQ(test, ret, 0);

	spi_dma_test_assert_nothing_mapped(test, ctx, 1);
}

static struct kunit_case spi_dma_error_path_cases[] = {
	KUNIT_CASE(spi_dma_later_tx_fail_rolls_back_earlier),
	KUNIT_CASE(spi_dma_later_rx_fail_rolls_back_earlier),
	KUNIT_CASE(spi_dma_map_success_publishes_dma_devs),
	KUNIT_CASE(spi_dma_map_nothing_is_success),
	{}
};

static struct kunit_suite spi_dma_error_path_suite = {
	.name = "spi_dma",
	.test_cases = spi_dma_error_path_cases,
};

kunit_test_suite(spi_dma_error_path_suite);

MODULE_DESCRIPTION("KUnit tests for SPI core DMA mapping");
MODULE_LICENSE("GPL");
