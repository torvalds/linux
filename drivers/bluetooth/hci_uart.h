/* 
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

/*
 * $Id: hci_uart.h,v 1.2 2002/09/09 01:17:32 maxk Exp $
 */

#ifndef N_HCI 
#define N_HCI	15
#endif

/* Ioctls */
#define HCIUARTSETPROTO	_IOW('U', 200, int)
#define HCIUARTGETPROTO	_IOR('U', 201, int)

/* UART protocols */
#define HCI_UART_MAX_PROTO	4

#define HCI_UART_H4	0
#define HCI_UART_BCSP	1
#define HCI_UART_3WIRE	2
#define HCI_UART_H4DS	3

#ifdef __KERNEL__
struct hci_uart;

struct hci_uart_proto {
	unsigned int id;
	int (*open)(struct hci_uart *hu);
	int (*close)(struct hci_uart *hu);
	int (*flush)(struct hci_uart *hu);
	int (*recv)(struct hci_uart *hu, void *data, int len);
	int (*enqueue)(struct hci_uart *hu, struct sk_buff *skb);
	struct sk_buff *(*dequeue)(struct hci_uart *hu);
};

struct hci_uart {
	struct tty_struct  *tty;
	struct hci_dev     *hdev;
	unsigned long      flags;

	struct hci_uart_proto *proto;
	void               *priv;
	
	struct sk_buff     *tx_skb;
	unsigned long      tx_state;
	spinlock_t         rx_lock;
};

/* HCI_UART flag bits */
#define HCI_UART_PROTO_SET		0

/* TX states  */
#define HCI_UART_SENDING		1
#define HCI_UART_TX_WAKEUP		2

int hci_uart_register_proto(struct hci_uart_proto *p);
int hci_uart_unregister_proto(struct hci_uart_proto *p);
int hci_uart_tx_wakeup(struct hci_uart *hu);

#endif /* __KERNEL__ */
