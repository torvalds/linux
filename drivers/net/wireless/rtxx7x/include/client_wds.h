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


#ifndef __CLIENT_WDS_H__
#define __CLIENT_WDS_H__

#include "client_wds_cmm.h"

VOID CliWds_ProxyTabInit(
	IN PRTMP_ADAPTER pAd);

VOID CliWds_ProxyTabDestory(
	IN PRTMP_ADAPTER pAd);

PCLIWDS_PROXY_ENTRY CliWdsEntyAlloc(
	IN PRTMP_ADAPTER pAd);


VOID CliWdsEntyFree(
	IN PRTMP_ADAPTER pAd,
	IN PCLIWDS_PROXY_ENTRY pCliWdsEntry);


PUCHAR CliWds_ProxyLookup(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pMac);


VOID CliWds_ProxyTabUpdate(
	IN PRTMP_ADAPTER pAd,
	IN SHORT Aid,
	IN PUCHAR pMac);


VOID CliWds_ProxyTabMaintain(
	IN PRTMP_ADAPTER pAd);

#endif /* __CLIENT_WDS_H__ */

