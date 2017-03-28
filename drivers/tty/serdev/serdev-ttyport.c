/*
 * Copyright (C) 2016-2017 Linaro Ltd., Rob Herring <robh@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/serdev.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/poll.h>

#define SERPORT_ACTIVE		1

struct serport {
	struct tty_port *port;
	struct tty_struct *tty;
	struct tty_driver *tty_drv;
	int tty_idx;
	unsigned long flags;
};

/*
 * Callback functions from the tty port.
 */

static int ttyport_receive_buf(struct tty_port *port, const unsigned char *cp,
				const unsigned char *fp, size_t count)
{
	struct serdev_controller *ctrl = port->client_data;
	struct serport *serport = serdev_controller_get_drvdata(ctrl);

	if (!test_bit(SERPORT_ACTIVE, &serport->flags))
		return 0;

	return serdev_controller_receive_buf(ctrl, cp, count);
}

static void ttyport_write_wakeup(struct tty_port *port)
{
	struct serdev_controller *ctrl = port->client_data;
	struct serport *serport = serdev_controller_get_drvdata(ctrl);

	if (test_and_clear_bit(TTY_DO_WRITE_WAKEUP, &port->tty->flags) &&
	    test_bit(SERPORT_ACTIVE, &serport->flags))
		serdev_controller_write_wakeup(ctrl);

	wake_up_interruptible_poll(&port->tty->write_wait, POLLOUT);
}

static const struct tty_port_client_operations client_ops = {
	.receive_buf = ttyport_receive_buf,
	.write_wakeup = ttyport_write_wakeup,
};

/*
 * Callback functions from the serdev core.
 */

static int ttyport_write_buf(struct serdev_controller *ctrl, const unsigned char *data, size_t len)
{
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty = serport->tty;

	if (!test_bit(SERPORT_ACTIVE, &serport->flags))
		return 0;

	set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
	return tty->ops->write(serport->tty, data, len);
}

static void ttyport_write_flush(struct serdev_controller *ctrl)
{
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty = serport->tty;

	tty_driver_flush_buffer(tty);
}

static int ttyport_write_room(struct serdev_controller *ctrl)
{
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty = serport->tty;

	return tty_write_room(tty);
}

static int ttyport_open(struct serdev_controller *ctrl)
{
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty;
	struct ktermios ktermios;

	tty = tty_init_dev(serport->tty_drv, serport->tty_idx);
	if (IS_ERR(tty))
		return PTR_ERR(tty);
	serport->tty = tty;

	serport->port->client_ops = &client_ops;
	serport->port->client_data = ctrl;

	if (tty->ops->open)
		tty->ops->open(serport->tty, NULL);
	else
		tty_port_open(serport->port, tty, NULL);

	/* Bring the UART into a known 8 bits no parity hw fc state */
	ktermios = tty->termios;
	ktermios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP |
			      INLCR | IGNCR | ICRNL | IXON);
	ktermios.c_oflag &= ~OPOST;
	ktermios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	ktermios.c_cflag &= ~(CSIZE | PARENB);
	ktermios.c_cflag |= CS8;
	ktermios.c_cflag |= CRTSCTS;
	tty_set_termios(tty, &ktermios);

	set_bit(SERPORT_ACTIVE, &serport->flags);

	tty_unlock(serport->tty);
	return 0;
}

static void ttyport_close(struct serdev_controller *ctrl)
{
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty = serport->tty;

	clear_bit(SERPORT_ACTIVE, &serport->flags);

	if (tty->ops->close)
		tty->ops->close(tty, NULL);

	tty_release_struct(tty, serport->tty_idx);
}

static unsigned int ttyport_set_baudrate(struct serdev_controller *ctrl, unsigned int speed)
{
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty = serport->tty;
	struct ktermios ktermios = tty->termios;

	ktermios.c_cflag &= ~CBAUD;
	tty_termios_encode_baud_rate(&ktermios, speed, speed);

	/* tty_set_termios() return not checked as it is always 0 */
	tty_set_termios(tty, &ktermios);
	return speed;
}

static void ttyport_set_flow_control(struct serdev_controller *ctrl, bool enable)
{
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty = serport->tty;
	struct ktermios ktermios = tty->termios;

	if (enable)
		ktermios.c_cflag |= CRTSCTS;
	else
		ktermios.c_cflag &= ~CRTSCTS;

	tty_set_termios(tty, &ktermios);
}

static void ttyport_wait_until_sent(struct serdev_controller *ctrl, long timeout)
{
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty = serport->tty;

	tty_wait_until_sent(tty, timeout);
}

static int ttyport_get_tiocm(struct serdev_controller *ctrl)
{
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty = serport->tty;

	if (!tty->ops->tiocmget)
		return -ENOTSUPP;

	return tty->driver->ops->tiocmget(tty);
}

static int ttyport_set_tiocm(struct serdev_controller *ctrl, unsigned int set, unsigned int clear)
{
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty = serport->tty;

	if (!tty->ops->tiocmset)
		return -ENOTSUPP;

	return tty->driver->ops->tiocmset(tty, set, clear);
}

static const struct serdev_controller_ops ctrl_ops = {
	.write_buf = ttyport_write_buf,
	.write_flush = ttyport_write_flush,
	.write_room = ttyport_write_room,
	.open = ttyport_open,
	.close = ttyport_close,
	.set_flow_control = ttyport_set_flow_control,
	.set_baudrate = ttyport_set_baudrate,
	.wait_until_sent = ttyport_wait_until_sent,
	.get_tiocm = ttyport_get_tiocm,
	.set_tiocm = ttyport_set_tiocm,
};

struct device *serdev_tty_port_register(struct tty_port *port,
					struct device *parent,
					struct tty_driver *drv, int idx)
{
	struct serdev_controller *ctrl;
	struct serport *serport;
	int ret;

	if (!port || !drv || !parent)
		return ERR_PTR(-ENODEV);

	ctrl = serdev_controller_alloc(parent, sizeof(struct serport));
	if (!ctrl)
		return ERR_PTR(-ENOMEM);
	serport = serdev_controller_get_drvdata(ctrl);

	serport->port = port;
	serport->tty_idx = idx;
	serport->tty_drv = drv;

	ctrl->ops = &ctrl_ops;

	ret = serdev_controller_add(ctrl);
	if (ret)
		goto err_controller_put;

	dev_info(&ctrl->dev, "tty port %s%d registered\n", drv->name, idx);
	return &ctrl->dev;

err_controller_put:
	serdev_controller_put(ctrl);
	return ERR_PTR(ret);
}

void serdev_tty_port_unregister(struct tty_port *port)
{
	struct serdev_controller *ctrl = port->client_data;
	struct serport *serport = serdev_controller_get_drvdata(ctrl);

	if (!serport)
		return;

	serdev_controller_remove(ctrl);
	port->client_ops = NULL;
	port->client_data = NULL;
	serdev_controller_put(ctrl);
}
