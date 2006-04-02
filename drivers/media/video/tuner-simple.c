/*
 *
 * i2c tv tuner chip device driver
 * controls all those simple 4-control-bytes style tuners.
 */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/videodev.h>
#include <media/tuner.h>

static int offset = 0;
module_param(offset, int, 0666);
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
#define PHILIPS_MF_SET_BG	0x01 /* Bit 2 must be zero, Bit 3 is system output */
#define PHILIPS_MF_SET_PAL_L	0x03 // France
#define PHILIPS_MF_SET_PAL_L2	0x02 // L'

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

/* ---------------------------------------------------------------------- */

static int tuner_getstatus(struct i2c_client *c)
{
	unsigned char byte;

	if (1 != i2c_master_recv(c,&byte,1))
		return 0;

	return byte;
}

static int tuner_signal(struct i2c_client *c)
{
	return (tuner_getstatus(c) & TUNER_SIGNAL) << 13;
}

static int tuner_stereo(struct i2c_client *c)
{
	int stereo, status;
	struct tuner *t = i2c_get_clientdata(c);

	status = tuner_getstatus (c);

	switch (t->type) {
		case TUNER_PHILIPS_FM1216ME_MK3:
    		case TUNER_PHILIPS_FM1236_MK3:
		case TUNER_PHILIPS_FM1256_IH3:
			stereo = ((status & TUNER_SIGNAL) == TUNER_STEREO_MK3);
			break;
		default:
			stereo = status & TUNER_STEREO;
	}

	return stereo;
}


/* ---------------------------------------------------------------------- */

static void default_set_tv_freq(struct i2c_client *c, unsigned int freq)
{
	struct tuner *t = i2c_get_clientdata(c);
	u8 config, cb, tuneraddr;
	u16 div;
	struct tunertype *tun;
	u8 buffer[4];
	int rc, IFPCoff, i, j;
	enum param_type desired_type;

	tun = &tuners[t->type];

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

	if (t->std == V4L2_STD_NTSC_M_JP) {
		IFPCoff      = 940;
		desired_type = TUNER_PARAM_TYPE_NTSC;
	} else if ((t->std & V4L2_STD_MN) &&
		  !(t->std & ~V4L2_STD_MN)) {
		IFPCoff      = 732;
		desired_type = TUNER_PARAM_TYPE_NTSC;
	} else if (t->std == V4L2_STD_SECAM_LC) {
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
	/* use default tuner_params if desired_type not available */
	if (desired_type != tun->params[j].type) {
		tuner_dbg("IFPCoff = %d: tuner_params undefined for tuner %d\n",
			  IFPCoff,t->type);
		j = 0;
	}

	for (i = 0; i < tun->params[j].count; i++) {
		if (freq > tun->params[j].ranges[i].limit)
			continue;
		break;
	}
	if (i == tun->params[j].count) {
		tuner_dbg("TV frequency out of range (%d > %d)",
				freq, tun->params[j].ranges[i - 1].limit);
		freq = tun->params[j].ranges[--i].limit;
	}
	config = tun->params[j].ranges[i].config;
	cb     = tun->params[j].ranges[i].cb;
	/*  i == 0 -> VHF_LO
	 *  i == 1 -> VHF_HI
	 *  i == 2 -> UHF     */
	tuner_dbg("tv: param %d, range %d\n",j,i);

	div=freq + IFPCoff + offset;

	tuner_dbg("Freq= %d.%02d MHz, V_IF=%d.%02d MHz, Offset=%d.%02d MHz, div=%0d\n",
					freq / 16, freq % 16 * 100 / 16,
					IFPCoff / 16, IFPCoff % 16 * 100 / 16,
					offset / 16, offset % 16 * 100 / 16,
					div);

	/* tv norm specific stuff for multi-norm tuners */
	switch (t->type) {
	case TUNER_PHILIPS_SECAM: // FI1216MF
		/* 0x01 -> ??? no change ??? */
		/* 0x02 -> PAL BDGHI / SECAM L */
		/* 0x04 -> ??? PAL others / SECAM others ??? */
		cb &= ~0x02;
		if (t->std & V4L2_STD_SECAM)
			cb |= 0x02;
		break;

	case TUNER_TEMIC_4046FM5:
		cb &= ~0x0f;

		if (t->std & V4L2_STD_PAL_BG) {
			cb |= TEMIC_SET_PAL_BG;

		} else if (t->std & V4L2_STD_PAL_I) {
			cb |= TEMIC_SET_PAL_I;

		} else if (t->std & V4L2_STD_PAL_DK) {
			cb |= TEMIC_SET_PAL_DK;

		} else if (t->std & V4L2_STD_SECAM_L) {
			cb |= TEMIC_SET_PAL_L;

		}
		break;

	case TUNER_PHILIPS_FQ1216ME:
		cb &= ~0x0f;

		if (t->std & (V4L2_STD_PAL_BG|V4L2_STD_PAL_DK)) {
			cb |= PHILIPS_SET_PAL_BGDK;

		} else if (t->std & V4L2_STD_PAL_I) {
			cb |= PHILIPS_SET_PAL_I;

		} else if (t->std & V4L2_STD_SECAM_L) {
			cb |= PHILIPS_SET_PAL_L;

		}
		break;

	case TUNER_PHILIPS_ATSC:
		/* 0x00 -> ATSC antenna input 1 */
		/* 0x01 -> ATSC antenna input 2 */
		/* 0x02 -> NTSC antenna input 1 */
		/* 0x03 -> NTSC antenna input 2 */
		cb &= ~0x03;
		if (!(t->std & V4L2_STD_ATSC))
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
		if (t->std & V4L2_STD_ATSC) {
			cb |= 0x40;
			buffer[1] = 0x04;
		}
		/* set to the correct mode (analog or digital) */
		tuneraddr = c->addr;
		c->addr = 0x0a;
		if (2 != (rc = i2c_master_send(c,&buffer[0],2)))
			tuner_warn("i2c i/o error: rc == %d (should be 2)\n",rc);
		if (2 != (rc = i2c_master_send(c,&buffer[2],2)))
			tuner_warn("i2c i/o error: rc == %d (should be 2)\n",rc);
		c->addr = tuneraddr;
		/* FIXME: input */
		break;
	}

	if (tuners[t->type].params->cb_first_if_lower_freq && div < t->last_div) {
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
	t->last_div = div;
	tuner_dbg("tv 0x%02x 0x%02x 0x%02x 0x%02x\n",
		  buffer[0],buffer[1],buffer[2],buffer[3]);

	if (4 != (rc = i2c_master_send(c,buffer,4)))
		tuner_warn("i2c i/o error: rc == %d (should be 4)\n",rc);

	if (t->type == TUNER_MICROTUNE_4042FI5) {
		// FIXME - this may also work for other tuners
		unsigned long timeout = jiffies + msecs_to_jiffies(1);
		u8 status_byte = 0;

		/* Wait until the PLL locks */
		for (;;) {
			if (time_after(jiffies,timeout))
				return;
			if (1 != (rc = i2c_master_recv(c,&status_byte,1))) {
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

		if (4 != (rc = i2c_master_send(c,buffer,4)))
			tuner_warn("i2c i/o error: rc == %d (should be 4)\n",rc);
	}
}

static void default_set_radio_freq(struct i2c_client *c, unsigned int freq)
{
	struct tunertype *tun;
	struct tuner *t = i2c_get_clientdata(c);
	u8 buffer[4];
	u16 div;
	int rc, j;
	enum param_type desired_type = TUNER_PARAM_TYPE_RADIO;

	tun = &tuners[t->type];

	for (j = 0; j < tun->count-1; j++) {
		if (desired_type != tun->params[j].type)
			continue;
		break;
	}
	/* use default tuner_params if desired_type not available */
	if (desired_type != tun->params[j].type)
		j = 0;

	div = (20 * freq / 16000) + (int)(20*10.7); /* IF 10.7 MHz */
	buffer[2] = (tun->params[j].ranges[0].config & ~TUNER_RATIO_MASK) | TUNER_RATIO_SELECT_50; /* 50 kHz step */

	switch (t->type) {
	case TUNER_TENA_9533_DI:
	case TUNER_YMEC_TVF_5533MF:
		tuner_dbg ("This tuner doesn't have FM. Most cards has a TEA5767 for FM\n");
		return;
	case TUNER_PHILIPS_FM1216ME_MK3:
	case TUNER_PHILIPS_FM1236_MK3:
	case TUNER_PHILIPS_FMD1216ME_MK3:
		buffer[3] = 0x19;
		break;
	case TUNER_TNF_5335MF:
		buffer[3] = 0x11;
		break;
	case TUNER_PHILIPS_FM1256_IH3:
		div = (20 * freq) / 16000 + (int)(33.3 * 20);  /* IF 33.3 MHz */
		buffer[3] = 0x19;
		break;
	case TUNER_LG_PAL_FM:
		buffer[3] = 0xa5;
		break;
	case TUNER_MICROTUNE_4049FM5:
		div = (20 * freq) / 16000 + (int)(33.3 * 20); /* IF 33.3 MHz */
		buffer[3] = 0xa4;
		break;
	default:
		buffer[3] = 0xa4;
		break;
	}
	buffer[0] = (div>>8) & 0x7f;
	buffer[1] = div      & 0xff;
	if (tuners[t->type].params->cb_first_if_lower_freq && div < t->last_div) {
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
	t->last_div = div;

	if (4 != (rc = i2c_master_send(c,buffer,4)))
		tuner_warn("i2c i/o error: rc == %d (should be 4)\n",rc);
}

int default_tuner_init(struct i2c_client *c)
{
	struct tuner *t = i2c_get_clientdata(c);

	tuner_info("type set to %d (%s)\n",
		   t->type, tuners[t->type].name);
	strlcpy(c->name, tuners[t->type].name, sizeof(c->name));

	t->set_tv_freq = default_set_tv_freq;
	t->set_radio_freq = default_set_radio_freq;
	t->has_signal = tuner_signal;
	t->is_stereo = tuner_stereo;
	t->standby = NULL;

	return 0;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
