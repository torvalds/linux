#include "sysdef.h"

extern void phy_calibration_winbond(hw_data_t *phw_data, u32 frequency);

// TRUE  : read command process successfully
// FALSE : register not support
// RegisterNo : start base
// pRegisterData : data point
// NumberOfData : number of register data
// Flag : AUTO_INCREMENT - RegisterNo will auto increment 4
//		  NO_INCREMENT - Function will write data into the same register
unsigned char
Wb35Reg_BurstWrite(phw_data_t pHwData, u16 RegisterNo, PULONG pRegisterData, u8 NumberOfData, u8 Flag)
{
	PWB35REG pWb35Reg = &pHwData->Wb35Reg;
	PURB		pUrb = NULL;
	PREG_QUEUE	pRegQueue = NULL;
	u16		UrbSize;
	struct      usb_ctrlrequest *dr;
	u16		i, DataSize = NumberOfData*4;

	// Module shutdown
	if (pHwData->SurpriseRemove)
		return FALSE;

	// Trying to use burst write function if use new hardware
	UrbSize = sizeof(REG_QUEUE) + DataSize + sizeof(struct usb_ctrlrequest);
	OS_MEMORY_ALLOC( (void* *)&pRegQueue, UrbSize );
	pUrb = wb_usb_alloc_urb(0);
	if( pUrb && pRegQueue ) {
		pRegQueue->DIRECT = 2;// burst write register
		pRegQueue->INDEX = RegisterNo;
		pRegQueue->pBuffer = (PULONG)((PUCHAR)pRegQueue + sizeof(REG_QUEUE));
		memcpy( pRegQueue->pBuffer, pRegisterData, DataSize );
		//the function for reversing register data from little endian to big endian
		for( i=0; i<NumberOfData ; i++ )
			pRegQueue->pBuffer[i] = cpu_to_le32( pRegQueue->pBuffer[i] );

		dr = (struct usb_ctrlrequest *)((PUCHAR)pRegQueue + sizeof(REG_QUEUE) + DataSize);
		dr->bRequestType = USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE;
		dr->bRequest = 0x04; // USB or vendor-defined request code, burst mode
		dr->wValue = cpu_to_le16( Flag ); // 0: Register number auto-increment, 1: No auto increment
		dr->wIndex = cpu_to_le16( RegisterNo );
		dr->wLength = cpu_to_le16( DataSize );
		pRegQueue->Next = NULL;
		pRegQueue->pUsbReq = dr;
		pRegQueue->pUrb = pUrb;

		OS_SPIN_LOCK_ACQUIRED( &pWb35Reg->EP0VM_spin_lock );
		if (pWb35Reg->pRegFirst == NULL)
			pWb35Reg->pRegFirst = pRegQueue;
		else
			pWb35Reg->pRegLast->Next = pRegQueue;
		pWb35Reg->pRegLast = pRegQueue;

		OS_SPIN_LOCK_RELEASED( &pWb35Reg->EP0VM_spin_lock );

		// Start EP0VM
		Wb35Reg_EP0VM_start(pHwData);

		return TRUE;
	} else {
		if (pUrb)
			usb_free_urb(pUrb);
		if (pRegQueue)
			kfree(pRegQueue);
		return FALSE;
	}
   return FALSE;
}

void
Wb35Reg_Update(phw_data_t pHwData,  u16 RegisterNo,  u32 RegisterValue)
{
	PWB35REG	pWb35Reg = &pHwData->Wb35Reg;
	switch (RegisterNo) {
	case 0x3b0: pWb35Reg->U1B0 = RegisterValue; break;
	case 0x3bc: pWb35Reg->U1BC_LEDConfigure = RegisterValue; break;
	case 0x400: pWb35Reg->D00_DmaControl = RegisterValue; break;
	case 0x800: pWb35Reg->M00_MacControl = RegisterValue; break;
	case 0x804: pWb35Reg->M04_MulticastAddress1 = RegisterValue; break;
	case 0x808: pWb35Reg->M08_MulticastAddress2 = RegisterValue; break;
	case 0x824: pWb35Reg->M24_MacControl = RegisterValue; break;
	case 0x828: pWb35Reg->M28_MacControl = RegisterValue; break;
	case 0x82c: pWb35Reg->M2C_MacControl = RegisterValue; break;
	case 0x838: pWb35Reg->M38_MacControl = RegisterValue; break;
	case 0x840: pWb35Reg->M40_MacControl = RegisterValue; break;
	case 0x844: pWb35Reg->M44_MacControl = RegisterValue; break;
	case 0x848: pWb35Reg->M48_MacControl = RegisterValue; break;
	case 0x84c: pWb35Reg->M4C_MacStatus = RegisterValue; break;
	case 0x860: pWb35Reg->M60_MacControl = RegisterValue; break;
	case 0x868: pWb35Reg->M68_MacControl = RegisterValue; break;
	case 0x870: pWb35Reg->M70_MacControl = RegisterValue; break;
	case 0x874: pWb35Reg->M74_MacControl = RegisterValue; break;
	case 0x878: pWb35Reg->M78_ERPInformation = RegisterValue; break;
	case 0x87C: pWb35Reg->M7C_MacControl = RegisterValue; break;
	case 0x880: pWb35Reg->M80_MacControl = RegisterValue; break;
	case 0x884: pWb35Reg->M84_MacControl = RegisterValue; break;
	case 0x888: pWb35Reg->M88_MacControl = RegisterValue; break;
	case 0x898: pWb35Reg->M98_MacControl = RegisterValue; break;
	case 0x100c: pWb35Reg->BB0C = RegisterValue; break;
	case 0x102c: pWb35Reg->BB2C = RegisterValue; break;
	case 0x1030: pWb35Reg->BB30 = RegisterValue; break;
	case 0x103c: pWb35Reg->BB3C = RegisterValue; break;
	case 0x1048: pWb35Reg->BB48 = RegisterValue; break;
	case 0x104c: pWb35Reg->BB4C = RegisterValue; break;
	case 0x1050: pWb35Reg->BB50 = RegisterValue; break;
	case 0x1054: pWb35Reg->BB54 = RegisterValue; break;
	case 0x1058: pWb35Reg->BB58 = RegisterValue; break;
	case 0x105c: pWb35Reg->BB5C = RegisterValue; break;
	case 0x1060: pWb35Reg->BB60 = RegisterValue; break;
	}
}

// TRUE  : read command process successfully
// FALSE : register not support
unsigned char
Wb35Reg_WriteSync(  phw_data_t pHwData,  u16 RegisterNo,  u32 RegisterValue )
{
	PWB35REG pWb35Reg = &pHwData->Wb35Reg;
	int ret = -1;

	// Module shutdown
	if (pHwData->SurpriseRemove)
		return FALSE;

	RegisterValue = cpu_to_le32(RegisterValue);

	// update the register by send usb message------------------------------------
	pWb35Reg->SyncIoPause = 1;

	// 20060717.5 Wait until EP0VM stop
	while (pWb35Reg->EP0vm_state != VM_STOP)
		OS_SLEEP(10000);

	// Sync IoCallDriver
	pWb35Reg->EP0vm_state = VM_RUNNING;
	ret = usb_control_msg( pHwData->WbUsb.udev,
			       usb_sndctrlpipe( pHwData->WbUsb.udev, 0 ),
			       0x03, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			       0x0,RegisterNo, &RegisterValue, 4, HZ*100 );
	pWb35Reg->EP0vm_state = VM_STOP;
	pWb35Reg->SyncIoPause = 0;

	Wb35Reg_EP0VM_start(pHwData);

	if (ret < 0) {
		#ifdef _PE_REG_DUMP_
		WBDEBUG(("EP0 Write register usb message sending error\n"));
		#endif

		pHwData->SurpriseRemove = 1; // 20060704.2
		return FALSE;
	}

	return TRUE;
}

// TRUE  : read command process successfully
// FALSE : register not support
unsigned char
Wb35Reg_Write(  phw_data_t pHwData,  u16 RegisterNo,  u32 RegisterValue )
{
	PWB35REG	pWb35Reg = &pHwData->Wb35Reg;
	struct usb_ctrlrequest *dr;
	PURB		pUrb = NULL;
	PREG_QUEUE	pRegQueue = NULL;
	u16		UrbSize;


	// Module shutdown
	if (pHwData->SurpriseRemove)
		return FALSE;

	// update the register by send urb request------------------------------------
	UrbSize = sizeof(REG_QUEUE) + sizeof(struct usb_ctrlrequest);
	OS_MEMORY_ALLOC( (void* *)&pRegQueue, UrbSize );
	pUrb = wb_usb_alloc_urb(0);
	if (pUrb && pRegQueue) {
		pRegQueue->DIRECT = 1;// burst write register
		pRegQueue->INDEX = RegisterNo;
		pRegQueue->VALUE = cpu_to_le32(RegisterValue);
		pRegQueue->RESERVED_VALID = FALSE;
		dr = (struct usb_ctrlrequest *)((PUCHAR)pRegQueue + sizeof(REG_QUEUE));
		dr->bRequestType = USB_TYPE_VENDOR|USB_DIR_OUT |USB_RECIP_DEVICE;
		dr->bRequest = 0x03; // USB or vendor-defined request code, burst mode
		dr->wValue = cpu_to_le16(0x0);
		dr->wIndex = cpu_to_le16(RegisterNo);
		dr->wLength = cpu_to_le16(4);

		// Enter the sending queue
		pRegQueue->Next = NULL;
		pRegQueue->pUsbReq = dr;
		pRegQueue->pUrb = pUrb;

		OS_SPIN_LOCK_ACQUIRED(&pWb35Reg->EP0VM_spin_lock );
		if (pWb35Reg->pRegFirst == NULL)
			pWb35Reg->pRegFirst = pRegQueue;
		else
			pWb35Reg->pRegLast->Next = pRegQueue;
		pWb35Reg->pRegLast = pRegQueue;

		OS_SPIN_LOCK_RELEASED( &pWb35Reg->EP0VM_spin_lock );

		// Start EP0VM
		Wb35Reg_EP0VM_start(pHwData);

		return TRUE;
	} else {
		if (pUrb)
			usb_free_urb(pUrb);
		kfree(pRegQueue);
		return FALSE;
	}
}

//This command will be executed with a user defined value. When it completes,
//this value is useful. For example, hal_set_current_channel will use it.
// TRUE  : read command process successfully
// FALSE : register not support
unsigned char
Wb35Reg_WriteWithCallbackValue( phw_data_t pHwData, u16 RegisterNo, u32 RegisterValue,
				PCHAR pValue, s8 Len)
{
	PWB35REG	pWb35Reg = &pHwData->Wb35Reg;
	struct usb_ctrlrequest *dr;
	PURB		pUrb = NULL;
	PREG_QUEUE	pRegQueue = NULL;
	u16		UrbSize;

	// Module shutdown
	if (pHwData->SurpriseRemove)
		return FALSE;

	// update the register by send urb request------------------------------------
	UrbSize = sizeof(REG_QUEUE) + sizeof(struct usb_ctrlrequest);
	OS_MEMORY_ALLOC((void* *) &pRegQueue, UrbSize );
	pUrb = wb_usb_alloc_urb(0);
	if (pUrb && pRegQueue) {
		pRegQueue->DIRECT = 1;// burst write register
		pRegQueue->INDEX = RegisterNo;
		pRegQueue->VALUE = cpu_to_le32(RegisterValue);
		//NOTE : Users must guarantee the size of value will not exceed the buffer size.
		memcpy(pRegQueue->RESERVED, pValue, Len);
		pRegQueue->RESERVED_VALID = TRUE;
		dr = (struct usb_ctrlrequest *)((PUCHAR)pRegQueue + sizeof(REG_QUEUE));
		dr->bRequestType = USB_TYPE_VENDOR|USB_DIR_OUT |USB_RECIP_DEVICE;
		dr->bRequest = 0x03; // USB or vendor-defined request code, burst mode
		dr->wValue = cpu_to_le16(0x0);
		dr->wIndex = cpu_to_le16(RegisterNo);
		dr->wLength = cpu_to_le16(4);

		// Enter the sending queue
		pRegQueue->Next = NULL;
		pRegQueue->pUsbReq = dr;
		pRegQueue->pUrb = pUrb;
		OS_SPIN_LOCK_ACQUIRED (&pWb35Reg->EP0VM_spin_lock );
		if( pWb35Reg->pRegFirst == NULL )
			pWb35Reg->pRegFirst = pRegQueue;
		else
			pWb35Reg->pRegLast->Next = pRegQueue;
		pWb35Reg->pRegLast = pRegQueue;

		OS_SPIN_LOCK_RELEASED ( &pWb35Reg->EP0VM_spin_lock );

		// Start EP0VM
		Wb35Reg_EP0VM_start(pHwData);
		return TRUE;
	} else {
		if (pUrb)
			usb_free_urb(pUrb);
		kfree(pRegQueue);
		return FALSE;
	}
}

// TRUE  : read command process successfully
// FALSE : register not support
// pRegisterValue : It must be a resident buffer due to asynchronous read register.
unsigned char
Wb35Reg_ReadSync(  phw_data_t pHwData,  u16 RegisterNo,   PULONG pRegisterValue )
{
	PWB35REG pWb35Reg = &pHwData->Wb35Reg;
	PULONG	pltmp = pRegisterValue;
	int ret = -1;

	// Module shutdown
	if (pHwData->SurpriseRemove)
		return FALSE;

	// Read the register by send usb message------------------------------------

	pWb35Reg->SyncIoPause = 1;

	// 20060717.5 Wait until EP0VM stop
	while (pWb35Reg->EP0vm_state != VM_STOP)
		OS_SLEEP(10000);

	pWb35Reg->EP0vm_state = VM_RUNNING;
	ret = usb_control_msg( pHwData->WbUsb.udev,
			       usb_rcvctrlpipe(pHwData->WbUsb.udev, 0),
			       0x01, USB_TYPE_VENDOR|USB_RECIP_DEVICE|USB_DIR_IN,
			       0x0, RegisterNo, pltmp, 4, HZ*100 );

	*pRegisterValue = cpu_to_le32(*pltmp);

	pWb35Reg->EP0vm_state = VM_STOP;

	Wb35Reg_Update( pHwData, RegisterNo, *pRegisterValue );
	pWb35Reg->SyncIoPause = 0;

	Wb35Reg_EP0VM_start( pHwData );

	if (ret < 0) {
		#ifdef _PE_REG_DUMP_
		WBDEBUG(("EP0 Read register usb message sending error\n"));
		#endif

		pHwData->SurpriseRemove = 1; // 20060704.2
		return FALSE;
	}

	return TRUE;
}

// TRUE  : read command process successfully
// FALSE : register not support
// pRegisterValue : It must be a resident buffer due to asynchronous read register.
unsigned char
Wb35Reg_Read(phw_data_t pHwData, u16 RegisterNo,  PULONG pRegisterValue )
{
	PWB35REG	pWb35Reg = &pHwData->Wb35Reg;
	struct usb_ctrlrequest * dr;
	PURB		pUrb;
	PREG_QUEUE	pRegQueue;
	u16		UrbSize;

	// Module shutdown
	if (pHwData->SurpriseRemove)
		return FALSE;

	// update the variable by send Urb to read register ------------------------------------
	UrbSize = sizeof(REG_QUEUE) + sizeof(struct usb_ctrlrequest);
	OS_MEMORY_ALLOC( (void* *)&pRegQueue, UrbSize );
	pUrb = wb_usb_alloc_urb(0);
	if( pUrb && pRegQueue )
	{
		pRegQueue->DIRECT = 0;// read register
		pRegQueue->INDEX = RegisterNo;
		pRegQueue->pBuffer = pRegisterValue;
		dr = (struct usb_ctrlrequest *)((PUCHAR)pRegQueue + sizeof(REG_QUEUE));
		dr->bRequestType = USB_TYPE_VENDOR|USB_RECIP_DEVICE|USB_DIR_IN;
		dr->bRequest = 0x01; // USB or vendor-defined request code, burst mode
		dr->wValue = cpu_to_le16(0x0);
		dr->wIndex = cpu_to_le16 (RegisterNo);
		dr->wLength = cpu_to_le16 (4);

		// Enter the sending queue
		pRegQueue->Next = NULL;
		pRegQueue->pUsbReq = dr;
		pRegQueue->pUrb = pUrb;
		OS_SPIN_LOCK_ACQUIRED ( &pWb35Reg->EP0VM_spin_lock );
		if( pWb35Reg->pRegFirst == NULL )
			pWb35Reg->pRegFirst = pRegQueue;
		else
			pWb35Reg->pRegLast->Next = pRegQueue;
		pWb35Reg->pRegLast = pRegQueue;

		OS_SPIN_LOCK_RELEASED( &pWb35Reg->EP0VM_spin_lock );

		// Start EP0VM
		Wb35Reg_EP0VM_start( pHwData );

		return TRUE;
	} else {
		if (pUrb)
			usb_free_urb( pUrb );
		kfree(pRegQueue);
		return FALSE;
	}
}


void
Wb35Reg_EP0VM_start(  phw_data_t pHwData )
{
	PWB35REG pWb35Reg = &pHwData->Wb35Reg;

	if (OS_ATOMIC_INC( pHwData->Adapter, &pWb35Reg->RegFireCount) == 1) {
		pWb35Reg->EP0vm_state = VM_RUNNING;
		Wb35Reg_EP0VM(pHwData);
	} else
		OS_ATOMIC_DEC( pHwData->Adapter, &pWb35Reg->RegFireCount );
}

void
Wb35Reg_EP0VM(phw_data_t pHwData )
{
	PWB35REG	pWb35Reg = &pHwData->Wb35Reg;
	PURB		pUrb;
	struct usb_ctrlrequest *dr;
	PULONG		pBuffer;
	int			ret = -1;
	PREG_QUEUE	pRegQueue;


	if (pWb35Reg->SyncIoPause)
		goto cleanup;

	if (pHwData->SurpriseRemove)
		goto cleanup;

	// Get the register data and send to USB through Irp
	OS_SPIN_LOCK_ACQUIRED( &pWb35Reg->EP0VM_spin_lock );
	pRegQueue = pWb35Reg->pRegFirst;
	OS_SPIN_LOCK_RELEASED( &pWb35Reg->EP0VM_spin_lock );

	if (!pRegQueue)
		goto cleanup;

	// Get an Urb, send it
	pUrb = (PURB)pRegQueue->pUrb;

	dr = pRegQueue->pUsbReq;
	pUrb = pRegQueue->pUrb;
	pBuffer = pRegQueue->pBuffer;
	if (pRegQueue->DIRECT == 1) // output
		pBuffer = &pRegQueue->VALUE;

	usb_fill_control_urb( pUrb, pHwData->WbUsb.udev,
			      REG_DIRECTION(pHwData->WbUsb.udev,pRegQueue),
			      (PUCHAR)dr,pBuffer,cpu_to_le16(dr->wLength),
			      Wb35Reg_EP0VM_complete, (void*)pHwData);

	pWb35Reg->EP0vm_state = VM_RUNNING;

	ret = wb_usb_submit_urb( pUrb );

	if (ret < 0) {
#ifdef _PE_REG_DUMP_
		WBDEBUG(("EP0 Irp sending error\n"));
#endif
		goto cleanup;
	}

	return;

 cleanup:
	pWb35Reg->EP0vm_state = VM_STOP;
	OS_ATOMIC_DEC( pHwData->Adapter, &pWb35Reg->RegFireCount );
}


void
Wb35Reg_EP0VM_complete(PURB pUrb)
{
	phw_data_t  pHwData = (phw_data_t)pUrb->context;
	PWB35REG	pWb35Reg = &pHwData->Wb35Reg;
	PREG_QUEUE	pRegQueue;


	// Variable setting
	pWb35Reg->EP0vm_state = VM_COMPLETED;
	pWb35Reg->EP0VM_status = pUrb->status;

	if (pHwData->SurpriseRemove) { // Let WbWlanHalt to handle surprise remove
		pWb35Reg->EP0vm_state = VM_STOP;
		OS_ATOMIC_DEC( pHwData->Adapter, &pWb35Reg->RegFireCount );
	} else {
		// Complete to send, remove the URB from the first
		OS_SPIN_LOCK_ACQUIRED( &pWb35Reg->EP0VM_spin_lock );
		pRegQueue = pWb35Reg->pRegFirst;
		if (pRegQueue == pWb35Reg->pRegLast)
			pWb35Reg->pRegLast = NULL;
		pWb35Reg->pRegFirst = pWb35Reg->pRegFirst->Next;
		OS_SPIN_LOCK_RELEASED( &pWb35Reg->EP0VM_spin_lock );

		if (pWb35Reg->EP0VM_status) {
#ifdef _PE_REG_DUMP_
			WBDEBUG(("EP0 IoCompleteRoutine return error\n"));
			DebugUsbdStatusInformation( pWb35Reg->EP0VM_status );
#endif
			pWb35Reg->EP0vm_state = VM_STOP;
			pHwData->SurpriseRemove = 1;
		} else {
			// Success. Update the result

			// Start the next send
			Wb35Reg_EP0VM(pHwData);
		}

   		kfree(pRegQueue);
	}

	usb_free_urb(pUrb);
}


void
Wb35Reg_destroy(phw_data_t pHwData)
{
	PWB35REG	pWb35Reg = &pHwData->Wb35Reg;
	PURB		pUrb;
	PREG_QUEUE	pRegQueue;


	Uxx_power_off_procedure(pHwData);

	// Wait for Reg operation completed
	do {
		OS_SLEEP(10000); // Delay for waiting function enter 940623.1.a
	} while (pWb35Reg->EP0vm_state != VM_STOP);
	OS_SLEEP(10000);  // Delay for waiting function enter 940623.1.b

	// Release all the data in RegQueue
	OS_SPIN_LOCK_ACQUIRED( &pWb35Reg->EP0VM_spin_lock );
	pRegQueue = pWb35Reg->pRegFirst;
	while (pRegQueue) {
		if (pRegQueue == pWb35Reg->pRegLast)
			pWb35Reg->pRegLast = NULL;
		pWb35Reg->pRegFirst = pWb35Reg->pRegFirst->Next;

		pUrb = pRegQueue->pUrb;
		OS_SPIN_LOCK_RELEASED( &pWb35Reg->EP0VM_spin_lock );
		if (pUrb) {
			usb_free_urb(pUrb);
			kfree(pRegQueue);
		} else {
			#ifdef _PE_REG_DUMP_
			WBDEBUG(("EP0 queue release error\n"));
			#endif
		}
		OS_SPIN_LOCK_ACQUIRED( &pWb35Reg->EP0VM_spin_lock );

		pRegQueue = pWb35Reg->pRegFirst;
	}
	OS_SPIN_LOCK_RELEASED( &pWb35Reg->EP0VM_spin_lock );

	// Free resource
	OS_SPIN_LOCK_FREE(  &pWb35Reg->EP0VM_spin_lock );
}

//====================================================================================
// The function can be run in passive-level only.
//====================================================================================
unsigned char Wb35Reg_initial(phw_data_t pHwData)
{
	PWB35REG pWb35Reg=&pHwData->Wb35Reg;
	u32 ltmp;
	u32 SoftwareSet, VCO_trim, TxVga, Region_ScanInterval;

	// Spin lock is acquired for read and write IRP command
	OS_SPIN_LOCK_ALLOCATE( &pWb35Reg->EP0VM_spin_lock );

	// Getting RF module type from EEPROM ------------------------------------
	Wb35Reg_WriteSync( pHwData, 0x03b4, 0x080d0000 ); // Start EEPROM access + Read + address(0x0d)
	Wb35Reg_ReadSync( pHwData, 0x03b4, &ltmp );

	//Update RF module type and determine the PHY type by inf or EEPROM
	pWb35Reg->EEPROMPhyType = (u8)( ltmp & 0xff );
	// 0 V MAX2825, 1 V MAX2827, 2 V MAX2828, 3 V MAX2829
	// 16V AL2230, 17 - AL7230, 18 - AL2230S
	// 32 Reserved
	// 33 - W89RF242(TxVGA 0~19), 34 - W89RF242(TxVGA 0~34)
	if (pWb35Reg->EEPROMPhyType != RF_DECIDE_BY_INF) {
		if( (pWb35Reg->EEPROMPhyType == RF_MAXIM_2825)	||
			(pWb35Reg->EEPROMPhyType == RF_MAXIM_2827)	||
			(pWb35Reg->EEPROMPhyType == RF_MAXIM_2828)	||
			(pWb35Reg->EEPROMPhyType == RF_MAXIM_2829)	||
			(pWb35Reg->EEPROMPhyType == RF_MAXIM_V1)	||
			(pWb35Reg->EEPROMPhyType == RF_AIROHA_2230)	||
			(pWb35Reg->EEPROMPhyType == RF_AIROHA_2230S)    ||
			(pWb35Reg->EEPROMPhyType == RF_AIROHA_7230)	||
			(pWb35Reg->EEPROMPhyType == RF_WB_242)		||
			(pWb35Reg->EEPROMPhyType == RF_WB_242_1))
			pHwData->phy_type = pWb35Reg->EEPROMPhyType;
	}

	// Power On procedure running. The relative parameter will be set according to phy_type
	Uxx_power_on_procedure( pHwData );

	// Reading MAC address
	Uxx_ReadEthernetAddress( pHwData );

	// Read VCO trim for RF parameter
	Wb35Reg_WriteSync( pHwData, 0x03b4, 0x08200000 );
	Wb35Reg_ReadSync( pHwData, 0x03b4, &VCO_trim );

	// Read Antenna On/Off of software flag
	Wb35Reg_WriteSync( pHwData, 0x03b4, 0x08210000 );
	Wb35Reg_ReadSync( pHwData, 0x03b4, &SoftwareSet );

	// Read TXVGA
	Wb35Reg_WriteSync( pHwData, 0x03b4, 0x08100000 );
	Wb35Reg_ReadSync( pHwData, 0x03b4, &TxVga );

	// Get Scan interval setting from EEPROM offset 0x1c
	Wb35Reg_WriteSync( pHwData, 0x03b4, 0x081d0000 );
	Wb35Reg_ReadSync( pHwData, 0x03b4, &Region_ScanInterval );

	// Update Ethernet address
	memcpy( pHwData->CurrentMacAddress, pHwData->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS );

	// Update software variable
	pHwData->SoftwareSet = (u16)(SoftwareSet & 0xffff);
	TxVga &= 0x000000ff;
	pHwData->PowerIndexFromEEPROM = (u8)TxVga;
	pHwData->VCO_trim = (u8)VCO_trim & 0xff;
	if (pHwData->VCO_trim == 0xff)
		pHwData->VCO_trim = 0x28;

	pWb35Reg->EEPROMRegion = (u8)(Region_ScanInterval>>8); // 20060720
	if( pWb35Reg->EEPROMRegion<1 || pWb35Reg->EEPROMRegion>6 )
		pWb35Reg->EEPROMRegion = REGION_AUTO;

	//For Get Tx VGA from EEPROM 20060315.5 move here
	GetTxVgaFromEEPROM( pHwData );

	// Set Scan Interval
	pHwData->Scan_Interval = (u8)(Region_ScanInterval & 0xff) * 10;
	if ((pHwData->Scan_Interval == 2550) || (pHwData->Scan_Interval < 10)) // Is default setting 0xff * 10
		pHwData->Scan_Interval = SCAN_MAX_CHNL_TIME;

	// Initial register
	RFSynthesizer_initial(pHwData);

	BBProcessor_initial(pHwData); // Async write, must wait until complete

	Wb35Reg_phy_calibration(pHwData);

	Mxx_initial(pHwData);
	Dxx_initial(pHwData);

	if (pHwData->SurpriseRemove)
		return FALSE;
	else
		return TRUE; // Initial fail
}

//===================================================================================
//  CardComputeCrc --
//
//  Description:
//    Runs the AUTODIN II CRC algorithm on buffer Buffer of length, Length.
//
//  Arguments:
//    Buffer - the input buffer
//    Length - the length of Buffer
//
//  Return Value:
//    The 32-bit CRC value.
//
//  Note:
//    This is adapted from the comments in the assembly language
//    version in _GENREQ.ASM of the DWB NE1000/2000 driver.
//==================================================================================
u32
CardComputeCrc(PUCHAR Buffer, u32 Length)
{
    u32 Crc, Carry;
    u32  i, j;
    u8 CurByte;

    Crc = 0xffffffff;

    for (i = 0; i < Length; i++) {

        CurByte = Buffer[i];

        for (j = 0; j < 8; j++) {

            Carry     = ((Crc & 0x80000000) ? 1 : 0) ^ (CurByte & 0x01);
            Crc     <<= 1;
            CurByte >>= 1;

            if (Carry) {
                Crc =(Crc ^ 0x04c11db6) | Carry;
            }
        }
    }

    return Crc;
}


//==================================================================
// BitReverse --
//   Reverse the bits in the input argument, dwData, which is
//   regarded as a string of bits with the length, DataLength.
//
// Arguments:
//   dwData     :
//   DataLength :
//
// Return:
//   The converted value.
//==================================================================
u32 BitReverse( u32 dwData, u32 DataLength)
{
	u32   HalfLength, i, j;
	u32   BitA, BitB;

	if ( DataLength <= 0)       return 0;   // No conversion is done.
	dwData = dwData & (0xffffffff >> (32 - DataLength));

	HalfLength = DataLength / 2;
	for ( i = 0, j = DataLength-1 ; i < HalfLength; i++, j--)
	{
		BitA = GetBit( dwData, i);
		BitB = GetBit( dwData, j);
		if (BitA && !BitB) {
			dwData = ClearBit( dwData, i);
			dwData = SetBit( dwData, j);
		} else if (!BitA && BitB) {
			dwData = SetBit( dwData, i);
			dwData = ClearBit( dwData, j);
		} else
		{
			// Do nothing since these two bits are of the save values.
		}
	}

	return dwData;
}

void Wb35Reg_phy_calibration(  phw_data_t pHwData )
{
	u32 BB3c, BB54;

	if ((pHwData->phy_type == RF_WB_242) ||
		(pHwData->phy_type == RF_WB_242_1)) {
		phy_calibration_winbond ( pHwData, 2412 ); // Sync operation
		Wb35Reg_ReadSync( pHwData, 0x103c, &BB3c );
		Wb35Reg_ReadSync( pHwData, 0x1054, &BB54 );

		pHwData->BB3c_cal = BB3c;
		pHwData->BB54_cal = BB54;

		RFSynthesizer_initial(pHwData);
		BBProcessor_initial(pHwData); // Async operation

		Wb35Reg_WriteSync( pHwData, 0x103c, BB3c );
		Wb35Reg_WriteSync( pHwData, 0x1054, BB54 );
	}
}


