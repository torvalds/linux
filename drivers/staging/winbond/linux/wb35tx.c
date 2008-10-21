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
#include "sysdef.h"


unsigned char
Wb35Tx_get_tx_buffer(phw_data_t pHwData, PUCHAR *pBuffer )
{
	PWB35TX pWb35Tx = &pHwData->Wb35Tx;

	*pBuffer = pWb35Tx->TxBuffer[0];
	return TRUE;
}

void Wb35Tx_start(phw_data_t pHwData)
{
	PWB35TX pWb35Tx = &pHwData->Wb35Tx;

	// Allow only one thread to run into function
	if (OS_ATOMIC_INC(pHwData->Adapter, &pWb35Tx->TxFireCounter) == 1) {
		pWb35Tx->EP4vm_state = VM_RUNNING;
		Wb35Tx(pHwData);
	} else
		OS_ATOMIC_DEC( pHwData->Adapter, &pWb35Tx->TxFireCounter );
}


void Wb35Tx(phw_data_t pHwData)
{
	PWB35TX		pWb35Tx = &pHwData->Wb35Tx;
	PADAPTER	Adapter = pHwData->Adapter;
	PUCHAR		pTxBufferAddress;
	PMDS		pMds = &Adapter->Mds;
	struct urb *	pUrb = (struct urb *)pWb35Tx->Tx4Urb;
	int         	retv;
	u32		SendIndex;


	if (pHwData->SurpriseRemove || pHwData->HwStop)
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
	usb_fill_bulk_urb(pUrb, pHwData->WbUsb.udev,
			  usb_sndbulkpipe(pHwData->WbUsb.udev, 4),
			  pTxBufferAddress, pMds->TxBufferSize[ SendIndex ],
			  Wb35Tx_complete, pHwData);

	pWb35Tx->EP4vm_state = VM_RUNNING;
	retv = wb_usb_submit_urb( pUrb );
	if (retv<0) {
		printk("EP4 Tx Irp sending error\n");
		goto cleanup;
	}

	// Check if driver needs issue Irp for EP2
	pWb35Tx->TxFillCount += pMds->TxCountInBuffer[SendIndex];
	if (pWb35Tx->TxFillCount > 12)
		Wb35Tx_EP2VM_start( pHwData );

	pWb35Tx->ByteTransfer += pMds->TxBufferSize[SendIndex];
	return;

 cleanup:
	pWb35Tx->EP4vm_state = VM_STOP;
	OS_ATOMIC_DEC( pHwData->Adapter, &pWb35Tx->TxFireCounter );
}


void Wb35Tx_complete(struct urb * pUrb)
{
	phw_data_t	pHwData = pUrb->context;
	PADAPTER	Adapter = (PADAPTER)pHwData->Adapter;
	PWB35TX		pWb35Tx = &pHwData->Wb35Tx;
	PMDS		pMds = &Adapter->Mds;

	printk("wb35: tx complete\n");
	// Variable setting
	pWb35Tx->EP4vm_state = VM_COMPLETED;
	pWb35Tx->EP4VM_status = pUrb->status; //Store the last result of Irp
	pMds->TxOwner[ pWb35Tx->TxSendIndex ] = 0;// Set the owner. Free the owner bit always.
	pWb35Tx->TxSendIndex++;
	pWb35Tx->TxSendIndex %= MAX_USB_TX_BUFFER_NUMBER;

	do {
		if (pHwData->SurpriseRemove || pHwData->HwStop) // Let WbWlanHalt to handle surprise remove
			break;

		if (pWb35Tx->tx_halt)
			break;

		// The URB is completed, check the result
		if (pWb35Tx->EP4VM_status != 0) {
			printk("URB submission failed\n");
			pWb35Tx->EP4vm_state = VM_STOP;
			break; // Exit while(FALSE);
		}

		Mds_Tx(Adapter);
		Wb35Tx(pHwData);
		return;
	} while(FALSE);

	OS_ATOMIC_DEC( pHwData->Adapter, &pWb35Tx->TxFireCounter );
	pWb35Tx->EP4vm_state = VM_STOP;
}

void Wb35Tx_reset_descriptor(  phw_data_t pHwData )
{
	PWB35TX pWb35Tx = &pHwData->Wb35Tx;

	pWb35Tx->TxSendIndex = 0;
	pWb35Tx->tx_halt = 0;
}

unsigned char Wb35Tx_initial(phw_data_t pHwData)
{
	PWB35TX pWb35Tx = &pHwData->Wb35Tx;

	pWb35Tx->Tx4Urb = wb_usb_alloc_urb(0);
	if (!pWb35Tx->Tx4Urb)
		return FALSE;

	pWb35Tx->Tx2Urb = wb_usb_alloc_urb(0);
	if (!pWb35Tx->Tx2Urb)
	{
		usb_free_urb( pWb35Tx->Tx4Urb );
		return FALSE;
	}

	return TRUE;
}

//======================================================
void Wb35Tx_stop(phw_data_t pHwData)
{
	PWB35TX pWb35Tx = &pHwData->Wb35Tx;

	// Trying to canceling the Trp of EP2
	if (pWb35Tx->EP2vm_state == VM_RUNNING)
		usb_unlink_urb( pWb35Tx->Tx2Urb ); // Only use unlink, let Wb35Tx_destrot to free them
	#ifdef _PE_TX_DUMP_
	WBDEBUG(("EP2 Tx stop\n"));
	#endif

	// Trying to canceling the Irp of EP4
	if (pWb35Tx->EP4vm_state == VM_RUNNING)
		usb_unlink_urb( pWb35Tx->Tx4Urb ); // Only use unlink, let Wb35Tx_destrot to free them
	#ifdef _PE_TX_DUMP_
	WBDEBUG(("EP4 Tx stop\n"));
	#endif
}

//======================================================
void Wb35Tx_destroy(phw_data_t pHwData)
{
	PWB35TX pWb35Tx = &pHwData->Wb35Tx;

	// Wait for VM stop
	do {
		OS_SLEEP(10000);  // Delay for waiting function enter 940623.1.a
	} while( (pWb35Tx->EP2vm_state != VM_STOP) && (pWb35Tx->EP4vm_state != VM_STOP) );
	OS_SLEEP(10000);  // Delay for waiting function enter 940623.1.b

	if (pWb35Tx->Tx4Urb)
		usb_free_urb( pWb35Tx->Tx4Urb );

	if (pWb35Tx->Tx2Urb)
		usb_free_urb( pWb35Tx->Tx2Urb );

	#ifdef _PE_TX_DUMP_
	WBDEBUG(("Wb35Tx_destroy OK\n"));
	#endif
}

void Wb35Tx_CurrentTime(phw_data_t pHwData, u32 TimeCount)
{
	PWB35TX pWb35Tx = &pHwData->Wb35Tx;
	unsigned char Trigger = FALSE;

	if (pWb35Tx->TxTimer > TimeCount)
		Trigger = TRUE;
	else if (TimeCount > (pWb35Tx->TxTimer+500))
		Trigger = TRUE;

	if (Trigger) {
		pWb35Tx->TxTimer = TimeCount;
		Wb35Tx_EP2VM_start( pHwData );
	}
}

void Wb35Tx_EP2VM_start(phw_data_t pHwData)
{
	PWB35TX pWb35Tx = &pHwData->Wb35Tx;

	// Allow only one thread to run into function
	if (OS_ATOMIC_INC( pHwData->Adapter, &pWb35Tx->TxResultCount ) == 1) {
		pWb35Tx->EP2vm_state = VM_RUNNING;
		Wb35Tx_EP2VM( pHwData );
	}
	else
		OS_ATOMIC_DEC( pHwData->Adapter, &pWb35Tx->TxResultCount );
}


void Wb35Tx_EP2VM(phw_data_t pHwData)
{
	PWB35TX pWb35Tx = &pHwData->Wb35Tx;
	struct urb *	pUrb = (struct urb *)pWb35Tx->Tx2Urb;
	PULONG	pltmp = (PULONG)pWb35Tx->EP2_buf;
	int		retv;

	do {
		if (pHwData->SurpriseRemove || pHwData->HwStop)
			break;

		if (pWb35Tx->tx_halt)
			break;

		//
		// Issuing URB
		//
		usb_fill_int_urb( pUrb, pHwData->WbUsb.udev, usb_rcvintpipe(pHwData->WbUsb.udev,2),
				  pltmp, MAX_INTERRUPT_LENGTH, Wb35Tx_EP2VM_complete, pHwData, 32);

		pWb35Tx->EP2vm_state = VM_RUNNING;
		retv = wb_usb_submit_urb( pUrb );

		if(retv < 0) {
			#ifdef _PE_TX_DUMP_
			WBDEBUG(("EP2 Tx Irp sending error\n"));
			#endif
			break;
		}

		return;

	} while(FALSE);

	pWb35Tx->EP2vm_state = VM_STOP;
	OS_ATOMIC_DEC( pHwData->Adapter, &pWb35Tx->TxResultCount );
}


void Wb35Tx_EP2VM_complete(struct urb * pUrb)
{
	phw_data_t	pHwData = pUrb->context;
	T02_DESCRIPTOR	T02, TSTATUS;
	PADAPTER	Adapter = (PADAPTER)pHwData->Adapter;
	PWB35TX		pWb35Tx = &pHwData->Wb35Tx;
	PULONG		pltmp = (PULONG)pWb35Tx->EP2_buf;
	u32		i;
	u16		InterruptInLength;


	// Variable setting
	pWb35Tx->EP2vm_state = VM_COMPLETED;
	pWb35Tx->EP2VM_status = pUrb->status;

	do {
		// For Linux 2.4. Interrupt will always trigger
		if( pHwData->SurpriseRemove || pHwData->HwStop ) // Let WbWlanHalt to handle surprise remove
			break;

		if( pWb35Tx->tx_halt )
			break;

		//The Urb is completed, check the result
		if (pWb35Tx->EP2VM_status != 0) {
			WBDEBUG(("EP2 IoCompleteRoutine return error\n"));
			pWb35Tx->EP2vm_state= VM_STOP;
			break; // Exit while(FALSE);
		}

		// Update the Tx result
		InterruptInLength = pUrb->actual_length;
		// Modify for minimum memory access and DWORD alignment.
		T02.value = cpu_to_le32(pltmp[0]) >> 8; // [31:8] -> [24:0]
		InterruptInLength -= 1;// 20051221.1.c Modify the follow for more stable
		InterruptInLength >>= 2; // InterruptInLength/4
		for (i=1; i<=InterruptInLength; i++) {
			T02.value |= ((cpu_to_le32(pltmp[i]) & 0xff) << 24);

			TSTATUS.value = T02.value;  //20061009 anson's endian
			Mds_SendComplete( Adapter, &TSTATUS );
			T02.value = cpu_to_le32(pltmp[i]) >> 8;
		}

		return;
	} while(FALSE);

	OS_ATOMIC_DEC( pHwData->Adapter, &pWb35Tx->TxResultCount );
	pWb35Tx->EP2vm_state = VM_STOP;
}

