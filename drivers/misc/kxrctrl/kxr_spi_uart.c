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

static int kxr_spi_uart_line;

static void kxr_spi_uart_write_spi(struct kxr_aphost *aphost, const void *buff, int length)
{
	struct kxr_spi_uart *uart = &aphost->uart;

	mutex_lock(&uart->tx_lock);
	kxr_cache_write(&uart->tx_cache, buff, length);
	mutex_unlock(&uart->tx_lock);
}

static int kxr_spi_uart_write_spi_user(struct kxr_aphost *aphost,
		const void __user *buff, int length)
{
	struct kxr_spi_uart *uart = &aphost->uart;

	mutex_lock(&uart->tx_lock);
	if (kxr_spi_xfer_mode_get() != KXR_SPI_WORK_MODE_UART) {
		mutex_unlock(&uart->tx_lock);
		return 0;
	}
	kxr_cache_write_user(&uart->tx_cache, buff, length);
	mutex_unlock(&uart->tx_lock);

	kxr_spi_xfer_wakeup(&aphost->xfer);

	return 0;
}

static void kxr_spi_uart_port_append(struct kxr_aphost *aphost,
		struct kxr_spi_uart_port *port, const u8 *buff, int length)
{
	const u8 *buff_end = buff + length;

	while (buff < buff_end) {
		u8 value = *buff++;

		switch (value) {
		case KXR_SPI_UART_CH_START:
			port->buff[0] = KXR_SPI_UART_CH_START;
			port->length = 1;
			break;

		case KXR_SPI_UART_CH_END:
			if (port->length < sizeof(port->buff)) {
				port->buff[port->length] = KXR_SPI_UART_CH_END;
				kxr_spi_uart_write_spi(aphost, port->buff, port->length + 1);
			}

			port->length = KXR_SPI_UART_INVALID;
			break;

		default:
			if (port->length < sizeof(port->buff)) {
				port->buff[port->length] = value;
				port->length++;
			} else {
				port->length = KXR_SPI_UART_INVALID;
			}
			break;
		}
	}
}

static void kxr_spi_uart_read_uart(struct kxr_aphost *aphost, int index)
{
	struct kxr_spi_uart_port *kxr_port = aphost->uart.ports + index;
	struct uart_port *port = &kxr_port->port;
	struct uart_state *state;
	struct circ_buf *xmit;

	state = port->state;
	if (state == NULL)
		return;

	xmit = &state->xmit;

	while (true) {
		int count = CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE);

		if (count < 1)
			break;

		kxr_spi_uart_port_append(aphost, kxr_port, xmit->buf + xmit->tail, count);
		xmit->tail = (xmit->tail + count) & (UART_XMIT_SIZE - 1);
		port->icount.tx += count;
	}

	uart_write_wakeup(port);
}

static void kxr_spi_uart_write_uart(struct kxr_spi_uart *uart,
		int index, const void *buff, int length)
{
	struct uart_state *state = uart->ports[index].port.state;
	struct tty_struct *tty;
	struct tty_port *port;

	if (state == NULL)
		return;

	tty = state->port.tty;
	if (tty == NULL)
		return;

	port = tty->port;
	if (port == NULL)
		return;

	tty_insert_flip_string(port, buff, length);
	tty_flip_buffer_push(port);
}

static void kxr_spi_uart_write_uart_all(struct kxr_spi_uart *uart, const void *buff, int length)
{
	int index;

	for (index = 0; index < KXR_SPI_UART_PORTS; index++)
		kxr_spi_uart_write_uart(uart, index, buff, length);
}

static void kxr_spi_uart_read_uart_all(struct kxr_aphost *aphost)
{
	int index;

	for (index = 0; index < KXR_SPI_UART_PORTS; index++)
		kxr_spi_uart_read_uart(aphost, index);
}

static int kxr_spi_uart_write_uart_user(struct kxr_aphost *aphost,
		int index, const void __user *buff, int length)
{
	struct kxr_spi_uart *uart = &aphost->uart;
	unsigned long remain;

	if (length > sizeof(uart->user_buff))
		return -EINVAL;

	mutex_lock(&uart->user_lock);

	remain = copy_from_user(uart->user_buff, buff, length);
	if (remain > 0)
		dev_err(&aphost->xfer.spi->dev, "Failed to copy_from_user: %ld\n", remain);
	else
		kxr_spi_uart_write_uart(uart, index, uart->user_buff, length);

	mutex_unlock(&uart->user_lock);

	return 0;
}

static unsigned int kxr_spi_uart_tx_empty(struct uart_port *port)
{
	struct kxr_aphost *aphost = kxr_aphost_get_uart_data(port);

	return kxr_cache_is_empty(&aphost->uart.tx_cache);
}

static void kxr_spi_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static unsigned int kxr_spi_uart_get_mctrl(struct uart_port *port)
{
	return 0;
}

static void kxr_spi_uart_stop_tx(struct uart_port *port)
{
}

static void kxr_spi_uart_start_tx(struct uart_port *port)
{
	struct kxr_aphost *aphost = kxr_aphost_get_uart_data(port);

	aphost->uart.send_pending = true;

	if (kxr_spi_xfer_mode_get() == KXR_SPI_WORK_MODE_UART)
		kxr_spi_xfer_wakeup(&aphost->xfer);
}

static void kxr_spi_uart_stop_rx(struct uart_port *port)
{
}

static void kxr_spi_uart_enable_ms(struct uart_port *port)
{
}

static void kxr_spi_uart_break_ctl(struct uart_port *port, int ctl)
{
}

static int kxr_spi_uart_startup(struct uart_port *port)
{
	uart_circ_clear(&port->state->xmit);
	return 0;
}

static void kxr_spi_uart_shutdown(struct uart_port *port)
{
}

static void kxr_spi_uart_set_termios(struct uart_port *port,
		struct ktermios *termios, const struct ktermios *old)
{
	int baud = tty_termios_baud_rate(termios);

	uart_update_timeout(port, termios->c_cflag, baud);
}

static const char *kxr_spi_uart_type(struct uart_port *port)
{
	return KXR_SPI_UART_TTY_NAME;
}

static void kxr_spi_uart_release_port(struct uart_port *port)
{
}

static int kxr_spi_uart_request_port(struct uart_port *port)
{
	return 0;
}

static void kxr_spi_uart_config_port(struct uart_port *port, int flags)
{
	port->type = KXR_SPI_UART_TYPE;
}

static int kxr_spi_uart_verify_port(struct uart_port *port, struct serial_struct *serial)
{
	return 0;
}

static int kxr_spi_uart_ioctl(struct uart_port *port, unsigned int command, unsigned long args)
{
	struct kxr_aphost *aphost = kxr_aphost_get_uart_data(port);

	switch (KXR_SPI_IOC_GET_CMD_RAW(command)) {
	case KXR_SPI_IOC_WriteUart0(0):
		return kxr_spi_uart_write_uart_user(aphost, 0,
				(void __user *) args, KXR_SPI_IOC_GET_SIZE(command));

	case KXR_SPI_IOC_WriteUart1(0):
		return kxr_spi_uart_write_uart_user(aphost, 1,
				(void __user *) args, KXR_SPI_IOC_GET_SIZE(command));

	case KXR_SPI_IOC_WriteUart2(0):
		return kxr_spi_uart_write_uart_user(aphost, 2,
				(void __user *) args, KXR_SPI_IOC_GET_SIZE(command));

	case KXR_SPI_IOC_WriteUart3(0):
		return kxr_spi_uart_write_uart_user(aphost, 3,
				(void __user *) args, KXR_SPI_IOC_GET_SIZE(command));

	case KXR_SPI_IOC_WriteSpi(0):
		return kxr_spi_uart_write_spi_user(aphost,
				(void __user *) args, KXR_SPI_IOC_GET_SIZE(command));

	case KXR_SPI_IOC_Transfer(0):
		return kxr_spi_xfer_sync_user(&aphost->xfer,
				(void __user *) args, KXR_SPI_IOC_GET_SIZE(command), true, true);

	case KXR_SPI_IOC_WriteOnly(0):
		return kxr_spi_xfer_sync_user(&aphost->xfer,
				(void __user *) args, KXR_SPI_IOC_GET_SIZE(command), true, false);

	case KXR_SPI_IOC_ReadOnly(0):
		return kxr_spi_xfer_sync_user(&aphost->xfer,
				(void __user *) args, KXR_SPI_IOC_GET_SIZE(command), false, true);

	case KXR_SPI_IOC_GetPowerMode:
		return kxr_aphost_power_mode_get();

	case KXR_SPI_IOC_SetPowerMode:
		kxr_aphost_power_mode_set(aphost, args);
		break;

	case KXR_SPI_IOC_GetWorkMode:
		return kxr_spi_xfer_mode_get();

	case KXR_SPI_IOC_SetWorkMode:
		kxr_spi_xfer_mode_set(&aphost->xfer, args);
		break;

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static const struct uart_ops kxr_spi_uart_uart_ops = {
	.tx_empty = kxr_spi_uart_tx_empty,
	.set_mctrl = kxr_spi_uart_set_mctrl,
	.get_mctrl = kxr_spi_uart_get_mctrl,
	.stop_tx = kxr_spi_uart_stop_tx,
	.start_tx = kxr_spi_uart_start_tx,
	.stop_rx = kxr_spi_uart_stop_rx,
	.enable_ms = kxr_spi_uart_enable_ms,
	.break_ctl = kxr_spi_uart_break_ctl,
	.startup = kxr_spi_uart_startup,
	.shutdown = kxr_spi_uart_shutdown,
	.set_termios = kxr_spi_uart_set_termios,
	.type = kxr_spi_uart_type,
	.release_port = kxr_spi_uart_release_port,
	.request_port = kxr_spi_uart_request_port,
	.config_port = kxr_spi_uart_config_port,
	.verify_port = kxr_spi_uart_verify_port,
	.ioctl = kxr_spi_uart_ioctl,
};

static struct uart_driver kxr_spi_uart_uart_driver = {
	.owner = THIS_MODULE,
	.major = KXR_SPI_UART_MAJOR,
	.driver_name = KXR_SPI_UART_TTY_NAME,
	.dev_name = KXR_SPI_UART_TTY_NAME,
	.minor = KXR_SPI_UART_MINOR,
	.nr = KXR_SPI_UART_PORTS,
	.cons = NULL
};

void kxr_spi_uart_clear(struct kxr_spi_uart *uart)
{
	mutex_lock(&uart->tx_lock);
	kxr_cache_clear(&uart->tx_cache);
	mutex_unlock(&uart->tx_lock);
}

bool kxr_spi_uart_sync(struct kxr_aphost *aphost)
{
	struct kxr_spi_uart *uart = &aphost->uart;
	struct kxr_spi_uart_pkg *tx_pkg = &uart->tx_pkg;
	struct kxr_spi_uart_pkg *rx_pkg = &uart->rx_pkg;
	u8 *tx_buff;
	int length;
	int ret;

	if (uart->send_pending) {
		uart->send_pending = false;
		kxr_spi_uart_read_uart_all(aphost);
	}

	mutex_lock(&uart->tx_lock);
	tx_buff = kxr_cache_read(&uart->tx_cache, tx_pkg->buff, sizeof(tx_pkg->buff));
	mutex_unlock(&uart->tx_lock);

	if (tx_buff > tx_pkg->buff) {
		tx_pkg->length = length = tx_buff - tx_pkg->buff;
	} else {
#ifndef CONFIG_KXR_SIMULATION_TEST
		if (gpiod_get_value(aphost->gpio_irq) == 0)
			return false;
#endif

		tx_pkg->length = 0;

		ret = kxr_spi_xfer_sync(&aphost->xfer, tx_pkg, rx_pkg, 1);
		if (ret < 0)
			return false;

		length = rx_pkg->length;

		if (length == 0 || length == 0xFF)
			return false;
	}

	ret = kxr_spi_xfer_sync(&aphost->xfer, tx_pkg, rx_pkg, length + 1);
	if (ret < 0)
		return false;

	if (length > rx_pkg->length)
		length = rx_pkg->length;

	if (length > 0 && length != 0xFF)
		kxr_spi_uart_write_uart_all(uart, rx_pkg->buff, length);

	return true;
}

int kxr_spi_uart_probe(struct kxr_aphost *aphost)
{
	struct kxr_spi_uart *uart = &aphost->uart;
	int line;
	int ret;

#if KXR_CACHE_SIZE_FIX == 0
	kxr_cache_init(&uart->tx_cache, sizeof(uart->tx_buff));
#else
	kxr_cache_init(&uart->tx_cache, 0);
#endif

	mutex_init(&uart->tx_lock);
	mutex_init(&uart->user_lock);

	if (kxr_spi_uart_line == 0) {
		ret = uart_register_driver(&kxr_spi_uart_uart_driver);
		if (ret < 0) {
			dev_err(&aphost->xfer.spi->dev,
					"Failed to uart_register_driver: %d\n", ret);
			return ret;
		}
	}

	for (line = 0; line < KXR_SPI_UART_PORTS; line++) {
		struct kxr_spi_uart_port *spi_port = uart->ports + line;
		struct uart_port *port = &spi_port->port;

		spi_port->length = KXR_SPI_UART_INVALID;

		port->line = kxr_spi_uart_line + line;
		port->ops = &kxr_spi_uart_uart_ops;
		port->uartclk = 1843200;
		port->fifosize = 256;
		port->iobase = (unsigned long) port;
		port->irq = 0;
		port->iotype = SERIAL_IO_PORT;
		port->flags = UPF_BOOT_AUTOCONF;
		port->private_data = aphost;

		ret = uart_add_one_port(&kxr_spi_uart_uart_driver, port);
		if (ret < 0) {
			dev_err(&aphost->xfer.spi->dev, "Failed to uart_add_one_port: %d\n", ret);
			return ret;
		}
	}

	kxr_spi_uart_line += line;

	return 0;
}

void kxr_spi_uart_remove(struct kxr_aphost *aphost)
{
	struct kxr_spi_uart *uart = &aphost->uart;
	int line;

	for (line = 0; line < KXR_SPI_UART_PORTS; line++)
		uart_remove_one_port(&kxr_spi_uart_uart_driver, &uart->ports[line].port);

	uart_unregister_driver(&kxr_spi_uart_uart_driver);

	mutex_destroy(&uart->user_lock);
	mutex_destroy(&uart->tx_lock);
}
