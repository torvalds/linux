/*
 *
 *  Bluetooth driver for the Anycom BlueCard (LSE039/LSE041)
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
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/spinlock.h>
#include <linux/moduleparam.h>
#include <linux/wait.h>

#include <linux/skbuff.h>
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
MODULE_DESCRIPTION("Bluetooth driver for the Anycom BlueCard (LSE039/LSE041)");
MODULE_LICENSE("GPL");



/* ======================== Local structures ======================== */


typedef struct bluecard_info_t {
	struct pcmcia_device *p_dev;
	dev_node_t node;

	struct hci_dev *hdev;

	spinlock_t lock;		/* For serializing operations */
	struct timer_list timer;	/* For LED control */

	struct sk_buff_head txq;
	unsigned long tx_state;

	unsigned long rx_state;
	unsigned long rx_count;
	struct sk_buff *rx_skb;

	unsigned char ctrl_reg;
	unsigned long hw_state;		/* Status of the hardware and LED control */
} bluecard_info_t;


static int bluecard_config(struct pcmcia_device *link);
static void bluecard_release(struct pcmcia_device *link);

static void bluecard_detach(struct pcmcia_device *p_dev);


/* Default baud rate: 57600, 115200, 230400 or 460800 */
#define DEFAULT_BAUD_RATE  230400


/* Hardware states */
#define CARD_READY             1
#define CARD_HAS_PCCARD_ID     4
#define CARD_HAS_POWER_LED     5
#define CARD_HAS_ACTIVITY_LED  6

/* Transmit states  */
#define XMIT_SENDING         1
#define XMIT_WAKEUP          2
#define XMIT_BUFFER_NUMBER   5	/* unset = buffer one, set = buffer two */
#define XMIT_BUF_ONE_READY   6
#define XMIT_BUF_TWO_READY   7
#define XMIT_SENDING_READY   8

/* Receiver states */
#define RECV_WAIT_PACKET_TYPE   0
#define RECV_WAIT_EVENT_HEADER  1
#define RECV_WAIT_ACL_HEADER    2
#define RECV_WAIT_SCO_HEADER    3
#define RECV_WAIT_DATA          4

/* Special packet types */
#define PKT_BAUD_RATE_57600   0x80
#define PKT_BAUD_RATE_115200  0x81
#define PKT_BAUD_RATE_230400  0x82
#define PKT_BAUD_RATE_460800  0x83


/* These are the register offsets */
#define REG_COMMAND     0x20
#define REG_INTERRUPT   0x21
#define REG_CONTROL     0x22
#define REG_RX_CONTROL  0x24
#define REG_CARD_RESET  0x30
#define REG_LED_CTRL    0x30

/* REG_COMMAND */
#define REG_COMMAND_TX_BUF_ONE  0x01
#define REG_COMMAND_TX_BUF_TWO  0x02
#define REG_COMMAND_RX_BUF_ONE  0x04
#define REG_COMMAND_RX_BUF_TWO  0x08
#define REG_COMMAND_RX_WIN_ONE  0x00
#define REG_COMMAND_RX_WIN_TWO  0x10

/* REG_CONTROL */
#define REG_CONTROL_BAUD_RATE_57600   0x00
#define REG_CONTROL_BAUD_RATE_115200  0x01
#define REG_CONTROL_BAUD_RATE_230400  0x02
#define REG_CONTROL_BAUD_RATE_460800  0x03
#define REG_CONTROL_RTS               0x04
#define REG_CONTROL_BT_ON             0x08
#define REG_CONTROL_BT_RESET          0x10
#define REG_CONTROL_BT_RES_PU         0x20
#define REG_CONTROL_INTERRUPT         0x40
#define REG_CONTROL_CARD_RESET        0x80

/* REG_RX_CONTROL */
#define RTS_LEVEL_SHIFT_BITS  0x02



/* ======================== LED handling routines ======================== */


static void bluecard_activity_led_timeout(u_long arg)
{
	bluecard_info_t *info = (bluecard_info_t *)arg;
	unsigned int iobase = info->p_dev->io.BasePort1;

	if (!test_bit(CARD_HAS_PCCARD_ID, &(info->hw_state)))
		return;

	if (test_bit(CARD_HAS_ACTIVITY_LED, &(info->hw_state))) {
		/* Disable activity LED */
		outb(0x08 | 0x20, iobase + 0x30);
	} else {
		/* Disable power LED */
		outb(0x00, iobase + 0x30);
	}
}


static void bluecard_enable_activity_led(bluecard_info_t *info)
{
	unsigned int iobase = info->p_dev->io.BasePort1;

	if (!test_bit(CARD_HAS_PCCARD_ID, &(info->hw_state)))
		return;

	if (test_bit(CARD_HAS_ACTIVITY_LED, &(info->hw_state))) {
		/* Enable activity LED */
		outb(0x10 | 0x40, iobase + 0x30);

		/* Stop the LED after HZ/4 */
		mod_timer(&(info->timer), jiffies + HZ / 4);
	} else {
		/* Enable power LED */
		outb(0x08 | 0x20, iobase + 0x30);

		/* Stop the LED after HZ/2 */
		mod_timer(&(info->timer), jiffies + HZ / 2);
	}
}



/* ======================== Interrupt handling ======================== */


static int bluecard_write(unsigned int iobase, unsigned int offset, __u8 *buf, int len)
{
	int i, actual;

	actual = (len > 15) ? 15 : len;

	outb_p(actual, iobase + offset);

	for (i = 0; i < actual; i++)
		outb_p(buf[i], iobase + offset + i + 1);

	return actual;
}


static void bluecard_write_wakeup(bluecard_info_t *info)
{
	if (!info) {
		BT_ERR("Unknown device");
		return;
	}

	if (!test_bit(XMIT_SENDING_READY, &(info->tx_state)))
		return;

	if (test_and_set_bit(XMIT_SENDING, &(info->tx_state))) {
		set_bit(XMIT_WAKEUP, &(info->tx_state));
		return;
	}

	do {
		register unsigned int iobase = info->p_dev->io.BasePort1;
		register unsigned int offset;
		register unsigned char command;
		register unsigned long ready_bit;
		register struct sk_buff *skb;
		register int len;

		clear_bit(XMIT_WAKEUP, &(info->tx_state));

		if (!pcmcia_dev_present(info->p_dev))
			return;

		if (test_bit(XMIT_BUFFER_NUMBER, &(info->tx_state))) {
			if (!test_bit(XMIT_BUF_TWO_READY, &(info->tx_state)))
				break;
			offset = 0x10;
			command = REG_COMMAND_TX_BUF_TWO;
			ready_bit = XMIT_BUF_TWO_READY;
		} else {
			if (!test_bit(XMIT_BUF_ONE_READY, &(info->tx_state)))
				break;
			offset = 0x00;
			command = REG_COMMAND_TX_BUF_ONE;
			ready_bit = XMIT_BUF_ONE_READY;
		}

		if (!(skb = skb_dequeue(&(info->txq))))
			break;

		if (bt_cb(skb)->pkt_type & 0x80) {
			/* Disable RTS */
			info->ctrl_reg |= REG_CONTROL_RTS;
			outb(info->ctrl_reg, iobase + REG_CONTROL);
		}

		/* Activate LED */
		bluecard_enable_activity_led(info);

		/* Send frame */
		len = bluecard_write(iobase, offset, skb->data, skb->len);

		/* Tell the FPGA to send the data */
		outb_p(command, iobase + REG_COMMAND);

		/* Mark the buffer as dirty */
		clear_bit(ready_bit, &(info->tx_state));

		if (bt_cb(skb)->pkt_type & 0x80) {
			DECLARE_WAIT_QUEUE_HEAD(wq);
			DEFINE_WAIT(wait);

			unsigned char baud_reg;

			switch (bt_cb(skb)->pkt_type) {
			case PKT_BAUD_RATE_460800:
				baud_reg = REG_CONTROL_BAUD_RATE_460800;
				break;
			case PKT_BAUD_RATE_230400:
				baud_reg = REG_CONTROL_BAUD_RATE_230400;
				break;
			case PKT_BAUD_RATE_115200:
				baud_reg = REG_CONTROL_BAUD_RATE_115200;
				break;
			case PKT_BAUD_RATE_57600:
				/* Fall through... */
			default:
				baud_reg = REG_CONTROL_BAUD_RATE_57600;
				break;
			}

			/* Wait until the command reaches the baseband */
			prepare_to_wait(&wq, &wait, TASK_INTERRUPTIBLE);
			schedule_timeout(HZ/10);
			finish_wait(&wq, &wait);

			/* Set baud on baseband */
			info->ctrl_reg &= ~0x03;
			info->ctrl_reg |= baud_reg;
			outb(info->ctrl_reg, iobase + REG_CONTROL);

			/* Enable RTS */
			info->ctrl_reg &= ~REG_CONTROL_RTS;
			outb(info->ctrl_reg, iobase + REG_CONTROL);

			/* Wait before the next HCI packet can be send */
			prepare_to_wait(&wq, &wait, TASK_INTERRUPTIBLE);
			schedule_timeout(HZ);
			finish_wait(&wq, &wait);
		}

		if (len == skb->len) {
			kfree_skb(skb);
		} else {
			skb_pull(skb, len);
			skb_queue_head(&(info->txq), skb);
		}

		info->hdev->stat.byte_tx += len;

		/* Change buffer */
		change_bit(XMIT_BUFFER_NUMBER, &(info->tx_state));

	} while (test_bit(XMIT_WAKEUP, &(info->tx_state)));

	clear_bit(XMIT_SENDING, &(info->tx_state));
}


static int bluecard_read(unsigned int iobase, unsigned int offset, __u8 *buf, int size)
{
	int i, n, len;

	outb(REG_COMMAND_RX_WIN_ONE, iobase + REG_COMMAND);

	len = inb(iobase + offset);
	n = 0;
	i = 1;

	while (n < len) {

		if (i == 16) {
			outb(REG_COMMAND_RX_WIN_TWO, iobase + REG_COMMAND);
			i = 0;
		}

		buf[n] = inb(iobase + offset + i);

		n++;
		i++;

	}

	return len;
}


static void bluecard_receive(bluecard_info_t *info, unsigned int offset)
{
	unsigned int iobase;
	unsigned char buf[31];
	int i, len;

	if (!info) {
		BT_ERR("Unknown device");
		return;
	}

	iobase = info->p_dev->io.BasePort1;

	if (test_bit(XMIT_SENDING_READY, &(info->tx_state)))
		bluecard_enable_activity_led(info);

	len = bluecard_read(iobase, offset, buf, sizeof(buf));

	for (i = 0; i < len; i++) {

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
			bt_cb(info->rx_skb)->pkt_type = buf[i];

			switch (bt_cb(info->rx_skb)->pkt_type) {

			case 0x00:
				/* init packet */
				if (offset != 0x00) {
					set_bit(XMIT_BUF_ONE_READY, &(info->tx_state));
					set_bit(XMIT_BUF_TWO_READY, &(info->tx_state));
					set_bit(XMIT_SENDING_READY, &(info->tx_state));
					bluecard_write_wakeup(info);
				}

				kfree_skb(info->rx_skb);
				info->rx_skb = NULL;
				break;

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
				/* unknown packet */
				BT_ERR("Unknown HCI packet with type 0x%02x received", bt_cb(info->rx_skb)->pkt_type);
				info->hdev->stat.err_rx++;

				kfree_skb(info->rx_skb);
				info->rx_skb = NULL;
				break;

			}

		} else {

			*skb_put(info->rx_skb, 1) = buf[i];
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


	}

	info->hdev->stat.byte_rx += len;
}


static irqreturn_t bluecard_interrupt(int irq, void *dev_inst)
{
	bluecard_info_t *info = dev_inst;
	unsigned int iobase;
	unsigned char reg;

	if (!info || !info->hdev) {
		BT_ERR("Call of irq %d for unknown device", irq);
		return IRQ_NONE;
	}

	if (!test_bit(CARD_READY, &(info->hw_state)))
		return IRQ_HANDLED;

	iobase = info->p_dev->io.BasePort1;

	spin_lock(&(info->lock));

	/* Disable interrupt */
	info->ctrl_reg &= ~REG_CONTROL_INTERRUPT;
	outb(info->ctrl_reg, iobase + REG_CONTROL);

	reg = inb(iobase + REG_INTERRUPT);

	if ((reg != 0x00) && (reg != 0xff)) {

		if (reg & 0x04) {
			bluecard_receive(info, 0x00);
			outb(0x04, iobase + REG_INTERRUPT);
			outb(REG_COMMAND_RX_BUF_ONE, iobase + REG_COMMAND);
		}

		if (reg & 0x08) {
			bluecard_receive(info, 0x10);
			outb(0x08, iobase + REG_INTERRUPT);
			outb(REG_COMMAND_RX_BUF_TWO, iobase + REG_COMMAND);
		}

		if (reg & 0x01) {
			set_bit(XMIT_BUF_ONE_READY, &(info->tx_state));
			outb(0x01, iobase + REG_INTERRUPT);
			bluecard_write_wakeup(info);
		}

		if (reg & 0x02) {
			set_bit(XMIT_BUF_TWO_READY, &(info->tx_state));
			outb(0x02, iobase + REG_INTERRUPT);
			bluecard_write_wakeup(info);
		}

	}

	/* Enable interrupt */
	info->ctrl_reg |= REG_CONTROL_INTERRUPT;
	outb(info->ctrl_reg, iobase + REG_CONTROL);

	spin_unlock(&(info->lock));

	return IRQ_HANDLED;
}



/* ======================== Device specific HCI commands ======================== */


static int bluecard_hci_set_baud_rate(struct hci_dev *hdev, int baud)
{
	bluecard_info_t *info = (bluecard_info_t *)(hdev->driver_data);
	struct sk_buff *skb;

	/* Ericsson baud rate command */
	unsigned char cmd[] = { HCI_COMMAND_PKT, 0x09, 0xfc, 0x01, 0x03 };

	if (!(skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE, GFP_ATOMIC))) {
		BT_ERR("Can't allocate mem for new packet");
		return -1;
	}

	switch (baud) {
	case 460800:
		cmd[4] = 0x00;
		bt_cb(skb)->pkt_type = PKT_BAUD_RATE_460800;
		break;
	case 230400:
		cmd[4] = 0x01;
		bt_cb(skb)->pkt_type = PKT_BAUD_RATE_230400;
		break;
	case 115200:
		cmd[4] = 0x02;
		bt_cb(skb)->pkt_type = PKT_BAUD_RATE_115200;
		break;
	case 57600:
		/* Fall through... */
	default:
		cmd[4] = 0x03;
		bt_cb(skb)->pkt_type = PKT_BAUD_RATE_57600;
		break;
	}

	memcpy(skb_put(skb, sizeof(cmd)), cmd, sizeof(cmd));

	skb_queue_tail(&(info->txq), skb);

	bluecard_write_wakeup(info);

	return 0;
}



/* ======================== HCI interface ======================== */


static int bluecard_hci_flush(struct hci_dev *hdev)
{
	bluecard_info_t *info = (bluecard_info_t *)(hdev->driver_data);

	/* Drop TX queue */
	skb_queue_purge(&(info->txq));

	return 0;
}


static int bluecard_hci_open(struct hci_dev *hdev)
{
	bluecard_info_t *info = (bluecard_info_t *)(hdev->driver_data);
	unsigned int iobase = info->p_dev->io.BasePort1;

	if (test_bit(CARD_HAS_PCCARD_ID, &(info->hw_state)))
		bluecard_hci_set_baud_rate(hdev, DEFAULT_BAUD_RATE);

	if (test_and_set_bit(HCI_RUNNING, &(hdev->flags)))
		return 0;

	if (test_bit(CARD_HAS_PCCARD_ID, &(info->hw_state))) {
		/* Enable LED */
		outb(0x08 | 0x20, iobase + 0x30);
	}

	return 0;
}


static int bluecard_hci_close(struct hci_dev *hdev)
{
	bluecard_info_t *info = (bluecard_info_t *)(hdev->driver_data);
	unsigned int iobase = info->p_dev->io.BasePort1;

	if (!test_and_clear_bit(HCI_RUNNING, &(hdev->flags)))
		return 0;

	bluecard_hci_flush(hdev);

	if (test_bit(CARD_HAS_PCCARD_ID, &(info->hw_state))) {
		/* Disable LED */
		outb(0x00, iobase + 0x30);
	}

	return 0;
}


static int bluecard_hci_send_frame(struct sk_buff *skb)
{
	bluecard_info_t *info;
	struct hci_dev *hdev = (struct hci_dev *)(skb->dev);

	if (!hdev) {
		BT_ERR("Frame for unknown HCI device (hdev=NULL)");
		return -ENODEV;
	}

	info = (bluecard_info_t *)(hdev->driver_data);

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

	bluecard_write_wakeup(info);

	return 0;
}


static void bluecard_hci_destruct(struct hci_dev *hdev)
{
}


static int bluecard_hci_ioctl(struct hci_dev *hdev, unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}



/* ======================== Card services HCI interaction ======================== */


static int bluecard_open(bluecard_info_t *info)
{
	unsigned int iobase = info->p_dev->io.BasePort1;
	struct hci_dev *hdev;
	unsigned char id;

	spin_lock_init(&(info->lock));

	init_timer(&(info->timer));
	info->timer.function = &bluecard_activity_led_timeout;
	info->timer.data = (u_long)info;

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
	SET_HCIDEV_DEV(hdev, &info->p_dev->dev);

	hdev->open     = bluecard_hci_open;
	hdev->close    = bluecard_hci_close;
	hdev->flush    = bluecard_hci_flush;
	hdev->send     = bluecard_hci_send_frame;
	hdev->destruct = bluecard_hci_destruct;
	hdev->ioctl    = bluecard_hci_ioctl;

	hdev->owner = THIS_MODULE;

	id = inb(iobase + 0x30);

	if ((id & 0x0f) == 0x02)
		set_bit(CARD_HAS_PCCARD_ID, &(info->hw_state));

	if (id & 0x10)
		set_bit(CARD_HAS_POWER_LED, &(info->hw_state));

	if (id & 0x20)
		set_bit(CARD_HAS_ACTIVITY_LED, &(info->hw_state));

	/* Reset card */
	info->ctrl_reg = REG_CONTROL_BT_RESET | REG_CONTROL_CARD_RESET;
	outb(info->ctrl_reg, iobase + REG_CONTROL);

	/* Turn FPGA off */
	outb(0x80, iobase + 0x30);

	/* Wait some time */
	msleep(10);

	/* Turn FPGA on */
	outb(0x00, iobase + 0x30);

	/* Activate card */
	info->ctrl_reg = REG_CONTROL_BT_ON | REG_CONTROL_BT_RES_PU;
	outb(info->ctrl_reg, iobase + REG_CONTROL);

	/* Enable interrupt */
	outb(0xff, iobase + REG_INTERRUPT);
	info->ctrl_reg |= REG_CONTROL_INTERRUPT;
	outb(info->ctrl_reg, iobase + REG_CONTROL);

	if ((id & 0x0f) == 0x03) {
		/* Disable RTS */
		info->ctrl_reg |= REG_CONTROL_RTS;
		outb(info->ctrl_reg, iobase + REG_CONTROL);

		/* Set baud rate */
		info->ctrl_reg |= 0x03;
		outb(info->ctrl_reg, iobase + REG_CONTROL);

		/* Enable RTS */
		info->ctrl_reg &= ~REG_CONTROL_RTS;
		outb(info->ctrl_reg, iobase + REG_CONTROL);

		set_bit(XMIT_BUF_ONE_READY, &(info->tx_state));
		set_bit(XMIT_BUF_TWO_READY, &(info->tx_state));
		set_bit(XMIT_SENDING_READY, &(info->tx_state));
	}

	/* Start the RX buffers */
	outb(REG_COMMAND_RX_BUF_ONE, iobase + REG_COMMAND);
	outb(REG_COMMAND_RX_BUF_TWO, iobase + REG_COMMAND);

	/* Signal that the hardware is ready */
	set_bit(CARD_READY, &(info->hw_state));

	/* Drop TX queue */
	skb_queue_purge(&(info->txq));

	/* Control the point at which RTS is enabled */
	outb((0x0f << RTS_LEVEL_SHIFT_BITS) | 1, iobase + REG_RX_CONTROL);

	/* Timeout before it is safe to send the first HCI packet */
	msleep(1250);

	/* Register HCI device */
	if (hci_register_dev(hdev) < 0) {
		BT_ERR("Can't register HCI device");
		info->hdev = NULL;
		hci_free_dev(hdev);
		return -ENODEV;
	}

	return 0;
}


static int bluecard_close(bluecard_info_t *info)
{
	unsigned int iobase = info->p_dev->io.BasePort1;
	struct hci_dev *hdev = info->hdev;

	if (!hdev)
		return -ENODEV;

	bluecard_hci_close(hdev);

	clear_bit(CARD_READY, &(info->hw_state));

	/* Reset card */
	info->ctrl_reg = REG_CONTROL_BT_RESET | REG_CONTROL_CARD_RESET;
	outb(info->ctrl_reg, iobase + REG_CONTROL);

	/* Turn FPGA off */
	outb(0x80, iobase + 0x30);

	if (hci_unregister_dev(hdev) < 0)
		BT_ERR("Can't unregister HCI device %s", hdev->name);

	hci_free_dev(hdev);

	return 0;
}

static int bluecard_probe(struct pcmcia_device *link)
{
	bluecard_info_t *info;

	/* Create new info device */
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->p_dev = link;
	link->priv = info;

	link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
	link->io.NumPorts1 = 8;
	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE | IRQ_HANDLE_PRESENT;
	link->irq.IRQInfo1 = IRQ_LEVEL_ID;

	link->irq.Handler = bluecard_interrupt;
	link->irq.Instance = info;

	link->conf.Attributes = CONF_ENABLE_IRQ;
	link->conf.IntType = INT_MEMORY_AND_IO;

	return bluecard_config(link);
}


static void bluecard_detach(struct pcmcia_device *link)
{
	bluecard_info_t *info = link->priv;

	bluecard_release(link);
	kfree(info);
}


static int first_tuple(struct pcmcia_device *handle, tuple_t *tuple, cisparse_t *parse)
{
	int i;

	i = pcmcia_get_first_tuple(handle, tuple);
	if (i != CS_SUCCESS)
		return CS_NO_MORE_ITEMS;

	i = pcmcia_get_tuple_data(handle, tuple);
	if (i != CS_SUCCESS)
		return i;

	return pcmcia_parse_tuple(handle, tuple, parse);
}

static int bluecard_config(struct pcmcia_device *link)
{
	bluecard_info_t *info = link->priv;
	tuple_t tuple;
	u_short buf[256];
	cisparse_t parse;
	int i, n, last_ret, last_fn;

	tuple.TupleData = (cisdata_t *)buf;
	tuple.TupleOffset = 0;
	tuple.TupleDataMax = 255;
	tuple.Attributes = 0;

	/* Get configuration register information */
	tuple.DesiredTuple = CISTPL_CONFIG;
	last_ret = first_tuple(link, &tuple, &parse);
	if (last_ret != CS_SUCCESS) {
		last_fn = ParseTuple;
		goto cs_failed;
	}
	link->conf.ConfigBase = parse.config.base;
	link->conf.Present = parse.config.rmask[0];

	link->conf.ConfigIndex = 0x20;
	link->io.NumPorts1 = 64;
	link->io.IOAddrLines = 6;

	for (n = 0; n < 0x400; n += 0x40) {
		link->io.BasePort1 = n ^ 0x300;
		i = pcmcia_request_io(link, &link->io);
		if (i == CS_SUCCESS)
			break;
	}

	if (i != CS_SUCCESS) {
		cs_error(link, RequestIO, i);
		goto failed;
	}

	i = pcmcia_request_irq(link, &link->irq);
	if (i != CS_SUCCESS) {
		cs_error(link, RequestIRQ, i);
		link->irq.AssignedIRQ = 0;
	}

	i = pcmcia_request_configuration(link, &link->conf);
	if (i != CS_SUCCESS) {
		cs_error(link, RequestConfiguration, i);
		goto failed;
	}

	if (bluecard_open(info) != 0)
		goto failed;

	strcpy(info->node.dev_name, info->hdev->name);
	link->dev_node = &info->node;

	return 0;

cs_failed:
	cs_error(link, last_fn, last_ret);

failed:
	bluecard_release(link);
	return -ENODEV;
}


static void bluecard_release(struct pcmcia_device *link)
{
	bluecard_info_t *info = link->priv;

	bluecard_close(info);

	del_timer(&(info->timer));

	pcmcia_disable_device(link);
}

static struct pcmcia_device_id bluecard_ids[] = {
	PCMCIA_DEVICE_PROD_ID12("BlueCard", "LSE041", 0xbaf16fbf, 0x657cc15e),
	PCMCIA_DEVICE_PROD_ID12("BTCFCARD", "LSE139", 0xe3987764, 0x2524b59c),
	PCMCIA_DEVICE_PROD_ID12("WSS", "LSE039", 0x0a0736ec, 0x24e6dfab),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, bluecard_ids);

static struct pcmcia_driver bluecard_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "bluecard_cs",
	},
	.probe		= bluecard_probe,
	.remove		= bluecard_detach,
	.id_table	= bluecard_ids,
};

static int __init init_bluecard_cs(void)
{
	return pcmcia_register_driver(&bluecard_driver);
}


static void __exit exit_bluecard_cs(void)
{
	pcmcia_unregister_driver(&bluecard_driver);
}

module_init(init_bluecard_cs);
module_exit(exit_bluecard_cs);
