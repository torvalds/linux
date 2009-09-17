/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
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
 *************************************************************************

	Module Name:
	aironet.h

	Abstract:

	Revision History:
	Who			When			What
	--------	----------		----------------------------------------------
	Name		Date			Modification logs
	Paul Lin	04-06-15		Initial
*/

#ifndef	__ACTION_H__
#define	__ACTION_H__

typedef struct PACKED __HT_INFO_OCTET
{
#ifdef RT_BIG_ENDIAN
	UCHAR	Reserved:5;
	UCHAR	STA_Channel_Width:1;
	UCHAR	Forty_MHz_Intolerant:1;
	UCHAR	Request:1;
#else
	UCHAR	Request:1;
	UCHAR	Forty_MHz_Intolerant:1;
	UCHAR	STA_Channel_Width:1;
	UCHAR	Reserved:5;
#endif
} HT_INFORMATION_OCTET;


typedef struct PACKED __FRAME_HT_INFO
{
	HEADER_802_11			Hdr;
	UCHAR					Category;
	UCHAR					Action;
	HT_INFORMATION_OCTET	HT_Info;
}   FRAME_HT_INFO, *PFRAME_HT_INFO;

#endif /* __ACTION_H__ */
