/*
 * ngene-dvb.c: nGene PCIe bridge driver - DVB functions
 *
 * Copyright (C) 2005-2007 Micronas
 *
 * Copyright (C) 2008-2009 Ralph Metzler <rjkm@metzlerbros.de>
 *                         Modifications for new nGene firmware,
 *                         support for EEPROM-copying,
 *                         support for new dual DVB-S2 card prototype
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
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/io.h>
#include <asm/div64.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <linux/byteorder/generic.h>
#include <linux/firmware.h>
#include <linux/vmalloc.h>

#include "ngene.h"

static int ci_tsfix = 1;
module_param(ci_tsfix, int, 0444);
MODULE_PARM_DESC(ci_tsfix, "Detect and fix TS buffer offset shifs in conjunction with CI expansions (default: 1/enabled)");

/****************************************************************************/
/* COMMAND API interface ****************************************************/
/****************************************************************************/

static ssize_t ts_write(struct file *file, const char __user *buf,
			size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ngene_channel *chan = dvbdev->priv;
	struct ngene *dev = chan->dev;

	if (wait_event_interruptible(dev->tsout_rbuf.queue,
				     dvb_ringbuffer_free
				     (&dev->tsout_rbuf) >= count) < 0)
		return 0;

	dvb_ringbuffer_write_user(&dev->tsout_rbuf, buf, count);

	return count;
}

static ssize_t ts_read(struct file *file, char __user *buf,
		       size_t count, loff_t *ppos)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ngene_channel *chan = dvbdev->priv;
	struct ngene *dev = chan->dev;
	int left, avail;

	left = count;
	while (left) {
		if (wait_event_interruptible(
			    dev->tsin_rbuf.queue,
			    dvb_ringbuffer_avail(&dev->tsin_rbuf) > 0) < 0)
			return -EAGAIN;
		avail = dvb_ringbuffer_avail(&dev->tsin_rbuf);
		if (avail > left)
			avail = left;
		dvb_ringbuffer_read_user(&dev->tsin_rbuf, buf, avail);
		left -= avail;
		buf += avail;
	}
	return count;
}

static __poll_t ts_poll(struct file *file, poll_table *wait)
{
	struct dvb_device *dvbdev = file->private_data;
	struct ngene_channel *chan = dvbdev->priv;
	struct ngene *dev = chan->dev;
	struct dvb_ringbuffer *rbuf = &dev->tsin_rbuf;
	struct dvb_ringbuffer *wbuf = &dev->tsout_rbuf;
	__poll_t mask = 0;

	poll_wait(file, &rbuf->queue, wait);
	poll_wait(file, &wbuf->queue, wait);

	if (!dvb_ringbuffer_empty(rbuf))
		mask |= EPOLLIN | EPOLLRDNORM;
	if (dvb_ringbuffer_free(wbuf) >= 188)
		mask |= EPOLLOUT | EPOLLWRNORM;

	return mask;
}

static const struct file_operations ci_fops = {
	.owner   = THIS_MODULE,
	.read    = ts_read,
	.write   = ts_write,
	.open    = dvb_generic_open,
	.release = dvb_generic_release,
	.poll    = ts_poll,
	.mmap    = NULL,
};

struct dvb_device ngene_dvbdev_ci = {
	.priv    = NULL,
	.readers = 1,
	.writers = 1,
	.users   = 2,
	.fops    = &ci_fops,
};


/****************************************************************************/
/* DVB functions and API interface ******************************************/
/****************************************************************************/

static void swap_buffer(u32 *p, u32 len)
{
	while (len) {
		*p = swab32(*p);
		p++;
		len -= 4;
	}
}

/* start of filler packet */
static u8 fill_ts[] = { 0x47, 0x1f, 0xff, 0x10, TS_FILLER };

static int tsin_find_offset(void *buf, u32 len)
{
	int i, l;

	l = len - sizeof(fill_ts);
	if (l <= 0)
		return -1;

	for (i = 0; i < l; i++) {
		if (((char *)buf)[i] == 0x47) {
			if (!memcmp(buf + i, fill_ts, sizeof(fill_ts)))
				return i % 188;
		}
	}

	return -1;
}

static inline void tsin_copy_stripped(struct ngene *dev, void *buf)
{
	if (memcmp(buf, fill_ts, sizeof(fill_ts)) != 0) {
		if (dvb_ringbuffer_free(&dev->tsin_rbuf) >= 188) {
			dvb_ringbuffer_write(&dev->tsin_rbuf, buf, 188);
			wake_up(&dev->tsin_rbuf.queue);
		}
	}
}

void *tsin_exchange(void *priv, void *buf, u32 len, u32 clock, u32 flags)
{
	struct ngene_channel *chan = priv;
	struct ngene *dev = chan->dev;
	int tsoff;

	if (flags & DF_SWAP32)
		swap_buffer(buf, len);

	if (dev->ci.en && chan->number == 2) {
		/* blindly copy buffers if ci_tsfix is disabled */
		if (!ci_tsfix) {
			while (len >= 188) {
				tsin_copy_stripped(dev, buf);

				buf += 188;
				len -= 188;
			}
			return NULL;
		}

		/* ci_tsfix = 1 */

		/*
		 * since the remainder of the TS packet which got cut off
		 * in the previous tsin_exchange() run is at the beginning
		 * of the new TS buffer, append this to the temp buffer and
		 * send it to the DVB ringbuffer afterwards.
		 */
		if (chan->tsin_offset) {
			memcpy(&chan->tsin_buffer[(188 - chan->tsin_offset)],
			       buf, chan->tsin_offset);
			tsin_copy_stripped(dev, &chan->tsin_buffer);

			buf += chan->tsin_offset;
			len -= chan->tsin_offset;
		}

		/*
		 * copy TS packets to the DVB ringbuffer and detect new offset
		 * shifts by checking for a valid TS SYNC byte
		 */
		while (len >= 188) {
			if (*((char *)buf) != 0x47) {
				/*
				 * no SYNC header, find new offset shift
				 * (max. 188 bytes, tsoff will be mod 188)
				 */
				tsoff = tsin_find_offset(buf, len);
				if (tsoff > 0) {
					chan->tsin_offset += tsoff;
					chan->tsin_offset %= 188;

					buf += tsoff;
					len -= tsoff;

					dev_info(&dev->pci_dev->dev,
						 "%s(): tsin_offset shift by %d on channel %d\n",
						 __func__, tsoff,
						 chan->number);

					/*
					 * offset corrected. re-check remaining
					 * len for a full TS frame, break and
					 * skip to fragment handling if < 188.
					 */
					if (len < 188)
						break;
				}
			}

			tsin_copy_stripped(dev, buf);

			buf += 188;
			len -= 188;
		}

		/*
		 * if a fragment is left, copy to temp buffer. The remainder
		 * will be appended in the next tsin_exchange() iteration.
		 */
		if (len > 0 && len < 188)
			memcpy(&chan->tsin_buffer, buf, len);

		return NULL;
	}

	if (chan->users > 0)
		dvb_dmx_swfilter(&chan->demux, buf, len);

	return NULL;
}

void *tsout_exchange(void *priv, void *buf, u32 len, u32 clock, u32 flags)
{
	struct ngene_channel *chan = priv;
	struct ngene *dev = chan->dev;
	u32 alen;

	alen = dvb_ringbuffer_avail(&dev->tsout_rbuf);
	alen -= alen % 188;

	if (alen < len)
		FillTSBuffer(buf + alen, len - alen, flags);
	else
		alen = len;
	dvb_ringbuffer_read(&dev->tsout_rbuf, buf, alen);
	if (flags & DF_SWAP32)
		swap_buffer((u32 *)buf, alen);
	wake_up_interruptible(&dev->tsout_rbuf.queue);
	return buf;
}



int ngene_start_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct ngene_channel *chan = dvbdmx->priv;

	if (chan->users == 0) {
		if (!chan->dev->cmd_timeout_workaround || !chan->running)
			set_transfer(chan, 1);
	}

	return ++chan->users;
}

int ngene_stop_feed(struct dvb_demux_feed *dvbdmxfeed)
{
	struct dvb_demux *dvbdmx = dvbdmxfeed->demux;
	struct ngene_channel *chan = dvbdmx->priv;

	if (--chan->users)
		return chan->users;

	if (!chan->dev->cmd_timeout_workaround)
		set_transfer(chan, 0);

	return 0;
}

int my_dvb_dmx_ts_card_init(struct dvb_demux *dvbdemux, char *id,
			    int (*start_feed)(struct dvb_demux_feed *),
			    int (*stop_feed)(struct dvb_demux_feed *),
			    void *priv)
{
	dvbdemux->priv = priv;

	dvbdemux->filternum = 256;
	dvbdemux->feednum = 256;
	dvbdemux->start_feed = start_feed;
	dvbdemux->stop_feed = stop_feed;
	dvbdemux->write_to_decoder = NULL;
	dvbdemux->dmx.capabilities = (DMX_TS_FILTERING |
				      DMX_SECTION_FILTERING |
				      DMX_MEMORY_BASED_FILTERING);
	return dvb_dmx_init(dvbdemux);
}

int my_dvb_dmxdev_ts_card_init(struct dmxdev *dmxdev,
			       struct dvb_demux *dvbdemux,
			       struct dmx_frontend *hw_frontend,
			       struct dmx_frontend *mem_frontend,
			       struct dvb_adapter *dvb_adapter)
{
	int ret;

	dmxdev->filternum = 256;
	dmxdev->demux = &dvbdemux->dmx;
	dmxdev->capabilities = 0;
	ret = dvb_dmxdev_init(dmxdev, dvb_adapter);
	if (ret < 0)
		return ret;

	hw_frontend->source = DMX_FRONTEND_0;
	dvbdemux->dmx.add_frontend(&dvbdemux->dmx, hw_frontend);
	mem_frontend->source = DMX_MEMORY_FE;
	dvbdemux->dmx.add_frontend(&dvbdemux->dmx, mem_frontend);
	return dvbdemux->dmx.connect_frontend(&dvbdemux->dmx, hw_frontend);
}
