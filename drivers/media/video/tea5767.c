/*
 * For Philips TEA5767 FM Chip used on some TV Cards like Prolink Pixelview
 * I2C address is allways 0xC0.
 *
 *
 * Copyright (c) 2005 Mauro Carvalho Chehab (mchehab@brturbo.com.br)
 * This code is placed under the terms of the GNU General Public License
 *
 * tea5767 autodetection thanks to Torsten Seeboth and Atsushi Nakagawa
 * from their contributions on DScaler.
 */

#include <linux/i2c.h>
#include <linux/videodev.h>
#include <linux/delay.h>
#include <media/tuner.h>

#define PREFIX "TEA5767 "

/*****************************************************************************/

/******************************
 * Write mode register values *
 ******************************/

/* First register */
#define TEA5767_MUTE		0x80	/* Mutes output */
#define TEA5767_SEARCH		0x40	/* Activates station search */
/* Bits 0-5 for divider MSB */

/* Second register */
/* Bits 0-7 for divider LSB */

/* Third register */

/* Station search from botton to up */
#define TEA5767_SEARCH_UP	0x80

/* Searches with ADC output = 10 */
#define TEA5767_SRCH_HIGH_LVL	0x60

/* Searches with ADC output = 10 */
#define TEA5767_SRCH_MID_LVL	0x40

/* Searches with ADC output = 5 */
#define TEA5767_SRCH_LOW_LVL	0x20

/* if on, div=4*(Frf+Fif)/Fref otherwise, div=4*(Frf-Fif)/Freq) */
#define TEA5767_HIGH_LO_INJECT	0x10

/* Disable stereo */
#define TEA5767_MONO		0x08

/* Disable right channel and turns to mono */
#define TEA5767_MUTE_RIGHT	0x04

/* Disable left channel and turns to mono */
#define TEA5767_MUTE_LEFT	0x02

#define TEA5767_PORT1_HIGH	0x01

/* Forth register */
#define TEA5767_PORT2_HIGH	0x80
/* Chips stops working. Only I2C bus remains on */
#define TEA5767_STDBY		0x40

/* Japan freq (76-108 MHz. If disabled, 87.5-108 MHz */
#define TEA5767_JAPAN_BAND	0x20

/* Unselected means 32.768 KHz freq as reference. Otherwise Xtal at 13 MHz */
#define TEA5767_XTAL_32768	0x10

/* Cuts weak signals */
#define TEA5767_SOFT_MUTE	0x08

/* Activates high cut control */
#define TEA5767_HIGH_CUT_CTRL	0x04

/* Activates stereo noise control */
#define TEA5767_ST_NOISE_CTL	0x02

/* If activate PORT 1 indicates SEARCH or else it is used as PORT1 */
#define TEA5767_SRCH_IND	0x01

/* Fiveth register */

/* By activating, it will use Xtal at 13 MHz as reference for divider */
#define TEA5767_PLLREF_ENABLE	0x80

/* By activating, deemphasis=50, or else, deemphasis of 50us */
#define TEA5767_DEEMPH_75	0X40

/*****************************
 * Read mode register values *
 *****************************/

/* First register */
#define TEA5767_READY_FLAG_MASK	0x80
#define TEA5767_BAND_LIMIT_MASK	0X40
/* Bits 0-5 for divider MSB after search or preset */

/* Second register */
/* Bits 0-7 for divider LSB after search or preset */

/* Third register */
#define TEA5767_STEREO_MASK	0x80
#define TEA5767_IF_CNTR_MASK	0x7f

/* Four register */
#define TEA5767_ADC_LEVEL_MASK	0xf0

/* should be 0 */
#define TEA5767_CHIP_ID_MASK	0x0f

/* Fiveth register */
/* Reserved for future extensions */
#define TEA5767_RESERVED_MASK	0xff

enum tea5767_xtal_freq {
        TEA5767_LOW_LO_32768    = 0,
        TEA5767_HIGH_LO_32768   = 1,
        TEA5767_LOW_LO_13MHz    = 2,
        TEA5767_HIGH_LO_13MHz   = 3,
};


/*****************************************************************************/

static void set_tv_freq(struct i2c_client *c, unsigned int freq)
{
	struct tuner *t = i2c_get_clientdata(c);

	tuner_warn("This tuner doesn't support TV freq.\n");
}

static void tea5767_status_dump(unsigned char *buffer)
{
	unsigned int div, frq;

	if (TEA5767_READY_FLAG_MASK & buffer[0])
		printk(PREFIX "Ready Flag ON\n");
	else
		printk(PREFIX "Ready Flag OFF\n");

	if (TEA5767_BAND_LIMIT_MASK & buffer[0])
		printk(PREFIX "Tuner at band limit\n");
	else
		printk(PREFIX "Tuner not at band limit\n");

	div = ((buffer[0] & 0x3f) << 8) | buffer[1];

	switch (TEA5767_HIGH_LO_32768) {
	case TEA5767_HIGH_LO_13MHz:
		frq = (div * 50000 - 700000 - 225000) / 4;	/* Freq in KHz */
		break;
	case TEA5767_LOW_LO_13MHz:
		frq = (div * 50000 + 700000 + 225000) / 4;	/* Freq in KHz */
		break;
	case TEA5767_LOW_LO_32768:
		frq = (div * 32768 + 700000 + 225000) / 4;	/* Freq in KHz */
		break;
	case TEA5767_HIGH_LO_32768:
	default:
		frq = (div * 32768 - 700000 - 225000) / 4;	/* Freq in KHz */
		break;
	}
	buffer[0] = (div >> 8) & 0x3f;
	buffer[1] = div & 0xff;

	printk(PREFIX "Frequency %d.%03d KHz (divider = 0x%04x)\n",
	       frq / 1000, frq % 1000, div);

	if (TEA5767_STEREO_MASK & buffer[2])
		printk(PREFIX "Stereo\n");
	else
		printk(PREFIX "Mono\n");

	printk(PREFIX "IF Counter = %d\n", buffer[2] & TEA5767_IF_CNTR_MASK);

	printk(PREFIX "ADC Level = %d\n",
	       (buffer[3] & TEA5767_ADC_LEVEL_MASK) >> 4);

	printk(PREFIX "Chip ID = %d\n", (buffer[3] & TEA5767_CHIP_ID_MASK));

	printk(PREFIX "Reserved = 0x%02x\n",
	       (buffer[4] & TEA5767_RESERVED_MASK));
}

/* Freq should be specifyed at 62.5 Hz */
static void set_radio_freq(struct i2c_client *c, unsigned int frq)
{
	struct tuner *t = i2c_get_clientdata(c);
	unsigned char buffer[5];
	unsigned div;
	int rc;

	tuner_dbg (PREFIX "radio freq = %d.%03d MHz\n", frq/16000,(frq/16)%1000);

	/* Rounds freq to next decimal value - for 62.5 KHz step */
	/* frq = 20*(frq/16)+radio_frq[frq%16]; */

	buffer[2] = TEA5767_PORT1_HIGH;
	buffer[3] = TEA5767_PORT2_HIGH | TEA5767_HIGH_CUT_CTRL |
		    TEA5767_ST_NOISE_CTL | TEA5767_JAPAN_BAND;
	buffer[4] = 0;

	if (t->audmode == V4L2_TUNER_MODE_MONO) {
		tuner_dbg("TEA5767 set to mono\n");
		buffer[2] |= TEA5767_MONO;
	} else {
		tuner_dbg("TEA5767 set to stereo\n");
	}

	/* Should be replaced */
	switch (TEA5767_HIGH_LO_32768) {
	case TEA5767_HIGH_LO_13MHz:
		tuner_dbg ("TEA5767 radio HIGH LO inject xtal @ 13 MHz\n");
		buffer[2] |= TEA5767_HIGH_LO_INJECT;
		buffer[4] |= TEA5767_PLLREF_ENABLE;
		div = (frq * 4000 / 16 + 700000 + 225000 + 25000) / 50000;
		break;
	case TEA5767_LOW_LO_13MHz:
		tuner_dbg ("TEA5767 radio LOW LO inject xtal @ 13 MHz\n");

		buffer[4] |= TEA5767_PLLREF_ENABLE;
		div = (frq * 4000 / 16 - 700000 - 225000 + 25000) / 50000;
		break;
	case TEA5767_LOW_LO_32768:
		tuner_dbg ("TEA5767 radio LOW LO inject xtal @ 32,768 MHz\n");
		buffer[3] |= TEA5767_XTAL_32768;
		/* const 700=4000*175 Khz - to adjust freq to right value */
		div = ((frq * 4000 / 16 - 700000 - 225000) + 16384) >> 15;
		break;
	case TEA5767_HIGH_LO_32768:
	default:
		tuner_dbg ("TEA5767 radio HIGH LO inject xtal @ 32,768 MHz\n");

		buffer[2] |= TEA5767_HIGH_LO_INJECT;
		buffer[3] |= TEA5767_XTAL_32768;
		div = ((frq * (4000 / 16) + 700000 + 225000) + 16384) >> 15;
		break;
	}
	buffer[0] = (div >> 8) & 0x3f;
	buffer[1] = div & 0xff;

	if (5 != (rc = i2c_master_send(c, buffer, 5)))
		tuner_warn("i2c i/o error: rc == %d (should be 5)\n", rc);

	if (tuner_debug) {
		if (5 != (rc = i2c_master_recv(c, buffer, 5)))
			tuner_warn("i2c i/o error: rc == %d (should be 5)\n", rc);
		else
			tea5767_status_dump(buffer);
	}
}

static int tea5767_signal(struct i2c_client *c)
{
	unsigned char buffer[5];
	int rc;
	struct tuner *t = i2c_get_clientdata(c);

	memset(buffer, 0, sizeof(buffer));
	if (5 != (rc = i2c_master_recv(c, buffer, 5)))
		tuner_warn("i2c i/o error: rc == %d (should be 5)\n", rc);

	return ((buffer[3] & TEA5767_ADC_LEVEL_MASK) << (13 - 4));
}

static int tea5767_stereo(struct i2c_client *c)
{
	unsigned char buffer[5];
	int rc;
	struct tuner *t = i2c_get_clientdata(c);

	memset(buffer, 0, sizeof(buffer));
	if (5 != (rc = i2c_master_recv(c, buffer, 5)))
		tuner_warn("i2c i/o error: rc == %d (should be 5)\n", rc);

	rc = buffer[2] & TEA5767_STEREO_MASK;

	tuner_dbg("TEA5767 radio ST GET = %02x\n", rc);

	return ((buffer[2] & TEA5767_STEREO_MASK) ? V4L2_TUNER_SUB_STEREO : 0);
}

static void tea5767_standby(struct i2c_client *c)
{
	unsigned char buffer[5];
	struct tuner *t = i2c_get_clientdata(c);
	unsigned div, rc;

	div = (87500 * 4 + 700 + 225 + 25) / 50; /* Set frequency to 87.5 MHz */
	buffer[0] = (div >> 8) & 0x3f;
	buffer[1] = div & 0xff;
	buffer[2] = TEA5767_PORT1_HIGH;
	buffer[3] = TEA5767_PORT2_HIGH | TEA5767_HIGH_CUT_CTRL |
		    TEA5767_ST_NOISE_CTL | TEA5767_JAPAN_BAND | TEA5767_STDBY;
	buffer[4] = 0;

	if (5 != (rc = i2c_master_send(c, buffer, 5)))
		tuner_warn("i2c i/o error: rc == %d (should be 5)\n", rc);
}

int tea5767_autodetection(struct i2c_client *c)
{
	unsigned char buffer[7] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	int rc;
	struct tuner *t = i2c_get_clientdata(c);

	if ((rc = i2c_master_recv(c, buffer, 7))< 5) {
		tuner_warn("It is not a TEA5767. Received %i bytes.\n", rc);
		return EINVAL;
	}

	/* If all bytes are the same then it's a TV tuner and not a tea5767 */
	if (buffer[0] == buffer[1] && buffer[0] == buffer[2] &&
	    buffer[0] == buffer[3] && buffer[0] == buffer[4]) {
		tuner_warn("All bytes are equal. It is not a TEA5767\n");
		return EINVAL;
	}

	/*  Status bytes:
	 *  Byte 4: bit 3:1 : CI (Chip Identification) == 0
	 *          bit 0   : internally set to 0
	 *  Byte 5: bit 7:0 : == 0
	 */
	if (((buffer[3] & 0x0f) != 0x00) || (buffer[4] != 0x00)) {
		tuner_warn("Chip ID is not zero. It is not a TEA5767\n");
		return EINVAL;
	}

	/* It seems that tea5767 returns 0xff after the 5th byte */
	if ((buffer[5] != 0xff) || (buffer[6] != 0xff)) {
		tuner_warn("Returned more than 5 bytes. It is not a TEA5767\n");
		return EINVAL;
	}

	tuner_warn("TEA5767 detected.\n");
	return 0;
}

int tea5767_tuner_init(struct i2c_client *c)
{
	struct tuner *t = i2c_get_clientdata(c);

	tuner_info("type set to %d (%s)\n", t->type, "Philips TEA5767HN FM Radio");
	strlcpy(c->name, "tea5767", sizeof(c->name));

	t->tv_freq = set_tv_freq;
	t->radio_freq = set_radio_freq;
	t->has_signal = tea5767_signal;
	t->is_stereo = tea5767_stereo;
	t->standby = tea5767_standby;

	return (0);
}
