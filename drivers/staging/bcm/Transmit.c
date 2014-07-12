/**
 * @file Transmit.c
 * @defgroup tx_functions Transmission
 * @section Queueing
 * @dot
 * digraph transmit1 {
 * node[shape=box]
 * edge[weight=5;color=red]
 *
 * bcm_transmit->GetPacketQueueIndex[label="IP Packet"]
 * GetPacketQueueIndex->IpVersion4[label="IPV4"]
 * GetPacketQueueIndex->IpVersion6[label="IPV6"]
 * }
 *
 * @enddot
 *
 * @section De-Queueing
 * @dot
 * digraph transmit2 {
 * node[shape=box]
 * edge[weight=5;color=red]
 * interrupt_service_thread->transmit_packets
 * tx_pkt_hdler->transmit_packets
 * transmit_packets->CheckAndSendPacketFromIndex
 * transmit_packets->UpdateTokenCount
 * CheckAndSendPacketFromIndex->PruneQueue
 * CheckAndSendPacketFromIndex->IsPacketAllowedForFlow
 * CheckAndSendPacketFromIndex->SendControlPacket[label="control pkt"]
 * SendControlPacket->bcm_cmd53
 * CheckAndSendPacketFromIndex->SendPacketFromQueue[label="data pkt"]
 * SendPacketFromQueue->SetupNextSend->bcm_cmd53
 * }
 * @enddot
 */

#include "headers.h"

/**
 * @ingroup ctrl_pkt_functions
 * This function dispatches control packet to the h/w interface
 * @return zero(success) or -ve value(failure)
 */
int SendControlPacket(struct bcm_mini_adapter *Adapter, char *pControlPacket)
{
	struct bcm_leader *PLeader = (struct bcm_leader *)pControlPacket;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "Tx");
	if (!pControlPacket || !Adapter) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL,
				"Got NULL Control Packet or Adapter");
		return STATUS_FAILURE;
	}
	if ((atomic_read(&Adapter->CurrNumFreeTxDesc) <
			((PLeader->PLength-1)/MAX_DEVICE_DESC_SIZE)+1))	{
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL,
				"NO FREE DESCRIPTORS TO SEND CONTROL PACKET");
		return STATUS_FAILURE;
	}

	/* Update the netdevice statistics */
	/* Dump Packet  */
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "Leader Status: %x", PLeader->Status);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "Leader VCID: %x", PLeader->Vcid);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "Leader Length: %x", PLeader->PLength);
	if (Adapter->device_removed)
		return 0;

	if (netif_msg_pktdata(Adapter))
		print_hex_dump(KERN_DEBUG, PFX "tx control: ", DUMP_PREFIX_NONE,
			16, 1, pControlPacket, PLeader->PLength + LEADER_SIZE, 0);

	Adapter->interface_transmit(Adapter->pvInterfaceAdapter,
				pControlPacket, (PLeader->PLength + LEADER_SIZE));

	atomic_dec(&Adapter->CurrNumFreeTxDesc);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "<=========");
	return STATUS_SUCCESS;
}

/**
 * @ingroup tx_functions
 * This function despatches the IP packets with the given vcid
 * to the target via the host h/w interface.
 * @return  zero(success) or -ve value(failure)
 */
int SetupNextSend(struct bcm_mini_adapter *Adapter,  struct sk_buff *Packet, USHORT Vcid)
{
	int	status = 0;
	bool	bHeaderSupressionEnabled = false;
	B_UINT16 uiClassifierRuleID;
	u16	QueueIndex = skb_get_queue_mapping(Packet);
	struct bcm_packet_info *curr_packet_info =
		&Adapter->PackInfo[QueueIndex];
	struct bcm_leader Leader = {0};

	if (Packet->len > MAX_DEVICE_DESC_SIZE) {
		status = STATUS_FAILURE;
		goto errExit;
	}

	/* Get the Classifier Rule ID */
	uiClassifierRuleID = *((UINT32 *) (Packet->cb) + SKB_CB_CLASSIFICATION_OFFSET);

	bHeaderSupressionEnabled = curr_packet_info->bHeaderSuppressionEnabled
		& Adapter->bPHSEnabled;

	if (Adapter->device_removed) {
		status = STATUS_FAILURE;
		goto errExit;
	}

	status = PHSTransmit(Adapter, &Packet, Vcid, uiClassifierRuleID, bHeaderSupressionEnabled,
			(UINT *)&Packet->len, curr_packet_info->bEthCSSupport);

	if (status != STATUS_SUCCESS) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL,
				"PHS Transmit failed..\n");
		goto errExit;
	}

	Leader.Vcid = Vcid;

	if (TCP_ACK == *((UINT32 *) (Packet->cb) + SKB_CB_TCPACK_OFFSET))
		Leader.Status = LEADER_STATUS_TCP_ACK;
	else
		Leader.Status = LEADER_STATUS;

	if (curr_packet_info->bEthCSSupport) {
		Leader.PLength = Packet->len;
		if (skb_headroom(Packet) < LEADER_SIZE) {
			status = skb_cow(Packet, LEADER_SIZE);
			if (status) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL, "bcm_transmit : Failed To Increase headRoom\n");
				goto errExit;
			}
		}
		skb_push(Packet, LEADER_SIZE);
		memcpy(Packet->data, &Leader, LEADER_SIZE);
	} else {
		Leader.PLength = Packet->len - ETH_HLEN;
		memcpy((struct bcm_leader *)skb_pull(Packet, (ETH_HLEN - LEADER_SIZE)), &Leader, LEADER_SIZE);
	}

	status = Adapter->interface_transmit(Adapter->pvInterfaceAdapter,
					Packet->data, (Leader.PLength + LEADER_SIZE));
	if (status) {
		++Adapter->dev->stats.tx_errors;
		if (netif_msg_tx_err(Adapter))
			pr_info(PFX "%s: transmit error %d\n", Adapter->dev->name,
				status);
	} else {
		struct net_device_stats *netstats = &Adapter->dev->stats;

		curr_packet_info->uiTotalTxBytes += Leader.PLength;

		netstats->tx_bytes += Leader.PLength;
		++netstats->tx_packets;

		curr_packet_info->uiCurrentTokenCount -= Leader.PLength << 3;
		curr_packet_info->uiSentBytes += (Packet->len);
		curr_packet_info->uiSentPackets++;
		curr_packet_info->NumOfPacketsSent++;

		atomic_dec(&curr_packet_info->uiPerSFTxResourceCount);
		curr_packet_info->uiThisPeriodSentBytes += Leader.PLength;
	}

	atomic_dec(&Adapter->CurrNumFreeTxDesc);

errExit:
	dev_kfree_skb(Packet);
	return status;
}

static int tx_pending(struct bcm_mini_adapter *Adapter)
{
	return (atomic_read(&Adapter->TxPktAvail)
		&& MINIMUM_PENDING_DESCRIPTORS < atomic_read(&Adapter->CurrNumFreeTxDesc))
		|| Adapter->device_removed || (1 == Adapter->downloadDDR);
}

/**
 * @ingroup tx_functions
 * Transmit thread
 */
int tx_pkt_handler(struct bcm_mini_adapter *Adapter /**< pointer to adapter object*/)
{
	int status = 0;

	while (!kthread_should_stop()) {
		/* FIXME - the timeout looks like workaround for racey usage of TxPktAvail */
		if (Adapter->LinkUpStatus)
			wait_event_timeout(Adapter->tx_packet_wait_queue,
					tx_pending(Adapter), msecs_to_jiffies(10));
		else
			wait_event_interruptible(Adapter->tx_packet_wait_queue,
						tx_pending(Adapter));

		if (Adapter->device_removed)
			break;

		if (Adapter->downloadDDR == 1) {
			Adapter->downloadDDR += 1;
			status = download_ddr_settings(Adapter);
			if (status)
				pr_err(PFX "DDR DOWNLOAD FAILED! %d\n", status);
			continue;
		}

		/* Check end point for halt/stall. */
		if (Adapter->bEndPointHalted == TRUE) {
			Bcm_clear_halt_of_endpoints(Adapter);
			Adapter->bEndPointHalted = false;
			StartInterruptUrb((struct bcm_interface_adapter *)(Adapter->pvInterfaceAdapter));
		}

		if (Adapter->LinkUpStatus && !Adapter->IdleMode) {
			if (atomic_read(&Adapter->TotalPacketCount))
				update_per_sf_desc_cnts(Adapter);
		}

		if (atomic_read(&Adapter->CurrNumFreeTxDesc) &&
			Adapter->LinkStatus == SYNC_UP_REQUEST &&
			!Adapter->bSyncUpRequestSent) {

			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS,
					DBG_LVL_ALL, "Calling LinkMessage");
			LinkMessage(Adapter);
		}

		if ((Adapter->IdleMode || Adapter->bShutStatus) && atomic_read(&Adapter->TotalPacketCount)) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX,
					TX_PACKETS, DBG_LVL_ALL,
					"Device in Low Power mode...waking up");
			Adapter->usIdleModePattern = ABORT_IDLE_MODE;
			Adapter->bWakeUpDevice = TRUE;
			wake_up(&Adapter->process_rx_cntrlpkt);
		}

		transmit_packets(Adapter);
		atomic_set(&Adapter->TxPktAvail, 0);
	}

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, TX_PACKETS, DBG_LVL_ALL,
			"Exiting the tx thread..\n");
	Adapter->transmit_packet_thread = NULL;
	return 0;
}
