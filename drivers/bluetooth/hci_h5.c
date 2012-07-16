/*
 *
 *  Bluetooth HCI Three-wire UART driver
 *
 *  Copyright (C) 2012  Intel Corporation
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/skbuff.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "hci_uart.h"

static int h5_open(struct hci_uart *hu)
{
	return -ENOSYS;
}

static int h5_close(struct hci_uart *hu)
{
	return -ENOSYS;
}

static int h5_recv(struct hci_uart *hu, void *data, int count)
{
	return -ENOSYS;
}

static int h5_enqueue(struct hci_uart *hu, struct sk_buff *skb)
{
	return -ENOSYS;
}

static struct sk_buff *h5_dequeue(struct hci_uart *hu)
{
	return NULL;
}

static int h5_flush(struct hci_uart *hu)
{
	return -ENOSYS;
}

static struct hci_uart_proto h5p = {
	.id		= HCI_UART_3WIRE,
	.open		= h5_open,
	.close		= h5_close,
	.recv		= h5_recv,
	.enqueue	= h5_enqueue,
	.dequeue	= h5_dequeue,
	.flush		= h5_flush,
};

int __init h5_init(void)
{
	int err = hci_uart_register_proto(&h5p);

	if (!err)
		BT_INFO("HCI Three-wire UART (H5) protocol initialized");
	else
		BT_ERR("HCI Three-wire UART (H5) protocol init failed");

	return err;
}

int __exit h5_deinit(void)
{
	return hci_uart_unregister_proto(&h5p);
}
