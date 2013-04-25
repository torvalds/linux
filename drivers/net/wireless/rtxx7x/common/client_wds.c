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


#ifdef CLIENT_WDS

#include "rt_config.h"

VOID CliWds_ProxyTabInit(
	IN PRTMP_ADAPTER pAd)
{
	INT idx;
	ULONG i;

	NdisAllocateSpinLock(pAd, &pAd->ApCfg.CliWdsTabLock);

/*	pAd->ApCfg.pCliWdsEntryPool = kmalloc(sizeof(CLIWDS_PROXY_ENTRY) * CLIWDS_POOL_SIZE, GFP_ATOMIC);*/
	os_alloc_mem(pAd, (UCHAR **)&(pAd->ApCfg.pCliWdsEntryPool), sizeof(CLIWDS_PROXY_ENTRY) * CLIWDS_POOL_SIZE);
	if (pAd->ApCfg.pCliWdsEntryPool)
	{
		NdisZeroMemory(pAd->ApCfg.pCliWdsEntryPool, sizeof(CLIWDS_PROXY_ENTRY) * CLIWDS_POOL_SIZE);
		initList(&pAd->ApCfg.CliWdsEntryFreeList);
		for (i = 0; i < CLIWDS_POOL_SIZE; i++)
			insertTailList(&pAd->ApCfg.CliWdsEntryFreeList, (PLIST_ENTRY)(pAd->ApCfg.pCliWdsEntryPool + (ULONG)i));
	}
	else
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s Fail to alloc memory for pAd->CommonCfg.pCliWdsEntryPool", __FUNCTION__));
	}

	for (idx = 0; idx < CLIWDS_HASH_TAB_SIZE; idx++)
		initList(&pAd->ApCfg.CliWdsProxyTab[idx]);

	return;
}


VOID CliWds_ProxyTabDestory(
	IN PRTMP_ADAPTER pAd)
{
	INT idx;
	PCLIWDS_PROXY_ENTRY pCliWdsEntry;

	NdisFreeSpinLock(&pAd->ApCfg.CliWdsTabLock);

	for (idx = 0; idx < CLIWDS_HASH_TAB_SIZE; idx++)
	{
		pCliWdsEntry =
			(PCLIWDS_PROXY_ENTRY)pAd->ApCfg.CliWdsProxyTab[idx].pHead;
		while(pCliWdsEntry)
		{
			PCLIWDS_PROXY_ENTRY pCliWdsEntryNext = pCliWdsEntry->pNext;
			CliWdsEntyFree(pAd, pCliWdsEntry);
			pCliWdsEntry = pCliWdsEntryNext;
		}
	}

	if (pAd->ApCfg.pCliWdsEntryPool)
/*		kfree(pAd->ApCfg.pCliWdsEntryPool);*/
		os_free_mem(NULL, pAd->ApCfg.pCliWdsEntryPool);
	pAd->ApCfg.pCliWdsEntryPool = NULL;	

	return;
}


PCLIWDS_PROXY_ENTRY CliWdsEntyAlloc(
	IN PRTMP_ADAPTER pAd)
{
	PCLIWDS_PROXY_ENTRY pCliWdsEntry;

	RTMP_SEM_LOCK(&pAd->ApCfg.CliWdsTabLock);

	pCliWdsEntry = (PCLIWDS_PROXY_ENTRY)removeHeadList(&pAd->ApCfg.CliWdsEntryFreeList);

	RTMP_SEM_UNLOCK(&pAd->ApCfg.CliWdsTabLock);

	return pCliWdsEntry;
}


VOID CliWdsEntyFree(
	IN PRTMP_ADAPTER pAd,
	IN PCLIWDS_PROXY_ENTRY pCliWdsEntry)
{
	RTMP_SEM_LOCK(&pAd->ApCfg.CliWdsTabLock);

	insertTailList(&pAd->ApCfg.CliWdsEntryFreeList, (PLIST_ENTRY)pCliWdsEntry);

	RTMP_SEM_UNLOCK(&pAd->ApCfg.CliWdsTabLock);

	return;
}


PUCHAR CliWds_ProxyLookup(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pMac)
{
	UINT8 HashId = (*(pMac + 5) & (CLIWDS_HASH_TAB_SIZE - 1));
	PCLIWDS_PROXY_ENTRY pCliWdsEntry;

	pCliWdsEntry =
		(PCLIWDS_PROXY_ENTRY)pAd->ApCfg.CliWdsProxyTab[HashId].pHead;
	while (pCliWdsEntry)
	{
		if (MAC_ADDR_EQUAL(pMac, pCliWdsEntry->Addr))
		{
			ULONG Now;
			NdisGetSystemUpTime(&Now);

			pCliWdsEntry->LastRefTime = Now;
			if (VALID_WCID(pCliWdsEntry->Aid))
				return pAd->MacTab.Content[pCliWdsEntry->Aid].Addr;
			else
				return NULL;
		}
		pCliWdsEntry = pCliWdsEntry->pNext;
	}
	return NULL;
}


VOID CliWds_ProxyTabUpdate(
	IN PRTMP_ADAPTER pAd,
	IN SHORT Aid,
	IN PUCHAR pMac)
{
	UINT8 HashId = (*(pMac + 5) & (CLIWDS_HASH_TAB_SIZE - 1));
	PCLIWDS_PROXY_ENTRY pCliWdsEntry;

	if (CliWds_ProxyLookup(pAd, pMac) != NULL)
		return;

	pCliWdsEntry = CliWdsEntyAlloc(pAd);
	if (pCliWdsEntry)
	{
		ULONG Now;
		NdisGetSystemUpTime(&Now);

		pCliWdsEntry->Aid = Aid;
		COPY_MAC_ADDR(&pCliWdsEntry->Addr, pMac);
		pCliWdsEntry->LastRefTime = Now;
		pCliWdsEntry->pNext = NULL;
		insertTailList(&pAd->ApCfg.CliWdsProxyTab[HashId], (PLIST_ENTRY)pCliWdsEntry);
	}
	return;
}


VOID CliWds_ProxyTabMaintain(
	IN PRTMP_ADAPTER pAd)
{
	ULONG idx;
	PCLIWDS_PROXY_ENTRY pCliWdsEntry;
	ULONG Now;

	NdisGetSystemUpTime(&Now);
	for (idx = 0; idx < CLIWDS_HASH_TAB_SIZE; idx++)
	{
		pCliWdsEntry = (PCLIWDS_PROXY_ENTRY)(pAd->ApCfg.CliWdsProxyTab[idx].pHead);
		while(pCliWdsEntry)
		{
			PCLIWDS_PROXY_ENTRY pCliWdsEntryNext = pCliWdsEntry->pNext;
			if (RTMP_TIME_AFTER(Now, pCliWdsEntry->LastRefTime + (CLI_WDS_ENTRY_AGEOUT * OS_HZ / 1000)))
			{
				delEntryList(&pAd->ApCfg.CliWdsProxyTab[idx], (PLIST_ENTRY)pCliWdsEntry);
				CliWdsEntyFree(pAd, pCliWdsEntry);
			}
			pCliWdsEntry = pCliWdsEntryNext;
		}
	}
	return;
}

#endif /* CLIENT_WDS */

