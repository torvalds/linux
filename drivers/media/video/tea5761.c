/*
 * For Philips TEA5761 FM Chip
 * I2C address is allways 0x20 (0x10 at 7-bit mode).
 *
 * Copyright (c) 2005-2007 Mauro Carvalho Chehab (mchehab@infradead.org)
 * This code is placed under the terms of the GNUv2 General Public License
 *
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev.h>
#include <media/tuner.h>
#include "tuner-i2c.h"
#include "tea5761.h"

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable verbose debug messages");

#define PREFIX "tea5761"

struct tea5761_priv {
	struct tuner_i2c_props i2c_props;

	u32 frequency;
};

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
static int set_radio_freq(struct dvb_frontend *fe,
			  struct analog_parameters *params)
{
	struct tea5761_priv *priv = fe->tuner_priv;
	unsigned int frq = params->frequency;
	unsigned char buffer[7] = {0, 0, 0, 0, 0, 0, 0 };
	unsigned div;
	int rc;

	tuner_dbg("radio freq counter %d\n", frq);

	if (params->mode == T_STANDBY) {
		tuner_dbg("TEA5761 set to standby mode\n");
		buffer[5] |= TEA5761_TNCTRL_MU;
	} else {
		buffer[4] |= TEA5761_TNCTRL_PUPD_0;
	}


	if (params->audmode == V4L2_TUNER_MODE_MONO) {
		tuner_dbg("TEA5761 set to mono\n");
		buffer[5] |= TEA5761_TNCTRL_MST;
	} else {
		tuner_dbg("TEA5761 set to stereo\n");
	}

	div = (1000 * (frq * 4 / 16 + 700 + 225) ) >> 15;
	buffer[1] = (div >> 8) & 0x3f;
	buffer[2] = div & 0xff;

	if (debug)
		tea5761_status_dump(buffer);

	if (7 != (rc = tuner_i2c_xfer_send(&priv->i2c_props, buffer, 7)))
		tuner_warn("i2c i/o error: rc == %d (should be 5)\n", rc);

	priv->frequency = frq * 125 / 2;

	return 0;
}

static int tea5761_read_status(struct dvb_frontend *fe, char *buffer)
{
	struct tea5761_priv *priv = fe->tuner_priv;
	int rc;

	memset(buffer, 0, 16);
	if (16 != (rc = tuner_i2c_xfer_recv(&priv->i2c_props, buffer, 16))) {
		tuner_warn("i2c i/o error: rc == %d (should be 16)\n", rc);
		return -EREMOTEIO;
	}

	return 0;
}

static inline int tea5761_signal(struct dvb_frontend *fe, const char *buffer)
{
	struct tea5761_priv *priv = fe->tuner_priv;

	int signal = ((buffer[9] & TEA5761_TUNCHECK_LEV_MASK) << (13 - 4));

	tuner_dbg("Signal strength: %d\n", signal);

	return signal;
}

static inline int tea5761_stereo(struct dvb_frontend *fe, const char *buffer)
{
	struct tea5761_priv *priv = fe->tuner_priv;

	int stereo = buffer[9] & TEA5761_TUNCHECK_STEREO;

	tuner_dbg("Radio ST GET = %02x\n", stereo);

	return (stereo ? V4L2_TUNER_SUB_STEREO : 0);
}

static int tea5761_get_status(struct dvb_frontend *fe, u32 *status)
{
	unsigned char buffer[16];

	*status = 0;

	if (0 == tea5761_read_status(fe, buffer)) {
		if (tea5761_signal(fe, buffer))
			*status = TUNER_STATUS_LOCKED;
		if (tea5761_stereo(fe, buffer))
			*status |= TUNER_STATUS_STEREO;
	}

	return 0;
}

static int tea5761_get_rf_strength(struct dvb_frontend *fe, u16 *strength)
{
	unsigned char buffer[16];

	*strength = 0;

	if (0 == tea5761_read_status(fe, buffer))
		*strength = tea5761_signal(fe, buffer);

	return 0;
}

int tea5761_autodetection(struct i2c_adapter* i2c_adap, u8 i2c_addr)
{
	unsigned char buffer[16];
	int rc;
	struct tuner_i2c_props i2c = { .adap = i2c_adap, .addr = i2c_addr };

	if (16 != (rc = tuner_i2c_xfer_recv(&i2c, buffer, 16))) {
		printk(KERN_WARNING "it is not a TEA5761. Received %i chars.\n", rc);
		return EINVAL;
	}

	if (!((buffer[13] != 0x2b) || (buffer[14] != 0x57) || (buffer[15] != 0x061))) {
		printk(KERN_WARNING "Manufacturer ID= 0x%02x, Chip ID = %02x%02x. It is not a TEA5761\n",buffer[13],buffer[14],buffer[15]);
		return EINVAL;
	}
	printk(KERN_WARNING "TEA5761 detected.\n");
	return 0;
}

static int tea5761_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;

	return 0;
}

static int tea5761_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct tea5761_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

static struct dvb_tuner_ops tea5761_tuner_ops = {
	.info = {
		.name           = "tea5761", // Philips TEA5761HN FM Radio
	},
	.set_analog_params = set_radio_freq,
	.release           = tea5761_release,
	.get_frequency     = tea5761_get_frequency,
	.get_status        = tea5761_get_status,
	.get_rf_strength   = tea5761_get_rf_strength,
};

struct dvb_frontend *tea5761_attach(struct dvb_frontend *fe,
				    struct i2c_adapter* i2c_adap,
				    u8 i2c_addr)
{
	struct tea5761_priv *priv = NULL;

	if (tea5761_autodetection(i2c_adap, i2c_addr) == EINVAL)
		return NULL;

	priv = kzalloc(sizeof(struct tea5761_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;
	fe->tuner_priv = priv;

	priv->i2c_props.addr = i2c_addr;
	priv->i2c_props.adap = i2c_adap;

	memcpy(&fe->ops.tuner_ops, &tea5761_tuner_ops,
	       sizeof(struct dvb_tuner_ops));

	tuner_info("type set to %s\n", "Philips TEA5761HN FM Radio");

	return fe;
}


EXPORT_SYMBOL_GPL(tea5761_attach);
EXPORT_SYMBOL_GPL(tea5761_autodetection);

MODULE_DESCRIPTION("Philips TEA5761 FM tuner driver");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@infradead.org>");
MODULE_LICENSE("GPL");
