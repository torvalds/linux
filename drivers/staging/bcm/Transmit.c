/**
@file Transmit.c
@defgroup tx_functions Transmission
@section Queueing
@dot
digraph transmit1 {
node[shape=box]
edge[weight=5;color=red]
bcm_transmit->reply_to_arp_request[label="ARP"]
bcm_transmit->GetPacketQueueIndex[label="IP Packet"]
GetPacketQueueIndex->IpVersion4[label="IPV4"]
GetPacketQueueIndex->IpVersion6[label="IPV6"]
}

@enddot

@section De-Queueing
@dot
digraph transmit2 {
node[shape=box]
edge[weight=5;color=red]
interrupt_service_thread->transmit_packets
tx_pkt_hdler->transmit_packets
transmit_packets->CheckAndSendPacketFromIndex
transmit_packets->UpdateTokenCount
CheckAndSendPacketFromIndex->PruneQueue
CheckAndSendPacketFromIndex->IsPacketAllowedForFlow
CheckAndSendPacketFromIndex->SendControlPacket[label="control pkt"]
SendControlPacket->bcm_cmd53
CheckAndSendPacketFromIndex->SendPacketFromQueue[label="data pkt"]
SendPacketFromQueue->SetupNextSend->bcm_cmd53
}
@enddot
*/

#include "headers.h"

/*******************************************************************
* Function    -	bcm_transmit()
*
* Description - This is the main transmit function for our virtual
*				interface(veth0). It handles the ARP packets. It
*				clones this packet and then Queue it to a suitable
* 		 		Queue. Then calls the transmit_packet().
*
* Parameter   -	 skb - Pointer to the socket buffer structure
*				 dev - Pointer to the virtual net device structure
*
* Returns     -	 zero (success) or -ve value (failure)
*
*********************************************************************/

INT bcm_transmit(struct sk_buff *skb, 		/**< skb */
					struct net_device *dev 	/**< net device pointer */
					)
{
	PMINI_ADAPTER      	Adapter = NULL;
	USHORT				qindex=0;
	struct timeval tv;
	UINT		pkt_type = 0;
	UINT 		calltransmit = 0;

	BCM_DEBUG_PRINT (Adapter, DBG_TYPE_TX, TX_OSAL_DBG, DBG_LVL_ALL, "\n%s====>\n",__FUNCTION__);

	memset(&tv, 0, sizeof(tv));
	/* Check for valid parameters */
	if(skb == NULL || dev==NULL)
	{
	    BCM_DEBUG_PRINT (Adapter, DBG_TYPE_TX,TX_OSAL_DBG, DBG_LVL_ALL, "Got NULL skb or dev\n");
		return -EINVAL;
	}

	Adapter = GET_BCM_ADAPTER(dev);
	if(!Adapter)
	{
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_TX, TX_OSAL_DBG, DBG_LVL_ALL, "Got Invalid Adapter\n");
  		return -EINVAL;
	}
	if(Adapter->device_removed == TRUE || !Adapter->LinkUpStatus)
	{
		if(!netif_queue_stopped(dev)) {
				netif_carrier_off(dev);
				netif_stop_queue(dev);
		}
		return STATUS_FAILURE;
	}
	BCM_DEBUG_PRINT (Adapter, DBG_TYPE_TX, TX_OSAL_DBG, DBG_LVL_ALL, "Packet size : %d\n", skb->len);

	/*Add Ethernet CS check here*/
	if(Adapter->TransferMode == IP_PACKET_ONLY_MODE )
	{
        pkt_type = ntohs(*(PUSHORT)(skb->data + 12));
		/* Get the queue index where the packet is to be queued */
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_TX, TX_OSAL_DBG, DBG_LVL_ALL, "Getting the Queue Index.....");

		qindex = GetPacketQueueIndex(Adapter,skb);

		if((SHORT)INVALID_QUEUE_INDEX==(SHORT)qindex)
		{
			if(pkt_type == ETH_ARP_FRAME)
			{
				/*
				Reply directly to ARP request packet
				ARP Spoofing only if NO ETH CS rule matches for it
				*/
				BCM_DEBUG_PRINT (Adapter,DBG_TYPE_TX, TX_OSAL_DBG, DBG_LVL_ALL,"ARP OPCODE = %02x",

                (*(PUCHAR)(skb->data + 21)));

                reply_to_arp_request(skb);

                BCM_DEBUG_PRINT (Adapter, DBG_TYPE_TX,TX_OSAL_DBG, DBG_LVL_ALL,"After reply_to_arp_request \n");

			}
			else
			{
                BCM_DEBUG_PRINT (Adapter, DBG_TYPE_TX, TX_OSAL_DBG, DBG_LVL_ALL,
    			"Invalid queue index, dropping pkt\n");

				dev_kfree_skb(skb);
			}
			return STATUS_SUCCESS;
        }

		if(Adapter->PackInfo[qindex].uiCurrentPacketsOnHost >= SF_MAX_ALLOWED_PACKETS_TO_BACKUP)
		{
			atomic_inc(&Adapter->TxDroppedPacketCount);
			dev_kfree_skb(skb);
			return STATUS_SUCCESS;
		}

		/* Now Enqueue the packet */
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL, "bcm_transmit Enqueueing the Packet To Queue %d",qindex);
		spin_lock(&Adapter->PackInfo[qindex].SFQueueLock);
		Adapter->PackInfo[qindex].uiCurrentBytesOnHost += skb->len;
		Adapter->PackInfo[qindex].uiCurrentPacketsOnHost++;

		*((B_UINT32 *)skb->cb + SKB_CB_LATENCY_OFFSET ) = jiffies;
		ENQUEUEPACKET(Adapter->PackInfo[qindex].FirstTxQueue,
  	                  Adapter->PackInfo[qindex].LastTxQueue, skb);
		atomic_inc(&Adapter->TotalPacketCount);
		spin_unlock(&Adapter->PackInfo[qindex].SFQueueLock);
		do_gettimeofday(&tv);

		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_OSAL_DBG, DBG_LVL_ALL,"ENQ: \n");
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_OSAL_DBG, DBG_LVL_ALL, "Pkt Len = %d, sec: %ld, usec: %ld\n",
		(skb->len-ETH_HLEN), tv.tv_sec, tv.tv_usec);

		if(calltransmit == 1)
			transmit_packets(Adapter);
		else
		{
			if(!atomic_read(&Adapter->TxPktAvail))
			{
				atomic_set(&Adapter->TxPktAvail, 1);
				wake_up(&Adapter->tx_packet_wait_queue);
			}
		}
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_OSAL_DBG, DBG_LVL_ALL, "<====");
	}
	else
		dev_kfree_skb(skb);

  return STATUS_SUCCESS;
}


/**
@ingroup ctrl_pkt_functions
This function dispatches control packet to the h/w interface
@return zero(success) or -ve value(failure)
*/
INT SendControlPacket(PMINI_ADAPTER Adapter, /**<Logical Adapter*/
							char *pControlPacket/**<Control Packet*/
							)
{
	PLEADER PLeader = NULL;
	struct timeval tv;
	memset(&tv, 0, sizeof(tv));



	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "========>");

	PLeader=(PLEADER)pControlPacket;
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "Tx");
	if(!pControlPacket || !Adapter)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "Got NULL Control Packet or Adapter");
		return STATUS_FAILURE;
	}
	if((atomic_read( &Adapter->CurrNumFreeTxDesc ) <
		((PLeader->PLength-1)/MAX_DEVICE_DESC_SIZE)+1))
    {
    	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "NO FREE DESCRIPTORS TO SEND CONTROL PACKET");
       	if(Adapter->bcm_jiffies == 0)
        {
        	Adapter->bcm_jiffies = jiffies;
            BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "UPDATED TIME(hex): %lu",
				Adapter->bcm_jiffies);
        }
        return STATUS_FAILURE;
    }

	/* Update the netdevice statistics */
	/* Dump Packet  */
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "Leader Status: %x", PLeader->Status);
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "Leader VCID: %x",PLeader->Vcid);
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "Leader Length: %x",PLeader->PLength);
	if(Adapter->device_removed)
		return 0;
	Adapter->interface_transmit(Adapter->pvInterfaceAdapter,
					pControlPacket, (PLeader->PLength + LEADER_SIZE));

	atomic_dec(&Adapter->CurrNumFreeTxDesc);
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "<=========");
	return STATUS_SUCCESS;
}
static	LEADER Leader={0};
/**
@ingroup tx_functions
This function despatches the IP packets with the given vcid
to the target via the host h/w interface.
@return  zero(success) or -ve value(failure)
*/
INT SetupNextSend(PMINI_ADAPTER Adapter, /**<Logical Adapter*/
					struct sk_buff *Packet, /**<data buffer*/
					USHORT Vcid)			/**<VCID for this packet*/
{
	int		status=0;
	BOOLEAN bHeaderSupressionEnabled = FALSE;
	B_UINT16            uiClassifierRuleID;
	int QueueIndex = NO_OF_QUEUES + 1;

	if(!Adapter || !Packet)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL, "Got NULL Adapter or Packet");
		return -EINVAL;
	}
	if(Packet->len > MAX_DEVICE_DESC_SIZE)
	{
		status = STATUS_FAILURE;
		goto errExit;
	}

	/* Get the Classifier Rule ID */
	uiClassifierRuleID = *((UINT32*) (Packet->cb)+SKB_CB_CLASSIFICATION_OFFSET);
	QueueIndex = SearchVcid( Adapter,Vcid);
	if(QueueIndex < NO_OF_QUEUES)
	{
		bHeaderSupressionEnabled =
			Adapter->PackInfo[QueueIndex].bHeaderSuppressionEnabled;
		bHeaderSupressionEnabled =
			bHeaderSupressionEnabled & Adapter->bPHSEnabled;
	}
	if(Adapter->device_removed)
		{
		status = STATUS_FAILURE;
		goto errExit;
		}

	status = PHSTransmit(Adapter, &Packet, Vcid, uiClassifierRuleID, bHeaderSupressionEnabled,
							(UINT *)&Packet->len, Adapter->PackInfo[QueueIndex].bEthCSSupport);

	if(status != STATUS_SUCCESS)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL, "PHS Transmit failed..\n");
		goto errExit;
	}

	Leader.Vcid	= Vcid;

    if(TCP_ACK == *((UINT32*) (Packet->cb) + SKB_CB_TCPACK_OFFSET ))
	{
        BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL, "Sending TCP ACK\n");
		Leader.Status = LEADER_STATUS_TCP_ACK;
	}
	else
	{
		Leader.Status = LEADER_STATUS;
	}

	if(Adapter->PackInfo[QueueIndex].bEthCSSupport)
	{
		Leader.PLength = Packet->len;
		if(skb_headroom(Packet) < LEADER_SIZE)
        {
			if((status = skb_cow(Packet,LEADER_SIZE)))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL,"bcm_transmit : Failed To Increase headRoom\n");
				goto errExit;
			}
		}
		skb_push(Packet, LEADER_SIZE);
		memcpy(Packet->data, &Leader, LEADER_SIZE);
	}

	else
	{
		Leader.PLength = Packet->len - ETH_HLEN;
		memcpy((LEADER*)skb_pull(Packet, (ETH_HLEN - LEADER_SIZE)), &Leader, LEADER_SIZE);
	}

	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL, "Packet->len = %d", Packet->len);
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL, "Vcid = %d", Vcid);

	status = Adapter->interface_transmit(Adapter->pvInterfaceAdapter,
			Packet->data, (Leader.PLength + LEADER_SIZE));
	if(status)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL, "Tx Failed..\n");
	}
	else
	{
		Adapter->PackInfo[QueueIndex].uiTotalTxBytes += Leader.PLength;
		atomic_add(Leader.PLength, &Adapter->GoodTxByteCount);
		atomic_inc(&Adapter->TxTotalPacketCount);
	}

	atomic_dec(&Adapter->CurrNumFreeTxDesc);

errExit:

	if(STATUS_SUCCESS == status)
	{
		Adapter->PackInfo[QueueIndex].uiCurrentTokenCount -= Leader.PLength << 3;
		Adapter->PackInfo[QueueIndex].uiSentBytes += (Packet->len);
		Adapter->PackInfo[QueueIndex].uiSentPackets++;
		Adapter->PackInfo[QueueIndex].NumOfPacketsSent++;

		atomic_dec(&Adapter->PackInfo[QueueIndex].uiPerSFTxResourceCount);
		Adapter->PackInfo[QueueIndex].uiThisPeriodSentBytes += Leader.PLength;
	}


	dev_kfree_skb(Packet);
	return status;
}

/**
@ingroup tx_functions
Transmit thread
*/
int tx_pkt_handler(PMINI_ADAPTER Adapter  /**< pointer to adapter object*/
				)
{
	int status = 0;

	UINT calltransmit = 1;
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL, "Entring to wait for signal from the interrupt service thread!Adapter = %p",Adapter);


	while(1)
	{
		if(Adapter->LinkUpStatus){
			wait_event_timeout(Adapter->tx_packet_wait_queue,
				((atomic_read(&Adapter->TxPktAvail) &&
				(MINIMUM_PENDING_DESCRIPTORS <
				atomic_read(&Adapter->CurrNumFreeTxDesc)) &&
				(Adapter->device_removed == FALSE))) ||
				(1 == Adapter->downloadDDR) || kthread_should_stop()
				|| (TRUE == Adapter->bEndPointHalted)
				, msecs_to_jiffies(10));
		}
		else{
			wait_event(Adapter->tx_packet_wait_queue,
				((atomic_read(&Adapter->TxPktAvail) &&
				(MINIMUM_PENDING_DESCRIPTORS <
				atomic_read(&Adapter->CurrNumFreeTxDesc)) &&
				(Adapter->device_removed == FALSE))) ||
				(1 == Adapter->downloadDDR) || kthread_should_stop()
				|| (TRUE == Adapter->bEndPointHalted)
				);
		}

		if(kthread_should_stop() || Adapter->device_removed)
		{
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL, "Exiting the tx thread..\n");
			Adapter->transmit_packet_thread = NULL;
			return 0;
		}


		if(Adapter->downloadDDR == 1)
		{
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL, "Downloading DDR Settings\n");
			Adapter->downloadDDR +=1;
			status = download_ddr_settings(Adapter);
			if(status)
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL, "DDR DOWNLOAD FAILED!\n");
			continue;
		}

		//Check end point for halt/stall.
		if(Adapter->bEndPointHalted == TRUE)
		{
			Bcm_clear_halt_of_endpoints(Adapter);
			Adapter->bEndPointHalted = FALSE;
			StartInterruptUrb((PS_INTERFACE_ADAPTER)(Adapter->pvInterfaceAdapter));
		}

		if(Adapter->LinkUpStatus && !Adapter->IdleMode)
		{
			if(atomic_read(&Adapter->TotalPacketCount))
			{
				update_per_sf_desc_cnts(Adapter);
			}
		}

		if( atomic_read(&Adapter->CurrNumFreeTxDesc) &&
			Adapter->LinkStatus == SYNC_UP_REQUEST &&
			!Adapter->bSyncUpRequestSent)
		{
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL, "Calling LinkMessage");
			LinkMessage(Adapter);
		}

		if((Adapter->IdleMode || Adapter->bShutStatus) && atomic_read(&Adapter->TotalPacketCount))
		{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL, "Device in Low Power mode...waking up");
    			Adapter->usIdleModePattern = ABORT_IDLE_MODE;
				Adapter->bWakeUpDevice = TRUE;
				wake_up(&Adapter->process_rx_cntrlpkt);
		}


		if(calltransmit)
			transmit_packets(Adapter);

		atomic_set(&Adapter->TxPktAvail, 0);
	}
	return 0;
}



