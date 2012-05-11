/**
 * ipoctal.c
 *
 * driver for the GE IP-OCTAL boards
 * Copyright (c) 2009 Nicolas Serafini, EIC2 SA
 * Copyright (c) 2010,2011 Samuel Iglesias Gonsalvez <siglesia@cern.ch>, CERN
 * Copyright (c) 2012 Samuel Iglesias Gonsalvez <siglesias@igalia.com>, Igalia
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/tty_flip.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include "../ipack.h"
#include "ipoctal.h"
#include "scc2698.h"

#define IP_OCTAL_MANUFACTURER_ID    0xF0
#define IP_OCTAL_232_ID             0x22
#define IP_OCTAL_422_ID             0x2A
#define IP_OCTAL_485_ID             0x48

#define IP_OCTAL_ID_SPACE_VECTOR    0x41
#define IP_OCTAL_NB_BLOCKS          4

static struct ipack_driver driver;
static const struct tty_operations ipoctal_fops;

struct ipoctal {
	struct list_head		list;
	struct ipack_device		*dev;
	unsigned int			board_id;
	struct scc2698_channel		*chan_regs;
	struct scc2698_block		*block_regs;
	struct ipoctal_stats		chan_stats[NR_CHANNELS];
	char				*buffer[NR_CHANNELS];
	unsigned int			nb_bytes[NR_CHANNELS];
	unsigned int			count_wr[NR_CHANNELS];
	struct ipoctal_config		chan_config[NR_CHANNELS];
	wait_queue_head_t		queue[NR_CHANNELS];
	unsigned short			error_flag[NR_CHANNELS];
	spinlock_t			lock[NR_CHANNELS];
	unsigned int			pointer_read[NR_CHANNELS];
	unsigned int			pointer_write[NR_CHANNELS];
	atomic_t			open[NR_CHANNELS];
	unsigned char			write;
	struct tty_port			tty_port[NR_CHANNELS];
	struct tty_driver		*tty_drv;
};

/* Linked list to save the registered devices */
static LIST_HEAD(ipoctal_list);

static inline void ipoctal_write_io_reg(struct ipoctal *ipoctal,
					unsigned char *dest,
					unsigned char value)
{
	unsigned long offset;

	offset = ((void *) dest) - ipoctal->dev->io_space.address;
	ipoctal->dev->ops->write8(ipoctal->dev, IPACK_IO_SPACE, offset, value);
}

static inline void ipoctal_write_cr_cmd(struct ipoctal *ipoctal,
					unsigned char *dest,
					unsigned char value)
{
	ipoctal_write_io_reg(ipoctal, dest, value);
}

static inline unsigned char ipoctal_read_io_reg(struct ipoctal *ipoctal,
						unsigned char *src)
{
	unsigned long offset;
	unsigned char value;

	offset = ((void *) src) - ipoctal->dev->io_space.address;
	ipoctal->dev->ops->read8(ipoctal->dev, IPACK_IO_SPACE, offset, &value);
	return value;
}

static struct ipoctal *ipoctal_find_board(struct tty_struct *tty)
{
	struct ipoctal *p;

	list_for_each_entry(p, &ipoctal_list, list) {
		if (tty->driver->major == p->tty_drv->major)
			return p;
	}

	return NULL;
}

static int ipoctal_port_activate(struct tty_port *port, struct tty_struct *tty)
{
	struct ipoctal *ipoctal;
	int channel = tty->index;

	ipoctal = ipoctal_find_board(tty);

	if (ipoctal == NULL) {
		pr_err("Device not found. Major %d\n", tty->driver->major);
		return -ENODEV;
	}

	ipoctal_write_io_reg(ipoctal, &ipoctal->chan_regs[channel].u.w.cr,
			     CR_ENABLE_RX);
	tty->driver_data = ipoctal;

	return 0;
}

static int ipoctal_open(struct tty_struct *tty, struct file *file)
{
	int channel = tty->index;
	int res;
	struct ipoctal *ipoctal;

	ipoctal = ipoctal_find_board(tty);

	if (ipoctal == NULL) {
		pr_err("Device not found. Major %d\n", tty->driver->major);
		return -ENODEV;
	}

	if (atomic_read(&ipoctal->open[channel]))
		return -EBUSY;

	res = tty_port_open(&ipoctal->tty_port[channel], tty, file);
	if (res)
		return res;

	atomic_inc(&ipoctal->open[channel]);
	return 0;
}

static void ipoctal_reset_stats(struct ipoctal_stats *stats)
{
	stats->tx = 0;
	stats->rx = 0;
	stats->rcv_break = 0;
	stats->framing_err = 0;
	stats->overrun_err = 0;
	stats->parity_err = 0;
}

static void ipoctal_free_channel(struct tty_struct *tty)
{
	int channel = tty->index;
	struct ipoctal *ipoctal = tty->driver_data;

	if (ipoctal == NULL)
		return;

	ipoctal_reset_stats(&ipoctal->chan_stats[channel]);
	ipoctal->pointer_read[channel] = 0;
	ipoctal->pointer_write[channel] = 0;
	ipoctal->nb_bytes[channel] = 0;
}

static void ipoctal_close(struct tty_struct *tty, struct file *filp)
{
	int channel = tty->index;
	struct ipoctal *ipoctal = tty->driver_data;

	tty_port_close(&ipoctal->tty_port[channel], tty, filp);

	if (atomic_dec_and_test(&ipoctal->open[channel]))
		ipoctal_free_channel(tty);
}

static int ipoctal_get_icount(struct tty_struct *tty,
			      struct serial_icounter_struct *icount)
{
	struct ipoctal *ipoctal = tty->driver_data;
	int channel = tty->index;

	icount->cts = 0;
	icount->dsr = 0;
	icount->rng = 0;
	icount->dcd = 0;
	icount->rx = ipoctal->chan_stats[channel].rx;
	icount->tx = ipoctal->chan_stats[channel].tx;
	icount->frame = ipoctal->chan_stats[channel].framing_err;
	icount->parity = ipoctal->chan_stats[channel].parity_err;
	icount->brk = ipoctal->chan_stats[channel].rcv_break;
	return 0;
}

static int ipoctal_irq_handler(void *arg)
{
	unsigned int channel;
	unsigned int block;
	unsigned char isr;
	unsigned char sr;
	unsigned char isr_tx_rdy, isr_rx_rdy;
	unsigned char value;
	unsigned char flag;
	struct tty_struct *tty;
	struct ipoctal *ipoctal = (struct ipoctal *) arg;

	/* Check all channels */
	for (channel = 0; channel < NR_CHANNELS; channel++) {
		/* If there is no client, skip the check */
		if (!atomic_read(&ipoctal->open[channel]))
			continue;

		tty = tty_port_tty_get(&ipoctal->tty_port[channel]);
		if (!tty)
			continue;

		/*
		 * The HW is organized in pair of channels.
		 * See which register we need to read from
		 */
		block = channel / 2;
		isr = ipoctal_read_io_reg(ipoctal,
					  &ipoctal->block_regs[block].u.r.isr);
		sr = ipoctal_read_io_reg(ipoctal,
					 &ipoctal->chan_regs[channel].u.r.sr);

		if ((channel % 2) == 1) {
			isr_tx_rdy = isr & ISR_TxRDY_B;
			isr_rx_rdy = isr & ISR_RxRDY_FFULL_B;
		} else {
			isr_tx_rdy = isr & ISR_TxRDY_A;
			isr_rx_rdy = isr & ISR_RxRDY_FFULL_A;
		}

		/* In case of RS-485, change from TX to RX when finishing TX.
		 * Half-duplex.
		 */
		if ((ipoctal->board_id == IP_OCTAL_485_ID) &&
		    (sr & SR_TX_EMPTY) &&
		    (ipoctal->nb_bytes[channel] == 0)) {
			ipoctal_write_io_reg(ipoctal,
					     &ipoctal->chan_regs[channel].u.w.cr,
					     CR_DISABLE_TX);
			ipoctal_write_cr_cmd(ipoctal,
					     &ipoctal->chan_regs[channel].u.w.cr,
					     CR_CMD_NEGATE_RTSN);
			ipoctal_write_io_reg(ipoctal,
					     &ipoctal->chan_regs[channel].u.w.cr,
					     CR_ENABLE_RX);
			ipoctal->write = 1;
			wake_up_interruptible(&ipoctal->queue[channel]);
		}

		/* RX data */
		if (isr_rx_rdy && (sr & SR_RX_READY)) {
			value = ipoctal_read_io_reg(ipoctal,
						    &ipoctal->chan_regs[channel].u.r.rhr);
			flag = TTY_NORMAL;

			/* Error: count statistics */
			if (sr & SR_ERROR) {
				ipoctal_write_cr_cmd(ipoctal,
						     &ipoctal->chan_regs[channel].u.w.cr,
						     CR_CMD_RESET_ERR_STATUS);

				if (sr & SR_OVERRUN_ERROR) {
					ipoctal->error_flag[channel] |= UART_OVERRUN;
					ipoctal->chan_stats[channel].overrun_err++;
					/* Overrun doesn't affect the current character*/
					tty_insert_flip_char(tty, 0, TTY_OVERRUN);
				}
				if (sr & SR_PARITY_ERROR) {
					ipoctal->error_flag[channel] |= UART_PARITY;
					ipoctal->chan_stats[channel].parity_err++;
					flag = TTY_PARITY;
				}
				if (sr & SR_FRAMING_ERROR) {
					ipoctal->error_flag[channel] |= UART_FRAMING;
					ipoctal->chan_stats[channel].framing_err++;
					flag = TTY_FRAME;
				}
				if (sr & SR_RECEIVED_BREAK) {
					ipoctal->error_flag[channel] |= UART_BREAK;
					ipoctal->chan_stats[channel].rcv_break++;
					flag = TTY_BREAK;
				}
			}

			tty_insert_flip_char(tty, value, flag);
		}

		/* TX of each character */
		if (isr_tx_rdy && (sr & SR_TX_READY)) {
			unsigned int *pointer_write =
				&ipoctal->pointer_write[channel];

			if (ipoctal->nb_bytes[channel] <= 0) {
				ipoctal->nb_bytes[channel] = 0;
				continue;
			}
			spin_lock(&ipoctal->lock[channel]);
			value = ipoctal->buffer[channel][*pointer_write];
			ipoctal_write_io_reg(ipoctal,
					     &ipoctal->chan_regs[channel].u.w.thr,
					     value);
			ipoctal->chan_stats[channel].tx++;
			ipoctal->count_wr[channel]++;
			(*pointer_write)++;
			*pointer_write = *pointer_write % PAGE_SIZE;
			ipoctal->nb_bytes[channel]--;
			spin_unlock(&ipoctal->lock[channel]);

			if ((ipoctal->nb_bytes[channel] == 0) &&
			    (waitqueue_active(&ipoctal->queue[channel]))) {

				if (ipoctal->board_id != IP_OCTAL_485_ID) {
					ipoctal->write = 1;
					wake_up_interruptible(&ipoctal->queue[channel]);
				}
			}
		}

		tty_flip_buffer_push(tty);
		tty_kref_put(tty);
	}
	return IRQ_HANDLED;
}

static int ipoctal_check_model(struct ipack_device *dev, unsigned char *id)
{
	unsigned char manufacturerID;
	unsigned char board_id;

	dev->ops->read8(dev, IPACK_ID_SPACE,
			IPACK_IDPROM_OFFSET_MANUFACTURER_ID, &manufacturerID);
	if (manufacturerID != IP_OCTAL_MANUFACTURER_ID)
		return -ENODEV;

	dev->ops->read8(dev, IPACK_ID_SPACE,
			IPACK_IDPROM_OFFSET_MODEL, (unsigned char *)&board_id);

	switch (board_id) {
	case IP_OCTAL_232_ID:
	case IP_OCTAL_422_ID:
	case IP_OCTAL_485_ID:
		*id = board_id;
		break;
	default:
		return -ENODEV;
	}

	return 0;
}

static const struct tty_port_operations ipoctal_tty_port_ops = {
	.dtr_rts = NULL,
	.activate = ipoctal_port_activate,
};

static int ipoctal_inst_slot(struct ipoctal *ipoctal, unsigned int bus_nr,
			     unsigned int slot, unsigned int vector)
{
	int res = 0;
	int i;
	struct tty_driver *tty;
	char name[20];
	unsigned char board_id;

	res = ipoctal->dev->ops->map_space(ipoctal->dev, 0, IPACK_ID_SPACE);
	if (res) {
		pr_err("Unable to map slot [%d:%d] ID space!\n", bus_nr, slot);
		return res;
	}

	res = ipoctal_check_model(ipoctal->dev, &board_id);
	if (res) {
		ipoctal->dev->ops->unmap_space(ipoctal->dev, IPACK_ID_SPACE);
		goto out_unregister_id_space;
	}
	ipoctal->board_id = board_id;

	res = ipoctal->dev->ops->map_space(ipoctal->dev, 0, IPACK_IO_SPACE);
	if (res) {
		pr_err("Unable to map slot [%d:%d] IO space!\n", bus_nr, slot);
		goto out_unregister_id_space;
	}

	res = ipoctal->dev->ops->map_space(ipoctal->dev,
					   0x8000, IPACK_MEM_SPACE);
	if (res) {
		pr_err("Unable to map slot [%d:%d] MEM space!\n", bus_nr, slot);
		goto out_unregister_io_space;
	}

	/* Save the virtual address to access the registers easily */
	ipoctal->chan_regs =
		(struct scc2698_channel *) ipoctal->dev->io_space.address;
	ipoctal->block_regs =
		(struct scc2698_block *) ipoctal->dev->io_space.address;

	/* Disable RX and TX before touching anything */
	for (i = 0; i < NR_CHANNELS ; i++) {
		ipoctal_write_io_reg(ipoctal, &ipoctal->chan_regs[i].u.w.cr,
				     CR_DISABLE_RX | CR_DISABLE_TX);
	}

	for (i = 0; i < IP_OCTAL_NB_BLOCKS; i++) {
		ipoctal_write_io_reg(ipoctal,
				     &ipoctal->block_regs[i].u.w.acr,
				     ACR_BRG_SET2);
		ipoctal_write_io_reg(ipoctal,
				     &ipoctal->block_regs[i].u.w.opcr,
				     OPCR_MPP_OUTPUT | OPCR_MPOa_RTSN |
				     OPCR_MPOb_RTSN);
		ipoctal_write_io_reg(ipoctal,
				     &ipoctal->block_regs[i].u.w.imr,
				     IMR_TxRDY_A | IMR_RxRDY_FFULL_A |
				     IMR_DELTA_BREAK_A | IMR_TxRDY_B |
				     IMR_RxRDY_FFULL_B | IMR_DELTA_BREAK_B);
	}

	/*
	 * IP-OCTAL has different addresses to copy its IRQ vector.
	 * Depending of the carrier these addresses are accesible or not.
	 * More info in the datasheet.
	 */
	ipoctal->dev->ops->request_irq(ipoctal->dev, vector,
				       ipoctal_irq_handler, ipoctal);
	ipoctal->dev->ops->write8(ipoctal->dev, IPACK_ID_SPACE, 0, vector);

	/* Register the TTY device */

	/* Each IP-OCTAL channel is a TTY port */
	tty = alloc_tty_driver(NR_CHANNELS);

	if (!tty) {
		res = -ENOMEM;
		goto out_unregister_slot_unmap;
	}

	/* Fill struct tty_driver with ipoctal data */
	tty->owner = THIS_MODULE;
	tty->driver_name = "ipoctal";
	sprintf(name, "ipoctal.%d.%d.", bus_nr, slot);
	tty->name = name;
	tty->major = 0;

	tty->minor_start = 0;
	tty->type = TTY_DRIVER_TYPE_SERIAL;
	tty->subtype = SERIAL_TYPE_NORMAL;
	tty->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	tty->init_termios = tty_std_termios;
	tty->init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	tty->init_termios.c_ispeed = 9600;
	tty->init_termios.c_ospeed = 9600;

	tty_set_operations(tty, &ipoctal_fops);
	res = tty_register_driver(tty);
	if (res) {
		pr_err("Can't register tty driver.\n");
		put_tty_driver(tty);
		goto out_unregister_slot_unmap;
	}

	/* Save struct tty_driver for use it when uninstalling the device */
	ipoctal->tty_drv = tty;

	for (i = 0; i < NR_CHANNELS; i++) {
		tty_port_init(&ipoctal->tty_port[i]);
		tty_port_alloc_xmit_buf(&ipoctal->tty_port[i]);
		ipoctal->tty_port[i].ops = &ipoctal_tty_port_ops;

		ipoctal_reset_stats(&ipoctal->chan_stats[i]);
		ipoctal->nb_bytes[i] = 0;
		init_waitqueue_head(&ipoctal->queue[i]);
		ipoctal->error_flag[i] = UART_NOERROR;

		spin_lock_init(&ipoctal->lock[i]);
		ipoctal->pointer_read[i] = 0;
		ipoctal->pointer_write[i] = 0;
		ipoctal->nb_bytes[i] = 0;
		tty_register_device(tty, i, NULL);

		/*
		 * Enable again the RX. TX will be enabled when
		 * there is something to send
		 */
		ipoctal_write_io_reg(ipoctal, &ipoctal->chan_regs[i].u.w.cr,
				     CR_ENABLE_RX);
	}

	return 0;

out_unregister_slot_unmap:
	ipoctal->dev->ops->unmap_space(ipoctal->dev, IPACK_ID_SPACE);
out_unregister_io_space:
	ipoctal->dev->ops->unmap_space(ipoctal->dev, IPACK_IO_SPACE);
out_unregister_id_space:
	ipoctal->dev->ops->unmap_space(ipoctal->dev, IPACK_MEM_SPACE);
	return res;
}

static inline int ipoctal_copy_write_buffer(struct ipoctal *ipoctal,
					    unsigned int channel,
					    const unsigned char *buf,
					    int count)
{
	unsigned long flags;
	int i;
	unsigned int *pointer_read = &ipoctal->pointer_read[channel];

	/* Copy the bytes from the user buffer to the internal one */
	for (i = 0; i < count; i++) {
		if (i <= (PAGE_SIZE - ipoctal->nb_bytes[channel])) {
			spin_lock_irqsave(&ipoctal->lock[channel], flags);
			ipoctal->tty_port[channel].xmit_buf[*pointer_read] = buf[i];
			*pointer_read = (*pointer_read + 1) % PAGE_SIZE;
			ipoctal->nb_bytes[channel]++;
			spin_unlock_irqrestore(&ipoctal->lock[channel], flags);
		} else {
			break;
		}
	}
	return i;
}

static int ipoctal_write(struct ipoctal *ipoctal, unsigned int channel,
			 const unsigned char *buf, int count)
{
	ipoctal->nb_bytes[channel] = 0;
	ipoctal->count_wr[channel] = 0;

	ipoctal_copy_write_buffer(ipoctal, channel, buf, count);

	ipoctal->error_flag[channel] = UART_NOERROR;

	/* As the IP-OCTAL 485 only supports half duplex, do it manually */
	if (ipoctal->board_id == IP_OCTAL_485_ID) {
		ipoctal_write_io_reg(ipoctal,
				     &ipoctal->chan_regs[channel].u.w.cr,
				     CR_DISABLE_RX);
		ipoctal_write_cr_cmd(ipoctal,
				     &ipoctal->chan_regs[channel].u.w.cr,
				     CR_CMD_ASSERT_RTSN);
	}

	/*
	 * Send a packet and then disable TX to avoid failure after several send
	 * operations
	 */
	ipoctal_write_io_reg(ipoctal,
			     &ipoctal->chan_regs[channel].u.w.cr,
			     CR_ENABLE_TX);
	wait_event_interruptible(ipoctal->queue[channel], ipoctal->write);
	ipoctal_write_io_reg(ipoctal,
			     &ipoctal->chan_regs[channel].u.w.cr,
			     CR_DISABLE_TX);

	ipoctal->write = 0;
	return ipoctal->count_wr[channel];
}

static int ipoctal_write_tty(struct tty_struct *tty,
			     const unsigned char *buf, int count)
{
	unsigned int channel = tty->index;
	struct ipoctal *ipoctal = tty->driver_data;

	return ipoctal_write(ipoctal, channel, buf, count);
}

static int ipoctal_write_room(struct tty_struct *tty)
{
	int channel = tty->index;
	struct ipoctal *ipoctal = tty->driver_data;

	return PAGE_SIZE - ipoctal->nb_bytes[channel];
}

static int ipoctal_chars_in_buffer(struct tty_struct *tty)
{
	int channel = tty->index;
	struct ipoctal *ipoctal = tty->driver_data;

	return ipoctal->nb_bytes[channel];
}

static void ipoctal_set_termios(struct tty_struct *tty,
				struct ktermios *old_termios)
{
	unsigned int cflag;
	unsigned char mr1 = 0;
	unsigned char mr2 = 0;
	unsigned char csr = 0;
	unsigned int channel = tty->index;
	struct ipoctal *ipoctal = tty->driver_data;
	speed_t baud;

	cflag = tty->termios->c_cflag;

	/* Disable and reset everything before change the setup */
	ipoctal_write_io_reg(ipoctal, &ipoctal->chan_regs[channel].u.w.cr,
			     CR_DISABLE_RX | CR_DISABLE_TX);
	ipoctal_write_cr_cmd(ipoctal, &ipoctal->chan_regs[channel].u.w.cr,
			     CR_CMD_RESET_RX);
	ipoctal_write_cr_cmd(ipoctal, &ipoctal->chan_regs[channel].u.w.cr,
			     CR_CMD_RESET_TX);
	ipoctal_write_cr_cmd(ipoctal, &ipoctal->chan_regs[channel].u.w.cr,
			     CR_CMD_RESET_ERR_STATUS);
	ipoctal_write_cr_cmd(ipoctal, &ipoctal->chan_regs[channel].u.w.cr,
			     CR_CMD_RESET_MR);

	/* Set Bits per chars */
	switch (cflag & CSIZE) {
	case CS6:
		mr1 |= MR1_CHRL_6_BITS;
		break;
	case CS7:
		mr1 |= MR1_CHRL_7_BITS;
		break;
	case CS8:
	default:
		mr1 |= MR1_CHRL_8_BITS;
		/* By default, select CS8 */
		tty->termios->c_cflag = (cflag & ~CSIZE) | CS8;
		break;
	}

	/* Set Parity */
	if (cflag & PARENB)
		if (cflag & PARODD)
			mr1 |= MR1_PARITY_ON | MR1_PARITY_ODD;
		else
			mr1 |= MR1_PARITY_ON | MR1_PARITY_EVEN;
	else
		mr1 |= MR1_PARITY_OFF;

	/* Mark or space parity is not supported */
	tty->termios->c_cflag &= ~CMSPAR;

	/* Set stop bits */
	if (cflag & CSTOPB)
		mr2 |= MR2_STOP_BITS_LENGTH_2;
	else
		mr2 |= MR2_STOP_BITS_LENGTH_1;

	/* Set the flow control */
	switch (ipoctal->board_id) {
	case IP_OCTAL_232_ID:
		if (cflag & CRTSCTS) {
			mr1 |= MR1_RxRTS_CONTROL_ON;
			mr2 |= MR2_TxRTS_CONTROL_OFF | MR2_CTS_ENABLE_TX_ON;
			ipoctal->chan_config[channel].flow_control = 1;
		} else {
			mr1 |= MR1_RxRTS_CONTROL_OFF;
			mr2 |= MR2_TxRTS_CONTROL_OFF | MR2_CTS_ENABLE_TX_OFF;
			ipoctal->chan_config[channel].flow_control = 0;
		}
		break;
	case IP_OCTAL_422_ID:
		mr1 |= MR1_RxRTS_CONTROL_OFF;
		mr2 |= MR2_TxRTS_CONTROL_OFF | MR2_CTS_ENABLE_TX_OFF;
		ipoctal->chan_config[channel].flow_control = 0;
		break;
	case IP_OCTAL_485_ID:
		mr1 |= MR1_RxRTS_CONTROL_OFF;
		mr2 |= MR2_TxRTS_CONTROL_ON | MR2_CTS_ENABLE_TX_OFF;
		ipoctal->chan_config[channel].flow_control = 0;
		break;
	default:
		return;
		break;
	}

	baud = tty_get_baud_rate(tty);
	tty_termios_encode_baud_rate(tty->termios, baud, baud);

	/* Set baud rate */
	switch (tty->termios->c_ospeed) {
	case 75:
		csr |= TX_CLK_75 | RX_CLK_75;
		break;
	case 110:
		csr |= TX_CLK_110 | RX_CLK_110;
		break;
	case 150:
		csr |= TX_CLK_150 | RX_CLK_150;
		break;
	case 300:
		csr |= TX_CLK_300 | RX_CLK_300;
		break;
	case 600:
		csr |= TX_CLK_600 | RX_CLK_600;
		break;
	case 1200:
		csr |= TX_CLK_1200 | RX_CLK_1200;
		break;
	case 1800:
		csr |= TX_CLK_1800 | RX_CLK_1800;
		break;
	case 2000:
		csr |= TX_CLK_2000 | RX_CLK_2000;
		break;
	case 2400:
		csr |= TX_CLK_2400 | RX_CLK_2400;
		break;
	case 4800:
		csr |= TX_CLK_4800  | RX_CLK_4800;
		break;
	case 9600:
		csr |= TX_CLK_9600  | RX_CLK_9600;
		break;
	case 19200:
		csr |= TX_CLK_19200 | RX_CLK_19200;
		break;
	case 38400:
	default:
		csr |= TX_CLK_38400 | RX_CLK_38400;
		/* In case of default, we establish 38400 bps */
		tty_termios_encode_baud_rate(tty->termios, 38400, 38400);
		break;
	}

	mr1 |= MR1_ERROR_CHAR;
	mr1 |= MR1_RxINT_RxRDY;

	/* Write the control registers */
	ipoctal_write_io_reg(ipoctal, &ipoctal->chan_regs[channel].u.w.mr, mr1);
	ipoctal_write_io_reg(ipoctal, &ipoctal->chan_regs[channel].u.w.mr, mr2);
	ipoctal_write_io_reg(ipoctal, &ipoctal->chan_regs[channel].u.w.csr, csr);

	/* save the setup in the structure */
	ipoctal->chan_config[channel].baud = tty_get_baud_rate(tty);
	ipoctal->chan_config[channel].bits_per_char = cflag & CSIZE;
	ipoctal->chan_config[channel].parity = cflag & PARENB;
	ipoctal->chan_config[channel].stop_bits = cflag & CSTOPB;

	/* Enable again the RX */
	ipoctal_write_io_reg(ipoctal, &ipoctal->chan_regs[channel].u.w.cr,
			     CR_ENABLE_RX);
}

static void ipoctal_hangup(struct tty_struct *tty)
{
	unsigned long flags;
	int channel = tty->index;
	struct ipoctal *ipoctal = tty->driver_data;

	if (ipoctal == NULL)
		return;

	spin_lock_irqsave(&ipoctal->lock[channel], flags);
	ipoctal->nb_bytes[channel] = 0;
	ipoctal->pointer_read[channel] = 0;
	ipoctal->pointer_write[channel] = 0;
	spin_unlock_irqrestore(&ipoctal->lock[channel], flags);

	tty_port_hangup(&ipoctal->tty_port[channel]);

	ipoctal_write_io_reg(ipoctal, &ipoctal->chan_regs[channel].u.w.cr,
			     CR_DISABLE_RX | CR_DISABLE_TX);
	ipoctal_write_cr_cmd(ipoctal, &ipoctal->chan_regs[channel].u.w.cr,
			     CR_CMD_RESET_RX);
	ipoctal_write_cr_cmd(ipoctal, &ipoctal->chan_regs[channel].u.w.cr,
			     CR_CMD_RESET_TX);
	ipoctal_write_cr_cmd(ipoctal, &ipoctal->chan_regs[channel].u.w.cr,
			     CR_CMD_RESET_ERR_STATUS);
	ipoctal_write_cr_cmd(ipoctal, &ipoctal->chan_regs[channel].u.w.cr,
			     CR_CMD_RESET_MR);

	clear_bit(ASYNCB_INITIALIZED, &ipoctal->tty_port[channel].flags);
	wake_up_interruptible(&ipoctal->tty_port[channel].open_wait);
}

static const struct tty_operations ipoctal_fops = {
	.ioctl =		NULL,
	.open =			ipoctal_open,
	.close =		ipoctal_close,
	.write =		ipoctal_write_tty,
	.set_termios =		ipoctal_set_termios,
	.write_room =		ipoctal_write_room,
	.chars_in_buffer =	ipoctal_chars_in_buffer,
	.get_icount =		ipoctal_get_icount,
	.hangup =		ipoctal_hangup,
};

static int ipoctal_match(struct ipack_device *dev)
{
	int res;
	unsigned char board_id;

	res = dev->ops->map_space(dev, 0, IPACK_ID_SPACE);
	if (res)
		return res;

	res = ipoctal_check_model(dev, &board_id);
	dev->ops->unmap_space(dev, IPACK_ID_SPACE);
	return res;
}

static int ipoctal_probe(struct ipack_device *dev)
{
	int res;
	struct ipoctal *ipoctal;

	ipoctal = kzalloc(sizeof(struct ipoctal), GFP_KERNEL);
	if (ipoctal == NULL)
		return -ENOMEM;

	ipoctal->dev = dev;
	res = ipoctal_inst_slot(ipoctal, dev->bus_nr, dev->slot, dev->irq);
	if (res)
		goto out_uninst;

	list_add_tail(&ipoctal->list, &ipoctal_list);
	return 0;

out_uninst:
	kfree(ipoctal);
	return res;
}

static void __ipoctal_remove(struct ipoctal *ipoctal)
{
	int i;

	for (i = 0; i < NR_CHANNELS; i++) {
		tty_unregister_device(ipoctal->tty_drv, i);
		tty_port_free_xmit_buf(&ipoctal->tty_port[i]);
	}

	tty_unregister_driver(ipoctal->tty_drv);
	put_tty_driver(ipoctal->tty_drv);

	/* Tell the carrier board to free all the resources for this device */
	if (ipoctal->dev->ops->remove_device != NULL)
		ipoctal->dev->ops->remove_device(ipoctal->dev);

	list_del(&ipoctal->list);
	kfree(ipoctal);
}

static void ipoctal_remove(struct ipack_device *device)
{
	struct ipoctal *ipoctal, *next;

	list_for_each_entry_safe(ipoctal, next, &ipoctal_list, list) {
		if (ipoctal->dev == device)
			__ipoctal_remove(ipoctal);
	}
}

static struct ipack_driver_ops ipoctal_drv_ops = {
	.match = ipoctal_match,
	.probe = ipoctal_probe,
	.remove = ipoctal_remove,
};

static int __init ipoctal_init(void)
{
	driver.owner = THIS_MODULE;
	driver.ops = &ipoctal_drv_ops;
	driver.driver.name = KBUILD_MODNAME;
	ipack_driver_register(&driver);
	return 0;
}

static void __exit ipoctal_exit(void)
{
	struct ipoctal *p, *next;

	list_for_each_entry_safe(p, next, &ipoctal_list, list)
		__ipoctal_remove(p);

	ipack_driver_unregister(&driver);
}

MODULE_DESCRIPTION("IP-Octal 232, 422 and 485 device driver");
MODULE_LICENSE("GPL");

module_init(ipoctal_init);
module_exit(ipoctal_exit);
