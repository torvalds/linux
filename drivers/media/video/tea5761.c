/*
 * For Philips TEA5761 FM Chip
 * I2C address is allways 0x20 (0x10 at 7-bit mode).
 *
 * Copyright (c) 2005-2007 Mauro Carvalho Chehab (mchehab@infradead.org)
 * This code is placed under the terms of the GNUv2 General Public License
 *
 */

#include <linux/i2c.h>
#include <linux/videodev.h>
#include <linux/delay.h>
#include <media/tuner.h>
#include "tuner-driver.h"

#define PREFIX "TEA5761 "

/* from tuner-core.c */
extern int tuner_debug;

/*****************************************************************************/

/***************************
 * TEA5761HN I2C registers *
 ***************************/

/* INTREG - Read: bytes 0 and 1 / Write: byte 0 */

	/* first byte for reading */
#define TEA5761_INTREG_IFFLAG		0x10
#define TEA5761_INTREG_LEVFLAG		0x8
#define TEA5761_INTREG_FRRFLAG		0x2
#define TEA5761_INTREG_BLFLAG		0x1

	/* second byte for reading / byte for writing */
#define TEA5761_INTREG_IFMSK		0x10
#define TEA5761_INTREG_LEVMSK		0x8
#define TEA5761_INTREG_FRMSK		0x2
#define TEA5761_INTREG_BLMSK		0x1

/* FRQSET - Read: bytes 2 and 3 / Write: byte 1 and 2 */

	/* First byte */
#define TEA5761_FRQSET_SEARCH_UP 0x80		/* 1=Station search from botton to up */
#define TEA5761_FRQSET_SEARCH_MODE 0x40		/* 1=Search mode */

	/* Bits 0-5 for divider MSB */

	/* Second byte */
	/* Bits 0-7 for divider LSB */

/* TNCTRL - Read: bytes 4 and 5 / Write: Bytes 3 and 4 */

	/* first byte */

#define TEA5761_TNCTRL_PUPD_0	0x40	/* Power UP/Power Down MSB */
#define TEA5761_TNCTRL_BLIM	0X20	/* 1= Japan Frequencies, 0= European frequencies */
#define TEA5761_TNCTRL_SWPM	0x10	/* 1= software port is FRRFLAG */
#define TEA5761_TNCTRL_IFCTC	0x08	/* 1= IF count time 15.02 ms, 0= IF count time 2.02 ms */
#define TEA5761_TNCTRL_AFM	0x04
#define TEA5761_TNCTRL_SMUTE	0x02	/* 1= Soft mute */
#define TEA5761_TNCTRL_SNC	0x01

	/* second byte */

#define TEA5761_TNCTRL_MU	0x80	/* 1=Hard mute */
#define TEA5761_TNCTRL_SSL_1	0x40
#define TEA5761_TNCTRL_SSL_0	0x20
#define TEA5761_TNCTRL_HLSI	0x10
#define TEA5761_TNCTRL_MST	0x08	/* 1 = mono */
#define TEA5761_TNCTRL_SWP	0x04
#define TEA5761_TNCTRL_DTC	0x02	/* 1 = deemphasis 50 us, 0 = deemphasis 75 us */
#define TEA5761_TNCTRL_AHLSI	0x01

/* FRQCHECK - Read: bytes 6 and 7  */
	/* First byte */

	/* Bits 0-5 for divider MSB */

	/* Second byte */
	/* Bits 0-7 for divider LSB */

/* TUNCHECK - Read: bytes 8 and 9  */

	/* First byte */
#define TEA5761_TUNCHECK_IF_MASK	0x7e	/* IF count */
#define TEA5761_TUNCHECK_TUNTO		0x01

	/* Second byte */
#define TEA5761_TUNCHECK_LEV_MASK	0xf0	/* Level Count */
#define TEA5761_TUNCHECK_LD		0x08
#define TEA5761_TUNCHECK_STEREO		0x04

/* TESTREG - Read: bytes 10 and 11 / Write: bytes 5 and 6 */

	/* All zero = no test mode */

/* MANID - Read: bytes 12 and 13 */

	/* First byte - should be 0x10 */
#define TEA5767_MANID_VERSION_MASK	0xf0	/* Version = 1 */
#define TEA5767_MANID_ID_MSB_MASK	0x0f	/* Manufacurer ID - should be 0 */

	/* Second byte - Should be 0x2b */

#define TEA5767_MANID_ID_LSB_MASK	0xfe	/* Manufacturer ID - should be 0x15 */
#define TEA5767_MANID_IDAV		0x01	/* 1 = Chip has ID, 0 = Chip has no ID */

/* Chip ID - Read: bytes 14 and 15 */

	/* First byte - should be 0x57 */

	/* Second byte - should be 0x61 */

/*****************************************************************************/

static void set_tv_freq(struct i2c_client *c, unsigned int freq)
{
	struct tuner *t = i2c_get_clientdata(c);

	tuner_warn("This tuner doesn't support TV freq.\n");
}

#define FREQ_OFFSET 0 /* for TEA5767, it is 700 to give the right freq */
static void tea5761_status_dump(unsigned char *buffer)
{
	unsigned int div, frq;

	div = ((buffer[2] & 0x3f) << 8) | buffer[3];

	frq = 1000 * (div * 32768 / 1000 + FREQ_OFFSET + 225) / 4;	/* Freq in KHz */

	printk(PREFIX "Frequency %d.%03d KHz (divider = 0x%04x)\n",
	       frq / 1000, frq % 1000, div);
}

/* Freq should be specifyed at 62.5 Hz */
static void set_radio_freq(struct i2c_client *c, unsigned int frq)
{
	struct tuner *t = i2c_get_clientdata(c);
	unsigned char buffer[7] = {0, 0, 0, 0, 0, 0, 0 };
	unsigned div;
	int rc;

	tuner_dbg (PREFIX "radio freq counter %d\n", frq);

	if (t->mode == T_STANDBY) {
		tuner_dbg("TEA5761 set to standby mode\n");
		buffer[5] |= TEA5761_TNCTRL_MU;
	} else {
		buffer[4] |= TEA5761_TNCTRL_PUPD_0;
	}


	if (t->audmode == V4L2_TUNER_MODE_MONO) {
		tuner_dbg("TEA5761 set to mono\n");
		buffer[5] |= TEA5761_TNCTRL_MST;
;
	} else {
		tuner_dbg("TEA5761 set to stereo\n");
	}

	div = (1000 * (frq * 4 / 16 + 700 + 225) ) >> 15;
	buffer[1] = (div >> 8) & 0x3f;
	buffer[2] = div & 0xff;

	if (tuner_debug)
		tea5761_status_dump(buffer);

	if (7 != (rc = i2c_master_send(c, buffer, 7)))
		tuner_warn("i2c i/o error: rc == %d (should be 5)\n", rc);
}

static int tea5761_signal(struct i2c_client *c)
{
	unsigned char buffer[16];
	int rc;
	struct tuner *t = i2c_get_clientdata(c);

	memset(buffer, 0, sizeof(buffer));
	if (16 != (rc = i2c_master_recv(c, buffer, 16)))
		tuner_warn("i2c i/o error: rc == %d (should be 5)\n", rc);

	return ((buffer[9] & TEA5761_TUNCHECK_LEV_MASK) << (13 - 4));
}

static int tea5761_stereo(struct i2c_client *c)
{
	unsigned char buffer[16];
	int rc;
	struct tuner *t = i2c_get_clientdata(c);

	memset(buffer, 0, sizeof(buffer));
	if (16 != (rc = i2c_master_recv(c, buffer, 16)))
		tuner_warn("i2c i/o error: rc == %d (should be 5)\n", rc);

	rc = buffer[9] & TEA5761_TUNCHECK_STEREO;

	tuner_dbg("TEA5761 radio ST GET = %02x\n", rc);

	return (rc ? V4L2_TUNER_SUB_STEREO : 0);
}

int tea5761_autodetection(struct i2c_client *c)
{
	unsigned char buffer[16];
	int rc;
	struct tuner *t = i2c_get_clientdata(c);

	if (16 != (rc = i2c_master_recv(c, buffer, 16))) {
		tuner_warn("it is not a TEA5761. Received %i chars.\n", rc);
		return EINVAL;
	}

	if (!((buffer[13] != 0x2b) || (buffer[14] != 0x57) || (buffer[15] != 0x061))) {
		tuner_warn("Manufacturer ID= 0x%02x, Chip ID = %02x%02x. It is not a TEA5761\n",buffer[13],buffer[14],buffer[15]);
		return EINVAL;
	}
	tuner_warn("TEA5761 detected.\n");
	return 0;
}

static struct tuner_operations tea5761_tuner_ops = {
	.set_tv_freq    = set_tv_freq,
	.set_radio_freq = set_radio_freq,
	.has_signal     = tea5761_signal,
	.is_stereo      = tea5761_stereo,
};

int tea5761_tuner_init(struct i2c_client *c)
{
	struct tuner *t = i2c_get_clientdata(c);

	if (tea5761_autodetection(c) == EINVAL)
		return EINVAL;

	tuner_info("type set to %d (%s)\n", t->type, "Philips TEA5761HN FM Radio");
	strlcpy(c->name, "tea5761", sizeof(c->name));

	memcpy(&t->ops, &tea5761_tuner_ops, sizeof(struct tuner_operations));

	return (0);
}
