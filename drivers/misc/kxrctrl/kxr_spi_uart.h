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

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_core.h>

#pragma once

#define KXR_SPI_UART_TTY_NAME			"ttyKXR"
#define KXR_SPI_UART_MAJOR				0
#define KXR_SPI_UART_MINOR				800
#define KXR_SPI_UART_PORTS				4
#define KXR_SPI_UART_TYPE				800

#define KXR_SPI_UART_CH_START			0xF5
#define KXR_SPI_UART_CH_END				0xF6
#define KXR_SPI_UART_INVALID			0xFFFF

#define KXR_SPI_UART_FRAME_SIZE			1024
#define KXR_SPI_UART_FRAME_BUFF			(KXR_SPI_UART_FRAME_SIZE * 2 + 8)

#pragma pack(1)

struct kxr_spi_uart_pkg {
	u8 length;
	u8 buff[254];
};

#pragma pack()

struct kxr_spi_uart_port {
	struct uart_port port;
	u16 length;
	u8 buff[KXR_SPI_UART_FRAME_BUFF];
};

struct kxr_spi_uart {
	struct kxr_spi_uart_port ports[KXR_SPI_UART_PORTS];

	struct kxr_spi_uart_pkg tx_pkg;
	struct kxr_spi_uart_pkg rx_pkg;
	struct mutex tx_lock;
	bool send_pending;

	struct mutex user_lock;
	u8 user_buff[KXR_SPI_UART_FRAME_BUFF];

#if KXR_CACHE_SIZE_FIX == 0
	struct {
		struct kxr_cache tx_cache;
		u8 tx_buff[KXR_SPI_UART_FRAME_BUFF * 2];
	};
#else
	struct kxr_cache tx_cache;
#endif
};

void kxr_spi_uart_clear(struct kxr_spi_uart *uart);

