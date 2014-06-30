/**********************************************************************
* 			LEAKYBUCKET.C
*	This file contains the routines related to Leaky Bucket Algorithm.
***********************************************************************/
#include "headers.h"

/*********************************************************************
* Function    - UpdateTokenCount()
*
* Description - This function calculates the token count for each
*				channel and updates the same in Adapter strucuture.
*
* Parameters  - Adapter: Pointer to the Adapter structure.
*
* Returns     - None
**********************************************************************/

static VOID UpdateTokenCount(register struct bcm_mini_adapter *Adapter)
{
	ULONG liCurrentTime;
	INT i = 0;
	struct timeval tv;
	struct bcm_packet_info *curr_pi;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TOKEN_COUNTS, DBG_LVL_ALL,
			"=====>\n");
	if (NULL == Adapter) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TOKEN_COUNTS,
				DBG_LVL_ALL, "Adapter found NULL!\n");
		return;
	}

	do_gettimeofday(&tv);
	for (i = 0; i < NO_OF_QUEUES; i++) {
		curr_pi = &Adapter->PackInfo[i];

		if (TRUE == curr_pi->bValid && (1 == curr_pi->ucDirection)) {
			liCurrentTime = ((tv.tv_sec -
				curr_pi->stLastUpdateTokenAt.tv_sec)*1000 +
				(tv.tv_usec - curr_pi->stLastUpdateTokenAt.tv_usec) /
				1000);
			if (0 != liCurrentTime) {
				curr_pi->uiCurrentTokenCount += (ULONG)
					((curr_pi->uiMaxAllowedRate) *
					((ULONG)((liCurrentTime)))/1000);
				memcpy(&curr_pi->stLastUpdateTokenAt, &tv,
				       sizeof(struct timeval));
				curr_pi->liLastUpdateTokenAt = liCurrentTime;
				if (curr_pi->uiCurrentTokenCount >=
				    curr_pi->uiMaxBucketSize) {
					curr_pi->uiCurrentTokenCount =
						curr_pi->uiMaxBucketSize;
				}
			}
		}
	}
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TOKEN_COUNTS, DBG_LVL_ALL,
			"<=====\n");
	return;

}


/*********************************************************************
* Function    - IsPacketAllowedForFlow()
*
* Description - This function checks whether the given packet from the
*				specified queue can be allowed for transmission by
*				checking the token count.
*
* Parameters  - Adapter	      :	Pointer to the Adpater structure.
* 			  - iQIndex	      :	The queue Identifier.
* 			  - ulPacketLength:	Number of bytes to be transmitted.
*
* Returns     - The number of bytes allowed for transmission.
*
***********************************************************************/
static ULONG GetSFTokenCount(struct bcm_mini_adapter *Adapter, struct bcm_packet_info *psSF)
{
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TOKEN_COUNTS, DBG_LVL_ALL,
			"IsPacketAllowedForFlow ===>");

	/* Validate the parameters */
	if (NULL == Adapter || (psSF < Adapter->PackInfo &&
	    (uintptr_t)psSF > (uintptr_t) &Adapter->PackInfo[HiPriority])) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TOKEN_COUNTS, DBG_LVL_ALL,
				"IPAFF: Got wrong Parameters:Adapter: %p, QIndex: %zd\n",
				Adapter, (psSF-Adapter->PackInfo));
		return 0;
	}

	if (false != psSF->bValid && psSF->ucDirection) {
		if (0 != psSF->uiCurrentTokenCount) {
			return psSF->uiCurrentTokenCount;
		} else {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TOKEN_COUNTS,
					DBG_LVL_ALL,
					"Not enough tokens in queue %zd Available %u\n",
					psSF-Adapter->PackInfo, psSF->uiCurrentTokenCount);
			psSF->uiPendedLast = 1;
		}
	} else {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TOKEN_COUNTS, DBG_LVL_ALL,
				"IPAFF: Queue %zd not valid\n",
				psSF-Adapter->PackInfo);
	}
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TOKEN_COUNTS, DBG_LVL_ALL,
			"IsPacketAllowedForFlow <===");
	return 0;
}

/**
@ingroup tx_functions
This function despatches packet from the specified queue.
@return Zero(success) or Negative value(failure)
*/
static INT SendPacketFromQueue(struct bcm_mini_adapter *Adapter,/**<Logical Adapter*/
			       struct bcm_packet_info *psSF, /**<Queue identifier*/
			       struct sk_buff *Packet)	/**<Pointer to the packet to be sent*/
{
	INT Status = STATUS_FAILURE;
	UINT uiIndex = 0, PktLen = 0;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, SEND_QUEUE, DBG_LVL_ALL,
			"=====>");
	if (!Adapter || !Packet || !psSF) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, SEND_QUEUE, DBG_LVL_ALL,
				"Got NULL Adapter or Packet");
		return -EINVAL;
	}

	if (psSF->liDrainCalculated == 0)
		psSF->liDrainCalculated = jiffies;
	/* send the packet to the fifo.. */
	PktLen = Packet->len;
	Status = SetupNextSend(Adapter, Packet, psSF->usVCID_Value);
	if (Status == 0) {
		for (uiIndex = 0; uiIndex < MIBS_MAX_HIST_ENTRIES; uiIndex++) {
			if ((PktLen <= MIBS_PKTSIZEHIST_RANGE*(uiIndex+1)) &&
			    (PktLen > MIBS_PKTSIZEHIST_RANGE*(uiIndex)))
				Adapter->aTxPktSizeHist[uiIndex]++;
		}
	}
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, SEND_QUEUE, DBG_LVL_ALL,
			"<=====");
	return Status;
}

/************************************************************************
* Function    - CheckAndSendPacketFromIndex()
*
* Description - This function dequeues the data/control packet from the
*				specified queue for transmission.
*
* Parameters  - Adapter : Pointer to the driver control structure.
* 			  - iQIndex : The queue Identifier.
*
* Returns     - None.
*
****************************************************************************/
static VOID CheckAndSendPacketFromIndex(struct bcm_mini_adapter *Adapter,
					struct bcm_packet_info *psSF)
{
	struct sk_buff *QueuePacket = NULL;
	char *pControlPacket = NULL;
	INT Status = 0;
	int iPacketLen = 0;


	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL,
			"%zd ====>", (psSF-Adapter->PackInfo));
	if ((psSF != &Adapter->PackInfo[HiPriority]) &&
	    Adapter->LinkUpStatus &&
	    atomic_read(&psSF->uiPerSFTxResourceCount)) { /* Get data packet */
		if (!psSF->ucDirection)
			return;

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL,
				"UpdateTokenCount ");
		if (Adapter->IdleMode || Adapter->bPreparingForLowPowerMode)
			return;	/* in idle mode */

		/* Check for Free Descriptors */
		if (atomic_read(&Adapter->CurrNumFreeTxDesc) <=
		    MINIMUM_PENDING_DESCRIPTORS) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS,
					DBG_LVL_ALL,
					" No Free Tx Descriptor(%d) is available for Data pkt..",
					atomic_read(&Adapter->CurrNumFreeTxDesc));
			return;
		}

		spin_lock_bh(&psSF->SFQueueLock);
		QueuePacket = psSF->FirstTxQueue;

		if (QueuePacket) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS,
					DBG_LVL_ALL, "Dequeuing Data Packet");

			if (psSF->bEthCSSupport)
				iPacketLen = QueuePacket->len;
			else
				iPacketLen = QueuePacket->len-ETH_HLEN;

			iPacketLen <<= 3;
			if (iPacketLen <= GetSFTokenCount(Adapter, psSF)) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX,
						TX_PACKETS, DBG_LVL_ALL,
						"Allowed bytes %d",
					(iPacketLen >> 3));

				DEQUEUEPACKET(psSF->FirstTxQueue,
					      psSF->LastTxQueue);
				psSF->uiCurrentBytesOnHost -=
					(QueuePacket->len);
				psSF->uiCurrentPacketsOnHost--;
				atomic_dec(&Adapter->TotalPacketCount);
				spin_unlock_bh(&psSF->SFQueueLock);

				Status = SendPacketFromQueue(Adapter, psSF,
							     QueuePacket);
				psSF->uiPendedLast = false;
			} else {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX,
						TX_PACKETS, DBG_LVL_ALL,
						"For Queue: %zd\n",
						psSF-Adapter->PackInfo);
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX,
						TX_PACKETS, DBG_LVL_ALL,
						"\nAvailable Tokens = %d required = %d\n",
					psSF->uiCurrentTokenCount, iPacketLen);
				/*
				this part indicates that because of
				non-availability of the tokens
				pkt has not been send out hence setting the
				pending flag indicating the host to send it out
				first next iteration.
				*/
				psSF->uiPendedLast = TRUE;
				spin_unlock_bh(&psSF->SFQueueLock);
			}
		} else {
			spin_unlock_bh(&psSF->SFQueueLock);
		}
	} else {

		if ((atomic_read(&Adapter->CurrNumFreeTxDesc) > 0) &&
		    (atomic_read(&Adapter->index_rd_txcntrlpkt) !=
		     atomic_read(&Adapter->index_wr_txcntrlpkt))) {
			pControlPacket = Adapter->txctlpacket
			[(atomic_read(&Adapter->index_rd_txcntrlpkt)%MAX_CNTRL_PKTS)];
			if (pControlPacket) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS,
						DBG_LVL_ALL,
						"Sending Control packet");
				Status = SendControlPacket(Adapter,
							   pControlPacket);
				if (STATUS_SUCCESS == Status) {
					spin_lock_bh(&psSF->SFQueueLock);
					psSF->NumOfPacketsSent++;
					psSF->uiSentBytes += ((struct bcm_leader *)pControlPacket)->PLength;
					psSF->uiSentPackets++;
					atomic_dec(&Adapter->TotalPacketCount);
					psSF->uiCurrentBytesOnHost -= ((struct bcm_leader *)pControlPacket)->PLength;
					psSF->uiCurrentPacketsOnHost--;
					atomic_inc(&Adapter->index_rd_txcntrlpkt);
					spin_unlock_bh(&psSF->SFQueueLock);
				} else {
					BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX,
							TX_PACKETS, DBG_LVL_ALL,
							"SendControlPacket Failed\n");
				}
			} else {
					BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX,
							TX_PACKETS, DBG_LVL_ALL,
							" Control Pkt is not available, Indexing is wrong....");
			}
		}
	}
}


/*******************************************************************
* Function    - transmit_packets()
*
* Description - This function transmits the packets from different
*				queues, if free descriptors are available on target.
*
* Parameters  - Adapter:  Pointer to the Adapter structure.
*
* Returns     - None.
********************************************************************/
VOID transmit_packets(struct bcm_mini_adapter *Adapter)
{
	UINT uiPrevTotalCount = 0;
	int iIndex = 0;

	bool exit_flag = TRUE;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL,
			"=====>");

	if (NULL == Adapter) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL,
				"Got NULL Adapter");
		return;
	}
	if (Adapter->device_removed == TRUE) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL,
				"Device removed");
		return;
	}

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL,
			"\nUpdateTokenCount ====>\n");

	UpdateTokenCount(Adapter);

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL,
			"\nPruneQueueAllSF ====>\n");

	PruneQueueAllSF(Adapter);

	uiPrevTotalCount = atomic_read(&Adapter->TotalPacketCount);

	for (iIndex = HiPriority; iIndex >= 0; iIndex--) {
		if (!uiPrevTotalCount || (TRUE == Adapter->device_removed))
				break;

		if (Adapter->PackInfo[iIndex].bValid &&
		    Adapter->PackInfo[iIndex].uiPendedLast &&
		    Adapter->PackInfo[iIndex].uiCurrentBytesOnHost) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS,
					DBG_LVL_ALL,
					"Calling CheckAndSendPacketFromIndex..");
			CheckAndSendPacketFromIndex(Adapter,
						    &Adapter->PackInfo[iIndex]);
			uiPrevTotalCount--;
		}
	}

	while (uiPrevTotalCount > 0 && !Adapter->device_removed) {
		exit_flag = TRUE;
		/* second iteration to parse non-pending queues */
		for (iIndex = HiPriority; iIndex >= 0; iIndex--) {
			if (!uiPrevTotalCount ||
			    (TRUE == Adapter->device_removed))
				break;

			if (Adapter->PackInfo[iIndex].bValid &&
			    Adapter->PackInfo[iIndex].uiCurrentBytesOnHost &&
			    !Adapter->PackInfo[iIndex].uiPendedLast) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX,
						TX_PACKETS, DBG_LVL_ALL,
						"Calling CheckAndSendPacketFromIndex..");
				CheckAndSendPacketFromIndex(Adapter, &Adapter->PackInfo[iIndex]);
				uiPrevTotalCount--;
				exit_flag = false;
			}
		}

		if (Adapter->IdleMode || Adapter->bPreparingForLowPowerMode) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS,
					DBG_LVL_ALL, "In Idle Mode\n");
			break;
		}
		if (exit_flag == TRUE)
			break;
	} /* end of inner while loop */

	update_per_cid_rx(Adapter);
	Adapter->txtransmit_running = 0;
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL,
			"<======");
}
