/* SPDX-License-Identifier: GPL-2.0-only */
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

#include "kxr_cache.h"

#include <linux/spi/spi.h>

#pragma once

#define KXR_SPI_XFER_COMPLETION			1
#define KXR_SPI_XFER_WAIT_QUEUE			0

#define KXR_SPI_XFER_SPEED_HZ			(8 * 1000 * 1000)
#define KXR_SPI_XFER_BITS_PER_WORD		8
#define KXR_SPI_XFER_MODE				SPI_MODE_0
#define KXR_SPI_XFER_USER_SECONDS		3

#define KXR_SPI_IOC_WriteUart0(len)		KXR_SPI_IOC_WriteUart(0, len)
#define KXR_SPI_IOC_WriteUart1(len)		KXR_SPI_IOC_WriteUart(1, len)
#define KXR_SPI_IOC_WriteUart2(len)		KXR_SPI_IOC_WriteUart(2, len)
#define KXR_SPI_IOC_WriteUart3(len)		KXR_SPI_IOC_WriteUart(3, len)
#define KXR_SPI_IOC_WriteSpi(len)		KXR_SPI_IOC_WriteUart(4, len)
#define KXR_SPI_IOC_Transfer(len)		KXR_SPI_IOC('K', 0x05, len)
#define KXR_SPI_IOC_WriteOnly(len)		KXR_SPI_IOC('K', 0x06, len)
#define KXR_SPI_IOC_ReadOnly(len)		KXR_SPI_IOC('K', 0x07, len)

#define KXR_SPI_IOC_GetPowerMode		KXR_SPI_IOC('K', 0x08, 0)
#define KXR_SPI_IOC_SetPowerMode		KXR_SPI_IOC('K', 0x09, 0)
#define KXR_SPI_IOC_GetWorkMode			KXR_SPI_IOC('K', 0x0A, 0)
#define KXR_SPI_IOC_SetWorkMode			KXR_SPI_IOC('K', 0x0B, 0)

#define KXR_SPI_IOC_TYPE_LEN			8
#define KXR_SPI_IOC_NR_LEN				8
#define KXR_SPI_IOC_SIZE_LEN			16

#define KXR_SPI_IOC_TYPE_MASK			KXR_SPI_IOC_LENGTH_TO_MASK(KXR_SPI_IOC_TYPE_LEN)
#define KXR_SPI_IOC_NR_MASK		KXR_SPI_IOC_LENGTH_TO_MASK(KXR_SPI_IOC_NR_LEN)
#define KXR_SPI_IOC_SIZE_MASK			KXR_SPI_IOC_LENGTH_TO_MASK(KXR_SPI_IOC_SIZE_LEN)

#define KXR_SPI_IOC_TYPE_SHIFT			0
#define KXR_SPI_IOC_NR_SHIFT			(KXR_SPI_IOC_TYPE_SHIFT + KXR_SPI_IOC_TYPE_LEN)
#define KXR_SPI_IOC_SIZE_SHIFT			(KXR_SPI_IOC_NR_SHIFT + KXR_SPI_IOC_NR_LEN)

#define KXR_SPI_IOC_LENGTH_TO_MASK(len) \
	((1 << (len)) - 1)

#define KXR_SPI_IOC_GET_VALUE(cmd, shift, mask) \
	(((cmd) >> (shift)) & (mask))

#define KXR_SPI_IOC_GET_TYPE(cmd) \
	KXR_SPI_IOC_GET_VALUE(cmd, KXR_SPI_IOC_TYPE_SHIFT, KXR_SPI_IOC_TYPE_MASK)

#define KXR_SPI_IOC_GET_NR(cmd) \
	KXR_SPI_IOC_GET_VALUE(cmd, KXR_SPI_IOC_NR_SHIFT, KXR_SPI_IOC_NR_MASK)

#define KXR_SPI_IOC_GET_SIZE(cmd) \
	KXR_SPI_IOC_GET_VALUE(cmd, KXR_SPI_IOC_SIZE_SHIFT, KXR_SPI_IOC_SIZE_MASK)

#define KXR_SPI_IOC_GET_CMD_RAW(cmd) \
	((cmd) & KXR_SPI_IOC_LENGTH_TO_MASK(KXR_SPI_IOC_TYPE_LEN + KXR_SPI_IOC_NR_LEN))

#define KXR_SPI_IOC(type, nr, size) \
	(((type) & KXR_SPI_IOC_TYPE_MASK) << KXR_SPI_IOC_TYPE_SHIFT | \
	((nr) & KXR_SPI_IOC_NR_MASK) << KXR_SPI_IOC_NR_SHIFT | \
	((size) & KXR_SPI_IOC_SIZE_MASK) << KXR_SPI_IOC_SIZE_SHIFT)

#define KXR_SPI_IOC_WriteUart(index, len) \
	KXR_SPI_IOC('K', index, len)

enum kxr_spi_work_mode {
	KXR_SPI_WORK_MODE_USER,
	KXR_SPI_WORK_MODE_UART,
	KXR_SPI_WORK_MODE_XCHG,
	KXR_SPI_WORK_MODE_OLD,
	KXR_SPI_WORK_MODE_IDLE,
	KXR_SPI_WORK_MODE_EXIT,
};

struct kxr_spi_message {
	const void *tx_buff;
	void *rx_buff;
};

struct kxr_aphost;

struct kxr_spi_xfer {
	struct task_struct *task;

#if KXR_SPI_XFER_COMPLETION
	struct completion sync_completion;
#else
#if KXR_SPI_XFER_WAIT_QUEUE
	wait_queue_head_t wait_queue;
#endif

	unsigned int sync_pending;
#endif

	bool irq_disabled;
	enum kxr_spi_work_mode mode_def;

#ifndef CONFIG_KXR_SIMULATION_TEST
	struct spi_device *spi;
	int irq;
#else
	struct platform_device *spi;
#endif

	u32 user_seconds;

	u8 tx_buff[256];
	u8 rx_buff[256];

	struct mutex mode_mutex;
	bool (*handler)(struct kxr_aphost *apohst);
};

void kxr_spi_xfer_wait(struct kxr_spi_xfer *xfer);
void kxr_spi_xfer_wakeup(struct kxr_spi_xfer *xfer);
int kxr_spi_xfer_sync(struct kxr_spi_xfer *xfer, const void *tx_buff, void *rx_buff, int length);
int kxr_spi_xfer_sync_user(struct kxr_spi_xfer *xfer, void __user *buff,
		int length, bool write, bool read);
bool kxr_spi_xfer_post_xchg(struct kxr_spi_xfer *xfer);
bool kxr_spi_xfer_mode_set(struct kxr_spi_xfer *xfer, enum kxr_spi_work_mode mode);
enum kxr_spi_work_mode kxr_spi_xfer_mode_get(void);

static inline bool kxr_spi_xfer_set_user(struct kxr_spi_xfer *xfer, u32 seconds)
{
	xfer->user_seconds = seconds;
	return kxr_spi_xfer_mode_set(xfer, KXR_SPI_WORK_MODE_USER);
}

static inline int kxr_spi_xfer_sync_dfu(struct kxr_spi_xfer *xfer,
		const void *tx_buff, void *rx_buff, int length)
{
	kxr_spi_xfer_set_user(xfer, 3);
	return kxr_spi_xfer_sync(xfer, tx_buff, rx_buff, length);
}

static inline bool kxr_spi_xfer_exited(void)
{
	return kxr_spi_xfer_mode_get() == KXR_SPI_WORK_MODE_EXIT;
}
