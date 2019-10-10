/*
 *
 * (C) COPYRIGHT 2016-2018 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "mali_kbase.h"
#include <midgard/backend/gpu/mali_kbase_device_internal.h>

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
struct kutf_application *irq_app;

/**
 * struct kutf_irq_fixture data - test fixture used by the test functions.
 * @kbdev:	kbase device for the GPU.
 *
 */
struct kutf_irq_fixture_data {
	struct kbase_device *kbdev;
};

#define SEC_TO_NANO(s)	      ((s)*1000000000LL)

/* ID for the GPU IRQ */
#define GPU_IRQ_HANDLER 2

#define NR_TEST_IRQS 1000000

/* IRQ for the test to trigger. Currently MULTIPLE_GPU_FAULTS as we would not
 * expect to see this in normal use (e.g., when Android is running). */
#define TEST_IRQ MULTIPLE_GPU_FAULTS

#define IRQ_TIMEOUT HZ

/* Kernel API for setting irq throttle hook callback and irq time in us*/
extern int kbase_set_custom_irq_handler(struct kbase_device *kbdev,
		irq_handler_t custom_handler,
		int irq_type);
extern irqreturn_t kbase_gpu_irq_handler(int irq, void *data);

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
	u32 val;

	val = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_STATUS));
	if (val & TEST_IRQ) {
		struct timespec tval;

		getnstimeofday(&tval);
		irq_time = SEC_TO_NANO(tval.tv_sec) + (tval.tv_nsec);

		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_CLEAR), val);

		triggered = true;
		wake_up(&wait);

		return IRQ_HANDLED;
	}

	/* Trigger main irq handler */
	return kbase_gpu_irq_handler(irq, data);
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
	int i;
	bool test_failed = false;

	/* Force GPU to be powered */
	kbase_pm_context_active(kbdev);

	kbase_set_custom_irq_handler(kbdev, kbase_gpu_irq_custom_handler,
			GPU_IRQ_HANDLER);

	for (i = 0; i < NR_TEST_IRQS; i++) {
		struct timespec tval;
		u64 start_time;
		int ret;

		triggered = false;
		getnstimeofday(&tval);
		start_time = SEC_TO_NANO(tval.tv_sec) + (tval.tv_nsec);

		/* Trigger fake IRQ */
		kbase_reg_write(kbdev, GPU_CONTROL_REG(GPU_IRQ_RAWSTAT),
				TEST_IRQ);

		ret = wait_event_timeout(wait, triggered != false, IRQ_TIMEOUT);

		if (ret == 0) {
			kutf_test_fail(context, "Timed out waiting for IRQ\n");
			test_failed = true;
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

	if (!test_failed) {
		const char *results;

		do_div(average_time, NR_TEST_IRQS);
		results = kutf_dsprintf(&context->fixture_pool,
				"Min latency = %lldns, Max latency = %lldns, Average latency = %lldns\n",
				min_time, max_time, average_time);
		kutf_test_pass(context, results);
	}
}

/**
 * Module entry point for this test.
 */
int mali_kutf_irq_test_main_init(void)
{
	struct kutf_suite *suite;

	irq_app = kutf_create_application("irq");

	if (NULL == irq_app) {
		pr_warn("Creation of test application failed!\n");
		return -ENOMEM;
	}

	suite = kutf_create_suite(irq_app, "irq_default",
			1, mali_kutf_irq_default_create_fixture,
			mali_kutf_irq_default_remove_fixture);

	if (NULL == suite) {
		pr_warn("Creation of test suite failed!\n");
		kutf_destroy_application(irq_app);
		return -ENOMEM;
	}

	kutf_add_test(suite, 0x0, "irq_latency",
			mali_kutf_irq_latency);
	return 0;
}

/**
 * Module exit point for this test.
 */
void mali_kutf_irq_test_main_exit(void)
{
	kutf_destroy_application(irq_app);
}

module_init(mali_kutf_irq_test_main_init);
module_exit(mali_kutf_irq_test_main_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ARM Ltd.");
MODULE_VERSION("1.0");
