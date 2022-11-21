// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2016 Jonathan Cameron <jic23@kernel.org>
 *
 * Based on a mashup of the hrtimer trigger and continuous sampling proposal of
 * Gregor Boirie <gregor.boirie@parrot.com>
 *
 * Note this is still rather experimental and may eat babies.
 *
 * Todo
 * * Protect against connection of devices that 'need' the top half
 *   handler.
 * * Work out how to run top half handlers in this context if it is
 *   safe to do so (timestamp grabbing for example)
 *
 * Tested against a max1363. Used about 33% cpu for the thread and 20%
 * for generic_buffer piping to /dev/null. Watermark set at 64 on a 128
 * element kfifo buffer.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/irq_work.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/sw_trigger.h>

struct iio_loop_info {
	struct iio_sw_trigger swt;
	struct task_struct *task;
};

static const struct config_item_type iio_loop_type = {
	.ct_owner = THIS_MODULE,
};

static int iio_loop_thread(void *data)
{
	struct iio_trigger *trig = data;

	set_freezable();

	do {
		iio_trigger_poll_chained(trig);
	} while (likely(!kthread_freezable_should_stop(NULL)));

	return 0;
}

static int iio_loop_trigger_set_state(struct iio_trigger *trig, bool state)
{
	struct iio_loop_info *loop_trig = iio_trigger_get_drvdata(trig);

	if (state) {
		loop_trig->task = kthread_run(iio_loop_thread,
					      trig, trig->name);
		if (IS_ERR(loop_trig->task)) {
			dev_err(&trig->dev,
				"failed to create trigger loop thread\n");
			return PTR_ERR(loop_trig->task);
		}
	} else {
		kthread_stop(loop_trig->task);
	}

	return 0;
}

static const struct iio_trigger_ops iio_loop_trigger_ops = {
	.set_trigger_state = iio_loop_trigger_set_state,
};

static struct iio_sw_trigger *iio_trig_loop_probe(const char *name)
{
	struct iio_loop_info *trig_info;
	int ret;

	trig_info = kzalloc(sizeof(*trig_info), GFP_KERNEL);
	if (!trig_info)
		return ERR_PTR(-ENOMEM);

	trig_info->swt.trigger = iio_trigger_alloc(NULL, "%s", name);
	if (!trig_info->swt.trigger) {
		ret = -ENOMEM;
		goto err_free_trig_info;
	}

	iio_trigger_set_drvdata(trig_info->swt.trigger, trig_info);
	trig_info->swt.trigger->ops = &iio_loop_trigger_ops;

	ret = iio_trigger_register(trig_info->swt.trigger);
	if (ret)
		goto err_free_trigger;

	iio_swt_group_init_type_name(&trig_info->swt, name, &iio_loop_type);

	return &trig_info->swt;

err_free_trigger:
	iio_trigger_free(trig_info->swt.trigger);
err_free_trig_info:
	kfree(trig_info);

	return ERR_PTR(ret);
}

static int iio_trig_loop_remove(struct iio_sw_trigger *swt)
{
	struct iio_loop_info *trig_info;

	trig_info = iio_trigger_get_drvdata(swt->trigger);

	iio_trigger_unregister(swt->trigger);
	iio_trigger_free(swt->trigger);
	kfree(trig_info);

	return 0;
}

static const struct iio_sw_trigger_ops iio_trig_loop_ops = {
	.probe = iio_trig_loop_probe,
	.remove = iio_trig_loop_remove,
};

static struct iio_sw_trigger_type iio_trig_loop = {
	.name = "loop",
	.owner = THIS_MODULE,
	.ops = &iio_trig_loop_ops,
};

module_iio_sw_trigger_driver(iio_trig_loop);

MODULE_AUTHOR("Jonathan Cameron <jic23@kernel.org>");
MODULE_DESCRIPTION("Loop based trigger for the iio subsystem");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:iio-trig-loop");
