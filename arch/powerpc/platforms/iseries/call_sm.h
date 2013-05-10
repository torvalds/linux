/*
 * Copyright (C) 2001  Mike Corrigan IBM Corporation
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#ifndef _ISERIES_CALL_SM_H
#define _ISERIES_CALL_SM_H

/*
 * This file contains the "hypervisor call" interface which is used to
 * drive the hypervisor from the OS.
 */

#include <asm/iseries/hv_call_sc.h>
#include <asm/iseries/hv_types.h>

#define HvCallSmGet64BitsOfAccessMap	HvCallSm  + 11

static inline u64 HvCallSm_get64BitsOfAccessMap(HvLpIndex lpIndex,
		u64 indexIntoBitMap)
{
	return HvCall2(HvCallSmGet64BitsOfAccessMap, lpIndex, indexIntoBitMap);
}

#endif /* _ISERIES_CALL_SM_H */
