/*
 * $Id: tuner-core.c,v 1.29 2005/06/21 15:40:33 mchehab Exp $
 *
 * i2c tv tuner chip device driver
 * core core, i.e. kernel interfaces, registering and so on
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/videodev.h>
#include <linux/init.h>

#include <media/tuner.h>
#include <media/audiochip.h>

/*
 * comment line bellow to return to old behavor, where only one I2C device is supported
 */

#define UNSET (-1U)

/* standard i2c insmod options */
static unsigned short normal_i2c[] = {
	0x4b, /* tda8290 */
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
	I2C_CLIENT_END
};
I2C_CLIENT_INSMOD;

/* insmod options used at init time => read/only */
static unsigned int addr  =  0;
module_param(addr, int, 0444);

/* insmod options used at runtime => read/write */
unsigned int tuner_debug   = 0;
module_param(tuner_debug,       int, 0644);

static unsigned int tv_range[2]    = { 44, 958 };
static unsigned int radio_range[2] = { 65, 108 };

module_param_array(tv_range,    int, NULL, 0644);
module_param_array(radio_range, int, NULL, 0644);

MODULE_DESCRIPTION("device driver for various TV and TV+FM radio tuners");
MODULE_AUTHOR("Ralph Metzler, Gerd Knorr, Gunther Mayer");
MODULE_LICENSE("GPL");

static int this_adap;
static unsigned short first_tuner, tv_tuner, radio_tuner;

static struct i2c_driver driver;
static struct i2c_client client_template;

/* ---------------------------------------------------------------------- */

/* Set tuner frequency,  freq in Units of 62.5kHz = 1/16MHz */
static void set_tv_freq(struct i2c_client *c, unsigned int freq)
{
	struct tuner *t = i2c_get_clientdata(c);

	if (t->type == UNSET) {
		tuner_info("tuner type not set\n");
		return;
	}
	if (NULL == t->tv_freq) {
		tuner_info("Huh? tv_set is NULL?\n");
		return;
	}
	if (freq < tv_range[0]*16 || freq > tv_range[1]*16) {
			tuner_info("TV freq (%d.%02d) out of range (%d-%d)\n",
				   freq/16,freq%16*100/16,tv_range[0],tv_range[1]);
	}
	t->tv_freq(c,freq);
}

static void set_radio_freq(struct i2c_client *c, unsigned int freq)
{
	struct tuner *t = i2c_get_clientdata(c);

	if (t->type == UNSET) {
		tuner_info("tuner type not set\n");
		return;
	}
	if (NULL == t->radio_freq) {
		tuner_info("no radio tuning for this one, sorry.\n");
		return;
	}
	if (freq >= radio_range[0]*16000 && freq <= radio_range[1]*16000) {
		if (tuner_debug)
			tuner_info("radio freq step 62.5Hz (%d.%06d)\n",
			 		    freq/16000,freq%16000*1000/16);
		t->radio_freq(c,freq);
        } else {
		tuner_info("radio freq (%d.%02d) out of range (%d-%d)\n",
			    freq/16,freq%16*100/16,
			    radio_range[0],radio_range[1]);
	}

	return;
}

static void set_freq(struct i2c_client *c, unsigned long freq)
{
	struct tuner *t = i2c_get_clientdata(c);

	switch (t->mode) {
	case V4L2_TUNER_RADIO:
		tuner_dbg("radio freq set to %lu.%02lu\n",
			  freq/16,freq%16*100/16);
		set_radio_freq(c,freq);
		break;
	case V4L2_TUNER_ANALOG_TV:
	case V4L2_TUNER_DIGITAL_TV:
		tuner_dbg("tv freq set to %lu.%02lu\n",
			  freq/16,freq%16*100/16);
		set_tv_freq(c, freq);
		break;
	}
	t->freq = freq;
}

static void set_type(struct i2c_client *c, unsigned int type)
{
	struct tuner *t = i2c_get_clientdata(c);
	unsigned char buffer[4];

	/* sanity check */
	if (type == UNSET || type == TUNER_ABSENT)
		return;
	if (type >= tuner_count)
		return;

	if (NULL == t->i2c.dev.driver) {
		/* not registered yet */
		t->type = type;
		return;
	}
	if ((t->initialized) && (t->type == type))
		/* run only once except type change  Hac 04/05*/
		return;

	t->initialized = 1;

	t->type = type;
	switch (t->type) {
	case TUNER_MT2032:
		microtune_init(c);
		break;
	case TUNER_PHILIPS_TDA8290:
		tda8290_init(c);
		break;
	case TUNER_TEA5767:
		if (tea5767_tuner_init(c)==EINVAL) t->type=TUNER_ABSENT;
		break;
	case TUNER_PHILIPS_FMD1216ME_MK3:
		buffer[0] = 0x0b;
		buffer[1] = 0xdc;
		buffer[2] = 0x9c;
		buffer[3] = 0x60;
    		i2c_master_send(c,buffer,4);
		mdelay(1);
		buffer[2] = 0x86;
		buffer[3] = 0x54;
    		i2c_master_send(c,buffer,4);
		default_tuner_init(c);
		break;
	default:
		/* TEA5767 autodetection code */
			if (tea5767_tuner_init(c)!=EINVAL) {
				t->type = TUNER_TEA5767;
				if (first_tuner == 0x60)
					first_tuner++;
				break;
			}

		default_tuner_init(c);
		break;
	}
	tuner_dbg ("I2C addr 0x%02x with type %d\n",c->addr<<1,type);
}

#define CHECK_ADDR(tp,cmd,tun)	if (client->addr!=tp) { \
			  return 0; } else if (tuner_debug) \
			  tuner_info ("Cmd %s accepted to "tun"\n",cmd);
#define CHECK_MODE(cmd)	if (t->mode == V4L2_TUNER_RADIO) { \
		 	CHECK_ADDR(radio_tuner,cmd,"radio") } else \
			{ CHECK_ADDR(tv_tuner,cmd,"TV"); }

static void set_addr(struct i2c_client *c, struct tuner_addr *tun_addr)
{
	/* ADDR_UNSET defaults to first available tuner */
	if ( tun_addr->addr == ADDR_UNSET ) {
		if (first_tuner != c->addr)
			return;
		switch (tun_addr->v4l2_tuner) {
		case V4L2_TUNER_RADIO:
	 		radio_tuner=c->addr;
			break;
		default:
			tv_tuner=c->addr;
			break;
		}
	} else {
		/* Sets tuner to its configured value */
		switch (tun_addr->v4l2_tuner) {
		case V4L2_TUNER_RADIO:
 			radio_tuner=tun_addr->addr;
			if ( tun_addr->addr == c->addr ) set_type(c,tun_addr->type);
			return;
		default:
			tv_tuner=tun_addr->addr;
			if ( tun_addr->addr == c->addr ) set_type(c,tun_addr->type);
			return;
		}
	}
	set_type(c,tun_addr->type);
}

static char pal[] = "-";
module_param_string(pal, pal, sizeof(pal), 0644);

static int tuner_fixup_std(struct tuner *t)
{
	if ((t->std & V4L2_STD_PAL) == V4L2_STD_PAL) {
		/* get more precise norm info from insmod option */
		switch (pal[0]) {
		case 'b':
		case 'B':
		case 'g':
		case 'G':
			tuner_dbg("insmod fixup: PAL => PAL-BG\n");
			t->std = V4L2_STD_PAL_BG;
			break;
		case 'i':
		case 'I':
			tuner_dbg("insmod fixup: PAL => PAL-I\n");
			t->std = V4L2_STD_PAL_I;
			break;
		case 'd':
		case 'D':
		case 'k':
		case 'K':
			tuner_dbg("insmod fixup: PAL => PAL-DK\n");
			t->std = V4L2_STD_PAL_DK;
			break;
		}
	}
	return 0;
}

/* ---------------------------------------------------------------------- */

static int tuner_attach(struct i2c_adapter *adap, int addr, int kind)
{
	struct tuner *t;

	/* by default, first I2C card is both tv and radio tuner */
	if (this_adap == 0) {
		first_tuner = addr;
		tv_tuner = addr;
		radio_tuner = addr;
	}
	this_adap++;

        client_template.adapter = adap;
        client_template.addr = addr;

        t = kmalloc(sizeof(struct tuner),GFP_KERNEL);
        if (NULL == t)
                return -ENOMEM;
        memset(t,0,sizeof(struct tuner));
        memcpy(&t->i2c,&client_template,sizeof(struct i2c_client));
	i2c_set_clientdata(&t->i2c, t);
	t->type       = UNSET;
	t->radio_if2  = 10700*1000; /* 10.7MHz - FM radio */
	t->audmode    = V4L2_TUNER_MODE_STEREO;

        i2c_attach_client(&t->i2c);
	tuner_info("chip found @ 0x%x (%s)\n",
		   addr << 1, adap->name);

	set_type(&t->i2c, t->type);
	return 0;
}

static int tuner_probe(struct i2c_adapter *adap)
{
	if (0 != addr) {
		normal_i2c[0] = addr;
		normal_i2c[1] = I2C_CLIENT_END;
	}
	this_adap = 0;

	first_tuner = 0;
	tv_tuner = 0;
	radio_tuner = 0;

	if (adap->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adap, &addr_data, tuner_attach);
	return 0;
}

static int tuner_detach(struct i2c_client *client)
{
	struct tuner *t = i2c_get_clientdata(client);
	int err;

	err=i2c_detach_client(&t->i2c);
	if (err) {
		tuner_warn ("Client deregistration failed, client not detached.\n");
		return err;
	}

	kfree(t);
	return 0;
}

#define SWITCH_V4L2	if (!t->using_v4l2 && tuner_debug) \
		          tuner_info("switching to v4l2\n"); \
	                  t->using_v4l2 = 1;
#define CHECK_V4L2	if (t->using_v4l2) { if (tuner_debug) \
			  tuner_info("ignore v4l1 call\n"); \
		          return 0; }

static int
tuner_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct tuner *t = i2c_get_clientdata(client);
        unsigned int *iarg = (int*)arg;

        switch (cmd) {
	/* --- configuration --- */
	case TUNER_SET_TYPE:
		set_type(client,*iarg);
		break;
	case TUNER_SET_TYPE_ADDR:
		set_addr(client,(struct tuner_addr *)arg);
		break;
	case AUDC_SET_RADIO:
		t->mode = V4L2_TUNER_RADIO;
		CHECK_ADDR(tv_tuner,"AUDC_SET_RADIO","TV");

		if (V4L2_TUNER_RADIO != t->mode) {
			set_tv_freq(client,400 * 16);
		}
		break;
	case AUDC_CONFIG_PINNACLE:
		CHECK_ADDR(tv_tuner,"AUDC_CONFIG_PINNACLE","TV");
		switch (*iarg) {
		case 2:
			tuner_dbg("pinnacle pal\n");
			t->radio_if2 = 33300 * 1000;
			break;
		case 3:
			tuner_dbg("pinnacle ntsc\n");
			t->radio_if2 = 41300 * 1000;
			break;
		}
		break;
	/* --- v4l ioctls --- */
	/* take care: bttv does userspace copying, we'll get a
	   kernel pointer here... */
	case VIDIOCSCHAN:
	{
		static const v4l2_std_id map[] = {
			[ VIDEO_MODE_PAL   ] = V4L2_STD_PAL,
			[ VIDEO_MODE_NTSC  ] = V4L2_STD_NTSC_M,
			[ VIDEO_MODE_SECAM ] = V4L2_STD_SECAM,
			[ 4 /* bttv */     ] = V4L2_STD_PAL_M,
			[ 5 /* bttv */     ] = V4L2_STD_PAL_N,
			[ 6 /* bttv */     ] = V4L2_STD_NTSC_M_JP,
		};
		struct video_channel *vc = arg;

		CHECK_V4L2;
		t->mode = V4L2_TUNER_ANALOG_TV;
		CHECK_ADDR(tv_tuner,"VIDIOCSCHAN","TV");

		if (vc->norm < ARRAY_SIZE(map))
			t->std = map[vc->norm];
		tuner_fixup_std(t);
		if (t->freq)
			set_tv_freq(client,t->freq);
		return 0;
	}
	case VIDIOCSFREQ:
	{
		unsigned long *v = arg;

		CHECK_MODE("VIDIOCSFREQ");
		CHECK_V4L2;
		set_freq(client,*v);
		return 0;
	}
	case VIDIOCGTUNER:
	{
		struct video_tuner *vt = arg;

		CHECK_ADDR(radio_tuner,"VIDIOCGTUNER","radio");
		CHECK_V4L2;
		if (V4L2_TUNER_RADIO == t->mode) {
			if (t->has_signal)
				vt->signal = t->has_signal(client);
			if (t->is_stereo) {
				if (t->is_stereo(client))
					vt->flags |= VIDEO_TUNER_STEREO_ON;
				else
					vt->flags &= ~VIDEO_TUNER_STEREO_ON;
			}
			vt->flags |= V4L2_TUNER_CAP_LOW; /* Allow freqs at 62.5 Hz */

			vt->rangelow = radio_range[0] * 16000;
			vt->rangehigh = radio_range[1] * 16000;

		} else {
			vt->rangelow = tv_range[0] * 16;
			vt->rangehigh = tv_range[1] * 16;
		}

		return 0;
	}
	case VIDIOCGAUDIO:
	{
		struct video_audio *va = arg;

		CHECK_ADDR(radio_tuner,"VIDIOCGAUDIO","radio");
		CHECK_V4L2;
		if (V4L2_TUNER_RADIO == t->mode  &&  t->is_stereo)
			va->mode = t->is_stereo(client)
				? VIDEO_SOUND_STEREO
				: VIDEO_SOUND_MONO;
		return 0;
	}

	case VIDIOC_S_STD:
	{
		v4l2_std_id *id = arg;

		SWITCH_V4L2;
		t->mode = V4L2_TUNER_ANALOG_TV;
		CHECK_ADDR(tv_tuner,"VIDIOC_S_STD","TV");

		t->std = *id;
		tuner_fixup_std(t);
		if (t->freq)
			set_freq(client,t->freq);
		break;
	}
	case VIDIOC_S_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		CHECK_MODE("VIDIOC_S_FREQUENCY");
		SWITCH_V4L2;
		if (V4L2_TUNER_RADIO == f->type &&
		    V4L2_TUNER_RADIO != t->mode)
			set_tv_freq(client,400*16);
		t->mode  = f->type;
		set_freq(client,f->frequency);
		break;
	}
	case VIDIOC_G_FREQUENCY:
	{
		struct v4l2_frequency *f = arg;

		CHECK_MODE("VIDIOC_G_FREQUENCY");
		SWITCH_V4L2;
		f->type = t->mode;
		f->frequency = t->freq;
		break;
	}
	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *tuner = arg;

		CHECK_MODE("VIDIOC_G_TUNER");
		SWITCH_V4L2;
		if (V4L2_TUNER_RADIO == t->mode) {
			if (t->has_signal)
				tuner -> signal = t->has_signal(client);
			if (t->is_stereo) {
				if (t->is_stereo(client)) {
					tuner -> rxsubchans = V4L2_TUNER_SUB_STEREO | V4L2_TUNER_SUB_MONO;
				} else {
					tuner -> rxsubchans = V4L2_TUNER_SUB_MONO;
				}
			}
			tuner->capability |= V4L2_TUNER_CAP_LOW | V4L2_TUNER_CAP_STEREO;
			tuner->audmode = t->audmode;

			tuner->rangelow = radio_range[0] * 16000;
			tuner->rangehigh = radio_range[1] * 16000;
		} else {
			tuner->rangelow = tv_range[0] * 16;
			tuner->rangehigh = tv_range[1] * 16;
		}
		break;
	}
	case VIDIOC_S_TUNER: /* Allow changing radio range and audio mode */
	{
		struct v4l2_tuner *tuner = arg;

		CHECK_ADDR(radio_tuner,"VIDIOC_S_TUNER","radio");
		SWITCH_V4L2;

		/* To switch the audio mode, applications initialize the
		   index and audmode fields and the reserved array and
		   call the VIDIOC_S_TUNER ioctl. */
		/* rxsubchannels: V4L2_TUNER_MODE_MONO, V4L2_TUNER_MODE_STEREO,
		   V4L2_TUNER_MODE_LANG1, V4L2_TUNER_MODE_LANG2,
		   V4L2_TUNER_MODE_SAP */

		if (tuner->audmode == V4L2_TUNER_MODE_MONO)
			t->audmode = V4L2_TUNER_MODE_MONO;
		else
			t->audmode = V4L2_TUNER_MODE_STEREO;

		set_radio_freq(client, t->freq);
		break;
	}
	case TDA9887_SET_CONFIG: /* Nothing to do on tuner-core */
		break;
	default:
		tuner_dbg ("Unimplemented IOCTL 0x%08x called to tuner.\n", cmd);
		/* nothing */
		break;
	}

	return 0;
}

static int tuner_suspend(struct device * dev, u32 state, u32 level)
{
	struct i2c_client *c = container_of(dev, struct i2c_client, dev);
	struct tuner *t = i2c_get_clientdata(c);

	tuner_dbg("suspend\n");
	/* FIXME: power down ??? */
	return 0;
}

static int tuner_resume(struct device * dev, u32 level)
{
	struct i2c_client *c = container_of(dev, struct i2c_client, dev);
	struct tuner *t = i2c_get_clientdata(c);

	tuner_dbg("resume\n");
	if (t->freq)
		set_freq(c,t->freq);
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver driver = {
	.owner          = THIS_MODULE,
        .name           = "tuner",
        .id             = I2C_DRIVERID_TUNER,
        .flags          = I2C_DF_NOTIFY,
        .attach_adapter = tuner_probe,
        .detach_client  = tuner_detach,
        .command        = tuner_command,
	.driver = {
		.suspend = tuner_suspend,
		.resume  = tuner_resume,
	},
};
static struct i2c_client client_template =
{
	I2C_DEVNAME("(tuner unset)"),
	.flags      = I2C_CLIENT_ALLOW_USE,
        .driver     = &driver,
};

static int __init tuner_init_module(void)
{
	return i2c_add_driver(&driver);
}

static void __exit tuner_cleanup_module(void)
{
	i2c_del_driver(&driver);
}

module_init(tuner_init_module);
module_exit(tuner_cleanup_module);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
