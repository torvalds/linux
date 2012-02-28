/*
 *
 *  A driver for Nokia Connectivity Card DTL-1 devices
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

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
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

#include <pcmcia/cistpl.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>



/* ======================== Module parameters ======================== */


MODULE_AUTHOR("Marcel Holtmann <marcel@holtmann.org>");
MODULE_DESCRIPTION("Bluetooth driver for Nokia Connectivity Card DTL-1");
MODULE_LICENSE("GPL");



/* ======================== Local structures ======================== */


typedef struct dtl1_info_t {
	struct pcmcia_device *p_dev;

	struct hci_dev *hdev;

	spinlock_t lock;		/* For serializing operations */

	unsigned long flowmask;		/* HCI flow mask */
	int ri_latch;

	struct sk_buff_head txq;
	unsigned long tx_state;

	unsigned long rx_state;
	unsigned long rx_count;
	struct sk_buff *rx_skb;
} dtl1_info_t;


static int dtl1_config(struct pcmcia_device *link);
static void dtl1_release(struct pcmcia_device *link);

static void dtl1_detach(struct pcmcia_device *p_dev);


/* Transmit states  */
#define XMIT_SENDING  1
#define XMIT_WAKEUP   2
#define XMIT_WAITING  8

/* Receiver States */
#define RECV_WAIT_NSH   0
#define RECV_WAIT_DATA  1


typedef struct {
	u8 type;
	u8 zero;
	u16 len;
} __packed nsh_t;	/* Nokia Specific Header */

#define NSHL  4				/* Nokia Specific Header Length */



/* ======================== Interrupt handling ======================== */


static int dtl1_write(unsigned int iobase, int fifo_size, __u8 *buf, int len)
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


static void dtl1_write_wakeup(dtl1_info_t *info)
{
	if (!info) {
		BT_ERR("Unknown device");
		return;
	}

	if (test_bit(XMIT_WAITING, &(info->tx_state))) {
		set_bit(XMIT_WAKEUP, &(info->tx_state));
		return;
	}

	if (test_and_set_bit(XMIT_SENDING, &(info->tx_state))) {
		set_bit(XMIT_WAKEUP, &(info->tx_state));
		return;
	}

	do {
		register unsigned int iobase = info->p_dev->resource[0]->start;
		register struct sk_buff *skb;
		register int len;

		clear_bit(XMIT_WAKEUP, &(info->tx_state));

		if (!pcmcia_dev_present(info->p_dev))
			return;

		if (!(skb = skb_dequeue(&(info->txq))))
			break;

		/* Send frame */
		len = dtl1_write(iobase, 32, skb->data, skb->len);

		if (len == skb->len) {
			set_bit(XMIT_WAITING, &(info->tx_state));
			kfree_skb(skb);
		} else {
			skb_pull(skb, len);
			skb_queue_head(&(info->txq), skb);
		}

		info->hdev->stat.byte_tx += len;

	} while (test_bit(XMIT_WAKEUP, &(info->tx_state)));

	clear_bit(XMIT_SENDING, &(info->tx_state));
}


static void dtl1_control(dtl1_info_t *info, struct sk_buff *skb)
{
	u8 flowmask = *(u8 *)skb->data;
	int i;

	printk(KERN_INFO "Bluetooth: Nokia control data =");
	for (i = 0; i < skb->len; i++) {
		printk(" %02x", skb->data[i]);
	}
	printk("\n");

	/* transition to active state */
	if (((info->flowmask & 0x07) == 0) && ((flowmask & 0x07) != 0)) {
		clear_bit(XMIT_WAITING, &(info->tx_state));
		dtl1_write_wakeup(info);
	}

	info->flowmask = flowmask;

	kfree_skb(skb);
}


static void dtl1_receive(dtl1_info_t *info)
{
	unsigned int iobase;
	nsh_t *nsh;
	int boguscount = 0;

	if (!info) {
		BT_ERR("Unknown device");
		return;
	}

	iobase = info->p_dev->resource[0]->start;

	do {
		info->hdev->stat.byte_rx++;

		/* Allocate packet */
		if (info->rx_skb == NULL)
			if (!(info->rx_skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE, GFP_ATOMIC))) {
				BT_ERR("Can't allocate mem for new packet");
				info->rx_state = RECV_WAIT_NSH;
				info->rx_count = NSHL;
				return;
			}

		*skb_put(info->rx_skb, 1) = inb(iobase + UART_RX);
		nsh = (nsh_t *)info->rx_skb->data;

		info->rx_count--;

		if (info->rx_count == 0) {

			switch (info->rx_state) {
			case RECV_WAIT_NSH:
				info->rx_state = RECV_WAIT_DATA;
				info->rx_count = nsh->len + (nsh->len & 0x0001);
				break;
			case RECV_WAIT_DATA:
				bt_cb(info->rx_skb)->pkt_type = nsh->type;

				/* remove PAD byte if it exists */
				if (nsh->len & 0x0001) {
					info->rx_skb->tail--;
					info->rx_skb->len--;
				}

				/* remove NSH */
				skb_pull(info->rx_skb, NSHL);

				switch (bt_cb(info->rx_skb)->pkt_type) {
				case 0x80:
					/* control data for the Nokia Card */
					dtl1_control(info, info->rx_skb);
					break;
				case 0x82:
				case 0x83:
				case 0x84:
					/* send frame to the HCI layer */
					info->rx_skb->dev = (void *) info->hdev;
					bt_cb(info->rx_skb)->pkt_type &= 0x0f;
					hci_recv_frame(info->rx_skb);
					break;
				default:
					/* unknown packet */
					BT_ERR("Unknown HCI packet with type 0x%02x received", bt_cb(info->rx_skb)->pkt_type);
					kfree_skb(info->rx_skb);
					break;
				}

				info->rx_state = RECV_WAIT_NSH;
				info->rx_count = NSHL;
				info->rx_skb = NULL;
				break;
			}

		}

		/* Make sure we don't stay here too long */
		if (boguscount++ > 32)
			break;

	} while (inb(iobase + UART_LSR) & UART_LSR_DR);
}


static irqreturn_t dtl1_interrupt(int irq, void *dev_inst)
{
	dtl1_info_t *info = dev_inst;
	unsigned int iobase;
	unsigned char msr;
	int boguscount = 0;
	int iir, lsr;
	irqreturn_t r = IRQ_NONE;

	if (!info || !info->hdev)
		/* our irq handler is shared */
		return IRQ_NONE;

	iobase = info->p_dev->resource[0]->start;

	spin_lock(&(info->lock));

	iir = inb(iobase + UART_IIR) & UART_IIR_ID;
	while (iir) {

		r = IRQ_HANDLED;
		/* Clear interrupt */
		lsr = inb(iobase + UART_LSR);

		switch (iir) {
		case UART_IIR_RLSI:
			BT_ERR("RLSI");
			break;
		case UART_IIR_RDI:
			/* Receive interrupt */
			dtl1_receive(info);
			break;
		case UART_IIR_THRI:
			if (lsr & UART_LSR_THRE) {
				/* Transmitter ready for data */
				dtl1_write_wakeup(info);
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

	msr = inb(iobase + UART_MSR);

	if (info->ri_latch ^ (msr & UART_MSR_RI)) {
		info->ri_latch = msr & UART_MSR_RI;
		clear_bit(XMIT_WAITING, &(info->tx_state));
		dtl1_write_wakeup(info);
		r = IRQ_HANDLED;
	}

	spin_unlock(&(info->lock));

	return r;
}



/* ======================== HCI interface ======================== */


static int dtl1_hci_open(struct hci_dev *hdev)
{
	set_bit(HCI_RUNNING, &(hdev->flags));

	return 0;
}


static int dtl1_hci_flush(struct hci_dev *hdev)
{
	dtl1_info_t *info = (dtl1_info_t *)(hdev->driver_data);

	/* Drop TX queue */
	skb_queue_purge(&(info->txq));

	return 0;
}


static int dtl1_hci_close(struct hci_dev *hdev)
{
	if (!test_and_clear_bit(HCI_RUNNING, &(hdev->flags)))
		return 0;

	dtl1_hci_flush(hdev);

	return 0;
}


static int dtl1_hci_send_frame(struct sk_buff *skb)
{
	dtl1_info_t *info;
	struct hci_dev *hdev = (struct hci_dev *)(skb->dev);
	struct sk_buff *s;
	nsh_t nsh;

	if (!hdev) {
		BT_ERR("Frame for unknown HCI device (hdev=NULL)");
		return -ENODEV;
	}

	info = (dtl1_info_t *)(hdev->driver_data);

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		nsh.type = 0x81;
		break;
	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		nsh.type = 0x82;
		break;
	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		nsh.type = 0x83;
		break;
	default:
		return -EILSEQ;
	};

	nsh.zero = 0;
	nsh.len = skb->len;

	s = bt_skb_alloc(NSHL + skb->len + 1, GFP_ATOMIC);
	if (!s)
		return -ENOMEM;

	skb_reserve(s, NSHL);
	skb_copy_from_linear_data(skb, skb_put(s, skb->len), skb->len);
	if (skb->len & 0x0001)
		*skb_put(s, 1) = 0;	/* PAD */

	/* Prepend skb with Nokia frame header and queue */
	memcpy(skb_push(s, NSHL), &nsh, NSHL);
	skb_queue_tail(&(info->txq), s);

	dtl1_write_wakeup(info);

	kfree_skb(skb);

	return 0;
}


static void dtl1_hci_destruct(struct hci_dev *hdev)
{
}


static int dtl1_hci_ioctl(struct hci_dev *hdev, unsigned int cmd,  unsigned long arg)
{
	return -ENOIOCTLCMD;
}



/* ======================== Card services HCI interaction ======================== */


static int dtl1_open(dtl1_info_t *info)
{
	unsigned long flags;
	unsigned int iobase = info->p_dev->resource[0]->start;
	struct hci_dev *hdev;

	spin_lock_init(&(info->lock));

	skb_queue_head_init(&(info->txq));

	info->rx_state = RECV_WAIT_NSH;
	info->rx_count = NSHL;
	info->rx_skb = NULL;

	set_bit(XMIT_WAITING, &(info->tx_state));

	/* Initialize HCI device */
	hdev = hci_alloc_dev();
	if (!hdev) {
		BT_ERR("Can't allocate HCI device");
		return -ENOMEM;
	}

	info->hdev = hdev;

	hdev->bus = HCI_PCCARD;
	hdev->driver_data = info;
	SET_HCIDEV_DEV(hdev, &info->p_dev->dev);

	hdev->open     = dtl1_hci_open;
	hdev->close    = dtl1_hci_close;
	hdev->flush    = dtl1_hci_flush;
	hdev->send     = dtl1_hci_send_frame;
	hdev->destruct = dtl1_hci_destruct;
	hdev->ioctl    = dtl1_hci_ioctl;

	hdev->owner = THIS_MODULE;

	spin_lock_irqsave(&(info->lock), flags);

	/* Reset UART */
	outb(0, iobase + UART_MCR);

	/* Turn off interrupts */
	outb(0, iobase + UART_IER);

	/* Initialize UART */
	outb(UART_LCR_WLEN8, iobase + UART_LCR);	/* Reset DLAB */
	outb((UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2), iobase + UART_MCR);

	info->ri_latch = inb(info->p_dev->resource[0]->start + UART_MSR)
				& UART_MSR_RI;

	/* Turn on interrupts */
	outb(UART_IER_RLSI | UART_IER_RDI | UART_IER_THRI, iobase + UART_IER);

	spin_unlock_irqrestore(&(info->lock), flags);

	/* Timeout before it is safe to send the first HCI packet */
	msleep(2000);

	/* Register HCI device */
	if (hci_register_dev(hdev) < 0) {
		BT_ERR("Can't register HCI device");
		info->hdev = NULL;
		hci_free_dev(hdev);
		return -ENODEV;
	}

	return 0;
}


static int dtl1_close(dtl1_info_t *info)
{
	unsigned long flags;
	unsigned int iobase = info->p_dev->resource[0]->start;
	struct hci_dev *hdev = info->hdev;

	if (!hdev)
		return -ENODEV;

	dtl1_hci_close(hdev);

	spin_lock_irqsave(&(info->lock), flags);

	/* Reset UART */
	outb(0, iobase + UART_MCR);

	/* Turn off interrupts */
	outb(0, iobase + UART_IER);

	spin_unlock_irqrestore(&(info->lock), flags);

	hci_unregister_dev(hdev);
	hci_free_dev(hdev);

	return 0;
}

static int dtl1_probe(struct pcmcia_device *link)
{
	dtl1_info_t *info;

	/* Create new info device */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->p_dev = link;
	link->priv = info;

	link->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;

	return dtl1_config(link);
}


static void dtl1_detach(struct pcmcia_device *link)
{
	dtl1_info_t *info = link->priv;

	dtl1_release(link);

	kfree(info);
}

static int dtl1_confcheck(struct pcmcia_device *p_dev, void *priv_data)
{
	if ((p_dev->resource[1]->end) || (p_dev->resource[1]->end < 8))
		return -ENODEV;

	p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
	p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_8;

	return pcmcia_request_io(p_dev);
}

static int dtl1_config(struct pcmcia_device *link)
{
	dtl1_info_t *info = link->priv;
	int i;

	/* Look for a generic full-sized window */
	link->resource[0]->end = 8;
	if (pcmcia_loop_config(link, dtl1_confcheck, NULL) < 0)
		goto failed;

	i = pcmcia_request_irq(link, dtl1_interrupt);
	if (i != 0)
		goto failed;

	i = pcmcia_enable_device(link);
	if (i != 0)
		goto failed;

	if (dtl1_open(info) != 0)
		goto failed;

	return 0;

failed:
	dtl1_release(link);
	return -ENODEV;
}


static void dtl1_release(struct pcmcia_device *link)
{
	dtl1_info_t *info = link->priv;

	dtl1_close(info);

	pcmcia_disable_device(link);
}


static const struct pcmcia_device_id dtl1_ids[] = {
	PCMCIA_DEVICE_PROD_ID12("Nokia Mobile Phones", "DTL-1", 0xe1bfdd64, 0xe168480d),
	PCMCIA_DEVICE_PROD_ID12("Nokia Mobile Phones", "DTL-4", 0xe1bfdd64, 0x9102bc82),
	PCMCIA_DEVICE_PROD_ID12("Socket", "CF", 0xb38bcc2e, 0x44ebf863),
	PCMCIA_DEVICE_PROD_ID12("Socket", "CF+ Personal Network Card", 0xb38bcc2e, 0xe732bae3),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, dtl1_ids);

static struct pcmcia_driver dtl1_driver = {
	.owner		= THIS_MODULE,
	.name		= "dtl1_cs",
	.probe		= dtl1_probe,
	.remove		= dtl1_detach,
	.id_table	= dtl1_ids,
};

static int __init init_dtl1_cs(void)
{
	return pcmcia_register_driver(&dtl1_driver);
}


static void __exit exit_dtl1_cs(void)
{
	pcmcia_unregister_driver(&dtl1_driver);
}

module_init(init_dtl1_cs);
module_exit(exit_dtl1_cs);
