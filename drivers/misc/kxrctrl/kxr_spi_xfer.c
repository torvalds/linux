// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI controller driver for the nordic52832 SoCs
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "kxr_aphost.h"

static ulong kxr_spi_irq_times;
static ulong kxr_spi_xfer_times;
static ulong kxr_spi_xfer_bytes;
static int kxr_spi_xfer_mode = KXR_SPI_WORK_MODE_USER;

int kxr_spi_xfer_sync(struct kxr_spi_xfer *xfer, const void *tx_buff, void *rx_buff, int length)
{
#ifndef CONFIG_KXR_SIMULATION_TEST
	struct spi_transfer transfer = {
		.tx_buf = tx_buff,
		.rx_buf = rx_buff,
		.len = length,
	};
	struct spi_message message;

	spi_message_init(&message);
	spi_message_add_tail(&transfer, &message);

	kxr_spi_xfer_bytes += length;
	kxr_spi_xfer_times++;

	return spi_sync(xfer->spi, &message);
#else
	return 0;
#endif
}

int kxr_spi_xfer_sync_user(struct kxr_spi_xfer *xfer, void __user *buff,
		int length, bool write, bool read)
{
	unsigned long remain;
	void *tx_buff;
	int ret;

	if (length > sizeof(xfer->tx_buff)) {
		dev_err(&xfer->spi->dev, "Invalid length: %d\n", length);
		return -EINVAL;
	}

	if (write) {
		tx_buff = xfer->tx_buff;

		remain = copy_from_user(tx_buff, buff, length);
		if (remain > 0) {
			dev_err(&xfer->spi->dev, "Failed to copy_from_user: %ld\n", remain);
			return -EFAULT;
		}
	} else {
		tx_buff = NULL;
	}

	kxr_spi_xfer_set_user(xfer, 3);

	ret = kxr_spi_xfer_sync(xfer, tx_buff, xfer->rx_buff, length);
	if (ret < 0) {
		dev_err(&xfer->spi->dev, "Failed to kxr_spi_xfer_sync: %d\n", ret);
		return ret;
	}

	if (read) {
		remain = copy_to_user(buff, xfer->rx_buff, length);
		if (remain > 0) {
			dev_err(&xfer->spi->dev, "Failed to copy_to_user: %ld\n", remain);
			return -EFAULT;
		}
	}

	return 0;
}

void kxr_spi_xfer_wait(struct kxr_spi_xfer *xfer)
{
#if KXR_SPI_XFER_COMPLETION
	wait_for_completion(&xfer->sync_completion);
	WRITE_ONCE(xfer->sync_completion.done, 0);
#else
#if KXR_SPI_XFER_WAIT_QUEUE
	wait_event_interruptible(xfer->wait_queue, READ_ONCE(xfer->sync_pending) == 0);
#else
	while (READ_ONCE(xfer->sync_pending) == 0) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule();
	}
#endif

	WRITE_ONCE(xfer->sync_pending, 0);
#endif
}

void kxr_spi_xfer_wakeup(struct kxr_spi_xfer *xfer)
{
#if KXR_SPI_XFER_COMPLETION
	complete(&xfer->sync_completion);
#else
	WRITE_ONCE(xfer->sync_pending, 1);

#if KXR_SPI_XFER_WAIT_QUEUE
	wake_up_interruptible(&xfer->wait_queue);
#else
	wake_up_process(xfer->task);
#endif
#endif
}

static int kxr_spi_xfer_thread(void *data)
{
	struct kxr_aphost *aphost = (struct kxr_aphost *) data;
	struct kxr_spi_xfer *xfer = &aphost->xfer;

	while (kxr_spi_xfer_mode != KXR_SPI_WORK_MODE_EXIT) {

		while (xfer->handler(aphost))
			;
		if (xfer->irq_disabled) {
			xfer->irq_disabled = false;
#ifndef CONFIG_KXR_SIMULATION_TEST
			enable_irq(xfer->irq);
#endif
		}

		kxr_spi_xfer_wait(xfer);
	}

	return 0;
}

#ifndef CONFIG_KXR_SIMULATION_TEST
static irqreturn_t kxr_spi_xfer_isr(int irq, void *dev_id)
{
	struct kxr_spi_xfer *xfer = (struct kxr_spi_xfer *) dev_id;

	disable_irq_nosync(irq);
	xfer->irq_disabled = true;
	kxr_spi_xfer_wakeup(xfer);
	kxr_spi_irq_times++;

	return IRQ_HANDLED;
}
#endif

static bool kxr_spi_xfer_mode_set_def(struct kxr_spi_xfer *xfer)
{
	return kxr_spi_xfer_mode_set(xfer, xfer->mode_def);
}

static void kxr_spi_xfer_resume(struct kxr_aphost *aphost)
{
	kxr_spi_xfer_mode_set_def(&aphost->xfer);
	kxr_spi_uart_clear(&aphost->uart);
	kxr_spi_xchg_clear(&aphost->xchg);
}

static bool kxr_spi_xfer_mode_user(struct kxr_aphost *aphost)
{
	struct kxr_spi_xfer *xfer = &aphost->xfer;
	int times;

	for (times = 10; times > 0; times--) {
		msleep(100);
		if (kxr_spi_xfer_mode != KXR_SPI_WORK_MODE_USER)
			return true;
	}

	pr_info("user_seconds = %d, mode = %d\n", xfer->user_seconds, kxr_spi_xfer_mode);

	if (xfer->user_seconds > 0)
		xfer->user_seconds--;
	else
		kxr_spi_xfer_resume(aphost);

	return true;
}

static bool kxr_spi_xfer_mode_idle(struct kxr_aphost *aphost)
{
	pr_info("%s:%d\n", __FILE__, __LINE__);
	if (kxr_aphost_power_disabled())
		return false;
	kxr_spi_xfer_resume(aphost);

	return true;
}

static bool kxr_spi_xfer_mode_exit(struct kxr_aphost *aphost)
{
	pr_info("%s:%d\n", __FILE__, __LINE__);
	return false;
}

static bool kxr_spi_xfer_mode_uart(struct kxr_aphost *aphost)
{
	return kxr_spi_uart_sync(aphost);
}

static bool kxr_spi_xfer_mode_xchg(struct kxr_aphost *aphost)
{
#ifndef CONFIG_KXR_SIMULATION_TEST
	int times;
#endif
	kxr_spi_xfer_mode_set_def(&aphost->xfer);

	kxr_spi_xchg_sync(aphost);

#ifndef CONFIG_KXR_SIMULATION_TEST
	for (times = 0; gpiod_get_value(aphost->gpio_irq) == 0 && times < 20; times++)
		usleep_range(900, 999);
#endif

	kxr_spi_xchg_sync(aphost);

	return false;
}

static bool kxr_spi_xfer_mode_old(struct kxr_aphost *aphost)
{
	return js_thread(aphost);
}

bool kxr_spi_xfer_mode_set(struct kxr_spi_xfer *xfer, enum kxr_spi_work_mode mode)
{
	int mode_backup;

	mutex_lock(&xfer->mode_mutex);
	mode_backup = kxr_spi_xfer_mode;
	if (mode_backup == mode || kxr_spi_xfer_exited()) {
		mutex_unlock(&xfer->mode_mutex);
		return false;
	}
	kxr_spi_xfer_mode = mode;

	switch (mode) {
	case KXR_SPI_WORK_MODE_USER:
		dev_info(&xfer->spi->dev, "KXR_SPI_WORK_MODE_USER\n");
		xfer->handler = kxr_spi_xfer_mode_user;

		if (xfer->user_seconds < KXR_SPI_XFER_USER_SECONDS)
			xfer->user_seconds = KXR_SPI_XFER_USER_SECONDS;

		kxr_spi_xfer_wakeup(xfer);
		msleep(20);
		break;

	case KXR_SPI_WORK_MODE_IDLE:
		dev_info(&xfer->spi->dev, "KXR_SPI_WORK_MODE_IDLE\n");
		xfer->handler = kxr_spi_xfer_mode_idle;
		break;

	case KXR_SPI_WORK_MODE_EXIT:
		dev_info(&xfer->spi->dev, "KXR_SPI_WORK_MODE_EXIT\n");
		xfer->handler = kxr_spi_xfer_mode_exit;
		kxr_spi_xfer_wakeup(xfer);
		break;

	case KXR_SPI_WORK_MODE_UART:
		dev_info(&xfer->spi->dev, "KXR_SPI_WORK_MODE_UART\n");
		xfer->mode_def = KXR_SPI_WORK_MODE_UART;
		xfer->handler = kxr_spi_xfer_mode_uart;
		break;

	case KXR_SPI_WORK_MODE_XCHG:
		dev_info(&xfer->spi->dev, "KXR_SPI_WORK_MODE_XCHG\n");
		xfer->handler = kxr_spi_xfer_mode_xchg;
		break;

	case KXR_SPI_WORK_MODE_OLD:
		dev_info(&xfer->spi->dev, "KXR_SPI_WORK_MODE_OLD\n");
		xfer->mode_def = KXR_SPI_WORK_MODE_OLD;
		xfer->handler = kxr_spi_xfer_mode_old;
		break;

	default:
		dev_err(&xfer->spi->dev, "Invalid xfer mode: %d\n", mode);
		kxr_spi_xfer_mode = mode_backup;
		mutex_unlock(&xfer->mode_mutex);
		return false;
	}

	mutex_unlock(&xfer->mode_mutex);

	return true;
}

enum kxr_spi_work_mode kxr_spi_xfer_mode_get(void)
{
	return kxr_spi_xfer_mode;
}

bool kxr_spi_xfer_post_xchg(struct kxr_spi_xfer *xfer)
{

	switch (kxr_spi_xfer_mode) {
	case KXR_SPI_WORK_MODE_USER:
		return true;
	case KXR_SPI_WORK_MODE_OLD:
		break;
	default:
		kxr_spi_xfer_mode_set(xfer, KXR_SPI_WORK_MODE_XCHG);
		break;
	}

	kxr_spi_xfer_wakeup(xfer);
	return true;
}

int kxr_spi_xfer_setup(struct spi_device *spi)
{
#ifndef CONFIG_KXR_SIMULATION_TEST
	int ret;

	spi->bits_per_word = KXR_SPI_XFER_BITS_PER_WORD;
	spi->max_speed_hz = KXR_SPI_XFER_SPEED_HZ;
	spi->mode = KXR_SPI_XFER_MODE;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "Failed to spi_setup: %d\n", ret);
		return ret;
	}
#endif

	return 0;
}

int kxr_spi_xfer_start(struct kxr_aphost *aphost)
{
	struct kxr_spi_xfer *xfer = &aphost->xfer;
#ifndef CONFIG_KXR_SIMULATION_TEST
	int ret;
#endif

	xfer->task = kthread_create(kxr_spi_xfer_thread, aphost, "kxr-spi-xfer");
	if (IS_ERR(xfer->task)) {
		dev_err(&xfer->spi->dev, "Failed to kthread_create\n");
		return PTR_ERR(xfer->task);
	}

	wake_up_process(xfer->task);

#ifndef CONFIG_KXR_SIMULATION_TEST
	if (aphost->gpio_irq != NULL) {
		xfer->irq = gpiod_to_irq(aphost->gpio_irq);

		ret = devm_request_irq(&xfer->spi->dev, xfer->irq,
				kxr_spi_xfer_isr, IRQF_TRIGGER_HIGH, "kxr-spi-xfer", xfer);
		if (ret < 0) {
			dev_err(&xfer->spi->dev, "Failed to request_irq: %d\n", ret);
			return ret;
		}
	}
#endif

	return 0;
}

static ssize_t xfer_mode_show(struct device *dev, struct device_attribute *attr, char *buff)
{
	return scnprintf(buff, PAGE_SIZE, "%d\n", kxr_spi_xfer_mode);
}

static ssize_t xfer_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buff, size_t size)
{
	struct kxr_aphost *aphost = kxr_aphost_get_drv_data(dev);
	int mode;

	if (kstrtoint(buff, 10, &mode) < 0)
		return -EINVAL;

	kxr_spi_xfer_mode_set(&aphost->xfer, mode);

	return size;
}

static DEVICE_ATTR_RW(xfer_mode);

int kxr_spi_xfer_probe(struct kxr_aphost *aphost)
{
	struct kxr_spi_xfer *xfer = &aphost->xfer;

	mutex_init(&(xfer->mode_mutex));

#if KXR_SPI_XFER_COMPLETION
	init_completion(&xfer->sync_completion);
#else
#if KXR_SPI_XFER_WAIT_QUEUE
	init_waitqueue_head(&xfer->wait_queue);
#endif

	xfer->sync_pending = 0;
#endif

	kxr_spi_xfer_mode_set(xfer, KXR_SPI_WORK_MODE_UART);
	device_create_file(&xfer->spi->dev, &dev_attr_xfer_mode);

	return 0;
}

void kxr_spi_xfer_remove(struct kxr_aphost *aphost)
{
	struct kxr_spi_xfer *xfer = &aphost->xfer;

	device_remove_file(&xfer->spi->dev, &dev_attr_xfer_mode);
	kxr_spi_xfer_mode_set(xfer, KXR_SPI_WORK_MODE_EXIT);
	mutex_destroy(&xfer->mode_mutex);
	kthread_stop(xfer->task);
}
