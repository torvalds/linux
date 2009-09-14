/*
   tm6000-dvb.c - dvb-t support for TM5600/TM6000 USB video capture devices

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

#include <linux/usb.h>

#include "tm6000.h"
#include "tm6000-regs.h"

#include "hack.h"

#include "zl10353.h"

#include <media/tuner.h>

#include "tuner-xc2028.h"

static void tm6000_urb_received(struct urb *urb)
{
	int ret;
	struct tm6000_core* dev = urb->context;

	if(urb->status != 0){
		printk(KERN_ERR "tm6000: status != 0\n");
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
	unsigned int pipe, maxPaketSize;
	struct tm6000_dvb *dvb = dev->dvb;

	printk(KERN_INFO "tm6000: got start stream request %s\n",__FUNCTION__);

	tm6000_init_digital_mode(dev);

/*
	ret = tm6000_set_led_status(tm6000_dev, 0x1);
	if(ret < 0) {
		return -1;
	}
*/

	dvb->bulk_urb = usb_alloc_urb(0, GFP_KERNEL);
	if(dvb->bulk_urb == NULL) {
		printk(KERN_ERR "tm6000: couldn't allocate urb\n");
		return -ENOMEM;
	}

	maxPaketSize = dev->bulk_in->desc.wMaxPacketSize;

	dvb->bulk_urb->transfer_buffer = kzalloc(maxPaketSize, GFP_KERNEL);
	if(dvb->bulk_urb->transfer_buffer == NULL) {
		usb_free_urb(dvb->bulk_urb);
		printk(KERN_ERR "tm6000: couldn't allocate transfer buffer!\n");
		return -ENOMEM;
	}

	pipe = usb_rcvbulkpipe(dev->udev, dev->bulk_in->desc.bEndpointAddress
							  & USB_ENDPOINT_NUMBER_MASK);

	usb_fill_bulk_urb(dvb->bulk_urb, dev->udev, pipe,
						 dvb->bulk_urb->transfer_buffer,
						 maxPaketSize,
						 tm6000_urb_received, dev);

	ret = usb_set_interface(dev->udev, 0, 1);
	if(ret < 0) {
		printk(KERN_ERR "tm6000: error %i in %s during set interface\n", ret, __FUNCTION__);
		return ret;
	}

	ret = usb_clear_halt(dev->udev, pipe);
	if(ret < 0) {
		printk(KERN_ERR "tm6000: error %i in %s during pipe reset\n",ret,__FUNCTION__);
		return ret;
	}
	else {
		printk(KERN_ERR "tm6000: pipe resetted\n");
	}

// 	mutex_lock(&tm6000_driver.open_close_mutex);
	ret = usb_submit_urb(dvb->bulk_urb, GFP_KERNEL);


// 	mutex_unlock(&tm6000_driver.open_close_mutex);
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
	int ret;
	struct tm6000_dvb *dvb = dev->dvb;

// 	tm6000_set_led_status(tm6000_dev, 0x0);

	ret = usb_set_interface(dev->udev, 0, 0);
	if(ret < 0) {
		printk(KERN_ERR "tm6000: error %i in %s during set interface\n",ret,__FUNCTION__);
	}

	if(dvb->bulk_urb) {
		usb_kill_urb(dvb->bulk_urb);
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
// 		mutex_init(&tm6000_dev->streaming_mutex);
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
	--dvb->streams;

	if(0 == dvb->streams) {
		tm6000_stop_stream(dev);
// 		mutex_destroy(&tm6000_dev->streaming_mutex);
	}
	mutex_unlock(&dvb->mutex);
// 	mutex_destroy(&tm6000_dev->streaming_mutex);

	return 0;
}

int tm6000_dvb_attach_frontend(struct tm6000_core *dev)
{
	struct tm6000_dvb *dvb = dev->dvb;

	if(dev->caps.has_zl10353) {
		struct zl10353_config config =
				    {.demod_address = dev->demod_addr >> 1,
				     .no_tuner = 1,
// 				     .input_frequency = 0x19e9,
// 				     .r56_agc_targets =  0x1c,
				    };

		dvb->frontend = pseudo_zl10353_attach(dev, &config,
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
		struct xc2028_config cfg = {
			.i2c_adap = &dev->i2c_adap,
			.i2c_addr = dev->tuner_addr,
		};

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
	} else {
		printk(KERN_ERR "tm6000: no frontend found\n");
	}

	dvb->demux.dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING
							    | DMX_MEMORY_BASED_FILTERING;
	dvb->demux.priv = dev;
	dvb->demux.filternum = 256;
	dvb->demux.feednum = 256;
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

// 	mutex_lock(&tm6000_driver.open_close_mutex);
	if(dvb->frontend) {
		dvb_frontend_detach(dvb->frontend);
		dvb_unregister_frontend(dvb->frontend);
	}

	dvb_dmxdev_release(&dvb->dmxdev);
	dvb_dmx_release(&dvb->demux);
	dvb_unregister_adapter(&dvb->adapter);
	mutex_destroy(&dvb->mutex);
// 	mutex_unlock(&tm6000_driver.open_close_mutex);

}
