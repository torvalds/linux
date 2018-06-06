/**
 * xhci-dbgtty.c - tty glue for xHCI debug capability
 *
 * Copyright (C) 2017 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */

#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>

#include "xhci.h"
#include "xhci-dbgcap.h"

static unsigned int
dbc_send_packet(struct dbc_port *port, char *packet, unsigned int size)
{
	unsigned int		len;

	len = kfifo_len(&port->write_fifo);
	if (len < size)
		size = len;
	if (size != 0)
		size = kfifo_out(&port->write_fifo, packet, size);
	return size;
}

static int dbc_start_tx(struct dbc_port *port)
	__releases(&port->port_lock)
	__acquires(&port->port_lock)
{
	int			len;
	struct dbc_request	*req;
	int			status = 0;
	bool			do_tty_wake = false;
	struct list_head	*pool = &port->write_pool;

	while (!list_empty(pool)) {
		req = list_entry(pool->next, struct dbc_request, list_pool);
		len = dbc_send_packet(port, req->buf, DBC_MAX_PACKET);
		if (len == 0)
			break;
		do_tty_wake = true;

		req->length = len;
		list_del(&req->list_pool);

		spin_unlock(&port->port_lock);
		status = dbc_ep_queue(port->out, req, GFP_ATOMIC);
		spin_lock(&port->port_lock);

		if (status) {
			list_add(&req->list_pool, pool);
			break;
		}
	}

	if (do_tty_wake && port->port.tty)
		tty_wakeup(port->port.tty);

	return status;
}

static void dbc_start_rx(struct dbc_port *port)
	__releases(&port->port_lock)
	__acquires(&port->port_lock)
{
	struct dbc_request	*req;
	int			status;
	struct list_head	*pool = &port->read_pool;

	while (!list_empty(pool)) {
		if (!port->port.tty)
			break;

		req = list_entry(pool->next, struct dbc_request, list_pool);
		list_del(&req->list_pool);
		req->length = DBC_MAX_PACKET;

		spin_unlock(&port->port_lock);
		status = dbc_ep_queue(port->in, req, GFP_ATOMIC);
		spin_lock(&port->port_lock);

		if (status) {
			list_add(&req->list_pool, pool);
			break;
		}
	}
}

static void
dbc_read_complete(struct xhci_hcd *xhci, struct dbc_request *req)
{
	unsigned long		flags;
	struct xhci_dbc		*dbc = xhci->dbc;
	struct dbc_port		*port = &dbc->port;

	spin_lock_irqsave(&port->port_lock, flags);
	list_add_tail(&req->list_pool, &port->read_queue);
	tasklet_schedule(&port->push);
	spin_unlock_irqrestore(&port->port_lock, flags);
}

static void dbc_write_complete(struct xhci_hcd *xhci, struct dbc_request *req)
{
	unsigned long		flags;
	struct xhci_dbc		*dbc = xhci->dbc;
	struct dbc_port		*port = &dbc->port;

	spin_lock_irqsave(&port->port_lock, flags);
	list_add(&req->list_pool, &port->write_pool);
	switch (req->status) {
	case 0:
		dbc_start_tx(port);
		break;
	case -ESHUTDOWN:
		break;
	default:
		xhci_warn(xhci, "unexpected write complete status %d\n",
			  req->status);
		break;
	}
	spin_unlock_irqrestore(&port->port_lock, flags);
}

static void xhci_dbc_free_req(struct dbc_ep *dep, struct dbc_request *req)
{
	kfree(req->buf);
	dbc_free_request(dep, req);
}

static int
xhci_dbc_alloc_requests(struct dbc_ep *dep, struct list_head *head,
			void (*fn)(struct xhci_hcd *, struct dbc_request *))
{
	int			i;
	struct dbc_request	*req;

	for (i = 0; i < DBC_QUEUE_SIZE; i++) {
		req = dbc_alloc_request(dep, GFP_ATOMIC);
		if (!req)
			break;

		req->length = DBC_MAX_PACKET;
		req->buf = kmalloc(req->length, GFP_KERNEL);
		if (!req->buf) {
			xhci_dbc_free_req(dep, req);
			break;
		}

		req->complete = fn;
		list_add_tail(&req->list_pool, head);
	}

	return list_empty(head) ? -ENOMEM : 0;
}

static void
xhci_dbc_free_requests(struct dbc_ep *dep, struct list_head *head)
{
	struct dbc_request	*req;

	while (!list_empty(head)) {
		req = list_entry(head->next, struct dbc_request, list_pool);
		list_del(&req->list_pool);
		xhci_dbc_free_req(dep, req);
	}
}

static int dbc_tty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct dbc_port		*port = driver->driver_state;

	tty->driver_data = port;

	return tty_port_install(&port->port, driver, tty);
}

static int dbc_tty_open(struct tty_struct *tty, struct file *file)
{
	struct dbc_port		*port = tty->driver_data;

	return tty_port_open(&port->port, tty, file);
}

static void dbc_tty_close(struct tty_struct *tty, struct file *file)
{
	struct dbc_port		*port = tty->driver_data;

	tty_port_close(&port->port, tty, file);
}

static int dbc_tty_write(struct tty_struct *tty,
			 const unsigned char *buf,
			 int count)
{
	struct dbc_port		*port = tty->driver_data;
	unsigned long		flags;

	spin_lock_irqsave(&port->port_lock, flags);
	if (count)
		count = kfifo_in(&port->write_fifo, buf, count);
	dbc_start_tx(port);
	spin_unlock_irqrestore(&port->port_lock, flags);

	return count;
}

static int dbc_tty_put_char(struct tty_struct *tty, unsigned char ch)
{
	struct dbc_port		*port = tty->driver_data;
	unsigned long		flags;
	int			status;

	spin_lock_irqsave(&port->port_lock, flags);
	status = kfifo_put(&port->write_fifo, ch);
	spin_unlock_irqrestore(&port->port_lock, flags);

	return status;
}

static void dbc_tty_flush_chars(struct tty_struct *tty)
{
	struct dbc_port		*port = tty->driver_data;
	unsigned long		flags;

	spin_lock_irqsave(&port->port_lock, flags);
	dbc_start_tx(port);
	spin_unlock_irqrestore(&port->port_lock, flags);
}

static int dbc_tty_write_room(struct tty_struct *tty)
{
	struct dbc_port		*port = tty->driver_data;
	unsigned long		flags;
	int			room = 0;

	spin_lock_irqsave(&port->port_lock, flags);
	room = kfifo_avail(&port->write_fifo);
	spin_unlock_irqrestore(&port->port_lock, flags);

	return room;
}

static int dbc_tty_chars_in_buffer(struct tty_struct *tty)
{
	struct dbc_port		*port = tty->driver_data;
	unsigned long		flags;
	int			chars = 0;

	spin_lock_irqsave(&port->port_lock, flags);
	chars = kfifo_len(&port->write_fifo);
	spin_unlock_irqrestore(&port->port_lock, flags);

	return chars;
}

static void dbc_tty_unthrottle(struct tty_struct *tty)
{
	struct dbc_port		*port = tty->driver_data;
	unsigned long		flags;

	spin_lock_irqsave(&port->port_lock, flags);
	tasklet_schedule(&port->push);
	spin_unlock_irqrestore(&port->port_lock, flags);
}

static const struct tty_operations dbc_tty_ops = {
	.install		= dbc_tty_install,
	.open			= dbc_tty_open,
	.close			= dbc_tty_close,
	.write			= dbc_tty_write,
	.put_char		= dbc_tty_put_char,
	.flush_chars		= dbc_tty_flush_chars,
	.write_room		= dbc_tty_write_room,
	.chars_in_buffer	= dbc_tty_chars_in_buffer,
	.unthrottle		= dbc_tty_unthrottle,
};

static struct tty_driver *dbc_tty_driver;

int xhci_dbc_tty_register_driver(struct xhci_hcd *xhci)
{
	int			status;
	struct xhci_dbc		*dbc = xhci->dbc;

	dbc_tty_driver = tty_alloc_driver(1, TTY_DRIVER_REAL_RAW |
					  TTY_DRIVER_DYNAMIC_DEV);
	if (IS_ERR(dbc_tty_driver)) {
		status = PTR_ERR(dbc_tty_driver);
		dbc_tty_driver = NULL;
		return status;
	}

	dbc_tty_driver->driver_name = "dbc_serial";
	dbc_tty_driver->name = "ttyDBC";

	dbc_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	dbc_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	dbc_tty_driver->init_termios = tty_std_termios;
	dbc_tty_driver->init_termios.c_cflag =
			B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	dbc_tty_driver->init_termios.c_ispeed = 9600;
	dbc_tty_driver->init_termios.c_ospeed = 9600;
	dbc_tty_driver->driver_state = &dbc->port;

	tty_set_operations(dbc_tty_driver, &dbc_tty_ops);

	status = tty_register_driver(dbc_tty_driver);
	if (status) {
		xhci_err(xhci,
			 "can't register dbc tty driver, err %d\n", status);
		put_tty_driver(dbc_tty_driver);
		dbc_tty_driver = NULL;
	}

	return status;
}

void xhci_dbc_tty_unregister_driver(void)
{
	if (dbc_tty_driver) {
		tty_unregister_driver(dbc_tty_driver);
		put_tty_driver(dbc_tty_driver);
		dbc_tty_driver = NULL;
	}
}

static void dbc_rx_push(unsigned long _port)
{
	struct dbc_request	*req;
	struct tty_struct	*tty;
	unsigned long		flags;
	bool			do_push = false;
	bool			disconnect = false;
	struct dbc_port		*port = (void *)_port;
	struct list_head	*queue = &port->read_queue;

	spin_lock_irqsave(&port->port_lock, flags);
	tty = port->port.tty;
	while (!list_empty(queue)) {
		req = list_first_entry(queue, struct dbc_request, list_pool);

		if (tty && tty_throttled(tty))
			break;

		switch (req->status) {
		case 0:
			break;
		case -ESHUTDOWN:
			disconnect = true;
			break;
		default:
			pr_warn("ttyDBC0: unexpected RX status %d\n",
				req->status);
			break;
		}

		if (req->actual) {
			char		*packet = req->buf;
			unsigned int	n, size = req->actual;
			int		count;

			n = port->n_read;
			if (n) {
				packet += n;
				size -= n;
			}

			count = tty_insert_flip_string(&port->port, packet,
						       size);
			if (count)
				do_push = true;
			if (count != size) {
				port->n_read += count;
				break;
			}
			port->n_read = 0;
		}

		list_move(&req->list_pool, &port->read_pool);
	}

	if (do_push)
		tty_flip_buffer_push(&port->port);

	if (!list_empty(queue) && tty) {
		if (!tty_throttled(tty)) {
			if (do_push)
				tasklet_schedule(&port->push);
			else
				pr_warn("ttyDBC0: RX not scheduled?\n");
		}
	}

	if (!disconnect)
		dbc_start_rx(port);

	spin_unlock_irqrestore(&port->port_lock, flags);
}

static int dbc_port_activate(struct tty_port *_port, struct tty_struct *tty)
{
	unsigned long	flags;
	struct dbc_port	*port = container_of(_port, struct dbc_port, port);

	spin_lock_irqsave(&port->port_lock, flags);
	dbc_start_rx(port);
	spin_unlock_irqrestore(&port->port_lock, flags);

	return 0;
}

static const struct tty_port_operations dbc_port_ops = {
	.activate =	dbc_port_activate,
};

static void
xhci_dbc_tty_init_port(struct xhci_hcd *xhci, struct dbc_port *port)
{
	tty_port_init(&port->port);
	spin_lock_init(&port->port_lock);
	tasklet_init(&port->push, dbc_rx_push, (unsigned long)port);
	INIT_LIST_HEAD(&port->read_pool);
	INIT_LIST_HEAD(&port->read_queue);
	INIT_LIST_HEAD(&port->write_pool);

	port->in =		get_in_ep(xhci);
	port->out =		get_out_ep(xhci);
	port->port.ops =	&dbc_port_ops;
	port->n_read =		0;
}

static void
xhci_dbc_tty_exit_port(struct dbc_port *port)
{
	tasklet_kill(&port->push);
	tty_port_destroy(&port->port);
}

int xhci_dbc_tty_register_device(struct xhci_hcd *xhci)
{
	int			ret;
	struct device		*tty_dev;
	struct xhci_dbc		*dbc = xhci->dbc;
	struct dbc_port		*port = &dbc->port;

	xhci_dbc_tty_init_port(xhci, port);
	tty_dev = tty_port_register_device(&port->port,
					   dbc_tty_driver, 0, NULL);
	if (IS_ERR(tty_dev)) {
		ret = PTR_ERR(tty_dev);
		goto register_fail;
	}

	ret = kfifo_alloc(&port->write_fifo, DBC_WRITE_BUF_SIZE, GFP_KERNEL);
	if (ret)
		goto buf_alloc_fail;

	ret = xhci_dbc_alloc_requests(port->in, &port->read_pool,
				      dbc_read_complete);
	if (ret)
		goto request_fail;

	ret = xhci_dbc_alloc_requests(port->out, &port->write_pool,
				      dbc_write_complete);
	if (ret)
		goto request_fail;

	port->registered = true;

	return 0;

request_fail:
	xhci_dbc_free_requests(port->in, &port->read_pool);
	xhci_dbc_free_requests(port->out, &port->write_pool);
	kfifo_free(&port->write_fifo);

buf_alloc_fail:
	tty_unregister_device(dbc_tty_driver, 0);

register_fail:
	xhci_dbc_tty_exit_port(port);

	xhci_err(xhci, "can't register tty port, err %d\n", ret);

	return ret;
}

void xhci_dbc_tty_unregister_device(struct xhci_hcd *xhci)
{
	struct xhci_dbc		*dbc = xhci->dbc;
	struct dbc_port		*port = &dbc->port;

	tty_unregister_device(dbc_tty_driver, 0);
	xhci_dbc_tty_exit_port(port);
	port->registered = false;

	kfifo_free(&port->write_fifo);
	xhci_dbc_free_requests(get_out_ep(xhci), &port->read_pool);
	xhci_dbc_free_requests(get_out_ep(xhci), &port->read_queue);
	xhci_dbc_free_requests(get_in_ep(xhci), &port->write_pool);
}
