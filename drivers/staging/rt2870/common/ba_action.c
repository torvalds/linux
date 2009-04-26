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


#ifdef DOT11_N_SUPPORT

#include "../rt_config.h"



#define BA_ORI_INIT_SEQ		(pEntry->TxSeq[TID]) //1			// inital sequence number of BA session

#define ORI_SESSION_MAX_RETRY	8
#define ORI_BA_SESSION_TIMEOUT	(2000)	// ms
#define REC_BA_SESSION_IDLE_TIMEOUT	(1000)	// ms

#define REORDERING_PACKET_TIMEOUT		((100 * HZ)/1000)	// system ticks -- 100 ms
#define MAX_REORDERING_PACKET_TIMEOUT	((3000 * HZ)/1000)	// system ticks -- 100 ms

#define RESET_RCV_SEQ		(0xFFFF)

static void ba_mpdu_blk_free(PRTMP_ADAPTER pAd, struct reordering_mpdu *mpdu_blk);


BA_ORI_ENTRY *BATableAllocOriEntry(
								  IN  PRTMP_ADAPTER   pAd,
								  OUT USHORT          *Idx);

BA_REC_ENTRY *BATableAllocRecEntry(
								  IN  PRTMP_ADAPTER   pAd,
								  OUT USHORT          *Idx);

VOID BAOriSessionSetupTimeout(
    IN PVOID SystemSpecific1,
    IN PVOID FunctionContext,
    IN PVOID SystemSpecific2,
    IN PVOID SystemSpecific3);

VOID BARecSessionIdleTimeout(
    IN PVOID SystemSpecific1,
    IN PVOID FunctionContext,
    IN PVOID SystemSpecific2,
    IN PVOID SystemSpecific3);


BUILD_TIMER_FUNCTION(BAOriSessionSetupTimeout);
BUILD_TIMER_FUNCTION(BARecSessionIdleTimeout);

#define ANNOUNCE_REORDERING_PACKET(_pAd, _mpdu_blk)	\
			Announce_Reordering_Packet(_pAd, _mpdu_blk);

VOID BA_MaxWinSizeReasign(
	IN PRTMP_ADAPTER	pAd,
	IN MAC_TABLE_ENTRY  *pEntryPeer,
	OUT UCHAR			*pWinSize)
{
	UCHAR MaxSize;


	if (pAd->MACVersion >= RALINK_2883_VERSION) // 3*3
	{
		if (pAd->MACVersion >= RALINK_3070_VERSION)
		{
			if (pEntryPeer->WepStatus != Ndis802_11EncryptionDisabled)
				MaxSize = 7; // for non-open mode
			else
				MaxSize = 13;
		}
		else
			MaxSize = 31;
	}
	else if (pAd->MACVersion >= RALINK_2880E_VERSION) // 2880 e
	{
		if (pEntryPeer->WepStatus != Ndis802_11EncryptionDisabled)
			MaxSize = 7; // for non-open mode
		else
			MaxSize = 13;
	}
	else
		MaxSize = 7;

	DBGPRINT(RT_DEBUG_TRACE, ("ba> Win Size = %d, Max Size = %d\n",
			*pWinSize, MaxSize));

	if ((*pWinSize) > MaxSize)
	{
		DBGPRINT(RT_DEBUG_TRACE, ("ba> reassign max win size from %d to %d\n",
				*pWinSize, MaxSize));

		*pWinSize = MaxSize;
	}
}

void Announce_Reordering_Packet(IN PRTMP_ADAPTER			pAd,
								IN struct reordering_mpdu	*mpdu)
{
	PNDIS_PACKET    pPacket;

	pPacket = mpdu->pPacket;

	if (mpdu->bAMSDU)
	{
		ASSERT(0);
		BA_Reorder_AMSDU_Annnounce(pAd, pPacket);
	}
	else
	{
		//
		// pass this 802.3 packet to upper layer or forward this packet to WM directly
		//

		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
			ANNOUNCE_OR_FORWARD_802_3_PACKET(pAd, pPacket, RTMP_GET_PACKET_IF(pPacket));
	}
}

/*
 * Insert a reordering mpdu into sorted linked list by sequence no.
 */
BOOLEAN ba_reordering_mpdu_insertsorted(struct reordering_list *list, struct reordering_mpdu *mpdu)
{

	struct reordering_mpdu **ppScan = &list->next;

	while (*ppScan != NULL)
	{
		if (SEQ_SMALLER((*ppScan)->Sequence, mpdu->Sequence, MAXSEQ))
		{
			ppScan = &(*ppScan)->next;
		}
		else if ((*ppScan)->Sequence == mpdu->Sequence)
		{
			/* give up this duplicated frame */
			return(FALSE);
		}
		else
		{
			/* find position */
			break;
		}
	}

	mpdu->next = *ppScan;
	*ppScan = mpdu;
	list->qlen++;
	return TRUE;
}


/*
 * caller lock critical section if necessary
 */
static inline void ba_enqueue(struct reordering_list *list, struct reordering_mpdu *mpdu_blk)
{
	list->qlen++;
	mpdu_blk->next = list->next;
	list->next = mpdu_blk;
}

/*
 * caller lock critical section if necessary
 */
static inline struct reordering_mpdu * ba_dequeue(struct reordering_list *list)
{
	struct reordering_mpdu *mpdu_blk = NULL;

	ASSERT(list);

		if (list->qlen)
		{
			list->qlen--;
			mpdu_blk = list->next;
			if (mpdu_blk)
			{
				list->next = mpdu_blk->next;
				mpdu_blk->next = NULL;
			}
		}
	return mpdu_blk;
}


static inline struct reordering_mpdu  *ba_reordering_mpdu_dequeue(struct reordering_list *list)
{
	return(ba_dequeue(list));
}


static inline struct reordering_mpdu  *ba_reordering_mpdu_probe(struct reordering_list *list)
	{
	ASSERT(list);

		return(list->next);
	}


/*
 * free all resource for reordering mechanism
 */
void ba_reordering_resource_release(PRTMP_ADAPTER pAd)
{
	BA_TABLE        *Tab;
	PBA_REC_ENTRY   pBAEntry;
	struct reordering_mpdu *mpdu_blk;
	int i;

	Tab = &pAd->BATable;

	/* I.  release all pending reordering packet */
	NdisAcquireSpinLock(&pAd->BATabLock);
	for (i = 0; i < MAX_LEN_OF_BA_REC_TABLE; i++)
	{
		pBAEntry = &Tab->BARecEntry[i];
		if (pBAEntry->REC_BA_Status != Recipient_NONE)
		{
			while ((mpdu_blk = ba_reordering_mpdu_dequeue(&pBAEntry->list)))
			{
				ASSERT(mpdu_blk->pPacket);
				RELEASE_NDIS_PACKET(pAd, mpdu_blk->pPacket, NDIS_STATUS_FAILURE);
				ba_mpdu_blk_free(pAd, mpdu_blk);
			}
		}
	}
	NdisReleaseSpinLock(&pAd->BATabLock);

	ASSERT(pBAEntry->list.qlen == 0);
	/* II. free memory of reordering mpdu table */
	NdisAcquireSpinLock(&pAd->mpdu_blk_pool.lock);
	os_free_mem(pAd, pAd->mpdu_blk_pool.mem);
	NdisReleaseSpinLock(&pAd->mpdu_blk_pool.lock);
}



/*
 * Allocate all resource for reordering mechanism
 */
BOOLEAN ba_reordering_resource_init(PRTMP_ADAPTER pAd, int num)
{
	int     i;
	PUCHAR  mem;
	struct reordering_mpdu *mpdu_blk;
	struct reordering_list *freelist;

	/* allocate spinlock */
	NdisAllocateSpinLock(&pAd->mpdu_blk_pool.lock);

	/* initialize freelist */
	freelist = &pAd->mpdu_blk_pool.freelist;
	freelist->next = NULL;
	freelist->qlen = 0;

	DBGPRINT(RT_DEBUG_TRACE, ("Allocate %d memory for BA reordering\n", (UINT32)(num*sizeof(struct reordering_mpdu))));

	/* allocate number of mpdu_blk memory */
	os_alloc_mem(pAd, (PUCHAR *)&mem, (num*sizeof(struct reordering_mpdu)));

	pAd->mpdu_blk_pool.mem = mem;

	if (mem == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR, ("Can't Allocate Memory for BA Reordering\n"));
		return(FALSE);
	}

	/* build mpdu_blk free list */
	for (i=0; i<num; i++)
	{
		/* get mpdu_blk */
		mpdu_blk = (struct reordering_mpdu *) mem;
		/* initial mpdu_blk */
		NdisZeroMemory(mpdu_blk, sizeof(struct reordering_mpdu));
		/* next mpdu_blk */
		mem += sizeof(struct reordering_mpdu);
		/* insert mpdu_blk into freelist */
		ba_enqueue(freelist, mpdu_blk);
	}

	return(TRUE);
}

//static int blk_count=0; // sample take off, no use

static struct reordering_mpdu *ba_mpdu_blk_alloc(PRTMP_ADAPTER pAd)
{
	struct reordering_mpdu *mpdu_blk;

	NdisAcquireSpinLock(&pAd->mpdu_blk_pool.lock);
	mpdu_blk = ba_dequeue(&pAd->mpdu_blk_pool.freelist);
	if (mpdu_blk)
	{
//		blk_count++;
		/* reset mpdu_blk */
		NdisZeroMemory(mpdu_blk, sizeof(struct reordering_mpdu));
	}
	NdisReleaseSpinLock(&pAd->mpdu_blk_pool.lock);
	return mpdu_blk;
}

static void ba_mpdu_blk_free(PRTMP_ADAPTER pAd, struct reordering_mpdu *mpdu_blk)
{
	ASSERT(mpdu_blk);

	NdisAcquireSpinLock(&pAd->mpdu_blk_pool.lock);
//	blk_count--;
	ba_enqueue(&pAd->mpdu_blk_pool.freelist, mpdu_blk);
	NdisReleaseSpinLock(&pAd->mpdu_blk_pool.lock);
}


static USHORT ba_indicate_reordering_mpdus_in_order(
												   IN PRTMP_ADAPTER    pAd,
												   IN PBA_REC_ENTRY    pBAEntry,
												   IN USHORT           StartSeq)
{
	struct reordering_mpdu *mpdu_blk;
	USHORT  LastIndSeq = RESET_RCV_SEQ;

	NdisAcquireSpinLock(&pBAEntry->RxReRingLock);

	while ((mpdu_blk = ba_reordering_mpdu_probe(&pBAEntry->list)))
		{
			/* find in-order frame */
		if (!SEQ_STEPONE(mpdu_blk->Sequence, StartSeq, MAXSEQ))
			{
				break;
			}
			/* dequeue in-order frame from reodering list */
			mpdu_blk = ba_reordering_mpdu_dequeue(&pBAEntry->list);
			/* pass this frame up */
		ANNOUNCE_REORDERING_PACKET(pAd, mpdu_blk);
		/* move to next sequence */
			StartSeq = mpdu_blk->Sequence;
		LastIndSeq = StartSeq;
		/* free mpdu_blk */
			ba_mpdu_blk_free(pAd, mpdu_blk);
	}

	NdisReleaseSpinLock(&pBAEntry->RxReRingLock);

	/* update last indicated sequence */
	return LastIndSeq;
}

static void ba_indicate_reordering_mpdus_le_seq(
											   IN PRTMP_ADAPTER    pAd,
											   IN PBA_REC_ENTRY    pBAEntry,
											   IN USHORT           Sequence)
{
	struct reordering_mpdu *mpdu_blk;

	NdisAcquireSpinLock(&pBAEntry->RxReRingLock);
	while ((mpdu_blk = ba_reordering_mpdu_probe(&pBAEntry->list)))
		{
			/* find in-order frame */
		if ((mpdu_blk->Sequence == Sequence) || SEQ_SMALLER(mpdu_blk->Sequence, Sequence, MAXSEQ))
		{
			/* dequeue in-order frame from reodering list */
			mpdu_blk = ba_reordering_mpdu_dequeue(&pBAEntry->list);
			/* pass this frame up */
			ANNOUNCE_REORDERING_PACKET(pAd, mpdu_blk);
			/* free mpdu_blk */
			ba_mpdu_blk_free(pAd, mpdu_blk);
		}
		else
			{
				break;
			}
	}
	NdisReleaseSpinLock(&pBAEntry->RxReRingLock);
}


static void ba_refresh_reordering_mpdus(
									   IN PRTMP_ADAPTER    pAd,
									   PBA_REC_ENTRY       pBAEntry)
{
	struct reordering_mpdu *mpdu_blk;

	NdisAcquireSpinLock(&pBAEntry->RxReRingLock);

			/* dequeue in-order frame from reodering list */
	while ((mpdu_blk = ba_reordering_mpdu_dequeue(&pBAEntry->list)))
	{
			/* pass this frame up */
		ANNOUNCE_REORDERING_PACKET(pAd, mpdu_blk);

		pBAEntry->LastIndSeq = mpdu_blk->Sequence;
			ba_mpdu_blk_free(pAd, mpdu_blk);

		/* update last indicated sequence */
	}
	ASSERT(pBAEntry->list.qlen == 0);
	pBAEntry->LastIndSeq = RESET_RCV_SEQ;
	NdisReleaseSpinLock(&pBAEntry->RxReRingLock);
}


//static
void ba_flush_reordering_timeout_mpdus(
									IN PRTMP_ADAPTER    pAd,
									IN PBA_REC_ENTRY    pBAEntry,
									IN ULONG            Now32)

{
	USHORT Sequence;

//	if ((RTMP_TIME_AFTER((unsigned long)Now32, (unsigned long)(pBAEntry->LastIndSeqAtTimer+REORDERING_PACKET_TIMEOUT)) &&
//		 (pBAEntry->list.qlen > ((pBAEntry->BAWinSize*7)/8))) //||
//		(RTMP_TIME_AFTER((unsigned long)Now32, (unsigned long)(pBAEntry->LastIndSeqAtTimer+(10*REORDERING_PACKET_TIMEOUT))) &&
//		 (pBAEntry->list.qlen > (pBAEntry->BAWinSize/8)))
	if (RTMP_TIME_AFTER((unsigned long)Now32, (unsigned long)(pBAEntry->LastIndSeqAtTimer+(MAX_REORDERING_PACKET_TIMEOUT/6)))
		 &&(pBAEntry->list.qlen > 1)
		)
	{
		DBGPRINT(RT_DEBUG_TRACE,("timeout[%d] (%08lx-%08lx = %d > %d): %x, flush all!\n ", pBAEntry->list.qlen, Now32, (pBAEntry->LastIndSeqAtTimer),
			   (int)((long) Now32 - (long)(pBAEntry->LastIndSeqAtTimer)), MAX_REORDERING_PACKET_TIMEOUT,
			   pBAEntry->LastIndSeq));
		ba_refresh_reordering_mpdus(pAd, pBAEntry);
		pBAEntry->LastIndSeqAtTimer = Now32;
	}
	else
	if (RTMP_TIME_AFTER((unsigned long)Now32, (unsigned long)(pBAEntry->LastIndSeqAtTimer+(REORDERING_PACKET_TIMEOUT)))
		&& (pBAEntry->list.qlen > 0)
	   )
		{
    		//
		// force LastIndSeq to shift to LastIndSeq+1
    		//
    		Sequence = (pBAEntry->LastIndSeq+1) & MAXSEQ;
    		ba_indicate_reordering_mpdus_le_seq(pAd, pBAEntry, Sequence);
    		pBAEntry->LastIndSeqAtTimer = Now32;
			pBAEntry->LastIndSeq = Sequence;
    		//
    		// indicate in-order mpdus
    		//
    		Sequence = ba_indicate_reordering_mpdus_in_order(pAd, pBAEntry, Sequence);
    		if (Sequence != RESET_RCV_SEQ)
    		{
    			pBAEntry->LastIndSeq = Sequence;
    		}

	}
#if 0
	else if (
			 (RTMP_TIME_AFTER((unsigned long)Now32, (unsigned long)(pBAEntry->LastIndSeqAtTimer+(MAX_REORDERING_PACKET_TIMEOUT))) &&
			  (pBAEntry->list.qlen > 1))
			)
		{
		DBGPRINT(RT_DEBUG_TRACE,("timeout[%d] (%lx-%lx = %d > %d): %x\n ", pBAEntry->list.qlen, Now32, (pBAEntry->LastIndSeqAtTimer),
			   (int)((long) Now32 - (long)(pBAEntry->LastIndSeqAtTimer)), MAX_REORDERING_PACKET_TIMEOUT,
			   pBAEntry->LastIndSeq));
		ba_refresh_reordering_mpdus(pAd, pBAEntry);
			pBAEntry->LastIndSeqAtTimer = Now32;
				}
#endif
}


/*
 * generate ADDBA request to
 * set up BA agreement
 */
VOID BAOriSessionSetUp(
					  IN PRTMP_ADAPTER    pAd,
					  IN MAC_TABLE_ENTRY  *pEntry,
					  IN UCHAR            TID,
					  IN USHORT           TimeOut,
					  IN ULONG            DelayTime,
					  IN BOOLEAN          isForced)

{
	//MLME_ADDBA_REQ_STRUCT	AddbaReq;
	BA_ORI_ENTRY            *pBAEntry = NULL;
	USHORT                  Idx;
	BOOLEAN                 Cancelled;

	if ((pAd->CommonCfg.BACapability.field.AutoBA != TRUE)  &&  (isForced == FALSE))
		return;

	// if this entry is limited to use legacy tx mode, it doesn't generate BA.
	if (RTMPStaFixedTxMode(pAd, pEntry) != FIXED_TXMODE_HT)
		return;

	if ((pEntry->BADeclineBitmap & (1<<TID)) && (isForced == FALSE))
	{
		// try again after 3 secs
		DelayTime = 3000;
//		printk("DeCline BA from Peer\n");
//		return;
	}


	Idx = pEntry->BAOriWcidArray[TID];
	if (Idx == 0)
	{
		// allocate a BA session
		pBAEntry = BATableAllocOriEntry(pAd, &Idx);
		if (pBAEntry == NULL)
		{
			DBGPRINT(RT_DEBUG_TRACE,("ADDBA - MlmeADDBAAction() allocate BA session failed \n"));
			return;
		}
	}
	else
	{
		pBAEntry =&pAd->BATable.BAOriEntry[Idx];
	}

	if (pBAEntry->ORI_BA_Status >= Originator_WaitRes)
	{
		return;
	}

	pEntry->BAOriWcidArray[TID] = Idx;

	// Initialize BA session
	pBAEntry->ORI_BA_Status = Originator_WaitRes;
	pBAEntry->Wcid = pEntry->Aid;
	pBAEntry->BAWinSize = pAd->CommonCfg.BACapability.field.RxBAWinLimit;
	pBAEntry->Sequence = BA_ORI_INIT_SEQ;
	pBAEntry->Token = 1;	// (2008-01-21) Jan Lee recommends it - this token can't be 0
	pBAEntry->TID = TID;
	pBAEntry->TimeOutValue = TimeOut;
	pBAEntry->pAdapter = pAd;

	if (!(pEntry->TXBAbitmap & (1<<TID)))
	{
		RTMPInitTimer(pAd, &pBAEntry->ORIBATimer, GET_TIMER_FUNCTION(BAOriSessionSetupTimeout), pBAEntry, FALSE);
	}
	else
		RTMPCancelTimer(&pBAEntry->ORIBATimer, &Cancelled);

	// set timer to send ADDBA request
	RTMPSetTimer(&pBAEntry->ORIBATimer, DelayTime);
}

VOID BAOriSessionAdd(
			IN PRTMP_ADAPTER    pAd,
					IN MAC_TABLE_ENTRY  *pEntry,
			IN PFRAME_ADDBA_RSP pFrame)
{
	BA_ORI_ENTRY  *pBAEntry = NULL;
	BOOLEAN       Cancelled;
	UCHAR         TID;
	USHORT        Idx;
	PUCHAR          pOutBuffer2 = NULL;
	NDIS_STATUS     NStatus;
	ULONG           FrameLen;
	FRAME_BAR       FrameBar;

	TID = pFrame->BaParm.TID;
	Idx = pEntry->BAOriWcidArray[TID];
	pBAEntry =&pAd->BATable.BAOriEntry[Idx];

	// Start fill in parameters.
	if ((Idx !=0) && (pBAEntry->TID == TID) && (pBAEntry->ORI_BA_Status == Originator_WaitRes))
	{
		pBAEntry->BAWinSize = min(pBAEntry->BAWinSize, ((UCHAR)pFrame->BaParm.BufSize));
		BA_MaxWinSizeReasign(pAd, pEntry, &pBAEntry->BAWinSize);

		pBAEntry->TimeOutValue = pFrame->TimeOutValue;
		pBAEntry->ORI_BA_Status = Originator_Done;
		// reset sequence number
		pBAEntry->Sequence = BA_ORI_INIT_SEQ;
		// Set Bitmap flag.
		pEntry->TXBAbitmap |= (1<<TID);
				RTMPCancelTimer(&pBAEntry->ORIBATimer, &Cancelled);

		pBAEntry->ORIBATimer.TimerValue = 0;	//pFrame->TimeOutValue;

		DBGPRINT(RT_DEBUG_TRACE,("%s : TXBAbitmap = %x, BAWinSize = %d, TimeOut = %ld\n", __func__, pEntry->TXBAbitmap,
								 pBAEntry->BAWinSize, pBAEntry->ORIBATimer.TimerValue));

		// SEND BAR ;
		NStatus = MlmeAllocateMemory(pAd, &pOutBuffer2);  //Get an unused nonpaged memory
		if (NStatus != NDIS_STATUS_SUCCESS)
		{
			DBGPRINT(RT_DEBUG_TRACE,("BA - BAOriSessionAdd() allocate memory failed \n"));
			return;
		}

		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
			BarHeaderInit(pAd, &FrameBar, pAd->MacTab.Content[pBAEntry->Wcid].Addr, pAd->CurrentAddress);

		FrameBar.StartingSeq.field.FragNum = 0;	// make sure sequence not clear in DEL function.
		FrameBar.StartingSeq.field.StartSeq = pBAEntry->Sequence; // make sure sequence not clear in DEL funciton.
		FrameBar.BarControl.TID = pBAEntry->TID; // make sure sequence not clear in DEL funciton.
		MakeOutgoingFrame(pOutBuffer2,              &FrameLen,
						  sizeof(FRAME_BAR),      &FrameBar,
					  END_OF_ARGS);
		MiniportMMRequest(pAd, QID_AC_BE, pOutBuffer2, FrameLen);
		MlmeFreeMemory(pAd, pOutBuffer2);


		if (pBAEntry->ORIBATimer.TimerValue)
			RTMPSetTimer(&pBAEntry->ORIBATimer, pBAEntry->ORIBATimer.TimerValue); // in mSec
	}
}

BOOLEAN BARecSessionAdd(
					   IN PRTMP_ADAPTER    pAd,
					   IN MAC_TABLE_ENTRY  *pEntry,
					   IN PFRAME_ADDBA_REQ pFrame)
{
	BA_REC_ENTRY            *pBAEntry = NULL;
	BOOLEAN                 Status = TRUE;
	BOOLEAN                 Cancelled;
	USHORT                  Idx;
	UCHAR                   TID;
	UCHAR                   BAWinSize;
	//UINT32                  Value;
	//UINT                    offset;


	ASSERT(pEntry);

	// find TID
	TID = pFrame->BaParm.TID;

	BAWinSize = min(((UCHAR)pFrame->BaParm.BufSize), (UCHAR)pAd->CommonCfg.BACapability.field.RxBAWinLimit);

	// Intel patch
	if (BAWinSize == 0)
	{
		BAWinSize = 64;
	}

	Idx = pEntry->BARecWcidArray[TID];


	if (Idx == 0)
	{
		pBAEntry = BATableAllocRecEntry(pAd, &Idx);
	}
	else
	{
		pBAEntry = &pAd->BATable.BARecEntry[Idx];
		// flush all pending reordering mpdus
		ba_refresh_reordering_mpdus(pAd, pBAEntry);
	}

	DBGPRINT(RT_DEBUG_TRACE,("%s(%ld): Idx = %d, BAWinSize(req %d) = %d\n", __func__, pAd->BATable.numAsRecipient, Idx,
							 pFrame->BaParm.BufSize, BAWinSize));

	// Start fill in parameters.
	if (pBAEntry != NULL)
	{
		ASSERT(pBAEntry->list.qlen == 0);

		pBAEntry->REC_BA_Status = Recipient_HandleRes;
		pBAEntry->BAWinSize = BAWinSize;
		pBAEntry->Wcid = pEntry->Aid;
		pBAEntry->TID = TID;
		pBAEntry->TimeOutValue = pFrame->TimeOutValue;
		pBAEntry->REC_BA_Status = Recipient_Accept;
		// initial sequence number
		pBAEntry->LastIndSeq = RESET_RCV_SEQ; //pFrame->BaStartSeq.field.StartSeq;

		printk("Start Seq = %08x\n",  pFrame->BaStartSeq.field.StartSeq);

		if (pEntry->RXBAbitmap & (1<<TID))
		{
			RTMPCancelTimer(&pBAEntry->RECBATimer, &Cancelled);
		}
		else
		{
			RTMPInitTimer(pAd, &pBAEntry->RECBATimer, GET_TIMER_FUNCTION(BARecSessionIdleTimeout), pBAEntry, TRUE);
		}

#if 0	// for debugging
		RTMPSetTimer(&pBAEntry->RECBATimer, REC_BA_SESSION_IDLE_TIMEOUT);
#endif

		// Set Bitmap flag.
		pEntry->RXBAbitmap |= (1<<TID);
		pEntry->BARecWcidArray[TID] = Idx;

		pEntry->BADeclineBitmap &= ~(1<<TID);

		// Set BA session mask in WCID table.
		RT28XX_ADD_BA_SESSION_TO_ASIC(pAd, pEntry->Aid, TID);

		DBGPRINT(RT_DEBUG_TRACE,("MACEntry[%d]RXBAbitmap = 0x%x. BARecWcidArray=%d\n",
				pEntry->Aid, pEntry->RXBAbitmap, pEntry->BARecWcidArray[TID]));
	}
	else
	{
		Status = FALSE;
		DBGPRINT(RT_DEBUG_TRACE,("Can't Accept ADDBA for %02x:%02x:%02x:%02x:%02x:%02x TID = %d\n",
				PRINT_MAC(pEntry->Addr), TID));
	}
	return(Status);
}


BA_REC_ENTRY *BATableAllocRecEntry(
								  IN  PRTMP_ADAPTER   pAd,
								  OUT USHORT          *Idx)
{
	int             i;
	BA_REC_ENTRY    *pBAEntry = NULL;


	NdisAcquireSpinLock(&pAd->BATabLock);

	if (pAd->BATable.numAsRecipient >= MAX_BARECI_SESSION)
	{
		printk("BA Recipeint Session (%ld) > %d\n", pAd->BATable.numAsRecipient,
			MAX_BARECI_SESSION);
		goto done;
	}

	// reserve idx 0 to identify BAWcidArray[TID] as empty
	for (i=1; i < MAX_LEN_OF_BA_REC_TABLE; i++)
	{
		pBAEntry =&pAd->BATable.BARecEntry[i];
		if ((pBAEntry->REC_BA_Status == Recipient_NONE))
		{
			// get one
			pAd->BATable.numAsRecipient++;
			pBAEntry->REC_BA_Status = Recipient_USED;
			*Idx = i;
			break;
		}
	}

done:
	NdisReleaseSpinLock(&pAd->BATabLock);
	return pBAEntry;
}

BA_ORI_ENTRY *BATableAllocOriEntry(
								  IN  PRTMP_ADAPTER   pAd,
								  OUT USHORT          *Idx)
{
	int             i;
	BA_ORI_ENTRY    *pBAEntry = NULL;

	NdisAcquireSpinLock(&pAd->BATabLock);

	if (pAd->BATable.numAsOriginator >= (MAX_LEN_OF_BA_ORI_TABLE))
	{
		goto done;
	}

	// reserve idx 0 to identify BAWcidArray[TID] as empty
	for (i=1; i<MAX_LEN_OF_BA_ORI_TABLE; i++)
	{
		pBAEntry =&pAd->BATable.BAOriEntry[i];
		if ((pBAEntry->ORI_BA_Status == Originator_NONE))
		{
			// get one
			pAd->BATable.numAsOriginator++;
			pBAEntry->ORI_BA_Status = Originator_USED;
			pBAEntry->pAdapter = pAd;
			*Idx = i;
			break;
		}
	}

done:
	NdisReleaseSpinLock(&pAd->BATabLock);
	return pBAEntry;
}


VOID BATableFreeOriEntry(
						IN  PRTMP_ADAPTER   pAd,
						IN  ULONG           Idx)
{
	BA_ORI_ENTRY    *pBAEntry = NULL;
	MAC_TABLE_ENTRY *pEntry;


	if ((Idx == 0) || (Idx >= MAX_LEN_OF_BA_ORI_TABLE))
		return;

	pBAEntry =&pAd->BATable.BAOriEntry[Idx];

	if (pBAEntry->ORI_BA_Status != Originator_NONE)
	{
		pEntry = &pAd->MacTab.Content[pBAEntry->Wcid];
		pEntry->BAOriWcidArray[pBAEntry->TID] = 0;


		NdisAcquireSpinLock(&pAd->BATabLock);
		if (pBAEntry->ORI_BA_Status == Originator_Done)
		{
		 	pEntry->TXBAbitmap &= (~(1<<(pBAEntry->TID) ));
			DBGPRINT(RT_DEBUG_TRACE, ("BATableFreeOriEntry numAsOriginator= %ld\n", pAd->BATable.numAsRecipient));
			// Erase Bitmap flag.
		}

		ASSERT(pAd->BATable.numAsOriginator != 0);

		pAd->BATable.numAsOriginator -= 1;

		pBAEntry->ORI_BA_Status = Originator_NONE;
		pBAEntry->Token = 0;
		NdisReleaseSpinLock(&pAd->BATabLock);
	}
}


VOID BATableFreeRecEntry(
						IN  PRTMP_ADAPTER   pAd,
						IN  ULONG           Idx)
{
	BA_REC_ENTRY    *pBAEntry = NULL;
	MAC_TABLE_ENTRY *pEntry;


	if ((Idx == 0) || (Idx >= MAX_LEN_OF_BA_REC_TABLE))
		return;

	pBAEntry =&pAd->BATable.BARecEntry[Idx];

	if (pBAEntry->REC_BA_Status != Recipient_NONE)
	{
		pEntry = &pAd->MacTab.Content[pBAEntry->Wcid];
		pEntry->BARecWcidArray[pBAEntry->TID] = 0;

		NdisAcquireSpinLock(&pAd->BATabLock);

		ASSERT(pAd->BATable.numAsRecipient != 0);

		pAd->BATable.numAsRecipient -= 1;

		pBAEntry->REC_BA_Status = Recipient_NONE;
		NdisReleaseSpinLock(&pAd->BATabLock);
	}
}


VOID BAOriSessionTearDown(
						 IN OUT  PRTMP_ADAPTER   pAd,
						 IN      UCHAR           Wcid,
						 IN      UCHAR           TID,
						 IN      BOOLEAN         bPassive,
						 IN      BOOLEAN         bForceSend)
{
	ULONG           Idx = 0;
	BA_ORI_ENTRY    *pBAEntry;
	BOOLEAN         Cancelled;

	if (Wcid >= MAX_LEN_OF_MAC_TABLE)
	{
		return;
	}

	//
	// Locate corresponding BA Originator Entry in BA Table with the (pAddr,TID).
	//
	Idx = pAd->MacTab.Content[Wcid].BAOriWcidArray[TID];
	if ((Idx == 0) || (Idx >= MAX_LEN_OF_BA_ORI_TABLE))
	{
		if (bForceSend == TRUE)
		{
			// force send specified TID DelBA
			MLME_DELBA_REQ_STRUCT   DelbaReq;
			MLME_QUEUE_ELEM *Elem = (MLME_QUEUE_ELEM *) kmalloc(sizeof(MLME_QUEUE_ELEM), MEM_ALLOC_FLAG);

			NdisZeroMemory(&DelbaReq, sizeof(DelbaReq));
			NdisZeroMemory(Elem, sizeof(MLME_QUEUE_ELEM));

			COPY_MAC_ADDR(DelbaReq.Addr, pAd->MacTab.Content[Wcid].Addr);
			DelbaReq.Wcid = Wcid;
			DelbaReq.TID = TID;
			DelbaReq.Initiator = ORIGINATOR;
#if 1
			Elem->MsgLen  = sizeof(DelbaReq);
			NdisMoveMemory(Elem->Msg, &DelbaReq, sizeof(DelbaReq));
			MlmeDELBAAction(pAd, Elem);
			kfree(Elem);
#else
			MlmeEnqueue(pAd, ACTION_STATE_MACHINE, MT2_MLME_ORI_DELBA_CATE, sizeof(MLME_DELBA_REQ_STRUCT), (PVOID)&DelbaReq);
			RT28XX_MLME_HANDLER(pAd);
#endif
		}

		return;
	}

	DBGPRINT(RT_DEBUG_TRACE,("%s===>Wcid=%d.TID=%d \n", __func__, Wcid, TID));

	pBAEntry = &pAd->BATable.BAOriEntry[Idx];
	DBGPRINT(RT_DEBUG_TRACE,("\t===>Idx = %ld, Wcid=%d.TID=%d, ORI_BA_Status = %d \n", Idx, Wcid, TID, pBAEntry->ORI_BA_Status));
	//
	// Prepare DelBA action frame and send to the peer.
	//
	if ((bPassive == FALSE) && (TID == pBAEntry->TID) && (pBAEntry->ORI_BA_Status == Originator_Done))
	{
		MLME_DELBA_REQ_STRUCT   DelbaReq;
		MLME_QUEUE_ELEM *Elem = (MLME_QUEUE_ELEM *) kmalloc(sizeof(MLME_QUEUE_ELEM), MEM_ALLOC_FLAG);

		NdisZeroMemory(&DelbaReq, sizeof(DelbaReq));
		NdisZeroMemory(Elem, sizeof(MLME_QUEUE_ELEM));

		COPY_MAC_ADDR(DelbaReq.Addr, pAd->MacTab.Content[Wcid].Addr);
		DelbaReq.Wcid = Wcid;
		DelbaReq.TID = pBAEntry->TID;
		DelbaReq.Initiator = ORIGINATOR;
#if 1
		Elem->MsgLen  = sizeof(DelbaReq);
		NdisMoveMemory(Elem->Msg, &DelbaReq, sizeof(DelbaReq));
		MlmeDELBAAction(pAd, Elem);
		kfree(Elem);
#else
		MlmeEnqueue(pAd, ACTION_STATE_MACHINE, MT2_MLME_ORI_DELBA_CATE, sizeof(MLME_DELBA_REQ_STRUCT), (PVOID)&DelbaReq);
		RT28XX_MLME_HANDLER(pAd);
#endif
	}
	RTMPCancelTimer(&pBAEntry->ORIBATimer, &Cancelled);
	BATableFreeOriEntry(pAd, Idx);

	if (bPassive)
	{
		//BAOriSessionSetUp(pAd, &pAd->MacTab.Content[Wcid], TID, 0, 10000, TRUE);
	}
}

VOID BARecSessionTearDown(
						 IN OUT  PRTMP_ADAPTER   pAd,
						 IN      UCHAR           Wcid,
						 IN      UCHAR           TID,
						 IN      BOOLEAN         bPassive)
{
	ULONG           Idx = 0;
	BA_REC_ENTRY    *pBAEntry;

	if (Wcid >= MAX_LEN_OF_MAC_TABLE)
	{
		return;
	}

	//
	//  Locate corresponding BA Originator Entry in BA Table with the (pAddr,TID).
	//
	Idx = pAd->MacTab.Content[Wcid].BARecWcidArray[TID];
	if (Idx == 0)
		return;

	DBGPRINT(RT_DEBUG_TRACE,("%s===>Wcid=%d.TID=%d \n", __func__, Wcid, TID));


	pBAEntry = &pAd->BATable.BARecEntry[Idx];
	DBGPRINT(RT_DEBUG_TRACE,("\t===>Idx = %ld, Wcid=%d.TID=%d, REC_BA_Status = %d \n", Idx, Wcid, TID, pBAEntry->REC_BA_Status));
	//
	// Prepare DelBA action frame and send to the peer.
	//
	if ((TID == pBAEntry->TID) && (pBAEntry->REC_BA_Status == Recipient_Accept))
	{
		MLME_DELBA_REQ_STRUCT   DelbaReq;
		BOOLEAN 				Cancelled;
		MLME_QUEUE_ELEM *Elem = (MLME_QUEUE_ELEM *) kmalloc(sizeof(MLME_QUEUE_ELEM), MEM_ALLOC_FLAG);
		//ULONG   offset;
		//UINT32  VALUE;

		RTMPCancelTimer(&pBAEntry->RECBATimer, &Cancelled);

		//
		// 1. Send DELBA Action Frame
		//
		if (bPassive == FALSE)
		{
			NdisZeroMemory(&DelbaReq, sizeof(DelbaReq));
			NdisZeroMemory(Elem, sizeof(MLME_QUEUE_ELEM));

			COPY_MAC_ADDR(DelbaReq.Addr, pAd->MacTab.Content[Wcid].Addr);
			DelbaReq.Wcid = Wcid;
			DelbaReq.TID = TID;
			DelbaReq.Initiator = RECIPIENT;
#if 1
			Elem->MsgLen  = sizeof(DelbaReq);
			NdisMoveMemory(Elem->Msg, &DelbaReq, sizeof(DelbaReq));
			MlmeDELBAAction(pAd, Elem);
			kfree(Elem);
#else
			MlmeEnqueue(pAd, ACTION_STATE_MACHINE, MT2_MLME_ORI_DELBA_CATE, sizeof(MLME_DELBA_REQ_STRUCT), (PVOID)&DelbaReq);
			RT28XX_MLME_HANDLER(pAd);
#endif
		}


		//
		// 2. Free resource of BA session
		//
		// flush all pending reordering mpdus
		ba_refresh_reordering_mpdus(pAd, pBAEntry);

		NdisAcquireSpinLock(&pAd->BATabLock);

		// Erase Bitmap flag.
		pBAEntry->LastIndSeq = RESET_RCV_SEQ;
		pBAEntry->BAWinSize = 0;
		// Erase Bitmap flag at software mactable
		pAd->MacTab.Content[Wcid].RXBAbitmap &= (~(1<<(pBAEntry->TID)));
		pAd->MacTab.Content[Wcid].BARecWcidArray[TID] = 0;

		RT28XX_DEL_BA_SESSION_FROM_ASIC(pAd, Wcid, TID);

		NdisReleaseSpinLock(&pAd->BATabLock);

	}

	BATableFreeRecEntry(pAd, Idx);
}

VOID BASessionTearDownALL(
						 IN OUT  PRTMP_ADAPTER pAd,
						 IN      UCHAR Wcid)
{
	int i;

	for (i=0; i<NUM_OF_TID; i++)
	{
		BAOriSessionTearDown(pAd, Wcid, i, FALSE, FALSE);
		BARecSessionTearDown(pAd, Wcid, i, FALSE);
	}
}


/*
	==========================================================================
	Description:
		Retry sending ADDBA Reqest.

	IRQL = DISPATCH_LEVEL

	Parametrs:
	p8023Header: if this is already 802.3 format, p8023Header is NULL

	Return	: TRUE if put into rx reordering buffer, shouldn't indicaterxhere.
				FALSE , then continue indicaterx at this moment.
	==========================================================================
 */
VOID BAOriSessionSetupTimeout(
    IN PVOID SystemSpecific1,
    IN PVOID FunctionContext,
    IN PVOID SystemSpecific2,
    IN PVOID SystemSpecific3)
{
	BA_ORI_ENTRY    *pBAEntry = (BA_ORI_ENTRY *)FunctionContext;
	MAC_TABLE_ENTRY *pEntry;
	PRTMP_ADAPTER   pAd;

	if (pBAEntry == NULL)
		return;

	pAd = pBAEntry->pAdapter;

	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		// Do nothing if monitor mode is on
		if (MONITOR_ON(pAd))
			return;
	}

	pEntry = &pAd->MacTab.Content[pBAEntry->Wcid];

	if ((pBAEntry->ORI_BA_Status == Originator_WaitRes) && (pBAEntry->Token < ORI_SESSION_MAX_RETRY))
	{
		MLME_ADDBA_REQ_STRUCT    AddbaReq;

		NdisZeroMemory(&AddbaReq, sizeof(AddbaReq));
		COPY_MAC_ADDR(AddbaReq.pAddr, pEntry->Addr);
		AddbaReq.Wcid = (UCHAR)(pEntry->Aid);
		AddbaReq.TID = pBAEntry->TID;
		AddbaReq.BaBufSize = pAd->CommonCfg.BACapability.field.RxBAWinLimit;
		AddbaReq.TimeOutValue = 0;
		AddbaReq.Token = pBAEntry->Token;
		MlmeEnqueue(pAd, ACTION_STATE_MACHINE, MT2_MLME_ADD_BA_CATE, sizeof(MLME_ADDBA_REQ_STRUCT), (PVOID)&AddbaReq);
		RT28XX_MLME_HANDLER(pAd);
		DBGPRINT(RT_DEBUG_TRACE,("BA Ori Session Timeout(%d) : Send ADD BA again\n", pBAEntry->Token));

		pBAEntry->Token++;
		RTMPSetTimer(&pBAEntry->ORIBATimer, ORI_BA_SESSION_TIMEOUT);
	}
	else
	{
		BATableFreeOriEntry(pAd, pEntry->BAOriWcidArray[pBAEntry->TID]);
	}
}

/*
	==========================================================================
	Description:
		Retry sending ADDBA Reqest.

	IRQL = DISPATCH_LEVEL

	Parametrs:
	p8023Header: if this is already 802.3 format, p8023Header is NULL

	Return	: TRUE if put into rx reordering buffer, shouldn't indicaterxhere.
				FALSE , then continue indicaterx at this moment.
	==========================================================================
 */
VOID BARecSessionIdleTimeout(
    IN PVOID SystemSpecific1,
    IN PVOID FunctionContext,
    IN PVOID SystemSpecific2,
    IN PVOID SystemSpecific3)
{

	BA_REC_ENTRY    *pBAEntry = (BA_REC_ENTRY *)FunctionContext;
	PRTMP_ADAPTER   pAd;
	ULONG           Now32;

	if (pBAEntry == NULL)
		return;

	if ((pBAEntry->REC_BA_Status == Recipient_Accept))
	{
		NdisGetSystemUpTime(&Now32);

		if (RTMP_TIME_AFTER((unsigned long)Now32, (unsigned long)(pBAEntry->LastIndSeqAtTimer + REC_BA_SESSION_IDLE_TIMEOUT)))
		{
			pAd = pBAEntry->pAdapter;
			// flush all pending reordering mpdus
			ba_refresh_reordering_mpdus(pAd, pBAEntry);
			printk("%ld: REC BA session Timeout\n", Now32);
		}
	}
}


VOID PeerAddBAReqAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)

{
	//	7.4.4.1
	//ULONG	Idx;
	UCHAR   Status = 1;
	UCHAR   pAddr[6];
	FRAME_ADDBA_RSP ADDframe;
	PUCHAR         pOutBuffer = NULL;
	NDIS_STATUS     NStatus;
	PFRAME_ADDBA_REQ  pAddreqFrame = NULL;
	//UCHAR		BufSize;
	ULONG       FrameLen;
	PULONG      ptemp;
	PMAC_TABLE_ENTRY	pMacEntry;

	DBGPRINT(RT_DEBUG_TRACE, ("%s ==> (Wcid = %d)\n", __func__, Elem->Wcid));

	//hex_dump("AddBAReq", Elem->Msg, Elem->MsgLen);

	//ADDBA Request from unknown peer, ignore this.
	if (Elem->Wcid >= MAX_LEN_OF_MAC_TABLE)
		return;

	pMacEntry = &pAd->MacTab.Content[Elem->Wcid];
	DBGPRINT(RT_DEBUG_TRACE,("BA - PeerAddBAReqAction----> \n"));
	ptemp = (PULONG)Elem->Msg;
	//DBGPRINT_RAW(RT_DEBUG_EMU, ("%08x:: %08x:: %08x:: %08x:: %08x:: %08x:: %08x:: %08x:: %08x\n", *(ptemp), *(ptemp+1), *(ptemp+2), *(ptemp+3), *(ptemp+4), *(ptemp+5), *(ptemp+6), *(ptemp+7), *(ptemp+8)));

	if (PeerAddBAReqActionSanity(pAd, Elem->Msg, Elem->MsgLen, pAddr))
	{

		if ((pAd->CommonCfg.bBADecline == FALSE) && IS_HT_STA(pMacEntry))
		{
			pAddreqFrame = (PFRAME_ADDBA_REQ)(&Elem->Msg[0]);
			printk("Rcv Wcid(%d) AddBAReq\n", Elem->Wcid);
			if (BARecSessionAdd(pAd, &pAd->MacTab.Content[Elem->Wcid], pAddreqFrame))
				Status = 0;
			else
				Status = 38; // more parameters have invalid values
		}
		else
		{
			Status = 37; // the request has been declined.
		}
	}

	if (pAd->MacTab.Content[Elem->Wcid].ValidAsCLI)
		ASSERT(pAd->MacTab.Content[Elem->Wcid].Sst == SST_ASSOC);

	pAddreqFrame = (PFRAME_ADDBA_REQ)(&Elem->Msg[0]);
	// 2. Always send back ADDBA Response
	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);	 //Get an unused nonpaged memory
	if (NStatus != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT(RT_DEBUG_TRACE,("ACTION - PeerBAAction() allocate memory failed \n"));
		return;
	}

	NdisZeroMemory(&ADDframe, sizeof(FRAME_ADDBA_RSP));

	// 2-1. Prepare ADDBA Response frame.
	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
	{
		if (ADHOC_ON(pAd))
			ActHeaderInit(pAd, &ADDframe.Hdr, pAddr, pAd->CurrentAddress, pAd->CommonCfg.Bssid);
		else
			ActHeaderInit(pAd, &ADDframe.Hdr, pAd->CommonCfg.Bssid, pAd->CurrentAddress, pAddr);
	}

	ADDframe.Category = CATEGORY_BA;
	ADDframe.Action = ADDBA_RESP;
	ADDframe.Token = pAddreqFrame->Token;
	// What is the Status code??  need to check.
	ADDframe.StatusCode = Status;
	ADDframe.BaParm.BAPolicy = IMMED_BA;
	ADDframe.BaParm.AMSDUSupported = 0;
	ADDframe.BaParm.TID = pAddreqFrame->BaParm.TID;
	ADDframe.BaParm.BufSize = min(((UCHAR)pAddreqFrame->BaParm.BufSize), (UCHAR)pAd->CommonCfg.BACapability.field.RxBAWinLimit);
	if (ADDframe.BaParm.BufSize == 0)
	{
		ADDframe.BaParm.BufSize = 64;
	}
	ADDframe.TimeOutValue = 0; //pAddreqFrame->TimeOutValue;

	*(USHORT *)(&ADDframe.BaParm) = cpu2le16(*(USHORT *)(&ADDframe.BaParm));
	ADDframe.StatusCode = cpu2le16(ADDframe.StatusCode);
	ADDframe.TimeOutValue = cpu2le16(ADDframe.TimeOutValue);

	MakeOutgoingFrame(pOutBuffer,               &FrameLen,
					  sizeof(FRAME_ADDBA_RSP),  &ADDframe,
			  END_OF_ARGS);
	MiniportMMRequest(pAd, QID_AC_BE, pOutBuffer, FrameLen);
	MlmeFreeMemory(pAd, pOutBuffer);

	DBGPRINT(RT_DEBUG_TRACE, ("%s(%d): TID(%d), BufSize(%d) <== \n", __func__, Elem->Wcid, ADDframe.BaParm.TID,
							  ADDframe.BaParm.BufSize));
}


VOID PeerAddBARspAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)

{
	//UCHAR		Idx, i;
	//PUCHAR		   pOutBuffer = NULL;
	PFRAME_ADDBA_RSP    pFrame = NULL;
	//PBA_ORI_ENTRY		pBAEntry;

	//ADDBA Response from unknown peer, ignore this.
	if (Elem->Wcid >= MAX_LEN_OF_MAC_TABLE)
		return;

	DBGPRINT(RT_DEBUG_TRACE, ("%s ==> Wcid(%d)\n", __func__, Elem->Wcid));

	//hex_dump("PeerAddBARspAction()", Elem->Msg, Elem->MsgLen);

	if (PeerAddBARspActionSanity(pAd, Elem->Msg, Elem->MsgLen))
	{
		pFrame = (PFRAME_ADDBA_RSP)(&Elem->Msg[0]);

		DBGPRINT(RT_DEBUG_TRACE, ("\t\t StatusCode = %d\n", pFrame->StatusCode));
		switch (pFrame->StatusCode)
		{
			case 0:
				// I want a BAsession with this peer as an originator.
				BAOriSessionAdd(pAd, &pAd->MacTab.Content[Elem->Wcid], pFrame);
				break;
			default:
				// check status == USED ???
				BAOriSessionTearDown(pAd, Elem->Wcid, pFrame->BaParm.TID, TRUE, FALSE);
				break;
		}
		// Rcv Decline StatusCode
		if ((pFrame->StatusCode == 37)
            || ((pAd->OpMode == OPMODE_STA) && STA_TGN_WIFI_ON(pAd) && (pFrame->StatusCode != 0))
            )
		{
			pAd->MacTab.Content[Elem->Wcid].BADeclineBitmap |= 1<<pFrame->BaParm.TID;
		}
	}
}

VOID PeerDelBAAction(
	IN PRTMP_ADAPTER pAd,
	IN MLME_QUEUE_ELEM *Elem)

{
	//UCHAR				Idx;
	//PUCHAR				pOutBuffer = NULL;
	PFRAME_DELBA_REQ    pDelFrame = NULL;

	DBGPRINT(RT_DEBUG_TRACE,("%s ==>\n", __func__));
	//DELBA Request from unknown peer, ignore this.
	if (PeerDelBAActionSanity(pAd, Elem->Wcid, Elem->Msg, Elem->MsgLen))
	{
		pDelFrame = (PFRAME_DELBA_REQ)(&Elem->Msg[0]);
		if (pDelFrame->DelbaParm.Initiator == ORIGINATOR)
		{
			DBGPRINT(RT_DEBUG_TRACE,("BA - PeerDelBAAction----> ORIGINATOR\n"));
			BARecSessionTearDown(pAd, Elem->Wcid, pDelFrame->DelbaParm.TID, TRUE);
		}
		else
		{
			DBGPRINT(RT_DEBUG_TRACE,("BA - PeerDelBAAction----> RECIPIENT, Reason = %d\n",  pDelFrame->ReasonCode));
			//hex_dump("DelBA Frame", pDelFrame, Elem->MsgLen);
			BAOriSessionTearDown(pAd, Elem->Wcid, pDelFrame->DelbaParm.TID, TRUE, FALSE);
		}
	}
}


BOOLEAN CntlEnqueueForRecv(
						  IN PRTMP_ADAPTER		pAd,
						  IN ULONG				Wcid,
						  IN ULONG				MsgLen,
						  IN PFRAME_BA_REQ		pMsg)
{
	PFRAME_BA_REQ   pFrame = pMsg;
	//PRTMP_REORDERBUF	pBuffer;
	//PRTMP_REORDERBUF	pDmaBuf;
	PBA_REC_ENTRY pBAEntry;
	//BOOLEAN 	Result;
	ULONG   Idx;
	//UCHAR	NumRxPkt;
	UCHAR	TID;//, i;

	TID = (UCHAR)pFrame->BARControl.TID;

	DBGPRINT(RT_DEBUG_TRACE, ("%s(): BAR-Wcid(%ld), Tid (%d)\n", __func__, Wcid, TID));
	//hex_dump("BAR", (PCHAR) pFrame, MsgLen);
	// Do nothing if the driver is starting halt state.
	// This might happen when timer already been fired before cancel timer with mlmehalt
	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS | fRTMP_ADAPTER_NIC_NOT_EXIST))
		return FALSE;

	// First check the size, it MUST not exceed the mlme queue size
	if (MsgLen > MGMT_DMA_BUFFER_SIZE)
	{
		DBGPRINT_ERR(("CntlEnqueueForRecv: frame too large, size = %ld \n", MsgLen));
		return FALSE;
	}
	else if (MsgLen != sizeof(FRAME_BA_REQ))
	{
		DBGPRINT_ERR(("CntlEnqueueForRecv: BlockAck Request frame length size = %ld incorrect\n", MsgLen));
		return FALSE;
	}
	else if (MsgLen != sizeof(FRAME_BA_REQ))
	{
		DBGPRINT_ERR(("CntlEnqueueForRecv: BlockAck Request frame length size = %ld incorrect\n", MsgLen));
		return FALSE;
	}

	if ((Wcid < MAX_LEN_OF_MAC_TABLE) && (TID < 8))
		{
		// if this receiving packet is from SA that is in our OriEntry. Since WCID <9 has direct mapping. no need search.
		Idx = pAd->MacTab.Content[Wcid].BARecWcidArray[TID];
		pBAEntry = &pAd->BATable.BARecEntry[Idx];
		}
		else
		{
		return FALSE;
	}

	DBGPRINT(RT_DEBUG_TRACE, ("BAR(%ld) : Tid (%d) - %04x:%04x\n", Wcid, TID, pFrame->BAStartingSeq.field.StartSeq, pBAEntry->LastIndSeq ));

	if (SEQ_SMALLER(pBAEntry->LastIndSeq, pFrame->BAStartingSeq.field.StartSeq, MAXSEQ))
	{
		//printk("BAR Seq = %x, LastIndSeq = %x\n", pFrame->BAStartingSeq.field.StartSeq, pBAEntry->LastIndSeq);
		ba_indicate_reordering_mpdus_le_seq(pAd, pBAEntry, pFrame->BAStartingSeq.field.StartSeq);
		pBAEntry->LastIndSeq = (pFrame->BAStartingSeq.field.StartSeq == 0) ? MAXSEQ :(pFrame->BAStartingSeq.field.StartSeq -1);
	}
	//ba_refresh_reordering_mpdus(pAd, pBAEntry);
	return TRUE;
}

/*
Description : Send PSMP Action frame If PSMP mode switches.
*/
VOID SendPSMPAction(
				   IN PRTMP_ADAPTER		pAd,
				   IN UCHAR				Wcid,
				   IN UCHAR				Psmp)
{
	PUCHAR          pOutBuffer = NULL;
	NDIS_STATUS     NStatus;
	//ULONG           Idx;
	FRAME_PSMP_ACTION   Frame;
	ULONG           FrameLen;

	NStatus = MlmeAllocateMemory(pAd, &pOutBuffer);	 //Get an unused nonpaged memory
	if (NStatus != NDIS_STATUS_SUCCESS)
	{
		DBGPRINT(RT_DEBUG_ERROR,("BA - MlmeADDBAAction() allocate memory failed \n"));
		return;
	}

	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		ActHeaderInit(pAd, &Frame.Hdr, pAd->CommonCfg.Bssid, pAd->CurrentAddress, pAd->MacTab.Content[Wcid].Addr);

	Frame.Category = CATEGORY_HT;
	Frame.Action = SMPS_ACTION;
	switch (Psmp)
	{
		case MMPS_ENABLE:
			Frame.Psmp = 0;
			break;
		case MMPS_DYNAMIC:
			Frame.Psmp = 3;
			break;
		case MMPS_STATIC:
			Frame.Psmp = 1;
			break;
	}
	MakeOutgoingFrame(pOutBuffer,               &FrameLen,
					  sizeof(FRAME_PSMP_ACTION),      &Frame,
					  END_OF_ARGS);
	MiniportMMRequest(pAd, QID_AC_BE, pOutBuffer, FrameLen);
	MlmeFreeMemory(pAd, pOutBuffer);
	DBGPRINT(RT_DEBUG_ERROR,("HT - SendPSMPAction( %d )  \n", Frame.Psmp));
}


#define RADIO_MEASUREMENT_REQUEST_ACTION	0

typedef struct PACKED
{
	UCHAR	RegulatoryClass;
	UCHAR	ChannelNumber;
	USHORT	RandomInterval;
	USHORT	MeasurementDuration;
	UCHAR	MeasurementMode;
	UCHAR   BSSID[MAC_ADDR_LEN];
	UCHAR	ReportingCondition;
	UCHAR	Threshold;
	UCHAR   SSIDIE[2];			// 2 byte
} BEACON_REQUEST;

typedef struct PACKED
{
	UCHAR	ID;
	UCHAR	Length;
	UCHAR	Token;
	UCHAR	RequestMode;
	UCHAR	Type;
} MEASUREMENT_REQ;




void convert_reordering_packet_to_preAMSDU_or_802_3_packet(
	IN	PRTMP_ADAPTER	pAd,
	IN	RX_BLK			*pRxBlk,
	IN  UCHAR			FromWhichBSSID)
{
	PNDIS_PACKET	pRxPkt;
	UCHAR			Header802_3[LENGTH_802_3];

	// 1. get 802.3 Header
	// 2. remove LLC
	// 		a. pointer pRxBlk->pData to payload
	//      b. modify pRxBlk->DataSize

	IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		RTMP_802_11_REMOVE_LLC_AND_CONVERT_TO_802_3(pRxBlk, Header802_3);

	ASSERT(pRxBlk->pRxPacket);
	pRxPkt = RTPKT_TO_OSPKT(pRxBlk->pRxPacket);

	RTPKT_TO_OSPKT(pRxPkt)->dev = get_netdev_from_bssid(pAd, FromWhichBSSID);
	RTPKT_TO_OSPKT(pRxPkt)->data = pRxBlk->pData;
	RTPKT_TO_OSPKT(pRxPkt)->len = pRxBlk->DataSize;
	RTPKT_TO_OSPKT(pRxPkt)->tail = RTPKT_TO_OSPKT(pRxPkt)->data + RTPKT_TO_OSPKT(pRxPkt)->len;

	//
	// copy 802.3 header, if necessary
	//
	if (!RX_BLK_TEST_FLAG(pRxBlk, fRX_AMSDU))
	{
		IF_DEV_CONFIG_OPMODE_ON_STA(pAd)
		{
#ifdef LINUX
			NdisMoveMemory(skb_push(pRxPkt, LENGTH_802_3), Header802_3, LENGTH_802_3);
#endif
		}
	}
}


#define INDICATE_LEGACY_OR_AMSDU(_pAd, _pRxBlk, _fromWhichBSSID)		\
	do																	\
	{																	\
    	if (RX_BLK_TEST_FLAG(_pRxBlk, fRX_AMSDU))						\
    	{																\
    		Indicate_AMSDU_Packet(_pAd, _pRxBlk, _fromWhichBSSID);		\
    	}																\
		else if (RX_BLK_TEST_FLAG(_pRxBlk, fRX_EAP))					\
		{																\
			Indicate_EAPOL_Packet(_pAd, _pRxBlk, _fromWhichBSSID);		\
		}																\
    	else															\
    	{																\
    		Indicate_Legacy_Packet(_pAd, _pRxBlk, _fromWhichBSSID);		\
    	}																\
	} while (0);



static VOID ba_enqueue_reordering_packet(
	IN	PRTMP_ADAPTER	pAd,
	IN	PBA_REC_ENTRY	pBAEntry,
	IN	RX_BLK			*pRxBlk,
	IN	UCHAR			FromWhichBSSID)
{
	struct reordering_mpdu *mpdu_blk;
	UINT16	Sequence = (UINT16) pRxBlk->pHeader->Sequence;

	mpdu_blk = ba_mpdu_blk_alloc(pAd);
	if (mpdu_blk != NULL)
	{
		// Write RxD buffer address & allocated buffer length
		NdisAcquireSpinLock(&pBAEntry->RxReRingLock);

		mpdu_blk->Sequence = Sequence;

		mpdu_blk->bAMSDU = RX_BLK_TEST_FLAG(pRxBlk, fRX_AMSDU);

		convert_reordering_packet_to_preAMSDU_or_802_3_packet(pAd, pRxBlk, FromWhichBSSID);

		STATS_INC_RX_PACKETS(pAd, FromWhichBSSID);

        //
		// it is necessary for reordering packet to record
		// which BSS it come from
		//
		RTMP_SET_PACKET_IF(pRxBlk->pRxPacket, FromWhichBSSID);

		mpdu_blk->pPacket = pRxBlk->pRxPacket;

		if (ba_reordering_mpdu_insertsorted(&pBAEntry->list, mpdu_blk) == FALSE)
		{
			// had been already within reordering list
			// don't indicate
			RELEASE_NDIS_PACKET(pAd, pRxBlk->pRxPacket, NDIS_STATUS_SUCCESS);
			ba_mpdu_blk_free(pAd, mpdu_blk);
		}

		ASSERT((0<= pBAEntry->list.qlen)  && (pBAEntry->list.qlen <= pBAEntry->BAWinSize));
		NdisReleaseSpinLock(&pBAEntry->RxReRingLock);
	}
	else
	{
#if 0
		DBGPRINT(RT_DEBUG_ERROR,  ("!!! (%d:%d) Can't allocate reordering mpdu blk\n",
								   blk_count, pBAEntry->list.qlen));
#else
		DBGPRINT(RT_DEBUG_ERROR,  ("!!! (%d) Can't allocate reordering mpdu blk\n",
								   pBAEntry->list.qlen));
#endif
		/*
		 * flush all pending reordering mpdus
		 * and receving mpdu to upper layer
		 * make tcp/ip to take care reordering mechanism
		 */
		//ba_refresh_reordering_mpdus(pAd, pBAEntry);
		ba_indicate_reordering_mpdus_le_seq(pAd, pBAEntry, Sequence);

		pBAEntry->LastIndSeq = Sequence;
		INDICATE_LEGACY_OR_AMSDU(pAd, pRxBlk, FromWhichBSSID);
	}
}


/*
	==========================================================================
	Description:
		Indicate this packet to upper layer or put it into reordering buffer

	Parametrs:
		pRxBlk         : carry necessary packet info 802.11 format
		FromWhichBSSID : the packet received from which BSS

	Return	:
			  none

	Note    :
	          the packet queued into reordering buffer need to cover to 802.3 format
			  or pre_AMSDU format
	==========================================================================
 */

VOID Indicate_AMPDU_Packet(
	IN	PRTMP_ADAPTER	pAd,
	IN	RX_BLK			*pRxBlk,
	IN	UCHAR			FromWhichBSSID)
{
	USHORT				Idx;
	PBA_REC_ENTRY		pBAEntry = NULL;
	UINT16				Sequence = pRxBlk->pHeader->Sequence;
	ULONG				Now32;
	UCHAR				Wcid = pRxBlk->pRxWI->WirelessCliID;
	UCHAR				TID = pRxBlk->pRxWI->TID;


	if (!RX_BLK_TEST_FLAG(pRxBlk, fRX_AMSDU) &&  (pRxBlk->DataSize > MAX_RX_PKT_LEN))
	{
#if 0 // sample take off, no use
		static int err_size;

		err_size++;
		if (err_size > 20) {
			 printk("AMPDU DataSize = %d\n", pRxBlk->DataSize);
			 hex_dump("802.11 Header", (UCHAR *)pRxBlk->pHeader, 24);
			 hex_dump("Payload", pRxBlk->pData, 64);
			 err_size = 0;
		}
#endif
		// release packet
		RELEASE_NDIS_PACKET(pAd, pRxBlk->pRxPacket, NDIS_STATUS_FAILURE);
		return;
	}


#if 0 // test
	/* Rec BA Session had been torn down */
	INDICATE_LEGACY_OR_AMSDU(pAd, pRxBlk, FromWhichBSSID);
	return;
#endif

	if (Wcid < MAX_LEN_OF_MAC_TABLE)
	{
		Idx = pAd->MacTab.Content[Wcid].BARecWcidArray[TID];
		if (Idx == 0)
		{
			/* Rec BA Session had been torn down */
			INDICATE_LEGACY_OR_AMSDU(pAd, pRxBlk, FromWhichBSSID);
			return;
		}
		pBAEntry = &pAd->BATable.BARecEntry[Idx];
	}
	else
	{
		// impossible !!!
		ASSERT(0);
		// release packet
		RELEASE_NDIS_PACKET(pAd, pRxBlk->pRxPacket, NDIS_STATUS_FAILURE);
		return;
	}

	ASSERT(pBAEntry);

	// update last rx time
	NdisGetSystemUpTime(&Now32);

	pBAEntry->rcvSeq = Sequence;


	ba_flush_reordering_timeout_mpdus(pAd, pBAEntry, Now32);
	pBAEntry->LastIndSeqAtTimer = Now32;

	//
	// Reset Last Indicate Sequence
	//
	if (pBAEntry->LastIndSeq == RESET_RCV_SEQ)
	{
		ASSERT((pBAEntry->list.qlen == 0) && (pBAEntry->list.next == NULL));

		// reset rcv sequence of BA session
		pBAEntry->LastIndSeq = Sequence;
		pBAEntry->LastIndSeqAtTimer = Now32;
		INDICATE_LEGACY_OR_AMSDU(pAd, pRxBlk, FromWhichBSSID);
		return;
	}


	//
	// I. Check if in order.
	//
	if (SEQ_STEPONE(Sequence, pBAEntry->LastIndSeq, MAXSEQ))
	{
		USHORT  LastIndSeq;

		pBAEntry->LastIndSeq = Sequence;
		INDICATE_LEGACY_OR_AMSDU(pAd, pRxBlk, FromWhichBSSID);
 		LastIndSeq = ba_indicate_reordering_mpdus_in_order(pAd, pBAEntry, pBAEntry->LastIndSeq);
		if (LastIndSeq != RESET_RCV_SEQ)
		{
			pBAEntry->LastIndSeq = LastIndSeq;
		}
		pBAEntry->LastIndSeqAtTimer = Now32;
	}
	//
	// II. Drop Duplicated Packet
	//
	else if (Sequence == pBAEntry->LastIndSeq)
	{

		// drop and release packet
		pBAEntry->nDropPacket++;
		RELEASE_NDIS_PACKET(pAd, pRxBlk->pRxPacket, NDIS_STATUS_FAILURE);
	}
	//
	// III. Drop Old Received Packet
	//
	else if (SEQ_SMALLER(Sequence, pBAEntry->LastIndSeq, MAXSEQ))
	{

		// drop and release packet
		pBAEntry->nDropPacket++;
		RELEASE_NDIS_PACKET(pAd, pRxBlk->pRxPacket, NDIS_STATUS_FAILURE);
	}
	//
	// IV. Receive Sequence within Window Size
	//
	else if (SEQ_SMALLER(Sequence, (((pBAEntry->LastIndSeq+pBAEntry->BAWinSize+1)) & MAXSEQ), MAXSEQ))
	{
		ba_enqueue_reordering_packet(pAd, pBAEntry, pRxBlk, FromWhichBSSID);
	}
	//
	// V. Receive seq surpasses Win(lastseq + nMSDU). So refresh all reorder buffer
	//
	else
	{
#if 0
		ba_refresh_reordering_mpdus(pAd, pBAEntry);
		INDICATE_LEGACY_OR_AMSDU(pAd, pRxBlk, FromWhichBSSID);
#else
		LONG WinStartSeq, TmpSeq;


		TmpSeq = Sequence - (pBAEntry->BAWinSize) -1;
		if (TmpSeq < 0)
		{
			TmpSeq = (MAXSEQ+1) + TmpSeq;
		}
		WinStartSeq = (TmpSeq+1) & MAXSEQ;
		ba_indicate_reordering_mpdus_le_seq(pAd, pBAEntry, WinStartSeq);
		pBAEntry->LastIndSeq = WinStartSeq; //TmpSeq;

		pBAEntry->LastIndSeqAtTimer = Now32;

		ba_enqueue_reordering_packet(pAd, pBAEntry, pRxBlk, FromWhichBSSID);

		TmpSeq = ba_indicate_reordering_mpdus_in_order(pAd, pBAEntry, pBAEntry->LastIndSeq);
		if (TmpSeq != RESET_RCV_SEQ)
		{
			pBAEntry->LastIndSeq = TmpSeq;
		}
#endif
	}
}

#endif // DOT11_N_SUPPORT //

