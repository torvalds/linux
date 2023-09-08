/*
 * Driver for simple i2c audio chips.
 *
 * Copyright (c) 2000 Gerd Knorr
 * based on code by:
 *   Eric Sandeen (eric_sandeen@bigfoot.com)
 *   Steve VanDeBogart (vandebo@uclink.berkeley.edu)
 *   Greg Alexander (galexand@acm.org)
 *
 * For the TDA9875 part:
 * Copyright (c) 2000 Guillaume Delvit based on Gerd Knorr source
 * and Eric Sandeen
 *
 * Copyright(c) 2005-2008 Mauro Carvalho Chehab
 *	- Some cleanups, code fixes, etc
 *	- Convert it to V4L2 API
 *
 * This code is placed under the terms of the GNU General Public License
 *
 * OPTIONS:
 *   debug - set to 1 if you'd like to see debug messages
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/freezer.h>

#include <media/i2c/tvaudio.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

/* ---------------------------------------------------------------------- */
/* insmod args                                                            */

static int debug;	/* insmod parameter */
module_param(debug, int, 0644);

MODULE_DESCRIPTION("device driver for various i2c TV sound decoder / audiomux chips");
MODULE_AUTHOR("Eric Sandeen, Steve VanDeBogart, Greg Alexander, Gerd Knorr");
MODULE_LICENSE("GPL");

#define UNSET    (-1U)

/* ---------------------------------------------------------------------- */
/* our structs                                                            */

#define MAXREGS 256

struct CHIPSTATE;
typedef int  (*getvalue)(int);
typedef int  (*checkit)(struct CHIPSTATE*);
typedef int  (*initialize)(struct CHIPSTATE*);
typedef int  (*getrxsubchans)(struct CHIPSTATE *);
typedef void (*setaudmode)(struct CHIPSTATE*, int mode);

/* i2c command */
typedef struct AUDIOCMD {
	int             count;             /* # of bytes to send */
	unsigned char   bytes[MAXREGS+1];  /* addr, data, data, ... */
} audiocmd;

/* chip description */
struct CHIPDESC {
	char       *name;             /* chip name         */
	int        addr_lo, addr_hi;  /* i2c address range */
	int        registers;         /* # of registers    */

	int        *insmodopt;
	checkit    checkit;
	initialize initialize;
	int        flags;
#define CHIP_HAS_VOLUME      1
#define CHIP_HAS_BASSTREBLE  2
#define CHIP_HAS_INPUTSEL    4
#define CHIP_NEED_CHECKMODE  8

	/* various i2c command sequences */
	audiocmd   init;

	/* which register has which value */
	int    leftreg, rightreg, treblereg, bassreg;

	/* initialize with (defaults to 65535/32768/32768 */
	int    volinit, trebleinit, bassinit;

	/* functions to convert the values (v4l -> chip) */
	getvalue volfunc, treblefunc, bassfunc;

	/* get/set mode */
	getrxsubchans	getrxsubchans;
	setaudmode	setaudmode;

	/* input switch register + values for v4l inputs */
	int  inputreg;
	int  inputmap[4];
	int  inputmute;
	int  inputmask;
};

/* current state of the chip */
struct CHIPSTATE {
	struct v4l2_subdev sd;
	struct v4l2_ctrl_handler hdl;
	struct {
		/* volume/balance cluster */
		struct v4l2_ctrl *volume;
		struct v4l2_ctrl *balance;
	};

	/* chip-specific description - should point to
	   an entry at CHIPDESC table */
	struct CHIPDESC *desc;

	/* shadow register set */
	audiocmd   shadow;

	/* current settings */
	u16 muted;
	int prevmode;
	int radio;
	int input;

	/* thread */
	struct task_struct   *thread;
	struct timer_list    wt;
	int		     audmode;
};

static inline struct CHIPSTATE *to_state(struct v4l2_subdev *sd)
{
	return container_of(sd, struct CHIPSTATE, sd);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler, struct CHIPSTATE, hdl)->sd;
}


/* ---------------------------------------------------------------------- */
/* i2c I/O functions                                                      */

static int chip_write(struct CHIPSTATE *chip, int subaddr, int val)
{
	struct v4l2_subdev *sd = &chip->sd;
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	unsigned char buffer[2];
	int rc;

	if (subaddr < 0) {
		v4l2_dbg(1, debug, sd, "chip_write: 0x%x\n", val);
		chip->shadow.bytes[1] = val;
		buffer[0] = val;
		rc = i2c_master_send(c, buffer, 1);
		if (rc != 1) {
			v4l2_warn(sd, "I/O error (write 0x%x)\n", val);
			if (rc < 0)
				return rc;
			return -EIO;
		}
	} else {
		if (subaddr + 1 >= ARRAY_SIZE(chip->shadow.bytes)) {
			v4l2_info(sd,
				"Tried to access a non-existent register: %d\n",
				subaddr);
			return -EINVAL;
		}

		v4l2_dbg(1, debug, sd, "chip_write: reg%d=0x%x\n",
			subaddr, val);
		chip->shadow.bytes[subaddr+1] = val;
		buffer[0] = subaddr;
		buffer[1] = val;
		rc = i2c_master_send(c, buffer, 2);
		if (rc != 2) {
			v4l2_warn(sd, "I/O error (write reg%d=0x%x)\n",
				subaddr, val);
			if (rc < 0)
				return rc;
			return -EIO;
		}
	}
	return 0;
}

static int chip_write_masked(struct CHIPSTATE *chip,
			     int subaddr, int val, int mask)
{
	struct v4l2_subdev *sd = &chip->sd;

	if (mask != 0) {
		if (subaddr < 0) {
			val = (chip->shadow.bytes[1] & ~mask) | (val & mask);
		} else {
			if (subaddr + 1 >= ARRAY_SIZE(chip->shadow.bytes)) {
				v4l2_info(sd,
					"Tried to access a non-existent register: %d\n",
					subaddr);
				return -EINVAL;
			}

			val = (chip->shadow.bytes[subaddr+1] & ~mask) | (val & mask);
		}
	}
	return chip_write(chip, subaddr, val);
}

static int chip_read(struct CHIPSTATE *chip)
{
	struct v4l2_subdev *sd = &chip->sd;
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	unsigned char buffer;
	int rc;

	rc = i2c_master_recv(c, &buffer, 1);
	if (rc != 1) {
		v4l2_warn(sd, "I/O error (read)\n");
		if (rc < 0)
			return rc;
		return -EIO;
	}
	v4l2_dbg(1, debug, sd, "chip_read: 0x%x\n", buffer);
	return buffer;
}

static int chip_read2(struct CHIPSTATE *chip, int subaddr)
{
	struct v4l2_subdev *sd = &chip->sd;
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	int rc;
	unsigned char write[1];
	unsigned char read[1];
	struct i2c_msg msgs[2] = {
		{
			.addr = c->addr,
			.len = 1,
			.buf = write
		},
		{
			.addr = c->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = read
		}
	};

	write[0] = subaddr;

	rc = i2c_transfer(c->adapter, msgs, 2);
	if (rc != 2) {
		v4l2_warn(sd, "I/O error (read2)\n");
		if (rc < 0)
			return rc;
		return -EIO;
	}
	v4l2_dbg(1, debug, sd, "chip_read2: reg%d=0x%x\n",
		subaddr, read[0]);
	return read[0];
}

static int chip_cmd(struct CHIPSTATE *chip, char *name, audiocmd *cmd)
{
	struct v4l2_subdev *sd = &chip->sd;
	struct i2c_client *c = v4l2_get_subdevdata(sd);
	int i, rc;

	if (0 == cmd->count)
		return 0;

	if (cmd->count + cmd->bytes[0] - 1 >= ARRAY_SIZE(chip->shadow.bytes)) {
		v4l2_info(sd,
			 "Tried to access a non-existent register range: %d to %d\n",
			 cmd->bytes[0] + 1, cmd->bytes[0] + cmd->count - 1);
		return -EINVAL;
	}

	/* FIXME: it seems that the shadow bytes are wrong below !*/

	/* update our shadow register set; print bytes if (debug > 0) */
	v4l2_dbg(1, debug, sd, "chip_cmd(%s): reg=%d, data:",
		name, cmd->bytes[0]);
	for (i = 1; i < cmd->count; i++) {
		if (debug)
			printk(KERN_CONT " 0x%x", cmd->bytes[i]);
		chip->shadow.bytes[i+cmd->bytes[0]] = cmd->bytes[i];
	}
	if (debug)
		printk(KERN_CONT "\n");

	/* send data to the chip */
	rc = i2c_master_send(c, cmd->bytes, cmd->count);
	if (rc != cmd->count) {
		v4l2_warn(sd, "I/O error (%s)\n", name);
		if (rc < 0)
			return rc;
		return -EIO;
	}
	return 0;
}

/* ---------------------------------------------------------------------- */
/* kernel thread for doing i2c stuff asyncronly
 *   right now it is used only to check the audio mode (mono/stereo/whatever)
 *   some time after switching to another TV channel, then turn on stereo
 *   if available, ...
 */

static void chip_thread_wake(struct timer_list *t)
{
	struct CHIPSTATE *chip = from_timer(chip, t, wt);
	wake_up_process(chip->thread);
}

static int chip_thread(void *data)
{
	struct CHIPSTATE *chip = data;
	struct CHIPDESC  *desc = chip->desc;
	struct v4l2_subdev *sd = &chip->sd;
	int mode, selected;

	v4l2_dbg(1, debug, sd, "thread started\n");
	set_freezable();
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (!kthread_should_stop())
			schedule();
		set_current_state(TASK_RUNNING);
		try_to_freeze();
		if (kthread_should_stop())
			break;
		v4l2_dbg(1, debug, sd, "thread wakeup\n");

		/* don't do anything for radio */
		if (chip->radio)
			continue;

		/* have a look what's going on */
		mode = desc->getrxsubchans(chip);
		if (mode == chip->prevmode)
			continue;

		/* chip detected a new audio mode - set it */
		v4l2_dbg(1, debug, sd, "thread checkmode\n");

		chip->prevmode = mode;

		selected = V4L2_TUNER_MODE_MONO;
		switch (chip->audmode) {
		case V4L2_TUNER_MODE_MONO:
			if (mode & V4L2_TUNER_SUB_LANG1)
				selected = V4L2_TUNER_MODE_LANG1;
			break;
		case V4L2_TUNER_MODE_STEREO:
		case V4L2_TUNER_MODE_LANG1:
			if (mode & V4L2_TUNER_SUB_LANG1)
				selected = V4L2_TUNER_MODE_LANG1;
			else if (mode & V4L2_TUNER_SUB_STEREO)
				selected = V4L2_TUNER_MODE_STEREO;
			break;
		case V4L2_TUNER_MODE_LANG2:
			if (mode & V4L2_TUNER_SUB_LANG2)
				selected = V4L2_TUNER_MODE_LANG2;
			else if (mode & V4L2_TUNER_SUB_STEREO)
				selected = V4L2_TUNER_MODE_STEREO;
			break;
		case V4L2_TUNER_MODE_LANG1_LANG2:
			if (mode & V4L2_TUNER_SUB_LANG2)
				selected = V4L2_TUNER_MODE_LANG1_LANG2;
			else if (mode & V4L2_TUNER_SUB_STEREO)
				selected = V4L2_TUNER_MODE_STEREO;
		}
		desc->setaudmode(chip, selected);

		/* schedule next check */
		mod_timer(&chip->wt, jiffies+msecs_to_jiffies(2000));
	}

	v4l2_dbg(1, debug, sd, "thread exiting\n");
	return 0;
}

/* ---------------------------------------------------------------------- */
/* audio chip descriptions - defines+functions for tda9840                */

#define TDA9840_SW         0x00
#define TDA9840_LVADJ      0x02
#define TDA9840_STADJ      0x03
#define TDA9840_TEST       0x04

#define TDA9840_MONO       0x10
#define TDA9840_STEREO     0x2a
#define TDA9840_DUALA      0x12
#define TDA9840_DUALB      0x1e
#define TDA9840_DUALAB     0x1a
#define TDA9840_DUALBA     0x16
#define TDA9840_EXTERNAL   0x7a

#define TDA9840_DS_DUAL    0x20 /* Dual sound identified          */
#define TDA9840_ST_STEREO  0x40 /* Stereo sound identified        */
#define TDA9840_PONRES     0x80 /* Power-on reset detected if = 1 */

#define TDA9840_TEST_INT1SN 0x1 /* Integration time 0.5s when set */
#define TDA9840_TEST_INTFU 0x02 /* Disables integrator function */

static int tda9840_getrxsubchans(struct CHIPSTATE *chip)
{
	struct v4l2_subdev *sd = &chip->sd;
	int val, mode;

	mode = V4L2_TUNER_SUB_MONO;

	val = chip_read(chip);
	if (val < 0)
		return mode;

	if (val & TDA9840_DS_DUAL)
		mode |= V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
	if (val & TDA9840_ST_STEREO)
		mode = V4L2_TUNER_SUB_STEREO;

	v4l2_dbg(1, debug, sd,
		"tda9840_getrxsubchans(): raw chip read: %d, return: %d\n",
		val, mode);
	return mode;
}

static void tda9840_setaudmode(struct CHIPSTATE *chip, int mode)
{
	int update = 1;
	int t = chip->shadow.bytes[TDA9840_SW + 1] & ~0x7e;

	switch (mode) {
	case V4L2_TUNER_MODE_MONO:
		t |= TDA9840_MONO;
		break;
	case V4L2_TUNER_MODE_STEREO:
		t |= TDA9840_STEREO;
		break;
	case V4L2_TUNER_MODE_LANG1:
		t |= TDA9840_DUALA;
		break;
	case V4L2_TUNER_MODE_LANG2:
		t |= TDA9840_DUALB;
		break;
	case V4L2_TUNER_MODE_LANG1_LANG2:
		t |= TDA9840_DUALAB;
		break;
	default:
		update = 0;
	}

	if (update)
		chip_write(chip, TDA9840_SW, t);
}

static int tda9840_checkit(struct CHIPSTATE *chip)
{
	int rc;

	rc = chip_read(chip);
	if (rc < 0)
		return 0;


	/* lower 5 bits should be 0 */
	return ((rc & 0x1f) == 0) ? 1 : 0;
}

/* ---------------------------------------------------------------------- */
/* audio chip descriptions - defines+functions for tda985x                */

/* subaddresses for TDA9855 */
#define TDA9855_VR	0x00 /* Volume, right */
#define TDA9855_VL	0x01 /* Volume, left */
#define TDA9855_BA	0x02 /* Bass */
#define TDA9855_TR	0x03 /* Treble */
#define TDA9855_SW	0x04 /* Subwoofer - not connected on DTV2000 */

/* subaddresses for TDA9850 */
#define TDA9850_C4	0x04 /* Control 1 for TDA9850 */

/* subaddesses for both chips */
#define TDA985x_C5	0x05 /* Control 2 for TDA9850, Control 1 for TDA9855 */
#define TDA985x_C6	0x06 /* Control 3 for TDA9850, Control 2 for TDA9855 */
#define TDA985x_C7	0x07 /* Control 4 for TDA9850, Control 3 for TDA9855 */
#define TDA985x_A1	0x08 /* Alignment 1 for both chips */
#define TDA985x_A2	0x09 /* Alignment 2 for both chips */
#define TDA985x_A3	0x0a /* Alignment 3 for both chips */

/* Masks for bits in TDA9855 subaddresses */
/* 0x00 - VR in TDA9855 */
/* 0x01 - VL in TDA9855 */
/* lower 7 bits control gain from -71dB (0x28) to 16dB (0x7f)
 * in 1dB steps - mute is 0x27 */


/* 0x02 - BA in TDA9855 */
/* lower 5 bits control bass gain from -12dB (0x06) to 16.5dB (0x19)
 * in .5dB steps - 0 is 0x0E */


/* 0x03 - TR in TDA9855 */
/* 4 bits << 1 control treble gain from -12dB (0x3) to 12dB (0xb)
 * in 3dB steps - 0 is 0x7 */

/* Masks for bits in both chips' subaddresses */
/* 0x04 - SW in TDA9855, C4/Control 1 in TDA9850 */
/* Unique to TDA9855: */
/* 4 bits << 2 control subwoofer/surround gain from -14db (0x1) to 14db (0xf)
 * in 3dB steps - mute is 0x0 */

/* Unique to TDA9850: */
/* lower 4 bits control stereo noise threshold, over which stereo turns off
 * set to values of 0x00 through 0x0f for Ster1 through Ster16 */


/* 0x05 - C5 - Control 1 in TDA9855 , Control 2 in TDA9850*/
/* Unique to TDA9855: */
#define TDA9855_MUTE	1<<7 /* GMU, Mute at outputs */
#define TDA9855_AVL	1<<6 /* AVL, Automatic Volume Level */
#define TDA9855_LOUD	1<<5 /* Loudness, 1==off */
#define TDA9855_SUR	1<<3 /* Surround / Subwoofer 1==.5(L-R) 0==.5(L+R) */
			     /* Bits 0 to 3 select various combinations
			      * of line in and line out, only the
			      * interesting ones are defined */
#define TDA9855_EXT	1<<2 /* Selects inputs LIR and LIL.  Pins 41 & 12 */
#define TDA9855_INT	0    /* Selects inputs LOR and LOL.  (internal) */

/* Unique to TDA9850:  */
/* lower 4 bits control SAP noise threshold, over which SAP turns off
 * set to values of 0x00 through 0x0f for SAP1 through SAP16 */


/* 0x06 - C6 - Control 2 in TDA9855, Control 3 in TDA9850 */
/* Common to TDA9855 and TDA9850: */
#define TDA985x_SAP	3<<6 /* Selects SAP output, mute if not received */
#define TDA985x_MONOSAP	2<<6 /* Selects Mono on left, SAP on right */
#define TDA985x_STEREO	1<<6 /* Selects Stereo output, mono if not received */
#define TDA985x_MONO	0    /* Forces Mono output */
#define TDA985x_LMU	1<<3 /* Mute (LOR/LOL for 9855, OUTL/OUTR for 9850) */

/* Unique to TDA9855: */
#define TDA9855_TZCM	1<<5 /* If set, don't mute till zero crossing */
#define TDA9855_VZCM	1<<4 /* If set, don't change volume till zero crossing*/
#define TDA9855_LINEAR	0    /* Linear Stereo */
#define TDA9855_PSEUDO	1    /* Pseudo Stereo */
#define TDA9855_SPAT_30	2    /* Spatial Stereo, 30% anti-phase crosstalk */
#define TDA9855_SPAT_50	3    /* Spatial Stereo, 52% anti-phase crosstalk */
#define TDA9855_E_MONO	7    /* Forced mono - mono select elseware, so useless*/

/* 0x07 - C7 - Control 3 in TDA9855, Control 4 in TDA9850 */
/* Common to both TDA9855 and TDA9850: */
/* lower 4 bits control input gain from -3.5dB (0x0) to 4dB (0xF)
 * in .5dB steps -  0dB is 0x7 */

/* 0x08, 0x09 - A1 and A2 (read/write) */
/* Common to both TDA9855 and TDA9850: */
/* lower 5 bites are wideband and spectral expander alignment
 * from 0x00 to 0x1f - nominal at 0x0f and 0x10 (read/write) */
#define TDA985x_STP	1<<5 /* Stereo Pilot/detect (read-only) */
#define TDA985x_SAPP	1<<6 /* SAP Pilot/detect (read-only) */
#define TDA985x_STS	1<<7 /* Stereo trigger 1= <35mV 0= <30mV (write-only)*/

/* 0x0a - A3 */
/* Common to both TDA9855 and TDA9850: */
/* lower 3 bits control timing current for alignment: -30% (0x0), -20% (0x1),
 * -10% (0x2), nominal (0x3), +10% (0x6), +20% (0x5), +30% (0x4) */
#define TDA985x_ADJ	1<<7 /* Stereo adjust on/off (wideband and spectral */

static int tda9855_volume(int val) { return val/0x2e8+0x27; }
static int tda9855_bass(int val)   { return val/0xccc+0x06; }
static int tda9855_treble(int val) { return (val/0x1c71+0x3)<<1; }

static int  tda985x_getrxsubchans(struct CHIPSTATE *chip)
{
	int mode, val;

	/* Add mono mode regardless of SAP and stereo */
	/* Allows forced mono */
	mode = V4L2_TUNER_SUB_MONO;
	val = chip_read(chip);
	if (val < 0)
		return mode;

	if (val & TDA985x_STP)
		mode = V4L2_TUNER_SUB_STEREO;
	if (val & TDA985x_SAPP)
		mode |= V4L2_TUNER_SUB_SAP;
	return mode;
}

static void tda985x_setaudmode(struct CHIPSTATE *chip, int mode)
{
	int update = 1;
	int c6 = chip->shadow.bytes[TDA985x_C6+1] & 0x3f;

	switch (mode) {
	case V4L2_TUNER_MODE_MONO:
		c6 |= TDA985x_MONO;
		break;
	case V4L2_TUNER_MODE_STEREO:
	case V4L2_TUNER_MODE_LANG1:
		c6 |= TDA985x_STEREO;
		break;
	case V4L2_TUNER_MODE_SAP:
		c6 |= TDA985x_SAP;
		break;
	case V4L2_TUNER_MODE_LANG1_LANG2:
		c6 |= TDA985x_MONOSAP;
		break;
	default:
		update = 0;
	}
	if (update)
		chip_write(chip,TDA985x_C6,c6);
}


/* ---------------------------------------------------------------------- */
/* audio chip descriptions - defines+functions for tda9873h               */

/* Subaddresses for TDA9873H */

#define TDA9873_SW	0x00 /* Switching                    */
#define TDA9873_AD	0x01 /* Adjust                       */
#define TDA9873_PT	0x02 /* Port                         */

/* Subaddress 0x00: Switching Data
 * B7..B0:
 *
 * B1, B0: Input source selection
 *  0,  0  internal
 *  1,  0  external stereo
 *  0,  1  external mono
 */
#define TDA9873_INP_MASK    3
#define TDA9873_INTERNAL    0
#define TDA9873_EXT_STEREO  2
#define TDA9873_EXT_MONO    1

/*    B3, B2: output signal select
 * B4    : transmission mode
 *  0, 0, 1   Mono
 *  1, 0, 0   Stereo
 *  1, 1, 1   Stereo (reversed channel)
 *  0, 0, 0   Dual AB
 *  0, 0, 1   Dual AA
 *  0, 1, 0   Dual BB
 *  0, 1, 1   Dual BA
 */

#define TDA9873_TR_MASK     (7 << 2)
#define TDA9873_TR_MONO     4
#define TDA9873_TR_STEREO   1 << 4
#define TDA9873_TR_REVERSE  ((1 << 3) | (1 << 2))
#define TDA9873_TR_DUALA    1 << 2
#define TDA9873_TR_DUALB    1 << 3
#define TDA9873_TR_DUALAB   0

/* output level controls
 * B5:  output level switch (0 = reduced gain, 1 = normal gain)
 * B6:  mute                (1 = muted)
 * B7:  auto-mute           (1 = auto-mute enabled)
 */

#define TDA9873_GAIN_NORMAL 1 << 5
#define TDA9873_MUTE        1 << 6
#define TDA9873_AUTOMUTE    1 << 7

/* Subaddress 0x01:  Adjust/standard */

/* Lower 4 bits (C3..C0) control stereo adjustment on R channel (-0.6 - +0.7 dB)
 * Recommended value is +0 dB
 */

#define	TDA9873_STEREO_ADJ	0x06 /* 0dB gain */

/* Bits C6..C4 control FM stantard
 * C6, C5, C4
 *  0,  0,  0   B/G (PAL FM)
 *  0,  0,  1   M
 *  0,  1,  0   D/K(1)
 *  0,  1,  1   D/K(2)
 *  1,  0,  0   D/K(3)
 *  1,  0,  1   I
 */
#define TDA9873_BG		0
#define TDA9873_M       1
#define TDA9873_DK1     2
#define TDA9873_DK2     3
#define TDA9873_DK3     4
#define TDA9873_I       5

/* C7 controls identification response time (1=fast/0=normal)
 */
#define TDA9873_IDR_NORM 0
#define TDA9873_IDR_FAST 1 << 7


/* Subaddress 0x02: Port data */

/* E1, E0   free programmable ports P1/P2
    0,  0   both ports low
    0,  1   P1 high
    1,  0   P2 high
    1,  1   both ports high
*/

#define TDA9873_PORTS    3

/* E2: test port */
#define TDA9873_TST_PORT 1 << 2

/* E5..E3 control mono output channel (together with transmission mode bit B4)
 *
 * E5 E4 E3 B4     OUTM
 *  0  0  0  0     mono
 *  0  0  1  0     DUAL B
 *  0  1  0  1     mono (from stereo decoder)
 */
#define TDA9873_MOUT_MONO   0
#define TDA9873_MOUT_FMONO  0
#define TDA9873_MOUT_DUALA  0
#define TDA9873_MOUT_DUALB  1 << 3
#define TDA9873_MOUT_ST     1 << 4
#define TDA9873_MOUT_EXTM   ((1 << 4) | (1 << 3))
#define TDA9873_MOUT_EXTL   1 << 5
#define TDA9873_MOUT_EXTR   ((1 << 5) | (1 << 3))
#define TDA9873_MOUT_EXTLR  ((1 << 5) | (1 << 4))
#define TDA9873_MOUT_MUTE   ((1 << 5) | (1 << 4) | (1 << 3))

/* Status bits: (chip read) */
#define TDA9873_PONR        0 /* Power-on reset detected if = 1 */
#define TDA9873_STEREO      2 /* Stereo sound is identified     */
#define TDA9873_DUAL        4 /* Dual sound is identified       */

static int tda9873_getrxsubchans(struct CHIPSTATE *chip)
{
	struct v4l2_subdev *sd = &chip->sd;
	int val,mode;

	mode = V4L2_TUNER_SUB_MONO;

	val = chip_read(chip);
	if (val < 0)
		return mode;

	if (val & TDA9873_STEREO)
		mode = V4L2_TUNER_SUB_STEREO;
	if (val & TDA9873_DUAL)
		mode |= V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
	v4l2_dbg(1, debug, sd,
		"tda9873_getrxsubchans(): raw chip read: %d, return: %d\n",
		val, mode);
	return mode;
}

static void tda9873_setaudmode(struct CHIPSTATE *chip, int mode)
{
	struct v4l2_subdev *sd = &chip->sd;
	int sw_data  = chip->shadow.bytes[TDA9873_SW+1] & ~ TDA9873_TR_MASK;
	/*	int adj_data = chip->shadow.bytes[TDA9873_AD+1] ; */

	if ((sw_data & TDA9873_INP_MASK) != TDA9873_INTERNAL) {
		v4l2_dbg(1, debug, sd,
			 "tda9873_setaudmode(): external input\n");
		return;
	}

	v4l2_dbg(1, debug, sd,
		 "tda9873_setaudmode(): chip->shadow.bytes[%d] = %d\n",
		 TDA9873_SW+1, chip->shadow.bytes[TDA9873_SW+1]);
	v4l2_dbg(1, debug, sd, "tda9873_setaudmode(): sw_data  = %d\n",
		 sw_data);

	switch (mode) {
	case V4L2_TUNER_MODE_MONO:
		sw_data |= TDA9873_TR_MONO;
		break;
	case V4L2_TUNER_MODE_STEREO:
		sw_data |= TDA9873_TR_STEREO;
		break;
	case V4L2_TUNER_MODE_LANG1:
		sw_data |= TDA9873_TR_DUALA;
		break;
	case V4L2_TUNER_MODE_LANG2:
		sw_data |= TDA9873_TR_DUALB;
		break;
	case V4L2_TUNER_MODE_LANG1_LANG2:
		sw_data |= TDA9873_TR_DUALAB;
		break;
	default:
		return;
	}

	chip_write(chip, TDA9873_SW, sw_data);
	v4l2_dbg(1, debug, sd,
		"tda9873_setaudmode(): req. mode %d; chip_write: %d\n",
		mode, sw_data);
}

static int tda9873_checkit(struct CHIPSTATE *chip)
{
	int rc;

	rc = chip_read2(chip, 254);
	if (rc < 0)
		return 0;
	return (rc & ~0x1f) == 0x80;
}


/* ---------------------------------------------------------------------- */
/* audio chip description - defines+functions for tda9874h and tda9874a   */
/* Dariusz Kowalewski <darekk@automex.pl>                                 */

/* Subaddresses for TDA9874H and TDA9874A (slave rx) */
#define TDA9874A_AGCGR		0x00	/* AGC gain */
#define TDA9874A_GCONR		0x01	/* general config */
#define TDA9874A_MSR		0x02	/* monitor select */
#define TDA9874A_C1FRA		0x03	/* carrier 1 freq. */
#define TDA9874A_C1FRB		0x04	/* carrier 1 freq. */
#define TDA9874A_C1FRC		0x05	/* carrier 1 freq. */
#define TDA9874A_C2FRA		0x06	/* carrier 2 freq. */
#define TDA9874A_C2FRB		0x07	/* carrier 2 freq. */
#define TDA9874A_C2FRC		0x08	/* carrier 2 freq. */
#define TDA9874A_DCR		0x09	/* demodulator config */
#define TDA9874A_FMER		0x0a	/* FM de-emphasis */
#define TDA9874A_FMMR		0x0b	/* FM dematrix */
#define TDA9874A_C1OLAR		0x0c	/* ch.1 output level adj. */
#define TDA9874A_C2OLAR		0x0d	/* ch.2 output level adj. */
#define TDA9874A_NCONR		0x0e	/* NICAM config */
#define TDA9874A_NOLAR		0x0f	/* NICAM output level adj. */
#define TDA9874A_NLELR		0x10	/* NICAM lower error limit */
#define TDA9874A_NUELR		0x11	/* NICAM upper error limit */
#define TDA9874A_AMCONR		0x12	/* audio mute control */
#define TDA9874A_SDACOSR	0x13	/* stereo DAC output select */
#define TDA9874A_AOSR		0x14	/* analog output select */
#define TDA9874A_DAICONR	0x15	/* digital audio interface config */
#define TDA9874A_I2SOSR		0x16	/* I2S-bus output select */
#define TDA9874A_I2SOLAR	0x17	/* I2S-bus output level adj. */
#define TDA9874A_MDACOSR	0x18	/* mono DAC output select (tda9874a) */
#define TDA9874A_ESP		0xFF	/* easy standard progr. (tda9874a) */

/* Subaddresses for TDA9874H and TDA9874A (slave tx) */
#define TDA9874A_DSR		0x00	/* device status */
#define TDA9874A_NSR		0x01	/* NICAM status */
#define TDA9874A_NECR		0x02	/* NICAM error count */
#define TDA9874A_DR1		0x03	/* add. data LSB */
#define TDA9874A_DR2		0x04	/* add. data MSB */
#define TDA9874A_LLRA		0x05	/* monitor level read-out LSB */
#define TDA9874A_LLRB		0x06	/* monitor level read-out MSB */
#define TDA9874A_SIFLR		0x07	/* SIF level */
#define TDA9874A_TR2		252	/* test reg. 2 */
#define TDA9874A_TR1		253	/* test reg. 1 */
#define TDA9874A_DIC		254	/* device id. code */
#define TDA9874A_SIC		255	/* software id. code */


static int tda9874a_mode = 1;		/* 0: A2, 1: NICAM */
static int tda9874a_GCONR = 0xc0;	/* default config. input pin: SIFSEL=0 */
static int tda9874a_NCONR = 0x01;	/* default NICAM config.: AMSEL=0,AMUTE=1 */
static int tda9874a_ESP = 0x07;		/* default standard: NICAM D/K */
static int tda9874a_dic = -1;		/* device id. code */

/* insmod options for tda9874a */
static unsigned int tda9874a_SIF   = UNSET;
static unsigned int tda9874a_AMSEL = UNSET;
static unsigned int tda9874a_STD   = UNSET;
module_param(tda9874a_SIF, int, 0444);
module_param(tda9874a_AMSEL, int, 0444);
module_param(tda9874a_STD, int, 0444);

/*
 * initialization table for tda9874 decoder:
 *  - carrier 1 freq. registers (3 bytes)
 *  - carrier 2 freq. registers (3 bytes)
 *  - demudulator config register
 *  - FM de-emphasis register (slow identification mode)
 * Note: frequency registers must be written in single i2c transfer.
 */
static struct tda9874a_MODES {
	char *name;
	audiocmd cmd;
} tda9874a_modelist[9] = {
  {	"A2, B/G", /* default */
	{ 9, { TDA9874A_C1FRA, 0x72,0x95,0x55, 0x77,0xA0,0x00, 0x00,0x00 }} },
  {	"A2, M (Korea)",
	{ 9, { TDA9874A_C1FRA, 0x5D,0xC0,0x00, 0x62,0x6A,0xAA, 0x20,0x22 }} },
  {	"A2, D/K (1)",
	{ 9, { TDA9874A_C1FRA, 0x87,0x6A,0xAA, 0x82,0x60,0x00, 0x00,0x00 }} },
  {	"A2, D/K (2)",
	{ 9, { TDA9874A_C1FRA, 0x87,0x6A,0xAA, 0x8C,0x75,0x55, 0x00,0x00 }} },
  {	"A2, D/K (3)",
	{ 9, { TDA9874A_C1FRA, 0x87,0x6A,0xAA, 0x77,0xA0,0x00, 0x00,0x00 }} },
  {	"NICAM, I",
	{ 9, { TDA9874A_C1FRA, 0x7D,0x00,0x00, 0x88,0x8A,0xAA, 0x08,0x33 }} },
  {	"NICAM, B/G",
	{ 9, { TDA9874A_C1FRA, 0x72,0x95,0x55, 0x79,0xEA,0xAA, 0x08,0x33 }} },
  {	"NICAM, D/K",
	{ 9, { TDA9874A_C1FRA, 0x87,0x6A,0xAA, 0x79,0xEA,0xAA, 0x08,0x33 }} },
  {	"NICAM, L",
	{ 9, { TDA9874A_C1FRA, 0x87,0x6A,0xAA, 0x79,0xEA,0xAA, 0x09,0x33 }} }
};

static int tda9874a_setup(struct CHIPSTATE *chip)
{
	struct v4l2_subdev *sd = &chip->sd;

	chip_write(chip, TDA9874A_AGCGR, 0x00); /* 0 dB */
	chip_write(chip, TDA9874A_GCONR, tda9874a_GCONR);
	chip_write(chip, TDA9874A_MSR, (tda9874a_mode) ? 0x03:0x02);
	if(tda9874a_dic == 0x11) {
		chip_write(chip, TDA9874A_FMMR, 0x80);
	} else { /* dic == 0x07 */
		chip_cmd(chip,"tda9874_modelist",&tda9874a_modelist[tda9874a_STD].cmd);
		chip_write(chip, TDA9874A_FMMR, 0x00);
	}
	chip_write(chip, TDA9874A_C1OLAR, 0x00); /* 0 dB */
	chip_write(chip, TDA9874A_C2OLAR, 0x00); /* 0 dB */
	chip_write(chip, TDA9874A_NCONR, tda9874a_NCONR);
	chip_write(chip, TDA9874A_NOLAR, 0x00); /* 0 dB */
	/* Note: If signal quality is poor you may want to change NICAM */
	/* error limit registers (NLELR and NUELR) to some greater values. */
	/* Then the sound would remain stereo, but won't be so clear. */
	chip_write(chip, TDA9874A_NLELR, 0x14); /* default */
	chip_write(chip, TDA9874A_NUELR, 0x50); /* default */

	if(tda9874a_dic == 0x11) {
		chip_write(chip, TDA9874A_AMCONR, 0xf9);
		chip_write(chip, TDA9874A_SDACOSR, (tda9874a_mode) ? 0x81:0x80);
		chip_write(chip, TDA9874A_AOSR, 0x80);
		chip_write(chip, TDA9874A_MDACOSR, (tda9874a_mode) ? 0x82:0x80);
		chip_write(chip, TDA9874A_ESP, tda9874a_ESP);
	} else { /* dic == 0x07 */
		chip_write(chip, TDA9874A_AMCONR, 0xfb);
		chip_write(chip, TDA9874A_SDACOSR, (tda9874a_mode) ? 0x81:0x80);
		chip_write(chip, TDA9874A_AOSR, 0x00); /* or 0x10 */
	}
	v4l2_dbg(1, debug, sd, "tda9874a_setup(): %s [0x%02X].\n",
		tda9874a_modelist[tda9874a_STD].name,tda9874a_STD);
	return 1;
}

static int tda9874a_getrxsubchans(struct CHIPSTATE *chip)
{
	struct v4l2_subdev *sd = &chip->sd;
	int dsr,nsr,mode;
	int necr; /* just for debugging */

	mode = V4L2_TUNER_SUB_MONO;

	dsr = chip_read2(chip, TDA9874A_DSR);
	if (dsr < 0)
		return mode;
	nsr = chip_read2(chip, TDA9874A_NSR);
	if (nsr < 0)
		return mode;
	necr = chip_read2(chip, TDA9874A_NECR);
	if (necr < 0)
		return mode;

	/* need to store dsr/nsr somewhere */
	chip->shadow.bytes[MAXREGS-2] = dsr;
	chip->shadow.bytes[MAXREGS-1] = nsr;

	if(tda9874a_mode) {
		/* Note: DSR.RSSF and DSR.AMSTAT bits are also checked.
		 * If NICAM auto-muting is enabled, DSR.AMSTAT=1 indicates
		 * that sound has (temporarily) switched from NICAM to
		 * mono FM (or AM) on 1st sound carrier due to high NICAM bit
		 * error count. So in fact there is no stereo in this case :-(
		 * But changing the mode to V4L2_TUNER_MODE_MONO would switch
		 * external 4052 multiplexer in audio_hook().
		 */
		if(nsr & 0x02) /* NSR.S/MB=1 */
			mode = V4L2_TUNER_SUB_STEREO;
		if(nsr & 0x01) /* NSR.D/SB=1 */
			mode |= V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
	} else {
		if(dsr & 0x02) /* DSR.IDSTE=1 */
			mode = V4L2_TUNER_SUB_STEREO;
		if(dsr & 0x04) /* DSR.IDDUA=1 */
			mode |= V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
	}

	v4l2_dbg(1, debug, sd,
		 "tda9874a_getrxsubchans(): DSR=0x%X, NSR=0x%X, NECR=0x%X, return: %d.\n",
		 dsr, nsr, necr, mode);
	return mode;
}

static void tda9874a_setaudmode(struct CHIPSTATE *chip, int mode)
{
	struct v4l2_subdev *sd = &chip->sd;

	/* Disable/enable NICAM auto-muting (based on DSR.RSSF status bit). */
	/* If auto-muting is disabled, we can hear a signal of degrading quality. */
	if (tda9874a_mode) {
		if(chip->shadow.bytes[MAXREGS-2] & 0x20) /* DSR.RSSF=1 */
			tda9874a_NCONR &= 0xfe; /* enable */
		else
			tda9874a_NCONR |= 0x01; /* disable */
		chip_write(chip, TDA9874A_NCONR, tda9874a_NCONR);
	}

	/* Note: TDA9874A supports automatic FM dematrixing (FMMR register)
	 * and has auto-select function for audio output (AOSR register).
	 * Old TDA9874H doesn't support these features.
	 * TDA9874A also has additional mono output pin (OUTM), which
	 * on same (all?) tv-cards is not used, anyway (as well as MONOIN).
	 */
	if(tda9874a_dic == 0x11) {
		int aosr = 0x80;
		int mdacosr = (tda9874a_mode) ? 0x82:0x80;

		switch(mode) {
		case V4L2_TUNER_MODE_MONO:
		case V4L2_TUNER_MODE_STEREO:
			break;
		case V4L2_TUNER_MODE_LANG1:
			aosr = 0x80; /* auto-select, dual A/A */
			mdacosr = (tda9874a_mode) ? 0x82:0x80;
			break;
		case V4L2_TUNER_MODE_LANG2:
			aosr = 0xa0; /* auto-select, dual B/B */
			mdacosr = (tda9874a_mode) ? 0x83:0x81;
			break;
		case V4L2_TUNER_MODE_LANG1_LANG2:
			aosr = 0x00; /* always route L to L and R to R */
			mdacosr = (tda9874a_mode) ? 0x82:0x80;
			break;
		default:
			return;
		}
		chip_write(chip, TDA9874A_AOSR, aosr);
		chip_write(chip, TDA9874A_MDACOSR, mdacosr);

		v4l2_dbg(1, debug, sd,
			"tda9874a_setaudmode(): req. mode %d; AOSR=0x%X, MDACOSR=0x%X.\n",
			mode, aosr, mdacosr);

	} else { /* dic == 0x07 */
		int fmmr,aosr;

		switch(mode) {
		case V4L2_TUNER_MODE_MONO:
			fmmr = 0x00; /* mono */
			aosr = 0x10; /* A/A */
			break;
		case V4L2_TUNER_MODE_STEREO:
			if(tda9874a_mode) {
				fmmr = 0x00;
				aosr = 0x00; /* handled by NICAM auto-mute */
			} else {
				fmmr = (tda9874a_ESP == 1) ? 0x05 : 0x04; /* stereo */
				aosr = 0x00;
			}
			break;
		case V4L2_TUNER_MODE_LANG1:
			fmmr = 0x02; /* dual */
			aosr = 0x10; /* dual A/A */
			break;
		case V4L2_TUNER_MODE_LANG2:
			fmmr = 0x02; /* dual */
			aosr = 0x20; /* dual B/B */
			break;
		case V4L2_TUNER_MODE_LANG1_LANG2:
			fmmr = 0x02; /* dual */
			aosr = 0x00; /* dual A/B */
			break;
		default:
			return;
		}
		chip_write(chip, TDA9874A_FMMR, fmmr);
		chip_write(chip, TDA9874A_AOSR, aosr);

		v4l2_dbg(1, debug, sd,
			"tda9874a_setaudmode(): req. mode %d; FMMR=0x%X, AOSR=0x%X.\n",
			mode, fmmr, aosr);
	}
}

static int tda9874a_checkit(struct CHIPSTATE *chip)
{
	struct v4l2_subdev *sd = &chip->sd;
	int dic,sic;	/* device id. and software id. codes */

	dic = chip_read2(chip, TDA9874A_DIC);
	if (dic < 0)
		return 0;
	sic = chip_read2(chip, TDA9874A_SIC);
	if (sic < 0)
		return 0;

	v4l2_dbg(1, debug, sd, "tda9874a_checkit(): DIC=0x%X, SIC=0x%X.\n", dic, sic);

	if((dic == 0x11)||(dic == 0x07)) {
		v4l2_info(sd, "found tda9874%s.\n", (dic == 0x11) ? "a" : "h");
		tda9874a_dic = dic;	/* remember device id. */
		return 1;
	}
	return 0;	/* not found */
}

static int tda9874a_initialize(struct CHIPSTATE *chip)
{
	if (tda9874a_SIF > 2)
		tda9874a_SIF = 1;
	if (tda9874a_STD >= ARRAY_SIZE(tda9874a_modelist))
		tda9874a_STD = 0;
	if(tda9874a_AMSEL > 1)
		tda9874a_AMSEL = 0;

	if(tda9874a_SIF == 1)
		tda9874a_GCONR = 0xc0;	/* sound IF input 1 */
	else
		tda9874a_GCONR = 0xc1;	/* sound IF input 2 */

	tda9874a_ESP = tda9874a_STD;
	tda9874a_mode = (tda9874a_STD < 5) ? 0 : 1;

	if(tda9874a_AMSEL == 0)
		tda9874a_NCONR = 0x01; /* auto-mute: analog mono input */
	else
		tda9874a_NCONR = 0x05; /* auto-mute: 1st carrier FM or AM */

	tda9874a_setup(chip);
	return 0;
}

/* ---------------------------------------------------------------------- */
/* audio chip description - defines+functions for tda9875                 */
/* The TDA9875 is made by Philips Semiconductor
 * http://www.semiconductors.philips.com
 * TDA9875: I2C-bus controlled DSP audio processor, FM demodulator
 *
 */

/* subaddresses for TDA9875 */
#define TDA9875_MUT         0x12  /*General mute  (value --> 0b11001100*/
#define TDA9875_CFG         0x01  /* Config register (value --> 0b00000000 */
#define TDA9875_DACOS       0x13  /*DAC i/o select (ADC) 0b0000100*/
#define TDA9875_LOSR        0x16  /*Line output select regirter 0b0100 0001*/

#define TDA9875_CH1V        0x0c  /*Channel 1 volume (mute)*/
#define TDA9875_CH2V        0x0d  /*Channel 2 volume (mute)*/
#define TDA9875_SC1         0x14  /*SCART 1 in (mono)*/
#define TDA9875_SC2         0x15  /*SCART 2 in (mono)*/

#define TDA9875_ADCIS       0x17  /*ADC input select (mono) 0b0110 000*/
#define TDA9875_AER         0x19  /*Audio effect (AVL+Pseudo) 0b0000 0110*/
#define TDA9875_MCS         0x18  /*Main channel select (DAC) 0b0000100*/
#define TDA9875_MVL         0x1a  /* Main volume gauche */
#define TDA9875_MVR         0x1b  /* Main volume droite */
#define TDA9875_MBA         0x1d  /* Main Basse */
#define TDA9875_MTR         0x1e  /* Main treble */
#define TDA9875_ACS         0x1f  /* Auxiliary channel select (FM) 0b0000000*/
#define TDA9875_AVL         0x20  /* Auxiliary volume gauche */
#define TDA9875_AVR         0x21  /* Auxiliary volume droite */
#define TDA9875_ABA         0x22  /* Auxiliary Basse */
#define TDA9875_ATR         0x23  /* Auxiliary treble */

#define TDA9875_MSR         0x02  /* Monitor select register */
#define TDA9875_C1MSB       0x03  /* Carrier 1 (FM) frequency register MSB */
#define TDA9875_C1MIB       0x04  /* Carrier 1 (FM) frequency register (16-8]b */
#define TDA9875_C1LSB       0x05  /* Carrier 1 (FM) frequency register LSB */
#define TDA9875_C2MSB       0x06  /* Carrier 2 (nicam) frequency register MSB */
#define TDA9875_C2MIB       0x07  /* Carrier 2 (nicam) frequency register (16-8]b */
#define TDA9875_C2LSB       0x08  /* Carrier 2 (nicam) frequency register LSB */
#define TDA9875_DCR         0x09  /* Demodulateur configuration regirter*/
#define TDA9875_DEEM        0x0a  /* FM de-emphasis regirter*/
#define TDA9875_FMAT        0x0b  /* FM Matrix regirter*/

/* values */
#define TDA9875_MUTE_ON	    0xff /* general mute */
#define TDA9875_MUTE_OFF    0xcc /* general no mute */

static int tda9875_initialize(struct CHIPSTATE *chip)
{
	chip_write(chip, TDA9875_CFG, 0xd0); /*reg de config 0 (reset)*/
	chip_write(chip, TDA9875_MSR, 0x03);    /* Monitor 0b00000XXX*/
	chip_write(chip, TDA9875_C1MSB, 0x00);  /*Car1(FM) MSB XMHz*/
	chip_write(chip, TDA9875_C1MIB, 0x00);  /*Car1(FM) MIB XMHz*/
	chip_write(chip, TDA9875_C1LSB, 0x00);  /*Car1(FM) LSB XMHz*/
	chip_write(chip, TDA9875_C2MSB, 0x00);  /*Car2(NICAM) MSB XMHz*/
	chip_write(chip, TDA9875_C2MIB, 0x00);  /*Car2(NICAM) MIB XMHz*/
	chip_write(chip, TDA9875_C2LSB, 0x00);  /*Car2(NICAM) LSB XMHz*/
	chip_write(chip, TDA9875_DCR, 0x00);    /*Demod config 0x00*/
	chip_write(chip, TDA9875_DEEM, 0x44);   /*DE-Emph 0b0100 0100*/
	chip_write(chip, TDA9875_FMAT, 0x00);   /*FM Matrix reg 0x00*/
	chip_write(chip, TDA9875_SC1, 0x00);    /* SCART 1 (SC1)*/
	chip_write(chip, TDA9875_SC2, 0x01);    /* SCART 2 (sc2)*/

	chip_write(chip, TDA9875_CH1V, 0x10);  /* Channel volume 1 mute*/
	chip_write(chip, TDA9875_CH2V, 0x10);  /* Channel volume 2 mute */
	chip_write(chip, TDA9875_DACOS, 0x02); /* sig DAC i/o(in:nicam)*/
	chip_write(chip, TDA9875_ADCIS, 0x6f); /* sig ADC input(in:mono)*/
	chip_write(chip, TDA9875_LOSR, 0x00);  /* line out (in:mono)*/
	chip_write(chip, TDA9875_AER, 0x00);   /*06 Effect (AVL+PSEUDO) */
	chip_write(chip, TDA9875_MCS, 0x44);   /* Main ch select (DAC) */
	chip_write(chip, TDA9875_MVL, 0x03);   /* Vol Main left 10dB */
	chip_write(chip, TDA9875_MVR, 0x03);   /* Vol Main right 10dB*/
	chip_write(chip, TDA9875_MBA, 0x00);   /* Main Bass Main 0dB*/
	chip_write(chip, TDA9875_MTR, 0x00);   /* Main Treble Main 0dB*/
	chip_write(chip, TDA9875_ACS, 0x44);   /* Aux chan select (dac)*/
	chip_write(chip, TDA9875_AVL, 0x00);   /* Vol Aux left 0dB*/
	chip_write(chip, TDA9875_AVR, 0x00);   /* Vol Aux right 0dB*/
	chip_write(chip, TDA9875_ABA, 0x00);   /* Aux Bass Main 0dB*/
	chip_write(chip, TDA9875_ATR, 0x00);   /* Aux Aigus Main 0dB*/

	chip_write(chip, TDA9875_MUT, 0xcc);   /* General mute  */
	return 0;
}

static int tda9875_volume(int val) { return (unsigned char)(val / 602 - 84); }
static int tda9875_bass(int val) { return (unsigned char)(max(-12, val / 2115 - 15)); }
static int tda9875_treble(int val) { return (unsigned char)(val / 2622 - 12); }

/* ----------------------------------------------------------------------- */


/* *********************** *
 * i2c interface functions *
 * *********************** */

static int tda9875_checkit(struct CHIPSTATE *chip)
{
	struct v4l2_subdev *sd = &chip->sd;
	int dic, rev;

	dic = chip_read2(chip, 254);
	if (dic < 0)
		return 0;
	rev = chip_read2(chip, 255);
	if (rev < 0)
		return 0;

	if (dic == 0 || dic == 2) { /* tda9875 and tda9875A */
		v4l2_info(sd, "found tda9875%s rev. %d.\n",
			dic == 0 ? "" : "A", rev);
		return 1;
	}
	return 0;
}

/* ---------------------------------------------------------------------- */
/* audio chip descriptions - defines+functions for tea6420                */

#define TEA6300_VL         0x00  /* volume left */
#define TEA6300_VR         0x01  /* volume right */
#define TEA6300_BA         0x02  /* bass */
#define TEA6300_TR         0x03  /* treble */
#define TEA6300_FA         0x04  /* fader control */
#define TEA6300_S          0x05  /* switch register */
				 /* values for those registers: */
#define TEA6300_S_SA       0x01  /* stereo A input */
#define TEA6300_S_SB       0x02  /* stereo B */
#define TEA6300_S_SC       0x04  /* stereo C */
#define TEA6300_S_GMU      0x80  /* general mute */

#define TEA6320_V          0x00  /* volume (0-5)/loudness off (6)/zero crossing mute(7) */
#define TEA6320_FFR        0x01  /* fader front right (0-5) */
#define TEA6320_FFL        0x02  /* fader front left (0-5) */
#define TEA6320_FRR        0x03  /* fader rear right (0-5) */
#define TEA6320_FRL        0x04  /* fader rear left (0-5) */
#define TEA6320_BA         0x05  /* bass (0-4) */
#define TEA6320_TR         0x06  /* treble (0-4) */
#define TEA6320_S          0x07  /* switch register */
				 /* values for those registers: */
#define TEA6320_S_SA       0x07  /* stereo A input */
#define TEA6320_S_SB       0x06  /* stereo B */
#define TEA6320_S_SC       0x05  /* stereo C */
#define TEA6320_S_SD       0x04  /* stereo D */
#define TEA6320_S_GMU      0x80  /* general mute */

#define TEA6420_S_SA       0x00  /* stereo A input */
#define TEA6420_S_SB       0x01  /* stereo B */
#define TEA6420_S_SC       0x02  /* stereo C */
#define TEA6420_S_SD       0x03  /* stereo D */
#define TEA6420_S_SE       0x04  /* stereo E */
#define TEA6420_S_GMU      0x05  /* general mute */

static int tea6300_shift10(int val) { return val >> 10; }
static int tea6300_shift12(int val) { return val >> 12; }

/* Assumes 16bit input (values 0x3f to 0x0c are unique, values less than */
/* 0x0c mirror those immediately higher) */
static int tea6320_volume(int val) { return (val / (65535/(63-12)) + 12) & 0x3f; }
static int tea6320_shift11(int val) { return val >> 11; }
static int tea6320_initialize(struct CHIPSTATE * chip)
{
	chip_write(chip, TEA6320_FFR, 0x3f);
	chip_write(chip, TEA6320_FFL, 0x3f);
	chip_write(chip, TEA6320_FRR, 0x3f);
	chip_write(chip, TEA6320_FRL, 0x3f);

	return 0;
}


/* ---------------------------------------------------------------------- */
/* audio chip descriptions - defines+functions for tda8425                */

#define TDA8425_VL         0x00  /* volume left */
#define TDA8425_VR         0x01  /* volume right */
#define TDA8425_BA         0x02  /* bass */
#define TDA8425_TR         0x03  /* treble */
#define TDA8425_S1         0x08  /* switch functions */
				 /* values for those registers: */
#define TDA8425_S1_OFF     0xEE  /* audio off (mute on) */
#define TDA8425_S1_CH1     0xCE  /* audio channel 1 (mute off) - "linear stereo" mode */
#define TDA8425_S1_CH2     0xCF  /* audio channel 2 (mute off) - "linear stereo" mode */
#define TDA8425_S1_MU      0x20  /* mute bit */
#define TDA8425_S1_STEREO  0x18  /* stereo bits */
#define TDA8425_S1_STEREO_SPATIAL 0x18 /* spatial stereo */
#define TDA8425_S1_STEREO_LINEAR  0x08 /* linear stereo */
#define TDA8425_S1_STEREO_PSEUDO  0x10 /* pseudo stereo */
#define TDA8425_S1_STEREO_MONO    0x00 /* forced mono */
#define TDA8425_S1_ML      0x06        /* language selector */
#define TDA8425_S1_ML_SOUND_A 0x02     /* sound a */
#define TDA8425_S1_ML_SOUND_B 0x04     /* sound b */
#define TDA8425_S1_ML_STEREO  0x06     /* stereo */
#define TDA8425_S1_IS      0x01        /* channel selector */


static int tda8425_shift10(int val) { return (val >> 10) | 0xc0; }
static int tda8425_shift12(int val) { return (val >> 12) | 0xf0; }

static void tda8425_setaudmode(struct CHIPSTATE *chip, int mode)
{
	int s1 = chip->shadow.bytes[TDA8425_S1+1] & 0xe1;

	switch (mode) {
	case V4L2_TUNER_MODE_LANG1:
		s1 |= TDA8425_S1_ML_SOUND_A;
		s1 |= TDA8425_S1_STEREO_PSEUDO;
		break;
	case V4L2_TUNER_MODE_LANG2:
		s1 |= TDA8425_S1_ML_SOUND_B;
		s1 |= TDA8425_S1_STEREO_PSEUDO;
		break;
	case V4L2_TUNER_MODE_LANG1_LANG2:
		s1 |= TDA8425_S1_ML_STEREO;
		s1 |= TDA8425_S1_STEREO_LINEAR;
		break;
	case V4L2_TUNER_MODE_MONO:
		s1 |= TDA8425_S1_ML_STEREO;
		s1 |= TDA8425_S1_STEREO_MONO;
		break;
	case V4L2_TUNER_MODE_STEREO:
		s1 |= TDA8425_S1_ML_STEREO;
		s1 |= TDA8425_S1_STEREO_SPATIAL;
		break;
	default:
		return;
	}
	chip_write(chip,TDA8425_S1,s1);
}


/* ---------------------------------------------------------------------- */
/* audio chip descriptions - defines+functions for pic16c54 (PV951)       */

/* the registers of 16C54, I2C sub address. */
#define PIC16C54_REG_KEY_CODE     0x01	       /* Not use. */
#define PIC16C54_REG_MISC         0x02

/* bit definition of the RESET register, I2C data. */
#define PIC16C54_MISC_RESET_REMOTE_CTL 0x01 /* bit 0, Reset to receive the key */
					    /*        code of remote controller */
#define PIC16C54_MISC_MTS_MAIN         0x02 /* bit 1 */
#define PIC16C54_MISC_MTS_SAP          0x04 /* bit 2 */
#define PIC16C54_MISC_MTS_BOTH         0x08 /* bit 3 */
#define PIC16C54_MISC_SND_MUTE         0x10 /* bit 4, Mute Audio(Line-in and Tuner) */
#define PIC16C54_MISC_SND_NOTMUTE      0x20 /* bit 5 */
#define PIC16C54_MISC_SWITCH_TUNER     0x40 /* bit 6	, Switch to Line-in */
#define PIC16C54_MISC_SWITCH_LINE      0x80 /* bit 7	, Switch to Tuner */

/* ---------------------------------------------------------------------- */
/* audio chip descriptions - defines+functions for TA8874Z                */

/* write 1st byte */
#define TA8874Z_LED_STE	0x80
#define TA8874Z_LED_BIL	0x40
#define TA8874Z_LED_EXT	0x20
#define TA8874Z_MONO_SET	0x10
#define TA8874Z_MUTE	0x08
#define TA8874Z_F_MONO	0x04
#define TA8874Z_MODE_SUB	0x02
#define TA8874Z_MODE_MAIN	0x01

/* write 2nd byte */
/*#define TA8874Z_TI	0x80  */ /* test mode */
#define TA8874Z_SEPARATION	0x3f
#define TA8874Z_SEPARATION_DEFAULT	0x10

/* read */
#define TA8874Z_B1	0x80
#define TA8874Z_B0	0x40
#define TA8874Z_CHAG_FLAG	0x20

/*
 *        B1 B0
 * mono    L  H
 * stereo  L  L
 * BIL     H  L
 */
static int ta8874z_getrxsubchans(struct CHIPSTATE *chip)
{
	int val, mode;

	mode = V4L2_TUNER_SUB_MONO;

	val = chip_read(chip);
	if (val < 0)
		return mode;

	if (val & TA8874Z_B1){
		mode |= V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
	}else if (!(val & TA8874Z_B0)){
		mode = V4L2_TUNER_SUB_STEREO;
	}
	/* v4l2_dbg(1, debug, &chip->sd,
		 "ta8874z_getrxsubchans(): raw chip read: 0x%02x, return: 0x%02x\n",
		 val, mode); */
	return mode;
}

static audiocmd ta8874z_stereo = { 2, {0, TA8874Z_SEPARATION_DEFAULT}};
static audiocmd ta8874z_mono = {2, { TA8874Z_MONO_SET, TA8874Z_SEPARATION_DEFAULT}};
static audiocmd ta8874z_main = {2, { 0, TA8874Z_SEPARATION_DEFAULT}};
static audiocmd ta8874z_sub = {2, { TA8874Z_MODE_SUB, TA8874Z_SEPARATION_DEFAULT}};
static audiocmd ta8874z_both = {2, { TA8874Z_MODE_MAIN | TA8874Z_MODE_SUB, TA8874Z_SEPARATION_DEFAULT}};

static void ta8874z_setaudmode(struct CHIPSTATE *chip, int mode)
{
	struct v4l2_subdev *sd = &chip->sd;
	int update = 1;
	audiocmd *t = NULL;

	v4l2_dbg(1, debug, sd, "ta8874z_setaudmode(): mode: 0x%02x\n", mode);

	switch(mode){
	case V4L2_TUNER_MODE_MONO:
		t = &ta8874z_mono;
		break;
	case V4L2_TUNER_MODE_STEREO:
		t = &ta8874z_stereo;
		break;
	case V4L2_TUNER_MODE_LANG1:
		t = &ta8874z_main;
		break;
	case V4L2_TUNER_MODE_LANG2:
		t = &ta8874z_sub;
		break;
	case V4L2_TUNER_MODE_LANG1_LANG2:
		t = &ta8874z_both;
		break;
	default:
		update = 0;
	}

	if(update)
		chip_cmd(chip, "TA8874Z", t);
}

static int ta8874z_checkit(struct CHIPSTATE *chip)
{
	int rc;

	rc = chip_read(chip);
	if (rc < 0)
		return rc;

	return ((rc & 0x1f) == 0x1f) ? 1 : 0;
}

/* ---------------------------------------------------------------------- */
/* audio chip descriptions - struct CHIPDESC                              */

/* insmod options to enable/disable individual audio chips */
static int tda8425  = 1;
static int tda9840  = 1;
static int tda9850  = 1;
static int tda9855  = 1;
static int tda9873  = 1;
static int tda9874a = 1;
static int tda9875  = 1;
static int tea6300;	/* default 0 - address clash with msp34xx */
static int tea6320;	/* default 0 - address clash with msp34xx */
static int tea6420  = 1;
static int pic16c54 = 1;
static int ta8874z;	/* default 0 - address clash with tda9840 */

module_param(tda8425, int, 0444);
module_param(tda9840, int, 0444);
module_param(tda9850, int, 0444);
module_param(tda9855, int, 0444);
module_param(tda9873, int, 0444);
module_param(tda9874a, int, 0444);
module_param(tda9875, int, 0444);
module_param(tea6300, int, 0444);
module_param(tea6320, int, 0444);
module_param(tea6420, int, 0444);
module_param(pic16c54, int, 0444);
module_param(ta8874z, int, 0444);

static struct CHIPDESC chiplist[] = {
	{
		.name       = "tda9840",
		.insmodopt  = &tda9840,
		.addr_lo    = I2C_ADDR_TDA9840 >> 1,
		.addr_hi    = I2C_ADDR_TDA9840 >> 1,
		.registers  = 5,
		.flags      = CHIP_NEED_CHECKMODE,

		/* callbacks */
		.checkit    = tda9840_checkit,
		.getrxsubchans = tda9840_getrxsubchans,
		.setaudmode = tda9840_setaudmode,

		.init       = { 2, { TDA9840_TEST, TDA9840_TEST_INT1SN
				/* ,TDA9840_SW, TDA9840_MONO */} }
	},
	{
		.name       = "tda9873h",
		.insmodopt  = &tda9873,
		.addr_lo    = I2C_ADDR_TDA985x_L >> 1,
		.addr_hi    = I2C_ADDR_TDA985x_H >> 1,
		.registers  = 3,
		.flags      = CHIP_HAS_INPUTSEL | CHIP_NEED_CHECKMODE,

		/* callbacks */
		.checkit    = tda9873_checkit,
		.getrxsubchans = tda9873_getrxsubchans,
		.setaudmode = tda9873_setaudmode,

		.init       = { 4, { TDA9873_SW, 0xa4, 0x06, 0x03 } },
		.inputreg   = TDA9873_SW,
		.inputmute  = TDA9873_MUTE | TDA9873_AUTOMUTE,
		.inputmap   = {0xa0, 0xa2, 0xa0, 0xa0},
		.inputmask  = TDA9873_INP_MASK|TDA9873_MUTE|TDA9873_AUTOMUTE,

	},
	{
		.name       = "tda9874h/a",
		.insmodopt  = &tda9874a,
		.addr_lo    = I2C_ADDR_TDA9874 >> 1,
		.addr_hi    = I2C_ADDR_TDA9874 >> 1,
		.flags      = CHIP_NEED_CHECKMODE,

		/* callbacks */
		.initialize = tda9874a_initialize,
		.checkit    = tda9874a_checkit,
		.getrxsubchans = tda9874a_getrxsubchans,
		.setaudmode = tda9874a_setaudmode,
	},
	{
		.name       = "tda9875",
		.insmodopt  = &tda9875,
		.addr_lo    = I2C_ADDR_TDA9875 >> 1,
		.addr_hi    = I2C_ADDR_TDA9875 >> 1,
		.flags      = CHIP_HAS_VOLUME | CHIP_HAS_BASSTREBLE,

		/* callbacks */
		.initialize = tda9875_initialize,
		.checkit    = tda9875_checkit,
		.volfunc    = tda9875_volume,
		.bassfunc   = tda9875_bass,
		.treblefunc = tda9875_treble,
		.leftreg    = TDA9875_MVL,
		.rightreg   = TDA9875_MVR,
		.bassreg    = TDA9875_MBA,
		.treblereg  = TDA9875_MTR,
		.volinit    = 58880,
	},
	{
		.name       = "tda9850",
		.insmodopt  = &tda9850,
		.addr_lo    = I2C_ADDR_TDA985x_L >> 1,
		.addr_hi    = I2C_ADDR_TDA985x_H >> 1,
		.registers  = 11,

		.getrxsubchans = tda985x_getrxsubchans,
		.setaudmode = tda985x_setaudmode,

		.init       = { 8, { TDA9850_C4, 0x08, 0x08, TDA985x_STEREO, 0x07, 0x10, 0x10, 0x03 } }
	},
	{
		.name       = "tda9855",
		.insmodopt  = &tda9855,
		.addr_lo    = I2C_ADDR_TDA985x_L >> 1,
		.addr_hi    = I2C_ADDR_TDA985x_H >> 1,
		.registers  = 11,
		.flags      = CHIP_HAS_VOLUME | CHIP_HAS_BASSTREBLE,

		.leftreg    = TDA9855_VL,
		.rightreg   = TDA9855_VR,
		.bassreg    = TDA9855_BA,
		.treblereg  = TDA9855_TR,

		/* callbacks */
		.volfunc    = tda9855_volume,
		.bassfunc   = tda9855_bass,
		.treblefunc = tda9855_treble,
		.getrxsubchans = tda985x_getrxsubchans,
		.setaudmode = tda985x_setaudmode,

		.init       = { 12, { 0, 0x6f, 0x6f, 0x0e, 0x07<<1, 0x8<<2,
				    TDA9855_MUTE | TDA9855_AVL | TDA9855_LOUD | TDA9855_INT,
				    TDA985x_STEREO | TDA9855_LINEAR | TDA9855_TZCM | TDA9855_VZCM,
				    0x07, 0x10, 0x10, 0x03 }}
	},
	{
		.name       = "tea6300",
		.insmodopt  = &tea6300,
		.addr_lo    = I2C_ADDR_TEA6300 >> 1,
		.addr_hi    = I2C_ADDR_TEA6300 >> 1,
		.registers  = 6,
		.flags      = CHIP_HAS_VOLUME | CHIP_HAS_BASSTREBLE | CHIP_HAS_INPUTSEL,

		.leftreg    = TEA6300_VR,
		.rightreg   = TEA6300_VL,
		.bassreg    = TEA6300_BA,
		.treblereg  = TEA6300_TR,

		/* callbacks */
		.volfunc    = tea6300_shift10,
		.bassfunc   = tea6300_shift12,
		.treblefunc = tea6300_shift12,

		.inputreg   = TEA6300_S,
		.inputmap   = { TEA6300_S_SA, TEA6300_S_SB, TEA6300_S_SC },
		.inputmute  = TEA6300_S_GMU,
	},
	{
		.name       = "tea6320",
		.insmodopt  = &tea6320,
		.addr_lo    = I2C_ADDR_TEA6300 >> 1,
		.addr_hi    = I2C_ADDR_TEA6300 >> 1,
		.registers  = 8,
		.flags      = CHIP_HAS_VOLUME | CHIP_HAS_BASSTREBLE | CHIP_HAS_INPUTSEL,

		.leftreg    = TEA6320_V,
		.rightreg   = TEA6320_V,
		.bassreg    = TEA6320_BA,
		.treblereg  = TEA6320_TR,

		/* callbacks */
		.initialize = tea6320_initialize,
		.volfunc    = tea6320_volume,
		.bassfunc   = tea6320_shift11,
		.treblefunc = tea6320_shift11,

		.inputreg   = TEA6320_S,
		.inputmap   = { TEA6320_S_SA, TEA6420_S_SB, TEA6300_S_SC, TEA6320_S_SD },
		.inputmute  = TEA6300_S_GMU,
	},
	{
		.name       = "tea6420",
		.insmodopt  = &tea6420,
		.addr_lo    = I2C_ADDR_TEA6420 >> 1,
		.addr_hi    = I2C_ADDR_TEA6420 >> 1,
		.registers  = 1,
		.flags      = CHIP_HAS_INPUTSEL,

		.inputreg   = -1,
		.inputmap   = { TEA6420_S_SA, TEA6420_S_SB, TEA6420_S_SC },
		.inputmute  = TEA6420_S_GMU,
		.inputmask  = 0x07,
	},
	{
		.name       = "tda8425",
		.insmodopt  = &tda8425,
		.addr_lo    = I2C_ADDR_TDA8425 >> 1,
		.addr_hi    = I2C_ADDR_TDA8425 >> 1,
		.registers  = 9,
		.flags      = CHIP_HAS_VOLUME | CHIP_HAS_BASSTREBLE | CHIP_HAS_INPUTSEL,

		.leftreg    = TDA8425_VL,
		.rightreg   = TDA8425_VR,
		.bassreg    = TDA8425_BA,
		.treblereg  = TDA8425_TR,

		/* callbacks */
		.volfunc    = tda8425_shift10,
		.bassfunc   = tda8425_shift12,
		.treblefunc = tda8425_shift12,
		.setaudmode = tda8425_setaudmode,

		.inputreg   = TDA8425_S1,
		.inputmap   = { TDA8425_S1_CH1, TDA8425_S1_CH1, TDA8425_S1_CH1 },
		.inputmute  = TDA8425_S1_OFF,

	},
	{
		.name       = "pic16c54 (PV951)",
		.insmodopt  = &pic16c54,
		.addr_lo    = I2C_ADDR_PIC16C54 >> 1,
		.addr_hi    = I2C_ADDR_PIC16C54>> 1,
		.registers  = 2,
		.flags      = CHIP_HAS_INPUTSEL,

		.inputreg   = PIC16C54_REG_MISC,
		.inputmap   = {PIC16C54_MISC_SND_NOTMUTE|PIC16C54_MISC_SWITCH_TUNER,
			     PIC16C54_MISC_SND_NOTMUTE|PIC16C54_MISC_SWITCH_LINE,
			     PIC16C54_MISC_SND_NOTMUTE|PIC16C54_MISC_SWITCH_LINE,
			     PIC16C54_MISC_SND_MUTE},
		.inputmute  = PIC16C54_MISC_SND_MUTE,
	},
	{
		.name       = "ta8874z",
		.checkit    = ta8874z_checkit,
		.insmodopt  = &ta8874z,
		.addr_lo    = I2C_ADDR_TDA9840 >> 1,
		.addr_hi    = I2C_ADDR_TDA9840 >> 1,
		.registers  = 2,

		/* callbacks */
		.getrxsubchans = ta8874z_getrxsubchans,
		.setaudmode = ta8874z_setaudmode,

		.init       = {2, { TA8874Z_MONO_SET, TA8874Z_SEPARATION_DEFAULT}},
	},
	{ .name = NULL } /* EOF */
};


/* ---------------------------------------------------------------------- */

static int tvaudio_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	struct CHIPSTATE *chip = to_state(sd);
	struct CHIPDESC *desc = chip->desc;

	switch (ctrl->id) {
	case V4L2_CID_AUDIO_MUTE:
		chip->muted = ctrl->val;
		if (chip->muted)
			chip_write_masked(chip,desc->inputreg,desc->inputmute,desc->inputmask);
		else
			chip_write_masked(chip,desc->inputreg,
					desc->inputmap[chip->input],desc->inputmask);
		return 0;
	case V4L2_CID_AUDIO_VOLUME: {
		u32 volume, balance;
		u32 left, right;

		volume = chip->volume->val;
		balance = chip->balance->val;
		left = (min(65536U - balance, 32768U) * volume) / 32768U;
		right = (min(balance, 32768U) * volume) / 32768U;

		chip_write(chip, desc->leftreg, desc->volfunc(left));
		chip_write(chip, desc->rightreg, desc->volfunc(right));
		return 0;
	}
	case V4L2_CID_AUDIO_BASS:
		chip_write(chip, desc->bassreg, desc->bassfunc(ctrl->val));
		return 0;
	case V4L2_CID_AUDIO_TREBLE:
		chip_write(chip, desc->treblereg, desc->treblefunc(ctrl->val));
		return 0;
	}
	return -EINVAL;
}


/* ---------------------------------------------------------------------- */
/* video4linux interface                                                  */

static int tvaudio_s_radio(struct v4l2_subdev *sd)
{
	struct CHIPSTATE *chip = to_state(sd);

	chip->radio = 1;
	/* del_timer(&chip->wt); */
	return 0;
}

static int tvaudio_s_routing(struct v4l2_subdev *sd,
			     u32 input, u32 output, u32 config)
{
	struct CHIPSTATE *chip = to_state(sd);
	struct CHIPDESC *desc = chip->desc;

	if (!(desc->flags & CHIP_HAS_INPUTSEL))
		return 0;
	if (input >= 4)
		return -EINVAL;
	/* There are four inputs: tuner, radio, extern and intern. */
	chip->input = input;
	if (chip->muted)
		return 0;
	chip_write_masked(chip, desc->inputreg,
			desc->inputmap[chip->input], desc->inputmask);
	return 0;
}

static int tvaudio_s_tuner(struct v4l2_subdev *sd, const struct v4l2_tuner *vt)
{
	struct CHIPSTATE *chip = to_state(sd);
	struct CHIPDESC *desc = chip->desc;

	if (!desc->setaudmode)
		return 0;
	if (chip->radio)
		return 0;

	switch (vt->audmode) {
	case V4L2_TUNER_MODE_MONO:
	case V4L2_TUNER_MODE_STEREO:
	case V4L2_TUNER_MODE_LANG1:
	case V4L2_TUNER_MODE_LANG2:
	case V4L2_TUNER_MODE_LANG1_LANG2:
		break;
	default:
		return -EINVAL;
	}
	chip->audmode = vt->audmode;

	if (chip->thread)
		wake_up_process(chip->thread);
	else
		desc->setaudmode(chip, vt->audmode);

	return 0;
}

static int tvaudio_g_tuner(struct v4l2_subdev *sd, struct v4l2_tuner *vt)
{
	struct CHIPSTATE *chip = to_state(sd);
	struct CHIPDESC *desc = chip->desc;

	if (!desc->getrxsubchans)
		return 0;
	if (chip->radio)
		return 0;

	vt->audmode = chip->audmode;
	vt->rxsubchans = desc->getrxsubchans(chip);
	vt->capability |= V4L2_TUNER_CAP_STEREO |
		V4L2_TUNER_CAP_LANG1 | V4L2_TUNER_CAP_LANG2;

	return 0;
}

static int tvaudio_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct CHIPSTATE *chip = to_state(sd);

	chip->radio = 0;
	return 0;
}

static int tvaudio_s_frequency(struct v4l2_subdev *sd, const struct v4l2_frequency *freq)
{
	struct CHIPSTATE *chip = to_state(sd);
	struct CHIPDESC *desc = chip->desc;

	/* For chips that provide getrxsubchans and setaudmode, and doesn't
	   automatically follows the stereo carrier, a kthread is
	   created to set the audio standard. In this case, when then
	   the video channel is changed, tvaudio starts on MONO mode.
	   After waiting for 2 seconds, the kernel thread is called,
	   to follow whatever audio standard is pointed by the
	   audio carrier.
	 */
	if (chip->thread) {
		desc->setaudmode(chip, V4L2_TUNER_MODE_MONO);
		chip->prevmode = -1; /* reset previous mode */
		mod_timer(&chip->wt, jiffies+msecs_to_jiffies(2000));
	}
	return 0;
}

static int tvaudio_log_status(struct v4l2_subdev *sd)
{
	struct CHIPSTATE *chip = to_state(sd);
	struct CHIPDESC *desc = chip->desc;

	v4l2_info(sd, "Chip: %s\n", desc->name);
	v4l2_ctrl_handler_log_status(&chip->hdl, sd->name);
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct v4l2_ctrl_ops tvaudio_ctrl_ops = {
	.s_ctrl = tvaudio_s_ctrl,
};

static const struct v4l2_subdev_core_ops tvaudio_core_ops = {
	.log_status = tvaudio_log_status,
};

static const struct v4l2_subdev_tuner_ops tvaudio_tuner_ops = {
	.s_radio = tvaudio_s_radio,
	.s_frequency = tvaudio_s_frequency,
	.s_tuner = tvaudio_s_tuner,
	.g_tuner = tvaudio_g_tuner,
};

static const struct v4l2_subdev_audio_ops tvaudio_audio_ops = {
	.s_routing = tvaudio_s_routing,
};

static const struct v4l2_subdev_video_ops tvaudio_video_ops = {
	.s_std = tvaudio_s_std,
};

static const struct v4l2_subdev_ops tvaudio_ops = {
	.core = &tvaudio_core_ops,
	.tuner = &tvaudio_tuner_ops,
	.audio = &tvaudio_audio_ops,
	.video = &tvaudio_video_ops,
};

/* ----------------------------------------------------------------------- */


/* i2c registration                                                       */

static int tvaudio_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct CHIPSTATE *chip;
	struct CHIPDESC  *desc;
	struct v4l2_subdev *sd;

	if (debug) {
		printk(KERN_INFO "tvaudio: TV audio decoder + audio/video mux driver\n");
		printk(KERN_INFO "tvaudio: known chips: ");
		for (desc = chiplist; desc->name != NULL; desc++)
			printk(KERN_CONT "%s%s",
			       (desc == chiplist) ? "" : ", ", desc->name);
		printk(KERN_CONT "\n");
	}

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	sd = &chip->sd;
	v4l2_i2c_subdev_init(sd, client, &tvaudio_ops);

	/* find description for the chip */
	v4l2_dbg(1, debug, sd, "chip found @ 0x%x\n", client->addr<<1);
	for (desc = chiplist; desc->name != NULL; desc++) {
		if (0 == *(desc->insmodopt))
			continue;
		if (client->addr < desc->addr_lo ||
		    client->addr > desc->addr_hi)
			continue;
		if (desc->checkit && !desc->checkit(chip))
			continue;
		break;
	}
	if (desc->name == NULL) {
		v4l2_dbg(1, debug, sd, "no matching chip description found\n");
		return -EIO;
	}
	v4l2_info(sd, "%s found @ 0x%x (%s)\n", desc->name, client->addr<<1, client->adapter->name);
	if (desc->flags) {
		v4l2_dbg(1, debug, sd, "matches:%s%s%s.\n",
			(desc->flags & CHIP_HAS_VOLUME)     ? " volume"      : "",
			(desc->flags & CHIP_HAS_BASSTREBLE) ? " bass/treble" : "",
			(desc->flags & CHIP_HAS_INPUTSEL)   ? " audiomux"    : "");
	}

	/* fill required data structures */
	if (!id)
		strscpy(client->name, desc->name, I2C_NAME_SIZE);
	chip->desc = desc;
	chip->shadow.count = desc->registers+1;
	chip->prevmode = -1;
	chip->audmode = V4L2_TUNER_MODE_LANG1;

	/* initialization  */
	if (desc->initialize != NULL)
		desc->initialize(chip);
	else
		chip_cmd(chip, "init", &desc->init);

	v4l2_ctrl_handler_init(&chip->hdl, 5);
	if (desc->flags & CHIP_HAS_INPUTSEL)
		v4l2_ctrl_new_std(&chip->hdl, &tvaudio_ctrl_ops,
			V4L2_CID_AUDIO_MUTE, 0, 1, 1, 0);
	if (desc->flags & CHIP_HAS_VOLUME) {
		if (!desc->volfunc) {
			/* This shouldn't be happen. Warn user, but keep working
			   without volume controls
			 */
			v4l2_info(sd, "volume callback undefined!\n");
			desc->flags &= ~CHIP_HAS_VOLUME;
		} else {
			chip->volume = v4l2_ctrl_new_std(&chip->hdl,
				&tvaudio_ctrl_ops, V4L2_CID_AUDIO_VOLUME,
				0, 65535, 65535 / 100,
				desc->volinit ? desc->volinit : 65535);
			chip->balance = v4l2_ctrl_new_std(&chip->hdl,
				&tvaudio_ctrl_ops, V4L2_CID_AUDIO_BALANCE,
				0, 65535, 65535 / 100, 32768);
			v4l2_ctrl_cluster(2, &chip->volume);
		}
	}
	if (desc->flags & CHIP_HAS_BASSTREBLE) {
		if (!desc->bassfunc || !desc->treblefunc) {
			/* This shouldn't be happen. Warn user, but keep working
			   without bass/treble controls
			 */
			v4l2_info(sd, "bass/treble callbacks undefined!\n");
			desc->flags &= ~CHIP_HAS_BASSTREBLE;
		} else {
			v4l2_ctrl_new_std(&chip->hdl,
				&tvaudio_ctrl_ops, V4L2_CID_AUDIO_BASS,
				0, 65535, 65535 / 100,
				desc->bassinit ? desc->bassinit : 32768);
			v4l2_ctrl_new_std(&chip->hdl,
				&tvaudio_ctrl_ops, V4L2_CID_AUDIO_TREBLE,
				0, 65535, 65535 / 100,
				desc->trebleinit ? desc->trebleinit : 32768);
		}
	}

	sd->ctrl_handler = &chip->hdl;
	if (chip->hdl.error) {
		int err = chip->hdl.error;

		v4l2_ctrl_handler_free(&chip->hdl);
		return err;
	}
	/* set controls to the default values */
	v4l2_ctrl_handler_setup(&chip->hdl);

	chip->thread = NULL;
	timer_setup(&chip->wt, chip_thread_wake, 0);
	if (desc->flags & CHIP_NEED_CHECKMODE) {
		if (!desc->getrxsubchans || !desc->setaudmode) {
			/* This shouldn't be happen. Warn user, but keep working
			   without kthread
			 */
			v4l2_info(sd, "set/get mode callbacks undefined!\n");
			return 0;
		}
		/* start async thread */
		chip->thread = kthread_run(chip_thread, chip, "%s",
					   client->name);
		if (IS_ERR(chip->thread)) {
			v4l2_warn(sd, "failed to create kthread\n");
			chip->thread = NULL;
		}
	}
	return 0;
}

static void tvaudio_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct CHIPSTATE *chip = to_state(sd);

	del_timer_sync(&chip->wt);
	if (chip->thread) {
		/* shutdown async thread */
		kthread_stop(chip->thread);
		chip->thread = NULL;
	}

	v4l2_device_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&chip->hdl);
}

/* This driver supports many devices and the idea is to let the driver
   detect which device is present. So rather than listing all supported
   devices here, we pretend to support a single, fake device type. */
static const struct i2c_device_id tvaudio_id[] = {
	{ "tvaudio", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tvaudio_id);

static struct i2c_driver tvaudio_driver = {
	.driver = {
		.name	= "tvaudio",
	},
	.probe_new	= tvaudio_probe,
	.remove		= tvaudio_remove,
	.id_table	= tvaudio_id,
};

module_i2c_driver(tvaudio_driver);
