// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Driver for the Auvitek USB bridge
 *
 *  Copyright (c) 2008 Steven Toth <stoth@linuxtv.org>
 */

#include "au0828.h"

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/device.h>
#include <media/v4l2-common.h>
#include <media/tuner.h>

#include "au8522.h"
#include "xc5000.h"
#include "mxl5007t.h"
#include "tda18271.h"

static int preallocate_big_buffers;
module_param_named(preallocate_big_buffers, preallocate_big_buffers, int, 0644);
MODULE_PARM_DESC(preallocate_big_buffers, "Preallocate the larger transfer buffers at module load time");

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

static struct xc5000_config hauppauge_xc5000a_config = {
	.i2c_address      = 0x61,
	.if_khz           = 6000,
	.chip_id          = XC5000A,
	.output_amp       = 0x8f,
};

static struct xc5000_config hauppauge_xc5000c_config = {
	.i2c_address      = 0x61,
	.if_khz           = 6000,
	.chip_id          = XC5000C,
	.output_amp       = 0x8f,
};

static struct mxl5007t_config mxl5007t_hvr950q_config = {
	.xtal_freq_hz = MxL_XTAL_24_MHZ,
	.if_freq_hz = MxL_IF_6_MHZ,
};

static struct tda18271_config hauppauge_woodbury_tunerconfig = {
	.gate    = TDA18271_GATE_DIGITAL,
};

static void au0828_restart_dvb_streaming(struct work_struct *work);

static void au0828_bulk_timeout(struct timer_list *t)
{
	struct au0828_dev *dev = from_timer(dev, t, bulk_timeout);

	dprintk(1, "%s called\n", __func__);
	dev->bulk_timeout_running = 0;
	schedule_work(&dev->restart_streaming);
}

/*-------------------------------------------------------------------*/
static void urb_completion(struct urb *purb)
{
	struct au0828_dev *dev = purb->context;
	int ptype = usb_pipetype(purb->pipe);
	unsigned char *ptr;

	dprintk(2, "%s: %d\n", __func__, purb->actual_length);

	if (!dev) {
		dprintk(2, "%s: no dev!\n", __func__);
		return;
	}

	if (!dev->urb_streaming) {
		dprintk(2, "%s: not streaming!\n", __func__);
		return;
	}

	if (ptype != PIPE_BULK) {
		pr_err("%s: Unsupported URB type %d\n",
		       __func__, ptype);
		return;
	}

	/* See if the stream is corrupted (to work around a hardware
	   bug where the stream gets misaligned */
	ptr = purb->transfer_buffer;
	if (purb->actual_length > 0 && ptr[0] != 0x47) {
		dprintk(1, "Need to restart streaming %02x len=%d!\n",
			ptr[0], purb->actual_length);
		schedule_work(&dev->restart_streaming);
		return;
	} else if (dev->bulk_timeout_running == 1) {
		/* The URB handler has fired, so cancel timer which would
		 * restart endpoint if we hadn't
		 */
		dprintk(1, "%s cancelling bulk timeout\n", __func__);
		dev->bulk_timeout_running = 0;
		del_timer(&dev->bulk_timeout);
	}

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

	if (!dev->urb_streaming)
		return 0;

	if (dev->bulk_timeout_running == 1) {
		dev->bulk_timeout_running = 0;
		del_timer(&dev->bulk_timeout);
	}

	dev->urb_streaming = false;
	for (i = 0; i < URB_COUNT; i++) {
		if (dev->urbs[i]) {
			usb_kill_urb(dev->urbs[i]);
			if (!preallocate_big_buffers)
				kfree(dev->urbs[i]->transfer_buffer);

			usb_free_urb(dev->urbs[i]);
		}
	}

	return 0;
}

static int start_urb_transfer(struct au0828_dev *dev)
{
	struct urb *purb;
	int i, ret;

	dprintk(2, "%s()\n", __func__);

	if (dev->urb_streaming) {
		dprintk(2, "%s: bulk xfer already running!\n", __func__);
		return 0;
	}

	for (i = 0; i < URB_COUNT; i++) {

		dev->urbs[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!dev->urbs[i])
			return -ENOMEM;

		purb = dev->urbs[i];

		if (preallocate_big_buffers)
			purb->transfer_buffer = dev->dig_transfer_buffer[i];
		else
			purb->transfer_buffer = kzalloc(URB_BUFSIZE,
					GFP_KERNEL);

		if (!purb->transfer_buffer) {
			usb_free_urb(purb);
			dev->urbs[i] = NULL;
			ret = -ENOMEM;
			pr_err("%s: failed big buffer allocation, err = %d\n",
			       __func__, ret);
			return ret;
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
			pr_err("%s: failed urb submission, err = %d\n",
			       __func__, ret);
			return ret;
		}
	}

	dev->urb_streaming = true;

	/* If we don't valid data within 1 second, restart stream */
	mod_timer(&dev->bulk_timeout, jiffies + (HZ));
	dev->bulk_timeout_running = 1;

	return 0;
}

static void au0828_start_transport(struct au0828_dev *dev)
{
	au0828_write(dev, 0x608, 0x90);
	au0828_write(dev, 0x609, 0x72);
	au0828_write(dev, 0x60a, 0x71);
	au0828_write(dev, 0x60b, 0x01);

}

static void au0828_stop_transport(struct au0828_dev *dev, int full_stop)
{
	if (full_stop) {
		au0828_write(dev, 0x608, 0x00);
		au0828_write(dev, 0x609, 0x00);
		au0828_write(dev, 0x60a, 0x00);
	}
	au0828_write(dev, 0x60b, 0x00);
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

	if (dvb->frontend) {
		mutex_lock(&dvb->lock);
		dvb->start_count++;
		dprintk(1, "%s(), start_count: %d, stop_count: %d\n", __func__,
			dvb->start_count, dvb->stop_count);
		if (dvb->feeding++ == 0) {
			/* Start transport */
			au0828_start_transport(dev);
			ret = start_urb_transfer(dev);
			if (ret < 0) {
				au0828_stop_transport(dev, 0);
				dvb->feeding--;	/* We ran out of memory... */
			}
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

	if (dvb->frontend) {
		cancel_work_sync(&dev->restart_streaming);

		mutex_lock(&dvb->lock);
		dvb->stop_count++;
		dprintk(1, "%s(), start_count: %d, stop_count: %d\n", __func__,
			dvb->start_count, dvb->stop_count);
		if (dvb->feeding > 0) {
			dvb->feeding--;
			if (dvb->feeding == 0) {
				/* Stop transport */
				ret = stop_urb_transfer(dev);
				au0828_stop_transport(dev, 0);
			}
		}
		mutex_unlock(&dvb->lock);
	}

	return ret;
}

static void au0828_restart_dvb_streaming(struct work_struct *work)
{
	struct au0828_dev *dev = container_of(work, struct au0828_dev,
					      restart_streaming);
	struct au0828_dvb *dvb = &dev->dvb;

	if (!dev->urb_streaming)
		return;

	dprintk(1, "Restarting streaming...!\n");

	mutex_lock(&dvb->lock);

	/* Stop transport */
	stop_urb_transfer(dev);
	au0828_stop_transport(dev, 1);

	/* Start transport */
	au0828_start_transport(dev);
	start_urb_transfer(dev);

	mutex_unlock(&dvb->lock);
}

static int au0828_set_frontend(struct dvb_frontend *fe)
{
	struct au0828_dev *dev = fe->dvb->priv;
	struct au0828_dvb *dvb = &dev->dvb;
	int ret, was_streaming;

	mutex_lock(&dvb->lock);
	was_streaming = dev->urb_streaming;
	if (was_streaming) {
		au0828_stop_transport(dev, 1);

		/*
		 * We can't hold a mutex here, as the restart_streaming
		 * kthread may also hold it.
		 */
		mutex_unlock(&dvb->lock);
		cancel_work_sync(&dev->restart_streaming);
		mutex_lock(&dvb->lock);

		stop_urb_transfer(dev);
	}
	mutex_unlock(&dvb->lock);

	ret = dvb->set_frontend(fe);

	if (was_streaming) {
		mutex_lock(&dvb->lock);
		au0828_start_transport(dev);
		start_urb_transfer(dev);
		mutex_unlock(&dvb->lock);
	}

	return ret;
}

static int dvb_register(struct au0828_dev *dev)
{
	struct au0828_dvb *dvb = &dev->dvb;
	int result;

	dprintk(1, "%s()\n", __func__);

	if (preallocate_big_buffers) {
		int i;
		for (i = 0; i < URB_COUNT; i++) {
			dev->dig_transfer_buffer[i] = kzalloc(URB_BUFSIZE,
					GFP_KERNEL);

			if (!dev->dig_transfer_buffer[i]) {
				result = -ENOMEM;

				pr_err("failed buffer allocation (errno = %d)\n",
				       result);
				goto fail_adapter;
			}
		}
	}

	INIT_WORK(&dev->restart_streaming, au0828_restart_dvb_streaming);

	/* register adapter */
	result = dvb_register_adapter(&dvb->adapter,
				      KBUILD_MODNAME, THIS_MODULE,
				      &dev->usbdev->dev, adapter_nr);
	if (result < 0) {
		pr_err("dvb_register_adapter failed (errno = %d)\n",
		       result);
		goto fail_adapter;
	}

#ifdef CONFIG_MEDIA_CONTROLLER_DVB
	dvb->adapter.mdev = dev->media_dev;
#endif

	dvb->adapter.priv = dev;

	/* register frontend */
	result = dvb_register_frontend(&dvb->adapter, dvb->frontend);
	if (result < 0) {
		pr_err("dvb_register_frontend failed (errno = %d)\n",
		       result);
		goto fail_frontend;
	}

	/* Hook dvb frontend */
	dvb->set_frontend = dvb->frontend->ops.set_frontend;
	dvb->frontend->ops.set_frontend = au0828_set_frontend;

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
		pr_err("dvb_dmx_init failed (errno = %d)\n", result);
		goto fail_dmx;
	}

	dvb->dmxdev.filternum    = 256;
	dvb->dmxdev.demux        = &dvb->demux.dmx;
	dvb->dmxdev.capabilities = 0;
	result = dvb_dmxdev_init(&dvb->dmxdev, &dvb->adapter);
	if (result < 0) {
		pr_err("dvb_dmxdev_init failed (errno = %d)\n", result);
		goto fail_dmxdev;
	}

	dvb->fe_hw.source = DMX_FRONTEND_0;
	result = dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	if (result < 0) {
		pr_err("add_frontend failed (DMX_FRONTEND_0, errno = %d)\n",
		       result);
		goto fail_fe_hw;
	}

	dvb->fe_mem.source = DMX_MEMORY_FE;
	result = dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->fe_mem);
	if (result < 0) {
		pr_err("add_frontend failed (DMX_MEMORY_FE, errno = %d)\n",
		       result);
		goto fail_fe_mem;
	}

	result = dvb->demux.dmx.connect_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	if (result < 0) {
		pr_err("connect_frontend failed (errno = %d)\n", result);
		goto fail_fe_conn;
	}

	/* register network adapter */
	dvb_net_init(&dvb->adapter, &dvb->net, &dvb->demux.dmx);

	dvb->start_count = 0;
	dvb->stop_count = 0;

	result = dvb_create_media_graph(&dvb->adapter, false);
	if (result < 0)
		goto fail_create_graph;

	return 0;

fail_create_graph:
	dvb_net_release(&dvb->net);
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

	if (preallocate_big_buffers) {
		int i;
		for (i = 0; i < URB_COUNT; i++)
			kfree(dev->dig_transfer_buffer[i]);
	}

	return result;
}

void au0828_dvb_unregister(struct au0828_dev *dev)
{
	struct au0828_dvb *dvb = &dev->dvb;

	dprintk(1, "%s()\n", __func__);

	if (dvb->frontend == NULL)
		return;

	cancel_work_sync(&dev->restart_streaming);

	dvb_net_release(&dvb->net);
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_mem);
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	dvb_dmxdev_release(&dvb->dmxdev);
	dvb_dmx_release(&dvb->demux);
	dvb_unregister_frontend(dvb->frontend);
	dvb_frontend_detach(dvb->frontend);
	dvb_unregister_adapter(&dvb->adapter);

	if (preallocate_big_buffers) {
		int i;
		for (i = 0; i < URB_COUNT; i++)
			kfree(dev->dig_transfer_buffer[i]);
	}
	dvb->frontend = NULL;
}

/* All the DVB attach calls go here, this function gets modified
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
			switch (dev->board.tuner_type) {
			default:
			case TUNER_XC5000:
				dvb_attach(xc5000_attach, dvb->frontend,
					   &dev->i2c_adap,
					   &hauppauge_xc5000a_config);
				break;
			case TUNER_XC5000C:
				dvb_attach(xc5000_attach, dvb->frontend,
					   &dev->i2c_adap,
					   &hauppauge_xc5000c_config);
				break;
			}
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
				&hauppauge_xc5000a_config);
		}
		break;
	default:
		pr_warn("The frontend of your DVB/ATSC card isn't supported yet\n");
		break;
	}
	if (NULL == dvb->frontend) {
		pr_err("%s() Frontend initialization failed\n",
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
		dvb->frontend = NULL;
		return ret;
	}

	timer_setup(&dev->bulk_timeout, au0828_bulk_timeout, 0);

	return 0;
}

void au0828_dvb_suspend(struct au0828_dev *dev)
{
	struct au0828_dvb *dvb = &dev->dvb;
	int rc;

	if (dvb->frontend) {
		if (dev->urb_streaming) {
			cancel_work_sync(&dev->restart_streaming);
			/* Stop transport */
			mutex_lock(&dvb->lock);
			stop_urb_transfer(dev);
			au0828_stop_transport(dev, 1);
			mutex_unlock(&dvb->lock);
			dev->need_urb_start = true;
		}
		/* suspend frontend - does tuner and fe to sleep */
		rc = dvb_frontend_suspend(dvb->frontend);
		pr_info("au0828_dvb_suspend(): Suspending DVB fe %d\n", rc);
	}
}

void au0828_dvb_resume(struct au0828_dev *dev)
{
	struct au0828_dvb *dvb = &dev->dvb;
	int rc;

	if (dvb->frontend) {
		/* resume frontend - does fe and tuner init */
		rc = dvb_frontend_resume(dvb->frontend);
		pr_info("au0828_dvb_resume(): Resuming DVB fe %d\n", rc);
		if (dev->need_urb_start) {
			/* Start transport */
			mutex_lock(&dvb->lock);
			au0828_start_transport(dev);
			start_urb_transfer(dev);
			mutex_unlock(&dvb->lock);
		}
	}
}
