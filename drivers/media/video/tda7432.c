/*
 * For the STS-Thompson TDA7432 audio processor chip
 *
 * Handles audio functions: volume, balance, tone, loudness
 * This driver will not complain if used with any
 * other i2c device with the same address.
 *
 * Muting and tone control by Jonathan Isom <jisom@ematic.com>
 *
 * Copyright (c) 2000 Eric Sandeen <eric_sandeen@bigfoot.com>
 * This code is placed under the terms of the GNU General Public License
 * Based on tda9855.c by Steve VanDeBogart (vandebo@uclink.berkeley.edu)
 * Which was based on tda8425.c by Greg Alexander (c) 1998
 *
 * OPTIONS:
 * debug    - set to 1 if you'd like to see debug messages
 *            set to 2 if you'd like to be inundated with debug messages
 *
 * loudness - set between 0 and 15 for varying degrees of loudness effect
 *
 * maxvol   - set maximium volume to +20db (1), default is 0db(0)
 *
 *
 *  Revision: 0.7 - maxvol module parm to set maximium volume 0db or +20db
 *  				store if muted so we can return it
 *  				change balance only if flaged to
 *  Revision: 0.6 - added tone controls
 *  Revision: 0.5 - Fixed odd balance problem
 *  Revision: 0.4 - added muting
 *  Revision: 0.3 - Fixed silly reversed volume controls.  :)
 *  Revision: 0.2 - Cleaned up #defines
 *			fixed volume control
 *          Added I2C_DRIVERID_TDA7432
 *			added loudness insmod control
 *  Revision: 0.1 - initial version
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/videodev.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>

#include <media/v4l2-common.h>
#include <media/i2c-addr.h>

#ifndef VIDEO_AUDIO_BALANCE
# define VIDEO_AUDIO_BALANCE 32
#endif

MODULE_AUTHOR("Eric Sandeen <eric_sandeen@bigfoot.com>");
MODULE_DESCRIPTION("bttv driver for the tda7432 audio processor chip");
MODULE_LICENSE("GPL");

static int maxvol;
static int loudness; /* disable loudness by default */
static int debug;	 /* insmod parameter */
module_param(debug, int, S_IRUGO | S_IWUSR);
module_param(loudness, int, S_IRUGO);
MODULE_PARM_DESC(maxvol,"Set maximium volume to +20db (0), default is 0db(1)");
module_param(maxvol, int, S_IRUGO | S_IWUSR);


/* Address to scan (I2C address of this chip) */
static unsigned short normal_i2c[] = {
	I2C_ADDR_TDA7432 >> 1,
	I2C_CLIENT_END,
};
I2C_CLIENT_INSMOD;

/* Structure of address and subaddresses for the tda7432 */

struct tda7432 {
	int addr;
	int input;
	int volume;
	int muted;
	int bass, treble;
	int lf, lr, rf, rr;
	int loud;
	struct i2c_client c;
};
static struct i2c_driver driver;
static struct i2c_client client_template;

/* The TDA7432 is made by STS-Thompson
 * http://www.st.com
 * http://us.st.com/stonline/books/pdf/docs/4056.pdf
 *
 * TDA7432: I2C-bus controlled basic audio processor
 *
 * The TDA7432 controls basic audio functions like volume, balance,
 * and tone control (including loudness).  It also has four channel
 * output (for front and rear).  Since most vidcap cards probably
 * don't have 4 channel output, this driver will set front & rear
 * together (no independent control).
 */

		/* Subaddresses for TDA7432 */

#define TDA7432_IN	0x00 /* Input select                 */
#define TDA7432_VL	0x01 /* Volume                       */
#define TDA7432_TN	0x02 /* Bass, Treble (Tone)          */
#define TDA7432_LF	0x03 /* Attenuation LF (Left Front)  */
#define TDA7432_LR	0x04 /* Attenuation LR (Left Rear)   */
#define TDA7432_RF	0x05 /* Attenuation RF (Right Front) */
#define TDA7432_RR	0x06 /* Attenuation RR (Right Rear)  */
#define TDA7432_LD	0x07 /* Loudness                     */


		/* Masks for bits in TDA7432 subaddresses */

/* Many of these not used - just for documentation */

/* Subaddress 0x00 - Input selection and bass control */

/* Bits 0,1,2 control input:
 * 0x00 - Stereo input
 * 0x02 - Mono input
 * 0x03 - Mute  (Using Attenuators Plays better with modules)
 * Mono probably isn't used - I'm guessing only the stereo
 * input is connected on most cards, so we'll set it to stereo.
 *
 * Bit 3 controls bass cut: 0/1 is non-symmetric/symmetric bass cut
 * Bit 4 controls bass range: 0/1 is extended/standard bass range
 *
 * Highest 3 bits not used
 */

#define TDA7432_STEREO_IN	0
#define TDA7432_MONO_IN		2	/* Probably won't be used */
#define TDA7432_BASS_SYM	1 << 3
#define TDA7432_BASS_NORM	1 << 4

/* Subaddress 0x01 - Volume */

/* Lower 7 bits control volume from -79dB to +32dB in 1dB steps
 * Recommended maximum is +20 dB
 *
 * +32dB: 0x00
 * +20dB: 0x0c
 *   0dB: 0x20
 * -79dB: 0x6f
 *
 * MSB (bit 7) controls loudness: 1/0 is loudness on/off
 */

#define	TDA7432_VOL_0DB		0x20
#define TDA7432_LD_ON		1 << 7


/* Subaddress 0x02 - Tone control */

/* Bits 0,1,2 control absolute treble gain from 0dB to 14dB
 * 0x0 is 14dB, 0x7 is 0dB
 *
 * Bit 3 controls treble attenuation/gain (sign)
 * 1 = gain (+)
 * 0 = attenuation (-)
 *
 * Bits 4,5,6 control absolute bass gain from 0dB to 14dB
 * (This is only true for normal base range, set in 0x00)
 * 0x0 << 4 is 14dB, 0x7 is 0dB
 *
 * Bit 7 controls bass attenuation/gain (sign)
 * 1 << 7 = gain (+)
 * 0 << 7 = attenuation (-)
 *
 * Example:
 * 1 1 0 1 0 1 0 1 is +4dB bass, -4dB treble
 */

#define TDA7432_TREBLE_0DB		0xf
#define TDA7432_TREBLE			7
#define TDA7432_TREBLE_GAIN		1 << 3
#define TDA7432_BASS_0DB		0xf
#define TDA7432_BASS			7 << 4
#define TDA7432_BASS_GAIN		1 << 7


/* Subaddress 0x03 - Left  Front attenuation */
/* Subaddress 0x04 - Left  Rear  attenuation */
/* Subaddress 0x05 - Right Front attenuation */
/* Subaddress 0x06 - Right Rear  attenuation */

/* Bits 0,1,2,3,4 control attenuation from 0dB to -37.5dB
 * in 1.5dB steps.
 *
 * 0x00 is     0dB
 * 0x1f is -37.5dB
 *
 * Bit 5 mutes that channel when set (1 = mute, 0 = unmute)
 * We'll use the mute on the input, though (above)
 * Bits 6,7 unused
 */

#define TDA7432_ATTEN_0DB	0x00
#define TDA7432_MUTE        0x1 << 5


/* Subaddress 0x07 - Loudness Control */

/* Bits 0,1,2,3 control loudness from 0dB to -15dB in 1dB steps
 * when bit 4 is NOT set
 *
 * 0x0 is   0dB
 * 0xf is -15dB
 *
 * If bit 4 is set, then there is a flat attenuation according to
 * the lower 4 bits, as above.
 *
 * Bits 5,6,7 unused
 */



/* Begin code */

static int tda7432_write(struct i2c_client *client, int subaddr, int val)
{
	unsigned char buffer[2];
	v4l_dbg(2, debug,client,"In tda7432_write\n");
	v4l_dbg(1, debug,client,"Writing %d 0x%x\n", subaddr, val);
	buffer[0] = subaddr;
	buffer[1] = val;
	if (2 != i2c_master_send(client,buffer,2)) {
		v4l_err(client,"I/O error, trying (write %d 0x%x)\n",
		       subaddr, val);
		return -1;
	}
	return 0;
}

/* I don't think we ever actually _read_ the chip... */

static int tda7432_set(struct i2c_client *client)
{
	struct tda7432 *t = i2c_get_clientdata(client);
	unsigned char buf[16];
	v4l_dbg(2, debug,client,"In tda7432_set\n");

	v4l_dbg(1, debug,client,
		"tda7432: 7432_set(0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x)\n",
		t->input,t->volume,t->bass,t->treble,t->lf,t->lr,t->rf,t->rr,t->loud);
	buf[0]  = TDA7432_IN;
	buf[1]  = t->input;
	buf[2]  = t->volume;
	buf[3]  = t->bass;
	buf[4]	= t->treble;
	buf[5]  = t->lf;
	buf[6]  = t->lr;
	buf[7]  = t->rf;
	buf[8]  = t->rr;
	buf[9]  = t->loud;
	if (10 != i2c_master_send(client,buf,10)) {
		v4l_err(client,"I/O error, trying tda7432_set\n");
		return -1;
	}

	return 0;
}

static void do_tda7432_init(struct i2c_client *client)
{
	struct tda7432 *t = i2c_get_clientdata(client);
	v4l_dbg(2, debug,client,"In tda7432_init\n");

	t->input  = TDA7432_STEREO_IN |  /* Main (stereo) input   */
		    TDA7432_BASS_SYM  |  /* Symmetric bass cut    */
		    TDA7432_BASS_NORM;   /* Normal bass range     */
	t->volume =  0x3b ;				 /* -27dB Volume            */
	if (loudness)			 /* Turn loudness on?     */
		t->volume |= TDA7432_LD_ON;
	t->muted    = VIDEO_AUDIO_MUTE;
	t->treble   = TDA7432_TREBLE_0DB; /* 0dB Treble            */
	t->bass		= TDA7432_BASS_0DB; 	 /* 0dB Bass              */
	t->lf     = TDA7432_ATTEN_0DB;	 /* 0dB attenuation       */
	t->lr     = TDA7432_ATTEN_0DB;	 /* 0dB attenuation       */
	t->rf     = TDA7432_ATTEN_0DB;	 /* 0dB attenuation       */
	t->rr     = TDA7432_ATTEN_0DB;	 /* 0dB attenuation       */
	t->loud   = loudness;		 /* insmod parameter      */

	tda7432_set(client);
}

/* *********************** *
 * i2c interface functions *
 * *********************** */

static int tda7432_attach(struct i2c_adapter *adap, int addr, int kind)
{
	struct tda7432 *t;
	struct i2c_client *client;

	t = kzalloc(sizeof *t,GFP_KERNEL);
	if (!t)
		return -ENOMEM;

	client = &t->c;
	memcpy(client,&client_template,sizeof(struct i2c_client));
	client->adapter = adap;
	client->addr = addr;
	i2c_set_clientdata(client, t);

	do_tda7432_init(client);
	i2c_attach_client(client);

	v4l_info(client, "chip found @ 0x%x (%s)\n", addr << 1, adap->name);
	return 0;
}

static int tda7432_probe(struct i2c_adapter *adap)
{
	if (adap->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adap, &addr_data, tda7432_attach);
	return 0;
}

static int tda7432_detach(struct i2c_client *client)
{
	struct tda7432 *t  = i2c_get_clientdata(client);

	do_tda7432_init(client);
	i2c_detach_client(client);

	kfree(t);
	return 0;
}

static int tda7432_command(struct i2c_client *client,
			   unsigned int cmd, void *arg)
{
	struct tda7432 *t = i2c_get_clientdata(client);
	v4l_dbg(2, debug,client,"In tda7432_command\n");
	if (debug>1)
		v4l_i2c_print_ioctl(client,cmd);

	switch (cmd) {
	/* --- v4l ioctls --- */
	/* take care: bttv does userspace copying, we'll get a
	   kernel pointer here... */

	/* Query card - scale from TDA7432 settings to V4L settings */
	case VIDIOCGAUDIO:
	{
		struct video_audio *va = arg;

		va->flags |= VIDEO_AUDIO_VOLUME |
			VIDEO_AUDIO_BASS |
			VIDEO_AUDIO_TREBLE |
			VIDEO_AUDIO_MUTABLE;
		if (t->muted)
			va->flags |= VIDEO_AUDIO_MUTE;
		va->mode |= VIDEO_SOUND_STEREO;
		/* Master volume control
		 * V4L volume is min 0, max 65535
		 * TDA7432 Volume:
		 * Min (-79dB) is 0x6f
		 * Max (+20dB) is 0x07 (630)
		 * Max (0dB) is 0x20 (829)
		 * (Mask out bit 7 of vol - it's for the loudness setting)
		 */
		if (!maxvol){  /* max +20db */
			va->volume = ( 0x6f - (t->volume & 0x7F) ) * 630;
		} else {       /* max 0db   */
			va->volume = ( 0x6f - (t->volume & 0x7F) ) * 829;
		}

		/* Balance depends on L,R attenuation
		 * V4L balance is 0 to 65535, middle is 32768
		 * TDA7432 attenuation: min (0dB) is 0, max (-37.5dB) is 0x1f
		 * to scale up to V4L numbers, mult by 1057
		 * attenuation exists for lf, lr, rf, rr
		 * we use only lf and rf (front channels)
		 */

		if ( (t->lf) < (t->rf) )
			/* right is attenuated, balance shifted left */
			va->balance = (32768 - 1057*(t->rf));
		else
			/* left is attenuated, balance shifted right */
			va->balance = (32768 + 1057*(t->lf));

		/* Bass/treble 4 bits each */
		va->bass=t->bass;
		if(va->bass >= 0x8)
			va->bass = ~(va->bass - 0x8) & 0xf;
		va->bass = (va->bass << 12)+(va->bass << 8)+(va->bass << 4)+(va->bass);
		va->treble=t->treble;
		if(va->treble >= 0x8)
			va->treble = ~(va->treble - 0x8) & 0xf;
		va->treble = (va->treble << 12)+(va->treble << 8)+(va->treble << 4)+(va->treble);

		break; /* VIDIOCGAUDIO case */
	}

	/* Set card - scale from V4L settings to TDA7432 settings */
	case VIDIOCSAUDIO:
	{
		struct video_audio *va = arg;

		if(va->flags & VIDEO_AUDIO_VOLUME){
			if(!maxvol){ /* max +20db */
				t->volume = 0x6f - ((va->volume)/630);
			} else {    /* max 0db   */
				t->volume = 0x6f - ((va->volume)/829);
			}

		if (loudness)		/* Turn on the loudness bit */
			t->volume |= TDA7432_LD_ON;

			tda7432_write(client,TDA7432_VL, t->volume);
		}

		if(va->flags & VIDEO_AUDIO_BASS)
		{
			t->bass = va->bass >> 12;
			if(t->bass>= 0x8)
					t->bass = (~t->bass & 0xf) + 0x8 ;
		}
		if(va->flags & VIDEO_AUDIO_TREBLE)
		{
			t->treble= va->treble >> 12;
			if(t->treble>= 0x8)
					t->treble = (~t->treble & 0xf) + 0x8 ;
		}
		if(va->flags & (VIDEO_AUDIO_TREBLE| VIDEO_AUDIO_BASS))
			tda7432_write(client,TDA7432_TN, 0x10 | (t->bass << 4) | t->treble );

		if(va->flags & VIDEO_AUDIO_BALANCE)	{
		if (va->balance < 32768)
		{
			/* shifted to left, attenuate right */
			t->rr = (32768 - va->balance)/1057;
			t->rf = t->rr;
			t->lr = TDA7432_ATTEN_0DB;
			t->lf = TDA7432_ATTEN_0DB;
		}
		else if(va->balance > 32769)
		{
			/* shifted to right, attenuate left */
			t->lf = (va->balance - 32768)/1057;
			t->lr = t->lf;
			t->rr = TDA7432_ATTEN_0DB;
			t->rf = TDA7432_ATTEN_0DB;
		}
		else
		{
			/* centered */
			t->rr = TDA7432_ATTEN_0DB;
			t->rf = TDA7432_ATTEN_0DB;
			t->lf = TDA7432_ATTEN_0DB;
			t->lr = TDA7432_ATTEN_0DB;
		}
		}

		t->muted=(va->flags & VIDEO_AUDIO_MUTE);
		if (t->muted)
		{
			/* Mute & update balance*/
			tda7432_write(client,TDA7432_LF, t->lf | TDA7432_MUTE);
			tda7432_write(client,TDA7432_LR, t->lr | TDA7432_MUTE);
			tda7432_write(client,TDA7432_RF, t->rf | TDA7432_MUTE);
			tda7432_write(client,TDA7432_RR, t->rr | TDA7432_MUTE);
		} else {
			tda7432_write(client,TDA7432_LF, t->lf);
			tda7432_write(client,TDA7432_LR, t->lr);
			tda7432_write(client,TDA7432_RF, t->rf);
			tda7432_write(client,TDA7432_RR, t->rr);
		}

		break;

	} /* end of VIDEOCSAUDIO case */

	} /* end of (cmd) switch */

	return 0;
}

static struct i2c_driver driver = {
	.driver = {
		.name    = "tda7432",
	},
	.id              = I2C_DRIVERID_TDA7432,
	.attach_adapter  = tda7432_probe,
	.detach_client   = tda7432_detach,
	.command         = tda7432_command,
};

static struct i2c_client client_template =
{
	.name       = "tda7432",
	.driver     = &driver,
};

static int __init tda7432_init(void)
{
	if ( (loudness < 0) || (loudness > 15) ) {
		printk(KERN_ERR "loudness parameter must be between 0 and 15\n");
		return -EINVAL;
	}

	return i2c_add_driver(&driver);
}

static void __exit tda7432_fini(void)
{
	i2c_del_driver(&driver);
}

module_init(tda7432_init);
module_exit(tda7432_fini);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
