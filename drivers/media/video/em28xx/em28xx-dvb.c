/*
 DVB device driver for em28xx

 (c) 2008 Mauro Carvalho Chehab <mchehab@infradead.org>

 (c) 2008 Devin Heitmueller <devin.heitmueller@gmail.com>
	- Fixes for the driver to properly work with HVR-950

 Based on cx88-dvb and saa7134-dvb originally written by:
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

#include "lgdt330x.h"

MODULE_DESCRIPTION("driver for em28xx based DVB cards");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@infradead.org>");
MODULE_LICENSE("GPL");

static unsigned int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable debug messages [dvb]");

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

#define dprintk(level, fmt, arg...) do {			\
if (debug >= level) 						\
	printk(KERN_DEBUG "%s/2-dvb: " fmt, dev->name, ## arg)	\
} while (0)

static int
buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
	struct em28xx_fh *fh = vq->priv_data;
	struct em28xx        *dev = fh->dev;

	/* FIXME: The better would be to allocate a smaller buffer */
	*size = 16 * fh->dev->width * fh->dev->height >> 3;
	if (0 == *count)
		*count = EM28XX_DEF_BUF;

	if (*count < EM28XX_MIN_BUF)
		*count = EM28XX_MIN_BUF;

	dev->mode = EM28XX_DIGITAL_MODE;

	return 0;
}

/* ------------------------------------------------------------------ */

static struct lgdt330x_config em2880_lgdt3303_dev = {
	.demod_address = 0x0e,
	.demod_chip = LGDT3303,
};

/* ------------------------------------------------------------------ */

static int attach_xc3028(u8 addr, struct em28xx *dev)
{
	struct dvb_frontend *fe;
	struct xc2028_ctrl ctl;
	struct xc2028_config cfg;

	memset (&cfg, 0, sizeof(cfg));
	cfg.i2c_adap  = &dev->i2c_adap;
	cfg.i2c_addr  = addr;
	cfg.ctrl      = &ctl;
	cfg.callback  = em28xx_tuner_callback;

	em28xx_setup_xc3028(dev, &ctl);

	if (!dev->dvb.frontend) {
		printk(KERN_ERR "%s/2: dvb frontend not attached. "
				"Can't attach xc3028\n",
		       dev->name);
		return -EINVAL;
	}

	fe = dvb_attach(xc2028_attach, dev->dvb.frontend, &cfg);
	if (!fe) {
		printk(KERN_ERR "%s/2: xc3028 attach failed\n",
		       dev->name);
		dvb_frontend_detach(dev->dvb.frontend);
		dvb_unregister_frontend(dev->dvb.frontend);
		dev->dvb.frontend = NULL;
		return -EINVAL;
	}

	printk(KERN_INFO "%s/2: xc3028 attached\n", dev->name);

	return 0;
}

static int dvb_init(struct em28xx *dev)
{
	/* init struct videobuf_dvb */
	dev->dvb.name = dev->name;

	dev->qops->buf_setup = buffer_setup;

	/* FIXME: Do we need more initialization here? */
	memset(&dev->dvb_fh, 0, sizeof (dev->dvb_fh));
	dev->dvb_fh.dev = dev;
	dev->dvb_fh.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

	videobuf_queue_vmalloc_init(&dev->dvb.dvbq, dev->qops,
			&dev->udev->dev, &dev->slock,
			V4L2_BUF_TYPE_VIDEO_CAPTURE,
			V4L2_FIELD_ALTERNATE,
			sizeof(struct em28xx_buffer), &dev->dvb_fh);

	/* init frontend */
	switch (dev->model) {
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_950:
		/* Enable lgdt330x */
		dev->mode = EM28XX_DIGITAL_MODE;
		em28xx_tuner_callback(dev, XC2028_TUNER_RESET, 0);

		dev->dvb.frontend = dvb_attach(lgdt330x_attach,
					       &em2880_lgdt3303_dev,
					       &dev->i2c_adap);
		if (attach_xc3028(0x61, dev) < 0)
			return -EINVAL;
		break;
	default:
		printk(KERN_ERR "%s/2: The frontend of your DVB/ATSC card"
				" isn't supported yet\n",
		       dev->name);
		break;
	}
	if (NULL == dev->dvb.frontend) {
		printk(KERN_ERR
		       "%s/2: frontend initialization failed\n",
		       dev->name);
		return -EINVAL;
	}

	/* register everything */
	return videobuf_dvb_register(&dev->dvb, THIS_MODULE, dev,
				     &dev->udev->dev,
				     adapter_nr);
}

static int dvb_fini(struct em28xx *dev)
{
	if (dev->dvb.frontend)
		videobuf_dvb_unregister(&dev->dvb);

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
