/*
 * experimental driver for simple i2c audio chips.
 *
 * Copyright (c) 2000 Gerd Knorr
 * based on code by:
 *   Eric Sandeen (eric_sandeen@bigfoot.com)
 *   Steve VanDeBogart (vandebo@uclink.berkeley.edu)
 *   Greg Alexander (galexand@acm.org)
 *
 * This code is placed under the terms of the GNU General Public License
 *
 * OPTIONS:
 *   debug - set to 1 if you'd like to see debug messages
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
#include <linux/videodev.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/init.h>
#include <linux/smp_lock.h>

#include <media/audiochip.h>

#include "tvaudio.h"

/* ---------------------------------------------------------------------- */
/* insmod args                                                            */

static int debug = 0;	/* insmod parameter */
module_param(debug, int, 0644);

MODULE_DESCRIPTION("device driver for various i2c TV sound decoder / audiomux chips");
MODULE_AUTHOR("Eric Sandeen, Steve VanDeBogart, Greg Alexander, Gerd Knorr");
MODULE_LICENSE("GPL");

#define UNSET    (-1U)

#define tvaudio_info(fmt, arg...) do {\
	printk(KERN_INFO "tvaudio %d-%04x: " fmt, \
			chip->c.adapter->nr, chip->c.addr , ##arg); } while (0)
#define tvaudio_warn(fmt, arg...) do {\
	printk(KERN_WARNING "tvaudio %d-%04x: " fmt, \
			chip->c.adapter->nr, chip->c.addr , ##arg); } while (0)
#define tvaudio_dbg(fmt, arg...) do {\
	if (debug) \
		printk(KERN_INFO "tvaudio %d-%04x: " fmt, \
			chip->c.adapter->nr, chip->c.addr , ##arg); } while (0)

/* ---------------------------------------------------------------------- */
/* our structs                                                            */

#define MAXREGS 64

struct CHIPSTATE;
typedef int  (*getvalue)(int);
typedef int  (*checkit)(struct CHIPSTATE*);
typedef int  (*initialize)(struct CHIPSTATE*);
typedef int  (*getmode)(struct CHIPSTATE*);
typedef void (*setmode)(struct CHIPSTATE*, int mode);
typedef void (*checkmode)(struct CHIPSTATE*);

/* i2c command */
typedef struct AUDIOCMD {
	int             count;             /* # of bytes to send */
	unsigned char   bytes[MAXREGS+1];  /* addr, data, data, ... */
} audiocmd;

/* chip description */
struct CHIPDESC {
	char       *name;             /* chip name         */
	int        id;                /* ID */
	int        addr_lo, addr_hi;  /* i2c address range */
	int        registers;         /* # of registers    */

	int        *insmodopt;
	checkit    checkit;
	initialize initialize;
	int        flags;
#define CHIP_HAS_VOLUME      1
#define CHIP_HAS_BASSTREBLE  2
#define CHIP_HAS_INPUTSEL    4

	/* various i2c command sequences */
	audiocmd   init;

	/* which register has which value */
	int    leftreg,rightreg,treblereg,bassreg;

	/* initialize with (defaults to 65535/65535/32768/32768 */
	int    leftinit,rightinit,trebleinit,bassinit;

	/* functions to convert the values (v4l -> chip) */
	getvalue volfunc,treblefunc,bassfunc;

	/* get/set mode */
	getmode  getmode;
	setmode  setmode;

	/* check / autoswitch audio after channel switches */
	checkmode  checkmode;

	/* input switch register + values for v4l inputs */
	int  inputreg;
	int  inputmap[8];
	int  inputmute;
	int  inputmask;
};
static struct CHIPDESC chiplist[];

/* current state of the chip */
struct CHIPSTATE {
	struct i2c_client c;

	/* index into CHIPDESC array */
	int type;

	/* shadow register set */
	audiocmd   shadow;

	/* current settings */
	__u16 left,right,treble,bass,mode;
	int prevmode;
	int norm;

	/* thread */
	pid_t                tpid;
	struct completion    texit;
	wait_queue_head_t    wq;
	struct timer_list    wt;
	int                  done;
	int                  watch_stereo;
};

#define VIDEO_MODE_RADIO 16      /* norm magic for radio mode */

/* ---------------------------------------------------------------------- */
/* i2c addresses                                                          */

static unsigned short normal_i2c[] = {
	I2C_TDA8425   >> 1,
	I2C_TEA6300   >> 1,
	I2C_TEA6420   >> 1,
	I2C_TDA9840   >> 1,
	I2C_TDA985x_L >> 1,
	I2C_TDA985x_H >> 1,
	I2C_TDA9874   >> 1,
	I2C_PIC16C54  >> 1,
	I2C_CLIENT_END };
I2C_CLIENT_INSMOD;

static struct i2c_driver driver;
static struct i2c_client client_template;


/* ---------------------------------------------------------------------- */
/* i2c I/O functions                                                      */

static int chip_write(struct CHIPSTATE *chip, int subaddr, int val)
{
	unsigned char buffer[2];

	if (-1 == subaddr) {
		tvaudio_dbg("%s: chip_write: 0x%x\n",
			chip->c.name, val);
		chip->shadow.bytes[1] = val;
		buffer[0] = val;
		if (1 != i2c_master_send(&chip->c,buffer,1)) {
			tvaudio_warn("%s: I/O error (write 0x%x)\n",
				chip->c.name, val);
			return -1;
		}
	} else {
		tvaudio_dbg("%s: chip_write: reg%d=0x%x\n",
			chip->c.name, subaddr, val);
		chip->shadow.bytes[subaddr+1] = val;
		buffer[0] = subaddr;
		buffer[1] = val;
		if (2 != i2c_master_send(&chip->c,buffer,2)) {
			tvaudio_warn("%s: I/O error (write reg%d=0x%x)\n",
						chip->c.name, subaddr, val);
			return -1;
		}
	}
	return 0;
}

static int chip_write_masked(struct CHIPSTATE *chip, int subaddr, int val, int mask)
{
	if (mask != 0) {
		if (-1 == subaddr) {
			val = (chip->shadow.bytes[1] & ~mask) | (val & mask);
		} else {
			val = (chip->shadow.bytes[subaddr+1] & ~mask) | (val & mask);
		}
	}
	return chip_write(chip, subaddr, val);
}

static int chip_read(struct CHIPSTATE *chip)
{
	unsigned char buffer;

	if (1 != i2c_master_recv(&chip->c,&buffer,1)) {
		tvaudio_warn("%s: I/O error (read)\n",
		chip->c.name);
		return -1;
	}
	tvaudio_dbg("%s: chip_read: 0x%x\n",chip->c.name,buffer);
	return buffer;
}

static int chip_read2(struct CHIPSTATE *chip, int subaddr)
{
	unsigned char write[1];
	unsigned char read[1];
	struct i2c_msg msgs[2] = {
		{ chip->c.addr, 0,        1, write },
		{ chip->c.addr, I2C_M_RD, 1, read  }
	};
	write[0] = subaddr;

	if (2 != i2c_transfer(chip->c.adapter,msgs,2)) {
		tvaudio_warn("%s: I/O error (read2)\n", chip->c.name);
		return -1;
	}
	tvaudio_dbg("%s: chip_read2: reg%d=0x%x\n",
			chip->c.name,subaddr,read[0]);
	return read[0];
}

static int chip_cmd(struct CHIPSTATE *chip, char *name, audiocmd *cmd)
{
	int i;

	if (0 == cmd->count)
		return 0;

	/* update our shadow register set; print bytes if (debug > 0) */
	tvaudio_dbg("%s: chip_cmd(%s): reg=%d, data:",
		chip->c.name,name,cmd->bytes[0]);
	for (i = 1; i < cmd->count; i++) {
		if (debug)
			printk(" 0x%x",cmd->bytes[i]);
		chip->shadow.bytes[i+cmd->bytes[0]] = cmd->bytes[i];
	}
	if (debug)
		printk("\n");

	/* send data to the chip */
	if (cmd->count != i2c_master_send(&chip->c,cmd->bytes,cmd->count)) {
		tvaudio_warn("%s: I/O error (%s)\n", chip->c.name, name);
		return -1;
	}
	return 0;
}

/* ---------------------------------------------------------------------- */
/* kernel thread for doing i2c stuff asyncronly
 *   right now it is used only to check the audio mode (mono/stereo/whatever)
 *   some time after switching to another TV channel, then turn on stereo
 *   if available, ...
 */

static void chip_thread_wake(unsigned long data)
{
	struct CHIPSTATE *chip = (struct CHIPSTATE*)data;
	wake_up_interruptible(&chip->wq);
}

static int chip_thread(void *data)
{
	DECLARE_WAITQUEUE(wait, current);
	struct CHIPSTATE *chip = data;
	struct CHIPDESC  *desc = chiplist + chip->type;

	daemonize("%s", chip->c.name);
	allow_signal(SIGTERM);
	tvaudio_dbg("%s: thread started\n", chip->c.name);

	for (;;) {
		add_wait_queue(&chip->wq, &wait);
		if (!chip->done) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
		}
		remove_wait_queue(&chip->wq, &wait);
		try_to_freeze();
		if (chip->done || signal_pending(current))
			break;
		tvaudio_dbg("%s: thread wakeup\n", chip->c.name);

		/* don't do anything for radio or if mode != auto */
		if (chip->norm == VIDEO_MODE_RADIO || chip->mode != 0)
			continue;

		/* have a look what's going on */
		desc->checkmode(chip);

		/* schedule next check */
		mod_timer(&chip->wt, jiffies+2*HZ);
	}

	tvaudio_dbg("%s: thread exiting\n", chip->c.name);
	complete_and_exit(&chip->texit, 0);
	return 0;
}

static void generic_checkmode(struct CHIPSTATE *chip)
{
	struct CHIPDESC  *desc = chiplist + chip->type;
	int mode = desc->getmode(chip);

	if (mode == chip->prevmode)
		return;

	tvaudio_dbg("%s: thread checkmode\n", chip->c.name);
	chip->prevmode = mode;

	if (mode & VIDEO_SOUND_STEREO)
		desc->setmode(chip,VIDEO_SOUND_STEREO);
	else if (mode & VIDEO_SOUND_LANG1)
		desc->setmode(chip,VIDEO_SOUND_LANG1);
	else if (mode & VIDEO_SOUND_LANG2)
		desc->setmode(chip,VIDEO_SOUND_LANG2);
	else
		desc->setmode(chip,VIDEO_SOUND_MONO);
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

static int tda9840_getmode(struct CHIPSTATE *chip)
{
	int val, mode;

	val = chip_read(chip);
	mode = VIDEO_SOUND_MONO;
	if (val & TDA9840_DS_DUAL)
		mode |= VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
	if (val & TDA9840_ST_STEREO)
		mode |= VIDEO_SOUND_STEREO;

	tvaudio_dbg ("tda9840_getmode(): raw chip read: %d, return: %d\n",
		val, mode);
	return mode;
}

static void tda9840_setmode(struct CHIPSTATE *chip, int mode)
{
	int update = 1;
	int t = chip->shadow.bytes[TDA9840_SW + 1] & ~0x7e;

	switch (mode) {
	case VIDEO_SOUND_MONO:
		t |= TDA9840_MONO;
		break;
	case VIDEO_SOUND_STEREO:
		t |= TDA9840_STEREO;
		break;
	case VIDEO_SOUND_LANG1:
		t |= TDA9840_DUALA;
		break;
	case VIDEO_SOUND_LANG2:
		t |= TDA9840_DUALB;
		break;
	default:
		update = 0;
	}

	if (update)
		chip_write(chip, TDA9840_SW, t);
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
/* lower 4 bits contol SAP noise threshold, over which SAP turns off
 * set to values of 0x00 through 0x0f for SAP1 through SAP16 */


/* 0x06 - C6 - Control 2 in TDA9855, Control 3 in TDA9850 */
/* Common to TDA9855 and TDA9850: */
#define TDA985x_SAP	3<<6 /* Selects SAP output, mute if not received */
#define TDA985x_STEREO	1<<6 /* Selects Stereo ouput, mono if not received */
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

static int  tda985x_getmode(struct CHIPSTATE *chip)
{
	int mode;

	mode = ((TDA985x_STP | TDA985x_SAPP) &
		chip_read(chip)) >> 4;
	/* Add mono mode regardless of SAP and stereo */
	/* Allows forced mono */
	return mode | VIDEO_SOUND_MONO;
}

static void tda985x_setmode(struct CHIPSTATE *chip, int mode)
{
	int update = 1;
	int c6 = chip->shadow.bytes[TDA985x_C6+1] & 0x3f;

	switch (mode) {
	case VIDEO_SOUND_MONO:
		c6 |= TDA985x_MONO;
		break;
	case VIDEO_SOUND_STEREO:
		c6 |= TDA985x_STEREO;
		break;
	case VIDEO_SOUND_LANG1:
		c6 |= TDA985x_SAP;
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
#define TDA9873_TR_REVERSE  (1 << 3) & (1 << 2)
#define TDA9873_TR_DUALA    1 << 2
#define TDA9873_TR_DUALB    1 << 3

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
#define TDA9873_MOUT_EXTM   (1 << 4 ) & (1 << 3)
#define TDA9873_MOUT_EXTL   1 << 5
#define TDA9873_MOUT_EXTR   (1 << 5 ) & (1 << 3)
#define TDA9873_MOUT_EXTLR  (1 << 5 ) & (1 << 4)
#define TDA9873_MOUT_MUTE   (1 << 5 ) & (1 << 4) & (1 << 3)

/* Status bits: (chip read) */
#define TDA9873_PONR        0 /* Power-on reset detected if = 1 */
#define TDA9873_STEREO      2 /* Stereo sound is identified     */
#define TDA9873_DUAL        4 /* Dual sound is identified       */

static int tda9873_getmode(struct CHIPSTATE *chip)
{
	int val,mode;

	val = chip_read(chip);
	mode = VIDEO_SOUND_MONO;
	if (val & TDA9873_STEREO)
		mode |= VIDEO_SOUND_STEREO;
	if (val & TDA9873_DUAL)
		mode |= VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
	tvaudio_dbg ("tda9873_getmode(): raw chip read: %d, return: %d\n",
		val, mode);
	return mode;
}

static void tda9873_setmode(struct CHIPSTATE *chip, int mode)
{
	int sw_data  = chip->shadow.bytes[TDA9873_SW+1] & ~ TDA9873_TR_MASK;
	/*	int adj_data = chip->shadow.bytes[TDA9873_AD+1] ; */

	if ((sw_data & TDA9873_INP_MASK) != TDA9873_INTERNAL) {
		tvaudio_dbg("tda9873_setmode(): external input\n");
		return;
	}

	tvaudio_dbg("tda9873_setmode(): chip->shadow.bytes[%d] = %d\n", TDA9873_SW+1, chip->shadow.bytes[TDA9873_SW+1]);
	tvaudio_dbg("tda9873_setmode(): sw_data  = %d\n", sw_data);

	switch (mode) {
	case VIDEO_SOUND_MONO:
		sw_data |= TDA9873_TR_MONO;
		break;
	case VIDEO_SOUND_STEREO:
		sw_data |= TDA9873_TR_STEREO;
		break;
	case VIDEO_SOUND_LANG1:
		sw_data |= TDA9873_TR_DUALA;
		break;
	case VIDEO_SOUND_LANG2:
		sw_data |= TDA9873_TR_DUALB;
		break;
	default:
		chip->mode = 0;
		return;
	}

	chip_write(chip, TDA9873_SW, sw_data);
	tvaudio_dbg("tda9873_setmode(): req. mode %d; chip_write: %d\n",
		mode, sw_data);
}

static int tda9873_checkit(struct CHIPSTATE *chip)
{
	int rc;

	if (-1 == (rc = chip_read2(chip,254)))
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
  {	"A2, B/G",
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
  {	"NICAM, D/K", /* default */
	{ 9, { TDA9874A_C1FRA, 0x87,0x6A,0xAA, 0x79,0xEA,0xAA, 0x08,0x33 }} },
  {	"NICAM, L",
	{ 9, { TDA9874A_C1FRA, 0x87,0x6A,0xAA, 0x79,0xEA,0xAA, 0x09,0x33 }} }
};

static int tda9874a_setup(struct CHIPSTATE *chip)
{
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
	tvaudio_dbg("tda9874a_setup(): %s [0x%02X].\n",
		tda9874a_modelist[tda9874a_STD].name,tda9874a_STD);
	return 1;
}

static int tda9874a_getmode(struct CHIPSTATE *chip)
{
	int dsr,nsr,mode;
	int necr; /* just for debugging */

	mode = VIDEO_SOUND_MONO;

	if(-1 == (dsr = chip_read2(chip,TDA9874A_DSR)))
		return mode;
	if(-1 == (nsr = chip_read2(chip,TDA9874A_NSR)))
		return mode;
	if(-1 == (necr = chip_read2(chip,TDA9874A_NECR)))
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
		 * But changing the mode to VIDEO_SOUND_MONO would switch
		 * external 4052 multiplexer in audio_hook().
		 */
		if(nsr & 0x02) /* NSR.S/MB=1 */
			mode |= VIDEO_SOUND_STEREO;
		if(nsr & 0x01) /* NSR.D/SB=1 */
			mode |= VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
	} else {
		if(dsr & 0x02) /* DSR.IDSTE=1 */
			mode |= VIDEO_SOUND_STEREO;
		if(dsr & 0x04) /* DSR.IDDUA=1 */
			mode |= VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
	}

	tvaudio_dbg("tda9874a_getmode(): DSR=0x%X, NSR=0x%X, NECR=0x%X, return: %d.\n",
		 dsr, nsr, necr, mode);
	return mode;
}

static void tda9874a_setmode(struct CHIPSTATE *chip, int mode)
{
	/* Disable/enable NICAM auto-muting (based on DSR.RSSF status bit). */
	/* If auto-muting is disabled, we can hear a signal of degrading quality. */
	if(tda9874a_mode) {
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
		case VIDEO_SOUND_MONO:
		case VIDEO_SOUND_STEREO:
			break;
		case VIDEO_SOUND_LANG1:
			aosr = 0x80; /* auto-select, dual A/A */
			mdacosr = (tda9874a_mode) ? 0x82:0x80;
			break;
		case VIDEO_SOUND_LANG2:
			aosr = 0xa0; /* auto-select, dual B/B */
			mdacosr = (tda9874a_mode) ? 0x83:0x81;
			break;
		default:
			chip->mode = 0;
			return;
		}
		chip_write(chip, TDA9874A_AOSR, aosr);
		chip_write(chip, TDA9874A_MDACOSR, mdacosr);

		tvaudio_dbg("tda9874a_setmode(): req. mode %d; AOSR=0x%X, MDACOSR=0x%X.\n",
			mode, aosr, mdacosr);

	} else { /* dic == 0x07 */
		int fmmr,aosr;

		switch(mode) {
		case VIDEO_SOUND_MONO:
			fmmr = 0x00; /* mono */
			aosr = 0x10; /* A/A */
			break;
		case VIDEO_SOUND_STEREO:
			if(tda9874a_mode) {
				fmmr = 0x00;
				aosr = 0x00; /* handled by NICAM auto-mute */
			} else {
				fmmr = (tda9874a_ESP == 1) ? 0x05 : 0x04; /* stereo */
				aosr = 0x00;
			}
			break;
		case VIDEO_SOUND_LANG1:
			fmmr = 0x02; /* dual */
			aosr = 0x10; /* dual A/A */
			break;
		case VIDEO_SOUND_LANG2:
			fmmr = 0x02; /* dual */
			aosr = 0x20; /* dual B/B */
			break;
		default:
			chip->mode = 0;
			return;
		}
		chip_write(chip, TDA9874A_FMMR, fmmr);
		chip_write(chip, TDA9874A_AOSR, aosr);

		tvaudio_dbg("tda9874a_setmode(): req. mode %d; FMMR=0x%X, AOSR=0x%X.\n",
			mode, fmmr, aosr);
	}
}

static int tda9874a_checkit(struct CHIPSTATE *chip)
{
	int dic,sic;	/* device id. and software id. codes */

	if(-1 == (dic = chip_read2(chip,TDA9874A_DIC)))
		return 0;
	if(-1 == (sic = chip_read2(chip,TDA9874A_SIC)))
		return 0;

	tvaudio_dbg("tda9874a_checkit(): DIC=0x%X, SIC=0x%X.\n", dic, sic);

	if((dic == 0x11)||(dic == 0x07)) {
		tvaudio_info("found tda9874%s.\n", (dic == 0x11) ? "a":"h");
		tda9874a_dic = dic;	/* remember device id. */
		return 1;
	}
	return 0;	/* not found */
}

static int tda9874a_initialize(struct CHIPSTATE *chip)
{
	if (tda9874a_SIF > 2)
		tda9874a_SIF = 1;
	if (tda9874a_STD > 8)
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

static int tda8425_initialize(struct CHIPSTATE *chip)
{
	struct CHIPDESC *desc = chiplist + chip->type;
	int inputmap[8] = { /* tuner	*/ TDA8425_S1_CH2, /* radio  */ TDA8425_S1_CH1,
			    /* extern	*/ TDA8425_S1_CH1, /* intern */ TDA8425_S1_OFF,
			    /* off	*/ TDA8425_S1_OFF, /* on     */ TDA8425_S1_CH2};

	if (chip->c.adapter->id == I2C_HW_B_RIVA) {
		memcpy (desc->inputmap, inputmap, sizeof (inputmap));
	}
	return 0;
}

static void tda8425_setmode(struct CHIPSTATE *chip, int mode)
{
	int s1 = chip->shadow.bytes[TDA8425_S1+1] & 0xe1;

	if (mode & VIDEO_SOUND_LANG1) {
		s1 |= TDA8425_S1_ML_SOUND_A;
		s1 |= TDA8425_S1_STEREO_PSEUDO;

	} else if (mode & VIDEO_SOUND_LANG2) {
		s1 |= TDA8425_S1_ML_SOUND_B;
		s1 |= TDA8425_S1_STEREO_PSEUDO;

	} else {
		s1 |= TDA8425_S1_ML_STEREO;

		if (mode & VIDEO_SOUND_MONO)
			s1 |= TDA8425_S1_STEREO_MONO;
		if (mode & VIDEO_SOUND_STEREO)
			s1 |= TDA8425_S1_STEREO_SPATIAL;
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
static int ta8874z_getmode(struct CHIPSTATE *chip)
{
	int val, mode;

	val = chip_read(chip);
	mode = VIDEO_SOUND_MONO;
	if (val & TA8874Z_B1){
		mode |= VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
	}else if (!(val & TA8874Z_B0)){
		mode |= VIDEO_SOUND_STEREO;
	}
	/* tvaudio_dbg ("ta8874z_getmode(): raw chip read: 0x%02x, return: 0x%02x\n", val, mode); */
	return mode;
}

static audiocmd ta8874z_stereo = { 2, {0, TA8874Z_SEPARATION_DEFAULT}};
static audiocmd ta8874z_mono = {2, { TA8874Z_MONO_SET, TA8874Z_SEPARATION_DEFAULT}};
static audiocmd ta8874z_main = {2, { 0, TA8874Z_SEPARATION_DEFAULT}};
static audiocmd ta8874z_sub = {2, { TA8874Z_MODE_SUB, TA8874Z_SEPARATION_DEFAULT}};

static void ta8874z_setmode(struct CHIPSTATE *chip, int mode)
{
	int update = 1;
	audiocmd *t = NULL;
	tvaudio_dbg("ta8874z_setmode(): mode: 0x%02x\n", mode);

	switch(mode){
	case VIDEO_SOUND_MONO:
		t = &ta8874z_mono;
		break;
	case VIDEO_SOUND_STEREO:
		t = &ta8874z_stereo;
		break;
	case VIDEO_SOUND_LANG1:
		t = &ta8874z_main;
		break;
	case VIDEO_SOUND_LANG2:
		t = &ta8874z_sub;
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
static int tea6300  = 0;  /* address clash with msp34xx */
static int tea6320  = 0;  /* address clash with msp34xx */
static int tea6420  = 1;
static int pic16c54 = 1;
static int ta8874z  = 0;  /* address clash with tda9840 */

module_param(tda8425, int, 0444);
module_param(tda9840, int, 0444);
module_param(tda9850, int, 0444);
module_param(tda9855, int, 0444);
module_param(tda9873, int, 0444);
module_param(tda9874a, int, 0444);
module_param(tea6300, int, 0444);
module_param(tea6320, int, 0444);
module_param(tea6420, int, 0444);
module_param(pic16c54, int, 0444);
module_param(ta8874z, int, 0444);

static struct CHIPDESC chiplist[] = {
	{
		.name       = "tda9840",
		.id         = I2C_DRIVERID_TDA9840,
		.insmodopt  = &tda9840,
		.addr_lo    = I2C_TDA9840 >> 1,
		.addr_hi    = I2C_TDA9840 >> 1,
		.registers  = 5,

		.getmode    = tda9840_getmode,
		.setmode    = tda9840_setmode,
		.checkmode  = generic_checkmode,

		.init       = { 2, { TDA9840_TEST, TDA9840_TEST_INT1SN
				/* ,TDA9840_SW, TDA9840_MONO */} }
	},
	{
		.name       = "tda9873h",
		.id         = I2C_DRIVERID_TDA9873,
		.checkit    = tda9873_checkit,
		.insmodopt  = &tda9873,
		.addr_lo    = I2C_TDA985x_L >> 1,
		.addr_hi    = I2C_TDA985x_H >> 1,
		.registers  = 3,
		.flags      = CHIP_HAS_INPUTSEL,

		.getmode    = tda9873_getmode,
		.setmode    = tda9873_setmode,
		.checkmode  = generic_checkmode,

		.init       = { 4, { TDA9873_SW, 0xa4, 0x06, 0x03 } },
		.inputreg   = TDA9873_SW,
		.inputmute  = TDA9873_MUTE | TDA9873_AUTOMUTE,
		.inputmap   = {0xa0, 0xa2, 0xa0, 0xa0, 0xc0},
		.inputmask  = TDA9873_INP_MASK|TDA9873_MUTE|TDA9873_AUTOMUTE,

	},
	{
		.name       = "tda9874h/a",
		.id         = I2C_DRIVERID_TDA9874,
		.checkit    = tda9874a_checkit,
		.initialize = tda9874a_initialize,
		.insmodopt  = &tda9874a,
		.addr_lo    = I2C_TDA9874 >> 1,
		.addr_hi    = I2C_TDA9874 >> 1,

		.getmode    = tda9874a_getmode,
		.setmode    = tda9874a_setmode,
		.checkmode  = generic_checkmode,
	},
	{
		.name       = "tda9850",
		.id         = I2C_DRIVERID_TDA9850,
		.insmodopt  = &tda9850,
		.addr_lo    = I2C_TDA985x_L >> 1,
		.addr_hi    = I2C_TDA985x_H >> 1,
		.registers  = 11,

		.getmode    = tda985x_getmode,
		.setmode    = tda985x_setmode,

		.init       = { 8, { TDA9850_C4, 0x08, 0x08, TDA985x_STEREO, 0x07, 0x10, 0x10, 0x03 } }
	},
	{
		.name       = "tda9855",
		.id         = I2C_DRIVERID_TDA9855,
		.insmodopt  = &tda9855,
		.addr_lo    = I2C_TDA985x_L >> 1,
		.addr_hi    = I2C_TDA985x_H >> 1,
		.registers  = 11,
		.flags      = CHIP_HAS_VOLUME | CHIP_HAS_BASSTREBLE,

		.leftreg    = TDA9855_VL,
		.rightreg   = TDA9855_VR,
		.bassreg    = TDA9855_BA,
		.treblereg  = TDA9855_TR,
		.volfunc    = tda9855_volume,
		.bassfunc   = tda9855_bass,
		.treblefunc = tda9855_treble,

		.getmode    = tda985x_getmode,
		.setmode    = tda985x_setmode,

		.init       = { 12, { 0, 0x6f, 0x6f, 0x0e, 0x07<<1, 0x8<<2,
				    TDA9855_MUTE | TDA9855_AVL | TDA9855_LOUD | TDA9855_INT,
				    TDA985x_STEREO | TDA9855_LINEAR | TDA9855_TZCM | TDA9855_VZCM,
				    0x07, 0x10, 0x10, 0x03 }}
	},
	{
		.name       = "tea6300",
		.id         = I2C_DRIVERID_TEA6300,
		.insmodopt  = &tea6300,
		.addr_lo    = I2C_TEA6300 >> 1,
		.addr_hi    = I2C_TEA6300 >> 1,
		.registers  = 6,
		.flags      = CHIP_HAS_VOLUME | CHIP_HAS_BASSTREBLE | CHIP_HAS_INPUTSEL,

		.leftreg    = TEA6300_VR,
		.rightreg   = TEA6300_VL,
		.bassreg    = TEA6300_BA,
		.treblereg  = TEA6300_TR,
		.volfunc    = tea6300_shift10,
		.bassfunc   = tea6300_shift12,
		.treblefunc = tea6300_shift12,

		.inputreg   = TEA6300_S,
		.inputmap   = { TEA6300_S_SA, TEA6300_S_SB, TEA6300_S_SC },
		.inputmute  = TEA6300_S_GMU,
	},
	{
		.name       = "tea6320",
		.id         = I2C_DRIVERID_TEA6300,
		.initialize = tea6320_initialize,
		.insmodopt  = &tea6320,
		.addr_lo    = I2C_TEA6300 >> 1,
		.addr_hi    = I2C_TEA6300 >> 1,
		.registers  = 8,
		.flags      = CHIP_HAS_VOLUME | CHIP_HAS_BASSTREBLE | CHIP_HAS_INPUTSEL,

		.leftreg    = TEA6320_V,
		.rightreg   = TEA6320_V,
		.bassreg    = TEA6320_BA,
		.treblereg  = TEA6320_TR,
		.volfunc    = tea6320_volume,
		.bassfunc   = tea6320_shift11,
		.treblefunc = tea6320_shift11,

		.inputreg   = TEA6320_S,
		.inputmap   = { TEA6320_S_SA, TEA6420_S_SB, TEA6300_S_SC, TEA6320_S_SD },
		.inputmute  = TEA6300_S_GMU,
	},
	{
		.name       = "tea6420",
		.id         = I2C_DRIVERID_TEA6420,
		.insmodopt  = &tea6420,
		.addr_lo    = I2C_TEA6420 >> 1,
		.addr_hi    = I2C_TEA6420 >> 1,
		.registers  = 1,
		.flags      = CHIP_HAS_INPUTSEL,

		.inputreg   = -1,
		.inputmap   = { TEA6420_S_SA, TEA6420_S_SB, TEA6420_S_SC },
		.inputmute  = TEA6300_S_GMU,
	},
	{
		.name       = "tda8425",
		.id         = I2C_DRIVERID_TDA8425,
		.insmodopt  = &tda8425,
		.addr_lo    = I2C_TDA8425 >> 1,
		.addr_hi    = I2C_TDA8425 >> 1,
		.registers  = 9,
		.flags      = CHIP_HAS_VOLUME | CHIP_HAS_BASSTREBLE | CHIP_HAS_INPUTSEL,

		.leftreg    = TDA8425_VL,
		.rightreg   = TDA8425_VR,
		.bassreg    = TDA8425_BA,
		.treblereg  = TDA8425_TR,
		.volfunc    = tda8425_shift10,
		.bassfunc   = tda8425_shift12,
		.treblefunc = tda8425_shift12,

		.inputreg   = TDA8425_S1,
		.inputmap   = { TDA8425_S1_CH1, TDA8425_S1_CH1, TDA8425_S1_CH1 },
		.inputmute  = TDA8425_S1_OFF,

		.setmode    = tda8425_setmode,
		.initialize = tda8425_initialize,
	},
	{
		.name       = "pic16c54 (PV951)",
		.id         = I2C_DRIVERID_PIC16C54_PV9,
		.insmodopt  = &pic16c54,
		.addr_lo    = I2C_PIC16C54 >> 1,
		.addr_hi    = I2C_PIC16C54>> 1,
		.registers  = 2,
		.flags      = CHIP_HAS_INPUTSEL,

		.inputreg   = PIC16C54_REG_MISC,
		.inputmap   = {PIC16C54_MISC_SND_NOTMUTE|PIC16C54_MISC_SWITCH_TUNER,
			     PIC16C54_MISC_SND_NOTMUTE|PIC16C54_MISC_SWITCH_LINE,
			     PIC16C54_MISC_SND_NOTMUTE|PIC16C54_MISC_SWITCH_LINE,
			     PIC16C54_MISC_SND_MUTE,PIC16C54_MISC_SND_MUTE,
			     PIC16C54_MISC_SND_NOTMUTE},
		.inputmute  = PIC16C54_MISC_SND_MUTE,
	},
	{
		.name       = "ta8874z",
		.id         = -1,
		/*.id         = I2C_DRIVERID_TA8874Z, */
		.checkit    = ta8874z_checkit,
		.insmodopt  = &ta8874z,
		.addr_lo    = I2C_TDA9840 >> 1,
		.addr_hi    = I2C_TDA9840 >> 1,
		.registers  = 2,

		.getmode    = ta8874z_getmode,
		.setmode    = ta8874z_setmode,
		.checkmode  = generic_checkmode,

		.init       = {2, { TA8874Z_MONO_SET, TA8874Z_SEPARATION_DEFAULT}},
	},
	{ .name = NULL } /* EOF */
};


/* ---------------------------------------------------------------------- */
/* i2c registration                                                       */

static int chip_attach(struct i2c_adapter *adap, int addr, int kind)
{
	struct CHIPSTATE *chip;
	struct CHIPDESC  *desc;

	chip = kmalloc(sizeof(*chip),GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	memset(chip,0,sizeof(*chip));
	memcpy(&chip->c,&client_template,sizeof(struct i2c_client));
	chip->c.adapter = adap;
	chip->c.addr = addr;
	i2c_set_clientdata(&chip->c, chip);

	/* find description for the chip */
	tvaudio_dbg("chip found @ 0x%x\n", addr<<1);
	for (desc = chiplist; desc->name != NULL; desc++) {
		if (0 == *(desc->insmodopt))
			continue;
		if (addr < desc->addr_lo ||
		    addr > desc->addr_hi)
			continue;
		if (desc->checkit && !desc->checkit(chip))
			continue;
		break;
	}
	if (desc->name == NULL) {
		tvaudio_dbg("no matching chip description found\n");
		return -EIO;
	}
	tvaudio_info("%s found @ 0x%x (%s)\n", desc->name, addr<<1, adap->name);
        if (desc->flags) {
                tvaudio_dbg("matches:%s%s%s.\n",
                        (desc->flags & CHIP_HAS_VOLUME)     ? " volume"      : "",
                        (desc->flags & CHIP_HAS_BASSTREBLE) ? " bass/treble" : "",
                        (desc->flags & CHIP_HAS_INPUTSEL)   ? " audiomux"    : "");
        }

	/* fill required data structures */
	strcpy(chip->c.name,desc->name);
	chip->type = desc-chiplist;
	chip->shadow.count = desc->registers+1;
        chip->prevmode = -1;
	/* register */
	i2c_attach_client(&chip->c);

	/* initialization  */
	if (desc->initialize != NULL)
		desc->initialize(chip);
	else
		chip_cmd(chip,"init",&desc->init);

	if (desc->flags & CHIP_HAS_VOLUME) {
		chip->left   = desc->leftinit   ? desc->leftinit   : 65535;
		chip->right  = desc->rightinit  ? desc->rightinit  : 65535;
		chip_write(chip,desc->leftreg,desc->volfunc(chip->left));
		chip_write(chip,desc->rightreg,desc->volfunc(chip->right));
	}
	if (desc->flags & CHIP_HAS_BASSTREBLE) {
		chip->treble = desc->trebleinit ? desc->trebleinit : 32768;
		chip->bass   = desc->bassinit   ? desc->bassinit   : 32768;
		chip_write(chip,desc->bassreg,desc->bassfunc(chip->bass));
		chip_write(chip,desc->treblereg,desc->treblefunc(chip->treble));
	}

	chip->tpid = -1;
	if (desc->checkmode) {
		/* start async thread */
		init_timer(&chip->wt);
		chip->wt.function = chip_thread_wake;
		chip->wt.data     = (unsigned long)chip;
		init_waitqueue_head(&chip->wq);
		init_completion(&chip->texit);
		chip->tpid = kernel_thread(chip_thread,(void *)chip,0);
		if (chip->tpid < 0)
			tvaudio_warn("%s: kernel_thread() failed\n",
			       chip->c.name);
		wake_up_interruptible(&chip->wq);
	}
	return 0;
}

static int chip_probe(struct i2c_adapter *adap)
{
	/* don't attach on saa7146 based cards,
	   because dedicated drivers are used */
	if ((adap->id == I2C_HW_SAA7146))
		return 0;
#ifdef I2C_CLASS_TV_ANALOG
	if (adap->class & I2C_CLASS_TV_ANALOG)
		return i2c_probe(adap, &addr_data, chip_attach);
#else
	switch (adap->id) {
	case I2C_HW_B_BT848:
	case I2C_HW_B_RIVA:
	case I2C_HW_SAA7134:
		return i2c_probe(adap, &addr_data, chip_attach);
	}
#endif
	return 0;
}

static int chip_detach(struct i2c_client *client)
{
	struct CHIPSTATE *chip = i2c_get_clientdata(client);

	del_timer_sync(&chip->wt);
	if (chip->tpid >= 0) {
		/* shutdown async thread */
		chip->done = 1;
		wake_up_interruptible(&chip->wq);
		wait_for_completion(&chip->texit);
	}

	i2c_detach_client(&chip->c);
	kfree(chip);
	return 0;
}

/* ---------------------------------------------------------------------- */
/* video4linux interface                                                  */

static int chip_command(struct i2c_client *client,
			unsigned int cmd, void *arg)
{
	__u16 *sarg = arg;
	struct CHIPSTATE *chip = i2c_get_clientdata(client);
	struct CHIPDESC  *desc = chiplist + chip->type;

	tvaudio_dbg("%s: chip_command 0x%x\n",chip->c.name,cmd);

	switch (cmd) {
	case AUDC_SET_INPUT:
		if (desc->flags & CHIP_HAS_INPUTSEL) {
			if (*sarg & 0x80)
				chip_write_masked(chip,desc->inputreg,desc->inputmute,desc->inputmask);
			else
				chip_write_masked(chip,desc->inputreg,desc->inputmap[*sarg],desc->inputmask);
		}
		break;

	case AUDC_SET_RADIO:
		chip->norm = VIDEO_MODE_RADIO;
		chip->watch_stereo = 0;
		/* del_timer(&chip->wt); */
		break;

	/* --- v4l ioctls --- */
	/* take care: bttv does userspace copying, we'll get a
					kernel pointer here... */
	case VIDIOCGAUDIO:
	{
		struct video_audio *va = arg;

		if (desc->flags & CHIP_HAS_VOLUME) {
			va->flags  |= VIDEO_AUDIO_VOLUME;
			va->volume  = max(chip->left,chip->right);
			if (va->volume)
				va->balance = (32768*min(chip->left,chip->right))/
					va->volume;
			else
				va->balance = 32768;
		}
		if (desc->flags & CHIP_HAS_BASSTREBLE) {
			va->flags |= VIDEO_AUDIO_BASS | VIDEO_AUDIO_TREBLE;
			va->bass   = chip->bass;
			va->treble = chip->treble;
		}
		if (chip->norm != VIDEO_MODE_RADIO) {
			if (desc->getmode)
				va->mode = desc->getmode(chip);
			else
				va->mode = VIDEO_SOUND_MONO;
		}
		break;
	}

	case VIDIOCSAUDIO:
	{
		struct video_audio *va = arg;

		if (desc->flags & CHIP_HAS_VOLUME) {
			chip->left = (min(65536 - va->balance,32768) *
				va->volume) / 32768;
			chip->right = (min(va->balance,(__u16)32768) *
				va->volume) / 32768;
			chip_write(chip,desc->leftreg,desc->volfunc(chip->left));
			chip_write(chip,desc->rightreg,desc->volfunc(chip->right));
		}
		if (desc->flags & CHIP_HAS_BASSTREBLE) {
			chip->bass = va->bass;
			chip->treble = va->treble;
			chip_write(chip,desc->bassreg,desc->bassfunc(chip->bass));
			chip_write(chip,desc->treblereg,desc->treblefunc(chip->treble));
		}
		if (desc->setmode && va->mode) {
			chip->watch_stereo = 0;
			/* del_timer(&chip->wt); */
			chip->mode = va->mode;
			desc->setmode(chip,va->mode);
		}
		break;
	}
	case VIDIOCSCHAN:
	{
		struct video_channel *vc = arg;

		chip->norm = vc->norm;
		break;
	}
	case VIDIOCSFREQ:
	{
		chip->mode = 0; /* automatic */
		if (desc->checkmode) {
			desc->setmode(chip,VIDEO_SOUND_MONO);
			if (chip->prevmode != VIDEO_SOUND_MONO)
				chip->prevmode = -1; /* reset previous mode */
			mod_timer(&chip->wt, jiffies+2*HZ);
			/* the thread will call checkmode() later */
		}
	}
	}
	return 0;
}


static struct i2c_driver driver = {
	.owner           = THIS_MODULE,
	.name            = "generic i2c audio driver",
	.id              = I2C_DRIVERID_TVAUDIO,
	.flags           = I2C_DF_NOTIFY,
	.attach_adapter  = chip_probe,
	.detach_client   = chip_detach,
	.command         = chip_command,
};

static struct i2c_client client_template =
{
	.name       = "(unset)",
	.flags      = I2C_CLIENT_ALLOW_USE,
	.driver     = &driver,
};

static int __init audiochip_init_module(void)
{
	struct CHIPDESC  *desc;

	if (debug) {
		printk(KERN_INFO "tvaudio: TV audio decoder + audio/video mux driver\n");
		printk(KERN_INFO "tvaudio: known chips: ");
		for (desc = chiplist; desc->name != NULL; desc++)
			printk("%s%s", (desc == chiplist) ? "" : ", ", desc->name);
		printk("\n");
	}

	return i2c_add_driver(&driver);
}

static void __exit audiochip_cleanup_module(void)
{
	i2c_del_driver(&driver);
}

module_init(audiochip_init_module);
module_exit(audiochip_cleanup_module);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
