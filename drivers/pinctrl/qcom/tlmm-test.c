// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "tlmm-test: " fmt

#include <kunit/test.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>

/*
 * This TLMM test module serves the purpose of validating that the TLMM driver
 * (pinctrl-msm) delivers expected number of interrupts in response to changing
 * GPIO state.
 *
 * To achieve this without external equipment the test takes a module parameter
 * "gpio", which the tester is expected to specify an unused and non-connected
 * pin. The GPIO state is then driven by adjusting the bias of the pin, at
 * suitable times through the different test cases.
 *
 * Upon execution, the test initialization will find the TLMM node (subject to
 * tlmm_of_match[] allow listing) and create the necessary references
 * dynamically, rather then relying on e.g. Devicetree and phandles.
 */

#define MSM_PULL_MASK		GENMASK(2, 0)
#define MSM_PULL_DOWN		1
#define MSM_PULL_UP		3
#define TLMM_REG_SIZE		0x1000

static int tlmm_test_gpio = -1;
module_param_named(gpio, tlmm_test_gpio, int, 0600);

static struct {
	void __iomem *base;
	void __iomem *reg;
	int irq;

	u32 low_val;
	u32 high_val;
} tlmm_suite;

/**
 * struct tlmm_test_priv - Per-test context
 * @intr_count:		number of times hard handler was hit with TLMM_TEST_COUNT op set
 * @thread_count:	number of times thread handler was hit with TLMM_TEST_COUNT op set
 * @intr_op:		operations to be performed by the hard IRQ handler
 * @intr_op_remain:	number of times the TLMM_TEST_THEN_* operations should be
 *			performed by the hard IRQ handler
 * @thread_op:		operations to be performed by the threaded IRQ handler
 * @thread_op_remain:	number of times the TLMM_TEST_THEN_* operations should
 *			be performed by the threaded IRQ handler
 */
struct tlmm_test_priv {
	atomic_t intr_count;
	atomic_t thread_count;

	unsigned int intr_op;
	atomic_t intr_op_remain;

	unsigned int thread_op;
	atomic_t thread_op_remain;
};

/* Operation masks for @intr_op and @thread_op */
#define TLMM_TEST_COUNT		BIT(0)
#define TLMM_TEST_OUTPUT_LOW	BIT(1)
#define TLMM_TEST_OUTPUT_HIGH	BIT(2)
#define TLMM_TEST_THEN_HIGH	BIT(3)
#define TLMM_TEST_THEN_LOW	BIT(4)
#define TLMM_TEST_WAKE_THREAD	BIT(5)

static void tlmm_output_low(void)
{
	writel(tlmm_suite.low_val, tlmm_suite.reg);
	readl(tlmm_suite.reg);
}

static void tlmm_output_high(void)
{
	writel(tlmm_suite.high_val, tlmm_suite.reg);
	readl(tlmm_suite.reg);
}

static irqreturn_t tlmm_test_intr_fn(int irq, void *dev_id)
{
	struct tlmm_test_priv *priv = dev_id;

	if (priv->intr_op & TLMM_TEST_COUNT)
		atomic_inc(&priv->intr_count);

	if (priv->intr_op & TLMM_TEST_OUTPUT_LOW)
		tlmm_output_low();
	if (priv->intr_op & TLMM_TEST_OUTPUT_HIGH)
		tlmm_output_high();

	if (atomic_dec_if_positive(&priv->intr_op_remain) > 0) {
		udelay(1);

		if (priv->intr_op & TLMM_TEST_THEN_LOW)
			tlmm_output_low();
		if (priv->intr_op & TLMM_TEST_THEN_HIGH)
			tlmm_output_high();
	}

	return priv->intr_op & TLMM_TEST_WAKE_THREAD ? IRQ_WAKE_THREAD : IRQ_HANDLED;
}

static irqreturn_t tlmm_test_intr_thread_fn(int irq, void *dev_id)
{
	struct tlmm_test_priv *priv = dev_id;

	if (priv->thread_op & TLMM_TEST_COUNT)
		atomic_inc(&priv->thread_count);

	if (priv->thread_op & TLMM_TEST_OUTPUT_LOW)
		tlmm_output_low();
	if (priv->thread_op & TLMM_TEST_OUTPUT_HIGH)
		tlmm_output_high();

	if (atomic_dec_if_positive(&priv->thread_op_remain) > 0) {
		udelay(1);
		if (priv->thread_op & TLMM_TEST_THEN_LOW)
			tlmm_output_low();
		if (priv->thread_op & TLMM_TEST_THEN_HIGH)
			tlmm_output_high();
	}

	return IRQ_HANDLED;
}

static void tlmm_test_request_hard_irq(struct kunit *test, unsigned long irqflags)
{
	struct tlmm_test_priv *priv = test->priv;
	int ret;

	ret = request_irq(tlmm_suite.irq, tlmm_test_intr_fn, irqflags, test->name, priv);
	KUNIT_EXPECT_EQ(test, ret, 0);
}

static void tlmm_test_request_threaded_irq(struct kunit *test, unsigned long irqflags)
{
	struct tlmm_test_priv *priv = test->priv;
	int ret;

	ret = request_threaded_irq(tlmm_suite.irq,
				   tlmm_test_intr_fn, tlmm_test_intr_thread_fn,
				   irqflags, test->name, priv);

	KUNIT_EXPECT_EQ(test, ret, 0);
}

static void tlmm_test_silent(struct kunit *test, unsigned long irqflags)
{
	struct tlmm_test_priv *priv = test->priv;

	priv->intr_op = TLMM_TEST_COUNT;

	/* GPIO line at non-triggering level */
	if (irqflags == IRQF_TRIGGER_LOW || irqflags == IRQF_TRIGGER_FALLING)
		tlmm_output_high();
	else
		tlmm_output_low();

	tlmm_test_request_hard_irq(test, irqflags);
	msleep(100);
	free_irq(tlmm_suite.irq, priv);

	KUNIT_ASSERT_EQ(test, atomic_read(&priv->intr_count), 0);
}

/*
 * Test that no RISING interrupts are triggered on a silent pin
 */
static void tlmm_test_silent_rising(struct kunit *test)
{
	tlmm_test_silent(test, IRQF_TRIGGER_RISING);
}

/*
 * Test that no FALLING interrupts are triggered on a silent pin
 */
static void tlmm_test_silent_falling(struct kunit *test)
{
	tlmm_test_silent(test, IRQF_TRIGGER_FALLING);
}

/*
 * Test that no LOW interrupts are triggered on a silent pin
 */
static void tlmm_test_silent_low(struct kunit *test)
{
	tlmm_test_silent(test, IRQF_TRIGGER_LOW);
}

/*
 * Test that no HIGH interrupts are triggered on a silent pin
 */
static void tlmm_test_silent_high(struct kunit *test)
{
	tlmm_test_silent(test, IRQF_TRIGGER_HIGH);
}

/*
 * Square wave with 10 high pulses, assert that we get 10 rising interrupts
 */
static void tlmm_test_rising(struct kunit *test)
{
	struct tlmm_test_priv *priv = test->priv;
	int i;

	priv->intr_op = TLMM_TEST_COUNT;

	tlmm_output_low();

	tlmm_test_request_hard_irq(test, IRQF_TRIGGER_RISING);
	for (i = 0; i < 10; i++) {
		tlmm_output_low();
		msleep(20);
		tlmm_output_high();
		msleep(20);
	}

	free_irq(tlmm_suite.irq, priv);

	KUNIT_ASSERT_EQ(test, atomic_read(&priv->intr_count), 10);
}

/*
 * Square wave with 10 low pulses, assert that we get 10 falling interrupts
 */
static void tlmm_test_falling(struct kunit *test)
{
	struct tlmm_test_priv *priv = test->priv;
	int i;

	priv->intr_op = TLMM_TEST_COUNT;

	tlmm_output_high();

	tlmm_test_request_hard_irq(test, IRQF_TRIGGER_FALLING);
	for (i = 0; i < 10; i++) {
		tlmm_output_high();
		msleep(20);
		tlmm_output_low();
		msleep(20);
	}
	free_irq(tlmm_suite.irq, priv);

	KUNIT_ASSERT_EQ(test, atomic_read(&priv->intr_count), 10);
}

/*
 * Drive line low 10 times, handler drives it high to "clear the interrupt
 * source", assert we get 10 interrupts
 */
static void tlmm_test_low(struct kunit *test)
{
	struct tlmm_test_priv *priv = test->priv;
	int i;

	priv->intr_op = TLMM_TEST_COUNT | TLMM_TEST_OUTPUT_HIGH;
	atomic_set(&priv->intr_op_remain, 9);

	tlmm_output_high();

	tlmm_test_request_hard_irq(test, IRQF_TRIGGER_LOW);
	for (i = 0; i < 10; i++) {
		msleep(20);
		tlmm_output_low();
	}
	msleep(100);
	free_irq(tlmm_suite.irq, priv);

	KUNIT_ASSERT_EQ(test, atomic_read(&priv->intr_count), 10);
}

/*
 * Drive line high 10 times, handler drives it low to "clear the interrupt
 * source", assert we get 10 interrupts
 */
static void tlmm_test_high(struct kunit *test)
{
	struct tlmm_test_priv *priv = test->priv;
	int i;

	priv->intr_op = TLMM_TEST_COUNT | TLMM_TEST_OUTPUT_LOW;
	atomic_set(&priv->intr_op_remain, 9);

	tlmm_output_low();

	tlmm_test_request_hard_irq(test, IRQF_TRIGGER_HIGH);
	for (i = 0; i < 10; i++) {
		msleep(20);
		tlmm_output_high();
	}
	msleep(100);
	free_irq(tlmm_suite.irq, priv);

	KUNIT_ASSERT_EQ(test, atomic_read(&priv->intr_count), 10);
}

/*
 * Handler drives GPIO high to "clear the interrupt source", then low to
 * simulate a new interrupt, repeated 10 times, assert we get 10 interrupts
 */
static void tlmm_test_falling_in_handler(struct kunit *test)
{
	struct tlmm_test_priv *priv = test->priv;

	priv->intr_op = TLMM_TEST_COUNT | TLMM_TEST_OUTPUT_HIGH | TLMM_TEST_THEN_LOW;
	atomic_set(&priv->intr_op_remain, 10);

	tlmm_output_high();

	tlmm_test_request_hard_irq(test, IRQF_TRIGGER_FALLING);
	msleep(20);
	tlmm_output_low();
	msleep(100);
	free_irq(tlmm_suite.irq, priv);

	KUNIT_ASSERT_EQ(test, atomic_read(&priv->intr_count), 10);
}

/*
 * Handler drives GPIO low to "clear the interrupt source", then high to
 * simulate a new interrupt, repeated 10 times, assert we get 10 interrupts
 */
static void tlmm_test_rising_in_handler(struct kunit *test)
{
	struct tlmm_test_priv *priv = test->priv;

	priv->intr_op = TLMM_TEST_COUNT | TLMM_TEST_OUTPUT_LOW | TLMM_TEST_THEN_HIGH;
	atomic_set(&priv->intr_op_remain, 10);

	tlmm_output_low();

	tlmm_test_request_hard_irq(test, IRQF_TRIGGER_RISING);
	msleep(20);
	tlmm_output_high();
	msleep(100);
	free_irq(tlmm_suite.irq, priv);

	KUNIT_ASSERT_EQ(test, atomic_read(&priv->intr_count), 10);
}

/*
 * Square wave with 10 high pulses, assert that we get 10 rising hard and
 * 10 threaded interrupts
 */
static void tlmm_test_thread_rising(struct kunit *test)
{
	struct tlmm_test_priv *priv = test->priv;
	int i;

	priv->intr_op = TLMM_TEST_COUNT | TLMM_TEST_WAKE_THREAD;
	priv->thread_op = TLMM_TEST_COUNT;

	tlmm_output_low();

	tlmm_test_request_threaded_irq(test, IRQF_TRIGGER_RISING);
	for (i = 0; i < 10; i++) {
		tlmm_output_low();
		msleep(20);
		tlmm_output_high();
		msleep(20);
	}
	free_irq(tlmm_suite.irq, priv);

	KUNIT_ASSERT_EQ(test, atomic_read(&priv->intr_count), 10);
	KUNIT_ASSERT_EQ(test, atomic_read(&priv->thread_count), 10);
}

/*
 * Square wave with 10 low pulses, assert that we get 10 falling interrupts
 */
static void tlmm_test_thread_falling(struct kunit *test)
{
	struct tlmm_test_priv *priv = test->priv;
	int i;

	priv->intr_op = TLMM_TEST_COUNT | TLMM_TEST_WAKE_THREAD;
	priv->thread_op = TLMM_TEST_COUNT;

	tlmm_output_high();

	tlmm_test_request_threaded_irq(test, IRQF_TRIGGER_FALLING);
	for (i = 0; i < 10; i++) {
		tlmm_output_high();
		msleep(20);
		tlmm_output_low();
		msleep(20);
	}
	free_irq(tlmm_suite.irq, priv);

	KUNIT_ASSERT_EQ(test, atomic_read(&priv->intr_count), 10);
	KUNIT_ASSERT_EQ(test, atomic_read(&priv->thread_count), 10);
}

/*
 * Drive line high 10 times, threaded handler drives it low to "clear the
 * interrupt source", assert we get 10 interrupts
 */
static void tlmm_test_thread_high(struct kunit *test)
{
	struct tlmm_test_priv *priv = test->priv;
	int i;

	priv->intr_op = TLMM_TEST_COUNT | TLMM_TEST_WAKE_THREAD;
	priv->thread_op = TLMM_TEST_COUNT | TLMM_TEST_OUTPUT_LOW;

	tlmm_output_low();

	tlmm_test_request_threaded_irq(test, IRQF_TRIGGER_HIGH | IRQF_ONESHOT);
	for (i = 0; i < 10; i++) {
		tlmm_output_high();
		msleep(20);
	}
	free_irq(tlmm_suite.irq, priv);

	KUNIT_ASSERT_EQ(test, atomic_read(&priv->intr_count), 10);
	KUNIT_ASSERT_EQ(test, atomic_read(&priv->thread_count), 10);
}

/*
 * Drive line low 10 times, threaded handler drives it high to "clear the
 * interrupt source", assert we get 10 interrupts
 */
static void tlmm_test_thread_low(struct kunit *test)
{
	struct tlmm_test_priv *priv = test->priv;
	int i;

	priv->intr_op = TLMM_TEST_COUNT | TLMM_TEST_WAKE_THREAD;
	priv->thread_op = TLMM_TEST_COUNT | TLMM_TEST_OUTPUT_HIGH;

	tlmm_output_high();

	tlmm_test_request_threaded_irq(test, IRQF_TRIGGER_LOW | IRQF_ONESHOT);
	for (i = 0; i < 10; i++) {
		tlmm_output_low();
		msleep(20);
	}
	free_irq(tlmm_suite.irq, priv);

	KUNIT_ASSERT_EQ(test, atomic_read(&priv->intr_count), 10);
	KUNIT_ASSERT_EQ(test, atomic_read(&priv->thread_count), 10);
}

/*
 * Handler drives GPIO low to "clear the interrupt source", then high in the
 * threaded handler to simulate a new interrupt, repeated 10 times, assert we
 * get 10 interrupts
 */
static void tlmm_test_thread_rising_in_handler(struct kunit *test)
{
	struct tlmm_test_priv *priv = test->priv;

	priv->intr_op = TLMM_TEST_COUNT | TLMM_TEST_OUTPUT_LOW | TLMM_TEST_WAKE_THREAD;
	priv->thread_op = TLMM_TEST_COUNT | TLMM_TEST_THEN_HIGH;
	atomic_set(&priv->thread_op_remain, 10);

	tlmm_output_low();

	tlmm_test_request_threaded_irq(test, IRQF_TRIGGER_RISING);
	msleep(20);
	tlmm_output_high();
	msleep(100);
	free_irq(tlmm_suite.irq, priv);

	KUNIT_ASSERT_EQ(test, atomic_read(&priv->intr_count), 10);
	KUNIT_ASSERT_EQ(test, atomic_read(&priv->thread_count), 10);
}

/*
 * Handler drives GPIO high to "clear the interrupt source", then low in the
 * threaded handler to simulate a new interrupt, repeated 10 times, assert we
 * get 10 interrupts
 */
static void tlmm_test_thread_falling_in_handler(struct kunit *test)
{
	struct tlmm_test_priv *priv = test->priv;

	priv->intr_op = TLMM_TEST_COUNT | TLMM_TEST_OUTPUT_HIGH | TLMM_TEST_WAKE_THREAD;
	priv->thread_op = TLMM_TEST_COUNT | TLMM_TEST_THEN_LOW;
	atomic_set(&priv->thread_op_remain, 10);

	tlmm_output_high();

	tlmm_test_request_threaded_irq(test, IRQF_TRIGGER_FALLING);
	msleep(20);
	tlmm_output_low();
	msleep(100);
	free_irq(tlmm_suite.irq, priv);

	KUNIT_ASSERT_EQ(test, atomic_read(&priv->intr_count), 10);
	KUNIT_ASSERT_EQ(test, atomic_read(&priv->thread_count), 10);
}

/*
 * Validate that edge interrupts occurring while irq is disabled is delivered
 * once the interrupt is reenabled.
 */
static void tlmm_test_rising_while_disabled(struct kunit *test)
{
	struct tlmm_test_priv *priv = test->priv;
	unsigned int after_edge;
	unsigned int before_edge;

	priv->intr_op = TLMM_TEST_COUNT;
	atomic_set(&priv->thread_op_remain, 10);

	tlmm_output_low();

	tlmm_test_request_hard_irq(test, IRQF_TRIGGER_RISING);
	msleep(20);

	disable_irq(tlmm_suite.irq);
	before_edge = atomic_read(&priv->intr_count);

	tlmm_output_high();
	msleep(20);
	after_edge = atomic_read(&priv->intr_count);

	msleep(20);
	enable_irq(tlmm_suite.irq);
	msleep(20);

	free_irq(tlmm_suite.irq, priv);

	KUNIT_ASSERT_EQ(test, before_edge, 0);
	KUNIT_ASSERT_EQ(test, after_edge, 0);
	KUNIT_ASSERT_EQ(test, atomic_read(&priv->intr_count), 1);
}

static int tlmm_test_init(struct kunit *test)
{
	struct tlmm_test_priv *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);

	atomic_set(&priv->intr_count, 0);
	atomic_set(&priv->thread_count, 0);

	atomic_set(&priv->intr_op_remain, 0);
	atomic_set(&priv->thread_op_remain, 0);

	test->priv = priv;

	return 0;
}

/*
 * NOTE: When adding compatibles to this list, ensure that TLMM_REG_SIZE and
 * pull configuration values are supported and correct.
 */
static const struct of_device_id tlmm_of_match[] = {
	{ .compatible = "qcom,sc8280xp-tlmm" },
	{ .compatible = "qcom,x1e80100-tlmm" },
	{}
};

static int tlmm_test_init_suite(struct kunit_suite *suite)
{
	struct of_phandle_args args = {};
	struct resource res;
	int ret;
	u32 val;

	if (tlmm_test_gpio < 0) {
		pr_err("use the tlmm-test.gpio module parameter to specify which GPIO to use\n");
		return -EINVAL;
	}

	struct device_node *tlmm __free(device_node) = of_find_matching_node(NULL, tlmm_of_match);
	if (!tlmm) {
		pr_err("failed to find tlmm node\n");
		return -EINVAL;
	}

	ret = of_address_to_resource(tlmm, 0, &res);
	if (ret < 0)
		return ret;

	tlmm_suite.base = ioremap(res.start, resource_size(&res));
	if (!tlmm_suite.base)
		return -ENOMEM;

	args.np = tlmm;
	args.args_count = 2;
	args.args[0] = tlmm_test_gpio;
	args.args[1] = 0;

	tlmm_suite.irq = irq_create_of_mapping(&args);
	if (!tlmm_suite.irq) {
		pr_err("failed to map TLMM irq %d\n", args.args[0]);
		goto err_unmap;
	}

	tlmm_suite.reg = tlmm_suite.base + tlmm_test_gpio * TLMM_REG_SIZE;
	val = readl(tlmm_suite.reg) & ~MSM_PULL_MASK;
	tlmm_suite.low_val = val | MSM_PULL_DOWN;
	tlmm_suite.high_val = val | MSM_PULL_UP;

	return 0;

err_unmap:
	iounmap(tlmm_suite.base);
	tlmm_suite.base = NULL;
	return -EINVAL;
}

static void tlmm_test_exit_suite(struct kunit_suite *suite)
{
	irq_dispose_mapping(tlmm_suite.irq);
	iounmap(tlmm_suite.base);

	tlmm_suite.base = NULL;
	tlmm_suite.irq = -1;
}

static struct kunit_case tlmm_test_cases[] = {
	KUNIT_CASE(tlmm_test_silent_rising),
	KUNIT_CASE(tlmm_test_silent_falling),
	KUNIT_CASE(tlmm_test_silent_low),
	KUNIT_CASE(tlmm_test_silent_high),
	KUNIT_CASE(tlmm_test_rising),
	KUNIT_CASE(tlmm_test_falling),
	KUNIT_CASE(tlmm_test_high),
	KUNIT_CASE(tlmm_test_low),
	KUNIT_CASE(tlmm_test_rising_in_handler),
	KUNIT_CASE(tlmm_test_falling_in_handler),
	KUNIT_CASE(tlmm_test_thread_rising),
	KUNIT_CASE(tlmm_test_thread_falling),
	KUNIT_CASE(tlmm_test_thread_high),
	KUNIT_CASE(tlmm_test_thread_low),
	KUNIT_CASE(tlmm_test_thread_rising_in_handler),
	KUNIT_CASE(tlmm_test_thread_falling_in_handler),
	KUNIT_CASE(tlmm_test_rising_while_disabled),
	{}
};

static struct kunit_suite tlmm_test_suite = {
	.name = "tlmm-test",
	.init = tlmm_test_init,
	.suite_init = tlmm_test_init_suite,
	.suite_exit = tlmm_test_exit_suite,
	.test_cases = tlmm_test_cases,
};

kunit_test_suites(&tlmm_test_suite);

MODULE_DESCRIPTION("Qualcomm TLMM test");
MODULE_LICENSE("GPL");
