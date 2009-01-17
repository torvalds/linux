/*
 * i2c tv tuner chip device driver
 * controls all those simple 4-control-bytes style tuners.
 *
 * This "tuner-simple" module was split apart from the original "tuner" module.
 */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/videodev2.h>
#include <media/tuner.h>
#include <media/v4l2-common.h>
#include <media/tuner-types.h>
#include "tuner-i2c.h"
#include "tuner-simple.h"

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "enable verbose debug messages");

#define TUNER_SIMPLE_MAX 64
static unsigned int simple_devcount;

static int offset;
module_param(offset, int, 0664);
MODULE_PARM_DESC(offset, "Allows to specify an offset for tuner");

static unsigned int atv_input[TUNER_SIMPLE_MAX] = \
			{ [0 ... (TUNER_SIMPLE_MAX-1)] = 0 };
static unsigned int dtv_input[TUNER_SIMPLE_MAX] = \
			{ [0 ... (TUNER_SIMPLE_MAX-1)] = 0 };
module_param_array(atv_input, int, NULL, 0644);
module_param_array(dtv_input, int, NULL, 0644);
MODULE_PARM_DESC(atv_input, "specify atv rf input, 0 for autoselect");
MODULE_PARM_DESC(dtv_input, "specify dtv rf input, 0 for autoselect");

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
#define TEMIC_SET_PAL_L         0x0a /* SECAM ? */
#define TEMIC_SET_PAL_L2        0x0b /* change IF ! */
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

static DEFINE_MUTEX(tuner_simple_list_mutex);
static LIST_HEAD(hybrid_tuner_instance_list);

struct tuner_simple_priv {
	unsigned int nr;
	u16 last_div;

	struct tuner_i2c_props i2c_props;
	struct list_head hybrid_tuner_instance_list;

	unsigned int type;
	struct tunertype *tun;

	u32 frequency;
	u32 bandwidth;
};

/* ---------------------------------------------------------------------- */

static int tuner_read_status(struct dvb_frontend *fe)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	unsigned char byte;

	if (1 != tuner_i2c_xfer_recv(&priv->i2c_props, &byte, 1))
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
	case TUNER_TCL_MF02GIP_5N:
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
	int tuner_status;

	if (priv->i2c_props.adap == NULL)
		return -EINVAL;

	tuner_status = tuner_read_status(fe);

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
	int signal;

	if (priv->i2c_props.adap == NULL)
		return -EINVAL;

	signal = tuner_signal(tuner_read_status(fe));

	*strength = signal;

	tuner_dbg("Signal strength: %d\n", signal);

	return 0;
}

/* ---------------------------------------------------------------------- */

static inline char *tuner_param_name(enum param_type type)
{
	char *name;

	switch (type) {
	case TUNER_PARAM_TYPE_RADIO:
		name = "radio";
		break;
	case TUNER_PARAM_TYPE_PAL:
		name = "pal";
		break;
	case TUNER_PARAM_TYPE_SECAM:
		name = "secam";
		break;
	case TUNER_PARAM_TYPE_NTSC:
		name = "ntsc";
		break;
	case TUNER_PARAM_TYPE_DIGITAL:
		name = "digital";
		break;
	default:
		name = "unknown";
		break;
	}
	return name;
}

static struct tuner_params *simple_tuner_params(struct dvb_frontend *fe,
						enum param_type desired_type)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	struct tunertype *tun = priv->tun;
	int i;

	for (i = 0; i < tun->count; i++)
		if (desired_type == tun->params[i].type)
			break;

	/* use default tuner params if desired_type not available */
	if (i == tun->count) {
		tuner_dbg("desired params (%s) undefined for tuner %d\n",
			  tuner_param_name(desired_type), priv->type);
		i = 0;
	}

	tuner_dbg("using tuner params #%d (%s)\n", i,
		  tuner_param_name(tun->params[i].type));

	return &tun->params[i];
}

static int simple_config_lookup(struct dvb_frontend *fe,
				struct tuner_params *t_params,
				unsigned *frequency, u8 *config, u8 *cb)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	int i;

	for (i = 0; i < t_params->count; i++) {
		if (*frequency > t_params->ranges[i].limit)
			continue;
		break;
	}
	if (i == t_params->count) {
		tuner_dbg("frequency out of range (%d > %d)\n",
			  *frequency, t_params->ranges[i - 1].limit);
		*frequency = t_params->ranges[--i].limit;
	}
	*config = t_params->ranges[i].config;
	*cb     = t_params->ranges[i].cb;

	tuner_dbg("freq = %d.%02d (%d), range = %d, "
		  "config = 0x%02x, cb = 0x%02x\n",
		  *frequency / 16, *frequency % 16 * 100 / 16, *frequency,
		  i, *config, *cb);

	return i;
}

/* ---------------------------------------------------------------------- */

static void simple_set_rf_input(struct dvb_frontend *fe,
				u8 *config, u8 *cb, unsigned int rf)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;

	switch (priv->type) {
	case TUNER_PHILIPS_TUV1236D:
		switch (rf) {
		case 1:
			*cb |= 0x08;
			break;
		default:
			*cb &= ~0x08;
			break;
		}
		break;
	case TUNER_PHILIPS_FCV1236D:
		switch (rf) {
		case 1:
			*cb |= 0x01;
			break;
		default:
			*cb &= ~0x01;
			break;
		}
		break;
	default:
		break;
	}
}

static int simple_std_setup(struct dvb_frontend *fe,
			    struct analog_parameters *params,
			    u8 *config, u8 *cb)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	u8 tuneraddr;
	int rc;

	/* tv norm specific stuff for multi-norm tuners */
	switch (priv->type) {
	case TUNER_PHILIPS_SECAM: /* FI1216MF */
		/* 0x01 -> ??? no change ??? */
		/* 0x02 -> PAL BDGHI / SECAM L */
		/* 0x04 -> ??? PAL others / SECAM others ??? */
		*cb &= ~0x03;
		if (params->std & V4L2_STD_SECAM_L)
			/* also valid for V4L2_STD_SECAM */
			*cb |= PHILIPS_MF_SET_STD_L;
		else if (params->std & V4L2_STD_SECAM_LC)
			*cb |= PHILIPS_MF_SET_STD_LC;
		else /* V4L2_STD_B|V4L2_STD_GH */
			*cb |= PHILIPS_MF_SET_STD_BG;
		break;

	case TUNER_TEMIC_4046FM5:
		*cb &= ~0x0f;

		if (params->std & V4L2_STD_PAL_BG) {
			*cb |= TEMIC_SET_PAL_BG;

		} else if (params->std & V4L2_STD_PAL_I) {
			*cb |= TEMIC_SET_PAL_I;

		} else if (params->std & V4L2_STD_PAL_DK) {
			*cb |= TEMIC_SET_PAL_DK;

		} else if (params->std & V4L2_STD_SECAM_L) {
			*cb |= TEMIC_SET_PAL_L;

		}
		break;

	case TUNER_PHILIPS_FQ1216ME:
		*cb &= ~0x0f;

		if (params->std & (V4L2_STD_PAL_BG|V4L2_STD_PAL_DK)) {
			*cb |= PHILIPS_SET_PAL_BGDK;

		} else if (params->std & V4L2_STD_PAL_I) {
			*cb |= PHILIPS_SET_PAL_I;

		} else if (params->std & V4L2_STD_SECAM_L) {
			*cb |= PHILIPS_SET_PAL_L;

		}
		break;

	case TUNER_PHILIPS_FCV1236D:
		/* 0x00 -> ATSC antenna input 1 */
		/* 0x01 -> ATSC antenna input 2 */
		/* 0x02 -> NTSC antenna input 1 */
		/* 0x03 -> NTSC antenna input 2 */
		*cb &= ~0x03;
		if (!(params->std & V4L2_STD_ATSC))
			*cb |= 2;
		break;

	case TUNER_MICROTUNE_4042FI5:
		/* Set the charge pump for fast tuning */
		*config |= TUNER_CHARGE_PUMP;
		break;

	case TUNER_PHILIPS_TUV1236D:
	{
		/* 0x40 -> ATSC antenna input 1 */
		/* 0x48 -> ATSC antenna input 2 */
		/* 0x00 -> NTSC antenna input 1 */
		/* 0x08 -> NTSC antenna input 2 */
		u8 buffer[4] = { 0x14, 0x00, 0x17, 0x00};
		*cb &= ~0x40;
		if (params->std & V4L2_STD_ATSC) {
			*cb |= 0x40;
			buffer[1] = 0x04;
		}
		/* set to the correct mode (analog or digital) */
		tuneraddr = priv->i2c_props.addr;
		priv->i2c_props.addr = 0x0a;
		rc = tuner_i2c_xfer_send(&priv->i2c_props, &buffer[0], 2);
		if (2 != rc)
			tuner_warn("i2c i/o error: rc == %d "
				   "(should be 2)\n", rc);
		rc = tuner_i2c_xfer_send(&priv->i2c_props, &buffer[2], 2);
		if (2 != rc)
			tuner_warn("i2c i/o error: rc == %d "
				   "(should be 2)\n", rc);
		priv->i2c_props.addr = tuneraddr;
		break;
	}
	}
	if (atv_input[priv->nr])
		simple_set_rf_input(fe, config, cb, atv_input[priv->nr]);

	return 0;
}

static int simple_post_tune(struct dvb_frontend *fe, u8 *buffer,
			    u16 div, u8 config, u8 cb)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	int rc;

	switch (priv->type) {
	case TUNER_LG_TDVS_H06XF:
		/* Set the Auxiliary Byte. */
		buffer[0] = buffer[2];
		buffer[0] &= ~0x20;
		buffer[0] |= 0x18;
		buffer[1] = 0x20;
		tuner_dbg("tv 0x%02x 0x%02x\n", buffer[0], buffer[1]);

		rc = tuner_i2c_xfer_send(&priv->i2c_props, buffer, 2);
		if (2 != rc)
			tuner_warn("i2c i/o error: rc == %d "
				   "(should be 2)\n", rc);
		break;
	case TUNER_MICROTUNE_4042FI5:
	{
		/* FIXME - this may also work for other tuners */
		unsigned long timeout = jiffies + msecs_to_jiffies(1);
		u8 status_byte = 0;

		/* Wait until the PLL locks */
		for (;;) {
			if (time_after(jiffies, timeout))
				return 0;
			rc = tuner_i2c_xfer_recv(&priv->i2c_props,
						 &status_byte, 1);
			if (1 != rc) {
				tuner_warn("i2c i/o read error: rc == %d "
					   "(should be 1)\n", rc);
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
			  buffer[0], buffer[1], buffer[2], buffer[3]);

		rc = tuner_i2c_xfer_send(&priv->i2c_props, buffer, 4);
		if (4 != rc)
			tuner_warn("i2c i/o error: rc == %d "
				   "(should be 4)\n", rc);
		break;
	}
	}

	return 0;
}

static int simple_radio_bandswitch(struct dvb_frontend *fe, u8 *buffer)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;

	switch (priv->type) {
	case TUNER_TENA_9533_DI:
	case TUNER_YMEC_TVF_5533MF:
		tuner_dbg("This tuner doesn't have FM. "
			  "Most cards have a TEA5767 for FM\n");
		return 0;
	case TUNER_PHILIPS_FM1216ME_MK3:
	case TUNER_PHILIPS_FM1236_MK3:
	case TUNER_PHILIPS_FMD1216ME_MK3:
	case TUNER_PHILIPS_FMD1216MEX_MK3:
	case TUNER_LG_NTSC_TAPE:
	case TUNER_PHILIPS_FM1256_IH3:
	case TUNER_TCL_MF02GIP_5N:
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

	return 0;
}

/* ---------------------------------------------------------------------- */

static int simple_set_tv_freq(struct dvb_frontend *fe,
			      struct analog_parameters *params)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	u8 config, cb;
	u16 div;
	struct tunertype *tun;
	u8 buffer[4];
	int rc, IFPCoff, i;
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

	t_params = simple_tuner_params(fe, desired_type);

	i = simple_config_lookup(fe, t_params, &params->frequency,
				 &config, &cb);

	div = params->frequency + IFPCoff + offset;

	tuner_dbg("Freq= %d.%02d MHz, V_IF=%d.%02d MHz, "
		  "Offset=%d.%02d MHz, div=%0d\n",
		  params->frequency / 16, params->frequency % 16 * 100 / 16,
		  IFPCoff / 16, IFPCoff % 16 * 100 / 16,
		  offset / 16, offset % 16 * 100 / 16, div);

	/* tv norm specific stuff for multi-norm tuners */
	simple_std_setup(fe, params, &config, &cb);

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
		struct v4l2_priv_tun_config tda9887_cfg;
		int tda_config = 0;
		int is_secam_l = (params->std & (V4L2_STD_SECAM_L |
						 V4L2_STD_SECAM_LC)) &&
			!(params->std & ~(V4L2_STD_SECAM_L |
					  V4L2_STD_SECAM_LC));

		tda9887_cfg.tuner = TUNER_TDA9887;
		tda9887_cfg.priv  = &tda_config;

		if (params->std == V4L2_STD_SECAM_LC) {
			if (t_params->port1_active ^ t_params->port1_invert_for_secam_lc)
				tda_config |= TDA9887_PORT1_ACTIVE;
			if (t_params->port2_active ^ t_params->port2_invert_for_secam_lc)
				tda_config |= TDA9887_PORT2_ACTIVE;
		} else {
			if (t_params->port1_active)
				tda_config |= TDA9887_PORT1_ACTIVE;
			if (t_params->port2_active)
				tda_config |= TDA9887_PORT2_ACTIVE;
		}
		if (t_params->intercarrier_mode)
			tda_config |= TDA9887_INTERCARRIER;
		if (is_secam_l) {
			if (i == 0 && t_params->default_top_secam_low)
				tda_config |= TDA9887_TOP(t_params->default_top_secam_low);
			else if (i == 1 && t_params->default_top_secam_mid)
				tda_config |= TDA9887_TOP(t_params->default_top_secam_mid);
			else if (t_params->default_top_secam_high)
				tda_config |= TDA9887_TOP(t_params->default_top_secam_high);
		} else {
			if (i == 0 && t_params->default_top_low)
				tda_config |= TDA9887_TOP(t_params->default_top_low);
			else if (i == 1 && t_params->default_top_mid)
				tda_config |= TDA9887_TOP(t_params->default_top_mid);
			else if (t_params->default_top_high)
				tda_config |= TDA9887_TOP(t_params->default_top_high);
		}
		if (t_params->default_pll_gating_18)
			tda_config |= TDA9887_GATING_18;
		i2c_clients_command(priv->i2c_props.adap, TUNER_SET_CONFIG,
				    &tda9887_cfg);
	}
	tuner_dbg("tv 0x%02x 0x%02x 0x%02x 0x%02x\n",
		  buffer[0], buffer[1], buffer[2], buffer[3]);

	rc = tuner_i2c_xfer_send(&priv->i2c_props, buffer, 4);
	if (4 != rc)
		tuner_warn("i2c i/o error: rc == %d (should be 4)\n", rc);

	simple_post_tune(fe, &buffer[0], div, config, cb);

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
		tuner_warn("Unsupported radio_if value %d\n",
			   t_params->radio_if);
		return 0;
	}

	/* Bandswitch byte */
	simple_radio_bandswitch(fe, &buffer[0]);

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
	       buffer[0], buffer[1], buffer[2], buffer[3]);
	priv->last_div = div;

	if (t_params->has_tda9887) {
		int config = 0;
		struct v4l2_priv_tun_config tda9887_cfg;

		tda9887_cfg.tuner = TUNER_TDA9887;
		tda9887_cfg.priv = &config;

		if (t_params->port1_active &&
		    !t_params->port1_fm_high_sensitivity)
			config |= TDA9887_PORT1_ACTIVE;
		if (t_params->port2_active &&
		    !t_params->port2_fm_high_sensitivity)
			config |= TDA9887_PORT2_ACTIVE;
		if (t_params->intercarrier_mode)
			config |= TDA9887_INTERCARRIER;
/*		if (t_params->port1_set_for_fm_mono)
			config &= ~TDA9887_PORT1_ACTIVE;*/
		if (t_params->fm_gain_normal)
			config |= TDA9887_GAIN_NORMAL;
		if (t_params->radio_if == 2)
			config |= TDA9887_RIF_41_3;
		i2c_clients_command(priv->i2c_props.adap, TUNER_SET_CONFIG,
				    &tda9887_cfg);
	}
	rc = tuner_i2c_xfer_send(&priv->i2c_props, buffer, 4);
	if (4 != rc)
		tuner_warn("i2c i/o error: rc == %d (should be 4)\n", rc);

	return 0;
}

static int simple_set_params(struct dvb_frontend *fe,
			     struct analog_parameters *params)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	int ret = -EINVAL;

	if (priv->i2c_props.adap == NULL)
		return -EINVAL;

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
	priv->bandwidth = 0;

	return ret;
}

static void simple_set_dvb(struct dvb_frontend *fe, u8 *buf,
			   const struct dvb_frontend_parameters *params)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;

	switch (priv->type) {
	case TUNER_PHILIPS_FMD1216ME_MK3:
	case TUNER_PHILIPS_FMD1216MEX_MK3:
		if (params->u.ofdm.bandwidth == BANDWIDTH_8_MHZ &&
		    params->frequency >= 158870000)
			buf[3] |= 0x08;
		break;
	case TUNER_PHILIPS_TD1316:
		/* determine band */
		buf[3] |= (params->frequency < 161000000) ? 1 :
			  (params->frequency < 444000000) ? 2 : 4;

		/* setup PLL filter */
		if (params->u.ofdm.bandwidth == BANDWIDTH_8_MHZ)
			buf[3] |= 1 << 3;
		break;
	case TUNER_PHILIPS_TUV1236D:
	case TUNER_PHILIPS_FCV1236D:
	{
		unsigned int new_rf;

		if (dtv_input[priv->nr])
			new_rf = dtv_input[priv->nr];
		else
			switch (params->u.vsb.modulation) {
			case QAM_64:
			case QAM_256:
				new_rf = 1;
				break;
			case VSB_8:
			default:
				new_rf = 0;
				break;
			}
		simple_set_rf_input(fe, &buf[2], &buf[3], new_rf);
		break;
	}
	default:
		break;
	}
}

static u32 simple_dvb_configure(struct dvb_frontend *fe, u8 *buf,
				const struct dvb_frontend_parameters *params)
{
	/* This function returns the tuned frequency on success, 0 on error */
	struct tuner_simple_priv *priv = fe->tuner_priv;
	struct tunertype *tun = priv->tun;
	static struct tuner_params *t_params;
	u8 config, cb;
	u32 div;
	int ret;
	unsigned frequency = params->frequency / 62500;

	if (!tun->stepsize) {
		/* tuner-core was loaded before the digital tuner was
		 * configured and somehow picked the wrong tuner type */
		tuner_err("attempt to treat tuner %d (%s) as digital tuner "
			  "without stepsize defined.\n",
			  priv->type, priv->tun->name);
		return 0; /* failure */
	}

	t_params = simple_tuner_params(fe, TUNER_PARAM_TYPE_DIGITAL);
	ret = simple_config_lookup(fe, t_params, &frequency, &config, &cb);
	if (ret < 0)
		return 0; /* failure */

	div = ((frequency + t_params->iffreq) * 62500 + offset +
	       tun->stepsize/2) / tun->stepsize;

	buf[0] = div >> 8;
	buf[1] = div & 0xff;
	buf[2] = config;
	buf[3] = cb;

	simple_set_dvb(fe, buf, params);

	tuner_dbg("%s: div=%d | buf=0x%02x,0x%02x,0x%02x,0x%02x\n",
		  tun->name, div, buf[0], buf[1], buf[2], buf[3]);

	/* calculate the frequency we set it to */
	return (div * tun->stepsize) - t_params->iffreq;
}

static int simple_dvb_calc_regs(struct dvb_frontend *fe,
				struct dvb_frontend_parameters *params,
				u8 *buf, int buf_len)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	u32 frequency;

	if (buf_len < 5)
		return -EINVAL;

	frequency = simple_dvb_configure(fe, buf+1, params);
	if (frequency == 0)
		return -EINVAL;

	buf[0] = priv->i2c_props.addr;

	priv->frequency = frequency;
	priv->bandwidth = (fe->ops.info.type == FE_OFDM) ?
		params->u.ofdm.bandwidth : 0;

	return 5;
}

static int simple_dvb_set_params(struct dvb_frontend *fe,
				 struct dvb_frontend_parameters *params)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	u32 prev_freq, prev_bw;
	int ret;
	u8 buf[5];

	if (priv->i2c_props.adap == NULL)
		return -EINVAL;

	prev_freq = priv->frequency;
	prev_bw   = priv->bandwidth;

	ret = simple_dvb_calc_regs(fe, params, buf, 5);
	if (ret != 5)
		goto fail;

	/* put analog demod in standby when tuning digital */
	if (fe->ops.analog_ops.standby)
		fe->ops.analog_ops.standby(fe);

	if (fe->ops.i2c_gate_ctrl)
		fe->ops.i2c_gate_ctrl(fe, 1);

	/* buf[0] contains the i2c address, but *
	 * we already have it in i2c_props.addr */
	ret = tuner_i2c_xfer_send(&priv->i2c_props, buf+1, 4);
	if (ret != 4)
		goto fail;

	return 0;
fail:
	/* calc_regs sets frequency and bandwidth. if we failed, unset them */
	priv->frequency = prev_freq;
	priv->bandwidth = prev_bw;

	return ret;
}

static int simple_init(struct dvb_frontend *fe)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;

	if (priv->i2c_props.adap == NULL)
		return -EINVAL;

	if (priv->tun->initdata) {
		int ret;

		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);

		ret = tuner_i2c_xfer_send(&priv->i2c_props,
					  priv->tun->initdata + 1,
					  priv->tun->initdata[0]);
		if (ret != priv->tun->initdata[0])
			return ret;
	}

	return 0;
}

static int simple_sleep(struct dvb_frontend *fe)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;

	if (priv->i2c_props.adap == NULL)
		return -EINVAL;

	if (priv->tun->sleepdata) {
		int ret;

		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);

		ret = tuner_i2c_xfer_send(&priv->i2c_props,
					  priv->tun->sleepdata + 1,
					  priv->tun->sleepdata[0]);
		if (ret != priv->tun->sleepdata[0])
			return ret;
	}

	return 0;
}

static int simple_release(struct dvb_frontend *fe)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;

	mutex_lock(&tuner_simple_list_mutex);

	if (priv)
		hybrid_tuner_release_state(priv);

	mutex_unlock(&tuner_simple_list_mutex);

	fe->tuner_priv = NULL;

	return 0;
}

static int simple_get_frequency(struct dvb_frontend *fe, u32 *frequency)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	*frequency = priv->frequency;
	return 0;
}

static int simple_get_bandwidth(struct dvb_frontend *fe, u32 *bandwidth)
{
	struct tuner_simple_priv *priv = fe->tuner_priv;
	*bandwidth = priv->bandwidth;
	return 0;
}

static struct dvb_tuner_ops simple_tuner_ops = {
	.init              = simple_init,
	.sleep             = simple_sleep,
	.set_analog_params = simple_set_params,
	.set_params        = simple_dvb_set_params,
	.calc_regs         = simple_dvb_calc_regs,
	.release           = simple_release,
	.get_frequency     = simple_get_frequency,
	.get_bandwidth     = simple_get_bandwidth,
	.get_status        = simple_get_status,
	.get_rf_strength   = simple_get_rf_strength,
};

struct dvb_frontend *simple_tuner_attach(struct dvb_frontend *fe,
					 struct i2c_adapter *i2c_adap,
					 u8 i2c_addr,
					 unsigned int type)
{
	struct tuner_simple_priv *priv = NULL;
	int instance;

	if (type >= tuner_count) {
		printk(KERN_WARNING "%s: invalid tuner type: %d (max: %d)\n",
		       __func__, type, tuner_count-1);
		return NULL;
	}

	/* If i2c_adap is set, check that the tuner is at the correct address.
	 * Otherwise, if i2c_adap is NULL, the tuner will be programmed directly
	 * by the digital demod via calc_regs.
	 */
	if (i2c_adap != NULL) {
		u8 b[1];
		struct i2c_msg msg = {
			.addr = i2c_addr, .flags = I2C_M_RD,
			.buf = b, .len = 1,
		};

		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 1);

		if (1 != i2c_transfer(i2c_adap, &msg, 1))
			printk(KERN_WARNING "tuner-simple %d-%04x: "
			       "unable to probe %s, proceeding anyway.",
			       i2c_adapter_id(i2c_adap), i2c_addr,
			       tuners[type].name);

		if (fe->ops.i2c_gate_ctrl)
			fe->ops.i2c_gate_ctrl(fe, 0);
	}

	mutex_lock(&tuner_simple_list_mutex);

	instance = hybrid_tuner_request_state(struct tuner_simple_priv, priv,
					      hybrid_tuner_instance_list,
					      i2c_adap, i2c_addr,
					      "tuner-simple");
	switch (instance) {
	case 0:
		mutex_unlock(&tuner_simple_list_mutex);
		return NULL;
	case 1:
		fe->tuner_priv = priv;

		priv->type = type;
		priv->tun  = &tuners[type];
		priv->nr   = simple_devcount++;
		break;
	default:
		fe->tuner_priv = priv;
		break;
	}

	mutex_unlock(&tuner_simple_list_mutex);

	memcpy(&fe->ops.tuner_ops, &simple_tuner_ops,
	       sizeof(struct dvb_tuner_ops));

	if (type != priv->type)
		tuner_warn("couldn't set type to %d. Using %d (%s) instead\n",
			    type, priv->type, priv->tun->name);
	else
		tuner_info("type set to %d (%s)\n",
			   priv->type, priv->tun->name);

	if ((debug) || ((atv_input[priv->nr] > 0) ||
			(dtv_input[priv->nr] > 0))) {
		if (0 == atv_input[priv->nr])
			tuner_info("tuner %d atv rf input will be "
				   "autoselected\n", priv->nr);
		else
			tuner_info("tuner %d atv rf input will be "
				   "set to input %d (insmod option)\n",
				   priv->nr, atv_input[priv->nr]);
		if (0 == dtv_input[priv->nr])
			tuner_info("tuner %d dtv rf input will be "
				   "autoselected\n", priv->nr);
		else
			tuner_info("tuner %d dtv rf input will be "
				   "set to input %d (insmod option)\n",
				   priv->nr, dtv_input[priv->nr]);
	}

	strlcpy(fe->ops.tuner_ops.info.name, priv->tun->name,
		sizeof(fe->ops.tuner_ops.info.name));

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
