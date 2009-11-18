/*
 *  Driver for the Auvitek USB bridge
 *
 *  Copyright (c) 2008 Steven Toth <stoth@linuxtv.org>
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
#include "mxl5007t.h"
#include "tda18271.h"

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define _AU0828_BULKPIPE 0x83
#define _BULKPIPESIZE 0xe522

static u8 hauppauge_hvr950q_led_states[] = {
	0x00, /* off */
	0x02, /* yellow */
	0x04, /* green */
};

static struct au8522_led_config hauppauge_hvr950q_led_cfg = {
	.gpio_output = 0x00e0,
	.gpio_output_enable  = 0x6006,
	.gpio_output_disable = 0x0660,

	.gpio_leds = 0x00e2,
	.led_states  = hauppauge_hvr950q_led_states,
	.num_led_states = sizeof(hauppauge_hvr950q_led_states),

	.vsb8_strong   = 20 /* dB */ * 10,
	.qam64_strong  = 25 /* dB */ * 10,
	.qam256_strong = 32 /* dB */ * 10,
};

static struct au8522_config hauppauge_hvr950q_config = {
	.demod_address = 0x8e >> 1,
	.status_mode   = AU8522_DEMODLOCKING,
	.qam_if        = AU8522_IF_6MHZ,
	.vsb_if        = AU8522_IF_6MHZ,
	.led_cfg       = &hauppauge_hvr950q_led_cfg,
};

static struct au8522_config fusionhdtv7usb_config = {
	.demod_address = 0x8e >> 1,
	.status_mode   = AU8522_DEMODLOCKING,
	.qam_if        = AU8522_IF_6MHZ,
	.vsb_if        = AU8522_IF_6MHZ,
};

static struct au8522_config hauppauge_woodbury_config = {
	.demod_address = 0x8e >> 1,
	.status_mode   = AU8522_DEMODLOCKING,
	.qam_if        = AU8522_IF_4MHZ,
	.vsb_if        = AU8522_IF_3_25MHZ,
};

static struct xc5000_config hauppauge_hvr950q_tunerconfig = {
	.i2c_address      = 0x61,
	.if_khz           = 6000,
};

static struct mxl5007t_config mxl5007t_hvr950q_config = {
	.xtal_freq_hz = MxL_XTAL_24_MHZ,
	.if_freq_hz = MxL_IF_6_MHZ,
};

static struct tda18271_config hauppauge_woodbury_tunerconfig = {
	.gate    = TDA18271_GATE_DIGITAL,
};

/*-------------------------------------------------------------------*/
static void urb_completion(struct urb *purb)
{
	u8 *ptr;
	struct au0828_dev *dev = purb->context;
	int ptype = usb_pipetype(purb->pipe);

	dprintk(2, "%s()\n", __func__);

	if (!dev)
		return;

	if (dev->urb_streaming == 0)
		return;

	if (ptype != PIPE_BULK) {
		printk(KERN_ERR "%s() Unsupported URB type %d\n",
		       __func__, ptype);
		return;
	}

	ptr = (u8 *)purb->transfer_buffer;

	/* Feed the transport payload into the kernel demux */
	dvb_dmx_swfilter_packets(&dev->dvb.demux,
		purb->transfer_buffer, purb->actual_length / 188);

	/* Clean the buffer before we requeue */
	memset(purb->transfer_buffer, 0, URB_BUFSIZE);

	/* Requeue URB */
	usb_submit_urb(purb, GFP_ATOMIC);
}

static int stop_urb_transfer(struct au0828_dev *dev)
{
	int i;

	dprintk(2, "%s()\n", __func__);

	for (i = 0; i < URB_COUNT; i++) {
		usb_kill_urb(dev->urbs[i]);
		kfree(dev->urbs[i]->transfer_buffer);
		usb_free_urb(dev->urbs[i]);
	}

	dev->urb_streaming = 0;

	return 0;
}

static int start_urb_transfer(struct au0828_dev *dev)
{
	struct urb *purb;
	int i, ret = -ENOMEM;

	dprintk(2, "%s()\n", __func__);

	if (dev->urb_streaming) {
		dprintk(2, "%s: bulk xfer already running!\n", __func__);
		return 0;
	}

	for (i = 0; i < URB_COUNT; i++) {

		dev->urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!dev->urbs[i])
			goto err;

		purb = dev->urbs[i];

		purb->transfer_buffer = kzalloc(URB_BUFSIZE, GFP_KERNEL);
		if (!purb->transfer_buffer) {
			usb_free_urb(purb);
			dev->urbs[i] = NULL;
			goto err;
		}

		purb->status = -EINPROGRESS;
		usb_fill_bulk_urb(purb,
				  dev->usbdev,
				  usb_rcvbulkpipe(dev->usbdev,
					_AU0828_BULKPIPE),
				  purb->transfer_buffer,
				  URB_BUFSIZE,
				  urb_completion,
				  dev);

	}

	for (i = 0; i < URB_COUNT; i++) {
		ret = usb_submit_urb(dev->urbs[i], GFP_ATOMIC);
		if (ret != 0) {
			stop_urb_transfer(dev);
			printk(KERN_ERR "%s: failed urb submission, "
			       "err = %d\n", __func__, ret);
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

	dprintk(1, "%s()\n", __func__);

	if (!demux->dmx.frontend)
		return -EINVAL;

	if (dvb) {
		mutex_lock(&dvb->lock);
		if (dvb->feeding++ == 0) {
			/* Start transport */
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

	dprintk(1, "%s()\n", __func__);

	if (dvb) {
		mutex_lock(&dvb->lock);
		if (--dvb->feeding == 0) {
			/* Stop transport */
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

static int dvb_register(struct au0828_dev *dev)
{
	struct au0828_dvb *dvb = &dev->dvb;
	int result;

	dprintk(1, "%s()\n", __func__);

	/* register adapter */
	result = dvb_register_adapter(&dvb->adapter, DRIVER_NAME, THIS_MODULE,
				      &dev->usbdev->dev, adapter_nr);
	if (result < 0) {
		printk(KERN_ERR "%s: dvb_register_adapter failed "
		       "(errno = %d)\n", DRIVER_NAME, result);
		goto fail_adapter;
	}
	dvb->adapter.priv = dev;

	/* register frontend */
	result = dvb_register_frontend(&dvb->adapter, dvb->frontend);
	if (result < 0) {
		printk(KERN_ERR "%s: dvb_register_frontend failed "
		       "(errno = %d)\n", DRIVER_NAME, result);
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
		printk(KERN_ERR "%s: dvb_dmx_init failed (errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_dmx;
	}

	dvb->dmxdev.filternum    = 256;
	dvb->dmxdev.demux        = &dvb->demux.dmx;
	dvb->dmxdev.capabilities = 0;
	result = dvb_dmxdev_init(&dvb->dmxdev, &dvb->adapter);
	if (result < 0) {
		printk(KERN_ERR "%s: dvb_dmxdev_init failed (errno = %d)\n",
		       DRIVER_NAME, result);
		goto fail_dmxdev;
	}

	dvb->fe_hw.source = DMX_FRONTEND_0;
	result = dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	if (result < 0) {
		printk(KERN_ERR "%s: add_frontend failed "
		       "(DMX_FRONTEND_0, errno = %d)\n", DRIVER_NAME, result);
		goto fail_fe_hw;
	}

	dvb->fe_mem.source = DMX_MEMORY_FE;
	result = dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->fe_mem);
	if (result < 0) {
		printk(KERN_ERR "%s: add_frontend failed "
		       "(DMX_MEMORY_FE, errno = %d)\n", DRIVER_NAME, result);
		goto fail_fe_mem;
	}

	result = dvb->demux.dmx.connect_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	if (result < 0) {
		printk(KERN_ERR "%s: connect_frontend failed (errno = %d)\n",
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

	dprintk(1, "%s()\n", __func__);

	if (dvb->frontend == NULL)
		return;

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

	dprintk(1, "%s()\n", __func__);

	/* init frontend */
	switch (dev->boardnr) {
	case AU0828_BOARD_HAUPPAUGE_HVR850:
	case AU0828_BOARD_HAUPPAUGE_HVR950Q:
		dvb->frontend = dvb_attach(au8522_attach,
				&hauppauge_hvr950q_config,
				&dev->i2c_adap);
		if (dvb->frontend != NULL)
			dvb_attach(xc5000_attach, dvb->frontend, &dev->i2c_adap,
				   &hauppauge_hvr950q_tunerconfig);
		break;
	case AU0828_BOARD_HAUPPAUGE_HVR950Q_MXL:
		dvb->frontend = dvb_attach(au8522_attach,
				&hauppauge_hvr950q_config,
				&dev->i2c_adap);
		if (dvb->frontend != NULL)
			dvb_attach(mxl5007t_attach, dvb->frontend,
				   &dev->i2c_adap, 0x60,
				   &mxl5007t_hvr950q_config);
		break;
	case AU0828_BOARD_HAUPPAUGE_WOODBURY:
		dvb->frontend = dvb_attach(au8522_attach,
				&hauppauge_woodbury_config,
				&dev->i2c_adap);
		if (dvb->frontend != NULL)
			dvb_attach(tda18271_attach, dvb->frontend,
				   0x60, &dev->i2c_adap,
				   &hauppauge_woodbury_tunerconfig);
		break;
	case AU0828_BOARD_DVICO_FUSIONHDTV7:
		dvb->frontend = dvb_attach(au8522_attach,
				&fusionhdtv7usb_config,
				&dev->i2c_adap);
		if (dvb->frontend != NULL) {
			dvb_attach(xc5000_attach, dvb->frontend,
				&dev->i2c_adap,
				&hauppauge_hvr950q_tunerconfig);
		}
		break;
	default:
		printk(KERN_WARNING "The frontend of your DVB/ATSC card "
		       "isn't supported yet\n");
		break;
	}
	if (NULL == dvb->frontend) {
		printk(KERN_ERR "%s() Frontend initialization failed\n",
		       __func__);
		return -1;
	}
	/* define general-purpose callback pointer */
	dvb->frontend->callback = au0828_tuner_callback;

	/* register everything */
	ret = dvb_register(dev);
	if (ret < 0) {
		if (dvb->frontend->ops.release)
			dvb->frontend->ops.release(dvb->frontend);
		return ret;
	}

	return 0;
}
