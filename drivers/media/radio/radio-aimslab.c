/* radiotrack (radioreveal) driver for Linux radio support
 * (c) 1997 M. Kirkwood
 * Converted to V4L2 API by Mauro Carvalho Chehab <mchehab@infradead.org>
 * Converted to new API by Alan Cox <alan@lxorguk.ukuu.org.uk>
 * Various bugfixes and enhancements by Russell Kroll <rkroll@exploits.org>
 *
 * History:
 * 1999-02-24	Russell Kroll <rkroll@exploits.org>
 * 		Fine tuning/VIDEO_TUNER_LOW
 *		Frequency range expanded to start at 87 MHz
 *
 * TODO: Allow for more than one of these foolish entities :-)
 *
 * Notes on the hardware (reverse engineered from other peoples'
 * reverse engineering of AIMS' code :-)
 *
 *  Frequency control is done digitally -- ie out(port,encodefreq(95.8));
 *
 *  The signal strength query is unsurprisingly inaccurate.  And it seems
 *  to indicate that (on my card, at least) the frequency setting isn't
 *  too great.  (I have to tune up .025MHz from what the freq should be
 *  to get a report that the thing is tuned.)
 *
 *  Volume control is (ugh) analogue:
 *   out(port, start_increasing_volume);
 *   wait(a_wee_while);
 *   out(port, stop_changing_the_volume);
 *
 */

#include <linux/module.h>	/* Modules 			*/
#include <linux/init.h>		/* Initdata			*/
#include <linux/ioport.h>	/* request_region		*/
#include <linux/delay.h>	/* msleep			*/
#include <linux/videodev2.h>	/* kernel radio structs		*/
#include <linux/version.h>	/* for KERNEL_VERSION MACRO	*/
#include <linux/io.h>		/* outb, outb_p			*/
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>

MODULE_AUTHOR("M.Kirkwood");
MODULE_DESCRIPTION("A driver for the RadioTrack/RadioReveal radio card.");
MODULE_LICENSE("GPL");

#ifndef CONFIG_RADIO_RTRACK_PORT
#define CONFIG_RADIO_RTRACK_PORT -1
#endif

static int io = CONFIG_RADIO_RTRACK_PORT;
static int radio_nr = -1;

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O address of the RadioTrack card (0x20f or 0x30f)");
module_param(radio_nr, int, 0);

#define RADIO_VERSION KERNEL_VERSION(0, 0, 2)

struct rtrack
{
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	int port;
	int curvol;
	unsigned long curfreq;
	int muted;
	int io;
	struct mutex lock;
};

static struct rtrack rtrack_card;

/* local things */

static void rt_decvol(struct rtrack *rt)
{
	outb(0x58, rt->io);		/* volume down + sigstr + on	*/
	msleep(100);
	outb(0xd8, rt->io);		/* volume steady + sigstr + on	*/
}

static void rt_incvol(struct rtrack *rt)
{
	outb(0x98, rt->io);		/* volume up + sigstr + on	*/
	msleep(100);
	outb(0xd8, rt->io);		/* volume steady + sigstr + on	*/
}

static void rt_mute(struct rtrack *rt)
{
	rt->muted = 1;
	mutex_lock(&rt->lock);
	outb(0xd0, rt->io);		/* volume steady, off		*/
	mutex_unlock(&rt->lock);
}

static int rt_setvol(struct rtrack *rt, int vol)
{
	int i;

	mutex_lock(&rt->lock);

	if (vol == rt->curvol) {	/* requested volume = current */
		if (rt->muted) {	/* user is unmuting the card  */
			rt->muted = 0;
			outb(0xd8, rt->io);	/* enable card */
		}
		mutex_unlock(&rt->lock);
		return 0;
	}

	if (vol == 0) {			/* volume = 0 means mute the card */
		outb(0x48, rt->io);	/* volume down but still "on"	*/
		msleep(2000);	/* make sure it's totally down	*/
		outb(0xd0, rt->io);	/* volume steady, off		*/
		rt->curvol = 0;		/* track the volume state!	*/
		mutex_unlock(&rt->lock);
		return 0;
	}

	rt->muted = 0;
	if (vol > rt->curvol)
		for (i = rt->curvol; i < vol; i++)
			rt_incvol(rt);
	else
		for (i = rt->curvol; i > vol; i--)
			rt_decvol(rt);

	rt->curvol = vol;
	mutex_unlock(&rt->lock);
	return 0;
}

/* the 128+64 on these outb's is to keep the volume stable while tuning
 * without them, the volume _will_ creep up with each frequency change
 * and bit 4 (+16) is to keep the signal strength meter enabled
 */

static void send_0_byte(struct rtrack *rt)
{
	if (rt->curvol == 0 || rt->muted) {
		outb_p(128+64+16+  1, rt->io);   /* wr-enable + data low */
		outb_p(128+64+16+2+1, rt->io);   /* clock */
	}
	else {
		outb_p(128+64+16+8+  1, rt->io);  /* on + wr-enable + data low */
		outb_p(128+64+16+8+2+1, rt->io);  /* clock */
	}
	msleep(1);
}

static void send_1_byte(struct rtrack *rt)
{
	if (rt->curvol == 0 || rt->muted) {
		outb_p(128+64+16+4  +1, rt->io);   /* wr-enable+data high */
		outb_p(128+64+16+4+2+1, rt->io);   /* clock */
	}
	else {
		outb_p(128+64+16+8+4  +1, rt->io); /* on+wr-enable+data high */
		outb_p(128+64+16+8+4+2+1, rt->io); /* clock */
	}

	msleep(1);
}

static int rt_setfreq(struct rtrack *rt, unsigned long freq)
{
	int i;

	mutex_lock(&rt->lock);			/* Stop other ops interfering */

	rt->curfreq = freq;

	/* now uses VIDEO_TUNER_LOW for fine tuning */

	freq += 171200;			/* Add 10.7 MHz IF 		*/
	freq /= 800;			/* Convert to 50 kHz units	*/

	send_0_byte(rt);		/*  0: LSB of frequency		*/

	for (i = 0; i < 13; i++)	/*   : frequency bits (1-13)	*/
		if (freq & (1 << i))
			send_1_byte(rt);
		else
			send_0_byte(rt);

	send_0_byte(rt);		/* 14: test bit - always 0    */
	send_0_byte(rt);		/* 15: test bit - always 0    */

	send_0_byte(rt);		/* 16: band data 0 - always 0 */
	send_0_byte(rt);		/* 17: band data 1 - always 0 */
	send_0_byte(rt);		/* 18: band data 2 - always 0 */
	send_0_byte(rt);		/* 19: time base - always 0   */

	send_0_byte(rt);		/* 20: spacing (0 = 25 kHz)   */
	send_1_byte(rt);		/* 21: spacing (1 = 25 kHz)   */
	send_0_byte(rt);		/* 22: spacing (0 = 25 kHz)   */
	send_1_byte(rt);		/* 23: AM/FM (FM = 1, always) */

	if (rt->curvol == 0 || rt->muted)
		outb(0xd0, rt->io);	/* volume steady + sigstr */
	else
		outb(0xd8, rt->io);	/* volume steady + sigstr + on */

	mutex_unlock(&rt->lock);

	return 0;
}

static int rt_getsigstr(struct rtrack *rt)
{
	int sig = 1;

	mutex_lock(&rt->lock);
	if (inb(rt->io) & 2)	/* bit set = no signal present	*/
		sig = 0;
	mutex_unlock(&rt->lock);
	return sig;
}

static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *v)
{
	strlcpy(v->driver, "radio-aimslab", sizeof(v->driver));
	strlcpy(v->card, "RadioTrack", sizeof(v->card));
	strlcpy(v->bus_info, "ISA", sizeof(v->bus_info));
	v->version = RADIO_VERSION;
	v->capabilities = V4L2_CAP_TUNER | V4L2_CAP_RADIO;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
					struct v4l2_tuner *v)
{
	struct rtrack *rt = video_drvdata(file);

	if (v->index > 0)
		return -EINVAL;

	strlcpy(v->name, "FM", sizeof(v->name));
	v->type = V4L2_TUNER_RADIO;
	v->rangelow = 87 * 16000;
	v->rangehigh = 108 * 16000;
	v->rxsubchans = V4L2_TUNER_SUB_MONO;
	v->capability = V4L2_TUNER_CAP_LOW;
	v->audmode = V4L2_TUNER_MODE_MONO;
	v->signal = 0xffff * rt_getsigstr(rt);
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
	struct rtrack *rt = video_drvdata(file);

	rt_setfreq(rt, f->frequency);
	return 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
					struct v4l2_frequency *f)
{
	struct rtrack *rt = video_drvdata(file);

	f->type = V4L2_TUNER_RADIO;
	f->frequency = rt->curfreq;
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
	struct rtrack *rt = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = rt->muted;
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = rt->curvol;
		return 0;
	}
	return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
					struct v4l2_control *ctrl)
{
	struct rtrack *rt = video_drvdata(file);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value)
			rt_mute(rt);
		else
			rt_setvol(rt, rt->curvol);
		return 0;
	case V4L2_CID_AUDIO_VOLUME:
		rt_setvol(rt, ctrl->value);
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

static const struct v4l2_file_operations rtrack_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= video_ioctl2,
};

static const struct v4l2_ioctl_ops rtrack_ioctl_ops = {
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

static int __init rtrack_init(void)
{
	struct rtrack *rt = &rtrack_card;
	struct v4l2_device *v4l2_dev = &rt->v4l2_dev;
	int res;

	strlcpy(v4l2_dev->name, "rtrack", sizeof(v4l2_dev->name));
	rt->io = io;

	if (rt->io == -1) {
		v4l2_err(v4l2_dev, "you must set an I/O address with io=0x20f or 0x30f\n");
		return -EINVAL;
	}

	if (!request_region(rt->io, 2, "rtrack")) {
		v4l2_err(v4l2_dev, "port 0x%x already in use\n", rt->io);
		return -EBUSY;
	}

	res = v4l2_device_register(NULL, v4l2_dev);
	if (res < 0) {
		release_region(rt->io, 2);
		v4l2_err(v4l2_dev, "could not register v4l2_device\n");
		return res;
	}

	strlcpy(rt->vdev.name, v4l2_dev->name, sizeof(rt->vdev.name));
	rt->vdev.v4l2_dev = v4l2_dev;
	rt->vdev.fops = &rtrack_fops;
	rt->vdev.ioctl_ops = &rtrack_ioctl_ops;
	rt->vdev.release = video_device_release_empty;
	video_set_drvdata(&rt->vdev, rt);

	if (video_register_device(&rt->vdev, VFL_TYPE_RADIO, radio_nr) < 0) {
		v4l2_device_unregister(&rt->v4l2_dev);
		release_region(rt->io, 2);
		return -EINVAL;
	}
	v4l2_info(v4l2_dev, "AIMSlab RadioTrack/RadioReveal card driver.\n");

	/* Set up the I/O locking */

	mutex_init(&rt->lock);

	/* mute card - prevents noisy bootups */

	/* this ensures that the volume is all the way down  */
	outb(0x48, rt->io);		/* volume down but still "on"	*/
	msleep(2000);	/* make sure it's totally down	*/
	outb(0xc0, rt->io);		/* steady volume, mute card	*/

	return 0;
}

static void __exit rtrack_exit(void)
{
	struct rtrack *rt = &rtrack_card;

	video_unregister_device(&rt->vdev);
	v4l2_device_unregister(&rt->v4l2_dev);
	release_region(rt->io, 2);
}

module_init(rtrack_init);
module_exit(rtrack_exit);

