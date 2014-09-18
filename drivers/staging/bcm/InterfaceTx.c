#include "headers.h"

static void prepare_low_power_mode(struct urb *urb,
				   struct bcm_interface_adapter *interface,
				   struct bcm_mini_adapter *ps_adapter,
				   struct bcm_mini_adapter *ad,
				   struct bcm_link_request *p_control_msg,
				   bool *b_power_down_msg)
{
	if (((p_control_msg->szData[0] == GO_TO_IDLE_MODE_PAYLOAD) &&
		(p_control_msg->szData[1] == TARGET_CAN_GO_TO_IDLE_MODE))) {

		*b_power_down_msg = TRUE;
		/*
		 * This covers the bus err while Idle Request msg
		 * sent down.
		 */
		if (urb->status != STATUS_SUCCESS) {
			ps_adapter->bPreparingForLowPowerMode = false;
			BCM_DEBUG_PRINT(ad, DBG_TYPE_TX, NEXT_SEND,
					DBG_LVL_ALL,
					"Idle Mode Request msg failed to reach to Modem");
			/* Signalling the cntrl pkt path in Ioctl */
			wake_up(&ps_adapter->lowpower_mode_wait_queue);
			StartInterruptUrb(interface);
			return;
		}

		if (ps_adapter->bDoSuspend == false) {
			ps_adapter->IdleMode = TRUE;
			/* since going in Idle mode completed hence making this var false */
			ps_adapter->bPreparingForLowPowerMode = false;

			BCM_DEBUG_PRINT(ad, DBG_TYPE_TX, NEXT_SEND,
					DBG_LVL_ALL,
					"Host Entered in Idle Mode State...");
			/* Signalling the cntrl pkt path in Ioctl*/
			wake_up(&ps_adapter->lowpower_mode_wait_queue);
		}

	} else if ((p_control_msg->Leader.Status == LINK_UP_CONTROL_REQ) &&
		(p_control_msg->szData[0] == LINK_UP_ACK) &&
		(p_control_msg->szData[1] == LINK_SHUTDOWN_REQ_FROM_FIRMWARE)  &&
		(p_control_msg->szData[2] == SHUTDOWN_ACK_FROM_DRIVER)) {
		/*
		 * This covers the bus err while shutdown Request
		 * msg sent down.
		 */
		if (urb->status != STATUS_SUCCESS) {
			ps_adapter->bPreparingForLowPowerMode = false;
			BCM_DEBUG_PRINT(ad, DBG_TYPE_TX, NEXT_SEND,
					DBG_LVL_ALL,
					"Shutdown Request Msg failed to reach to Modem");
			/* Signalling the cntrl pkt path in Ioctl */
			wake_up(&ps_adapter->lowpower_mode_wait_queue);
			StartInterruptUrb(interface);
			return;
		}

		*b_power_down_msg = TRUE;
		if (ps_adapter->bDoSuspend == false) {
			ps_adapter->bShutStatus = TRUE;
			/*
			 * since going in shutdown mode completed hence
			 * making this var false
			 */
			ps_adapter->bPreparingForLowPowerMode = false;
			BCM_DEBUG_PRINT(ad, DBG_TYPE_TX, NEXT_SEND,
					DBG_LVL_ALL,
					"Host Entered in shutdown Mode State...");
			/* Signalling the cntrl pkt path in Ioctl */
			wake_up(&ps_adapter->lowpower_mode_wait_queue);
		}
	}

	if (ps_adapter->bDoSuspend && *b_power_down_msg) {
		/* issuing bus suspend request */
		BCM_DEBUG_PRINT(ad, DBG_TYPE_TX, NEXT_SEND, DBG_LVL_ALL,
				"Issuing the Bus suspend request to USB stack");
		interface->bPreparingForBusSuspend = TRUE;
		schedule_work(&interface->usbSuspendWork);
	}
}

/*this is transmit call-back(BULK OUT)*/
static void write_bulk_callback(struct urb *urb/*, struct pt_regs *regs*/)
{
	struct bcm_usb_tcb *pTcb = (struct bcm_usb_tcb *)urb->context;
	struct bcm_interface_adapter *psIntfAdapter = pTcb->psIntfAdapter;
	struct bcm_link_request *pControlMsg =
		(struct bcm_link_request *)urb->transfer_buffer;
	struct bcm_mini_adapter *psAdapter = psIntfAdapter->psAdapter;
	bool bpowerDownMsg = false;
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);

	if (unlikely(netif_msg_tx_done(Adapter)))
		pr_info(PFX "%s: transmit status %d\n", Adapter->dev->name,
			urb->status);

	if (urb->status != STATUS_SUCCESS) {
		if (urb->status == -EPIPE) {
			psIntfAdapter->psAdapter->bEndPointHalted = TRUE;
			wake_up(&psIntfAdapter->psAdapter->tx_packet_wait_queue);
		} else {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_TX, NEXT_SEND,
					DBG_LVL_ALL,
					"Tx URB has got cancelled. status :%d",
					urb->status);
		}
	}

	pTcb->bUsed = false;
	atomic_dec(&psIntfAdapter->uNumTcbUsed);

	if (TRUE == psAdapter->bPreparingForLowPowerMode) {
		prepare_low_power_mode(urb, psIntfAdapter, psAdapter, Adapter,
				       pControlMsg, &bpowerDownMsg);
	}

	usb_free_coherent(urb->dev, urb->transfer_buffer_length,
			urb->transfer_buffer, urb->transfer_dma);
}


static struct bcm_usb_tcb *GetBulkOutTcb(struct bcm_interface_adapter *psIntfAdapter)
{
	struct bcm_usb_tcb *pTcb = NULL;
	UINT index = 0;

	if ((atomic_read(&psIntfAdapter->uNumTcbUsed) < MAXIMUM_USB_TCB) &&
		(psIntfAdapter->psAdapter->StopAllXaction == false)) {
		index = atomic_read(&psIntfAdapter->uCurrTcb);
		pTcb = &psIntfAdapter->asUsbTcb[index];
		pTcb->bUsed = TRUE;
		pTcb->psIntfAdapter = psIntfAdapter;
		BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_TX,
				NEXT_SEND, DBG_LVL_ALL,
				"Got Tx desc %d used %d",
				index,
				atomic_read(&psIntfAdapter->uNumTcbUsed));
		index = (index + 1) % MAXIMUM_USB_TCB;
		atomic_set(&psIntfAdapter->uCurrTcb, index);
		atomic_inc(&psIntfAdapter->uNumTcbUsed);
	}
	return pTcb;
}

static int TransmitTcb(struct bcm_interface_adapter *psIntfAdapter,
		       struct bcm_usb_tcb *pTcb, PVOID data, int len)
{

	struct urb *urb = pTcb->urb;
	int retval = 0;

	urb->transfer_buffer = usb_alloc_coherent(psIntfAdapter->udev, len,
						GFP_ATOMIC, &urb->transfer_dma);
	if (!urb->transfer_buffer) {
		BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_PRINTK, 0, 0,
				"Error allocating memory\n");
		return  -ENOMEM;
	}
	memcpy(urb->transfer_buffer, data, len);
	urb->transfer_buffer_length = len;

	BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_TX, NEXT_SEND,
			DBG_LVL_ALL, "Sending Bulk out packet\n");
	/* For T3B,INT OUT end point will be used as bulk out end point */
	if ((psIntfAdapter->psAdapter->chip_id == T3B) &&
			(psIntfAdapter->bHighSpeedDevice == TRUE)) {
		usb_fill_int_urb(urb, psIntfAdapter->udev,
			psIntfAdapter->sBulkOut.bulk_out_pipe,
			urb->transfer_buffer, len, write_bulk_callback, pTcb,
			psIntfAdapter->sBulkOut.int_out_interval);
	} else {
	usb_fill_bulk_urb(urb, psIntfAdapter->udev,
		  psIntfAdapter->sBulkOut.bulk_out_pipe,
		  urb->transfer_buffer, len, write_bulk_callback, pTcb);
	}
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP; /* For DMA transfer */

	if (false == psIntfAdapter->psAdapter->device_removed &&
	   false == psIntfAdapter->psAdapter->bEndPointHalted &&
	   false == psIntfAdapter->bSuspended &&
	   false == psIntfAdapter->bPreparingForBusSuspend) {
		retval = usb_submit_urb(urb, GFP_ATOMIC);
		if (retval) {
			BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_TX,
					NEXT_SEND, DBG_LVL_ALL,
					"failed submitting write urb, error %d",
					retval);
			if (retval == -EPIPE) {
				psIntfAdapter->psAdapter->bEndPointHalted = TRUE;
				wake_up(&psIntfAdapter->psAdapter->tx_packet_wait_queue);
			}
		}
	}
	return retval;
}

int InterfaceTransmitPacket(PVOID arg, PVOID data, UINT len)
{
	struct bcm_usb_tcb *pTcb = NULL;
	struct bcm_interface_adapter *psIntfAdapter = arg;

	pTcb = GetBulkOutTcb(psIntfAdapter);
	if (pTcb == NULL) {
		BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_PRINTK, 0, 0,
				"No URB to transmit packet, dropping packet");
		return -EFAULT;
	}
	return TransmitTcb(psIntfAdapter, pTcb, data, len);
}

