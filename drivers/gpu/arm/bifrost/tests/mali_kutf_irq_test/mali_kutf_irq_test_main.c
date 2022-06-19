// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2016-2018, 2020-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "mali_kbase.h"
#include <device/mali_kbase_device.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <backend/gpu/mali_kbase_irq_internal.h>

#include <kutf/kutf_suite.h>
#include <kutf/kutf_utils.h>

/*
 * This file contains the code which is used for measuring interrupt latency
 * of the Mali GPU IRQ. In particular, function mali_kutf_irq_latency() is
 * used with this purpose and it is called within KUTF framework - a kernel
 * unit test framework. The measured latency provided by this test should
 * be representative for the latency of the Mali JOB/MMU IRQs as well.
 */

/* KUTF test application pointer for this test */
static struct kutf_application *irq_app;

/**
 * struct kutf_irq_fixture_data - test fixture used by the test functions.
 * @kbdev:	kbase device for the GPU.
 *
 */
struct kutf_irq_fixture_data {
	struct kbase_device *kbdev;
};

#define SEC_TO_NANO(s)	      ((s)*1000000000LL)

/* ID for the GPU IRQ */
#define GPU_IRQ_HANDLER 2

#define NR_TEST_IRQS ((u32)1000000)

/* IRQ for the test to trigger. Currently POWER_CHANGED_SINGLE as it is
 * otherwise unused in the DDK
 */
#define TEST_IRQ POWER_CHANGED_SINGLE

#define IRQ_TIMEOUT HZ

/* Kernel API for setting irq throttle hook callback and irq time in us*/
extern int kbase_set_custom_irq_handler(struct kbase_device *kbdev,
		irq_handler_t custom_handler,
		int irq_type);
extern irqreturn_t kbase_gpu_irq_test_handler(int irq, void *data, u32 val);

static DECLARE_WAIT_QUEUE_HEAD(wait);
static bool triggered;
static u64 irq_time;

static void *kbase_untag(void *ptr)
{
	return (void *)(((uintptr_t) ptr) & ~3);
}

/**
 * kbase_gpu_irq_custom_handler - Custom IRQ throttle handler
 * @irq:  IRQ number
 * @data: Data associated with this IRQ
 *
 * Return: state of the IRQ
 */
static irqreturn_t kbase_gpu_irq_custom_handler(int irq, void *data)
{
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_STATUS));
	irqreturn_t result;
	u64 tval;
	bool has_test_irq = val & TEST_IRQ;

	if (has_test_irq) {
		tval = ktime_get_real_ns();
		/* Clear the test source only here */
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR),
				TEST_IRQ);
		/* Remove the test IRQ status bit */
		val = val ^ TEST_IRQ;
	}

	result = kbase_gpu_irq_test_handler(irq, data, val);

	if (has_test_irq) {
		irq_time = tval;
		triggered = true;
		wake_up(&wait);
		result = IRQ_HANDLED;
	}

	return result;
}

/**
 * mali_kutf_irq_default_create_fixture() - Creates the fixture data required
 *                                          for all the tests in the irq suite.
 * @context:             KUTF context.
 *
 * Return: Fixture data created on success or NULL on failure
 */
static void *mali_kutf_irq_default_create_fixture(
		struct kutf_context *context)
{
	struct kutf_irq_fixture_data *data;

	data = kutf_mempool_alloc(&context->fixture_pool,
			sizeof(struct kutf_irq_fixture_data));

	if (!data)
		goto fail;

	/* Acquire the kbase device */
	data->kbdev = kbase_find_device(-1);
	if (data->kbdev == NULL) {
		kutf_test_fail(context, "Failed to find kbase device");
		goto fail;
	}

	return data;

fail:
	return NULL;
}

/**
 * mali_kutf_irq_default_remove_fixture() - Destroy fixture data previously
 *                          created by mali_kutf_irq_default_create_fixture.
 *
 * @context:             KUTF context.
 */
static void mali_kutf_irq_default_remove_fixture(
		struct kutf_context *context)
{
	struct kutf_irq_fixture_data *data = context->fixture;
	struct kbase_device *kbdev = data->kbdev;

	kbase_release_device(kbdev);
}

/**
 * mali_kutf_irq_latency() - measure GPU IRQ latency
 * @context:		kutf context within which to perform the test
 *
 * The test triggers IRQs manually, and measures the
 * time between triggering the IRQ and the IRQ handler being executed.
 *
 * This is not a traditional test, in that the pass/fail status has little
 * meaning (other than indicating that the IRQ handler executed at all). Instead
 * the results are in the latencies provided with the test result. There is no
 * meaningful pass/fail result that can be obtained here, instead the latencies
 * are provided for manual analysis only.
 */
static void mali_kutf_irq_latency(struct kutf_context *context)
{
	struct kutf_irq_fixture_data *data = context->fixture;
	struct kbase_device *kbdev = data->kbdev;
	u64 min_time = U64_MAX, max_time = 0, average_time = 0;
	u32 i;
	const char *results;

	/* Force GPU to be powered */
	kbase_pm_context_active(kbdev);
	kbase_pm_wait_for_desired_state(kbdev);

	kbase_set_custom_irq_handler(kbdev, kbase_gpu_irq_custom_handler,
			GPU_IRQ_HANDLER);

	for (i = 1; i <= NR_TEST_IRQS; i++) {
		u64 start_time = ktime_get_real_ns();

		triggered = false;

		/* Trigger fake IRQ */
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_RAWSTAT),
				TEST_IRQ);

		if (wait_event_timeout(wait, triggered, IRQ_TIMEOUT) == 0) {
			/* Wait extra time to see if it would come */
			wait_event_timeout(wait, triggered, 10 * IRQ_TIMEOUT);
			break;
		}

		if ((irq_time - start_time) < min_time)
			min_time = irq_time - start_time;
		if ((irq_time - start_time) > max_time)
			max_time = irq_time - start_time;
		average_time += irq_time - start_time;

		udelay(10);
	}

	/* Go back to default handler */
	kbase_set_custom_irq_handler(kbdev, NULL, GPU_IRQ_HANDLER);

	kbase_pm_context_idle(kbdev);

	if (i > NR_TEST_IRQS) {
		do_div(average_time, NR_TEST_IRQS);
		results = kutf_dsprintf(&context->fixture_pool,
				"Min latency = %lldns, Max latency = %lldns, Average latency = %lldns\n",
				min_time, max_time, average_time);
		kutf_test_pass(context, results);
	} else {
		results = kutf_dsprintf(&context->fixture_pool,
				"Timed out for the %u-th IRQ (loop_limit: %u), triggered late: %d\n",
				i, NR_TEST_IRQS, triggered);
		kutf_test_fail(context, results);
	}
}

/**
 * mali_kutf_irq_test_main_init - Module entry point for this test.
 *
 * Return: 0 on success, error code otherwise
 */
static int __init mali_kutf_irq_test_main_init(void)
{
	struct kutf_suite *suite;

	irq_app = kutf_create_application("irq");

	if (irq_app == NULL) {
		pr_warn("Creation of test application failed!\n");
		return -ENOMEM;
	}

	suite = kutf_create_suite(irq_app, "irq_default",
			1, mali_kutf_irq_default_create_fixture,
			mali_kutf_irq_default_remove_fixture);

	if (suite == NULL) {
		pr_warn("Creation of test suite failed!\n");
		kutf_destroy_application(irq_app);
		return -ENOMEM;
	}

	kutf_add_test(suite, 0x0, "irq_latency",
			mali_kutf_irq_latency);
	return 0;
}

/**
 * mali_kutf_irq_test_main_exit - Module exit point for this test.
 */
static void __exit mali_kutf_irq_test_main_exit(void)
{
	kutf_destroy_application(irq_app);
}

module_init(mali_kutf_irq_test_main_init);
module_exit(mali_kutf_irq_test_main_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ARM Ltd.");
MODULE_VERSION("1.0");
