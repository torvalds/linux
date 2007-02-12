/* Miro PCM20 radio driver for Linux radio support
 * (c) 1998 Ruurd Reitsma <R.A.Reitsma@wbmt.tudelft.nl>
 * Thanks to Norberto Pellici for the ACI device interface specification
 * The API part is based on the radiotrack driver by M. Kirkwood
 * This driver relies on the aci mixer (drivers/sound/aci.c)
 * Look there for further info...
 */

/* Revision history:
 *
 *   1998        Ruurd Reitsma <R.A.Reitsma@wbmt.tudelft.nl>
 *   2000-09-05  Robert Siemer <Robert.Siemer@gmx.de>
 *        removed unfinished volume control (maybe adding it later again)
 *        use OSS-mixer; added stereo control
 */

/* What ever you think about the ACI, version 0x07 is not very well!
 * I can't get frequency, 'tuner status', 'tuner flags' or mute/mono
 * conditions...                Robert
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/videodev.h>
#include <media/v4l2-common.h>
#include "oss/aci.h"
#include "miropcm20-rds-core.h"

static int radio_nr = -1;
module_param(radio_nr, int, 0);

struct pcm20_device {
	unsigned long freq;
	int muted;
	int stereo;
};


static int pcm20_mute(struct pcm20_device *dev, unsigned char mute)
{
	dev->muted = mute;
	return aci_write_cmd(ACI_SET_TUNERMUTE, mute);
}

static int pcm20_stereo(struct pcm20_device *dev, unsigned char stereo)
{
	dev->stereo = stereo;
	return aci_write_cmd(ACI_SET_TUNERMONO, !stereo);
}

static int pcm20_setfreq(struct pcm20_device *dev, unsigned long freq)
{
	unsigned char freql;
	unsigned char freqh;

	dev->freq=freq;

	freq /= 160;
	if (!(aci_version==0x07 || aci_version>=0xb0))
		freq /= 10;  /* I don't know exactly which version
			      * needs this hack */
	freql = freq & 0xff;
	freqh = freq >> 8;

	aci_rds_cmd(RDS_RESET, NULL, 0);
	pcm20_stereo(dev, 1);

	return aci_rw_cmd(ACI_WRITE_TUNE, freql, freqh);
}

static int pcm20_getflags(struct pcm20_device *dev, __u32 *flags, __u16 *signal)
{
	/* okay, check for signal, stereo and rds here... */
	int i;
	unsigned char buf;

	if ((i=aci_rw_cmd(ACI_READ_TUNERSTATION, -1, -1))<0)
		return i;
	pr_debug("check_sig: 0x%x\n", i);
	if (i & 0x80) {
		/* no signal from tuner */
		*flags=0;
		*signal=0;
		return 0;
	} else
		*signal=0xffff;

	if ((i=aci_rw_cmd(ACI_READ_TUNERSTEREO, -1, -1))<0)
		return i;
	if (i & 0x40) {
		*flags=0;
	} else {
		/* stereo */
		*flags=VIDEO_TUNER_STEREO_ON;
		/* I can't see stereo, when forced to mono */
		dev->stereo=1;
	}

	if ((i=aci_rds_cmd(RDS_STATUS, &buf, 1))<0)
		return i;
	if (buf & 1)
		/* RDS available */
		*flags|=VIDEO_TUNER_RDS_ON;
	else
		return 0;

	if ((i=aci_rds_cmd(RDS_RXVALUE, &buf, 1))<0)
		return i;
	pr_debug("rds-signal: %d\n", buf);
	if (buf > 15) {
		printk("miropcm20-radio: RX strengths unexpected high...\n");
		buf=15;
	}
	/* refine signal */
	if ((*signal=SCALE(15, 0xffff, buf))==0)
		*signal = 1;

	return 0;
}

static int pcm20_do_ioctl(struct inode *inode, struct file *file,
			  unsigned int cmd, void *arg)
{
	struct video_device *dev = video_devdata(file);
	struct pcm20_device *pcm20 = dev->priv;
	int i;

	switch(cmd)
	{
		case VIDIOCGCAP:
		{
			struct video_capability *v = arg;
			memset(v,0,sizeof(*v));
			v->type=VID_TYPE_TUNER;
			strcpy(v->name, "Miro PCM20");
			v->channels=1;
			v->audios=1;
			return 0;
		}
		case VIDIOCGTUNER:
		{
			struct video_tuner *v = arg;
			if(v->tuner)	/* Only 1 tuner */
				return -EINVAL;
			v->rangelow=87*16000;
			v->rangehigh=108*16000;
			pcm20_getflags(pcm20, &v->flags, &v->signal);
			v->flags|=VIDEO_TUNER_LOW;
			v->mode=VIDEO_MODE_AUTO;
			strcpy(v->name, "FM");
			return 0;
		}
		case VIDIOCSTUNER:
		{
			struct video_tuner *v = arg;
			if(v->tuner!=0)
				return -EINVAL;
			/* Only 1 tuner so no setting needed ! */
			return 0;
		}
		case VIDIOCGFREQ:
		{
			unsigned long *freq = arg;
			*freq = pcm20->freq;
			return 0;
		}
		case VIDIOCSFREQ:
		{
			unsigned long *freq = arg;
			pcm20->freq = *freq;
			i=pcm20_setfreq(pcm20, pcm20->freq);
			pr_debug("First view (setfreq): 0x%x\n", i);
			return i;
		}
		case VIDIOCGAUDIO:
		{
			struct video_audio *v = arg;
			memset(v,0, sizeof(*v));
			v->flags=VIDEO_AUDIO_MUTABLE;
			if (pcm20->muted)
				v->flags|=VIDEO_AUDIO_MUTE;
			v->mode=VIDEO_SOUND_STEREO;
			if (pcm20->stereo)
				v->mode|=VIDEO_SOUND_MONO;
			/* v->step=2048; */
			strcpy(v->name, "Radio");
			return 0;
		}
		case VIDIOCSAUDIO:
		{
			struct video_audio *v = arg;
			if(v->audio)
				return -EINVAL;

			pcm20_mute(pcm20, !!(v->flags&VIDEO_AUDIO_MUTE));
			if(v->flags&VIDEO_SOUND_MONO)
				pcm20_stereo(pcm20, 0);
			if(v->flags&VIDEO_SOUND_STEREO)
				pcm20_stereo(pcm20, 1);

			return 0;
		}
		default:
			return -ENOIOCTLCMD;
	}
}

static int pcm20_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, pcm20_do_ioctl);
}

static struct pcm20_device pcm20_unit = {
	.freq   = 87*16000,
	.muted  = 1,
};

static const struct file_operations pcm20_fops = {
	.owner		= THIS_MODULE,
	.open           = video_exclusive_open,
	.release        = video_exclusive_release,
	.ioctl		= pcm20_ioctl,
	.compat_ioctl	= v4l_compat_ioctl32,
	.llseek         = no_llseek,
};

static struct video_device pcm20_radio = {
	.owner		= THIS_MODULE,
	.name		= "Miro PCM 20 radio",
	.type		= VID_TYPE_TUNER,
	.hardware	= VID_HARDWARE_RTRACK,
	.fops           = &pcm20_fops,
	.priv		= &pcm20_unit
};

static int __init pcm20_init(void)
{
	if(video_register_device(&pcm20_radio, VFL_TYPE_RADIO, radio_nr)==-1)
		goto video_register_device;

	if(attach_aci_rds()<0)
		goto attach_aci_rds;

	printk(KERN_INFO "Miro PCM20 radio card driver.\n");

	return 0;

 attach_aci_rds:
	video_unregister_device(&pcm20_radio);
 video_register_device:
	return -EINVAL;
}

MODULE_AUTHOR("Ruurd Reitsma");
MODULE_DESCRIPTION("A driver for the Miro PCM20 radio card.");
MODULE_LICENSE("GPL");

static void __exit pcm20_cleanup(void)
{
	unload_aci_rds();
	video_unregister_device(&pcm20_radio);
}

module_init(pcm20_init);
module_exit(pcm20_cleanup);
