/*
 * i2c tv tuner chip device driver
 * controls all those simple 4-control-bytes style tuners.
 *
 * This "tuner-simple" module was split apart from the original "tuner" module.
 */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/videodev.h>
#include <media/tuner.h>
#include <media/v4l2-common.h>
#include <media/tuner-types.h>
#include "tuner-i2c.h"
#include "tuner-simple.h"

static int debug = 0;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable verbose debug messages");

#define PREFIX "tuner-simple "

static int offset = 0;
module_param(offset, int, 0664);
MODULE_PARM_DESC(offset,"Allows to specify an offset for tuner");

/* ---------------------------------------------------------------------- */

/* tv standard selection for Temic 4046 FM5
   this value takes the low bits of control byte 2
   from datasheet Rev.01, Feb.00
     standard     BG      I       L       L2      D
     picture IF   38.9    38.9    38.9    33.95   38.9
     sound 1      33.4    32.9    32.4    40.45   32.4
     sound 2      33.16
     NICAM        33.05   32.348  33.05           33.05
 */
#define TEMIC_SET_PAL_I         0x05
#define TEMIC_SET_PAL_DK        0x09
#define TEMIC_SET_PAL_L         0x0a // SECAM ?
#define TEMIC_SET_PAL_L2        0x0b // change IF !
#define TEMIC_SET_PAL_BG        0x0c

/* tv tuner system standard selection for Philips FQ1216ME
   this value takes the low bits of control byte 2
   from datasheet "1999 Nov 16" (supersedes "1999 Mar 23")
     standard 		BG	DK	I	L	L`
     picture carrier	38.90	38.90	38.90	38.90	33.95
     colour		34.47	34.47	34.47	34.47	38.38
     sound 1		33.40	32.40	32.90	32.40	40.45
     sound 2		33.16	-	-	-	-
     NICAM		33.05	33.05	32.35	33.05	39.80
 */
#define PHILIPS_SET_PAL_I	0x01 /* Bit 2 always zero !*/
#define PHILIPS_SET_PAL_BGDK	0x09
#define PHILIPS_SET_PAL_L2	0x0a
#define PHILIPS_SET_PAL_L	0x0b

/* system switching for Philips FI1216MF MK2
   from datasheet "1996 Jul 09",
    standard         BG     L      L'
    picture carrier  38.90  38.90  33.95
    colour	     34.47  34.37  38.38
    sound 1          33.40  32.40  40.45
    sound 2          33.16  -      -
    NICAM            33.05  33.05  39.80
 */
#define PHILIPS_MF_SET_STD_BG	0x01 /* Bit 2 must be zero, Bit 3 is system output */
#define PHILIPS_MF_SET_STD_L	0x03 /* Used on Secam France */
#define PHILIPS_MF_SET_STD_LC	0x02 /* Used on SECAM L' */

/* Control byte */

#define TUNER_RATIO_MASK        0x06 /* Bit cb1:cb2 */
#define TUNER_RATIO_SELECT_50   0x00
#define TUNER_RATIO_SELECT_32   0x02
#define TUNER_RATIO_SELECT_166  0x04
#define TUNER_RATIO_SELECT_62   0x06

#define TUNER_CHARGE_PUMP       0x40  /* Bit cb6 */

/* Status byte */

#define TUNER_POR	  0x80
#define TUNER_FL          0x40
#define TUNER_MODE        0x38
#define TUNER_AFC         0x07
#define TUNER_SIGNAL      0x07
#define TUNER_STEREO      0x10

#define TUNER_PLL_LOCKED   0x40
#define TUNER_STEREO_MK3   0x04

struct tuner_simple_priv {
	u16 last_div;
	struct tuner_i2c_props i2c_props;

	unsigned int type;
	struct tunertype *tun;

	u32 frequency;
};

/* ---------------------------------------------------------------------- */

static int tuner_read_status(struct dvb_frontend *fe)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	unsigned char byte;

	if (1 != tuner_i2c_xfer_recv(&priv->i2c_props,&byte,1))
		return 0;

	return byte;
}

static inline int tuner_signal(const int status)
{
	return (status & TUNER_SIGNAL) << 13;
}

static inline int tuner_stereo(const int type, const int status)
{
	switch (type) {
		case TUNER_PHILIPS_FM1216ME_MK3:
		case TUNER_PHILIPS_FM1236_MK3:
		case TUNER_PHILIPS_FM1256_IH3:
		case TUNER_LG_NTSC_TAPE:
			return ((status & TUNER_SIGNAL) == TUNER_STEREO_MK3);
		default:
			return status & TUNER_STEREO;
	}
}

static inline int tuner_islocked(const int status)
{
	return (status & TUNER_FL);
}

static inline int tuner_afcstatus(const int status)
{
	return (status & TUNER_AFC) - 2;
}


static int simple_get_status(struct dvb_frontend *fe, u32 *status)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	int tuner_status = tuner_read_status(fe);

	*status = 0;

	if (tuner_islocked(tuner_status))
		*status = TUNER_STATUS_LOCKED;
	if (tuner_stereo(priv->type, tuner_status))
		*status |= TUNER_STATUS_STEREO;

	tuner_dbg("AFC Status: %d\n", tuner_afcstatus(tuner_status));

	return 0;
}

static int simple_get_rf_strength(struct dvb_frontend *fe, u16 *strength)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	int signal = tuner_signal(tuner_read_status(fe));

	*strength = signal;

	tuner_dbg("Signal strength: %d\n", signal);

	return 0;
}

/* ---------------------------------------------------------------------- */

static int simple_set_tv_freq(struct dvb_frontend *fe,
			      struct analog_parameters *params)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	u8 config, cb, tuneraddr;
	u16 div;
	struct tunertype *tun;
	u8 buffer[4];
	int rc, IFPCoff, i, j;
	enum param_type desired_type;
	struct tuner_params *t_params;

	tun = priv->tun;

	/* IFPCoff = Video Intermediate Frequency - Vif:
		940  =16*58.75  NTSC/J (Japan)
		732  =16*45.75  M/N STD
		704  =16*44     ATSC (at DVB code)
		632  =16*39.50  I U.K.
		622.4=16*38.90  B/G D/K I, L STD
		592  =16*37.00  D China
		590  =16.36.875 B Australia
		543.2=16*33.95  L' STD
		171.2=16*10.70  FM Radio (at set_radio_freq)
	*/

	if (params->std == V4L2_STD_NTSC_M_JP) {
		IFPCoff      = 940;
		desired_type = TUNER_PARAM_TYPE_NTSC;
	} else if ((params->std & V4L2_STD_MN) &&
		  !(params->std & ~V4L2_STD_MN)) {
		IFPCoff      = 732;
		desired_type = TUNER_PARAM_TYPE_NTSC;
	} else if (params->std == V4L2_STD_SECAM_LC) {
		IFPCoff      = 543;
		desired_type = TUNER_PARAM_TYPE_SECAM;
	} else {
		IFPCoff      = 623;
		desired_type = TUNER_PARAM_TYPE_PAL;
	}

	for (j = 0; j < tun->count-1; j++) {
		if (desired_type != tun->params[j].type)
			continue;
		break;
	}
	/* use default tuner_t_params if desired_type not available */
	if (desired_type != tun->params[j].type) {
		tuner_dbg("IFPCoff = %d: tuner_t_params undefined for tuner %d\n",
			  IFPCoff, priv->type);
		j = 0;
	}
	t_params = &tun->params[j];

	for (i = 0; i < t_params->count; i++) {
		if (params->frequency > t_params->ranges[i].limit)
			continue;
		break;
	}
	if (i == t_params->count) {
		tuner_dbg("TV frequency out of range (%d > %d)",
				params->frequency, t_params->ranges[i - 1].limit);
		params->frequency = t_params->ranges[--i].limit;
	}
	config = t_params->ranges[i].config;
	cb     = t_params->ranges[i].cb;
	/*  i == 0 -> VHF_LO
	 *  i == 1 -> VHF_HI
	 *  i == 2 -> UHF     */
	tuner_dbg("tv: param %d, range %d\n",j,i);

	div=params->frequency + IFPCoff + offset;

	tuner_dbg("Freq= %d.%02d MHz, V_IF=%d.%02d MHz, Offset=%d.%02d MHz, div=%0d\n",
					params->frequency / 16, params->frequency % 16 * 100 / 16,
					IFPCoff / 16, IFPCoff % 16 * 100 / 16,
					offset / 16, offset % 16 * 100 / 16,
					div);

	/* tv norm specific stuff for multi-norm tuners */
	switch (priv->type) {
	case TUNER_PHILIPS_SECAM: // FI1216MF
		/* 0x01 -> ??? no change ??? */
		/* 0x02 -> PAL BDGHI / SECAM L */
		/* 0x04 -> ??? PAL others / SECAM others ??? */
		cb &= ~0x03;
		if (params->std & V4L2_STD_SECAM_L) //also valid for V4L2_STD_SECAM
			cb |= PHILIPS_MF_SET_STD_L;
		else if (params->std & V4L2_STD_SECAM_LC)
			cb |= PHILIPS_MF_SET_STD_LC;
		else /* V4L2_STD_B|V4L2_STD_GH */
			cb |= PHILIPS_MF_SET_STD_BG;
		break;

	case TUNER_TEMIC_4046FM5:
		cb &= ~0x0f;

		if (params->std & V4L2_STD_PAL_BG) {
			cb |= TEMIC_SET_PAL_BG;

		} else if (params->std & V4L2_STD_PAL_I) {
			cb |= TEMIC_SET_PAL_I;

		} else if (params->std & V4L2_STD_PAL_DK) {
			cb |= TEMIC_SET_PAL_DK;

		} else if (params->std & V4L2_STD_SECAM_L) {
			cb |= TEMIC_SET_PAL_L;

		}
		break;

	case TUNER_PHILIPS_FQ1216ME:
		cb &= ~0x0f;

		if (params->std & (V4L2_STD_PAL_BG|V4L2_STD_PAL_DK)) {
			cb |= PHILIPS_SET_PAL_BGDK;

		} else if (params->std & V4L2_STD_PAL_I) {
			cb |= PHILIPS_SET_PAL_I;

		} else if (params->std & V4L2_STD_SECAM_L) {
			cb |= PHILIPS_SET_PAL_L;

		}
		break;

	case TUNER_PHILIPS_ATSC:
		/* 0x00 -> ATSC antenna input 1 */
		/* 0x01 -> ATSC antenna input 2 */
		/* 0x02 -> NTSC antenna input 1 */
		/* 0x03 -> NTSC antenna input 2 */
		cb &= ~0x03;
		if (!(params->std & V4L2_STD_ATSC))
			cb |= 2;
		/* FIXME: input */
		break;

	case TUNER_MICROTUNE_4042FI5:
		/* Set the charge pump for fast tuning */
		config |= TUNER_CHARGE_PUMP;
		break;

	case TUNER_PHILIPS_TUV1236D:
		/* 0x40 -> ATSC antenna input 1 */
		/* 0x48 -> ATSC antenna input 2 */
		/* 0x00 -> NTSC antenna input 1 */
		/* 0x08 -> NTSC antenna input 2 */
		buffer[0] = 0x14;
		buffer[1] = 0x00;
		buffer[2] = 0x17;
		buffer[3] = 0x00;
		cb &= ~0x40;
		if (params->std & V4L2_STD_ATSC) {
			cb |= 0x40;
			buffer[1] = 0x04;
		}
		/* set to the correct mode (analog or digital) */
		tuneraddr = priv->i2c_props.addr;
		priv->i2c_props.addr = 0x0a;
		if (2 != (rc = tuner_i2c_xfer_send(&priv->i2c_props,&buffer[0],2)))
			tuner_warn("i2c i/o error: rc == %d (should be 2)\n",rc);
		if (2 != (rc = tuner_i2c_xfer_send(&priv->i2c_props,&buffer[2],2)))
			tuner_warn("i2c i/o error: rc == %d (should be 2)\n",rc);
		priv->i2c_props.addr = tuneraddr;
		/* FIXME: input */
		break;
	}

	if (t_params->cb_first_if_lower_freq && div < priv->last_div) {
		buffer[0] = config;
		buffer[1] = cb;
		buffer[2] = (div>>8) & 0x7f;
		buffer[3] = div      & 0xff;
	} else {
		buffer[0] = (div>>8) & 0x7f;
		buffer[1] = div      & 0xff;
		buffer[2] = config;
		buffer[3] = cb;
	}
	priv->last_div = div;
	if (t_params->has_tda9887) {
		int config = 0;
		int is_secam_l = (params->std & (V4L2_STD_SECAM_L | V4L2_STD_SECAM_LC)) &&
			!(params->std & ~(V4L2_STD_SECAM_L | V4L2_STD_SECAM_LC));

		if (params->std == V4L2_STD_SECAM_LC) {
			if (t_params->port1_active ^ t_params->port1_invert_for_secam_lc)
				config |= TDA9887_PORT1_ACTIVE;
			if (t_params->port2_active ^ t_params->port2_invert_for_secam_lc)
				config |= TDA9887_PORT2_ACTIVE;
		}
		else {
			if (t_params->port1_active)
				config |= TDA9887_PORT1_ACTIVE;
			if (t_params->port2_active)
				config |= TDA9887_PORT2_ACTIVE;
		}
		if (t_params->intercarrier_mode)
			config |= TDA9887_INTERCARRIER;
		if (is_secam_l) {
			if (i == 0 && t_params->default_top_secam_low)
				config |= TDA9887_TOP(t_params->default_top_secam_low);
			else if (i == 1 && t_params->default_top_secam_mid)
				config |= TDA9887_TOP(t_params->default_top_secam_mid);
			else if (t_params->default_top_secam_high)
				config |= TDA9887_TOP(t_params->default_top_secam_high);
		}
		else {
			if (i == 0 && t_params->default_top_low)
				config |= TDA9887_TOP(t_params->default_top_low);
			else if (i == 1 && t_params->default_top_mid)
				config |= TDA9887_TOP(t_params->default_top_mid);
			else if (t_params->default_top_high)
				config |= TDA9887_TOP(t_params->default_top_high);
		}
		if (t_params->default_pll_gating_18)
			config |= TDA9887_GATING_18;
		i2c_clients_command(priv->i2c_props.adap, TDA9887_SET_CONFIG, &config);
	}
	tuner_dbg("tv 0x%02x 0x%02x 0x%02x 0x%02x\n",
		  buffer[0],buffer[1],buffer[2],buffer[3]);

	if (4 != (rc = tuner_i2c_xfer_send(&priv->i2c_props,buffer,4)))
		tuner_warn("i2c i/o error: rc == %d (should be 4)\n",rc);

	switch (priv->type) {
	case TUNER_LG_TDVS_H06XF:
		/* Set the Auxiliary Byte. */
		buffer[0] = buffer[2];
		buffer[0] &= ~0x20;
		buffer[0] |= 0x18;
		buffer[1] = 0x20;
		tuner_dbg("tv 0x%02x 0x%02x\n",buffer[0],buffer[1]);

		if (2 != (rc = tuner_i2c_xfer_send(&priv->i2c_props,buffer,2)))
			tuner_warn("i2c i/o error: rc == %d (should be 2)\n",rc);
		break;
	case TUNER_MICROTUNE_4042FI5:
	{
		// FIXME - this may also work for other tuners
		unsigned long timeout = jiffies + msecs_to_jiffies(1);
		u8 status_byte = 0;

		/* Wait until the PLL locks */
		for (;;) {
			if (time_after(jiffies,timeout))
				return 0;
			if (1 != (rc = tuner_i2c_xfer_recv(&priv->i2c_props,&status_byte,1))) {
				tuner_warn("i2c i/o read error: rc == %d (should be 1)\n",rc);
				break;
			}
			if (status_byte & TUNER_PLL_LOCKED)
				break;
			udelay(10);
		}

		/* Set the charge pump for optimized phase noise figure */
		config &= ~TUNER_CHARGE_PUMP;
		buffer[0] = (div>>8) & 0x7f;
		buffer[1] = div      & 0xff;
		buffer[2] = config;
		buffer[3] = cb;
		tuner_dbg("tv 0x%02x 0x%02x 0x%02x 0x%02x\n",
			  buffer[0],buffer[1],buffer[2],buffer[3]);

		if (4 != (rc = tuner_i2c_xfer_send(&priv->i2c_props,buffer,4)))
			tuner_warn("i2c i/o error: rc == %d (should be 4)\n",rc);
		break;
	}
	}
	return 0;
}

static int simple_set_radio_freq(struct dvb_frontend *fe,
				 struct analog_parameters *params)
{
	struct tunertype *tun;
	struct tuner_simple_priv *priv = fe->tuner_priv;
	u8 buffer[4];
	u16 div;
	int rc, j;
	struct tuner_params *t_params;
	unsigned int freq = params->frequency;

	tun = priv->tun;

	for (j = tun->count-1; j > 0; j--)
		if (tun->params[j].type == TUNER_PARAM_TYPE_RADIO)
			break;
	/* default t_params (j=0) will be used if desired type wasn't found */
	t_params = &tun->params[j];

	/* Select Radio 1st IF used */
	switch (t_params->radio_if) {
	case 0: /* 10.7 MHz */
		freq += (unsigned int)(10.7*16000);
		break;
	case 1: /* 33.3 MHz */
		freq += (unsigned int)(33.3*16000);
		break;
	case 2: /* 41.3 MHz */
		freq += (unsigned int)(41.3*16000);
		break;
	default:
		tuner_warn("Unsupported radio_if value %d\n", t_params->radio_if);
		return 0;
	}

	/* Bandswitch byte */
	switch (priv->type) {
	case TUNER_TENA_9533_DI:
	case TUNER_YMEC_TVF_5533MF:
		tuner_dbg("This tuner doesn't have FM. Most cards have a TEA5767 for FM\n");
		return 0;
	case TUNER_PHILIPS_FM1216ME_MK3:
	case TUNER_PHILIPS_FM1236_MK3:
	case TUNER_PHILIPS_FMD1216ME_MK3:
	case TUNER_LG_NTSC_TAPE:
	case TUNER_PHILIPS_FM1256_IH3:
		buffer[3] = 0x19;
		break;
	case TUNER_TNF_5335MF:
		buffer[3] = 0x11;
		break;
	case TUNER_LG_PAL_FM:
		buffer[3] = 0xa5;
		break;
	case TUNER_THOMSON_DTT761X:
		buffer[3] = 0x39;
		break;
	case TUNER_MICROTUNE_4049FM5:
	default:
		buffer[3] = 0xa4;
		break;
	}

	buffer[2] = (t_params->ranges[0].config & ~TUNER_RATIO_MASK) |
		    TUNER_RATIO_SELECT_50; /* 50 kHz step */

	/* Convert from 1/16 kHz V4L steps to 1/20 MHz (=50 kHz) PLL steps
	   freq * (1 Mhz / 16000 V4L steps) * (20 PLL steps / 1 MHz) =
	   freq * (1/800) */
	div = (freq + 400) / 800;

	if (t_params->cb_first_if_lower_freq && div < priv->last_div) {
		buffer[0] = buffer[2];
		buffer[1] = buffer[3];
		buffer[2] = (div>>8) & 0x7f;
		buffer[3] = div      & 0xff;
	} else {
		buffer[0] = (div>>8) & 0x7f;
		buffer[1] = div      & 0xff;
	}

	tuner_dbg("radio 0x%02x 0x%02x 0x%02x 0x%02x\n",
	       buffer[0],buffer[1],buffer[2],buffer[3]);
	priv->last_div = div;

	if (t_params->has_tda9887) {
		int config = 0;
		if (t_params->port1_active && !t_params->port1_fm_high_sensitivity)
			config |= TDA9887_PORT1_ACTIVE;
		if (t_params->port2_active && !t_params->port2_fm_high_sensitivity)
			config |= TDA9887_PORT2_ACTIVE;
		if (t_params->intercarrier_mode)
			config |= TDA9887_INTERCARRIER;
/*		if (t_params->port1_set_for_fm_mono)
			config &= ~TDA9887_PORT1_ACTIVE;*/
		if (t_params->fm_gain_normal)
			config |= TDA9887_GAIN_NORMAL;
		if (t_params->radio_if == 2)
			config |= TDA9887_RIF_41_3;
		i2c_clients_command(priv->i2c_props.adap, TDA9887_SET_CONFIG, &config);
	}
	if (4 != (rc = tuner_i2c_xfer_send(&priv->i2c_props,buffer,4)))
		tuner_warn("i2c i/o error: rc == %d (should be 4)\n",rc);

	return 0;
}

static int simple_set_params(struct dvb_frontend *fe,
			     struct analog_parameters *params)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	int ret = -EINVAL;

	switch (params->mode) {
	case V4L2_TUNER_RADIO:
		ret = simple_set_radio_freq(fe, params);
		priv->frequency = params->frequency * 125 / 2;
		break;
	case V4L2_TUNER_ANALOG_TV:
	case V4L2_TUNER_DIGITAL_TV:
		ret = simple_set_tv_freq(fe, params);
		priv->frequency = params->frequency * 62500;
		break;
	}

	return ret;
}


static int simple_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;

	return 0;
}

static int simple_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

static struct dvb_tuner_ops simple_tuner_ops = {
	.set_analog_params = simple_set_params,
	.release           = simple_release,
	.get_frequency     = simple_get_frequency,
	.get_status        = simple_get_status,
	.get_rf_strength   = simple_get_rf_strength,
};

struct dvb_frontend *simple_tuner_attach(struct dvb_frontend *fe,
					 struct i2c_adapter *i2c_adap,
					 u8 i2c_addr,
					 struct simple_tuner_config *cfg)
{
	struct tuner_simple_priv *priv = NULL;

	priv = kzalloc(sizeof(struct tuner_simple_priv), GFP_KERNEL);
	if (priv == NULL)
		return NULL;
	fe->tuner_priv = priv;

	priv->i2c_props.addr = i2c_addr;
	priv->i2c_props.adap = i2c_adap;
	priv->type = cfg->type;
	priv->tun  = cfg->tun;

	memcpy(&fe->ops.tuner_ops, &simple_tuner_ops, sizeof(struct dvb_tuner_ops));

	tuner_info("type set to %d (%s)\n", cfg->type, cfg->tun->name);

	strlcpy(fe->ops.tuner_ops.info.name, cfg->tun->name, sizeof(fe->ops.tuner_ops.info.name));

	return fe;
}


EXPORT_SYMBOL_GPL(simple_tuner_attach);

MODULE_DESCRIPTION("Simple 4-control-bytes style tuner driver");
MODULE_AUTHOR("Ralph Metzler, Gerd Knorr, Gunther Mayer");
MODULE_LICENSE("GPL");

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
