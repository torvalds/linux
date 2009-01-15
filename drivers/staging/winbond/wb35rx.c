//============================================================================
//  Copyright (c) 1996-2002 Winbond Electronic Corporation
//
//  Module Name:
//    Wb35Rx.c
//
//  Abstract:
//    Processing the Rx message from down layer
//
//============================================================================
#include <linux/usb.h>

#include "core.h"
#include "sysdef.h"
#include "wb35rx_f.h"

static void packet_came(struct ieee80211_hw *hw, char *pRxBufferAddress, int PacketSize)
{
	struct wbsoft_priv *priv = hw->priv;
	struct sk_buff *skb;
	struct ieee80211_rx_status rx_status = {0};

	if (!priv->enabled)
		return;

	skb = dev_alloc_skb(PacketSize);
	if (!skb) {
		printk("Not enough memory for packet, FIXME\n");
		return;
	}

	memcpy(skb_put(skb, PacketSize),
	       pRxBufferAddress,
	       PacketSize);

/*
	rx_status.rate = 10;
	rx_status.channel = 1;
	rx_status.freq = 12345;
	rx_status.phymode = MODE_IEEE80211B;
*/

	ieee80211_rx_irqsafe(hw, skb, &rx_status);
}

static void Wb35Rx_adjust(PDESCRIPTOR pRxDes)
{
	u32 *	pRxBufferAddress;
	u32	DecryptionMethod;
	u32	i;
	u16	BufferSize;

	DecryptionMethod = pRxDes->R01.R01_decryption_method;
	pRxBufferAddress = pRxDes->buffer_address[0];
	BufferSize = pRxDes->buffer_size[0];

	// Adjust the last part of data. Only data left
	BufferSize -= 4; // For CRC-32
	if (DecryptionMethod)
		BufferSize -= 4;
	if (DecryptionMethod == 3) // For CCMP
		BufferSize -= 4;

	// Adjust the IV field which after 802.11 header and ICV field.
	if (DecryptionMethod == 1) // For WEP
	{
		for( i=6; i>0; i-- )
			pRxBufferAddress[i] = pRxBufferAddress[i-1];
		pRxDes->buffer_address[0] = pRxBufferAddress + 1;
		BufferSize -= 4; // 4 byte for IV
	}
	else if( DecryptionMethod ) // For TKIP and CCMP
	{
		for (i=7; i>1; i--)
			pRxBufferAddress[i] = pRxBufferAddress[i-2];
		pRxDes->buffer_address[0] = pRxBufferAddress + 2;//Update the descriptor, shift 8 byte
		BufferSize -= 8; // 8 byte for IV + ICV
	}
	pRxDes->buffer_size[0] = BufferSize;
}

static u16 Wb35Rx_indicate(struct ieee80211_hw *hw)
{
	struct wbsoft_priv *priv = hw->priv;
	phw_data_t pHwData = &priv->sHwData;
	DESCRIPTOR	RxDes;
	PWB35RX	pWb35Rx = &pHwData->Wb35Rx;
	u8 *		pRxBufferAddress;
	u16		PacketSize;
	u16		stmp, BufferSize, stmp2 = 0;
	u32		RxBufferId;

	// Only one thread be allowed to run into the following
	do {
		RxBufferId = pWb35Rx->RxProcessIndex;
		if (pWb35Rx->RxOwner[ RxBufferId ]) //Owner by VM
			break;

		pWb35Rx->RxProcessIndex++;
		pWb35Rx->RxProcessIndex %= MAX_USB_RX_BUFFER_NUMBER;

		pRxBufferAddress = pWb35Rx->pDRx;
		BufferSize = pWb35Rx->RxBufferSize[ RxBufferId ];

		// Parse the bulkin buffer
		while (BufferSize >= 4) {
			if ((cpu_to_le32(*(u32 *)pRxBufferAddress) & 0x0fffffff) == RX_END_TAG) //Is ending? 921002.9.a
				break;

			// Get the R00 R01 first
			RxDes.R00.value = le32_to_cpu(*(u32 *)pRxBufferAddress);
			PacketSize = (u16)RxDes.R00.R00_receive_byte_count;
			RxDes.R01.value = le32_to_cpu(*((u32 *)(pRxBufferAddress+4)));
			// For new DMA 4k
			if ((PacketSize & 0x03) > 0)
				PacketSize -= 4;

			// Basic check for Rx length. Is length valid?
			if (PacketSize > MAX_PACKET_SIZE) {
				#ifdef _PE_RX_DUMP_
				WBDEBUG(("Serious ERROR : Rx data size too long, size =%d\n", PacketSize));
				#endif

				pWb35Rx->EP3vm_state = VM_STOP;
				pWb35Rx->Ep3ErrorCount2++;
				break;
			}

			// Start to process Rx buffer
//			RxDes.Descriptor_ID = RxBufferId; // Due to synchronous indicate, the field doesn't necessary to use.
			BufferSize -= 8; //subtract 8 byte for 35's USB header length
			pRxBufferAddress += 8;

			RxDes.buffer_address[0] = pRxBufferAddress;
			RxDes.buffer_size[0] = PacketSize;
			RxDes.buffer_number = 1;
			RxDes.buffer_start_index = 0;
			RxDes.buffer_total_size = RxDes.buffer_size[0];
			Wb35Rx_adjust(&RxDes);

			packet_came(hw, pRxBufferAddress, PacketSize);

			// Move RxBuffer point to the next
			stmp = PacketSize + 3;
			stmp &= ~0x03; // 4n alignment
			pRxBufferAddress += stmp;
			BufferSize -= stmp;
			stmp2 += stmp;
		}

		// Reclaim resource
		pWb35Rx->RxOwner[ RxBufferId ] = 1;
	} while (true);

	return stmp2;
}

static void Wb35Rx(struct ieee80211_hw *hw);

static void Wb35Rx_Complete(struct urb *urb)
{
	struct ieee80211_hw *hw = urb->context;
	struct wbsoft_priv *priv = hw->priv;
	phw_data_t pHwData = &priv->sHwData;
	PWB35RX		pWb35Rx = &pHwData->Wb35Rx;
	u8 *		pRxBufferAddress;
	u32		SizeCheck;
	u16		BulkLength;
	u32		RxBufferId;
	R00_DESCRIPTOR 	R00;

	// Variable setting
	pWb35Rx->EP3vm_state = VM_COMPLETED;
	pWb35Rx->EP3VM_status = urb->status;//Store the last result of Irp

	RxBufferId = pWb35Rx->CurrentRxBufferId;

	pRxBufferAddress = pWb35Rx->pDRx;
	BulkLength = (u16)urb->actual_length;

	// The IRP is completed
	pWb35Rx->EP3vm_state = VM_COMPLETED;

	if (pHwData->SurpriseRemove || pHwData->HwStop) // Must be here, or RxBufferId is invalid
		goto error;

	if (pWb35Rx->rx_halt)
		goto error;

	// Start to process the data only in successful condition
	pWb35Rx->RxOwner[ RxBufferId ] = 0; // Set the owner to driver
	R00.value = le32_to_cpu(*(u32 *)pRxBufferAddress);

	// The URB is completed, check the result
	if (pWb35Rx->EP3VM_status != 0) {
		#ifdef _PE_USB_STATE_DUMP_
		WBDEBUG(("EP3 IoCompleteRoutine return error\n"));
		DebugUsbdStatusInformation( pWb35Rx->EP3VM_status );
		#endif
		pWb35Rx->EP3vm_state = VM_STOP;
		goto error;
	}

	// 20060220 For recovering. check if operating in single USB mode
	if (!HAL_USB_MODE_BURST(pHwData)) {
		SizeCheck = R00.R00_receive_byte_count;  //20060926 anson's endian
		if ((SizeCheck & 0x03) > 0)
			SizeCheck -= 4;
		SizeCheck = (SizeCheck + 3) & ~0x03;
		SizeCheck += 12; // 8 + 4 badbeef
		if ((BulkLength > 1600) ||
			(SizeCheck > 1600) ||
			(BulkLength != SizeCheck) ||
			(BulkLength == 0)) { // Add for fail Urb
			pWb35Rx->EP3vm_state = VM_STOP;
			pWb35Rx->Ep3ErrorCount2++;
		}
	}

	// Indicating the receiving data
	pWb35Rx->ByteReceived += BulkLength;
	pWb35Rx->RxBufferSize[ RxBufferId ] = BulkLength;

	if (!pWb35Rx->RxOwner[ RxBufferId ])
		Wb35Rx_indicate(hw);

	kfree(pWb35Rx->pDRx);
	// Do the next receive
	Wb35Rx(hw);
	return;

error:
	pWb35Rx->RxOwner[ RxBufferId ] = 1; // Set the owner to hardware
	atomic_dec(&pWb35Rx->RxFireCounter);
	pWb35Rx->EP3vm_state = VM_STOP;
}

// This function cannot reentrain
static void Wb35Rx(struct ieee80211_hw *hw)
{
	struct wbsoft_priv *priv = hw->priv;
	phw_data_t pHwData = &priv->sHwData;
	PWB35RX	pWb35Rx = &pHwData->Wb35Rx;
	u8 *	pRxBufferAddress;
	struct urb *urb = pWb35Rx->RxUrb;
	int	retv;
	u32	RxBufferId;

	//
	// Issuing URB
	//
	if (pHwData->SurpriseRemove || pHwData->HwStop)
		goto error;

	if (pWb35Rx->rx_halt)
		goto error;

	// Get RxBuffer's ID
	RxBufferId = pWb35Rx->RxBufferId;
	if (!pWb35Rx->RxOwner[RxBufferId]) {
		// It's impossible to run here.
		#ifdef _PE_RX_DUMP_
		WBDEBUG(("Rx driver fifo unavailable\n"));
		#endif
		goto error;
	}

	// Update buffer point, then start to bulkin the data from USB
	pWb35Rx->RxBufferId++;
	pWb35Rx->RxBufferId %= MAX_USB_RX_BUFFER_NUMBER;

	pWb35Rx->CurrentRxBufferId = RxBufferId;

	pWb35Rx->pDRx = kzalloc(MAX_USB_RX_BUFFER, GFP_ATOMIC);
	if (!pWb35Rx->pDRx) {
		printk("w35und: Rx memory alloc failed\n");
		goto error;
	}
	pRxBufferAddress = pWb35Rx->pDRx;

	usb_fill_bulk_urb(urb, pHwData->WbUsb.udev,
			  usb_rcvbulkpipe(pHwData->WbUsb.udev, 3),
			  pRxBufferAddress, MAX_USB_RX_BUFFER,
			  Wb35Rx_Complete, hw);

	pWb35Rx->EP3vm_state = VM_RUNNING;

	retv = usb_submit_urb(urb, GFP_ATOMIC);

	if (retv != 0) {
		printk("Rx URB sending error\n");
		goto error;
	}
	return;

error:
	// VM stop
	pWb35Rx->EP3vm_state = VM_STOP;
	atomic_dec(&pWb35Rx->RxFireCounter);
}

void Wb35Rx_start(struct ieee80211_hw *hw)
{
	struct wbsoft_priv *priv = hw->priv;
	phw_data_t pHwData = &priv->sHwData;
	PWB35RX pWb35Rx = &pHwData->Wb35Rx;

	// Allow only one thread to run into the Wb35Rx() function
	if (atomic_inc_return(&pWb35Rx->RxFireCounter) == 1) {
		pWb35Rx->EP3vm_state = VM_RUNNING;
		Wb35Rx(hw);
	} else
		atomic_dec(&pWb35Rx->RxFireCounter);
}

//=====================================================================================
static void Wb35Rx_reset_descriptor(  phw_data_t pHwData )
{
	PWB35RX pWb35Rx = &pHwData->Wb35Rx;
	u32	i;

	pWb35Rx->ByteReceived = 0;
	pWb35Rx->RxProcessIndex = 0;
	pWb35Rx->RxBufferId = 0;
	pWb35Rx->EP3vm_state = VM_STOP;
	pWb35Rx->rx_halt = 0;

	// Initial the Queue. The last buffer is reserved for used if the Rx resource is unavailable.
	for( i=0; i<MAX_USB_RX_BUFFER_NUMBER; i++ )
		pWb35Rx->RxOwner[i] = 1;
}

unsigned char Wb35Rx_initial(phw_data_t pHwData)
{
	PWB35RX pWb35Rx = &pHwData->Wb35Rx;

	// Initial the Buffer Queue
	Wb35Rx_reset_descriptor( pHwData );

	pWb35Rx->RxUrb = usb_alloc_urb(0, GFP_ATOMIC);
	return (!!pWb35Rx->RxUrb);
}

void Wb35Rx_stop(phw_data_t pHwData)
{
	PWB35RX pWb35Rx = &pHwData->Wb35Rx;

	// Canceling the Irp if already sends it out.
	if (pWb35Rx->EP3vm_state == VM_RUNNING) {
		usb_unlink_urb( pWb35Rx->RxUrb ); // Only use unlink, let Wb35Rx_destroy to free them
		#ifdef _PE_RX_DUMP_
		WBDEBUG(("EP3 Rx stop\n"));
		#endif
	}
}

// Needs process context
void Wb35Rx_destroy(phw_data_t pHwData)
{
	PWB35RX pWb35Rx = &pHwData->Wb35Rx;

	do {
		msleep(10); // Delay for waiting function enter 940623.1.a
	} while (pWb35Rx->EP3vm_state != VM_STOP);
	msleep(10); // Delay for waiting function exit 940623.1.b

	if (pWb35Rx->RxUrb)
		usb_free_urb( pWb35Rx->RxUrb );
	#ifdef _PE_RX_DUMP_
	WBDEBUG(("Wb35Rx_destroy OK\n"));
	#endif
}

