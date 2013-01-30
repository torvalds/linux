/* Miro PCM20 radio driver for Linux radio support
 * (c) 1998 Ruurd Reitsma <R.A.Reitsma@wbmt.tudelft.nl>
 * Thanks to Norberto Pellici for the ACI device interface specification
 * The API part is based on the radiotrack driver by M. Kirkwood
 * This driver relies on the aci mixer provided by the snd-miro
 * ALSA driver.
 * Look there for further info...
 */

/* What ever you think about the ACI, version 0x07 is not very well!
 * I can't get frequency, 'tuner status', 'tuner flags' or mute/mono
 * conditions...                Robert
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <sound/aci.h>

static int radio_nr = -1;
module_param(radio_nr, int, 0);
MODULE_PARM_DESC(radio_nr, "Set radio device number (/dev/radioX).  Default: -1 (autodetect)");

static bool mono;
module_param(mono, bool, 0);
MODULE_PARM_DESC(mono, "Force tuner into mono mode.");

struct pcm20 {
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct v4l2_ctrl_handler ctrl_handler;
	unsigned long freq;
	int muted;
	struct snd_miro_aci *aci;
	struct mutex lock;
};

static struct pcm20 pcm20_card = {
	.freq   = 87*16000,
	.muted  = 1,
};

static int pcm20_mute(struct pcm20 *dev, unsigned char mute)
{
	dev->muted = mute;
	return snd_aci_cmd(dev->aci, ACI_SET_TUNERMUTE, mute, -1);
}

static int pcm20_stereo(struct pcm20 *dev, unsigned char stereo)
{
	return snd_aci_cmd(dev->aci, ACI_SET_TUNERMONO, !stereo, -1);
}

static int pcm20_setfreq(struct pcm20 *dev, unsigned long freq)
{
	unsigned char freql;
	unsigned char freqh;
	struct snd_miro_aci *aci = dev->aci;

	dev->freq = freq;

	freq /= 160;
	if (!(aci->aci_version == 0x07 || aci->aci_version >= 0xb0))
		freq /= 10;  /* I don't know exactly which version
			      * needs this hack */
	freql = freq & 0xff;
	freqh = freq >> 8;

	pcm20_stereo(dev, !mono);
	return snd_aci_cmd(aci, ACI_WRITE_TUNE, freql, freqh);
}

static const struct v4l2_file_operations pcm20_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= video_ioctl2,
};

static int vidioc_querycap(struct file *file, void *priv,
				struct v4l2_capability *v)
{
	struct pcm20 *dev = video_drvdata(file);

	strlcpy(v->driver, "Miro PCM20", sizeof(v->driver));
	strlcpy(v->card, "Miro PCM20", sizeof(v->card));
	snprintf(v->bus_info, sizeof(v->bus_info), "ISA:%s", dev->v4l2_dev.name);
	v->device_caps = V4L2_CAP_TUNER | V4L2_CAP_RADIO;
	v->capabilities = v->device_caps | V4L2_CAP_DEVICE_CAPS;
	return 0;
}

static int vidioc_g_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	if (v->index)	/* Only 1 tuner */
		return -EINVAL;
	strlcpy(v->name, "FM", sizeof(v->name));
	v->type = V4L2_TUNER_RADIO;
	v->rangelow = 87*16000;
	v->rangehigh = 108*16000;
	v->signal = 0xffff;
	v->rxsubchans = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
	v->capability = V4L2_TUNER_CAP_LOW;
	v->audmode = V4L2_TUNER_MODE_MONO;
	return 0;
}

static int vidioc_s_tuner(struct file *file, void *priv,
				struct v4l2_tuner *v)
{
	return v->index ? -EINVAL : 0;
}

static int vidioc_g_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct pcm20 *dev = video_drvdata(file);

	if (f->tuner != 0)
		return -EINVAL;

	f->type = V4L2_TUNER_RADIO;
	f->frequency = dev->freq;
	return 0;
}


static int vidioc_s_frequency(struct file *file, void *priv,
				struct v4l2_frequency *f)
{
	struct pcm20 *dev = video_drvdata(file);

	if (f->tuner != 0 || f->type != V4L2_TUNER_RADIO)
		return -EINVAL;

	dev->freq = f->frequency;
	pcm20_setfreq(dev, f->frequency);
	return 0;
}

static int pcm20_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct pcm20 *dev = container_of(ctrl->handler, struct pcm20, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		pcm20_mute(dev, ctrl->val);
		return 0;
	}
	return -EINVAL;
}

static const struct v4l2_ioctl_ops pcm20_ioctl_ops = {
	.vidioc_querycap    = vidioc_querycap,
	.vidioc_g_tuner     = vidioc_g_tuner,
	.vidioc_s_tuner     = vidioc_s_tuner,
	.vidioc_g_frequency = vidioc_g_frequency,
	.vidioc_s_frequency = vidioc_s_frequency,
};

static const struct v4l2_ctrl_ops pcm20_ctrl_ops = {
	.s_ctrl = pcm20_s_ctrl,
};

static int __init pcm20_init(void)
{
	struct pcm20 *dev = &pcm20_card;
	struct v4l2_device *v4l2_dev = &dev->v4l2_dev;
	struct v4l2_ctrl_handler *hdl;
	int res;

	dev->aci = snd_aci_get_aci();
	if (dev->aci == NULL) {
		v4l2_err(v4l2_dev,
			 "you must load the snd-miro driver first!\n");
		return -ENODEV;
	}
	strlcpy(v4l2_dev->name, "radio-miropcm20", sizeof(v4l2_dev->name));
	mutex_init(&dev->lock);

	res = v4l2_device_register(NULL, v4l2_dev);
	if (res < 0) {
		v4l2_err(v4l2_dev, "could not register v4l2_device\n");
		return -EINVAL;
	}

	hdl = &dev->ctrl_handler;
	v4l2_ctrl_handler_init(hdl, 1);
	v4l2_ctrl_new_std(hdl, &pcm20_ctrl_ops,
			V4L2_CID_AUDIO_MUTE, 0, 1, 1, 1);
	v4l2_dev->ctrl_handler = hdl;
	if (hdl->error) {
		res = hdl->error;
		v4l2_err(v4l2_dev, "Could not register control\n");
		goto err_hdl;
	}
	strlcpy(dev->vdev.name, v4l2_dev->name, sizeof(dev->vdev.name));
	dev->vdev.v4l2_dev = v4l2_dev;
	dev->vdev.fops = &pcm20_fops;
	dev->vdev.ioctl_ops = &pcm20_ioctl_ops;
	dev->vdev.release = video_device_release_empty;
	dev->vdev.lock = &dev->lock;
	video_set_drvdata(&dev->vdev, dev);

	if (video_register_device(&dev->vdev, VFL_TYPE_RADIO, radio_nr) < 0)
		goto err_hdl;

	v4l2_info(v4l2_dev, "Mirosound PCM20 Radio tuner\n");
	return 0;
err_hdl:
	v4l2_ctrl_handler_free(hdl);
	v4l2_device_unregister(v4l2_dev);
	return -EINVAL;
}

MODULE_AUTHOR("Ruurd Reitsma, Krzysztof Helt");
MODULE_DESCRIPTION("A driver for the Miro PCM20 radio card.");
MODULE_LICENSE("GPL");

static void __exit pcm20_cleanup(void)
{
	struct pcm20 *dev = &pcm20_card;

	video_unregister_device(&dev->vdev);
	v4l2_ctrl_handler_free(&dev->ctrl_handler);
	v4l2_device_unregister(&dev->v4l2_dev);
}

module_init(pcm20_init);
module_exit(pcm20_cleanup);
