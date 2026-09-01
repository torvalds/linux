// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023-2026 Intel Corporation */
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/jiffies.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include "cdev.h"
#include "fw_client.h"
#include "host_client.h"
#include "ham.h"
#include "issei_dev.h"

static void issei_rst_state_set(struct issei_device *idev, enum issei_rst_state state)
{
	idev->rst_state = state;
	/* wake up the thread */
	if (waitqueue_active(&idev->wait_rst_state))
		wake_up(&idev->wait_rst_state);
}

static int issei_reset(struct issei_device *idev)
{
	int ret;

	idev->ops->irq_clear(idev);

	issei_cl_all_disconnect(idev);
	issei_fw_cl_remove_all(idev);
	/* No need to check for overflow here, the counter is used only for info */
	idev->all_reset_count++;
	ret = idev->ops->hw_reset(idev, !idev->power_down);
	issei_dmam_setup(idev);
	if (ret) {
		dev_err(&idev->dev, "hw_reset failed ret = %d\n", ret);
		return ret;
	}

	if (idev->power_down) {
		dev_dbg(&idev->dev, "powering down: end of reset\n");
		issei_rst_state_set(idev, ISSEI_RST_STATE_DISABLED);
		return -ENODEV;
	}
	return 0;
}

static int issei_process_read_msg(struct issei_device *idev)
{
	struct issei_dma_data data = {};
	int ret;

	ret = issei_dma_read(idev, &data);
	if (ret)
		return ret;

	dev_dbg(&idev->dev, "Processing response %u %u %u %u\n", data.fw_id, data.host_id,
		data.status, data.length);
	if (data.status != HAMS_SUCCESS) {
		dev_err(&idev->dev, "Command failed with status 0x%02X", data.status);
		kfree(data.buf);
		ret = -EIO;
	} else {
		if (issei_is_ham_rsp(data.fw_id, data.host_id))
			ret = issei_ham_process_ham_rsp(idev, data.buf, data.length);
		else
			ret = issei_cl_read_buf(idev, data.fw_id, data.host_id,
						data.buf, data.length);
	}
	idev->ops->irq_write_generate(idev);
	return ret;
}

static int issei_process_write_msg(struct issei_device *idev)
{
	if (idev->rst_state != ISSEI_RST_STATE_DONE)
		return 0;

	return issei_cl_write_from_queue(idev);
}

static int issei_process_thread(void *_dev)
{
	long timeout, old_timeout = MAX_SCHEDULE_TIMEOUT;
	struct issei_device *idev = _dev;
	int ret;

	while (!kthread_should_stop()) {
		dev_dbg(&idev->dev, "process_work in %d\n", idev->rst_state);
		if (!idev->ops->hw_is_ready(idev) && idev->rst_state > ISSEI_RST_STATE_HW_READY) {
			if (!idev->power_down)
				dev_dbg(&idev->dev, "HW not ready, resetting\n");
			idev->rst_state = ISSEI_RST_STATE_INIT;
		}
		if (idev->power_down)
			idev->rst_state = ISSEI_RST_STATE_INIT;
		WRITE_ONCE(idev->has_data, false);
		dev_dbg(&idev->dev, "reset_step in %d\n", idev->rst_state);
		timeout = MAX_SCHEDULE_TIMEOUT;
		ret = 0;
		switch (idev->rst_state) {
		case ISSEI_RST_STATE_DISABLED:
			if (idev->power_down) {
				dev_dbg(&idev->dev, "Interrupt in power down?\n");
				break;
			}
			idev->rst_state = ISSEI_RST_STATE_INIT;
			fallthrough;

		case ISSEI_RST_STATE_INIT:
			idev->ops->irq_clear(idev);
			idev->ops->irq_sync(idev);

			if (!idev->power_down) {
				idev->reset_count++;
				if (idev->reset_count > ISSEI_MAX_CONSEC_RESET) {
					dev_err(&idev->dev, "reset: reached maximal consecutive resets: disabling the device\n");
					issei_rst_state_set(idev, ISSEI_RST_STATE_DISABLED);
					break;
				}
			}

			ret = issei_reset(idev);
			if (idev->power_down) {
				dev_dbg(&idev->dev, "Powering down\n");
				return 0;
			}
			if (ret)
				break;

			idev->rst_state = ISSEI_RST_STATE_HW_READY;
			timeout = msecs_to_jiffies(ISSEI_RST_HW_READY_TIMEOUT_MSEC);
			break;

		case ISSEI_RST_STATE_HW_READY:
			if (!idev->ops->hw_is_ready(idev)) {
				dev_dbg(&idev->dev, "HW is not ready?\n");
				timeout = old_timeout;
				break;
			}

			dev_dbg(&idev->dev, "HW is ready\n");
			idev->ops->hw_reset_release(idev);
			idev->ops->host_set_ready(idev);
			ret = idev->ops->setup_message_send(idev);
			if (ret)
				break;

			idev->rst_state = ISSEI_RST_STATE_SETUP;
			timeout = msecs_to_jiffies(ISSEI_RST_STEP_TIMEOUT_MSEC);
			break;

		case ISSEI_RST_STATE_SETUP:
			ret = idev->ops->setup_message_recv(idev);
			if (ret) {
				if (ret == -ENODATA) {
					ret = 0;
					timeout = old_timeout;
				}
			} else {
				timeout = msecs_to_jiffies(ISSEI_RST_STEP_TIMEOUT_MSEC);
				ret = issei_ham_send_start_req(idev);
				idev->rst_state = ISSEI_RST_STATE_START;
			}
			break;

		case ISSEI_RST_STATE_START:
			ret = issei_process_read_msg(idev);
			if (!ret) {
				timeout = msecs_to_jiffies(ISSEI_RST_STEP_TIMEOUT_MSEC);
				idev->rst_state = ISSEI_RST_STATE_CLIENT_ENUM;
			} else if (ret == -ENODATA) {
				ret = 0;
				timeout = old_timeout;
			}
			break;

		case ISSEI_RST_STATE_CLIENT_ENUM:
			ret = issei_process_read_msg(idev);
			if (ret) {
				if (ret == -ENODATA) {
					ret = 0;
					timeout = old_timeout;
				}
			} else {
				idev->reset_count = 0;
				idev->rst_state = ISSEI_RST_STATE_DONE;
				dev_dbg(&idev->dev, "Reset finished successfully\n");
			}
			break;

		case ISSEI_RST_STATE_DONE:
			ret = issei_process_read_msg(idev);
			if (ret != 0 && ret != -ENODATA)
				break;

			ret = issei_process_write_msg(idev);
			break;
		}

		if (ret) {
			dev_warn(&idev->dev, "Process failed ret = %d\n", ret);
			idev->rst_state = ISSEI_RST_STATE_INIT;
			continue;
		}

		/*
		 * Every thread that has data to process sets the 'has_data' flag and
		 * triggers the wait queue.
		 * The processing thread, in each loop iteration, resets 'has_data'
		 * and processes all available data.
		 *
		 * After processing, the thread waits for 'has_data' to be set again.
		 *
		 * If the wait function times out but 'has_data' becomes 1 before
		 * the subsequent atomic read check, this is acceptable from a flow
		 * perspective - the thread will continue processing the data.
		 *
		 * The 'has_data' flag cannot become 0 between the wait function and
		 * the atomic read check, since only this thread is allowed to reset it to 0.
		 */

		old_timeout = wait_event_interruptible_timeout(idev->wait_has_data,
							       READ_ONCE(idev->has_data),
							       timeout);
		if (idev->rst_state == ISSEI_RST_STATE_DISABLED)
			continue;

		if (!READ_ONCE(idev->has_data)) {
			dev_warn(&idev->dev, "Timed out at state %d, resetting\n",
				 idev->rst_state);
			idev->rst_state = ISSEI_RST_STATE_INIT;
		}
	}

	return 0;
}

/**
 * issei_start - configure HW device and start processing thread.
 * @idev: the device structure
 *
 * Return: 0 on success, < 0 on failure
 */
int issei_start(struct issei_device *idev)
{
	int ret;

	idev->power_down = false;

	ret = issei_dmam_setup(idev);
	if (ret)
		return ret;

	idev->ops->irq_clear(idev);

	ret = idev->ops->hw_config(idev);
	if (ret)
		return ret;

	idev->process_thread = kthread_run(issei_process_thread, idev,
					   "kisseiprocess/%s", dev_name(&idev->dev));
	if (IS_ERR(idev->process_thread)) {
		ret = PTR_ERR(idev->process_thread);
		dev_err(&idev->dev, "unable to create process thread. ret = %d\n", ret);
		return ret;
	}

	issei_poke_process_thread(idev);
	return 0;
}
EXPORT_SYMBOL_GPL(issei_start);

/**
 * issei_stop - stop interrupts and processing thread.
 * @idev: the device structure
 */
void issei_stop(struct issei_device *idev)
{
	idev->power_down = true;

	idev->ops->irq_clear(idev);
	idev->ops->irq_sync(idev);

	issei_poke_process_thread(idev);

	wait_event_timeout(idev->wait_rst_state,
			   (idev->rst_state == ISSEI_RST_STATE_DISABLED),
			   msecs_to_jiffies(ISSEI_STOP_TIMEOUT_MSEC));
	kthread_stop(idev->process_thread);
}
EXPORT_SYMBOL_GPL(issei_stop);
