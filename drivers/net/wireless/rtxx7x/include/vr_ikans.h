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


#ifndef __VR_IKANS_H__
#define __VR_IKANS_H__

#ifndef MODULE_IKANOS
#define IKANOS_EXTERN	extern
#else
#define IKANOS_EXTERN
#endif /* MODULE_IKANOS */

#ifdef IKANOS_VX_1X0
	typedef void (*IkanosWlanTxCbFuncP)(void *, void *);

	struct IKANOS_TX_INFO
	{
		struct net_device *netdev;
		IkanosWlanTxCbFuncP *fp;
	};
#endif /* IKANOS_VX_1X0 */


IKANOS_EXTERN void VR_IKANOS_FP_Init(UINT8 BssNum, UINT8 *pApMac);

IKANOS_EXTERN INT32 IKANOS_DataFramesTx(struct sk_buff *pSkb,
										struct net_device *pNetDev);

IKANOS_EXTERN void IKANOS_DataFrameRx(PRTMP_ADAPTER pAd,
										struct sk_buff *pSkb);

#endif /* __VR_IKANS_H__ */

/* End of vr_ikans.h */
