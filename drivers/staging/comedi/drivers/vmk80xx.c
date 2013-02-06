/*
    comedi/drivers/vmk80xx.c
    Velleman USB Board Low-Level Driver

    Copyright (C) 2009 Manuel Gebele <forensixs@gmx.de>, Germany

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/
/*
Driver: vmk80xx
Description: Velleman USB Board Low-Level Driver
Devices: K8055/K8061 aka VM110/VM140
Author: Manuel Gebele <forensixs@gmx.de>
Updated: Sun, 10 May 2009 11:14:59 +0200
Status: works

Supports:
 - analog input
 - analog output
 - digital input
 - digital output
 - counter
 - pwm
*/
/*
Changelog:

0.8.81	-3-  code completely rewritten (adjust driver logic)
0.8.81  -2-  full support for K8061
0.8.81  -1-  fix some mistaken among others the number of
	     supported boards and I/O handling

0.7.76  -4-  renamed to vmk80xx
0.7.76  -3-  detect K8061 (only theoretically supported)
0.7.76  -2-  code completely rewritten (adjust driver logic)
0.7.76  -1-  support for digital and counter subdevice
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/usb.h>
#include <linux/uaccess.h>

#include "../comedidev.h"

enum {
	DEVICE_VMK8055,
	DEVICE_VMK8061
};

#define VMK8055_DI_REG          0x00
#define VMK8055_DO_REG          0x01
#define VMK8055_AO1_REG         0x02
#define VMK8055_AO2_REG         0x03
#define VMK8055_AI1_REG         0x02
#define VMK8055_AI2_REG         0x03
#define VMK8055_CNT1_REG        0x04
#define VMK8055_CNT2_REG        0x06

#define VMK8061_CH_REG          0x01
#define VMK8061_DI_REG          0x01
#define VMK8061_DO_REG          0x01
#define VMK8061_PWM_REG1        0x01
#define VMK8061_PWM_REG2        0x02
#define VMK8061_CNT_REG         0x02
#define VMK8061_AO_REG          0x02
#define VMK8061_AI_REG1         0x02
#define VMK8061_AI_REG2         0x03

#define VMK8055_CMD_RST         0x00
#define VMK8055_CMD_DEB1_TIME   0x01
#define VMK8055_CMD_DEB2_TIME   0x02
#define VMK8055_CMD_RST_CNT1    0x03
#define VMK8055_CMD_RST_CNT2    0x04
#define VMK8055_CMD_WRT_AD      0x05

#define VMK8061_CMD_RD_AI       0x00
#define VMK8061_CMR_RD_ALL_AI   0x01	/* !non-active! */
#define VMK8061_CMD_SET_AO      0x02
#define VMK8061_CMD_SET_ALL_AO  0x03	/* !non-active! */
#define VMK8061_CMD_OUT_PWM     0x04
#define VMK8061_CMD_RD_DI       0x05
#define VMK8061_CMD_DO          0x06	/* !non-active! */
#define VMK8061_CMD_CLR_DO      0x07
#define VMK8061_CMD_SET_DO      0x08
#define VMK8061_CMD_RD_CNT      0x09	/* TODO: completely pointless? */
#define VMK8061_CMD_RST_CNT     0x0a	/* TODO: completely pointless? */
#define VMK8061_CMD_RD_VERSION  0x0b	/* internal usage */
#define VMK8061_CMD_RD_JMP_STAT 0x0c	/* TODO: not implemented yet */
#define VMK8061_CMD_RD_PWR_STAT 0x0d	/* internal usage */
#define VMK8061_CMD_RD_DO       0x0e
#define VMK8061_CMD_RD_AO       0x0f
#define VMK8061_CMD_RD_PWM      0x10

#define VMK80XX_MAX_BOARDS      COMEDI_NUM_BOARD_MINORS

#define TRANS_OUT_BUSY          1
#define TRANS_IN_BUSY           2
#define TRANS_IN_RUNNING        3

#define IC3_VERSION             (1 << 0)
#define IC6_VERSION             (1 << 1)

#define URB_RCV_FLAG            (1 << 0)
#define URB_SND_FLAG            (1 << 1)

#ifdef CONFIG_COMEDI_DEBUG
static int dbgcm = 1;
#else
static int dbgcm;
#endif

#define dbgcm(fmt, arg...)                     \
do {                                           \
	if (dbgcm)                             \
		printk(KERN_DEBUG fmt, ##arg); \
} while (0)

enum vmk80xx_model {
	VMK8055_MODEL,
	VMK8061_MODEL
};

struct firmware_version {
	unsigned char ic3_vers[32];	/* USB-Controller */
	unsigned char ic6_vers[32];	/* CPU */
};

static const struct comedi_lrange vmk8055_range = {
	1, {UNI_RANGE(5)}
};

static const struct comedi_lrange vmk8061_range = {
	2, {UNI_RANGE(5), UNI_RANGE(10)}
};

struct vmk80xx_board {
	const char *name;
	enum vmk80xx_model model;
	const struct comedi_lrange *range;
	int ai_nchans;
	unsigned int ai_maxdata;
	int ao_nchans;
	int di_nchans;
	unsigned int cnt_maxdata;
	int pwm_nchans;
	unsigned int pwm_maxdata;
};

static const struct vmk80xx_board vmk80xx_boardinfo[] = {
	[DEVICE_VMK8055] = {
		.name		= "K8055 (VM110)",
		.model		= VMK8055_MODEL,
		.range		= &vmk8055_range,
		.ai_nchans	= 2,
		.ai_maxdata	= 0x00ff,
		.ao_nchans	= 2,
		.di_nchans	= 6,
		.cnt_maxdata	= 0xffff,
	},
	[DEVICE_VMK8061] = {
		.name		= "K8061 (VM140)",
		.model		= VMK8061_MODEL,
		.range		= &vmk8061_range,
		.ai_nchans	= 8,
		.ai_maxdata	= 0x03ff,
		.ao_nchans	= 8,
		.di_nchans	= 8,
		.cnt_maxdata	= 0,	/* unknown, device is not writeable */
		.pwm_nchans	= 1,
		.pwm_maxdata	= 0x03ff,
	},
};

struct vmk80xx_private {
	struct usb_device *usb;
	struct usb_interface *intf;
	struct usb_endpoint_descriptor *ep_rx;
	struct usb_endpoint_descriptor *ep_tx;
	struct usb_anchor rx_anchor;
	struct usb_anchor tx_anchor;
	struct firmware_version fw;
	struct semaphore limit_sem;
	wait_queue_head_t read_wait;
	wait_queue_head_t write_wait;
	unsigned char *usb_rx_buf;
	unsigned char *usb_tx_buf;
	unsigned long flags;
	enum vmk80xx_model model;
};

static void vmk80xx_tx_callback(struct urb *urb)
{
	struct vmk80xx_private *devpriv = urb->context;
	unsigned long *flags = &devpriv->flags;
	int stat = urb->status;

	if (stat && !(stat == -ENOENT
		      || stat == -ECONNRESET || stat == -ESHUTDOWN))
		dbgcm("comedi#: vmk80xx: %s - nonzero urb status (%d)\n",
		      __func__, stat);

	if (!test_bit(TRANS_OUT_BUSY, flags))
		return;

	clear_bit(TRANS_OUT_BUSY, flags);

	wake_up_interruptible(&devpriv->write_wait);
}

static void vmk80xx_rx_callback(struct urb *urb)
{
	struct vmk80xx_private *devpriv = urb->context;
	unsigned long *flags = &devpriv->flags;
	int stat = urb->status;

	switch (stat) {
	case 0:
		break;
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		break;
	default:
		dbgcm("comedi#: vmk80xx: %s - nonzero urb status (%d)\n",
		      __func__, stat);
		goto resubmit;
	}

	goto exit;
resubmit:
	if (test_bit(TRANS_IN_RUNNING, flags) && devpriv->intf) {
		usb_anchor_urb(urb, &devpriv->rx_anchor);

		if (!usb_submit_urb(urb, GFP_KERNEL))
			goto exit;

		dev_err(&urb->dev->dev,
			"comedi#: vmk80xx: %s - submit urb failed\n",
			__func__);

		usb_unanchor_urb(urb);
	}
exit:
	clear_bit(TRANS_IN_BUSY, flags);

	wake_up_interruptible(&devpriv->read_wait);
}

static int vmk80xx_check_data_link(struct vmk80xx_private *devpriv)
{
	struct usb_device *usb = devpriv->usb;
	unsigned int tx_pipe;
	unsigned int rx_pipe;
	unsigned char tx[1];
	unsigned char rx[2];

	tx_pipe = usb_sndbulkpipe(usb, 0x01);
	rx_pipe = usb_rcvbulkpipe(usb, 0x81);

	tx[0] = VMK8061_CMD_RD_PWR_STAT;

	/*
	 * Check that IC6 (PIC16F871) is powered and
	 * running and the data link between IC3 and
	 * IC6 is working properly
	 */
	usb_bulk_msg(usb, tx_pipe, tx, 1, NULL, devpriv->ep_tx->bInterval);
	usb_bulk_msg(usb, rx_pipe, rx, 2, NULL, HZ * 10);

	return (int)rx[1];
}

static void vmk80xx_read_eeprom(struct vmk80xx_private *devpriv, int flag)
{
	struct usb_device *usb = devpriv->usb;
	unsigned int tx_pipe;
	unsigned int rx_pipe;
	unsigned char tx[1];
	unsigned char rx[64];
	int cnt;

	tx_pipe = usb_sndbulkpipe(usb, 0x01);
	rx_pipe = usb_rcvbulkpipe(usb, 0x81);

	tx[0] = VMK8061_CMD_RD_VERSION;

	/*
	 * Read the firmware version info of IC3 and
	 * IC6 from the internal EEPROM of the IC
	 */
	usb_bulk_msg(usb, tx_pipe, tx, 1, NULL, devpriv->ep_tx->bInterval);
	usb_bulk_msg(usb, rx_pipe, rx, 64, &cnt, HZ * 10);

	rx[cnt] = '\0';

	if (flag & IC3_VERSION)
		strncpy(devpriv->fw.ic3_vers, rx + 1, 24);
	else			/* IC6_VERSION */
		strncpy(devpriv->fw.ic6_vers, rx + 25, 24);
}

static int vmk80xx_reset_device(struct vmk80xx_private *devpriv)
{
	struct usb_device *usb = devpriv->usb;
	unsigned char *tx_buf = devpriv->usb_tx_buf;
	struct urb *urb;
	unsigned int tx_pipe;
	int ival;
	size_t size;

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;

	tx_pipe = usb_sndintpipe(usb, 0x01);

	ival = devpriv->ep_tx->bInterval;
	size = le16_to_cpu(devpriv->ep_tx->wMaxPacketSize);

	tx_buf[0] = VMK8055_CMD_RST;
	tx_buf[1] = 0x00;
	tx_buf[2] = 0x00;
	tx_buf[3] = 0x00;
	tx_buf[4] = 0x00;
	tx_buf[5] = 0x00;
	tx_buf[6] = 0x00;
	tx_buf[7] = 0x00;

	usb_fill_int_urb(urb, usb, tx_pipe, tx_buf, size,
			 vmk80xx_tx_callback, devpriv, ival);

	usb_anchor_urb(urb, &devpriv->tx_anchor);

	return usb_submit_urb(urb, GFP_KERNEL);
}

static void vmk80xx_build_int_urb(struct urb *urb, int flag)
{
	struct vmk80xx_private *devpriv = urb->context;
	struct usb_device *usb = devpriv->usb;
	__u8 rx_addr;
	__u8 tx_addr;
	unsigned int pipe;
	unsigned char *buf;
	size_t size;
	void (*callback) (struct urb *);
	int ival;

	if (flag & URB_RCV_FLAG) {
		rx_addr = devpriv->ep_rx->bEndpointAddress;
		pipe = usb_rcvintpipe(usb, rx_addr);
		buf = devpriv->usb_rx_buf;
		size = le16_to_cpu(devpriv->ep_rx->wMaxPacketSize);
		callback = vmk80xx_rx_callback;
		ival = devpriv->ep_rx->bInterval;
	} else {		/* URB_SND_FLAG */
		tx_addr = devpriv->ep_tx->bEndpointAddress;
		pipe = usb_sndintpipe(usb, tx_addr);
		buf = devpriv->usb_tx_buf;
		size = le16_to_cpu(devpriv->ep_tx->wMaxPacketSize);
		callback = vmk80xx_tx_callback;
		ival = devpriv->ep_tx->bInterval;
	}

	usb_fill_int_urb(urb, usb, pipe, buf, size, callback, devpriv, ival);
}

static void vmk80xx_do_bulk_msg(struct vmk80xx_private *devpriv)
{
	struct usb_device *usb = devpriv->usb;
	unsigned long *flags = &devpriv->flags;
	__u8 tx_addr;
	__u8 rx_addr;
	unsigned int tx_pipe;
	unsigned int rx_pipe;
	size_t size;

	set_bit(TRANS_IN_BUSY, flags);
	set_bit(TRANS_OUT_BUSY, flags);

	tx_addr = devpriv->ep_tx->bEndpointAddress;
	rx_addr = devpriv->ep_rx->bEndpointAddress;
	tx_pipe = usb_sndbulkpipe(usb, tx_addr);
	rx_pipe = usb_rcvbulkpipe(usb, rx_addr);

	/*
	 * The max packet size attributes of the K8061
	 * input/output endpoints are identical
	 */
	size = le16_to_cpu(devpriv->ep_tx->wMaxPacketSize);

	usb_bulk_msg(usb, tx_pipe, devpriv->usb_tx_buf,
		     size, NULL, devpriv->ep_tx->bInterval);
	usb_bulk_msg(usb, rx_pipe, devpriv->usb_rx_buf, size, NULL, HZ * 10);

	clear_bit(TRANS_OUT_BUSY, flags);
	clear_bit(TRANS_IN_BUSY, flags);
}

static int vmk80xx_read_packet(struct vmk80xx_private *devpriv)
{
	unsigned long *flags = &devpriv->flags;
	struct urb *urb;
	int retval;

	if (!devpriv->intf)
		return -ENODEV;

	/* Only useful for interrupt transfers */
	if (test_bit(TRANS_IN_BUSY, flags))
		if (wait_event_interruptible(devpriv->read_wait,
					     !test_bit(TRANS_IN_BUSY, flags)))
			return -ERESTART;

	if (devpriv->model == VMK8061_MODEL) {
		vmk80xx_do_bulk_msg(devpriv);

		return 0;
	}

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;

	urb->context = devpriv;
	vmk80xx_build_int_urb(urb, URB_RCV_FLAG);

	set_bit(TRANS_IN_RUNNING, flags);
	set_bit(TRANS_IN_BUSY, flags);

	usb_anchor_urb(urb, &devpriv->rx_anchor);

	retval = usb_submit_urb(urb, GFP_KERNEL);
	if (!retval)
		goto exit;

	clear_bit(TRANS_IN_RUNNING, flags);
	usb_unanchor_urb(urb);

exit:
	usb_free_urb(urb);

	return retval;
}

static int vmk80xx_write_packet(struct vmk80xx_private *devpriv, int cmd)
{
	unsigned long *flags = &devpriv->flags;
	struct urb *urb;
	int retval;

	if (!devpriv->intf)
		return -ENODEV;

	if (test_bit(TRANS_OUT_BUSY, flags))
		if (wait_event_interruptible(devpriv->write_wait,
					     !test_bit(TRANS_OUT_BUSY, flags)))
			return -ERESTART;

	if (devpriv->model == VMK8061_MODEL) {
		devpriv->usb_tx_buf[0] = cmd;
		vmk80xx_do_bulk_msg(devpriv);

		return 0;
	}

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb)
		return -ENOMEM;

	urb->context = devpriv;
	vmk80xx_build_int_urb(urb, URB_SND_FLAG);

	set_bit(TRANS_OUT_BUSY, flags);

	usb_anchor_urb(urb, &devpriv->tx_anchor);

	devpriv->usb_tx_buf[0] = cmd;

	retval = usb_submit_urb(urb, GFP_KERNEL);
	if (!retval)
		goto exit;

	clear_bit(TRANS_OUT_BUSY, flags);
	usb_unanchor_urb(urb);

exit:
	usb_free_urb(urb);

	return retval;
}

#define DIR_IN  1
#define DIR_OUT 2

static int rudimentary_check(struct vmk80xx_private *devpriv, int dir)
{
	if (!devpriv)
		return -EFAULT;
	if (dir & DIR_IN) {
		if (test_bit(TRANS_IN_BUSY, &devpriv->flags))
			return -EBUSY;
	}
	if (dir & DIR_OUT) {
		if (test_bit(TRANS_OUT_BUSY, &devpriv->flags))
			return -EBUSY;
	}

	return 0;
}

static int vmk80xx_ai_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct vmk80xx_private *devpriv = dev->private;
	int chan;
	int reg[2];
	int n;

	n = rudimentary_check(devpriv, DIR_IN);
	if (n)
		return n;

	down(&devpriv->limit_sem);
	chan = CR_CHAN(insn->chanspec);

	switch (devpriv->model) {
	case VMK8055_MODEL:
		if (!chan)
			reg[0] = VMK8055_AI1_REG;
		else
			reg[0] = VMK8055_AI2_REG;
		break;
	case VMK8061_MODEL:
	default:
		reg[0] = VMK8061_AI_REG1;
		reg[1] = VMK8061_AI_REG2;
		devpriv->usb_tx_buf[0] = VMK8061_CMD_RD_AI;
		devpriv->usb_tx_buf[VMK8061_CH_REG] = chan;
		break;
	}

	for (n = 0; n < insn->n; n++) {
		if (vmk80xx_read_packet(devpriv))
			break;

		if (devpriv->model == VMK8055_MODEL) {
			data[n] = devpriv->usb_rx_buf[reg[0]];
			continue;
		}

		/* VMK8061_MODEL */
		data[n] = devpriv->usb_rx_buf[reg[0]] + 256 *
		    devpriv->usb_rx_buf[reg[1]];
	}

	up(&devpriv->limit_sem);

	return n;
}

static int vmk80xx_ao_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct vmk80xx_private *devpriv = dev->private;
	int chan;
	int cmd;
	int reg;
	int n;

	n = rudimentary_check(devpriv, DIR_OUT);
	if (n)
		return n;

	down(&devpriv->limit_sem);
	chan = CR_CHAN(insn->chanspec);

	switch (devpriv->model) {
	case VMK8055_MODEL:
		cmd = VMK8055_CMD_WRT_AD;
		if (!chan)
			reg = VMK8055_AO1_REG;
		else
			reg = VMK8055_AO2_REG;
		break;
	default:		/* NOTE: avoid compiler warnings */
		cmd = VMK8061_CMD_SET_AO;
		reg = VMK8061_AO_REG;
		devpriv->usb_tx_buf[VMK8061_CH_REG] = chan;
		break;
	}

	for (n = 0; n < insn->n; n++) {
		devpriv->usb_tx_buf[reg] = data[n];

		if (vmk80xx_write_packet(devpriv, cmd))
			break;
	}

	up(&devpriv->limit_sem);

	return n;
}

static int vmk80xx_ao_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct vmk80xx_private *devpriv = dev->private;
	int chan;
	int reg;
	int n;

	n = rudimentary_check(devpriv, DIR_IN);
	if (n)
		return n;

	down(&devpriv->limit_sem);
	chan = CR_CHAN(insn->chanspec);

	reg = VMK8061_AO_REG - 1;

	devpriv->usb_tx_buf[0] = VMK8061_CMD_RD_AO;

	for (n = 0; n < insn->n; n++) {
		if (vmk80xx_read_packet(devpriv))
			break;

		data[n] = devpriv->usb_rx_buf[reg + chan];
	}

	up(&devpriv->limit_sem);

	return n;
}

static int vmk80xx_di_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct vmk80xx_private *devpriv = dev->private;
	unsigned char *rx_buf;
	int reg;
	int retval;

	retval = rudimentary_check(devpriv, DIR_IN);
	if (retval)
		return retval;

	down(&devpriv->limit_sem);

	rx_buf = devpriv->usb_rx_buf;

	if (devpriv->model == VMK8061_MODEL) {
		reg = VMK8061_DI_REG;
		devpriv->usb_tx_buf[0] = VMK8061_CMD_RD_DI;
	} else {
		reg = VMK8055_DI_REG;
	}

	retval = vmk80xx_read_packet(devpriv);

	if (!retval) {
		if (devpriv->model == VMK8055_MODEL)
			data[1] = (((rx_buf[reg] >> 4) & 0x03) |
				  ((rx_buf[reg] << 2) & 0x04) |
				  ((rx_buf[reg] >> 3) & 0x18));
		else
			data[1] = rx_buf[reg];

		retval = 2;
	}

	up(&devpriv->limit_sem);

	return retval;
}

static int vmk80xx_di_insn_read(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct vmk80xx_private *devpriv = dev->private;
	int chan;
	unsigned char *rx_buf;
	int reg;
	int inp;
	int n;

	n = rudimentary_check(devpriv, DIR_IN);
	if (n)
		return n;

	down(&devpriv->limit_sem);
	chan = CR_CHAN(insn->chanspec);

	rx_buf = devpriv->usb_rx_buf;

	if (devpriv->model == VMK8061_MODEL) {
		reg = VMK8061_DI_REG;
		devpriv->usb_tx_buf[0] = VMK8061_CMD_RD_DI;
	} else {
		reg = VMK8055_DI_REG;
	}
	for (n = 0; n < insn->n; n++) {
		if (vmk80xx_read_packet(devpriv))
			break;

		if (devpriv->model == VMK8055_MODEL)
			inp = (((rx_buf[reg] >> 4) & 0x03) |
			       ((rx_buf[reg] << 2) & 0x04) |
			       ((rx_buf[reg] >> 3) & 0x18));
		else
			inp = rx_buf[reg];

		data[n] = (inp >> chan) & 1;
	}

	up(&devpriv->limit_sem);

	return n;
}

static int vmk80xx_do_insn_write(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct vmk80xx_private *devpriv = dev->private;
	int chan;
	unsigned char *tx_buf;
	int reg;
	int cmd;
	int n;

	n = rudimentary_check(devpriv, DIR_OUT);
	if (n)
		return n;

	down(&devpriv->limit_sem);
	chan = CR_CHAN(insn->chanspec);

	tx_buf = devpriv->usb_tx_buf;

	for (n = 0; n < insn->n; n++) {
		if (devpriv->model == VMK8055_MODEL) {
			reg = VMK8055_DO_REG;
			cmd = VMK8055_CMD_WRT_AD;
			if (data[n] == 1)
				tx_buf[reg] |= (1 << chan);
			else
				tx_buf[reg] ^= (1 << chan);
		} else { /* VMK8061_MODEL */
			reg = VMK8061_DO_REG;
			if (data[n] == 1) {
				cmd = VMK8061_CMD_SET_DO;
				tx_buf[reg] = 1 << chan;
			} else {
				cmd = VMK8061_CMD_CLR_DO;
				tx_buf[reg] = 0xff - (1 << chan);
			}
		}

		if (vmk80xx_write_packet(devpriv, cmd))
			break;
	}

	up(&devpriv->limit_sem);

	return n;
}

static int vmk80xx_do_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct vmk80xx_private *devpriv = dev->private;
	unsigned char *rx_buf, *tx_buf;
	int dir, reg, cmd;
	int retval;

	dir = 0;

	if (data[0])
		dir |= DIR_OUT;

	if (devpriv->model == VMK8061_MODEL)
		dir |= DIR_IN;

	retval = rudimentary_check(devpriv, dir);
	if (retval)
		return retval;

	down(&devpriv->limit_sem);

	rx_buf = devpriv->usb_rx_buf;
	tx_buf = devpriv->usb_tx_buf;

	if (data[0]) {
		if (devpriv->model == VMK8055_MODEL) {
			reg = VMK8055_DO_REG;
			cmd = VMK8055_CMD_WRT_AD;
		} else { /* VMK8061_MODEL */
			reg = VMK8061_DO_REG;
			cmd = VMK8061_CMD_DO;
		}

		tx_buf[reg] &= ~data[0];
		tx_buf[reg] |= (data[0] & data[1]);

		retval = vmk80xx_write_packet(devpriv, cmd);

		if (retval)
			goto out;
	}

	if (devpriv->model == VMK8061_MODEL) {
		reg = VMK8061_DO_REG;
		tx_buf[0] = VMK8061_CMD_RD_DO;

		retval = vmk80xx_read_packet(devpriv);

		if (!retval) {
			data[1] = rx_buf[reg];
			retval = 2;
		}
	} else {
		data[1] = tx_buf[reg];
		retval = 2;
	}

out:
	up(&devpriv->limit_sem);

	return retval;
}

static int vmk80xx_cnt_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct vmk80xx_private *devpriv = dev->private;
	int chan;
	int reg[2];
	int n;

	n = rudimentary_check(devpriv, DIR_IN);
	if (n)
		return n;

	down(&devpriv->limit_sem);
	chan = CR_CHAN(insn->chanspec);

	switch (devpriv->model) {
	case VMK8055_MODEL:
		if (!chan)
			reg[0] = VMK8055_CNT1_REG;
		else
			reg[0] = VMK8055_CNT2_REG;
		break;
	case VMK8061_MODEL:
	default:
		reg[0] = VMK8061_CNT_REG;
		reg[1] = VMK8061_CNT_REG;
		devpriv->usb_tx_buf[0] = VMK8061_CMD_RD_CNT;
		break;
	}

	for (n = 0; n < insn->n; n++) {
		if (vmk80xx_read_packet(devpriv))
			break;

		if (devpriv->model == VMK8055_MODEL)
			data[n] = devpriv->usb_rx_buf[reg[0]];
		else /* VMK8061_MODEL */
			data[n] = devpriv->usb_rx_buf[reg[0] * (chan + 1) + 1]
			    + 256 * devpriv->usb_rx_buf[reg[1] * 2 + 2];
	}

	up(&devpriv->limit_sem);

	return n;
}

static int vmk80xx_cnt_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct vmk80xx_private *devpriv = dev->private;
	unsigned int insn_cmd;
	int chan;
	int cmd;
	int reg;
	int n;

	n = rudimentary_check(devpriv, DIR_OUT);
	if (n)
		return n;

	insn_cmd = data[0];
	if (insn_cmd != INSN_CONFIG_RESET && insn_cmd != GPCT_RESET)
		return -EINVAL;

	down(&devpriv->limit_sem);

	chan = CR_CHAN(insn->chanspec);

	if (devpriv->model == VMK8055_MODEL) {
		if (!chan) {
			cmd = VMK8055_CMD_RST_CNT1;
			reg = VMK8055_CNT1_REG;
		} else {
			cmd = VMK8055_CMD_RST_CNT2;
			reg = VMK8055_CNT2_REG;
		}

		devpriv->usb_tx_buf[reg] = 0x00;
	} else {
		cmd = VMK8061_CMD_RST_CNT;
	}

	for (n = 0; n < insn->n; n++)
		if (vmk80xx_write_packet(devpriv, cmd))
			break;

	up(&devpriv->limit_sem);

	return n;
}

static int vmk80xx_cnt_insn_write(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	struct vmk80xx_private *devpriv = dev->private;
	unsigned long debtime;
	unsigned long val;
	int chan;
	int cmd;
	int n;

	n = rudimentary_check(devpriv, DIR_OUT);
	if (n)
		return n;

	down(&devpriv->limit_sem);
	chan = CR_CHAN(insn->chanspec);

	if (!chan)
		cmd = VMK8055_CMD_DEB1_TIME;
	else
		cmd = VMK8055_CMD_DEB2_TIME;

	for (n = 0; n < insn->n; n++) {
		debtime = data[n];
		if (debtime == 0)
			debtime = 1;

		/* TODO: Prevent overflows */
		if (debtime > 7450)
			debtime = 7450;

		val = int_sqrt(debtime * 1000 / 115);
		if (((val + 1) * val) < debtime * 1000 / 115)
			val += 1;

		devpriv->usb_tx_buf[6 + chan] = val;

		if (vmk80xx_write_packet(devpriv, cmd))
			break;
	}

	up(&devpriv->limit_sem);

	return n;
}

static int vmk80xx_pwm_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct vmk80xx_private *devpriv = dev->private;
	unsigned char *tx_buf;
	unsigned char *rx_buf;
	int reg[2];
	int n;

	n = rudimentary_check(devpriv, DIR_IN);
	if (n)
		return n;

	down(&devpriv->limit_sem);

	tx_buf = devpriv->usb_tx_buf;
	rx_buf = devpriv->usb_rx_buf;

	reg[0] = VMK8061_PWM_REG1;
	reg[1] = VMK8061_PWM_REG2;

	tx_buf[0] = VMK8061_CMD_RD_PWM;

	for (n = 0; n < insn->n; n++) {
		if (vmk80xx_read_packet(devpriv))
			break;

		data[n] = rx_buf[reg[0]] + 4 * rx_buf[reg[1]];
	}

	up(&devpriv->limit_sem);

	return n;
}

static int vmk80xx_pwm_insn_write(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	struct vmk80xx_private *devpriv = dev->private;
	unsigned char *tx_buf;
	int reg[2];
	int cmd;
	int n;

	n = rudimentary_check(devpriv, DIR_OUT);
	if (n)
		return n;

	down(&devpriv->limit_sem);

	tx_buf = devpriv->usb_tx_buf;

	reg[0] = VMK8061_PWM_REG1;
	reg[1] = VMK8061_PWM_REG2;

	cmd = VMK8061_CMD_OUT_PWM;

	/*
	 * The followin piece of code was translated from the inline
	 * assembler code in the DLL source code.
	 *
	 * asm
	 *   mov eax, k  ; k is the value (data[n])
	 *   and al, 03h ; al are the lower 8 bits of eax
	 *   mov lo, al  ; lo is the low part (tx_buf[reg[0]])
	 *   mov eax, k
	 *   shr eax, 2  ; right shift eax register by 2
	 *   mov hi, al  ; hi is the high part (tx_buf[reg[1]])
	 * end;
	 */
	for (n = 0; n < insn->n; n++) {
		tx_buf[reg[0]] = (unsigned char)(data[n] & 0x03);
		tx_buf[reg[1]] = (unsigned char)(data[n] >> 2) & 0xff;

		if (vmk80xx_write_packet(devpriv, cmd))
			break;
	}

	up(&devpriv->limit_sem);

	return n;
}

static int vmk80xx_find_usb_endpoints(struct comedi_device *dev)
{
	struct vmk80xx_private *devpriv = dev->private;
	struct usb_interface *intf = devpriv->intf;
	struct usb_host_interface *iface_desc = intf->cur_altsetting;
	struct usb_endpoint_descriptor *ep_desc;
	int i;

	if (iface_desc->desc.bNumEndpoints != 2)
		return -ENODEV;

	for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
		ep_desc = &iface_desc->endpoint[i].desc;

		if (usb_endpoint_is_int_in(ep_desc) ||
		    usb_endpoint_is_bulk_in(ep_desc)) {
			if (!devpriv->ep_rx)
				devpriv->ep_rx = ep_desc;
			continue;
		}

		if (usb_endpoint_is_int_out(ep_desc) ||
		    usb_endpoint_is_bulk_out(ep_desc)) {
			if (!devpriv->ep_tx)
				devpriv->ep_tx = ep_desc;
			continue;
		}
	}

	if (!devpriv->ep_rx || !devpriv->ep_tx)
		return -ENODEV;

	return 0;
}

static int vmk80xx_alloc_usb_buffers(struct comedi_device *dev)
{
	struct vmk80xx_private *devpriv = dev->private;
	size_t size;

	size = le16_to_cpu(devpriv->ep_rx->wMaxPacketSize);
	devpriv->usb_rx_buf = kmalloc(size, GFP_KERNEL);
	if (!devpriv->usb_rx_buf)
		return -ENOMEM;

	size = le16_to_cpu(devpriv->ep_tx->wMaxPacketSize);
	devpriv->usb_tx_buf = kmalloc(size, GFP_KERNEL);
	if (!devpriv->usb_tx_buf) {
		kfree(devpriv->usb_rx_buf);
		return -ENOMEM;
	}

	return 0;
}

static int vmk80xx_attach_common(struct comedi_device *dev)
{
	const struct vmk80xx_board *boardinfo = comedi_board(dev);
	struct vmk80xx_private *devpriv = dev->private;
	struct comedi_subdevice *s;
	int n_subd;
	int ret;

	down(&devpriv->limit_sem);

	if (devpriv->model == VMK8055_MODEL)
		n_subd = 5;
	else
		n_subd = 6;
	ret = comedi_alloc_subdevices(dev, n_subd);
	if (ret) {
		up(&devpriv->limit_sem);
		return ret;
	}

	/* Analog input subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND;
	s->n_chan	= boardinfo->ai_nchans;
	s->maxdata	= boardinfo->ai_maxdata;
	s->range_table	= boardinfo->range;
	s->insn_read	= vmk80xx_ai_insn_read;

	/* Analog output subdevice */
	s = &dev->subdevices[1];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITEABLE | SDF_GROUND;
	s->n_chan	= boardinfo->ao_nchans;
	s->maxdata	= 0x00ff;
	s->range_table	= boardinfo->range;
	s->insn_write	= vmk80xx_ao_insn_write;
	if (devpriv->model == VMK8061_MODEL) {
		s->subdev_flags	|= SDF_READABLE;
		s->insn_read	= vmk80xx_ao_insn_read;
	}

	/* Digital input subdevice */
	s = &dev->subdevices[2];
	s->type		= COMEDI_SUBD_DI;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= boardinfo->di_nchans;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_read	= vmk80xx_di_insn_read;
	s->insn_bits	= vmk80xx_di_insn_bits;

	/* Digital output subdevice */
	s = &dev->subdevices[3];
	s->type		= COMEDI_SUBD_DO;
	s->subdev_flags	= SDF_WRITEABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_write	= vmk80xx_do_insn_write;
	s->insn_bits	= vmk80xx_do_insn_bits;

	/* Counter subdevice */
	s = &dev->subdevices[4];
	s->type		= COMEDI_SUBD_COUNTER;
	s->subdev_flags	= SDF_READABLE;
	s->n_chan	= 2;
	s->maxdata	= boardinfo->cnt_maxdata;
	s->insn_read	= vmk80xx_cnt_insn_read;
	s->insn_config	= vmk80xx_cnt_insn_config;
	if (devpriv->model == VMK8055_MODEL) {
		s->subdev_flags	|= SDF_WRITEABLE;
		s->insn_write	= vmk80xx_cnt_insn_write;
	}

	/* PWM subdevice */
	if (devpriv->model == VMK8061_MODEL) {
		s = &dev->subdevices[5];
		s->type		= COMEDI_SUBD_PWM;
		s->subdev_flags	= SDF_READABLE | SDF_WRITEABLE;
		s->n_chan	= boardinfo->pwm_nchans;
		s->maxdata	= boardinfo->pwm_maxdata;
		s->insn_read	= vmk80xx_pwm_insn_read;
		s->insn_write	= vmk80xx_pwm_insn_write;
	}

	up(&devpriv->limit_sem);

	return 0;
}

static int vmk80xx_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct usb_interface *intf = comedi_to_usb_interface(dev);
	const struct vmk80xx_board *boardinfo;
	struct vmk80xx_private *devpriv;
	int ret;

	boardinfo = &vmk80xx_boardinfo[context];
	dev->board_ptr = boardinfo;
	dev->board_name = boardinfo->name;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	devpriv->usb = interface_to_usbdev(intf);
	devpriv->intf = intf;
	devpriv->model = boardinfo->model;

	ret = vmk80xx_find_usb_endpoints(dev);
	if (ret)
		return ret;

	ret = vmk80xx_alloc_usb_buffers(dev);
	if (ret)
		return ret;

	sema_init(&devpriv->limit_sem, 8);
	init_waitqueue_head(&devpriv->read_wait);
	init_waitqueue_head(&devpriv->write_wait);

	init_usb_anchor(&devpriv->rx_anchor);
	init_usb_anchor(&devpriv->tx_anchor);

	usb_set_intfdata(intf, devpriv);

	if (devpriv->model == VMK8061_MODEL) {
		vmk80xx_read_eeprom(devpriv, IC3_VERSION);
		dev_info(&intf->dev, "%s\n", devpriv->fw.ic3_vers);

		if (vmk80xx_check_data_link(devpriv)) {
			vmk80xx_read_eeprom(devpriv, IC6_VERSION);
			dev_info(&intf->dev, "%s\n", devpriv->fw.ic6_vers);
		} else {
			dbgcm("comedi#: vmk80xx: no conn. to CPU\n");
		}
	}

	if (devpriv->model == VMK8055_MODEL)
		vmk80xx_reset_device(devpriv);

	return vmk80xx_attach_common(dev);
}

static void vmk80xx_detach(struct comedi_device *dev)
{
	struct vmk80xx_private *devpriv = dev->private;

	if (!devpriv)
		return;

	down(&devpriv->limit_sem);

	usb_set_intfdata(devpriv->intf, NULL);

	usb_kill_anchored_urbs(&devpriv->rx_anchor);
	usb_kill_anchored_urbs(&devpriv->tx_anchor);

	kfree(devpriv->usb_rx_buf);
	kfree(devpriv->usb_tx_buf);

	up(&devpriv->limit_sem);
}

static struct comedi_driver vmk80xx_driver = {
	.module		= THIS_MODULE,
	.driver_name	= "vmk80xx",
	.auto_attach	= vmk80xx_auto_attach,
	.detach		= vmk80xx_detach,
};

static int vmk80xx_usb_probe(struct usb_interface *intf,
			     const struct usb_device_id *id)
{
	return comedi_usb_auto_config(intf, &vmk80xx_driver, id->driver_info);
}

static const struct usb_device_id vmk80xx_usb_id_table[] = {
	{ USB_DEVICE(0x10cf, 0x5500), .driver_info = DEVICE_VMK8055 },
	{ USB_DEVICE(0x10cf, 0x5501), .driver_info = DEVICE_VMK8055 },
	{ USB_DEVICE(0x10cf, 0x5502), .driver_info = DEVICE_VMK8055 },
	{ USB_DEVICE(0x10cf, 0x5503), .driver_info = DEVICE_VMK8055 },
	{ USB_DEVICE(0x10cf, 0x8061), .driver_info = DEVICE_VMK8061 },
	{ USB_DEVICE(0x10cf, 0x8062), .driver_info = DEVICE_VMK8061 },
	{ USB_DEVICE(0x10cf, 0x8063), .driver_info = DEVICE_VMK8061 },
	{ USB_DEVICE(0x10cf, 0x8064), .driver_info = DEVICE_VMK8061 },
	{ USB_DEVICE(0x10cf, 0x8065), .driver_info = DEVICE_VMK8061 },
	{ USB_DEVICE(0x10cf, 0x8066), .driver_info = DEVICE_VMK8061 },
	{ USB_DEVICE(0x10cf, 0x8067), .driver_info = DEVICE_VMK8061 },
	{ USB_DEVICE(0x10cf, 0x8068), .driver_info = DEVICE_VMK8061 },
	{ }
};
MODULE_DEVICE_TABLE(usb, vmk80xx_usb_id_table);

static struct usb_driver vmk80xx_usb_driver = {
	.name		= "vmk80xx",
	.id_table	= vmk80xx_usb_id_table,
	.probe		= vmk80xx_usb_probe,
	.disconnect	= comedi_usb_auto_unconfig,
};
module_comedi_usb_driver(vmk80xx_driver, vmk80xx_usb_driver);

MODULE_AUTHOR("Manuel Gebele <forensixs@gmx.de>");
MODULE_DESCRIPTION("Velleman USB Board Low-Level Driver");
MODULE_SUPPORTED_DEVICE("K8055/K8061 aka VM110/VM140");
MODULE_VERSION("0.8.01");
MODULE_LICENSE("GPL");
