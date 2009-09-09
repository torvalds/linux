/*
 DVB device driver for em28xx

 (c) 2008 Mauro Carvalho Chehab <mchehab@infradead.org>

 (c) 2008 Devin Heitmueller <devin.heitmueller@gmail.com>
	- Fixes for the driver to properly work with HVR-950
	- Fixes for the driver to properly work with Pinnacle PCTV HD Pro Stick
	- Fixes for the driver to properly work with AMD ATI TV Wonder HD 600

 (c) 2008 Aidan Thornton <makosoft@googlemail.com>

 Based on cx88-dvb, saa7134-dvb and videobuf-dvb originally written by:
	(c) 2004, 2005 Chris Pascoe <c.pascoe@itee.uq.edu.au>
	(c) 2004 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License.
 */

#include <linux/kernel.h>
#include <linux/usb.h>

#include "em28xx.h"
#include <media/v4l2-common.h>
#include <media/videobuf-vmalloc.h>
#include <media/tuner.h>
#include "tuner-simple.h"

#include "lgdt330x.h"
#include "zl10353.h"
#include "s5h1409.h"

MODULE_DESCRIPTION("driver for em28xx based DVB cards");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@infradead.org>");
MODULE_LICENSE("GPL");

static unsigned int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debug messages [dvb]");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define dprintk(level, fmt, arg...) do {			\
if (debug >= level) 						\
	printk(KERN_DEBUG "%s/2-dvb: " fmt, dev->name, ## arg);	\
} while (0)

#define EM28XX_DVB_NUM_BUFS 5
#define EM28XX_DVB_MAX_PACKETS 64

struct em28xx_dvb {
	struct dvb_frontend        *frontend;

	/* feed count management */
	struct mutex               lock;
	int                        nfeeds;

	/* general boilerplate stuff */
	struct dvb_adapter         adapter;
	struct dvb_demux           demux;
	struct dmxdev              dmxdev;
	struct dmx_frontend        fe_hw;
	struct dmx_frontend        fe_mem;
	struct dvb_net             net;
};


static inline void print_err_status(struct em28xx *dev,
				     int packet, int status)
{
	char *errmsg = "Unknown";

	switch (status) {
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
	if (packet < 0) {
		dprintk(1, "URB status %d [%s].\n", status, errmsg);
	} else {
		dprintk(1, "URB packet %d, status %d [%s].\n",
			packet, status, errmsg);
	}
}

static inline int dvb_isoc_copy(struct em28xx *dev, struct urb *urb)
{
	int i;

	if (!dev)
		return 0;

	if ((dev->state & DEV_DISCONNECTED) || (dev->state & DEV_MISCONFIGURED))
		return 0;

	if (urb->status < 0) {
		print_err_status(dev, -1, urb->status);
		if (urb->status == -ENOENT)
			return 0;
	}

	for (i = 0; i < urb->number_of_packets; i++) {
		int status = urb->iso_frame_desc[i].status;

		if (status < 0) {
			print_err_status(dev, i, status);
			if (urb->iso_frame_desc[i].status != -EPROTO)
				continue;
		}

		dvb_dmx_swfilter(&dev->dvb->demux, urb->transfer_buffer +
				 urb->iso_frame_desc[i].offset,
				 urb->iso_frame_desc[i].actual_length);
	}

	return 0;
}

static int start_streaming(struct em28xx_dvb *dvb)
{
	int rc;
	struct em28xx *dev = dvb->adapter.priv;
	int max_dvb_packet_size;

	usb_set_interface(dev->udev, 0, 1);
	rc = em28xx_set_mode(dev, EM28XX_DIGITAL_MODE);
	if (rc < 0)
		return rc;

	max_dvb_packet_size = em28xx_isoc_dvb_max_packetsize(dev);

	return em28xx_init_isoc(dev, EM28XX_DVB_MAX_PACKETS,
				EM28XX_DVB_NUM_BUFS, max_dvb_packet_size,
				dvb_isoc_copy);
}

static int stop_streaming(struct em28xx_dvb *dvb)
{
	struct em28xx *dev = dvb->adapter.priv;

	em28xx_uninit_isoc(dev);

	em28xx_set_mode(dev, EM28XX_SUSPEND);

	return 0;
}

static int start_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux  = feed->demux;
	struct em28xx_dvb *dvb = demux->priv;
	int rc, ret;

	if (!demux->dmx.frontend)
		return -EINVAL;

	mutex_lock(&dvb->lock);
	dvb->nfeeds++;
	rc = dvb->nfeeds;

	if (dvb->nfeeds == 1) {
		ret = start_streaming(dvb);
		if (ret < 0)
			rc = ret;
	}

	mutex_unlock(&dvb->lock);
	return rc;
}

static int stop_feed(struct dvb_demux_feed *feed)
{
	struct dvb_demux *demux  = feed->demux;
	struct em28xx_dvb *dvb = demux->priv;
	int err = 0;

	mutex_lock(&dvb->lock);
	dvb->nfeeds--;

	if (0 == dvb->nfeeds)
		err = stop_streaming(dvb);

	mutex_unlock(&dvb->lock);
	return err;
}



/* ------------------------------------------------------------------ */
static int em28xx_dvb_bus_ctrl(struct dvb_frontend *fe, int acquire)
{
	struct em28xx *dev = fe->dvb->priv;

	if (acquire)
		return em28xx_set_mode(dev, EM28XX_DIGITAL_MODE);
	else
		return em28xx_set_mode(dev, EM28XX_SUSPEND);
}

/* ------------------------------------------------------------------ */

static struct lgdt330x_config em2880_lgdt3303_dev = {
	.demod_address = 0x0e,
	.demod_chip = LGDT3303,
};

static struct zl10353_config em28xx_zl10353_with_xc3028 = {
	.demod_address = (0x1e >> 1),
	.no_tuner = 1,
	.parallel_ts = 1,
	.if2 = 45600,
};

static struct s5h1409_config em28xx_s5h1409_with_xc3028 = {
	.demod_address = 0x32 >> 1,
	.output_mode   = S5H1409_PARALLEL_OUTPUT,
	.gpio          = S5H1409_GPIO_OFF,
	.inversion     = S5H1409_INVERSION_OFF,
	.status_mode   = S5H1409_DEMODLOCKING,
	.mpeg_timing   = S5H1409_MPEGTIMING_CONTINOUS_NONINVERTING_CLOCK
};

#ifdef EM28XX_DRX397XD_SUPPORT
/* [TODO] djh - not sure yet what the device config needs to contain */
static struct drx397xD_config em28xx_drx397xD_with_xc3028 = {
	.demod_address = (0xe0 >> 1),
};
#endif

/* ------------------------------------------------------------------ */

static int attach_xc3028(u8 addr, struct em28xx *dev)
{
	struct dvb_frontend *fe;
	struct xc2028_config cfg;

	memset(&cfg, 0, sizeof(cfg));
	cfg.i2c_adap  = &dev->i2c_adap;
	cfg.i2c_addr  = addr;

	if (!dev->dvb->frontend) {
		printk(KERN_ERR "%s/2: dvb frontend not attached. "
				"Can't attach xc3028\n",
		       dev->name);
		return -EINVAL;
	}

	fe = dvb_attach(xc2028_attach, dev->dvb->frontend, &cfg);
	if (!fe) {
		printk(KERN_ERR "%s/2: xc3028 attach failed\n",
		       dev->name);
		dvb_frontend_detach(dev->dvb->frontend);
		dev->dvb->frontend = NULL;
		return -EINVAL;
	}

	printk(KERN_INFO "%s/2: xc3028 attached\n", dev->name);

	return 0;
}

/* ------------------------------------------------------------------ */

static int register_dvb(struct em28xx_dvb *dvb,
		 struct module *module,
		 struct em28xx *dev,
		 struct device *device)
{
	int result;

	mutex_init(&dvb->lock);

	/* register adapter */
	result = dvb_register_adapter(&dvb->adapter, dev->name, module, device,
				      adapter_nr);
	if (result < 0) {
		printk(KERN_WARNING "%s: dvb_register_adapter failed (errno = %d)\n",
		       dev->name, result);
		goto fail_adapter;
	}

	/* Ensure all frontends negotiate bus access */
	dvb->frontend->ops.ts_bus_ctrl = em28xx_dvb_bus_ctrl;

	dvb->adapter.priv = dev;

	/* register frontend */
	result = dvb_register_frontend(&dvb->adapter, dvb->frontend);
	if (result < 0) {
		printk(KERN_WARNING "%s: dvb_register_frontend failed (errno = %d)\n",
		       dev->name, result);
		goto fail_frontend;
	}

	/* register demux stuff */
	dvb->demux.dmx.capabilities =
		DMX_TS_FILTERING | DMX_SECTION_FILTERING |
		DMX_MEMORY_BASED_FILTERING;
	dvb->demux.priv       = dvb;
	dvb->demux.filternum  = 256;
	dvb->demux.feednum    = 256;
	dvb->demux.start_feed = start_feed;
	dvb->demux.stop_feed  = stop_feed;

	result = dvb_dmx_init(&dvb->demux);
	if (result < 0) {
		printk(KERN_WARNING "%s: dvb_dmx_init failed (errno = %d)\n",
		       dev->name, result);
		goto fail_dmx;
	}

	dvb->dmxdev.filternum    = 256;
	dvb->dmxdev.demux        = &dvb->demux.dmx;
	dvb->dmxdev.capabilities = 0;
	result = dvb_dmxdev_init(&dvb->dmxdev, &dvb->adapter);
	if (result < 0) {
		printk(KERN_WARNING "%s: dvb_dmxdev_init failed (errno = %d)\n",
		       dev->name, result);
		goto fail_dmxdev;
	}

	dvb->fe_hw.source = DMX_FRONTEND_0;
	result = dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	if (result < 0) {
		printk(KERN_WARNING "%s: add_frontend failed (DMX_FRONTEND_0, errno = %d)\n",
		       dev->name, result);
		goto fail_fe_hw;
	}

	dvb->fe_mem.source = DMX_MEMORY_FE;
	result = dvb->demux.dmx.add_frontend(&dvb->demux.dmx, &dvb->fe_mem);
	if (result < 0) {
		printk(KERN_WARNING "%s: add_frontend failed (DMX_MEMORY_FE, errno = %d)\n",
		       dev->name, result);
		goto fail_fe_mem;
	}

	result = dvb->demux.dmx.connect_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	if (result < 0) {
		printk(KERN_WARNING "%s: connect_frontend failed (errno = %d)\n",
		       dev->name, result);
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

static void unregister_dvb(struct em28xx_dvb *dvb)
{
	dvb_net_release(&dvb->net);
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_mem);
	dvb->demux.dmx.remove_frontend(&dvb->demux.dmx, &dvb->fe_hw);
	dvb_dmxdev_release(&dvb->dmxdev);
	dvb_dmx_release(&dvb->demux);
	dvb_unregister_frontend(dvb->frontend);
	dvb_frontend_detach(dvb->frontend);
	dvb_unregister_adapter(&dvb->adapter);
}


static int dvb_init(struct em28xx *dev)
{
	int result = 0;
	struct em28xx_dvb *dvb;

	if (!dev->board.has_dvb) {
		/* This device does not support the extension */
		return 0;
	}

	dvb = kzalloc(sizeof(struct em28xx_dvb), GFP_KERNEL);

	if (dvb == NULL) {
		printk(KERN_INFO "em28xx_dvb: memory allocation failed\n");
		return -ENOMEM;
	}
	dev->dvb = dvb;

	em28xx_set_mode(dev, EM28XX_DIGITAL_MODE);
	/* init frontend */
	switch (dev->model) {
	case EM2883_BOARD_HAUPPAUGE_WINTV_HVR_850:
	case EM2883_BOARD_HAUPPAUGE_WINTV_HVR_950:
	case EM2880_BOARD_PINNACLE_PCTV_HD_PRO:
	case EM2880_BOARD_AMD_ATI_TV_WONDER_HD_600:
		dvb->frontend = dvb_attach(lgdt330x_attach,
					   &em2880_lgdt3303_dev,
					   &dev->i2c_adap);
		if (attach_xc3028(0x61, dev) < 0) {
			result = -EINVAL;
			goto out_free;
		}
		break;
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900:
	case EM2880_BOARD_TERRATEC_HYBRID_XS:
	case EM2880_BOARD_KWORLD_DVB_310U:
	case EM2880_BOARD_EMPIRE_DUAL_TV:
		dvb->frontend = dvb_attach(zl10353_attach,
					   &em28xx_zl10353_with_xc3028,
					   &dev->i2c_adap);
		if (attach_xc3028(0x61, dev) < 0) {
			result = -EINVAL;
			goto out_free;
		}
		break;
	case EM2883_BOARD_KWORLD_HYBRID_330U:
	case EM2882_BOARD_EVGA_INDTUBE:
		dvb->frontend = dvb_attach(s5h1409_attach,
					   &em28xx_s5h1409_with_xc3028,
					   &dev->i2c_adap);
		if (attach_xc3028(0x61, dev) < 0) {
			result = -EINVAL;
			goto out_free;
		}
		break;
	case EM2882_BOARD_KWORLD_ATSC_315U:
		dvb->frontend = dvb_attach(lgdt330x_attach,
					   &em2880_lgdt3303_dev,
					   &dev->i2c_adap);
		if (dvb->frontend != NULL) {
			if (!dvb_attach(simple_tuner_attach, dvb->frontend,
				&dev->i2c_adap, 0x61, TUNER_THOMSON_DTT761X)) {
				result = -EINVAL;
				goto out_free;
			}
		}
		break;
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900_R2:
#ifdef EM28XX_DRX397XD_SUPPORT
		/* We don't have the config structure properly populated, so
		   this is commented out for now */
		dvb->frontend = dvb_attach(drx397xD_attach,
					   &em28xx_drx397xD_with_xc3028,
					   &dev->i2c_adap);
		if (attach_xc3028(0x61, dev) < 0) {
			result = -EINVAL;
			goto out_free;
		}
		break;
#endif
	default:
		printk(KERN_ERR "%s/2: The frontend of your DVB/ATSC card"
				" isn't supported yet\n",
		       dev->name);
		break;
	}
	if (NULL == dvb->frontend) {
		printk(KERN_ERR
		       "%s/2: frontend initialization failed\n",
		       dev->name);
		result = -EINVAL;
		goto out_free;
	}
	/* define general-purpose callback pointer */
	dvb->frontend->callback = em28xx_tuner_callback;

	/* register everything */
	result = register_dvb(dvb, THIS_MODULE, dev, &dev->udev->dev);

	if (result < 0)
		goto out_free;

	em28xx_set_mode(dev, EM28XX_SUSPEND);
	printk(KERN_INFO "Successfully loaded em28xx-dvb\n");
	return 0;

out_free:
	em28xx_set_mode(dev, EM28XX_SUSPEND);
	kfree(dvb);
	dev->dvb = NULL;
	return result;
}

static int dvb_fini(struct em28xx *dev)
{
	if (!dev->board.has_dvb) {
		/* This device does not support the extension */
		return 0;
	}

	if (dev->dvb) {
		unregister_dvb(dev->dvb);
		dev->dvb = NULL;
	}

	return 0;
}

static struct em28xx_ops dvb_ops = {
	.id   = EM28XX_DVB,
	.name = "Em28xx dvb Extension",
	.init = dvb_init,
	.fini = dvb_fini,
};

static int __init em28xx_dvb_register(void)
{
	return em28xx_register_extension(&dvb_ops);
}

static void __exit em28xx_dvb_unregister(void)
{
	em28xx_unregister_extension(&dvb_ops);
}

module_init(em28xx_dvb_register);
module_exit(em28xx_dvb_unregister);
