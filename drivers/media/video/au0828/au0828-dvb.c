/*
 *  Driver for the Auvitek USB bridge
 *
 *  Copyright (c) 2008 Steven Toth <stoth@hauppauge.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/suspend.h>
#include <media/v4l2-common.h>

#include "au0828.h"

#include "au8522.h"
#include "xc5000.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

unsigned int dvb_debug = 1;

#define _dbg(level, fmt, arg...)\
	do { if (dvb_debug >= level)\
		printk(KERN_DEBUG "%s/0: " fmt, DRIVER_NAME, ## arg);\
	} while (0)

static struct au8522_config hauppauge_hvr950q_config = {
	.demod_address = 0x8e >> 1,
	.status_mode   = AU8522_DEMODLOCKING,
};

static struct xc5000_config hauppauge_hvr950q_tunerconfig = {
	.i2c_address      = 0x61,
	.if_khz           = 6000,
	.tuner_callback   = au0828_tuner_callback
};

/*-------------------------------------------------------------------*/
static void urb_completion(struct urb *purb)
{
	u8 *ptr;
	struct au0828_dev *dev = purb->context;
	int ptype = usb_pipetype(purb->pipe);

	if (dev->urb_streaming == 0)
		return;

	if (ptype != PIPE_BULK) {
		printk(KERN_ERR "%s() Unsupported URB type %d\n", __FUNCTION__, ptype);
		return;
	}

	ptr = (u8 *)purb->transfer_buffer;

	/* Feed the transport payload into the kernel demux */
	dvb_dmx_swfilter_packets(&dev->dvb.demux, purb->transfer_buffer, purb->actual_length / 188);

	/* Clean the buffer before we requeue */
	memset(purb->transfer_buffer, 0, URB_BUFSIZE);

	/* Requeue URB */
	usb_submit_urb(purb, GFP_ATOMIC);
}

static int stop_urb_transfer(struct au0828_dev *dev)
{
	int i;

	printk(KERN_INFO "%s()\n", __FUNCTION__);

	/* FIXME:  Do we need to free the transfer_buffers? */
	for (i = 0; i < URB_COUNT; i++) {
		usb_kill_urb(dev->urbs[i]);
		kfree(dev->urbs[i]->transfer_buffer);
		usb_free_urb(dev->urbs[i]);
	}

	dev->urb_streaming = 0;

	return 0;
}

#define _AU0828_BULKPIPE 0x83
#define _BULKPIPESIZE 0xe522

static int start_urb_transfer(struct au0828_dev *dev)
{
	struct urb *purb;
	int i, ret = -ENOMEM;
	unsigned int pipe = usb_rcvbulkpipe(dev->usbdev, _AU0828_BULKPIPE);
	int pipesize = usb_maxpacket(dev->usbdev, pipe, usb_pipeout(pipe));
	int packets = _BULKPIPESIZE / pipesize;
	int transfer_buflen = packets * pipesize;

	printk(KERN_INFO "%s() transfer_buflen = %d\n", __FUNCTION__, transfer_buflen);

	if (dev->urb_streaming) {
		printk("%s: iso xfer already running!\n", __FUNCTION__);
		return 0;
	}

	for (i = 0; i < URB_COUNT; i++) {

		dev->urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!dev->urbs[i]) {
			goto err;
		}

		purb = dev->urbs[i];

		purb->transfer_buffer = kzalloc(URB_BUFSIZE, GFP_KERNEL);
		if (!purb->transfer_buffer) {
			usb_free_urb(purb);
			dev->urbs[i] = 0;
			goto err;
		}

		purb->status = -EINPROGRESS;
		usb_fill_bulk_urb(purb,
				  dev->usbdev,
				  usb_rcvbulkpipe(dev->usbdev, _AU0828_BULKPIPE),
				  purb->transfer_buffer,
				  URB_BUFSIZE,
				  urb_completion,
				  dev);

	}

	for (i = 0; i < URB_COUNT; i++) {
		ret = usb_submit_urb(dev->urbs[i], GFP_ATOMIC);
		if (ret != 0) {
			stop_urb_transfer(dev);
			printk("%s: failed urb submission, err = %d\n", __FUNCTION__, ret);
			return ret;
		}
	}

	dev->urb_streaming = 1;
	ret = 0;

err:
	return ret;
}

static int au0828_dvb_start_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct au0828_dev *dev = (struct au0828_dev *) demux->priv;
	struct au0828_dvb *dvb = &dev->dvb;
	int ret = 0;

	printk(KERN_INFO "%s() pid = 0x%x index = %d\n", __FUNCTION__, feed->pid, feed->index);

	if (!demux->dmx.frontend)
		return -EINVAL;

	printk(KERN_INFO "%s() Preparing, feeding = %d\n", __FUNCTION__, dvb->feeding);
	if (dvb) {
		mutex_lock(&dvb->lock);
		if (dvb->feeding++ == 0) {
			printk(KERN_INFO "%s() Starting Transport DMA\n",
				__FUNCTION__);
			au0828_write(dev, 0x608, 0x90);
			au0828_write(dev, 0x609, 0x72);
			au0828_write(dev, 0x60a, 0x71);
			au0828_write(dev, 0x60b, 0x01);
			ret = start_urb_transfer(dev);
		}
		mutex_unlock(&dvb->lock);
	}

	return ret;
}

static int au0828_dvb_stop_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct au0828_dev *dev = (struct au0828_dev *) demux->priv;
	struct au0828_dvb *dvb = &dev->dvb;
	int ret = 0;

	printk(KERN_INFO "%s() pid = 0x%x index = %d\n", __FUNCTION__, feed->pid, feed->index);

	if (dvb) {
		mutex_lock(&dvb->lock);
		if (--dvb->feeding == 0) {
			printk(KERN_INFO "%s() Stopping Transport DMA\n",
				__FUNCTION__);
			au0828_write(dev, 0x608, 0x00);
			au0828_write(dev, 0x609, 0x00);
			au0828_write(dev, 0x60a, 0x00);
			au0828_write(dev, 0x60b, 0x00);
			ret = stop_urb_transfer(dev);
		}
		mutex_unlock(&dvb->lock);
	}

	return ret;
}

int dvb_register(struct au0828_dev *dev)
{
	struct au0828_dvb *dvb = &dev->dvb;
	int result;

	/* register adapter */
	result = dvb_register_adapter(&dvb->adapter, DRIVER_NAME, THIS_MODULE,
				      &dev->usbdev->dev, adapter_nr);
	if (result < 0) {
		printk(KERN_WARNING "%s: dvb_register_adapter failed (errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_adapter;
	}
	dvb->adapter.priv = dev;

	/* register frontend */
	result = dvb_register_frontend(&dvb->adapter, dvb->frontend);
	if (result < 0) {
		printk(KERN_WARNING "%s: dvb_register_frontend failed (errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_frontend;
	}

	/* register demux stuff */
	dvb->demux.dmx.capabilities =
		DMX_TS_FILTERING | DMX_SECTION_FILTERING |
		DMX_MEMORY_BASED_FILTERING;
	dvb->demux.priv       = dev;
	dvb->demux.filternum  = 256;
	dvb->demux.feednum    = 256;
	dvb->demux.start_feed = au0828_dvb_start_feed;
	dvb->demux.stop_feed  = au0828_dvb_stop_feed;
	result = dvb_dmx_init(&dvb->demux);
	if (result < 0) {
		printk(KERN_WARNING "%s: dvb_dmx_init failed (errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_dmx;
	}

	dvb->dmxdev.filternum    = 256;
	dvb->dmxdev.demux        = &dvb->demux.dmx;
	dvb->dmxdev.capabilities = 0;
	result = dvb_dmxdev_init(&dvb->dmxdev, &dvb->adapter);
	if (result < 0) {
		printk(KERN_WARNING "%s: dvb_dmxdev_init failed (errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_dmxdev;
	}

	dvb->fe_hw.source = DMX_FRONTEND_0;
	result = dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	if (result < 0) {
		printk(KERN_WARNING "%s: add_frontend failed (DMX_FRONTEND_0, errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_fe_hw;
	}

	dvb->fe_mem.source = DMX_MEMORY_FE;
	result = dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->fe_mem);
	if (result < 0) {
		printk(KERN_WARNING "%s: add_frontend failed (DMX_MEMORY_FE, errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_fe_mem;
	}

	result = dvb->demux.dmx.connect_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	if (result < 0) {
		printk(KERN_WARNING "%s: connect_frontend failed (errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_fe_conn;
	}

	/* register network adapter */
	dvb_net_init(&dvb->adapter, &dvb->net, &dvb->demux.dmx);
	return 0;

fail_fe_conn:
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_mem);
fail_fe_mem:
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_hw);
fail_fe_hw:
	dvb_dmxdev_release(&dvb->dmxdev);
fail_dmxdev:
	dvb_dmx_release(&dvb->demux);
fail_dmx:
	dvb_unregister_frontend(dvb->frontend);
fail_frontend:
	dvb_frontend_detach(dvb->frontend);
	dvb_unregister_adapter(&dvb->adapter);
fail_adapter:
	return result;
}

void au0828_dvb_unregister(struct au0828_dev *dev)
{
	struct au0828_dvb *dvb = &dev->dvb;

	dvb_net_release(&dvb->net);
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_mem);
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	dvb_dmxdev_release(&dvb->dmxdev);
	dvb_dmx_release(&dvb->demux);
	dvb_unregister_frontend(dvb->frontend);
	dvb_frontend_detach(dvb->frontend);
	dvb_unregister_adapter(&dvb->adapter);
}

/* All the DVB attach calls go here, this function get's modified
 * for each new card. No other function in this file needs
 * to change.
 */
int au0828_dvb_register(struct au0828_dev *dev)
{
	struct au0828_dvb *dvb = &dev->dvb;
	int ret;

	/* init frontend */
	switch (dev->board) {
	case AU0828_BOARD_HAUPPAUGE_HVR850:
	case AU0828_BOARD_HAUPPAUGE_HVR950Q:
	case AU0828_BOARD_DVICO_FUSIONHDTV7:
		dvb->frontend = dvb_attach(au8522_attach,
				&hauppauge_hvr950q_config,
				&dev->i2c_adap);
		if (dvb->frontend != NULL) {
			hauppauge_hvr950q_tunerconfig.priv = dev;
			dvb_attach(xc5000_attach, dvb->frontend,
				&dev->i2c_adap,
				&hauppauge_hvr950q_tunerconfig);
		}
		break;
	default:
		printk("The frontend of your DVB/ATSC card isn't supported yet\n");
		break;
	}
	if (NULL == dvb->frontend) {
		printk("Frontend initialization failed\n");
		return -1;
	}

	/* Put the analog decoder in standby to keep it quiet */
	au0828_call_i2c_clients(dev, TUNER_SET_STANDBY, NULL);

	if (dvb->frontend->ops.analog_ops.standby)
		dvb->frontend->ops.analog_ops.standby(dvb->frontend);

	/* register everything */
	ret = dvb_register(dev);
	if (ret < 0) {
		if (dvb->frontend->ops.release)
			dvb->frontend->ops.release(dvb->frontend);
		return ret;
	}

	return 0;
}
