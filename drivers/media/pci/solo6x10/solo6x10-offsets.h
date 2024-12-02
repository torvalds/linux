/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2010-2013 Bluecherry, LLC <https://www.bluecherrydvr.com>
 *
 * Original author:
 * Ben Collins <bcollins@ubuntu.com>
 *
 * Additional work by:
 * John Brooks <john.brooks@bluecherry.net>
 */

#ifndef __SOLO6X10_OFFSETS_H
#define __SOLO6X10_OFFSETS_H

#define SOLO_DISP_EXT_ADDR			0x00000000
#define SOLO_DISP_EXT_SIZE			0x00480000

#define SOLO_EOSD_EXT_ADDR \
	(SOLO_DISP_EXT_ADDR + SOLO_DISP_EXT_SIZE)
#define SOLO_EOSD_EXT_SIZE(__solo) \
	(__solo->type == SOLO_DEV_6010 ? 0x10000 : 0x20000)
#define SOLO_EOSD_EXT_SIZE_MAX			0x20000
#define SOLO_EOSD_EXT_AREA(__solo) \
	(SOLO_EOSD_EXT_SIZE(__solo) * 32)
#define SOLO_EOSD_EXT_ADDR_CHAN(__solo, ch) \
	(SOLO_EOSD_EXT_ADDR + SOLO_EOSD_EXT_SIZE(__solo) * (ch))

#define SOLO_MOTION_EXT_ADDR(__solo) \
	(SOLO_EOSD_EXT_ADDR + SOLO_EOSD_EXT_AREA(__solo))
#define SOLO_MOTION_EXT_SIZE			0x00080000

#define SOLO_G723_EXT_ADDR(__solo) \
	(SOLO_MOTION_EXT_ADDR(__solo) + SOLO_MOTION_EXT_SIZE)
#define SOLO_G723_EXT_SIZE			0x00010000

#define SOLO_CAP_EXT_ADDR(__solo) \
	(SOLO_G723_EXT_ADDR(__solo) + SOLO_G723_EXT_SIZE)

/* 18 is the maximum number of pages required for PAL@D1, the largest frame
 * possible */
#define SOLO_CAP_PAGE_SIZE			(18 << 16)

/* Always allow the encoder enough for 16 channels, even if we have less. The
 * exception is if we have card with only 32Megs of memory. */
#define SOLO_CAP_EXT_SIZE(__solo) \
	((((__solo->sdram_size <= (32 << 20)) ? 4 : 16) + 1)	\
	 * SOLO_CAP_PAGE_SIZE)

#define SOLO_EREF_EXT_ADDR(__solo) \
	(SOLO_CAP_EXT_ADDR(__solo) + SOLO_CAP_EXT_SIZE(__solo))
#define SOLO_EREF_EXT_SIZE			0x00140000
#define SOLO_EREF_EXT_AREA(__solo) \
	(SOLO_EREF_EXT_SIZE * __solo->nr_chans * 2)

#define __SOLO_JPEG_MIN_SIZE(__solo)		(__solo->nr_chans * 0x00080000)

#define SOLO_MP4E_EXT_ADDR(__solo) \
	(SOLO_EREF_EXT_ADDR(__solo) + SOLO_EREF_EXT_AREA(__solo))
#define SOLO_MP4E_EXT_SIZE(__solo) \
	clamp(__solo->sdram_size - SOLO_MP4E_EXT_ADDR(__solo) -	\
	      __SOLO_JPEG_MIN_SIZE(__solo),			\
	      __solo->nr_chans * 0x00080000, 0x00ff0000)

#define __SOLO_JPEG_MIN_SIZE(__solo)		(__solo->nr_chans * 0x00080000)
#define SOLO_JPEG_EXT_ADDR(__solo) \
		(SOLO_MP4E_EXT_ADDR(__solo) + SOLO_MP4E_EXT_SIZE(__solo))
#define SOLO_JPEG_EXT_SIZE(__solo) \
	clamp(__solo->sdram_size - SOLO_JPEG_EXT_ADDR(__solo),	\
	      __SOLO_JPEG_MIN_SIZE(__solo), 0x00ff0000)

#define SOLO_SDRAM_END(__solo) \
	(SOLO_JPEG_EXT_ADDR(__solo) + SOLO_JPEG_EXT_SIZE(__solo))

#endif /* __SOLO6X10_OFFSETS_H */
