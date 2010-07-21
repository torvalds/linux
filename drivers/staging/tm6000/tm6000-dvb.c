/*
   tm6000-dvb.c - dvb-t support for TM5600/TM6000/TM6010 USB video capture devices

   Copyright (C) 2007 Michel Ludwig <michel.ludwig@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation version 2

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include "tm6000.h"
#include "tm6000-regs.h"

#include "zl10353.h"

#include <media/tuner.h>

#include "tuner-xc2028.h"
#include "xc5000.h"

static void inline print_err_status (struct tm6000_core *dev,
				     int packet, int status)
{
	char *errmsg = "Unknown";

	switch(status) {
	case -ENOENT:
		errmsg = "unlinked synchronuously";
		break;
	case -ECONNRESET:
		errmsg = "unlinked asynchronuously";
		break;
	case -ENOSR:
		errmsg = "Buffer error (overrun)";
		break;
	case -EPIPE:
		errmsg = "Stalled (device not responding)";
		break;
	case -EOVERFLOW:
		errmsg = "Babble (bad cable?)";
		break;
	case -EPROTO:
		errmsg = "Bit-stuff error (bad cable?)";
		break;
	case -EILSEQ:
		errmsg = "CRC/Timeout (could be anything)";
		break;
	case -ETIME:
		errmsg = "Device does not respond";
		break;
	}
	if (packet<0) {
		dprintk(dev, 1, "URB status %d [%s].\n",
			status, errmsg);
	} else {
		dprintk(dev, 1, "URB packet %d, status %d [%s].\n",
			packet, status, errmsg);
	}
}

static void tm6000_urb_received(struct urb *urb)
{
	int ret;
	struct tm6000_core* dev = urb->context;

	if(urb->status != 0) {
		print_err_status (dev,0,urb->status);
	}
	else if(urb->actual_length>0){
		dvb_dmx_swfilter(&dev->dvb->demux, urb->transfer_buffer,
						   urb->actual_length);
	}

	if(dev->dvb->streams > 0) {
		ret = usb_submit_urb(urb, GFP_ATOMIC);
		if(ret < 0) {
			printk(KERN_ERR "tm6000:  error %s\n", __FUNCTION__);
			kfree(urb->transfer_buffer);
			usb_free_urb(urb);
		}
	}
}

int tm6000_start_stream(struct tm6000_core *dev)
{
	int ret;
	unsigned int pipe, size;
	struct tm6000_dvb *dvb = dev->dvb;

	printk(KERN_INFO "tm6000: got start stream request %s\n",__FUNCTION__);

	if (dev->mode != TM6000_MODE_DIGITAL) {
		tm6000_init_digital_mode(dev);
		dev->mode = TM6000_MODE_DIGITAL;
	}

	dvb->bulk_urb = usb_alloc_urb(0, GFP_KERNEL);
	if(dvb->bulk_urb == NULL) {
		printk(KERN_ERR "tm6000: couldn't allocate urb\n");
		return -ENOMEM;
	}

	pipe = usb_rcvbulkpipe(dev->udev, dev->bulk_in.endp->desc.bEndpointAddress
							  & USB_ENDPOINT_NUMBER_MASK);

	size = usb_maxpacket(dev->udev, pipe, usb_pipeout(pipe));
	size = size * 15; /* 512 x 8 or 12 or 15 */

	dvb->bulk_urb->transfer_buffer = kzalloc(size, GFP_KERNEL);
	if(dvb->bulk_urb->transfer_buffer == NULL) {
		usb_free_urb(dvb->bulk_urb);
		printk(KERN_ERR "tm6000: couldn't allocate transfer buffer!\n");
		return -ENOMEM;
	}

	usb_fill_bulk_urb(dvb->bulk_urb, dev->udev, pipe,
						 dvb->bulk_urb->transfer_buffer,
						 size,
						 tm6000_urb_received, dev);

	ret = usb_clear_halt(dev->udev, pipe);
	if(ret < 0) {
		printk(KERN_ERR "tm6000: error %i in %s during pipe reset\n",ret,__FUNCTION__);
		return ret;
	}
	else {
		printk(KERN_ERR "tm6000: pipe resetted\n");
	}

/*	mutex_lock(&tm6000_driver.open_close_mutex); */
	ret = usb_submit_urb(dvb->bulk_urb, GFP_KERNEL);

/*	mutex_unlock(&tm6000_driver.open_close_mutex); */
	if (ret) {
		printk(KERN_ERR "tm6000: submit of urb failed (error=%i)\n",ret);

		kfree(dvb->bulk_urb->transfer_buffer);
		usb_free_urb(dvb->bulk_urb);
		return ret;
	}

	return 0;
}

void tm6000_stop_stream(struct tm6000_core *dev)
{
	struct tm6000_dvb *dvb = dev->dvb;

	if(dvb->bulk_urb) {
		printk (KERN_INFO "urb killing\n");
		usb_kill_urb(dvb->bulk_urb);
		printk (KERN_INFO "urb buffer free\n");
		kfree(dvb->bulk_urb->transfer_buffer);
		usb_free_urb(dvb->bulk_urb);
		dvb->bulk_urb = NULL;
	}
}

int tm6000_start_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux = feed->demux;
	struct tm6000_core *dev = demux->priv;
	struct tm6000_dvb *dvb = dev->dvb;
	printk(KERN_INFO "tm6000: got start feed request %s\n",__FUNCTION__);

	mutex_lock(&dvb->mutex);
	if(dvb->streams == 0) {
		dvb->streams = 1;
/*		mutex_init(&tm6000_dev->streming_mutex); */
		tm6000_start_stream(dev);
	}
	else {
		++(dvb->streams);
	}
	mutex_unlock(&dvb->mutex);

	return 0;
}

int tm6000_stop_feed(struct dvb_demux_feed *feed) {
	struct dvb_demux *demux = feed->demux;
	struct tm6000_core *dev = demux->priv;
	struct tm6000_dvb *dvb = dev->dvb;

	printk(KERN_INFO "tm6000: got stop feed request %s\n",__FUNCTION__);

	mutex_lock(&dvb->mutex);

	printk (KERN_INFO "stream %#x\n", dvb->streams);
	--(dvb->streams);
	if(dvb->streams == 0) {
		printk (KERN_INFO "stop stream\n");
		tm6000_stop_stream(dev);
/*		mutex_destroy(&tm6000_dev->streaming_mutex); */
	}
	mutex_unlock(&dvb->mutex);
/*	mutex_destroy(&tm6000_dev->streaming_mutex); */

	return 0;
}

int tm6000_dvb_attach_frontend(struct tm6000_core *dev)
{
	struct tm6000_dvb *dvb = dev->dvb;

	if(dev->caps.has_zl10353) {
		struct zl10353_config config =
				    {.demod_address = dev->demod_addr,
				     .no_tuner = 1,
				     .parallel_ts = 1,
				     .if2 = 45700,
				     .disable_i2c_gate_ctrl = 1,
				    };

		dvb->frontend = dvb_attach(zl10353_attach, &config,
							   &dev->i2c_adap);
	}
	else {
		printk(KERN_ERR "tm6000: no frontend defined for the device!\n");
		return -1;
	}

	return (!dvb->frontend) ? -1 : 0;
}

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

int tm6000_dvb_register(struct tm6000_core *dev)
{
	int ret = -1;
	struct tm6000_dvb *dvb = dev->dvb;

	mutex_init(&dvb->mutex);

	dvb->streams = 0;

	/* attach the frontend */
	ret = tm6000_dvb_attach_frontend(dev);
	if(ret < 0) {
		printk(KERN_ERR "tm6000: couldn't attach the frontend!\n");
		goto err;
	}

	ret = dvb_register_adapter(&dvb->adapter, "Trident TVMaster 6000 DVB-T",
							  THIS_MODULE, &dev->udev->dev, adapter_nr);
	dvb->adapter.priv = dev;

	if (dvb->frontend) {
		switch (dev->tuner_type) {
		case TUNER_XC2028: {
			struct xc2028_config cfg = {
				.i2c_adap = &dev->i2c_adap,
				.i2c_addr = dev->tuner_addr,
			};

			dvb->frontend->callback = tm6000_tuner_callback;
			ret = dvb_register_frontend(&dvb->adapter, dvb->frontend);
			if (ret < 0) {
				printk(KERN_ERR
					"tm6000: couldn't register frontend\n");
				goto adapter_err;
			}

			if (!dvb_attach(xc2028_attach, dvb->frontend, &cfg)) {
				printk(KERN_ERR "tm6000: couldn't register "
						"frontend (xc3028)\n");
				ret = -EINVAL;
				goto frontend_err;
			}
			printk(KERN_INFO "tm6000: XC2028/3028 asked to be "
					 "attached to frontend!\n");
			break;
			}
		case TUNER_XC5000: {
			struct xc5000_config cfg = {
				.i2c_address = dev->tuner_addr,
			};

			dvb->frontend->callback = tm6000_xc5000_callback;
			ret = dvb_register_frontend(&dvb->adapter, dvb->frontend);
			if (ret < 0) {
				printk(KERN_ERR
					"tm6000: couldn't register frontend\n");
				goto adapter_err;
			}

			if (!dvb_attach(xc5000_attach, dvb->frontend, &dev->i2c_adap, &cfg)) {
				printk(KERN_ERR "tm6000: couldn't register "
						"frontend (xc5000)\n");
				ret = -EINVAL;
				goto frontend_err;
			}
			printk(KERN_INFO "tm6000: XC5000 asked to be "
					 "attached to frontend!\n");
			break;
			}
		}
	} else {
		printk(KERN_ERR "tm6000: no frontend found\n");
	}

	dvb->demux.dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING
							    | DMX_MEMORY_BASED_FILTERING;
	dvb->demux.priv = dev;
	dvb->demux.filternum = 8;
	dvb->demux.feednum = 8;
	dvb->demux.start_feed = tm6000_start_feed;
	dvb->demux.stop_feed = tm6000_stop_feed;
	dvb->demux.write_to_decoder = NULL;
	ret = dvb_dmx_init(&dvb->demux);
	if(ret < 0) {
		printk("tm6000: dvb_dmx_init failed (errno = %d)\n", ret);
		goto frontend_err;
	}

	dvb->dmxdev.filternum = dev->dvb->demux.filternum;
	dvb->dmxdev.demux = &dev->dvb->demux.dmx;
	dvb->dmxdev.capabilities = 0;

	ret =  dvb_dmxdev_init(&dvb->dmxdev, &dvb->adapter);
	if(ret < 0) {
		printk("tm6000: dvb_dmxdev_init failed (errno = %d)\n", ret);
		goto dvb_dmx_err;
	}

	return 0;

dvb_dmx_err:
	dvb_dmx_release(&dvb->demux);
frontend_err:
	if(dvb->frontend) {
		dvb_frontend_detach(dvb->frontend);
		dvb_unregister_frontend(dvb->frontend);
	}
adapter_err:
	dvb_unregister_adapter(&dvb->adapter);
err:
	return ret;
}

void tm6000_dvb_unregister(struct tm6000_core *dev)
{
	struct tm6000_dvb *dvb = dev->dvb;

	if(dvb->bulk_urb != NULL) {
		struct urb *bulk_urb = dvb->bulk_urb;

		kfree(bulk_urb->transfer_buffer);
		bulk_urb->transfer_buffer = NULL;
		usb_unlink_urb(bulk_urb);
		usb_free_urb(bulk_urb);
	}

/*	mutex_lock(&tm6000_driver.open_close_mutex); */
	if(dvb->frontend) {
		dvb_frontend_detach(dvb->frontend);
		dvb_unregister_frontend(dvb->frontend);
	}

	dvb_dmxdev_release(&dvb->dmxdev);
	dvb_dmx_release(&dvb->demux);
	dvb_unregister_adapter(&dvb->adapter);
	mutex_destroy(&dvb->mutex);
/*	mutex_unlock(&tm6000_driver.open_close_mutex); */

}
