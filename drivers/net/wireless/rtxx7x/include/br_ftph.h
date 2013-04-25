/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#ifndef __BR_FTPH_H__
#define __BR_FTPH_H__

/* Public function prototype */
/*
========================================================================
Routine Description:
	Init bridge fast path module.

Arguments:
	None

Return Value:
	None

Note:
	Used in module init.
========================================================================
*/
VOID BG_FTPH_Init(VOID);

/*
========================================================================
Routine Description:
	Remove bridge fast path module.

Arguments:
	None

Return Value:
	None

Note:
	Used in module remove.
========================================================================
*/
VOID BG_FTPH_Remove(VOID);

/*
========================================================================
Routine Description:
	Forward the received packet.

Arguments:
	pPacket			- the received packet

Return Value:
	None

Note:
========================================================================
*/
UINT32 BG_FTPH_PacketFromApHandle(
	IN		PNDIS_PACKET	pPacket);

#endif /* __BR_FTPH_H__ */

/* End of br_ftph.h */

