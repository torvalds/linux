#include "headers.h"

static VOID default_wimax_protocol_initialize(PMINI_ADAPTER Adapter)
{

	UINT    uiLoopIndex;

    for(uiLoopIndex=0; uiLoopIndex < NO_OF_QUEUES-1; uiLoopIndex++)
    {
    	Adapter->PackInfo[uiLoopIndex].uiThreshold=TX_PACKET_THRESHOLD;
        Adapter->PackInfo[uiLoopIndex].uiMaxAllowedRate=MAX_ALLOWED_RATE;
        Adapter->PackInfo[uiLoopIndex].uiMaxBucketSize=20*1024*1024;
    }

    Adapter->BEBucketSize=BE_BUCKET_SIZE;
    Adapter->rtPSBucketSize=rtPS_BUCKET_SIZE;
    Adapter->LinkStatus=SYNC_UP_REQUEST;
    Adapter->TransferMode=IP_PACKET_ONLY_MODE;
    Adapter->usBestEffortQueueIndex=-1;
    return;
}


INT
InitAdapter(PMINI_ADAPTER psAdapter)
{
    int i = 0;
	INT Status = STATUS_SUCCESS ;
	BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT,  DBG_LVL_ALL,  "Initialising Adapter = %p", psAdapter);

	if(psAdapter == NULL)
	{
		BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT,  DBG_LVL_ALL, "Adapter is NULL");
		return -EINVAL;
	}

	sema_init(&psAdapter->NVMRdmWrmLock,1);
//	psAdapter->ulFlashCalStart = FLASH_AUTO_INIT_BASE_ADDR;

	sema_init(&psAdapter->rdmwrmsync, 1);
	spin_lock_init(&psAdapter->control_queue_lock);
	spin_lock_init(&psAdapter->txtransmitlock);
    sema_init(&psAdapter->RxAppControlQueuelock, 1);
//    sema_init(&psAdapter->data_packet_queue_lock, 1);
    sema_init(&psAdapter->fw_download_sema, 1);
  	sema_init(&psAdapter->LowPowerModeSync,1);

  // spin_lock_init(&psAdapter->sleeper_lock);

    for(i=0;i<NO_OF_QUEUES; i++)
        spin_lock_init(&psAdapter->PackInfo[i].SFQueueLock);
    i=0;

    init_waitqueue_head(&psAdapter->process_rx_cntrlpkt);
    init_waitqueue_head(&psAdapter->tx_packet_wait_queue);
    init_waitqueue_head(&psAdapter->process_read_wait_queue);
    init_waitqueue_head(&psAdapter->ioctl_fw_dnld_wait_queue);
    init_waitqueue_head(&psAdapter->lowpower_mode_wait_queue);
	psAdapter->waiting_to_fw_download_done = TRUE;
    //init_waitqueue_head(&psAdapter->device_wake_queue);
    psAdapter->fw_download_done=FALSE;

    psAdapter->pvOsDepData = (PLINUX_DEP_DATA) kmalloc(sizeof(LINUX_DEP_DATA),
                 GFP_KERNEL);

    if(psAdapter->pvOsDepData == NULL)
	{
        BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Linux Specific Data allocation failed");
        return -ENOMEM;
    }
    memset(psAdapter->pvOsDepData, 0, sizeof(LINUX_DEP_DATA));

	default_wimax_protocol_initialize(psAdapter);
	for (i=0;i<MAX_CNTRL_PKTS;i++)
	{
		psAdapter->txctlpacket[i] = (char *)kmalloc(MAX_CNTL_PKT_SIZE,
												GFP_KERNEL);
		if(!psAdapter->txctlpacket[i])
		{
			BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "No More Cntl pkts got, max got is %d", i);
			return -ENOMEM;
		}
	}
	if(AllocAdapterDsxBuffer(psAdapter))
	{
		BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Failed to allocate DSX buffers");
		return -EINVAL;
	}

	//Initialize PHS interface
	if(phs_init(&psAdapter->stBCMPhsContext,psAdapter)!=0)
	{
		BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL,"%s:%s:%d:Error PHS Init Failed=====>\n", __FILE__, __FUNCTION__, __LINE__);
		return -ENOMEM;
	}

	Status = BcmAllocFlashCSStructure(psAdapter);
	if(Status)
	{
		BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL,"Memory Allocation for Flash structure failed");
		return Status ;
	}

	Status = vendorextnInit(psAdapter);

	if(STATUS_SUCCESS != Status)
	{
		BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL,"Vendor Init Failed");
		return Status ;
	}

	BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL,  "Adapter initialised");


	return STATUS_SUCCESS;
}

VOID AdapterFree(PMINI_ADAPTER Adapter)
{
	INT count = 0;

	beceem_protocol_reset(Adapter);

	vendorextnExit(Adapter);

	if(Adapter->control_packet_handler && !IS_ERR(Adapter->control_packet_handler))
	  	kthread_stop (Adapter->control_packet_handler);
	if(Adapter->transmit_packet_thread && !IS_ERR(Adapter->transmit_packet_thread))
    	kthread_stop (Adapter->transmit_packet_thread);
    wake_up(&Adapter->process_read_wait_queue);
	if(Adapter->LEDInfo.led_thread_running & (BCM_LED_THREAD_RUNNING_ACTIVELY | BCM_LED_THREAD_RUNNING_INACTIVELY))
		kthread_stop (Adapter->LEDInfo.led_cntrl_threadid);
	bcm_unregister_networkdev(Adapter);
	while(atomic_read(&Adapter->ApplicationRunning))
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Waiting for Application to close.. %d\n",atomic_read(&Adapter->ApplicationRunning));
		msleep(100);
	}
	unregister_control_device_interface(Adapter);
	if(Adapter->dev && !IS_ERR(Adapter->dev))
		free_netdev(Adapter->dev);
	if(Adapter->pstargetparams != NULL)
	{
		bcm_kfree(Adapter->pstargetparams);
	}
	for (count =0;count < MAX_CNTRL_PKTS;count++)
	{
		if(Adapter->txctlpacket[count])
			bcm_kfree(Adapter->txctlpacket[count]);
	}
	FreeAdapterDsxBuffer(Adapter);
	if(Adapter->pvOsDepData)
		bcm_kfree (Adapter->pvOsDepData);
	if(Adapter->pvInterfaceAdapter)
		bcm_kfree(Adapter->pvInterfaceAdapter);

	//Free the PHS Interface
	PhsCleanup(&Adapter->stBCMPhsContext);

#ifndef BCM_SHM_INTERFACE
	BcmDeAllocFlashCSStructure(Adapter);
#endif

	bcm_kfree (Adapter);
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "<========\n");
}


int create_worker_threads(PMINI_ADAPTER psAdapter)
{
	BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Init Threads...");
	// Rx Control Packets Processing
	psAdapter->control_packet_handler = kthread_run((int (*)(void *))
			control_packet_handler, psAdapter, "CtrlPktHdlr");
	if(IS_ERR(psAdapter->control_packet_handler))
	{
		BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "No Kernel Thread, but still returning success\n");
		return PTR_ERR(psAdapter->control_packet_handler);
	}
	// Tx Thread
	psAdapter->transmit_packet_thread = kthread_run((int (*)(void *))
		tx_pkt_handler, psAdapter, "TxPktThread");
	if(IS_ERR (psAdapter->transmit_packet_thread))
	{
		BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "No Kernel Thread, but still returning success");
		kthread_stop(psAdapter->control_packet_handler);
		return PTR_ERR(psAdapter->transmit_packet_thread);
	}
	return 0;
}


static inline struct file *open_firmware_file(PMINI_ADAPTER Adapter, char *path)
{
    struct file             *flp=NULL;
    mm_segment_t        oldfs;
    oldfs=get_fs();
	set_fs(get_ds());
    flp=filp_open(path, O_RDONLY, S_IRWXU);
    set_fs(oldfs);
    if(IS_ERR(flp))
    {
        BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Unable To Open File %s, err  %lx",
				path, PTR_ERR(flp));
		flp = NULL;
    }
    else
    {
        BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Got file descriptor pointer of %s!",
			path);
    }
	if(Adapter->device_removed)
	{
		flp = NULL;
	}

    return flp;
}


int BcmFileDownload(PMINI_ADAPTER Adapter,/**< Logical Adapter */
                        char *path,     /**< path to image file */
                        unsigned int loc    /**< Download Address on the chip*/
                        )
{
    int             errorno=0;
    struct file     *flp=NULL;
    mm_segment_t    oldfs;
    struct timeval tv={0};

    flp=open_firmware_file(Adapter, path);
    if(!flp)
    {
        errorno = -ENOENT;
        BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Unable to Open %s\n", path);
        goto exit_download;
    }
    BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Opened file is = %s and length =0x%lx to be downloaded at =0x%x", path,(unsigned long)flp->f_dentry->d_inode->i_size, loc);
    do_gettimeofday(&tv);

	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "download start %lx", ((tv.tv_sec * 1000) +
                            (tv.tv_usec/1000)));
    if(Adapter->bcm_file_download(Adapter->pvInterfaceAdapter, flp, loc))
    {
        BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Failed to download the firmware with error\
		 %x!!!", -EIO);
        errorno=-EIO;
        goto exit_download;
    }
    oldfs=get_fs();set_fs(get_ds());
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    vfs_llseek(flp, 0, 0);
#endif
    set_fs(oldfs);
    if(Adapter->bcm_file_readback_from_chip(Adapter->pvInterfaceAdapter,
										flp, loc))
    {
        BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Failed to read back firmware!");
        errorno=-EIO;
        goto exit_download;
    }

exit_download:
    oldfs=get_fs();set_fs(get_ds());
	if(flp && !(IS_ERR(flp)))
    	filp_close(flp, current->files);
    set_fs(oldfs);
    do_gettimeofday(&tv);
    BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "file download done at %lx", ((tv.tv_sec * 1000) +
                            (tv.tv_usec/1000)));
    return errorno;
}


void bcm_kfree_skb(struct sk_buff *skb)
{
	if(skb)
    {
    	kfree_skb(skb);
    }
	skb = NULL ;
}

VOID bcm_kfree(VOID *ptr)
{
	if(ptr)
	{
		kfree(ptr);
	}
	ptr = NULL ;
}

/**
@ingroup ctrl_pkt_functions
This function copies the contents of given buffer
to the control packet and queues it for transmission.
@note Do not acquire the spinock, as it it already acquired.
@return  SUCCESS/FAILURE.
*/
INT CopyBufferToControlPacket(PMINI_ADAPTER Adapter,/**<Logical Adapter*/
									  PVOID ioBuffer/**<Control Packet Buffer*/
									  )
{
	PLEADER				pLeader=NULL;
	INT					Status=0;
	unsigned char		*ctrl_buff=NULL;
	UINT				pktlen=0;
	PLINK_REQUEST		pLinkReq 	= NULL;
	PUCHAR				pucAddIndication = NULL;

	BCM_DEBUG_PRINT( Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "======>");
	if(!ioBuffer)
	{
		BCM_DEBUG_PRINT( Adapter,DBG_TYPE_TX, TX_CONTROL,DBG_LVL_ALL, "Got Null Buffer\n");
		return -EINVAL;
	}

	pLinkReq = (PLINK_REQUEST)ioBuffer;
	pLeader=(PLEADER)ioBuffer; //ioBuffer Contains sw_Status and Payload

	if(Adapter->bShutStatus == TRUE &&
		pLinkReq->szData[0] == LINK_DOWN_REQ_PAYLOAD &&
		pLinkReq->szData[1] == LINK_SYNC_UP_SUBTYPE)
	{
		//Got sync down in SHUTDOWN..we could not process this.
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL,DBG_LVL_ALL, "SYNC DOWN Request in Shut Down Mode..\n");
		return STATUS_FAILURE;
	}

	if((pLeader->Status == LINK_UP_CONTROL_REQ) &&
		((pLinkReq->szData[0] == LINK_UP_REQ_PAYLOAD &&
		 (pLinkReq->szData[1] == LINK_SYNC_UP_SUBTYPE)) ||//Sync Up Command
		 pLinkReq->szData[0] == NETWORK_ENTRY_REQ_PAYLOAD)) //Net Entry Command
	{
		if(Adapter->LinkStatus > PHY_SYNC_ACHIVED)
		{
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL,DBG_LVL_ALL,"LinkStatus is Greater than PHY_SYN_ACHIEVED");
			return STATUS_FAILURE;
		}
		if(TRUE == Adapter->bShutStatus)
		{
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL,DBG_LVL_ALL, "SYNC UP IN SHUTDOWN..Device WakeUp\n");
			if(Adapter->bTriedToWakeUpFromlowPowerMode == FALSE)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL,DBG_LVL_ALL, "Waking up for the First Time..\n");
				Adapter->usIdleModePattern = ABORT_SHUTDOWN_MODE; // change it to 1 for current support.
				Adapter->bWakeUpDevice = TRUE;
				wake_up(&Adapter->process_rx_cntrlpkt);

				Status = wait_event_interruptible_timeout(Adapter->lowpower_mode_wait_queue,
					!Adapter->bShutStatus, (5 * HZ));

				if(Status == -ERESTARTSYS)
					return Status;

				if(Adapter->bShutStatus)
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL,DBG_LVL_ALL, "Shutdown Mode Wake up Failed - No Wake Up Received\n");
					return STATUS_FAILURE;
				}
			}
			else
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL,DBG_LVL_ALL, "Wakeup has been tried already...\n");
			}
		}

	}
	if(TRUE == Adapter->IdleMode)
	{
		//BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Device is in Idle mode ... hence \n");
		if(pLeader->Status == LINK_UP_CONTROL_REQ || pLeader->Status == 0x80 ||
			pLeader->Status == CM_CONTROL_NEWDSX_MULTICLASSIFIER_REQ )

		{
			if((pLeader->Status == LINK_UP_CONTROL_REQ) && (pLinkReq->szData[0]==LINK_DOWN_REQ_PAYLOAD))
			{
				if((pLinkReq->szData[1] == LINK_SYNC_DOWN_SUBTYPE))
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL,DBG_LVL_ALL, "Link Down Sent in Idle Mode\n");
					Adapter->usIdleModePattern = ABORT_IDLE_SYNCDOWN;//LINK DOWN sent in Idle Mode
				}
				else
				{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL,DBG_LVL_ALL,"ABORT_IDLE_MODE pattern is being written\n");
					Adapter->usIdleModePattern = ABORT_IDLE_REG;
				}
			}
			else
			{
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL,DBG_LVL_ALL,"ABORT_IDLE_MODE pattern is being written\n");
					Adapter->usIdleModePattern = ABORT_IDLE_MODE;
			}

			/*Setting bIdleMode_tx_from_host to TRUE to indicate LED control thread to represent
			  the wake up from idlemode is from host*/
			//Adapter->LEDInfo.bIdleMode_tx_from_host = TRUE;
#if 0
			if(STATUS_SUCCESS != InterfaceIdleModeWakeup(Adapter))
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL, "Idle Mode Wake up Failed\n");
				return STATUS_FAILURE;
			}
#endif
			Adapter->bWakeUpDevice = TRUE;
			wake_up(&Adapter->process_rx_cntrlpkt);



			if(LINK_DOWN_REQ_PAYLOAD == pLinkReq->szData[0])
			{
				// We should not send DREG message down while in idlemode.
				return STATUS_SUCCESS;
			}

			Status = wait_event_interruptible_timeout(Adapter->lowpower_mode_wait_queue,
				!Adapter->IdleMode, (5 * HZ));

			if(Status == -ERESTARTSYS)
				return Status;

			if(Adapter->IdleMode)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL,DBG_LVL_ALL, "Idle Mode Wake up Failed - No Wake Up Received\n");
				return STATUS_FAILURE;
			}
		}
		else
			return STATUS_SUCCESS;
	}
	//The Driver has to send control messages with a particular VCID
	pLeader->Vcid = VCID_CONTROL_PACKET;//VCID for control packet.

	/* Allocate skb for Control Packet */
	pktlen = pLeader->PLength;
	ctrl_buff = (char *)Adapter->txctlpacket[atomic_read(&Adapter->index_wr_txcntrlpkt)%MAX_CNTRL_PKTS];

	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL,DBG_LVL_ALL, "Control packet to be taken =%d and address is =%pincoming address is =%p and packet len=%x",
								atomic_read(&Adapter->index_wr_txcntrlpkt), ctrl_buff, ioBuffer, pktlen);
	if(ctrl_buff)
	{
		if(pLeader)
		{
			if((pLeader->Status == 0x80) ||
				(pLeader->Status == CM_CONTROL_NEWDSX_MULTICLASSIFIER_REQ))
			{
				/*
				//Restructure the DSX message to handle Multiple classifier Support
				// Write the Service Flow param Structures directly to the target
				//and embed the pointers in the DSX messages sent to target.
				*/
				//Lets store the current length of the control packet we are transmitting
				pucAddIndication = (PUCHAR)ioBuffer + LEADER_SIZE;
				pktlen = pLeader->PLength;
				Status = StoreCmControlResponseMessage(Adapter,pucAddIndication, &pktlen);
				if(Status != 1)
				{
					ClearTargetDSXBuffer(Adapter,((stLocalSFAddIndicationAlt *)pucAddIndication)->u16TID, FALSE);
					BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, " Error Restoring The DSX Control Packet. Dsx Buffers on Target may not be Setup Properly ");
					return STATUS_FAILURE;
				}
				/*
				//update the leader to use the new length
				//The length of the control packet is length of message being sent + Leader length
				*/
				pLeader->PLength = pktlen;
			}
		}
		memset(ctrl_buff, 0, pktlen+LEADER_SIZE);
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "Copying the Control Packet Buffer with length=%d\n", pLeader->PLength);
		*(PLEADER)ctrl_buff=*pLeader;
		memcpy(ctrl_buff + LEADER_SIZE, ((PUCHAR)ioBuffer + LEADER_SIZE), pLeader->PLength);
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "Enqueuing the Control Packet");

		/*Update the statistics counters */
		spin_lock_bh(&Adapter->PackInfo[HiPriority].SFQueueLock);
		Adapter->PackInfo[HiPriority].uiCurrentBytesOnHost+=pLeader->PLength;
		Adapter->PackInfo[HiPriority].uiCurrentPacketsOnHost++;
		atomic_inc(&Adapter->TotalPacketCount);
		spin_unlock_bh(&Adapter->PackInfo[HiPriority].SFQueueLock);

		Adapter->PackInfo[HiPriority].bValid = TRUE;

		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "CurrBytesOnHost: %x bValid: %x",
			Adapter->PackInfo[HiPriority].uiCurrentBytesOnHost,
			Adapter->PackInfo[HiPriority].bValid);
		Status=STATUS_SUCCESS;
		/*Queue the packet for transmission */
		atomic_inc(&Adapter->index_wr_txcntrlpkt);
		BCM_DEBUG_PRINT( Adapter,DBG_TYPE_TX, TX_CONTROL,DBG_LVL_ALL, "Calling transmit_packets");
		atomic_set(&Adapter->TxPktAvail, 1);
#ifdef BCM_SHM_INTERFACE
		virtual_mail_box_interrupt();
#endif
		wake_up(&Adapter->tx_packet_wait_queue);
	}
	else
	{
		Status=-ENOMEM;
		BCM_DEBUG_PRINT( Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "mem allocation Failed");
    }
	BCM_DEBUG_PRINT( Adapter,DBG_TYPE_TX, TX_CONTROL, DBG_LVL_ALL, "<====");
	return Status;
}

#if 0
/*****************************************************************
* Function    - SendStatisticsPointerRequest()
*
* Description - This function builds and forwards the Statistics
*				Pointer Request control Packet.
*
* Parameters  - Adapter					: Pointer to Adapter structure.
* 			  - pstStatisticsPtrRequest : Pointer to link request.
*
* Returns     - None.
*****************************************************************/
static VOID SendStatisticsPointerRequest(PMINI_ADAPTER Adapter,
								PLINK_REQUEST	pstStatisticsPtrRequest)
{
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_RX, RX_DPC, DBG_LVL_ALL, "======>");
	pstStatisticsPtrRequest->Leader.Status = STATS_POINTER_REQ_STATUS;
	pstStatisticsPtrRequest->Leader.PLength = sizeof(ULONG);//minimum 4 bytes
	pstStatisticsPtrRequest->szData[0] = STATISTICS_POINTER_REQ;

	CopyBufferToControlPacket(Adapter,pstStatisticsPtrRequest);
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_RX, RX_DPC, DBG_LVL_ALL, "<=====");
	return;
}
#endif


void SendLinkDown(PMINI_ADAPTER Adapter)
{
	LINK_REQUEST	stLinkDownRequest;
	memset(&stLinkDownRequest, 0, sizeof(LINK_REQUEST));
	stLinkDownRequest.Leader.Status=LINK_UP_CONTROL_REQ;
	stLinkDownRequest.Leader.PLength=sizeof(ULONG);//minimum 4 bytes
	stLinkDownRequest.szData[0]=LINK_DOWN_REQ_PAYLOAD;
	Adapter->bLinkDownRequested = TRUE;

	CopyBufferToControlPacket(Adapter,&stLinkDownRequest);
}

/******************************************************************
* Function    - LinkMessage()
*
* Description - This function builds the Sync-up and Link-up request
*				packet messages depending on the device Link status.
*
* Parameters  - Adapter:	Pointer to the Adapter structure.
*
* Returns     - None.
*******************************************************************/
__inline VOID LinkMessage(PMINI_ADAPTER Adapter)
{
	PLINK_REQUEST	pstLinkRequest=NULL;
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, LINK_UP_MSG, DBG_LVL_ALL, "=====>");
	if(Adapter->LinkStatus == SYNC_UP_REQUEST && Adapter->AutoSyncup)
	{
		pstLinkRequest=kmalloc(sizeof(LINK_REQUEST), GFP_ATOMIC);
		if(!pstLinkRequest)
		{
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, LINK_UP_MSG, DBG_LVL_ALL, "Can not allocate memory for Link request!");
			return;
		}
		memset(pstLinkRequest,0,sizeof(LINK_REQUEST));
		//sync up request...
		Adapter->LinkStatus = WAIT_FOR_SYNC;// current link status
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, LINK_UP_MSG, DBG_LVL_ALL, "Requesting For SyncUp...");
		pstLinkRequest->szData[0]=LINK_UP_REQ_PAYLOAD;
		pstLinkRequest->szData[1]=LINK_SYNC_UP_SUBTYPE;
		pstLinkRequest->Leader.Status=LINK_UP_CONTROL_REQ;
		pstLinkRequest->Leader.PLength=sizeof(ULONG);
		Adapter->bSyncUpRequestSent = TRUE;
	}
	else if(Adapter->LinkStatus == PHY_SYNC_ACHIVED && Adapter->AutoLinkUp)
	{
		pstLinkRequest=kmalloc(sizeof(LINK_REQUEST), GFP_ATOMIC);
		if(!pstLinkRequest)
		{
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, LINK_UP_MSG, DBG_LVL_ALL, "Can not allocate memory for Link request!");
			return;
		}
		memset(pstLinkRequest,0,sizeof(LINK_REQUEST));
		//LINK_UP_REQUEST
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, LINK_UP_MSG, DBG_LVL_ALL, "Requesting For LinkUp...");
		pstLinkRequest->szData[0]=LINK_UP_REQ_PAYLOAD;
		pstLinkRequest->szData[1]=LINK_NET_ENTRY;
		pstLinkRequest->Leader.Status=LINK_UP_CONTROL_REQ;
		pstLinkRequest->Leader.PLength=sizeof(ULONG);
	}
	if(pstLinkRequest)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, LINK_UP_MSG, DBG_LVL_ALL, "Calling CopyBufferToControlPacket");
		CopyBufferToControlPacket(Adapter, pstLinkRequest);
		bcm_kfree(pstLinkRequest);
	}
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, LINK_UP_MSG, DBG_LVL_ALL, "LinkMessage <=====");
	return;
}


/**********************************************************************
* Function    - StatisticsResponse()
*
* Description - This function handles the Statistics response packet.
*
* Parameters  - Adapter	: Pointer to the Adapter structure.
* 			  - pvBuffer: Starting address of Statistic response data.
*
* Returns     - None.
************************************************************************/
VOID StatisticsResponse(PMINI_ADAPTER Adapter,PVOID pvBuffer)
{
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "%s====>",__FUNCTION__);
	Adapter->StatisticsPointer = ntohl(*(PULONG)pvBuffer);
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "Stats at %lx", Adapter->StatisticsPointer);
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "%s <====",__FUNCTION__);
	return;
}


/**********************************************************************
* Function    - LinkControlResponseMessage()
*
* Description - This function handles the Link response packets.
*
* Parameters  - Adapter	 : Pointer to the Adapter structure.
* 			  - pucBuffer: Starting address of Link response data.
*
* Returns     - None.
***********************************************************************/
VOID LinkControlResponseMessage(PMINI_ADAPTER Adapter,PUCHAR pucBuffer)
{
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_RX, RX_DPC, DBG_LVL_ALL, "=====>");

	if(*pucBuffer==LINK_UP_ACK)
	{
		switch(*(pucBuffer+1))
		{
			case PHY_SYNC_ACHIVED: //SYNCed UP
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "PHY_SYNC_ACHIVED");

				if(Adapter->LinkStatus == LINKUP_DONE)
			   	{
					beceem_protocol_reset(Adapter);
				}

				Adapter->usBestEffortQueueIndex=INVALID_QUEUE_INDEX ;
				Adapter->LinkStatus=PHY_SYNC_ACHIVED;

				if(Adapter->LEDInfo.led_thread_running & BCM_LED_THREAD_RUNNING_ACTIVELY)
				{
					Adapter->DriverState = NO_NETWORK_ENTRY;
					wake_up(&Adapter->LEDInfo.notify_led_event);
				}

				LinkMessage(Adapter);
				break;

			case LINKUP_DONE:
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_RX, RX_DPC, DBG_LVL_ALL, "LINKUP_DONE");
				Adapter->LinkStatus=LINKUP_DONE;
				Adapter->bPHSEnabled = *(pucBuffer+3);
               	Adapter->bETHCSEnabled = *(pucBuffer+4) & ETH_CS_MASK;
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "PHS Support Status Recieved In LinkUp Ack : %x \n",Adapter->bPHSEnabled);
				if((FALSE == Adapter->bShutStatus)&&
					(FALSE == Adapter->IdleMode))
				{
					if(Adapter->LEDInfo.led_thread_running & BCM_LED_THREAD_RUNNING_ACTIVELY)
					{
						Adapter->DriverState = NORMAL_OPERATION;
						wake_up(&Adapter->LEDInfo.notify_led_event);
					}
				}
				LinkMessage(Adapter);
				break;
			case WAIT_FOR_SYNC:

				/*
				 * Driver to ignore the DREG_RECEIVED
				 * WiMAX Application should handle this Message
				 */
				//Adapter->liTimeSinceLastNetEntry = 0;
				Adapter->LinkUpStatus = 0;
				Adapter->LinkStatus = 0;
				Adapter->usBestEffortQueueIndex=INVALID_QUEUE_INDEX ;
				Adapter->bTriedToWakeUpFromlowPowerMode = FALSE;
				Adapter->IdleMode = FALSE;
				beceem_protocol_reset(Adapter);

				break;
			case LINK_SHUTDOWN_REQ_FROM_FIRMWARE:
			case COMPLETE_WAKE_UP_NOTIFICATION_FRM_FW:
			{
				HandleShutDownModeRequest(Adapter, pucBuffer);
			}
				break;
			default:
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "default case:LinkResponse %x",*(pucBuffer+1));
				break;
		}
	}
	else if(SET_MAC_ADDRESS_RESPONSE==*pucBuffer)
	{
		PUCHAR puMacAddr = (pucBuffer + 1);
		Adapter->LinkStatus=SYNC_UP_REQUEST;
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_RX, RX_DPC, DBG_LVL_ALL, "MAC address response, sending SYNC_UP");
		LinkMessage(Adapter);
		memcpy(Adapter->dev->dev_addr, puMacAddr, MAC_ADDRESS_SIZE);
	}
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_RX, RX_DPC, DBG_LVL_ALL, "%s <=====",__FUNCTION__);
	return;
}

void SendIdleModeResponse(PMINI_ADAPTER Adapter)
{
	INT status = 0, NVMAccess = 0,lowPwrAbortMsg = 0;
	struct timeval tv;
	CONTROL_MESSAGE		stIdleResponse = {{0}};
	memset(&tv, 0, sizeof(tv));
	stIdleResponse.Leader.Status  = IDLE_MESSAGE;
	stIdleResponse.Leader.PLength = IDLE_MODE_PAYLOAD_LENGTH;
	stIdleResponse.szData[0] = GO_TO_IDLE_MODE_PAYLOAD;
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_RX, RX_DPC, DBG_LVL_ALL," ============>");

	/*********************************
	**down_trylock -
	** if [ semaphore is available ]
	**		 acquire semaphone and return value 0 ;
	**   else
	**		 return non-zero value ;
	**
	***********************************/

	NVMAccess = down_trylock(&Adapter->NVMRdmWrmLock);

	lowPwrAbortMsg= down_trylock(&Adapter->LowPowerModeSync);


	if((NVMAccess || lowPwrAbortMsg || atomic_read(&Adapter->TotalPacketCount)) &&
		  (Adapter->ulPowerSaveMode != DEVICE_POWERSAVE_MODE_AS_PROTOCOL_IDLE_MODE)  )
	{
		if(!NVMAccess)
			up(&Adapter->NVMRdmWrmLock);

		if(!lowPwrAbortMsg)
			up(&Adapter->LowPowerModeSync);

		stIdleResponse.szData[1] = TARGET_CAN_NOT_GO_TO_IDLE_MODE;//NACK- device access is going on.
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_RX, RX_DPC, DBG_LVL_ALL, "HOST IS NACKING Idle mode To F/W!!!!!!!!");
		Adapter->bPreparingForLowPowerMode = FALSE;
	}
	else
	{
		stIdleResponse.szData[1] = TARGET_CAN_GO_TO_IDLE_MODE; //2;//Idle ACK
		Adapter->StatisticsPointer = 0;

		/* Wait for the LED to TURN OFF before sending ACK response */
		if(Adapter->LEDInfo.led_thread_running & BCM_LED_THREAD_RUNNING_ACTIVELY)
		{
			INT iRetVal = 0;

			/* Wake the LED Thread with IDLEMODE_ENTER State */
			Adapter->DriverState = LOWPOWER_MODE_ENTER;
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_RX, RX_DPC, DBG_LVL_ALL,"LED Thread is Running..Hence Setting LED Event as IDLEMODE_ENTER jiffies:%ld",jiffies);;
			wake_up(&Adapter->LEDInfo.notify_led_event);

			/* Wait for 1 SEC for LED to OFF */
			iRetVal = wait_event_timeout(Adapter->LEDInfo.idleModeSyncEvent, \
				Adapter->LEDInfo.bIdle_led_off, msecs_to_jiffies(1000));


			/* If Timed Out to Sync IDLE MODE Enter, do IDLE mode Exit and Send NACK to device */
			if(iRetVal <= 0)
			{
				stIdleResponse.szData[1] = TARGET_CAN_NOT_GO_TO_IDLE_MODE;//NACK- device access is going on.
				Adapter->DriverState = NORMAL_OPERATION;
				wake_up(&Adapter->LEDInfo.notify_led_event);
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_RX, RX_DPC, DBG_LVL_ALL, "NACKING Idle mode as time out happen from LED side!!!!!!!!");
			}
		}
		if(stIdleResponse.szData[1] == TARGET_CAN_GO_TO_IDLE_MODE)
		{
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_RX, RX_DPC, DBG_LVL_ALL,"ACKING IDLE MODE !!!!!!!!!");
			down(&Adapter->rdmwrmsync);
			Adapter->bPreparingForLowPowerMode = TRUE;
			up(&Adapter->rdmwrmsync);
#ifndef BCM_SHM_INTERFACE
			//Killing all URBS.
			if(Adapter->bDoSuspend == TRUE)
				Bcm_kill_all_URBs((PS_INTERFACE_ADAPTER)(Adapter->pvInterfaceAdapter));

#endif
		}
		else
		{
			Adapter->bPreparingForLowPowerMode = FALSE;
		}

		if(!NVMAccess)
			up(&Adapter->NVMRdmWrmLock);

		if(!lowPwrAbortMsg)
			up(&Adapter->LowPowerModeSync);

	}
	status = CopyBufferToControlPacket(Adapter,&stIdleResponse);
	if((status != STATUS_SUCCESS))
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"fail to send the Idle mode Request \n");
		Adapter->bPreparingForLowPowerMode = FALSE;
#ifndef BCM_SHM_INTERFACE
		StartInterruptUrb((PS_INTERFACE_ADAPTER)(Adapter->pvInterfaceAdapter));
#endif
	}
	do_gettimeofday(&tv);
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_RX, RX_DPC, DBG_LVL_ALL, "IdleMode Msg submitter to Q :%ld ms", tv.tv_sec *1000 + tv.tv_usec /1000);

}

/******************************************************************
* Function    - DumpPackInfo()
*
* Description - This function dumps the all Queue(PackInfo[]) details.
*
* Parameters  - Adapter: Pointer to the Adapter structure.
*
* Returns     - None.
*******************************************************************/
VOID DumpPackInfo(PMINI_ADAPTER Adapter)
{

    UINT uiLoopIndex = 0;
	UINT uiIndex = 0;
	UINT uiClsfrIndex = 0;
	S_CLASSIFIER_RULE *pstClassifierEntry = NULL;

	for(uiLoopIndex=0;uiLoopIndex<NO_OF_QUEUES;uiLoopIndex++)
	{
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"*********** Showing Details Of Queue %d***** ******",uiLoopIndex);
		if(FALSE == Adapter->PackInfo[uiLoopIndex].bValid)
		{
			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"bValid is FALSE for %X index\n",uiLoopIndex);
			continue;
		}

		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"	Dumping	SF Rule Entry For SFID %lX \n",Adapter->PackInfo[uiLoopIndex].ulSFID);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"	ucDirection %X \n",Adapter->PackInfo[uiLoopIndex].ucDirection);
		if(Adapter->PackInfo[uiLoopIndex].ucIpVersion == IPV6)
		{
			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"Ipv6 Service Flow \n");
		}
		else
		{
			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"Ipv4 Service Flow \n");
		}
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"	SF Traffic Priority %X \n",Adapter->PackInfo[uiLoopIndex].u8TrafficPriority);

		for(uiClsfrIndex=0;uiClsfrIndex<MAX_CLASSIFIERS;uiClsfrIndex++)
		{
			pstClassifierEntry = &Adapter->astClassifierTable[uiClsfrIndex];
			if(!pstClassifierEntry->bUsed)
				continue;

			if(pstClassifierEntry->ulSFID != Adapter->PackInfo[uiLoopIndex].ulSFID)
				continue;

			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tDumping Classifier Rule Entry For Index: %X Classifier Rule ID : %X\n",uiClsfrIndex,pstClassifierEntry->uiClassifierRuleIndex);
			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tDumping Classifier Rule Entry For Index: %X usVCID_Value : %X\n",uiClsfrIndex,pstClassifierEntry->usVCID_Value);
			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tDumping Classifier Rule Entry For Index: %X bProtocolValid : %X\n",uiClsfrIndex,pstClassifierEntry->bProtocolValid);
			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tDumping	Classifier Rule Entry For Index: %X bTOSValid : %X\n",uiClsfrIndex,pstClassifierEntry->bTOSValid);
			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tDumping	Classifier Rule Entry For Index: %X bDestIpValid : %X\n",uiClsfrIndex,pstClassifierEntry->bDestIpValid);
			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tDumping	Classifier Rule Entry For Index: %X bSrcIpValid : %X\n",uiClsfrIndex,pstClassifierEntry->bSrcIpValid);


			for(uiIndex=0;uiIndex<MAX_PORT_RANGE;uiIndex++)
			{
				BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tusSrcPortRangeLo:%X\n",pstClassifierEntry->usSrcPortRangeLo[uiIndex]);
				BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tusSrcPortRangeHi:%X\n",pstClassifierEntry->usSrcPortRangeHi[uiIndex]);
				BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tusDestPortRangeLo:%X\n",pstClassifierEntry->usDestPortRangeLo[uiIndex]);
				BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tusDestPortRangeHi:%X\n",pstClassifierEntry->usDestPortRangeHi[uiIndex]);
			}

			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL," \tucIPSourceAddressLength : 0x%x\n",pstClassifierEntry->ucIPSourceAddressLength);
			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tucIPDestinationAddressLength : 0x%x\n",pstClassifierEntry->ucIPDestinationAddressLength);
			for(uiIndex=0;uiIndex<pstClassifierEntry->ucIPSourceAddressLength;uiIndex++)
			{
				if(Adapter->PackInfo[uiLoopIndex].ucIpVersion == IPV6)
				{
					BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tIpv6 ulSrcIpAddr :\n");
					DumpIpv6Address(pstClassifierEntry->stSrcIpAddress.ulIpv6Addr);
					BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tIpv6 ulSrcIpMask :\n");
					DumpIpv6Address(pstClassifierEntry->stSrcIpAddress.ulIpv6Mask);
				}
				else
				{
				BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tulSrcIpAddr:%lX\n",pstClassifierEntry->stSrcIpAddress.ulIpv4Addr[uiIndex]);
				BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tulSrcIpMask:%lX\n",pstClassifierEntry->stSrcIpAddress.ulIpv4Mask[uiIndex]);
				}
			}
			for(uiIndex=0;uiIndex<pstClassifierEntry->ucIPDestinationAddressLength;uiIndex++)
			{
				if(Adapter->PackInfo[uiLoopIndex].ucIpVersion == IPV6)
				{
					BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tIpv6 ulDestIpAddr :\n");
					DumpIpv6Address(pstClassifierEntry->stDestIpAddress.ulIpv6Addr);
					BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tIpv6 ulDestIpMask :\n");
					DumpIpv6Address(pstClassifierEntry->stDestIpAddress.ulIpv6Mask);

				}
				else
				{
					BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tulDestIpAddr:%lX\n",pstClassifierEntry->stDestIpAddress.ulIpv4Addr[uiIndex]);
					BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tulDestIpMask:%lX\n",pstClassifierEntry->stDestIpAddress.ulIpv4Mask[uiIndex]);
				}
			}
			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tucProtocol:0x%X\n",pstClassifierEntry->ucProtocol[0]);
			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"\tu8ClassifierRulePriority:%X\n",pstClassifierEntry->u8ClassifierRulePriority);


		}
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"ulSFID:%lX\n",Adapter->PackInfo[uiLoopIndex].ulSFID);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"usVCID_Value:%X\n",Adapter->PackInfo[uiLoopIndex].usVCID_Value);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"PhsEnabled: 0x%X\n",Adapter->PackInfo[uiLoopIndex].bHeaderSuppressionEnabled);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"uiThreshold:%X\n",Adapter->PackInfo[uiLoopIndex].uiThreshold);


		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"bValid:%X\n",Adapter->PackInfo[uiLoopIndex].bValid);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"bActive:%X\n",Adapter->PackInfo[uiLoopIndex].bActive);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"ActivateReqSent: %x", Adapter->PackInfo[uiLoopIndex].bActivateRequestSent);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"u8QueueType:%X\n",Adapter->PackInfo[uiLoopIndex].u8QueueType);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"uiMaxBucketSize:%X\n",Adapter->PackInfo[uiLoopIndex].uiMaxBucketSize);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"uiPerSFTxResourceCount:%X\n",atomic_read(&Adapter->PackInfo[uiLoopIndex].uiPerSFTxResourceCount));
		//DumpDebug(DUMP_INFO,("				bCSSupport:%X\n",Adapter->PackInfo[uiLoopIndex].bCSSupport));
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"CurrQueueDepthOnTarget: %x\n", Adapter->PackInfo[uiLoopIndex].uiCurrentQueueDepthOnTarget);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"uiCurrentBytesOnHost:%X\n",Adapter->PackInfo[uiLoopIndex].uiCurrentBytesOnHost);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"uiCurrentPacketsOnHost:%X\n",Adapter->PackInfo[uiLoopIndex].uiCurrentPacketsOnHost);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"uiDroppedCountBytes:%X\n",Adapter->PackInfo[uiLoopIndex].uiDroppedCountBytes);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"uiDroppedCountPackets:%X\n",Adapter->PackInfo[uiLoopIndex].uiDroppedCountPackets);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"uiSentBytes:%X\n",Adapter->PackInfo[uiLoopIndex].uiSentBytes);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"uiSentPackets:%X\n",Adapter->PackInfo[uiLoopIndex].uiSentPackets);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"uiCurrentDrainRate:%X\n",Adapter->PackInfo[uiLoopIndex].uiCurrentDrainRate);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"uiThisPeriodSentBytes:%X\n",Adapter->PackInfo[uiLoopIndex].uiThisPeriodSentBytes);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"liDrainCalculated:%llX\n",Adapter->PackInfo[uiLoopIndex].liDrainCalculated);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"uiCurrentTokenCount:%X\n",Adapter->PackInfo[uiLoopIndex].uiCurrentTokenCount);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"liLastUpdateTokenAt:%llX\n",Adapter->PackInfo[uiLoopIndex].liLastUpdateTokenAt);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"uiMaxAllowedRate:%X\n",Adapter->PackInfo[uiLoopIndex].uiMaxAllowedRate);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"uiPendedLast:%X\n",Adapter->PackInfo[uiLoopIndex].uiPendedLast);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"NumOfPacketsSent:%X\n",Adapter->PackInfo[uiLoopIndex].NumOfPacketsSent);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "Direction: %x\n", Adapter->PackInfo[uiLoopIndex].ucDirection);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "CID: %x\n", Adapter->PackInfo[uiLoopIndex].usCID);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "ProtocolValid: %x\n", Adapter->PackInfo[uiLoopIndex].bProtocolValid);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "TOSValid: %x\n", Adapter->PackInfo[uiLoopIndex].bTOSValid);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "DestIpValid: %x\n", Adapter->PackInfo[uiLoopIndex].bDestIpValid);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "SrcIpValid: %x\n", Adapter->PackInfo[uiLoopIndex].bSrcIpValid);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "ActiveSet: %x\n", Adapter->PackInfo[uiLoopIndex].bActiveSet);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "AdmittedSet: %x\n", Adapter->PackInfo[uiLoopIndex].bAdmittedSet);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "AuthzSet: %x\n", Adapter->PackInfo[uiLoopIndex].bAuthorizedSet);
		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "ClassifyPrority: %x\n", Adapter->PackInfo[uiLoopIndex].bClassifierPriority);
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "uiMaxLatency: %x\n",Adapter->PackInfo[uiLoopIndex].uiMaxLatency);
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "ServiceClassName: %x %x %x %x\n",Adapter->PackInfo[uiLoopIndex].ucServiceClassName[0],Adapter->PackInfo[uiLoopIndex].ucServiceClassName[1],Adapter->PackInfo[uiLoopIndex].ucServiceClassName[2],Adapter->PackInfo[uiLoopIndex].ucServiceClassName[3]);
//	BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "bHeaderSuppressionEnabled :%X\n", Adapter->PackInfo[uiLoopIndex].bHeaderSuppressionEnabled);
//		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "uiTotalTxBytes:%X\n", Adapter->PackInfo[uiLoopIndex].uiTotalTxBytes);
//		BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL, "uiTotalRxBytes:%X\n", Adapter->PackInfo[uiLoopIndex].uiTotalRxBytes);
//		DumpDebug(DUMP_INFO,("				uiRanOutOfResCount:%X\n",Adapter->PackInfo[uiLoopIndex].uiRanOutOfResCount));
	}

	for(uiLoopIndex = 0 ; uiLoopIndex < MIBS_MAX_HIST_ENTRIES ; uiLoopIndex++)
			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"Adapter->aRxPktSizeHist[%x] = %x\n",uiLoopIndex,Adapter->aRxPktSizeHist[uiLoopIndex]);

	for(uiLoopIndex = 0 ; uiLoopIndex < MIBS_MAX_HIST_ENTRIES ; uiLoopIndex++)
			BCM_DEBUG_PRINT (Adapter, DBG_TYPE_OTHERS, DUMP_INFO, DBG_LVL_ALL,"Adapter->aTxPktSizeHist[%x] = %x\n",uiLoopIndex,Adapter->aTxPktSizeHist[uiLoopIndex]);



	return;


}


__inline int reset_card_proc(PMINI_ADAPTER ps_adapter)
{
	int retval = STATUS_SUCCESS;

#ifndef BCM_SHM_INTERFACE
    PMINI_ADAPTER Adapter = GET_BCM_ADAPTER(gblpnetdev);
	PS_INTERFACE_ADAPTER psIntfAdapter = NULL;
	unsigned int value = 0, uiResetValue = 0;

	psIntfAdapter = ((PS_INTERFACE_ADAPTER)(ps_adapter->pvInterfaceAdapter)) ;

	ps_adapter->bDDRInitDone = FALSE;

	if(ps_adapter->chip_id >= T3LPB)
	{
		//SYS_CFG register is write protected hence for modifying this reg value, it should be read twice before
		rdmalt(ps_adapter,SYS_CFG, &value, sizeof(value));
		rdmalt(ps_adapter,SYS_CFG, &value, sizeof(value));

		//making bit[6...5] same as was before f/w download. this setting force the h/w to
		//re-populated the SP RAM area with the string descriptor .
		value = value | (ps_adapter->syscfgBefFwDld & 0x00000060) ;
		wrmalt(ps_adapter, SYS_CFG, &value, sizeof(value));
	}

#ifndef BCM_SHM_INTERFACE
	//killing all submitted URBs.
	psIntfAdapter->psAdapter->StopAllXaction = TRUE ;
	Bcm_kill_all_URBs(psIntfAdapter);
#endif
	/* Reset the UMA-B Device */
	if(ps_adapter->chip_id >= T3LPB)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Reseting UMA-B \n");
		retval = usb_reset_device(psIntfAdapter->udev);

		psIntfAdapter->psAdapter->StopAllXaction = FALSE ;

		if(retval != STATUS_SUCCESS)
		{
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Reset failed with ret value :%d", retval);
			goto err_exit;
		}
		if (ps_adapter->chip_id == BCS220_2 ||
			ps_adapter->chip_id == BCS220_2BC ||
			ps_adapter->chip_id == BCS250_BC ||
				ps_adapter->chip_id == BCS220_3)
		{
			retval = rdmalt(ps_adapter,HPM_CONFIG_LDO145, &value, sizeof(value));
			if( retval < 0)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"read failed with status :%d",retval);
				goto err_exit;
			}
			//setting 0th bit
			value |= (1<<0);
			retval = wrmalt(ps_adapter, HPM_CONFIG_LDO145, &value, sizeof(value));
			if( retval < 0)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"write failed with status :%d",retval);
				goto err_exit;
			}
		}

	}
	else
	{
		retval = rdmalt(ps_adapter,0x0f007018, &value, sizeof(value));
		if( retval < 0) {
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"read failed with status :%d",retval);
			goto err_exit;
		}
		value&=(~(1<<16));
		retval= wrmalt(ps_adapter, 0x0f007018, &value, sizeof(value)) ;
		if( retval < 0) {
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"write failed with status :%d",retval);
			goto err_exit;
		}

		// Toggling the GPIO 8, 9
		value = 0;
		retval = wrmalt(ps_adapter, GPIO_OUTPUT_REGISTER, &value, sizeof(value));
		if(retval < 0) {
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"write failed with status :%d",retval);
			goto err_exit;
		}
		value = 0x300;
		retval = wrmalt(ps_adapter, GPIO_MODE_REGISTER, &value, sizeof(value)) ;
		if(retval < 0) {
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"write failed with status :%d",retval);
			goto err_exit;
		}
		mdelay(50);
	}

	//ps_adapter->downloadDDR = false;

	if(ps_adapter->bFlashBoot)
	{
		//In flash boot mode MIPS state register has reverse polarity.
		// So just or with setting bit 30.
		//Make the MIPS in Reset state.
		rdmalt(ps_adapter, CLOCK_RESET_CNTRL_REG_1, &uiResetValue, sizeof(uiResetValue));

		uiResetValue |=(1<<30);
		wrmalt(ps_adapter, CLOCK_RESET_CNTRL_REG_1, &uiResetValue, sizeof(uiResetValue));
	}

	if(ps_adapter->chip_id >= T3LPB)
	{
		uiResetValue = 0;
		//
		// WA for SYSConfig Issue.
		// Read SYSCFG Twice to make it writable.
		//
		rdmalt(ps_adapter, SYS_CFG, &uiResetValue, sizeof(uiResetValue));
		if(uiResetValue & (1<<4))
		{
			uiResetValue = 0;
			rdmalt(ps_adapter, SYS_CFG, &uiResetValue, sizeof(uiResetValue));//2nd read to make it writable.
			uiResetValue &= (~(1<<4));
			wrmalt(ps_adapter,SYS_CFG, &uiResetValue, sizeof(uiResetValue));
		}

	}
	uiResetValue = 0;
	wrmalt(ps_adapter, 0x0f01186c, &uiResetValue, sizeof(uiResetValue));

err_exit :
	psIntfAdapter->psAdapter->StopAllXaction = FALSE ;
#endif
	return retval;
}

__inline int run_card_proc(PMINI_ADAPTER ps_adapter )
{
	unsigned int value=0;
	{

		if(rdmalt(ps_adapter, CLOCK_RESET_CNTRL_REG_1, &value, sizeof(value)) < 0) {
			BCM_DEBUG_PRINT(ps_adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL,"%s:%d\n", __FUNCTION__, __LINE__);
			return STATUS_FAILURE;
		}

		if(ps_adapter->bFlashBoot)
		{

			value&=(~(1<<30));
		}
		else
		{
			value |=(1<<30);
		}

		if(wrmalt(ps_adapter, CLOCK_RESET_CNTRL_REG_1, &value, sizeof(value)) < 0) {
			BCM_DEBUG_PRINT(ps_adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL,"%s:%d\n", __FUNCTION__, __LINE__);
			return STATUS_FAILURE;
		}
	}
	return STATUS_SUCCESS;
}

int InitCardAndDownloadFirmware(PMINI_ADAPTER ps_adapter)
{

	UINT status = STATUS_SUCCESS;
	UINT value = 0;
#ifdef BCM_SHM_INTERFACE
	unsigned char *pConfigFileAddr = (unsigned char *)CPE_MACXVI_CFG_ADDR;
#endif
	/*
 	 * Create the threads first and then download the
 	 * Firm/DDR Settings..
 	 */

	if((status = create_worker_threads(ps_adapter))<0)
	{
		BCM_DEBUG_PRINT(ps_adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Cannot create thread");
		return status;
	}
	/*
 	 * For Downloading the Firm, parse the cfg file first.
 	 */
	status = bcm_parse_target_params (ps_adapter);
	if(status){
		return status;
	}

#ifndef BCM_SHM_INTERFACE
	if(ps_adapter->chip_id >= T3LPB)
	{
		rdmalt(ps_adapter, SYS_CFG, &value, sizeof (value));
		ps_adapter->syscfgBefFwDld = value ;
		if((value & 0x60)== 0)
		{
			ps_adapter->bFlashBoot = TRUE;
		}
	}

	reset_card_proc(ps_adapter);

	//Initializing the NVM.
	BcmInitNVM(ps_adapter);
	status = ddr_init(ps_adapter);
	if(status)
	{
		BCM_DEBUG_PRINT (ps_adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "ddr_init Failed\n");
		return status;
	}

	/* Download cfg file */
	status = buffDnldVerify(ps_adapter,
							 (PUCHAR)ps_adapter->pstargetparams,
							 sizeof(STARGETPARAMS),
							 CONFIG_BEGIN_ADDR);
	if(status)
	{
		BCM_DEBUG_PRINT(ps_adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Error downloading CFG file");
		goto OUT;
	}
	BCM_DEBUG_PRINT(ps_adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "CFG file downloaded");

	if(register_networkdev(ps_adapter))
	{
		BCM_DEBUG_PRINT(ps_adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Register Netdevice failed. Cleanup needs to be performed.");
		return -EIO;
	}

	if(FALSE == ps_adapter->AutoFirmDld)
	{
		BCM_DEBUG_PRINT(ps_adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "AutoFirmDld Disabled in CFG File..\n");
		//If Auto f/w download is disable, register the control interface,
		//register the control interface after the mailbox.
		if(register_control_device_interface(ps_adapter) < 0)
		{
			BCM_DEBUG_PRINT(ps_adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Register Control Device failed. Cleanup needs to be performed.");
			return -EIO;
		}

		return STATUS_SUCCESS;
	}

	/*
     * Do the LED Settings here. It will be used by the Firmware Download
     * Thread.
     */

	/*
     * 1. If the LED Settings fails, do not stop and do the Firmware download.
     * 2. This init would happend only if the cfg file is present, else
     *    call from the ioctl context.
     */

	status = InitLedSettings (ps_adapter);

	if(status)
	{
		BCM_DEBUG_PRINT(ps_adapter,DBG_TYPE_PRINTK, 0, 0,"INIT LED FAILED\n");
		return status;
	}
	if(ps_adapter->LEDInfo.led_thread_running & BCM_LED_THREAD_RUNNING_ACTIVELY)
	{
		ps_adapter->DriverState = DRIVER_INIT;
		wake_up(&ps_adapter->LEDInfo.notify_led_event);
	}

	if(ps_adapter->LEDInfo.led_thread_running & BCM_LED_THREAD_RUNNING_ACTIVELY)
	{
		ps_adapter->DriverState = FW_DOWNLOAD;
		wake_up(&ps_adapter->LEDInfo.notify_led_event);
	}

	value = 0;
	wrmalt(ps_adapter, EEPROM_CAL_DATA_INTERNAL_LOC - 4, &value, sizeof(value));
	wrmalt(ps_adapter, EEPROM_CAL_DATA_INTERNAL_LOC - 8, &value, sizeof(value));

	if(ps_adapter->eNVMType == NVM_FLASH)
	{
		status = PropagateCalParamsFromFlashToMemory(ps_adapter);
		if(status)
		{
			BCM_DEBUG_PRINT(ps_adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL," Propogation of Cal param failed .." );
			goto OUT;
		}
	}
#if 0
	else if(psAdapter->eNVMType == NVM_EEPROM)
	{
		PropagateCalParamsFromEEPROMToMemory();
	}
#endif

	/* Download Firmare */
	if ((status = BcmFileDownload( ps_adapter, BIN_FILE, FIRMWARE_BEGIN_ADDR)))
	{
		BCM_DEBUG_PRINT(ps_adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "No Firmware File is present... \n");
		goto OUT;
	}

	BCM_DEBUG_PRINT(ps_adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "BIN file downloaded");
	status = run_card_proc(ps_adapter);
	if(status)
	{
		BCM_DEBUG_PRINT (ps_adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "run_card_proc Failed\n");
		goto OUT;
	}


	ps_adapter->fw_download_done = TRUE;
	mdelay(10);

OUT:
	if(ps_adapter->LEDInfo.led_thread_running & BCM_LED_THREAD_RUNNING_ACTIVELY)
	{
		ps_adapter->DriverState = FW_DOWNLOAD_DONE;
		wake_up(&ps_adapter->LEDInfo.notify_led_event);
	}

#else

	ps_adapter->bDDRInitDone = TRUE;
	//Initializing the NVM.
	BcmInitNVM(ps_adapter);

	//Propagating the cal param from Flash to DDR
	value = 0;
	wrmalt(ps_adapter, EEPROM_CAL_DATA_INTERNAL_LOC - 4, &value, sizeof(value));
	wrmalt(ps_adapter, EEPROM_CAL_DATA_INTERNAL_LOC - 8, &value, sizeof(value));

	if(ps_adapter->eNVMType == NVM_FLASH)
	{
		status = PropagateCalParamsFromFlashToMemory(ps_adapter);
		if(status)
		{
			printk("\nPropogation of Cal param from flash to DDR failed ..\n" );
		}
	}

	//Copy config file param to DDR.
	memcpy(pConfigFileAddr,ps_adapter->pstargetparams, sizeof(STARGETPARAMS));

	if(register_networkdev(ps_adapter))
	{
		BCM_DEBUG_PRINT(ps_adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Register Netdevice failed. Cleanup needs to be performed.");
		return -EIO;
	}


	status = InitLedSettings (ps_adapter);
	if(status)
	{
		BCM_DEBUG_PRINT(ps_adapter,DBG_TYPE_PRINTK, 0, 0,"INIT LED FAILED\n");
		return status;
	}


	if(register_control_device_interface(ps_adapter) < 0)
	{
		BCM_DEBUG_PRINT(ps_adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Register Control Device failed. Cleanup needs to be performed.");
		return -EIO;
	}

	ps_adapter->fw_download_done = TRUE;
#endif
	return status;
}


int bcm_parse_target_params(PMINI_ADAPTER Adapter)
{
#ifdef BCM_SHM_INTERFACE
	extern void read_cfg_file(PMINI_ADAPTER Adapter);
#endif
	struct file 		*flp=NULL;
	mm_segment_t 	oldfs={0};
	char *buff = NULL;
	int len = 0;
	loff_t	pos = 0;

	buff=(PCHAR)kmalloc(BUFFER_1K, GFP_KERNEL);
	if(!buff)
	{
		return -ENOMEM;
	}
	if((Adapter->pstargetparams =
		kmalloc(sizeof(STARGETPARAMS), GFP_KERNEL)) == NULL)
	{
		bcm_kfree(buff);
		return -ENOMEM;
	}
	flp=open_firmware_file(Adapter, CFG_FILE);
	if(!flp) {
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "NOT ABLE TO OPEN THE %s FILE \n", CFG_FILE);
		bcm_kfree(buff);
		bcm_kfree(Adapter->pstargetparams);
		Adapter->pstargetparams = NULL;
		return -ENOENT;
	}
	oldfs=get_fs();	set_fs(get_ds());
	len=vfs_read(flp, (void __user __force *)buff, BUFFER_1K, &pos);
	set_fs(oldfs);

	if(len != sizeof(STARGETPARAMS))
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL,"Mismatch in Target Param Structure!\n");
		bcm_kfree(buff);
		bcm_kfree(Adapter->pstargetparams);
		Adapter->pstargetparams = NULL;
		filp_close(flp, current->files);
		return -ENOENT;
	}
	filp_close(flp, current->files);

	/* Check for autolink in config params */
	/*
	 * Values in Adapter->pstargetparams are in network byte order
	 */
	memcpy(Adapter->pstargetparams, buff, sizeof(STARGETPARAMS));
	bcm_kfree (buff);
	beceem_parse_target_struct(Adapter);
#ifdef BCM_SHM_INTERFACE
	read_cfg_file(Adapter);

#endif
	return STATUS_SUCCESS;
}

void beceem_parse_target_struct(PMINI_ADAPTER Adapter)
{
	UINT uiHostDrvrCfg6 =0, uiEEPROMFlag = 0;;

	if(ntohl(Adapter->pstargetparams->m_u32PhyParameter2) & AUTO_SYNC_DISABLE)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "AutoSyncup is Disabled\n");
		Adapter->AutoSyncup = FALSE;
	}
	else
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "AutoSyncup is Enabled\n");
		Adapter->AutoSyncup	= TRUE;
	}
	if(ntohl(Adapter->pstargetparams->HostDrvrConfig6) & AUTO_LINKUP_ENABLE)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Enabling autolink up");
		Adapter->AutoLinkUp = TRUE;
	}
	else
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Disabling autolink up");
		Adapter->AutoLinkUp = FALSE;
	}
	// Setting the DDR Setting..
	Adapter->DDRSetting =
			(ntohl(Adapter->pstargetparams->HostDrvrConfig6) >>8)&0x0F;
	Adapter->ulPowerSaveMode =
			(ntohl(Adapter->pstargetparams->HostDrvrConfig6)>>12)&0x0F;

	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "DDR Setting: %x\n", Adapter->DDRSetting);
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT,DBG_LVL_ALL, "Power Save Mode: %lx\n",
							Adapter->ulPowerSaveMode);
	if(ntohl(Adapter->pstargetparams->HostDrvrConfig6) & AUTO_FIRM_DOWNLOAD)
    {
        BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Enabling Auto Firmware Download\n");
        Adapter->AutoFirmDld = TRUE;
    }
    else
    {
        BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Disabling Auto Firmware Download\n");
        Adapter->AutoFirmDld = FALSE;
    }
	uiHostDrvrCfg6 = ntohl(Adapter->pstargetparams->HostDrvrConfig6);
	Adapter->bMipsConfig = (uiHostDrvrCfg6>>20)&0x01;
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL,"MIPSConfig   : 0x%X\n",Adapter->bMipsConfig);
	//used for backward compatibility.
	Adapter->bDPLLConfig = (uiHostDrvrCfg6>>19)&0x01;

	Adapter->PmuMode= (uiHostDrvrCfg6 >> 24 ) & 0x03;
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "PMU MODE: %x", Adapter->PmuMode);

    if((uiHostDrvrCfg6 >> HOST_BUS_SUSPEND_BIT ) & (0x01))
    {
        Adapter->bDoSuspend = TRUE;
        BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "Making DoSuspend TRUE as per configFile");
    }

	uiEEPROMFlag = ntohl(Adapter->pstargetparams->m_u32EEPROMFlag);
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL, "uiEEPROMFlag  : 0x%X\n",uiEEPROMFlag);
	Adapter->eNVMType = (NVM_TYPE)((uiEEPROMFlag>>4)&0x3);


	Adapter->bStatusWrite = (uiEEPROMFlag>>6)&0x1;
	//printk(("bStatusWrite   : 0x%X\n", Adapter->bStatusWrite));

	Adapter->uiSectorSizeInCFG = 1024*(0xFFFF & ntohl(Adapter->pstargetparams->HostDrvrConfig4));
	//printk(("uiSectorSize   : 0x%X\n", Adapter->uiSectorSizeInCFG));

	Adapter->bSectorSizeOverride =(bool) ((ntohl(Adapter->pstargetparams->HostDrvrConfig4))>>16)&0x1;
	//printk(MP_INIT,("bSectorSizeOverride   : 0x%X\n",Adapter->bSectorSizeOverride));

	if(ntohl(Adapter->pstargetparams->m_u32PowerSavingModeOptions) &0x01)
		Adapter->ulPowerSaveMode = DEVICE_POWERSAVE_MODE_AS_PROTOCOL_IDLE_MODE;
	//autocorrection part
	if(Adapter->ulPowerSaveMode != DEVICE_POWERSAVE_MODE_AS_PROTOCOL_IDLE_MODE)
		doPowerAutoCorrection(Adapter);

}

VOID doPowerAutoCorrection(PMINI_ADAPTER psAdapter)
{
	UINT reporting_mode = 0;

	reporting_mode = ntohl(psAdapter->pstargetparams->m_u32PowerSavingModeOptions) &0x02 ;
	psAdapter->bIsAutoCorrectEnabled = !((char)(psAdapter->ulPowerSaveMode >> 3) & 0x1);

	if(reporting_mode == TRUE)
	{
		BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL,"can't do suspen/resume as reporting mode is enable");
		psAdapter->bDoSuspend = FALSE;
	}

	if (psAdapter->bIsAutoCorrectEnabled && (psAdapter->chip_id >= T3LPB))
	{
		//If reporting mode is enable, switch PMU to PMC
		#if 0
		if(reporting_mode == FALSE)
		{
			psAdapter->ulPowerSaveMode = DEVICE_POWERSAVE_MODE_AS_PMU_SHUTDOWN;
			psAdapter->bDoSuspend = TRUE;
			BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL,"PMU selected ....");

		}
		else
		#endif
		{
			psAdapter->ulPowerSaveMode = DEVICE_POWERSAVE_MODE_AS_PMU_CLOCK_GATING;
			psAdapter->bDoSuspend =FALSE;
			BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL,"PMC selected..");

		}

		//clearing space bit[15..12]
		psAdapter->pstargetparams->HostDrvrConfig6 &= ~(htonl((0xF << 12)));
		//placing the power save mode option
		psAdapter->pstargetparams->HostDrvrConfig6 |= htonl((psAdapter->ulPowerSaveMode << 12));

	}
	else if (psAdapter->bIsAutoCorrectEnabled == FALSE)
	{

		// remove the autocorrect disable bit set before dumping.
		psAdapter->ulPowerSaveMode &= ~(1 << 3);
		psAdapter->pstargetparams->HostDrvrConfig6 &= ~(htonl(1 << 15));
		BCM_DEBUG_PRINT(psAdapter,DBG_TYPE_INITEXIT, MP_INIT, DBG_LVL_ALL,"Using Forced User Choice: %lx\n", psAdapter->ulPowerSaveMode);
	}
}

#if 0
static unsigned char *ReadMacAddrEEPROM(PMINI_ADAPTER Adapter, ulong dwAddress)
{
	unsigned char *pucmacaddr = NULL;
	int status = 0, i=0;
	unsigned int temp =0;


	pucmacaddr = (unsigned char *)kmalloc(MAC_ADDRESS_SIZE, GFP_KERNEL);
	if(!pucmacaddr)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "No Buffers to Read the EEPROM Address\n");
		return NULL;
	}

	dwAddress |= 0x5b000000;
	status = wrmalt(Adapter, EEPROM_COMMAND_Q_REG,
						(PUINT)&dwAddress, sizeof(UINT));
	if(status != STATUS_SUCCESS)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "wrm Failed..\n");
		bcm_kfree(pucmacaddr);
		pucmacaddr = NULL;
		goto OUT;
	}
	for(i=0;i<MAC_ADDRESS_SIZE;i++)
	{
		status = rdmalt(Adapter, EEPROM_READ_DATA_Q_REG, &temp,sizeof(temp));
		if(status != STATUS_SUCCESS)
		{
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "rdm Failed..\n");
			bcm_kfree(pucmacaddr);
			pucmacaddr = NULL;
			goto OUT;
		}
		pucmacaddr[i] = temp & 0xff;
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL,"%x \n", pucmacaddr[i]);
	}
OUT:
	return pucmacaddr;
}
#endif

#if 0
INT ReadMacAddressFromEEPROM(PMINI_ADAPTER Adapter)
{
	unsigned char *puMacAddr = NULL;
	int i =0;

	puMacAddr = ReadMacAddrEEPROM(Adapter,0x200);
	if(!puMacAddr)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL, "Couldn't retrieve the Mac Address\n");
		return STATUS_FAILURE;
	}
	else
	{
		if((puMacAddr[0] == 0x0  && puMacAddr[1] == 0x0  &&
			puMacAddr[2] == 0x0  && puMacAddr[3] == 0x0  &&
			puMacAddr[4] == 0x0  && puMacAddr[5] == 0x0) ||
		   (puMacAddr[0] == 0xFF && puMacAddr[1] == 0xFF &&
			puMacAddr[2] == 0xFF && puMacAddr[3] == 0xFF &&
			puMacAddr[4] == 0xFF && puMacAddr[5] == 0xFF))
		{
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL, "Invalid Mac Address\n");
			bcm_kfree(puMacAddr);
			return STATUS_FAILURE;
		}
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL, "The Mac Address received is: \n");
		memcpy(Adapter->dev->dev_addr, puMacAddr, MAC_ADDRESS_SIZE);
        for(i=0;i<MAC_ADDRESS_SIZE;i++)
        {
            BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"%02x ", Adapter->dev->dev_addr[i]);
        }
        BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"\n");
		bcm_kfree(puMacAddr);
	}
	return STATUS_SUCCESS;
}
#endif

static void convertEndian(B_UINT8 rwFlag, PUINT puiBuffer, UINT uiByteCount)
{
	UINT uiIndex = 0;

	if(RWM_WRITE == rwFlag) {
		for(uiIndex =0; uiIndex < (uiByteCount/sizeof(UINT)); uiIndex++) {
			puiBuffer[uiIndex] = htonl(puiBuffer[uiIndex]);
		}
	} else {
		for(uiIndex =0; uiIndex < (uiByteCount/sizeof(UINT)); uiIndex++) {
			puiBuffer[uiIndex] = ntohl(puiBuffer[uiIndex]);
		}
	}
}

#define CACHE_ADDRESS_MASK	0x80000000
#define UNCACHE_ADDRESS_MASK	0xa0000000

int rdm(PMINI_ADAPTER Adapter, UINT uiAddress, PCHAR pucBuff, size_t sSize)
{
	INT uiRetVal =0;

#ifndef BCM_SHM_INTERFACE
	uiRetVal = Adapter->interface_rdm(Adapter->pvInterfaceAdapter,
			uiAddress, pucBuff, sSize);

	if(uiRetVal < 0)
		return uiRetVal;

#else
	int indx;
	uiRetVal = STATUS_SUCCESS;
	if(uiAddress & 0x10000000) {
			// DDR Memory Access
		uiAddress |= CACHE_ADDRESS_MASK;
		memcpy(pucBuff,(unsigned char *)uiAddress ,sSize);
	}
	else {
		// Register, SPRAM, Flash
		uiAddress |= UNCACHE_ADDRESS_MASK;
    if ((uiAddress & FLASH_ADDR_MASK) == (FLASH_CONTIGIOUS_START_ADDR_BCS350 & FLASH_ADDR_MASK))
	{
		#if defined(FLASH_DIRECT_ACCESS)
        	memcpy(pucBuff,(unsigned char *)uiAddress ,sSize);
		#else
			printk("\nInvalid GSPI ACCESS :Addr :%#X", uiAddress);
			uiRetVal = STATUS_FAILURE;
		#endif
	}
    else if(((unsigned int )uiAddress & 0x3) ||
			((unsigned int )pucBuff & 0x3) ||
			((unsigned int )sSize & 0x3)) {
		  	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"rdmalt :unalligned register access uiAddress =  %x,pucBuff = %x  size = %x\n",(unsigned int )uiAddress,(unsigned int )pucBuff,(unsigned int )sSize);
			 uiRetVal = STATUS_FAILURE;
		}
		else {
		 	for (indx=0;indx<sSize;indx+=4){
		   		*(PUINT)(pucBuff + indx) = *(PUINT)(uiAddress + indx);
		  	}
		}
	}
#endif
	return uiRetVal;
}
int wrm(PMINI_ADAPTER Adapter, UINT uiAddress, PCHAR pucBuff, size_t sSize)
{
	int iRetVal;

#ifndef BCM_SHM_INTERFACE
	iRetVal = Adapter->interface_wrm(Adapter->pvInterfaceAdapter,
			uiAddress, pucBuff, sSize);

#else
	int indx;
	if(uiAddress & 0x10000000) {
		// DDR Memory Access
		uiAddress |= CACHE_ADDRESS_MASK;
		memcpy((unsigned char *)(uiAddress),pucBuff,sSize);
	}
	else {
		// Register, SPRAM, Flash
		uiAddress |= UNCACHE_ADDRESS_MASK;

		if(((unsigned int )uiAddress & 0x3) ||
			((unsigned int )pucBuff & 0x3) ||
			((unsigned int )sSize & 0x3)) {
		  		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"wrmalt: unalligned register access uiAddress =  %x,pucBuff = %x  size = %x\n",(unsigned int )uiAddress,(unsigned int )pucBuff,(unsigned int )sSize);
			 iRetVal = STATUS_FAILURE;
		}
		else {
		 	for (indx=0;indx<sSize;indx+=4) {
		  		*(PUINT)(uiAddress + indx) = *(PUINT)(pucBuff + indx);
			}
		}
	}
	iRetVal = STATUS_SUCCESS;
#endif

	return iRetVal;
}

int wrmalt (PMINI_ADAPTER Adapter, UINT uiAddress, PUINT pucBuff, size_t size)
{
	convertEndian(RWM_WRITE, pucBuff, size);
	return wrm(Adapter, uiAddress, (PUCHAR)pucBuff, size);
}

int rdmalt (PMINI_ADAPTER Adapter, UINT uiAddress, PUINT pucBuff, size_t size)
{
	INT uiRetVal =0;

	uiRetVal = rdm(Adapter,uiAddress,(PUCHAR)pucBuff,size);
	convertEndian(RWM_READ, (PUINT)pucBuff, size);

	return uiRetVal;
}

int rdmWithLock(PMINI_ADAPTER Adapter, UINT uiAddress, PCHAR pucBuff, size_t sSize)
{

	INT status = STATUS_SUCCESS ;
	down(&Adapter->rdmwrmsync);

	if((Adapter->IdleMode == TRUE) ||
		(Adapter->bShutStatus ==TRUE) ||
		(Adapter->bPreparingForLowPowerMode ==TRUE))
	{
		status = -EACCES;
		goto exit;
	}

	status = rdm(Adapter, uiAddress, pucBuff, sSize);

exit:
	up(&Adapter->rdmwrmsync);
	return status ;
}
int wrmWithLock(PMINI_ADAPTER Adapter, UINT uiAddress, PCHAR pucBuff, size_t sSize)
{
	INT status = STATUS_SUCCESS ;
	down(&Adapter->rdmwrmsync);

	if((Adapter->IdleMode == TRUE) ||
		(Adapter->bShutStatus ==TRUE) ||
		(Adapter->bPreparingForLowPowerMode ==TRUE))
	{
		status = -EACCES;
		goto exit;
	}

	status =wrm(Adapter, uiAddress, pucBuff, sSize);

exit:
	up(&Adapter->rdmwrmsync);
	return status ;
}

int wrmaltWithLock (PMINI_ADAPTER Adapter, UINT uiAddress, PUINT pucBuff, size_t size)
{
	int iRetVal = STATUS_SUCCESS;

	down(&Adapter->rdmwrmsync);

	if((Adapter->IdleMode == TRUE) ||
		(Adapter->bShutStatus ==TRUE) ||
		(Adapter->bPreparingForLowPowerMode ==TRUE))
	{
		iRetVal = -EACCES;
		goto exit;
	}

	iRetVal = wrmalt(Adapter,uiAddress,pucBuff,size);

exit:
	up(&Adapter->rdmwrmsync);
	return iRetVal;
}

int rdmaltWithLock (PMINI_ADAPTER Adapter, UINT uiAddress, PUINT pucBuff, size_t size)
{
	INT uiRetVal =STATUS_SUCCESS;

	down(&Adapter->rdmwrmsync);

	if((Adapter->IdleMode == TRUE) ||
		(Adapter->bShutStatus ==TRUE) ||
		(Adapter->bPreparingForLowPowerMode ==TRUE))
	{
		uiRetVal = -EACCES;
		goto exit;
	}

	uiRetVal = rdmalt(Adapter,uiAddress, pucBuff, size);

exit:
	up(&Adapter->rdmwrmsync);
	return uiRetVal;
}


static VOID HandleShutDownModeWakeup(PMINI_ADAPTER Adapter)
{
	int clear_abort_pattern = 0,Status = 0;
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, MP_SHUTDOWN, DBG_LVL_ALL, "====>\n");
	//target has woken up From Shut Down
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, MP_SHUTDOWN, DBG_LVL_ALL, "Clearing Shut Down Software abort pattern\n");
	Status = wrmalt(Adapter,SW_ABORT_IDLEMODE_LOC, (PUINT)&clear_abort_pattern, sizeof(clear_abort_pattern));
	if(Status)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, MP_SHUTDOWN, DBG_LVL_ALL,"WRM to SW_ABORT_IDLEMODE_LOC failed with err:%d", Status);
		return;
	}
	if(Adapter->ulPowerSaveMode != DEVICE_POWERSAVE_MODE_AS_PROTOCOL_IDLE_MODE)
	{
		msleep(100);
		InterfaceHandleShutdownModeWakeup(Adapter);
		msleep(100);
	}
	if(Adapter->LEDInfo.led_thread_running & BCM_LED_THREAD_RUNNING_ACTIVELY)
	{
		Adapter->DriverState = NO_NETWORK_ENTRY;
		wake_up(&Adapter->LEDInfo.notify_led_event);
	}

	Adapter->bTriedToWakeUpFromlowPowerMode = FALSE;
	Adapter->bShutStatus = FALSE;
	wake_up(&Adapter->lowpower_mode_wait_queue);
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, MP_SHUTDOWN, DBG_LVL_ALL, "<====\n");
}

static VOID SendShutModeResponse(PMINI_ADAPTER Adapter)
{
	CONTROL_MESSAGE		stShutdownResponse;
	UINT NVMAccess = 0,lowPwrAbortMsg = 0;
	UINT Status = 0;

	memset (&stShutdownResponse, 0, sizeof(CONTROL_MESSAGE));
	stShutdownResponse.Leader.Status  = LINK_UP_CONTROL_REQ;
	stShutdownResponse.Leader.PLength = 8;//8 bytes;
	stShutdownResponse.szData[0] = LINK_UP_ACK;
	stShutdownResponse.szData[1] = LINK_SHUTDOWN_REQ_FROM_FIRMWARE;

	/*********************************
	**down_trylock -
	** if [ semaphore is available ]
	**		 acquire semaphone and return value 0 ;
	**   else
	**		 return non-zero value ;
	**
	***********************************/

	NVMAccess = down_trylock(&Adapter->NVMRdmWrmLock);

	lowPwrAbortMsg= down_trylock(&Adapter->LowPowerModeSync);


	if(NVMAccess || lowPwrAbortMsg|| atomic_read(&Adapter->TotalPacketCount))
	{
		if(!NVMAccess)
			up(&Adapter->NVMRdmWrmLock);

		if(!lowPwrAbortMsg)
			up(&Adapter->LowPowerModeSync);

		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, MP_SHUTDOWN, DBG_LVL_ALL, "Device Access is going on NACK the Shut Down MODE\n");
		stShutdownResponse.szData[2] = SHUTDOWN_NACK_FROM_DRIVER;//NACK- device access is going on.
		Adapter->bPreparingForLowPowerMode = FALSE;
	}
	else
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, MP_SHUTDOWN, DBG_LVL_ALL, "Sending SHUTDOWN MODE ACK\n");
		stShutdownResponse.szData[2] = SHUTDOWN_ACK_FROM_DRIVER;//ShutDown ACK

		/* Wait for the LED to TURN OFF before sending ACK response */
		if(Adapter->LEDInfo.led_thread_running & BCM_LED_THREAD_RUNNING_ACTIVELY)
		{
			INT iRetVal = 0;

			/* Wake the LED Thread with LOWPOWER_MODE_ENTER State */
			Adapter->DriverState = LOWPOWER_MODE_ENTER;
			wake_up(&Adapter->LEDInfo.notify_led_event);

			/* Wait for 1 SEC for LED to OFF */
			iRetVal = wait_event_timeout(Adapter->LEDInfo.idleModeSyncEvent,\
				Adapter->LEDInfo.bIdle_led_off, msecs_to_jiffies(1000));

			/* If Timed Out to Sync IDLE MODE Enter, do IDLE mode Exit and Send NACK to device */
			if(iRetVal <= 0)
			{
				stShutdownResponse.szData[1] = SHUTDOWN_NACK_FROM_DRIVER;//NACK- device access is going on.

				Adapter->DriverState = NO_NETWORK_ENTRY;
				wake_up(&Adapter->LEDInfo.notify_led_event);
			}
		}

		if(stShutdownResponse.szData[2] == SHUTDOWN_ACK_FROM_DRIVER)
		{
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, MP_SHUTDOWN, DBG_LVL_ALL,"ACKING SHUTDOWN MODE !!!!!!!!!");
			down(&Adapter->rdmwrmsync);
			Adapter->bPreparingForLowPowerMode = TRUE;
			up(&Adapter->rdmwrmsync);
			//Killing all URBS.
#ifndef BCM_SHM_INTERFACE
			if(Adapter->bDoSuspend == TRUE)
				Bcm_kill_all_URBs((PS_INTERFACE_ADAPTER)(Adapter->pvInterfaceAdapter));
#endif
		}
		else
		{
			Adapter->bPreparingForLowPowerMode = FALSE;
		}

		if(!NVMAccess)
			up(&Adapter->NVMRdmWrmLock);

		if(!lowPwrAbortMsg)
			up(&Adapter->LowPowerModeSync);
	}
	Status = CopyBufferToControlPacket(Adapter,&stShutdownResponse);
	if((Status != STATUS_SUCCESS))
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, MP_SHUTDOWN, DBG_LVL_ALL,"fail to send the Idle mode Request \n");
		Adapter->bPreparingForLowPowerMode = FALSE;

#ifndef BCM_SHM_INTERFACE
		StartInterruptUrb((PS_INTERFACE_ADAPTER)(Adapter->pvInterfaceAdapter));
#endif
	}
}


void HandleShutDownModeRequest(PMINI_ADAPTER Adapter,PUCHAR pucBuffer)
{
	B_UINT32 uiResetValue = 0;

	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, MP_SHUTDOWN, DBG_LVL_ALL, "====>\n");

	if(*(pucBuffer+1) ==  COMPLETE_WAKE_UP_NOTIFICATION_FRM_FW)
	{
		HandleShutDownModeWakeup(Adapter);
	}
	else if(*(pucBuffer+1) ==  LINK_SHUTDOWN_REQ_FROM_FIRMWARE)
	{
		//Target wants to go to Shut Down Mode
		//InterfacePrepareForShutdown(Adapter);
		if(Adapter->chip_id == BCS220_2 ||
		   Adapter->chip_id == BCS220_2BC ||
		   Adapter->chip_id == BCS250_BC ||
		   Adapter->chip_id == BCS220_3)
		{
			rdmalt(Adapter,HPM_CONFIG_MSW, &uiResetValue, 4);
			uiResetValue |= (1<<17);
			wrmalt(Adapter, HPM_CONFIG_MSW, &uiResetValue, 4);
		}

		SendShutModeResponse(Adapter);
		BCM_DEBUG_PRINT (Adapter,DBG_TYPE_OTHERS, MP_SHUTDOWN, DBG_LVL_ALL,"ShutDownModeResponse:Notification received: Sending the response(Ack/Nack)\n");
	}

	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, MP_SHUTDOWN, DBG_LVL_ALL, "<====\n");
	return;

}

VOID ResetCounters(PMINI_ADAPTER Adapter)
{

   	beceem_protocol_reset(Adapter);

	Adapter->CurrNumRecvDescs = 0;
    Adapter->PrevNumRecvDescs = 0;
    Adapter->LinkUpStatus = 0;
	Adapter->LinkStatus = 0;
    atomic_set(&Adapter->cntrlpktCnt,0);
    atomic_set (&Adapter->TotalPacketCount,0);
    Adapter->fw_download_done=FALSE;
	Adapter->LinkStatus = 0;
    Adapter->AutoLinkUp = FALSE;
	Adapter->IdleMode = FALSE;
	Adapter->bShutStatus = FALSE;

}
S_CLASSIFIER_RULE *GetFragIPClsEntry(PMINI_ADAPTER Adapter,USHORT usIpIdentification,ULONG SrcIP)
{
	UINT uiIndex=0;
	for(uiIndex=0;uiIndex<MAX_FRAGMENTEDIP_CLASSIFICATION_ENTRIES;uiIndex++)
	{
		if((Adapter->astFragmentedPktClassifierTable[uiIndex].bUsed)&&
			(Adapter->astFragmentedPktClassifierTable[uiIndex].usIpIdentification == usIpIdentification)&&
			(Adapter->astFragmentedPktClassifierTable[uiIndex].ulSrcIpAddress== SrcIP)&&
			!Adapter->astFragmentedPktClassifierTable[uiIndex].bOutOfOrderFragment)
			return Adapter->astFragmentedPktClassifierTable[uiIndex].pstMatchedClassifierEntry;
	}
	return NULL;
}

void AddFragIPClsEntry(PMINI_ADAPTER Adapter,PS_FRAGMENTED_PACKET_INFO psFragPktInfo)
{
	UINT uiIndex=0;
	for(uiIndex=0;uiIndex<MAX_FRAGMENTEDIP_CLASSIFICATION_ENTRIES;uiIndex++)
	{
		if(!Adapter->astFragmentedPktClassifierTable[uiIndex].bUsed)
		{
			memcpy(&Adapter->astFragmentedPktClassifierTable[uiIndex],psFragPktInfo,sizeof(S_FRAGMENTED_PACKET_INFO));
			break;
		}
	}

}

void DelFragIPClsEntry(PMINI_ADAPTER Adapter,USHORT usIpIdentification,ULONG SrcIp)
{
	UINT uiIndex=0;
	for(uiIndex=0;uiIndex<MAX_FRAGMENTEDIP_CLASSIFICATION_ENTRIES;uiIndex++)
	{
		if((Adapter->astFragmentedPktClassifierTable[uiIndex].bUsed)&&
			(Adapter->astFragmentedPktClassifierTable[uiIndex].usIpIdentification == usIpIdentification)&&
			(Adapter->astFragmentedPktClassifierTable[uiIndex].ulSrcIpAddress== SrcIp))
		memset(&Adapter->astFragmentedPktClassifierTable[uiIndex],0,sizeof(S_FRAGMENTED_PACKET_INFO));
	}
}

void update_per_cid_rx (PMINI_ADAPTER Adapter)
{
	UINT  qindex = 0;

	if((jiffies - Adapter->liDrainCalculated) < XSECONDS)
		return;

	for(qindex = 0; qindex < HiPriority; qindex++)
	{
		if(Adapter->PackInfo[qindex].ucDirection == 0)
		{
			Adapter->PackInfo[qindex].uiCurrentRxRate =
				(Adapter->PackInfo[qindex].uiCurrentRxRate +
				Adapter->PackInfo[qindex].uiThisPeriodRxBytes)/2;

			Adapter->PackInfo[qindex].uiThisPeriodRxBytes = 0;
		}
		else
		{
			Adapter->PackInfo[qindex].uiCurrentDrainRate =
				(Adapter->PackInfo[qindex].uiCurrentDrainRate +
				Adapter->PackInfo[qindex].uiThisPeriodSentBytes)/2;

			Adapter->PackInfo[qindex].uiThisPeriodSentBytes=0;
		}
	}
	Adapter->liDrainCalculated=jiffies;
}
void update_per_sf_desc_cnts( PMINI_ADAPTER Adapter)
{
	INT iIndex = 0;
	u32 uibuff[MAX_TARGET_DSX_BUFFERS];

	if(!atomic_read (&Adapter->uiMBupdate))
		return;

#ifdef BCM_SHM_INTERFACE
	if(rdmalt(Adapter, TARGET_SFID_TXDESC_MAP_LOC, (PUINT)uibuff, sizeof(UINT) * MAX_TARGET_DSX_BUFFERS)<0)
#else
	if(rdmaltWithLock(Adapter, TARGET_SFID_TXDESC_MAP_LOC, (PUINT)uibuff, sizeof(UINT) * MAX_TARGET_DSX_BUFFERS)<0)
#endif
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "rdm failed\n");
		return;
	}
	for(iIndex = 0;iIndex < HiPriority; iIndex++)
	{
		if(Adapter->PackInfo[iIndex].bValid && Adapter->PackInfo[iIndex].ucDirection)
		{
			if(Adapter->PackInfo[iIndex].usVCID_Value < MAX_TARGET_DSX_BUFFERS)
			{
				atomic_set(&Adapter->PackInfo[iIndex].uiPerSFTxResourceCount, uibuff[Adapter->PackInfo[iIndex].usVCID_Value]);
			}
			else
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Invalid VCID : %x \n",
					Adapter->PackInfo[iIndex].usVCID_Value);
			}
		}
	}
	atomic_set (&Adapter->uiMBupdate, FALSE);
}

void flush_queue(PMINI_ADAPTER Adapter, UINT iQIndex)
{
	struct sk_buff* 			PacketToDrop=NULL;
	struct net_device_stats*		netstats=NULL;

	netstats = &((PLINUX_DEP_DATA)Adapter->pvOsDepData)->netstats;

	spin_lock_bh(&Adapter->PackInfo[iQIndex].SFQueueLock);

	while(Adapter->PackInfo[iQIndex].FirstTxQueue &&
		atomic_read(&Adapter->TotalPacketCount))
	{
		PacketToDrop = Adapter->PackInfo[iQIndex].FirstTxQueue;
		if(PacketToDrop && PacketToDrop->len)
		{
			netstats->tx_dropped++;
			DEQUEUEPACKET(Adapter->PackInfo[iQIndex].FirstTxQueue, \
					Adapter->PackInfo[iQIndex].LastTxQueue);

			Adapter->PackInfo[iQIndex].uiCurrentPacketsOnHost--;
			Adapter->PackInfo[iQIndex].uiCurrentBytesOnHost -= PacketToDrop->len;

			//Adding dropped statistics
			Adapter->PackInfo[iQIndex].uiDroppedCountBytes += PacketToDrop->len;
			Adapter->PackInfo[iQIndex].uiDroppedCountPackets++;

			bcm_kfree_skb(PacketToDrop);
			atomic_dec(&Adapter->TotalPacketCount);
			atomic_inc(&Adapter->TxDroppedPacketCount);

		}
	}
	spin_unlock_bh(&Adapter->PackInfo[iQIndex].SFQueueLock);

}

void beceem_protocol_reset (PMINI_ADAPTER Adapter)
{
	int i =0;

	if(NULL != Adapter->dev)
	{
		netif_carrier_off(Adapter->dev);
		netif_stop_queue(Adapter->dev);
	}

	Adapter->IdleMode = FALSE;
	Adapter->LinkUpStatus = FALSE;
	ClearTargetDSXBuffer(Adapter,0, TRUE);
	//Delete All Classifier Rules

	for(i = 0;i<HiPriority;i++)
	{
		DeleteAllClassifiersForSF(Adapter,i);
	}

	flush_all_queues(Adapter);

	if(Adapter->TimerActive == TRUE)
		Adapter->TimerActive = FALSE;

	memset(Adapter->astFragmentedPktClassifierTable, 0,
			sizeof(S_FRAGMENTED_PACKET_INFO) *
			MAX_FRAGMENTEDIP_CLASSIFICATION_ENTRIES);

	for(i = 0;i<HiPriority;i++)
	{
		//resetting only the first size (S_MIBS_SERVICEFLOW_TABLE) for the SF.
		// It is same between MIBs and SF.
		memset((PVOID)&Adapter->PackInfo[i],0,sizeof(S_MIBS_SERVICEFLOW_TABLE));
	}
}



#ifdef BCM_SHM_INTERFACE


#define GET_GTB_DIFF(start, end)  \
( (start) < (end) )? ( (end) - (start) ) : ( ~0x0 - ( (start) - (end)) +1 )

void usdelay ( unsigned int a) {
	unsigned int start= *(unsigned int *)0xaf8051b4;
	unsigned int end  = start+1;
	unsigned int diff = 0;

	while(1) {
		end = *(unsigned int *)0xaf8051b4;
		diff = (GET_GTB_DIFF(start,end))/80;
		if (diff >= a)
			break;
	}
}
void read_cfg_file(PMINI_ADAPTER Adapter) {


	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Config File Version = 0x%x \n",Adapter->pstargetparams->m_u32CfgVersion );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Center Frequency =  0x%x \n",Adapter->pstargetparams->m_u32CenterFrequency );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Band A Scan = 0x%x \n",Adapter->pstargetparams->m_u32BandAScan );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Band B Scan = 0x%x \n",Adapter->pstargetparams->m_u32BandBScan );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Band C Scan = 0x%x \n",Adapter->pstargetparams->m_u32BandCScan );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"ERTPS Options = 0x%x \n",Adapter->pstargetparams->m_u32ErtpsOptions );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"PHS Enable = 0x%x \n",Adapter->pstargetparams->m_u32PHSEnable );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Handoff Enable = 0x%x \n",Adapter->pstargetparams->m_u32HoEnable );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"HO Reserved1 = 0x%x \n",Adapter->pstargetparams->m_u32HoReserved1 );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"HO Reserved2 = 0x%x \n",Adapter->pstargetparams->m_u32HoReserved2 );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"MIMO Enable = 0x%x \n",Adapter->pstargetparams->m_u32MimoEnable );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"PKMv2 Enable = 0x%x \n",Adapter->pstargetparams->m_u32SecurityEnable );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Powersaving Modes Enable = 0x%x \n",Adapter->pstargetparams->m_u32PowerSavingModesEnable );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Power Saving Mode Options = 0x%x \n",Adapter->pstargetparams->m_u32PowerSavingModeOptions );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"ARQ Enable = 0x%x \n",Adapter->pstargetparams->m_u32ArqEnable );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Harq Enable = 0x%x \n",Adapter->pstargetparams->m_u32HarqEnable );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"EEPROM Flag = 0x%x \n",Adapter->pstargetparams->m_u32EEPROMFlag );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Customize = 0x%x \n",Adapter->pstargetparams->m_u32Customize );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Bandwidth = 0x%x \n",Adapter->pstargetparams->m_u32ConfigBW );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"ShutDown Timer Value = 0x%x \n",Adapter->pstargetparams->m_u32ShutDownInitThresholdTimer );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"RadioParameter = 0x%x \n",Adapter->pstargetparams->m_u32RadioParameter );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"PhyParameter1 = 0x%x \n",Adapter->pstargetparams->m_u32PhyParameter1 );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"PhyParameter2 = 0x%x \n",Adapter->pstargetparams->m_u32PhyParameter2 );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"PhyParameter3 = 0x%x \n",Adapter->pstargetparams->m_u32PhyParameter3 );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"m_u32TestOptions = 0x%x \n",Adapter->pstargetparams->m_u32TestOptions );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"MaxMACDataperDLFrame = 0x%x \n",Adapter->pstargetparams->m_u32MaxMACDataperDLFrame );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"MaxMACDataperULFrame = 0x%x \n",Adapter->pstargetparams->m_u32MaxMACDataperULFrame );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Corr2MacFlags = 0x%x \n",Adapter->pstargetparams->m_u32Corr2MacFlags );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"HostDrvrConfig1 = 0x%x \n",Adapter->pstargetparams->HostDrvrConfig1 );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"HostDrvrConfig2 = 0x%x \n",Adapter->pstargetparams->HostDrvrConfig2 );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"HostDrvrConfig3 = 0x%x \n",Adapter->pstargetparams->HostDrvrConfig3 );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"HostDrvrConfig4 = 0x%x \n",Adapter->pstargetparams->HostDrvrConfig4 );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"HostDrvrConfig5 = 0x%x \n",Adapter->pstargetparams->HostDrvrConfig5 );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"HostDrvrConfig6 = 0x%x \n",Adapter->pstargetparams->HostDrvrConfig6 );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"Segmented PUSC Enable = 0x%x \n",Adapter->pstargetparams->m_u32SegmentedPUSCenable );
	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0,"BamcEnable = 0x%x \n",Adapter->pstargetparams->m_u32BandAMCEnable );
}

#endif


