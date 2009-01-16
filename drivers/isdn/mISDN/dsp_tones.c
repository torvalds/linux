/*
 * Audio support data for ISDN4Linux.
 *
 * Copyright Andreas Eversberg (jolly@eversberg.eu)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/mISDNif.h>
#include <linux/mISDNdsp.h>
#include "core.h"
#include "dsp.h"


#define DATA_S sample_silence
#define SIZE_S (&sizeof_silence)
#define DATA_GA sample_german_all
#define SIZE_GA (&sizeof_german_all)
#define DATA_GO sample_german_old
#define SIZE_GO (&sizeof_german_old)
#define DATA_DT sample_american_dialtone
#define SIZE_DT (&sizeof_american_dialtone)
#define DATA_RI sample_american_ringing
#define SIZE_RI (&sizeof_american_ringing)
#define DATA_BU sample_american_busy
#define SIZE_BU (&sizeof_american_busy)
#define DATA_S1 sample_special1
#define SIZE_S1 (&sizeof_special1)
#define DATA_S2 sample_special2
#define SIZE_S2 (&sizeof_special2)
#define DATA_S3 sample_special3
#define SIZE_S3 (&sizeof_special3)

/***************/
/* tones loops */
/***************/

/* all tones are alaw encoded */
/* the last sample+1 is in phase with the first sample. the error is low */

static u8 sample_german_all[] = {
	0x80, 0xab, 0x81, 0x6d, 0xfd, 0xdd, 0x5d, 0x9d,
	0x4d, 0xd1, 0x89, 0x88, 0xd0, 0x4c, 0x9c, 0x5c,
	0xdc, 0xfc, 0x6c,
	0x80, 0xab, 0x81, 0x6d, 0xfd, 0xdd, 0x5d, 0x9d,
	0x4d, 0xd1, 0x89, 0x88, 0xd0, 0x4c, 0x9c, 0x5c,
	0xdc, 0xfc, 0x6c,
	0x80, 0xab, 0x81, 0x6d, 0xfd, 0xdd, 0x5d, 0x9d,
	0x4d, 0xd1, 0x89, 0x88, 0xd0, 0x4c, 0x9c, 0x5c,
	0xdc, 0xfc, 0x6c,
	0x80, 0xab, 0x81, 0x6d, 0xfd, 0xdd, 0x5d, 0x9d,
	0x4d, 0xd1, 0x89, 0x88, 0xd0, 0x4c, 0x9c, 0x5c,
	0xdc, 0xfc, 0x6c,
};
static u32 sizeof_german_all = sizeof(sample_german_all);

static u8 sample_german_old[] = {
	0xec, 0x68, 0xe1, 0x6d, 0x6d, 0x91, 0x51, 0xed,
	0x6d, 0x01, 0x1e, 0x10, 0x0c, 0x90, 0x60, 0x70,
	0x8c,
	0xec, 0x68, 0xe1, 0x6d, 0x6d, 0x91, 0x51, 0xed,
	0x6d, 0x01, 0x1e, 0x10, 0x0c, 0x90, 0x60, 0x70,
	0x8c,
	0xec, 0x68, 0xe1, 0x6d, 0x6d, 0x91, 0x51, 0xed,
	0x6d, 0x01, 0x1e, 0x10, 0x0c, 0x90, 0x60, 0x70,
	0x8c,
	0xec, 0x68, 0xe1, 0x6d, 0x6d, 0x91, 0x51, 0xed,
	0x6d, 0x01, 0x1e, 0x10, 0x0c, 0x90, 0x60, 0x70,
	0x8c,
};
static u32 sizeof_german_old = sizeof(sample_german_old);

static u8 sample_american_dialtone[] = {
	0x2a, 0x18, 0x90, 0x6c, 0x4c, 0xbc, 0x4c, 0x6c,
	0x10, 0x58, 0x32, 0xb9, 0x31, 0x2d, 0x8d, 0x0d,
	0x8d, 0x2d, 0x31, 0x99, 0x0f, 0x28, 0x60, 0xf0,
	0xd0, 0x50, 0xd0, 0x30, 0x60, 0x08, 0x8e, 0x67,
	0x09, 0x19, 0x21, 0xe1, 0xd9, 0xb9, 0x29, 0x67,
	0x83, 0x02, 0xce, 0xbe, 0xee, 0x1a, 0x1b, 0xef,
	0xbf, 0xcf, 0x03, 0x82, 0x66, 0x28, 0xb8, 0xd8,
	0xe0, 0x20, 0x18, 0x08, 0x66, 0x8f, 0x09, 0x61,
	0x31, 0xd1, 0x51, 0xd1, 0xf1, 0x61, 0x29, 0x0e,
	0x98, 0x30, 0x2c, 0x8c, 0x0c, 0x8c, 0x2c, 0x30,
	0xb8, 0x33, 0x59, 0x11, 0x6d, 0x4d, 0xbd, 0x4d,
	0x6d, 0x91, 0x19,
};
static u32 sizeof_american_dialtone = sizeof(sample_american_dialtone);

static u8 sample_american_ringing[] = {
	0x2a, 0xe0, 0xac, 0x0c, 0xbc, 0x4c, 0x8c, 0x90,
	0x48, 0xc7, 0xc1, 0xed, 0xcd, 0x4d, 0xcd, 0xed,
	0xc1, 0xb7, 0x08, 0x30, 0xec, 0xcc, 0xcc, 0x8c,
	0x10, 0x58, 0x1a, 0x99, 0x71, 0xed, 0x8d, 0x8d,
	0x2d, 0x41, 0x89, 0x9e, 0x20, 0x70, 0x2c, 0xec,
	0x2c, 0x70, 0x20, 0x86, 0x77, 0xe1, 0x31, 0x11,
	0xd1, 0xf1, 0x81, 0x09, 0xa3, 0x56, 0x58, 0x00,
	0x40, 0xc0, 0x60, 0x38, 0x46, 0x43, 0x57, 0x39,
	0xd9, 0x59, 0x99, 0xc9, 0x77, 0x2f, 0x2e, 0xc6,
	0xd6, 0x28, 0xd6, 0x36, 0x26, 0x2e, 0x8a, 0xa3,
	0x43, 0x63, 0x4b, 0x4a, 0x62, 0x42, 0xa2, 0x8b,
	0x2f, 0x27, 0x37, 0xd7, 0x29, 0xd7, 0xc7, 0x2f,
	0x2e, 0x76, 0xc8, 0x98, 0x58, 0xd8, 0x38, 0x56,
	0x42, 0x47, 0x39, 0x61, 0xc1, 0x41, 0x01, 0x59,
	0x57, 0xa2, 0x08, 0x80, 0xf0, 0xd0, 0x10, 0x30,
	0xe0, 0x76, 0x87, 0x21, 0x71, 0x2d, 0xed, 0x2d,
	0x71, 0x21, 0x9f, 0x88, 0x40, 0x2c, 0x8c, 0x8c,
	0xec, 0x70, 0x98, 0x1b, 0x59, 0x11, 0x8d, 0xcd,
	0xcd, 0xed, 0x31, 0x09, 0xb6, 0xc0, 0xec, 0xcc,
	0x4c, 0xcc, 0xec, 0xc0, 0xc6, 0x49, 0x91, 0x8d,
	0x4d, 0xbd, 0x0d, 0xad, 0xe1,
};
static u32 sizeof_american_ringing = sizeof(sample_american_ringing);

static u8 sample_american_busy[] = {
	0x2a, 0x00, 0x6c, 0x4c, 0x4c, 0x6c, 0xb0, 0x66,
	0x99, 0x11, 0x6d, 0x8d, 0x2d, 0x41, 0xd7, 0x96,
	0x60, 0xf0, 0x70, 0x40, 0x58, 0xf6, 0x53, 0x57,
	0x09, 0x89, 0xd7, 0x5f, 0xe3, 0x2a, 0xe3, 0x5f,
	0xd7, 0x89, 0x09, 0x57, 0x53, 0xf6, 0x58, 0x40,
	0x70, 0xf0, 0x60, 0x96, 0xd7, 0x41, 0x2d, 0x8d,
	0x6d, 0x11, 0x99, 0x66, 0xb0, 0x6c, 0x4c, 0x4c,
	0x6c, 0x00, 0x2a, 0x01, 0x6d, 0x4d, 0x4d, 0x6d,
	0xb1, 0x67, 0x98, 0x10, 0x6c, 0x8c, 0x2c, 0x40,
	0xd6, 0x97, 0x61, 0xf1, 0x71, 0x41, 0x59, 0xf7,
	0x52, 0x56, 0x08, 0x88, 0xd6, 0x5e, 0xe2, 0x2a,
	0xe2, 0x5e, 0xd6, 0x88, 0x08, 0x56, 0x52, 0xf7,
	0x59, 0x41, 0x71, 0xf1, 0x61, 0x97, 0xd6, 0x40,
	0x2c, 0x8c, 0x6c, 0x10, 0x98, 0x67, 0xb1, 0x6d,
	0x4d, 0x4d, 0x6d, 0x01,
};
static u32 sizeof_american_busy = sizeof(sample_american_busy);

static u8 sample_special1[] = {
	0x2a, 0x2c, 0xbc, 0x6c, 0xd6, 0x71, 0xbd, 0x0d,
	0xd9, 0x80, 0xcc, 0x4c, 0x40, 0x39, 0x0d, 0xbd,
	0x11, 0x86, 0xec, 0xbc, 0xec, 0x0e, 0x51, 0xbd,
	0x8d, 0x89, 0x30, 0x4c, 0xcc, 0xe0, 0xe1, 0xcd,
	0x4d, 0x31, 0x88, 0x8c, 0xbc, 0x50, 0x0f, 0xed,
	0xbd, 0xed, 0x87, 0x10, 0xbc, 0x0c, 0x38, 0x41,
	0x4d, 0xcd, 0x81, 0xd8, 0x0c, 0xbc, 0x70, 0xd7,
	0x6d, 0xbd, 0x2d,
};
static u32 sizeof_special1 = sizeof(sample_special1);

static u8 sample_special2[] = {
	0x2a, 0xcc, 0x8c, 0xd7, 0x4d, 0x2d, 0x18, 0xbc,
	0x10, 0xc1, 0xbd, 0xc1, 0x10, 0xbc, 0x18, 0x2d,
	0x4d, 0xd7, 0x8c, 0xcc, 0x2a, 0xcd, 0x8d, 0xd6,
	0x4c, 0x2c, 0x19, 0xbd, 0x11, 0xc0, 0xbc, 0xc0,
	0x11, 0xbd, 0x19, 0x2c, 0x4c, 0xd6, 0x8d, 0xcd,
	0x2a, 0xcc, 0x8c, 0xd7, 0x4d, 0x2d, 0x18, 0xbc,
	0x10, 0xc1, 0xbd, 0xc1, 0x10, 0xbc, 0x18, 0x2d,
	0x4d, 0xd7, 0x8c, 0xcc, 0x2a, 0xcd, 0x8d, 0xd6,
	0x4c, 0x2c, 0x19, 0xbd, 0x11, 0xc0, 0xbc, 0xc0,
	0x11, 0xbd, 0x19, 0x2c, 0x4c, 0xd6, 0x8d, 0xcd,
};
static u32 sizeof_special2 = sizeof(sample_special2);

static u8 sample_special3[] = {
	0x2a, 0xbc, 0x18, 0xcd, 0x11, 0x2c, 0x8c, 0xc1,
	0x4d, 0xd6, 0xbc, 0xd6, 0x4d, 0xc1, 0x8c, 0x2c,
	0x11, 0xcd, 0x18, 0xbc, 0x2a, 0xbd, 0x19, 0xcc,
	0x10, 0x2d, 0x8d, 0xc0, 0x4c, 0xd7, 0xbd, 0xd7,
	0x4c, 0xc0, 0x8d, 0x2d, 0x10, 0xcc, 0x19, 0xbd,
	0x2a, 0xbc, 0x18, 0xcd, 0x11, 0x2c, 0x8c, 0xc1,
	0x4d, 0xd6, 0xbc, 0xd6, 0x4d, 0xc1, 0x8c, 0x2c,
	0x11, 0xcd, 0x18, 0xbc, 0x2a, 0xbd, 0x19, 0xcc,
	0x10, 0x2d, 0x8d, 0xc0, 0x4c, 0xd7, 0xbd, 0xd7,
	0x4c, 0xc0, 0x8d, 0x2d, 0x10, 0xcc, 0x19, 0xbd,
};
static u32 sizeof_special3 = sizeof(sample_special3);

static u8 sample_silence[] = {
	0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a,
	0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a,
	0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a,
	0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a,
	0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a,
	0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a,
	0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a,
	0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a,
	0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a,
	0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a,
	0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a,
	0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a, 0x2a,
};
static u32 sizeof_silence = sizeof(sample_silence);

struct tones_samples {
	u32 *len;
	u8 *data;
};
static struct
tones_samples samples[] = {
	{&sizeof_german_all, sample_german_all},
	{&sizeof_german_old, sample_german_old},
	{&sizeof_american_dialtone, sample_american_dialtone},
	{&sizeof_american_ringing, sample_american_ringing},
	{&sizeof_american_busy, sample_american_busy},
	{&sizeof_special1, sample_special1},
	{&sizeof_special2, sample_special2},
	{&sizeof_special3, sample_special3},
	{NULL, NULL},
};

/***********************************
 * generate ulaw from alaw samples *
 ***********************************/

void
dsp_audio_generate_ulaw_samples(void)
{
	int i, j;

	i = 0;
	while (samples[i].len) {
		j = 0;
		while (j < (*samples[i].len)) {
			samples[i].data[j] =
				dsp_audio_alaw_to_ulaw[samples[i].data[j]];
			j++;
		}
		i++;
	}
}


/****************************
 * tone sequence definition *
 ****************************/

static struct pattern {
	int tone;
	u8 *data[10];
	u32 *siz[10];
	u32 seq[10];
} pattern[] = {
	{TONE_GERMAN_DIALTONE,
	{DATA_GA, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_GA, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{1900, 0, 0, 0, 0, 0, 0, 0, 0, 0} },

	{TONE_GERMAN_OLDDIALTONE,
	{DATA_GO, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_GO, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{1998, 0, 0, 0, 0, 0, 0, 0, 0, 0} },

	{TONE_AMERICAN_DIALTONE,
	{DATA_DT, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_DT, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{8000, 0, 0, 0, 0, 0, 0, 0, 0, 0} },

	{TONE_GERMAN_DIALPBX,
	{DATA_GA, DATA_S, DATA_GA, DATA_S, DATA_GA, DATA_S, NULL, NULL, NULL, NULL},
	{SIZE_GA, SIZE_S, SIZE_GA, SIZE_S, SIZE_GA, SIZE_S, NULL, NULL, NULL, NULL},
	{2000, 2000, 2000, 2000, 2000, 12000, 0, 0, 0, 0} },

	{TONE_GERMAN_OLDDIALPBX,
	{DATA_GO, DATA_S, DATA_GO, DATA_S, DATA_GO, DATA_S, NULL, NULL, NULL, NULL},
	{SIZE_GO, SIZE_S, SIZE_GO, SIZE_S, SIZE_GO, SIZE_S, NULL, NULL, NULL, NULL},
	{2000, 2000, 2000, 2000, 2000, 12000, 0, 0, 0, 0} },

	{TONE_AMERICAN_DIALPBX,
	{DATA_DT, DATA_S, DATA_DT, DATA_S, DATA_DT, DATA_S, NULL, NULL, NULL, NULL},
	{SIZE_DT, SIZE_S, SIZE_DT, SIZE_S, SIZE_DT, SIZE_S, NULL, NULL, NULL, NULL},
	{2000, 2000, 2000, 2000, 2000, 12000, 0, 0, 0, 0} },

	{TONE_GERMAN_RINGING,
	{DATA_GA, DATA_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_GA, SIZE_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{8000, 32000, 0, 0, 0, 0, 0, 0, 0, 0} },

	{TONE_GERMAN_OLDRINGING,
	{DATA_GO, DATA_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_GO, SIZE_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{8000, 40000, 0, 0, 0, 0, 0, 0, 0, 0} },

	{TONE_AMERICAN_RINGING,
	{DATA_RI, DATA_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_RI, SIZE_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{8000, 32000, 0, 0, 0, 0, 0, 0, 0, 0} },

	{TONE_GERMAN_RINGPBX,
	{DATA_GA, DATA_S, DATA_GA, DATA_S, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_GA, SIZE_S, SIZE_GA, SIZE_S, NULL, NULL, NULL, NULL, NULL, NULL},
	{4000, 4000, 4000, 28000, 0, 0, 0, 0, 0, 0} },

	{TONE_GERMAN_OLDRINGPBX,
	{DATA_GO, DATA_S, DATA_GO, DATA_S, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_GO, SIZE_S, SIZE_GO, SIZE_S, NULL, NULL, NULL, NULL, NULL, NULL},
	{4000, 4000, 4000, 28000, 0, 0, 0, 0, 0, 0} },

	{TONE_AMERICAN_RINGPBX,
	{DATA_RI, DATA_S, DATA_RI, DATA_S, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_RI, SIZE_S, SIZE_RI, SIZE_S, NULL, NULL, NULL, NULL, NULL, NULL},
	{4000, 4000, 4000, 28000, 0, 0, 0, 0, 0, 0} },

	{TONE_GERMAN_BUSY,
	{DATA_GA, DATA_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_GA, SIZE_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{4000, 4000, 0, 0, 0, 0, 0, 0, 0, 0} },

	{TONE_GERMAN_OLDBUSY,
	{DATA_GO, DATA_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_GO, SIZE_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{1000, 5000, 0, 0, 0, 0, 0, 0, 0, 0} },

	{TONE_AMERICAN_BUSY,
	{DATA_BU, DATA_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_BU, SIZE_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{4000, 4000, 0, 0, 0, 0, 0, 0, 0, 0} },

	{TONE_GERMAN_HANGUP,
	{DATA_GA, DATA_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_GA, SIZE_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{4000, 4000, 0, 0, 0, 0, 0, 0, 0, 0} },

	{TONE_GERMAN_OLDHANGUP,
	{DATA_GO, DATA_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_GO, SIZE_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{1000, 5000, 0, 0, 0, 0, 0, 0, 0, 0} },

	{TONE_AMERICAN_HANGUP,
	{DATA_DT, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_DT, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{8000, 0, 0, 0, 0, 0, 0, 0, 0, 0} },

	{TONE_SPECIAL_INFO,
	{DATA_S1, DATA_S2, DATA_S3, DATA_S, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_S1, SIZE_S2, SIZE_S3, SIZE_S, NULL, NULL, NULL, NULL, NULL, NULL},
	{2666, 2666, 2666, 8002, 0, 0, 0, 0, 0, 0} },

	{TONE_GERMAN_GASSENBESETZT,
	{DATA_GA, DATA_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_GA, SIZE_S, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{2000, 2000, 0, 0, 0, 0, 0, 0, 0, 0} },

	{TONE_GERMAN_AUFSCHALTTON,
	{DATA_GO, DATA_S, DATA_GO, DATA_S, NULL, NULL, NULL, NULL, NULL, NULL},
	{SIZE_GO, SIZE_S, SIZE_GO, SIZE_S, NULL, NULL, NULL, NULL, NULL, NULL},
	{1000, 5000, 1000, 17000, 0, 0, 0, 0, 0, 0} },

	{0,
	{NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0} },
};

/******************
 * copy tone data *
 ******************/

/* an sk_buff is generated from the number of samples needed.
 * the count will be changed and may begin from 0 each pattern period.
 * the clue is to precalculate the pointers and legths to use only one
 * memcpy per function call, or two memcpy if the tone sequence changes.
 *
 * pattern - the type of the pattern
 * count - the sample from the beginning of the pattern (phase)
 * len - the number of bytes
 *
 * return - the sk_buff with the sample
 *
 * if tones has finished (e.g. knocking tone), dsp->tones is turned off
 */
void dsp_tone_copy(struct dsp *dsp, u8 *data, int len)
{
	int index, count, start, num;
	struct pattern *pat;
	struct dsp_tone *tone = &dsp->tone;

	/* if we have no tone, we copy silence */
	if (!tone->tone) {
		memset(data, dsp_silence, len);
		return;
	}

	/* process pattern */
	pat = (struct pattern *)tone->pattern;
		/* points to the current pattern */
	index = tone->index; /* gives current sequence index */
	count = tone->count; /* gives current sample */

	/* copy sample */
	while (len) {
		/* find sample to start with */
		while (42) {
			/* warp arround */
			if (!pat->seq[index]) {
				count = 0;
				index = 0;
			}
			/* check if we are currently playing this tone */
			if (count < pat->seq[index])
				break;
			if (dsp_debug & DEBUG_DSP_TONE)
				printk(KERN_DEBUG "%s: reaching next sequence "
					"(index=%d)\n", __func__, index);
			count -= pat->seq[index];
			index++;
		}
		/* calculate start and number of samples */
		start = count % (*(pat->siz[index]));
		num = len;
		if (num+count > pat->seq[index])
			num = pat->seq[index] - count;
		if (num+start > (*(pat->siz[index])))
			num = (*(pat->siz[index])) - start;
		/* copy memory */
		memcpy(data, pat->data[index]+start, num);
		/* reduce length */
		data += num;
		count += num;
		len -= num;
	}
	tone->index = index;
	tone->count = count;

	/* return sk_buff */
	return;
}


/*******************************
 * send HW message to hfc card *
 *******************************/

static void
dsp_tone_hw_message(struct dsp *dsp, u8 *sample, int len)
{
	struct sk_buff *nskb;

	/* unlocking is not required, because we don't expect a response */
	nskb = _alloc_mISDN_skb(PH_CONTROL_REQ,
		(len)?HFC_SPL_LOOP_ON:HFC_SPL_LOOP_OFF, len, sample,
		GFP_ATOMIC);
	if (nskb) {
		if (dsp->ch.peer) {
			if (dsp->ch.recv(dsp->ch.peer, nskb))
				dev_kfree_skb(nskb);
		} else
			dev_kfree_skb(nskb);
	}
}


/*****************
 * timer expires *
 *****************/
void
dsp_tone_timeout(void *arg)
{
	struct dsp *dsp = arg;
	struct dsp_tone *tone = &dsp->tone;
	struct pattern *pat = (struct pattern *)tone->pattern;
	int index = tone->index;

	if (!tone->tone)
		return;

	index++;
	if (!pat->seq[index])
		index = 0;
	tone->index = index;

	/* set next tone */
	if (pat->data[index] == DATA_S)
		dsp_tone_hw_message(dsp, NULL, 0);
	else
		dsp_tone_hw_message(dsp, pat->data[index], *(pat->siz[index]));
	/* set timer */
	init_timer(&tone->tl);
	tone->tl.expires = jiffies + (pat->seq[index] * HZ) / 8000;
	add_timer(&tone->tl);
}


/********************
 * set/release tone *
 ********************/

/*
 * tones are relaized by streaming or by special loop commands if supported
 * by hardware. when hardware is used, the patterns will be controlled by
 * timers.
 */
int
dsp_tone(struct dsp *dsp, int tone)
{
	struct pattern *pat;
	int i;
	struct dsp_tone *tonet = &dsp->tone;

	tonet->software = 0;
	tonet->hardware = 0;

	/* we turn off the tone */
	if (!tone) {
		if (dsp->features.hfc_loops)
		if (timer_pending(&tonet->tl))
			del_timer(&tonet->tl);
		if (dsp->features.hfc_loops)
			dsp_tone_hw_message(dsp, NULL, 0);
		tonet->tone = 0;
		return 0;
	}

	pat = NULL;
	i = 0;
	while (pattern[i].tone) {
		if (pattern[i].tone == tone) {
			pat = &pattern[i];
			break;
		}
		i++;
	}
	if (!pat) {
		printk(KERN_WARNING "dsp: given tone 0x%x is invalid\n", tone);
		return -EINVAL;
	}
	if (dsp_debug & DEBUG_DSP_TONE)
		printk(KERN_DEBUG "%s: now starting tone %d (index=%d)\n",
			__func__, tone, 0);
	tonet->tone = tone;
	tonet->pattern = pat;
	tonet->index = 0;
	tonet->count = 0;

	if (dsp->features.hfc_loops) {
		tonet->hardware = 1;
		/* set first tone */
		dsp_tone_hw_message(dsp, pat->data[0], *(pat->siz[0]));
		/* set timer */
		if (timer_pending(&tonet->tl))
			del_timer(&tonet->tl);
		init_timer(&tonet->tl);
		tonet->tl.expires = jiffies + (pat->seq[0] * HZ) / 8000;
		add_timer(&tonet->tl);
	} else {
		tonet->software = 1;
	}

	return 0;
}





