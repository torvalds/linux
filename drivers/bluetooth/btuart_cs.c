/*
 *
 *  Driver for Bluetooth PCMCIA cards with HCI UART interface
 *
 *  Copyright (C) 2001-2002  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation;
 *
 *  Software distributed under the License is distributed on an "AS
 *  IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 *  implied. See the License for the specific language governing
 *  rights and limitations under the License.
 *
 *  The initial developer of the original code is David A. Hinds
 *  <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 *  are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>
#include <linux/moduleparam.h>

#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>
#include <linux/bitops.h>
#include <asm/system.h>
#include <asm/io.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>



/* ======================== Module parameters ======================== */


MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth driver for Bluetooth PCMCIA cards with HCI UART interface");
MODULE_LICENSE("GPL");



/* ======================== Local structures ======================== */


typedef struct btuart_info_t {
	dev_link_t link;
	dev_node_t node;

	struct hci_dev *hdev;

	spinlock_t lock;	/* For serializing operations */

	struct sk_buff_head txq;
	unsigned long tx_state;

	unsigned long rx_state;
	unsigned long rx_count;
	struct sk_buff *rx_skb;
} btuart_info_t;


static void btuart_config(dev_link_t *link);
static void btuart_release(dev_link_t *link);

static void btuart_detach(struct pcmcia_device *p_dev);


/* Maximum baud rate */
#define SPEED_MAX  115200

/* Default baud rate: 57600, 115200, 230400 or 460800 */
#define DEFAULT_BAUD_RATE  115200


/* Transmit states  */
#define XMIT_SENDING	1
#define XMIT_WAKEUP	2
#define XMIT_WAITING	8

/* Receiver states */
#define RECV_WAIT_PACKET_TYPE	0
#define RECV_WAIT_EVENT_HEADER	1
#define RECV_WAIT_ACL_HEADER	2
#define RECV_WAIT_SCO_HEADER	3
#define RECV_WAIT_DATA		4



/* ======================== Interrupt handling ======================== */


static int btuart_write(unsigned int iobase, int fifo_size, __u8 *buf, int len)
{
	int actual = 0;

	/* Tx FIFO should be empty */
	if (!(inb(iobase + UART_LSR) & UART_LSR_THRE))
		return 0;

	/* Fill FIFO with current frame */
	while ((fifo_size-- > 0) && (actual < len)) {
		/* Transmit next byte */
		outb(buf[actual], iobase + UART_TX);
		actual++;
	}

	return actual;
}


static void btuart_write_wakeup(btuart_info_t *info)
{
	if (!info) {
		BT_ERR("Unknown device");
		return;
	}

	if (test_and_set_bit(XMIT_SENDING, &(info->tx_state))) {
		set_bit(XMIT_WAKEUP, &(info->tx_state));
		return;
	}

	do {
		register unsigned int iobase = info->link.io.BasePort1;
		register struct sk_buff *skb;
		register int len;

		clear_bit(XMIT_WAKEUP, &(info->tx_state));

		if (!(info->link.state & DEV_PRESENT))
			return;

		if (!(skb = skb_dequeue(&(info->txq))))
			break;

		/* Send frame */
		len = btuart_write(iobase, 16, skb->data, skb->len);
		set_bit(XMIT_WAKEUP, &(info->tx_state));

		if (len == skb->len) {
			kfree_skb(skb);
		} else {
			skb_pull(skb, len);
			skb_queue_head(&(info->txq), skb);
		}

		info->hdev->stat.byte_tx += len;

	} while (test_bit(XMIT_WAKEUP, &(info->tx_state)));

	clear_bit(XMIT_SENDING, &(info->tx_state));
}


static void btuart_receive(btuart_info_t *info)
{
	unsigned int iobase;
	int boguscount = 0;

	if (!info) {
		BT_ERR("Unknown device");
		return;
	}

	iobase = info->link.io.BasePort1;

	do {
		info->hdev->stat.byte_rx++;

		/* Allocate packet */
		if (info->rx_skb == NULL) {
			info->rx_state = RECV_WAIT_PACKET_TYPE;
			info->rx_count = 0;
			if (!(info->rx_skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE, GFP_ATOMIC))) {
				BT_ERR("Can't allocate mem for new packet");
				return;
			}
		}

		if (info->rx_state == RECV_WAIT_PACKET_TYPE) {

			info->rx_skb->dev = (void *) info->hdev;
			bt_cb(info->rx_skb)->pkt_type = inb(iobase + UART_RX);

			switch (bt_cb(info->rx_skb)->pkt_type) {

			case HCI_EVENT_PKT:
				info->rx_state = RECV_WAIT_EVENT_HEADER;
				info->rx_count = HCI_EVENT_HDR_SIZE;
				break;

			case HCI_ACLDATA_PKT:
				info->rx_state = RECV_WAIT_ACL_HEADER;
				info->rx_count = HCI_ACL_HDR_SIZE;
				break;

			case HCI_SCODATA_PKT:
				info->rx_state = RECV_WAIT_SCO_HEADER;
				info->rx_count = HCI_SCO_HDR_SIZE;
				break;

			default:
				/* Unknown packet */
				BT_ERR("Unknown HCI packet with type 0x%02x received", bt_cb(info->rx_skb)->pkt_type);
				info->hdev->stat.err_rx++;
				clear_bit(HCI_RUNNING, &(info->hdev->flags));

				kfree_skb(info->rx_skb);
				info->rx_skb = NULL;
				break;

			}

		} else {

			*skb_put(info->rx_skb, 1) = inb(iobase + UART_RX);
			info->rx_count--;

			if (info->rx_count == 0) {

				int dlen;
				struct hci_event_hdr *eh;
				struct hci_acl_hdr *ah;
				struct hci_sco_hdr *sh;


				switch (info->rx_state) {

				case RECV_WAIT_EVENT_HEADER:
					eh = (struct hci_event_hdr *)(info->rx_skb->data);
					info->rx_state = RECV_WAIT_DATA;
					info->rx_count = eh->plen;
					break;

				case RECV_WAIT_ACL_HEADER:
					ah = (struct hci_acl_hdr *)(info->rx_skb->data);
					dlen = __le16_to_cpu(ah->dlen);
					info->rx_state = RECV_WAIT_DATA;
					info->rx_count = dlen;
					break;

				case RECV_WAIT_SCO_HEADER:
					sh = (struct hci_sco_hdr *)(info->rx_skb->data);
					info->rx_state = RECV_WAIT_DATA;
					info->rx_count = sh->dlen;
					break;

				case RECV_WAIT_DATA:
					hci_recv_frame(info->rx_skb);
					info->rx_skb = NULL;
					break;

				}

			}

		}

		/* Make sure we don't stay here too long */
		if (boguscount++ > 16)
			break;

	} while (inb(iobase + UART_LSR) & UART_LSR_DR);
}


static irqreturn_t btuart_interrupt(int irq, void *dev_inst, struct pt_regs *regs)
{
	btuart_info_t *info = dev_inst;
	unsigned int iobase;
	int boguscount = 0;
	int iir, lsr;

	if (!info || !info->hdev) {
		BT_ERR("Call of irq %d for unknown device", irq);
		return IRQ_NONE;
	}

	iobase = info->link.io.BasePort1;

	spin_lock(&(info->lock));

	iir = inb(iobase + UART_IIR) & UART_IIR_ID;
	while (iir) {

		/* Clear interrupt */
		lsr = inb(iobase + UART_LSR);

		switch (iir) {
		case UART_IIR_RLSI:
			BT_ERR("RLSI");
			break;
		case UART_IIR_RDI:
			/* Receive interrupt */
			btuart_receive(info);
			break;
		case UART_IIR_THRI:
			if (lsr & UART_LSR_THRE) {
				/* Transmitter ready for data */
				btuart_write_wakeup(info);
			}
			break;
		default:
			BT_ERR("Unhandled IIR=%#x", iir);
			break;
		}

		/* Make sure we don't stay here too long */
		if (boguscount++ > 100)
			break;

		iir = inb(iobase + UART_IIR) & UART_IIR_ID;

	}

	spin_unlock(&(info->lock));

	return IRQ_HANDLED;
}


static void btuart_change_speed(btuart_info_t *info, unsigned int speed)
{
	unsigned long flags;
	unsigned int iobase;
	int fcr;		/* FIFO control reg */
	int lcr;		/* Line control reg */
	int divisor;

	if (!info) {
		BT_ERR("Unknown device");
		return;
	}

	iobase = info->link.io.BasePort1;

	spin_lock_irqsave(&(info->lock), flags);

	/* Turn off interrupts */
	outb(0, iobase + UART_IER);

	divisor = SPEED_MAX / speed;

	fcr = UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT;

	/* 
	 * Use trigger level 1 to avoid 3 ms. timeout delay at 9600 bps, and
	 * almost 1,7 ms at 19200 bps. At speeds above that we can just forget
	 * about this timeout since it will always be fast enough. 
	 */

	if (speed < 38400)
		fcr |= UART_FCR_TRIGGER_1;
	else
		fcr |= UART_FCR_TRIGGER_14;

	/* Bluetooth cards use 8N1 */
	lcr = UART_LCR_WLEN8;

	outb(UART_LCR_DLAB | lcr, iobase + UART_LCR);	/* Set DLAB */
	outb(divisor & 0xff, iobase + UART_DLL);	/* Set speed */
	outb(divisor >> 8, iobase + UART_DLM);
	outb(lcr, iobase + UART_LCR);	/* Set 8N1  */
	outb(fcr, iobase + UART_FCR);	/* Enable FIFO's */

	/* Turn on interrups */
	outb(UART_IER_RLSI | UART_IER_RDI | UART_IER_THRI, iobase + UART_IER);

	spin_unlock_irqrestore(&(info->lock), flags);
}



/* ======================== HCI interface ======================== */


static int btuart_hci_flush(struct hci_dev *hdev)
{
	btuart_info_t *info = (btuart_info_t *)(hdev->driver_data);

	/* Drop TX queue */
	skb_queue_purge(&(info->txq));

	return 0;
}


static int btuart_hci_open(struct hci_dev *hdev)
{
	set_bit(HCI_RUNNING, &(hdev->flags));

	return 0;
}


static int btuart_hci_close(struct hci_dev *hdev)
{
	if (!test_and_clear_bit(HCI_RUNNING, &(hdev->flags)))
		return 0;

	btuart_hci_flush(hdev);

	return 0;
}


static int btuart_hci_send_frame(struct sk_buff *skb)
{
	btuart_info_t *info;
	struct hci_dev *hdev = (struct hci_dev *)(skb->dev);

	if (!hdev) {
		BT_ERR("Frame for unknown HCI device (hdev=NULL)");
		return -ENODEV;
	}

	info = (btuart_info_t *)(hdev->driver_data);

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;
	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;
	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		break;
	};

	/* Prepend skb with frame type */
	memcpy(skb_push(skb, 1), &bt_cb(skb)->pkt_type, 1);
	skb_queue_tail(&(info->txq), skb);

	btuart_write_wakeup(info);

	return 0;
}


static void btuart_hci_destruct(struct hci_dev *hdev)
{
}


static int btuart_hci_ioctl(struct hci_dev *hdev, unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}



/* ======================== Card services HCI interaction ======================== */


static int btuart_open(btuart_info_t *info)
{
	unsigned long flags;
	unsigned int iobase = info->link.io.BasePort1;
	struct hci_dev *hdev;

	spin_lock_init(&(info->lock));

	skb_queue_head_init(&(info->txq));

	info->rx_state = RECV_WAIT_PACKET_TYPE;
	info->rx_count = 0;
	info->rx_skb = NULL;

	/* Initialize HCI device */
	hdev = hci_alloc_dev();
	if (!hdev) {
		BT_ERR("Can't allocate HCI device");
		return -ENOMEM;
	}

	info->hdev = hdev;

	hdev->type = HCI_PCCARD;
	hdev->driver_data = info;

	hdev->open     = btuart_hci_open;
	hdev->close    = btuart_hci_close;
	hdev->flush    = btuart_hci_flush;
	hdev->send     = btuart_hci_send_frame;
	hdev->destruct = btuart_hci_destruct;
	hdev->ioctl    = btuart_hci_ioctl;

	hdev->owner = THIS_MODULE;

	spin_lock_irqsave(&(info->lock), flags);

	/* Reset UART */
	outb(0, iobase + UART_MCR);

	/* Turn off interrupts */
	outb(0, iobase + UART_IER);

	/* Initialize UART */
	outb(UART_LCR_WLEN8, iobase + UART_LCR);	/* Reset DLAB */
	outb((UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2), iobase + UART_MCR);

	/* Turn on interrupts */
	// outb(UART_IER_RLSI | UART_IER_RDI | UART_IER_THRI, iobase + UART_IER);

	spin_unlock_irqrestore(&(info->lock), flags);

	btuart_change_speed(info, DEFAULT_BAUD_RATE);

	/* Timeout before it is safe to send the first HCI packet */
	msleep(1000);

	/* Register HCI device */
	if (hci_register_dev(hdev) < 0) {
		BT_ERR("Can't register HCI device");
		info->hdev = NULL;
		hci_free_dev(hdev);
		return -ENODEV;
	}

	return 0;
}


static int btuart_close(btuart_info_t *info)
{
	unsigned long flags;
	unsigned int iobase = info->link.io.BasePort1;
	struct hci_dev *hdev = info->hdev;

	if (!hdev)
		return -ENODEV;

	btuart_hci_close(hdev);

	spin_lock_irqsave(&(info->lock), flags);

	/* Reset UART */
	outb(0, iobase + UART_MCR);

	/* Turn off interrupts */
	outb(0, iobase + UART_IER);

	spin_unlock_irqrestore(&(info->lock), flags);

	if (hci_unregister_dev(hdev) < 0)
		BT_ERR("Can't unregister HCI device %s", hdev->name);

	hci_free_dev(hdev);

	return 0;
}

static int btuart_attach(struct pcmcia_device *p_dev)
{
	btuart_info_t *info;
	dev_link_t *link;

	/* Create new info device */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	link = &info->link;
	link->priv = info;

	link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
	link->io.NumPorts1 = 8;
	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
	link->irq.IRQInfo1 = IRQ_LEVEL_ID;

	link->irq.Handler = btuart_interrupt;
	link->irq.Instance = info;

	link->conf.Attributes = CONF_ENABLE_IRQ;
	link->conf.Vcc = 50;
	link->conf.IntType = INT_MEMORY_AND_IO;

	link->handle = p_dev;
	p_dev->instance = link;

	link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
	btuart_config(link);

	return 0;
}


static void btuart_detach(struct pcmcia_device *p_dev)
{
	dev_link_t *link = dev_to_instance(p_dev);
	btuart_info_t *info = link->priv;

	if (link->state & DEV_CONFIG)
		btuart_release(link);

	kfree(info);
}

static int get_tuple(client_handle_t handle, tuple_t *tuple, cisparse_t *parse)
{
	int i;

	i = pcmcia_get_tuple_data(handle, tuple);
	if (i != CS_SUCCESS)
		return i;

	return pcmcia_parse_tuple(handle, tuple, parse);
}

static int first_tuple(client_handle_t handle, tuple_t *tuple, cisparse_t *parse)
{
	if (pcmcia_get_first_tuple(handle, tuple) != CS_SUCCESS)
		return CS_NO_MORE_ITEMS;
	return get_tuple(handle, tuple, parse);
}

static int next_tuple(client_handle_t handle, tuple_t *tuple, cisparse_t *parse)
{
	if (pcmcia_get_next_tuple(handle, tuple) != CS_SUCCESS)
		return CS_NO_MORE_ITEMS;
	return get_tuple(handle, tuple, parse);
}

static void btuart_config(dev_link_t *link)
{
	static kio_addr_t base[5] = { 0x3f8, 0x2f8, 0x3e8, 0x2e8, 0x0 };
	client_handle_t handle = link->handle;
	btuart_info_t *info = link->priv;
	tuple_t tuple;
	u_short buf[256];
	cisparse_t parse;
	cistpl_cftable_entry_t *cf = &parse.cftable_entry;
	config_info_t config;
	int i, j, try, last_ret, last_fn;

	tuple.TupleData = (cisdata_t *)buf;
	tuple.TupleOffset = 0;
	tuple.TupleDataMax = 255;
	tuple.Attributes = 0;

	/* Get configuration register information */
	tuple.DesiredTuple = CISTPL_CONFIG;
	last_ret = first_tuple(handle, &tuple, &parse);
	if (last_ret != CS_SUCCESS) {
		last_fn = ParseTuple;
		goto cs_failed;
	}
	link->conf.ConfigBase = parse.config.base;
	link->conf.Present = parse.config.rmask[0];

	/* Configure card */
	link->state |= DEV_CONFIG;
	i = pcmcia_get_configuration_info(handle, &config);
	link->conf.Vcc = config.Vcc;

	/* First pass: look for a config entry that looks normal. */
	tuple.TupleData = (cisdata_t *) buf;
	tuple.TupleOffset = 0;
	tuple.TupleDataMax = 255;
	tuple.Attributes = 0;
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	/* Two tries: without IO aliases, then with aliases */
	for (try = 0; try < 2; try++) {
		i = first_tuple(handle, &tuple, &parse);
		while (i != CS_NO_MORE_ITEMS) {
			if (i != CS_SUCCESS)
				goto next_entry;
			if (cf->vpp1.present & (1 << CISTPL_POWER_VNOM))
				link->conf.Vpp1 = link->conf.Vpp2 = cf->vpp1.param[CISTPL_POWER_VNOM] / 10000;
			if ((cf->io.nwin > 0) && (cf->io.win[0].len == 8) && (cf->io.win[0].base != 0)) {
				link->conf.ConfigIndex = cf->index;
				link->io.BasePort1 = cf->io.win[0].base;
				link->io.IOAddrLines = (try == 0) ? 16 : cf->io.flags & CISTPL_IO_LINES_MASK;
				i = pcmcia_request_io(link->handle, &link->io);
				if (i == CS_SUCCESS)
					goto found_port;
			}
next_entry:
			i = next_tuple(handle, &tuple, &parse);
		}
	}

	/* Second pass: try to find an entry that isn't picky about
	   its base address, then try to grab any standard serial port
	   address, and finally try to get any free port. */
	i = first_tuple(handle, &tuple, &parse);
	while (i != CS_NO_MORE_ITEMS) {
		if ((i == CS_SUCCESS) && (cf->io.nwin > 0)
		    && ((cf->io.flags & CISTPL_IO_LINES_MASK) <= 3)) {
			link->conf.ConfigIndex = cf->index;
			for (j = 0; j < 5; j++) {
				link->io.BasePort1 = base[j];
				link->io.IOAddrLines = base[j] ? 16 : 3;
				i = pcmcia_request_io(link->handle, &link->io);
				if (i == CS_SUCCESS)
					goto found_port;
			}
		}
		i = next_tuple(handle, &tuple, &parse);
	}

found_port:
	if (i != CS_SUCCESS) {
		BT_ERR("No usable port range found");
		cs_error(link->handle, RequestIO, i);
		goto failed;
	}

	i = pcmcia_request_irq(link->handle, &link->irq);
	if (i != CS_SUCCESS) {
		cs_error(link->handle, RequestIRQ, i);
		link->irq.AssignedIRQ = 0;
	}

	i = pcmcia_request_configuration(link->handle, &link->conf);
	if (i != CS_SUCCESS) {
		cs_error(link->handle, RequestConfiguration, i);
		goto failed;
	}

	if (btuart_open(info) != 0)
		goto failed;

	strcpy(info->node.dev_name, info->hdev->name);
	link->dev = &info->node;
	link->state &= ~DEV_CONFIG_PENDING;

	return;

cs_failed:
	cs_error(link->handle, last_fn, last_ret);

failed:
	btuart_release(link);
}


static void btuart_release(dev_link_t *link)
{
	btuart_info_t *info = link->priv;

	if (link->state & DEV_PRESENT)
		btuart_close(info);

	pcmcia_disable_device(link->handle);
}

static int btuart_suspend(struct pcmcia_device *dev)
{
	dev_link_t *link = dev_to_instance(dev);

	link->state |= DEV_SUSPEND;
	if (link->state & DEV_CONFIG)
		pcmcia_release_configuration(link->handle);

	return 0;
}

static int btuart_resume(struct pcmcia_device *dev)
{
	dev_link_t *link = dev_to_instance(dev);

	link->state &= ~DEV_SUSPEND;
	if (DEV_OK(link))
		pcmcia_request_configuration(link->handle, &link->conf);

	return 0;
}


static struct pcmcia_device_id btuart_ids[] = {
	/* don't use this driver. Use serial_cs + hci_uart instead */
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, btuart_ids);

static struct pcmcia_driver btuart_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "btuart_cs",
	},
	.probe		= btuart_attach,
	.remove		= btuart_detach,
	.id_table	= btuart_ids,
	.suspend	= btuart_suspend,
	.resume		= btuart_resume,
};

static int __init init_btuart_cs(void)
{
	return pcmcia_register_driver(&btuart_driver);
}


static void __exit exit_btuart_cs(void)
{
	pcmcia_unregister_driver(&btuart_driver);
}

module_init(init_btuart_cs);
module_exit(exit_btuart_cs);
