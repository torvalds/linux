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

#include "../rt_config.h"
#include "netif_block.h"

static NETIF_ENTRY freeNetIfEntryPool[FREE_NETIF_POOL_SIZE];
static LIST_HEADER freeNetIfEntryList;

void initblockQueueTab(
	IN PRTMP_ADAPTER pAd)
{
	int i;

	initList(&freeNetIfEntryList);
	for (i = 0; i < FREE_NETIF_POOL_SIZE; i++)
		insertTailList(&freeNetIfEntryList, (PLIST_ENTRY)&freeNetIfEntryPool[i]);

	for (i=0; i < NUM_OF_TX_RING; i++)
		initList(&pAd->blockQueueTab[i].NetIfList);

	return;
}

BOOLEAN blockNetIf(
	IN PBLOCK_QUEUE_ENTRY pBlockQueueEntry,
	IN PNET_DEV pNetDev)
{
	PNETIF_ENTRY pNetIfEntry = NULL;

	if ((pNetIfEntry = (PNETIF_ENTRY)removeHeadList(&freeNetIfEntryList)) != NULL)
	{
		netif_stop_queue(pNetDev);
		pNetIfEntry->pNetDev = pNetDev;
		insertTailList(&pBlockQueueEntry->NetIfList, (PLIST_ENTRY)pNetIfEntry);

		pBlockQueueEntry->SwTxQueueBlockFlag = TRUE;
		DBGPRINT(RT_DEBUG_TRACE, ("netif_stop_queue(%s)\n", pNetDev->name));
	}
	else
		return FALSE;

	return TRUE;
}

VOID releaseNetIf(
	IN PBLOCK_QUEUE_ENTRY pBlockQueueEntry)
{
	PNETIF_ENTRY pNetIfEntry = NULL;
	PLIST_HEADER pNetIfList = &pBlockQueueEntry->NetIfList;

	while((pNetIfEntry = (PNETIF_ENTRY)removeHeadList(pNetIfList)) !=  NULL)
	{
		PNET_DEV pNetDev = pNetIfEntry->pNetDev;
		netif_wake_queue(pNetDev);
		insertTailList(&freeNetIfEntryList, (PLIST_ENTRY)pNetIfEntry);

		DBGPRINT(RT_DEBUG_TRACE, ("netif_wake_queue(%s)\n", pNetDev->name));
	}
	pBlockQueueEntry->SwTxQueueBlockFlag = FALSE;
	return;
}


VOID StopNetIfQueue(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR QueIdx,
	IN PNDIS_PACKET pPacket)
{
	PNET_DEV NetDev = NULL;
	UCHAR IfIdx = 0;
	BOOLEAN valid = FALSE;

#ifdef APCLI_SUPPORT
	if (RTMP_GET_PACKET_NET_DEVICE(pPacket) >= MIN_NET_DEVICE_FOR_APCLI)
	{
		IfIdx = (RTMP_GET_PACKET_NET_DEVICE(pPacket) - MIN_NET_DEVICE_FOR_APCLI) % MAX_APCLI_NUM;
		NetDev = pAd->ApCfg.ApCliTab[IfIdx].dev;
	}
	else
#endif // APCLI_SUPPORT //
	{
#ifdef MBSS_SUPPORT
		if (pAd->OpMode == OPMODE_AP)
		{
			IfIdx = (RTMP_GET_PACKET_NET_DEVICE(pPacket) - MIN_NET_DEVICE_FOR_MBSSID) % MAX_MBSSID_NUM;
			NetDev = pAd->ApCfg.MBSSID[IfIdx].MSSIDDev;
		}
		else
		{
			IfIdx = MAIN_MBSSID;
			NetDev = pAd->net_dev;
		}
#else
		IfIdx = MAIN_MBSSID;
		NetDev = pAd->net_dev;
#endif
	}

	// WMM support 4 software queues.
	// One software queue full doesn't mean device have no capbility to transmit packet.
	// So disable block Net-If queue function while WMM enable.
#ifdef CONFIG_STA_SUPPORT
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		valid = (pAd->CommonCfg.bWmmCapable == TRUE) ? FALSE : TRUE;
#endif // CONFIG_STA_SUPPORT //

	if (valid)
		blockNetIf(&pAd->blockQueueTab[QueIdx], NetDev);
	return;
}

