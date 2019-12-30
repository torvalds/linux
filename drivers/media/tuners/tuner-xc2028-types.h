/* SPDX-License-Identifier: GPL-2.0 */
/*
 * tuner-xc2028_types
 *
 * This file includes internal tipes to be used inside tuner-xc2028.
 * Shouldn't be included outside tuner-xc2028
 *
 * Copyright (c) 2007-2008 Mauro Carvalho Chehab <mchehab@kernel.org>
 */

/* xc3028 firmware types */

/* BASE firmware should be loaded before any other firmware */
#define BASE		(1<<0)
#define BASE_TYPES	(BASE|F8MHZ|MTS|FM|INPUT1|INPUT2|INIT1)

/* F8MHZ marks BASE firmwares for 8 MHz Bandwidth */
#define F8MHZ		(1<<1)

/* Multichannel Television Sound (MTS)
   Those firmwares are capable of using xc2038 DSP to decode audio and
   produce a baseband audio output on some pins of the chip.
   There are MTS firmwares for the most used video standards. It should be
   required to use MTS firmwares, depending on the way audio is routed into
   the bridge chip
 */
#define MTS		(1<<2)

/* FIXME: I have no idea what's the difference between
   D2620 and D2633 firmwares
 */
#define D2620		(1<<3)
#define D2633		(1<<4)

/* DTV firmwares for 6, 7 and 8 MHz
   DTV6 - 6MHz - ATSC/DVB-C/DVB-T/ISDB-T/DOCSIS
   DTV8 - 8MHz - DVB-C/DVB-T
 */
#define DTV6           (1 << 5)
#define QAM            (1 << 6)
#define DTV7		(1<<7)
#define DTV78		(1<<8)
#define DTV8		(1<<9)

#define DTV_TYPES	(D2620|D2633|DTV6|QAM|DTV7|DTV78|DTV8|ATSC)

/* There's a FM | BASE firmware + FM specific firmware (std=0) */
#define	FM		(1<<10)

#define STD_SPECIFIC_TYPES (MTS|FM|LCD|NOGD)

/* Applies only for FM firmware
   Makes it use RF input 1 (pin #2) instead of input 2 (pin #4)
 */
#define INPUT1		(1<<11)


/* LCD firmwares exist only for MTS STD/MN (PAL or NTSC/M)
	and for non-MTS STD/MN (PAL, NTSC/M or NTSC/Kr)
	There are variants both with and without NOGD
	Those firmwares produce better result with LCD displays
 */
#define LCD		(1<<12)

/* NOGD firmwares exist only for MTS STD/MN (PAL or NTSC/M)
	and for non-MTS STD/MN (PAL, NTSC/M or NTSC/Kr)
	The NOGD firmwares don't have group delay compensation filter
 */
#define NOGD		(1<<13)

/* Old firmwares were broken into init0 and init1 */
#define INIT1		(1<<14)

/* SCODE firmware selects particular behaviours */
#define MONO           (1 << 15)
#define ATSC           (1 << 16)
#define IF             (1 << 17)
#define LG60           (1 << 18)
#define ATI638         (1 << 19)
#define OREN538        (1 << 20)
#define OREN36         (1 << 21)
#define TOYOTA388      (1 << 22)
#define TOYOTA794      (1 << 23)
#define DIBCOM52       (1 << 24)
#define ZARLINK456     (1 << 25)
#define CHINA          (1 << 26)
#define F6MHZ          (1 << 27)
#define INPUT2         (1 << 28)
#define SCODE          (1 << 29)

/* This flag identifies that the scode table has a new format */
#define HAS_IF         (1 << 30)

/* There are different scode tables for MTS and non-MTS.
   The MTS firmwares support mono only
  */
#define SCODE_TYPES (SCODE | MTS)


/* Newer types not defined on videodev2.h.
   The original idea were to move all those types to videodev2.h, but
   it seemed overkill, since, with the exception of SECAM/K3, the other
   types seem to be autodetected.
   It is not clear where secam/k3 is used, nor we have a feedback of this
   working or being autodetected by the standard secam firmware.
 */

#define V4L2_STD_SECAM_K3	(0x04000000)

/* Audio types */

#define V4L2_STD_A2_A		(1LL<<32)
#define V4L2_STD_A2_B		(1LL<<33)
#define V4L2_STD_NICAM_A	(1LL<<34)
#define V4L2_STD_NICAM_B	(1LL<<35)
#define V4L2_STD_AM		(1LL<<36)
#define V4L2_STD_BTSC		(1LL<<37)
#define V4L2_STD_EIAJ		(1LL<<38)

#define V4L2_STD_A2		(V4L2_STD_A2_A    | V4L2_STD_A2_B)
#define V4L2_STD_NICAM		(V4L2_STD_NICAM_A | V4L2_STD_NICAM_B)

/* To preserve backward compatibility,
   (std & V4L2_STD_AUDIO) = 0 means that ALL audio stds are supported
 */

#define V4L2_STD_AUDIO		(V4L2_STD_A2    | \
				 V4L2_STD_NICAM | \
				 V4L2_STD_AM    | \
				 V4L2_STD_BTSC  | \
				 V4L2_STD_EIAJ)

/* Used standards with audio restrictions */

#define V4L2_STD_PAL_BG_A2_A	(V4L2_STD_PAL_BG | V4L2_STD_A2_A)
#define V4L2_STD_PAL_BG_A2_B	(V4L2_STD_PAL_BG | V4L2_STD_A2_B)
#define V4L2_STD_PAL_BG_NICAM_A	(V4L2_STD_PAL_BG | V4L2_STD_NICAM_A)
#define V4L2_STD_PAL_BG_NICAM_B	(V4L2_STD_PAL_BG | V4L2_STD_NICAM_B)
#define V4L2_STD_PAL_DK_A2	(V4L2_STD_PAL_DK | V4L2_STD_A2)
#define V4L2_STD_PAL_DK_NICAM	(V4L2_STD_PAL_DK | V4L2_STD_NICAM)
#define V4L2_STD_SECAM_L_NICAM	(V4L2_STD_SECAM_L | V4L2_STD_NICAM)
#define V4L2_STD_SECAM_L_AM	(V4L2_STD_SECAM_L | V4L2_STD_AM)
