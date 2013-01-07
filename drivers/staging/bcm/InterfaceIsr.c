#include "headers.h"


static void read_int_callback(struct urb *urb/*, struct pt_regs *regs*/)
{
	int		status = urb->status;
	struct bcm_interface_adapter *psIntfAdapter = (struct bcm_interface_adapter *)urb->context;
	struct bcm_mini_adapter *Adapter = psIntfAdapter->psAdapter ;

	if (netif_msg_intr(Adapter))
		pr_info(PFX "%s: interrupt status %d\n",
			Adapter->dev->name, status);

	if(Adapter->device_removed == TRUE)
	{
		BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, INTF_INIT, DBG_LVL_ALL,"Device has Got Removed.");
		return ;
	}

	if(((Adapter->bPreparingForLowPowerMode == TRUE) && (Adapter->bDoSuspend == TRUE)) ||
		psIntfAdapter->bSuspended ||
		psIntfAdapter->bPreparingForBusSuspend)
	{
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, INTF_INIT, DBG_LVL_ALL,"Interrupt call back is called while suspending the device");
			return ;
	}

	//BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL, "interrupt urb status %d", status);
	switch (status) {
	    /* success */
	    case STATUS_SUCCESS:
		if ( urb->actual_length )
		{

			if(psIntfAdapter->ulInterruptData[1] & 0xFF)
			{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, INTF_INIT, DBG_LVL_ALL, "Got USIM interrupt");
			}

			if(psIntfAdapter->ulInterruptData[1] & 0xFF00)
			{
				atomic_set(&Adapter->CurrNumFreeTxDesc,
					(psIntfAdapter->ulInterruptData[1] & 0xFF00) >> 8);
				atomic_set (&Adapter->uiMBupdate, TRUE);
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, INTF_INIT, DBG_LVL_ALL, "TX mailbox contains %d",
					atomic_read(&Adapter->CurrNumFreeTxDesc));
			}
			if(psIntfAdapter->ulInterruptData[1] >> 16)
			{
				Adapter->CurrNumRecvDescs=
					(psIntfAdapter->ulInterruptData[1]  >> 16);
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, INTF_INIT, DBG_LVL_ALL,"RX mailbox contains %d",
					Adapter->CurrNumRecvDescs);
				InterfaceRx(psIntfAdapter);
			}
			if(Adapter->fw_download_done &&
				!Adapter->downloadDDR &&
				atomic_read(&Adapter->CurrNumFreeTxDesc))
			{
				psIntfAdapter->psAdapter->downloadDDR +=1;
				wake_up(&Adapter->tx_packet_wait_queue);
			}
			if(FALSE == Adapter->waiting_to_fw_download_done)
			{
				Adapter->waiting_to_fw_download_done = TRUE;
				wake_up(&Adapter->ioctl_fw_dnld_wait_queue);
			}
			if(!atomic_read(&Adapter->TxPktAvail))
			{
				atomic_set(&Adapter->TxPktAvail, 1);
				wake_up(&Adapter->tx_packet_wait_queue);
			}
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, INTF_INIT, DBG_LVL_ALL,"Firing interrupt in URB");
		}
		break;
		case -ENOENT :
		{
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, INTF_INIT, DBG_LVL_ALL,"URB has got disconnected ....");
			return ;
		}
		case -EINPROGRESS:
		{
			//This situation may happened when URBunlink is used. for detail check usb_unlink_urb documentation.
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, INTF_INIT, DBG_LVL_ALL,"Impossibe condition has occurred... something very bad is going on");
			break ;
			//return;
		}
		case -EPIPE:
		{
				BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, INTF_INIT, DBG_LVL_ALL,"Interrupt IN endPoint  has got halted/stalled...need to clear this");
				Adapter->bEndPointHalted = TRUE ;
				wake_up(&Adapter->tx_packet_wait_queue);
				urb->status = STATUS_SUCCESS ;
				return;
		}
	    /* software-driven interface shutdown */
	    case -ECONNRESET: //URB got unlinked.
	    case -ESHUTDOWN:		// hardware gone. this is the serious problem.
	    						//Occurs only when something happens with the host controller device
	    case -ENODEV : //Device got removed
		case -EINVAL : //Some thing very bad happened with the URB. No description is available.
	    	BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, INTF_INIT, DBG_LVL_ALL,"interrupt urb error %d", status);
			urb->status = STATUS_SUCCESS ;
			break ;
			//return;
	    default:
			//This is required to check what is the defaults conditions when it occurs..
			BCM_DEBUG_PRINT(Adapter,DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL,"GOT DEFAULT INTERRUPT URB STATUS :%d..Please Analyze it...", status);
		break;
	}

	StartInterruptUrb(psIntfAdapter);


}

int CreateInterruptUrb(struct bcm_interface_adapter *psIntfAdapter)
{
	psIntfAdapter->psInterruptUrb = usb_alloc_urb(0, GFP_KERNEL);
	if (!psIntfAdapter->psInterruptUrb)
	{
		BCM_DEBUG_PRINT(psIntfAdapter->psAdapter,DBG_TYPE_OTHERS, INTF_INIT, DBG_LVL_ALL,"Cannot allocate interrupt urb");
		return -ENOMEM;
	}
	psIntfAdapter->psInterruptUrb->transfer_buffer =
								psIntfAdapter->ulInterruptData;
	psIntfAdapter->psInterruptUrb->transfer_buffer_length =
							sizeof(psIntfAdapter->ulInterruptData);

	psIntfAdapter->sIntrIn.int_in_pipe = usb_rcvintpipe(psIntfAdapter->udev,
						psIntfAdapter->sIntrIn.int_in_endpointAddr);

	usb_fill_int_urb(psIntfAdapter->psInterruptUrb, psIntfAdapter->udev,
					psIntfAdapter->sIntrIn.int_in_pipe,
					psIntfAdapter->psInterruptUrb->transfer_buffer,
					psIntfAdapter->psInterruptUrb->transfer_buffer_length,
					read_int_callback, psIntfAdapter,
					psIntfAdapter->sIntrIn.int_in_interval);

	BCM_DEBUG_PRINT(psIntfAdapter->psAdapter,DBG_TYPE_OTHERS, INTF_INIT, DBG_LVL_ALL,"Interrupt Interval: %d\n",
				psIntfAdapter->sIntrIn.int_in_interval);
	return 0;
}


INT StartInterruptUrb(struct bcm_interface_adapter *psIntfAdapter)
{
	INT status = 0;

	if( FALSE == psIntfAdapter->psAdapter->device_removed &&
		FALSE == psIntfAdapter->psAdapter->bEndPointHalted &&
		FALSE == psIntfAdapter->bSuspended &&
		FALSE == psIntfAdapter->bPreparingForBusSuspend &&
		FALSE == psIntfAdapter->psAdapter->StopAllXaction)
	{
		status = usb_submit_urb(psIntfAdapter->psInterruptUrb, GFP_ATOMIC);
		if (status)
		{
			BCM_DEBUG_PRINT(psIntfAdapter->psAdapter,DBG_TYPE_OTHERS, INTF_INIT, DBG_LVL_ALL,"Cannot send int urb %d\n", status);
			if(status == -EPIPE)
			{
				psIntfAdapter->psAdapter->bEndPointHalted = TRUE ;
				wake_up(&psIntfAdapter->psAdapter->tx_packet_wait_queue);
			}
		}
	}
	return status;
}

