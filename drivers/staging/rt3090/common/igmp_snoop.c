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


#ifdef IGMP_SNOOP_SUPPORT

#include "../rt_config.h"
#include "../ipv6.h"
#include "../igmp_snoop.h"


static inline void initFreeEntryList(
	IN PMULTICAST_FILTER_TABLE pMulticastFilterTable,
	IN PLIST_HEADER pList)
{
	int i;

	for (i = 0; i < FREE_MEMBER_POOL_SIZE; i++)
		insertTailList(pList, (PLIST_ENTRY)&(pMulticastFilterTable->freeMemberPool[i]));

	return;
}

static inline PMEMBER_ENTRY AllocaGrpMemberEntry(
	IN PMULTICAST_FILTER_TABLE pMulticastFilterTable)
{
	PMEMBER_ENTRY pMemberEntry;

	RTMP_SEM_LOCK(&pMulticastFilterTable->FreeMemberPoolTabLock);

	pMemberEntry = (PMEMBER_ENTRY)removeHeadList(&pMulticastFilterTable->freeEntryList);

	RTMP_SEM_UNLOCK(&pMulticastFilterTable->FreeMemberPoolTabLock);

	return (PMEMBER_ENTRY)pMemberEntry;
}

static inline VOID FreeGrpMemberEntry(
	IN PMULTICAST_FILTER_TABLE pMulticastFilterTable,
	IN PMEMBER_ENTRY pEntry)
{
	RTMP_SEM_LOCK(&pMulticastFilterTable->FreeMemberPoolTabLock);

	insertTailList(&pMulticastFilterTable->freeEntryList, (PLIST_ENTRY)pEntry);

	RTMP_SEM_UNLOCK(&pMulticastFilterTable->FreeMemberPoolTabLock);
}

static VOID IGMPTableDisplay(
	IN PRTMP_ADAPTER pAd);

static BOOLEAN isIgmpMacAddr(
	IN PUCHAR pMacAddr);

static VOID InsertIgmpMember(
	IN PMULTICAST_FILTER_TABLE pMulticastFilterTable,
	IN PLIST_HEADER pList,
	IN PUCHAR pMemberAddr);

static VOID DeleteIgmpMember(
	IN PMULTICAST_FILTER_TABLE pMulticastFilterTable,
	IN PLIST_HEADER pList,
	IN PUCHAR pMemberAddr);

static VOID DeleteIgmpMemberList(
	IN PMULTICAST_FILTER_TABLE pMulticastFilterTable,
	IN PLIST_HEADER pList);


/*
    ==========================================================================
    Description:
        This routine init the entire IGMP table.
    ==========================================================================
 */
VOID MulticastFilterTableInit(
	IN PMULTICAST_FILTER_TABLE *ppMulticastFilterTable)
{
	// Initialize MAC table and allocate spin lock
	*ppMulticastFilterTable = kmalloc(sizeof(MULTICAST_FILTER_TABLE), MEM_ALLOC_FLAG);
	if (*ppMulticastFilterTable == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s unable to alloc memory for Multicase filter table, size=%d\n",
			__FUNCTION__, sizeof(MULTICAST_FILTER_TABLE)));
		return;
	}

	NdisZeroMemory(*ppMulticastFilterTable, sizeof(MULTICAST_FILTER_TABLE));
	NdisAllocateSpinLock(&((*ppMulticastFilterTable)->MulticastFilterTabLock));

	NdisAllocateSpinLock(&((*ppMulticastFilterTable)->FreeMemberPoolTabLock));
	initList(&((*ppMulticastFilterTable)->freeEntryList));
	initFreeEntryList(*ppMulticastFilterTable, &((*ppMulticastFilterTable)->freeEntryList));
	return;
}

/*
    ==========================================================================
    Description:
        This routine reset the entire IGMP table.
    ==========================================================================
 */
VOID MultiCastFilterTableReset(
	IN PMULTICAST_FILTER_TABLE *ppMulticastFilterTable)
{
	if(*ppMulticastFilterTable == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s Multicase filter table is not ready.\n", __FUNCTION__));
		return;
	}

	NdisFreeSpinLock(&((*ppMulticastFilterTable)->FreeMemberPoolTabLock));
	NdisFreeSpinLock(&((*ppMulticastFilterTable)->MulticastFilterTabLock));
	kfree(*ppMulticastFilterTable);
	*ppMulticastFilterTable = NULL;
}

/*
    ==========================================================================
    Description:
        Display all entrys in IGMP table
    ==========================================================================
 */
static VOID IGMPTableDisplay(
	IN PRTMP_ADAPTER pAd)
{
	int i;
	MULTICAST_FILTER_TABLE_ENTRY *pEntry = NULL;
	PMULTICAST_FILTER_TABLE pMulticastFilterTable = pAd->pMulticastFilterTable;

	if (pMulticastFilterTable == NULL)
	{
		DBGPRINT(RT_DEBUG_OFF, ("%s Multicase filter table is not ready.\n", __FUNCTION__));
		return;
	}

	// if FULL, return
	if (pMulticastFilterTable->Size == 0)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("Table empty.\n"));
		return;
	}

	// allocate one MAC entry
	RTMP_SEM_LOCK(&pMulticastFilterTable->MulticastFilterTabLock);

	for (i = 0; i< MAX_LEN_OF_MULTICAST_FILTER_TABLE; i++)
	{
		// pick up the first available vacancy
		if (pMulticastFilterTable->Content[i].Valid == TRUE)
		{
			PMEMBER_ENTRY pMemberEntry = NULL;
			pEntry = &pMulticastFilterTable->Content[i];

			DBGPRINT(RT_DEBUG_OFF, ("IF(%s) entry #%d, type=%s, GrpId=(%02x:%02x:%02x:%02x:%02x:%02x) memberCnt=%d\n",
				RTMP_OS_NETDEV_GET_DEVNAME(pEntry->net_dev), i, (pEntry->type==0 ? "static":"dynamic"),
				PRINT_MAC(pEntry->Addr), IgmpMemberCnt(&pEntry->MemberList)));

			pMemberEntry = (PMEMBER_ENTRY)pEntry->MemberList.pHead;
			while (pMemberEntry)
			{
				DBGPRINT(RT_DEBUG_OFF, ("member mac=(%02x:%02x:%02x:%02x:%02x:%02x)\n",
										PRINT_MAC(pMemberEntry->Addr)));

				pMemberEntry = pMemberEntry->pNext;
			}
		}
	}

	RTMP_SEM_UNLOCK(&pMulticastFilterTable->MulticastFilterTabLock);

	return;
}

/*
    ==========================================================================
    Description:
        Add and new entry into MAC table
    ==========================================================================
 */
BOOLEAN MulticastFilterTableInsertEntry(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pGrpId,
	IN PUCHAR pMemberAddr,
	IN PNET_DEV dev,
	IN MulticastFilterEntryType type)
{
	UCHAR HashIdx;
	int i;
	MULTICAST_FILTER_TABLE_ENTRY *pEntry = NULL, *pCurrEntry, *pPrevEntry;
	PMEMBER_ENTRY pMemberEntry;
	PMULTICAST_FILTER_TABLE pMulticastFilterTable = pAd->pMulticastFilterTable;

	if (pMulticastFilterTable == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s Multicase filter table is not ready.\n", __FUNCTION__));
		return FALSE;
	}

	// if FULL, return
	if (pMulticastFilterTable->Size >= MAX_LEN_OF_MULTICAST_FILTER_TABLE)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s Multicase filter table full. max-entries = %d\n",
			__FUNCTION__, MAX_LEN_OF_MULTICAST_FILTER_TABLE));
		return FALSE;
	}

	// check the rule is in table already or not.
	if ((pEntry = MulticastFilterTableLookup(pMulticastFilterTable, pGrpId, dev)))
	{
		// doesn't indicate member mac address.
		if(pMemberAddr == NULL)
		{
			return FALSE;
		}

		pMemberEntry = (PMEMBER_ENTRY)pEntry->MemberList.pHead;

		while (pMemberEntry)
		{
			if (MAC_ADDR_EQUAL(pMemberAddr, pMemberEntry->Addr))
			{
				DBGPRINT(RT_DEBUG_ERROR, ("%s: already in Members list.\n", __FUNCTION__));
				return FALSE;
			}

			pMemberEntry = pMemberEntry->pNext;
		}
	}

	RTMP_SEM_LOCK(&pMulticastFilterTable->MulticastFilterTabLock);
	do
	{
		ULONG Now;
		// the multicast entry already exist but doesn't include the member yet.
		if (pEntry != NULL && pMemberAddr != NULL)
		{
			InsertIgmpMember(pMulticastFilterTable, &pEntry->MemberList, pMemberAddr);
			break;
		}

		// allocate one MAC entry
		for (i = 0; i < MAX_LEN_OF_MULTICAST_FILTER_TABLE; i++)
		{
			// pick up the first available vacancy
			pEntry = &pMulticastFilterTable->Content[i];
			NdisGetSystemUpTime(&Now);
			if ((pEntry->Valid == TRUE) && (pEntry->type == MCAT_FILTER_DYNAMIC)
				&& ((Now - pEntry->lastTime) > IGMPMAC_TB_ENTRY_AGEOUT_TIME))
			{
				PMULTICAST_FILTER_TABLE_ENTRY pHashEntry;

				HashIdx = MULTICAST_ADDR_HASH_INDEX(pEntry->Addr);
				pHashEntry = pMulticastFilterTable->Hash[HashIdx];

				if ((pEntry->net_dev == pHashEntry->net_dev)
					&& MAC_ADDR_EQUAL(pEntry->Addr, pHashEntry->Addr))
				{
					pMulticastFilterTable->Hash[HashIdx] = pHashEntry->pNext;
					pMulticastFilterTable->Size --;
					DBGPRINT(RT_DEBUG_TRACE, ("MCastFilterTableDeleteEntry 1 - Total= %d\n", pMulticastFilterTable->Size));
				} else
				{
					while (pHashEntry->pNext)
					{
						pPrevEntry = pHashEntry;
						pHashEntry = pHashEntry->pNext;
						if ((pEntry->net_dev == pHashEntry->net_dev)
							&& MAC_ADDR_EQUAL(pEntry->Addr, pHashEntry->Addr))
						{
							pPrevEntry->pNext = pHashEntry->pNext;
							pMulticastFilterTable->Size --;
							DBGPRINT(RT_DEBUG_TRACE, ("MCastFilterTableDeleteEntry 2 - Total= %d\n", pMulticastFilterTable->Size));
							break;
						}
					}
				}
				pEntry->Valid = FALSE;
				DeleteIgmpMemberList(pMulticastFilterTable, &pEntry->MemberList);
			}

			if (pEntry->Valid == FALSE)
			{
				NdisZeroMemory(pEntry, sizeof(MULTICAST_FILTER_TABLE_ENTRY));
				pEntry->Valid = TRUE;

				COPY_MAC_ADDR(pEntry->Addr, pGrpId);
				pEntry->net_dev = dev;
				NdisGetSystemUpTime(&Now);
				pEntry->lastTime = Now;
				pEntry->type = type;
				initList(&pEntry->MemberList);
				if (pMemberAddr != NULL)
					InsertIgmpMember(pMulticastFilterTable, &pEntry->MemberList, pMemberAddr);

				pMulticastFilterTable->Size ++;

				DBGPRINT(RT_DEBUG_TRACE, ("MulticastFilterTableInsertEntry -IF(%s) allocate entry #%d, Total= %d\n", RTMP_OS_NETDEV_GET_DEVNAME(dev), i, pMulticastFilterTable->Size));
				break;
			}
		}

		// add this MAC entry into HASH table
		if (pEntry)
		{
			HashIdx = MULTICAST_ADDR_HASH_INDEX(pGrpId);
			if (pMulticastFilterTable->Hash[HashIdx] == NULL)
			{
				pMulticastFilterTable->Hash[HashIdx] = pEntry;
			} else
			{
				pCurrEntry = pMulticastFilterTable->Hash[HashIdx];
				while (pCurrEntry->pNext != NULL)
					pCurrEntry = pCurrEntry->pNext;
				pCurrEntry->pNext = pEntry;
			}
		}
	}while(FALSE);

	RTMP_SEM_UNLOCK(&pMulticastFilterTable->MulticastFilterTabLock);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Delete a specified client from MAC table
    ==========================================================================
 */
BOOLEAN MulticastFilterTableDeleteEntry(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pGrpId,
	IN PUCHAR pMemberAddr,
	IN PNET_DEV dev)
{
	USHORT HashIdx;
	MULTICAST_FILTER_TABLE_ENTRY *pEntry, *pPrevEntry;
	PMULTICAST_FILTER_TABLE pMulticastFilterTable = pAd->pMulticastFilterTable;
	USHORT Aid = MCAST_WCID;
	SST	Sst = SST_ASSOC;
	UCHAR PsMode = PWR_ACTIVE, Rate;

	if (pMulticastFilterTable == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s Multicase filter table is not ready.\n", __FUNCTION__));
		return FALSE;
	}

	RTMP_SEM_LOCK(&pMulticastFilterTable->MulticastFilterTabLock);

	do
	{
		HashIdx = MULTICAST_ADDR_HASH_INDEX(pGrpId);
		pPrevEntry = pEntry = pMulticastFilterTable->Hash[HashIdx];

		while (pEntry && pEntry->Valid)
		{
			if ((pEntry->net_dev ==  dev)
				&& MAC_ADDR_EQUAL(pEntry->Addr, pGrpId))
				break;
			else
			{
				pPrevEntry = pEntry;
				pEntry = pEntry->pNext;
			}
		}

		// check the rule is in table already or not.
		if (pEntry && (pMemberAddr != NULL))
		{
			if(APSsPsInquiry(pAd, pMemberAddr, &Sst, &Aid, &PsMode, &Rate))
				DeleteIgmpMember(pMulticastFilterTable, &pEntry->MemberList, pMemberAddr);
			if (IgmpMemberCnt(&pEntry->MemberList) > 0)
				break;
		}

		if (pEntry)
		{
			if (pEntry == pMulticastFilterTable->Hash[HashIdx])
			{
				pMulticastFilterTable->Hash[HashIdx] = pEntry->pNext;
				DeleteIgmpMemberList(pMulticastFilterTable, &pEntry->MemberList);
				NdisZeroMemory(pEntry, sizeof(MULTICAST_FILTER_TABLE_ENTRY));
				pMulticastFilterTable->Size --;
				DBGPRINT(RT_DEBUG_TRACE, ("MCastFilterTableDeleteEntry 1 - Total= %d\n", pMulticastFilterTable->Size));
			}
			else
			{
				pPrevEntry->pNext = pEntry->pNext;
				DeleteIgmpMemberList(pMulticastFilterTable, &pEntry->MemberList);
				NdisZeroMemory(pEntry, sizeof(MULTICAST_FILTER_TABLE_ENTRY));
				pMulticastFilterTable->Size --;
				DBGPRINT(RT_DEBUG_TRACE, ("MCastFilterTableDeleteEntry 2 - Total= %d\n", pMulticastFilterTable->Size));
			}
		}
		else
		{
			DBGPRINT(RT_DEBUG_ERROR, ("%s: the Group doesn't exist.\n", __FUNCTION__));
		}
	} while(FALSE);

	RTMP_SEM_UNLOCK(&pMulticastFilterTable->MulticastFilterTabLock);

	return TRUE;
}

/*
    ==========================================================================
    Description:
        Look up the MAC address in the IGMP table. Return NULL if not found.
    Return:
        pEntry - pointer to the MAC entry; NULL is not found
    ==========================================================================
*/
PMULTICAST_FILTER_TABLE_ENTRY MulticastFilterTableLookup(
	IN PMULTICAST_FILTER_TABLE pMulticastFilterTable,
	IN PUCHAR pAddr,
	IN PNET_DEV dev)
{
	ULONG HashIdx, Now;
	PMULTICAST_FILTER_TABLE_ENTRY pEntry = NULL, pPrev = NULL;

	if (pMulticastFilterTable == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s Multicase filter table is not ready.\n", __FUNCTION__));
		return NULL;
	}

	RTMP_SEM_LOCK(&pMulticastFilterTable->MulticastFilterTabLock);

	HashIdx = MULTICAST_ADDR_HASH_INDEX(pAddr);
	pEntry = pPrev = pMulticastFilterTable->Hash[HashIdx];

	while (pEntry && pEntry->Valid)
	{
		if ((pEntry->net_dev ==  dev)
			&& MAC_ADDR_EQUAL(pEntry->Addr, pAddr))
		{
			NdisGetSystemUpTime(&Now);
			pEntry->lastTime = Now;
			break;
		}
		else
		{
			NdisGetSystemUpTime(&Now);
			if ((pEntry->Valid == TRUE) && (pEntry->type == MCAT_FILTER_DYNAMIC)
				&& RTMP_TIME_AFTER(Now, pEntry->lastTime+IGMPMAC_TB_ENTRY_AGEOUT_TIME))
			{
				// Remove the aged entry
				if (pEntry == pMulticastFilterTable->Hash[HashIdx])
				{
					pMulticastFilterTable->Hash[HashIdx] = pEntry->pNext;
					pPrev = pMulticastFilterTable->Hash[HashIdx];
					DeleteIgmpMemberList(pMulticastFilterTable, &pEntry->MemberList);
					NdisZeroMemory(pEntry, sizeof(MULTICAST_FILTER_TABLE_ENTRY));
					pMulticastFilterTable->Size --;
					pEntry = pPrev;
					DBGPRINT(RT_DEBUG_TRACE, ("MCastFilterTableDeleteEntry 2 - Total= %d\n", pMulticastFilterTable->Size));
				}
				else
				{
					pPrev->pNext = pEntry->pNext;
					DeleteIgmpMemberList(pMulticastFilterTable, &pEntry->MemberList);
					NdisZeroMemory(pEntry, sizeof(MULTICAST_FILTER_TABLE_ENTRY));
					pMulticastFilterTable->Size --;
					pEntry = (pPrev == NULL ? NULL: pPrev->pNext);
					DBGPRINT(RT_DEBUG_TRACE, ("MCastFilterTableDeleteEntry 2 - Total= %d\n", pMulticastFilterTable->Size));
				}
			}
			else
			{
				pPrev = pEntry;
				pEntry = pEntry->pNext;
			}
		}
	}

	RTMP_SEM_UNLOCK(&pMulticastFilterTable->MulticastFilterTabLock);

	return pEntry;
}

VOID IGMPSnooping(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pDstMacAddr,
	IN PUCHAR pSrcMacAddr,
	IN PUCHAR pIpHeader,
	IN PNET_DEV pDev)
{
	INT i;
	INT IpHeaderLen;
	UCHAR GroupType;
	UINT16 numOfGroup;
	UCHAR IgmpVerType;
	PUCHAR pIgmpHeader;
	PUCHAR pGroup;
	UCHAR AuxDataLen;
	UINT16 numOfSources;
	PUCHAR pGroupIpAddr;
	UCHAR GroupMacAddr[6];
	PUCHAR pGroupMacAddr = (PUCHAR)&GroupMacAddr;

	if(isIgmpPkt(pDstMacAddr, pIpHeader))
	{
		IpHeaderLen = (*(pIpHeader + 2) & 0x0f) * 4;
		pIgmpHeader = pIpHeader + 2 + IpHeaderLen;
		IgmpVerType = (UCHAR)(*(pIgmpHeader));

		DBGPRINT(RT_DEBUG_TRACE, ("IGMP type=%0x\n", IgmpVerType));

		switch(IgmpVerType)
		{
		case IGMP_V1_MEMBERSHIP_REPORT: // IGMP version 1 membership report.
		case IGMP_V2_MEMBERSHIP_REPORT: // IGMP version 2 membership report.
			pGroupIpAddr = (PUCHAR)(pIgmpHeader + 4);
				ConvertMulticastIP2MAC(pGroupIpAddr, (PUCHAR *)&pGroupMacAddr, ETH_P_IP);
			DBGPRINT(RT_DEBUG_TRACE, ("IGMP Group=%02x:%02x:%02x:%02x:%02x:%02x\n",
				GroupMacAddr[0], GroupMacAddr[1], GroupMacAddr[2], GroupMacAddr[3], GroupMacAddr[4], GroupMacAddr[5]));
			MulticastFilterTableInsertEntry(pAd, GroupMacAddr, pSrcMacAddr, pDev, MCAT_FILTER_DYNAMIC);
			break;

		case IGMP_LEAVE_GROUP: // IGMP version 1 and version 2 leave group.
			pGroupIpAddr = (PUCHAR)(pIgmpHeader + 4);
				ConvertMulticastIP2MAC(pGroupIpAddr, (PUCHAR *)&pGroupMacAddr, ETH_P_IP);
			DBGPRINT(RT_DEBUG_TRACE, ("IGMP Group=%02x:%02x:%02x:%02x:%02x:%02x\n",
				GroupMacAddr[0], GroupMacAddr[1], GroupMacAddr[2], GroupMacAddr[3], GroupMacAddr[4], GroupMacAddr[5]));
			MulticastFilterTableDeleteEntry(pAd, GroupMacAddr, pSrcMacAddr, pDev);
			break;

		case IGMP_V3_MEMBERSHIP_REPORT: // IGMP version 3 membership report.
			numOfGroup = ntohs(*((UINT16 *)(pIgmpHeader + 6)));
			pGroup = (PUCHAR)(pIgmpHeader + 8);
			for (i=0; i < numOfGroup; i++)
			{
				GroupType = (UCHAR)(*pGroup);
				AuxDataLen = (UCHAR)(*(pGroup + 1));
				numOfSources = ntohs(*((UINT16 *)(pGroup + 2)));
				pGroupIpAddr = (PUCHAR)(pGroup + 4);
				DBGPRINT(RT_DEBUG_TRACE, ("IGMPv3 Type=%d, ADL=%d, numOfSource=%d\n", GroupType, AuxDataLen, numOfSources));
					ConvertMulticastIP2MAC(pGroupIpAddr, (PUCHAR *)&pGroupMacAddr, ETH_P_IP);
				DBGPRINT(RT_DEBUG_TRACE, ("IGMP Group=%02x:%02x:%02x:%02x:%02x:%02x\n",
					GroupMacAddr[0], GroupMacAddr[1], GroupMacAddr[2], GroupMacAddr[3], GroupMacAddr[4], GroupMacAddr[5]));

				do
				{
					if((GroupType == MODE_IS_EXCLUDE) || (GroupType == CHANGE_TO_EXCLUDE_MODE) || (GroupType == ALLOW_NEW_SOURCES))
					{
						MulticastFilterTableInsertEntry(pAd, GroupMacAddr, pSrcMacAddr, pDev, MCAT_FILTER_DYNAMIC);
						break;
					}

					if((GroupType == MODE_IS_INCLUDE) || (GroupType == BLOCK_OLD_SOURCES))
					{
						MulticastFilterTableDeleteEntry(pAd, GroupMacAddr, pSrcMacAddr, pDev);
						break;
					}

					if((GroupType == CHANGE_TO_INCLUDE_MODE))
					{
						if(numOfSources == 0)
							MulticastFilterTableDeleteEntry(pAd, GroupMacAddr, pSrcMacAddr, pDev);
						else
							MulticastFilterTableInsertEntry(pAd, GroupMacAddr, pSrcMacAddr, pDev, MCAT_FILTER_DYNAMIC);
						break;
					}
				} while(FALSE);
				pGroup += (8 + (numOfSources * 4) + AuxDataLen);
			}
			break;

		default:
			DBGPRINT(RT_DEBUG_TRACE, ("unknow IGMP Type=%d\n", IgmpVerType));
			break;
		}
	}

	return;
}


static BOOLEAN isIgmpMacAddr(
	IN PUCHAR pMacAddr)
{
	if((pMacAddr[0] == 0x01)
		&& (pMacAddr[1] == 0x00)
		&& (pMacAddr[2] == 0x5e))
		return TRUE;
	return FALSE;
}

BOOLEAN isIgmpPkt(
	IN PUCHAR pDstMacAddr,
	IN PUCHAR pIpHeader)
{
	UINT16 IpProtocol = ntohs(*((UINT16 *)(pIpHeader)));
	UCHAR IgmpProtocol;

	if(!isIgmpMacAddr(pDstMacAddr))
		return FALSE;

	if(IpProtocol == ETH_P_IP)
	{
		IgmpProtocol = (UCHAR)*(pIpHeader + 11);
		if(IgmpProtocol == IGMP_PROTOCOL_DESCRIPTOR)
				return TRUE;
	}

	return FALSE;
}

static VOID InsertIgmpMember(
	IN PMULTICAST_FILTER_TABLE pMulticastFilterTable,
	IN PLIST_HEADER pList,
	IN PUCHAR pMemberAddr)
{
	PMEMBER_ENTRY pMemberEntry;

	if(pList == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: membert list doesn't exist.\n", __FUNCTION__));
		return;
	}

	if (pMemberAddr == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: invalid member.\n", __FUNCTION__));
		return;
	}

	if((pMemberEntry = (PMEMBER_ENTRY)AllocaGrpMemberEntry(pMulticastFilterTable)) != NULL)
	{
		NdisZeroMemory(pMemberEntry, sizeof(MEMBER_ENTRY));
		COPY_MAC_ADDR(pMemberEntry->Addr, pMemberAddr);
		insertTailList(pList, (PLIST_ENTRY)pMemberEntry);

		DBGPRINT(RT_DEBUG_TRACE, ("%s Member Mac=%02x:%02x:%02x:%02x:%02x:%02x\n", __FUNCTION__,
			pMemberEntry->Addr[0], pMemberEntry->Addr[1], pMemberEntry->Addr[2],
			pMemberEntry->Addr[3], pMemberEntry->Addr[4], pMemberEntry->Addr[5]));
	}
	return;
}

static VOID DeleteIgmpMember(
	IN PMULTICAST_FILTER_TABLE pMulticastFilterTable,
	IN PLIST_HEADER pList,
	IN PUCHAR pMemberAddr)
{
	PMEMBER_ENTRY pCurEntry;

	if((pList == NULL) || (pList->pHead == NULL))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: membert list doesn't exist.\n", __FUNCTION__));
		return;
	}

	if (pMemberAddr == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: invalid member.\n", __FUNCTION__));
		return;
	}

	pCurEntry = (PMEMBER_ENTRY)pList->pHead;
	while (pCurEntry)
	{
		if(MAC_ADDR_EQUAL(pMemberAddr, pCurEntry->Addr))
		{
			delEntryList(pList, (PLIST_ENTRY)pCurEntry);
			FreeGrpMemberEntry(pMulticastFilterTable, pCurEntry);
			break;
		}
		pCurEntry = pCurEntry->pNext;
	}

	return;
}

static VOID DeleteIgmpMemberList(
	IN PMULTICAST_FILTER_TABLE pMulticastFilterTable,
	IN PLIST_HEADER pList)
{
	PMEMBER_ENTRY pCurEntry, pPrvEntry;

	if((pList == NULL) || (pList->pHead == NULL))
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: membert list doesn't exist.\n", __FUNCTION__));
		return;
	}

	pPrvEntry = pCurEntry = (PMEMBER_ENTRY)pList->pHead;
	while (pCurEntry)
	{
		delEntryList(pList, (PLIST_ENTRY)pCurEntry);
		pPrvEntry = pCurEntry;
		pCurEntry = pCurEntry->pNext;
		FreeGrpMemberEntry(pMulticastFilterTable, pPrvEntry);
	}

	initList(pList);
	return;
}


UCHAR IgmpMemberCnt(
	IN PLIST_HEADER pList)
{
	if(pList == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("%s: membert list doesn't exist.\n", __FUNCTION__));
		return 0;
	}

	return getListSize(pList);
}

VOID IgmpGroupDelMembers(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pMemberAddr,
	IN PNET_DEV pDev)
{
	INT i;
	MULTICAST_FILTER_TABLE_ENTRY *pEntry = NULL;
	PMULTICAST_FILTER_TABLE pMulticastFilterTable = pAd->pMulticastFilterTable;

	for (i = 0; i < MAX_LEN_OF_MULTICAST_FILTER_TABLE; i++)
	{
		// pick up the first available vacancy
		pEntry = &pMulticastFilterTable->Content[i];
		if (pEntry->Valid == TRUE)
		{
			if(pMemberAddr != NULL)
			{
				RTMP_SEM_LOCK(&pMulticastFilterTable->MulticastFilterTabLock);
				DeleteIgmpMember(pMulticastFilterTable, &pEntry->MemberList, pMemberAddr);
				RTMP_SEM_UNLOCK(&pMulticastFilterTable->MulticastFilterTabLock);
			}

			if((pEntry->type == MCAT_FILTER_DYNAMIC)
				&& (IgmpMemberCnt(&pEntry->MemberList) == 0))
				MulticastFilterTableDeleteEntry(pAd, pEntry->Addr, pMemberAddr, pDev);
		}
	}
}

INT Set_IgmpSn_Enable_Proc(
	IN PRTMP_ADAPTER pAd,
	IN PSTRING arg)
{
	UINT Enable;
	POS_COOKIE pObj;
	UCHAR ifIndex;
	PNET_DEV pDev;

	pObj = (POS_COOKIE) pAd->OS_Cookie;
	ifIndex = pObj->ioctl_if;

	pDev = (ifIndex == MAIN_MBSSID) ? (pAd->net_dev) : (pAd->ApCfg.MBSSID[ifIndex].MSSIDDev);
	Enable = (UINT) simple_strtol(arg, 0, 10);

	pAd->ApCfg.MBSSID[ifIndex].IgmpSnoopEnable = (BOOLEAN)(Enable == 0 ? 0 : 1);
	DBGPRINT(RT_DEBUG_TRACE, ("%s::(%s) %s\n", __FUNCTION__, RTMP_OS_NETDEV_GET_DEVNAME(pDev), Enable == TRUE ? "Enable IGMP Snooping":"Disable IGMP Snooping"));

	return TRUE;
}

INT Set_IgmpSn_AddEntry_Proc(
	IN PRTMP_ADAPTER pAd,
	IN PSTRING arg)
{
	INT i;
	BOOLEAN bGroupId = 1;
	PSTRING value;
	PSTRING thisChar;
	UCHAR IpAddr[4];
	UCHAR Addr[ETH_LENGTH_OF_ADDRESS];
	UCHAR GroupId[ETH_LENGTH_OF_ADDRESS];
	PUCHAR *pAddr = (PUCHAR *)&Addr;
	PNET_DEV pDev;
	POS_COOKIE pObj;
	UCHAR ifIndex;

	pObj = (POS_COOKIE) pAd->OS_Cookie;
	ifIndex = pObj->ioctl_if;

	pDev = (ifIndex == MAIN_MBSSID) ? (pAd->net_dev) : (pAd->ApCfg.MBSSID[ifIndex].MSSIDDev);

	while ((thisChar = strsep((char **)&arg, "-")) != NULL)
	{
		// refuse the Member if it's not a MAC address.
		if((bGroupId == 0) && (strlen(thisChar) != 17))
			continue;

		if(strlen(thisChar) == 17)  //Mac address acceptable format 01:02:03:04:05:06 length 17
		{
			for (i=0, value = rstrtok(thisChar,":"); value; value = rstrtok(NULL,":"))
			{
				if((strlen(value) != 2) || (!isxdigit(*value)) || (!isxdigit(*(value+1))) )
					return FALSE;  //Invalid

				AtoH(value, &Addr[i++], 1);
			}

			if(i != 6)
				return FALSE;  //Invalid
		}
		else
		{
			for (i=0, value = rstrtok(thisChar,"."); value; value = rstrtok(NULL,"."))
			{
				if((strlen(value) > 0) && (strlen(value) <= 3))
				{
					int ii;
					for(ii=0; ii<strlen(value); ii++)
						if (!isxdigit(*(value + ii)))
							return FALSE;
				}
				else
					return FALSE;  //Invalid

				IpAddr[i] = (UCHAR)simple_strtol(value, NULL, 10);
				i++;
			}

			if(i != 4)
				return FALSE;  //Invalid

			ConvertMulticastIP2MAC(IpAddr, (PUCHAR *)&pAddr, ETH_P_IP);
		}

		if(bGroupId == 1)
			COPY_MAC_ADDR(GroupId, Addr);

		// Group-Id must be a MCAST address.
		if((bGroupId == 1) && IS_MULTICAST_MAC_ADDR(Addr))
			MulticastFilterTableInsertEntry(pAd, GroupId, NULL, pDev, MCAT_FILTER_STATIC);
		// Group-Member must be a UCAST address.
		else if ((bGroupId == 0) && !IS_MULTICAST_MAC_ADDR(Addr))
			MulticastFilterTableInsertEntry(pAd, GroupId, Addr, pDev, MCAT_FILTER_STATIC);
		else
		{
			DBGPRINT(RT_DEBUG_TRACE, ("%s (%2X:%2X:%2X:%2X:%2X:%2X) is not a acceptable address.\n",
				__FUNCTION__, Addr[0], Addr[1], Addr[2], Addr[3], Addr[4], Addr[5]));
			return FALSE;
		}

		bGroupId = 0;
		DBGPRINT(RT_DEBUG_TRACE, ("%s (%2X:%2X:%2X:%2X:%2X:%2X)\n",
			__FUNCTION__, Addr[0], Addr[1], Addr[2], Addr[3], Addr[4], Addr[5]));

	}

	return TRUE;
}

INT Set_IgmpSn_DelEntry_Proc(
	IN PRTMP_ADAPTER pAd,
	IN PSTRING arg)
{
	INT i, memberCnt = 0;
	BOOLEAN bGroupId = 1;
	PSTRING value;
	PSTRING thisChar;
	UCHAR IpAddr[4];
	UCHAR Addr[ETH_LENGTH_OF_ADDRESS];
	UCHAR GroupId[ETH_LENGTH_OF_ADDRESS];
	PUCHAR *pAddr = (PUCHAR *)&Addr;
	PNET_DEV pDev;
	POS_COOKIE pObj;
	UCHAR ifIndex;

	pObj = (POS_COOKIE) pAd->OS_Cookie;
	ifIndex = pObj->ioctl_if;

	pDev = (ifIndex == MAIN_MBSSID) ? (pAd->net_dev) : (pAd->ApCfg.MBSSID[ifIndex].MSSIDDev);

	while ((thisChar = strsep((char **)&arg, "-")) != NULL)
	{
		// refuse the Member if it's not a MAC address.
		if((bGroupId == 0) && (strlen(thisChar) != 17))
			continue;

		if(strlen(thisChar) == 17)  //Mac address acceptable format 01:02:03:04:05:06 length 17
		{
			for (i=0, value = rstrtok(thisChar,":"); value; value = rstrtok(NULL,":"))
			{
				if((strlen(value) != 2) || (!isxdigit(*value)) || (!isxdigit(*(value+1))) )
					return FALSE;  //Invalid

				AtoH(value, &Addr[i++], 1);
			}

			if(i != 6)
				return FALSE;  //Invalid
		}
		else
		{
			for (i=0, value = rstrtok(thisChar,"."); value; value = rstrtok(NULL,"."))
			{
				if((strlen(value) > 0) && (strlen(value) <= 3))
				{
					int ii;
					for(ii=0; ii<strlen(value); ii++)
						if (!isxdigit(*(value + ii)))
							return FALSE;
				}
				else
					return FALSE;  //Invalid

				IpAddr[i] = (UCHAR)simple_strtol(value, NULL, 10);
				i++;
			}

			if(i != 4)
				return FALSE;  //Invalid

			ConvertMulticastIP2MAC(IpAddr, (PUCHAR *)&pAddr, ETH_P_IP);
		}

		if(bGroupId == 1)
			COPY_MAC_ADDR(GroupId, Addr);
		else
			memberCnt++;

		if (memberCnt > 0 )
			MulticastFilterTableDeleteEntry(pAd, (PUCHAR)GroupId, Addr, pDev);

		bGroupId = 0;
	}

	if(memberCnt == 0)
		MulticastFilterTableDeleteEntry(pAd, (PUCHAR)GroupId, NULL, pDev);

	DBGPRINT(RT_DEBUG_TRACE, ("%s (%2X:%2X:%2X:%2X:%2X:%2X)\n",
		__FUNCTION__, Addr[0], Addr[1], Addr[2], Addr[3], Addr[4], Addr[5]));

	return TRUE;
}

INT Set_IgmpSn_TabDisplay_Proc(
	IN PRTMP_ADAPTER pAd,
	IN PSTRING arg)
{
	IGMPTableDisplay(pAd);
	return TRUE;
}

void rtmp_read_igmp_snoop_from_file(
	IN  PRTMP_ADAPTER pAd,
	PSTRING tmpbuf,
	PSTRING buffer)
{
	PSTRING		macptr;
	INT			i=0;

	//IgmpSnEnable
	if(RTMPGetKeyParameter("IgmpSnEnable", tmpbuf, 128, buffer, TRUE))
	{
		for (i = 0, macptr = rstrtok(tmpbuf,";"); (macptr && i < pAd->ApCfg.BssidNum); macptr = rstrtok(NULL,";"), i++)
		{
			if ((strncmp(macptr, "0", 1) == 0))
				pAd->ApCfg.MBSSID[i].IgmpSnoopEnable = FALSE;
			else if ((strncmp(macptr, "1", 1) == 0))
				pAd->ApCfg.MBSSID[i].IgmpSnoopEnable = TRUE;
	        else
				pAd->ApCfg.MBSSID[i].IgmpSnoopEnable = FALSE;

			DBGPRINT(RT_DEBUG_TRACE, ("MBSSID[%d].Enable=%d\n", i, pAd->ApCfg.MBSSID[i].IgmpSnoopEnable));
	    }
	}
}

NDIS_STATUS IgmpPktInfoQuery(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pSrcBufVA,
	IN PNDIS_PACKET pPacket,
	IN UCHAR apidx,
	OUT BOOLEAN *pInIgmpGroup,
	OUT PMULTICAST_FILTER_TABLE_ENTRY *ppGroupEntry)
{
	if(IS_MULTICAST_MAC_ADDR(pSrcBufVA))
	{
		BOOLEAN IgmpMldPkt = FALSE;
		PUCHAR pIpHeader = pSrcBufVA + 12;

		if(ntohs(*((UINT16 *)(pIpHeader))) == ETH_P_IPV6)
			IgmpMldPkt = isMldPkt(pSrcBufVA, pIpHeader, NULL, NULL);
		else
			IgmpMldPkt = isIgmpPkt(pSrcBufVA, pIpHeader);

		if (IgmpMldPkt)
		{
			*ppGroupEntry = NULL;
		}
		else if ((*ppGroupEntry = MulticastFilterTableLookup(pAd->pMulticastFilterTable, pSrcBufVA,
									pAd->ApCfg.MBSSID[apidx].MSSIDDev)) == NULL)
		{
			RELEASE_NDIS_PACKET(pAd, pPacket, NDIS_STATUS_FAILURE);
			return NDIS_STATUS_FAILURE;
		}
		*pInIgmpGroup = TRUE;
	}
	else if (IS_BROADCAST_MAC_ADDR(pSrcBufVA))
	{
		PUCHAR pDstIpAddr = pSrcBufVA + 30; // point to Destination of Ip address of IP header.
		UCHAR GroupMacAddr[6];
		PUCHAR pGroupMacAddr = (PUCHAR)&GroupMacAddr;

		ConvertMulticastIP2MAC(pDstIpAddr, (PUCHAR *)&pGroupMacAddr, ETH_P_IP);
		if ((*ppGroupEntry = MulticastFilterTableLookup(pAd->pMulticastFilterTable, pGroupMacAddr,
								pAd->ApCfg.MBSSID[apidx].MSSIDDev)) != NULL)
		{
			*pInIgmpGroup = TRUE;
		}
	}
	return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS IgmpPktClone(
	IN PRTMP_ADAPTER pAd,
	IN PNDIS_PACKET pPacket,
	IN UCHAR QueIdx,
	IN PMULTICAST_FILTER_TABLE_ENTRY pGroupEntry)
{
	PNDIS_PACKET pSkbClone = NULL;
	PMEMBER_ENTRY pMemberEntry = (PMEMBER_ENTRY)pGroupEntry->MemberList.pHead;
	MAC_TABLE_ENTRY *pMacEntry = NULL;
	USHORT Aid;
	SST	Sst = SST_ASSOC;
	UCHAR PsMode = PWR_ACTIVE;
	UCHAR Rate;
	unsigned long IrqFlags;

	// check all members of the IGMP group.
	while(pMemberEntry != NULL)
	{
		pMacEntry = APSsPsInquiry(pAd, pMemberEntry->Addr, &Sst, &Aid, &PsMode, &Rate);

		if (pMacEntry && (Sst == SST_ASSOC) && (PsMode != PWR_SAVE))
		{
		pSkbClone = skb_clone(RTPKT_TO_OSPKT(pPacket), MEM_ALLOC_FLAG);
		if(pSkbClone)
		{
				RTMP_SET_PACKET_WCID(pSkbClone, (UCHAR)Aid);
				// Pkt type must set to PKTSRC_NDIS.
				// It cause of the deason that APHardTransmit()
				// doesn't handle PKTSRC_DRIVER pkt type in version 1.3.0.0.
				RTMP_SET_PACKET_SOURCE(pSkbClone, PKTSRC_NDIS);
			}
			else
			{
				pMemberEntry = pMemberEntry->pNext;
				continue;
			}

			// insert the pkt to TxSwQueue.
			if (pAd->TxSwQueue[QueIdx].Number >= MAX_PACKETS_IN_QUEUE)
			{
#ifdef BLOCK_NET_IF
				StopNetIfQueue(pAd, QueIdx, pSkbClone);
#endif // BLOCK_NET_IF //
				RELEASE_NDIS_PACKET(pAd, pSkbClone, NDIS_STATUS_FAILURE);
				return NDIS_STATUS_FAILURE;
			}
			else
			{
				RTMP_IRQ_LOCK(&pAd->irq_lock, IrqFlags);
				InsertTailQueueAc(pAd, pMacEntry, &pAd->TxSwQueue[QueIdx], PACKET_TO_QUEUE_ENTRY(pSkbClone));
				RTMP_IRQ_UNLOCK(&pAd->irq_lock, IrqFlags);
			}
		}
		pMemberEntry = pMemberEntry->pNext;
	}
	return NDIS_STATUS_SUCCESS;
}

static inline BOOLEAN isMldMacAddr(
	IN PUCHAR pMacAddr)
{
	return ((pMacAddr[0] == 0x33) && (pMacAddr[1] == 0x33)) ? TRUE : FALSE;
}

static inline BOOLEAN IsSupportedMldMsg(
	IN UINT8 MsgType)
{
	BOOLEAN result = FALSE;
	switch(MsgType)
	{
		case MLD_V1_LISTENER_REPORT:
		case MLD_V1_LISTENER_DONE:
		case MLD_V2_LISTERNER_REPORT:
			result = TRUE;
			break;
		default:
			result = FALSE;
			break;
	}

	return result;
}

BOOLEAN isMldPkt(
	IN PUCHAR pDstMacAddr,
	IN PUCHAR pIpHeader,
	OUT UINT8 *pProtoType,
	OUT PUCHAR *pMldHeader)
{
	BOOLEAN result = FALSE;
	UINT16 IpProtocol = ntohs(*((UINT16 *)(pIpHeader)));

	if(!isMldMacAddr(pDstMacAddr))
		return FALSE;

	if(IpProtocol != ETH_P_IPV6)
		return FALSE;

	// skip protocol (2 Bytes).
	pIpHeader += 2;
	do
	{
		PRT_IPV6_HDR pIpv6Hdr = (PRT_IPV6_HDR)(pIpHeader);
		UINT8 nextProtocol = pIpv6Hdr->nextHdr;
		UINT32 offset = IPV6_HDR_LEN;

		while(nextProtocol != IPV6_NEXT_HEADER_ICMPV6)
		{
			if(IPv6ExtHdrHandle((RT_IPV6_EXT_HDR *)(pIpHeader + offset), &nextProtocol, &offset) == FALSE)
				break;
		}

		if(nextProtocol == IPV6_NEXT_HEADER_ICMPV6)
		{
			PRT_ICMPV6_HDR pICMPv6Hdr = (PRT_ICMPV6_HDR)(pIpHeader + offset);
			if (IsSupportedMldMsg(pICMPv6Hdr->type) == TRUE)
			{
				if (pProtoType != NULL)
					*pProtoType = pICMPv6Hdr->type;
				if (pMldHeader != NULL)
					*pMldHeader = (PUCHAR)pICMPv6Hdr;
				result = TRUE;
			}
		}
	}while(FALSE);

	return result;
}

/*  MLD v1 messages have the following format:
	0                   1                   2                   3
	0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|     Type      |     Code      |          Checksum             |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|     Maximum Response Delay    |          Reserved             |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                                                               |
	+                                                               +
	|                                                               |
	+                       Multicast Address                       +
	|                                                               |
	+                                                               +
	|                                                               |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

/*	Version 3 Membership Report Message
	0                   1                   2                   3
	0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|  Type = 143   |    Reserved   |           Checksum            |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|           Reserved            |  Number of Group Records (M)  |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                                                               |
	.                                                               .
	.               Multicast Address Record [1]                    .
	.                                                               .
	|                                                               |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                                                               |
	.                                                               .
	.               Multicast Address Record [2]                    .
	.                                                               .
	|                                                               |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                               .                               |
	.                               .                               .
	|                               .                               |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|                                                               |
	.                                                               .
	.               Multicast Address Record [M]                    .
	.                                                               .
	|                                                               |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+


	where each Group Record has the following internal format:
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|  Record Type  |  Aux Data Len |     Number of Sources (N)     |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                                                               |
    *                                                               *
    |                                                               |
    *                       Multicast Address                       *
    |                                                               |
    *                                                               *
    |                                                               |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                                                               |
    *                                                               *
    |                                                               |
    *                       Source Address [1]                      *
    |                                                               |
    *                                                               *
    |                                                               |
    +-                                                             -+
    |                                                               |
    *                                                               *
    |                                                               |
    *                       Source Address [2]                      *
    |                                                               |
    *                                                               *
    |                                                               |
    +-                                                             -+
    .                               .                               .
    .                               .                               .
    .                               .                               .
    +-                                                             -+
    |                                                               |
    *                                                               *
    |                                                               |
    *                       Source Address [N]                      *
    |                                                               |
    *                                                               *
    |                                                               |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |                                                               |
    .                                                               .
    .                         Auxiliary Data                        .
    .                                                               .
    |                                                               |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/

VOID MLDSnooping(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pDstMacAddr,
	IN PUCHAR pSrcMacAddr,
	IN PUCHAR pIpHeader,
	IN PNET_DEV pDev)
{
	INT i;
	UCHAR GroupType;
	UINT16 numOfGroup;
	PUCHAR pGroup;
	UCHAR AuxDataLen;
	UINT16 numOfSources;
	PUCHAR pGroupIpAddr;
	UCHAR GroupMacAddr[6];
	PUCHAR pGroupMacAddr = (PUCHAR)&GroupMacAddr;

	UINT8 MldType;
	PUCHAR pMldHeader;

	if(isMldPkt(pDstMacAddr, pIpHeader, &MldType, &pMldHeader) == TRUE)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("MLD type=%0x\n", MldType));

		switch(MldType)
		{
			case MLD_V1_LISTENER_REPORT:
				// skip Type(1 Byte), code(1 Byte), checksum(2 Bytes), Maximum Rsp Delay(2 Bytes), Reserve(2 Bytes).
				pGroupIpAddr = (PUCHAR)(pMldHeader + 8);
				ConvertMulticastIP2MAC(pGroupIpAddr, (PUCHAR *)&pGroupMacAddr, ETH_P_IPV6);
				DBGPRINT(RT_DEBUG_TRACE, ("Group Id=%02x:%02x:%02x:%02x:%02x:%02x\n",
						GroupMacAddr[0], GroupMacAddr[1], GroupMacAddr[2], GroupMacAddr[3], GroupMacAddr[4], GroupMacAddr[5]));
				MulticastFilterTableInsertEntry(pAd, GroupMacAddr, pSrcMacAddr, pDev, MCAT_FILTER_DYNAMIC);
				break;

			case MLD_V1_LISTENER_DONE:
				// skip Type(1 Byte), code(1 Byte), checksum(2 Bytes), Maximum Rsp Delay(2 Bytes), Reserve(2 Bytes).
				pGroupIpAddr = (PUCHAR)(pMldHeader + 8);
				ConvertMulticastIP2MAC(pGroupIpAddr, (PUCHAR *)&pGroupMacAddr, ETH_P_IPV6);
				DBGPRINT(RT_DEBUG_TRACE, ("Group Id=%02x:%02x:%02x:%02x:%02x:%02x\n",
						GroupMacAddr[0], GroupMacAddr[1], GroupMacAddr[2], GroupMacAddr[3], GroupMacAddr[4], GroupMacAddr[5]));
				MulticastFilterTableDeleteEntry(pAd, GroupMacAddr, pSrcMacAddr, pDev);
				break;

			case MLD_V2_LISTERNER_REPORT: // IGMP version 3 membership report.
				numOfGroup = ntohs(*((UINT16 *)(pMldHeader + 6)));
				pGroup = (PUCHAR)(pMldHeader + 8);
				for (i=0; i < numOfGroup; i++)
				{
					GroupType = (UCHAR)(*pGroup);
					AuxDataLen = (UCHAR)(*(pGroup + 1));
					numOfSources = ntohs(*((UINT16 *)(pGroup + 2)));
					pGroupIpAddr = (PUCHAR)(pGroup + 4);
					DBGPRINT(RT_DEBUG_TRACE, ("MLDv2 Type=%d, ADL=%d, numOfSource=%d\n", GroupType, AuxDataLen, numOfSources));
					ConvertMulticastIP2MAC(pGroupIpAddr, (PUCHAR *)&pGroupMacAddr, ETH_P_IPV6);
					DBGPRINT(RT_DEBUG_TRACE, ("MLD Group=%02x:%02x:%02x:%02x:%02x:%02x\n",
							GroupMacAddr[0], GroupMacAddr[1], GroupMacAddr[2], GroupMacAddr[3], GroupMacAddr[4], GroupMacAddr[5]));

					do
					{
						if((GroupType == MODE_IS_EXCLUDE) || (GroupType == CHANGE_TO_EXCLUDE_MODE) || (GroupType == ALLOW_NEW_SOURCES))
						{
							MulticastFilterTableInsertEntry(pAd, GroupMacAddr, pSrcMacAddr, pDev, MCAT_FILTER_DYNAMIC);
							break;
						}

						if((GroupType == MODE_IS_INCLUDE) || (GroupType == BLOCK_OLD_SOURCES))
						{
							MulticastFilterTableDeleteEntry(pAd, GroupMacAddr, pSrcMacAddr, pDev);
							break;
						}

						if((GroupType == CHANGE_TO_INCLUDE_MODE))
						{
							if(numOfSources == 0)
								MulticastFilterTableDeleteEntry(pAd, GroupMacAddr, pSrcMacAddr, pDev);
							else
								MulticastFilterTableInsertEntry(pAd, GroupMacAddr, pSrcMacAddr, pDev, MCAT_FILTER_DYNAMIC);
							break;
						}
					} while(FALSE);
					// skip 4 Bytes (Record Type, Aux Data Len, Number of Sources) + a IPv6 address.
					pGroup += (4 + IPV6_ADDR_LEN + (numOfSources * 16) + AuxDataLen);
				}
				break;

			default:
				DBGPRINT(RT_DEBUG_TRACE, ("unknow MLD Type=%d\n", MldType));
				break;
		}
	}

	return;
}


#endif // IGMP_SNOOP_SUPPORT //
