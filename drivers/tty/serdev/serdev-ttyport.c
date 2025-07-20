// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016-2017 Linaro Ltd., Rob Herring <robh@kernel.org>
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

static size_t ttyport_receive_buf(struct tty_port *port, const u8 *cp,
				  const u8 *fp, size_t count)
{
	struct serdev_controller *ctrl = port->client_data;
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	size_t ret;

	if (!test_bit(SERPORT_ACTIVE, &serport->flags))
		return 0;

	ret = serdev_controller_receive_buf(ctrl, cp, count);

	dev_WARN_ONCE(&ctrl->dev, ret > count,
				"receive_buf returns %zu (count = %zu)\n",
				ret, count);
	if (ret > count)
		return count;

	return ret;
}

static void ttyport_write_wakeup(struct tty_port *port)
{
	struct serdev_controller *ctrl = port->client_data;
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty;

	tty = tty_port_tty_get(port);
	if (!tty)
		return;

	if (test_and_clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags) &&
	    test_bit(SERPORT_ACTIVE, &serport->flags))
		serdev_controller_write_wakeup(ctrl);

	/* Wake up any tty_wait_until_sent() */
	wake_up_interruptible(&tty->write_wait);

	tty_kref_put(tty);
}

static const struct tty_port_client_operations client_ops = {
	.receive_buf = ttyport_receive_buf,
	.write_wakeup = ttyport_write_wakeup,
};

/*
 * Callback functions from the serdev core.
 */

static ssize_t ttyport_write_buf(struct serdev_controller *ctrl, const u8 *data, size_t len)
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

static int ttyport_open(struct serdev_controller *ctrl)
{
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty;
	struct ktermios ktermios;
	int ret;

	tty = tty_init_dev(serport->tty_drv, serport->tty_idx);
	if (IS_ERR(tty))
		return PTR_ERR(tty);
	serport->tty = tty;

	if (!tty->ops->open || !tty->ops->close) {
		ret = -ENODEV;
		goto err_unlock;
	}

	ret = tty->ops->open(serport->tty, NULL);
	if (ret)
		goto err_close;

	tty_unlock(serport->tty);

	/* Bring the UART into a known 8 bits no parity hw fc state */
	ktermios = tty->termios;
	ktermios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP |
			      INLCR | IGNCR | ICRNL | IXON);
	ktermios.c_oflag &= ~OPOST;
	ktermios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	ktermios.c_cflag &= ~(CSIZE | PARENB);
	ktermios.c_cflag |= CS8;
	ktermios.c_cflag |= CRTSCTS;
	/* Hangups are not supported so make sure to ignore carrier detect. */
	ktermios.c_cflag |= CLOCAL;
	tty_set_termios(tty, &ktermios);

	set_bit(SERPORT_ACTIVE, &serport->flags);

	return 0;

err_close:
	tty->ops->close(tty, NULL);
err_unlock:
	tty_unlock(tty);
	tty_release_struct(tty, serport->tty_idx);

	return ret;
}

static void ttyport_close(struct serdev_controller *ctrl)
{
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty = serport->tty;

	clear_bit(SERPORT_ACTIVE, &serport->flags);

	tty_lock(tty);
	if (tty->ops->close)
		tty->ops->close(tty, NULL);
	tty_unlock(tty);

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
	return ktermios.c_ospeed;
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

static int ttyport_set_parity(struct serdev_controller *ctrl,
			      enum serdev_parity parity)
{
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty = serport->tty;
	struct ktermios ktermios = tty->termios;

	ktermios.c_cflag &= ~(PARENB | PARODD | CMSPAR);
	if (parity != SERDEV_PARITY_NONE) {
		ktermios.c_cflag |= PARENB;
		if (parity == SERDEV_PARITY_ODD)
			ktermios.c_cflag |= PARODD;
	}

	tty_set_termios(tty, &ktermios);

	if ((tty->termios.c_cflag & (PARENB | PARODD | CMSPAR)) !=
	    (ktermios.c_cflag & (PARENB | PARODD | CMSPAR)))
		return -EINVAL;

	return 0;
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
		return -EOPNOTSUPP;

	return tty->ops->tiocmget(tty);
}

static int ttyport_set_tiocm(struct serdev_controller *ctrl, unsigned int set, unsigned int clear)
{
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty = serport->tty;

	if (!tty->ops->tiocmset)
		return -EOPNOTSUPP;

	return tty->ops->tiocmset(tty, set, clear);
}

static int ttyport_break_ctl(struct serdev_controller *ctrl, unsigned int break_state)
{
	struct serport *serport = serdev_controller_get_drvdata(ctrl);
	struct tty_struct *tty = serport->tty;

	if (!tty->ops->break_ctl)
		return -EOPNOTSUPP;

	return tty->ops->break_ctl(tty, break_state);
}

static const struct serdev_controller_ops ctrl_ops = {
	.write_buf = ttyport_write_buf,
	.write_flush = ttyport_write_flush,
	.open = ttyport_open,
	.close = ttyport_close,
	.set_flow_control = ttyport_set_flow_control,
	.set_parity = ttyport_set_parity,
	.set_baudrate = ttyport_set_baudrate,
	.wait_until_sent = ttyport_wait_until_sent,
	.get_tiocm = ttyport_get_tiocm,
	.set_tiocm = ttyport_set_tiocm,
	.break_ctl = ttyport_break_ctl,
};

struct device *serdev_tty_port_register(struct tty_port *port,
					struct device *host,
					struct device *parent,
					struct tty_driver *drv, int idx)
{
	struct serdev_controller *ctrl;
	struct serport *serport;
	int ret;

	if (!port || !drv || !parent)
		return ERR_PTR(-ENODEV);

	ctrl = serdev_controller_alloc(host, parent, sizeof(struct serport));
	if (!ctrl)
		return ERR_PTR(-ENOMEM);
	serport = serdev_controller_get_drvdata(ctrl);

	serport->port = port;
	serport->tty_idx = idx;
	serport->tty_drv = drv;

	ctrl->ops = &ctrl_ops;

	port->client_ops = &client_ops;
	port->client_data = ctrl;

	ret = serdev_controller_add(ctrl);
	if (ret)
		goto err_reset_data;

	dev_info(&ctrl->dev, "tty port %s%d registered\n", drv->name, idx);
	return &ctrl->dev;

err_reset_data:
	port->client_data = NULL;
	port->client_ops = &tty_port_default_client_ops;
	serdev_controller_put(ctrl);

	return ERR_PTR(ret);
}

int serdev_tty_port_unregister(struct tty_port *port)
{
	struct serdev_controller *ctrl = port->client_data;
	struct serport *serport = serdev_controller_get_drvdata(ctrl);

	if (!serport)
		return -ENODEV;

	serdev_controller_remove(ctrl);
	port->client_data = NULL;
	port->client_ops = &tty_port_default_client_ops;
	serdev_controller_put(ctrl);

	return 0;
}
