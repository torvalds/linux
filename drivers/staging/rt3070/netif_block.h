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
 */

#ifndef __NET_IF_BLOCK_H__
#define __NET_IF_BLOCK_H__

//#include <linux/device.h>
#include "link_list.h"
#include "rtmp.h"

#define FREE_NETIF_POOL_SIZE 32

typedef struct _NETIF_ENTRY
{
	struct _NETIF_ENTRY *pNext;
	PNET_DEV pNetDev;
} NETIF_ENTRY, *PNETIF_ENTRY;

void initblockQueueTab(
	IN PRTMP_ADAPTER pAd);

BOOLEAN blockNetIf(
	IN PBLOCK_QUEUE_ENTRY pBlockQueueEntry,
	IN PNET_DEV pNetDev);

VOID releaseNetIf(
	IN PBLOCK_QUEUE_ENTRY pBlockQueueEntry);

VOID StopNetIfQueue(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR QueIdx,
	IN PNDIS_PACKET pPacket);
#endif // __NET_IF_BLOCK_H__

