//============================================================================
//  Copyright (c) 1996-2002 Winbond Electronic Corporation
//
//  Module Name:
//    Wb35Tx.c
//
//  Abstract:
//    Processing the Tx message and put into down layer
//
//============================================================================
#include <linux/usb.h>
#include <linux/gfp.h>

#include "wb35tx_f.h"
#include "mds_f.h"

unsigned char
Wb35Tx_get_tx_buffer(struct hw_data * pHwData, u8 **pBuffer)
{
	struct wb35_tx *pWb35Tx = &pHwData->Wb35Tx;

	*pBuffer = pWb35Tx->TxBuffer[0];
	return true;
}

static void Wb35Tx(struct wbsoft_priv *adapter);

static void Wb35Tx_complete(struct urb * pUrb)
{
	struct wbsoft_priv *adapter = pUrb->context;
	struct hw_data *	pHwData = &adapter->sHwData;
	struct wb35_tx *pWb35Tx = &pHwData->Wb35Tx;
	struct wb35_mds *pMds = &adapter->Mds;

	printk("wb35: tx complete\n");
	// Variable setting
	pWb35Tx->EP4vm_state = VM_COMPLETED;
	pWb35Tx->EP4VM_status = pUrb->status; //Store the last result of Irp
	pMds->TxOwner[ pWb35Tx->TxSendIndex ] = 0;// Set the owner. Free the owner bit always.
	pWb35Tx->TxSendIndex++;
	pWb35Tx->TxSendIndex %= MAX_USB_TX_BUFFER_NUMBER;

	if (pHwData->SurpriseRemove) // Let WbWlanHalt to handle surprise remove
		goto error;

	if (pWb35Tx->tx_halt)
		goto error;

	// The URB is completed, check the result
	if (pWb35Tx->EP4VM_status != 0) {
		printk("URB submission failed\n");
		pWb35Tx->EP4vm_state = VM_STOP;
		goto error;
	}

	Mds_Tx(adapter);
	Wb35Tx(adapter);
	return;

error:
	atomic_dec(&pWb35Tx->TxFireCounter);
	pWb35Tx->EP4vm_state = VM_STOP;
}

static void Wb35Tx(struct wbsoft_priv *adapter)
{
	struct hw_data *	pHwData = &adapter->sHwData;
	struct wb35_tx *pWb35Tx = &pHwData->Wb35Tx;
	u8		*pTxBufferAddress;
	struct wb35_mds *pMds = &adapter->Mds;
	struct urb *	pUrb = (struct urb *)pWb35Tx->Tx4Urb;
	int         	retv;
	u32		SendIndex;


	if (pHwData->SurpriseRemove)
		goto cleanup;

	if (pWb35Tx->tx_halt)
		goto cleanup;

	// Ownership checking
	SendIndex = pWb35Tx->TxSendIndex;
	if (!pMds->TxOwner[SendIndex]) //No more data need to be sent, return immediately
		goto cleanup;

	pTxBufferAddress = pWb35Tx->TxBuffer[SendIndex];
	//
	// Issuing URB
	//
	usb_fill_bulk_urb(pUrb, pHwData->udev,
			  usb_sndbulkpipe(pHwData->udev, 4),
			  pTxBufferAddress, pMds->TxBufferSize[ SendIndex ],
			  Wb35Tx_complete, adapter);

	pWb35Tx->EP4vm_state = VM_RUNNING;
	retv = usb_submit_urb(pUrb, GFP_ATOMIC);
	if (retv<0) {
		printk("EP4 Tx Irp sending error\n");
		goto cleanup;
	}

	// Check if driver needs issue Irp for EP2
	pWb35Tx->TxFillCount += pMds->TxCountInBuffer[SendIndex];
	if (pWb35Tx->TxFillCount > 12)
		Wb35Tx_EP2VM_start(adapter);

	pWb35Tx->ByteTransfer += pMds->TxBufferSize[SendIndex];
	return;

 cleanup:
	pWb35Tx->EP4vm_state = VM_STOP;
	atomic_dec(&pWb35Tx->TxFireCounter);
}

void Wb35Tx_start(struct wbsoft_priv *adapter)
{
	struct hw_data * pHwData = &adapter->sHwData;
	struct wb35_tx *pWb35Tx = &pHwData->Wb35Tx;

	// Allow only one thread to run into function
	if (atomic_inc_return(&pWb35Tx->TxFireCounter) == 1) {
		pWb35Tx->EP4vm_state = VM_RUNNING;
		Wb35Tx(adapter);
	} else
		atomic_dec(&pWb35Tx->TxFireCounter);
}

unsigned char Wb35Tx_initial(struct hw_data * pHwData)
{
	struct wb35_tx *pWb35Tx = &pHwData->Wb35Tx;

	pWb35Tx->Tx4Urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!pWb35Tx->Tx4Urb)
		return false;

	pWb35Tx->Tx2Urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!pWb35Tx->Tx2Urb)
	{
		usb_free_urb( pWb35Tx->Tx4Urb );
		return false;
	}

	return true;
}

//======================================================
void Wb35Tx_stop(struct hw_data * pHwData)
{
	struct wb35_tx *pWb35Tx = &pHwData->Wb35Tx;

	// Trying to canceling the Trp of EP2
	if (pWb35Tx->EP2vm_state == VM_RUNNING)
		usb_unlink_urb( pWb35Tx->Tx2Urb ); // Only use unlink, let Wb35Tx_destrot to free them
	pr_debug("EP2 Tx stop\n");

	// Trying to canceling the Irp of EP4
	if (pWb35Tx->EP4vm_state == VM_RUNNING)
		usb_unlink_urb( pWb35Tx->Tx4Urb ); // Only use unlink, let Wb35Tx_destrot to free them
	pr_debug("EP4 Tx stop\n");
}

//======================================================
void Wb35Tx_destroy(struct hw_data * pHwData)
{
	struct wb35_tx *pWb35Tx = &pHwData->Wb35Tx;

	// Wait for VM stop
	do {
		msleep(10);  // Delay for waiting function enter 940623.1.a
	} while( (pWb35Tx->EP2vm_state != VM_STOP) && (pWb35Tx->EP4vm_state != VM_STOP) );
	msleep(10);  // Delay for waiting function enter 940623.1.b

	if (pWb35Tx->Tx4Urb)
		usb_free_urb( pWb35Tx->Tx4Urb );

	if (pWb35Tx->Tx2Urb)
		usb_free_urb( pWb35Tx->Tx2Urb );

	pr_debug("Wb35Tx_destroy OK\n");
}

void Wb35Tx_CurrentTime(struct wbsoft_priv *adapter, u32 TimeCount)
{
	struct hw_data * pHwData = &adapter->sHwData;
	struct wb35_tx *pWb35Tx = &pHwData->Wb35Tx;
	unsigned char Trigger = false;

	if (pWb35Tx->TxTimer > TimeCount)
		Trigger = true;
	else if (TimeCount > (pWb35Tx->TxTimer+500))
		Trigger = true;

	if (Trigger) {
		pWb35Tx->TxTimer = TimeCount;
		Wb35Tx_EP2VM_start(adapter);
	}
}

static void Wb35Tx_EP2VM(struct wbsoft_priv *adapter);

static void Wb35Tx_EP2VM_complete(struct urb * pUrb)
{
	struct wbsoft_priv *adapter = pUrb->context;
	struct hw_data *	pHwData = &adapter->sHwData;
	struct T02_descriptor	T02, TSTATUS;
	struct wb35_tx *pWb35Tx = &pHwData->Wb35Tx;
	u32 *		pltmp = (u32 *)pWb35Tx->EP2_buf;
	u32		i;
	u16		InterruptInLength;


	// Variable setting
	pWb35Tx->EP2vm_state = VM_COMPLETED;
	pWb35Tx->EP2VM_status = pUrb->status;

	// For Linux 2.4. Interrupt will always trigger
	if (pHwData->SurpriseRemove) // Let WbWlanHalt to handle surprise remove
		goto error;

	if (pWb35Tx->tx_halt)
		goto error;

	//The Urb is completed, check the result
	if (pWb35Tx->EP2VM_status != 0) {
		printk("EP2 IoCompleteRoutine return error\n");
		pWb35Tx->EP2vm_state= VM_STOP;
		goto error;
	}

	// Update the Tx result
	InterruptInLength = pUrb->actual_length;
	// Modify for minimum memory access and DWORD alignment.
	T02.value = cpu_to_le32(pltmp[0]) >> 8; // [31:8] -> [24:0]
	InterruptInLength -= 1;// 20051221.1.c Modify the follow for more stable
	InterruptInLength >>= 2; // InterruptInLength/4
	for (i = 1; i <= InterruptInLength; i++) {
		T02.value |= ((cpu_to_le32(pltmp[i]) & 0xff) << 24);

		TSTATUS.value = T02.value;  //20061009 anson's endian
		Mds_SendComplete( adapter, &TSTATUS );
		T02.value = cpu_to_le32(pltmp[i]) >> 8;
	}

	return;
error:
	atomic_dec(&pWb35Tx->TxResultCount);
	pWb35Tx->EP2vm_state = VM_STOP;
}

static void Wb35Tx_EP2VM(struct wbsoft_priv *adapter)
{
	struct hw_data *	pHwData = &adapter->sHwData;
	struct wb35_tx *pWb35Tx = &pHwData->Wb35Tx;
	struct urb *	pUrb = (struct urb *)pWb35Tx->Tx2Urb;
	u32 *	pltmp = (u32 *)pWb35Tx->EP2_buf;
	int		retv;

	if (pHwData->SurpriseRemove)
		goto error;

	if (pWb35Tx->tx_halt)
		goto error;

	//
	// Issuing URB
	//
	usb_fill_int_urb( pUrb, pHwData->udev, usb_rcvintpipe(pHwData->udev,2),
			  pltmp, MAX_INTERRUPT_LENGTH, Wb35Tx_EP2VM_complete, adapter, 32);

	pWb35Tx->EP2vm_state = VM_RUNNING;
	retv = usb_submit_urb(pUrb, GFP_ATOMIC);

	if (retv < 0) {
		pr_debug("EP2 Tx Irp sending error\n");
		goto error;
	}

	return;
error:
	pWb35Tx->EP2vm_state = VM_STOP;
	atomic_dec(&pWb35Tx->TxResultCount);
}

void Wb35Tx_EP2VM_start(struct wbsoft_priv *adapter)
{
	struct hw_data * pHwData = &adapter->sHwData;
	struct wb35_tx *pWb35Tx = &pHwData->Wb35Tx;

	// Allow only one thread to run into function
	if (atomic_inc_return(&pWb35Tx->TxResultCount) == 1) {
		pWb35Tx->EP2vm_state = VM_RUNNING;
		Wb35Tx_EP2VM(adapter);
	}
	else
		atomic_dec(&pWb35Tx->TxResultCount);
}
