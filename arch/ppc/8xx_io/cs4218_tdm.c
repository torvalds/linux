
/* This is a modified version of linux/drivers/sound/dmasound.c to
 * support the CS4218 codec on the 8xx TDM port.  Thanks to everyone
 * that contributed to the dmasound software (which includes me :-).
 *
 * The CS4218 is configured in Mode 4, sub-mode 0.  This provides
 * left/right data only on the TDM port, as a 32-bit word, per frame
 * pulse.  The control of the CS4218 is provided by some other means,
 * like the SPI port.
 * Dan Malek (dmalek@jlc.net)
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/major.h>
#include <linux/fcntl.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sound.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>
#include <asm/io.h>

/* Should probably do something different with this path name.....
 * Actually, I should just stop using it...
 */
#include "cs4218.h"
#include <linux/soundcard.h>

#include <asm/mpc8xx.h>
#include <asm/8xx_immap.h>
#include <asm/commproc.h>

#define DMASND_CS4218		5

#define MAX_CATCH_RADIUS	10
#define MIN_BUFFERS		4
#define MIN_BUFSIZE 		4
#define MAX_BUFSIZE		128

#define HAS_8BIT_TABLES

static int sq_unit = -1;
static int mixer_unit = -1;
static int state_unit = -1;
static int irq_installed = 0;
static char **sound_buffers = NULL;
static char **sound_read_buffers = NULL;

static DEFINE_SPINLOCK(cs4218_lock);

/* Local copies of things we put in the control register.  Output
 * volume, like most codecs is really attenuation.
 */
static int cs4218_rate_index;

/*
 * Stuff for outputting a beep.  The values range from -327 to +327
 * so we can multiply by an amplitude in the range 0..100 to get a
 * signed short value to put in the output buffer.
 */
static short beep_wform[256] = {
	0,	40,	79,	117,	153,	187,	218,	245,
	269,	288,	304,	316,	323,	327,	327,	324,
	318,	310,	299,	288,	275,	262,	249,	236,
	224,	213,	204,	196,	190,	186,	183,	182,
	182,	183,	186,	189,	192,	196,	200,	203,
	206,	208,	209,	209,	209,	207,	204,	201,
	197,	193,	188,	183,	179,	174,	170,	166,
	163,	161,	160,	159,	159,	160,	161,	162,
	164,	166,	168,	169,	171,	171,	171,	170,
	169,	167,	163,	159,	155,	150,	144,	139,
	133,	128,	122,	117,	113,	110,	107,	105,
	103,	103,	103,	103,	104,	104,	105,	105,
	105,	103,	101,	97,	92,	86,	78,	68,
	58,	45,	32,	18,	3,	-11,	-26,	-41,
	-55,	-68,	-79,	-88,	-95,	-100,	-102,	-102,
	-99,	-93,	-85,	-75,	-62,	-48,	-33,	-16,
	0,	16,	33,	48,	62,	75,	85,	93,
	99,	102,	102,	100,	95,	88,	79,	68,
	55,	41,	26,	11,	-3,	-18,	-32,	-45,
	-58,	-68,	-78,	-86,	-92,	-97,	-101,	-103,
	-105,	-105,	-105,	-104,	-104,	-103,	-103,	-103,
	-103,	-105,	-107,	-110,	-113,	-117,	-122,	-128,
	-133,	-139,	-144,	-150,	-155,	-159,	-163,	-167,
	-169,	-170,	-171,	-171,	-171,	-169,	-168,	-166,
	-164,	-162,	-161,	-160,	-159,	-159,	-160,	-161,
	-163,	-166,	-170,	-174,	-179,	-183,	-188,	-193,
	-197,	-201,	-204,	-207,	-209,	-209,	-209,	-208,
	-206,	-203,	-200,	-196,	-192,	-189,	-186,	-183,
	-182,	-182,	-183,	-186,	-190,	-196,	-204,	-213,
	-224,	-236,	-249,	-262,	-275,	-288,	-299,	-310,
	-318,	-324,	-327,	-327,	-323,	-316,	-304,	-288,
	-269,	-245,	-218,	-187,	-153,	-117,	-79,	-40,
};

#define BEEP_SPEED	5	/* 22050 Hz sample rate */
#define BEEP_BUFLEN	512
#define BEEP_VOLUME	15	/* 0 - 100 */

static int beep_volume = BEEP_VOLUME;
static int beep_playing = 0;
static int beep_state = 0;
static short *beep_buf;
static void (*orig_mksound)(unsigned int, unsigned int);

/* This is found someplace else......I guess in the keyboard driver
 * we don't include.
 */
static void (*kd_mksound)(unsigned int, unsigned int);

static int catchRadius = 0;
static int numBufs = 4, bufSize = 32;
static int numReadBufs = 4, readbufSize = 32;


/* TDM/Serial transmit and receive buffer descriptors.
*/
static volatile cbd_t	*rx_base, *rx_cur, *tx_base, *tx_cur;

module_param(catchRadius, int, 0);
module_param(numBufs, int, 0);
module_param(bufSize, int, 0);
module_param(numreadBufs, int, 0);
module_param(readbufSize, int, 0);

#define arraysize(x)	(sizeof(x)/sizeof(*(x)))
#define le2be16(x)	(((x)<<8 & 0xff00) | ((x)>>8 & 0x00ff))
#define le2be16dbl(x)	(((x)<<8 & 0xff00ff00) | ((x)>>8 & 0x00ff00ff))

#define IOCTL_IN(arg, ret) \
	do { int error = get_user(ret, (int *)(arg)); \
		if (error) return error; \
	} while (0)
#define IOCTL_OUT(arg, ret)	ioctl_return((int *)(arg), ret)

/* CS4218 serial port control in mode 4.
*/
#define CS_INTMASK	((uint)0x40000000)
#define CS_DO1		((uint)0x20000000)
#define CS_LATTEN	((uint)0x1f000000)
#define CS_RATTEN	((uint)0x00f80000)
#define CS_MUTE		((uint)0x00040000)
#define CS_ISL		((uint)0x00020000)
#define CS_ISR		((uint)0x00010000)
#define CS_LGAIN	((uint)0x0000f000)
#define CS_RGAIN	((uint)0x00000f00)

#define CS_LATTEN_SET(X)	(((X) & 0x1f) << 24)
#define CS_RATTEN_SET(X)	(((X) & 0x1f) << 19)
#define CS_LGAIN_SET(X)		(((X) & 0x0f) << 12)
#define CS_RGAIN_SET(X)		(((X) & 0x0f) << 8)

#define CS_LATTEN_GET(X)	(((X) >> 24) & 0x1f)
#define CS_RATTEN_GET(X)	(((X) >> 19) & 0x1f)
#define CS_LGAIN_GET(X)		(((X) >> 12) & 0x0f)
#define CS_RGAIN_GET(X)		(((X) >> 8) & 0x0f)

/* The control register is effectively write only.  We have to keep a copy
 * of what we write.
 */
static	uint	cs4218_control;

/* A place to store expanding information.
*/
static int	expand_bal;
static int	expand_data;

/* Since I can't make the microcode patch work for the SPI, I just
 * clock the bits using software.
 */
static	void	sw_spi_init(void);
static	void	sw_spi_io(u_char *obuf, u_char *ibuf, uint bcnt);
static	uint	cs4218_ctl_write(uint ctlreg);

/*** Some low level helpers **************************************************/

/* 16 bit mu-law */

static short ulaw2dma16[] = {
	-32124,	-31100,	-30076,	-29052,	-28028,	-27004,	-25980,	-24956,
	-23932,	-22908,	-21884,	-20860,	-19836,	-18812,	-17788,	-16764,
	-15996,	-15484,	-14972,	-14460,	-13948,	-13436,	-12924,	-12412,
	-11900,	-11388,	-10876,	-10364,	-9852,	-9340,	-8828,	-8316,
	-7932,	-7676,	-7420,	-7164,	-6908,	-6652,	-6396,	-6140,
	-5884,	-5628,	-5372,	-5116,	-4860,	-4604,	-4348,	-4092,
	-3900,	-3772,	-3644,	-3516,	-3388,	-3260,	-3132,	-3004,
	-2876,	-2748,	-2620,	-2492,	-2364,	-2236,	-2108,	-1980,
	-1884,	-1820,	-1756,	-1692,	-1628,	-1564,	-1500,	-1436,
	-1372,	-1308,	-1244,	-1180,	-1116,	-1052,	-988,	-924,
	-876,	-844,	-812,	-780,	-748,	-716,	-684,	-652,
	-620,	-588,	-556,	-524,	-492,	-460,	-428,	-396,
	-372,	-356,	-340,	-324,	-308,	-292,	-276,	-260,
	-244,	-228,	-212,	-196,	-180,	-164,	-148,	-132,
	-120,	-112,	-104,	-96,	-88,	-80,	-72,	-64,
	-56,	-48,	-40,	-32,	-24,	-16,	-8,	0,
	32124,	31100,	30076,	29052,	28028,	27004,	25980,	24956,
	23932,	22908,	21884,	20860,	19836,	18812,	17788,	16764,
	15996,	15484,	14972,	14460,	13948,	13436,	12924,	12412,
	11900,	11388,	10876,	10364,	9852,	9340,	8828,	8316,
	7932,	7676,	7420,	7164,	6908,	6652,	6396,	6140,
	5884,	5628,	5372,	5116,	4860,	4604,	4348,	4092,
	3900,	3772,	3644,	3516,	3388,	3260,	3132,	3004,
	2876,	2748,	2620,	2492,	2364,	2236,	2108,	1980,
	1884,	1820,	1756,	1692,	1628,	1564,	1500,	1436,
	1372,	1308,	1244,	1180,	1116,	1052,	988,	924,
	876,	844,	812,	780,	748,	716,	684,	652,
	620,	588,	556,	524,	492,	460,	428,	396,
	372,	356,	340,	324,	308,	292,	276,	260,
	244,	228,	212,	196,	180,	164,	148,	132,
	120,	112,	104,	96,	88,	80,	72,	64,
	56,	48,	40,	32,	24,	16,	8,	0,
};

/* 16 bit A-law */

static short alaw2dma16[] = {
	-5504,	-5248,	-6016,	-5760,	-4480,	-4224,	-4992,	-4736,
	-7552,	-7296,	-8064,	-7808,	-6528,	-6272,	-7040,	-6784,
	-2752,	-2624,	-3008,	-2880,	-2240,	-2112,	-2496,	-2368,
	-3776,	-3648,	-4032,	-3904,	-3264,	-3136,	-3520,	-3392,
	-22016,	-20992,	-24064,	-23040,	-17920,	-16896,	-19968,	-18944,
	-30208,	-29184,	-32256,	-31232,	-26112,	-25088,	-28160,	-27136,
	-11008,	-10496,	-12032,	-11520,	-8960,	-8448,	-9984,	-9472,
	-15104,	-14592,	-16128,	-15616,	-13056,	-12544,	-14080,	-13568,
	-344,	-328,	-376,	-360,	-280,	-264,	-312,	-296,
	-472,	-456,	-504,	-488,	-408,	-392,	-440,	-424,
	-88,	-72,	-120,	-104,	-24,	-8,	-56,	-40,
	-216,	-200,	-248,	-232,	-152,	-136,	-184,	-168,
	-1376,	-1312,	-1504,	-1440,	-1120,	-1056,	-1248,	-1184,
	-1888,	-1824,	-2016,	-1952,	-1632,	-1568,	-1760,	-1696,
	-688,	-656,	-752,	-720,	-560,	-528,	-624,	-592,
	-944,	-912,	-1008,	-976,	-816,	-784,	-880,	-848,
	5504,	5248,	6016,	5760,	4480,	4224,	4992,	4736,
	7552,	7296,	8064,	7808,	6528,	6272,	7040,	6784,
	2752,	2624,	3008,	2880,	2240,	2112,	2496,	2368,
	3776,	3648,	4032,	3904,	3264,	3136,	3520,	3392,
	22016,	20992,	24064,	23040,	17920,	16896,	19968,	18944,
	30208,	29184,	32256,	31232,	26112,	25088,	28160,	27136,
	11008,	10496,	12032,	11520,	8960,	8448,	9984,	9472,
	15104,	14592,	16128,	15616,	13056,	12544,	14080,	13568,
	344,	328,	376,	360,	280,	264,	312,	296,
	472,	456,	504,	488,	408,	392,	440,	424,
	88,	72,	120,	104,	24,	8,	56,	40,
	216,	200,	248,	232,	152,	136,	184,	168,
	1376,	1312,	1504,	1440,	1120,	1056,	1248,	1184,
	1888,	1824,	2016,	1952,	1632,	1568,	1760,	1696,
	688,	656,	752,	720,	560,	528,	624,	592,
	944,	912,	1008,	976,	816,	784,	880,	848,
};


/*** Translations ************************************************************/


static ssize_t cs4218_ct_law(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t cs4218_ct_s8(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft);
static ssize_t cs4218_ct_u8(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft);
static ssize_t cs4218_ct_s16(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t cs4218_ct_u16(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t cs4218_ctx_law(const u_char *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft);
static ssize_t cs4218_ctx_s8(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t cs4218_ctx_u8(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t cs4218_ctx_s16(const u_char *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft);
static ssize_t cs4218_ctx_u16(const u_char *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft);
static ssize_t cs4218_ct_s16_read(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);
static ssize_t cs4218_ct_u16_read(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft);


/*** Low level stuff *********************************************************/

struct cs_sound_settings {
	MACHINE mach;		/* machine dependent things */
	SETTINGS hard;		/* hardware settings */
	SETTINGS soft;		/* software settings */
	SETTINGS dsp;		/* /dev/dsp default settings */
	TRANS *trans_write;	/* supported translations for playback */
	TRANS *trans_read;	/* supported translations for record */
	int volume_left;	/* volume (range is machine dependent) */
	int volume_right;
	int bass;		/* tone (range is machine dependent) */
	int treble;
	int gain;
	int minDev;		/* minor device number currently open */
};

static struct cs_sound_settings sound;

static void *CS_Alloc(unsigned int size, gfp_t flags);
static void CS_Free(void *ptr, unsigned int size);
static int CS_IrqInit(void);
#ifdef MODULE
static void CS_IrqCleanup(void);
#endif /* MODULE */
static void CS_Silence(void);
static void CS_Init(void);
static void CS_Play(void);
static void CS_Record(void);
static int CS_SetFormat(int format);
static int CS_SetVolume(int volume);
static void cs4218_tdm_tx_intr(void *devid);
static void cs4218_tdm_rx_intr(void *devid);
static void cs4218_intr(void *devid);
static int cs_get_volume(uint reg);
static int cs_volume_setter(int volume, int mute);
static int cs_get_gain(uint reg);
static int cs_set_gain(int gain);
static void cs_mksound(unsigned int hz, unsigned int ticks);
static void cs_nosound(unsigned long xx);

/*** Mid level stuff *********************************************************/


static void sound_silence(void);
static void sound_init(void);
static int sound_set_format(int format);
static int sound_set_speed(int speed);
static int sound_set_stereo(int stereo);
static int sound_set_volume(int volume);

static ssize_t sound_copy_translate(const u_char *userPtr,
				    size_t userCount,
				    u_char frame[], ssize_t *frameUsed,
				    ssize_t frameLeft);
static ssize_t sound_copy_translate_read(const u_char *userPtr,
				    size_t userCount,
				    u_char frame[], ssize_t *frameUsed,
				    ssize_t frameLeft);


/*
 * /dev/mixer abstraction
 */

struct sound_mixer {
    int busy;
    int modify_counter;
};

static struct sound_mixer mixer;

static struct sound_queue sq;
static struct sound_queue read_sq;

#define sq_block_address(i)	(sq.buffers[i])
#define SIGNAL_RECEIVED	(signal_pending(current))
#define NON_BLOCKING(open_mode)	(open_mode & O_NONBLOCK)
#define ONE_SECOND	HZ	/* in jiffies (100ths of a second) */
#define NO_TIME_LIMIT	0xffffffff

/*
 * /dev/sndstat
 */

struct sound_state {
	int busy;
	char buf[512];
	int len, ptr;
};

static struct sound_state state;

/*** Common stuff ********************************************************/

static long long sound_lseek(struct file *file, long long offset, int orig);

/*** Config & Setup **********************************************************/

void dmasound_setup(char *str, int *ints);

/*** Translations ************************************************************/


/* ++TeSche: radically changed for new expanding purposes...
 *
 * These two routines now deal with copying/expanding/translating the samples
 * from user space into our buffer at the right frequency. They take care about
 * how much data there's actually to read, how much buffer space there is and
 * to convert samples into the right frequency/encoding. They will only work on
 * complete samples so it may happen they leave some bytes in the input stream
 * if the user didn't write a multiple of the current sample size. They both
 * return the number of bytes they've used from both streams so you may detect
 * such a situation. Luckily all programs should be able to cope with that.
 *
 * I think I've optimized anything as far as one can do in plain C, all
 * variables should fit in registers and the loops are really short. There's
 * one loop for every possible situation. Writing a more generalized and thus
 * parameterized loop would only produce slower code. Feel free to optimize
 * this in assembler if you like. :)
 *
 * I think these routines belong here because they're not yet really hardware
 * independent, especially the fact that the Falcon can play 16bit samples
 * only in stereo is hardcoded in both of them!
 *
 * ++geert: split in even more functions (one per format)
 */

static ssize_t cs4218_ct_law(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	short *table = sound.soft.format == AFMT_MU_LAW ? ulaw2dma16: alaw2dma16;
	ssize_t count, used;
	short *p = (short *) &frame[*frameUsed];
	int val, stereo = sound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	used = count = min(userCount, frameLeft);
	while (count > 0) {
		u_char data;
		if (get_user(data, userPtr++))
			return -EFAULT;
		val = table[data];
		*p++ = val;
		if (stereo) {
			if (get_user(data, userPtr++))
				return -EFAULT;
			val = table[data];
		}
		*p++ = val;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 2: used;
}


static ssize_t cs4218_ct_s8(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	short *p = (short *) &frame[*frameUsed];
	int val, stereo = sound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	used = count = min(userCount, frameLeft);
	while (count > 0) {
		u_char data;
		if (get_user(data, userPtr++))
			return -EFAULT;
		val = data << 8;
		*p++ = val;
		if (stereo) {
			if (get_user(data, userPtr++))
				return -EFAULT;
			val = data << 8;
		}
		*p++ = val;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 2: used;
}


static ssize_t cs4218_ct_u8(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	short *p = (short *) &frame[*frameUsed];
	int val, stereo = sound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	used = count = min(userCount, frameLeft);
	while (count > 0) {
		u_char data;
		if (get_user(data, userPtr++))
			return -EFAULT;
		val = (data ^ 0x80) << 8;
		*p++ = val;
		if (stereo) {
			if (get_user(data, userPtr++))
				return -EFAULT;
			val = (data ^ 0x80) << 8;
		}
		*p++ = val;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 2: used;
}


/* This is the default format of the codec.  Signed, 16-bit stereo
 * generated by an application shouldn't have to be copied at all.
 * We should just get the phsical address of the buffers and update
 * the TDM BDs directly.
 */
static ssize_t cs4218_ct_s16(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	ssize_t count, used;
	int stereo = sound.soft.stereo;
	short *fp = (short *) &frame[*frameUsed];

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	used = count = min(userCount, frameLeft);
	if (!stereo) {
		short *up = (short *) userPtr;
		while (count > 0) {
			short data;
			if (get_user(data, up++))
				return -EFAULT;
			*fp++ = data;
			*fp++ = data;
			count--;
		}
	} else {
		if (copy_from_user(fp, userPtr, count * 4))
			return -EFAULT;
	}
	*frameUsed += used * 4;
	return stereo? used * 4: used * 2;
}

static ssize_t cs4218_ct_u16(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	ssize_t count, used;
	int mask = (sound.soft.format == AFMT_U16_LE? 0x0080: 0x8000);
	int stereo = sound.soft.stereo;
	short *fp = (short *) &frame[*frameUsed];
	short *up = (short *) userPtr;

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	used = count = min(userCount, frameLeft);
	while (count > 0) {
		int data;
		if (get_user(data, up++))
			return -EFAULT;
		data ^= mask;
		*fp++ = data;
		if (stereo) {
			if (get_user(data, up++))
				return -EFAULT;
			data ^= mask;
		}
		*fp++ = data;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 4: used * 2;
}


static ssize_t cs4218_ctx_law(const u_char *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft)
{
	unsigned short *table = (unsigned short *)
		(sound.soft.format == AFMT_MU_LAW ? ulaw2dma16: alaw2dma16);
	unsigned int data = expand_data;
	unsigned int *p = (unsigned int *) &frame[*frameUsed];
	int bal = expand_bal;
	int hSpeed = sound.hard.speed, sSpeed = sound.soft.speed;
	int utotal, ftotal;
	int stereo = sound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		u_char c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(c, userPtr++))
				return -EFAULT;
			data = table[c];
			if (stereo) {
				if (get_user(c, userPtr++))
					return -EFAULT;
				data = (data << 16) + table[c];
			} else
				data = (data << 16) + data;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 2: utotal;
}


static ssize_t cs4218_ctx_s8(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	unsigned int *p = (unsigned int *) &frame[*frameUsed];
	unsigned int data = expand_data;
	int bal = expand_bal;
	int hSpeed = sound.hard.speed, sSpeed = sound.soft.speed;
	int stereo = sound.soft.stereo;
	int utotal, ftotal;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		u_char c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(c, userPtr++))
				return -EFAULT;
			data = c << 8;
			if (stereo) {
				if (get_user(c, userPtr++))
					return -EFAULT;
				data = (data << 16) + (c << 8);
			} else
				data = (data << 16) + data;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 2: utotal;
}


static ssize_t cs4218_ctx_u8(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	unsigned int *p = (unsigned int *) &frame[*frameUsed];
	unsigned int data = expand_data;
	int bal = expand_bal;
	int hSpeed = sound.hard.speed, sSpeed = sound.soft.speed;
	int stereo = sound.soft.stereo;
	int utotal, ftotal;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		u_char c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(c, userPtr++))
				return -EFAULT;
			data = (c ^ 0x80) << 8;
			if (stereo) {
				if (get_user(c, userPtr++))
					return -EFAULT;
				data = (data << 16) + ((c ^ 0x80) << 8);
			} else
				data = (data << 16) + data;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 2: utotal;
}


static ssize_t cs4218_ctx_s16(const u_char *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft)
{
	unsigned int *p = (unsigned int *) &frame[*frameUsed];
	unsigned int data = expand_data;
	unsigned short *up = (unsigned short *) userPtr;
	int bal = expand_bal;
	int hSpeed = sound.hard.speed, sSpeed = sound.soft.speed;
	int stereo = sound.soft.stereo;
	int utotal, ftotal;

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		unsigned short c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(data, up++))
				return -EFAULT;
			if (stereo) {
				if (get_user(c, up++))
					return -EFAULT;
				data = (data << 16) + c;
			} else
				data = (data << 16) + data;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 4: utotal * 2;
}


static ssize_t cs4218_ctx_u16(const u_char *userPtr, size_t userCount,
			    u_char frame[], ssize_t *frameUsed,
			    ssize_t frameLeft)
{
	int mask = (sound.soft.format == AFMT_U16_LE? 0x0080: 0x8000);
	unsigned int *p = (unsigned int *) &frame[*frameUsed];
	unsigned int data = expand_data;
	unsigned short *up = (unsigned short *) userPtr;
	int bal = expand_bal;
	int hSpeed = sound.hard.speed, sSpeed = sound.soft.speed;
	int stereo = sound.soft.stereo;
	int utotal, ftotal;

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	ftotal = frameLeft;
	utotal = userCount;
	while (frameLeft) {
		unsigned short c;
		if (bal < 0) {
			if (userCount == 0)
				break;
			if (get_user(data, up++))
				return -EFAULT;
			data ^= mask;
			if (stereo) {
				if (get_user(c, up++))
					return -EFAULT;
				data = (data << 16) + (c ^ mask);
			} else
				data = (data << 16) + data;
			userCount--;
			bal += hSpeed;
		}
		*p++ = data;
		frameLeft--;
		bal -= sSpeed;
	}
	expand_bal = bal;
	expand_data = data;
	*frameUsed += (ftotal - frameLeft) * 4;
	utotal -= userCount;
	return stereo? utotal * 4: utotal * 2;
}

static ssize_t cs4218_ct_s8_read(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	short *p = (short *) &frame[*frameUsed];
	int val, stereo = sound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	used = count = min(userCount, frameLeft);
	while (count > 0) {
		u_char data;

		val = *p++;
		data = val >> 8;
		if (put_user(data, (u_char *)userPtr++))
			return -EFAULT;
		if (stereo) {
			val = *p;
			data = val >> 8;
			if (put_user(data, (u_char *)userPtr++))
				return -EFAULT;
		}
		p++;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 2: used;
}


static ssize_t cs4218_ct_u8_read(const u_char *userPtr, size_t userCount,
			  u_char frame[], ssize_t *frameUsed,
			  ssize_t frameLeft)
{
	ssize_t count, used;
	short *p = (short *) &frame[*frameUsed];
	int val, stereo = sound.soft.stereo;

	frameLeft >>= 2;
	if (stereo)
		userCount >>= 1;
	used = count = min(userCount, frameLeft);
	while (count > 0) {
		u_char data;

		val = *p++;
		data = (val >> 8) ^ 0x80;
		if (put_user(data, (u_char *)userPtr++))
			return -EFAULT;
		if (stereo) {
			val = *p;
			data = (val >> 8) ^ 0x80;
			if (put_user(data, (u_char *)userPtr++))
				return -EFAULT;
		}
		p++;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 2: used;
}


static ssize_t cs4218_ct_s16_read(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	ssize_t count, used;
	int stereo = sound.soft.stereo;
	short *fp = (short *) &frame[*frameUsed];

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	used = count = min(userCount, frameLeft);
	if (!stereo) {
		short *up = (short *) userPtr;
		while (count > 0) {
			short data;
			data = *fp;
			if (put_user(data, up++))
				return -EFAULT;
			fp+=2;
			count--;
		}
	} else {
		if (copy_to_user((u_char *)userPtr, fp, count * 4))
			return -EFAULT;
	}
	*frameUsed += used * 4;
	return stereo? used * 4: used * 2;
}

static ssize_t cs4218_ct_u16_read(const u_char *userPtr, size_t userCount,
			   u_char frame[], ssize_t *frameUsed,
			   ssize_t frameLeft)
{
	ssize_t count, used;
	int mask = (sound.soft.format == AFMT_U16_LE? 0x0080: 0x8000);
	int stereo = sound.soft.stereo;
	short *fp = (short *) &frame[*frameUsed];
	short *up = (short *) userPtr;

	frameLeft >>= 2;
	userCount >>= (stereo? 2: 1);
	used = count = min(userCount, frameLeft);
	while (count > 0) {
		int data;

		data = *fp++;
		data ^= mask;
		if (put_user(data, up++))
			return -EFAULT;
		if (stereo) {
			data = *fp;
			data ^= mask;
			if (put_user(data, up++))
				return -EFAULT;
		}
		fp++;
		count--;
	}
	*frameUsed += used * 4;
	return stereo? used * 4: used * 2;
}

static TRANS transCSNormal = {
	cs4218_ct_law, cs4218_ct_law, cs4218_ct_s8, cs4218_ct_u8,
	cs4218_ct_s16, cs4218_ct_u16, cs4218_ct_s16, cs4218_ct_u16
};

static TRANS transCSExpand = {
	cs4218_ctx_law, cs4218_ctx_law, cs4218_ctx_s8, cs4218_ctx_u8,
	cs4218_ctx_s16, cs4218_ctx_u16, cs4218_ctx_s16, cs4218_ctx_u16
};

static TRANS transCSNormalRead = {
	NULL, NULL, cs4218_ct_s8_read, cs4218_ct_u8_read,
	cs4218_ct_s16_read, cs4218_ct_u16_read,
	cs4218_ct_s16_read, cs4218_ct_u16_read
};

/*** Low level stuff *********************************************************/

static void *CS_Alloc(unsigned int size, gfp_t flags)
{
	int	order;

	size >>= 13;
	for (order=0; order < 5; order++) {
		if (size == 0)
			break;
		size >>= 1;
	}
	return (void *)__get_free_pages(flags, order);
}

static void CS_Free(void *ptr, unsigned int size)
{
	int	order;

	size >>= 13;
	for (order=0; order < 5; order++) {
		if (size == 0)
			break;
		size >>= 1;
	}
	free_pages((ulong)ptr, order);
}

static int __init CS_IrqInit(void)
{
	cpm_install_handler(CPMVEC_SMC2, cs4218_intr, NULL);
	return 1;
}

#ifdef MODULE
static void CS_IrqCleanup(void)
{
	volatile smc_t		*sp;
	volatile cpm8xx_t	*cp;

	/* First disable transmitter and receiver.
	*/
	sp = &cpmp->cp_smc[1];
	sp->smc_smcmr &= ~(SMCMR_REN | SMCMR_TEN);

	/* And now shut down the SMC.
	*/
	cp = cpmp;	/* Get pointer to Communication Processor */
	cp->cp_cpcr = mk_cr_cmd(CPM_CR_CH_SMC2,
				CPM_CR_STOP_TX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);

	/* Release the interrupt handler.
	*/
	cpm_free_handler(CPMVEC_SMC2);

	kfree(beep_buf);
	kd_mksound = orig_mksound;
}
#endif /* MODULE */

static void CS_Silence(void)
{
	volatile smc_t		*sp;

	/* Disable transmitter.
	*/
	sp = &cpmp->cp_smc[1];
	sp->smc_smcmr &= ~SMCMR_TEN;
}

/* Frequencies depend upon external oscillator.  There are two
 * choices, 12.288 and 11.2896 MHz.  The RPCG audio supports both through
 * and external control register selection bit.
 */
static int cs4218_freqs[] = {
    /* 12.288  11.2896  */
	48000, 44100,
	32000, 29400,
	24000, 22050,
	19200, 17640,
	16000, 14700,
	12000, 11025,
	 9600,  8820,
	 8000,  7350
};

static void CS_Init(void)
{
	int i, tolerance;

	switch (sound.soft.format) {
	case AFMT_S16_LE:
	case AFMT_U16_LE:
		sound.hard.format = AFMT_S16_LE;
		break;
	default:
		sound.hard.format = AFMT_S16_BE;
		break;
	}
	sound.hard.stereo = 1;
	sound.hard.size = 16;

	/*
	 * If we have a sample rate which is within catchRadius percent
	 * of the requested value, we don't have to expand the samples.
	 * Otherwise choose the next higher rate.
	 */
	i = (sizeof(cs4218_freqs) / sizeof(int));
	do {
		tolerance = catchRadius * cs4218_freqs[--i] / 100;
	} while (sound.soft.speed > cs4218_freqs[i] + tolerance && i > 0);
	if (sound.soft.speed >= cs4218_freqs[i] - tolerance)
		sound.trans_write = &transCSNormal;
	else
		sound.trans_write = &transCSExpand;
	sound.trans_read = &transCSNormalRead;
	sound.hard.speed = cs4218_freqs[i];
	cs4218_rate_index = i;

	/* The CS4218 has seven selectable clock dividers for the sample
	 * clock.  The HIOX then provides one of two external rates.
	 * An even numbered frequency table index uses the high external
	 * clock rate.
	 */
	*(uint *)HIOX_CSR4_ADDR &= ~(HIOX_CSR4_AUDCLKHI | HIOX_CSR4_AUDCLKSEL);
	if ((i & 1) == 0)
		*(uint *)HIOX_CSR4_ADDR |= HIOX_CSR4_AUDCLKHI;
	i >>= 1;
	*(uint *)HIOX_CSR4_ADDR |= (i & HIOX_CSR4_AUDCLKSEL);

	expand_bal = -sound.soft.speed;
}

static int CS_SetFormat(int format)
{
	int size;

	switch (format) {
	case AFMT_QUERY:
		return sound.soft.format;
	case AFMT_MU_LAW:
	case AFMT_A_LAW:
	case AFMT_U8:
	case AFMT_S8:
		size = 8;
		break;
	case AFMT_S16_BE:
	case AFMT_U16_BE:
	case AFMT_S16_LE:
	case AFMT_U16_LE:
		size = 16;
		break;
	default: /* :-) */
		printk(KERN_ERR "dmasound: unknown format 0x%x, using AFMT_U8\n",
		       format);
		size = 8;
		format = AFMT_U8;
	}

	sound.soft.format = format;
	sound.soft.size = size;
	if (sound.minDev == SND_DEV_DSP) {
		sound.dsp.format = format;
		sound.dsp.size = size;
	}

	CS_Init();

	return format;
}

/* Volume is the amount of attenuation we tell the codec to impose
 * on the outputs.  There are 32 levels, with 0 the "loudest".
 */
#define CS_VOLUME_TO_MASK(x)	(31 - ((((x) - 1) * 31) / 99))
#define CS_MASK_TO_VOLUME(y)	(100 - ((y) * 99 / 31))

static int cs_get_volume(uint reg)
{
	int volume;

	volume = CS_MASK_TO_VOLUME(CS_LATTEN_GET(reg));
	volume |= CS_MASK_TO_VOLUME(CS_RATTEN_GET(reg)) << 8;
	return volume;
}

static int cs_volume_setter(int volume, int mute)
{
	uint tempctl;

	if (mute && volume == 0) {
		tempctl = cs4218_control | CS_MUTE;
	} else {
		tempctl = cs4218_control & ~CS_MUTE;
		tempctl = tempctl & ~(CS_LATTEN | CS_RATTEN);
		tempctl |= CS_LATTEN_SET(CS_VOLUME_TO_MASK(volume & 0xff));
		tempctl |= CS_RATTEN_SET(CS_VOLUME_TO_MASK((volume >> 8) & 0xff));
		volume = cs_get_volume(tempctl);
	}
	if (tempctl != cs4218_control) {
		cs4218_ctl_write(tempctl);
	}
	return volume;
}


/* Gain has 16 steps from 0 to 15.  These are in 1.5dB increments from
 * 0 (no gain) to 22.5 dB.
 */
#define CS_RECLEVEL_TO_GAIN(v) \
	((v) < 0 ? 0 : (v) > 100 ? 15 : (v) * 3 / 20)
#define CS_GAIN_TO_RECLEVEL(v) (((v) * 20 + 2) / 3)

static int cs_get_gain(uint reg)
{
	int gain;

	gain = CS_GAIN_TO_RECLEVEL(CS_LGAIN_GET(reg));
	gain |= CS_GAIN_TO_RECLEVEL(CS_RGAIN_GET(reg)) << 8;
	return gain;
}

static int cs_set_gain(int gain)
{
	uint tempctl;

	tempctl = cs4218_control & ~(CS_LGAIN | CS_RGAIN);
	tempctl |= CS_LGAIN_SET(CS_RECLEVEL_TO_GAIN(gain & 0xff));
	tempctl |= CS_RGAIN_SET(CS_RECLEVEL_TO_GAIN((gain >> 8) & 0xff));
	gain = cs_get_gain(tempctl);

	if (tempctl != cs4218_control) {
		cs4218_ctl_write(tempctl);
	}
	return gain;
}

static int CS_SetVolume(int volume)
{
	return cs_volume_setter(volume, CS_MUTE);
}

static void CS_Play(void)
{
	int i, count;
	unsigned long flags;
	volatile cbd_t	*bdp;
	volatile cpm8xx_t *cp;

	/* Protect buffer */
	spin_lock_irqsave(&cs4218_lock, flags);
#if 0
	if (awacs_beep_state) {
		/* sound takes precedence over beeps */
		out_le32(&awacs_txdma->control, (RUN|PAUSE|FLUSH|WAKE) << 16);
		out_le32(&awacs->control,
			 (in_le32(&awacs->control) & ~0x1f00)
			 | (awacs_rate_index << 8));
		out_le32(&awacs->byteswap, sound.hard.format != AFMT_S16_BE);
		out_le32(&awacs_txdma->cmdptr, virt_to_bus(&(awacs_tx_cmds[(sq.front+sq.active) % sq.max_count])));

		beep_playing = 0;
		awacs_beep_state = 0;
	}
#endif
	i = sq.front + sq.active;
	if (i >= sq.max_count)
		i -= sq.max_count;
	while (sq.active < 2 && sq.active < sq.count) {
		count = (sq.count == sq.active + 1)?sq.rear_size:sq.block_size;
		if (count < sq.block_size && !sq.syncing)
			/* last block not yet filled, and we're not syncing. */
			break;

		bdp = &tx_base[i];
		bdp->cbd_datlen = count;

		flush_dcache_range((ulong)sound_buffers[i],
					(ulong)(sound_buffers[i] + count));

		if (++i >= sq.max_count)
			i = 0;

		if (sq.active == 0) {
			/* The SMC does not load its fifo until the first
			 * TDM frame pulse, so the transmit data gets shifted
			 * by one word.  To compensate for this, we incorrectly
			 * transmit the first buffer and shorten it by one
			 * word.  Subsequent buffers are then aligned properly.
			 */
			bdp->cbd_datlen -= 2;

			/* Start up the SMC Transmitter.
			*/
			cp = cpmp;
			cp->cp_smc[1].smc_smcmr |= SMCMR_TEN;
			cp->cp_cpcr = mk_cr_cmd(CPM_CR_CH_SMC2,
					CPM_CR_RESTART_TX) | CPM_CR_FLG;
			while (cp->cp_cpcr & CPM_CR_FLG);
		}

		/* Buffer is ready now.
		*/
		bdp->cbd_sc |= BD_SC_READY;

		++sq.active;
	}
	spin_unlock_irqrestore(&cs4218_lock, flags);
}


static void CS_Record(void)
{
	unsigned long flags;
	volatile smc_t		*sp;

	if (read_sq.active)
		return;

	/* Protect buffer */
	spin_lock_irqsave(&cs4218_lock, flags);

	/* This is all we have to do......Just start it up.
	*/
	sp = &cpmp->cp_smc[1];
	sp->smc_smcmr |= SMCMR_REN;

	read_sq.active = 1;

        spin_unlock_irqrestore(&cs4218_lock, flags);
}


static void
cs4218_tdm_tx_intr(void *devid)
{
	int i = sq.front;
	volatile cbd_t *bdp;

	while (sq.active > 0) {
		bdp = &tx_base[i];
		if (bdp->cbd_sc & BD_SC_READY)
			break;	/* this frame is still going */
		--sq.count;
		--sq.active;
		if (++i >= sq.max_count)
			i = 0;
	}
	if (i != sq.front)
		WAKE_UP(sq.action_queue);
	sq.front = i;

	CS_Play();

	if (!sq.active)
		WAKE_UP(sq.sync_queue);
}


static void
cs4218_tdm_rx_intr(void *devid)
{

	/* We want to blow 'em off when shutting down.
	*/
	if (read_sq.active == 0)
		return;

	/* Check multiple buffers in case we were held off from
	 * interrupt processing for a long time.  Geeze, I really hope
	 * this doesn't happen.
	 */
	while ((rx_base[read_sq.rear].cbd_sc & BD_SC_EMPTY) == 0) {

		/* Invalidate the data cache range for this buffer.
		*/
		invalidate_dcache_range(
		    (uint)(sound_read_buffers[read_sq.rear]),
		    (uint)(sound_read_buffers[read_sq.rear] + read_sq.block_size));

		/* Make buffer available again and move on.
		*/
		rx_base[read_sq.rear].cbd_sc |= BD_SC_EMPTY;
		read_sq.rear++;

		/* Wrap the buffer ring.
		*/
		if (read_sq.rear >= read_sq.max_active)
			read_sq.rear = 0;

		/* If we have caught up to the front buffer, bump it.
		 * This will cause weird (but not fatal) results if the
		 * read loop is currently using this buffer.  The user is
		 * behind in this case anyway, so weird things are going
		 * to happen.
		 */
		if (read_sq.rear == read_sq.front) {
			read_sq.front++;
			if (read_sq.front >= read_sq.max_active)
				read_sq.front = 0;
		}
	}

	WAKE_UP(read_sq.action_queue);
}

static void cs_nosound(unsigned long xx)
{
	unsigned long flags;

	/* not sure if this is needed, since hardware command is #if 0'd */
	spin_lock_irqsave(&cs4218_lock, flags);
	if (beep_playing) {
#if 0
		st_le16(&beep_dbdma_cmd->command, DBDMA_STOP);
#endif
		beep_playing = 0;
	}
	spin_unlock_irqrestore(&cs4218_lock, flags);
}

static DEFINE_TIMER(beep_timer, cs_nosound, 0, 0);
};

static void cs_mksound(unsigned int hz, unsigned int ticks)
{
	unsigned long flags;
	int beep_speed = BEEP_SPEED;
	int srate = cs4218_freqs[beep_speed];
	int period, ncycles, nsamples;
	int i, j, f;
	short *p;
	static int beep_hz_cache;
	static int beep_nsamples_cache;
	static int beep_volume_cache;

	if (hz <= srate / BEEP_BUFLEN || hz > srate / 2) {
#if 1
		/* this is a hack for broken X server code */
		hz = 750;
		ticks = 12;
#else
		/* cancel beep currently playing */
		awacs_nosound(0);
		return;
#endif
	}
	/* lock while modifying beep_timer */
	spin_lock_irqsave(&cs4218_lock, flags);
	del_timer(&beep_timer);
	if (ticks) {
		beep_timer.expires = jiffies + ticks;
		add_timer(&beep_timer);
	}
	if (beep_playing || sq.active || beep_buf == NULL) {
		spin_unlock_irqrestore(&cs4218_lock, flags);
		return;		/* too hard, sorry :-( */
	}
	beep_playing = 1;
#if 0
	st_le16(&beep_dbdma_cmd->command, OUTPUT_MORE + BR_ALWAYS);
#endif
	spin_unlock_irqrestore(&cs4218_lock, flags);

	if (hz == beep_hz_cache && beep_volume == beep_volume_cache) {
		nsamples = beep_nsamples_cache;
	} else {
		period = srate * 256 / hz;	/* fixed point */
		ncycles = BEEP_BUFLEN * 256 / period;
		nsamples = (period * ncycles) >> 8;
		f = ncycles * 65536 / nsamples;
		j = 0;
		p = beep_buf;
		for (i = 0; i < nsamples; ++i, p += 2) {
			p[0] = p[1] = beep_wform[j >> 8] * beep_volume;
			j = (j + f) & 0xffff;
		}
		beep_hz_cache = hz;
		beep_volume_cache = beep_volume;
		beep_nsamples_cache = nsamples;
	}

#if 0
	st_le16(&beep_dbdma_cmd->req_count, nsamples*4);
	st_le16(&beep_dbdma_cmd->xfer_status, 0);
	st_le32(&beep_dbdma_cmd->cmd_dep, virt_to_bus(beep_dbdma_cmd));
	st_le32(&beep_dbdma_cmd->phy_addr, virt_to_bus(beep_buf));
	awacs_beep_state = 1;

	spin_lock_irqsave(&cs4218_lock, flags);
	if (beep_playing) {	/* i.e. haven't been terminated already */
		out_le32(&awacs_txdma->control, (RUN|WAKE|FLUSH|PAUSE) << 16);
		out_le32(&awacs->control,
			 (in_le32(&awacs->control) & ~0x1f00)
			 | (beep_speed << 8));
		out_le32(&awacs->byteswap, 0);
		out_le32(&awacs_txdma->cmdptr, virt_to_bus(beep_dbdma_cmd));
		out_le32(&awacs_txdma->control, RUN | (RUN << 16));
	}
	spin_unlock_irqrestore(&cs4218_lock, flags);
#endif
}

static MACHINE mach_cs4218 = {
	.owner =	THIS_MODULE,
	.name =		"HIOX CS4218",
	.name2 =	"Built-in Sound",
	.dma_alloc =	CS_Alloc,
	.dma_free =	CS_Free,
	.irqinit =	CS_IrqInit,
#ifdef MODULE
	.irqcleanup =	CS_IrqCleanup,
#endif /* MODULE */
	.init =		CS_Init,
	.silence =	CS_Silence,
	.setFormat =	CS_SetFormat,
	.setVolume =	CS_SetVolume,
	.play =		CS_Play
};


/*** Mid level stuff *********************************************************/


static void sound_silence(void)
{
	/* update hardware settings one more */
	(*sound.mach.init)();

	(*sound.mach.silence)();
}


static void sound_init(void)
{
	(*sound.mach.init)();
}


static int sound_set_format(int format)
{
	return(*sound.mach.setFormat)(format);
}


static int sound_set_speed(int speed)
{
	if (speed < 0)
		return(sound.soft.speed);

	sound.soft.speed = speed;
	(*sound.mach.init)();
	if (sound.minDev == SND_DEV_DSP)
		sound.dsp.speed = sound.soft.speed;

	return(sound.soft.speed);
}


static int sound_set_stereo(int stereo)
{
	if (stereo < 0)
		return(sound.soft.stereo);

	stereo = !!stereo;    /* should be 0 or 1 now */

	sound.soft.stereo = stereo;
	if (sound.minDev == SND_DEV_DSP)
		sound.dsp.stereo = stereo;
	(*sound.mach.init)();

	return(stereo);
}


static int sound_set_volume(int volume)
{
	return(*sound.mach.setVolume)(volume);
}

static ssize_t sound_copy_translate(const u_char *userPtr,
				    size_t userCount,
				    u_char frame[], ssize_t *frameUsed,
				    ssize_t frameLeft)
{
	ssize_t (*ct_func)(const u_char *, size_t, u_char *, ssize_t *, ssize_t) = NULL;

	switch (sound.soft.format) {
	case AFMT_MU_LAW:
		ct_func = sound.trans_write->ct_ulaw;
		break;
	case AFMT_A_LAW:
		ct_func = sound.trans_write->ct_alaw;
		break;
	case AFMT_S8:
		ct_func = sound.trans_write->ct_s8;
		break;
	case AFMT_U8:
		ct_func = sound.trans_write->ct_u8;
		break;
	case AFMT_S16_BE:
		ct_func = sound.trans_write->ct_s16be;
		break;
	case AFMT_U16_BE:
		ct_func = sound.trans_write->ct_u16be;
		break;
	case AFMT_S16_LE:
		ct_func = sound.trans_write->ct_s16le;
		break;
	case AFMT_U16_LE:
		ct_func = sound.trans_write->ct_u16le;
		break;
	}
	if (ct_func)
		return ct_func(userPtr, userCount, frame, frameUsed, frameLeft);
	else
		return 0;
}

static ssize_t sound_copy_translate_read(const u_char *userPtr,
				    size_t userCount,
				    u_char frame[], ssize_t *frameUsed,
				    ssize_t frameLeft)
{
	ssize_t (*ct_func)(const u_char *, size_t, u_char *, ssize_t *, ssize_t) = NULL;

	switch (sound.soft.format) {
	case AFMT_MU_LAW:
		ct_func = sound.trans_read->ct_ulaw;
		break;
	case AFMT_A_LAW:
		ct_func = sound.trans_read->ct_alaw;
		break;
	case AFMT_S8:
		ct_func = sound.trans_read->ct_s8;
		break;
	case AFMT_U8:
		ct_func = sound.trans_read->ct_u8;
		break;
	case AFMT_S16_BE:
		ct_func = sound.trans_read->ct_s16be;
		break;
	case AFMT_U16_BE:
		ct_func = sound.trans_read->ct_u16be;
		break;
	case AFMT_S16_LE:
		ct_func = sound.trans_read->ct_s16le;
		break;
	case AFMT_U16_LE:
		ct_func = sound.trans_read->ct_u16le;
		break;
	}
	if (ct_func)
		return ct_func(userPtr, userCount, frame, frameUsed, frameLeft);
	else
		return 0;
}


/*
 * /dev/mixer abstraction
 */

static int mixer_open(struct inode *inode, struct file *file)
{
	mixer.busy = 1;
	return nonseekable_open(inode, file);
}


static int mixer_release(struct inode *inode, struct file *file)
{
	mixer.busy = 0;
	return 0;
}


static int mixer_ioctl(struct inode *inode, struct file *file, u_int cmd,
		       u_long arg)
{
	int data;
	uint tmpcs;

	if (_SIOC_DIR(cmd) & _SIOC_WRITE)
	    mixer.modify_counter++;
	if (cmd == OSS_GETVERSION)
	    return IOCTL_OUT(arg, SOUND_VERSION);
	switch (cmd) {
		case SOUND_MIXER_INFO: {
		    mixer_info info;
		    strlcpy(info.id, "CS4218_TDM", sizeof(info.id));
		    strlcpy(info.name, "CS4218_TDM", sizeof(info.name));
		    info.name[sizeof(info.name)-1] = 0;
		    info.modify_counter = mixer.modify_counter;
		    if (copy_to_user((int *)arg, &info, sizeof(info)))
		    		return -EFAULT;
		    return 0;
		}
		case SOUND_MIXER_READ_DEVMASK:
			data = SOUND_MASK_VOLUME | SOUND_MASK_LINE
				| SOUND_MASK_MIC | SOUND_MASK_RECLEV
				| SOUND_MASK_ALTPCM;
			return IOCTL_OUT(arg, data);
		case SOUND_MIXER_READ_RECMASK:
			data = SOUND_MASK_LINE | SOUND_MASK_MIC;
			return IOCTL_OUT(arg, data);
		case SOUND_MIXER_READ_RECSRC:
			if (cs4218_control & CS_DO1)
				data = SOUND_MASK_LINE;
			else
				data = SOUND_MASK_MIC;
			return IOCTL_OUT(arg, data);
		case SOUND_MIXER_WRITE_RECSRC:
			IOCTL_IN(arg, data);
			data &= (SOUND_MASK_LINE | SOUND_MASK_MIC);
			if (data & SOUND_MASK_LINE)
				tmpcs = cs4218_control |
						(CS_ISL | CS_ISR | CS_DO1);
			if (data & SOUND_MASK_MIC)
				tmpcs = cs4218_control &
						~(CS_ISL | CS_ISR | CS_DO1);
			if (tmpcs != cs4218_control)
				cs4218_ctl_write(tmpcs);
			return IOCTL_OUT(arg, data);
		case SOUND_MIXER_READ_STEREODEVS:
			data = SOUND_MASK_VOLUME | SOUND_MASK_RECLEV;
			return IOCTL_OUT(arg, data);
		case SOUND_MIXER_READ_CAPS:
			return IOCTL_OUT(arg, 0);
		case SOUND_MIXER_READ_VOLUME:
			data = (cs4218_control & CS_MUTE)? 0:
				cs_get_volume(cs4218_control);
			return IOCTL_OUT(arg, data);
		case SOUND_MIXER_WRITE_VOLUME:
			IOCTL_IN(arg, data);
			return IOCTL_OUT(arg, sound_set_volume(data));
		case SOUND_MIXER_WRITE_ALTPCM:	/* really bell volume */
			IOCTL_IN(arg, data);
			beep_volume = data & 0xff;
			/* fall through */
		case SOUND_MIXER_READ_ALTPCM:
			return IOCTL_OUT(arg, beep_volume);
		case SOUND_MIXER_WRITE_RECLEV:
			IOCTL_IN(arg, data);
			data = cs_set_gain(data);
			return IOCTL_OUT(arg, data);
		case SOUND_MIXER_READ_RECLEV:
			data = cs_get_gain(cs4218_control);
			return IOCTL_OUT(arg, data);
	}

	return -EINVAL;
}


static struct file_operations mixer_fops =
{
	.owner =	THIS_MODULE,
	.llseek =	sound_lseek,
	.ioctl =	mixer_ioctl,
	.open =		mixer_open,
	.release =	mixer_release,
};


static void __init mixer_init(void)
{
	mixer_unit = register_sound_mixer(&mixer_fops, -1);
	if (mixer_unit < 0)
		return;

	mixer.busy = 0;
	sound.treble = 0;
	sound.bass = 0;

	/* Set Line input, no gain, no attenuation.
	*/
	cs4218_control = CS_ISL | CS_ISR | CS_DO1;
	cs4218_control |= CS_LGAIN_SET(0) | CS_RGAIN_SET(0);
	cs4218_control |= CS_LATTEN_SET(0) | CS_RATTEN_SET(0);
	cs4218_ctl_write(cs4218_control);
}


/*
 * Sound queue stuff, the heart of the driver
 */


static int sq_allocate_buffers(void)
{
	int i;

	if (sound_buffers)
		return 0;
	sound_buffers = kmalloc (numBufs * sizeof(char *), GFP_KERNEL);
	if (!sound_buffers)
		return -ENOMEM;
	for (i = 0; i < numBufs; i++) {
		sound_buffers[i] = sound.mach.dma_alloc (bufSize << 10, GFP_KERNEL);
		if (!sound_buffers[i]) {
			while (i--)
				sound.mach.dma_free (sound_buffers[i], bufSize << 10);
			kfree (sound_buffers);
			sound_buffers = 0;
			return -ENOMEM;
		}
	}
	return 0;
}


static void sq_release_buffers(void)
{
	int i;

	if (sound_buffers) {
		for (i = 0; i < numBufs; i++)
			sound.mach.dma_free (sound_buffers[i], bufSize << 10);
		kfree (sound_buffers);
		sound_buffers = 0;
	}
}


static int sq_allocate_read_buffers(void)
{
	int i;

	if (sound_read_buffers)
		return 0;
	sound_read_buffers = kmalloc(numReadBufs * sizeof(char *), GFP_KERNEL);
	if (!sound_read_buffers)
		return -ENOMEM;
	for (i = 0; i < numBufs; i++) {
		sound_read_buffers[i] = sound.mach.dma_alloc (readbufSize<<10,
							      GFP_KERNEL);
		if (!sound_read_buffers[i]) {
			while (i--)
				sound.mach.dma_free (sound_read_buffers[i],
						     readbufSize << 10);
			kfree (sound_read_buffers);
			sound_read_buffers = 0;
			return -ENOMEM;
		}
	}
	return 0;
}

static void sq_release_read_buffers(void)
{
	int i;

	if (sound_read_buffers) {
		cpmp->cp_smc[1].smc_smcmr &= ~SMCMR_REN;
		for (i = 0; i < numReadBufs; i++)
			sound.mach.dma_free (sound_read_buffers[i],
					     bufSize << 10);
		kfree (sound_read_buffers);
		sound_read_buffers = 0;
	}
}


static void sq_setup(int numBufs, int bufSize, char **write_buffers)
{
	int i;
	volatile cbd_t *bdp;
	volatile cpm8xx_t	*cp;
	volatile smc_t	*sp;

	/* Make sure the SMC transmit is shut down.
	*/
	cp = cpmp;
	sp = &cpmp->cp_smc[1];
	sp->smc_smcmr &= ~SMCMR_TEN;

	sq.max_count = numBufs;
	sq.max_active = numBufs;
	sq.block_size = bufSize;
	sq.buffers = write_buffers;

	sq.front = sq.count = 0;
	sq.rear = -1;
	sq.syncing = 0;
	sq.active = 0;

	bdp = tx_base;
	for (i=0; i<numBufs; i++) {
		bdp->cbd_bufaddr = virt_to_bus(write_buffers[i]);
		bdp++;
	}

	/* This causes the SMC to sync up with the first buffer again.
	*/
	cp->cp_cpcr = mk_cr_cmd(CPM_CR_CH_SMC2, CPM_CR_INIT_TX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);
}

static void read_sq_setup(int numBufs, int bufSize, char **read_buffers)
{
	int i;
	volatile cbd_t *bdp;
	volatile cpm8xx_t	*cp;
	volatile smc_t	*sp;

	/* Make sure the SMC receive is shut down.
	*/
	cp = cpmp;
	sp = &cpmp->cp_smc[1];
	sp->smc_smcmr &= ~SMCMR_REN;

	read_sq.max_count = numBufs;
	read_sq.max_active = numBufs;
	read_sq.block_size = bufSize;
	read_sq.buffers = read_buffers;

	read_sq.front = read_sq.count = 0;
	read_sq.rear = 0;
	read_sq.rear_size = 0;
	read_sq.syncing = 0;
	read_sq.active = 0;

	bdp = rx_base;
	for (i=0; i<numReadBufs; i++) {
		bdp->cbd_bufaddr = virt_to_bus(read_buffers[i]);
		bdp->cbd_datlen = read_sq.block_size;
		bdp++;
	}

	/* This causes the SMC to sync up with the first buffer again.
	*/
	cp->cp_cpcr = mk_cr_cmd(CPM_CR_CH_SMC2, CPM_CR_INIT_RX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);
}


static void sq_play(void)
{
	(*sound.mach.play)();
}


/* ++TeSche: radically changed this one too */

static ssize_t sq_write(struct file *file, const char *src, size_t uLeft,
			loff_t *ppos)
{
	ssize_t uWritten = 0;
	u_char *dest;
	ssize_t uUsed, bUsed, bLeft;

	/* ++TeSche: Is something like this necessary?
	 * Hey, that's an honest question! Or does any other part of the
	 * filesystem already checks this situation? I really don't know.
	 */
	if (uLeft == 0)
		return 0;

	/* The interrupt doesn't start to play the last, incomplete frame.
	 * Thus we can append to it without disabling the interrupts! (Note
	 * also that sq.rear isn't affected by the interrupt.)
	 */

	if (sq.count > 0 && (bLeft = sq.block_size-sq.rear_size) > 0) {
		dest = sq_block_address(sq.rear);
		bUsed = sq.rear_size;
		uUsed = sound_copy_translate(src, uLeft, dest, &bUsed, bLeft);
		if (uUsed <= 0)
			return uUsed;
		src += uUsed;
		uWritten += uUsed;
		uLeft -= uUsed;
		sq.rear_size = bUsed;
	}

	do {
		while (sq.count == sq.max_active) {
			sq_play();
			if (NON_BLOCKING(sq.open_mode))
				return uWritten > 0 ? uWritten : -EAGAIN;
			SLEEP(sq.action_queue);
			if (SIGNAL_RECEIVED)
				return uWritten > 0 ? uWritten : -EINTR;
		}

		/* Here, we can avoid disabling the interrupt by first
		 * copying and translating the data, and then updating
		 * the sq variables. Until this is done, the interrupt
		 * won't see the new frame and we can work on it
		 * undisturbed.
		 */

		dest = sq_block_address((sq.rear+1) % sq.max_count);
		bUsed = 0;
		bLeft = sq.block_size;
		uUsed = sound_copy_translate(src, uLeft, dest, &bUsed, bLeft);
		if (uUsed <= 0)
			break;
		src += uUsed;
		uWritten += uUsed;
		uLeft -= uUsed;
		if (bUsed) {
			sq.rear = (sq.rear+1) % sq.max_count;
			sq.rear_size = bUsed;
			sq.count++;
		}
	} while (bUsed);   /* uUsed may have been 0 */

	sq_play();

	return uUsed < 0? uUsed: uWritten;
}


/***********/

/* Here is how the values are used for reading.
 * The value 'active' simply indicates the DMA is running.  This is
 * done so the driver semantics are DMA starts when the first read is
 * posted.  The value 'front' indicates the buffer we should next
 * send to the user.  The value 'rear' indicates the buffer the DMA is
 * currently filling.  When 'front' == 'rear' the buffer "ring" is
 * empty (we always have an empty available).  The 'rear_size' is used
 * to track partial offsets into the current buffer.  Right now, I just keep
 * The DMA running.  If the reader can't keep up, the interrupt tosses
 * the oldest buffer.  We could also shut down the DMA in this case.
 */
static ssize_t sq_read(struct file *file, char *dst, size_t uLeft,
                       loff_t *ppos)
{

	ssize_t	uRead, bLeft, bUsed, uUsed;

	if (uLeft == 0)
		return 0;

	if (!read_sq.active)
		CS_Record();	/* Kick off the record process. */

	uRead = 0;

	/* Move what the user requests, depending upon other options.
	*/
	while (uLeft > 0) {

		/* When front == rear, the DMA is not done yet.
		*/
		while (read_sq.front == read_sq.rear) {
			if (NON_BLOCKING(read_sq.open_mode)) {
			       return uRead > 0 ? uRead : -EAGAIN;
			}
			SLEEP(read_sq.action_queue);
			if (SIGNAL_RECEIVED)
				return uRead > 0 ? uRead : -EINTR;
		}

		/* The amount we move is either what is left in the
		 * current buffer or what the user wants.
		 */
		bLeft = read_sq.block_size - read_sq.rear_size;
		bUsed = read_sq.rear_size;
		uUsed = sound_copy_translate_read(dst, uLeft,
			read_sq.buffers[read_sq.front], &bUsed, bLeft);
		if (uUsed <= 0)
			return uUsed;
		dst += uUsed;
		uRead += uUsed;
		uLeft -= uUsed;
		read_sq.rear_size += bUsed;
		if (read_sq.rear_size >= read_sq.block_size) {
			read_sq.rear_size = 0;
			read_sq.front++;
			if (read_sq.front >= read_sq.max_active)
				read_sq.front = 0;
		}
	}
	return uRead;
}

static int sq_open(struct inode *inode, struct file *file)
{
	int rc = 0;

	if (file->f_mode & FMODE_WRITE) {
		if (sq.busy) {
			rc = -EBUSY;
			if (NON_BLOCKING(file->f_flags))
				goto err_out;
			rc = -EINTR;
			while (sq.busy) {
				SLEEP(sq.open_queue);
				if (SIGNAL_RECEIVED)
					goto err_out;
			}
		}
		sq.busy = 1; /* Let's play spot-the-race-condition */

		if (sq_allocate_buffers()) goto err_out_nobusy;

		sq_setup(numBufs, bufSize<<10,sound_buffers);
		sq.open_mode = file->f_mode;
	}


	if (file->f_mode & FMODE_READ) {
		if (read_sq.busy) {
			rc = -EBUSY;
			if (NON_BLOCKING(file->f_flags))
				goto err_out;
			rc = -EINTR;
			while (read_sq.busy) {
				SLEEP(read_sq.open_queue);
				if (SIGNAL_RECEIVED)
					goto err_out;
			}
			rc = 0;
		}
		read_sq.busy = 1;
		if (sq_allocate_read_buffers()) goto err_out_nobusy;

		read_sq_setup(numReadBufs,readbufSize<<10, sound_read_buffers);
		read_sq.open_mode = file->f_mode;
	}

	/* Start up the 4218 by:
	 * Reset.
	 * Enable, unreset.
	 */
	*((volatile uint *)HIOX_CSR4_ADDR) &= ~HIOX_CSR4_RSTAUDIO;
	eieio();
	*((volatile uint *)HIOX_CSR4_ADDR) |= HIOX_CSR4_ENAUDIO;
	mdelay(50);
	*((volatile uint *)HIOX_CSR4_ADDR) |= HIOX_CSR4_RSTAUDIO;

	/* We need to send the current control word in case someone
	 * opened /dev/mixer and changed things while we were shut
	 * down.  Chances are good the initialization that follows
	 * would have done this, but it is still possible it wouldn't.
	 */
	cs4218_ctl_write(cs4218_control);

	sound.minDev = iminor(inode) & 0x0f;
	sound.soft = sound.dsp;
	sound.hard = sound.dsp;
	sound_init();
	if ((iminor(inode) & 0x0f) == SND_DEV_AUDIO) {
		sound_set_speed(8000);
		sound_set_stereo(0);
		sound_set_format(AFMT_MU_LAW);
	}

	return nonseekable_open(inode, file);

err_out_nobusy:
	if (file->f_mode & FMODE_WRITE) {
		sq.busy = 0;
		WAKE_UP(sq.open_queue);
	}
	if (file->f_mode & FMODE_READ) {
		read_sq.busy = 0;
		WAKE_UP(read_sq.open_queue);
	}
err_out:
	return rc;
}


static void sq_reset(void)
{
	sound_silence();
	sq.active = 0;
	sq.count = 0;
	sq.front = (sq.rear+1) % sq.max_count;
#if 0
	init_tdm_buffers();
#endif
}


static int sq_fsync(struct file *filp, struct dentry *dentry)
{
	int rc = 0;

	sq.syncing = 1;
	sq_play();	/* there may be an incomplete frame waiting */

	while (sq.active) {
		SLEEP(sq.sync_queue);
		if (SIGNAL_RECEIVED) {
			/* While waiting for audio output to drain, an
			 * interrupt occurred.  Stop audio output immediately
			 * and clear the queue. */
			sq_reset();
			rc = -EINTR;
			break;
		}
	}

	sq.syncing = 0;
	return rc;
}

static int sq_release(struct inode *inode, struct file *file)
{
	int rc = 0;

	if (sq.busy)
		rc = sq_fsync(file, file->f_dentry);
	sound.soft = sound.dsp;
	sound.hard = sound.dsp;
	sound_silence();

	sq_release_read_buffers();
	sq_release_buffers();

	if (file->f_mode & FMODE_READ) {
		read_sq.busy = 0;
		WAKE_UP(read_sq.open_queue);
	}

	if (file->f_mode & FMODE_WRITE) {
		sq.busy = 0;
		WAKE_UP(sq.open_queue);
	}

	/* Shut down the SMC.
	*/
	cpmp->cp_smc[1].smc_smcmr &= ~(SMCMR_TEN | SMCMR_REN);

	/* Shut down the codec.
	*/
	*((volatile uint *)HIOX_CSR4_ADDR) |= HIOX_CSR4_RSTAUDIO;
	eieio();
	*((volatile uint *)HIOX_CSR4_ADDR) &= ~HIOX_CSR4_ENAUDIO;

	/* Wake up a process waiting for the queue being released.
	 * Note: There may be several processes waiting for a call
	 * to open() returning. */

	return rc;
}


static int sq_ioctl(struct inode *inode, struct file *file, u_int cmd,
		    u_long arg)
{
	u_long fmt;
	int data;
#if 0
	int size, nbufs;
#else
	int size;
#endif

	switch (cmd) {
	case SNDCTL_DSP_RESET:
		sq_reset();
		return 0;
	case SNDCTL_DSP_POST:
	case SNDCTL_DSP_SYNC:
		return sq_fsync(file, file->f_dentry);

		/* ++TeSche: before changing any of these it's
		 * probably wise to wait until sound playing has
		 * settled down. */
	case SNDCTL_DSP_SPEED:
		sq_fsync(file, file->f_dentry);
		IOCTL_IN(arg, data);
		return IOCTL_OUT(arg, sound_set_speed(data));
	case SNDCTL_DSP_STEREO:
		sq_fsync(file, file->f_dentry);
		IOCTL_IN(arg, data);
		return IOCTL_OUT(arg, sound_set_stereo(data));
	case SOUND_PCM_WRITE_CHANNELS:
		sq_fsync(file, file->f_dentry);
		IOCTL_IN(arg, data);
		return IOCTL_OUT(arg, sound_set_stereo(data-1)+1);
	case SNDCTL_DSP_SETFMT:
		sq_fsync(file, file->f_dentry);
		IOCTL_IN(arg, data);
		return IOCTL_OUT(arg, sound_set_format(data));
	case SNDCTL_DSP_GETFMTS:
		fmt = 0;
		if (sound.trans_write) {
			if (sound.trans_write->ct_ulaw)
				fmt |= AFMT_MU_LAW;
			if (sound.trans_write->ct_alaw)
				fmt |= AFMT_A_LAW;
			if (sound.trans_write->ct_s8)
				fmt |= AFMT_S8;
			if (sound.trans_write->ct_u8)
				fmt |= AFMT_U8;
			if (sound.trans_write->ct_s16be)
				fmt |= AFMT_S16_BE;
			if (sound.trans_write->ct_u16be)
				fmt |= AFMT_U16_BE;
			if (sound.trans_write->ct_s16le)
				fmt |= AFMT_S16_LE;
			if (sound.trans_write->ct_u16le)
				fmt |= AFMT_U16_LE;
		}
		return IOCTL_OUT(arg, fmt);
	case SNDCTL_DSP_GETBLKSIZE:
		size = sq.block_size
			* sound.soft.size * (sound.soft.stereo + 1)
			/ (sound.hard.size * (sound.hard.stereo + 1));
		return IOCTL_OUT(arg, size);
	case SNDCTL_DSP_SUBDIVIDE:
		break;
#if 0	/* Sorry can't do this at the moment.  The CPM allocated buffers
	 * long ago that can't be changed.
	 */
	case SNDCTL_DSP_SETFRAGMENT:
		if (sq.count || sq.active || sq.syncing)
			return -EINVAL;
		IOCTL_IN(arg, size);
		nbufs = size >> 16;
		if (nbufs < 2 || nbufs > numBufs)
			nbufs = numBufs;
		size &= 0xffff;
		if (size >= 8 && size <= 30) {
			size = 1 << size;
			size *= sound.hard.size * (sound.hard.stereo + 1);
			size /= sound.soft.size * (sound.soft.stereo + 1);
			if (size > (bufSize << 10))
				size = bufSize << 10;
		} else
			size = bufSize << 10;
		sq_setup(numBufs, size, sound_buffers);
		sq.max_active = nbufs;
		return 0;
#endif

	default:
		return mixer_ioctl(inode, file, cmd, arg);
	}
	return -EINVAL;
}



static struct file_operations sq_fops =
{
	.owner =	THIS_MODULE,
	.llseek =	sound_lseek,
	.read =		sq_read,			/* sq_read */
	.write =	sq_write,
	.ioctl =	sq_ioctl,
	.open =		sq_open,
	.release =	sq_release,
};


static void __init sq_init(void)
{
	sq_unit = register_sound_dsp(&sq_fops, -1);
	if (sq_unit < 0)
		return;

	init_waitqueue_head(&sq.action_queue);
	init_waitqueue_head(&sq.open_queue);
	init_waitqueue_head(&sq.sync_queue);
	init_waitqueue_head(&read_sq.action_queue);
	init_waitqueue_head(&read_sq.open_queue);
	init_waitqueue_head(&read_sq.sync_queue);

	sq.busy = 0;
	read_sq.busy = 0;

	/* whatever you like as startup mode for /dev/dsp,
	 * (/dev/audio hasn't got a startup mode). note that
	 * once changed a new open() will *not* restore these!
	 */
	sound.dsp.format = AFMT_S16_BE;
	sound.dsp.stereo = 1;
	sound.dsp.size = 16;

	/* set minimum rate possible without expanding */
	sound.dsp.speed = 8000;

	/* before the first open to /dev/dsp this wouldn't be set */
	sound.soft = sound.dsp;
	sound.hard = sound.dsp;

	sound_silence();
}

/*
 * /dev/sndstat
 */


/* state.buf should not overflow! */

static int state_open(struct inode *inode, struct file *file)
{
	char *buffer = state.buf, *mach = "", cs4218_buf[50];
	int len = 0;

	if (state.busy)
		return -EBUSY;

	state.ptr = 0;
	state.busy = 1;

	sprintf(cs4218_buf, "Crystal CS4218 on TDM, ");
	mach = cs4218_buf;

	len += sprintf(buffer+len, "%sDMA sound driver:\n", mach);

	len += sprintf(buffer+len, "\tsound.format = 0x%x", sound.soft.format);
	switch (sound.soft.format) {
	case AFMT_MU_LAW:
		len += sprintf(buffer+len, " (mu-law)");
		break;
	case AFMT_A_LAW:
		len += sprintf(buffer+len, " (A-law)");
		break;
	case AFMT_U8:
		len += sprintf(buffer+len, " (unsigned 8 bit)");
		break;
	case AFMT_S8:
		len += sprintf(buffer+len, " (signed 8 bit)");
		break;
	case AFMT_S16_BE:
		len += sprintf(buffer+len, " (signed 16 bit big)");
		break;
	case AFMT_U16_BE:
		len += sprintf(buffer+len, " (unsigned 16 bit big)");
		break;
	case AFMT_S16_LE:
		len += sprintf(buffer+len, " (signed 16 bit little)");
		break;
	case AFMT_U16_LE:
		len += sprintf(buffer+len, " (unsigned 16 bit little)");
		break;
	}
	len += sprintf(buffer+len, "\n");
	len += sprintf(buffer+len, "\tsound.speed = %dHz (phys. %dHz)\n",
		       sound.soft.speed, sound.hard.speed);
	len += sprintf(buffer+len, "\tsound.stereo = 0x%x (%s)\n",
		       sound.soft.stereo, sound.soft.stereo ? "stereo" : "mono");
	len += sprintf(buffer+len, "\tsq.block_size = %d sq.max_count = %d"
		       " sq.max_active = %d\n",
		       sq.block_size, sq.max_count, sq.max_active);
	len += sprintf(buffer+len, "\tsq.count = %d sq.rear_size = %d\n", sq.count,
		       sq.rear_size);
	len += sprintf(buffer+len, "\tsq.active = %d sq.syncing = %d\n",
		       sq.active, sq.syncing);
	state.len = len;
	return nonseekable_open(inode, file);
}


static int state_release(struct inode *inode, struct file *file)
{
	state.busy = 0;
	return 0;
}


static ssize_t state_read(struct file *file, char *buf, size_t count,
			  loff_t *ppos)
{
	int n = state.len - state.ptr;
	if (n > count)
		n = count;
	if (n <= 0)
		return 0;
	if (copy_to_user(buf, &state.buf[state.ptr], n))
		return -EFAULT;
	state.ptr += n;
	return n;
}


static struct file_operations state_fops =
{
	.owner =	THIS_MODULE,
	.llseek =	sound_lseek,
	.read =		state_read,
	.open =		state_open,
	.release =	state_release,
};


static void __init state_init(void)
{
	state_unit = register_sound_special(&state_fops, SND_DEV_STATUS);
	if (state_unit < 0)
		return;
	state.busy = 0;
}


/*** Common stuff ********************************************************/

static long long sound_lseek(struct file *file, long long offset, int orig)
{
	return -ESPIPE;
}


/*** Config & Setup **********************************************************/


int __init tdm8xx_sound_init(void)
{
	int i, has_sound;
	uint			dp_offset;
	volatile uint		*sirp;
	volatile cbd_t		*bdp;
	volatile cpm8xx_t	*cp;
	volatile smc_t		*sp;
	volatile smc_uart_t	*up;
	volatile immap_t	*immap;

	has_sound = 0;

	/* Program the SI/TSA to use TDMa, connected to SMC2, for 4 bytes.
	*/
	cp = cpmp;	/* Get pointer to Communication Processor */
	immap = (immap_t *)IMAP_ADDR;	/* and to internal registers */

	/* Set all TDMa control bits to zero.  This enables most features
	 * we want.
	 */
	cp->cp_simode &= ~0x00000fff;

	/* Enable common receive/transmit clock pins, use IDL format.
	 * Sync on falling edge, transmit rising clock, receive falling
	 * clock, delay 1 bit on both Tx and Rx.  Common Tx/Rx clocks and
	 * sync.
	 * Connect SMC2 to TSA.
	 */
	cp->cp_simode |= 0x80000141;

	/* Configure port A pins for TDMa operation.
	 * The RPX-Lite (MPC850/823) loses SMC2 when TDM is used.
	 */
	immap->im_ioport.iop_papar |= 0x01c0; /* Enable TDMa functions */
	immap->im_ioport.iop_padir |= 0x00c0; /* Enable TDMa Tx/Rx */
	immap->im_ioport.iop_padir &= ~0x0100; /* Enable L1RCLKa */

	immap->im_ioport.iop_pcpar |= 0x0800; /* Enable L1RSYNCa */
	immap->im_ioport.iop_pcdir &= ~0x0800;

	/* Initialize the SI TDM routing table.  We use TDMa only.
	 * The receive table and transmit table each have only one
	 * entry, to capture/send four bytes after each frame pulse.
	 * The 16-bit ram entry is 0000 0001 1000 1111. (SMC2)
	 */
	cp->cp_sigmr = 0;
	sirp = (uint *)cp->cp_siram;

	*sirp = 0x018f0000;		/* Receive entry */
	sirp += 64;
	*sirp = 0x018f0000;		/* Tramsmit entry */

	/* Enable single TDMa routing.
	*/
	cp->cp_sigmr = 0x04;

	/* Initialize the SMC for transparent operation.
	*/
	sp = &cpmp->cp_smc[1];
	up = (smc_uart_t *)&cp->cp_dparam[PROFF_SMC2];

	/* We need to allocate a transmit and receive buffer
	 * descriptors from dual port ram.
	 */
	dp_addr = cpm_dpalloc(sizeof(cbd_t) * numReadBufs, 8);

	/* Set the physical address of the host memory
	 * buffers in the buffer descriptors, and the
	 * virtual address for us to work with.
	 */
	bdp = (cbd_t *)&cp->cp_dpmem[dp_addr];
	up->smc_rbase = dp_offset;
	rx_cur = rx_base = (cbd_t *)bdp;

	for (i=0; i<(numReadBufs-1); i++) {
		bdp->cbd_bufaddr = 0;
		bdp->cbd_datlen = 0;
		bdp->cbd_sc = BD_SC_EMPTY | BD_SC_INTRPT;
		bdp++;
	}
	bdp->cbd_bufaddr = 0;
	bdp->cbd_datlen = 0;
	bdp->cbd_sc = BD_SC_WRAP | BD_SC_EMPTY | BD_SC_INTRPT;

	/* Now, do the same for the transmit buffers.
	*/
	dp_offset = cpm_dpalloc(sizeof(cbd_t) * numBufs, 8);

	bdp = (cbd_t *)&cp->cp_dpmem[dp_addr];
	up->smc_tbase = dp_offset;
	tx_cur = tx_base = (cbd_t *)bdp;

	for (i=0; i<(numBufs-1); i++) {
		bdp->cbd_bufaddr = 0;
		bdp->cbd_datlen = 0;
		bdp->cbd_sc = BD_SC_INTRPT;
		bdp++;
	}
	bdp->cbd_bufaddr = 0;
	bdp->cbd_datlen = 0;
	bdp->cbd_sc = (BD_SC_WRAP | BD_SC_INTRPT);

	/* Set transparent SMC mode.
	 * A few things are specific to our application.  The codec interface
	 * is MSB first, hence the REVD selection.  The CD/CTS pulse are
	 * used by the TSA to indicate the frame start to the SMC.
	 */
	up->smc_rfcr = SCC_EB;
	up->smc_tfcr = SCC_EB;
	up->smc_mrblr = readbufSize * 1024;

	/* Set 16-bit reversed data, transparent mode.
	*/
	sp->smc_smcmr = smcr_mk_clen(15) |
		SMCMR_SM_TRANS | SMCMR_REVD | SMCMR_BS;

	/* Enable and clear events.
	 * Because of FIFO delays, all we need is the receive interrupt
	 * and we can process both the current receive and current
	 * transmit interrupt within a few microseconds of the transmit.
	 */
	sp->smc_smce = 0xff;
	sp->smc_smcm = SMCM_TXE | SMCM_TX | SMCM_RX;

	/* Send the CPM an initialize command.
	*/
	cp->cp_cpcr = mk_cr_cmd(CPM_CR_CH_SMC2,
				CPM_CR_INIT_TRX) | CPM_CR_FLG;
	while (cp->cp_cpcr & CPM_CR_FLG);

	sound.mach = mach_cs4218;
	has_sound = 1;

	/* Initialize beep stuff */
	orig_mksound = kd_mksound;
	kd_mksound = cs_mksound;
	beep_buf = (short *) kmalloc(BEEP_BUFLEN * 4, GFP_KERNEL);
	if (beep_buf == NULL)
		printk(KERN_WARNING "dmasound: no memory for "
		       "beep buffer\n");

	if (!has_sound)
		return -ENODEV;

	/* Initialize the software SPI.
	*/
	sw_spi_init();

	/* Set up sound queue, /dev/audio and /dev/dsp. */

	/* Set default settings. */
	sq_init();

	/* Set up /dev/sndstat. */
	state_init();

	/* Set up /dev/mixer. */
	mixer_init();

	if (!sound.mach.irqinit()) {
		printk(KERN_ERR "DMA sound driver: Interrupt initialization failed\n");
		return -ENODEV;
	}
#ifdef MODULE
	irq_installed = 1;
#endif

	printk(KERN_INFO "DMA sound driver installed, using %d buffers of %dk.\n",
	       numBufs, bufSize);

	return 0;
}

/* Due to FIFOs and bit delays, the transmit interrupt occurs a few
 * microseconds ahead of the receive interrupt.
 * When we get an interrupt, we service the transmit first, then
 * check for a receive to prevent the overhead of returning through
 * the interrupt handler only to get back here right away during
 * full duplex operation.
 */
static void
cs4218_intr(void *dev_id)
{
	volatile smc_t	*sp;
	volatile cpm8xx_t	*cp;

	sp = &cpmp->cp_smc[1];

	if (sp->smc_smce & SCCM_TX) {
		sp->smc_smce = SCCM_TX;
		cs4218_tdm_tx_intr((void *)sp);
	}

	if (sp->smc_smce & SCCM_RX) {
		sp->smc_smce = SCCM_RX;
		cs4218_tdm_rx_intr((void *)sp);
	}

	if (sp->smc_smce & SCCM_TXE) {
		/* Transmit underrun.  This happens with the application
		 * didn't keep up sending buffers.  We tell the SMC to
		 * restart, which will cause it to poll the current (next)
		 * BD.  If the user supplied data since this occurred,
		 * we just start running again.  If they didn't, the SMC
		 * will poll the descriptor until data is placed there.
		 */
		sp->smc_smce = SCCM_TXE;
		cp = cpmp;	/* Get pointer to Communication Processor */
		cp->cp_cpcr = mk_cr_cmd(CPM_CR_CH_SMC2,
					CPM_CR_RESTART_TX) | CPM_CR_FLG;
		while (cp->cp_cpcr & CPM_CR_FLG);
	}
}


#define MAXARGS		8	/* Should be sufficient for now */

void __init dmasound_setup(char *str, int *ints)
{
	/* check the bootstrap parameter for "dmasound=" */

	switch (ints[0]) {
	case 3:
		if ((ints[3] < 0) || (ints[3] > MAX_CATCH_RADIUS))
			printk("dmasound_setup: invalid catch radius, using default = %d\n", catchRadius);
		else
			catchRadius = ints[3];
		/* fall through */
	case 2:
		if (ints[1] < MIN_BUFFERS)
			printk("dmasound_setup: invalid number of buffers, using default = %d\n", numBufs);
		else
			numBufs = ints[1];
		if (ints[2] < MIN_BUFSIZE || ints[2] > MAX_BUFSIZE)
			printk("dmasound_setup: invalid buffer size, using default = %d\n", bufSize);
		else
			bufSize = ints[2];
		break;
	case 0:
		break;
	default:
		printk("dmasound_setup: invalid number of arguments\n");
	}
}

/* Software SPI functions.
 * These are on Port B.
 */
#define PB_SPICLK	((uint)0x00000002)
#define PB_SPIMOSI	((uint)0x00000004)
#define PB_SPIMISO	((uint)0x00000008)

static
void	sw_spi_init(void)
{
	volatile cpm8xx_t	*cp;
	volatile uint		*hcsr4;

	hcsr4 = (volatile uint *)HIOX_CSR4_ADDR;
	cp = cpmp;	/* Get pointer to Communication Processor */

	*hcsr4 &= ~HIOX_CSR4_AUDSPISEL;	/* Disable SPI select */

	/* Make these Port B signals general purpose I/O.
	 * First, make sure the clock is low.
	 */
	cp->cp_pbdat &= ~PB_SPICLK;
	cp->cp_pbpar &= ~(PB_SPICLK | PB_SPIMOSI | PB_SPIMISO);

	/* Clock and Master Output are outputs.
	*/
	cp->cp_pbdir |= (PB_SPICLK | PB_SPIMOSI);

	/* Master Input.
	*/
	cp->cp_pbdir &= ~PB_SPIMISO;

}

/* Write the CS4218 control word out the SPI port.  While the
 * the control word is going out, the status word is arriving.
 */
static
uint	cs4218_ctl_write(uint ctlreg)
{
	uint	status;

	sw_spi_io((u_char *)&ctlreg, (u_char *)&status, 4);

	/* Shadow the control register.....I guess we could do
	 * the same for the status, but for now we just return it
	 * and let the caller decide.
	 */
	cs4218_control = ctlreg;
	return status;
}

static
void	sw_spi_io(u_char *obuf, u_char *ibuf, uint bcnt)
{
	int	bits, i;
	u_char	outbyte, inbyte;
	volatile cpm8xx_t	*cp;
	volatile uint		*hcsr4;

	hcsr4 = (volatile uint *)HIOX_CSR4_ADDR;
	cp = cpmp;	/* Get pointer to Communication Processor */

	/* The timing on the bus is pretty slow.  Code inefficiency
	 * and eieio() is our friend here :-).
	 */
	cp->cp_pbdat &= ~PB_SPICLK;
	*hcsr4 |= HIOX_CSR4_AUDSPISEL;	/* Enable SPI select */
	eieio();

	/* Clock in/out the bytes.  Data is valid on the falling edge
	 * of the clock.  Data is MSB first.
	 */
	for (i=0; i<bcnt; i++) {
		outbyte = *obuf++;
		inbyte = 0;
		for (bits=0; bits<8; bits++) {
			eieio();
			cp->cp_pbdat |= PB_SPICLK;
			eieio();
			if (outbyte & 0x80)
				cp->cp_pbdat |= PB_SPIMOSI;
			else
				cp->cp_pbdat &= ~PB_SPIMOSI;
			eieio();
			cp->cp_pbdat &= ~PB_SPICLK;
			eieio();
			outbyte <<= 1;
			inbyte <<= 1;
			if (cp->cp_pbdat & PB_SPIMISO)
				inbyte |= 1;
		}
		*ibuf++ = inbyte;
	}

	*hcsr4 &= ~HIOX_CSR4_AUDSPISEL;	/* Disable SPI select */
	eieio();
}

void cleanup_module(void)
{
	if (irq_installed) {
		sound_silence();
#ifdef MODULE
		sound.mach.irqcleanup();
#endif
	}

	sq_release_read_buffers();
	sq_release_buffers();

	if (mixer_unit >= 0)
		unregister_sound_mixer(mixer_unit);
	if (state_unit >= 0)
		unregister_sound_special(state_unit);
	if (sq_unit >= 0)
		unregister_sound_dsp(sq_unit);
}

module_init(tdm8xx_sound_init);
module_exit(cleanup_module);

