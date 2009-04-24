/* Maestro PCI sound card radio driver for Linux support
 * (c) 2000 A. Tlalka, atlka@pg.gda.pl
 * Notes on the hardware
 *
 *  + Frequency control is done digitally
 *  + No volume control - only mute/unmute - you have to use Aux line volume
 *  control on Maestro card to set the volume
 *  + Radio status (tuned/not_tuned and stereo/mono) is valid some time after
 *  frequency setting (>100ms) and only when the radio is unmuted.
 *  version 0.02
 *  + io port is automatically detected - only the first radio is used
 *  version 0.03
 *  + thread access locking additions
 *  version 0.04
 * + code improvements
 * + VIDEO_TUNER_LOW is permanent
 *
 * Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@infradead.org>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/version.h>      /* for KERNEL_VERSION MACRO     */
#include <linux/pci.h>
#include <linux/videodev2.h>
#include <linux/io.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

MODULE_AUTHOR("Adam Tlalka, atlka@pg.gda.pl");
MODULE_DESCRIPTION("Radio driver for the Maestro PCI sound card radio.");
MODULE_LICENSE("GPL");

static int radio_nr = -1;
module_param(radio_nr, int, 0);

#define RADIO_VERSION KERNEL_VERSION(0, 0, 6)
#define DRIVER_VERSION	"0.06"

#define GPIO_DATA	0x60   /* port offset from ESS_IO_BASE */

#define IO_MASK		4      /* mask      register offset from GPIO_DATA
				bits 1=unmask write to given bit */
#define IO_DIR		8      /* direction register offset from GPIO_DATA
				bits 0/1=read/write direction */

#define GPIO6		0x0040 /* mask bits for GPIO lines */
#define GPIO7		0x0080
#define GPIO8		0x0100
#define GPIO9		0x0200

#define STR_DATA	GPIO6  /* radio TEA5757 pins and GPIO bits */
#define STR_CLK		GPIO7
#define STR_WREN	GPIO8
#define STR_MOST	GPIO9

#define FREQ_LO		 50*16000
#define FREQ_HI		150*16000

#define FREQ_IF		171200 /* 10.7*16000   */
#define FREQ_STEP	200    /* 12.5*16      */

#define FREQ2BITS(x)	((((unsigned int)(x)+FREQ_IF+(FREQ_STEP<<1))\
			/(FREQ_STEP<<2))<<2) /* (x==fmhz*16*1000) -> bits */

#define BITS2FREQ(x)	((x) * FREQ_STEP - FREQ_IF)

struct maestro {
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct pci_dev *pdev;
	struct mutex lock;

	u16	io;	/* base of Maestro card radio io (GPIO_DATA)*/
	u16	muted;	/* VIDEO_AUDIO_MUTE */
	u16	stereo;	/* VIDEO_TUNER_STEREO_ON */
	u16	tuned;	/* signal strength (0 or 0xffff) */
};

static inline struct maestro *to_maestro(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct maestro, v4l2_dev);
}

static u32 radio_bits_get(struct maestro *dev)
{
	u16 io = dev->io, l, rdata;
	u32 data = 0;
	u16 omask;

	omask = inw(io + IO_MASK);
	outw(~(STR_CLK | STR_WREN), io + IO_MASK);
	outw(0, io);
	udelay(16);

	for (l = 24; l--;) {
		outw(STR_CLK, io);		/* HI state */
		udelay(2);
		if (!l)
			dev->tuned = inw(io) & STR_MOST ? 0 : 0xffff;
		outw(0, io);			/* LO state */
		udelay(2);
		data <<= 1;			/* shift data */
		rdata = inw(io);
		if (!l)
			dev->stereo = (rdata & STR_MOST) ?  0 : 1;
		else if (rdata & STR_DATA)
			data++;
		udelay(2);
	}

	if (dev->muted)
		outw(STR_WREN, io);

	udelay(4);
	outw(omask, io + IO_MASK);

	return data & 0x3ffe;
}

static void radio_bits_set(struct maestro *dev, u32 data)
{
	u16 io = dev->io, l, bits;
	u16 omask, odir;

	omask = inw(io + IO_MASK);
	odir = (inw(io + IO_DIR) & ~STR_DATA) | (STR_CLK | STR_WREN);
	outw(odir | STR_DATA, io + IO_DIR);
	outw(~(STR_DATA | STR_CLK | STR_WREN), io + IO_MASK);
	udelay(16);
	for (l = 25; l; l--) {
		bits = ((data >> 18) & STR_DATA) | STR_WREN;
		data <<= 1;			/* shift data */
		outw(bits, io);			/* start strobe */
		udelay(2);
		outw(bits | STR_CLK, io);	/* HI level */
		udelay(2);
		outw(bits, io);			/* LO level */
		udelay(4);
	}

	if (!dev->muted)
		outw(0, io);

	udelay(4);
	outw(omask, io + IO_MASK);
	outw(odir, io + IO_DIR);
	msleep(125);
}

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *v)
{
	struct maestro *dev = video_drvdata(file);

	strlcpy(v->driver, "radio-maestro", sizeof(v->driver));
	strlcpy(v->card, "Maestro Radio", sizeof(v->card));
	snprintf(v->bus_info, sizeof(v->bus_info), "PCI:%s", pci_name(dev->pdev));
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER | V4L2_CAP_RADIO;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	struct maestro *dev = video_drvdata(file);

	if (v->index > 0)
		return -EINVAL;

	mutex_lock(&dev->lock);
	radio_bits_get(dev);

	strlcpy(v->name, "FM", sizeof(v->name));
	v->type = V4L2_TUNER_RADIO;
	v->rangelow = FREQ_LO;
	v->rangehigh = FREQ_HI;
	v->rxsubchans = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
	v->capability = V4L2_TUNER_CAP_LOW;
	if (dev->stereo)
		v->audmode = V4L2_TUNER_MODE_STEREO;
	else
		v->audmode = V4L2_TUNER_MODE_MONO;
	v->signal = dev->tuned;
	mutex_unlock(&dev->lock);
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	return v->index ? -EINVAL : 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct maestro *dev = video_drvdata(file);

	if (f->frequency < FREQ_LO || f->frequency > FREQ_HI)
		return -EINVAL;
	mutex_lock(&dev->lock);
	radio_bits_set(dev, FREQ2BITS(f->frequency));
	mutex_unlock(&dev->lock);
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct maestro *dev = video_drvdata(file);

	f->type = V4L2_TUNER_RADIO;
	mutex_lock(&dev->lock);
	f->frequency = BITS2FREQ(radio_bits_get(dev));
	mutex_unlock(&dev->lock);
	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
					struct v4l2_queryctrl *qc)
{
	switch (qc->id) {
	case V4L2_CID_AUDIO_MUTE:
		return v4l2_ctrl_query_fill(qc, 0, 1, 1, 1);
	}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct maestro *dev = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = dev->muted;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct maestro *dev = video_drvdata(file);
	u16 io = dev->io;
	u16 omask;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		mutex_lock(&dev->lock);
		omask = inw(io + IO_MASK);
		outw(~STR_WREN, io + IO_MASK);
		dev->muted = ctrl->value;
		outw(dev->muted ? STR_WREN : 0, io);
		udelay(4);
		outw(omask, io + IO_MASK);
		msleep(125);
		mutex_unlock(&dev->lock);
		return 0;
	}
	return -EINVAL;
}

static int vidioc_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int vidioc_s_input(struct file *filp, void *priv, unsigned int i)
{
	return i ? -EINVAL : 0;
}

static int vidioc_g_audio(struct file *file, void *priv,
					struct v4l2_audio *a)
{
	a->index = 0;
	strlcpy(a->name, "Radio", sizeof(a->name));
	a->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int vidioc_s_audio(struct file *file, void *priv,
					struct v4l2_audio *a)
{
	return a->index ? -EINVAL : 0;
}

static const struct v4l2_file_operations maestro_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= video_ioctl2,
};

static const struct v4l2_ioctl_ops maestro_ioctl_ops = {
	.vidioc_querycap    = vidioc_querycap,
	.vidioc_g_tuner     = vidioc_g_tuner,
	.vidioc_s_tuner     = vidioc_s_tuner,
	.vidioc_g_audio     = vidioc_g_audio,
	.vidioc_s_audio     = vidioc_s_audio,
	.vidioc_g_input     = vidioc_g_input,
	.vidioc_s_input     = vidioc_s_input,
	.vidioc_g_frequency = vidioc_g_frequency,
	.vidioc_s_frequency = vidioc_s_frequency,
	.vidioc_queryctrl   = vidioc_queryctrl,
	.vidioc_g_ctrl      = vidioc_g_ctrl,
	.vidioc_s_ctrl      = vidioc_s_ctrl,
};

static u16 __devinit radio_power_on(struct maestro *dev)
{
	register u16 io = dev->io;
	register u32 ofreq;
	u16 omask, odir;

	omask = inw(io + IO_MASK);
	odir = (inw(io + IO_DIR) & ~STR_DATA) | (STR_CLK | STR_WREN);
	outw(odir & ~STR_WREN, io + IO_DIR);
	dev->muted = inw(io) & STR_WREN ? 0 : 1;
	outw(odir, io + IO_DIR);
	outw(~(STR_WREN | STR_CLK), io + IO_MASK);
	outw(dev->muted ? 0 : STR_WREN, io);
	udelay(16);
	outw(omask, io + IO_MASK);
	ofreq = radio_bits_get(dev);

	if ((ofreq < FREQ2BITS(FREQ_LO)) || (ofreq > FREQ2BITS(FREQ_HI)))
		ofreq = FREQ2BITS(FREQ_LO);
	radio_bits_set(dev, ofreq);

	return (ofreq == radio_bits_get(dev));
}

static int __devinit maestro_probe(struct pci_dev *pdev,
	const struct pci_device_id *ent)
{
	struct maestro *dev;
	struct v4l2_device *v4l2_dev;
	int retval;

	retval = pci_enable_device(pdev);
	if (retval) {
		dev_err(&pdev->dev, "enabling pci device failed!\n");
		goto err;
	}

	retval = -ENOMEM;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		dev_err(&pdev->dev, "not enough memory\n");
		goto err;
	}

	v4l2_dev = &dev->v4l2_dev;
	mutex_init(&dev->lock);
	dev->pdev = pdev;

	strlcpy(v4l2_dev->name, "maestro", sizeof(v4l2_dev->name));

	retval = v4l2_device_register(&pdev->dev, v4l2_dev);
	if (retval < 0) {
		v4l2_err(v4l2_dev, "Could not register v4l2_device\n");
		goto errfr;
	}

	dev->io = pci_resource_start(pdev, 0) + GPIO_DATA;

	strlcpy(dev->vdev.name, v4l2_dev->name, sizeof(dev->vdev.name));
	dev->vdev.v4l2_dev = v4l2_dev;
	dev->vdev.fops = &maestro_fops;
	dev->vdev.ioctl_ops = &maestro_ioctl_ops;
	dev->vdev.release = video_device_release_empty;
	video_set_drvdata(&dev->vdev, dev);

	retval = video_register_device(&dev->vdev, VFL_TYPE_RADIO, radio_nr);
	if (retval) {
		v4l2_err(v4l2_dev, "can't register video device!\n");
		goto errfr1;
	}

	if (!radio_power_on(dev)) {
		retval = -EIO;
		goto errunr;
	}

	v4l2_info(v4l2_dev, "version " DRIVER_VERSION "\n");

	return 0;
errunr:
	video_unregister_device(&dev->vdev);
errfr1:
	v4l2_device_unregister(v4l2_dev);
errfr:
	kfree(dev);
err:
	return retval;

}

static void __devexit maestro_remove(struct pci_dev *pdev)
{
	struct v4l2_device *v4l2_dev = dev_get_drvdata(&pdev->dev);
	struct maestro *dev = to_maestro(v4l2_dev);

	video_unregister_device(&dev->vdev);
	v4l2_device_unregister(&dev->v4l2_dev);
}

static struct pci_device_id maestro_r_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ESS, PCI_DEVICE_ID_ESS_ESS1968),
		.class = PCI_CLASS_MULTIMEDIA_AUDIO << 8,
		.class_mask = 0xffff00 },
	{ PCI_DEVICE(PCI_VENDOR_ID_ESS, PCI_DEVICE_ID_ESS_ESS1978),
		.class = PCI_CLASS_MULTIMEDIA_AUDIO << 8,
		.class_mask = 0xffff00 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, maestro_r_pci_tbl);

static struct pci_driver maestro_r_driver = {
	.name		= "maestro_radio",
	.id_table	= maestro_r_pci_tbl,
	.probe		= maestro_probe,
	.remove		= __devexit_p(maestro_remove),
};

static int __init maestro_radio_init(void)
{
	int retval = pci_register_driver(&maestro_r_driver);

	if (retval)
		printk(KERN_ERR "error during registration pci driver\n");

	return retval;
}

static void __exit maestro_radio_exit(void)
{
	pci_unregister_driver(&maestro_r_driver);
}

module_init(maestro_radio_init);
module_exit(maestro_radio_exit);
