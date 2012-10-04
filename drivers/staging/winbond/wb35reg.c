#include "wb35reg_f.h"

#include <linux/usb.h>
#include <linux/slab.h>

extern void phy_calibration_winbond(struct hw_data *phw_data, u32 frequency);

/*
 * true  : read command process successfully
 * false : register not support
 * RegisterNo : start base
 * pRegisterData : data point
 * NumberOfData : number of register data
 * Flag : AUTO_INCREMENT - RegisterNo will auto increment 4
 *	  NO_INCREMENT - Function will write data into the same register
 */
unsigned char Wb35Reg_BurstWrite(struct hw_data *pHwData, u16 RegisterNo, u32 *pRegisterData, u8 NumberOfData, u8 Flag)
{
	struct wb35_reg		*reg = &pHwData->reg;
	struct urb		*urb = NULL;
	struct wb35_reg_queue	*reg_queue = NULL;
	u16			UrbSize;
	struct usb_ctrlrequest	*dr;
	u16			i, DataSize = NumberOfData * 4;

	/* Module shutdown */
	if (pHwData->SurpriseRemove)
		return false;

	/* Trying to use burst write function if use new hardware */
	UrbSize = sizeof(struct wb35_reg_queue) + DataSize + sizeof(struct usb_ctrlrequest);
	reg_queue = kzalloc(UrbSize, GFP_ATOMIC);
	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (urb && reg_queue) {
		reg_queue->DIRECT = 2; /* burst write register */
		reg_queue->INDEX = RegisterNo;
		reg_queue->pBuffer = (u32 *)((u8 *)reg_queue + sizeof(struct wb35_reg_queue));
		memcpy(reg_queue->pBuffer, pRegisterData, DataSize);
		/* the function for reversing register data from little endian to big endian */
		for (i = 0; i < NumberOfData ; i++)
			reg_queue->pBuffer[i] = cpu_to_le32(reg_queue->pBuffer[i]);

		dr = (struct usb_ctrlrequest *)((u8 *)reg_queue + sizeof(struct wb35_reg_queue) + DataSize);
		dr->bRequestType = USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE;
		dr->bRequest = 0x04; /* USB or vendor-defined request code, burst mode */
		dr->wValue = cpu_to_le16(Flag); /* 0: Register number auto-increment, 1: No auto increment */
		dr->wIndex = cpu_to_le16(RegisterNo);
		dr->wLength = cpu_to_le16(DataSize);
		reg_queue->Next = NULL;
		reg_queue->pUsbReq = dr;
		reg_queue->urb = urb;

		spin_lock_irq(&reg->EP0VM_spin_lock);
		if (reg->reg_first == NULL)
			reg->reg_first = reg_queue;
		else
			reg->reg_last->Next = reg_queue;
		reg->reg_last = reg_queue;

		spin_unlock_irq(&reg->EP0VM_spin_lock);

		/* Start EP0VM */
		Wb35Reg_EP0VM_start(pHwData);

		return true;
	} else {
		if (urb)
			usb_free_urb(urb);
		kfree(reg_queue);
		return false;
	}
   return false;
}

void Wb35Reg_Update(struct hw_data *pHwData,  u16 RegisterNo,  u32 RegisterValue)
{
	struct wb35_reg *reg = &pHwData->reg;
	switch (RegisterNo) {
	case 0x3b0: reg->U1B0 = RegisterValue; break;
	case 0x3bc: reg->U1BC_LEDConfigure = RegisterValue; break;
	case 0x400: reg->D00_DmaControl = RegisterValue; break;
	case 0x800: reg->M00_MacControl = RegisterValue; break;
	case 0x804: reg->M04_MulticastAddress1 = RegisterValue; break;
	case 0x808: reg->M08_MulticastAddress2 = RegisterValue; break;
	case 0x824: reg->M24_MacControl = RegisterValue; break;
	case 0x828: reg->M28_MacControl = RegisterValue; break;
	case 0x82c: reg->M2C_MacControl = RegisterValue; break;
	case 0x838: reg->M38_MacControl = RegisterValue; break;
	case 0x840: reg->M40_MacControl = RegisterValue; break;
	case 0x844: reg->M44_MacControl = RegisterValue; break;
	case 0x848: reg->M48_MacControl = RegisterValue; break;
	case 0x84c: reg->M4C_MacStatus = RegisterValue; break;
	case 0x860: reg->M60_MacControl = RegisterValue; break;
	case 0x868: reg->M68_MacControl = RegisterValue; break;
	case 0x870: reg->M70_MacControl = RegisterValue; break;
	case 0x874: reg->M74_MacControl = RegisterValue; break;
	case 0x878: reg->M78_ERPInformation = RegisterValue; break;
	case 0x87C: reg->M7C_MacControl = RegisterValue; break;
	case 0x880: reg->M80_MacControl = RegisterValue; break;
	case 0x884: reg->M84_MacControl = RegisterValue; break;
	case 0x888: reg->M88_MacControl = RegisterValue; break;
	case 0x898: reg->M98_MacControl = RegisterValue; break;
	case 0x100c: reg->BB0C = RegisterValue; break;
	case 0x102c: reg->BB2C = RegisterValue; break;
	case 0x1030: reg->BB30 = RegisterValue; break;
	case 0x103c: reg->BB3C = RegisterValue; break;
	case 0x1048: reg->BB48 = RegisterValue; break;
	case 0x104c: reg->BB4C = RegisterValue; break;
	case 0x1050: reg->BB50 = RegisterValue; break;
	case 0x1054: reg->BB54 = RegisterValue; break;
	case 0x1058: reg->BB58 = RegisterValue; break;
	case 0x105c: reg->BB5C = RegisterValue; break;
	case 0x1060: reg->BB60 = RegisterValue; break;
	}
}

/*
 * true  : read command process successfully
 * false : register not support
 */
unsigned char Wb35Reg_WriteSync(struct hw_data *pHwData, u16 RegisterNo, u32 RegisterValue)
{
	struct wb35_reg *reg = &pHwData->reg;
	int ret = -1;

	/* Module shutdown */
	if (pHwData->SurpriseRemove)
		return false;

	RegisterValue = cpu_to_le32(RegisterValue);

	/* update the register by send usb message */
	reg->SyncIoPause = 1;

	/* Wait until EP0VM stop */
	while (reg->EP0vm_state != VM_STOP)
		msleep(10);

	/* Sync IoCallDriver */
	reg->EP0vm_state = VM_RUNNING;
	ret = usb_control_msg(pHwData->udev,
			       usb_sndctrlpipe(pHwData->udev, 0),
			       0x03, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_OUT,
			       0x0, RegisterNo, &RegisterValue, 4, HZ * 100);
	reg->EP0vm_state = VM_STOP;
	reg->SyncIoPause = 0;

	Wb35Reg_EP0VM_start(pHwData);

	if (ret < 0) {
		pr_debug("EP0 Write register usb message sending error\n");
		pHwData->SurpriseRemove = 1;
		return false;
	}
	return true;
}

/*
 * true  : read command process successfully
 * false : register not support
 */
unsigned char Wb35Reg_Write(struct hw_data *pHwData, u16 RegisterNo, u32 RegisterValue)
{
	struct wb35_reg		*reg = &pHwData->reg;
	struct usb_ctrlrequest	*dr;
	struct urb		*urb = NULL;
	struct wb35_reg_queue	*reg_queue = NULL;
	u16			UrbSize;

	/* Module shutdown */
	if (pHwData->SurpriseRemove)
		return false;

	/* update the register by send urb request */
	UrbSize = sizeof(struct wb35_reg_queue) + sizeof(struct usb_ctrlrequest);
	reg_queue = kzalloc(UrbSize, GFP_ATOMIC);
	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (urb && reg_queue) {
		reg_queue->DIRECT = 1; /* burst write register */
		reg_queue->INDEX = RegisterNo;
		reg_queue->VALUE = cpu_to_le32(RegisterValue);
		reg_queue->RESERVED_VALID = false;
		dr = (struct usb_ctrlrequest *)((u8 *)reg_queue + sizeof(struct wb35_reg_queue));
		dr->bRequestType = USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE;
		dr->bRequest = 0x03; /* USB or vendor-defined request code, burst mode */
		dr->wValue = cpu_to_le16(0x0);
		dr->wIndex = cpu_to_le16(RegisterNo);
		dr->wLength = cpu_to_le16(4);

		/* Enter the sending queue */
		reg_queue->Next = NULL;
		reg_queue->pUsbReq = dr;
		reg_queue->urb = urb;

		spin_lock_irq(&reg->EP0VM_spin_lock);
		if (reg->reg_first == NULL)
			reg->reg_first = reg_queue;
		else
			reg->reg_last->Next = reg_queue;
		reg->reg_last = reg_queue;

		spin_unlock_irq(&reg->EP0VM_spin_lock);

		/* Start EP0VM */
		Wb35Reg_EP0VM_start(pHwData);

		return true;
	} else {
		if (urb)
			usb_free_urb(urb);
		kfree(reg_queue);
		return false;
	}
}

/*
 * This command will be executed with a user defined value. When it completes,
 * this value is useful. For example, hal_set_current_channel will use it.
 * true  : read command process successfully
 * false : register not supported
 */
unsigned char Wb35Reg_WriteWithCallbackValue(struct hw_data *pHwData,
						u16 RegisterNo,
						u32 RegisterValue,
						s8 *pValue,
						s8 Len)
{
	struct wb35_reg		*reg = &pHwData->reg;
	struct usb_ctrlrequest	*dr;
	struct urb		*urb = NULL;
	struct wb35_reg_queue	*reg_queue = NULL;
	u16			UrbSize;

	/* Module shutdown */
	if (pHwData->SurpriseRemove)
		return false;

	/* update the register by send urb request */
	UrbSize = sizeof(struct wb35_reg_queue) + sizeof(struct usb_ctrlrequest);
	reg_queue = kzalloc(UrbSize, GFP_ATOMIC);
	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (urb && reg_queue) {
		reg_queue->DIRECT = 1; /* burst write register */
		reg_queue->INDEX = RegisterNo;
		reg_queue->VALUE = cpu_to_le32(RegisterValue);
		/* NOTE : Users must guarantee the size of value will not exceed the buffer size. */
		memcpy(reg_queue->RESERVED, pValue, Len);
		reg_queue->RESERVED_VALID = true;
		dr = (struct usb_ctrlrequest *)((u8 *)reg_queue + sizeof(struct wb35_reg_queue));
		dr->bRequestType = USB_TYPE_VENDOR | USB_DIR_OUT | USB_RECIP_DEVICE;
		dr->bRequest = 0x03; /* USB or vendor-defined request code, burst mode */
		dr->wValue = cpu_to_le16(0x0);
		dr->wIndex = cpu_to_le16(RegisterNo);
		dr->wLength = cpu_to_le16(4);

		/* Enter the sending queue */
		reg_queue->Next = NULL;
		reg_queue->pUsbReq = dr;
		reg_queue->urb = urb;
		spin_lock_irq(&reg->EP0VM_spin_lock);
		if (reg->reg_first == NULL)
			reg->reg_first = reg_queue;
		else
			reg->reg_last->Next = reg_queue;
		reg->reg_last = reg_queue;

		spin_unlock_irq(&reg->EP0VM_spin_lock);

		/* Start EP0VM */
		Wb35Reg_EP0VM_start(pHwData);
		return true;
	} else {
		if (urb)
			usb_free_urb(urb);
		kfree(reg_queue);
		return false;
	}
}

/*
 * true  : read command process successfully
 * false : register not support
 * pRegisterValue : It must be a resident buffer due to
 *		    asynchronous read register.
 */
unsigned char Wb35Reg_ReadSync(struct hw_data *pHwData, u16 RegisterNo, u32 *pRegisterValue)
{
	struct wb35_reg *reg = &pHwData->reg;
	u32		*pltmp = pRegisterValue;
	int		ret = -1;

	/* Module shutdown */
	if (pHwData->SurpriseRemove)
		return false;

	/* Read the register by send usb message */
	reg->SyncIoPause = 1;

	/* Wait until EP0VM stop */
	while (reg->EP0vm_state != VM_STOP)
		msleep(10);

	reg->EP0vm_state = VM_RUNNING;
	ret = usb_control_msg(pHwData->udev,
			       usb_rcvctrlpipe(pHwData->udev, 0),
			       0x01, USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN,
			       0x0, RegisterNo, pltmp, 4, HZ * 100);

	*pRegisterValue = cpu_to_le32(*pltmp);

	reg->EP0vm_state = VM_STOP;

	Wb35Reg_Update(pHwData, RegisterNo, *pRegisterValue);
	reg->SyncIoPause = 0;

	Wb35Reg_EP0VM_start(pHwData);

	if (ret < 0) {
		pr_debug("EP0 Read register usb message sending error\n");
		pHwData->SurpriseRemove = 1;
		return false;
	}
	return true;
}

/*
 * true  : read command process successfully
 * false : register not support
 * pRegisterValue : It must be a resident buffer due to
 *		    asynchronous read register.
 */
unsigned char Wb35Reg_Read(struct hw_data *pHwData, u16 RegisterNo, u32 *pRegisterValue)
{
	struct wb35_reg		*reg = &pHwData->reg;
	struct usb_ctrlrequest	*dr;
	struct urb		*urb;
	struct wb35_reg_queue	*reg_queue;
	u16			UrbSize;

	/* Module shutdown */
	if (pHwData->SurpriseRemove)
		return false;

	/* update the variable by send Urb to read register */
	UrbSize = sizeof(struct wb35_reg_queue) + sizeof(struct usb_ctrlrequest);
	reg_queue = kzalloc(UrbSize, GFP_ATOMIC);
	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (urb && reg_queue) {
		reg_queue->DIRECT = 0; /* read register */
		reg_queue->INDEX = RegisterNo;
		reg_queue->pBuffer = pRegisterValue;
		dr = (struct usb_ctrlrequest *)((u8 *)reg_queue + sizeof(struct wb35_reg_queue));
		dr->bRequestType = USB_TYPE_VENDOR | USB_RECIP_DEVICE | USB_DIR_IN;
		dr->bRequest = 0x01; /* USB or vendor-defined request code, burst mode */
		dr->wValue = cpu_to_le16(0x0);
		dr->wIndex = cpu_to_le16(RegisterNo);
		dr->wLength = cpu_to_le16(4);

		/* Enter the sending queue */
		reg_queue->Next = NULL;
		reg_queue->pUsbReq = dr;
		reg_queue->urb = urb;
		spin_lock_irq(&reg->EP0VM_spin_lock);
		if (reg->reg_first == NULL)
			reg->reg_first = reg_queue;
		else
			reg->reg_last->Next = reg_queue;
		reg->reg_last = reg_queue;

		spin_unlock_irq(&reg->EP0VM_spin_lock);

		/* Start EP0VM */
		Wb35Reg_EP0VM_start(pHwData);

		return true;
	} else {
		if (urb)
			usb_free_urb(urb);
		kfree(reg_queue);
		return false;
	}
}


void Wb35Reg_EP0VM_start(struct hw_data *pHwData)
{
	struct wb35_reg *reg = &pHwData->reg;

	if (atomic_inc_return(&reg->RegFireCount) == 1) {
		reg->EP0vm_state = VM_RUNNING;
		Wb35Reg_EP0VM(pHwData);
	} else
		atomic_dec(&reg->RegFireCount);
}

void Wb35Reg_EP0VM(struct hw_data *pHwData)
{
	struct wb35_reg		*reg = &pHwData->reg;
	struct urb		*urb;
	struct usb_ctrlrequest	*dr;
	u32			*pBuffer;
	int			ret = -1;
	struct wb35_reg_queue	*reg_queue;


	if (reg->SyncIoPause)
		goto cleanup;

	if (pHwData->SurpriseRemove)
		goto cleanup;

	/* Get the register data and send to USB through Irp */
	spin_lock_irq(&reg->EP0VM_spin_lock);
	reg_queue = reg->reg_first;
	spin_unlock_irq(&reg->EP0VM_spin_lock);

	if (!reg_queue)
		goto cleanup;

	/* Get an Urb, send it */
	urb = (struct urb *)reg_queue->urb;

	dr = reg_queue->pUsbReq;
	urb = reg_queue->urb;
	pBuffer = reg_queue->pBuffer;
	if (reg_queue->DIRECT == 1) /* output */
		pBuffer = &reg_queue->VALUE;

	usb_fill_control_urb(urb, pHwData->udev,
			      REG_DIRECTION(pHwData->udev, reg_queue),
			      (u8 *)dr, pBuffer, cpu_to_le16(dr->wLength),
			      Wb35Reg_EP0VM_complete, (void *)pHwData);

	reg->EP0vm_state = VM_RUNNING;

	ret = usb_submit_urb(urb, GFP_ATOMIC);

	if (ret < 0) {
		pr_debug("EP0 Irp sending error\n");
		goto cleanup;
	}
	return;

 cleanup:
	reg->EP0vm_state = VM_STOP;
	atomic_dec(&reg->RegFireCount);
}


void Wb35Reg_EP0VM_complete(struct urb *urb)
{
	struct hw_data		*pHwData = (struct hw_data *)urb->context;
	struct wb35_reg		*reg = &pHwData->reg;
	struct wb35_reg_queue	*reg_queue;


	/* Variable setting */
	reg->EP0vm_state = VM_COMPLETED;
	reg->EP0VM_status = urb->status;

	if (pHwData->SurpriseRemove) { /* Let WbWlanHalt to handle surprise remove */
		reg->EP0vm_state = VM_STOP;
		atomic_dec(&reg->RegFireCount);
	} else {
		/* Complete to send, remove the URB from the first */
		spin_lock_irq(&reg->EP0VM_spin_lock);
		reg_queue = reg->reg_first;
		if (reg_queue == reg->reg_last)
			reg->reg_last = NULL;
		reg->reg_first = reg->reg_first->Next;
		spin_unlock_irq(&reg->EP0VM_spin_lock);

		if (reg->EP0VM_status) {
			pr_debug("EP0 IoCompleteRoutine return error\n");
			reg->EP0vm_state = VM_STOP;
			pHwData->SurpriseRemove = 1;
		} else {
			/* Success. Update the result */

			/* Start the next send */
			Wb35Reg_EP0VM(pHwData);
		}

		kfree(reg_queue);
	}

	usb_free_urb(urb);
}


void Wb35Reg_destroy(struct hw_data *pHwData)
{
	struct wb35_reg		*reg = &pHwData->reg;
	struct urb		*urb;
	struct wb35_reg_queue	*reg_queue;

	Uxx_power_off_procedure(pHwData);

	/* Wait for Reg operation completed */
	do {
		msleep(10); /* Delay for waiting function enter */
	} while (reg->EP0vm_state != VM_STOP);
	msleep(10);  /* Delay for waiting function enter */

	/* Release all the data in RegQueue */
	spin_lock_irq(&reg->EP0VM_spin_lock);
	reg_queue = reg->reg_first;
	while (reg_queue) {
		if (reg_queue == reg->reg_last)
			reg->reg_last = NULL;
		reg->reg_first = reg->reg_first->Next;

		urb = reg_queue->urb;
		spin_unlock_irq(&reg->EP0VM_spin_lock);
		if (urb) {
			usb_free_urb(urb);
			kfree(reg_queue);
		} else {
			pr_debug("EP0 queue release error\n");
		}
		spin_lock_irq(&reg->EP0VM_spin_lock);

		reg_queue = reg->reg_first;
	}
	spin_unlock_irq(&reg->EP0VM_spin_lock);
}

/*
 * =======================================================================
 * The function can be run in passive-level only.
 * =========================================================================
 */
unsigned char Wb35Reg_initial(struct hw_data *pHwData)
{
	struct wb35_reg *reg = &pHwData->reg;
	u32 ltmp;
	u32 SoftwareSet, VCO_trim, TxVga, Region_ScanInterval;

	/* Spin lock is acquired for read and write IRP command */
	spin_lock_init(&reg->EP0VM_spin_lock);

	/* Getting RF module type from EEPROM */
	Wb35Reg_WriteSync(pHwData, 0x03b4, 0x080d0000); /* Start EEPROM access + Read + address(0x0d) */
	Wb35Reg_ReadSync(pHwData, 0x03b4, &ltmp);

	/* Update RF module type and determine the PHY type by inf or EEPROM */
	reg->EEPROMPhyType = (u8)(ltmp & 0xff);
	/*
	 * 0 V MAX2825, 1 V MAX2827, 2 V MAX2828, 3 V MAX2829
	 * 16V AL2230, 17 - AL7230, 18 - AL2230S
	 * 32 Reserved
	 * 33 - W89RF242(TxVGA 0~19), 34 - W89RF242(TxVGA 0~34)
	 */
	if (reg->EEPROMPhyType != RF_DECIDE_BY_INF) {
		if ((reg->EEPROMPhyType == RF_MAXIM_2825)	||
			(reg->EEPROMPhyType == RF_MAXIM_2827)	||
			(reg->EEPROMPhyType == RF_MAXIM_2828)	||
			(reg->EEPROMPhyType == RF_MAXIM_2829)	||
			(reg->EEPROMPhyType == RF_MAXIM_V1)	||
			(reg->EEPROMPhyType == RF_AIROHA_2230)	||
			(reg->EEPROMPhyType == RF_AIROHA_2230S)	||
			(reg->EEPROMPhyType == RF_AIROHA_7230)	||
			(reg->EEPROMPhyType == RF_WB_242)	||
			(reg->EEPROMPhyType == RF_WB_242_1))
			pHwData->phy_type = reg->EEPROMPhyType;
	}

	/* Power On procedure running. The relative parameter will be set according to phy_type */
	Uxx_power_on_procedure(pHwData);

	/* Reading MAC address */
	Uxx_ReadEthernetAddress(pHwData);

	/* Read VCO trim for RF parameter */
	Wb35Reg_WriteSync(pHwData, 0x03b4, 0x08200000);
	Wb35Reg_ReadSync(pHwData, 0x03b4, &VCO_trim);

	/* Read Antenna On/Off of software flag */
	Wb35Reg_WriteSync(pHwData, 0x03b4, 0x08210000);
	Wb35Reg_ReadSync(pHwData, 0x03b4, &SoftwareSet);

	/* Read TXVGA */
	Wb35Reg_WriteSync(pHwData, 0x03b4, 0x08100000);
	Wb35Reg_ReadSync(pHwData, 0x03b4, &TxVga);

	/* Get Scan interval setting from EEPROM offset 0x1c */
	Wb35Reg_WriteSync(pHwData, 0x03b4, 0x081d0000);
	Wb35Reg_ReadSync(pHwData, 0x03b4, &Region_ScanInterval);

	/* Update Ethernet address */
	memcpy(pHwData->CurrentMacAddress, pHwData->PermanentMacAddress, ETH_ALEN);

	/* Update software variable */
	pHwData->SoftwareSet = (u16)(SoftwareSet & 0xffff);
	TxVga &= 0x000000ff;
	pHwData->PowerIndexFromEEPROM = (u8)TxVga;
	pHwData->VCO_trim = (u8)VCO_trim & 0xff;
	if (pHwData->VCO_trim == 0xff)
		pHwData->VCO_trim = 0x28;

	reg->EEPROMRegion = (u8)(Region_ScanInterval >> 8);
	if (reg->EEPROMRegion < 1 || reg->EEPROMRegion > 6)
		reg->EEPROMRegion = REGION_AUTO;

	/* For Get Tx VGA from EEPROM */
	GetTxVgaFromEEPROM(pHwData);

	/* Set Scan Interval */
	pHwData->Scan_Interval = (u8)(Region_ScanInterval & 0xff) * 10;
	if ((pHwData->Scan_Interval == 2550) || (pHwData->Scan_Interval < 10)) /* Is default setting 0xff * 10 */
		pHwData->Scan_Interval = SCAN_MAX_CHNL_TIME;

	/* Initial register */
	RFSynthesizer_initial(pHwData);

	BBProcessor_initial(pHwData); /* Async write, must wait until complete */

	Wb35Reg_phy_calibration(pHwData);

	Mxx_initial(pHwData);
	Dxx_initial(pHwData);

	if (pHwData->SurpriseRemove)
		return false;
	else
		return true; /* Initial fail */
}

/*
 * ================================================================
 *  CardComputeCrc --
 *
 *  Description:
 *    Runs the AUTODIN II CRC algorithm on the buffers Buffer length.
 *
 *  Arguments:
 *    Buffer - the input buffer
 *    Length - the length of Buffer
 *
 *  Return Value:
 *    The 32-bit CRC value.
 * ===================================================================
 */
u32 CardComputeCrc(u8 *Buffer, u32 Length)
{
	u32	Crc, Carry;
	u32	i, j;
	u8	CurByte;

	Crc = 0xffffffff;

	for (i = 0; i < Length; i++) {
		CurByte = Buffer[i];
		for (j = 0; j < 8; j++) {
			Carry = ((Crc & 0x80000000) ? 1 : 0) ^ (CurByte & 0x01);
			Crc <<= 1;
			CurByte >>= 1;
			if (Carry)
				Crc = (Crc ^ 0x04c11db6) | Carry;
		}
	}
	return Crc;
}


/*
 * ==================================================================
 * BitReverse --
 *   Reverse the bits in the input argument, dwData, which is
 *   regarded as a string of bits with the length, DataLength.
 *
 * Arguments:
 *   dwData     :
 *   DataLength :
 *
 * Return:
 *   The converted value.
 * ==================================================================
 */
u32 BitReverse(u32 dwData, u32 DataLength)
{
	u32	HalfLength, i, j;
	u32	BitA, BitB;

	if (DataLength <= 0)
		return 0;	/* No conversion is done. */
	dwData = dwData & (0xffffffff >> (32 - DataLength));

	HalfLength = DataLength / 2;
	for (i = 0, j = DataLength - 1; i < HalfLength; i++, j--) {
		BitA = GetBit(dwData, i);
		BitB = GetBit(dwData, j);
		if (BitA && !BitB) {
			dwData = ClearBit(dwData, i);
			dwData = SetBit(dwData, j);
		} else if (!BitA && BitB) {
			dwData = SetBit(dwData, i);
			dwData = ClearBit(dwData, j);
		} else {
			/* Do nothing since these two bits are of the save values. */
		}
	}
	return dwData;
}

void Wb35Reg_phy_calibration(struct hw_data *pHwData)
{
	u32	BB3c, BB54;

	if ((pHwData->phy_type == RF_WB_242) ||
		(pHwData->phy_type == RF_WB_242_1)) {
		phy_calibration_winbond(pHwData, 2412); /* Sync operation */
		Wb35Reg_ReadSync(pHwData, 0x103c, &BB3c);
		Wb35Reg_ReadSync(pHwData, 0x1054, &BB54);

		pHwData->BB3c_cal = BB3c;
		pHwData->BB54_cal = BB54;

		RFSynthesizer_initial(pHwData);
		BBProcessor_initial(pHwData); /* Async operation */

		Wb35Reg_WriteSync(pHwData, 0x103c, BB3c);
		Wb35Reg_WriteSync(pHwData, 0x1054, BB54);
	}
}


