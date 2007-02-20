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
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>

#include <linux/version.h>      /* for KERNEL_VERSION MACRO     */
#define RADIO_VERSION KERNEL_VERSION(0,0,6)
#define DRIVER_VERSION	"0.06"

static struct v4l2_queryctrl radio_qctrl[] = {
	{
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.default_value = 1,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	}
};

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

static int radio_nr = -1;
module_param(radio_nr, int, 0);

static int radio_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg);
static int maestro_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
static void maestro_remove(struct pci_dev *pdev);

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

static const struct file_operations maestro_fops = {
	.owner		= THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl		= radio_ioctl,
	.compat_ioctl	= v4l_compat_ioctl32,
	.llseek         = no_llseek,
};

static struct video_device maestro_radio = {
	.name		= "Maestro radio",
	.type		= VID_TYPE_TUNER,
	.hardware	= 0,
	.fops		= &maestro_fops,
};

struct radio_device {
	u16	io,	/* base of Maestro card radio io (GPIO_DATA)*/
		muted,	/* VIDEO_AUDIO_MUTE */
		stereo,	/* VIDEO_TUNER_STEREO_ON */
		tuned;	/* signal strength (0 or 0xffff) */
	struct mutex lock;
};

static u32 radio_bits_get(struct radio_device *dev)
{
	register u16 io=dev->io, l, rdata;
	register u32 data=0;
	u16 omask;

	omask = inw(io + IO_MASK);
	outw(~(STR_CLK | STR_WREN), io + IO_MASK);
	outw(0, io);
	udelay(16);

	for (l=24;l--;) {
		outw(STR_CLK, io);		/* HI state */
		udelay(2);
		if(!l)
			dev->tuned = inw(io) & STR_MOST ? 0 : 0xffff;
		outw(0, io);			/* LO state */
		udelay(2);
		data <<= 1;			/* shift data */
		rdata = inw(io);
		if(!l)
			dev->stereo =  rdata & STR_MOST ?
			0 : 1;
		else
			if(rdata & STR_DATA)
				data++;
		udelay(2);
	}

	if(dev->muted)
		outw(STR_WREN, io);

	udelay(4);
	outw(omask, io + IO_MASK);

	return data & 0x3ffe;
}

static void radio_bits_set(struct radio_device *dev, u32 data)
{
	register u16 io=dev->io, l, bits;
	u16 omask, odir;

	omask = inw(io + IO_MASK);
	odir  = (inw(io + IO_DIR) & ~STR_DATA) | (STR_CLK | STR_WREN);
	outw(odir | STR_DATA, io + IO_DIR);
	outw(~(STR_DATA | STR_CLK | STR_WREN), io + IO_MASK);
	udelay(16);
	for (l=25;l;l--) {
		bits = ((data >> 18) & STR_DATA) | STR_WREN ;
		data <<= 1;			/* shift data */
		outw(bits, io);			/* start strobe */
		udelay(2);
		outw(bits | STR_CLK, io);	/* HI level */
		udelay(2);
		outw(bits, io);			/* LO level */
		udelay(4);
	}

	if(!dev->muted)
		outw(0, io);

	udelay(4);
	outw(omask, io + IO_MASK);
	outw(odir, io + IO_DIR);
	msleep(125);
}

static inline int radio_function(struct inode *inode, struct file *file,
	unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct radio_device *card = video_get_drvdata(dev);

	switch (cmd) {
		case VIDIOC_QUERYCAP:
		{
			struct v4l2_capability *v = arg;
			memset(v,0,sizeof(*v));
			strlcpy(v->driver, "radio-maestro", sizeof (v->driver));
			strlcpy(v->card, "Maestro Radio", sizeof (v->card));
			sprintf(v->bus_info,"PCI");
			v->version = RADIO_VERSION;
			v->capabilities = V4L2_CAP_TUNER;

			return 0;
		}
		case VIDIOC_G_TUNER:
		{
			struct v4l2_tuner *v = arg;

			if (v->index > 0)
				return -EINVAL;

			(void)radio_bits_get(card);

			memset(v,0,sizeof(*v));
			strcpy(v->name, "FM");
			v->type = V4L2_TUNER_RADIO;

			v->rangelow = FREQ_LO;
			v->rangehigh = FREQ_HI;
			v->rxsubchans =V4L2_TUNER_SUB_MONO|V4L2_TUNER_SUB_STEREO;
			v->capability=V4L2_TUNER_CAP_LOW;
			if(card->stereo)
				v->audmode = V4L2_TUNER_MODE_STEREO;
			else
				v->audmode = V4L2_TUNER_MODE_MONO;
			v->signal=card->tuned;

			return 0;
		}
		case VIDIOC_S_TUNER:
		{
			struct v4l2_tuner *v = arg;

			if (v->index > 0)
				return -EINVAL;

			return 0;
		}
		case VIDIOC_S_FREQUENCY:
		{
			struct v4l2_frequency *f = arg;

			if (f->frequency < FREQ_LO || f->frequency > FREQ_HI)
				return -EINVAL;
			radio_bits_set(card, FREQ2BITS(f->frequency));

			return 0;
		}
		case VIDIOC_G_FREQUENCY:
		{
			struct v4l2_frequency *f = arg;

			f->type = V4L2_TUNER_RADIO;
			f->frequency = BITS2FREQ(radio_bits_get(card));

			return 0;
		}
		case VIDIOC_QUERYCTRL:
		{
			struct v4l2_queryctrl *qc = arg;
			int i;

			for (i = 0; i < ARRAY_SIZE(radio_qctrl); i++) {
				if (qc->id && qc->id == radio_qctrl[i].id) {
					memcpy(qc, &(radio_qctrl[i]),
								sizeof(*qc));
					return (0);
				}
			}
			return -EINVAL;
		}
		case VIDIOC_G_CTRL:
		{
			struct v4l2_control *ctrl= arg;

			switch (ctrl->id) {
				case V4L2_CID_AUDIO_MUTE:
					ctrl->value=card->muted;
					return (0);
			}
			return -EINVAL;
		}
		case VIDIOC_S_CTRL:
		{
			struct v4l2_control *ctrl= arg;

			switch (ctrl->id) {
				case V4L2_CID_AUDIO_MUTE:
				{
					register u16 io = card->io;
					register u16 omask = inw(io + IO_MASK);
					outw(~STR_WREN, io + IO_MASK);
					outw((card->muted = ctrl->value ) ?
						STR_WREN : 0, io);
					udelay(4);
					outw(omask, io + IO_MASK);
					msleep(125);

					return (0);
				}
			}
			return -EINVAL;
		}
		default:
			return v4l_compat_translate_ioctl(inode,file,cmd,arg,
							  radio_function);
	}
}

static int radio_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	struct video_device *dev = video_devdata(file);
	struct radio_device *card = video_get_drvdata(dev);
	int ret;

	mutex_lock(&card->lock);
	ret = video_usercopy(inode, file, cmd, arg, radio_function);
	mutex_unlock(&card->lock);

	return ret;
}

static u16 __devinit radio_power_on(struct radio_device *dev)
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
	struct radio_device *radio_unit;
	struct video_device *maestro_radio_inst;
	int retval;

	retval = pci_enable_device(pdev);
	if (retval) {
		dev_err(&pdev->dev, "enabling pci device failed!\n");
		goto err;
	}

	retval = -ENOMEM;

	radio_unit = kzalloc(sizeof(*radio_unit), GFP_KERNEL);
	if (radio_unit == NULL) {
		dev_err(&pdev->dev, "not enough memory\n");
		goto err;
	}

	radio_unit->io = pci_resource_start(pdev, 0) + GPIO_DATA;
	mutex_init(&radio_unit->lock);

	maestro_radio_inst = video_device_alloc();
	if (maestro_radio_inst == NULL) {
		dev_err(&pdev->dev, "not enough memory\n");
		goto errfr;
	}

	memcpy(maestro_radio_inst, &maestro_radio, sizeof(maestro_radio));
	video_set_drvdata(maestro_radio_inst, radio_unit);
	pci_set_drvdata(pdev, maestro_radio_inst);

	retval = video_register_device(maestro_radio_inst, VFL_TYPE_RADIO,
		radio_nr);
	if (retval) {
		printk(KERN_ERR "can't register video device!\n");
		goto errfr1;
	}

	if (!radio_power_on(radio_unit)) {
		retval = -EIO;
		goto errunr;
	}

	dev_info(&pdev->dev, "version " DRIVER_VERSION " time " __TIME__ "  "
		 __DATE__ "\n");
	dev_info(&pdev->dev, "radio chip initialized\n");

	return 0;
errunr:
	video_unregister_device(maestro_radio_inst);
errfr1:
	kfree(maestro_radio_inst);
errfr:
	kfree(radio_unit);
err:
	return retval;

}

static void __devexit maestro_remove(struct pci_dev *pdev)
{
	struct video_device *vdev = pci_get_drvdata(pdev);

	video_unregister_device(vdev);
}

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

MODULE_AUTHOR("Adam Tlalka, atlka@pg.gda.pl");
MODULE_DESCRIPTION("Radio driver for the Maestro PCI sound card radio.");
MODULE_LICENSE("GPL");
