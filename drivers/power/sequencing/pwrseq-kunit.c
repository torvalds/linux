// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 */

#include <linux/err.h>
#include <linux/fwnode.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pwrseq/consumer.h>
#include <linux/pwrseq/provider.h>

#include <kunit/fwnode.h>
#include <kunit/platform_device.h>
#include <kunit/resource.h>
#include <kunit/test.h>

#define PWRSEQ_TEST_PARENT			"pwrseq-test-parent"
#define PWRSEQ_TEST_CONSUMER			"pwrseq-test-consumer"

#define PWRSEQ_SWNODE_TEST_PROVIDER		"pwrseq-swnode-test-provider"
#define PWRSEQ_SWNODE_TEST_PROVIDER_2		"pwrseq-swnode-test-provider-2"
#define PWRSEQ_PROBE_ORDER_TEST_CONSUMER	"pwrseq-probe-order-test-consumer"
#define PWRSEQ_PROBE_DEFER_TEST_CONSUMER	"pwrseq-probe-defer-test-consumer"

static const struct software_node pwrseq_test_provider_swnode = {
	.name = "pwrseq-test-provider",
};

KUNIT_DEFINE_ACTION_WRAPPER(pwrseq_device_unregister_wrapper,
			    pwrseq_device_unregister,
			    struct pwrseq_device *);

static struct pwrseq_device *
kunit_pwrseq_device_register(struct kunit *test,
			     const struct pwrseq_config *config)
{
	struct pwrseq_device *pwrseq;
	int ret;

	pwrseq = pwrseq_device_register(config);
	if (IS_ERR(pwrseq))
		return pwrseq;

	ret = kunit_add_action_or_reset(test, pwrseq_device_unregister_wrapper,
					pwrseq);
	if (ret)
		return ERR_PTR(ret);

	return pwrseq;
}

KUNIT_DEFINE_ACTION_WRAPPER(pwrseq_put_wrapper, pwrseq_put,
			    struct pwrseq_desc *);

static struct pwrseq_desc *
kunit_pwrseq_get(struct kunit *test, struct device *dev, const char *target)
{
	struct pwrseq_desc *desc;
	int ret;

	desc = pwrseq_get(dev, target);
	if (IS_ERR(desc))
		return desc;

	ret = kunit_add_action_or_reset(test, pwrseq_put_wrapper, desc);
	if (ret)
		return ERR_PTR(ret);

	return desc;
}

struct pwrseq_test_ctx {
	const char *consumer_name;
	int enable_count_a;
	int enable_count_b;
	int disable_count_a;
	int disable_count_b;
	int enable_calls_a;
	int post_enable_calls;
	bool enable_error_a;
	bool enable_error_b;
	bool post_enable_error;
};

static int pwrseq_test_no_match(struct pwrseq_device *pwrseq,
				struct device *dev)
{
	return PWRSEQ_NO_MATCH;
}

static int pwrseq_test_match_by_name(struct pwrseq_device *pwrseq,
				     struct device *dev)
{
	struct pwrseq_test_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	return strcmp(dev_name(dev), ctx->consumer_name) == 0 ?
					PWRSEQ_MATCH_OK : PWRSEQ_NO_MATCH;
}

static int pwrseq_test_match_always(struct pwrseq_device *pwrseq,
				    struct device *dev)
{
	return PWRSEQ_MATCH_OK;
}

static int pwrseq_test_enable_a(struct pwrseq_device *pwrseq)
{
	struct pwrseq_test_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	ctx->enable_calls_a++;

	if (ctx->enable_error_a)
		return -EIO;

	ctx->enable_count_a++;

	return 0;
}

static int pwrseq_test_disable_a(struct pwrseq_device *pwrseq)
{
	struct pwrseq_test_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	ctx->disable_count_a++;

	return 0;
}

static int pwrseq_test_enable_b(struct pwrseq_device *pwrseq)
{
	struct pwrseq_test_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	if (ctx->enable_error_b)
		return -EIO;

	ctx->enable_count_b++;

	return 0;
}

static int pwrseq_test_disable_b(struct pwrseq_device *pwrseq)
{
	struct pwrseq_test_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	ctx->disable_count_b++;

	return 0;
}

static int pwrseq_test_post_enable(struct pwrseq_device *pwrseq)
{
	struct pwrseq_test_ctx *ctx = pwrseq_device_get_drvdata(pwrseq);

	ctx->post_enable_calls++;

	return ctx->post_enable_error ? -EIO : 0;
}

static int pwrseq_test_parent_probe(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver pwrseq_test_parent_driver = {
	.probe = pwrseq_test_parent_probe,
	.driver = {
		.name = PWRSEQ_TEST_PARENT,
	},
};

static int pwrseq_test_parent_init(struct kunit *test)
{
	static const struct platform_device_info pdevinfo = {
		.name = PWRSEQ_TEST_PARENT,
		.id = PLATFORM_DEVID_NONE,
	};

	struct platform_device *pdev;
	bool bound;
	int ret;

	ret = kunit_platform_driver_register(test, &pwrseq_test_parent_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	pdev = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	wait_for_device_probe();
	scoped_guard(device, &pdev->dev)
		bound = device_is_bound(&pdev->dev);

	KUNIT_ASSERT_TRUE(test, bound);

	test->priv = pdev;

	return 0;
}

/*
 * Test that registering a power sequencer whose unit dependency graph
 * contains a two-node cycle (A -> B -> A) is rejected with -EINVAL.
 */
static void pwrseq_circular_deps(struct kunit *test)
{
	const struct pwrseq_unit_data *unit_a_deps[2] = { };
	const struct pwrseq_unit_data *unit_b_deps[2] = { };
	const struct pwrseq_target_data *targets[2] = { };
	struct platform_device *pdev = test->priv;
	struct pwrseq_unit_data unit_a, unit_b;
	struct pwrseq_target_data target;
	struct pwrseq_device *pwrseq;
	struct pwrseq_config config;

	unit_a = (struct pwrseq_unit_data){
		.name = "unit-a",
		.deps = unit_a_deps,
	};

	unit_b = (struct pwrseq_unit_data){
		.name = "unit-b",
		.deps = unit_b_deps,
	};

	unit_a_deps[0] = &unit_b;
	unit_b_deps[0] = &unit_a;

	target = (struct pwrseq_target_data){
		.name = "test-target",
		.unit = &unit_a,
	};

	targets[0] = &target;

	config = (struct pwrseq_config){
		.parent = &pdev->dev,
		.match = pwrseq_test_no_match,
		.targets = targets,
	};

	kunit_warning_suppress(test) {
		pwrseq = kunit_pwrseq_device_register(test, &config);
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, 1);
	}

	KUNIT_EXPECT_TRUE(test, IS_ERR(pwrseq));
	KUNIT_EXPECT_EQ(test, PTR_ERR(pwrseq), -EINVAL);
}

/*
 * Test that a longer chain cycle (A -> B -> C -> D -> A) is also rejected
 * with -EINVAL.
 */
static void pwrseq_circular_deps_chain(struct kunit *test)
{
	struct pwrseq_unit_data unit_a, unit_b, unit_c, unit_d;
	const struct pwrseq_unit_data *unit_a_deps[2] = { };
	const struct pwrseq_unit_data *unit_b_deps[2] = { };
	const struct pwrseq_unit_data *unit_c_deps[2] = { };
	const struct pwrseq_unit_data *unit_d_deps[2] = { };
	const struct pwrseq_target_data *targets[2] = { };
	struct platform_device *pdev = test->priv;
	struct pwrseq_target_data target;
	struct pwrseq_device *pwrseq;
	struct pwrseq_config config;

	unit_a = (struct pwrseq_unit_data){
		.name = "unit-a",
		.deps = unit_a_deps,
	};
	unit_b = (struct pwrseq_unit_data){
		.name = "unit-b",
		.deps = unit_b_deps,
	};
	unit_c = (struct pwrseq_unit_data){
		.name = "unit-c",
		.deps = unit_c_deps,
	};
	unit_d = (struct pwrseq_unit_data){
		.name = "unit-d",
		.deps = unit_d_deps,
	};

	unit_a_deps[0] = &unit_b;
	unit_b_deps[0] = &unit_c;
	unit_c_deps[0] = &unit_d;
	unit_d_deps[0] = &unit_a;

	target = (struct pwrseq_target_data){
		.name = "test-target",
		.unit = &unit_a,
	};

	targets[0] = &target;

	config = (struct pwrseq_config){
		.parent = &pdev->dev,
		.match = pwrseq_test_no_match,
		.targets = targets,
	};

	kunit_warning_suppress(test) {
		pwrseq = kunit_pwrseq_device_register(test, &config);
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, 1);
	}

	KUNIT_EXPECT_TRUE(test, IS_ERR(pwrseq));
	KUNIT_EXPECT_EQ(test, PTR_ERR(pwrseq), -EINVAL);
}

/*
 * Test that a valid acyclic dependency graph (A -> B) registers successfully.
 */
static void pwrseq_register_valid(struct kunit *test)
{
	const struct pwrseq_unit_data *unit_a_deps[2] = { };
	const struct pwrseq_target_data *targets[2] = { };
	struct platform_device *pdev = test->priv;
	struct pwrseq_unit_data unit_a, unit_b;
	struct pwrseq_target_data target;
	struct pwrseq_device *pwrseq;
	struct pwrseq_config config;

	unit_b = (struct pwrseq_unit_data){
		.name = "unit-b",
	};

	unit_a_deps[0] = &unit_b;
	unit_a = (struct pwrseq_unit_data){
		.name = "unit-a",
		.deps = unit_a_deps,
	};

	target = (struct pwrseq_target_data){
		.name = "test-target",
		.unit = &unit_a,
	};

	targets[0] = &target;

	config = (struct pwrseq_config){
		.parent = &pdev->dev,
		.match = pwrseq_test_no_match,
		.targets = targets,
	};

	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pwrseq);
}

/*
 * Test that registration is rejected with -EINVAL when .parent, .match or
 * .targets is missing, or when .targets is a non-NULL but empty array.
 */
static void pwrseq_register_invalid_args(struct kunit *test)
{
	const struct pwrseq_target_data *empty_targets[1] = { };
	const struct pwrseq_target_data *targets[2] = { };
	struct platform_device *pdev = test->priv;
	struct pwrseq_target_data target;
	struct pwrseq_unit_data unit;
	struct pwrseq_device *pwrseq;
	struct pwrseq_config config;

	unit = (struct pwrseq_unit_data){ .name = "unit-a" };
	target = (struct pwrseq_target_data){
		.name = "test-target",
		.unit = &unit,
	};
	targets[0] = &target;

	config = (struct pwrseq_config){
		.parent = &pdev->dev,
		.match = pwrseq_test_no_match,
		.targets = targets,
	};

	config.parent = NULL;
	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_EXPECT_TRUE(test, IS_ERR(pwrseq));
	KUNIT_EXPECT_EQ(test, PTR_ERR(pwrseq), -EINVAL);
	config.parent = &pdev->dev;

	config.match = NULL;
	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_EXPECT_TRUE(test, IS_ERR(pwrseq));
	KUNIT_EXPECT_EQ(test, PTR_ERR(pwrseq), -EINVAL);
	config.match = pwrseq_test_no_match;

	config.targets = NULL;
	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_EXPECT_TRUE(test, IS_ERR(pwrseq));
	KUNIT_EXPECT_EQ(test, PTR_ERR(pwrseq), -EINVAL);

	config.targets = empty_targets;
	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_EXPECT_TRUE(test, IS_ERR(pwrseq));
	KUNIT_EXPECT_EQ(test, PTR_ERR(pwrseq), -EINVAL);
}

/*
 * Test that a target without a unit is rejected with -EINVAL.
 */
static void pwrseq_register_target_without_unit(struct kunit *test)
{
	const struct pwrseq_target_data *targets[2] = { };
	struct platform_device *pdev = test->priv;
	struct pwrseq_target_data target;
	struct pwrseq_device *pwrseq;
	struct pwrseq_config config;

	target = (struct pwrseq_target_data){
		.name = "test-target",
		.unit = NULL,
	};
	targets[0] = &target;

	config = (struct pwrseq_config){
		.parent = &pdev->dev,
		.match = pwrseq_test_no_match,
		.targets = targets,
	};

	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_EXPECT_TRUE(test, IS_ERR(pwrseq));
	KUNIT_EXPECT_EQ(test, PTR_ERR(pwrseq), -EINVAL);
}

/*
 * Test that enabling and disabling a single-unit target increments and
 * decrements enable_count correctly and fires the enable/disable callbacks
 * exactly once.
 */
static void pwrseq_enable_disable(struct kunit *test)
{
	const struct pwrseq_target_data *targets[2] = { };
	struct platform_device *pdev = test->priv;
	struct platform_device_info pdevinfo;
	struct pwrseq_target_data target;
	struct pwrseq_test_ctx *ctx;
	struct pwrseq_unit_data unit;
	struct pwrseq_device *pwrseq;
	struct pwrseq_config config;
	struct pwrseq_desc *desc;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	unit = (struct pwrseq_unit_data){
		.name = "unit-a",
		.enable = pwrseq_test_enable_a,
		.disable = pwrseq_test_disable_a,
	};

	target = (struct pwrseq_target_data){
		.name = "test-target",
		.unit = &unit,
	};

	targets[0] = &target;

	ctx->consumer_name = PWRSEQ_TEST_CONSUMER;

	config = (struct pwrseq_config){
		.parent = &pdev->dev,
		.drvdata = ctx,
		.match = pwrseq_test_match_by_name,
		.targets = targets,
	};

	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pwrseq);

	pdevinfo = (struct platform_device_info){
		.name = PWRSEQ_TEST_CONSUMER,
		.id = PLATFORM_DEVID_NONE,
	};

	pdev = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	desc = kunit_pwrseq_get(test, &pdev->dev, "test-target");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, desc);

	ret = pwrseq_enable(desc);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->enable_count_a, 1);
	KUNIT_EXPECT_EQ(test, ctx->disable_count_a, 0);

	ret = pwrseq_disable(desc);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->enable_count_a, 1);
	KUNIT_EXPECT_EQ(test, ctx->disable_count_a, 1);
}

/*
 * Test that two consumers sharing a common dependency unit cause it to be
 * enabled only once and disabled only once, while enable_count tracks each
 * consumer correctly.
 */
static void pwrseq_shared_deps(struct kunit *test)
{
	const struct pwrseq_unit_data *unit_a_deps[2] = { };
	const struct pwrseq_unit_data *unit_b_deps[2] = { };
	const struct pwrseq_target_data *targets[3] = { };
	struct pwrseq_unit_data dep_unit, unit_a, unit_b;
	struct pwrseq_target_data target_a, target_b;
	struct platform_device *parent = test->priv;
	struct platform_device *pdev_a, *pdev_b;
	struct platform_device_info pdevinfo;
	struct pwrseq_desc *desc_a, *desc_b;
	struct pwrseq_device *pwrseq;
	struct pwrseq_config config;
	struct pwrseq_test_ctx *ctx;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	dep_unit = (struct pwrseq_unit_data){
		.name = "dep",
		.enable = pwrseq_test_enable_a,
		.disable = pwrseq_test_disable_a,
	};

	unit_a_deps[0] = &dep_unit;
	unit_a = (struct pwrseq_unit_data){
		.name = "unit-a",
		.deps = unit_a_deps,
		.enable = pwrseq_test_enable_b,
		.disable = pwrseq_test_disable_b,
	};

	unit_b_deps[0] = &dep_unit;
	unit_b = (struct pwrseq_unit_data){
		.name = "unit-b",
		.deps = unit_b_deps,
	};

	target_a = (struct pwrseq_target_data){
		.name = "target-a",
		.unit = &unit_a,
	};
	target_b = (struct pwrseq_target_data){
		.name = "target-b",
		.unit = &unit_b,
	};

	targets[0] = &target_a;
	targets[1] = &target_b;

	ctx->consumer_name = PWRSEQ_TEST_CONSUMER "-a";

	config = (struct pwrseq_config){
		.parent = &parent->dev,
		.drvdata = ctx,
		.match = pwrseq_test_match_by_name,
		.targets = targets,
	};

	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pwrseq);

	pdevinfo = (struct platform_device_info){
		.name = PWRSEQ_TEST_CONSUMER "-a",
		.id = PLATFORM_DEVID_NONE,
	};
	pdev_a = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev_a);

	pdevinfo = (struct platform_device_info){
		.name = PWRSEQ_TEST_CONSUMER "-b",
		.id = PLATFORM_DEVID_NONE,
	};
	pdev_b = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev_b);

	/*
	 * match_by_name() only matches one consumer name at a time, so
	 * reconfigure ctx->consumer_name between the two pwrseq_get() calls
	 * below to acquire descriptors for both consumer-a and consumer-b.
	 */
	desc_a = kunit_pwrseq_get(test, &pdev_a->dev, "target-a");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, desc_a);

	ctx->consumer_name = PWRSEQ_TEST_CONSUMER "-b";
	desc_b = kunit_pwrseq_get(test, &pdev_b->dev, "target-b");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, desc_b);

	ret = pwrseq_enable(desc_a);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->enable_count_a, 1);
	KUNIT_EXPECT_EQ(test, ctx->enable_count_b, 1);

	ret = pwrseq_enable(desc_b);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->enable_count_a, 1);

	ret = pwrseq_disable(desc_a);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->disable_count_a, 0);
	KUNIT_EXPECT_EQ(test, ctx->disable_count_b, 1);

	ret = pwrseq_disable(desc_b);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->disable_count_a, 1);
}

/*
 * Test that calling pwrseq_enable() twice on the same descriptor has no
 * effect.
 */
static void pwrseq_enable_idempotent(struct kunit *test)
{
	const struct pwrseq_target_data *targets[2] = { };
	struct platform_device *pdev = test->priv;
	struct platform_device_info pdevinfo;
	struct pwrseq_target_data target;
	struct pwrseq_device *pwrseq;
	struct pwrseq_unit_data unit;
	struct pwrseq_config config;
	struct pwrseq_test_ctx *ctx;
	struct pwrseq_desc *desc;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	unit = (struct pwrseq_unit_data){
		.name = "unit-a",
		.enable = pwrseq_test_enable_a,
		.disable = pwrseq_test_disable_a,
	};

	target = (struct pwrseq_target_data){
		.name = "test-target",
		.unit = &unit,
	};

	targets[0] = &target;

	ctx->consumer_name = PWRSEQ_TEST_CONSUMER;

	config = (struct pwrseq_config){
		.parent = &pdev->dev,
		.drvdata = ctx,
		.match = pwrseq_test_match_by_name,
		.targets = targets,
	};

	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pwrseq);

	pdevinfo = (struct platform_device_info){
		.name = PWRSEQ_TEST_CONSUMER,
		.id = PLATFORM_DEVID_NONE,
	};

	pdev = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	desc = kunit_pwrseq_get(test, &pdev->dev, "test-target");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, desc);

	ret = pwrseq_enable(desc);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->enable_count_a, 1);

	/* Second power_on on same descriptor must be a no-op. */
	ret = pwrseq_enable(desc);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->enable_count_a, 1);

	ret = pwrseq_disable(desc);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->disable_count_a, 1);
}

/*
 * Test that when a dependency unit's enable() callback fails, the error
 * propagates to the caller, the top unit's enable callback is never reached,
 * and target->post_enable() is never invoked.
 */
static void pwrseq_enable_enable_error(struct kunit *test)
{
	const struct pwrseq_target_data *targets[2] = { };
	const struct pwrseq_unit_data *top_deps[2] = { };
	struct pwrseq_unit_data dep_unit, top_unit;
	struct platform_device *pdev = test->priv;
	struct platform_device_info pdevinfo;
	struct pwrseq_target_data target;
	struct pwrseq_device *pwrseq;
	struct pwrseq_config config;
	struct pwrseq_test_ctx *ctx;
	struct pwrseq_desc *desc;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	dep_unit = (struct pwrseq_unit_data){
		.name = "dep",
		.enable = pwrseq_test_enable_a,
	};

	top_deps[0] = &dep_unit;
	top_unit = (struct pwrseq_unit_data){
		.name = "top",
		.deps = top_deps,
		.enable = pwrseq_test_enable_b,
	};

	target = (struct pwrseq_target_data){
		.name = "test-target",
		.unit = &top_unit,
		.post_enable = pwrseq_test_post_enable,
	};

	targets[0] = &target;

	ctx->consumer_name = PWRSEQ_TEST_CONSUMER;
	ctx->enable_error_a = true;

	config = (struct pwrseq_config){
		.parent = &pdev->dev,
		.drvdata = ctx,
		.match = pwrseq_test_match_by_name,
		.targets = targets,
	};

	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pwrseq);

	pdevinfo = (struct platform_device_info){
		.name = PWRSEQ_TEST_CONSUMER,
		.id = PLATFORM_DEVID_NONE,
	};

	pdev = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	desc = kunit_pwrseq_get(test, &pdev->dev, "test-target");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, desc);

	ret = pwrseq_enable(desc);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	/*
	 * Dependency's enable() was attempted but failed, so it never
	 * incremented.
	 */
	KUNIT_EXPECT_EQ(test, ctx->enable_calls_a, 1);
	KUNIT_EXPECT_EQ(test, ctx->enable_count_a, 0);
	/* Target unit's enable() was never reached. */
	KUNIT_EXPECT_EQ(test, ctx->enable_count_b, 0);
	/* Target's .post_enable() must not run on an unpowered sequencer. */
	KUNIT_EXPECT_EQ(test, ctx->post_enable_calls, 0);
}

/*
 * Test that when the top unit's own enable() callback fails after its
 * dependency's enable() already succeeded, the dependency is rolled back.
 */
static void pwrseq_enable_rollback_on_top_failure(struct kunit *test)
{
	const struct pwrseq_target_data *targets[2] = { };
	const struct pwrseq_unit_data *top_deps[2] = { };
	struct pwrseq_unit_data dep_unit, top_unit;
	struct platform_device *pdev = test->priv;
	struct platform_device_info pdevinfo;
	struct pwrseq_target_data target;
	struct pwrseq_device *pwrseq;
	struct pwrseq_config config;
	struct pwrseq_test_ctx *ctx;
	struct pwrseq_desc *desc;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	dep_unit = (struct pwrseq_unit_data){
		.name = "dep",
		.enable = pwrseq_test_enable_a,
		.disable = pwrseq_test_disable_a,
	};

	top_deps[0] = &dep_unit;
	top_unit = (struct pwrseq_unit_data){
		.name = "top",
		.deps = top_deps,
		.enable = pwrseq_test_enable_b,
	};

	target = (struct pwrseq_target_data){
		.name = "test-target",
		.unit = &top_unit,
	};

	targets[0] = &target;

	ctx->consumer_name = PWRSEQ_TEST_CONSUMER;
	ctx->enable_error_b = true;

	config = (struct pwrseq_config){
		.parent = &pdev->dev,
		.drvdata = ctx,
		.match = pwrseq_test_match_by_name,
		.targets = targets,
	};

	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pwrseq);

	pdevinfo = (struct platform_device_info){
		.name = PWRSEQ_TEST_CONSUMER,
		.id = PLATFORM_DEVID_NONE,
	};

	pdev = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	desc = kunit_pwrseq_get(test, &pdev->dev, "test-target");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, desc);

	ret = pwrseq_enable(desc);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	/*
	 * Dependency was enabled, then rolled back once top's own enable
	 * failed.
	 */
	KUNIT_EXPECT_EQ(test, ctx->enable_count_a, 1);
	KUNIT_EXPECT_EQ(test, ctx->disable_count_a, 1);
	/*
	 * Target unit's own enable never incremented since it returned an
	 * error.
	 */
	KUNIT_EXPECT_EQ(test, ctx->enable_count_b, 0);
}

/*
 * Test target->post_enable() on both the success and failure paths: it must
 * run exactly once per pwrseq_enable() call that actually powers the target
 * unit on, and a failing post_enable() must roll the unit back and clear
 * powered_on so a subsequent pwrseq_enable() is called again.
 */
static void pwrseq_enable_post_enable(struct kunit *test)
{
	const struct pwrseq_target_data *targets[2] = { };
	struct platform_device *pdev = test->priv;
	struct platform_device_info pdevinfo;
	struct pwrseq_target_data target;
	struct pwrseq_unit_data unit;
	struct pwrseq_device *pwrseq;
	struct pwrseq_config config;
	struct pwrseq_test_ctx *ctx;
	struct pwrseq_desc *desc;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	unit = (struct pwrseq_unit_data){
		.name = "unit-a",
		.enable = pwrseq_test_enable_a,
		.disable = pwrseq_test_disable_a,
	};

	target = (struct pwrseq_target_data){
		.name = "test-target",
		.unit = &unit,
		.post_enable = pwrseq_test_post_enable,
	};

	targets[0] = &target;

	ctx->consumer_name = PWRSEQ_TEST_CONSUMER;

	config = (struct pwrseq_config){
		.parent = &pdev->dev,
		.drvdata = ctx,
		.match = pwrseq_test_match_by_name,
		.targets = targets,
	};

	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pwrseq);

	pdevinfo = (struct platform_device_info){
		.name = PWRSEQ_TEST_CONSUMER,
		.id = PLATFORM_DEVID_NONE,
	};

	pdev = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	desc = kunit_pwrseq_get(test, &pdev->dev, "test-target");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, desc);

	/* Success path: post_enable() runs once and reports success. */
	ret = pwrseq_enable(desc);
	KUNIT_EXPECT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->post_enable_calls, 1);
	KUNIT_EXPECT_EQ(test, ctx->enable_count_a, 1);

	ret = pwrseq_disable(desc);
	KUNIT_ASSERT_EQ(test, ret, 0);

	/* Failure path: post_enable() fails, unit is rolled back. */
	ctx->post_enable_error = true;
	ret = pwrseq_enable(desc);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	KUNIT_EXPECT_EQ(test, ctx->post_enable_calls, 2);
	KUNIT_EXPECT_EQ(test, ctx->enable_count_a, 2);
	KUNIT_EXPECT_EQ(test, ctx->disable_count_a, 2);

	/* powered_on must have been cleared. */
	ret = pwrseq_enable(desc);
	KUNIT_EXPECT_EQ(test, ret, -EIO);
	KUNIT_EXPECT_EQ(test, ctx->enable_count_a, 3);
}

/*
 * Test that pwrseq_get() returns -ENOENT when the matched provider does not
 * have the requested target name.
 */
static void pwrseq_get_target_not_found(struct kunit *test)
{
	const struct pwrseq_target_data *targets[2] = { };
	struct platform_device *pdev = test->priv;
	struct platform_device_info pdevinfo;
	struct pwrseq_target_data target;
	struct pwrseq_unit_data unit;
	struct pwrseq_device *pwrseq;
	struct pwrseq_config config;
	struct pwrseq_desc *desc;

	unit = (struct pwrseq_unit_data){
		.name = "unit-a",
	};

	target = (struct pwrseq_target_data){
		.name = "real-target",
		.unit = &unit,
	};

	targets[0] = &target;

	config = (struct pwrseq_config){
		.parent = &pdev->dev,
		.match = pwrseq_test_match_always,
		.targets = targets,
	};

	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pwrseq);

	pdevinfo = (struct platform_device_info){
		.name = PWRSEQ_TEST_CONSUMER,
		.id = PLATFORM_DEVID_NONE,
	};

	pdev = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	desc = kunit_pwrseq_get(test, &pdev->dev, "nonexistent-target");
	KUNIT_EXPECT_TRUE(test, IS_ERR(desc));
	KUNIT_EXPECT_EQ(test, PTR_ERR(desc), -ENOENT);
}

/*
 * Test that pwrseq_put() on a descriptor that is still powered on disables
 * it as part of tear-down, instead of leaking the power-on state.
 */
static void pwrseq_put_disables_powered_desc(struct kunit *test)
{
	const struct pwrseq_target_data *targets[2] = { };
	struct platform_device *pdev = test->priv;
	struct platform_device_info pdevinfo;
	struct pwrseq_target_data target;
	struct pwrseq_unit_data unit;
	struct pwrseq_device *pwrseq;
	struct pwrseq_test_ctx *ctx;
	struct pwrseq_config config;
	struct pwrseq_desc *desc;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	unit = (struct pwrseq_unit_data){
		.name = "unit-a",
		.enable = pwrseq_test_enable_a,
		.disable = pwrseq_test_disable_a,
	};

	target = (struct pwrseq_target_data){
		.name = "test-target",
		.unit = &unit,
	};

	targets[0] = &target;

	config = (struct pwrseq_config){
		.parent = &pdev->dev,
		.drvdata = ctx,
		.match = pwrseq_test_match_always,
		.targets = targets,
	};

	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pwrseq);

	pdevinfo = (struct platform_device_info){
		.name = PWRSEQ_TEST_CONSUMER,
		.id = PLATFORM_DEVID_NONE,
	};

	pdev = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pdev);

	desc = kunit_pwrseq_get(test, &pdev->dev, "test-target");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, desc);

	ret = pwrseq_enable(desc);
	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_EXPECT_EQ(test, ctx->disable_count_a, 0);

	/*
	 * Run the deferred pwrseq_put() action now instead of at test
	 * teardown, so its effect can be asserted below.
	 */
	kunit_release_action(test, pwrseq_put_wrapper, desc);
	KUNIT_EXPECT_EQ(test, ctx->disable_count_a, 1);
}

/*
 * Test that pwrseq_device_unregister() on a target with an active user
 * triggers "REMOVING POWER SEQUENCER WITH ACTIVE USERS", and that both
 * pwrseq_enable() and pwrseq_disable() start returning -ENODEV for
 * previously acquired descriptors afterwards.
 */
static void pwrseq_unregister_with_active_user(struct kunit *test)
{
	struct platform_device *pdev = test->priv, *cons_a, *cons_b;
	const struct pwrseq_target_data *targets[2] = { };
	struct platform_device_info pdevinfo;
	struct pwrseq_desc *desc_a, *desc_b;
	struct pwrseq_target_data target;
	struct pwrseq_unit_data unit;
	struct pwrseq_device *pwrseq;
	struct pwrseq_test_ctx *ctx;
	struct pwrseq_config config;
	bool bound;
	int ret;

	ctx = kunit_kzalloc(test, sizeof(*ctx), GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, ctx);

	unit = (struct pwrseq_unit_data){
		.name = "unit-a",
		.enable = pwrseq_test_enable_a,
		.disable = pwrseq_test_disable_a,
	};

	target = (struct pwrseq_target_data){
		.name = "test-target",
		.unit = &unit,
	};

	targets[0] = &target;

	config = (struct pwrseq_config){
		.parent = &pdev->dev,
		.drvdata = ctx,
		.match = pwrseq_test_match_always,
		.targets = targets,
	};

	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pwrseq);

	pdevinfo = (struct platform_device_info){
		.name = PWRSEQ_TEST_CONSUMER,
		.id = 0,
	};
	cons_a = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cons_a);

	wait_for_device_probe();
	scoped_guard(device, &cons_a->dev)
		bound = device_is_bound(&cons_a->dev);

	KUNIT_ASSERT_FALSE(test, bound);

	pdevinfo.id = 1;
	cons_b = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cons_b);

	wait_for_device_probe();
	scoped_guard(device, &cons_b->dev)
		bound = device_is_bound(&cons_b->dev);

	KUNIT_ASSERT_FALSE(test, bound);

	desc_a = kunit_pwrseq_get(test, &cons_a->dev, "test-target");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, desc_a);
	desc_b = kunit_pwrseq_get(test, &cons_b->dev, "test-target");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, desc_b);

	ret = pwrseq_enable(desc_a);
	KUNIT_ASSERT_EQ(test, ret, 0);

	kunit_warning_suppress(test) {
		kunit_release_action(test, pwrseq_device_unregister_wrapper, pwrseq);
		KUNIT_EXPECT_SUPPRESSED_WARNING_COUNT(test, 1);
	}

	ret = pwrseq_enable(desc_b);
	KUNIT_EXPECT_EQ(test, ret, -ENODEV);

	ret = pwrseq_disable(desc_a);
	KUNIT_EXPECT_EQ(test, ret, -ENODEV);
}

/*
 * Test that pwrseq_to_device() returns the pwrseq provider's own device,
 * and NULL for a NULL descriptor.
 */
static void pwrseq_to_device_test(struct kunit *test)
{
	struct platform_device *parent = test->priv, *cons;
	const struct pwrseq_target_data *targets[2] = { };
	struct platform_device_info pdevinfo;
	struct pwrseq_target_data target;
	struct pwrseq_unit_data unit;
	struct pwrseq_device *pwrseq;
	struct pwrseq_config config;
	struct pwrseq_desc *desc;
	struct device *dev;

	unit = (struct pwrseq_unit_data){ .name = "unit-a" };
	target = (struct pwrseq_target_data){
		.name = "test-target",
		.unit = &unit,
	};
	targets[0] = &target;

	config = (struct pwrseq_config){
		.parent = &parent->dev,
		.match = pwrseq_test_match_always,
		.targets = targets,
	};

	pwrseq = kunit_pwrseq_device_register(test, &config);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, pwrseq);

	pdevinfo = (struct platform_device_info){
		.name = PWRSEQ_TEST_CONSUMER,
		.id = PLATFORM_DEVID_NONE,
	};

	cons = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cons);

	desc = kunit_pwrseq_get(test, &cons->dev, "test-target");
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, desc);

	dev = pwrseq_to_device(desc);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, dev);
	KUNIT_EXPECT_PTR_EQ(test, dev->parent, &parent->dev);
	KUNIT_EXPECT_TRUE(test, device_is_registered(dev));

	KUNIT_EXPECT_PTR_EQ(test, pwrseq_to_device(NULL), NULL);
}

static struct kunit_case pwrseq_tests[] = {
	KUNIT_CASE(pwrseq_circular_deps),
	KUNIT_CASE(pwrseq_circular_deps_chain),
	KUNIT_CASE(pwrseq_register_valid),
	KUNIT_CASE(pwrseq_register_invalid_args),
	KUNIT_CASE(pwrseq_register_target_without_unit),
	KUNIT_CASE(pwrseq_enable_disable),
	KUNIT_CASE(pwrseq_shared_deps),
	KUNIT_CASE(pwrseq_enable_idempotent),
	KUNIT_CASE(pwrseq_enable_enable_error),
	KUNIT_CASE(pwrseq_enable_rollback_on_top_failure),
	KUNIT_CASE(pwrseq_enable_post_enable),
	KUNIT_CASE(pwrseq_get_target_not_found),
	KUNIT_CASE(pwrseq_put_disables_powered_desc),
	KUNIT_CASE(pwrseq_unregister_with_active_user),
	KUNIT_CASE(pwrseq_to_device_test),
	{ }
};

static struct kunit_suite pwrseq_test_suite = {
	.name = "pwrseq",
	.init = pwrseq_test_parent_init,
	.test_cases = pwrseq_tests,
};

/*
 * Match a consumer to this test provider by resolving its "pwrseq-provider"
 * software-node reference and comparing it to the provider's own fwnode.
 */
static int pwrseq_test_match_by_swnode_ref(struct pwrseq_device *pwrseq,
					   struct device *dev)
{
	const struct software_node *provider_swnode;
	struct fwnode_handle *provider_fwnode, *ref;
	bool match;

	if (!dev_fwnode(dev))
		return PWRSEQ_NO_MATCH;

	ref = fwnode_find_reference(dev_fwnode(dev), "pwrseq-provider", 0);
	if (IS_ERR_OR_NULL(ref))
		return PWRSEQ_NO_MATCH;

	provider_swnode = pwrseq_device_get_drvdata(pwrseq);
	provider_fwnode = software_node_fwnode(provider_swnode);
	match = (ref == provider_fwnode);
	fwnode_handle_put(ref);

	return match ? PWRSEQ_MATCH_OK : PWRSEQ_NO_MATCH;
}

static const struct pwrseq_unit_data pwrseq_swnode_test_unit = {
	.name = "test-target",
};

static const struct pwrseq_target_data pwrseq_swnode_test_target = {
	.name = "test-target",
	.unit = &pwrseq_swnode_test_unit,
};

static const struct pwrseq_target_data *pwrseq_swnode_test_targets[] = {
	&pwrseq_swnode_test_target,
	NULL,
};

struct pwrseq_swnode_provider_pdata {
	const struct software_node *provider_swnode;
};

static int pwrseq_swnode_provider_probe(struct platform_device *pdev)
{
	const struct pwrseq_swnode_provider_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct pwrseq_config config;

	config = (struct pwrseq_config){
		.parent = &pdev->dev,
		.drvdata = (void *)pdata->provider_swnode,
		.match = pwrseq_test_match_by_swnode_ref,
		.targets = pwrseq_swnode_test_targets,
	};

	return PTR_ERR_OR_ZERO(devm_pwrseq_device_register(&pdev->dev, &config));
}

static struct platform_driver pwrseq_swnode_provider_driver = {
	.probe = pwrseq_swnode_provider_probe,
	.driver = {
		.name = PWRSEQ_SWNODE_TEST_PROVIDER,
	},
};

static struct platform_driver pwrseq_swnode_provider_driver_2 = {
	.probe = pwrseq_swnode_provider_probe,
	.driver = {
		.name = PWRSEQ_SWNODE_TEST_PROVIDER_2,
	},
};

struct pwrseq_probe_order_pdata {
	unsigned int probe_count;
	int pwrseq_err;
};

static const struct pwrseq_probe_order_pdata pwrseq_probe_order_pdata_template;

static int pwrseq_probe_order_consumer_probe(struct platform_device *pdev)
{
	struct pwrseq_probe_order_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct pwrseq_desc *desc;

	pdata->probe_count++;

	desc = devm_pwrseq_get(&pdev->dev, "test-target");
	pdata->pwrseq_err = PTR_ERR_OR_ZERO(desc);
	if (IS_ERR(desc))
		return PTR_ERR(desc);

	return 0;
}

static struct platform_driver pwrseq_probe_order_consumer_driver = {
	.probe = pwrseq_probe_order_consumer_probe,
	.driver = {
		.name = PWRSEQ_PROBE_ORDER_TEST_CONSUMER,
	},
};

static struct platform_driver pwrseq_probe_defer_consumer_driver = {
	.probe = pwrseq_probe_order_consumer_probe,
	.driver = {
		.name = PWRSEQ_PROBE_DEFER_TEST_CONSUMER,
	},
};

/*
 * Verify that driver core orders the probe of a pwrseq consumer after its
 * provider. The consumer references the provider through a software node and
 * is registered first and we rely on devlink for ordering.
 */
static void pwrseq_swnode_probe_order(struct kunit *test)
{
	struct property_entry properties[2] = { };
	struct pwrseq_probe_order_pdata *pdata;
	struct platform_device_info pdevinfo;
	struct platform_device *prvd, *cons;
	struct fwnode_handle *fwnode;
	bool bound = false;
	int ret;

	ret = kunit_platform_driver_register(test, &pwrseq_swnode_provider_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_platform_driver_register(test, &pwrseq_probe_order_consumer_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	fwnode = kunit_software_node_register(test, &pwrseq_test_provider_swnode);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fwnode);

	properties[0] = PROPERTY_ENTRY_REF("pwrseq-provider",
					   &pwrseq_test_provider_swnode);

	pdevinfo = (struct platform_device_info){
		.name = PWRSEQ_PROBE_ORDER_TEST_CONSUMER,
		.id = PLATFORM_DEVID_NONE,
		.data = &pwrseq_probe_order_pdata_template,
		.size_data = sizeof(pwrseq_probe_order_pdata_template),
		.properties = properties,
	};

	cons = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cons);

	wait_for_device_probe();
	scoped_guard(device, &cons->dev)
		bound = device_is_bound(&cons->dev);

	KUNIT_ASSERT_FALSE(test, bound);

	pdata = dev_get_platdata(&cons->dev);
	KUNIT_ASSERT_EQ(test, pdata->probe_count, 0);

	pdevinfo = (struct platform_device_info){
		.name = PWRSEQ_SWNODE_TEST_PROVIDER,
		.id = PLATFORM_DEVID_NONE,
		.swnode = &pwrseq_test_provider_swnode,
		.data = &(const struct pwrseq_swnode_provider_pdata){
			.provider_swnode = &pwrseq_test_provider_swnode,
		},
		.size_data = sizeof(struct pwrseq_swnode_provider_pdata),
	};

	prvd = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, prvd);

	wait_for_device_probe();

	scoped_guard(device, &prvd->dev)
		bound = device_is_bound(&prvd->dev);
	KUNIT_ASSERT_TRUE(test, bound);

	scoped_guard(device, &cons->dev)
		bound = device_is_bound(&cons->dev);
	KUNIT_ASSERT_TRUE(test, bound);

	pdata = dev_get_platdata(&cons->dev);
	KUNIT_EXPECT_EQ(test, pdata->probe_count, 1);
	KUNIT_EXPECT_EQ(test, pdata->pwrseq_err, 0);
}

/*
 * Verify that a pwrseq consumer referencing a provider whose software node is
 * not registered yet, defers its probe instead of failing.
 *
 * The provider software node is deliberately left unregistered when the
 * consumer is added. fw_devlink cannot resolve the reference, so it creates no
 * supplier link and does not order the consumer - the consumer's probe() runs
 * and calls pwrseq_get(), which finds no matching provider and returns
 * -EPROBE_DEFER. Once the provider software node and device appear, the
 * deferred consumer probes again and binds.
 */
static void pwrseq_swnode_probe_defer_on_unregistered(struct kunit *test)
{
	struct property_entry properties[2] = { };
	struct pwrseq_probe_order_pdata *pdata;
	struct platform_device_info pdevinfo;
	struct platform_device *prvd, *cons;
	struct fwnode_handle *fwnode;
	bool bound = false;
	int ret;

	ret = kunit_platform_driver_register(test, &pwrseq_swnode_provider_driver_2);
	KUNIT_ASSERT_EQ(test, ret, 0);

	ret = kunit_platform_driver_register(test, &pwrseq_probe_defer_consumer_driver);
	KUNIT_ASSERT_EQ(test, ret, 0);

	properties[0] = PROPERTY_ENTRY_REF("pwrseq-provider",
					   &pwrseq_test_provider_swnode);

	pdevinfo = (struct platform_device_info){
		.name = PWRSEQ_PROBE_DEFER_TEST_CONSUMER,
		.id = PLATFORM_DEVID_NONE,
		.data = &pwrseq_probe_order_pdata_template,
		.size_data = sizeof(pwrseq_probe_order_pdata_template),
		.properties = properties,
	};

	cons = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, cons);

	wait_for_device_probe();
	scoped_guard(device, &cons->dev)
		bound = device_is_bound(&cons->dev);

	KUNIT_ASSERT_FALSE(test, bound);

	pdata = dev_get_platdata(&cons->dev);
	KUNIT_ASSERT_GT(test, pdata->probe_count, 0);
	KUNIT_ASSERT_EQ(test, pdata->pwrseq_err, -EPROBE_DEFER);

	fwnode = kunit_software_node_register(test, &pwrseq_test_provider_swnode);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, fwnode);

	pdevinfo = (struct platform_device_info){
		.name = PWRSEQ_SWNODE_TEST_PROVIDER_2,
		.id = PLATFORM_DEVID_NONE,
		.swnode = &pwrseq_test_provider_swnode,
		.data = &(const struct pwrseq_swnode_provider_pdata){
			.provider_swnode = &pwrseq_test_provider_swnode,
		},
		.size_data = sizeof(struct pwrseq_swnode_provider_pdata),
	};

	prvd = kunit_platform_device_register_full(test, &pdevinfo);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, prvd);

	wait_for_device_probe();

	scoped_guard(device, &prvd->dev)
		bound = device_is_bound(&prvd->dev);
	KUNIT_ASSERT_TRUE(test, bound);

	scoped_guard(device, &cons->dev)
		bound = device_is_bound(&cons->dev);
	KUNIT_ASSERT_TRUE(test, bound);

	pdata = dev_get_platdata(&cons->dev);
	KUNIT_EXPECT_EQ(test, pdata->pwrseq_err, 0);
}

static int pwrseq_swnode_test_init(struct kunit *test)
{
	/*
	 * A device link teardown from an earlier test case, or from this
	 * suite's previous module load, may still be queued on device_link_mq.
	 * Flush it so software_node_register() below doesn't spuriously see
	 * the about-to-be-reused node name as still taken.
	 */
	device_link_wait_removal();

	return 0;
}

static struct kunit_case pwrseq_swnode_probe_order_tests[] = {
	KUNIT_CASE(pwrseq_swnode_probe_order),
	KUNIT_CASE(pwrseq_swnode_probe_defer_on_unregistered),
	{ }
};

static struct kunit_suite pwrseq_swnode_probe_order_test_suite = {
	.name = "pwrseq-swnode-probe-order",
	.init = pwrseq_swnode_test_init,
	.test_cases = pwrseq_swnode_probe_order_tests,
};

kunit_test_suites(&pwrseq_test_suite,
		  &pwrseq_swnode_probe_order_test_suite);

MODULE_DESCRIPTION("KUnit test cases for the power sequencing subsystem");
MODULE_AUTHOR("Bartosz Golaszewski <bartosz.golaszewski@oss.qualcomm.com>");
MODULE_LICENSE("GPL");
