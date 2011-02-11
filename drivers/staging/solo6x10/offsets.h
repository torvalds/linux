/*
 * Copyright (C) 2010 Bluecherry, LLC www.bluecherrydvr.com
 * Copyright (C) 2010 Ben Collins <bcollins@bluecherry.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef __SOLO6X10_OFFSETS_H
#define __SOLO6X10_OFFSETS_H

/* Offsets and sizes of the external address */
#define SOLO_DISP_EXT_ADDR			0x00000000
#define SOLO_DISP_EXT_SIZE			0x00480000

#define SOLO_DEC2LIVE_EXT_ADDR (SOLO_DISP_EXT_ADDR + SOLO_DISP_EXT_SIZE)
#define SOLO_DEC2LIVE_EXT_SIZE			0x00240000

#define SOLO_OSG_EXT_ADDR (SOLO_DEC2LIVE_EXT_ADDR + SOLO_DEC2LIVE_EXT_SIZE)
#define SOLO_OSG_EXT_SIZE			0x00120000

#define SOLO_EOSD_EXT_ADDR (SOLO_OSG_EXT_ADDR + SOLO_OSG_EXT_SIZE)
#define SOLO_EOSD_EXT_SIZE			0x00010000

#define SOLO_MOTION_EXT_ADDR(__solo) (SOLO_EOSD_EXT_ADDR +	\
				      (SOLO_EOSD_EXT_SIZE * __solo->nr_chans))
#define SOLO_MOTION_EXT_SIZE			0x00080000

#define SOLO_G723_EXT_ADDR(__solo) \
		(SOLO_MOTION_EXT_ADDR(__solo) + SOLO_MOTION_EXT_SIZE)
#define SOLO_G723_EXT_SIZE			0x00010000

#define SOLO_CAP_EXT_ADDR(__solo) \
		(SOLO_G723_EXT_ADDR(__solo) + SOLO_G723_EXT_SIZE)
#define SOLO_CAP_EXT_MAX_PAGE			(18 + 15)
#define SOLO_CAP_EXT_SIZE			(SOLO_CAP_EXT_MAX_PAGE * 65536)

/* This +1 is very important -- Why?! -- BenC */
#define SOLO_EREF_EXT_ADDR(__solo) \
		(SOLO_CAP_EXT_ADDR(__solo) + \
		 (SOLO_CAP_EXT_SIZE * (__solo->nr_chans + 1)))
#define SOLO_EREF_EXT_SIZE			0x00140000

#define SOLO_MP4E_EXT_ADDR(__solo) \
		(SOLO_EREF_EXT_ADDR(__solo) + \
		 (SOLO_EREF_EXT_SIZE * __solo->nr_chans))
#define SOLO_MP4E_EXT_SIZE(__solo)		(0x00080000 * __solo->nr_chans)

#define SOLO_DREF_EXT_ADDR(__solo) \
		(SOLO_MP4E_EXT_ADDR(__solo) + SOLO_MP4E_EXT_SIZE(__solo))
#define SOLO_DREF_EXT_SIZE			0x00140000

#define SOLO_MP4D_EXT_ADDR(__solo) \
		(SOLO_DREF_EXT_ADDR(__solo) + \
		 (SOLO_DREF_EXT_SIZE * __solo->nr_chans))
#define SOLO_MP4D_EXT_SIZE			0x00080000

#define SOLO_JPEG_EXT_ADDR(__solo) \
		(SOLO_MP4D_EXT_ADDR(__solo) + \
		 (SOLO_MP4D_EXT_SIZE * __solo->nr_chans))
#define SOLO_JPEG_EXT_SIZE(__solo)		(0x00080000 * __solo->nr_chans)

#endif /* __SOLO6X10_OFFSETS_H */
