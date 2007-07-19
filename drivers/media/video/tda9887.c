#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/types.h>
#include <linux/videodev.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <media/v4l2-common.h>
#include <media/tuner.h>
#include "tuner-driver.h"


/* Chips:
   TDA9885 (PAL, NTSC)
   TDA9886 (PAL, SECAM, NTSC)
   TDA9887 (PAL, SECAM, NTSC, FM Radio)

   Used as part of several tuners
*/

#define tda9887_info(fmt, arg...) do {\
	printk(KERN_INFO "%s %d-%04x: " fmt, t->i2c.name, \
			i2c_adapter_id(t->i2c.adapter), t->i2c.addr , ##arg); } while (0)
#define tda9887_dbg(fmt, arg...) do {\
	if (tuner_debug) \
		printk(KERN_INFO "%s %d-%04x: " fmt, t->i2c.name, \
			i2c_adapter_id(t->i2c.adapter), t->i2c.addr , ##arg); } while (0)

struct tda9887_priv {
	unsigned char 	   data[4];
};

/* ---------------------------------------------------------------------- */

#define UNSET       (-1U)

struct tvnorm {
	v4l2_std_id       std;
	char              *name;
	unsigned char     b;
	unsigned char     c;
	unsigned char     e;
};

/* ---------------------------------------------------------------------- */

//
// TDA defines
//

//// first reg (b)
#define cVideoTrapBypassOFF     0x00    // bit b0
#define cVideoTrapBypassON      0x01    // bit b0

#define cAutoMuteFmInactive     0x00    // bit b1
#define cAutoMuteFmActive       0x02    // bit b1

#define cIntercarrier           0x00    // bit b2
#define cQSS                    0x04    // bit b2

#define cPositiveAmTV           0x00    // bit b3:4
#define cFmRadio                0x08    // bit b3:4
#define cNegativeFmTV           0x10    // bit b3:4


#define cForcedMuteAudioON      0x20    // bit b5
#define cForcedMuteAudioOFF     0x00    // bit b5

#define cOutputPort1Active      0x00    // bit b6
#define cOutputPort1Inactive    0x40    // bit b6

#define cOutputPort2Active      0x00    // bit b7
#define cOutputPort2Inactive    0x80    // bit b7


//// second reg (c)
#define cDeemphasisOFF          0x00    // bit c5
#define cDeemphasisON           0x20    // bit c5

#define cDeemphasis75           0x00    // bit c6
#define cDeemphasis50           0x40    // bit c6

#define cAudioGain0             0x00    // bit c7
#define cAudioGain6             0x80    // bit c7

#define cTopMask                0x1f    // bit c0:4
#define cTopDefault		0x10 	// bit c0:4

//// third reg (e)
#define cAudioIF_4_5             0x00    // bit e0:1
#define cAudioIF_5_5             0x01    // bit e0:1
#define cAudioIF_6_0             0x02    // bit e0:1
#define cAudioIF_6_5             0x03    // bit e0:1


#define cVideoIF_58_75           0x00    // bit e2:4
#define cVideoIF_45_75           0x04    // bit e2:4
#define cVideoIF_38_90           0x08    // bit e2:4
#define cVideoIF_38_00           0x0C    // bit e2:4
#define cVideoIF_33_90           0x10    // bit e2:4
#define cVideoIF_33_40           0x14    // bit e2:4
#define cRadioIF_45_75           0x18    // bit e2:4
#define cRadioIF_38_90           0x1C    // bit e2:4


#define cTunerGainNormal         0x00    // bit e5
#define cTunerGainLow            0x20    // bit e5

#define cGating_18               0x00    // bit e6
#define cGating_36               0x40    // bit e6

#define cAgcOutON                0x80    // bit e7
#define cAgcOutOFF               0x00    // bit e7

/* ---------------------------------------------------------------------- */

static struct tvnorm tvnorms[] = {
	{
		.std   = V4L2_STD_PAL_BG | V4L2_STD_PAL_H | V4L2_STD_PAL_N,
		.name  = "PAL-BGHN",
		.b     = ( cNegativeFmTV  |
			   cQSS           ),
		.c     = ( cDeemphasisON  |
			   cDeemphasis50  |
			   cTopDefault),
		.e     = ( cGating_36     |
			   cAudioIF_5_5   |
			   cVideoIF_38_90 ),
	},{
		.std   = V4L2_STD_PAL_I,
		.name  = "PAL-I",
		.b     = ( cNegativeFmTV  |
			   cQSS           ),
		.c     = ( cDeemphasisON  |
			   cDeemphasis50  |
			   cTopDefault),
		.e     = ( cGating_36     |
			   cAudioIF_6_0   |
			   cVideoIF_38_90 ),
	},{
		.std   = V4L2_STD_PAL_DK,
		.name  = "PAL-DK",
		.b     = ( cNegativeFmTV  |
			   cQSS           ),
		.c     = ( cDeemphasisON  |
			   cDeemphasis50  |
			   cTopDefault),
		.e     = ( cGating_36     |
			   cAudioIF_6_5   |
			   cVideoIF_38_90 ),
	},{
		.std   = V4L2_STD_PAL_M | V4L2_STD_PAL_Nc,
		.name  = "PAL-M/Nc",
		.b     = ( cNegativeFmTV  |
			   cQSS           ),
		.c     = ( cDeemphasisON  |
			   cDeemphasis75  |
			   cTopDefault),
		.e     = ( cGating_36     |
			   cAudioIF_4_5   |
			   cVideoIF_45_75 ),
	},{
		.std   = V4L2_STD_SECAM_B | V4L2_STD_SECAM_G | V4L2_STD_SECAM_H,
		.name  = "SECAM-BGH",
		.b     = ( cPositiveAmTV  |
			   cQSS           ),
		.c     = ( cTopDefault),
		.e     = ( cGating_36	  |
			   cAudioIF_5_5   |
			   cVideoIF_38_90 ),
	},{
		.std   = V4L2_STD_SECAM_L,
		.name  = "SECAM-L",
		.b     = ( cPositiveAmTV  |
			   cQSS           ),
		.c     = ( cTopDefault),
		.e     = ( cGating_36	  |
			   cAudioIF_6_5   |
			   cVideoIF_38_90 ),
	},{
		.std   = V4L2_STD_SECAM_LC,
		.name  = "SECAM-L'",
		.b     = ( cOutputPort2Inactive |
			   cPositiveAmTV  |
			   cQSS           ),
		.c     = ( cTopDefault),
		.e     = ( cGating_36	  |
			   cAudioIF_6_5   |
			   cVideoIF_33_90 ),
	},{
		.std   = V4L2_STD_SECAM_DK,
		.name  = "SECAM-DK",
		.b     = ( cNegativeFmTV  |
			   cQSS           ),
		.c     = ( cDeemphasisON  |
			   cDeemphasis50  |
			   cTopDefault),
		.e     = ( cGating_36     |
			   cAudioIF_6_5   |
			   cVideoIF_38_90 ),
	},{
		.std   = V4L2_STD_NTSC_M | V4L2_STD_NTSC_M_KR,
		.name  = "NTSC-M",
		.b     = ( cNegativeFmTV  |
			   cQSS           ),
		.c     = ( cDeemphasisON  |
			   cDeemphasis75  |
			   cTopDefault),
		.e     = ( cGating_36     |
			   cAudioIF_4_5   |
			   cVideoIF_45_75 ),
	},{
		.std   = V4L2_STD_NTSC_M_JP,
		.name  = "NTSC-M-JP",
		.b     = ( cNegativeFmTV  |
			   cQSS           ),
		.c     = ( cDeemphasisON  |
			   cDeemphasis50  |
			   cTopDefault),
		.e     = ( cGating_36     |
			   cAudioIF_4_5   |
			   cVideoIF_58_75 ),
	}
};

static struct tvnorm radio_stereo = {
	.name = "Radio Stereo",
	.b    = ( cFmRadio       |
		  cQSS           ),
	.c    = ( cDeemphasisOFF |
		  cAudioGain6    |
		  cTopDefault),
	.e    = ( cTunerGainLow  |
		  cAudioIF_5_5   |
		  cRadioIF_38_90 ),
};

static struct tvnorm radio_mono = {
	.name = "Radio Mono",
	.b    = ( cFmRadio       |
		  cQSS           ),
	.c    = ( cDeemphasisON  |
		  cDeemphasis75  |
		  cTopDefault),
	.e    = ( cTunerGainLow  |
		  cAudioIF_5_5   |
		  cRadioIF_38_90 ),
};

/* ---------------------------------------------------------------------- */

static void dump_read_message(struct tuner *t, unsigned char *buf)
{
	static char *afc[16] = {
		"- 12.5 kHz",
		"- 37.5 kHz",
		"- 62.5 kHz",
		"- 87.5 kHz",
		"-112.5 kHz",
		"-137.5 kHz",
		"-162.5 kHz",
		"-187.5 kHz [min]",
		"+187.5 kHz [max]",
		"+162.5 kHz",
		"+137.5 kHz",
		"+112.5 kHz",
		"+ 87.5 kHz",
		"+ 62.5 kHz",
		"+ 37.5 kHz",
		"+ 12.5 kHz",
	};
	tda9887_info("read: 0x%2x\n", buf[0]);
	tda9887_info("  after power on : %s\n", (buf[0] & 0x01) ? "yes" : "no");
	tda9887_info("  afc            : %s\n", afc[(buf[0] >> 1) & 0x0f]);
	tda9887_info("  fmif level     : %s\n", (buf[0] & 0x20) ? "high" : "low");
	tda9887_info("  afc window     : %s\n", (buf[0] & 0x40) ? "in" : "out");
	tda9887_info("  vfi level      : %s\n", (buf[0] & 0x80) ? "high" : "low");
}

static void dump_write_message(struct tuner *t, unsigned char *buf)
{
	static char *sound[4] = {
		"AM/TV",
		"FM/radio",
		"FM/TV",
		"FM/radio"
	};
	static char *adjust[32] = {
		"-16", "-15", "-14", "-13", "-12", "-11", "-10", "-9",
		"-8",  "-7",  "-6",  "-5",  "-4",  "-3",  "-2",  "-1",
		"0",   "+1",  "+2",  "+3",  "+4",  "+5",  "+6",  "+7",
		"+8",  "+9",  "+10", "+11", "+12", "+13", "+14", "+15"
	};
	static char *deemph[4] = {
		"no", "no", "75", "50"
	};
	static char *carrier[4] = {
		"4.5 MHz",
		"5.5 MHz",
		"6.0 MHz",
		"6.5 MHz / AM"
	};
	static char *vif[8] = {
		"58.75 MHz",
		"45.75 MHz",
		"38.9 MHz",
		"38.0 MHz",
		"33.9 MHz",
		"33.4 MHz",
		"45.75 MHz + pin13",
		"38.9 MHz + pin13",
	};
	static char *rif[4] = {
		"44 MHz",
		"52 MHz",
		"52 MHz",
		"44 MHz",
	};

	tda9887_info("write: byte B 0x%02x\n",buf[1]);
	tda9887_info("  B0   video mode      : %s\n",
	       (buf[1] & 0x01) ? "video trap" : "sound trap");
	tda9887_info("  B1   auto mute fm    : %s\n",
	       (buf[1] & 0x02) ? "yes" : "no");
	tda9887_info("  B2   carrier mode    : %s\n",
	       (buf[1] & 0x04) ? "QSS" : "Intercarrier");
	tda9887_info("  B3-4 tv sound/radio  : %s\n",
	       sound[(buf[1] & 0x18) >> 3]);
	tda9887_info("  B5   force mute audio: %s\n",
	       (buf[1] & 0x20) ? "yes" : "no");
	tda9887_info("  B6   output port 1   : %s\n",
	       (buf[1] & 0x40) ? "high (inactive)" : "low (active)");
	tda9887_info("  B7   output port 2   : %s\n",
	       (buf[1] & 0x80) ? "high (inactive)" : "low (active)");

	tda9887_info("write: byte C 0x%02x\n",buf[2]);
	tda9887_info("  C0-4 top adjustment  : %s dB\n", adjust[buf[2] & 0x1f]);
	tda9887_info("  C5-6 de-emphasis     : %s\n", deemph[(buf[2] & 0x60) >> 5]);
	tda9887_info("  C7   audio gain      : %s\n",
	       (buf[2] & 0x80) ? "-6" : "0");

	tda9887_info("write: byte E 0x%02x\n",buf[3]);
	tda9887_info("  E0-1 sound carrier   : %s\n",
	       carrier[(buf[3] & 0x03)]);
	tda9887_info("  E6   l pll gating   : %s\n",
	       (buf[3] & 0x40) ? "36" : "13");

	if (buf[1] & 0x08) {
		/* radio */
		tda9887_info("  E2-4 video if        : %s\n",
		       rif[(buf[3] & 0x0c) >> 2]);
		tda9887_info("  E7   vif agc output  : %s\n",
		       (buf[3] & 0x80)
		       ? ((buf[3] & 0x10) ? "fm-agc radio" : "sif-agc radio")
		       : "fm radio carrier afc");
	} else {
		/* video */
		tda9887_info("  E2-4 video if        : %s\n",
		       vif[(buf[3] & 0x1c) >> 2]);
		tda9887_info("  E5   tuner gain      : %s\n",
		       (buf[3] & 0x80)
		       ? ((buf[3] & 0x20) ? "external" : "normal")
		       : ((buf[3] & 0x20) ? "minimum"  : "normal"));
		tda9887_info("  E7   vif agc output  : %s\n",
		       (buf[3] & 0x80)
		       ? ((buf[3] & 0x20)
			  ? "pin3 port, pin22 vif agc out"
			  : "pin22 port, pin3 vif acg ext in")
		       : "pin3+pin22 port");
	}
	tda9887_info("--\n");
}

/* ---------------------------------------------------------------------- */

static int tda9887_set_tvnorm(struct tuner *t, char *buf)
{
	struct tvnorm *norm = NULL;
	int i;

	if (t->mode == V4L2_TUNER_RADIO) {
		if (t->audmode == V4L2_TUNER_MODE_MONO)
			norm = &radio_mono;
		else
			norm = &radio_stereo;
	} else {
		for (i = 0; i < ARRAY_SIZE(tvnorms); i++) {
			if (tvnorms[i].std & t->std) {
				norm = tvnorms+i;
				break;
			}
		}
	}
	if (NULL == norm) {
		tda9887_dbg("Unsupported tvnorm entry - audio muted\n");
		return -1;
	}

	tda9887_dbg("configure for: %s\n",norm->name);
	buf[1] = norm->b;
	buf[2] = norm->c;
	buf[3] = norm->e;
	return 0;
}

static unsigned int port1  = UNSET;
static unsigned int port2  = UNSET;
static unsigned int qss    = UNSET;
static unsigned int adjust = UNSET;

module_param(port1, int, 0644);
module_param(port2, int, 0644);
module_param(qss, int, 0644);
module_param(adjust, int, 0644);

static int tda9887_set_insmod(struct tuner *t, char *buf)
{
	if (UNSET != port1) {
		if (port1)
			buf[1] |= cOutputPort1Inactive;
		else
			buf[1] &= ~cOutputPort1Inactive;
	}
	if (UNSET != port2) {
		if (port2)
			buf[1] |= cOutputPort2Inactive;
		else
			buf[1] &= ~cOutputPort2Inactive;
	}

	if (UNSET != qss) {
		if (qss)
			buf[1] |= cQSS;
		else
			buf[1] &= ~cQSS;
	}

	if (adjust >= 0x00 && adjust < 0x20) {
		buf[2] &= ~cTopMask;
		buf[2] |= adjust;
	}
	return 0;
}

static int tda9887_set_config(struct tuner *t, char *buf)
{
	if (t->tda9887_config & TDA9887_PORT1_ACTIVE)
		buf[1] &= ~cOutputPort1Inactive;
	if (t->tda9887_config & TDA9887_PORT1_INACTIVE)
		buf[1] |= cOutputPort1Inactive;
	if (t->tda9887_config & TDA9887_PORT2_ACTIVE)
		buf[1] &= ~cOutputPort2Inactive;
	if (t->tda9887_config & TDA9887_PORT2_INACTIVE)
		buf[1] |= cOutputPort2Inactive;

	if (t->tda9887_config & TDA9887_QSS)
		buf[1] |= cQSS;
	if (t->tda9887_config & TDA9887_INTERCARRIER)
		buf[1] &= ~cQSS;

	if (t->tda9887_config & TDA9887_AUTOMUTE)
		buf[1] |= cAutoMuteFmActive;
	if (t->tda9887_config & TDA9887_DEEMPHASIS_MASK) {
		buf[2] &= ~0x60;
		switch (t->tda9887_config & TDA9887_DEEMPHASIS_MASK) {
		case TDA9887_DEEMPHASIS_NONE:
			buf[2] |= cDeemphasisOFF;
			break;
		case TDA9887_DEEMPHASIS_50:
			buf[2] |= cDeemphasisON | cDeemphasis50;
			break;
		case TDA9887_DEEMPHASIS_75:
			buf[2] |= cDeemphasisON | cDeemphasis75;
			break;
		}
	}
	if (t->tda9887_config & TDA9887_TOP_SET) {
		buf[2] &= ~cTopMask;
		buf[2] |= (t->tda9887_config >> 8) & cTopMask;
	}
	if ((t->tda9887_config & TDA9887_INTERCARRIER_NTSC) && (t->std & V4L2_STD_NTSC))
		buf[1] &= ~cQSS;
	if (t->tda9887_config & TDA9887_GATING_18)
		buf[3] &= ~cGating_36;

	if (t->tda9887_config & TDA9887_GAIN_NORMAL) {
		radio_stereo.e &= ~cTunerGainLow;
		radio_mono.e &= ~cTunerGainLow;
	}

	return 0;
}

/* ---------------------------------------------------------------------- */

static int tda9887_status(struct tuner *t)
{
	unsigned char buf[1];
	int rc;

	memset(buf,0,sizeof(buf));
	if (1 != (rc = i2c_master_recv(&t->i2c,buf,1)))
		tda9887_info("i2c i/o error: rc == %d (should be 1)\n",rc);
	dump_read_message(t, buf);
	return 0;
}

static void tda9887_configure(struct i2c_client *client)
{
	struct tuner *t = i2c_get_clientdata(client);
	struct tda9887_priv *priv = t->priv;
	int rc;

	memset(priv->data,0,sizeof(priv->data));
	tda9887_set_tvnorm(t,priv->data);

	/* A note on the port settings:
	   These settings tend to depend on the specifics of the board.
	   By default they are set to inactive (bit value 1) by this driver,
	   overwriting any changes made by the tvnorm. This means that it
	   is the responsibility of the module using the tda9887 to set
	   these values in case of changes in the tvnorm.
	   In many cases port 2 should be made active (0) when selecting
	   SECAM-L, and port 2 should remain inactive (1) for SECAM-L'.

	   For the other standards the tda9887 application note says that
	   the ports should be set to active (0), but, again, that may
	   differ depending on the precise hardware configuration.
	 */
	priv->data[1] |= cOutputPort1Inactive;
	priv->data[1] |= cOutputPort2Inactive;

	tda9887_set_config(t,priv->data);
	tda9887_set_insmod(t,priv->data);

	if (t->mode == T_STANDBY) {
		priv->data[1] |= cForcedMuteAudioON;
	}

	tda9887_dbg("writing: b=0x%02x c=0x%02x e=0x%02x\n",
		priv->data[1],priv->data[2],priv->data[3]);
	if (tuner_debug > 1)
		dump_write_message(t, priv->data);

	if (4 != (rc = i2c_master_send(&t->i2c,priv->data,4)))
		tda9887_info("i2c i/o error: rc == %d (should be 4)\n",rc);

	if (tuner_debug > 2) {
		msleep_interruptible(1000);
		tda9887_status(t);
	}
}

/* ---------------------------------------------------------------------- */

static void tda9887_tuner_status(struct i2c_client *client)
{
	struct tuner *t = i2c_get_clientdata(client);
	struct tda9887_priv *priv = t->priv;
	tda9887_info("Data bytes: b=0x%02x c=0x%02x e=0x%02x\n", priv->data[1], priv->data[2], priv->data[3]);
}

static int tda9887_get_afc(struct i2c_client *client)
{
	struct tuner *t = i2c_get_clientdata(client);
	static int AFC_BITS_2_kHz[] = {
		-12500,  -37500,  -62500,  -97500,
		-112500, -137500, -162500, -187500,
		187500,  162500,  137500,  112500,
		97500 ,  62500,   37500 ,  12500
	};
	int afc=0;
	__u8 reg = 0;

	if (1 == i2c_master_recv(&t->i2c,&reg,1))
		afc = AFC_BITS_2_kHz[(reg>>1)&0x0f];

	return afc;
}

static void tda9887_standby(struct i2c_client *client)
{
	tda9887_configure(client);
}

static void tda9887_set_freq(struct i2c_client *client, unsigned int freq)
{
	tda9887_configure(client);
}

static void tda9887_release(struct i2c_client *c)
{
	struct tuner *t = i2c_get_clientdata(c);

	kfree(t->priv);
	t->priv = NULL;
}

static struct tuner_operations tda9887_tuner_ops = {
	.set_tv_freq    = tda9887_set_freq,
	.set_radio_freq = tda9887_set_freq,
	.standby        = tda9887_standby,
	.tuner_status   = tda9887_tuner_status,
	.get_afc        = tda9887_get_afc,
	.release        = tda9887_release,
};

int tda9887_tuner_init(struct i2c_client *c)
{
	struct tda9887_priv *priv = NULL;
	struct tuner *t = i2c_get_clientdata(c);

	priv = kzalloc(sizeof(struct tda9887_priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;
	t->priv = priv;

	strlcpy(c->name, "tda9887", sizeof(c->name));

	tda9887_info("tda988[5/6/7] found @ 0x%x (%s)\n", t->i2c.addr,
						t->i2c.driver->driver.name);

	memcpy(&t->ops, &tda9887_tuner_ops, sizeof(struct tuner_operations));

	return 0;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
