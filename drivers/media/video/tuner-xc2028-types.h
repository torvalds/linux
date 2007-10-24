/* tuner-xc2028_types
 *
 * Copyright (c) 2007 Mauro Carvalho Chehab (mchehab@infradead.org)
 * This code is placed under the terms of the GNU General Public License v2
 */

/* xc3028 firmware types */

/* BASE firmware should be loaded before any other firmware */
#define BASE		(1<<0)

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
#define DTV6_ATSC	(1<<5)
#define DTV6_QAM	(1<<6)
#define DTV7		(1<<7)
#define DTV78		(1<<8)
#define DTV8		(1<<9)

/* There's a FM | BASE firmware + FM specific firmware (std=0) */
#define	FM		(1<<10)

/* Applies only for FM firmware
   Makes it use RF input 1 (pin #2) instead of input 2 (pin #4)
 */
#define INPUT1		(1<<11)


/* LCD firmwares exist only for MTS STD/MN (PAL or NTSC/M)
	and for non-MTS STD/MN (PAL, NTSC/M or NTSC/Kr)
	There are variants both with and without NOGD
 */
#define LCD		(1<<12)

/* NOGD firmwares exist only for MTS STD/MN (PAL or NTSC/M)
	and for non-MTS STD/MN (PAL, NTSC/M or NTSC/Kr)
 */
#define NOGD		(1<<13)

/* Old firmwares were broken into init0 and init1 */
#define INIT1		(1<<14)

/* Newer types to be moved to videodev2.h */

#define V4L2_STD_SECAM_K3	(0x02000000)

/* Audio types */

#define V4L2_STD_A2_A		(1L<<32)
#define V4L2_STD_A2_B		(1L<<33)
#define V4L2_STD_NICAM_A	(1L<<34)
#define V4L2_STD_NICAM_B	(1L<<35)
#define V4L2_STD_AM		(1L<<36)
#define V4L2_STD_BTSC		(1L<<37)
#define V4L2_STD__EIAJ		(1L<<38)

#define V4L2_STD_A2		(V4L2_STD_A2_A    | V4L2_STD_A2_B)
#define V4L2_STD_NICAM		(V4L2_STD_NICAM_A | V4L2_STD_NICAM_B)

/* To preserve backward compatibilty,
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
