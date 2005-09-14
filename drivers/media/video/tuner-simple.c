/*
 *
 * i2c tv tuner chip device driver
 * controls all those simple 4-control-bytes style tuners.
 */
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/videodev.h>
#include <media/tuner.h>

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

struct tunertype
{
	char *name;
	unsigned char Vendor;
	unsigned char Type;

	unsigned short thresh1;  /*  band switch VHF_LO <=> VHF_HI  */
	unsigned short thresh2;  /*  band switch VHF_HI <=> UHF     */
	unsigned char VHF_L;
	unsigned char VHF_H;
	unsigned char UHF;
	unsigned char config;
	unsigned short IFPCoff; /* 622.4=16*38.90 MHz PAL,
				   732  =16*45.75 NTSCi,
				   940  =16*58.75 NTSC-Japan
				   704  =16*44    ATSC */
};

/*
 *	The floats in the tuner struct are computed at compile time
 *	by gcc and cast back to integers. Thus we don't violate the
 *	"no float in kernel" rule.
 */
static struct tunertype tuners[] = {
	/* 0-9 */
        { "Temic PAL (4002 FH5)", TEMIC, PAL,
	  16*140.25,16*463.25,0x02,0x04,0x01,0x8e,623},
	{ "Philips PAL_I (FI1246 and compatibles)", Philips, PAL_I,
	  16*140.25,16*463.25,0xa0,0x90,0x30,0x8e,623},
	{ "Philips NTSC (FI1236,FM1236 and compatibles)", Philips, NTSC,
	  16*157.25,16*451.25,0xA0,0x90,0x30,0x8e,732},
	{ "Philips (SECAM+PAL_BG) (FI1216MF, FM1216MF, FR1216MF)", Philips, SECAM,
	  16*168.25,16*447.25,0xA7,0x97,0x37,0x8e,623},
	{ "NoTuner", NoTuner, NOTUNER,
	  0,0,0x00,0x00,0x00,0x00,0x00},
	{ "Philips PAL_BG (FI1216 and compatibles)", Philips, PAL,
	  16*168.25,16*447.25,0xA0,0x90,0x30,0x8e,623},
	{ "Temic NTSC (4032 FY5)", TEMIC, NTSC,
	  16*157.25,16*463.25,0x02,0x04,0x01,0x8e,732},
	{ "Temic PAL_I (4062 FY5)", TEMIC, PAL_I,
	  16*170.00,16*450.00,0x02,0x04,0x01,0x8e,623},
 	{ "Temic NTSC (4036 FY5)", TEMIC, NTSC,
	  16*157.25,16*463.25,0xa0,0x90,0x30,0x8e,732},
        { "Alps HSBH1", TEMIC, NTSC,
	  16*137.25,16*385.25,0x01,0x02,0x08,0x8e,732},

	/* 10-19 */
        { "Alps TSBE1", TEMIC, PAL,
	  16*137.25,16*385.25,0x01,0x02,0x08,0x8e,732},
        { "Alps TSBB5", Alps, PAL_I, /* tested (UK UHF) with Modulartech MM205 */
	  16*133.25,16*351.25,0x01,0x02,0x08,0x8e,632},
        { "Alps TSBE5", Alps, PAL, /* untested - data sheet guess. Only IF differs. */
	  16*133.25,16*351.25,0x01,0x02,0x08,0x8e,622},
        { "Alps TSBC5", Alps, PAL, /* untested - data sheet guess. Only IF differs. */
	  16*133.25,16*351.25,0x01,0x02,0x08,0x8e,608},
	{ "Temic PAL_BG (4006FH5)", TEMIC, PAL,
	  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
  	{ "Alps TSCH6", Alps, NTSC,
  	  16*137.25,16*385.25,0x14,0x12,0x11,0x8e,732},
  	{ "Temic PAL_DK (4016 FY5)", TEMIC, PAL,
  	  16*168.25,16*456.25,0xa0,0x90,0x30,0x8e,623},
  	{ "Philips NTSC_M (MK2)", Philips, NTSC,
  	  16*160.00,16*454.00,0xa0,0x90,0x30,0x8e,732},
        { "Temic PAL_I (4066 FY5)", TEMIC, PAL_I,
          16*169.00, 16*454.00, 0xa0,0x90,0x30,0x8e,623},
        { "Temic PAL* auto (4006 FN5)", TEMIC, PAL,
          16*169.00, 16*454.00, 0xa0,0x90,0x30,0x8e,623},

	/* 20-29 */
        { "Temic PAL_BG (4009 FR5) or PAL_I (4069 FR5)", TEMIC, PAL,
          16*141.00, 16*464.00, 0xa0,0x90,0x30,0x8e,623},
        { "Temic NTSC (4039 FR5)", TEMIC, NTSC,
          16*158.00, 16*453.00, 0xa0,0x90,0x30,0x8e,732},
        { "Temic PAL/SECAM multi (4046 FM5)", TEMIC, PAL,
          16*169.00, 16*454.00, 0xa0,0x90,0x30,0x8e,623},
        { "Philips PAL_DK (FI1256 and compatibles)", Philips, PAL,
	  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
	{ "Philips PAL/SECAM multi (FQ1216ME)", Philips, PAL,
	  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
	{ "LG PAL_I+FM (TAPC-I001D)", LGINNOTEK, PAL_I,
	  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
	{ "LG PAL_I (TAPC-I701D)", LGINNOTEK, PAL_I,
	  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
	{ "LG NTSC+FM (TPI8NSR01F)", LGINNOTEK, NTSC,
	  16*210.00,16*497.00,0xa0,0x90,0x30,0x8e,732},
	{ "LG PAL_BG+FM (TPI8PSB01D)", LGINNOTEK, PAL,
	  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},
	{ "LG PAL_BG (TPI8PSB11D)", LGINNOTEK, PAL,
	  16*170.00,16*450.00,0xa0,0x90,0x30,0x8e,623},

	/* 30-39 */
	{ "Temic PAL* auto + FM (4009 FN5)", TEMIC, PAL,
	  16*141.00, 16*464.00, 0xa0,0x90,0x30,0x8e,623},
	{ "SHARP NTSC_JP (2U5JF5540)", SHARP, NTSC, /* 940=16*58.75 NTSC@Japan */
	  16*137.25,16*317.25,0x01,0x02,0x08,0x8e,940 },
	{ "Samsung PAL TCPM9091PD27", Samsung, PAL, /* from sourceforge v3tv */
          16*169,16*464,0xA0,0x90,0x30,0x8e,623},
	{ "MT20xx universal", Microtune, PAL|NTSC,
	  /* see mt20xx.c for details */ },
	{ "Temic PAL_BG (4106 FH5)", TEMIC, PAL,
          16*141.00, 16*464.00, 0xa0,0x90,0x30,0x8e,623},
	{ "Temic PAL_DK/SECAM_L (4012 FY5)", TEMIC, PAL,
          16*140.25, 16*463.25, 0x02,0x04,0x01,0x8e,623},
	{ "Temic NTSC (4136 FY5)", TEMIC, NTSC,
          16*158.00, 16*453.00, 0xa0,0x90,0x30,0x8e,732},
        { "LG PAL (newer TAPC series)", LGINNOTEK, PAL,
          16*170.00, 16*450.00, 0x01,0x02,0x08,0x8e,623},
	{ "Philips PAL/SECAM multi (FM1216ME MK3)", Philips, PAL,
	  16*160.00,16*442.00,0x01,0x02,0x04,0x8e,623 },
	{ "LG NTSC (newer TAPC series)", LGINNOTEK, NTSC,
          16*170.00, 16*450.00, 0x01,0x02,0x08,0x8e,732},

	/* 40-49 */
	{ "HITACHI V7-J180AT", HITACHI, NTSC,
	  16*170.00, 16*450.00, 0x01,0x02,0x08,0x8e,940 },
	{ "Philips PAL_MK (FI1216 MK)", Philips, PAL,
	  16*140.25,16*463.25,0x01,0xc2,0xcf,0x8e,623},
	{ "Philips 1236D ATSC/NTSC daul in", Philips, ATSC,
	  16*157.25,16*454.00,0xa0,0x90,0x30,0x8e,732},
        { "Philips NTSC MK3 (FM1236MK3 or FM1236/F)", Philips, NTSC,
          16*160.00,16*442.00,0x01,0x02,0x04,0x8e,732},
        { "Philips 4 in 1 (ATI TV Wonder Pro/Conexant)", Philips, NTSC,
          16*160.00,16*442.00,0x01,0x02,0x04,0x8e,732},
	{ "Microtune 4049 FM5", Microtune, PAL,
	  16*141.00,16*464.00,0xa0,0x90,0x30,0x8e,623},
	{ "Panasonic VP27s/ENGE4324D", Panasonic, NTSC,
	  16*160.00,16*454.00,0x01,0x02,0x08,0xce,940},
        { "LG NTSC (TAPE series)", LGINNOTEK, NTSC,
          16*160.00,16*442.00,0x01,0x02,0x04,0x8e,732 },
        { "Tenna TNF 8831 BGFF)", Philips, PAL,
          16*161.25,16*463.25,0xa0,0x90,0x30,0x8e,623},
	{ "Microtune 4042 FI5 ATSC/NTSC dual in", Microtune, NTSC,
	  16*162.00,16*457.00,0xa2,0x94,0x31,0x8e,732},

	/* 50-59 */
        { "TCL 2002N", TCL, NTSC,
          16*172.00,16*448.00,0x01,0x02,0x08,0x8e,732},
	{ "Philips PAL/SECAM_D (FM 1256 I-H3)", Philips, PAL,
	  16*160.00,16*442.00,0x01,0x02,0x04,0x8e,623 },
	{ "Thomson DDT 7610 (ATSC/NTSC)", THOMSON, ATSC,
	  16*157.25,16*454.00,0x39,0x3a,0x3c,0x8e,732},
	{ "Philips FQ1286", Philips, NTSC,
	  16*160.00,16*454.00,0x41,0x42,0x04,0x8e,940}, /* UHF band untested */
	{ "tda8290+75", Philips, PAL|NTSC,
	  /* see tda8290.c for details */ },
	{ "LG PAL (TAPE series)", LGINNOTEK, PAL,
          16*170.00, 16*450.00, 0x01,0x02,0x08,0xce,623},
	{ "Philips PAL/SECAM multi (FQ1216AME MK4)", Philips, PAL,
	  16*160.00,16*442.00,0x01,0x02,0x04,0xce,623 },
	{ "Philips FQ1236A MK4", Philips, NTSC,
	  16*160.00,16*442.00,0x01,0x02,0x04,0x8e,732 },
	{ "Ymec TVision TVF-8531MF/8831MF/8731MF", Philips, NTSC,
	  16*160.00,16*454.00,0xa0,0x90,0x30,0x8e,732},
	{ "Ymec TVision TVF-5533MF", Philips, NTSC,
	  16*160.00,16*454.00,0x01,0x02,0x04,0x8e,732},

	/* 60-66 */
	{ "Thomson DDT 7611 (ATSC/NTSC)", THOMSON, ATSC,
	  16*157.25,16*454.00,0x39,0x3a,0x3c,0x8e,732},
	{ "Tena TNF9533-D/IF/TNF9533-B/DF", Philips, PAL,
          16*160.25,16*464.25,0x01,0x02,0x04,0x8e,623},
	{ "Philips TEA5767HN FM Radio", Philips, RADIO,
          /* see tea5767.c for details */},
	{ "Philips FMD1216ME MK3 Hybrid Tuner", Philips, PAL,
	  16*160.00,16*442.00,0x51,0x52,0x54,0x86,623 },
	{ "LG TDVS-H062F/TUA6034", LGINNOTEK, ATSC,
	  16*160.00,16*455.00,0x01,0x02,0x04,0x8e,732},
	{ "Ymec TVF66T5-B/DFF", Philips, PAL,
          16*160.25,16*464.25,0x01,0x02,0x08,0x8e,623},
 	{ "LG NTSC (TALN mini series)", LGINNOTEK, NTSC,
	  16*137.25,16*373.25,0x01,0x02,0x08,0x8e,732 },
};

unsigned const int tuner_count = ARRAY_SIZE(tuners);

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
	u8 config;
	u16 div;
	struct tunertype *tun;
        unsigned char buffer[4];
	int rc;

	tun = &tuners[t->type];
	if (freq < tun->thresh1) {
		config = tun->VHF_L;
		tuner_dbg("tv: VHF lowrange\n");
	} else if (freq < tun->thresh2) {
		config = tun->VHF_H;
		tuner_dbg("tv: VHF high range\n");
	} else {
		config = tun->UHF;
		tuner_dbg("tv: UHF range\n");
	}


	/* tv norm specific stuff for multi-norm tuners */
	switch (t->type) {
	case TUNER_PHILIPS_SECAM: // FI1216MF
		/* 0x01 -> ??? no change ??? */
		/* 0x02 -> PAL BDGHI / SECAM L */
		/* 0x04 -> ??? PAL others / SECAM others ??? */
		config &= ~0x02;
		if (t->std & V4L2_STD_SECAM)
			config |= 0x02;
		break;

	case TUNER_TEMIC_4046FM5:
		config &= ~0x0f;

		if (t->std & V4L2_STD_PAL_BG) {
			config |= TEMIC_SET_PAL_BG;

		} else if (t->std & V4L2_STD_PAL_I) {
			config |= TEMIC_SET_PAL_I;

		} else if (t->std & V4L2_STD_PAL_DK) {
			config |= TEMIC_SET_PAL_DK;

		} else if (t->std & V4L2_STD_SECAM_L) {
			config |= TEMIC_SET_PAL_L;

		}
		break;

	case TUNER_PHILIPS_FQ1216ME:
		config &= ~0x0f;

		if (t->std & (V4L2_STD_PAL_BG|V4L2_STD_PAL_DK)) {
			config |= PHILIPS_SET_PAL_BGDK;

		} else if (t->std & V4L2_STD_PAL_I) {
			config |= PHILIPS_SET_PAL_I;

		} else if (t->std & V4L2_STD_SECAM_L) {
			config |= PHILIPS_SET_PAL_L;

		}
		break;

	case TUNER_PHILIPS_ATSC:
		/* 0x00 -> ATSC antenna input 1 */
		/* 0x01 -> ATSC antenna input 2 */
		/* 0x02 -> NTSC antenna input 1 */
		/* 0x03 -> NTSC antenna input 2 */
		config &= ~0x03;
		if (!(t->std & V4L2_STD_ATSC))
			config |= 2;
		/* FIXME: input */
		break;

	case TUNER_MICROTUNE_4042FI5:
		/* Set the charge pump for fast tuning */
		tun->config |= TUNER_CHARGE_PUMP;
		break;
	}

	/*
	 * Philips FI1216MK2 remark from specification :
	 * for channel selection involving band switching, and to ensure
	 * smooth tuning to the desired channel without causing
	 * unnecessary charge pump action, it is recommended to consider
	 * the difference between wanted channel frequency and the
	 * current channel frequency.  Unnecessary charge pump action
	 * will result in very low tuning voltage which may drive the
	 * oscillator to extreme conditions.
	 *
	 * Progfou: specification says to send config data before
	 * frequency in case (wanted frequency < current frequency).
	 */

	div=freq + tun->IFPCoff;
	if (t->type == TUNER_PHILIPS_SECAM && freq < t->freq) {
		buffer[0] = tun->config;
		buffer[1] = config;
		buffer[2] = (div>>8) & 0x7f;
		buffer[3] = div      & 0xff;
	} else {
		buffer[0] = (div>>8) & 0x7f;
		buffer[1] = div      & 0xff;
		buffer[2] = tun->config;
		buffer[3] = config;
	}
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
		tun->config &= ~TUNER_CHARGE_PUMP;
		buffer[0] = (div>>8) & 0x7f;
		buffer[1] = div      & 0xff;
		buffer[2] = tun->config;
		buffer[3] = config;
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
        unsigned char buffer[4];
	unsigned div;
	int rc;

	tun = &tuners[t->type];
	div = (20 * freq / 16000) + (int)(20*10.7); /* IF 10.7 MHz */
	buffer[2] = (tun->config & ~TUNER_RATIO_MASK) | TUNER_RATIO_SELECT_50; /* 50 kHz step */

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

	tuner_dbg("radio 0x%02x 0x%02x 0x%02x 0x%02x\n",
	       buffer[0],buffer[1],buffer[2],buffer[3]);

        if (4 != (rc = i2c_master_send(c,buffer,4)))
		tuner_warn("i2c i/o error: rc == %d (should be 4)\n",rc);
}

int default_tuner_init(struct i2c_client *c)
{
	struct tuner *t = i2c_get_clientdata(c);

	tuner_info("type set to %d (%s)\n",
		   t->type, tuners[t->type].name);
	strlcpy(c->name, tuners[t->type].name, sizeof(c->name));

	t->tv_freq    = default_set_tv_freq;
	t->radio_freq = default_set_radio_freq;
	t->has_signal = tuner_signal;
	t->is_stereo  = tuner_stereo;
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
