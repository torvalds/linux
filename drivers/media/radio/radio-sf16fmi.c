/* SF16FMI radio driver for Linux radio support
 * heavily based on rtrack driver...
 * (c) 1997 M. Kirkwood
 * (c) 1998 Petr Vandrovec, vandrove@vc.cvut.cz
 *
 * Fitted to new interface by Alan Cox <alan.cox@linux.org>
 * Made working and cleaned up functions <mikael.hedin@irf.se>
 * Support for ISAPnP by Ladislav Michl <ladis@psi.cz>
 *
 * Notes on the hardware
 *
 *  Frequency control is done digitally -- ie out(port,encodefreq(95.8));
 *  No volume control - only mute/unmute - you have to use line volume
 *  control on SB-part of SF16FMI
 *
 * Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@infradead.org>
 */

#include <linux/version.h>
#include <linux/kernel.h>	/* __setup			*/
#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/delay.h>	/* udelay			*/
#include <linux/videodev2.h>	/* kernel radio structs		*/
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <linux/isapnp.h>
#include <asm/io.h>		/* outb, outb_p			*/
#include <asm/uaccess.h>	/* copy to/from user		*/
#include <linux/mutex.h>

#define RADIO_VERSION KERNEL_VERSION(0,0,2)

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

struct fmi_device
{
	int port;
	int curvol; /* 1 or 0 */
	unsigned long curfreq; /* freq in kHz */
	__u32 flags;
};

static int io = -1;
static int radio_nr = -1;
static struct pnp_dev *dev = NULL;
static struct mutex lock;

/* freq is in 1/16 kHz to internal number, hw precision is 50 kHz */
/* It is only useful to give freq in intervall of 800 (=0.05Mhz),
 * other bits will be truncated, e.g 92.7400016 -> 92.7, but
 * 92.7400017 -> 92.75
 */
#define RSF16_ENCODE(x)	((x)/800+214)
#define RSF16_MINFREQ 87*16000
#define RSF16_MAXFREQ 108*16000

static void outbits(int bits, unsigned int data, int port)
{
	while(bits--) {
		if(data & 1) {
			outb(5, port);
			udelay(6);
			outb(7, port);
			udelay(6);
		} else {
			outb(1, port);
			udelay(6);
			outb(3, port);
			udelay(6);
		}
		data>>=1;
	}
}

static inline void fmi_mute(int port)
{
	mutex_lock(&lock);
	outb(0x00, port);
	mutex_unlock(&lock);
}

static inline void fmi_unmute(int port)
{
	mutex_lock(&lock);
	outb(0x08, port);
	mutex_unlock(&lock);
}

static inline int fmi_setfreq(struct fmi_device *dev)
{
	int myport = dev->port;
	unsigned long freq = dev->curfreq;

	mutex_lock(&lock);

	outbits(16, RSF16_ENCODE(freq), myport);
	outbits(8, 0xC0, myport);
	msleep(143);		/* was schedule_timeout(HZ/7) */
	mutex_unlock(&lock);
	if (dev->curvol) fmi_unmute(myport);
	return 0;
}

static inline int fmi_getsigstr(struct fmi_device *dev)
{
	int val;
	int res;
	int myport = dev->port;


	mutex_lock(&lock);
	val = dev->curvol ? 0x08 : 0x00;	/* unmute/mute */
	outb(val, myport);
	outb(val | 0x10, myport);
	msleep(143); 		/* was schedule_timeout(HZ/7) */
	res = (int)inb(myport+1);
	outb(val, myport);

	mutex_unlock(&lock);
	return (res & 2) ? 0 : 0xFFFF;
}

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *v)
{
	strlcpy(v->driver, "radio-sf16fmi", sizeof(v->driver));
	strlcpy(v->card, "SF16-FMx radio", sizeof(v->card));
	sprintf(v->bus_info, "ISA");
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	int mult;
	struct video_device *dev = video_devdata(file);
	struct fmi_device *fmi = dev->priv;

	if (v->index > 0)
		return -EINVAL;

	strcpy(v->name, "FM");
	v->type = V4L2_TUNER_RADIO;
	mult = (fmi->flags & V4L2_TUNER_CAP_LOW) ? 1 : 1000;
	v->rangelow = RSF16_MINFREQ/mult;
	v->rangehigh = RSF16_MAXFREQ/mult;
	v->rxsubchans = V4L2_TUNER_SUB_MONO | V4L2_TUNER_MODE_STEREO;
	v->capability = fmi->flags&V4L2_TUNER_CAP_LOW;
	v->audmode = V4L2_TUNER_MODE_STEREO;
	v->signal = fmi_getsigstr(fmi);
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	if (v->index > 0)
		return -EINVAL;
	return 0;
}

static int vidioc_s_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct video_device *dev = video_devdata(file);
	struct fmi_device *fmi = dev->priv;

	if (!(fmi->flags & V4L2_TUNER_CAP_LOW))
		f->frequency *= 1000;
	if (f->frequency < RSF16_MINFREQ ||
			f->frequency > RSF16_MAXFREQ )
		return -EINVAL;
	/*rounding in steps of 800 to match th freq
	that will be used */
	fmi->curfreq = (f->frequency/800)*800;
	fmi_setfreq(fmi);
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct video_device *dev = video_devdata(file);
	struct fmi_device *fmi = dev->priv;

	f->type = V4L2_TUNER_RADIO;
	f->frequency = fmi->curfreq;
	if (!(fmi->flags & V4L2_TUNER_CAP_LOW))
		f->frequency /= 1000;
	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
					struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(radio_qctrl); i++) {
		if (qc->id && qc->id == radio_qctrl[i].id) {
			memcpy(qc, &(radio_qctrl[i]),
						sizeof(*qc));
			return 0;
		}
	}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct video_device *dev = video_devdata(file);
	struct fmi_device *fmi = dev->priv;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = fmi->curvol;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct video_device *dev = video_devdata(file);
	struct fmi_device *fmi = dev->priv;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value)
			fmi_mute(fmi->port);
		else
			fmi_unmute(fmi->port);
		fmi->curvol = ctrl->value;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_g_audio(struct file *file, void *priv,
					struct v4l2_audio *a)
{
	if (a->index > 1)
		return -EINVAL;

	strcpy(a->name, "Radio");
	a->capability = V4L2_AUDCAP_STEREO;
	return 0;
}

static int vidioc_g_input(struct file *filp, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int vidioc_s_input(struct file *filp, void *priv, unsigned int i)
{
	if (i != 0)
		return -EINVAL;
	return 0;
}

static int vidioc_s_audio(struct file *file, void *priv,
					struct v4l2_audio *a)
{
	if (a->index != 0)
		return -EINVAL;
	return 0;
}

static struct fmi_device fmi_unit;

static const struct file_operations fmi_fops = {
	.owner		= THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl		= video_ioctl2,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= v4l_compat_ioctl32,
#endif
	.llseek         = no_llseek,
};

static const struct v4l2_ioctl_ops fmi_ioctl_ops = {
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

static struct video_device fmi_radio = {
	.name		= "SF16FMx radio",
	.fops           = &fmi_fops,
	.ioctl_ops 	= &fmi_ioctl_ops,
};

/* ladis: this is my card. does any other types exist? */
static struct isapnp_device_id id_table[] __devinitdata = {
	{	ISAPNP_ANY_ID, ISAPNP_ANY_ID,
		ISAPNP_VENDOR('M','F','R'), ISAPNP_FUNCTION(0xad10), 0},
	{	ISAPNP_CARD_END, },
};

MODULE_DEVICE_TABLE(isapnp, id_table);

static int __init isapnp_fmi_probe(void)
{
	int i = 0;

	while (id_table[i].card_vendor != 0 && dev == NULL) {
		dev = pnp_find_dev(NULL, id_table[i].vendor,
				   id_table[i].function, NULL);
		i++;
	}

	if (!dev)
		return -ENODEV;
	if (pnp_device_attach(dev) < 0)
		return -EAGAIN;
	if (pnp_activate_dev(dev) < 0) {
		printk ("radio-sf16fmi: PnP configure failed (out of resources?)\n");
		pnp_device_detach(dev);
		return -ENOMEM;
	}
	if (!pnp_port_valid(dev, 0)) {
		pnp_device_detach(dev);
		return -ENODEV;
	}

	i = pnp_port_start(dev, 0);
	printk ("radio-sf16fmi: PnP reports card at %#x\n", i);

	return i;
}

static int __init fmi_init(void)
{
	if (io < 0)
		io = isapnp_fmi_probe();
	if (io < 0) {
		printk(KERN_ERR "radio-sf16fmi: No PnP card found.\n");
		return io;
	}
	if (!request_region(io, 2, "radio-sf16fmi")) {
		printk(KERN_ERR "radio-sf16fmi: port 0x%x already in use\n", io);
		pnp_device_detach(dev);
		return -EBUSY;
	}

	fmi_unit.port = io;
	fmi_unit.curvol = 0;
	fmi_unit.curfreq = 0;
	fmi_unit.flags = V4L2_TUNER_CAP_LOW;
	fmi_radio.priv = &fmi_unit;

	mutex_init(&lock);

	if (video_register_device(&fmi_radio, VFL_TYPE_RADIO, radio_nr) < 0) {
		release_region(io, 2);
		return -EINVAL;
	}

	printk(KERN_INFO "SF16FMx radio card driver at 0x%x\n", io);
	/* mute card - prevents noisy bootups */
	fmi_mute(io);
	return 0;
}

MODULE_AUTHOR("Petr Vandrovec, vandrove@vc.cvut.cz and M. Kirkwood");
MODULE_DESCRIPTION("A driver for the SF16MI radio.");
MODULE_LICENSE("GPL");

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of the SF16MI card (0x284 or 0x384)");
module_param(radio_nr, int, 0);

static void __exit fmi_cleanup_module(void)
{
	video_unregister_device(&fmi_radio);
	release_region(io, 2);
	if (dev)
		pnp_device_detach(dev);
}

module_init(fmi_init);
module_exit(fmi_cleanup_module);
