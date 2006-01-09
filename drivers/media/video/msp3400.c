/*
 * programming the msp34* sound processor family
 *
 * (c) 1997-2001 Gerd Knorr <kraxel@bytesex.org>
 *
 * what works and what doesn't:
 *
 *  AM-Mono
 *      Support for Hauppauge cards added (decoding handled by tuner) added by
 *      Frederic Crozat <fcrozat@mail.dotcom.fr>
 *
 *  FM-Mono
 *      should work. The stereo modes are backward compatible to FM-mono,
 *      therefore FM-Mono should be allways available.
 *
 *  FM-Stereo (B/G, used in germany)
 *      should work, with autodetect
 *
 *  FM-Stereo (satellite)
 *      should work, no autodetect (i.e. default is mono, but you can
 *      switch to stereo -- untested)
 *
 *  NICAM (B/G, L , used in UK, Scandinavia, Spain and France)
 *      should work, with autodetect. Support for NICAM was added by
 *      Pekka Pietikainen <pp@netppl.fi>
 *
 *
 * TODO:
 *   - better SAT support
 *
 *
 * 980623  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *         using soundcore instead of OSS
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/kthread.h>
#include <linux/suspend.h>
#include <asm/semaphore.h>
#include <asm/pgtable.h>

#include <linux/videodev.h>
#include <media/audiochip.h>
#include <media/v4l2-common.h>
#include "msp3400.h"

/* ---------------------------------------------------------------------- */

MODULE_DESCRIPTION("device driver for msp34xx TV sound processor");
MODULE_AUTHOR("Gerd Knorr");
MODULE_LICENSE("GPL");

#define OPMODE_AUTO       -1
#define OPMODE_MANUAL      0
#define OPMODE_AUTODETECT  1   /* use autodetect (>= msp3410 only) */
#define OPMODE_AUTOSELECT  2   /* use autodetect & autoselect (>= msp34xxG)   */

/* module parameters */
static int opmode   = OPMODE_AUTO;
static int debug    = 0;    /* debug output */
static int once     = 0;    /* no continous stereo monitoring */
static int amsound  = 0;    /* hard-wire AM sound at 6.5 Hz (france),
			       the autoscan seems work well only with FM... */
static int standard = 1;    /* Override auto detect of audio standard, if needed. */
static int dolby    = 0;

static int stereo_threshold = 0x190; /* a2 threshold for stereo/bilingual
					(msp34xxg only) 0x00a0-0x03c0 */

/* read-only */
module_param(opmode,           int, 0444);

/* read-write */
module_param(once,             int, 0644);
module_param(debug,            int, 0644);
module_param(stereo_threshold, int, 0644);
module_param(standard,         int, 0644);
module_param(amsound,          int, 0644);
module_param(dolby,            int, 0644);

MODULE_PARM_DESC(opmode, "Forces a MSP3400 opmode. 0=Manual, 1=Autodetect, 2=Autodetect and autoselect");
MODULE_PARM_DESC(once, "No continuous stereo monitoring");
MODULE_PARM_DESC(debug, "Enable debug messages");
MODULE_PARM_DESC(stereo_threshold, "Sets signal threshold to activate stereo");
MODULE_PARM_DESC(standard, "Specify audio standard: 32 = NTSC, 64 = radio, Default: Autodetect");
MODULE_PARM_DESC(amsound, "Hardwire AM sound at 6.5Hz (France), FM can autoscan");
MODULE_PARM_DESC(dolby, "Activates Dolby processsing");

/* ---------------------------------------------------------------------- */

#define msp3400_err(fmt, arg...) do { \
	printk(KERN_ERR "%s %d-%04x: " fmt, client->driver->driver.name, \
		i2c_adapter_id(client->adapter), client->addr , ## arg); } while (0)
#define msp3400_warn(fmt, arg...) do { \
	printk(KERN_WARNING "%s %d-%04x: " fmt, client->driver->driver.name, \
		i2c_adapter_id(client->adapter), client->addr , ## arg); } while (0)
#define msp3400_info(fmt, arg...) do { \
	printk(KERN_INFO "%s %d-%04x: " fmt, client->driver->driver.name, \
		i2c_adapter_id(client->adapter), client->addr , ## arg); } while (0)

/* level 1 debug. */
#define msp_dbg1(fmt, arg...) \
	do { \
		if (debug) \
			printk(KERN_INFO "%s debug %d-%04x: " fmt, \
			       client->driver->driver.name, \
			       i2c_adapter_id(client->adapter), client->addr , ## arg); \
	} while (0)

/* level 2 debug. */
#define msp_dbg2(fmt, arg...) \
	do { \
		if (debug >= 2) \
			printk(KERN_INFO "%s debug %d-%04x: " fmt, \
				client->driver->driver.name, \
				i2c_adapter_id(client->adapter), client->addr , ## arg); \
	} while (0)

/* level 3 debug. Use with care. */
#define msp_dbg3(fmt, arg...) \
	do { \
		if (debug >= 16) \
			printk(KERN_INFO "%s debug %d-%04x: " fmt, \
				client->driver->driver.name, \
				i2c_adapter_id(client->adapter), client->addr , ## arg); \
	} while (0)

/* control subaddress */
#define I2C_MSP_CONTROL 0x00
/* demodulator unit subaddress */
#define I2C_MSP_DEM     0x10
/* DSP unit subaddress */
#define I2C_MSP_DSP     0x12

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x80 >> 1, 0x88 >> 1, I2C_CLIENT_END };
>>>>>>> remote


I2C_CLIENT_INSMOD;

#define DFP_COUNT 0x41
static const int bl_dfp[] = {
	0x00, 0x01, 0x02, 0x03, 0x06, 0x08, 0x09, 0x0a,
	0x0b, 0x0d, 0x0e, 0x10
};

#define HAVE_NICAM(state)   (((state->rev2 >> 8) & 0xff) != 0)
#define HAVE_RADIO(state)   ((state->rev1 & 0x0f) >= 'G'-'@')

struct msp_state {
	int rev1, rev2;

	int opmode;
	int mode;
	int norm;
	int stereo;
	int nicam_on;
	int acb;
	int in_scart;
	int i2s_mode;
	int main, second;	/* sound carrier */
	int input;
	int source;             /* see msp34xxg_set_source */

	/* v4l2 */
	int audmode;
	int rxsubchans;

	int muted;
	int volume, balance;
	int bass, treble;

	/* shadow register set */
	int dfp_regs[DFP_COUNT];

	/* thread */
	struct task_struct   *kthread;
	wait_queue_head_t    wq;
	int                  restart:1;
	int                  watch_stereo:1;
};

#define VIDEO_MODE_RADIO 16      /* norm magic for radio mode */


/* ----------------------------------------------------------------------- */
/* functions for talking to the MSP3400C Sound processor                   */

static int msp_reset(struct i2c_client *client)
{
	/* reset and read revision code */
	static u8 reset_off[3] = { I2C_MSP_CONTROL, 0x80, 0x00 };
	static u8 reset_on[3]  = { I2C_MSP_CONTROL, 0x00, 0x00 };
	static u8 write[3]     = { I2C_MSP_DSP + 1, 0x00, 0x1e };
	u8 read[2];
	struct i2c_msg reset[2] = {
		{ client->addr, I2C_M_IGNORE_NAK, 3, reset_off },
		{ client->addr, I2C_M_IGNORE_NAK, 3, reset_on  },
	};
	struct i2c_msg test[2] = {
		{ client->addr, 0,        3, write },
		{ client->addr, I2C_M_RD, 2, read  },
	};

	msp_dbg3("msp_reset\n");
	if (i2c_transfer(client->adapter, &reset[0], 1) != 1 ||
	    i2c_transfer(client->adapter, &reset[1], 1) != 1 ||
	    i2c_transfer(client->adapter, test, 2) != 2) {
		msp_err("chip reset failed\n");
		return -1;
	}
	return 0;
}

static int msp_read(struct i2c_client *client, int dev, int addr)
{
	int err, retval;
	u8 write[3];
	u8 read[2];
	struct i2c_msg msgs[2] = {
		{ client->addr, 0,        3, write },
		{ client->addr, I2C_M_RD, 2, read  }
	};

	write[0] = dev + 1;
	write[1] = addr >> 8;
	write[2] = addr & 0xff;

	for (err = 0; err < 3; err++) {
		if (i2c_transfer(client->adapter, msgs, 2) == 2)
			break;
		msp_warn("I/O error #%d (read 0x%02x/0x%02x)\n", err,
		       dev, addr);
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(msecs_to_jiffies(10));
	}
	if (err == 3) {
		msp_warn("giving up, resetting chip. Sound will go off, sorry folks :-|\n");
		msp_reset(client);
		return -1;
	}
	retval = read[0] << 8 | read[1];
	msp_dbg3("msp_read(0x%x, 0x%x): 0x%x\n", dev, addr, retval);
	return retval;
}

static inline int msp_read_dem(struct i2c_client *client, int addr)
{
	return msp_read(client, I2C_MSP_DEM, addr);
}

static inline int msp_read_dsp(struct i2c_client *client, int addr)
{
	return msp_read(client, I2C_MSP_DSP, addr);
}

static int msp_write(struct i2c_client *client, int dev, int addr, int val)
{
	int err;
	u8 buffer[5];

	buffer[0] = dev;
	buffer[1] = addr >> 8;
	buffer[2] = addr &  0xff;
	buffer[3] = val  >> 8;
	buffer[4] = val  &  0xff;

	msp_dbg3("msp_write(0x%x, 0x%x, 0x%x)\n", dev, addr, val);
	for (err = 0; err < 3; err++) {
		if (i2c_master_send(client, buffer, 5) == 5)
			break;
		msp_warn("I/O error #%d (write 0x%02x/0x%02x)\n", err,
		       dev, addr);
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(msecs_to_jiffies(10));
	}
	if (err == 3) {
		msp_warn("giving up, resetting chip. Sound will go off, sorry folks :-|\n");
		msp_reset(client);
		return -1;
	}
	return 0;
}

static inline int msp_write_dem(struct i2c_client *client, int addr, int val)
{
	return msp_write(client, I2C_MSP_DEM, addr, val);
}

static inline int msp_write_dsp(struct i2c_client *client, int addr, int val)
{
	return msp_write(client, I2C_MSP_DSP, addr, val);
}

/* ----------------------------------------------------------------------- *
 * bits  9  8  5 - SCART DSP input Select:
 *       0  0  0 - SCART 1 to DSP input (reset position)
 *       0  1  0 - MONO to DSP input
 *       1  0  0 - SCART 2 to DSP input
 *       1  1  1 - Mute DSP input
 *
 * bits 11 10  6 - SCART 1 Output Select:
 *       0  0  0 - undefined (reset position)
 *       0  1  0 - SCART 2 Input to SCART 1 Output (for devices with 2 SCARTS)
 *       1  0  0 - MONO input to SCART 1 Output
 *       1  1  0 - SCART 1 DA to SCART 1 Output
 *       0  0  1 - SCART 2 DA to SCART 1 Output
 *       0  1  1 - SCART 1 Input to SCART 1 Output
 *       1  1  1 - Mute SCART 1 Output
 *
 * bits 13 12  7 - SCART 2 Output Select (for devices with 2 Output SCART):
 *       0  0  0 - SCART 1 DA to SCART 2 Output (reset position)
 *       0  1  0 - SCART 1 Input to SCART 2 Output
 *       1  0  0 - MONO input to SCART 2 Output
 *       0  0  1 - SCART 2 DA to SCART 2 Output
 *       0  1  1 - SCART 2 Input to SCART 2 Output
 *       1  1  0 - Mute SCART 2 Output
 *
 * Bits 4 to 0 should be zero.
 * ----------------------------------------------------------------------- */

static int scarts[3][9] = {
	/* MASK    IN1     IN2     IN1_DA  IN2_DA  IN3     IN4     MONO    MUTE   */
	/* SCART DSP Input select */
	{ 0x0320, 0x0000, 0x0200, -1,     -1,     0x0300, 0x0020, 0x0100, 0x0320 },
	/* SCART1 Output select */
	{ 0x0c40, 0x0440, 0x0400, 0x0c00, 0x0040, 0x0000, 0x0840, 0x0800, 0x0c40 },
	/* SCART2 Output select */
	{ 0x3080, 0x1000, 0x1080, 0x0000, 0x0080, 0x2080, 0x3080, 0x2000, 0x3000 },
};

static char *scart_names[] = {
	"mask", "in1", "in2", "in1 da", "in2 da", "in3", "in4", "mono", "mute"
};

static void msp_set_scart(struct i2c_client *client, int in, int out)
{
	struct msp_state *state = i2c_get_clientdata(client);

	state->in_scart=in;

	if (in >= 1 && in <= 8 && out >= 0 && out <= 2) {
		if (-1 == scarts[out][in])
			return;

		state->acb &= ~scarts[out][SCART_MASK];
		state->acb |=  scarts[out][in];
	} else
		state->acb = 0xf60; /* Mute Input and SCART 1 Output */

	msp_dbg1("scart switch: %s => %d (ACB=0x%04x)\n",
						scart_names[in], out, state->acb);
	msp_write_dsp(client, 0x13, state->acb);

	/* Sets I2S speed 0 = 1.024 Mbps, 1 = 2.048 Mbps */
	msp_write_dem(client, 0x40, state->i2s_mode);
}

/* ------------------------------------------------------------------------ */

/* This macro is allowed for *constants* only, gcc must calculate it
   at compile time.  Remember -- no floats in kernel mode */
#define MSP_CARRIER(freq) ((int)((float)(freq / 18.432) * (1 << 24)))

#define MSP_MODE_AM_DETECT   0
#define MSP_MODE_FM_RADIO    2
#define MSP_MODE_FM_TERRA    3
#define MSP_MODE_FM_SAT      4
#define MSP_MODE_FM_NICAM1   5
#define MSP_MODE_FM_NICAM2   6
#define MSP_MODE_AM_NICAM    7
#define MSP_MODE_BTSC        8
#define MSP_MODE_EXTERN      9

static struct msp3400c_init_data_dem {
	int fir1[6];
	int fir2[6];
	int cdo1;
	int cdo2;
	int ad_cv;
	int mode_reg;
	int dfp_src;
	int dfp_matrix;
} msp3400c_init_data[] = {
	{	/* AM (for carrier detect / msp3400) */
		{75, 19, 36, 35, 39, 40},
		{75, 19, 36, 35, 39, 40},
		MSP_CARRIER(5.5), MSP_CARRIER(5.5),
		0x00d0, 0x0500, 0x0020, 0x3000
	},{	/* AM (for carrier detect / msp3410) */
		{-1, -1, -8, 2, 59, 126},
		{-1, -1, -8, 2, 59, 126},
		MSP_CARRIER(5.5), MSP_CARRIER(5.5),
		0x00d0, 0x0100, 0x0020, 0x3000
	},{	/* FM Radio */
		{-8, -8, 4, 6, 78, 107},
		{-8, -8, 4, 6, 78, 107},
		MSP_CARRIER(10.7), MSP_CARRIER(10.7),
		0x00d0, 0x0480, 0x0020, 0x3000
	},{	/* Terrestial FM-mono + FM-stereo */
		{3, 18, 27, 48, 66, 72},
		{3, 18, 27, 48, 66, 72},
		MSP_CARRIER(5.5), MSP_CARRIER(5.5),
		0x00d0, 0x0480, 0x0030, 0x3000
	},{	/* Sat FM-mono */
		{ 1, 9, 14, 24, 33, 37},
		{ 3, 18, 27, 48, 66, 72},
		MSP_CARRIER(6.5), MSP_CARRIER(6.5),
		0x00c6, 0x0480, 0x0000, 0x3000
	},{	/* NICAM/FM --  B/G (5.5/5.85), D/K (6.5/5.85) */
		{-2, -8, -10, 10, 50, 86},
		{3, 18, 27, 48, 66, 72},
		MSP_CARRIER(5.5), MSP_CARRIER(5.5),
		0x00d0, 0x0040, 0x0120, 0x3000
	},{	/* NICAM/FM -- I (6.0/6.552) */
		{2, 4, -6, -4, 40, 94},
		{3, 18, 27, 48, 66, 72},
		MSP_CARRIER(6.0), MSP_CARRIER(6.0),
		0x00d0, 0x0040, 0x0120, 0x3000
	},{	/* NICAM/AM -- L (6.5/5.85) */
		{-2, -8, -10, 10, 50, 86},
		{-4, -12, -9, 23, 79, 126},
		MSP_CARRIER(6.5), MSP_CARRIER(6.5),
		0x00c6, 0x0140, 0x0120, 0x7c03
	},
};

struct msp3400c_carrier_detect {
	int   cdo;
	char *name;
};

static struct msp3400c_carrier_detect msp3400c_carrier_detect_main[] = {
	/* main carrier */
	{ MSP_CARRIER(4.5),        "4.5   NTSC"                   },
	{ MSP_CARRIER(5.5),        "5.5   PAL B/G"                },
	{ MSP_CARRIER(6.0),        "6.0   PAL I"                  },
	{ MSP_CARRIER(6.5),        "6.5   PAL D/K + SAT + SECAM"  }
};

static struct msp3400c_carrier_detect msp3400c_carrier_detect_55[] = {
	/* PAL B/G */
	{ MSP_CARRIER(5.7421875),  "5.742 PAL B/G FM-stereo"     },
	{ MSP_CARRIER(5.85),       "5.85  PAL B/G NICAM"         }
};

static struct msp3400c_carrier_detect msp3400c_carrier_detect_65[] = {
	/* PAL SAT / SECAM */
	{ MSP_CARRIER(5.85),       "5.85  PAL D/K + SECAM NICAM" },
	{ MSP_CARRIER(6.2578125),  "6.25  PAL D/K1 FM-stereo" },
	{ MSP_CARRIER(6.7421875),  "6.74  PAL D/K2 FM-stereo" },
	{ MSP_CARRIER(7.02),       "7.02  PAL SAT FM-stereo s/b" },
	{ MSP_CARRIER(7.20),       "7.20  PAL SAT FM-stereo s"   },
	{ MSP_CARRIER(7.38),       "7.38  PAL SAT FM-stereo b"   },
};

/* ------------------------------------------------------------------------ */

static void msp3400c_setcarrier(struct i2c_client *client, int cdo1, int cdo2)
{
	msp_write_dem(client, 0x0093, cdo1 & 0xfff);
	msp_write_dem(client, 0x009b, cdo1 >> 12);
	msp_write_dem(client, 0x00a3, cdo2 & 0xfff);
	msp_write_dem(client, 0x00ab, cdo2 >> 12);
	msp_write_dem(client, 0x0056, 0); /*LOAD_REG_1/2*/
}

static void msp_set_mute(struct i2c_client *client)
{
	msp_dbg1("mute audio\n");
	msp_write_dsp(client, 0x0000, 0); /* loudspeaker */
	msp_write_dsp(client, 0x0006, 0); /* headphones */
}

static void msp_set_audio(struct i2c_client *client)
{
	struct msp_state *state = i2c_get_clientdata(client);
	int val = 0, bal = 0, bass, treble;

	if (!state->muted)
		val = (state->volume * 0x7f / 65535) << 8;
	if (val)
		bal = (state->balance / 256) - 128;
	bass = ((state->bass - 32768) * 0x60 / 65535) << 8;
	treble = ((state->treble - 32768) * 0x60 / 65535) << 8;

	msp_dbg1("mute=%s volume=%d balance=%d bass=%d treble=%d\n",
		state->muted ? "on" : "off", state->volume, state->balance,
		state->bass, state->treble);

	msp_write_dsp(client, 0x0000, val); /* loudspeaker */
	msp_write_dsp(client, 0x0006, val); /* headphones */
	msp_write_dsp(client, 0x0007, state->muted ? 0x1 : (val | 0x1));
	msp_write_dsp(client, 0x0001, bal << 8);
	msp_write_dsp(client, 0x0002, bass); /* loudspeaker */
	msp_write_dsp(client, 0x0003, treble); /* loudspeaker */
}

static void msp3400c_setmode(struct i2c_client *client, int type)
{
	struct msp_state *state = i2c_get_clientdata(client);
	int i;

	msp_dbg1("setmode: %d\n",type);
	state->mode       = type;
	state->audmode    = V4L2_TUNER_MODE_MONO;
	state->rxsubchans = V4L2_TUNER_SUB_MONO;

	msp_write_dem(client, 0x00bb,          /* ad_cv */
		       msp3400c_init_data[type].ad_cv);

	for (i = 5; i >= 0; i--)                                   /* fir 1 */
		msp_write_dem(client, 0x0001,
			       msp3400c_init_data[type].fir1[i]);

	msp_write_dem(client, 0x0005, 0x0004); /* fir 2 */
	msp_write_dem(client, 0x0005, 0x0040);
	msp_write_dem(client, 0x0005, 0x0000);
	for (i = 5; i >= 0; i--)
		msp_write_dem(client, 0x0005,
			       msp3400c_init_data[type].fir2[i]);

	msp_write_dem(client, 0x0083,     /* MODE_REG */
		       msp3400c_init_data[type].mode_reg);

	msp3400c_setcarrier(client, msp3400c_init_data[type].cdo1,
			    msp3400c_init_data[type].cdo2);

	msp_write_dem(client, 0x0056, 0); /*LOAD_REG_1/2*/

	if (dolby) {
		msp_write_dsp(client, 0x0008,
			       0x0520); /* I2S1 */
		msp_write_dsp(client, 0x0009,
			       0x0620); /* I2S2 */
		msp_write_dsp(client, 0x000b,
			       msp3400c_init_data[type].dfp_src);
	} else {
		msp_write_dsp(client, 0x0008,
			       msp3400c_init_data[type].dfp_src);
		msp_write_dsp(client, 0x0009,
			       msp3400c_init_data[type].dfp_src);
		msp_write_dsp(client, 0x000b,
			       msp3400c_init_data[type].dfp_src);
	}
	msp_write_dsp(client, 0x000a,
		       msp3400c_init_data[type].dfp_src);
	msp_write_dsp(client, 0x000e,
		       msp3400c_init_data[type].dfp_matrix);

	if (HAVE_NICAM(state)) {
		/* nicam prescale */
		msp_write_dsp(client, 0x0010, 0x5a00); /* was: 0x3000 */
	}
}

/* given a bitmask of VIDEO_SOUND_XXX returns the "best" in the bitmask */
static int msp3400c_best_video_sound(int rxsubchans)
{
	if (rxsubchans & V4L2_TUNER_SUB_STEREO)
		return V4L2_TUNER_MODE_STEREO;
	if (rxsubchans & V4L2_TUNER_SUB_LANG1)
		return V4L2_TUNER_MODE_LANG1;
	if (rxsubchans & V4L2_TUNER_SUB_LANG2)
		return V4L2_TUNER_MODE_LANG2;
	return V4L2_TUNER_MODE_MONO;
}

/* turn on/off nicam + stereo */
static void msp3400c_setstereo(struct i2c_client *client, int mode)
{
	static char *strmode[] = { "0", "mono", "stereo", "3",
		"lang1", "5", "6", "7", "lang2"
	};
	struct msp_state *state = i2c_get_clientdata(client);
	int nicam = 0;		/* channel source: FM/AM or nicam */
	int src = 0;

	if (state->opmode == OPMODE_AUTOSELECT) {
		/* this method would break everything, let's make sure
		 * it's never called
		 */
		msp_dbg1("setstereo called with mode=%d instead of set_source (ignored)\n",
		     mode);
		return;
	}

	/* switch demodulator */
	switch (state->mode) {
	case MSP_MODE_FM_TERRA:
		msp_dbg1("FM setstereo: %s\n", strmode[mode]);
		msp3400c_setcarrier(client,state->second,state->main);
		switch (mode) {
		case V4L2_TUNER_MODE_STEREO:
			msp_write_dsp(client, 0x000e, 0x3001);
			break;
		case V4L2_TUNER_MODE_MONO:
		case V4L2_TUNER_MODE_LANG1:
		case V4L2_TUNER_MODE_LANG2:
			msp_write_dsp(client, 0x000e, 0x3000);
			break;
		}
		break;
	case MSP_MODE_FM_SAT:
		msp_dbg1("SAT setstereo: %s\n", strmode[mode]);
		switch (mode) {
		case V4L2_TUNER_MODE_MONO:
			msp3400c_setcarrier(client, MSP_CARRIER(6.5), MSP_CARRIER(6.5));
			break;
		case V4L2_TUNER_MODE_STEREO:
			msp3400c_setcarrier(client, MSP_CARRIER(7.2), MSP_CARRIER(7.02));
			break;
		case V4L2_TUNER_MODE_LANG1:
			msp3400c_setcarrier(client, MSP_CARRIER(7.38), MSP_CARRIER(7.02));
			break;
		case V4L2_TUNER_MODE_LANG2:
			msp3400c_setcarrier(client, MSP_CARRIER(7.38), MSP_CARRIER(7.02));
			break;
		}
		break;
	case MSP_MODE_FM_NICAM1:
	case MSP_MODE_FM_NICAM2:
	case MSP_MODE_AM_NICAM:
		msp_dbg1("NICAM setstereo: %s\n",strmode[mode]);
		msp3400c_setcarrier(client,state->second,state->main);
		if (state->nicam_on)
			nicam=0x0100;
		break;
	case MSP_MODE_BTSC:
		msp_dbg1("BTSC setstereo: %s\n",strmode[mode]);
		nicam=0x0300;
		break;
	case MSP_MODE_EXTERN:
		msp_dbg1("extern setstereo: %s\n",strmode[mode]);
		nicam = 0x0200;
		break;
	case MSP_MODE_FM_RADIO:
		msp_dbg1("FM-Radio setstereo: %s\n",strmode[mode]);
		break;
	default:
		msp_dbg1("mono setstereo\n");
		return;
	}

	/* switch audio */
	switch (msp3400c_best_video_sound(mode)) {
	case V4L2_TUNER_MODE_STEREO:
		src = 0x0020 | nicam;
		break;
	case V4L2_TUNER_MODE_MONO:
		if (state->mode == MSP_MODE_AM_NICAM) {
			msp_dbg1("switching to AM mono\n");
			/* AM mono decoding is handled by tuner, not MSP chip */
			/* SCART switching control register */
			msp_set_scart(client,SCART_MONO,0);
			src = 0x0200;
			break;
		}
	case V4L2_TUNER_MODE_LANG1:
		src = 0x0000 | nicam;
		break;
	case V4L2_TUNER_MODE_LANG2:
		src = 0x0010 | nicam;
		break;
	}
	msp_dbg1("setstereo final source/matrix = 0x%x\n", src);

	if (dolby) {
		msp_write_dsp(client, 0x0008,0x0520);
		msp_write_dsp(client, 0x0009,0x0620);
		msp_write_dsp(client, 0x000a,src);
		msp_write_dsp(client, 0x000b,src);
	} else {
		msp_write_dsp(client, 0x0008,src);
		msp_write_dsp(client, 0x0009,src);
		msp_write_dsp(client, 0x000a,src);
		msp_write_dsp(client, 0x000b,src);
	}
}

static void msp3400c_print_mode(struct i2c_client *client)
{
	struct msp_state *state = i2c_get_clientdata(client);

	if (state->main == state->second) {
		msp_dbg1("mono sound carrier: %d.%03d MHz\n",
		       state->main/910000,(state->main/910)%1000);
	} else {
		msp_dbg1("main sound carrier: %d.%03d MHz\n",
		       state->main/910000,(state->main/910)%1000);
	}
	if (state->mode == MSP_MODE_FM_NICAM1 || state->mode == MSP_MODE_FM_NICAM2)
		msp_dbg1("NICAM/FM carrier   : %d.%03d MHz\n",
		       state->second/910000,(state->second/910)%1000);
	if (state->mode == MSP_MODE_AM_NICAM)
		msp_dbg1("NICAM/AM carrier   : %d.%03d MHz\n",
		       state->second/910000,(state->second/910)%1000);
	if (state->mode == MSP_MODE_FM_TERRA &&
	    state->main != state->second) {
		msp_dbg1("FM-stereo carrier : %d.%03d MHz\n",
		       state->second/910000,(state->second/910)%1000);
	}
}

static void msp3400c_restore_dfp(struct i2c_client *client)
{
	struct msp_state *state = i2c_get_clientdata(client);
	int i;

	for (i = 0; i < DFP_COUNT; i++) {
		if (-1 == state->dfp_regs[i])
			continue;
		msp_write_dsp(client, i, state->dfp_regs[i]);
	}
}

/* if the dfp_regs is set, set what's in there. Otherwise, set the default value */
static int msp34xxg_write_dfp_with_default(struct i2c_client *client,
					int addr, int default_value)
{
	struct msp_state *state = i2c_get_clientdata(client);
	int value = default_value;
	if (addr < DFP_COUNT && -1 != state->dfp_regs[addr])
		value = state->dfp_regs[addr];
	return msp_write_dsp(client, addr, value);
}

/* ----------------------------------------------------------------------- */

static int autodetect_stereo(struct i2c_client *client)
{
	struct msp_state *state = i2c_get_clientdata(client);
	int val;
	int rxsubchans = state->rxsubchans;
	int newnicam   = state->nicam_on;
	int update = 0;

	switch (state->mode) {
	case MSP_MODE_FM_TERRA:
		val = msp_read_dsp(client, 0x18);
		if (val > 32767)
			val -= 65536;
		msp_dbg2("stereo detect register: %d\n",val);
		if (val > 4096) {
			rxsubchans = V4L2_TUNER_SUB_STEREO | V4L2_TUNER_SUB_MONO;
		} else if (val < -4096) {
			rxsubchans = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
		} else {
			rxsubchans = V4L2_TUNER_SUB_MONO;
		}
		newnicam = 0;
		break;
	case MSP_MODE_FM_NICAM1:
	case MSP_MODE_FM_NICAM2:
	case MSP_MODE_AM_NICAM:
		val = msp_read_dem(client, 0x23);
		msp_dbg2("nicam sync=%d, mode=%d\n",
			val & 1, (val & 0x1e) >> 1);

		if (val & 1) {
			/* nicam synced */
			switch ((val & 0x1e) >> 1)  {
			case 0:
			case 8:
				rxsubchans = V4L2_TUNER_SUB_STEREO;
				break;
			case 1:
			case 9:
				rxsubchans = V4L2_TUNER_SUB_MONO
					| V4L2_TUNER_SUB_LANG1;
				break;
			case 2:
			case 10:
				rxsubchans = V4L2_TUNER_SUB_MONO
					| V4L2_TUNER_SUB_LANG1
					| V4L2_TUNER_SUB_LANG2;
				break;
			default:
				rxsubchans = V4L2_TUNER_SUB_MONO;
				break;
			}
			newnicam=1;
		} else {
			newnicam = 0;
			rxsubchans = V4L2_TUNER_SUB_MONO;
		}
		break;
	case MSP_MODE_BTSC:
		val = msp_read_dem(client, 0x200);
		msp_dbg2("status=0x%x (pri=%s, sec=%s, %s%s%s)\n",
			val,
			(val & 0x0002) ? "no"     : "yes",
			(val & 0x0004) ? "no"     : "yes",
			(val & 0x0040) ? "stereo" : "mono",
			(val & 0x0080) ? ", nicam 2nd mono" : "",
			(val & 0x0100) ? ", bilingual/SAP"  : "");
		rxsubchans = V4L2_TUNER_SUB_MONO;
		if (val & 0x0040) rxsubchans |= V4L2_TUNER_SUB_STEREO;
		if (val & 0x0100) rxsubchans |= V4L2_TUNER_SUB_LANG1;
		break;
	}
	if (rxsubchans != state->rxsubchans) {
		update = 1;
		msp_dbg1("watch: rxsubchans %d => %d\n",
			state->rxsubchans,rxsubchans);
		state->rxsubchans = rxsubchans;
	}
	if (newnicam != state->nicam_on) {
		update = 1;
		msp_dbg1("watch: nicam %d => %d\n",
			state->nicam_on,newnicam);
		state->nicam_on = newnicam;
	}
	return update;
}

/*
 * A kernel thread for msp3400 control -- we don't want to block the
 * in the ioctl while doing the sound carrier & stereo detect
 */

static int msp_sleep(struct msp_state *state, int timeout)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&state->wq, &wait);
	if (!kthread_should_stop()) {
		if (timeout < 0) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
		} else {
			schedule_timeout_interruptible
						(msecs_to_jiffies(timeout));
		}
	}

	remove_wait_queue(&state->wq, &wait);
	try_to_freeze();
	return state->restart;
}

/* stereo/multilang monitoring */
static void watch_stereo(struct i2c_client *client)
{
	struct msp_state *state = i2c_get_clientdata(client);

	if (autodetect_stereo(client)) {
		if (state->stereo & V4L2_TUNER_MODE_STEREO)
			msp3400c_setstereo(client, V4L2_TUNER_MODE_STEREO);
		else if (state->stereo & VIDEO_SOUND_LANG1)
			msp3400c_setstereo(client, V4L2_TUNER_MODE_LANG1);
		else
			msp3400c_setstereo(client, V4L2_TUNER_MODE_MONO);
	}

	if (once)
		state->watch_stereo = 0;
}


static int msp3400c_thread(void *data)
{
	struct i2c_client *client = data;
	struct msp_state *state = i2c_get_clientdata(client);
	struct msp3400c_carrier_detect *cd;
	int count, max1,max2,val1,val2, val,this;


	msp_info("msp3400 daemon started\n");
	for (;;) {
		msp_dbg2("msp3400 thread: sleep\n");
		msp_sleep(state, -1);
		msp_dbg2("msp3400 thread: wakeup\n");

	restart:
		msp_dbg1("thread: restart scan\n");
		state->restart = 0;
		if (kthread_should_stop())
			break;

		if (VIDEO_MODE_RADIO == state->norm ||
		    MSP_MODE_EXTERN  == state->mode) {
			/* no carrier scan, just unmute */
			msp_info("thread: no carrier scan\n");
			msp_set_audio(client);
			continue;
		}

		/* mute */
		msp_set_mute(client);
		msp3400c_setmode(client, MSP_MODE_AM_DETECT /* +1 */ );
		val1 = val2 = 0;
		max1 = max2 = -1;
		state->watch_stereo = 0;

		/* some time for the tuner to sync */
		if (msp_sleep(state,200))
			goto restart;

		/* carrier detect pass #1 -- main carrier */
		cd = msp3400c_carrier_detect_main;
		count = ARRAY_SIZE(msp3400c_carrier_detect_main);

		if (amsound && (state->norm == VIDEO_MODE_SECAM)) {
			/* autodetect doesn't work well with AM ... */
			max1 = 3;
			count = 0;
			msp_dbg1("AM sound override\n");
		}

		for (this = 0; this < count; this++) {
			msp3400c_setcarrier(client, cd[this].cdo,cd[this].cdo);
			if (msp_sleep(state,100))
				goto restart;
			val = msp_read_dsp(client, 0x1b);
			if (val > 32767)
				val -= 65536;
			if (val1 < val)
				val1 = val, max1 = this;
			msp_dbg1("carrier1 val: %5d / %s\n", val,cd[this].name);
		}

		/* carrier detect pass #2 -- second (stereo) carrier */
		switch (max1) {
		case 1: /* 5.5 */
			cd = msp3400c_carrier_detect_55;
			count = ARRAY_SIZE(msp3400c_carrier_detect_55);
			break;
		case 3: /* 6.5 */
			cd = msp3400c_carrier_detect_65;
			count = ARRAY_SIZE(msp3400c_carrier_detect_65);
			break;
		case 0: /* 4.5 */
		case 2: /* 6.0 */
		default:
			cd = NULL;
			count = 0;
			break;
		}

		if (amsound && (state->norm == VIDEO_MODE_SECAM)) {
			/* autodetect doesn't work well with AM ... */
			cd = NULL;
			count = 0;
			max2 = 0;
		}
		for (this = 0; this < count; this++) {
			msp3400c_setcarrier(client, cd[this].cdo,cd[this].cdo);
			if (msp_sleep(state,100))
				goto restart;
			val = msp_read_dsp(client, 0x1b);
			if (val > 32767)
				val -= 65536;
			if (val2 < val)
				val2 = val, max2 = this;
			msp_dbg1("carrier2 val: %5d / %s\n", val,cd[this].name);
		}

		/* program the msp3400 according to the results */
		state->main   = msp3400c_carrier_detect_main[max1].cdo;
		switch (max1) {
		case 1: /* 5.5 */
			if (max2 == 0) {
				/* B/G FM-stereo */
				state->second = msp3400c_carrier_detect_55[max2].cdo;
				msp3400c_setmode(client, MSP_MODE_FM_TERRA);
				state->nicam_on = 0;
				msp3400c_setstereo(client, V4L2_TUNER_MODE_MONO);
				state->watch_stereo = 1;
			} else if (max2 == 1 && HAVE_NICAM(state)) {
				/* B/G NICAM */
				state->second = msp3400c_carrier_detect_55[max2].cdo;
				msp3400c_setmode(client, MSP_MODE_FM_NICAM1);
				state->nicam_on = 1;
				msp3400c_setcarrier(client, state->second, state->main);
				state->watch_stereo = 1;
			} else {
				goto no_second;
			}
			break;
		case 2: /* 6.0 */
			/* PAL I NICAM */
			state->second = MSP_CARRIER(6.552);
			msp3400c_setmode(client, MSP_MODE_FM_NICAM2);
			state->nicam_on = 1;
			msp3400c_setcarrier(client, state->second, state->main);
			state->watch_stereo = 1;
			break;
		case 3: /* 6.5 */
			if (max2 == 1 || max2 == 2) {
				/* D/K FM-stereo */
				state->second = msp3400c_carrier_detect_65[max2].cdo;
				msp3400c_setmode(client, MSP_MODE_FM_TERRA);
				state->nicam_on = 0;
				msp3400c_setstereo(client, V4L2_TUNER_MODE_MONO);
				state->watch_stereo = 1;
			} else if (max2 == 0 &&
				   state->norm == VIDEO_MODE_SECAM) {
				/* L NICAM or AM-mono */
				state->second = msp3400c_carrier_detect_65[max2].cdo;
				msp3400c_setmode(client, MSP_MODE_AM_NICAM);
				state->nicam_on = 0;
				msp3400c_setstereo(client, V4L2_TUNER_MODE_MONO);
				msp3400c_setcarrier(client, state->second, state->main);
				/* volume prescale for SCART (AM mono input) */
				msp_write_dsp(client, 0x000d, 0x1900);
				state->watch_stereo = 1;
			} else if (max2 == 0 && HAVE_NICAM(state)) {
				/* D/K NICAM */
				state->second = msp3400c_carrier_detect_65[max2].cdo;
				msp3400c_setmode(client, MSP_MODE_FM_NICAM1);
				state->nicam_on = 1;
				msp3400c_setcarrier(client, state->second, state->main);
				state->watch_stereo = 1;
			} else {
				goto no_second;
			}
			break;
		case 0: /* 4.5 */
		default:
		no_second:
			state->second = msp3400c_carrier_detect_main[max1].cdo;
			msp3400c_setmode(client, MSP_MODE_FM_TERRA);
			state->nicam_on = 0;
			msp3400c_setcarrier(client, state->second, state->main);
			state->rxsubchans = V4L2_TUNER_SUB_MONO;
			msp3400c_setstereo(client, V4L2_TUNER_MODE_MONO);
			break;
		}

		/* unmute */
		msp_set_audio(client);
		msp3400c_restore_dfp(client);

		if (debug)
			msp3400c_print_mode(client);

		/* monitor tv audio mode */
		while (state->watch_stereo) {
			if (msp_sleep(state,5000))
				goto restart;
			watch_stereo(client);
		}
	}
	msp_dbg1("thread: exit\n");
	return 0;
}

/* ----------------------------------------------------------------------- */
/* this one uses the automatic sound standard detection of newer           */
/* msp34xx chip versions                                                   */

static struct MODES {
	int retval;
	int main, second;
	char *name;
} modelist[] = {
	{ 0x0000, 0, 0, "could not detect sound standard" },
	{ 0x0001, 0, 0, "autodetect started" },
	{ 0x0002, MSP_CARRIER(4.5), MSP_CARRIER(4.72), "4.5/4.72  M Dual FM-Stereo" },
	{ 0x0003, MSP_CARRIER(5.5), MSP_CARRIER(5.7421875), "5.5/5.74  B/G Dual FM-Stereo" },
	{ 0x0004, MSP_CARRIER(6.5), MSP_CARRIER(6.2578125), "6.5/6.25  D/K1 Dual FM-Stereo" },
	{ 0x0005, MSP_CARRIER(6.5), MSP_CARRIER(6.7421875), "6.5/6.74  D/K2 Dual FM-Stereo" },
	{ 0x0006, MSP_CARRIER(6.5), MSP_CARRIER(6.5), "6.5  D/K FM-Mono (HDEV3)" },
	{ 0x0008, MSP_CARRIER(5.5), MSP_CARRIER(5.85), "5.5/5.85  B/G NICAM FM" },
	{ 0x0009, MSP_CARRIER(6.5), MSP_CARRIER(5.85), "6.5/5.85  L NICAM AM" },
	{ 0x000a, MSP_CARRIER(6.0), MSP_CARRIER(6.55), "6.0/6.55  I NICAM FM" },
	{ 0x000b, MSP_CARRIER(6.5), MSP_CARRIER(5.85), "6.5/5.85  D/K NICAM FM" },
	{ 0x000c, MSP_CARRIER(6.5), MSP_CARRIER(5.85), "6.5/5.85  D/K NICAM FM (HDEV2)" },
	{ 0x0020, MSP_CARRIER(4.5), MSP_CARRIER(4.5), "4.5  M BTSC-Stereo" },
	{ 0x0021, MSP_CARRIER(4.5), MSP_CARRIER(4.5), "4.5  M BTSC-Mono + SAP" },
	{ 0x0030, MSP_CARRIER(4.5), MSP_CARRIER(4.5), "4.5  M EIA-J Japan Stereo" },
	{ 0x0040, MSP_CARRIER(10.7), MSP_CARRIER(10.7), "10.7  FM-Stereo Radio" },
	{ 0x0050, MSP_CARRIER(6.5), MSP_CARRIER(6.5), "6.5  SAT-Mono" },
	{ 0x0051, MSP_CARRIER(7.02), MSP_CARRIER(7.20), "7.02/7.20  SAT-Stereo" },
	{ 0x0060, MSP_CARRIER(7.2), MSP_CARRIER(7.2), "7.2  SAT ADR" },
	{     -1, 0, 0, NULL }, /* EOF */
};

static inline const char *msp_standard_mode_name(int mode)
{
	int i;
	for (i = 0; modelist[i].name != NULL; i++)
		if (modelist[i].retval == mode)
			return modelist[i].name;
	return "unknown";
}

static int msp_modus(struct i2c_client *client, int norm)
{
	switch (norm) {
	case VIDEO_MODE_PAL:
		msp_dbg1("video mode selected to PAL\n");

#if 1
		/* experimental: not sure this works with all chip versions */
		return 0x7003;
#else
		/* previous value, try this if it breaks ... */
		return 0x1003;
#endif
	case VIDEO_MODE_NTSC:  /* BTSC */
		msp_dbg1("video mode selected to NTSC\n");
		return 0x2003;
	case VIDEO_MODE_SECAM:
		msp_dbg1("video mode selected to SECAM\n");
		return 0x0003;
	case VIDEO_MODE_RADIO:
		msp_dbg1("video mode selected to Radio\n");
		return 0x0003;
	case VIDEO_MODE_AUTO:
		msp_dbg1("video mode selected to Auto\n");
		return 0x2003;
	default:
		return 0x0003;
	}
}

static int msp_standard(int norm)
{
	switch (norm) {
	case VIDEO_MODE_PAL:
		return 1;
	case VIDEO_MODE_NTSC:  /* BTSC */
		return 0x0020;
	case VIDEO_MODE_SECAM:
		return 1;
	case VIDEO_MODE_RADIO:
		return 0x0040;
	default:
		return 1;
	}
}

static int msp3410d_thread(void *data)
{
	struct i2c_client *client = data;
	struct msp_state *state = i2c_get_clientdata(client);
	int mode,val,i,std;

	msp_info("msp3410 daemon started\n");

	for (;;) {
		msp_dbg2("msp3410 thread: sleep\n");
		msp_sleep(state,-1);
		msp_dbg2("msp3410 thread: wakeup\n");

	restart:
		msp_dbg1("thread: restart scan\n");
		state->restart = 0;
		if (kthread_should_stop())
			break;

		if (state->mode == MSP_MODE_EXTERN) {
			/* no carrier scan needed, just unmute */
			msp_dbg1("thread: no carrier scan\n");
			msp_set_audio(client);
			continue;
		}

		/* put into sane state (and mute) */
		msp_reset(client);

		/* some time for the tuner to sync */
		if (msp_sleep(state,200))
			goto restart;

		/* start autodetect */
		mode = msp_modus(client, state->norm);
		std  = msp_standard(state->norm);
		msp_write_dem(client, 0x30, mode);
		msp_write_dem(client, 0x20, std);
		state->watch_stereo = 0;

		if (debug)
			msp_dbg1("setting mode: %s (0x%04x)\n",
			       msp_standard_mode_name(std) ,std);

		if (std != 1) {
			/* programmed some specific mode */
			val = std;
		} else {
			/* triggered autodetect */
			for (;;) {
				if (msp_sleep(state,100))
					goto restart;

				/* check results */
				val = msp_read_dem(client, 0x7e);
				if (val < 0x07ff)
					break;
				msp_dbg1("detection still in progress\n");
			}
		}
		for (i = 0; modelist[i].name != NULL; i++)
			if (modelist[i].retval == val)
				break;
		msp_dbg1("current mode: %s (0x%04x)\n",
			modelist[i].name ? modelist[i].name : "unknown",
			val);
		state->main   = modelist[i].main;
		state->second = modelist[i].second;

		if (amsound && (state->norm == VIDEO_MODE_SECAM) && (val != 0x0009)) {
			/* autodetection has failed, let backup */
			msp_dbg1("autodetection failed,"
				" switching to backup mode: %s (0x%04x)\n",
				modelist[8].name ? modelist[8].name : "unknown",val);
			val = 0x0009;
			msp_write_dem(client, 0x20, val);
		}

		/* set various prescales */
		msp_write_dsp(client, 0x0d, 0x1900); /* scart */
		msp_write_dsp(client, 0x0e, 0x2403); /* FM */
		msp_write_dsp(client, 0x10, 0x5a00); /* nicam */

		/* set stereo */
		switch (val) {
		case 0x0008: /* B/G NICAM */
		case 0x000a: /* I NICAM */
			if (val == 0x0008)
				state->mode = MSP_MODE_FM_NICAM1;
			else
				state->mode = MSP_MODE_FM_NICAM2;
			/* just turn on stereo */
			state->rxsubchans = V4L2_TUNER_SUB_STEREO;
			state->nicam_on = 1;
			state->watch_stereo = 1;
			msp3400c_setstereo(client,V4L2_TUNER_MODE_STEREO);
			break;
		case 0x0009:
			state->mode = MSP_MODE_AM_NICAM;
			state->rxsubchans = V4L2_TUNER_SUB_MONO;
			state->nicam_on = 1;
			msp3400c_setstereo(client,V4L2_TUNER_MODE_MONO);
			state->watch_stereo = 1;
			break;
		case 0x0020: /* BTSC */
			/* just turn on stereo */
			state->mode = MSP_MODE_BTSC;
			state->rxsubchans = V4L2_TUNER_SUB_STEREO;
			state->nicam_on = 0;
			state->watch_stereo = 1;
			msp3400c_setstereo(client,V4L2_TUNER_MODE_STEREO);
			break;
		case 0x0040: /* FM radio */
			state->mode   = MSP_MODE_FM_RADIO;
			state->rxsubchans = V4L2_TUNER_SUB_STEREO;
			state->audmode = V4L2_TUNER_MODE_STEREO;
			state->nicam_on = 0;
			state->watch_stereo = 0;
			/* not needed in theory if HAVE_RADIO(), but
			   short programming enables carrier mute */
			msp3400c_setmode(client,MSP_MODE_FM_RADIO);
			msp3400c_setcarrier(client, MSP_CARRIER(10.7),
					    MSP_CARRIER(10.7));
			/* scart routing */
			msp_set_scart(client,SCART_IN2,0);
			/* msp34xx does radio decoding */
			msp_write_dsp(client, 0x08, 0x0020);
			msp_write_dsp(client, 0x09, 0x0020);
			msp_write_dsp(client, 0x0b, 0x0020);
			break;
		case 0x0003:
		case 0x0004:
		case 0x0005:
			state->mode   = MSP_MODE_FM_TERRA;
			state->rxsubchans = V4L2_TUNER_SUB_MONO;
			state->audmode = V4L2_TUNER_MODE_MONO;
			state->nicam_on = 0;
			state->watch_stereo = 1;
			break;
		}

		/* unmute, restore misc registers */
		msp_set_audio(client);
		msp_write_dsp(client, 0x13, state->acb);
		msp_write_dem(client, 0x40, state->i2s_mode);
		msp3400c_restore_dfp(client);

		/* monitor tv audio mode */
		while (state->watch_stereo) {
			if (msp_sleep(state,5000))
				goto restart;
			watch_stereo(client);
		}
	}
	msp_dbg1("thread: exit\n");
	return 0;
}

/* ----------------------------------------------------------------------- */
/* msp34xxG + (autoselect no-thread)                                          */
/* this one uses both automatic standard detection and automatic sound     */
/* select which are available in the newer G versions                      */
/* struct msp: only norm, acb and source are really used in this mode      */

/* set the same 'source' for the loudspeaker, scart and quasi-peak detector
 * the value for source is the same as bit 15:8 of DFP registers 0x08,
 * 0x0a and 0x0c: 0=mono, 1=stereo or A|B, 2=SCART, 3=stereo or A, 4=stereo or B
 *
 * this function replaces msp3400c_setstereo
 */
static void msp34xxg_set_source(struct i2c_client *client, int source)
{
	struct msp_state *state = i2c_get_clientdata(client);

	/* fix matrix mode to stereo and let the msp choose what
	 * to output according to 'source', as recommended
	 * for MONO (source==0) downmixing set bit[7:0] to 0x30
	 */
	int value = (source & 0x07) << 8 | (source == 0 ? 0x30 : 0x20);

	msp_dbg1("set source to %d (0x%x)\n", source, value);
	/* Loudspeaker Output */
	msp_write_dsp(client, 0x08, value);
	/* SCART1 DA Output */
	msp_write_dsp(client, 0x0a, value);
	/* Quasi-peak detector */
	msp_write_dsp(client, 0x0c, value);
	/*
	 * set identification threshold. Personally, I
	 * I set it to a higher value that the default
	 * of 0x190 to ignore noisy stereo signals.
	 * this needs tuning. (recommended range 0x00a0-0x03c0)
	 * 0x7f0 = forced mono mode
	 */
	/* a2 threshold for stereo/bilingual */
	msp_write_dem(client, 0x22, stereo_threshold);
	state->source = source;
}

/* (re-)initialize the msp34xxg, according to the current norm in state->norm
 * return 0 if it worked, -1 if it failed
 */
static int msp34xxg_reset(struct i2c_client *client)
{
	struct msp_state *state = i2c_get_clientdata(client);
	int modus, std;

	if (msp_reset(client))
		return -1;

	/* make sure that input/output is muted (paranoid mode) */
	/* ACB, mute DSP input, mute SCART 1 */
	if (msp_write_dsp(client, 0x13, 0x0f20))
		return -1;

	msp_write_dem(client, 0x40, state->i2s_mode);

	/* step-by-step initialisation, as described in the manual */
	modus = msp_modus(client, state->norm);
	std   = msp_standard(state->norm);
	modus &= ~0x03; /* STATUS_CHANGE = 0 */
	modus |= 0x01;  /* AUTOMATIC_SOUND_DETECTION = 1 */
	if (msp_write_dem(client, 0x30, modus))
		return -1;
	if (msp_write_dem(client, 0x20, std))
		return -1;

	/* write the dfps that may have an influence on
	   standard/audio autodetection right now */
	msp34xxg_set_source(client, state->source);

	/* AM/FM Prescale, default: [15:8] 75khz deviation */
	if (msp34xxg_write_dfp_with_default(client, 0x0e, 0x3000))
		return -1;

	/* NICAM Prescale, default: 9db gain (as recommended) */
	if (msp34xxg_write_dfp_with_default(client, 0x10, 0x5a00))
		return -1;

	return 0;
}

static int msp34xxg_thread(void *data)
{
	struct i2c_client *client = data;
	struct msp_state *state = i2c_get_clientdata(client);
	int val, std, i;

	msp_info("msp34xxg daemon started\n");

	state->source = 1; /* default */
	for (;;) {
		msp_dbg2("msp34xxg thread: sleep\n");
		msp_sleep(state, -1);
		msp_dbg2("msp34xxg thread: wakeup\n");

	restart:
		msp_dbg1("thread: restart scan\n");
		state->restart = 0;
		if (kthread_should_stop())
			break;

		/* setup the chip*/
		msp34xxg_reset(client);
		std = standard;
		if (std != 0x01)
			goto unmute;

		/* watch autodetect */
		msp_dbg1("triggered autodetect, waiting for result\n");
		for (i = 0; i < 10; i++) {
			if (msp_sleep(state, 100))
				goto restart;

			/* check results */
			val = msp_read_dem(client, 0x7e);
			if (val < 0x07ff) {
				std = val;
				break;
			}
			msp_dbg1("detection still in progress\n");
		}
		if (std == 1) {
			msp_dbg1("detection still in progress after 10 tries. giving up.\n");
			continue;
		}

	unmute:
		state->mode = std;
		msp_dbg1("current mode: %s (0x%04x)\n",
			msp_standard_mode_name(std), std);

		/* unmute: dispatch sound to scart output, set scart volume */
		msp_set_audio(client);

		/* restore ACB */
		if (msp_write_dsp(client, 0x13, state->acb))
			return -1;

		msp_write_dem(client, 0x40, state->i2s_mode);
	}
	msp_dbg1("thread: exit\n");
	return 0;
}

static void msp34xxg_detect_stereo(struct i2c_client *client)
{
	struct msp_state *state = i2c_get_clientdata(client);

	int status = msp_read_dem(client, 0x0200);
	int is_bilingual = status & 0x100;
	int is_stereo = status & 0x40;

	state->rxsubchans = 0;
	if (is_stereo)
		state->rxsubchans |= V4L2_TUNER_SUB_STEREO;
	else
		state->rxsubchans |= V4L2_TUNER_SUB_MONO;
	if (is_bilingual) {
		state->rxsubchans |= V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
		/* I'm supposed to check whether it's SAP or not
		 * and set only LANG2/SAP in this case. Yet, the MSP
		 * does a lot of work to hide this and handle everything
		 * the same way. I don't want to work around it so unless
		 * this is a problem, I'll handle SAP just like lang1/lang2.
		 */
	}
	msp_dbg1("status=0x%x, stereo=%d, bilingual=%d -> rxsubchans=%d\n",
		status, is_stereo, is_bilingual, state->rxsubchans);
}

static void msp34xxg_set_audmode(struct i2c_client *client, int audmode)
{
	struct msp_state *state = i2c_get_clientdata(client);
	int source;

	switch (audmode) {
	case V4L2_TUNER_MODE_MONO:
		source = 0; /* mono only */
		break;
	case V4L2_TUNER_MODE_STEREO:
		source = 1; /* stereo or A|B, see comment in msp34xxg_get_v4l2_stereo() */
		/* problem: that could also mean 2 (scart input) */
		break;
	case V4L2_TUNER_MODE_LANG1:
		source = 3; /* stereo or A */
		break;
	case V4L2_TUNER_MODE_LANG2:
		source = 4; /* stereo or B */
		break;
	default:
		audmode = 0;
		source  = 1;
		break;
	}
	state->audmode = audmode;
	msp34xxg_set_source(client, source);
}


/* ----------------------------------------------------------------------- */

static void msp_wake_thread(struct i2c_client *client)
{
	struct msp_state *state = i2c_get_clientdata(client);

	if (NULL == state->kthread)
		return;
	msp_set_mute(client);
	state->watch_stereo = 0;
	state->restart = 1;
	wake_up_interruptible(&state->wq);
}

/* ----------------------------------------------------------------------- */

static int mode_v4l2_to_v4l1(int rxsubchans)
{
	int mode = 0;

	if (rxsubchans & V4L2_TUNER_SUB_STEREO)
		mode |= VIDEO_SOUND_STEREO;
	if (rxsubchans & V4L2_TUNER_SUB_LANG2)
		mode |= VIDEO_SOUND_LANG2;
	if (rxsubchans & V4L2_TUNER_SUB_LANG1)
		mode |= VIDEO_SOUND_LANG1;
	if (mode == 0)
		mode |= VIDEO_SOUND_MONO;
	return mode;
}

static int mode_v4l1_to_v4l2(int mode)
{
	if (mode & VIDEO_SOUND_STEREO)
		return V4L2_TUNER_MODE_STEREO;
	if (mode & VIDEO_SOUND_LANG2)
		return V4L2_TUNER_MODE_LANG2;
	if (mode & VIDEO_SOUND_LANG1)
		return V4L2_TUNER_MODE_LANG1;
	return V4L2_TUNER_MODE_MONO;
}

static void msp_any_detect_stereo(struct i2c_client *client)
{
	struct msp_state *state  = i2c_get_clientdata(client);

	switch (state->opmode) {
	case OPMODE_MANUAL:
	case OPMODE_AUTODETECT:
		autodetect_stereo(client);
		break;
	case OPMODE_AUTOSELECT:
		msp34xxg_detect_stereo(client);
		break;
	}
}

static struct v4l2_queryctrl msp_qctrl[] = {
	{
		.id            = V4L2_CID_AUDIO_VOLUME,
		.name          = "Volume",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 58880,
		.flags         = 0,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_AUDIO_MUTE,
		.name          = "Mute",
		.minimum       = 0,
		.maximum       = 1,
		.step          = 1,
		.default_value = 1,
		.flags         = 0,
		.type          = V4L2_CTRL_TYPE_BOOLEAN,
	},{
		.id            = V4L2_CID_AUDIO_BASS,
		.name          = "Bass",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 32768,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},{
		.id            = V4L2_CID_AUDIO_TREBLE,
		.name          = "Treble",
		.minimum       = 0,
		.maximum       = 65535,
		.step          = 65535/100,
		.default_value = 32768,
		.type          = V4L2_CTRL_TYPE_INTEGER,
	},
};


static void msp_any_set_audmode(struct i2c_client *client, int audmode)
{
	struct msp_state *state = i2c_get_clientdata(client);

	switch (state->opmode) {
	case OPMODE_MANUAL:
	case OPMODE_AUTODETECT:
		state->watch_stereo = 0;
		msp3400c_setstereo(client, audmode);
		break;
	case OPMODE_AUTOSELECT:
		msp34xxg_set_audmode(client, audmode);
		break;
	}
}

static int msp_get_ctrl(struct i2c_client *client, struct v4l2_control *ctrl)
{
	struct msp_state *state = i2c_get_clientdata(client);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		ctrl->value = state->muted;
		break;

	case V4L2_CID_AUDIO_BALANCE:
		ctrl->value = state->balance;
		break;

	case V4L2_CID_AUDIO_BASS:
		ctrl->value = state->bass;
		break;

	case V4L2_CID_AUDIO_TREBLE:
		ctrl->value = state->treble;
		break;

	case V4L2_CID_AUDIO_VOLUME:
		ctrl->value = state->volume;
		break;

	default:
		return -EINVAL;
	}
	return 0;
}

static int msp_set_ctrl(struct i2c_client *client, struct v4l2_control *ctrl)
{
	struct msp_state *state = i2c_get_clientdata(client);

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		if (ctrl->value < 0 || ctrl->value >= 2)
			return -ERANGE;
		state->muted = ctrl->value;
		break;

	case V4L2_CID_AUDIO_BASS:
		state->bass = ctrl->value;
		break;

	case V4L2_CID_AUDIO_TREBLE:
		state->treble = ctrl->value;
		break;

	case V4L2_CID_AUDIO_BALANCE:
		state->balance = ctrl->value;
		break;

	case V4L2_CID_AUDIO_VOLUME:
		state->volume = ctrl->value;
		if (state->volume == 0)
			state->balance = 32768;
		break;

	default:
		return -EINVAL;
	}
	msp_set_audio(client);
	return 0;
}

static int msp_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct msp_state *state = i2c_get_clientdata(client);
	u16 *sarg = arg;
	int scart = 0;

	if (debug >= 2)
		v4l_i2c_print_ioctl(client, cmd);

	switch (cmd) {
	case AUDC_SET_INPUT:
		if (*sarg == state->input)
			break;
		state->input = *sarg;
		switch (*sarg) {
		case AUDIO_RADIO:
			/* Hauppauge uses IN2 for the radio */
			state->mode = MSP_MODE_FM_RADIO;
			scart       = SCART_IN2;
			break;
		case AUDIO_EXTERN_1:
			/* IN1 is often used for external input ... */
			state->mode = MSP_MODE_EXTERN;
			scart       = SCART_IN1;
			break;
		case AUDIO_EXTERN_2:
			/* ... sometimes it is IN2 through ;) */
			state->mode = MSP_MODE_EXTERN;
			scart       = SCART_IN2;
			break;
		case AUDIO_TUNER:
			state->mode = -1;
			break;
		default:
			if (*sarg & AUDIO_MUTE)
				msp_set_scart(client, SCART_MUTE, 0);
			break;
		}
		if (scart) {
			state->rxsubchans = V4L2_TUNER_SUB_STEREO;
			state->audmode = V4L2_TUNER_MODE_STEREO;
			msp_set_scart(client, scart, 0);
			msp_write_dsp(client, 0x000d, 0x1900);
			if (state->opmode != OPMODE_AUTOSELECT)
				msp3400c_setstereo(client, state->audmode);
		}
		msp_wake_thread(client);
		break;

	case AUDC_SET_RADIO:
		state->norm = VIDEO_MODE_RADIO;
		msp_dbg1("switching to radio mode\n");
		state->watch_stereo = 0;
		switch (state->opmode) {
		case OPMODE_MANUAL:
			/* set msp3400 to FM radio mode */
			msp3400c_setmode(client, MSP_MODE_FM_RADIO);
			msp3400c_setcarrier(client, MSP_CARRIER(10.7),
					    MSP_CARRIER(10.7));
			msp_set_audio(client);
			break;
		case OPMODE_AUTODETECT:
		case OPMODE_AUTOSELECT:
			/* the thread will do for us */
			msp_wake_thread(client);
			break;
		}
		break;
		/* work-in-progress:  hook to control the DFP registers */
	case MSP_SET_DFPREG:
	{
		struct msp_dfpreg *r = arg;
		int i;

		if (r->reg < 0 || r->reg >= DFP_COUNT)
			return -EINVAL;
		for (i = 0; i < sizeof(bl_dfp) / sizeof(int); i++)
			if (r->reg == bl_dfp[i])
				return -EINVAL;
		state->dfp_regs[r->reg] = r->value;
		msp_write_dsp(client, r->reg, r->value);
		return 0;
	}

	case MSP_GET_DFPREG:
	{
		struct msp_dfpreg *r = arg;

		if (r->reg < 0 || r->reg >= DFP_COUNT)
			return -EINVAL;
		r->value = msp_read_dsp(client, r->reg);
		return 0;
	}

	/* --- v4l ioctls --- */
	/* take care: bttv does userspace copying, we'll get a
	   kernel pointer here... */
	case VIDIOCGAUDIO:
	{
		struct video_audio *va = arg;

		va->flags |= VIDEO_AUDIO_VOLUME |
			VIDEO_AUDIO_BASS |
			VIDEO_AUDIO_TREBLE |
			VIDEO_AUDIO_MUTABLE;
		if (state->muted)
			va->flags |= VIDEO_AUDIO_MUTE;

		if (state->muted)
			va->flags |= VIDEO_AUDIO_MUTE;
		va->volume = state->volume;
		va->balance = state->volume ? state->balance : 32768;
		va->bass = state->bass;
		va->treble = state->treble;

		msp_any_detect_stereo(client);
		va->mode = mode_v4l2_to_v4l1(state->rxsubchans);
		break;
	}

	case VIDIOCSAUDIO:
	{
		struct video_audio *va = arg;

		state->muted = (va->flags & VIDEO_AUDIO_MUTE);
		state->volume = va->volume;
		state->balance = va->balance;
		state->bass = va->bass;
		state->treble = va->treble;
		msp_set_audio(client);

		if (va->mode != 0 && state->norm != VIDEO_MODE_RADIO)
			msp_any_set_audmode(client,mode_v4l1_to_v4l2(va->mode));
		break;
	}

	case VIDIOCSCHAN:
	{
		struct video_channel *vc = arg;

		state->norm = vc->norm;
		msp_wake_thread(client);
		break;
	}

	case VIDIOCSFREQ:
	case VIDIOC_S_FREQUENCY:
	{
		/* new channel -- kick audio carrier scan */
		msp_wake_thread(client);
		break;
	}

	/* msp34xx specific */
	case MSP_SET_MATRIX:
	{
		struct msp_matrix *mspm = arg;

		msp_set_scart(client, mspm->input, mspm->output);
		break;
	}

	/* --- v4l2 ioctls --- */
	case VIDIOC_S_STD:
	{
		v4l2_std_id *id = arg;

		/*FIXME: use V4L2 mode flags on msp3400 instead of V4L1*/
		if (*id & V4L2_STD_PAL) {
			state->norm = VIDEO_MODE_PAL;
		} else if (*id & V4L2_STD_SECAM) {
			state->norm = VIDEO_MODE_SECAM;
		} else {
			state->norm = VIDEO_MODE_NTSC;
		}

		msp_wake_thread(client);
		return 0;
	}

	case VIDIOC_ENUMINPUT:
	{
		struct v4l2_input *i = arg;

		if (i->index != 0)
			return -EINVAL;

		i->type = V4L2_INPUT_TYPE_TUNER;
		switch (i->index) {
		case AUDIO_RADIO:
			strcpy(i->name, "Radio");
			break;
		case AUDIO_EXTERN_1:
			strcpy(i->name, "Extern 1");
			break;
		case AUDIO_EXTERN_2:
			strcpy(i->name, "Extern 2");
			break;
		case AUDIO_TUNER:
			strcpy(i->name, "Television");
			break;
		default:
			return -EINVAL;
		}
		return 0;
	}

	case VIDIOC_G_AUDIO:
	{
		struct v4l2_audio *a = arg;

		memset(a, 0, sizeof(*a));

		switch (a->index) {
		case AUDIO_RADIO:
			strcpy(a->name, "Radio");
			break;
		case AUDIO_EXTERN_1:
			strcpy(a->name, "Extern 1");
			break;
		case AUDIO_EXTERN_2:
			strcpy(a->name, "Extern 2");
			break;
		case AUDIO_TUNER:
			strcpy(a->name, "Television");
			break;
		default:
			return -EINVAL;
		}

		msp_any_detect_stereo(client);
		if (state->audmode == V4L2_TUNER_MODE_STEREO) {
			a->capability = V4L2_AUDCAP_STEREO;
		}

		break;
	}

	case VIDIOC_S_AUDIO:
	{
		struct v4l2_audio *sarg = arg;

		switch (sarg->index) {
		case AUDIO_RADIO:
			/* Hauppauge uses IN2 for the radio */
			state->mode = MSP_MODE_FM_RADIO;
			scart       = SCART_IN2;
			break;
		case AUDIO_EXTERN_1:
			/* IN1 is often used for external input ... */
			state->mode = MSP_MODE_EXTERN;
			scart       = SCART_IN1;
			break;
		case AUDIO_EXTERN_2:
			/* ... sometimes it is IN2 through ;) */
			state->mode = MSP_MODE_EXTERN;
			scart       = SCART_IN2;
			break;
		case AUDIO_TUNER:
			state->mode = -1;
			break;
		}
		if (scart) {
			state->rxsubchans = V4L2_TUNER_SUB_STEREO;
			state->audmode = V4L2_TUNER_MODE_STEREO;
			msp_set_scart(client, scart, 0);
			msp_write_dsp(client, 0x000d, 0x1900);
		}
		if (sarg->capability == V4L2_AUDCAP_STEREO) {
			state->audmode = V4L2_TUNER_MODE_STEREO;
		} else {
			state->audmode &= ~V4L2_TUNER_MODE_STEREO;
		}
		msp_any_set_audmode(client, state->audmode);
		msp_wake_thread(client);
		break;
	}

	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *vt = arg;

		msp_any_detect_stereo(client);
		vt->audmode    = state->audmode;
		vt->rxsubchans = state->rxsubchans;
		vt->capability = V4L2_TUNER_CAP_STEREO |
			V4L2_TUNER_CAP_LANG1 | V4L2_TUNER_CAP_LANG2;
		break;
	}

	case VIDIOC_S_TUNER:
	{
		struct v4l2_tuner *vt = (struct v4l2_tuner *)arg;

		/* only set audmode */
		if (vt->audmode != -1 && vt->audmode != 0)
			msp_any_set_audmode(client, vt->audmode);
		break;
	}

	case VIDIOC_G_AUDOUT:
	{
		struct v4l2_audioout *a = (struct v4l2_audioout *)arg;
		int idx = a->index;

		memset(a, 0, sizeof(*a));

		switch (idx) {
		case 0:
			strcpy(a->name, "Scart1 Out");
			break;
		case 1:
			strcpy(a->name, "Scart2 Out");
			break;
		case 2:
			strcpy(a->name, "I2S Out");
			break;
		default:
			return -EINVAL;
		}
		break;

	}

	case VIDIOC_S_AUDOUT:
	{
		struct v4l2_audioout *a = (struct v4l2_audioout *)arg;

		if (a->index < 0 || a->index > 2)
			return -EINVAL;

		msp_dbg1("Setting audio out on msp34xx to input %i\n", a->index);
		msp_set_scart(client, state->in_scart, a->index + 1);

		break;
	}

	case VIDIOC_INT_I2S_CLOCK_FREQ:
	{
		u32 *a = (u32 *)arg;

		msp_dbg1("Setting I2S speed to %d\n", *a);

		switch (*a) {
			case 1024000:
				state->i2s_mode = 0;
				break;
			case 2048000:
				state->i2s_mode = 1;
				break;
			default:
				return -EINVAL;
		}
		break;
	}

	case VIDIOC_QUERYCTRL:
	{
		struct v4l2_queryctrl *qc = arg;
		int i;

		for (i = 0; i < ARRAY_SIZE(msp_qctrl); i++)
			if (qc->id && qc->id == msp_qctrl[i].id) {
				memcpy(qc, &msp_qctrl[i], sizeof(*qc));
				return 0;
			}
		return -EINVAL;
	}

	case VIDIOC_G_CTRL:
		return msp_get_ctrl(client, arg);

	case VIDIOC_S_CTRL:
		return msp_set_ctrl(client, arg);

	case VIDIOC_LOG_STATUS:
		msp_any_detect_stereo(client);
		msp_info("%s rev1 = 0x%04x rev2 = 0x%04x\n",
				client->name, state->rev1, state->rev2);
		msp_info("Audio:  volume %d balance %d bass %d treble %d%s\n",
				state->volume, state->balance,
				state->bass, state->treble,
				state->muted ? " (muted)" : "");
		msp_info("Mode:   %s (%s%s)\n", msp_standard_mode_name(state->mode),
			(state->rxsubchans & V4L2_TUNER_SUB_STEREO) ? "stereo" : "mono",
			(state->rxsubchans & V4L2_TUNER_SUB_LANG2) ? ", dual" : "");
		msp_info("ACB:    0x%04x\n", state->acb);
		break;

	default:
		/* nothing */
		break;
	}
	return 0;
}

static int msp_suspend(struct device * dev, pm_message_t state)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	msp_dbg1("suspend\n");
	msp_reset(client);
	return 0;
}

static int msp_resume(struct device * dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	msp_dbg1("resume\n");
	msp_wake_thread(client);
	return 0;
}

/* ----------------------------------------------------------------------- */

static struct i2c_driver i2c_driver;

static int msp_attach(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	struct msp_state *state;
	int (*thread_func)(void *data) = NULL;
	int i;

	client = kmalloc(sizeof(*client), GFP_KERNEL);
	if (client == NULL)
		return -ENOMEM;
	memset(client, 0, sizeof(*client));
	client->addr = address;
	client->adapter = adapter;
	client->driver = &i2c_driver;
	client->flags = I2C_CLIENT_ALLOW_USE;
	snprintf(client->name, sizeof(client->name) - 1, "msp3400");

	if (msp_reset(client) == -1) {
		msp_dbg1("msp3400 not found\n");
		kfree(client);
		return -1;
	}

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (state == NULL) {
		kfree(client);
		return -ENOMEM;
	}
	i2c_set_clientdata(client, state);

	memset(state, 0, sizeof(*state));
	state->norm = VIDEO_MODE_NTSC;
	state->volume = 58880;	/* 0db gain */
	state->balance = 32768;	/* 0db gain */
	state->bass = 32768;
	state->treble = 32768;
	state->input = -1;
	state->muted = 0;
	state->i2s_mode = 0;
	for (i = 0; i < DFP_COUNT; i++)
		state->dfp_regs[i] = -1;
	init_waitqueue_head(&state->wq);

	state->rev1 = msp_read_dsp(client, 0x1e);
	if (state->rev1 != -1)
		state->rev2 = msp_read_dsp(client, 0x1f);
	msp_dbg1("rev1=0x%04x, rev2=0x%04x\n", state->rev1, state->rev2);
	if (state->rev1 == -1 || (state->rev1 == 0 && state->rev2 == 0)) {
		msp_dbg1("error while reading chip version\n");
		kfree(state);
		kfree(client);
		return -1;
	}

	msp_set_audio(client);

	snprintf(client->name, sizeof(client->name), "MSP%c4%02d%c-%c%d",
		 ((state->rev1 >> 4) & 0x0f) + '3',
		 (state->rev2 >> 8) & 0xff,
		 (state->rev1 & 0x0f) + '@',
		 ((state->rev1 >> 8) & 0xff) + '@',
		 state->rev2 & 0x1f);

	state->opmode = opmode;
	if (state->opmode == OPMODE_AUTO) {
		/* MSP revision G and up have both autodetect and autoselect */
		if ((state->rev1 & 0x0f) >= 'G'-'@')
			state->opmode = OPMODE_AUTOSELECT;
		/* MSP revision D and up have autodetect */
		else if ((state->rev1 & 0x0f) >= 'D'-'@')
			state->opmode = OPMODE_AUTODETECT;
		else
			state->opmode = OPMODE_MANUAL;
	}

	/* hello world :-) */
	msp_info("%s found @ 0x%x (%s)\n", client->name, address << 1, adapter->name);
	msp_info("%s ", client->name);
	if (HAVE_NICAM(state) && HAVE_RADIO(state))
		printk("supports nicam and radio, ");
	else if (HAVE_NICAM(state))
		printk("supports nicam, ");
	else if (HAVE_RADIO(state))
		printk("supports radio, ");
	printk("mode is ");

	/* version-specific initialization */
	switch (state->opmode) {
	case OPMODE_MANUAL:
		printk("manual");
		thread_func = msp3400c_thread;
		break;
	case OPMODE_AUTODETECT:
		printk("autodetect");
		thread_func = msp3410d_thread;
		break;
	case OPMODE_AUTOSELECT:
		printk("autodetect and autoselect");
		thread_func = msp34xxg_thread;
		break;
	}
	printk("\n");

	/* startup control thread if needed */
	if (thread_func) {
		state->kthread = kthread_run(thread_func, client, "msp34xx");

		if (state->kthread == NULL)
			msp_warn("kernel_thread() failed\n");
		msp_wake_thread(client);
	}

	/* done */
	i2c_attach_client(client);

	return 0;
}

static int msp_probe(struct i2c_adapter *adapter)
{
	if (adapter->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adapter, &addr_data, msp_attach);
	return 0;
}

static int msp_detach(struct i2c_client *client)
{
	struct msp_state *state = i2c_get_clientdata(client);
	int err;

	/* shutdown control thread */
	if (state->kthread) {
		state->restart = 1;
		kthread_stop(state->kthread);
	}
	msp_reset(client);

	err = i2c_detach_client(client);
	if (err) {
		return err;
	}

	kfree(state);
	kfree(client);
	return 0;
}

/* ----------------------------------------------------------------------- */

/* i2c implementation */
static struct i2c_driver i2c_driver = {
	.name           = "msp3400",
	.id             = I2C_DRIVERID_MSP3400,
	.flags          = I2C_DF_NOTIFY,
	.attach_adapter = msp_probe,
	.detach_client  = msp_detach,
	.command        = msp_command,
	.driver = {
		.suspend = msp_suspend,
		.resume  = msp_resume,
	},
	.owner          = THIS_MODULE,
};

static int __init msp3400_init_module(void)
{
	return i2c_add_driver(&i2c_driver);
}

static void __exit msp3400_cleanup_module(void)
{
	i2c_del_driver(&i2c_driver);
}

module_init(msp3400_init_module);
module_exit(msp3400_cleanup_module);

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
