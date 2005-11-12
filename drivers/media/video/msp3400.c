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
#include <linux/videodev.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/kthread.h>
#include <linux/suspend.h>
#include <asm/semaphore.h>
#include <asm/pgtable.h>

#include <media/audiochip.h>
#include "msp3400.h"

#define msp3400_dbg(fmt, arg...) \
	do { \
		if (debug) \
			printk(KERN_INFO "%s debug %d-%04x: " fmt, client->driver->name, \
			       i2c_adapter_id(client->adapter), client->addr , ## arg); \
	} while (0)

/* Medium volume debug. */
#define msp3400_dbg_mediumvol(fmt, arg...) \
	do { \
		if (debug >= 2) \
			printk(KERN_INFO "%s debug %d-%04x: " fmt, client->driver->name, \
				i2c_adapter_id(client->adapter), client->addr , ## arg); \
	} while (0)

/* High volume debug. Use with care. */
#define msp3400_dbg_highvol(fmt, arg...) \
	do { \
		if (debug >= 16) \
			printk(KERN_INFO "%s debug %d-%04x: " fmt, client->driver->name, \
				i2c_adapter_id(client->adapter), client->addr , ## arg); \
	} while (0)

#define msp3400_err(fmt, arg...) do { \
	printk(KERN_ERR "%s %d-%04x: " fmt, client->driver->name, \
		i2c_adapter_id(client->adapter), client->addr , ## arg); } while (0)
#define msp3400_warn(fmt, arg...) do { \
	printk(KERN_WARNING "%s %d-%04x: " fmt, client->driver->name, \
		i2c_adapter_id(client->adapter), client->addr , ## arg); } while (0)
#define msp3400_info(fmt, arg...) do { \
	printk(KERN_INFO "%s %d-%04x: " fmt, client->driver->name, \
		i2c_adapter_id(client->adapter), client->addr , ## arg); } while (0)

#define OPMODE_AUTO    -1
#define OPMODE_MANUAL   0
#define OPMODE_SIMPLE   1   /* use short programming (>= msp3410 only) */
#define OPMODE_SIMPLER  2   /* use shorter programming (>= msp34xxG)   */

/* insmod parameters */
static int opmode   = OPMODE_AUTO;
static int debug    = 0;    /* debug output */
static int once     = 0;    /* no continous stereo monitoring */
static int amsound  = 0;    /* hard-wire AM sound at 6.5 Hz (france),
			       the autoscan seems work well only with FM... */
static int standard = 1;    /* Override auto detect of audio standard, if needed. */
static int dolby    = 0;

static int stereo_threshold = 0x190; /* a2 threshold for stereo/bilingual
					(msp34xxg only) 0x00a0-0x03c0 */
#define DFP_COUNT 0x41
static const int bl_dfp[] = {
	0x00, 0x01, 0x02, 0x03, 0x06, 0x08, 0x09, 0x0a,
	0x0b, 0x0d, 0x0e, 0x10
};

#define IS_MSP34XX_G(msp) ((msp)->opmode==2)

struct msp3400c {
	int rev1,rev2;

	int opmode;
	int nicam;
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
	int left, right;        /* volume */
	int bass, treble;

	/* shadow register set */
	int dfp_regs[DFP_COUNT];

	/* thread */
	struct task_struct   *kthread;
	wait_queue_head_t    wq;
	int                  restart:1;
	int                  watch_stereo:1;
};

#define MIN(a,b) (((a)>(b))?(b):(a))
#define MAX(a,b) (((a)>(b))?(a):(b))
#define HAVE_NICAM(msp)   (((msp->rev2>>8) & 0xff) != 00)
#define HAVE_SIMPLE(msp)  ((msp->rev1      & 0xff) >= 'D'-'@')
#define HAVE_SIMPLER(msp) ((msp->rev1      & 0xff) >= 'G'-'@')
#define HAVE_RADIO(msp)   ((msp->rev1      & 0xff) >= 'G'-'@')

#define VIDEO_MODE_RADIO 16      /* norm magic for radio mode */

/* ---------------------------------------------------------------------- */

/* read-only */
module_param(opmode,           int, 0444);

/* read-write */
module_param(once,             int, 0644);
module_param(debug,            int, 0644);
module_param(stereo_threshold, int, 0644);
module_param(standard,         int, 0644);
module_param(amsound,          int, 0644);
module_param(dolby,            int, 0644);

MODULE_PARM_DESC(opmode, "Forces a MSP3400 opmode. 0=Manual, 1=Simple, 2=Simpler");
MODULE_PARM_DESC(once, "No continuous stereo monitoring");
MODULE_PARM_DESC(debug, "Enable debug messages");
MODULE_PARM_DESC(stereo_threshold, "Sets signal threshold to activate stereo");
MODULE_PARM_DESC(standard, "Specify audio standard: 32 = NTSC, 64 = radio, Default: Autodetect");
MODULE_PARM_DESC(amsound, "Hardwire AM sound at 6.5Hz (France), FM can autoscan");
MODULE_PARM_DESC(dolby, "Activates Dolby processsing");

/* ---------------------------------------------------------------------- */

#define I2C_MSP3400C       0x80
#define I2C_MSP3400C_ALT   0x88

#define I2C_MSP3400C_DEM   0x10
#define I2C_MSP3400C_DFP   0x12

/* Addresses to scan */
static unsigned short normal_i2c[] = {
	I2C_MSP3400C      >> 1,
	I2C_MSP3400C_ALT  >> 1,
	I2C_CLIENT_END
};
I2C_CLIENT_INSMOD;

MODULE_DESCRIPTION("device driver for msp34xx TV sound processor");
MODULE_AUTHOR("Gerd Knorr");
MODULE_LICENSE("GPL");

/* ----------------------------------------------------------------------- */
/* functions for talking to the MSP3400C Sound processor                   */

static int msp3400c_reset(struct i2c_client *client)
{
	/* reset and read revision code */
	static char reset_off[3] = { 0x00, 0x80, 0x00 };
	static char reset_on[3]  = { 0x00, 0x00, 0x00 };
	static char write[3]     = { I2C_MSP3400C_DFP + 1, 0x00, 0x1e };
	char read[2];
	struct i2c_msg reset[2] = {
		{ client->addr, I2C_M_IGNORE_NAK, 3, reset_off },
		{ client->addr, I2C_M_IGNORE_NAK, 3, reset_on  },
	};
	struct i2c_msg test[2] = {
		{ client->addr, 0,        3, write },
		{ client->addr, I2C_M_RD, 2, read  },
	};

	msp3400_dbg_highvol("msp3400c_reset\n");
	if ( (1 != i2c_transfer(client->adapter,&reset[0],1)) ||
	     (1 != i2c_transfer(client->adapter,&reset[1],1)) ||
	     (2 != i2c_transfer(client->adapter,test,2)) ) {
		msp3400_err("chip reset failed\n");
		return -1;
	}
	return 0;
}

static int msp3400c_read(struct i2c_client *client, int dev, int addr)
{
	int err,retval;

	unsigned char write[3];
	unsigned char read[2];
	struct i2c_msg msgs[2] = {
		{ client->addr, 0,        3, write },
		{ client->addr, I2C_M_RD, 2, read  }
	};

	write[0] = dev+1;
	write[1] = addr >> 8;
	write[2] = addr & 0xff;

	for (err = 0; err < 3;) {
		if (2 == i2c_transfer(client->adapter,msgs,2))
			break;
		err++;
		msp3400_warn("I/O error #%d (read 0x%02x/0x%02x)\n", err,
		       dev, addr);
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(msecs_to_jiffies(10));
	}
	if (3 == err) {
		msp3400_warn("giving up, resetting chip. Sound will go off, sorry folks :-|\n");
		msp3400c_reset(client);
		return -1;
	}
	retval = read[0] << 8 | read[1];
	msp3400_dbg_highvol("msp3400c_read(0x%x, 0x%x): 0x%x\n", dev, addr, retval);
	return retval;
}

static int msp3400c_write(struct i2c_client *client, int dev, int addr, int val)
{
	int err;
	unsigned char buffer[5];

	buffer[0] = dev;
	buffer[1] = addr >> 8;
	buffer[2] = addr &  0xff;
	buffer[3] = val  >> 8;
	buffer[4] = val  &  0xff;

	msp3400_dbg_highvol("msp3400c_write(0x%x, 0x%x, 0x%x)\n", dev, addr, val);
	for (err = 0; err < 3;) {
		if (5 == i2c_master_send(client, buffer, 5))
			break;
		err++;
		msp3400_warn("I/O error #%d (write 0x%02x/0x%02x)\n", err,
		       dev, addr);
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(msecs_to_jiffies(10));
	}
	if (3 == err) {
		msp3400_warn("giving up, reseting chip. Sound will go off, sorry folks :-|\n");
		msp3400c_reset(client);
		return -1;
	}
	return 0;
}

/* ------------------------------------------------------------------------ */

/* This macro is allowed for *constants* only, gcc must calculate it
   at compile time.  Remember -- no floats in kernel mode */
#define MSP_CARRIER(freq) ((int)((float)(freq/18.432)*(1<<24)))

#define MSP_MODE_AM_DETECT   0
#define MSP_MODE_FM_RADIO    2
#define MSP_MODE_FM_TERRA    3
#define MSP_MODE_FM_SAT      4
#define MSP_MODE_FM_NICAM1   5
#define MSP_MODE_FM_NICAM2   6
#define MSP_MODE_AM_NICAM    7
#define MSP_MODE_BTSC        8
#define MSP_MODE_EXTERN      9

static struct MSP_INIT_DATA_DEM {
	int fir1[6];
	int fir2[6];
	int cdo1;
	int cdo2;
	int ad_cv;
	int mode_reg;
	int dfp_src;
	int dfp_matrix;
} msp_init_data[] = {
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

struct CARRIER_DETECT {
	int   cdo;
	char *name;
};

static struct CARRIER_DETECT carrier_detect_main[] = {
	/* main carrier */
	{ MSP_CARRIER(4.5),        "4.5   NTSC"                   },
	{ MSP_CARRIER(5.5),        "5.5   PAL B/G"                },
	{ MSP_CARRIER(6.0),        "6.0   PAL I"                  },
	{ MSP_CARRIER(6.5),        "6.5   PAL D/K + SAT + SECAM"  }
};

static struct CARRIER_DETECT carrier_detect_55[] = {
	/* PAL B/G */
	{ MSP_CARRIER(5.7421875),  "5.742 PAL B/G FM-stereo"     },
	{ MSP_CARRIER(5.85),       "5.85  PAL B/G NICAM"         }
};

static struct CARRIER_DETECT carrier_detect_65[] = {
	/* PAL SAT / SECAM */
	{ MSP_CARRIER(5.85),       "5.85  PAL D/K + SECAM NICAM" },
	{ MSP_CARRIER(6.2578125),  "6.25  PAL D/K1 FM-stereo" },
	{ MSP_CARRIER(6.7421875),  "6.74  PAL D/K2 FM-stereo" },
	{ MSP_CARRIER(7.02),       "7.02  PAL SAT FM-stereo s/b" },
	{ MSP_CARRIER(7.20),       "7.20  PAL SAT FM-stereo s"   },
	{ MSP_CARRIER(7.38),       "7.38  PAL SAT FM-stereo b"   },
};

#define CARRIER_COUNT(x) (sizeof(x)/sizeof(struct CARRIER_DETECT))

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

static void msp3400c_set_scart(struct i2c_client *client, int in, int out)
{
	struct msp3400c *msp = i2c_get_clientdata(client);

	msp->in_scart=in;

	if (in >= 1 && in <= 8 && out >= 0 && out <= 2) {
		if (-1 == scarts[out][in])
			return;

		msp->acb &= ~scarts[out][SCART_MASK];
		msp->acb |=  scarts[out][in];
	} else
		msp->acb = 0xf60; /* Mute Input and SCART 1 Output */

	msp3400_dbg("scart switch: %s => %d (ACB=0x%04x)\n",
						scart_names[in], out, msp->acb);
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x13, msp->acb);

	/* Sets I2S speed 0 = 1.024 Mbps, 1 = 2.048 Mbps */
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x40, msp->i2s_mode);
}

/* ------------------------------------------------------------------------ */

static void msp3400c_setcarrier(struct i2c_client *client, int cdo1, int cdo2)
{
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x0093, cdo1 & 0xfff);
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x009b, cdo1 >> 12);
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x00a3, cdo2 & 0xfff);
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x00ab, cdo2 >> 12);
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x0056, 0); /*LOAD_REG_1/2*/
}

static void msp3400c_setvolume(struct i2c_client *client,
			       int muted, int left, int right)
 {
	int vol = 0, val = 0, balance = 0;

	if (!muted) {
		/* 0x7f instead if 0x73 here has sound quality issues,
		 * probably due to overmodulation + clipping ... */
		vol = (left > right) ? left : right;
		val = (vol * 0x73 / 65535) << 8;
	}
	if (vol > 0) {
		balance = ((right - left) * 127) / vol;
	}

	msp3400_dbg("setvolume: mute=%s %d:%d  v=0x%02x b=0x%02x\n",
		muted ? "on" : "off", left, right, val >> 8, balance);
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x0000, val); /* loudspeaker */
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x0006, val); /* headphones  */
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x0007,
					muted ? 0x1 : (val | 0x1));
	msp3400c_write(client, I2C_MSP3400C_DFP, 0x0001, balance << 8);
}

static void msp3400c_setbass(struct i2c_client *client, int bass)
{
	int val = ((bass-32768) * 0x60 / 65535) << 8;

	msp3400_dbg("setbass: %d 0x%02x\n", bass, val >> 8);
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x0002, val); /* loudspeaker */
}

static void msp3400c_settreble(struct i2c_client *client, int treble)
{
	int val = ((treble-32768) * 0x60 / 65535) << 8;

	msp3400_dbg("settreble: %d 0x%02x\n",treble, val>>8);
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x0003, val); /* loudspeaker */
}

static void msp3400c_setmode(struct i2c_client *client, int type)
{
	struct msp3400c *msp = i2c_get_clientdata(client);
	int i;

	msp3400_dbg("setmode: %d\n",type);
	msp->mode       = type;
	msp->audmode    = V4L2_TUNER_MODE_MONO;
	msp->rxsubchans = V4L2_TUNER_SUB_MONO;

	msp3400c_write(client,I2C_MSP3400C_DEM, 0x00bb,          /* ad_cv */
		       msp_init_data[type].ad_cv);

	for (i = 5; i >= 0; i--)                                   /* fir 1 */
		msp3400c_write(client,I2C_MSP3400C_DEM, 0x0001,
			       msp_init_data[type].fir1[i]);

	msp3400c_write(client,I2C_MSP3400C_DEM, 0x0005, 0x0004); /* fir 2 */
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x0005, 0x0040);
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x0005, 0x0000);
	for (i = 5; i >= 0; i--)
		msp3400c_write(client,I2C_MSP3400C_DEM, 0x0005,
			       msp_init_data[type].fir2[i]);

	msp3400c_write(client,I2C_MSP3400C_DEM, 0x0083,     /* MODE_REG */
		       msp_init_data[type].mode_reg);

	msp3400c_setcarrier(client, msp_init_data[type].cdo1,
			    msp_init_data[type].cdo2);

	msp3400c_write(client,I2C_MSP3400C_DEM, 0x0056, 0); /*LOAD_REG_1/2*/

	if (dolby) {
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0008,
			       0x0520); /* I2S1 */
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0009,
			       0x0620); /* I2S2 */
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x000b,
			       msp_init_data[type].dfp_src);
	} else {
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0008,
			       msp_init_data[type].dfp_src);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0009,
			       msp_init_data[type].dfp_src);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x000b,
			       msp_init_data[type].dfp_src);
	}
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x000a,
		       msp_init_data[type].dfp_src);
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x000e,
		       msp_init_data[type].dfp_matrix);

	if (HAVE_NICAM(msp)) {
		/* nicam prescale */
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0010, 0x5a00); /* was: 0x3000 */
	}
}

/* given a bitmask of VIDEO_SOUND_XXX returns the "best" in the bitmask */
static int best_video_sound(int rxsubchans)
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
	struct msp3400c *msp = i2c_get_clientdata(client);
	int nicam = 0;		/* channel source: FM/AM or nicam */
	int src = 0;

	if (IS_MSP34XX_G(msp)) {
		/* this method would break everything, let's make sure
		 * it's never called
		 */
		msp3400_dbg
		    ("DEBUG WARNING setstereo called with mode=%d instead of set_source (ignored)\n",
		     mode);
		return;
	}

	/* switch demodulator */
	switch (msp->mode) {
	case MSP_MODE_FM_TERRA:
		msp3400_dbg("FM setstereo: %s\n", strmode[mode]);
		msp3400c_setcarrier(client,msp->second,msp->main);
		switch (mode) {
		case V4L2_TUNER_MODE_STEREO:
			msp3400c_write(client,I2C_MSP3400C_DFP, 0x000e, 0x3001);
			break;
		case V4L2_TUNER_MODE_MONO:
		case V4L2_TUNER_MODE_LANG1:
		case V4L2_TUNER_MODE_LANG2:
			msp3400c_write(client,I2C_MSP3400C_DFP, 0x000e, 0x3000);
			break;
		}
		break;
	case MSP_MODE_FM_SAT:
		msp3400_dbg("SAT setstereo: %s\n", strmode[mode]);
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
		msp3400_dbg("NICAM setstereo: %s\n",strmode[mode]);
		msp3400c_setcarrier(client,msp->second,msp->main);
		if (msp->nicam_on)
			nicam=0x0100;
		break;
	case MSP_MODE_BTSC:
		msp3400_dbg("BTSC setstereo: %s\n",strmode[mode]);
		nicam=0x0300;
		break;
	case MSP_MODE_EXTERN:
		msp3400_dbg("extern setstereo: %s\n",strmode[mode]);
		nicam = 0x0200;
		break;
	case MSP_MODE_FM_RADIO:
		msp3400_dbg("FM-Radio setstereo: %s\n",strmode[mode]);
		break;
	default:
		msp3400_dbg("mono setstereo\n");
		return;
	}

	/* switch audio */
	switch (best_video_sound(mode)) {
	case V4L2_TUNER_MODE_STEREO:
		src = 0x0020 | nicam;
		break;
	case V4L2_TUNER_MODE_MONO:
		if (msp->mode == MSP_MODE_AM_NICAM) {
			msp3400_dbg("switching to AM mono\n");
			/* AM mono decoding is handled by tuner, not MSP chip */
			/* SCART switching control register */
			msp3400c_set_scart(client,SCART_MONO,0);
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
	msp3400_dbg("setstereo final source/matrix = 0x%x\n", src);

	if (dolby) {
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0008,0x0520);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0009,0x0620);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x000a,src);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x000b,src);
	} else {
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0008,src);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0009,src);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x000a,src);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x000b,src);
	}
}

static void
msp3400c_print_mode(struct i2c_client *client)
{
	struct msp3400c *msp = i2c_get_clientdata(client);

	if (msp->main == msp->second) {
		msp3400_dbg("mono sound carrier: %d.%03d MHz\n",
		       msp->main/910000,(msp->main/910)%1000);
	} else {
		msp3400_dbg("main sound carrier: %d.%03d MHz\n",
		       msp->main/910000,(msp->main/910)%1000);
	}
	if (msp->mode == MSP_MODE_FM_NICAM1 || msp->mode == MSP_MODE_FM_NICAM2)
		msp3400_dbg("NICAM/FM carrier   : %d.%03d MHz\n",
		       msp->second/910000,(msp->second/910)%1000);
	if (msp->mode == MSP_MODE_AM_NICAM)
		msp3400_dbg("NICAM/AM carrier   : %d.%03d MHz\n",
		       msp->second/910000,(msp->second/910)%1000);
	if (msp->mode == MSP_MODE_FM_TERRA &&
	    msp->main != msp->second) {
		msp3400_dbg("FM-stereo carrier : %d.%03d MHz\n",
		       msp->second/910000,(msp->second/910)%1000);
	}
}

#define MSP3400_MAX 4
static struct i2c_client *msps[MSP3400_MAX];
static void msp3400c_restore_dfp(struct i2c_client *client)
{
	struct msp3400c *msp = i2c_get_clientdata(client);
	int i;

	for (i = 0; i < DFP_COUNT; i++) {
		if (-1 == msp->dfp_regs[i])
			continue;
		msp3400c_write(client, I2C_MSP3400C_DFP, i, msp->dfp_regs[i]);
	}
}

/* if the dfp_regs is set, set what's in there. Otherwise, set the default value */
static int msp3400c_write_dfp_with_default(struct i2c_client *client,
					int addr, int default_value)
{
	struct msp3400c *msp = i2c_get_clientdata(client);
	int value = default_value;
	if (addr < DFP_COUNT && -1 != msp->dfp_regs[addr])
		value = msp->dfp_regs[addr];
	return msp3400c_write(client, I2C_MSP3400C_DFP, addr, value);
}

/* ----------------------------------------------------------------------- */

struct REGISTER_DUMP {
	int   addr;
	char *name;
};

struct REGISTER_DUMP d1[] = {
	{0x007e, "autodetect"},
	{0x0023, "C_AD_BITS "},
	{0x0038, "ADD_BITS  "},
	{0x003e, "CIB_BITS  "},
	{0x0057, "ERROR_RATE"},
};

static int autodetect_stereo(struct i2c_client *client)
{
	struct msp3400c *msp = i2c_get_clientdata(client);
	int val;
	int rxsubchans = msp->rxsubchans;
	int newnicam   = msp->nicam_on;
	int update = 0;

	switch (msp->mode) {
	case MSP_MODE_FM_TERRA:
		val = msp3400c_read(client, I2C_MSP3400C_DFP, 0x18);
		if (val > 32767)
			val -= 65536;
		msp3400_dbg("stereo detect register: %d\n",val);
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
		val = msp3400c_read(client, I2C_MSP3400C_DEM, 0x23);
		msp3400_dbg("nicam sync=%d, mode=%d\n",
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
		val = msp3400c_read(client, I2C_MSP3400C_DEM, 0x200);
		msp3400_dbg("status=0x%x (pri=%s, sec=%s, %s%s%s)\n",
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
	if (rxsubchans != msp->rxsubchans) {
		update = 1;
		msp3400_dbg("watch: rxsubchans %d => %d\n",
			msp->rxsubchans,rxsubchans);
		msp->rxsubchans = rxsubchans;
	}
	if (newnicam != msp->nicam_on) {
		update = 1;
		msp3400_dbg("watch: nicam %d => %d\n",
			msp->nicam_on,newnicam);
		msp->nicam_on = newnicam;
	}
	return update;
}

/*
 * A kernel thread for msp3400 control -- we don't want to block the
 * in the ioctl while doing the sound carrier & stereo detect
 */

static int msp34xx_sleep(struct msp3400c *msp, int timeout)
{
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&msp->wq, &wait);
	if (!kthread_should_stop()) {
		if (timeout < 0) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
		} else {
			schedule_timeout_interruptible
						(msecs_to_jiffies(timeout));
		}
	}

	remove_wait_queue(&msp->wq, &wait);
	try_to_freeze();
	return msp->restart;
}

/* stereo/multilang monitoring */
static void watch_stereo(struct i2c_client *client)
{
	struct msp3400c *msp = i2c_get_clientdata(client);

	if (autodetect_stereo(client)) {
		if (msp->stereo & V4L2_TUNER_MODE_STEREO)
			msp3400c_setstereo(client, V4L2_TUNER_MODE_STEREO);
		else if (msp->stereo & VIDEO_SOUND_LANG1)
			msp3400c_setstereo(client, V4L2_TUNER_MODE_LANG1);
		else
			msp3400c_setstereo(client, V4L2_TUNER_MODE_MONO);
	}

	if (once)
		msp->watch_stereo = 0;
}

static int msp3400c_thread(void *data)
{
	struct i2c_client *client = data;
	struct msp3400c *msp = i2c_get_clientdata(client);
	struct CARRIER_DETECT *cd;
	int count, max1,max2,val1,val2, val,this;

	msp3400_info("msp3400 daemon started\n");
	for (;;) {
		msp3400_dbg_mediumvol("msp3400 thread: sleep\n");
		msp34xx_sleep(msp,-1);
		msp3400_dbg_mediumvol("msp3400 thread: wakeup\n");

	restart:
		msp3400_dbg("thread: restart scan\n");
		msp->restart = 0;
		if (kthread_should_stop())
			break;

		if (VIDEO_MODE_RADIO == msp->norm ||
		    MSP_MODE_EXTERN  == msp->mode) {
			/* no carrier scan, just unmute */
			msp3400_info("thread: no carrier scan\n");
			msp3400c_setvolume(client, msp->muted, msp->left, msp->right);
			continue;
		}

		/* mute */
		msp3400c_setvolume(client, msp->muted, 0, 0);
		msp3400c_setmode(client, MSP_MODE_AM_DETECT /* +1 */ );
		val1 = val2 = 0;
		max1 = max2 = -1;
		msp->watch_stereo = 0;

		/* some time for the tuner to sync */
		if (msp34xx_sleep(msp,200))
			goto restart;

		/* carrier detect pass #1 -- main carrier */
		cd = carrier_detect_main;
		count = CARRIER_COUNT(carrier_detect_main);

		if (amsound && (msp->norm == VIDEO_MODE_SECAM)) {
			/* autodetect doesn't work well with AM ... */
			max1 = 3;
			count = 0;
			msp3400_dbg("AM sound override\n");
		}

		for (this = 0; this < count; this++) {
			msp3400c_setcarrier(client, cd[this].cdo,cd[this].cdo);
			if (msp34xx_sleep(msp,100))
				goto restart;
			val = msp3400c_read(client, I2C_MSP3400C_DFP, 0x1b);
			if (val > 32767)
				val -= 65536;
			if (val1 < val)
				val1 = val, max1 = this;
			msp3400_dbg("carrier1 val: %5d / %s\n", val,cd[this].name);
		}

		/* carrier detect pass #2 -- second (stereo) carrier */
		switch (max1) {
		case 1: /* 5.5 */
			cd = carrier_detect_55;
			count = CARRIER_COUNT(carrier_detect_55);
			break;
		case 3: /* 6.5 */
			cd = carrier_detect_65;
			count = CARRIER_COUNT(carrier_detect_65);
			break;
		case 0: /* 4.5 */
		case 2: /* 6.0 */
		default:
			cd = NULL;
			count = 0;
			break;
		}

		if (amsound && (msp->norm == VIDEO_MODE_SECAM)) {
			/* autodetect doesn't work well with AM ... */
			cd = NULL;
			count = 0;
			max2 = 0;
		}
		for (this = 0; this < count; this++) {
			msp3400c_setcarrier(client, cd[this].cdo,cd[this].cdo);
			if (msp34xx_sleep(msp,100))
				goto restart;
			val = msp3400c_read(client, I2C_MSP3400C_DFP, 0x1b);
			if (val > 32767)
				val -= 65536;
			if (val2 < val)
				val2 = val, max2 = this;
			msp3400_dbg("carrier2 val: %5d / %s\n", val,cd[this].name);
		}

		/* programm the msp3400 according to the results */
		msp->main   = carrier_detect_main[max1].cdo;
		switch (max1) {
		case 1: /* 5.5 */
			if (max2 == 0) {
				/* B/G FM-stereo */
				msp->second = carrier_detect_55[max2].cdo;
				msp3400c_setmode(client, MSP_MODE_FM_TERRA);
				msp->nicam_on = 0;
				msp3400c_setstereo(client, V4L2_TUNER_MODE_MONO);
				msp->watch_stereo = 1;
			} else if (max2 == 1 && HAVE_NICAM(msp)) {
				/* B/G NICAM */
				msp->second = carrier_detect_55[max2].cdo;
				msp3400c_setmode(client, MSP_MODE_FM_NICAM1);
				msp->nicam_on = 1;
				msp3400c_setcarrier(client, msp->second, msp->main);
				msp->watch_stereo = 1;
			} else {
				goto no_second;
			}
			break;
		case 2: /* 6.0 */
			/* PAL I NICAM */
			msp->second = MSP_CARRIER(6.552);
			msp3400c_setmode(client, MSP_MODE_FM_NICAM2);
			msp->nicam_on = 1;
			msp3400c_setcarrier(client, msp->second, msp->main);
			msp->watch_stereo = 1;
			break;
		case 3: /* 6.5 */
			if (max2 == 1 || max2 == 2) {
				/* D/K FM-stereo */
				msp->second = carrier_detect_65[max2].cdo;
				msp3400c_setmode(client, MSP_MODE_FM_TERRA);
				msp->nicam_on = 0;
				msp3400c_setstereo(client, V4L2_TUNER_MODE_MONO);
				msp->watch_stereo = 1;
			} else if (max2 == 0 &&
				   msp->norm == VIDEO_MODE_SECAM) {
				/* L NICAM or AM-mono */
				msp->second = carrier_detect_65[max2].cdo;
				msp3400c_setmode(client, MSP_MODE_AM_NICAM);
				msp->nicam_on = 0;
				msp3400c_setstereo(client, V4L2_TUNER_MODE_MONO);
				msp3400c_setcarrier(client, msp->second, msp->main);
				/* volume prescale for SCART (AM mono input) */
				msp3400c_write(client,I2C_MSP3400C_DFP, 0x000d, 0x1900);
				msp->watch_stereo = 1;
			} else if (max2 == 0 && HAVE_NICAM(msp)) {
				/* D/K NICAM */
				msp->second = carrier_detect_65[max2].cdo;
				msp3400c_setmode(client, MSP_MODE_FM_NICAM1);
				msp->nicam_on = 1;
				msp3400c_setcarrier(client, msp->second, msp->main);
				msp->watch_stereo = 1;
			} else {
				goto no_second;
			}
			break;
		case 0: /* 4.5 */
		default:
		no_second:
			msp->second = carrier_detect_main[max1].cdo;
			msp3400c_setmode(client, MSP_MODE_FM_TERRA);
			msp->nicam_on = 0;
			msp3400c_setcarrier(client, msp->second, msp->main);
			msp->rxsubchans = V4L2_TUNER_SUB_MONO;
			msp3400c_setstereo(client, V4L2_TUNER_MODE_MONO);
			break;
		}

		/* unmute */
		msp3400c_setvolume(client, msp->muted, msp->left, msp->right);
		msp3400c_restore_dfp(client);

		if (debug)
			msp3400c_print_mode(client);

		/* monitor tv audio mode */
		while (msp->watch_stereo) {
			if (msp34xx_sleep(msp,5000))
				goto restart;
			watch_stereo(client);
		}
	}
	msp3400_dbg("thread: exit\n");
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
	{ 0x0000, 0, 0, "ERROR" },
	{ 0x0001, 0, 0, "autodetect start" },
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

static inline const char *msp34xx_standard_mode_name(int mode)
{
	int i;
	for (i = 0; modelist[i].name != NULL; i++)
		if (modelist[i].retval == mode)
			return modelist[i].name;
	return "unknown";
}

static int msp34xx_modus(struct i2c_client *client, int norm)
{
	switch (norm) {
	case VIDEO_MODE_PAL:
		msp3400_dbg("video mode selected to PAL\n");

#if 1
		/* experimental: not sure this works with all chip versions */
		return 0x7003;
#else
		/* previous value, try this if it breaks ... */
		return 0x1003;
#endif
	case VIDEO_MODE_NTSC:  /* BTSC */
		msp3400_dbg("video mode selected to NTSC\n");
		return 0x2003;
	case VIDEO_MODE_SECAM:
		msp3400_dbg("video mode selected to SECAM\n");
		return 0x0003;
	case VIDEO_MODE_RADIO:
		msp3400_dbg("video mode selected to Radio\n");
		return 0x0003;
	case VIDEO_MODE_AUTO:
		msp3400_dbg("video mode selected to Auto\n");
		return 0x2003;
	default:
		return 0x0003;
	}
}

static int msp34xx_standard(int norm)
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
	struct msp3400c *msp = i2c_get_clientdata(client);
	int mode,val,i,std;

	msp3400_info("msp3410 daemon started\n");
	for (;;) {
		msp3400_dbg_mediumvol("msp3410 thread: sleep\n");
		msp34xx_sleep(msp,-1);
		msp3400_dbg_mediumvol("msp3410 thread: wakeup\n");

	restart:
		msp3400_dbg("thread: restart scan\n");
		msp->restart = 0;
		if (kthread_should_stop())
			break;

		if (msp->mode == MSP_MODE_EXTERN) {
			/* no carrier scan needed, just unmute */
			msp3400_dbg("thread: no carrier scan\n");
		msp3400c_setvolume(client, msp->muted, msp->left, msp->right);
			continue;
		}

		/* put into sane state (and mute) */
		msp3400c_reset(client);

		/* some time for the tuner to sync */
		if (msp34xx_sleep(msp,200))
			goto restart;

		/* start autodetect */
		mode = msp34xx_modus(client, msp->norm);
		std  = msp34xx_standard(msp->norm);
		msp3400c_write(client, I2C_MSP3400C_DEM, 0x30, mode);
		msp3400c_write(client, I2C_MSP3400C_DEM, 0x20, std);
		msp->watch_stereo = 0;

		if (debug)
			msp3400_dbg("setting mode: %s (0x%04x)\n",
			       msp34xx_standard_mode_name(std) ,std);

		if (std != 1) {
			/* programmed some specific mode */
			val = std;
		} else {
			/* triggered autodetect */
			for (;;) {
				if (msp34xx_sleep(msp,100))
					goto restart;

				/* check results */
				val = msp3400c_read(client, I2C_MSP3400C_DEM, 0x7e);
				if (val < 0x07ff)
					break;
				msp3400_dbg("detection still in progress\n");
			}
		}
		for (i = 0; modelist[i].name != NULL; i++)
			if (modelist[i].retval == val)
				break;
		msp3400_dbg("current mode: %s (0x%04x)\n",
			modelist[i].name ? modelist[i].name : "unknown",
			val);
		msp->main   = modelist[i].main;
		msp->second = modelist[i].second;

		if (amsound && (msp->norm == VIDEO_MODE_SECAM) && (val != 0x0009)) {
			/* autodetection has failed, let backup */
			msp3400_dbg("autodetection failed,"
				" switching to backup mode: %s (0x%04x)\n",
				modelist[8].name ? modelist[8].name : "unknown",val);
			val = 0x0009;
			msp3400c_write(client, I2C_MSP3400C_DEM, 0x20, val);
		}

		/* set various prescales */
		msp3400c_write(client, I2C_MSP3400C_DFP, 0x0d, 0x1900); /* scart */
		msp3400c_write(client, I2C_MSP3400C_DFP, 0x0e, 0x2403); /* FM */
		msp3400c_write(client, I2C_MSP3400C_DFP, 0x10, 0x5a00); /* nicam */

		/* set stereo */
		switch (val) {
		case 0x0008: /* B/G NICAM */
		case 0x000a: /* I NICAM */
			if (val == 0x0008)
				msp->mode = MSP_MODE_FM_NICAM1;
			else
				msp->mode = MSP_MODE_FM_NICAM2;
			/* just turn on stereo */
			msp->rxsubchans = V4L2_TUNER_SUB_STEREO;
			msp->nicam_on = 1;
			msp->watch_stereo = 1;
			msp3400c_setstereo(client,V4L2_TUNER_MODE_STEREO);
			break;
		case 0x0009:
			msp->mode = MSP_MODE_AM_NICAM;
			msp->rxsubchans = V4L2_TUNER_SUB_MONO;
			msp->nicam_on = 1;
			msp3400c_setstereo(client,V4L2_TUNER_MODE_MONO);
			msp->watch_stereo = 1;
			break;
		case 0x0020: /* BTSC */
			/* just turn on stereo */
			msp->mode = MSP_MODE_BTSC;
			msp->rxsubchans = V4L2_TUNER_SUB_STEREO;
			msp->nicam_on = 0;
			msp->watch_stereo = 1;
			msp3400c_setstereo(client,V4L2_TUNER_MODE_STEREO);
			break;
		case 0x0040: /* FM radio */
			msp->mode   = MSP_MODE_FM_RADIO;
			msp->rxsubchans = V4L2_TUNER_SUB_STEREO;
			msp->audmode = V4L2_TUNER_MODE_STEREO;
			msp->nicam_on = 0;
			msp->watch_stereo = 0;
			/* not needed in theory if HAVE_RADIO(), but
			   short programming enables carrier mute */
			msp3400c_setmode(client,MSP_MODE_FM_RADIO);
			msp3400c_setcarrier(client, MSP_CARRIER(10.7),
					    MSP_CARRIER(10.7));
			/* scart routing */
			msp3400c_set_scart(client,SCART_IN2,0);
			/* msp34xx does radio decoding */
			msp3400c_write(client,I2C_MSP3400C_DFP, 0x08, 0x0020);
			msp3400c_write(client,I2C_MSP3400C_DFP, 0x09, 0x0020);
			msp3400c_write(client,I2C_MSP3400C_DFP, 0x0b, 0x0020);
			break;
		case 0x0003:
		case 0x0004:
		case 0x0005:
			msp->mode   = MSP_MODE_FM_TERRA;
			msp->rxsubchans = V4L2_TUNER_SUB_MONO;
			msp->audmode = V4L2_TUNER_MODE_MONO;
			msp->nicam_on = 0;
			msp->watch_stereo = 1;
			break;
		}

		/* unmute, restore misc registers */
		msp3400c_setbass(client, msp->bass);
		msp3400c_settreble(client, msp->treble);
		msp3400c_setvolume(client, msp->muted, msp->left, msp->right);
		msp3400c_write(client, I2C_MSP3400C_DFP, 0x13, msp->acb);
		msp3400c_write(client,I2C_MSP3400C_DEM, 0x40, msp->i2s_mode);
		msp3400c_restore_dfp(client);

		/* monitor tv audio mode */
		while (msp->watch_stereo) {
			if (msp34xx_sleep(msp,5000))
				goto restart;
			watch_stereo(client);
		}
	}
	msp3400_dbg("thread: exit\n");
	return 0;
}

/* ----------------------------------------------------------------------- */
/* msp34xxG + (simpler no-thread)                                          */
/* this one uses both automatic standard detection and automatic sound     */
/* select which are available in the newer G versions                      */
/* struct msp: only norm, acb and source are really used in this mode      */

static void msp34xxg_set_source(struct i2c_client *client, int source);

/* (re-)initialize the msp34xxg, according to the current norm in msp->norm
 * return 0 if it worked, -1 if it failed
 */
static int msp34xxg_reset(struct i2c_client *client)
{
	struct msp3400c *msp = i2c_get_clientdata(client);
	int modus,std;

	if (msp3400c_reset(client))
		return -1;

	/* make sure that input/output is muted (paranoid mode) */
	if (msp3400c_write(client,
			   I2C_MSP3400C_DFP,
			   0x13, /* ACB */
			   0x0f20 /* mute DSP input, mute SCART 1 */))
		return -1;

	msp3400c_write(client,I2C_MSP3400C_DEM, 0x40, msp->i2s_mode);

	/* step-by-step initialisation, as described in the manual */
	modus = msp34xx_modus(client, msp->norm);
	std   = msp34xx_standard(msp->norm);
	modus &= ~0x03; /* STATUS_CHANGE=0 */
	modus |= 0x01;  /* AUTOMATIC_SOUND_DETECTION=1 */
	if (msp3400c_write(client,
			   I2C_MSP3400C_DEM,
			   0x30/*MODUS*/,
			   modus))
		return -1;
	if (msp3400c_write(client,
			   I2C_MSP3400C_DEM,
			   0x20/*standard*/,
			   std))
		return -1;

	/* write the dfps that may have an influence on
	   standard/audio autodetection right now */
	msp34xxg_set_source(client, msp->source);

	if (msp3400c_write_dfp_with_default(client, 0x0e,	/* AM/FM Prescale */
					    0x3000
					    /* default: [15:8] 75khz deviation */
	    ))
		return -1;

	if (msp3400c_write_dfp_with_default(client, 0x10,	/* NICAM Prescale */
					    0x5a00
					    /* default: 9db gain (as recommended) */
	    ))
		return -1;

	return 0;
}

static int msp34xxg_thread(void *data)
{
	struct i2c_client *client = data;
	struct msp3400c *msp = i2c_get_clientdata(client);
	int val, std, i;

	msp3400_info("msp34xxg daemon started\n");
	msp->source = 1; /* default */
	for (;;) {
		msp3400_dbg_mediumvol("msp34xxg thread: sleep\n");
		msp34xx_sleep(msp,-1);
		msp3400_dbg_mediumvol("msp34xxg thread: wakeup\n");

	restart:
		msp3400_dbg("thread: restart scan\n");
		msp->restart = 0;
		if (kthread_should_stop())
			break;

		/* setup the chip*/
		msp34xxg_reset(client);
		std = standard;
		if (std != 0x01)
			goto unmute;

		/* watch autodetect */
		msp3400_dbg("triggered autodetect, waiting for result\n");
		for (i = 0; i < 10; i++) {
			if (msp34xx_sleep(msp,100))
				goto restart;

			/* check results */
			val = msp3400c_read(client, I2C_MSP3400C_DEM, 0x7e);
			if (val < 0x07ff) {
				std = val;
				break;
			}
			msp3400_dbg("detection still in progress\n");
		}
		if (0x01 == std) {
			msp3400_dbg("detection still in progress after 10 tries. giving up.\n");
			continue;
		}

	unmute:
		msp3400_dbg("current mode: %s (0x%04x)\n",
			msp34xx_standard_mode_name(std), std);

		/* unmute: dispatch sound to scart output, set scart volume */
		msp3400_dbg("unmute\n");

		msp3400c_setbass(client, msp->bass);
		msp3400c_settreble(client, msp->treble);
		msp3400c_setvolume(client, msp->muted, msp->left, msp->right);

		/* restore ACB */
		if (msp3400c_write(client,
				   I2C_MSP3400C_DFP,
				   0x13, /* ACB */
				   msp->acb))
			return -1;

		msp3400c_write(client,I2C_MSP3400C_DEM, 0x40, msp->i2s_mode);
	}
	msp3400_dbg("thread: exit\n");
	return 0;
}

/* set the same 'source' for the loudspeaker, scart and quasi-peak detector
 * the value for source is the same as bit 15:8 of DFP registers 0x08,
 * 0x0a and 0x0c: 0=mono, 1=stereo or A|B, 2=SCART, 3=stereo or A, 4=stereo or B
 *
 * this function replaces msp3400c_setstereo
 */
static void msp34xxg_set_source(struct i2c_client *client, int source)
{
	struct msp3400c *msp = i2c_get_clientdata(client);

	/* fix matrix mode to stereo and let the msp choose what
	 * to output according to 'source', as recommended
	 * for MONO (source==0) downmixing set bit[7:0] to 0x30
	 */
	int value = (source&0x07)<<8|(source==0 ? 0x30:0x20);
	msp3400_dbg("set source to %d (0x%x)\n", source, value);
	msp3400c_write(client,
		       I2C_MSP3400C_DFP,
		       0x08, /* Loudspeaker Output */
		       value);
	msp3400c_write(client,
		       I2C_MSP3400C_DFP,
		       0x0a, /* SCART1 DA Output */
		       value);
	msp3400c_write(client,
		       I2C_MSP3400C_DFP,
		       0x0c, /* Quasi-peak detector */
		       value);
	/*
	 * set identification threshold. Personally, I
	 * I set it to a higher value that the default
	 * of 0x190 to ignore noisy stereo signals.
	 * this needs tuning. (recommended range 0x00a0-0x03c0)
	 * 0x7f0 = forced mono mode
	 */
	msp3400c_write(client,
		       I2C_MSP3400C_DEM,
		       0x22, /* a2 threshold for stereo/bilingual */
		       stereo_threshold);
	msp->source=source;
}

static void msp34xxg_detect_stereo(struct i2c_client *client)
{
	struct msp3400c *msp = i2c_get_clientdata(client);

	int status = msp3400c_read(client,
				   I2C_MSP3400C_DEM,
				   0x0200 /* STATUS */);
	int is_bilingual = status&0x100;
	int is_stereo = status&0x40;

	msp->rxsubchans = 0;
	if (is_stereo)
		msp->rxsubchans |= V4L2_TUNER_SUB_STEREO;
	else
		msp->rxsubchans |= V4L2_TUNER_SUB_MONO;
	if (is_bilingual) {
		msp->rxsubchans |= V4L2_TUNER_SUB_LANG1|V4L2_TUNER_SUB_LANG2;
		/* I'm supposed to check whether it's SAP or not
		 * and set only LANG2/SAP in this case. Yet, the MSP
		 * does a lot of work to hide this and handle everything
		 * the same way. I don't want to work around it so unless
		 * this is a problem, I'll handle SAP just like lang1/lang2.
		 */
	}
	msp3400_dbg("status=0x%x, stereo=%d, bilingual=%d -> rxsubchans=%d\n",
		status, is_stereo, is_bilingual, msp->rxsubchans);
}

static void msp34xxg_set_audmode(struct i2c_client *client, int audmode)
{
	struct msp3400c *msp = i2c_get_clientdata(client);
	int source;

	switch (audmode) {
	case V4L2_TUNER_MODE_MONO:
		source=0; /* mono only */
		break;
	case V4L2_TUNER_MODE_STEREO:
		source=1; /* stereo or A|B, see comment in msp34xxg_get_v4l2_stereo() */
		/* problem: that could also mean 2 (scart input) */
		break;
	case V4L2_TUNER_MODE_LANG1:
		source=3; /* stereo or A */
		break;
	case V4L2_TUNER_MODE_LANG2:
		source=4; /* stereo or B */
		break;
	default:
		audmode = 0;
		source  = 1;
		break;
	}
	msp->audmode = audmode;
	msp34xxg_set_source(client, source);
}


/* ----------------------------------------------------------------------- */

static int msp_attach(struct i2c_adapter *adap, int addr, int kind);
static int msp_detach(struct i2c_client *client);
static int msp_probe(struct i2c_adapter *adap);
static int msp_command(struct i2c_client *client, unsigned int cmd, void *arg);

static int msp_suspend(struct device * dev, pm_message_t state);
static int msp_resume(struct device * dev);

static void msp_wake_thread(struct i2c_client *client);

static struct i2c_driver driver = {
	.owner          = THIS_MODULE,
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
};

static struct i2c_client client_template =
{
	.name      = "(unset)",
	.flags     = I2C_CLIENT_ALLOW_USE,
        .driver    = &driver,
};

static int msp_attach(struct i2c_adapter *adap, int addr, int kind)
{
	struct msp3400c *msp;
	struct i2c_client *client = &client_template;
	int (*thread_func)(void *data) = NULL;
	int i;

	client_template.adapter = adap;
	client_template.addr = addr;

	if (-1 == msp3400c_reset(&client_template)) {
		msp3400_dbg("no chip found\n");
		return -1;
	}

	if (NULL == (client = kmalloc(sizeof(struct i2c_client),GFP_KERNEL)))
		return -ENOMEM;
	memcpy(client,&client_template,sizeof(struct i2c_client));
	if (NULL == (msp = kmalloc(sizeof(struct msp3400c),GFP_KERNEL))) {
		kfree(client);
		return -ENOMEM;
	}

	memset(msp,0,sizeof(struct msp3400c));
	msp->norm = VIDEO_MODE_NTSC;
	msp->left = 58880;	/* 0db gain */
	msp->right = 58880;	/* 0db gain */
	msp->bass = 32768;
	msp->treble = 32768;
	msp->input = -1;
	msp->muted = 0;
	msp->i2s_mode = 0;
	for (i = 0; i < DFP_COUNT; i++)
		msp->dfp_regs[i] = -1;

	i2c_set_clientdata(client, msp);
	init_waitqueue_head(&msp->wq);

	if (-1 == msp3400c_reset(client)) {
		kfree(msp);
		kfree(client);
		msp3400_dbg("no chip found\n");
		return -1;
	}

	msp->rev1 = msp3400c_read(client, I2C_MSP3400C_DFP, 0x1e);
	if (-1 != msp->rev1)
		msp->rev2 = msp3400c_read(client, I2C_MSP3400C_DFP, 0x1f);
	if ((-1 == msp->rev1) || (0 == msp->rev1 && 0 == msp->rev2)) {
		kfree(msp);
		kfree(client);
		msp3400_dbg("error while reading chip version\n");
		return -1;
	}
	msp3400_dbg("rev1=0x%04x, rev2=0x%04x\n", msp->rev1, msp->rev2);

	msp3400c_setvolume(client, msp->muted, msp->left, msp->right);

	snprintf(client->name, sizeof(client->name), "MSP%c4%02d%c-%c%d",
		 ((msp->rev1>>4)&0x0f) + '3',
		 (msp->rev2>>8)&0xff, (msp->rev1&0x0f)+'@',
		 ((msp->rev1>>8)&0xff)+'@', msp->rev2&0x1f);

	msp->opmode = opmode;
	if (OPMODE_AUTO == msp->opmode) {
		if (HAVE_SIMPLER(msp))
			msp->opmode = OPMODE_SIMPLER;
		else if (HAVE_SIMPLE(msp))
			msp->opmode = OPMODE_SIMPLE;
		else
			msp->opmode = OPMODE_MANUAL;
	}

	/* hello world :-) */
	msp3400_info("chip=%s", client->name);
	if (HAVE_NICAM(msp))
		printk(" +nicam");
	if (HAVE_SIMPLE(msp))
		printk(" +simple");
	if (HAVE_SIMPLER(msp))
		printk(" +simpler");
	if (HAVE_RADIO(msp))
		printk(" +radio");

	/* version-specific initialization */
	switch (msp->opmode) {
	case OPMODE_MANUAL:
		printk(" mode=manual");
		thread_func = msp3400c_thread;
		break;
	case OPMODE_SIMPLE:
		printk(" mode=simple");
		thread_func = msp3410d_thread;
		break;
	case OPMODE_SIMPLER:
		printk(" mode=simpler");
		thread_func = msp34xxg_thread;
		break;
	}
	printk("\n");

	/* startup control thread if needed */
	if (thread_func) {
		msp->kthread = kthread_run(thread_func, client, "msp34xx");

		if (NULL == msp->kthread)
			msp3400_warn("kernel_thread() failed\n");
		msp_wake_thread(client);
	}

	/* done */
	i2c_attach_client(client);

	/* update our own array */
	for (i = 0; i < MSP3400_MAX; i++) {
		if (NULL == msps[i]) {
			msps[i] = client;
			break;
		}
	}

	return 0;
}

static int msp_detach(struct i2c_client *client)
{
	struct msp3400c *msp  = i2c_get_clientdata(client);
	int i;

	/* shutdown control thread */
	if (msp->kthread) {
		msp->restart = 1;
		kthread_stop(msp->kthread);
	}
	msp3400c_reset(client);

	/* update our own array */
	for (i = 0; i < MSP3400_MAX; i++) {
		if (client == msps[i]) {
			msps[i] = NULL;
			break;
		}
	}

	i2c_detach_client(client);

	kfree(msp);
	kfree(client);
	return 0;
}

static int msp_probe(struct i2c_adapter *adap)
{
	if (adap->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adap, &addr_data, msp_attach);
	return 0;
}

static void msp_wake_thread(struct i2c_client *client)
{
	struct msp3400c *msp  = i2c_get_clientdata(client);

	if (NULL == msp->kthread)
		return;
	msp3400c_setvolume(client,msp->muted,0,0);
	msp->watch_stereo = 0;
	msp->restart = 1;
	wake_up_interruptible(&msp->wq);
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
	if (0 == mode)
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
	struct msp3400c *msp  = i2c_get_clientdata(client);

	switch (msp->opmode) {
	case OPMODE_MANUAL:
	case OPMODE_SIMPLE:
		autodetect_stereo(client);
		break;
	case OPMODE_SIMPLER:
		msp34xxg_detect_stereo(client);
		break;
	}
}

static void msp_any_set_audmode(struct i2c_client *client, int audmode)
{
	struct msp3400c *msp  = i2c_get_clientdata(client);

	switch (msp->opmode) {
	case OPMODE_MANUAL:
	case OPMODE_SIMPLE:
		msp->watch_stereo = 0;
		msp3400c_setstereo(client, audmode);
		break;
	case OPMODE_SIMPLER:
		msp34xxg_set_audmode(client, audmode);
		break;
	}
}


static int msp_command(struct i2c_client *client, unsigned int cmd, void *arg)
{
	struct msp3400c *msp  = i2c_get_clientdata(client);
	__u16           *sarg = arg;
	int scart = 0;

	switch (cmd) {

	case AUDC_SET_INPUT:
		msp3400_dbg("AUDC_SET_INPUT(%d)\n",*sarg);

		if (*sarg == msp->input)
			break;
		msp->input = *sarg;
		switch (*sarg) {
		case AUDIO_RADIO:
			/* Hauppauge uses IN2 for the radio */
			msp->mode   = MSP_MODE_FM_RADIO;
			scart       = SCART_IN2;
			break;
		case AUDIO_EXTERN_1:
			/* IN1 is often used for external input ... */
			msp->mode   = MSP_MODE_EXTERN;
			scart       = SCART_IN1;
			break;
		case AUDIO_EXTERN_2:
			/* ... sometimes it is IN2 through ;) */
			msp->mode   = MSP_MODE_EXTERN;
			scart       = SCART_IN2;
			break;
		case AUDIO_TUNER:
			msp->mode   = -1;
			break;
		default:
			if (*sarg & AUDIO_MUTE)
				msp3400c_set_scart(client,SCART_MUTE,0);
			break;
		}
		if (scart) {
			msp->rxsubchans = V4L2_TUNER_SUB_STEREO;
			msp->audmode = V4L2_TUNER_MODE_STEREO;
			msp3400c_set_scart(client,scart,0);
			msp3400c_write(client,I2C_MSP3400C_DFP,0x000d,0x1900);
			if (msp->opmode != OPMODE_SIMPLER)
				msp3400c_setstereo(client, msp->audmode);
		}
		msp_wake_thread(client);
		break;

	case AUDC_SET_RADIO:
		msp3400_dbg("AUDC_SET_RADIO\n");
		msp->norm = VIDEO_MODE_RADIO;
		msp3400_dbg("switching to radio mode\n");
		msp->watch_stereo = 0;
		switch (msp->opmode) {
		case OPMODE_MANUAL:
			/* set msp3400 to FM radio mode */
			msp3400c_setmode(client,MSP_MODE_FM_RADIO);
			msp3400c_setcarrier(client, MSP_CARRIER(10.7),
					    MSP_CARRIER(10.7));
			msp3400c_setvolume(client, msp->muted, msp->left, msp->right);
			break;
		case OPMODE_SIMPLE:
		case OPMODE_SIMPLER:
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
		msp->dfp_regs[r->reg] = r->value;
		msp3400c_write(client, I2C_MSP3400C_DFP, r->reg, r->value);
		return 0;
	}
	case MSP_GET_DFPREG:
	{
		struct msp_dfpreg *r = arg;

		if (r->reg < 0 || r->reg >= DFP_COUNT)
			return -EINVAL;
		r->value = msp3400c_read(client, I2C_MSP3400C_DFP, r->reg);
		return 0;
	}

	/* --- v4l ioctls --- */
	/* take care: bttv does userspace copying, we'll get a
	   kernel pointer here... */
	case VIDIOCGAUDIO:
	{
		struct video_audio *va = arg;

		msp3400_dbg("VIDIOCGAUDIO\n");
		va->flags |= VIDEO_AUDIO_VOLUME |
			VIDEO_AUDIO_BASS |
			VIDEO_AUDIO_TREBLE |
			VIDEO_AUDIO_MUTABLE;
		if (msp->muted)
			va->flags |= VIDEO_AUDIO_MUTE;

		if (msp->muted)
			va->flags |= VIDEO_AUDIO_MUTE;
		va->volume = MAX(msp->left, msp->right);
		va->balance = (32768 * MIN(msp->left, msp->right)) /
		    (va->volume ? va->volume : 1);
		va->balance = (msp->left < msp->right) ?
		    (65535 - va->balance) : va->balance;
		if (0 == va->volume)
			va->balance = 32768;
		va->bass = msp->bass;
		va->treble = msp->treble;

		msp_any_detect_stereo(client);
		va->mode = mode_v4l2_to_v4l1(msp->rxsubchans);
		break;
	}
	case VIDIOCSAUDIO:
	{
		struct video_audio *va = arg;

		msp3400_dbg("VIDIOCSAUDIO\n");
		msp->muted = (va->flags & VIDEO_AUDIO_MUTE);
		msp->left = (MIN(65536 - va->balance, 32768) *
			     va->volume) / 32768;
		msp->right = (MIN(va->balance, 32768) * va->volume) / 32768;
		msp->bass = va->bass;
		msp->treble = va->treble;
		msp3400_dbg("VIDIOCSAUDIO setting va->volume to %d\n",
			va->volume);
		msp3400_dbg("VIDIOCSAUDIO setting va->balance to %d\n",
			va->balance);
		msp3400_dbg("VIDIOCSAUDIO setting va->flags to %d\n",
			va->flags);
		msp3400_dbg("VIDIOCSAUDIO setting msp->left to %d\n",
			msp->left);
		msp3400_dbg("VIDIOCSAUDIO setting msp->right to %d\n",
			msp->right);
		msp3400_dbg("VIDIOCSAUDIO setting msp->bass to %d\n",
			msp->bass);
		msp3400_dbg("VIDIOCSAUDIO setting msp->treble to %d\n",
			msp->treble);
		msp3400_dbg("VIDIOCSAUDIO setting msp->mode to %d\n",
			msp->mode);
		msp3400c_setvolume(client, msp->muted, msp->left, msp->right);
		msp3400c_setbass(client, msp->bass);
		msp3400c_settreble(client, msp->treble);

		if (va->mode != 0 && msp->norm != VIDEO_MODE_RADIO)
			msp_any_set_audmode(client,mode_v4l1_to_v4l2(va->mode));
		break;
	}

	case VIDIOCSCHAN:
	{
		struct video_channel *vc = arg;

		msp3400_dbg("VIDIOCSCHAN (norm=%d)\n",vc->norm);
		msp->norm = vc->norm;
		msp_wake_thread(client);
		break;
	}

	case VIDIOCSFREQ:
	case VIDIOC_S_FREQUENCY:
	{
		/* new channel -- kick audio carrier scan */
		msp3400_dbg("VIDIOCSFREQ\n");
		msp_wake_thread(client);
		break;
	}

	/* msp34xx specific */
	case MSP_SET_MATRIX:
	{
		struct msp_matrix *mspm = arg;

		msp3400_dbg("MSP_SET_MATRIX\n");
		msp3400c_set_scart(client, mspm->input, mspm->output);
		break;
	}

	/* --- v4l2 ioctls --- */
	case VIDIOC_S_STD:
	{
		v4l2_std_id *id = arg;

		/*FIXME: use V4L2 mode flags on msp3400 instead of V4L1*/
		if (*id & V4L2_STD_PAL) {
			msp->norm=VIDEO_MODE_PAL;
		} else if (*id & V4L2_STD_SECAM) {
			msp->norm=VIDEO_MODE_SECAM;
		} else {
			msp->norm=VIDEO_MODE_NTSC;
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
			strcpy(i->name,"Radio");
			break;
		case AUDIO_EXTERN_1:
			strcpy(i->name,"Extern 1");
			break;
		case AUDIO_EXTERN_2:
			strcpy(i->name,"Extern 2");
			break;
		case AUDIO_TUNER:
			strcpy(i->name,"Television");
			break;
		default:
			return -EINVAL;
		}
		return 0;
	}

	case VIDIOC_G_AUDIO:
	{
		struct v4l2_audio *a = arg;

		memset(a,0,sizeof(*a));

		switch (a->index) {
		case AUDIO_RADIO:
			strcpy(a->name,"Radio");
			break;
		case AUDIO_EXTERN_1:
			strcpy(a->name,"Extern 1");
			break;
		case AUDIO_EXTERN_2:
			strcpy(a->name,"Extern 2");
			break;
		case AUDIO_TUNER:
			strcpy(a->name,"Television");
			break;
		default:
			return -EINVAL;
		}

		msp_any_detect_stereo(client);
		if (msp->audmode == V4L2_TUNER_MODE_STEREO) {
			a->capability=V4L2_AUDCAP_STEREO;
		}

		break;
	}
	case VIDIOC_S_AUDIO:
	{
		struct v4l2_audio *sarg = arg;

		switch (sarg->index) {
		case AUDIO_RADIO:
			/* Hauppauge uses IN2 for the radio */
			msp->mode   = MSP_MODE_FM_RADIO;
			scart       = SCART_IN2;
			break;
		case AUDIO_EXTERN_1:
			/* IN1 is often used for external input ... */
			msp->mode   = MSP_MODE_EXTERN;
			scart       = SCART_IN1;
			break;
		case AUDIO_EXTERN_2:
			/* ... sometimes it is IN2 through ;) */
			msp->mode   = MSP_MODE_EXTERN;
			scart       = SCART_IN2;
			break;
		case AUDIO_TUNER:
			msp->mode   = -1;
			break;
		}
		if (scart) {
			msp->rxsubchans = V4L2_TUNER_SUB_STEREO;
			msp->audmode = V4L2_TUNER_MODE_STEREO;
			msp3400c_set_scart(client,scart,0);
			msp3400c_write(client,I2C_MSP3400C_DFP,0x000d,0x1900);
		}
		if (sarg->capability==V4L2_AUDCAP_STEREO) {
			msp->audmode = V4L2_TUNER_MODE_STEREO;
		} else {
			msp->audmode &= ~V4L2_TUNER_MODE_STEREO;
		}
		msp_any_set_audmode(client, msp->audmode);
		msp_wake_thread(client);
		break;
	}
	case VIDIOC_G_TUNER:
	{
		struct v4l2_tuner *vt = arg;

		msp_any_detect_stereo(client);
		vt->audmode    = msp->audmode;
		vt->rxsubchans = msp->rxsubchans;
		vt->capability = V4L2_TUNER_CAP_STEREO |
			V4L2_TUNER_CAP_LANG1|
			V4L2_TUNER_CAP_LANG2;
		break;
	}
	case VIDIOC_S_TUNER:
	{
		struct v4l2_tuner *vt=(struct v4l2_tuner *)arg;

		/* only set audmode */
		if (vt->audmode != -1 && vt->audmode != 0)
			msp_any_set_audmode(client, vt->audmode);
		break;
	}

	case VIDIOC_G_AUDOUT:
	{
		struct v4l2_audioout *a=(struct v4l2_audioout *)arg;
		int idx=a->index;

		memset(a,0,sizeof(*a));

		switch (idx) {
		case 0:
			strcpy(a->name,"Scart1 Out");
			break;
		case 1:
			strcpy(a->name,"Scart2 Out");
			break;
		case 2:
			strcpy(a->name,"I2S Out");
			break;
		default:
			return -EINVAL;
		}
		break;

	}
	case VIDIOC_S_AUDOUT:
	{
		struct v4l2_audioout *a=(struct v4l2_audioout *)arg;

		if (a->index<0||a->index>2)
			return -EINVAL;

		if (a->index==2) {
			if (a->mode == V4L2_AUDMODE_32BITS)
				msp->i2s_mode=1;
			else
				msp->i2s_mode=0;
		}
		msp3400_dbg("Setting audio out on msp34xx to input %i, mode %i\n",
						a->index,msp->i2s_mode);
		msp3400c_set_scart(client,msp->in_scart,a->index+1);

		break;
	}

	default:
		/* nothing */
		break;
	}
	return 0;
}

static int msp_suspend(struct device * dev, pm_message_t state)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	msp3400_dbg("msp34xx: suspend\n");
	msp3400c_reset(client);
	return 0;
}

static int msp_resume(struct device * dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	msp3400_dbg("msp34xx: resume\n");
	msp_wake_thread(client);
	return 0;
}

/* ----------------------------------------------------------------------- */

static int __init msp3400_init_module(void)
{
	return i2c_add_driver(&driver);
}

static void __exit msp3400_cleanup_module(void)
{
	i2c_del_driver(&driver);
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
