/* radio-aztech.c - Aztech radio card driver for Linux 2.2
 *
 * Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@infradead.org>
 * Adapted to support the Video for Linux API by
 * Russell Kroll <rkroll@exploits.org>.  Based on original tuner code by:
 *
 * Quay Ly
 * Donald Song
 * Jason Lewis      (jlewis@twilight.vtc.vsc.edu)
 * Scott McGrath    (smcgrath@twilight.vtc.vsc.edu)
 * William McGrath  (wmcgrath@twilight.vtc.vsc.edu)
 *
 * The basis for this code may be found at http://bigbang.vtc.vsc.edu/fmradio/
 * along with more information on the card itself.
 *
 * History:
 * 1999-02-24	Russell Kroll <rkroll@exploits.org>
 *		Fine tuning/VIDEO_TUNER_LOW
 * 		Range expanded to 87-108 MHz (from 87.9-107.8)
 *
 * Notable changes from the original source:
 * - includes stripped down to the essentials
 * - for loops used as delays replaced with udelay()
 * - #defines removed, changed to static values
 * - tuning structure changed - no more character arrays, other changes
*/

#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/delay.h>	/* udelay			*/
#include <linux/videodev2.h>	/* kernel radio structs		*/
#include <linux/version.h>      /* for KERNEL_VERSION MACRO     */
#include <linux/io.h>		/* outb, outb_p			*/
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

MODULE_AUTHOR("Russell Kroll, Quay Lu, Donald Song, Jason Lewis, Scott McGrath, William McGrath");
MODULE_DESCRIPTION("A driver for the Aztech radio card.");
MODULE_LICENSE("GPL");

/* acceptable ports: 0x350 (JP3 shorted), 0x358 (JP3 open) */

#ifndef CONFIG_RADIO_AZTECH_PORT
#define CONFIG_RADIO_AZTECH_PORT -1
#endif

static int io = CONFIG_RADIO_AZTECH_PORT;
static int radio_nr = -1;
static int radio_wait_time = 1000;

module_param(io, int, 0);
module_param(radio_nr, int, 0);
MODULE_PARM_DESC(io, "I/O address of the Aztech card (0x350 or 0x358)");

#define RADIO_VERSION KERNEL_VERSION(0, 0, 2)

struct aztech
{
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	int io;
	int curvol;
	unsigned long curfreq;
	int stereo;
	struct mutex lock;
};

static struct aztech aztech_card;

static int volconvert(int level)
{
	level >>= 14;		/* Map 16bits down to 2 bit */
	level &= 3;

	/* convert to card-friendly values */
	switch (level) {
	case 0:
		return 0;
	case 1:
		return 1;
	case 2:
		return 4;
	case 3:
		return 5;
	}
	return 0;	/* Quieten gcc */
}

static void send_0_byte(struct aztech *az)
{
	udelay(radio_wait_time);
	outb_p(2 + volconvert(az->curvol), az->io);
	outb_p(64 + 2 + volconvert(az->curvol), az->io);
}

static void send_1_byte(struct aztech *az)
{
	udelay (radio_wait_time);
	outb_p(128 + 2 + volconvert(az->curvol), az->io);
	outb_p(128 + 64 + 2 + volconvert(az->curvol), az->io);
}

static int az_setvol(struct aztech *az, int vol)
{
	mutex_lock(&az->lock);
	outb(volconvert(vol), az->io);
	mutex_unlock(&az->lock);
	return 0;
}

/* thanks to Michael Dwyer for giving me a dose of clues in
 * the signal strength department..
 *
 * This card has a stereo bit - bit 0 set = mono, not set = stereo
 * It also has a "signal" bit - bit 1 set = bad signal, not set = good
 *
 */

static int az_getsigstr(struct aztech *az)
{
	int sig = 1;

	mutex_lock(&az->lock);
	if (inb(az->io) & 2)	/* bit set = no signal present */
		sig = 0;
	mutex_unlock(&az->lock);
	return sig;
}

static int az_getstereo(struct aztech *az)
{
	int stereo = 1;

	mutex_lock(&az->lock);
	if (inb(az->io) & 1) 	/* bit set = mono */
		stereo = 0;
	mutex_unlock(&az->lock);
	return stereo;
}

static int az_setfreq(struct aztech *az, unsigned long frequency)
{
	int  i;

	mutex_lock(&az->lock);

	az->curfreq = frequency;
	frequency += 171200;		/* Add 10.7 MHz IF		*/
	frequency /= 800;		/* Convert to 50 kHz units	*/

	send_0_byte(az);		/*  0: LSB of frequency       */

	for (i = 0; i < 13; i++)	/*   : frequency bits (1-13)  */
		if (frequency & (1 << i))
			send_1_byte(az);
		else
			send_0_byte(az);

	send_0_byte(az);		/* 14: test bit - always 0    */
	send_0_byte(az);		/* 15: test bit - always 0    */
	send_0_byte(az);		/* 16: band data 0 - always 0 */
	if (az->stereo)		/* 17: stereo (1 to enable)   */
		send_1_byte(az);
	else
		send_0_byte(az);

	send_1_byte(az);		/* 18: band data 1 - unknown  */
	send_0_byte(az);		/* 19: time base - always 0   */
	send_0_byte(az);		/* 20: spacing (0 = 25 kHz)   */
	send_1_byte(az);		/* 21: spacing (1 = 25 kHz)   */
	send_0_byte(az);		/* 22: spacing (0 = 25 kHz)   */
	send_1_byte(az);		/* 23: AM/FM (FM = 1, always) */

	/* latch frequency */

	udelay(radio_wait_time);
	outb_p(128 + 64 + volconvert(az->curvol), az->io);

	mutex_unlock(&az->lock);

	return 0;
}

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *v)
{
	strlcpy(v->driver, "radio-aztech", sizeof(v->driver));
	strlcpy(v->card, "Aztech Radio", sizeof(v->card));
	strlcpy(v->bus_info, "ISA", sizeof(v->bus_info));
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER | V4L2_CAP_RADIO;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	struct aztech *az = video_drvdata(file);

	if (v->index > 0)
		return -EINVAL;

	strlcpy(v->name, "FM", sizeof(v->name));
	v->type = V4L2_TUNER_RADIO;

	v->rangelow = 87 * 16000;
	v->rangehigh = 108 * 16000;
	v->rxsubchans = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
	v->capability = V4L2_TUNER_CAP_LOW;
	if (az_getstereo(az))
		v->audmode = V4L2_TUNER_MODE_STEREO;
	else
		v->audmode = V4L2_TUNER_MODE_MONO;
	v->signal = 0xFFFF * az_getsigstr(az);

	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	return v->index ? -EINVAL : 0;
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

static int vidioc_s_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct aztech *az = video_drvdata(file);

	if (f->tuner != 0 || f->type != V4L2_TUNER_RADIO)
		return -EINVAL;
	az_setfreq(az, f->frequency);
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct aztech *az = video_drvdata(file);

	if (f->tuner != 0)
		return -EINVAL;
	f->type = V4L2_TUNER_RADIO;
	f->frequency = az->curfreq;
	return 0;
}

static int vidioc_queryctrl(struct file *file, void *priv,
			    struct v4l2_queryctrl *qc)
{
	switch (qc->id) {
	case V4L2_CID_AUDIO_MUTE:
		return v4l2_ctrl_query_fill(qc, 0, 1, 1, 1);
	case V4L2_CID_AUDIO_VOLUME:
		return v4l2_ctrl_query_fill(qc, 0, 0xff, 1, 0xff);
	}
	return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			    struct v4l2_control *ctrl)
{
	struct aztech *az = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (az->curvol == 0)
			ctrl->value = 1;
		else
			ctrl->value = 0;
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = az->curvol * 6554;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
			    struct v4l2_control *ctrl)
{
	struct aztech *az = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value)
			az_setvol(az, 0);
		else
			az_setvol(az, az->curvol);
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		az_setvol(az, ctrl->value);
		return 0;
	}
	return -EINVAL;
}

static const struct v4l2_file_operations aztech_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
};

static const struct v4l2_ioctl_ops aztech_ioctl_ops = {
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

static int __init aztech_init(void)
{
	struct aztech *az = &aztech_card;
	struct v4l2_device *v4l2_dev = &az->v4l2_dev;
	int res;

	strlcpy(v4l2_dev->name, "aztech", sizeof(v4l2_dev->name));
	az->io = io;

	if (az->io == -1) {
		v4l2_err(v4l2_dev, "you must set an I/O address with io=0x350 or 0x358\n");
		return -EINVAL;
	}

	if (!request_region(az->io, 2, "aztech")) {
		v4l2_err(v4l2_dev, "port 0x%x already in use\n", az->io);
		return -EBUSY;
	}

	res = v4l2_device_register(NULL, v4l2_dev);
	if (res < 0) {
		release_region(az->io, 2);
		v4l2_err(v4l2_dev, "Could not register v4l2_device\n");
		return res;
	}

	mutex_init(&az->lock);
	strlcpy(az->vdev.name, v4l2_dev->name, sizeof(az->vdev.name));
	az->vdev.v4l2_dev = v4l2_dev;
	az->vdev.fops = &aztech_fops;
	az->vdev.ioctl_ops = &aztech_ioctl_ops;
	az->vdev.release = video_device_release_empty;
	video_set_drvdata(&az->vdev, az);
	/* mute card - prevents noisy bootups */
	outb(0, az->io);

	if (video_register_device(&az->vdev, VFL_TYPE_RADIO, radio_nr) < 0) {
		v4l2_device_unregister(v4l2_dev);
		release_region(az->io, 2);
		return -EINVAL;
	}

	v4l2_info(v4l2_dev, "Aztech radio card driver v1.00/19990224 rkroll@exploits.org\n");
	return 0;
}

static void __exit aztech_exit(void)
{
	struct aztech *az = &aztech_card;

	video_unregister_device(&az->vdev);
	v4l2_device_unregister(&az->v4l2_dev);
	release_region(az->io, 2);
}

module_init(aztech_init);
module_exit(aztech_exit);
