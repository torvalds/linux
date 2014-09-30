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

static void get_data_packet(struct bcm_mini_adapter *ad,
			    struct bcm_packet_info *ps_sf)
{
	int packet_len;
	struct sk_buff *qpacket;

	if (!ps_sf->ucDirection)
		return;

	BCM_DEBUG_PRINT(ad, DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL,
			"UpdateTokenCount ");
	if (ad->IdleMode || ad->bPreparingForLowPowerMode)
		return; /* in idle mode */

	/* Check for Free Descriptors */
	if (atomic_read(&ad->CurrNumFreeTxDesc) <=
	    MINIMUM_PENDING_DESCRIPTORS) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL,
				" No Free Tx Descriptor(%d) is available for Data pkt..",
				atomic_read(&ad->CurrNumFreeTxDesc));
		return;
	}

	spin_lock_bh(&ps_sf->SFQueueLock);
	qpacket = ps_sf->FirstTxQueue;

	if (qpacket) {
		BCM_DEBUG_PRINT(ad, DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL,
				"Dequeuing Data Packet");

		if (ps_sf->bEthCSSupport)
			packet_len = qpacket->len;
		else
			packet_len = qpacket->len - ETH_HLEN;

		packet_len <<= 3;
		if (packet_len <= GetSFTokenCount(ad, ps_sf)) {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_TX, TX_PACKETS,
					DBG_LVL_ALL, "Allowed bytes %d",
					(packet_len >> 3));

			DEQUEUEPACKET(ps_sf->FirstTxQueue, ps_sf->LastTxQueue);
			ps_sf->uiCurrentBytesOnHost -= (qpacket->len);
			ps_sf->uiCurrentPacketsOnHost--;
				atomic_dec(&ad->TotalPacketCount);
			spin_unlock_bh(&ps_sf->SFQueueLock);

			SendPacketFromQueue(ad, ps_sf, qpacket);
			ps_sf->uiPendedLast = false;
		} else {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_TX, TX_PACKETS,
					DBG_LVL_ALL, "For Queue: %zd\n",
					ps_sf - ad->PackInfo);
			BCM_DEBUG_PRINT(ad, DBG_TYPE_TX, TX_PACKETS,
					DBG_LVL_ALL,
					"\nAvailable Tokens = %d required = %d\n",
					ps_sf->uiCurrentTokenCount,
					packet_len);
			/*
			this part indicates that because of
			non-availability of the tokens
			pkt has not been send out hence setting the
			pending flag indicating the host to send it out
			first next iteration.
			*/
			ps_sf->uiPendedLast = TRUE;
			spin_unlock_bh(&ps_sf->SFQueueLock);
		}
	} else {
		spin_unlock_bh(&ps_sf->SFQueueLock);
	}
}

static void send_control_packet(struct bcm_mini_adapter *ad,
				struct bcm_packet_info *ps_sf)
{
	char *ctrl_packet = NULL;
	INT status = 0;

	if ((atomic_read(&ad->CurrNumFreeTxDesc) > 0) &&
	    (atomic_read(&ad->index_rd_txcntrlpkt) !=
	     atomic_read(&ad->index_wr_txcntrlpkt))) {
		ctrl_packet = ad->txctlpacket
		[(atomic_read(&ad->index_rd_txcntrlpkt)%MAX_CNTRL_PKTS)];
		if (ctrl_packet) {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_TX, TX_PACKETS,
					DBG_LVL_ALL,
					"Sending Control packet");
			status = SendControlPacket(ad, ctrl_packet);
			if (STATUS_SUCCESS == status) {
				spin_lock_bh(&ps_sf->SFQueueLock);
				ps_sf->NumOfPacketsSent++;
				ps_sf->uiSentBytes += ((struct bcm_leader *)ctrl_packet)->PLength;
				ps_sf->uiSentPackets++;
				atomic_dec(&ad->TotalPacketCount);
				ps_sf->uiCurrentBytesOnHost -= ((struct bcm_leader *)ctrl_packet)->PLength;
				ps_sf->uiCurrentPacketsOnHost--;
				atomic_inc(&ad->index_rd_txcntrlpkt);
				spin_unlock_bh(&ps_sf->SFQueueLock);
			} else {
				BCM_DEBUG_PRINT(ad, DBG_TYPE_TX, TX_PACKETS,
						DBG_LVL_ALL,
						"SendControlPacket Failed\n");
			}
		} else {
			BCM_DEBUG_PRINT(ad, DBG_TYPE_TX, TX_PACKETS,
					DBG_LVL_ALL,
					" Control Pkt is not available, Indexing is wrong....");
		}
	}
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
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL,
			"%zd ====>", (psSF-Adapter->PackInfo));
	if ((psSF != &Adapter->PackInfo[HiPriority]) &&
	    Adapter->LinkUpStatus &&
	    atomic_read(&psSF->uiPerSFTxResourceCount)) { /* Get data packet */

		get_data_packet(Adapter, psSF);
	} else {
		send_control_packet(Adapter, psSF);
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
