/*
 * Copyright 2013 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alon Levy
 */

#include "qxl_drv.h"

irqreturn_t qxl_irq_handler(DRM_IRQ_ARGS)
{
	struct drm_device *dev = (struct drm_device *) arg;
	struct qxl_device *qdev = (struct qxl_device *)dev->dev_private;
	uint32_t pending;

	pending = xchg(&qdev->ram_header->int_pending, 0);

	atomic_inc(&qdev->irq_received);

	if (pending & QXL_INTERRUPT_DISPLAY) {
		atomic_inc(&qdev->irq_received_display);
		wake_up_all(&qdev->display_event);
		qxl_queue_garbage_collect(qdev, false);
	}
	if (pending & QXL_INTERRUPT_CURSOR) {
		atomic_inc(&qdev->irq_received_cursor);
		wake_up_all(&qdev->cursor_event);
	}
	if (pending & QXL_INTERRUPT_IO_CMD) {
		atomic_inc(&qdev->irq_received_io_cmd);
		wake_up_all(&qdev->io_cmd_event);
	}
	if (pending & QXL_INTERRUPT_ERROR) {
		/* TODO: log it, reset device (only way to exit this condition)
		 * (do it a certain number of times, afterwards admit defeat,
		 * to avoid endless loops).
		 */
		qdev->irq_received_error++;
		qxl_io_log(qdev, "%s: driver is in bug mode.\n", __func__);
	}
	if (pending & QXL_INTERRUPT_CLIENT_MONITORS_CONFIG) {
		qxl_io_log(qdev, "QXL_INTERRUPT_CLIENT_MONITORS_CONFIG\n");
		schedule_work(&qdev->client_monitors_config_work);
	}
	qdev->ram_header->int_mask = QXL_INTERRUPT_MASK;
	outb(0, qdev->io_base + QXL_IO_UPDATE_IRQ);
	return IRQ_HANDLED;
}

static void qxl_client_monitors_config_work_func(struct work_struct *work)
{
	struct qxl_device *qdev = container_of(work, struct qxl_device,
					       client_monitors_config_work);

	qxl_display_read_client_monitors_config(qdev);
}

int qxl_irq_init(struct qxl_device *qdev)
{
	int ret;

	init_waitqueue_head(&qdev->display_event);
	init_waitqueue_head(&qdev->cursor_event);
	init_waitqueue_head(&qdev->io_cmd_event);
	INIT_WORK(&qdev->client_monitors_config_work,
		  qxl_client_monitors_config_work_func);
	atomic_set(&qdev->irq_received, 0);
	atomic_set(&qdev->irq_received_display, 0);
	atomic_set(&qdev->irq_received_cursor, 0);
	atomic_set(&qdev->irq_received_io_cmd, 0);
	qdev->irq_received_error = 0;
	ret = drm_irq_install(qdev->ddev);
	qdev->ram_header->int_mask = QXL_INTERRUPT_MASK;
	if (unlikely(ret != 0)) {
		DRM_ERROR("Failed installing irq: %d\n", ret);
		return 1;
	}
	return 0;
}
