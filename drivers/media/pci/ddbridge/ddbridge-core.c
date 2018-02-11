/*
 * ddbridge-core.c: Digital Devices bridge core functions
 *
 * Copyright (C) 2010-2017 Digital Devices GmbH
 *                         Marcus Metzler <mocm@metzlerbros.de>
 *                         Ralph Metzler <rjkm@metzlerbros.de>
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * To obtain the license, point your browser to
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/timer.h>
#include <linux/i2c.h>
#include <linux/swab.h>
#include <linux/vmalloc.h>

#include "ddbridge.h"
#include "ddbridge-i2c.h"
#include "ddbridge-regs.h"
#include "ddbridge-max.h"
#include "ddbridge-ci.h"
#include "ddbridge-io.h"

#include "tda18271c2dd.h"
#include "stv6110x.h"
#include "stv090x.h"
#include "lnbh24.h"
#include "drxk.h"
#include "stv0367.h"
#include "stv0367_priv.h"
#include "cxd2841er.h"
#include "tda18212.h"
#include "stv0910.h"
#include "stv6111.h"
#include "lnbh25.h"
#include "cxd2099.h"

/****************************************************************************/

#define DDB_MAX_ADAPTER 64

/****************************************************************************/

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static int adapter_alloc;
module_param(adapter_alloc, int, 0444);
MODULE_PARM_DESC(adapter_alloc,
		 "0-one adapter per io, 1-one per tab with io, 2-one per tab, 3-one for all");

/****************************************************************************/

static DEFINE_MUTEX(redirect_lock);

struct workqueue_struct *ddb_wq;

static struct ddb *ddbs[DDB_MAX_ADAPTER];

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static void ddb_set_dma_table(struct ddb_io *io)
{
	struct ddb *dev = io->port->dev;
	struct ddb_dma *dma = io->dma;
	u32 i;
	u64 mem;

	if (!dma)
		return;
	for (i = 0; i < dma->num; i++) {
		mem = dma->pbuf[i];
		ddbwritel(dev, mem & 0xffffffff, dma->bufregs + i * 8);
		ddbwritel(dev, mem >> 32, dma->bufregs + i * 8 + 4);
	}
	dma->bufval = ((dma->div & 0x0f) << 16) |
		((dma->num & 0x1f) << 11) |
		((dma->size >> 7) & 0x7ff);
}

static void ddb_set_dma_tables(struct ddb *dev)
{
	u32 i;

	for (i = 0; i < DDB_MAX_PORT; i++) {
		if (dev->port[i].input[0])
			ddb_set_dma_table(dev->port[i].input[0]);
		if (dev->port[i].input[1])
			ddb_set_dma_table(dev->port[i].input[1]);
		if (dev->port[i].output)
			ddb_set_dma_table(dev->port[i].output);
	}
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static void ddb_redirect_dma(struct ddb *dev,
			     struct ddb_dma *sdma,
			     struct ddb_dma *ddma)
{
	u32 i, base;
	u64 mem;

	sdma->bufval = ddma->bufval;
	base = sdma->bufregs;
	for (i = 0; i < ddma->num; i++) {
		mem = ddma->pbuf[i];
		ddbwritel(dev, mem & 0xffffffff, base + i * 8);
		ddbwritel(dev, mem >> 32, base + i * 8 + 4);
	}
}

static int ddb_unredirect(struct ddb_port *port)
{
	struct ddb_input *oredi, *iredi = NULL;
	struct ddb_output *iredo = NULL;

	/* dev_info(port->dev->dev,
	 * "unredirect %d.%d\n", port->dev->nr, port->nr);
	 */
	mutex_lock(&redirect_lock);
	if (port->output->dma->running) {
		mutex_unlock(&redirect_lock);
		return -EBUSY;
	}
	oredi = port->output->redi;
	if (!oredi)
		goto done;
	if (port->input[0]) {
		iredi = port->input[0]->redi;
		iredo = port->input[0]->redo;

		if (iredo) {
			iredo->port->output->redi = oredi;
			if (iredo->port->input[0]) {
				iredo->port->input[0]->redi = iredi;
				ddb_redirect_dma(oredi->port->dev,
						 oredi->dma, iredo->dma);
			}
			port->input[0]->redo = NULL;
			ddb_set_dma_table(port->input[0]);
		}
		oredi->redi = iredi;
		port->input[0]->redi = NULL;
	}
	oredi->redo = NULL;
	port->output->redi = NULL;

	ddb_set_dma_table(oredi);
done:
	mutex_unlock(&redirect_lock);
	return 0;
}

static int ddb_redirect(u32 i, u32 p)
{
	struct ddb *idev = ddbs[(i >> 4) & 0x3f];
	struct ddb_input *input, *input2;
	struct ddb *pdev = ddbs[(p >> 4) & 0x3f];
	struct ddb_port *port;

	if (!idev || !pdev)
		return -EINVAL;
	if (!idev->has_dma || !pdev->has_dma)
		return -EINVAL;

	port = &pdev->port[p & 0x0f];
	if (!port->output)
		return -EINVAL;
	if (ddb_unredirect(port))
		return -EBUSY;

	if (i == 8)
		return 0;

	input = &idev->input[i & 7];
	if (!input)
		return -EINVAL;

	mutex_lock(&redirect_lock);
	if (port->output->dma->running || input->dma->running) {
		mutex_unlock(&redirect_lock);
		return -EBUSY;
	}
	input2 = port->input[0];
	if (input2) {
		if (input->redi) {
			input2->redi = input->redi;
			input->redi = NULL;
		} else {
			input2->redi = input;
		}
	}
	input->redo = port->output;
	port->output->redi = input;

	ddb_redirect_dma(input->port->dev, input->dma, port->output->dma);
	mutex_unlock(&redirect_lock);
	return 0;
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static void dma_free(struct pci_dev *pdev, struct ddb_dma *dma, int dir)
{
	int i;

	if (!dma)
		return;
	for (i = 0; i < dma->num; i++) {
		if (dma->vbuf[i]) {
			if (alt_dma) {
				dma_unmap_single(&pdev->dev, dma->pbuf[i],
						 dma->size,
						 dir ? DMA_TO_DEVICE :
						 DMA_FROM_DEVICE);
				kfree(dma->vbuf[i]);
				dma->vbuf[i] = NULL;
			} else {
				dma_free_coherent(&pdev->dev, dma->size,
						  dma->vbuf[i], dma->pbuf[i]);
			}

			dma->vbuf[i] = NULL;
		}
	}
}

static int dma_alloc(struct pci_dev *pdev, struct ddb_dma *dma, int dir)
{
	int i;

	if (!dma)
		return 0;
	for (i = 0; i < dma->num; i++) {
		if (alt_dma) {
			dma->vbuf[i] = kmalloc(dma->size, __GFP_RETRY_MAYFAIL);
			if (!dma->vbuf[i])
				return -ENOMEM;
			dma->pbuf[i] = dma_map_single(&pdev->dev,
						      dma->vbuf[i],
						      dma->size,
						      dir ? DMA_TO_DEVICE :
						      DMA_FROM_DEVICE);
			if (dma_mapping_error(&pdev->dev, dma->pbuf[i])) {
				kfree(dma->vbuf[i]);
				dma->vbuf[i] = NULL;
				return -ENOMEM;
			}
		} else {
			dma->vbuf[i] = dma_alloc_coherent(&pdev->dev,
							  dma->size,
							  &dma->pbuf[i],
							  GFP_KERNEL);
			if (!dma->vbuf[i])
				return -ENOMEM;
		}
	}
	return 0;
}

int ddb_buffers_alloc(struct ddb *dev)
{
	int i;
	struct ddb_port *port;

	for (i = 0; i < dev->port_num; i++) {
		port = &dev->port[i];
		switch (port->class) {
		case DDB_PORT_TUNER:
			if (port->input[0]->dma)
				if (dma_alloc(dev->pdev, port->input[0]->dma, 0)
					< 0)
					return -1;
			if (port->input[1]->dma)
				if (dma_alloc(dev->pdev, port->input[1]->dma, 0)
					< 0)
					return -1;
			break;
		case DDB_PORT_CI:
		case DDB_PORT_LOOP:
			if (port->input[0]->dma)
				if (dma_alloc(dev->pdev, port->input[0]->dma, 0)
					< 0)
					return -1;
			if (port->output->dma)
				if (dma_alloc(dev->pdev, port->output->dma, 1)
					< 0)
					return -1;
			break;
		default:
			break;
		}
	}
	ddb_set_dma_tables(dev);
	return 0;
}

void ddb_buffers_free(struct ddb *dev)
{
	int i;
	struct ddb_port *port;

	for (i = 0; i < dev->port_num; i++) {
		port = &dev->port[i];

		if (port->input[0] && port->input[0]->dma)
			dma_free(dev->pdev, port->input[0]->dma, 0);
		if (port->input[1] && port->input[1]->dma)
			dma_free(dev->pdev, port->input[1]->dma, 0);
		if (port->output && port->output->dma)
			dma_free(dev->pdev, port->output->dma, 1);
	}
}

static void calc_con(struct ddb_output *output, u32 *con, u32 *con2, u32 flags)
{
	struct ddb *dev = output->port->dev;
	u32 bitrate = output->port->obr, max_bitrate = 72000;
	u32 gap = 4, nco = 0;

	*con = 0x1c;
	if (output->port->gap != 0xffffffff) {
		flags |= 1;
		gap = output->port->gap;
		max_bitrate = 0;
	}
	if (dev->link[0].info->type == DDB_OCTOPUS_CI && output->port->nr > 1) {
		*con = 0x10c;
		if (dev->link[0].ids.regmapid >= 0x10003 && !(flags & 1)) {
			if (!(flags & 2)) {
				/* NCO */
				max_bitrate = 0;
				gap = 0;
				if (bitrate != 72000) {
					if (bitrate >= 96000) {
						*con |= 0x800;
					} else {
						*con |= 0x1000;
						nco = (bitrate * 8192 + 71999)
							/ 72000;
					}
				}
			} else {
				/* Divider and gap */
				*con |= 0x1810;
				if (bitrate <= 64000) {
					max_bitrate = 64000;
					nco = 8;
				} else if (bitrate <= 72000) {
					max_bitrate = 72000;
					nco = 7;
				} else {
					max_bitrate = 96000;
					nco = 5;
				}
			}
		} else {
			if (bitrate > 72000) {
				*con |= 0x810; /* 96 MBit/s and gap */
				max_bitrate = 96000;
			}
			*con |= 0x10; /* enable gap */
		}
	}
	if (max_bitrate > 0) {
		if (bitrate > max_bitrate)
			bitrate = max_bitrate;
		if (bitrate < 31000)
			bitrate = 31000;
		gap = ((max_bitrate - bitrate) * 94) / bitrate;
		if (gap < 2)
			*con &= ~0x10; /* Disable gap */
		else
			gap -= 2;
		if (gap > 127)
			gap = 127;
	}

	*con2 = (nco << 16) | gap;
}

static void ddb_output_start(struct ddb_output *output)
{
	struct ddb *dev = output->port->dev;
	u32 con = 0x11c, con2 = 0;

	if (output->dma) {
		spin_lock_irq(&output->dma->lock);
		output->dma->cbuf = 0;
		output->dma->coff = 0;
		output->dma->stat = 0;
		ddbwritel(dev, 0, DMA_BUFFER_CONTROL(output->dma));
	}

	if (output->port->input[0]->port->class == DDB_PORT_LOOP)
		con = (1UL << 13) | 0x14;
	else
		calc_con(output, &con, &con2, 0);

	ddbwritel(dev, 0, TS_CONTROL(output));
	ddbwritel(dev, 2, TS_CONTROL(output));
	ddbwritel(dev, 0, TS_CONTROL(output));
	ddbwritel(dev, con, TS_CONTROL(output));
	ddbwritel(dev, con2, TS_CONTROL2(output));

	if (output->dma) {
		ddbwritel(dev, output->dma->bufval,
			  DMA_BUFFER_SIZE(output->dma));
		ddbwritel(dev, 0, DMA_BUFFER_ACK(output->dma));
		ddbwritel(dev, 1, DMA_BASE_READ);
		ddbwritel(dev, 7, DMA_BUFFER_CONTROL(output->dma));
	}

	ddbwritel(dev, con | 1, TS_CONTROL(output));

	if (output->dma) {
		output->dma->running = 1;
		spin_unlock_irq(&output->dma->lock);
	}
}

static void ddb_output_stop(struct ddb_output *output)
{
	struct ddb *dev = output->port->dev;

	if (output->dma)
		spin_lock_irq(&output->dma->lock);

	ddbwritel(dev, 0, TS_CONTROL(output));

	if (output->dma) {
		ddbwritel(dev, 0, DMA_BUFFER_CONTROL(output->dma));
		output->dma->running = 0;
		spin_unlock_irq(&output->dma->lock);
	}
}

static void ddb_input_stop(struct ddb_input *input)
{
	struct ddb *dev = input->port->dev;
	u32 tag = DDB_LINK_TAG(input->port->lnr);

	if (input->dma)
		spin_lock_irq(&input->dma->lock);
	ddbwritel(dev, 0, tag | TS_CONTROL(input));
	if (input->dma) {
		ddbwritel(dev, 0, DMA_BUFFER_CONTROL(input->dma));
		input->dma->running = 0;
		spin_unlock_irq(&input->dma->lock);
	}
}

static void ddb_input_start(struct ddb_input *input)
{
	struct ddb *dev = input->port->dev;

	if (input->dma) {
		spin_lock_irq(&input->dma->lock);
		input->dma->cbuf = 0;
		input->dma->coff = 0;
		input->dma->stat = 0;
		ddbwritel(dev, 0, DMA_BUFFER_CONTROL(input->dma));
	}
	ddbwritel(dev, 0, TS_CONTROL(input));
	ddbwritel(dev, 2, TS_CONTROL(input));
	ddbwritel(dev, 0, TS_CONTROL(input));

	if (input->dma) {
		ddbwritel(dev, input->dma->bufval,
			  DMA_BUFFER_SIZE(input->dma));
		ddbwritel(dev, 0, DMA_BUFFER_ACK(input->dma));
		ddbwritel(dev, 1, DMA_BASE_WRITE);
		ddbwritel(dev, 3, DMA_BUFFER_CONTROL(input->dma));
	}

	ddbwritel(dev, 0x09, TS_CONTROL(input));

	if (input->dma) {
		input->dma->running = 1;
		spin_unlock_irq(&input->dma->lock);
	}
}

static void ddb_input_start_all(struct ddb_input *input)
{
	struct ddb_input *i = input;
	struct ddb_output *o;

	mutex_lock(&redirect_lock);
	while (i && (o = i->redo)) {
		ddb_output_start(o);
		i = o->port->input[0];
		if (i)
			ddb_input_start(i);
	}
	ddb_input_start(input);
	mutex_unlock(&redirect_lock);
}

static void ddb_input_stop_all(struct ddb_input *input)
{
	struct ddb_input *i = input;
	struct ddb_output *o;

	mutex_lock(&redirect_lock);
	ddb_input_stop(input);
	while (i && (o = i->redo)) {
		ddb_output_stop(o);
		i = o->port->input[0];
		if (i)
			ddb_input_stop(i);
	}
	mutex_unlock(&redirect_lock);
}

static u32 ddb_output_free(struct ddb_output *output)
{
	u32 idx, off, stat = output->dma->stat;
	s32 diff;

	idx = (stat >> 11) & 0x1f;
	off = (stat & 0x7ff) << 7;

	if (output->dma->cbuf != idx) {
		if ((((output->dma->cbuf + 1) % output->dma->num) == idx) &&
		    (output->dma->size - output->dma->coff <= 188))
			return 0;
		return 188;
	}
	diff = off - output->dma->coff;
	if (diff <= 0 || diff > 188)
		return 188;
	return 0;
}

static ssize_t ddb_output_write(struct ddb_output *output,
				const __user u8 *buf, size_t count)
{
	struct ddb *dev = output->port->dev;
	u32 idx, off, stat = output->dma->stat;
	u32 left = count, len;

	idx = (stat >> 11) & 0x1f;
	off = (stat & 0x7ff) << 7;

	while (left) {
		len = output->dma->size - output->dma->coff;
		if ((((output->dma->cbuf + 1) % output->dma->num) == idx) &&
		    off == 0) {
			if (len <= 188)
				break;
			len -= 188;
		}
		if (output->dma->cbuf == idx) {
			if (off > output->dma->coff) {
				len = off - output->dma->coff;
				len -= (len % 188);
				if (len <= 188)
					break;
				len -= 188;
			}
		}
		if (len > left)
			len = left;
		if (copy_from_user(output->dma->vbuf[output->dma->cbuf] +
				   output->dma->coff,
				   buf, len))
			return -EIO;
		if (alt_dma)
			dma_sync_single_for_device(
				dev->dev,
				output->dma->pbuf[output->dma->cbuf],
				output->dma->size, DMA_TO_DEVICE);
		left -= len;
		buf += len;
		output->dma->coff += len;
		if (output->dma->coff == output->dma->size) {
			output->dma->coff = 0;
			output->dma->cbuf = ((output->dma->cbuf + 1) %
					     output->dma->num);
		}
		ddbwritel(dev,
			  (output->dma->cbuf << 11) |
			  (output->dma->coff >> 7),
			  DMA_BUFFER_ACK(output->dma));
	}
	return count - left;
}

static u32 ddb_input_avail(struct ddb_input *input)
{
	struct ddb *dev = input->port->dev;
	u32 idx, off, stat = input->dma->stat;
	u32 ctrl = ddbreadl(dev, DMA_BUFFER_CONTROL(input->dma));

	idx = (stat >> 11) & 0x1f;
	off = (stat & 0x7ff) << 7;

	if (ctrl & 4) {
		dev_err(dev->dev, "IA %d %d %08x\n", idx, off, ctrl);
		ddbwritel(dev, stat, DMA_BUFFER_ACK(input->dma));
		return 0;
	}
	if (input->dma->cbuf != idx)
		return 188;
	return 0;
}

static ssize_t ddb_input_read(struct ddb_input *input,
			      __user u8 *buf, size_t count)
{
	struct ddb *dev = input->port->dev;
	u32 left = count;
	u32 idx, free, stat = input->dma->stat;
	int ret;

	idx = (stat >> 11) & 0x1f;

	while (left) {
		if (input->dma->cbuf == idx)
			return count - left;
		free = input->dma->size - input->dma->coff;
		if (free > left)
			free = left;
		if (alt_dma)
			dma_sync_single_for_cpu(
				dev->dev,
				input->dma->pbuf[input->dma->cbuf],
				input->dma->size, DMA_FROM_DEVICE);
		ret = copy_to_user(buf, input->dma->vbuf[input->dma->cbuf] +
				   input->dma->coff, free);
		if (ret)
			return -EFAULT;
		input->dma->coff += free;
		if (input->dma->coff == input->dma->size) {
			input->dma->coff = 0;
			input->dma->cbuf = (input->dma->cbuf + 1) %
				input->dma->num;
		}
		left -= free;
		buf += free;
		ddbwritel(dev,
			  (input->dma->cbuf << 11) | (input->dma->coff >> 7),
			  DMA_BUFFER_ACK(input->dma));
	}
	return count;
}

/****************************************************************************/
/****************************************************************************/

static ssize_t ts_write(struct file *file, const __user char *buf,
			size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ddb_output *output = dvbdev->priv;
	struct ddb *dev = output->port->dev;
	size_t left = count;
	int stat;

	if (!dev->has_dma)
		return -EINVAL;
	while (left) {
		if (ddb_output_free(output) < 188) {
			if (file->f_flags & O_NONBLOCK)
				break;
			if (wait_event_interruptible(
				    output->dma->wq,
				    ddb_output_free(output) >= 188) < 0)
				break;
		}
		stat = ddb_output_write(output, buf, left);
		if (stat < 0)
			return stat;
		buf += stat;
		left -= stat;
	}
	return (left == count) ? -EAGAIN : (count - left);
}

static ssize_t ts_read(struct file *file, __user char *buf,
		       size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ddb_output *output = dvbdev->priv;
	struct ddb_input *input = output->port->input[0];
	struct ddb *dev = output->port->dev;
	size_t left = count;
	int stat;

	if (!dev->has_dma)
		return -EINVAL;
	while (left) {
		if (ddb_input_avail(input) < 188) {
			if (file->f_flags & O_NONBLOCK)
				break;
			if (wait_event_interruptible(
				    input->dma->wq,
				    ddb_input_avail(input) >= 188) < 0)
				break;
		}
		stat = ddb_input_read(input, buf, left);
		if (stat < 0)
			return stat;
		left -= stat;
		buf += stat;
	}
	return (count && (left == count)) ? -EAGAIN : (count - left);
}

static __poll_t ts_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ddb_output *output = dvbdev->priv;
	struct ddb_input *input = output->port->input[0];

	__poll_t mask = 0;

	poll_wait(file, &input->dma->wq, wait);
	poll_wait(file, &output->dma->wq, wait);
	if (ddb_input_avail(input) >= 188)
		mask |= POLLIN | POLLRDNORM;
	if (ddb_output_free(output) >= 188)
		mask |= POLLOUT | POLLWRNORM;
	return mask;
}

static int ts_release(struct inode *inode, struct file *file)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ddb_output *output = NULL;
	struct ddb_input *input = NULL;

	if (dvbdev) {
		output = dvbdev->priv;
		input = output->port->input[0];
	}

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		if (!input)
			return -EINVAL;
		ddb_input_stop(input);
	} else if ((file->f_flags & O_ACCMODE) == O_WRONLY) {
		if (!output)
			return -EINVAL;
		ddb_output_stop(output);
	}
	return dvb_generic_release(inode, file);
}

static int ts_open(struct inode *inode, struct file *file)
{
	int err;
	struct dvb_device *dvbdev = file->private_data;
	struct ddb_output *output = NULL;
	struct ddb_input *input = NULL;

	if (dvbdev) {
		output = dvbdev->priv;
		input = output->port->input[0];
	}

	if ((file->f_flags & O_ACCMODE) == O_RDONLY) {
		if (!input)
			return -EINVAL;
		if (input->redo || input->redi)
			return -EBUSY;
	} else if ((file->f_flags & O_ACCMODE) == O_WRONLY) {
		if (!output)
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	err = dvb_generic_open(inode, file);
	if (err < 0)
		return err;
	if ((file->f_flags & O_ACCMODE) == O_RDONLY)
		ddb_input_start(input);
	else if ((file->f_flags & O_ACCMODE) == O_WRONLY)
		ddb_output_start(output);
	return err;
}

static const struct file_operations ci_fops = {
	.owner   = THIS_MODULE,
	.read    = ts_read,
	.write   = ts_write,
	.open    = ts_open,
	.release = ts_release,
	.poll    = ts_poll,
	.mmap    = NULL,
};

static struct dvb_device dvbdev_ci = {
	.priv    = NULL,
	.readers = 1,
	.writers = 1,
	.users   = 2,
	.fops    = &ci_fops,
};

/****************************************************************************/
/****************************************************************************/

static int locked_gate_ctrl(struct dvb_frontend *fe, int enable)
{
	struct ddb_input *input = fe->sec_priv;
	struct ddb_port *port = input->port;
	struct ddb_dvb *dvb = &port->dvb[input->nr & 1];
	int status;

	if (enable) {
		mutex_lock(&port->i2c_gate_lock);
		status = dvb->i2c_gate_ctrl(fe, 1);
	} else {
		status = dvb->i2c_gate_ctrl(fe, 0);
		mutex_unlock(&port->i2c_gate_lock);
	}
	return status;
}

static int demod_attach_drxk(struct ddb_input *input)
{
	struct i2c_adapter *i2c = &input->port->i2c->adap;
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];
	struct device *dev = input->port->dev->dev;
	struct drxk_config config;

	memset(&config, 0, sizeof(config));
	config.adr = 0x29 + (input->nr & 1);
	config.microcode_name = "drxk_a3.mc";

	dvb->fe = dvb_attach(drxk_attach, &config, i2c);
	if (!dvb->fe) {
		dev_err(dev, "No DRXK found!\n");
		return -ENODEV;
	}
	dvb->fe->sec_priv = input;
	dvb->i2c_gate_ctrl = dvb->fe->ops.i2c_gate_ctrl;
	dvb->fe->ops.i2c_gate_ctrl = locked_gate_ctrl;
	return 0;
}

static int tuner_attach_tda18271(struct ddb_input *input)
{
	struct i2c_adapter *i2c = &input->port->i2c->adap;
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];
	struct device *dev = input->port->dev->dev;
	struct dvb_frontend *fe;

	if (dvb->fe->ops.i2c_gate_ctrl)
		dvb->fe->ops.i2c_gate_ctrl(dvb->fe, 1);
	fe = dvb_attach(tda18271c2dd_attach, dvb->fe, i2c, 0x60);
	if (dvb->fe->ops.i2c_gate_ctrl)
		dvb->fe->ops.i2c_gate_ctrl(dvb->fe, 0);
	if (!fe) {
		dev_err(dev, "No TDA18271 found!\n");
		return -ENODEV;
	}
	return 0;
}

/******************************************************************************/
/******************************************************************************/
/******************************************************************************/

static struct stv0367_config ddb_stv0367_config[] = {
	{
		.demod_address = 0x1f,
		.xtal = 27000000,
		.if_khz = 0,
		.if_iq_mode = FE_TER_NORMAL_IF_TUNER,
		.ts_mode = STV0367_SERIAL_PUNCT_CLOCK,
		.clk_pol = STV0367_CLOCKPOLARITY_DEFAULT,
	}, {
		.demod_address = 0x1e,
		.xtal = 27000000,
		.if_khz = 0,
		.if_iq_mode = FE_TER_NORMAL_IF_TUNER,
		.ts_mode = STV0367_SERIAL_PUNCT_CLOCK,
		.clk_pol = STV0367_CLOCKPOLARITY_DEFAULT,
	},
};

static int demod_attach_stv0367(struct ddb_input *input)
{
	struct i2c_adapter *i2c = &input->port->i2c->adap;
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];
	struct device *dev = input->port->dev->dev;

	/* attach frontend */
	dvb->fe = dvb_attach(stv0367ddb_attach,
			     &ddb_stv0367_config[(input->nr & 1)], i2c);

	if (!dvb->fe) {
		dev_err(dev, "No stv0367 found!\n");
		return -ENODEV;
	}
	dvb->fe->sec_priv = input;
	dvb->i2c_gate_ctrl = dvb->fe->ops.i2c_gate_ctrl;
	dvb->fe->ops.i2c_gate_ctrl = locked_gate_ctrl;
	return 0;
}

static int tuner_tda18212_ping(struct ddb_input *input, unsigned short adr)
{
	struct i2c_adapter *adapter = &input->port->i2c->adap;
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];
	struct device *dev = input->port->dev->dev;
	u8 tda_id[2];
	u8 subaddr = 0x00;

	dev_dbg(dev, "stv0367-tda18212 tuner ping\n");
	if (dvb->fe->ops.i2c_gate_ctrl)
		dvb->fe->ops.i2c_gate_ctrl(dvb->fe, 1);

	if (i2c_read_regs(adapter, adr, subaddr, tda_id, sizeof(tda_id)) < 0)
		dev_dbg(dev, "tda18212 ping 1 fail\n");
	if (i2c_read_regs(adapter, adr, subaddr, tda_id, sizeof(tda_id)) < 0)
		dev_warn(dev, "tda18212 ping failed, expect problems\n");

	if (dvb->fe->ops.i2c_gate_ctrl)
		dvb->fe->ops.i2c_gate_ctrl(dvb->fe, 0);

	return 0;
}

static int demod_attach_cxd28xx(struct ddb_input *input, int par, int osc24)
{
	struct i2c_adapter *i2c = &input->port->i2c->adap;
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];
	struct device *dev = input->port->dev->dev;
	struct cxd2841er_config cfg;

	/* the cxd2841er driver expects 8bit/shifted I2C addresses */
	cfg.i2c_addr = ((input->nr & 1) ? 0x6d : 0x6c) << 1;

	cfg.xtal = osc24 ? SONY_XTAL_24000 : SONY_XTAL_20500;
	cfg.flags = CXD2841ER_AUTO_IFHZ | CXD2841ER_EARLY_TUNE |
		CXD2841ER_NO_WAIT_LOCK | CXD2841ER_NO_AGCNEG |
		CXD2841ER_TSBITS;

	if (!par)
		cfg.flags |= CXD2841ER_TS_SERIAL;

	/* attach frontend */
	dvb->fe = dvb_attach(cxd2841er_attach_t_c, &cfg, i2c);

	if (!dvb->fe) {
		dev_err(dev, "No cxd2837/38/43/54 found!\n");
		return -ENODEV;
	}
	dvb->fe->sec_priv = input;
	dvb->i2c_gate_ctrl = dvb->fe->ops.i2c_gate_ctrl;
	dvb->fe->ops.i2c_gate_ctrl = locked_gate_ctrl;
	return 0;
}

static int tuner_attach_tda18212(struct ddb_input *input, u32 porttype)
{
	struct i2c_adapter *adapter = &input->port->i2c->adap;
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];
	struct device *dev = input->port->dev->dev;
	struct i2c_client *client;
	struct tda18212_config config = {
		.fe = dvb->fe,
		.if_dvbt_6 = 3550,
		.if_dvbt_7 = 3700,
		.if_dvbt_8 = 4150,
		.if_dvbt2_6 = 3250,
		.if_dvbt2_7 = 4000,
		.if_dvbt2_8 = 4000,
		.if_dvbc = 5000,
	};
	struct i2c_board_info board_info = {
		.type = "tda18212",
		.platform_data = &config,
	};

	if (input->nr & 1)
		board_info.addr = 0x63;
	else
		board_info.addr = 0x60;

	/* due to a hardware quirk with the I2C gate on the stv0367+tda18212
	 * combo, the tda18212 must be probed by reading it's id _twice_ when
	 * cold started, or it very likely will fail.
	 */
	if (porttype == DDB_TUNER_DVBCT_ST)
		tuner_tda18212_ping(input, board_info.addr);

	request_module(board_info.type);

	/* perform tuner init/attach */
	client = i2c_new_device(adapter, &board_info);
	if (!client || !client->dev.driver)
		goto err;

	if (!try_module_get(client->dev.driver->owner)) {
		i2c_unregister_device(client);
		goto err;
	}

	dvb->i2c_client[0] = client;

	return 0;
err:
	dev_err(dev, "TDA18212 tuner not found. Device is not fully operational.\n");
	return -ENODEV;
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static struct stv090x_config stv0900 = {
	.device         = STV0900,
	.demod_mode     = STV090x_DUAL,
	.clk_mode       = STV090x_CLK_EXT,

	.xtal           = 27000000,
	.address        = 0x69,

	.ts1_mode       = STV090x_TSMODE_SERIAL_PUNCTURED,
	.ts2_mode       = STV090x_TSMODE_SERIAL_PUNCTURED,

	.ts1_tei        = 1,
	.ts2_tei        = 1,

	.repeater_level = STV090x_RPTLEVEL_16,

	.adc1_range	= STV090x_ADC_1Vpp,
	.adc2_range	= STV090x_ADC_1Vpp,

	.diseqc_envelope_mode = true,
};

static struct stv090x_config stv0900_aa = {
	.device         = STV0900,
	.demod_mode     = STV090x_DUAL,
	.clk_mode       = STV090x_CLK_EXT,

	.xtal           = 27000000,
	.address        = 0x68,

	.ts1_mode       = STV090x_TSMODE_SERIAL_PUNCTURED,
	.ts2_mode       = STV090x_TSMODE_SERIAL_PUNCTURED,

	.ts1_tei        = 1,
	.ts2_tei        = 1,

	.repeater_level = STV090x_RPTLEVEL_16,

	.adc1_range	= STV090x_ADC_1Vpp,
	.adc2_range	= STV090x_ADC_1Vpp,

	.diseqc_envelope_mode = true,
};

static struct stv6110x_config stv6110a = {
	.addr    = 0x60,
	.refclk	 = 27000000,
	.clk_div = 1,
};

static struct stv6110x_config stv6110b = {
	.addr    = 0x63,
	.refclk	 = 27000000,
	.clk_div = 1,
};

static int demod_attach_stv0900(struct ddb_input *input, int type)
{
	struct i2c_adapter *i2c = &input->port->i2c->adap;
	struct stv090x_config *feconf = type ? &stv0900_aa : &stv0900;
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];
	struct device *dev = input->port->dev->dev;

	dvb->fe = dvb_attach(stv090x_attach, feconf, i2c,
			     (input->nr & 1) ? STV090x_DEMODULATOR_1
			     : STV090x_DEMODULATOR_0);
	if (!dvb->fe) {
		dev_err(dev, "No STV0900 found!\n");
		return -ENODEV;
	}
	if (!dvb_attach(lnbh24_attach, dvb->fe, i2c, 0,
			0, (input->nr & 1) ?
			(0x09 - type) : (0x0b - type))) {
		dev_err(dev, "No LNBH24 found!\n");
		dvb_frontend_detach(dvb->fe);
		return -ENODEV;
	}
	return 0;
}

static int tuner_attach_stv6110(struct ddb_input *input, int type)
{
	struct i2c_adapter *i2c = &input->port->i2c->adap;
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];
	struct device *dev = input->port->dev->dev;
	struct stv090x_config *feconf = type ? &stv0900_aa : &stv0900;
	struct stv6110x_config *tunerconf = (input->nr & 1) ?
		&stv6110b : &stv6110a;
	const struct stv6110x_devctl *ctl;

	ctl = dvb_attach(stv6110x_attach, dvb->fe, tunerconf, i2c);
	if (!ctl) {
		dev_err(dev, "No STV6110X found!\n");
		return -ENODEV;
	}
	dev_info(dev, "attach tuner input %d adr %02x\n",
		 input->nr, tunerconf->addr);

	feconf->tuner_init          = ctl->tuner_init;
	feconf->tuner_sleep         = ctl->tuner_sleep;
	feconf->tuner_set_mode      = ctl->tuner_set_mode;
	feconf->tuner_set_frequency = ctl->tuner_set_frequency;
	feconf->tuner_get_frequency = ctl->tuner_get_frequency;
	feconf->tuner_set_bandwidth = ctl->tuner_set_bandwidth;
	feconf->tuner_get_bandwidth = ctl->tuner_get_bandwidth;
	feconf->tuner_set_bbgain    = ctl->tuner_set_bbgain;
	feconf->tuner_get_bbgain    = ctl->tuner_get_bbgain;
	feconf->tuner_set_refclk    = ctl->tuner_set_refclk;
	feconf->tuner_get_status    = ctl->tuner_get_status;

	return 0;
}

static const struct stv0910_cfg stv0910_p = {
	.adr      = 0x68,
	.parallel = 1,
	.rptlvl   = 4,
	.clk      = 30000000,
};

static const struct lnbh25_config lnbh25_cfg = {
	.i2c_address = 0x0c << 1,
	.data2_config = LNBH25_TEN
};

static int demod_attach_stv0910(struct ddb_input *input, int type)
{
	struct i2c_adapter *i2c = &input->port->i2c->adap;
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];
	struct device *dev = input->port->dev->dev;
	struct stv0910_cfg cfg = stv0910_p;
	struct lnbh25_config lnbcfg = lnbh25_cfg;

	if (stv0910_single)
		cfg.single = 1;

	if (type)
		cfg.parallel = 2;
	dvb->fe = dvb_attach(stv0910_attach, i2c, &cfg, (input->nr & 1));
	if (!dvb->fe) {
		cfg.adr = 0x6c;
		dvb->fe = dvb_attach(stv0910_attach, i2c,
				     &cfg, (input->nr & 1));
	}
	if (!dvb->fe) {
		dev_err(dev, "No STV0910 found!\n");
		return -ENODEV;
	}

	/* attach lnbh25 - leftshift by one as the lnbh25 driver expects 8bit
	 * i2c addresses
	 */
	lnbcfg.i2c_address = (((input->nr & 1) ? 0x0d : 0x0c) << 1);
	if (!dvb_attach(lnbh25_attach, dvb->fe, &lnbcfg, i2c)) {
		lnbcfg.i2c_address = (((input->nr & 1) ? 0x09 : 0x08) << 1);
		if (!dvb_attach(lnbh25_attach, dvb->fe, &lnbcfg, i2c)) {
			dev_err(dev, "No LNBH25 found!\n");
			dvb_frontend_detach(dvb->fe);
			return -ENODEV;
		}
	}

	return 0;
}

static int tuner_attach_stv6111(struct ddb_input *input, int type)
{
	struct i2c_adapter *i2c = &input->port->i2c->adap;
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];
	struct device *dev = input->port->dev->dev;
	struct dvb_frontend *fe;
	u8 adr = (type ? 0 : 4) + ((input->nr & 1) ? 0x63 : 0x60);

	fe = dvb_attach(stv6111_attach, dvb->fe, i2c, adr);
	if (!fe) {
		fe = dvb_attach(stv6111_attach, dvb->fe, i2c, adr & ~4);
		if (!fe) {
			dev_err(dev, "No STV6111 found at 0x%02x!\n", adr);
			return -ENODEV;
		}
	}
	return 0;
}

static int start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct ddb_input *input = dvbdmx->priv;
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];

	if (!dvb->users)
		ddb_input_start_all(input);

	return ++dvb->users;
}

static int stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct ddb_input *input = dvbdmx->priv;
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];

	if (--dvb->users)
		return dvb->users;

	ddb_input_stop_all(input);
	return 0;
}

static void dvb_input_detach(struct ddb_input *input)
{
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];
	struct dvb_demux *dvbdemux = &dvb->demux;
	struct i2c_client *client;

	switch (dvb->attached) {
	case 0x31:
		if (dvb->fe2)
			dvb_unregister_frontend(dvb->fe2);
		if (dvb->fe)
			dvb_unregister_frontend(dvb->fe);
		/* fallthrough */
	case 0x30:
		client = dvb->i2c_client[0];
		if (client) {
			module_put(client->dev.driver->owner);
			i2c_unregister_device(client);
			dvb->i2c_client[0] = NULL;
			client = NULL;
		}

		if (dvb->fe2)
			dvb_frontend_detach(dvb->fe2);
		if (dvb->fe)
			dvb_frontend_detach(dvb->fe);
		dvb->fe = NULL;
		dvb->fe2 = NULL;
		/* fallthrough */
	case 0x20:
		dvb_net_release(&dvb->dvbnet);
		/* fallthrough */
	case 0x12:
		dvbdemux->dmx.remove_frontend(&dvbdemux->dmx,
					      &dvb->hw_frontend);
		dvbdemux->dmx.remove_frontend(&dvbdemux->dmx,
					      &dvb->mem_frontend);
		/* fallthrough */
	case 0x11:
		dvb_dmxdev_release(&dvb->dmxdev);
		/* fallthrough */
	case 0x10:
		dvb_dmx_release(&dvb->demux);
		/* fallthrough */
	case 0x01:
		break;
	}
	dvb->attached = 0x00;
}

static int dvb_register_adapters(struct ddb *dev)
{
	int i, ret = 0;
	struct ddb_port *port;
	struct dvb_adapter *adap;

	if (adapter_alloc == 3) {
		port = &dev->port[0];
		adap = port->dvb[0].adap;
		ret = dvb_register_adapter(adap, "DDBridge", THIS_MODULE,
					   port->dev->dev,
					   adapter_nr);
		if (ret < 0)
			return ret;
		port->dvb[0].adap_registered = 1;
		for (i = 0; i < dev->port_num; i++) {
			port = &dev->port[i];
			port->dvb[0].adap = adap;
			port->dvb[1].adap = adap;
		}
		return 0;
	}

	for (i = 0; i < dev->port_num; i++) {
		port = &dev->port[i];
		switch (port->class) {
		case DDB_PORT_TUNER:
			adap = port->dvb[0].adap;
			ret = dvb_register_adapter(adap, "DDBridge",
						   THIS_MODULE,
						   port->dev->dev,
						   adapter_nr);
			if (ret < 0)
				return ret;
			port->dvb[0].adap_registered = 1;

			if (adapter_alloc > 0) {
				port->dvb[1].adap = port->dvb[0].adap;
				break;
			}
			adap = port->dvb[1].adap;
			ret = dvb_register_adapter(adap, "DDBridge",
						   THIS_MODULE,
						   port->dev->dev,
						   adapter_nr);
			if (ret < 0)
				return ret;
			port->dvb[1].adap_registered = 1;
			break;

		case DDB_PORT_CI:
		case DDB_PORT_LOOP:
			adap = port->dvb[0].adap;
			ret = dvb_register_adapter(adap, "DDBridge",
						   THIS_MODULE,
						   port->dev->dev,
						   adapter_nr);
			if (ret < 0)
				return ret;
			port->dvb[0].adap_registered = 1;
			break;
		default:
			if (adapter_alloc < 2)
				break;
			adap = port->dvb[0].adap;
			ret = dvb_register_adapter(adap, "DDBridge",
						   THIS_MODULE,
						   port->dev->dev,
						   adapter_nr);
			if (ret < 0)
				return ret;
			port->dvb[0].adap_registered = 1;
			break;
		}
	}
	return ret;
}

static void dvb_unregister_adapters(struct ddb *dev)
{
	int i;
	struct ddb_port *port;
	struct ddb_dvb *dvb;

	for (i = 0; i < dev->link[0].info->port_num; i++) {
		port = &dev->port[i];

		dvb = &port->dvb[0];
		if (dvb->adap_registered)
			dvb_unregister_adapter(dvb->adap);
		dvb->adap_registered = 0;

		dvb = &port->dvb[1];
		if (dvb->adap_registered)
			dvb_unregister_adapter(dvb->adap);
		dvb->adap_registered = 0;
	}
}

static int dvb_input_attach(struct ddb_input *input)
{
	int ret = 0;
	struct ddb_dvb *dvb = &input->port->dvb[input->nr & 1];
	struct ddb_port *port = input->port;
	struct dvb_adapter *adap = dvb->adap;
	struct dvb_demux *dvbdemux = &dvb->demux;
	int par = 0, osc24 = 0;

	dvb->attached = 0x01;

	dvbdemux->priv = input;
	dvbdemux->dmx.capabilities = DMX_TS_FILTERING |
		DMX_SECTION_FILTERING | DMX_MEMORY_BASED_FILTERING;
	dvbdemux->start_feed = start_feed;
	dvbdemux->stop_feed = stop_feed;
	dvbdemux->filternum = 256;
	dvbdemux->feednum = 256;
	ret = dvb_dmx_init(dvbdemux);
	if (ret < 0)
		return ret;
	dvb->attached = 0x10;

	dvb->dmxdev.filternum = 256;
	dvb->dmxdev.demux = &dvbdemux->dmx;
	ret = dvb_dmxdev_init(&dvb->dmxdev, adap);
	if (ret < 0)
		goto err_detach;
	dvb->attached = 0x11;

	dvb->mem_frontend.source = DMX_MEMORY_FE;
	dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->mem_frontend);
	dvb->hw_frontend.source = DMX_FRONTEND_0;
	dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->hw_frontend);
	ret = dvbdemux->dmx.connect_frontend(&dvbdemux->dmx, &dvb->hw_frontend);
	if (ret < 0)
		goto err_detach;
	dvb->attached = 0x12;

	ret = dvb_net_init(adap, &dvb->dvbnet, dvb->dmxdev.demux);
	if (ret < 0)
		goto err_detach;
	dvb->attached = 0x20;

	dvb->fe = NULL;
	dvb->fe2 = NULL;
	switch (port->type) {
	case DDB_TUNER_MXL5XX:
		if (ddb_fe_attach_mxl5xx(input) < 0)
			goto err_detach;
		break;
	case DDB_TUNER_DVBS_ST:
		if (demod_attach_stv0900(input, 0) < 0)
			goto err_detach;
		if (tuner_attach_stv6110(input, 0) < 0)
			goto err_tuner;
		break;
	case DDB_TUNER_DVBS_ST_AA:
		if (demod_attach_stv0900(input, 1) < 0)
			goto err_detach;
		if (tuner_attach_stv6110(input, 1) < 0)
			goto err_tuner;
		break;
	case DDB_TUNER_DVBS_STV0910:
		if (demod_attach_stv0910(input, 0) < 0)
			goto err_detach;
		if (tuner_attach_stv6111(input, 0) < 0)
			goto err_tuner;
		break;
	case DDB_TUNER_DVBS_STV0910_PR:
		if (demod_attach_stv0910(input, 1) < 0)
			goto err_detach;
		if (tuner_attach_stv6111(input, 1) < 0)
			goto err_tuner;
		break;
	case DDB_TUNER_DVBS_STV0910_P:
		if (demod_attach_stv0910(input, 0) < 0)
			goto err_detach;
		if (tuner_attach_stv6111(input, 1) < 0)
			goto err_tuner;
		break;
	case DDB_TUNER_DVBCT_TR:
		if (demod_attach_drxk(input) < 0)
			goto err_detach;
		if (tuner_attach_tda18271(input) < 0)
			goto err_tuner;
		break;
	case DDB_TUNER_DVBCT_ST:
		if (demod_attach_stv0367(input) < 0)
			goto err_detach;
		if (tuner_attach_tda18212(input, port->type) < 0)
			goto err_tuner;
		break;
	case DDB_TUNER_DVBC2T2I_SONY_P:
		if (input->port->dev->link[input->port->lnr].info->ts_quirks &
		    TS_QUIRK_ALT_OSC)
			osc24 = 0;
		else
			osc24 = 1;
		/* fall-through */
	case DDB_TUNER_DVBCT2_SONY_P:
	case DDB_TUNER_DVBC2T2_SONY_P:
	case DDB_TUNER_ISDBT_SONY_P:
		if (input->port->dev->link[input->port->lnr].info->ts_quirks
			& TS_QUIRK_SERIAL)
			par = 0;
		else
			par = 1;
		if (demod_attach_cxd28xx(input, par, osc24) < 0)
			goto err_detach;
		if (tuner_attach_tda18212(input, port->type) < 0)
			goto err_tuner;
		break;
	case DDB_TUNER_DVBC2T2I_SONY:
		osc24 = 1;
		/* fall-through */
	case DDB_TUNER_DVBCT2_SONY:
	case DDB_TUNER_DVBC2T2_SONY:
	case DDB_TUNER_ISDBT_SONY:
		if (demod_attach_cxd28xx(input, 0, osc24) < 0)
			goto err_detach;
		if (tuner_attach_tda18212(input, port->type) < 0)
			goto err_tuner;
		break;
	default:
		return 0;
	}
	dvb->attached = 0x30;

	if (dvb->fe) {
		if (dvb_register_frontend(adap, dvb->fe) < 0)
			goto err_detach;

		if (dvb->fe2) {
			if (dvb_register_frontend(adap, dvb->fe2) < 0) {
				dvb_unregister_frontend(dvb->fe);
				goto err_detach;
			}
			dvb->fe2->tuner_priv = dvb->fe->tuner_priv;
			memcpy(&dvb->fe2->ops.tuner_ops,
			       &dvb->fe->ops.tuner_ops,
			       sizeof(struct dvb_tuner_ops));
		}
	}

	dvb->attached = 0x31;
	return 0;

err_tuner:
	dev_err(port->dev->dev, "tuner attach failed!\n");

	if (dvb->fe2)
		dvb_frontend_detach(dvb->fe2);
	if (dvb->fe)
		dvb_frontend_detach(dvb->fe);
err_detach:
	dvb_input_detach(input);

	/* return error from ret if set */
	if (ret < 0)
		return ret;

	return -ENODEV;
}

static int port_has_encti(struct ddb_port *port)
{
	struct device *dev = port->dev->dev;
	u8 val;
	int ret = i2c_read_reg(&port->i2c->adap, 0x20, 0, &val);

	if (!ret)
		dev_info(dev, "[0x20]=0x%02x\n", val);
	return ret ? 0 : 1;
}

static int port_has_cxd(struct ddb_port *port, u8 *type)
{
	u8 val;
	u8 probe[4] = { 0xe0, 0x00, 0x00, 0x00 }, data[4];
	struct i2c_msg msgs[2] = {{ .addr = 0x40,  .flags = 0,
				    .buf  = probe, .len   = 4 },
				  { .addr = 0x40,  .flags = I2C_M_RD,
				    .buf  = data,  .len   = 4 } };
	val = i2c_transfer(&port->i2c->adap, msgs, 2);
	if (val != 2)
		return 0;

	if (data[0] == 0x02 && data[1] == 0x2b && data[3] == 0x43)
		*type = 2;
	else
		*type = 1;
	return 1;
}

static int port_has_xo2(struct ddb_port *port, u8 *type, u8 *id)
{
	u8 probe[1] = { 0x00 }, data[4];

	if (i2c_io(&port->i2c->adap, 0x10, probe, 1, data, 4))
		return 0;
	if (data[0] == 'D' && data[1] == 'F') {
		*id = data[2];
		*type = 1;
		return 1;
	}
	if (data[0] == 'C' && data[1] == 'I') {
		*id = data[2];
		*type = 2;
		return 1;
	}
	return 0;
}

static int port_has_stv0900(struct ddb_port *port)
{
	u8 val;

	if (i2c_read_reg16(&port->i2c->adap, 0x69, 0xf100, &val) < 0)
		return 0;
	return 1;
}

static int port_has_stv0900_aa(struct ddb_port *port, u8 *id)
{
	if (i2c_read_reg16(&port->i2c->adap, 0x68, 0xf100, id) < 0)
		return 0;
	return 1;
}

static int port_has_drxks(struct ddb_port *port)
{
	u8 val;

	if (i2c_read(&port->i2c->adap, 0x29, &val) < 0)
		return 0;
	if (i2c_read(&port->i2c->adap, 0x2a, &val) < 0)
		return 0;
	return 1;
}

static int port_has_stv0367(struct ddb_port *port)
{
	u8 val;

	if (i2c_read_reg16(&port->i2c->adap, 0x1e, 0xf000, &val) < 0)
		return 0;
	if (val != 0x60)
		return 0;
	if (i2c_read_reg16(&port->i2c->adap, 0x1f, 0xf000, &val) < 0)
		return 0;
	if (val != 0x60)
		return 0;
	return 1;
}

static int init_xo2(struct ddb_port *port)
{
	struct i2c_adapter *i2c = &port->i2c->adap;
	struct ddb *dev = port->dev;
	u8 val, data[2];
	int res;

	res = i2c_read_regs(i2c, 0x10, 0x04, data, 2);
	if (res < 0)
		return res;

	if (data[0] != 0x01)  {
		dev_info(dev->dev, "Port %d: invalid XO2\n", port->nr);
		return -1;
	}

	i2c_read_reg(i2c, 0x10, 0x08, &val);
	if (val != 0) {
		i2c_write_reg(i2c, 0x10, 0x08, 0x00);
		msleep(100);
	}
	/* Enable tuner power, disable pll, reset demods */
	i2c_write_reg(i2c, 0x10, 0x08, 0x04);
	usleep_range(2000, 3000);
	/* Release demod resets */
	i2c_write_reg(i2c, 0x10, 0x08, 0x07);

	/* speed: 0=55,1=75,2=90,3=104 MBit/s */
	i2c_write_reg(i2c, 0x10, 0x09, xo2_speed);

	if (dev->link[port->lnr].info->con_clock) {
		dev_info(dev->dev, "Setting continuous clock for XO2\n");
		i2c_write_reg(i2c, 0x10, 0x0a, 0x03);
		i2c_write_reg(i2c, 0x10, 0x0b, 0x03);
	} else {
		i2c_write_reg(i2c, 0x10, 0x0a, 0x01);
		i2c_write_reg(i2c, 0x10, 0x0b, 0x01);
	}

	usleep_range(2000, 3000);
	/* Start XO2 PLL */
	i2c_write_reg(i2c, 0x10, 0x08, 0x87);

	return 0;
}

static int init_xo2_ci(struct ddb_port *port)
{
	struct i2c_adapter *i2c = &port->i2c->adap;
	struct ddb *dev = port->dev;
	u8 val, data[2];
	int res;

	res = i2c_read_regs(i2c, 0x10, 0x04, data, 2);
	if (res < 0)
		return res;

	if (data[0] > 1)  {
		dev_info(dev->dev, "Port %d: invalid XO2 CI %02x\n",
			 port->nr, data[0]);
		return -1;
	}
	dev_info(dev->dev, "Port %d: DuoFlex CI %u.%u\n",
		 port->nr, data[0], data[1]);

	i2c_read_reg(i2c, 0x10, 0x08, &val);
	if (val != 0) {
		i2c_write_reg(i2c, 0x10, 0x08, 0x00);
		msleep(100);
	}
	/* Enable both CI */
	i2c_write_reg(i2c, 0x10, 0x08, 3);
	usleep_range(2000, 3000);

	/* speed: 0=55,1=75,2=90,3=104 MBit/s */
	i2c_write_reg(i2c, 0x10, 0x09, 1);

	i2c_write_reg(i2c, 0x10, 0x08, 0x83);
	usleep_range(2000, 3000);

	if (dev->link[port->lnr].info->con_clock) {
		dev_info(dev->dev, "Setting continuous clock for DuoFlex CI\n");
		i2c_write_reg(i2c, 0x10, 0x0a, 0x03);
		i2c_write_reg(i2c, 0x10, 0x0b, 0x03);
	} else {
		i2c_write_reg(i2c, 0x10, 0x0a, 0x01);
		i2c_write_reg(i2c, 0x10, 0x0b, 0x01);
	}
	return 0;
}

static int port_has_cxd28xx(struct ddb_port *port, u8 *id)
{
	struct i2c_adapter *i2c = &port->i2c->adap;
	int status;

	status = i2c_write_reg(&port->i2c->adap, 0x6e, 0, 0);
	if (status)
		return 0;
	status = i2c_read_reg(i2c, 0x6e, 0xfd, id);
	if (status)
		return 0;
	return 1;
}

static char *xo2names[] = {
	"DUAL DVB-S2", "DUAL DVB-C/T/T2",
	"DUAL DVB-ISDBT", "DUAL DVB-C/C2/T/T2",
	"DUAL ATSC", "DUAL DVB-C/C2/T/T2,ISDB-T",
	"", ""
};

static char *xo2types[] = {
	"DVBS_ST", "DVBCT2_SONY",
	"ISDBT_SONY", "DVBC2T2_SONY",
	"ATSC_ST", "DVBC2T2I_SONY"
};

static void ddb_port_probe(struct ddb_port *port)
{
	struct ddb *dev = port->dev;
	u32 l = port->lnr;
	u8 id, type;

	port->name = "NO MODULE";
	port->type_name = "NONE";
	port->class = DDB_PORT_NONE;

	/* Handle missing ports and ports without I2C */

	if (port->nr == ts_loop) {
		port->name = "TS LOOP";
		port->class = DDB_PORT_LOOP;
		return;
	}

	if (port->nr == 1 && dev->link[l].info->type == DDB_OCTOPUS_CI &&
	    dev->link[l].info->i2c_mask == 1) {
		port->name = "NO TAB";
		port->class = DDB_PORT_NONE;
		return;
	}

	if (dev->link[l].info->type == DDB_OCTOPUS_MAX) {
		port->name = "DUAL DVB-S2 MAX";
		port->type_name = "MXL5XX";
		port->class = DDB_PORT_TUNER;
		port->type = DDB_TUNER_MXL5XX;
		if (port->i2c)
			ddbwritel(dev, I2C_SPEED_400,
				  port->i2c->regs + I2C_TIMING);
		return;
	}

	if (port->nr > 1 && dev->link[l].info->type == DDB_OCTOPUS_CI) {
		port->name = "CI internal";
		port->type_name = "INTERNAL";
		port->class = DDB_PORT_CI;
		port->type = DDB_CI_INTERNAL;
	}

	if (!port->i2c)
		return;

	/* Probe ports with I2C */

	if (port_has_cxd(port, &id)) {
		if (id == 1) {
			port->name = "CI";
			port->type_name = "CXD2099";
			port->class = DDB_PORT_CI;
			port->type = DDB_CI_EXTERNAL_SONY;
			ddbwritel(dev, I2C_SPEED_400,
				  port->i2c->regs + I2C_TIMING);
		} else {
			dev_info(dev->dev, "Port %d: Uninitialized DuoFlex\n",
				 port->nr);
			return;
		}
	} else if (port_has_xo2(port, &type, &id)) {
		ddbwritel(dev, I2C_SPEED_400, port->i2c->regs + I2C_TIMING);
		/*dev_info(dev->dev, "XO2 ID %02x\n", id);*/
		if (type == 2) {
			port->name = "DuoFlex CI";
			port->class = DDB_PORT_CI;
			port->type = DDB_CI_EXTERNAL_XO2;
			port->type_name = "CI_XO2";
			init_xo2_ci(port);
			return;
		}
		id >>= 2;
		if (id > 5) {
			port->name = "unknown XO2 DuoFlex";
			port->type_name = "UNKNOWN";
		} else {
			port->name = xo2names[id];
			port->class = DDB_PORT_TUNER;
			port->type = DDB_TUNER_XO2 + id;
			port->type_name = xo2types[id];
			init_xo2(port);
		}
	} else if (port_has_cxd28xx(port, &id)) {
		switch (id) {
		case 0xa4:
			port->name = "DUAL DVB-C2T2 CXD2843";
			port->type = DDB_TUNER_DVBC2T2_SONY_P;
			port->type_name = "DVBC2T2_SONY";
			break;
		case 0xb1:
			port->name = "DUAL DVB-CT2 CXD2837";
			port->type = DDB_TUNER_DVBCT2_SONY_P;
			port->type_name = "DVBCT2_SONY";
			break;
		case 0xb0:
			port->name = "DUAL ISDB-T CXD2838";
			port->type = DDB_TUNER_ISDBT_SONY_P;
			port->type_name = "ISDBT_SONY";
			break;
		case 0xc1:
			port->name = "DUAL DVB-C2T2 ISDB-T CXD2854";
			port->type = DDB_TUNER_DVBC2T2I_SONY_P;
			port->type_name = "DVBC2T2I_ISDBT_SONY";
			break;
		default:
			return;
		}
		port->class = DDB_PORT_TUNER;
		ddbwritel(dev, I2C_SPEED_400, port->i2c->regs + I2C_TIMING);
	} else if (port_has_stv0900(port)) {
		port->name = "DUAL DVB-S2";
		port->class = DDB_PORT_TUNER;
		port->type = DDB_TUNER_DVBS_ST;
		port->type_name = "DVBS_ST";
		ddbwritel(dev, I2C_SPEED_100, port->i2c->regs + I2C_TIMING);
	} else if (port_has_stv0900_aa(port, &id)) {
		port->name = "DUAL DVB-S2";
		port->class = DDB_PORT_TUNER;
		if (id == 0x51) {
			if (port->nr == 0 &&
			    dev->link[l].info->ts_quirks & TS_QUIRK_REVERSED)
				port->type = DDB_TUNER_DVBS_STV0910_PR;
			else
				port->type = DDB_TUNER_DVBS_STV0910_P;
			port->type_name = "DVBS_ST_0910";
		} else {
			port->type = DDB_TUNER_DVBS_ST_AA;
			port->type_name = "DVBS_ST_AA";
		}
		ddbwritel(dev, I2C_SPEED_100, port->i2c->regs + I2C_TIMING);
	} else if (port_has_drxks(port)) {
		port->name = "DUAL DVB-C/T";
		port->class = DDB_PORT_TUNER;
		port->type = DDB_TUNER_DVBCT_TR;
		port->type_name = "DVBCT_TR";
		ddbwritel(dev, I2C_SPEED_400, port->i2c->regs + I2C_TIMING);
	} else if (port_has_stv0367(port)) {
		port->name = "DUAL DVB-C/T";
		port->class = DDB_PORT_TUNER;
		port->type = DDB_TUNER_DVBCT_ST;
		port->type_name = "DVBCT_ST";
		ddbwritel(dev, I2C_SPEED_100, port->i2c->regs + I2C_TIMING);
	} else if (port_has_encti(port)) {
		port->name = "ENCTI";
		port->class = DDB_PORT_LOOP;
	}
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static int ddb_port_attach(struct ddb_port *port)
{
	int ret = 0;

	switch (port->class) {
	case DDB_PORT_TUNER:
		ret = dvb_input_attach(port->input[0]);
		if (ret < 0)
			break;
		ret = dvb_input_attach(port->input[1]);
		if (ret < 0) {
			dvb_input_detach(port->input[0]);
			break;
		}
		port->input[0]->redi = port->input[0];
		port->input[1]->redi = port->input[1];
		break;
	case DDB_PORT_CI:
		ret = ddb_ci_attach(port, ci_bitrate);
		if (ret < 0)
			break;
		/* fall-through */
	case DDB_PORT_LOOP:
		ret = dvb_register_device(port->dvb[0].adap,
					  &port->dvb[0].dev,
					  &dvbdev_ci, (void *)port->output,
					  DVB_DEVICE_SEC, 0);
		break;
	default:
		break;
	}
	if (ret < 0)
		dev_err(port->dev->dev, "port_attach on port %d failed\n",
			port->nr);
	return ret;
}

int ddb_ports_attach(struct ddb *dev)
{
	int i, numports, err_ports = 0, ret = 0;
	struct ddb_port *port;

	if (dev->port_num) {
		ret = dvb_register_adapters(dev);
		if (ret < 0) {
			dev_err(dev->dev, "Registering adapters failed. Check DVB_MAX_ADAPTERS in config.\n");
			return ret;
		}
	}

	numports = dev->port_num;

	for (i = 0; i < dev->port_num; i++) {
		port = &dev->port[i];
		if (port->class != DDB_PORT_NONE) {
			ret = ddb_port_attach(port);
			if (ret)
				err_ports++;
		} else {
			numports--;
		}
	}

	if (err_ports) {
		if (err_ports == numports) {
			dev_err(dev->dev, "All connected ports failed to initialise!\n");
			return -ENODEV;
		}

		dev_warn(dev->dev, "%d of %d connected ports failed to initialise!\n",
			 err_ports, numports);
	}

	return 0;
}

void ddb_ports_detach(struct ddb *dev)
{
	int i;
	struct ddb_port *port;

	for (i = 0; i < dev->port_num; i++) {
		port = &dev->port[i];

		switch (port->class) {
		case DDB_PORT_TUNER:
			dvb_input_detach(port->input[1]);
			dvb_input_detach(port->input[0]);
			break;
		case DDB_PORT_CI:
		case DDB_PORT_LOOP:
			ddb_ci_detach(port);
			break;
		}
	}
	dvb_unregister_adapters(dev);
}

/* Copy input DMA pointers to output DMA and ACK. */

static void input_write_output(struct ddb_input *input,
			       struct ddb_output *output)
{
	ddbwritel(output->port->dev,
		  input->dma->stat, DMA_BUFFER_ACK(output->dma));
	output->dma->cbuf = (input->dma->stat >> 11) & 0x1f;
	output->dma->coff = (input->dma->stat & 0x7ff) << 7;
}

static void output_ack_input(struct ddb_output *output,
			     struct ddb_input *input)
{
	ddbwritel(input->port->dev,
		  output->dma->stat, DMA_BUFFER_ACK(input->dma));
}

static void input_write_dvb(struct ddb_input *input,
			    struct ddb_input *input2)
{
	struct ddb_dvb *dvb = &input2->port->dvb[input2->nr & 1];
	struct ddb_dma *dma, *dma2;
	struct ddb *dev = input->port->dev;
	int ack = 1;

	dma = input->dma;
	dma2 = input->dma;
	/*
	 * if there also is an output connected, do not ACK.
	 * input_write_output will ACK.
	 */
	if (input->redo) {
		dma2 = input->redo->dma;
		ack = 0;
	}
	while (dma->cbuf != ((dma->stat >> 11) & 0x1f) ||
	       (4 & dma->ctrl)) {
		if (4 & dma->ctrl) {
			/* dev_err(dev->dev, "Overflow dma %d\n", dma->nr); */
			ack = 1;
		}
		if (alt_dma)
			dma_sync_single_for_cpu(dev->dev, dma2->pbuf[dma->cbuf],
						dma2->size, DMA_FROM_DEVICE);
		dvb_dmx_swfilter_packets(&dvb->demux,
					 dma2->vbuf[dma->cbuf],
					 dma2->size / 188);
		dma->cbuf = (dma->cbuf + 1) % dma2->num;
		if (ack)
			ddbwritel(dev, (dma->cbuf << 11),
				  DMA_BUFFER_ACK(dma));
		dma->stat = safe_ddbreadl(dev, DMA_BUFFER_CURRENT(dma));
		dma->ctrl = safe_ddbreadl(dev, DMA_BUFFER_CONTROL(dma));
	}
}

static void input_work(struct work_struct *work)
{
	struct ddb_dma *dma = container_of(work, struct ddb_dma, work);
	struct ddb_input *input = (struct ddb_input *)dma->io;
	struct ddb *dev = input->port->dev;
	unsigned long flags;

	spin_lock_irqsave(&dma->lock, flags);
	if (!dma->running) {
		spin_unlock_irqrestore(&dma->lock, flags);
		return;
	}
	dma->stat = ddbreadl(dev, DMA_BUFFER_CURRENT(dma));
	dma->ctrl = ddbreadl(dev, DMA_BUFFER_CONTROL(dma));

	if (input->redi)
		input_write_dvb(input, input->redi);
	if (input->redo)
		input_write_output(input, input->redo);
	wake_up(&dma->wq);
	spin_unlock_irqrestore(&dma->lock, flags);
}

static void input_handler(unsigned long data)
{
	struct ddb_input *input = (struct ddb_input *)data;
	struct ddb_dma *dma = input->dma;

	/*
	 * If there is no input connected, input_tasklet() will
	 * just copy pointers and ACK. So, there is no need to go
	 * through the tasklet scheduler.
	 */
	if (input->redi)
		queue_work(ddb_wq, &dma->work);
	else
		input_work(&dma->work);
}

static void output_handler(unsigned long data)
{
	struct ddb_output *output = (struct ddb_output *)data;
	struct ddb_dma *dma = output->dma;
	struct ddb *dev = output->port->dev;

	spin_lock(&dma->lock);
	if (!dma->running) {
		spin_unlock(&dma->lock);
		return;
	}
	dma->stat = ddbreadl(dev, DMA_BUFFER_CURRENT(dma));
	dma->ctrl = ddbreadl(dev, DMA_BUFFER_CONTROL(dma));
	if (output->redi)
		output_ack_input(output, output->redi);
	wake_up(&dma->wq);
	spin_unlock(&dma->lock);
}

/****************************************************************************/
/****************************************************************************/

static const struct ddb_regmap *io_regmap(struct ddb_io *io, int link)
{
	const struct ddb_info *info;

	if (link)
		info = io->port->dev->link[io->port->lnr].info;
	else
		info = io->port->dev->link[0].info;

	if (!info)
		return NULL;

	return info->regmap;
}

static void ddb_dma_init(struct ddb_io *io, int nr, int out)
{
	struct ddb_dma *dma;
	const struct ddb_regmap *rm = io_regmap(io, 0);

	dma = out ? &io->port->dev->odma[nr] : &io->port->dev->idma[nr];
	io->dma = dma;
	dma->io = io;

	spin_lock_init(&dma->lock);
	init_waitqueue_head(&dma->wq);
	if (out) {
		dma->regs = rm->odma->base + rm->odma->size * nr;
		dma->bufregs = rm->odma_buf->base + rm->odma_buf->size * nr;
		dma->num = OUTPUT_DMA_BUFS;
		dma->size = OUTPUT_DMA_SIZE;
		dma->div = OUTPUT_DMA_IRQ_DIV;
	} else {
		INIT_WORK(&dma->work, input_work);
		dma->regs = rm->idma->base + rm->idma->size * nr;
		dma->bufregs = rm->idma_buf->base + rm->idma_buf->size * nr;
		dma->num = INPUT_DMA_BUFS;
		dma->size = INPUT_DMA_SIZE;
		dma->div = INPUT_DMA_IRQ_DIV;
	}
	ddbwritel(io->port->dev, 0, DMA_BUFFER_ACK(dma));
	dev_dbg(io->port->dev->dev, "init link %u, io %u, dma %u, dmaregs %08x bufregs %08x\n",
		io->port->lnr, io->nr, nr, dma->regs, dma->bufregs);
}

static void ddb_input_init(struct ddb_port *port, int nr, int pnr, int anr)
{
	struct ddb *dev = port->dev;
	struct ddb_input *input = &dev->input[anr];
	const struct ddb_regmap *rm;

	port->input[pnr] = input;
	input->nr = nr;
	input->port = port;
	rm = io_regmap(input, 1);
	input->regs = DDB_LINK_TAG(port->lnr) |
		(rm->input->base + rm->input->size * nr);
	dev_dbg(dev->dev, "init link %u, input %u, regs %08x\n",
		port->lnr, nr, input->regs);

	if (dev->has_dma) {
		const struct ddb_regmap *rm0 = io_regmap(input, 0);
		u32 base = rm0->irq_base_idma;
		u32 dma_nr = nr;

		if (port->lnr)
			dma_nr += 32 + (port->lnr - 1) * 8;

		dev_dbg(dev->dev, "init link %u, input %u, handler %u\n",
			port->lnr, nr, dma_nr + base);

		dev->handler[0][dma_nr + base] = input_handler;
		dev->handler_data[0][dma_nr + base] = (unsigned long)input;
		ddb_dma_init(input, dma_nr, 0);
	}
}

static void ddb_output_init(struct ddb_port *port, int nr)
{
	struct ddb *dev = port->dev;
	struct ddb_output *output = &dev->output[nr];
	const struct ddb_regmap *rm;

	port->output = output;
	output->nr = nr;
	output->port = port;
	rm = io_regmap(output, 1);
	output->regs = DDB_LINK_TAG(port->lnr) |
		(rm->output->base + rm->output->size * nr);

	dev_dbg(dev->dev, "init link %u, output %u, regs %08x\n",
		port->lnr, nr, output->regs);

	if (dev->has_dma) {
		const struct ddb_regmap *rm0 = io_regmap(output, 0);
		u32 base = rm0->irq_base_odma;

		dev->handler[0][nr + base] = output_handler;
		dev->handler_data[0][nr + base] = (unsigned long)output;
		ddb_dma_init(output, nr, 1);
	}
}

static int ddb_port_match_i2c(struct ddb_port *port)
{
	struct ddb *dev = port->dev;
	u32 i;

	for (i = 0; i < dev->i2c_num; i++) {
		if (dev->i2c[i].link == port->lnr &&
		    dev->i2c[i].nr == port->nr) {
			port->i2c = &dev->i2c[i];
			return 1;
		}
	}
	return 0;
}

static int ddb_port_match_link_i2c(struct ddb_port *port)
{
	struct ddb *dev = port->dev;
	u32 i;

	for (i = 0; i < dev->i2c_num; i++) {
		if (dev->i2c[i].link == port->lnr) {
			port->i2c = &dev->i2c[i];
			return 1;
		}
	}
	return 0;
}

void ddb_ports_init(struct ddb *dev)
{
	u32 i, l, p;
	struct ddb_port *port;
	const struct ddb_info *info;
	const struct ddb_regmap *rm;

	for (p = l = 0; l < DDB_MAX_LINK; l++) {
		info = dev->link[l].info;
		if (!info)
			continue;
		rm = info->regmap;
		if (!rm)
			continue;
		for (i = 0; i < info->port_num; i++, p++) {
			port = &dev->port[p];
			port->dev = dev;
			port->nr = i;
			port->lnr = l;
			port->pnr = p;
			port->gap = 0xffffffff;
			port->obr = ci_bitrate;
			mutex_init(&port->i2c_gate_lock);

			if (!ddb_port_match_i2c(port)) {
				if (info->type == DDB_OCTOPUS_MAX)
					ddb_port_match_link_i2c(port);
			}

			ddb_port_probe(port);

			port->dvb[0].adap = &dev->adap[2 * p];
			port->dvb[1].adap = &dev->adap[2 * p + 1];

			if (port->class == DDB_PORT_NONE && i && p &&
			    dev->port[p - 1].type == DDB_CI_EXTERNAL_XO2) {
				port->class = DDB_PORT_CI;
				port->type = DDB_CI_EXTERNAL_XO2_B;
				port->name = "DuoFlex CI_B";
				port->i2c = dev->port[p - 1].i2c;
			}

			dev_info(dev->dev, "Port %u: Link %u, Link Port %u (TAB %u): %s\n",
				 port->pnr, port->lnr, port->nr, port->nr + 1,
				 port->name);

			if (port->class == DDB_PORT_CI &&
			    port->type == DDB_CI_EXTERNAL_XO2) {
				ddb_input_init(port, 2 * i, 0, 2 * i);
				ddb_output_init(port, i);
				continue;
			}

			if (port->class == DDB_PORT_CI &&
			    port->type == DDB_CI_EXTERNAL_XO2_B) {
				ddb_input_init(port, 2 * i - 1, 0, 2 * i - 1);
				ddb_output_init(port, i);
				continue;
			}

			if (port->class == DDB_PORT_NONE)
				continue;

			switch (dev->link[l].info->type) {
			case DDB_OCTOPUS_CI:
				if (i >= 2) {
					ddb_input_init(port, 2 + i, 0, 2 + i);
					ddb_input_init(port, 4 + i, 1, 4 + i);
					ddb_output_init(port, i);
					break;
				} /* fallthrough */
			case DDB_OCTOPUS:
				ddb_input_init(port, 2 * i, 0, 2 * i);
				ddb_input_init(port, 2 * i + 1, 1, 2 * i + 1);
				ddb_output_init(port, i);
				break;
			case DDB_OCTOPUS_MAX:
			case DDB_OCTOPUS_MAX_CT:
				ddb_input_init(port, 2 * i, 0, 2 * p);
				ddb_input_init(port, 2 * i + 1, 1, 2 * p + 1);
				break;
			default:
				break;
			}
		}
	}
	dev->port_num = p;
}

void ddb_ports_release(struct ddb *dev)
{
	int i;
	struct ddb_port *port;

	for (i = 0; i < dev->port_num; i++) {
		port = &dev->port[i];
		if (port->input[0] && port->input[0]->dma)
			cancel_work_sync(&port->input[0]->dma->work);
		if (port->input[1] && port->input[1]->dma)
			cancel_work_sync(&port->input[1]->dma->work);
		if (port->output && port->output->dma)
			cancel_work_sync(&port->output->dma->work);
	}
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

#define IRQ_HANDLE(_nr) \
	do { if ((s & (1UL << ((_nr) & 0x1f))) && dev->handler[0][_nr]) \
		dev->handler[0][_nr](dev->handler_data[0][_nr]); } \
	while (0)

static void irq_handle_msg(struct ddb *dev, u32 s)
{
	dev->i2c_irq++;
	IRQ_HANDLE(0);
	IRQ_HANDLE(1);
	IRQ_HANDLE(2);
	IRQ_HANDLE(3);
}

static void irq_handle_io(struct ddb *dev, u32 s)
{
	dev->ts_irq++;
	if ((s & 0x000000f0)) {
		IRQ_HANDLE(4);
		IRQ_HANDLE(5);
		IRQ_HANDLE(6);
		IRQ_HANDLE(7);
	}
	if ((s & 0x0000ff00)) {
		IRQ_HANDLE(8);
		IRQ_HANDLE(9);
		IRQ_HANDLE(10);
		IRQ_HANDLE(11);
		IRQ_HANDLE(12);
		IRQ_HANDLE(13);
		IRQ_HANDLE(14);
		IRQ_HANDLE(15);
	}
	if ((s & 0x00ff0000)) {
		IRQ_HANDLE(16);
		IRQ_HANDLE(17);
		IRQ_HANDLE(18);
		IRQ_HANDLE(19);
		IRQ_HANDLE(20);
		IRQ_HANDLE(21);
		IRQ_HANDLE(22);
		IRQ_HANDLE(23);
	}
	if ((s & 0xff000000)) {
		IRQ_HANDLE(24);
		IRQ_HANDLE(25);
		IRQ_HANDLE(26);
		IRQ_HANDLE(27);
		IRQ_HANDLE(28);
		IRQ_HANDLE(29);
		IRQ_HANDLE(30);
		IRQ_HANDLE(31);
	}
}

irqreturn_t ddb_irq_handler0(int irq, void *dev_id)
{
	struct ddb *dev = (struct ddb *)dev_id;
	u32 s = ddbreadl(dev, INTERRUPT_STATUS);

	do {
		if (s & 0x80000000)
			return IRQ_NONE;
		if (!(s & 0xfffff00))
			return IRQ_NONE;
		ddbwritel(dev, s & 0xfffff00, INTERRUPT_ACK);
		irq_handle_io(dev, s);
	} while ((s = ddbreadl(dev, INTERRUPT_STATUS)));

	return IRQ_HANDLED;
}

irqreturn_t ddb_irq_handler1(int irq, void *dev_id)
{
	struct ddb *dev = (struct ddb *)dev_id;
	u32 s = ddbreadl(dev, INTERRUPT_STATUS);

	do {
		if (s & 0x80000000)
			return IRQ_NONE;
		if (!(s & 0x0000f))
			return IRQ_NONE;
		ddbwritel(dev, s & 0x0000f, INTERRUPT_ACK);
		irq_handle_msg(dev, s);
	} while ((s = ddbreadl(dev, INTERRUPT_STATUS)));

	return IRQ_HANDLED;
}

irqreturn_t ddb_irq_handler(int irq, void *dev_id)
{
	struct ddb *dev = (struct ddb *)dev_id;
	u32 s = ddbreadl(dev, INTERRUPT_STATUS);
	int ret = IRQ_HANDLED;

	if (!s)
		return IRQ_NONE;
	do {
		if (s & 0x80000000)
			return IRQ_NONE;
		ddbwritel(dev, s, INTERRUPT_ACK);

		if (s & 0x0000000f)
			irq_handle_msg(dev, s);
		if (s & 0x0fffff00)
			irq_handle_io(dev, s);
	} while ((s = ddbreadl(dev, INTERRUPT_STATUS)));

	return ret;
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static int reg_wait(struct ddb *dev, u32 reg, u32 bit)
{
	u32 count = 0;

	while (safe_ddbreadl(dev, reg) & bit) {
		ndelay(10);
		if (++count == 100)
			return -1;
	}
	return 0;
}

static int flashio(struct ddb *dev, u32 lnr, u8 *wbuf, u32 wlen, u8 *rbuf,
		   u32 rlen)
{
	u32 data, shift;
	u32 tag = DDB_LINK_TAG(lnr);
	struct ddb_link *link = &dev->link[lnr];

	mutex_lock(&link->flash_mutex);
	if (wlen > 4)
		ddbwritel(dev, 1, tag | SPI_CONTROL);
	while (wlen > 4) {
		/* FIXME: check for big-endian */
		data = swab32(*(u32 *)wbuf);
		wbuf += 4;
		wlen -= 4;
		ddbwritel(dev, data, tag | SPI_DATA);
		if (reg_wait(dev, tag | SPI_CONTROL, 4))
			goto fail;
	}
	if (rlen)
		ddbwritel(dev, 0x0001 | ((wlen << (8 + 3)) & 0x1f00),
			  tag | SPI_CONTROL);
	else
		ddbwritel(dev, 0x0003 | ((wlen << (8 + 3)) & 0x1f00),
			  tag | SPI_CONTROL);

	data = 0;
	shift = ((4 - wlen) * 8);
	while (wlen) {
		data <<= 8;
		data |= *wbuf;
		wlen--;
		wbuf++;
	}
	if (shift)
		data <<= shift;
	ddbwritel(dev, data, tag | SPI_DATA);
	if (reg_wait(dev, tag | SPI_CONTROL, 4))
		goto fail;

	if (!rlen) {
		ddbwritel(dev, 0, tag | SPI_CONTROL);
		goto exit;
	}
	if (rlen > 4)
		ddbwritel(dev, 1, tag | SPI_CONTROL);

	while (rlen > 4) {
		ddbwritel(dev, 0xffffffff, tag | SPI_DATA);
		if (reg_wait(dev, tag | SPI_CONTROL, 4))
			goto fail;
		data = ddbreadl(dev, tag | SPI_DATA);
		*(u32 *)rbuf = swab32(data);
		rbuf += 4;
		rlen -= 4;
	}
	ddbwritel(dev, 0x0003 | ((rlen << (8 + 3)) & 0x1F00),
		  tag | SPI_CONTROL);
	ddbwritel(dev, 0xffffffff, tag | SPI_DATA);
	if (reg_wait(dev, tag | SPI_CONTROL, 4))
		goto fail;

	data = ddbreadl(dev, tag | SPI_DATA);
	ddbwritel(dev, 0, tag | SPI_CONTROL);

	if (rlen < 4)
		data <<= ((4 - rlen) * 8);

	while (rlen > 0) {
		*rbuf = ((data >> 24) & 0xff);
		data <<= 8;
		rbuf++;
		rlen--;
	}
exit:
	mutex_unlock(&link->flash_mutex);
	return 0;
fail:
	mutex_unlock(&link->flash_mutex);
	return -1;
}

int ddbridge_flashread(struct ddb *dev, u32 link, u8 *buf, u32 addr, u32 len)
{
	u8 cmd[4] = {0x03, (addr >> 16) & 0xff,
		     (addr >> 8) & 0xff, addr & 0xff};

	return flashio(dev, link, cmd, 4, buf, len);
}

/*
 * TODO/FIXME: add/implement IOCTLs from upstream driver
 */

#define DDB_NAME "ddbridge"

static u32 ddb_num;
static int ddb_major;
static DEFINE_MUTEX(ddb_mutex);

static int ddb_release(struct inode *inode, struct file *file)
{
	struct ddb *dev = file->private_data;

	dev->ddb_dev_users--;
	return 0;
}

static int ddb_open(struct inode *inode, struct file *file)
{
	struct ddb *dev = ddbs[iminor(inode)];

	if (dev->ddb_dev_users)
		return -EBUSY;
	dev->ddb_dev_users++;
	file->private_data = dev;
	return 0;
}

static long ddb_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ddb *dev = file->private_data;

	dev_warn(dev->dev, "DDB IOCTLs unsupported (cmd: %d, arg: %lu)\n",
		 cmd, arg);

	return -ENOTTY;
}

static const struct file_operations ddb_fops = {
	.unlocked_ioctl = ddb_ioctl,
	.open           = ddb_open,
	.release        = ddb_release,
};

static char *ddb_devnode(struct device *device, umode_t *mode)
{
	struct ddb *dev = dev_get_drvdata(device);

	return kasprintf(GFP_KERNEL, "ddbridge/card%d", dev->nr);
}

#define __ATTR_MRO(_name, _show) {				\
	.attr	= { .name = __stringify(_name), .mode = 0444 },	\
	.show	= _show,					\
}

#define __ATTR_MWO(_name, _store) {				\
	.attr	= { .name = __stringify(_name), .mode = 0222 },	\
	.store	= _store,					\
}

static ssize_t ports_show(struct device *device,
			  struct device_attribute *attr, char *buf)
{
	struct ddb *dev = dev_get_drvdata(device);

	return sprintf(buf, "%d\n", dev->port_num);
}

static ssize_t ts_irq_show(struct device *device,
			   struct device_attribute *attr, char *buf)
{
	struct ddb *dev = dev_get_drvdata(device);

	return sprintf(buf, "%d\n", dev->ts_irq);
}

static ssize_t i2c_irq_show(struct device *device,
			    struct device_attribute *attr, char *buf)
{
	struct ddb *dev = dev_get_drvdata(device);

	return sprintf(buf, "%d\n", dev->i2c_irq);
}

static ssize_t fan_show(struct device *device,
			struct device_attribute *attr, char *buf)
{
	struct ddb *dev = dev_get_drvdata(device);
	u32 val;

	val = ddbreadl(dev, GPIO_OUTPUT) & 1;
	return sprintf(buf, "%d\n", val);
}

static ssize_t fan_store(struct device *device, struct device_attribute *d,
			 const char *buf, size_t count)
{
	struct ddb *dev = dev_get_drvdata(device);
	u32 val;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;
	ddbwritel(dev, 1, GPIO_DIRECTION);
	ddbwritel(dev, val & 1, GPIO_OUTPUT);
	return count;
}

static ssize_t fanspeed_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	struct ddb *dev = dev_get_drvdata(device);
	int num = attr->attr.name[8] - 0x30;
	struct ddb_link *link = &dev->link[num];
	u32 spd;

	spd = ddblreadl(link, TEMPMON_FANCONTROL) & 0xff;
	return sprintf(buf, "%u\n", spd * 100);
}

static ssize_t temp_show(struct device *device,
			 struct device_attribute *attr, char *buf)
{
	struct ddb *dev = dev_get_drvdata(device);
	struct ddb_link *link = &dev->link[0];
	struct i2c_adapter *adap;
	int temp, temp2;
	u8 tmp[2];

	if (!link->info->temp_num)
		return sprintf(buf, "no sensor\n");
	adap = &dev->i2c[link->info->temp_bus].adap;
	if (i2c_read_regs(adap, 0x48, 0, tmp, 2) < 0)
		return sprintf(buf, "read_error\n");
	temp = (tmp[0] << 3) | (tmp[1] >> 5);
	temp *= 125;
	if (link->info->temp_num == 2) {
		if (i2c_read_regs(adap, 0x49, 0, tmp, 2) < 0)
			return sprintf(buf, "read_error\n");
		temp2 = (tmp[0] << 3) | (tmp[1] >> 5);
		temp2 *= 125;
		return sprintf(buf, "%d %d\n", temp, temp2);
	}
	return sprintf(buf, "%d\n", temp);
}

static ssize_t ctemp_show(struct device *device,
			  struct device_attribute *attr, char *buf)
{
	struct ddb *dev = dev_get_drvdata(device);
	struct i2c_adapter *adap;
	int temp;
	u8 tmp[2];
	int num = attr->attr.name[4] - 0x30;

	adap = &dev->i2c[num].adap;
	if (!adap)
		return 0;
	if (i2c_read_regs(adap, 0x49, 0, tmp, 2) < 0)
		if (i2c_read_regs(adap, 0x4d, 0, tmp, 2) < 0)
			return sprintf(buf, "no sensor\n");
	temp = tmp[0] * 1000;
	return sprintf(buf, "%d\n", temp);
}

static ssize_t led_show(struct device *device,
			struct device_attribute *attr, char *buf)
{
	struct ddb *dev = dev_get_drvdata(device);
	int num = attr->attr.name[3] - 0x30;

	return sprintf(buf, "%d\n", dev->leds & (1 << num) ? 1 : 0);
}

static void ddb_set_led(struct ddb *dev, int num, int val)
{
	if (!dev->link[0].info->led_num)
		return;
	switch (dev->port[num].class) {
	case DDB_PORT_TUNER:
		switch (dev->port[num].type) {
		case DDB_TUNER_DVBS_ST:
			i2c_write_reg16(&dev->i2c[num].adap,
					0x69, 0xf14c, val ? 2 : 0);
			break;
		case DDB_TUNER_DVBCT_ST:
			i2c_write_reg16(&dev->i2c[num].adap,
					0x1f, 0xf00e, 0);
			i2c_write_reg16(&dev->i2c[num].adap,
					0x1f, 0xf00f, val ? 1 : 0);
			break;
		case DDB_TUNER_XO2 ... DDB_TUNER_DVBC2T2I_SONY:
		{
			u8 v;

			i2c_read_reg(&dev->i2c[num].adap, 0x10, 0x08, &v);
			v = (v & ~0x10) | (val ? 0x10 : 0);
			i2c_write_reg(&dev->i2c[num].adap, 0x10, 0x08, v);
			break;
		}
		default:
			break;
		}
		break;
	}
}

static ssize_t led_store(struct device *device,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct ddb *dev = dev_get_drvdata(device);
	int num = attr->attr.name[3] - 0x30;
	u32 val;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;
	if (val)
		dev->leds |= (1 << num);
	else
		dev->leds &= ~(1 << num);
	ddb_set_led(dev, num, val);
	return count;
}

static ssize_t snr_show(struct device *device,
			struct device_attribute *attr, char *buf)
{
	struct ddb *dev = dev_get_drvdata(device);
	char snr[32];
	int num = attr->attr.name[3] - 0x30;

	if (dev->port[num].type >= DDB_TUNER_XO2) {
		if (i2c_read_regs(&dev->i2c[num].adap, 0x10, 0x10, snr, 16) < 0)
			return sprintf(buf, "NO SNR\n");
		snr[16] = 0;
	} else {
		/* serial number at 0x100-0x11f */
		if (i2c_read_regs16(&dev->i2c[num].adap,
				    0x57, 0x100, snr, 32) < 0)
			if (i2c_read_regs16(&dev->i2c[num].adap,
					    0x50, 0x100, snr, 32) < 0)
				return sprintf(buf, "NO SNR\n");
		snr[31] = 0; /* in case it is not terminated on EEPROM */
	}
	return sprintf(buf, "%s\n", snr);
}

static ssize_t bsnr_show(struct device *device,
			 struct device_attribute *attr, char *buf)
{
	struct ddb *dev = dev_get_drvdata(device);
	char snr[16];

	ddbridge_flashread(dev, 0, snr, 0x10, 15);
	snr[15] = 0; /* in case it is not terminated on EEPROM */
	return sprintf(buf, "%s\n", snr);
}

static ssize_t bpsnr_show(struct device *device,
			  struct device_attribute *attr, char *buf)
{
	struct ddb *dev = dev_get_drvdata(device);
	unsigned char snr[32];

	if (!dev->i2c_num)
		return 0;

	if (i2c_read_regs16(&dev->i2c[0].adap,
			    0x50, 0x0000, snr, 32) < 0 ||
	    snr[0] == 0xff)
		return sprintf(buf, "NO SNR\n");
	snr[31] = 0; /* in case it is not terminated on EEPROM */
	return sprintf(buf, "%s\n", snr);
}

static ssize_t redirect_show(struct device *device,
			     struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t redirect_store(struct device *device,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	unsigned int i, p;
	int res;

	if (sscanf(buf, "%x %x\n", &i, &p) != 2)
		return -EINVAL;
	res = ddb_redirect(i, p);
	if (res < 0)
		return res;
	dev_info(device, "redirect: %02x, %02x\n", i, p);
	return count;
}

static ssize_t gap_show(struct device *device,
			struct device_attribute *attr, char *buf)
{
	struct ddb *dev = dev_get_drvdata(device);
	int num = attr->attr.name[3] - 0x30;

	return sprintf(buf, "%d\n", dev->port[num].gap);
}

static ssize_t gap_store(struct device *device, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct ddb *dev = dev_get_drvdata(device);
	int num = attr->attr.name[3] - 0x30;
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;
	if (val > 128)
		return -EINVAL;
	if (val == 128)
		val = 0xffffffff;
	dev->port[num].gap = val;
	return count;
}

static ssize_t version_show(struct device *device,
			    struct device_attribute *attr, char *buf)
{
	struct ddb *dev = dev_get_drvdata(device);

	return sprintf(buf, "%08x %08x\n",
		       dev->link[0].ids.hwid, dev->link[0].ids.regmapid);
}

static ssize_t hwid_show(struct device *device,
			 struct device_attribute *attr, char *buf)
{
	struct ddb *dev = dev_get_drvdata(device);

	return sprintf(buf, "0x%08X\n", dev->link[0].ids.hwid);
}

static ssize_t regmap_show(struct device *device,
			   struct device_attribute *attr, char *buf)
{
	struct ddb *dev = dev_get_drvdata(device);

	return sprintf(buf, "0x%08X\n", dev->link[0].ids.regmapid);
}

static ssize_t fmode_show(struct device *device,
			  struct device_attribute *attr, char *buf)
{
	int num = attr->attr.name[5] - 0x30;
	struct ddb *dev = dev_get_drvdata(device);

	return sprintf(buf, "%u\n", dev->link[num].lnb.fmode);
}

static ssize_t devid_show(struct device *device,
			  struct device_attribute *attr, char *buf)
{
	int num = attr->attr.name[5] - 0x30;
	struct ddb *dev = dev_get_drvdata(device);

	return sprintf(buf, "%08x\n", dev->link[num].ids.devid);
}

static ssize_t fmode_store(struct device *device, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct ddb *dev = dev_get_drvdata(device);
	int num = attr->attr.name[5] - 0x30;
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;
	if (val > 3)
		return -EINVAL;
	ddb_lnb_init_fmode(dev, &dev->link[num], val);
	return count;
}

static struct device_attribute ddb_attrs[] = {
	__ATTR_RO(version),
	__ATTR_RO(ports),
	__ATTR_RO(ts_irq),
	__ATTR_RO(i2c_irq),
	__ATTR(gap0, 0664, gap_show, gap_store),
	__ATTR(gap1, 0664, gap_show, gap_store),
	__ATTR(gap2, 0664, gap_show, gap_store),
	__ATTR(gap3, 0664, gap_show, gap_store),
	__ATTR(fmode0, 0664, fmode_show, fmode_store),
	__ATTR(fmode1, 0664, fmode_show, fmode_store),
	__ATTR(fmode2, 0664, fmode_show, fmode_store),
	__ATTR(fmode3, 0664, fmode_show, fmode_store),
	__ATTR_MRO(devid0, devid_show),
	__ATTR_MRO(devid1, devid_show),
	__ATTR_MRO(devid2, devid_show),
	__ATTR_MRO(devid3, devid_show),
	__ATTR_RO(hwid),
	__ATTR_RO(regmap),
	__ATTR(redirect, 0664, redirect_show, redirect_store),
	__ATTR_MRO(snr,  bsnr_show),
	__ATTR_RO(bpsnr),
	__ATTR_NULL,
};

static struct device_attribute ddb_attrs_temp[] = {
	__ATTR_RO(temp),
};

static struct device_attribute ddb_attrs_fan[] = {
	__ATTR(fan, 0664, fan_show, fan_store),
};

static struct device_attribute ddb_attrs_snr[] = {
	__ATTR_MRO(snr0, snr_show),
	__ATTR_MRO(snr1, snr_show),
	__ATTR_MRO(snr2, snr_show),
	__ATTR_MRO(snr3, snr_show),
};

static struct device_attribute ddb_attrs_ctemp[] = {
	__ATTR_MRO(temp0, ctemp_show),
	__ATTR_MRO(temp1, ctemp_show),
	__ATTR_MRO(temp2, ctemp_show),
	__ATTR_MRO(temp3, ctemp_show),
};

static struct device_attribute ddb_attrs_led[] = {
	__ATTR(led0, 0664, led_show, led_store),
	__ATTR(led1, 0664, led_show, led_store),
	__ATTR(led2, 0664, led_show, led_store),
	__ATTR(led3, 0664, led_show, led_store),
};

static struct device_attribute ddb_attrs_fanspeed[] = {
	__ATTR_MRO(fanspeed0, fanspeed_show),
	__ATTR_MRO(fanspeed1, fanspeed_show),
	__ATTR_MRO(fanspeed2, fanspeed_show),
	__ATTR_MRO(fanspeed3, fanspeed_show),
};

static struct class ddb_class = {
	.name		= "ddbridge",
	.owner          = THIS_MODULE,
	.devnode        = ddb_devnode,
};

int ddb_class_create(void)
{
	ddb_major = register_chrdev(0, DDB_NAME, &ddb_fops);
	if (ddb_major < 0)
		return ddb_major;
	if (class_register(&ddb_class) < 0)
		return -1;
	return 0;
}

void ddb_class_destroy(void)
{
	class_unregister(&ddb_class);
	unregister_chrdev(ddb_major, DDB_NAME);
}

static void ddb_device_attrs_del(struct ddb *dev)
{
	int i;

	for (i = 0; i < 4; i++)
		if (dev->link[i].info && dev->link[i].info->tempmon_irq)
			device_remove_file(dev->ddb_dev,
					   &ddb_attrs_fanspeed[i]);
	for (i = 0; i < dev->link[0].info->temp_num; i++)
		device_remove_file(dev->ddb_dev, &ddb_attrs_temp[i]);
	for (i = 0; i < dev->link[0].info->fan_num; i++)
		device_remove_file(dev->ddb_dev, &ddb_attrs_fan[i]);
	for (i = 0; i < dev->i2c_num && i < 4; i++) {
		if (dev->link[0].info->led_num)
			device_remove_file(dev->ddb_dev, &ddb_attrs_led[i]);
		device_remove_file(dev->ddb_dev, &ddb_attrs_snr[i]);
		device_remove_file(dev->ddb_dev, &ddb_attrs_ctemp[i]);
	}
	for (i = 0; ddb_attrs[i].attr.name; i++)
		device_remove_file(dev->ddb_dev, &ddb_attrs[i]);
}

static int ddb_device_attrs_add(struct ddb *dev)
{
	int i;

	for (i = 0; ddb_attrs[i].attr.name; i++)
		if (device_create_file(dev->ddb_dev, &ddb_attrs[i]))
			goto fail;
	for (i = 0; i < dev->link[0].info->temp_num; i++)
		if (device_create_file(dev->ddb_dev, &ddb_attrs_temp[i]))
			goto fail;
	for (i = 0; i < dev->link[0].info->fan_num; i++)
		if (device_create_file(dev->ddb_dev, &ddb_attrs_fan[i]))
			goto fail;
	for (i = 0; (i < dev->i2c_num) && (i < 4); i++) {
		if (device_create_file(dev->ddb_dev, &ddb_attrs_snr[i]))
			goto fail;
		if (device_create_file(dev->ddb_dev, &ddb_attrs_ctemp[i]))
			goto fail;
		if (dev->link[0].info->led_num)
			if (device_create_file(dev->ddb_dev,
					       &ddb_attrs_led[i]))
				goto fail;
	}
	for (i = 0; i < 4; i++)
		if (dev->link[i].info && dev->link[i].info->tempmon_irq)
			if (device_create_file(dev->ddb_dev,
					       &ddb_attrs_fanspeed[i]))
				goto fail;
	return 0;
fail:
	return -1;
}

int ddb_device_create(struct ddb *dev)
{
	int res = 0;

	if (ddb_num == DDB_MAX_ADAPTER)
		return -ENOMEM;
	mutex_lock(&ddb_mutex);
	dev->nr = ddb_num;
	ddbs[dev->nr] = dev;
	dev->ddb_dev = device_create(&ddb_class, dev->dev,
				     MKDEV(ddb_major, dev->nr),
				     dev, "ddbridge%d", dev->nr);
	if (IS_ERR(dev->ddb_dev)) {
		res = PTR_ERR(dev->ddb_dev);
		dev_info(dev->dev, "Could not create ddbridge%d\n", dev->nr);
		goto fail;
	}
	res = ddb_device_attrs_add(dev);
	if (res) {
		ddb_device_attrs_del(dev);
		device_destroy(&ddb_class, MKDEV(ddb_major, dev->nr));
		ddbs[dev->nr] = NULL;
		dev->ddb_dev = ERR_PTR(-ENODEV);
	} else {
		ddb_num++;
	}
fail:
	mutex_unlock(&ddb_mutex);
	return res;
}

void ddb_device_destroy(struct ddb *dev)
{
	if (IS_ERR(dev->ddb_dev))
		return;
	ddb_device_attrs_del(dev);
	device_destroy(&ddb_class, MKDEV(ddb_major, dev->nr));
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static void tempmon_setfan(struct ddb_link *link)
{
	u32 temp, temp2, pwm;

	if ((ddblreadl(link, TEMPMON_CONTROL) &
	    TEMPMON_CONTROL_OVERTEMP) != 0) {
		dev_info(link->dev->dev, "Over temperature condition\n");
		link->overtemperature_error = 1;
	}
	temp  = (ddblreadl(link, TEMPMON_SENSOR0) >> 8) & 0xFF;
	if (temp & 0x80)
		temp = 0;
	temp2  = (ddblreadl(link, TEMPMON_SENSOR1) >> 8) & 0xFF;
	if (temp2 & 0x80)
		temp2 = 0;
	if (temp2 > temp)
		temp = temp2;

	pwm = (ddblreadl(link, TEMPMON_FANCONTROL) >> 8) & 0x0F;
	if (pwm > 10)
		pwm = 10;

	if (temp >= link->temp_tab[pwm]) {
		while (pwm < 10 && temp >= link->temp_tab[pwm + 1])
			pwm += 1;
	} else {
		while (pwm > 1 && temp < link->temp_tab[pwm - 2])
			pwm -= 1;
	}
	ddblwritel(link, (pwm << 8), TEMPMON_FANCONTROL);
}

static void temp_handler(unsigned long data)
{
	struct ddb_link *link = (struct ddb_link *)data;

	spin_lock(&link->temp_lock);
	tempmon_setfan(link);
	spin_unlock(&link->temp_lock);
}

static int tempmon_init(struct ddb_link *link, int first_time)
{
	struct ddb *dev = link->dev;
	int status = 0;
	u32 l = link->nr;

	spin_lock_irq(&link->temp_lock);
	if (first_time) {
		static u8 temperature_table[11] = {
			30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80 };

		memcpy(link->temp_tab, temperature_table,
		       sizeof(temperature_table));
	}
	dev->handler[l][link->info->tempmon_irq] = temp_handler;
	dev->handler_data[l][link->info->tempmon_irq] = (unsigned long)link;
	ddblwritel(link, (TEMPMON_CONTROL_OVERTEMP | TEMPMON_CONTROL_AUTOSCAN |
			  TEMPMON_CONTROL_INTENABLE),
		   TEMPMON_CONTROL);
	ddblwritel(link, (3 << 8), TEMPMON_FANCONTROL);

	link->overtemperature_error =
		((ddblreadl(link, TEMPMON_CONTROL) &
			TEMPMON_CONTROL_OVERTEMP) != 0);
	if (link->overtemperature_error) {
		dev_info(link->dev->dev, "Over temperature condition\n");
		status = -1;
	}
	tempmon_setfan(link);
	spin_unlock_irq(&link->temp_lock);
	return status;
}

static int ddb_init_tempmon(struct ddb_link *link)
{
	const struct ddb_info *info = link->info;

	if (!info->tempmon_irq)
		return 0;
	if (info->type == DDB_OCTOPUS_MAX_CT)
		if (link->ids.regmapid < 0x00010002)
			return 0;
	spin_lock_init(&link->temp_lock);
	dev_dbg(link->dev->dev, "init_tempmon\n");
	return tempmon_init(link, 1);
}

/****************************************************************************/
/****************************************************************************/
/****************************************************************************/

static int ddb_init_boards(struct ddb *dev)
{
	const struct ddb_info *info;
	struct ddb_link *link;
	u32 l;

	for (l = 0; l < DDB_MAX_LINK; l++) {
		link = &dev->link[l];
		info = link->info;

		if (!info)
			continue;
		if (info->board_control) {
			ddbwritel(dev, 0, DDB_LINK_TAG(l) | BOARD_CONTROL);
			msleep(100);
			ddbwritel(dev, info->board_control_2,
				  DDB_LINK_TAG(l) | BOARD_CONTROL);
			usleep_range(2000, 3000);
			ddbwritel(dev,
				  info->board_control_2 | info->board_control,
				  DDB_LINK_TAG(l) | BOARD_CONTROL);
			usleep_range(2000, 3000);
		}
		ddb_init_tempmon(link);
	}
	return 0;
}

int ddb_init(struct ddb *dev)
{
	mutex_init(&dev->link[0].lnb.lock);
	mutex_init(&dev->link[0].flash_mutex);
	if (no_init) {
		ddb_device_create(dev);
		return 0;
	}

	ddb_init_boards(dev);

	if (ddb_i2c_init(dev) < 0)
		goto fail1;
	ddb_ports_init(dev);
	if (ddb_buffers_alloc(dev) < 0) {
		dev_info(dev->dev, "Could not allocate buffer memory\n");
		goto fail2;
	}
	if (ddb_ports_attach(dev) < 0)
		goto fail3;

	ddb_device_create(dev);

	if (dev->link[0].info->fan_num)	{
		ddbwritel(dev, 1, GPIO_DIRECTION);
		ddbwritel(dev, 1, GPIO_OUTPUT);
	}
	return 0;

fail3:
	dev_err(dev->dev, "fail3\n");
	ddb_ports_detach(dev);
	ddb_buffers_free(dev);
fail2:
	dev_err(dev->dev, "fail2\n");
	ddb_ports_release(dev);
	ddb_i2c_release(dev);
fail1:
	dev_err(dev->dev, "fail1\n");
	return -1;
}

void ddb_unmap(struct ddb *dev)
{
	if (dev->regs)
		iounmap(dev->regs);
	vfree(dev);
}
