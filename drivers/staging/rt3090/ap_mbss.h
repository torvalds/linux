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
    ap_mbss.h

    Abstract:
    Miniport generic portion header file

    Revision History:
    Who         When          What
    --------    ----------    ----------------------------------------------
*/

#ifndef MODULE_MBSS

#define MBSS_EXTERN    extern

#else

#define MBSS_EXTERN

#endif // MODULE_MBSS //


/* Public function list */
MBSS_EXTERN VOID RT28xx_MBSS_Init(
	IN PRTMP_ADAPTER ad_p,
	IN PNET_DEV main_dev_p);

MBSS_EXTERN VOID RT28xx_MBSS_Close(
	IN PRTMP_ADAPTER ad_p);

MBSS_EXTERN VOID RT28xx_MBSS_Remove(
	IN PRTMP_ADAPTER ad_p);

INT MBSS_VirtualIF_Open(
	IN	PNET_DEV			dev_p);
INT MBSS_VirtualIF_Close(
	IN	PNET_DEV			dev_p);
INT MBSS_VirtualIF_PacketSend(
	IN PNDIS_PACKET			skb_p,
	IN PNET_DEV				dev_p);
INT MBSS_VirtualIF_Ioctl(
	IN PNET_DEV				dev_p,
	IN OUT struct ifreq	*rq_p,
	IN INT cmd);

/* End of ap_mbss.h */
