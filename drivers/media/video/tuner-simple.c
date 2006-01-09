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

struct tunertype
{
	char *name;

	unsigned short thresh1;  /*  band switch VHF_LO <=> VHF_HI  */
	unsigned short thresh2;  /*  band switch VHF_HI <=> UHF     */
	unsigned char VHF_L;
	unsigned char VHF_H;
	unsigned char UHF;
	unsigned char config;
};

/*
 *	The floats in the tuner struct are computed at compile time
 *	by gcc and cast back to integers. Thus we don't violate the
 *	"no float in kernel" rule.
 */
static struct tunertype tuners[] = {
	/* 0-9 */
	[TUNER_TEMIC_PAL] = { /* TEMIC PAL */
		.name   = "Temic PAL (4002 FH5)",
		.thresh1= 16 * 140.25 /*MHz*/,
		.thresh2= 16 * 463.25 /*MHz*/,
		.VHF_L  = 0x02,
		.VHF_H  = 0x04,
		.UHF    = 0x01,
		.config = 0x8e,
	},
	[TUNER_PHILIPS_PAL_I] = { /* Philips PAL_I */
		.name   = "Philips PAL_I (FI1246 and compatibles)",
		.thresh1= 16 * 140.25 /*MHz*/,
		.thresh2= 16 * 463.25 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_PHILIPS_NTSC] = { /* Philips NTSC */
		.name   = "Philips NTSC (FI1236,FM1236 and compatibles)",
		.thresh1= 16 * 157.25 /*MHz*/,
		.thresh2= 16 * 451.25 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_PHILIPS_SECAM] = { /* Philips SECAM */
		.name   = "Philips (SECAM+PAL_BG) (FI1216MF, FM1216MF, FR1216MF)",
		.thresh1= 16 * 168.25 /*MHz*/,
		.thresh2= 16 * 447.25 /*MHz*/,
		.VHF_L  = 0xa7,
		.VHF_H  = 0x97,
		.UHF    = 0x37,
		.config = 0x8e,
	},
	[TUNER_ABSENT] = { /* Tuner Absent */
		.name   = "NoTuner",
		.thresh1= 0 /*MHz*/,
		.thresh2= 0 /*MHz*/,
		.VHF_L  = 0x00,
		.VHF_H  = 0x00,
		.UHF    = 0x00,
		.config = 0x00,
	},
	[TUNER_PHILIPS_PAL] = { /* Philips PAL */
		.name   = "Philips PAL_BG (FI1216 and compatibles)",
		.thresh1= 16 * 168.25 /*MHz*/,
		.thresh2= 16 * 447.25 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_TEMIC_NTSC] = { /* TEMIC NTSC */
		.name   = "Temic NTSC (4032 FY5)",
		.thresh1= 16 * 157.25 /*MHz*/,
		.thresh2= 16 * 463.25 /*MHz*/,
		.VHF_L  = 0x02,
		.VHF_H  = 0x04,
		.UHF    = 0x01,
		.config = 0x8e,
	},
	[TUNER_TEMIC_PAL_I] = { /* TEMIC PAL_I */
		.name   = "Temic PAL_I (4062 FY5)",
		.thresh1= 16 * 170.00 /*MHz*/,
		.thresh2= 16 * 450.00 /*MHz*/,
		.VHF_L  = 0x02,
		.VHF_H  = 0x04,
		.UHF    = 0x01,
		.config = 0x8e,
	},
	[TUNER_TEMIC_4036FY5_NTSC] = { /* TEMIC NTSC */
		.name   = "Temic NTSC (4036 FY5)",
		.thresh1= 16 * 157.25 /*MHz*/,
		.thresh2= 16 * 463.25 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_ALPS_TSBH1_NTSC] = { /* TEMIC NTSC */
		.name   = "Alps HSBH1",
		.thresh1= 16 * 137.25 /*MHz*/,
		.thresh2= 16 * 385.25 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x08,
		.config = 0x8e,
	},

	/* 10-19 */
	[TUNER_ALPS_TSBE1_PAL] = { /* TEMIC PAL */
		.name   = "Alps TSBE1",
		.thresh1= 16 * 137.25 /*MHz*/,
		.thresh2= 16 * 385.25 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x08,
		.config = 0x8e,
	},
	[TUNER_ALPS_TSBB5_PAL_I] = { /* Alps PAL_I */
		.name   = "Alps TSBB5",
		.thresh1= 16 * 133.25 /*MHz*/,
		.thresh2= 16 * 351.25 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x08,
		.config = 0x8e,
	},
	[TUNER_ALPS_TSBE5_PAL] = { /* Alps PAL */
		.name   = "Alps TSBE5",
		.thresh1= 16 * 133.25 /*MHz*/,
		.thresh2= 16 * 351.25 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x08,
		.config = 0x8e,
	},
	[TUNER_ALPS_TSBC5_PAL] = { /* Alps PAL */
		.name   = "Alps TSBC5",
		.thresh1= 16 * 133.25 /*MHz*/,
		.thresh2= 16 * 351.25 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x08,
		.config = 0x8e,
	},
	[TUNER_TEMIC_4006FH5_PAL] = { /* TEMIC PAL */
		.name   = "Temic PAL_BG (4006FH5)",
		.thresh1= 16 * 170.00 /*MHz*/,
		.thresh2= 16 * 450.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_ALPS_TSHC6_NTSC] = { /* Alps NTSC */
		.name   = "Alps TSCH6",
		.thresh1= 16 * 137.25 /*MHz*/,
		.thresh2= 16 * 385.25 /*MHz*/,
		.VHF_L  = 0x14,
		.VHF_H  = 0x12,
		.UHF    = 0x11,
		.config = 0x8e,
	},
	[TUNER_TEMIC_PAL_DK] = { /* TEMIC PAL */
		.name   = "Temic PAL_DK (4016 FY5)",
		.thresh1= 16 * 168.25 /*MHz*/,
		.thresh2= 16 * 456.25 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_PHILIPS_NTSC_M] = { /* Philips NTSC */
		.name   = "Philips NTSC_M (MK2)",
		.thresh1= 16 * 160.00 /*MHz*/,
		.thresh2= 16 * 454.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_TEMIC_4066FY5_PAL_I] = { /* TEMIC PAL_I */
		.name   = "Temic PAL_I (4066 FY5)",
		.thresh1= 16 * 169.00 /*MHz*/,
		.thresh2= 16 * 454.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_TEMIC_4006FN5_MULTI_PAL] = { /* TEMIC PAL */
		.name   = "Temic PAL* auto (4006 FN5)",
		.thresh1= 16 * 169.00 /*MHz*/,
		.thresh2= 16 * 454.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},

	/* 20-29 */
	[TUNER_TEMIC_4009FR5_PAL] = { /* TEMIC PAL */
		.name   = "Temic PAL_BG (4009 FR5) or PAL_I (4069 FR5)",
		.thresh1= 16 * 141.00 /*MHz*/,
		.thresh2= 16 * 464.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_TEMIC_4039FR5_NTSC] = { /* TEMIC NTSC */
		.name   = "Temic NTSC (4039 FR5)",
		.thresh1= 16 * 158.00 /*MHz*/,
		.thresh2= 16 * 453.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_TEMIC_4046FM5] = { /* TEMIC PAL */
		.name   = "Temic PAL/SECAM multi (4046 FM5)",
		.thresh1= 16 * 169.00 /*MHz*/,
		.thresh2= 16 * 454.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_PHILIPS_PAL_DK] = { /* Philips PAL */
		.name   = "Philips PAL_DK (FI1256 and compatibles)",
		.thresh1= 16 * 170.00 /*MHz*/,
		.thresh2= 16 * 450.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_PHILIPS_FQ1216ME] = { /* Philips PAL */
		.name   = "Philips PAL/SECAM multi (FQ1216ME)",
		.thresh1= 16 * 170.00 /*MHz*/,
		.thresh2= 16 * 450.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_LG_PAL_I_FM] = { /* LGINNOTEK PAL_I */
		.name   = "LG PAL_I+FM (TAPC-I001D)",
		.thresh1= 16 * 170.00 /*MHz*/,
		.thresh2= 16 * 450.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_LG_PAL_I] = { /* LGINNOTEK PAL_I */
		.name   = "LG PAL_I (TAPC-I701D)",
		.thresh1= 16 * 170.00 /*MHz*/,
		.thresh2= 16 * 450.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_LG_NTSC_FM] = { /* LGINNOTEK NTSC */
		.name   = "LG NTSC+FM (TPI8NSR01F)",
		.thresh1= 16 * 210.00 /*MHz*/,
		.thresh2= 16 * 497.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_LG_PAL_FM] = { /* LGINNOTEK PAL */
		.name   = "LG PAL_BG+FM (TPI8PSB01D)",
		.thresh1= 16 * 170.00 /*MHz*/,
		.thresh2= 16 * 450.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_LG_PAL] = { /* LGINNOTEK PAL */
		.name   = "LG PAL_BG (TPI8PSB11D)",
		.thresh1= 16 * 170.00 /*MHz*/,
		.thresh2= 16 * 450.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},

	/* 30-39 */
	[TUNER_TEMIC_4009FN5_MULTI_PAL_FM] = { /* TEMIC PAL */
		.name   = "Temic PAL* auto + FM (4009 FN5)",
		.thresh1= 16 * 141.00 /*MHz*/,
		.thresh2= 16 * 464.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_SHARP_2U5JF5540_NTSC] = { /* SHARP NTSC */
		.name   = "SHARP NTSC_JP (2U5JF5540)",
		.thresh1= 16 * 137.25 /*MHz*/,
		.thresh2= 16 * 317.25 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x08,
		.config = 0x8e,
	},
	[TUNER_Samsung_PAL_TCPM9091PD27] = { /* Samsung PAL */
		.name   = "Samsung PAL TCPM9091PD27",
		.thresh1= 16 * 169 /*MHz*/,
		.thresh2= 16 * 464 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_MT2032] = { /* Microtune PAL|NTSC */
		.name   = "MT20xx universal",
	  /* see mt20xx.c for details */ },
	[TUNER_TEMIC_4106FH5] = { /* TEMIC PAL */
		.name   = "Temic PAL_BG (4106 FH5)",
		.thresh1= 16 * 141.00 /*MHz*/,
		.thresh2= 16 * 464.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_TEMIC_4012FY5] = { /* TEMIC PAL */
		.name   = "Temic PAL_DK/SECAM_L (4012 FY5)",
		.thresh1= 16 * 140.25 /*MHz*/,
		.thresh2= 16 * 463.25 /*MHz*/,
		.VHF_L  = 0x02,
		.VHF_H  = 0x04,
		.UHF    = 0x01,
		.config = 0x8e,
	},
	[TUNER_TEMIC_4136FY5] = { /* TEMIC NTSC */
		.name   = "Temic NTSC (4136 FY5)",
		.thresh1= 16 * 158.00 /*MHz*/,
		.thresh2= 16 * 453.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_LG_PAL_NEW_TAPC] = { /* LGINNOTEK PAL */
		.name   = "LG PAL (newer TAPC series)",
		.thresh1= 16 * 170.00 /*MHz*/,
		.thresh2= 16 * 450.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x08,
		.config = 0x8e,
	},
	[TUNER_PHILIPS_FM1216ME_MK3] = { /* Philips PAL */
		.name   = "Philips PAL/SECAM multi (FM1216ME MK3)",
		.thresh1= 16 * 158.00 /*MHz*/,
		.thresh2= 16 * 442.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x04,
		.config = 0x8e,
	},
	[TUNER_LG_NTSC_NEW_TAPC] = { /* LGINNOTEK NTSC */
		.name   = "LG NTSC (newer TAPC series)",
		.thresh1= 16 * 170.00 /*MHz*/,
		.thresh2= 16 * 450.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x08,
		.config = 0x8e,
	},

	/* 40-49 */
	[TUNER_HITACHI_NTSC] = { /* HITACHI NTSC */
		.name   = "HITACHI V7-J180AT",
		.thresh1= 16 * 170.00 /*MHz*/,
		.thresh2= 16 * 450.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x08,
		.config = 0x8e,
	},
	[TUNER_PHILIPS_PAL_MK] = { /* Philips PAL */
		.name   = "Philips PAL_MK (FI1216 MK)",
		.thresh1= 16 * 140.25 /*MHz*/,
		.thresh2= 16 * 463.25 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0xc2,
		.UHF    = 0xcf,
		.config = 0x8e,
	},
	[TUNER_PHILIPS_ATSC] = { /* Philips ATSC */
		.name   = "Philips 1236D ATSC/NTSC daul in",
		.thresh1= 16 * 157.25 /*MHz*/,
		.thresh2= 16 * 454.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_PHILIPS_FM1236_MK3] = { /* Philips NTSC */
		.name   = "Philips NTSC MK3 (FM1236MK3 or FM1236/F)",
		.thresh1= 16 * 160.00 /*MHz*/,
		.thresh2= 16 * 442.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x04,
		.config = 0x8e,
	},
	[TUNER_PHILIPS_4IN1] = { /* Philips NTSC */
		.name   = "Philips 4 in 1 (ATI TV Wonder Pro/Conexant)",
		.thresh1= 16 * 160.00 /*MHz*/,
		.thresh2= 16 * 442.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x04,
		.config = 0x8e,
	},
	[TUNER_MICROTUNE_4049FM5] = { /* Microtune PAL */
		.name   = "Microtune 4049 FM5",
		.thresh1= 16 * 141.00 /*MHz*/,
		.thresh2= 16 * 464.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_PANASONIC_VP27] = { /* Panasonic NTSC */
		.name   = "Panasonic VP27s/ENGE4324D",
		.thresh1= 16 * 160.00 /*MHz*/,
		.thresh2= 16 * 454.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x08,
		.config = 0xce,
	},
	[TUNER_LG_NTSC_TAPE] = { /* LGINNOTEK NTSC */
		.name   = "LG NTSC (TAPE series)",
		.thresh1= 16 * 160.00 /*MHz*/,
		.thresh2= 16 * 442.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x04,
		.config = 0x8e,
	},
	[TUNER_TNF_8831BGFF] = { /* Philips PAL */
		.name   = "Tenna TNF 8831 BGFF)",
		.thresh1= 16 * 161.25 /*MHz*/,
		.thresh2= 16 * 463.25 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_MICROTUNE_4042FI5] = { /* Microtune NTSC */
		.name   = "Microtune 4042 FI5 ATSC/NTSC dual in",
		.thresh1= 16 * 162.00 /*MHz*/,
		.thresh2= 16 * 457.00 /*MHz*/,
		.VHF_L  = 0xa2,
		.VHF_H  = 0x94,
		.UHF    = 0x31,
		.config = 0x8e,
	},

	/* 50-59 */
	[TUNER_TCL_2002N] = { /* TCL NTSC */
		.name   = "TCL 2002N",
		.thresh1= 16 * 172.00 /*MHz*/,
		.thresh2= 16 * 448.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x08,
		.config = 0x8e,
	},
	[TUNER_PHILIPS_FM1256_IH3] = { /* Philips PAL */
		.name   = "Philips PAL/SECAM_D (FM 1256 I-H3)",
		.thresh1= 16 * 160.00 /*MHz*/,
		.thresh2= 16 * 442.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x04,
		.config = 0x8e,
	},
	[TUNER_THOMSON_DTT7610] = { /* THOMSON ATSC */
		.name   = "Thomson DDT 7610 (ATSC/NTSC)",
		.thresh1= 16 * 157.25 /*MHz*/,
		.thresh2= 16 * 454.00 /*MHz*/,
		.VHF_L  = 0x39,
		.VHF_H  = 0x3a,
		.UHF    = 0x3c,
		.config = 0x8e,
	},
	[TUNER_PHILIPS_FQ1286] = { /* Philips NTSC */
		.name   = "Philips FQ1286",
		.thresh1= 16 * 160.00 /*MHz*/,
		.thresh2= 16 * 454.00 /*MHz*/,
		.VHF_L  = 0x41,
		.VHF_H  = 0x42,
		.UHF    = 0x04,
		.config = 0x8e,
	},
	[TUNER_PHILIPS_TDA8290] = { /* Philips PAL|NTSC */
		.name   = "tda8290+75",
	  /* see tda8290.c for details */ },
	[TUNER_TCL_2002MB] = { /* TCL PAL */
		.name   = "TCL 2002MB",
		.thresh1= 16 * 170.00 /*MHz*/,
		.thresh2= 16 * 450.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x08,
		.config = 0xce,
	},
	[TUNER_PHILIPS_FQ1216AME_MK4] = { /* Philips PAL */
		.name   = "Philips PAL/SECAM multi (FQ1216AME MK4)",
		.thresh1= 16 * 160.00 /*MHz*/,
		.thresh2= 16 * 442.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x04,
		.config = 0xce,
	},
	[TUNER_PHILIPS_FQ1236A_MK4] = { /* Philips NTSC */
		.name   = "Philips FQ1236A MK4",
		.thresh1= 16 * 160.00 /*MHz*/,
		.thresh2= 16 * 442.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x04,
		.config = 0x8e,
	},
	[TUNER_YMEC_TVF_8531MF] = { /* Philips NTSC */
		.name   = "Ymec TVision TVF-8531MF/8831MF/8731MF",
		.thresh1= 16 * 160.00 /*MHz*/,
		.thresh2= 16 * 454.00 /*MHz*/,
		.VHF_L  = 0xa0,
		.VHF_H  = 0x90,
		.UHF    = 0x30,
		.config = 0x8e,
	},
	[TUNER_YMEC_TVF_5533MF] = { /* Philips NTSC */
		.name   = "Ymec TVision TVF-5533MF",
		.thresh1= 16 * 160.00 /*MHz*/,
		.thresh2= 16 * 454.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x04,
		.config = 0x8e,
	},

	/* 60-69 */
	[TUNER_THOMSON_DTT7611] = { /* THOMSON ATSC */
		.name   = "Thomson DDT 7611 (ATSC/NTSC)",
		.thresh1= 16 * 157.25 /*MHz*/,
		.thresh2= 16 * 454.00 /*MHz*/,
		.VHF_L  = 0x39,
		.VHF_H  = 0x3a,
		.UHF    = 0x3c,
		.config = 0x8e,
	},
	[TUNER_TENA_9533_DI] = { /* Philips PAL */
		.name   = "Tena TNF9533-D/IF/TNF9533-B/DF",
		.thresh1= 16 * 160.25 /*MHz*/,
		.thresh2= 16 * 464.25 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x04,
		.config = 0x8e,
	},
	[TUNER_TEA5767] = { /* Philips RADIO */
		.name   = "Philips TEA5767HN FM Radio",
	  /* see tea5767.c for details */},
	[TUNER_PHILIPS_FMD1216ME_MK3] = { /* Philips PAL */
		.name   = "Philips FMD1216ME MK3 Hybrid Tuner",
		.thresh1= 16 * 160.00 /*MHz*/,
		.thresh2= 16 * 442.00 /*MHz*/,
		.VHF_L  = 0x51,
		.VHF_H  = 0x52,
		.UHF    = 0x54,
		.config = 0x86,
	},
	[TUNER_LG_TDVS_H062F] = { /* LGINNOTEK ATSC */
		.name   = "LG TDVS-H062F/TUA6034",
		.thresh1= 16 * 160.00 /*MHz*/,
		.thresh2= 16 * 455.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x04,
		.config = 0x8e,
	},
	[TUNER_YMEC_TVF66T5_B_DFF] = { /* Philips PAL */
		.name   = "Ymec TVF66T5-B/DFF",
		.thresh1= 16 * 160.25 /*MHz*/,
		.thresh2= 16 * 464.25 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x08,
		.config = 0x8e,
	},
	[TUNER_LG_NTSC_TALN_MINI] = { /* LGINNOTEK NTSC */
		.name   = "LG NTSC (TALN mini series)",
		.thresh1= 16 * 137.25 /*MHz*/,
		.thresh2= 16 * 373.25 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x08,
		.config = 0x8e,
	},
	[TUNER_PHILIPS_TD1316] = { /* Philips PAL */
		.name   = "Philips TD1316 Hybrid Tuner",
		.thresh1= 16 * 160.00 /*MHz*/,
		.thresh2= 16 * 442.00 /*MHz*/,
		.VHF_L  = 0xa1,
		.VHF_H  = 0xa2,
		.UHF    = 0xa4,
		.config = 0xc8,
	},
	[TUNER_PHILIPS_TUV1236D] = { /* Philips ATSC */
		.name   = "Philips TUV1236D ATSC/NTSC dual in",
		.thresh1= 16 * 157.25 /*MHz*/,
		.thresh2= 16 * 454.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x04,
		.config = 0xce,
	},
	[TUNER_TNF_5335MF] = { /* Philips NTSC */
		.name   = "Tena TNF 5335 MF",
		.thresh1= 16 * 157.25 /*MHz*/,
		.thresh2= 16 * 454.00 /*MHz*/,
		.VHF_L  = 0x01,
		.VHF_H  = 0x02,
		.UHF    = 0x04,
		.config = 0x8e,
	},
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
	u8 config, tuneraddr;
	u16 div;
	struct tunertype *tun;
	unsigned char buffer[4];
	int rc, IFPCoff;

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

	case TUNER_PHILIPS_TUV1236D:
		/* 0x40 -> ATSC antenna input 1 */
		/* 0x48 -> ATSC antenna input 2 */
		/* 0x00 -> NTSC antenna input 1 */
		/* 0x08 -> NTSC antenna input 2 */
		buffer[0] = 0x14;
		buffer[1] = 0x00;
		buffer[2] = 0x17;
		buffer[3] = 0x00;
		config &= ~0x40;
		if (t->std & V4L2_STD_ATSC) {
			config |= 0x40;
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
		IFPCoff = 940;
	} else if ((t->std & V4L2_STD_MN) &&
		  !(t->std & ~V4L2_STD_MN)) {
		IFPCoff = 732;
	} else if (t->std == V4L2_STD_SECAM_LC) {
		IFPCoff = 543;
	} else {
		IFPCoff = 623;
	}

	div=freq + IFPCoff + offset;

	tuner_dbg("Freq= %d.%02d MHz, V_IF=%d.%02d MHz, Offset=%d.%02d MHz, div=%0d\n",
					freq / 16, freq % 16 * 100 / 16,
					IFPCoff / 16, IFPCoff % 16 * 100 / 16,
					offset / 16, offset % 16 * 100 / 16,
					div);

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
