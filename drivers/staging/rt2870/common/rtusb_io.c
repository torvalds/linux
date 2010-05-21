/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************

	Module Name:
	rtusb_io.c

	Abstract:

	Revision History:
	Who			When	    What
	--------	----------  ----------------------------------------------
	Name		Date	    Modification logs
	Paul Lin    06-25-2004  created
*/

#ifdef RTMP_MAC_USB

#include "../rt_config.h"

/*
	========================================================================

	Routine Description: NIC initialization complete

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/

static int RTUSBFirmwareRun(struct rt_rtmp_adapter *pAd)
{
	int Status;

	Status = RTUSB_VendorRequest(pAd,
				     USBD_TRANSFER_DIRECTION_OUT,
				     DEVICE_VENDOR_REQUEST_OUT,
				     0x01, 0x8, 0, NULL, 0);

	return Status;
}

/*
	========================================================================

	Routine Description: Write Firmware to NIC.

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
int RTUSBFirmwareWrite(struct rt_rtmp_adapter *pAd,
		       const u8 *pFwImage, unsigned long FwLen)
{
	u32 MacReg;
	int Status;
/*      unsigned long           i; */
	u16 writeLen;

	Status = RTUSBReadMACRegister(pAd, MAC_CSR0, &MacReg);

	writeLen = FwLen;
	RTUSBMultiWrite(pAd, FIRMWARE_IMAGE_BASE, pFwImage, writeLen);

	Status = RTUSBWriteMACRegister(pAd, 0x7014, 0xffffffff);
	Status = RTUSBWriteMACRegister(pAd, 0x701c, 0xffffffff);
	Status = RTUSBFirmwareRun(pAd);

	/*2008/11/28:KH add to fix the dead rf frequency offset bug<-- */
	RTMPusecDelay(10000);
	RTUSBWriteMACRegister(pAd, H2M_MAILBOX_CSR, 0);
	AsicSendCommandToMcu(pAd, 0x72, 0x00, 0x00, 0x00);	/*reset rf by MCU supported by new firmware */
	/*2008/11/28:KH add to fix the dead rf frequency offset bug--> */

	return Status;
}

int RTUSBVenderReset(struct rt_rtmp_adapter *pAd)
{
	int Status;
	DBGPRINT_RAW(RT_DEBUG_ERROR, ("-->RTUSBVenderReset\n"));
	Status = RTUSB_VendorRequest(pAd,
				     USBD_TRANSFER_DIRECTION_OUT,
				     DEVICE_VENDOR_REQUEST_OUT,
				     0x01, 0x1, 0, NULL, 0);

	DBGPRINT_RAW(RT_DEBUG_ERROR, ("<--RTUSBVenderReset\n"));
	return Status;
}

/*
	========================================================================

	Routine Description: Read various length data from RT2573

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
int RTUSBMultiRead(struct rt_rtmp_adapter *pAd,
			u16 Offset, u8 *pData, u16 length)
{
	int Status;

	Status = RTUSB_VendorRequest(pAd,
				     (USBD_TRANSFER_DIRECTION_IN |
				      USBD_SHORT_TRANSFER_OK),
				     DEVICE_VENDOR_REQUEST_IN, 0x7, 0, Offset,
				     pData, length);

	return Status;
}

/*
	========================================================================

	Routine Description: Write various length data to RT2573

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
int RTUSBMultiWrite_OneByte(struct rt_rtmp_adapter *pAd,
			    u16 Offset, const u8 *pData)
{
	int Status;

	/* TODO: In 2870, use this funciton carefully cause it's not stable. */
	Status = RTUSB_VendorRequest(pAd,
				     USBD_TRANSFER_DIRECTION_OUT,
				     DEVICE_VENDOR_REQUEST_OUT,
				     0x6, 0, Offset, (u8 *)pData, 1);

	return Status;
}

int RTUSBMultiWrite(struct rt_rtmp_adapter *pAd,
		    u16 Offset, const u8 *pData, u16 length)
{
	int Status;

	u16 index = 0, Value;
	const u8 *pSrc = pData;
	u16 resude = 0;

	resude = length % 2;
	length += resude;
	do {
		Value = (u16)(*pSrc | (*(pSrc + 1) << 8));
		Status = RTUSBSingleWrite(pAd, Offset + index, Value);
		index += 2;
		length -= 2;
		pSrc = pSrc + 2;
	} while (length > 0);

	return Status;
}

int RTUSBSingleWrite(struct rt_rtmp_adapter *pAd,
			  u16 Offset, u16 Value)
{
	int Status;

	Status = RTUSB_VendorRequest(pAd,
				     USBD_TRANSFER_DIRECTION_OUT,
				     DEVICE_VENDOR_REQUEST_OUT,
				     0x2, Value, Offset, NULL, 0);

	return Status;

}

/*
	========================================================================

	Routine Description: Read 32-bit MAC register

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
int RTUSBReadMACRegister(struct rt_rtmp_adapter *pAd,
			      u16 Offset, u32 *pValue)
{
	int Status = 0;
	u32 localVal;

	Status = RTUSB_VendorRequest(pAd,
				     (USBD_TRANSFER_DIRECTION_IN |
				      USBD_SHORT_TRANSFER_OK),
				     DEVICE_VENDOR_REQUEST_IN, 0x7, 0, Offset,
				     &localVal, 4);

	*pValue = le2cpu32(localVal);

	if (Status < 0)
		*pValue = 0xffffffff;

	return Status;
}

/*
	========================================================================

	Routine Description: Write 32-bit MAC register

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
int RTUSBWriteMACRegister(struct rt_rtmp_adapter *pAd,
			       u16 Offset, u32 Value)
{
	int Status;
	u32 localVal;

	localVal = Value;

	Status = RTUSBSingleWrite(pAd, Offset, (u16)(localVal & 0xffff));
	Status =
	    RTUSBSingleWrite(pAd, Offset + 2,
			     (u16)((localVal & 0xffff0000) >> 16));

	return Status;
}

/*
	========================================================================

	Routine Description: Read 8-bit BBP register

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
int RTUSBReadBBPRegister(struct rt_rtmp_adapter *pAd,
			      u8 Id, u8 *pValue)
{
	BBP_CSR_CFG_STRUC BbpCsr;
	u32 i = 0;
	int status;

	/* Verify the busy condition */
	do {
		status = RTUSBReadMACRegister(pAd, BBP_CSR_CFG, &BbpCsr.word);
		if (status >= 0) {
			if (!(BbpCsr.field.Busy == BUSY))
				break;
		}
		DBGPRINT(RT_DEBUG_TRACE,
			 ("RTUSBReadBBPRegister(BBP_CSR_CFG_1):retry count=%d!\n",
			  i));
		i++;
	} while ((i < RETRY_LIMIT)
		 && (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)));

	if ((i == RETRY_LIMIT)
	    || (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))) {
		/* */
		/* Read failed then Return Default value. */
		/* */
		*pValue = pAd->BbpWriteLatch[Id];

		DBGPRINT_RAW(RT_DEBUG_ERROR,
			     ("Retry count exhausted or device removed!!!\n"));
		return STATUS_UNSUCCESSFUL;
	}
	/* Prepare for write material */
	BbpCsr.word = 0;
	BbpCsr.field.fRead = 1;
	BbpCsr.field.Busy = 1;
	BbpCsr.field.RegNum = Id;
	RTUSBWriteMACRegister(pAd, BBP_CSR_CFG, BbpCsr.word);

	i = 0;
	/* Verify the busy condition */
	do {
		status = RTUSBReadMACRegister(pAd, BBP_CSR_CFG, &BbpCsr.word);
		if (status >= 0) {
			if (!(BbpCsr.field.Busy == BUSY)) {
				*pValue = (u8)BbpCsr.field.Value;
				break;
			}
		}
		DBGPRINT(RT_DEBUG_TRACE,
			 ("RTUSBReadBBPRegister(BBP_CSR_CFG_2):retry count=%d!\n",
			  i));
		i++;
	} while ((i < RETRY_LIMIT)
		 && (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)));

	if ((i == RETRY_LIMIT)
	    || (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))) {
		/* */
		/* Read failed then Return Default value. */
		/* */
		*pValue = pAd->BbpWriteLatch[Id];

		DBGPRINT_RAW(RT_DEBUG_ERROR,
			     ("Retry count exhausted or device removed!!!\n"));
		return STATUS_UNSUCCESSFUL;
	}

	return STATUS_SUCCESS;
}

/*
	========================================================================

	Routine Description: Write 8-bit BBP register

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
int RTUSBWriteBBPRegister(struct rt_rtmp_adapter *pAd,
			       u8 Id, u8 Value)
{
	BBP_CSR_CFG_STRUC BbpCsr;
	u32 i = 0;
	int status;
	/* Verify the busy condition */
	do {
		status = RTUSBReadMACRegister(pAd, BBP_CSR_CFG, &BbpCsr.word);
		if (status >= 0) {
			if (!(BbpCsr.field.Busy == BUSY))
				break;
		}
		DBGPRINT(RT_DEBUG_TRACE,
			 ("RTUSBWriteBBPRegister(BBP_CSR_CFG):retry count=%d!\n",
			  i));
		i++;
	} while ((i < RETRY_LIMIT)
	       && (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)));

	if ((i == RETRY_LIMIT)
	    || (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))) {
		DBGPRINT_RAW(RT_DEBUG_ERROR,
			     ("Retry count exhausted or device removed!!!\n"));
		return STATUS_UNSUCCESSFUL;
	}
	/* Prepare for write material */
	BbpCsr.word = 0;
	BbpCsr.field.fRead = 0;
	BbpCsr.field.Value = Value;
	BbpCsr.field.Busy = 1;
	BbpCsr.field.RegNum = Id;
	RTUSBWriteMACRegister(pAd, BBP_CSR_CFG, BbpCsr.word);

	pAd->BbpWriteLatch[Id] = Value;

	return STATUS_SUCCESS;
}

/*
	========================================================================

	Routine Description: Write RF register through MAC

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
int RTUSBWriteRFRegister(struct rt_rtmp_adapter *pAd, u32 Value)
{
	PHY_CSR4_STRUC PhyCsr4;
	u32 i = 0;
	int status;

	NdisZeroMemory(&PhyCsr4, sizeof(PHY_CSR4_STRUC));
	do {
		status = RTUSBReadMACRegister(pAd, RF_CSR_CFG0, &PhyCsr4.word);
		if (status >= 0) {
			if (!(PhyCsr4.field.Busy))
				break;
		}
		DBGPRINT(RT_DEBUG_TRACE,
			 ("RTUSBWriteRFRegister(RF_CSR_CFG0):retry count=%d!\n",
			  i));
		i++;
	} while ((i < RETRY_LIMIT)
	       && (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)));

	if ((i == RETRY_LIMIT)
	    || (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))) {
		DBGPRINT_RAW(RT_DEBUG_ERROR,
			     ("Retry count exhausted or device removed!!!\n"));
		return STATUS_UNSUCCESSFUL;
	}

	RTUSBWriteMACRegister(pAd, RF_CSR_CFG0, Value);

	return STATUS_SUCCESS;
}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
int RTUSBReadEEPROM(struct rt_rtmp_adapter *pAd,
			 u16 Offset, u8 *pData, u16 length)
{
	int Status = STATUS_SUCCESS;

	Status = RTUSB_VendorRequest(pAd,
				     (USBD_TRANSFER_DIRECTION_IN |
				      USBD_SHORT_TRANSFER_OK),
				     DEVICE_VENDOR_REQUEST_IN, 0x9, 0, Offset,
				     pData, length);

	return Status;
}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
int RTUSBWriteEEPROM(struct rt_rtmp_adapter *pAd,
			  u16 Offset, u8 *pData, u16 length)
{
	int Status = STATUS_SUCCESS;

	Status = RTUSB_VendorRequest(pAd,
				     USBD_TRANSFER_DIRECTION_OUT,
				     DEVICE_VENDOR_REQUEST_OUT,
				     0x8, 0, Offset, pData, length);

	return Status;
}

int RTUSBReadEEPROM16(struct rt_rtmp_adapter *pAd,
			   u16 offset, u16 *pData)
{
	int status;
	u16 localData;

	status = RTUSBReadEEPROM(pAd, offset, (u8 *)(&localData), 2);
	if (status == STATUS_SUCCESS)
		*pData = le2cpu16(localData);

	return status;

}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
void RTUSBPutToSleep(struct rt_rtmp_adapter *pAd)
{
	u32 value;

	/* Timeout 0x40 x 50us */
	value = (SLEEPCID << 16) + (OWNERMCU << 24) + (0x40 << 8) + 1;
	RTUSBWriteMACRegister(pAd, 0x7010, value);
	RTUSBWriteMACRegister(pAd, 0x404, 0x30);
	/*RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS); */
	DBGPRINT_RAW(RT_DEBUG_ERROR, ("Sleep Mailbox testvalue %x\n", value));

}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
int RTUSBWakeUp(struct rt_rtmp_adapter *pAd)
{
	int Status;

	Status = RTUSB_VendorRequest(pAd,
				     USBD_TRANSFER_DIRECTION_OUT,
				     DEVICE_VENDOR_REQUEST_OUT,
				     0x01, 0x09, 0, NULL, 0);

	return Status;
}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
void RTUSBInitializeCmdQ(struct rt_cmdq *cmdq)
{
	cmdq->head = NULL;
	cmdq->tail = NULL;
	cmdq->size = 0;
	cmdq->CmdQState = RTMP_TASK_STAT_INITED;
}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
int RTUSBEnqueueCmdFromNdis(struct rt_rtmp_adapter *pAd,
				    IN NDIS_OID Oid,
				    IN BOOLEAN SetInformation,
				    void *pInformationBuffer,
				    u32 InformationBufferLength)
{
	int status;
	struct rt_cmdqelmt *cmdqelmt = NULL;
	struct rt_rtmp_os_task *pTask = &pAd->cmdQTask;

#ifdef KTHREAD_SUPPORT
	if (pTask->kthread_task == NULL)
#else
	CHECK_PID_LEGALITY(pTask->taskPID) {
	}
	else
#endif
	return NDIS_STATUS_RESOURCES;

	status = os_alloc_mem(pAd, (u8 **) (&cmdqelmt), sizeof(struct rt_cmdqelmt));
	if ((status != NDIS_STATUS_SUCCESS) || (cmdqelmt == NULL))
		return NDIS_STATUS_RESOURCES;

	cmdqelmt->buffer = NULL;
	if (pInformationBuffer != NULL) {
		status =
		    os_alloc_mem(pAd, (u8 **) & cmdqelmt->buffer,
				 InformationBufferLength);
		if ((status != NDIS_STATUS_SUCCESS)
		    || (cmdqelmt->buffer == NULL)) {
			kfree(cmdqelmt);
			return NDIS_STATUS_RESOURCES;
		} else {
			NdisMoveMemory(cmdqelmt->buffer, pInformationBuffer,
				       InformationBufferLength);
			cmdqelmt->bufferlength = InformationBufferLength;
		}
	} else
		cmdqelmt->bufferlength = 0;

	cmdqelmt->command = Oid;
	cmdqelmt->CmdFromNdis = TRUE;
	if (SetInformation == TRUE)
		cmdqelmt->SetOperation = TRUE;
	else
		cmdqelmt->SetOperation = FALSE;

	NdisAcquireSpinLock(&pAd->CmdQLock);
	if (pAd->CmdQ.CmdQState & RTMP_TASK_CAN_DO_INSERT) {
		EnqueueCmd((&pAd->CmdQ), cmdqelmt);
		status = NDIS_STATUS_SUCCESS;
	} else {
		status = NDIS_STATUS_FAILURE;
	}
	NdisReleaseSpinLock(&pAd->CmdQLock);

	if (status == NDIS_STATUS_FAILURE) {
		if (cmdqelmt->buffer)
			os_free_mem(pAd, cmdqelmt->buffer);
		os_free_mem(pAd, cmdqelmt);
	} else
		RTUSBCMDUp(pAd);

	return NDIS_STATUS_SUCCESS;
}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
int RTUSBEnqueueInternalCmd(struct rt_rtmp_adapter *pAd,
				    IN NDIS_OID Oid,
				    void *pInformationBuffer,
				    u32 InformationBufferLength)
{
	int status;
	struct rt_cmdqelmt *cmdqelmt = NULL;

	status = os_alloc_mem(pAd, (u8 **) & cmdqelmt, sizeof(struct rt_cmdqelmt));
	if ((status != NDIS_STATUS_SUCCESS) || (cmdqelmt == NULL))
		return NDIS_STATUS_RESOURCES;
	NdisZeroMemory(cmdqelmt, sizeof(struct rt_cmdqelmt));

	if (InformationBufferLength > 0) {
		status =
		    os_alloc_mem(pAd, (u8 **) & cmdqelmt->buffer,
				 InformationBufferLength);
		if ((status != NDIS_STATUS_SUCCESS)
		    || (cmdqelmt->buffer == NULL)) {
			os_free_mem(pAd, cmdqelmt);
			return NDIS_STATUS_RESOURCES;
		} else {
			NdisMoveMemory(cmdqelmt->buffer, pInformationBuffer,
				       InformationBufferLength);
			cmdqelmt->bufferlength = InformationBufferLength;
		}
	} else {
		cmdqelmt->buffer = NULL;
		cmdqelmt->bufferlength = 0;
	}

	cmdqelmt->command = Oid;
	cmdqelmt->CmdFromNdis = FALSE;

	if (cmdqelmt != NULL) {
		NdisAcquireSpinLock(&pAd->CmdQLock);
		if (pAd->CmdQ.CmdQState & RTMP_TASK_CAN_DO_INSERT) {
			EnqueueCmd((&pAd->CmdQ), cmdqelmt);
			status = NDIS_STATUS_SUCCESS;
		} else {
			status = NDIS_STATUS_FAILURE;
		}
		NdisReleaseSpinLock(&pAd->CmdQLock);

		if (status == NDIS_STATUS_FAILURE) {
			if (cmdqelmt->buffer)
				os_free_mem(pAd, cmdqelmt->buffer);
			os_free_mem(pAd, cmdqelmt);
		} else
			RTUSBCMDUp(pAd);
	}
	return NDIS_STATUS_SUCCESS;
}

/*
	========================================================================

	Routine Description:

	Arguments:

	Return Value:

	IRQL =

	Note:

	========================================================================
*/
void RTUSBDequeueCmd(struct rt_cmdq *cmdq, struct rt_cmdqelmt * * pcmdqelmt)
{
	*pcmdqelmt = cmdq->head;

	if (*pcmdqelmt != NULL) {
		cmdq->head = cmdq->head->next;
		cmdq->size--;
		if (cmdq->size == 0)
			cmdq->tail = NULL;
	}
}

/*
    ========================================================================
	  usb_control_msg - Builds a control urb, sends it off and waits for completion
	  @dev: pointer to the usb device to send the message to
	  @pipe: endpoint "pipe" to send the message to
	  @request: USB message request value
	  @requesttype: USB message request type value
	  @value: USB message value
	  @index: USB message index value
	  @data: pointer to the data to send
	  @size: length in bytes of the data to send
	  @timeout: time in jiffies to wait for the message to complete before
			  timing out (if 0 the wait is forever)
	  Context: !in_interrupt ()

	  This function sends a simple control message to a specified endpoint
	  and waits for the message to complete, or timeout.
	  If successful, it returns the number of bytes transferred, otherwise a negative error number.

	 Don't use this function from within an interrupt context, like a
	  bottom half handler.	If you need an asynchronous message, or need to send
	  a message from within interrupt context, use usb_submit_urb()
	  If a thread in your driver uses this call, make sure your disconnect()
	  method can wait for it to complete.  Since you don't have a handle on
	  the URB used, you can't cancel the request.

	Routine Description:

	Arguments:

	Return Value:

	Note:

	========================================================================
*/
int RTUSB_VendorRequest(struct rt_rtmp_adapter *pAd,
			     u32 TransferFlags,
			     u8 RequestType,
			     u8 Request,
			     u16 Value,
			     u16 Index,
			     void *TransferBuffer,
			     u32 TransferBufferLength)
{
	int ret = 0;
	struct os_cookie *pObj = (struct os_cookie *)pAd->OS_Cookie;

	if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) {
		DBGPRINT(RT_DEBUG_ERROR, ("device disconnected\n"));
		return -1;
	} else if (in_interrupt()) {
		DBGPRINT(RT_DEBUG_ERROR,
			 ("in_interrupt, RTUSB_VendorRequest Request%02x Value%04x Offset%04x\n",
			  Request, Value, Index));

		return -1;
	} else {
#define MAX_RETRY_COUNT  10

		int retryCount = 0;
		void *tmpBuf = TransferBuffer;

		ret = down_interruptible(&(pAd->UsbVendorReq_semaphore));
		if (pAd->UsbVendorReqBuf) {
			ASSERT(TransferBufferLength < MAX_PARAM_BUFFER_SIZE);

			tmpBuf = (void *)pAd->UsbVendorReqBuf;
			NdisZeroMemory(pAd->UsbVendorReqBuf,
				       TransferBufferLength);

			if (RequestType == DEVICE_VENDOR_REQUEST_OUT)
				NdisMoveMemory(tmpBuf, TransferBuffer,
					       TransferBufferLength);
		}

		do {
			if (RequestType == DEVICE_VENDOR_REQUEST_OUT)
				ret =
				    usb_control_msg(pObj->pUsb_Dev,
						    usb_sndctrlpipe(pObj->
								    pUsb_Dev,
								    0), Request,
						    RequestType, Value, Index,
						    tmpBuf,
						    TransferBufferLength,
						    CONTROL_TIMEOUT_JIFFIES);
			else if (RequestType == DEVICE_VENDOR_REQUEST_IN)
				ret =
				    usb_control_msg(pObj->pUsb_Dev,
						    usb_rcvctrlpipe(pObj->
								    pUsb_Dev,
								    0), Request,
						    RequestType, Value, Index,
						    tmpBuf,
						    TransferBufferLength,
						    CONTROL_TIMEOUT_JIFFIES);
			else {
				DBGPRINT(RT_DEBUG_ERROR,
					 ("vendor request direction is failed\n"));
				ret = -1;
			}

			retryCount++;
			if (ret < 0) {
				DBGPRINT(RT_DEBUG_OFF, ("#\n"));
				RTMPusecDelay(5000);
			}
		} while ((ret < 0) && (retryCount < MAX_RETRY_COUNT));

		if ((pAd->UsbVendorReqBuf)
		    && (RequestType == DEVICE_VENDOR_REQUEST_IN))
			NdisMoveMemory(TransferBuffer, tmpBuf,
				       TransferBufferLength);
		up(&(pAd->UsbVendorReq_semaphore));

		if (ret < 0) {
			DBGPRINT(RT_DEBUG_ERROR,
				 ("RTUSB_VendorRequest failed(%d),TxFlags=0x%x, ReqType=%s, Req=0x%x, Index=0x%x\n",
				  ret, TransferFlags,
				  (RequestType ==
				   DEVICE_VENDOR_REQUEST_OUT ? "OUT" : "IN"),
				  Request, Index));
			if (Request == 0x2)
				DBGPRINT(RT_DEBUG_ERROR,
					 ("\tRequest Value=0x%04x!\n", Value));

			if ((TransferBuffer != NULL)
			    && (TransferBufferLength > 0))
				hex_dump("Failed TransferBuffer value",
					 TransferBuffer, TransferBufferLength);
		}

	}

	if (ret != -1)
		return STATUS_SUCCESS;
	else
		return STATUS_UNSUCCESSFUL;
}

/*
	========================================================================

	Routine Description:
	  Creates an IRP to submite an IOCTL_INTERNAL_USB_RESET_PORT
	  synchronously. Callers of this function must be running at
	  PASSIVE LEVEL.

	Arguments:

	Return Value:

	Note:

	========================================================================
*/
int RTUSB_ResetDevice(struct rt_rtmp_adapter *pAd)
{
	int Status = TRUE;

	DBGPRINT_RAW(RT_DEBUG_TRACE, ("--->USB_ResetDevice\n"));
	/*RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS); */
	return Status;
}

void CMDHandler(struct rt_rtmp_adapter *pAd)
{
	struct rt_cmdqelmt *cmdqelmt;
	u8 *pData;
	int NdisStatus = NDIS_STATUS_SUCCESS;
/*      unsigned long                   Now = 0; */
	int ntStatus;
/*      unsigned long   IrqFlags; */

	while (pAd && pAd->CmdQ.size > 0) {
		NdisStatus = NDIS_STATUS_SUCCESS;

		NdisAcquireSpinLock(&pAd->CmdQLock);
		RTUSBDequeueCmd(&pAd->CmdQ, &cmdqelmt);
		NdisReleaseSpinLock(&pAd->CmdQLock);

		if (cmdqelmt == NULL)
			break;

		pData = cmdqelmt->buffer;

		if (!
		    (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)
		     || RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))) {
			switch (cmdqelmt->command) {
			case CMDTHREAD_CHECK_GPIO:
				{
					u32 data;

					{
						/* Read GPIO pin2 as Hardware controlled radio state */

						RTUSBReadMACRegister(pAd,
								     GPIO_CTRL_CFG,
								     &data);

						if (data & 0x04) {
							pAd->StaCfg.bHwRadio =
							    TRUE;
						} else {
							pAd->StaCfg.bHwRadio =
							    FALSE;
						}

						if (pAd->StaCfg.bRadio !=
						    (pAd->StaCfg.bHwRadio
						     && pAd->StaCfg.bSwRadio)) {
							pAd->StaCfg.bRadio =
							    (pAd->StaCfg.
							     bHwRadio
							     && pAd->StaCfg.
							     bSwRadio);
							if (pAd->StaCfg.
							    bRadio == TRUE) {
								DBGPRINT_RAW
								    (RT_DEBUG_ERROR,
								     ("!!! Radio On !!!\n"));

								MlmeRadioOn
								    (pAd);
								/* Update extra information */
								pAd->ExtraInfo =
								    EXTRA_INFO_CLEAR;
							} else {
								DBGPRINT_RAW
								    (RT_DEBUG_ERROR,
								     ("!!! Radio Off !!!\n"));

								MlmeRadioOff
								    (pAd);
								/* Update extra information */
								pAd->ExtraInfo =
								    HW_RADIO_OFF;
							}
						}
					}
				}
				break;

			case CMDTHREAD_QKERIODIC_EXECUT:
				{
					StaQuickResponeForRateUpExec(NULL, pAd,
								     NULL,
								     NULL);
				}
				break;

			case CMDTHREAD_RESET_BULK_OUT:
				{
					u32 MACValue;
					u8 Index;
					int ret = 0;
					struct rt_ht_tx_context *pHTTXContext;
/*                                              struct rt_rtmp_tx_ring *pTxRing; */
					unsigned long IrqFlags;

					DBGPRINT_RAW(RT_DEBUG_TRACE,
						     ("CmdThread : CMDTHREAD_RESET_BULK_OUT(ResetPipeid=0x%0x)===>\n",
						      pAd->bulkResetPipeid));
					/* All transfers must be aborted or cancelled before attempting to reset the pipe. */
					/*RTUSBCancelPendingBulkOutIRP(pAd); */
					/* Wait 10ms to let previous packet that are already in HW FIFO to clear. by MAXLEE 12-25-2007 */
					Index = 0;
					do {
						RTUSBReadMACRegister(pAd,
								     TXRXQ_PCNT,
								     &MACValue);
						if ((MACValue & 0xf00000
						     /*0x800000 */) == 0)
							break;
						Index++;
						RTMPusecDelay(10000);
					} while (Index < 100);
					MACValue = 0;
					RTUSBReadMACRegister(pAd, USB_DMA_CFG,
							     &MACValue);
					/* To prevent Read Register error, we 2nd check the validity. */
					if ((MACValue & 0xc00000) == 0)
						RTUSBReadMACRegister(pAd,
								     USB_DMA_CFG,
								     &MACValue);
					/* To prevent Read Register error, we 3rd check the validity. */
					if ((MACValue & 0xc00000) == 0)
						RTUSBReadMACRegister(pAd,
								     USB_DMA_CFG,
								     &MACValue);
					MACValue |= 0x80000;
					RTUSBWriteMACRegister(pAd, USB_DMA_CFG,
							      MACValue);

					/* Wait 1ms to prevent next URB to bulkout before HW reset. by MAXLEE 12-25-2007 */
					RTMPusecDelay(1000);

					MACValue &= (~0x80000);
					RTUSBWriteMACRegister(pAd, USB_DMA_CFG,
							      MACValue);
					DBGPRINT_RAW(RT_DEBUG_TRACE,
						     ("\tSet 0x2a0 bit19. Clear USB DMA TX path\n"));

					/* Wait 5ms to prevent next URB to bulkout before HW reset. by MAXLEE 12-25-2007 */
					/*RTMPusecDelay(5000); */

					if ((pAd->
					     bulkResetPipeid &
					     BULKOUT_MGMT_RESET_FLAG) ==
					    BULKOUT_MGMT_RESET_FLAG) {
						RTMP_CLEAR_FLAG(pAd,
								fRTMP_ADAPTER_BULKOUT_RESET);
						if (pAd->MgmtRing.TxSwFreeIdx <
						    MGMT_RING_SIZE
						    /* pMLMEContext->bWaitingBulkOut == TRUE */
						    ) {
							RTUSB_SET_BULK_FLAG(pAd,
									    fRTUSB_BULK_OUT_MLME);
						}
						RTUSBKickBulkOut(pAd);

						DBGPRINT_RAW(RT_DEBUG_TRACE,
							     ("\tTX MGMT RECOVER Done!\n"));
					} else {
						pHTTXContext =
						    &(pAd->
						      TxContext[pAd->
								bulkResetPipeid]);
						/*NdisAcquireSpinLock(&pAd->BulkOutLock[pAd->bulkResetPipeid]); */
						RTMP_INT_LOCK(&pAd->
							      BulkOutLock[pAd->
									  bulkResetPipeid],
							      IrqFlags);
						if (pAd->
						    BulkOutPending[pAd->
								   bulkResetPipeid]
						    == FALSE) {
							pAd->
							    BulkOutPending[pAd->
									   bulkResetPipeid]
							    = TRUE;
							pHTTXContext->
							    IRPPending = TRUE;
							pAd->
							    watchDogTxPendingCnt
							    [pAd->
							     bulkResetPipeid] =
							    1;

							/* no matter what, clean the flag */
							RTMP_CLEAR_FLAG(pAd,
									fRTMP_ADAPTER_BULKOUT_RESET);

							/*NdisReleaseSpinLock(&pAd->BulkOutLock[pAd->bulkResetPipeid]); */
							RTMP_INT_UNLOCK(&pAd->
									BulkOutLock
									[pAd->
									 bulkResetPipeid],
									IrqFlags);
							{
								RTUSBInitHTTxDesc
								    (pAd,
								     pHTTXContext,
								     pAd->
								     bulkResetPipeid,
								     pHTTXContext->
								     BulkOutSize,
								     (usb_complete_t)
								     RTUSBBulkOutDataPacketComplete);

								ret = RTUSB_SUBMIT_URB
								     (pHTTXContext->
								      pUrb);
								if (ret != 0) {
									RTMP_INT_LOCK
									    (&pAd->
									     BulkOutLock
									     [pAd->
									      bulkResetPipeid],
									     IrqFlags);
									pAd->
									    BulkOutPending
									    [pAd->
									     bulkResetPipeid]
									    =
									    FALSE;
									pHTTXContext->
									    IRPPending
									    =
									    FALSE;
									pAd->
									    watchDogTxPendingCnt
									    [pAd->
									     bulkResetPipeid]
									    = 0;
									RTMP_INT_UNLOCK
									    (&pAd->
									     BulkOutLock
									     [pAd->
									      bulkResetPipeid],
									     IrqFlags);

									DBGPRINT
									    (RT_DEBUG_ERROR,
									     ("CmdThread : CMDTHREAD_RESET_BULK_OUT: Submit Tx URB failed %d\n",
									      ret));
								} else {
									RTMP_IRQ_LOCK
									    (&pAd->
									     BulkOutLock
									     [pAd->
									      bulkResetPipeid],
									     IrqFlags);
									DBGPRINT_RAW
									    (RT_DEBUG_TRACE,
									     ("\tCMDTHREAD_RESET_BULK_OUT: TxContext[%d]:CWPos=%ld, NBPos=%ld, ENBPos=%ld, bCopy=%d, pending=%d!\n",
									      pAd->
									      bulkResetPipeid,
									      pHTTXContext->
									      CurWritePosition,
									      pHTTXContext->
									      NextBulkOutPosition,
									      pHTTXContext->
									      ENextBulkOutPosition,
									      pHTTXContext->
									      bCopySavePad,
									      pAd->
									      BulkOutPending
									      [pAd->
									       bulkResetPipeid]));
									DBGPRINT_RAW
									    (RT_DEBUG_TRACE,
									     ("\t\tBulkOut Req=0x%lx, Complete=0x%lx, Other=0x%lx\n",
									      pAd->
									      BulkOutReq,
									      pAd->
									      BulkOutComplete,
									      pAd->
									      BulkOutCompleteOther));
									RTMP_IRQ_UNLOCK
									    (&pAd->
									     BulkOutLock
									     [pAd->
									      bulkResetPipeid],
									     IrqFlags);
									DBGPRINT_RAW
									    (RT_DEBUG_TRACE,
									     ("\tCMDTHREAD_RESET_BULK_OUT: Submit Tx DATA URB for failed BulkReq(0x%lx) Done, status=%d!\n",
									      pAd->
									      bulkResetReq
									      [pAd->
									       bulkResetPipeid],
									      pHTTXContext->
									      pUrb->
									      status));

								}
							}
						} else {
							/*NdisReleaseSpinLock(&pAd->BulkOutLock[pAd->bulkResetPipeid]); */
							/*RTMP_INT_UNLOCK(&pAd->BulkOutLock[pAd->bulkResetPipeid], IrqFlags); */

							DBGPRINT_RAW
							    (RT_DEBUG_ERROR,
							     ("CmdThread : TX DATA RECOVER FAIL for BulkReq(0x%lx) because BulkOutPending[%d] is TRUE!\n",
							      pAd->
							      bulkResetReq[pAd->
									   bulkResetPipeid],
							      pAd->
							      bulkResetPipeid));
							if (pAd->
							    bulkResetPipeid ==
							    0) {
								u8
								    pendingContext
								    = 0;
								struct rt_ht_tx_context *
								    pHTTXContext
								    =
								    (struct rt_ht_tx_context *)
								    (&pAd->
								     TxContext
								     [pAd->
								      bulkResetPipeid]);
								struct rt_tx_context *
								    pMLMEContext
								    =
								    (struct rt_tx_context *)
								    (pAd->
								     MgmtRing.
								     Cell[pAd->
									  MgmtRing.
									  TxDmaIdx].
								     AllocVa);
								struct rt_tx_context *
								    pNULLContext
								    =
								    (struct rt_tx_context *)
								    (&pAd->
								     PsPollContext);
								struct rt_tx_context *
								    pPsPollContext
								    =
								    (struct rt_tx_context *)
								    (&pAd->
								     NullContext);

								if (pHTTXContext->IRPPending)
									pendingContext
									    |=
									    1;
								else if
								    (pMLMEContext->
								     IRPPending)
									pendingContext
									    |=
									    2;
								else if
								    (pNULLContext->
								     IRPPending)
									pendingContext
									    |=
									    4;
								else if
								    (pPsPollContext->
								     IRPPending)
									pendingContext
									    |=
									    8;
								else
									pendingContext
									    = 0;

								DBGPRINT_RAW
								    (RT_DEBUG_ERROR,
								     ("\tTX Occupied by %d!\n",
								      pendingContext));
							}
							/* no matter what, clean the flag */
							RTMP_CLEAR_FLAG(pAd,
									fRTMP_ADAPTER_BULKOUT_RESET);

							RTMP_INT_UNLOCK(&pAd->
									BulkOutLock
									[pAd->
									 bulkResetPipeid],
									IrqFlags);

							RTUSB_SET_BULK_FLAG(pAd,
									    (fRTUSB_BULK_OUT_DATA_NORMAL
									     <<
									     pAd->
									     bulkResetPipeid));
						}

						RTMPDeQueuePacket(pAd, FALSE,
								  NUM_OF_TX_RING,
								  MAX_TX_PROCESS);
						/*RTUSBKickBulkOut(pAd); */
					}

				}
				/*
				   // Don't cancel BULKIN.
				   while ((atomic_read(&pAd->PendingRx) > 0) &&
				   (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)))
				   {
				   if (atomic_read(&pAd->PendingRx) > 0)
				   {
				   DBGPRINT_RAW(RT_DEBUG_ERROR, ("BulkIn IRP Pending!!cancel it!\n"));
				   RTUSBCancelPendingBulkInIRP(pAd);
				   }
				   RTMPusecDelay(100000);
				   }

				   if ((atomic_read(&pAd->PendingRx) == 0) && (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS)))
				   {
				   u8        i;
				   RTUSBRxPacket(pAd);
				   pAd->NextRxBulkInReadIndex = 0;      // Next Rx Read index
				   pAd->NextRxBulkInIndex               = 0;    // Rx Bulk pointer
				   for (i = 0; i < (RX_RING_SIZE); i++)
				   {
				   struct rt_rx_context *pRxContext = &(pAd->RxContext[i]);

				   pRxContext->pAd      = pAd;
				   pRxContext->InUse            = FALSE;
				   pRxContext->IRPPending       = FALSE;
				   pRxContext->Readable = FALSE;
				   pRxContext->ReorderInUse = FALSE;

				   }
				   RTUSBBulkReceive(pAd);
				   DBGPRINT_RAW(RT_DEBUG_ERROR, ("RTUSBBulkReceive\n"));
				   } */
				DBGPRINT_RAW(RT_DEBUG_TRACE,
					     ("CmdThread : CMDTHREAD_RESET_BULK_OUT<===\n"));
				break;

			case CMDTHREAD_RESET_BULK_IN:
				DBGPRINT_RAW(RT_DEBUG_TRACE,
					     ("CmdThread : CMDTHREAD_RESET_BULK_IN === >\n"));

				/* All transfers must be aborted or cancelled before attempting to reset the pipe. */
				{
					u32 MACValue;
					{
						/*while ((atomic_read(&pAd->PendingRx) > 0) && (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST))) */
						if ((pAd->PendingRx > 0)
						    &&
						    (!RTMP_TEST_FLAG
						     (pAd,
						      fRTMP_ADAPTER_NIC_NOT_EXIST))) {
							DBGPRINT_RAW
							    (RT_DEBUG_ERROR,
							     ("BulkIn IRP Pending!!!\n"));
							RTUSBCancelPendingBulkInIRP
							    (pAd);
							RTMPusecDelay(100000);
							pAd->PendingRx = 0;
						}
					}
					/* Wait 10ms before reading register. */
					RTMPusecDelay(10000);
					ntStatus =
					    RTUSBReadMACRegister(pAd, MAC_CSR0,
								 &MACValue);

					if ((NT_SUCCESS(ntStatus) == TRUE) &&
					    (!(RTMP_TEST_FLAG
					       (pAd,
						(fRTMP_ADAPTER_RESET_IN_PROGRESS
						 | fRTMP_ADAPTER_RADIO_OFF |
						 fRTMP_ADAPTER_HALT_IN_PROGRESS
						 |
						 fRTMP_ADAPTER_NIC_NOT_EXIST)))))
					{
						u8 i;

						if (RTMP_TEST_FLAG
						    (pAd,
						     (fRTMP_ADAPTER_RESET_IN_PROGRESS
						      | fRTMP_ADAPTER_RADIO_OFF
						      |
						      fRTMP_ADAPTER_HALT_IN_PROGRESS
						      |
						      fRTMP_ADAPTER_NIC_NOT_EXIST)))
							break;
						pAd->NextRxBulkInPosition =
						    pAd->RxContext[pAd->
								   NextRxBulkInIndex].
						    BulkInOffset;
						DBGPRINT(RT_DEBUG_TRACE,
							 ("BULK_IN_RESET: NBIIdx=0x%x,NBIRIdx=0x%x, BIRPos=0x%lx. BIReq=x%lx, BIComplete=0x%lx, BICFail0x%lx\n",
							  pAd->
							  NextRxBulkInIndex,
							  pAd->
							  NextRxBulkInReadIndex,
							  pAd->
							  NextRxBulkInPosition,
							  pAd->BulkInReq,
							  pAd->BulkInComplete,
							  pAd->
							  BulkInCompleteFail));
						for (i = 0; i < RX_RING_SIZE;
						     i++) {
							DBGPRINT(RT_DEBUG_TRACE,
								 ("\tRxContext[%d]: IRPPending=%d, InUse=%d, Readable=%d!\n",
								  i,
								  pAd->
								  RxContext[i].
								  IRPPending,
								  pAd->
								  RxContext[i].
								  InUse,
								  pAd->
								  RxContext[i].
								  Readable));
						}
						/*

						   DBGPRINT_RAW(RT_DEBUG_ERROR, ("==========================================\n"));

						   pAd->NextRxBulkInReadIndex = 0;      // Next Rx Read index
						   pAd->NextRxBulkInIndex               = 0;    // Rx Bulk pointer
						   for (i = 0; i < (RX_RING_SIZE); i++)
						   {
						   struct rt_rx_context *pRxContext = &(pAd->RxContext[i]);

						   pRxContext->pAd      = pAd;
						   pRxContext->InUse            = FALSE;
						   pRxContext->IRPPending       = FALSE;
						   pRxContext->Readable = FALSE;
						   pRxContext->ReorderInUse = FALSE;

						   } */
						RTMP_CLEAR_FLAG(pAd,
								fRTMP_ADAPTER_BULKIN_RESET);
						for (i = 0;
						     i <
						     pAd->CommonCfg.
						     NumOfBulkInIRP; i++) {
							/*RTUSBBulkReceive(pAd); */
							struct rt_rx_context *pRxContext;
							PURB pUrb;
							int ret = 0;
							unsigned long IrqFlags;

							RTMP_IRQ_LOCK(&pAd->
								      BulkInLock,
								      IrqFlags);
							pRxContext =
							    &(pAd->
							      RxContext[pAd->
									NextRxBulkInIndex]);
							if ((pAd->PendingRx > 0)
							    || (pRxContext->
								Readable ==
								TRUE)
							    || (pRxContext->
								InUse ==
								TRUE)) {
								RTMP_IRQ_UNLOCK
								    (&pAd->
								     BulkInLock,
								     IrqFlags);
								break;
							}
							pRxContext->InUse =
							    TRUE;
							pRxContext->IRPPending =
							    TRUE;
							pAd->PendingRx++;
							pAd->BulkInReq++;
							RTMP_IRQ_UNLOCK(&pAd->
									BulkInLock,
									IrqFlags);

							/* Init Rx context descriptor */
							RTUSBInitRxDesc(pAd,
									pRxContext);
							pUrb = pRxContext->pUrb;
							ret = RTUSB_SUBMIT_URB(pUrb);
							if (ret != 0) {	/* fail */

								RTMP_IRQ_LOCK
								    (&pAd->
								     BulkInLock,
								     IrqFlags);
								pRxContext->
								    InUse =
								    FALSE;
								pRxContext->
								    IRPPending =
								    FALSE;
								pAd->
								    PendingRx--;
								pAd->
								    BulkInReq--;
								RTMP_IRQ_UNLOCK
								    (&pAd->
								     BulkInLock,
								     IrqFlags);
								DBGPRINT
								    (RT_DEBUG_ERROR,
								     ("CMDTHREAD_RESET_BULK_IN: Submit Rx URB failed(%d), status=%d\n",
								      ret,
								      pUrb->
								      status));
							} else {	/* success */
								/*DBGPRINT(RT_DEBUG_TRACE, ("BIDone, Pend=%d,BIIdx=%d,BIRIdx=%d!\n", */
								/*                                                      pAd->PendingRx, pAd->NextRxBulkInIndex, pAd->NextRxBulkInReadIndex)); */
								DBGPRINT_RAW
								    (RT_DEBUG_TRACE,
								     ("CMDTHREAD_RESET_BULK_IN: Submit Rx URB Done, status=%d!\n",
								      pUrb->
								      status));
								ASSERT((pRxContext->InUse == pRxContext->IRPPending));
							}
						}

					} else {
						/* Card must be removed */
						if (NT_SUCCESS(ntStatus) !=
						    TRUE) {
							RTMP_SET_FLAG(pAd,
								      fRTMP_ADAPTER_NIC_NOT_EXIST);
							DBGPRINT_RAW
							    (RT_DEBUG_ERROR,
							     ("CMDTHREAD_RESET_BULK_IN: Read Register Failed!Card must be removed!!\n\n"));
						} else {
							DBGPRINT_RAW
							    (RT_DEBUG_ERROR,
							     ("CMDTHREAD_RESET_BULK_IN: Cannot do bulk in because flags(0x%lx) on !\n",
							      pAd->Flags));
						}
					}
				}
				DBGPRINT_RAW(RT_DEBUG_TRACE,
					     ("CmdThread : CMDTHREAD_RESET_BULK_IN <===\n"));
				break;

			case CMDTHREAD_SET_ASIC_WCID:
				{
					struct rt_set_asic_wcid SetAsicWcid;
					u16 offset;
					u32 MACValue, MACRValue = 0;
					SetAsicWcid =
					    *((struct rt_set_asic_wcid *)(pData));

					if (SetAsicWcid.WCID >=
					    MAX_LEN_OF_MAC_TABLE)
						return;

					offset =
					    MAC_WCID_BASE +
					    ((u8)SetAsicWcid.WCID) *
					    HW_WCID_ENTRY_SIZE;

					DBGPRINT_RAW(RT_DEBUG_TRACE,
						     ("CmdThread : CMDTHREAD_SET_ASIC_WCID : WCID = %ld, SetTid  = %lx, DeleteTid = %lx.\n",
						      SetAsicWcid.WCID,
						      SetAsicWcid.SetTid,
						      SetAsicWcid.DeleteTid));
					MACValue =
					    (pAd->MacTab.
					     Content[SetAsicWcid.WCID].
					     Addr[3] << 24) +
					    (pAd->MacTab.
					     Content[SetAsicWcid.WCID].
					     Addr[2] << 16) +
					    (pAd->MacTab.
					     Content[SetAsicWcid.WCID].
					     Addr[1] << 8) +
					    (pAd->MacTab.
					     Content[SetAsicWcid.WCID].Addr[0]);
					DBGPRINT_RAW(RT_DEBUG_TRACE,
						     ("1-MACValue= %x,\n",
						      MACValue));
					RTUSBWriteMACRegister(pAd, offset,
							      MACValue);
					/* Read bitmask */
					RTUSBReadMACRegister(pAd, offset + 4,
							     &MACRValue);
					if (SetAsicWcid.DeleteTid != 0xffffffff)
						MACRValue &=
						    (~SetAsicWcid.DeleteTid);
					if (SetAsicWcid.SetTid != 0xffffffff)
						MACRValue |=
						    (SetAsicWcid.SetTid);
					MACRValue &= 0xffff0000;

					MACValue =
					    (pAd->MacTab.
					     Content[SetAsicWcid.WCID].
					     Addr[5] << 8) +
					    pAd->MacTab.Content[SetAsicWcid.
								WCID].Addr[4];
					MACValue |= MACRValue;
					RTUSBWriteMACRegister(pAd, offset + 4,
							      MACValue);

					DBGPRINT_RAW(RT_DEBUG_TRACE,
						     ("2-MACValue= %x,\n",
						      MACValue));
				}
				break;

			case CMDTHREAD_SET_ASIC_WCID_CIPHER:
				{
					struct rt_set_asic_wcid_attri SetAsicWcidAttri;
					u16 offset;
					u32 MACRValue = 0;
					SHAREDKEY_MODE_STRUC csr1;
					SetAsicWcidAttri =
					    *((struct rt_set_asic_wcid_attri *)
					      (pData));

					if (SetAsicWcidAttri.WCID >=
					    MAX_LEN_OF_MAC_TABLE)
						return;

					offset =
					    MAC_WCID_ATTRIBUTE_BASE +
					    ((u8)SetAsicWcidAttri.WCID) *
					    HW_WCID_ATTRI_SIZE;

					DBGPRINT_RAW(RT_DEBUG_TRACE,
						     ("Cmd : CMDTHREAD_SET_ASIC_WCID_CIPHER : WCID = %ld, Cipher = %lx.\n",
						      SetAsicWcidAttri.WCID,
						      SetAsicWcidAttri.Cipher));
					/* Read bitmask */
					RTUSBReadMACRegister(pAd, offset,
							     &MACRValue);
					MACRValue = 0;
					MACRValue |=
					    (((u8)SetAsicWcidAttri.
					      Cipher) << 1);

					RTUSBWriteMACRegister(pAd, offset,
							      MACRValue);
					DBGPRINT_RAW(RT_DEBUG_TRACE,
						     ("2-offset = %x , MACValue= %x,\n",
						      offset, MACRValue));

					offset =
					    PAIRWISE_IVEIV_TABLE_BASE +
					    ((u8)SetAsicWcidAttri.WCID) *
					    HW_IVEIV_ENTRY_SIZE;
					MACRValue = 0;
					if ((SetAsicWcidAttri.Cipher <=
					     CIPHER_WEP128))
						MACRValue |=
						    (pAd->StaCfg.
						     DefaultKeyId << 30);
					else
						MACRValue |= (0x20000000);
					RTUSBWriteMACRegister(pAd, offset,
							      MACRValue);
					DBGPRINT_RAW(RT_DEBUG_TRACE,
						     ("2-offset = %x , MACValue= %x,\n",
						      offset, MACRValue));

					/* */
					/* Update cipher algorithm. WSTA always use BSS0 */
					/* */
					/* for adhoc mode only ,because wep status slow than add key, when use zero config */
					if (pAd->StaCfg.BssType == BSS_ADHOC) {
						offset =
						    MAC_WCID_ATTRIBUTE_BASE;

						RTUSBReadMACRegister(pAd,
								     offset,
								     &MACRValue);
						MACRValue &= (~0xe);
						MACRValue |=
						    (((u8)SetAsicWcidAttri.
						      Cipher) << 1);

						RTUSBWriteMACRegister(pAd,
								      offset,
								      MACRValue);

						/*Update group key cipher,,because wep status slow than add key, when use zero config */
						RTUSBReadMACRegister(pAd,
								     SHARED_KEY_MODE_BASE
								     +
								     4 * (0 /
									  2),
								     &csr1.
								     word);

						csr1.field.Bss0Key0CipherAlg =
						    SetAsicWcidAttri.Cipher;
						csr1.field.Bss0Key1CipherAlg =
						    SetAsicWcidAttri.Cipher;

						RTUSBWriteMACRegister(pAd,
								      SHARED_KEY_MODE_BASE
								      +
								      4 * (0 /
									   2),
								      csr1.
								      word);
					}
				}
				break;

/*Benson modified for USB interface, avoid in interrupt when write key, 20080724 --> */
			case RT_CMD_SET_KEY_TABLE:	/*General call for AsicAddPairwiseKeyEntry() */
				{
					struct rt_add_pairwise_key_entry KeyInfo;
					KeyInfo =
					    *((struct rt_add_pairwise_key_entry *)
					      (pData));
					AsicAddPairwiseKeyEntry(pAd,
								KeyInfo.MacAddr,
								(u8)KeyInfo.
								MacTabMatchWCID,
								&KeyInfo.
								CipherKey);
				}
				break;

			case RT_CMD_SET_RX_WCID_TABLE:	/*General call for RTMPAddWcidAttributeEntry() */
				{
					struct rt_mac_table_entry *pEntry;
					u8 KeyIdx = 0;
					u8 CipherAlg = CIPHER_NONE;
					u8 ApIdx = BSS0;

					pEntry = (struct rt_mac_table_entry *)(pData);

					RTMPAddWcidAttributeEntry(pAd,
								  ApIdx,
								  KeyIdx,
								  CipherAlg,
								  pEntry);
				}
				break;
/*Benson modified for USB interface, avoid in interrupt when write key, 20080724 <-- */

			case CMDTHREAD_SET_CLIENT_MAC_ENTRY:
				{
					struct rt_mac_table_entry *pEntry;
					pEntry = (struct rt_mac_table_entry *)pData;

					{
						AsicRemovePairwiseKeyEntry(pAd,
									   pEntry->
									   apidx,
									   (u8)
									   pEntry->
									   Aid);
						if ((pEntry->AuthMode <=
						     Ndis802_11AuthModeAutoSwitch)
						    && (pEntry->WepStatus ==
							Ndis802_11Encryption1Enabled))
						{
							u32 uIV = 1;
							u8 *ptr;

							ptr = (u8 *)& uIV;
							*(ptr + 3) =
							    (pAd->StaCfg.
							     DefaultKeyId << 6);
							AsicUpdateWCIDIVEIV(pAd,
									    pEntry->
									    Aid,
									    uIV,
									    0);
							AsicUpdateWCIDAttribute
							    (pAd, pEntry->Aid,
							     BSS0,
							     pAd->
							     SharedKey[BSS0]
							     [pAd->StaCfg.
							      DefaultKeyId].
							     CipherAlg, FALSE);
						} else if (pEntry->AuthMode ==
							   Ndis802_11AuthModeWPANone)
						{
							u32 uIV = 1;
							u8 *ptr;

							ptr = (u8 *)& uIV;
							*(ptr + 3) =
							    (pAd->StaCfg.
							     DefaultKeyId << 6);
							AsicUpdateWCIDIVEIV(pAd,
									    pEntry->
									    Aid,
									    uIV,
									    0);
							AsicUpdateWCIDAttribute
							    (pAd, pEntry->Aid,
							     BSS0,
							     pAd->
							     SharedKey[BSS0]
							     [pAd->StaCfg.
							      DefaultKeyId].
							     CipherAlg, FALSE);
						} else {
							/* */
							/* Other case, disable engine. */
							/* Don't worry WPA key, we will add WPA Key after 4-Way handshaking. */
							/* */
							u16 offset;
							offset =
							    MAC_WCID_ATTRIBUTE_BASE
							    +
							    (pEntry->Aid *
							     HW_WCID_ATTRI_SIZE);
							/* RX_PKEY_MODE:0 for no security; RX_KEY_TAB:0 for shared key table; BSS_IDX:0 */
							RTUSBWriteMACRegister
							    (pAd, offset, 0);
						}
					}

					AsicUpdateRxWCIDTable(pAd, pEntry->Aid,
							      pEntry->Addr);
					DBGPRINT(RT_DEBUG_TRACE,
						 ("UpdateRxWCIDTable(): Aid=%d, Addr=%02x:%02x:%02x:%02x:%02x:%02x!\n",
						  pEntry->Aid, pEntry->Addr[0],
						  pEntry->Addr[1],
						  pEntry->Addr[2],
						  pEntry->Addr[3],
						  pEntry->Addr[4],
						  pEntry->Addr[5]));
				}
				break;

/* add by johnli, fix "in_interrupt" error when call "MacTableDeleteEntry" in Rx tasklet */
			case CMDTHREAD_UPDATE_PROTECT:
				{
					AsicUpdateProtect(pAd, 0,
							  (ALLN_SETPROTECT),
							  TRUE, 0);
				}
				break;
/* end johnli */

			case OID_802_11_ADD_WEP:
				{
					u32 i;
					u32 KeyIdx;
					struct rt_ndis_802_11_wep *pWepKey;

					DBGPRINT(RT_DEBUG_TRACE,
						 ("CmdThread::OID_802_11_ADD_WEP  \n"));

					pWepKey = (struct rt_ndis_802_11_wep *)pData;
					KeyIdx = pWepKey->KeyIndex & 0x0fffffff;

					/* it is a shared key */
					if ((KeyIdx >= 4)
					    || ((pWepKey->KeyLength != 5)
						&& (pWepKey->KeyLength !=
						    13))) {
						NdisStatus =
						    NDIS_STATUS_INVALID_DATA;
						DBGPRINT(RT_DEBUG_ERROR,
							 ("CmdThread::OID_802_11_ADD_WEP, INVALID_DATA!!\n"));
					} else {
						u8 CipherAlg;
						pAd->SharedKey[BSS0][KeyIdx].
						    KeyLen =
						    (u8)pWepKey->KeyLength;
						NdisMoveMemory(pAd->
							       SharedKey[BSS0]
							       [KeyIdx].Key,
							       &pWepKey->
							       KeyMaterial,
							       pWepKey->
							       KeyLength);
						CipherAlg =
						    (pAd->
						     SharedKey[BSS0][KeyIdx].
						     KeyLen ==
						     5) ? CIPHER_WEP64 :
						    CIPHER_WEP128;

						/* */
						/* Change the WEP cipher to CKIP cipher if CKIP KP on. */
						/* Funk UI or Meetinghouse UI will add ckip key from this path. */
						/* */

						if (pAd->OpMode == OPMODE_STA) {
							pAd->MacTab.
							    Content[BSSID_WCID].
							    PairwiseKey.
							    CipherAlg =
							    pAd->
							    SharedKey[BSS0]
							    [KeyIdx].CipherAlg;
							pAd->MacTab.
							    Content[BSSID_WCID].
							    PairwiseKey.KeyLen =
							    pAd->
							    SharedKey[BSS0]
							    [KeyIdx].KeyLen;
						}
						pAd->SharedKey[BSS0][KeyIdx].
						    CipherAlg = CipherAlg;
						if (pWepKey->
						    KeyIndex & 0x80000000) {
							/* Default key for tx (shared key) */
							u8 IVEIV[8];
							u32 WCIDAttri, Value;
							u16 offset, offset2;
							NdisZeroMemory(IVEIV,
								       8);
							pAd->StaCfg.
							    DefaultKeyId =
							    (u8)KeyIdx;
							/* Add BSSID to WCTable. because this is Tx wep key. */
							/* WCID Attribute UDF:3, BSSIdx:3, Alg:3, Keytable:1=PAIRWISE KEY, BSSIdx is 0 */
							WCIDAttri =
							    (CipherAlg << 1) |
							    SHAREDKEYTABLE;

							offset =
							    MAC_WCID_ATTRIBUTE_BASE
							    +
							    (BSSID_WCID *
							     HW_WCID_ATTRI_SIZE);
							RTUSBWriteMACRegister
							    (pAd, offset,
							     WCIDAttri);
							/* 1. IV/EIV */
							/* Specify key index to find shared key. */
							IVEIV[3] = (u8)(KeyIdx << 6);	/*WEP Eiv bit off. groupkey index is not 0 */
							offset =
							    PAIRWISE_IVEIV_TABLE_BASE
							    +
							    (BSS0Mcast_WCID *
							     HW_IVEIV_ENTRY_SIZE);
							offset2 =
							    PAIRWISE_IVEIV_TABLE_BASE
							    +
							    (BSSID_WCID *
							     HW_IVEIV_ENTRY_SIZE);
							for (i = 0; i < 8;) {
								Value =
								    IVEIV[i];
								Value +=
								    (IVEIV
								     [i +
								      1] << 8);
								Value +=
								    (IVEIV
								     [i +
								      2] << 16);
								Value +=
								    (IVEIV
								     [i +
								      3] << 24);
								RTUSBWriteMACRegister
								    (pAd,
								     offset + i,
								     Value);
								RTUSBWriteMACRegister
								    (pAd,
								     offset2 +
								     i, Value);
								i += 4;
							}

							/* 2. WCID Attribute UDF:3, BSSIdx:3, Alg:3, Keytable:use share key, BSSIdx is 0 */
							WCIDAttri =
							    (pAd->
							     SharedKey[BSS0]
							     [KeyIdx].
							     CipherAlg << 1) |
							    SHAREDKEYTABLE;
							offset =
							    MAC_WCID_ATTRIBUTE_BASE
							    +
							    (BSS0Mcast_WCID *
							     HW_WCID_ATTRI_SIZE);
							DBGPRINT(RT_DEBUG_TRACE,
								 ("BSS0Mcast_WCID : offset = %x, WCIDAttri = %x\n",
								  offset,
								  WCIDAttri));
							RTUSBWriteMACRegister
							    (pAd, offset,
							     WCIDAttri);

						}
						AsicAddSharedKeyEntry(pAd, BSS0,
								      (u8)
								      KeyIdx,
								      CipherAlg,
								      pWepKey->
								      KeyMaterial,
								      NULL,
								      NULL);
						DBGPRINT(RT_DEBUG_TRACE,
							 ("CmdThread::OID_802_11_ADD_WEP (KeyIdx=%d, Len=%d-byte)\n",
							  KeyIdx,
							  pWepKey->KeyLength));
					}
				}
				break;

			case CMDTHREAD_802_11_COUNTER_MEASURE:
				break;

			case CMDTHREAD_SET_GROUP_KEY:
				WpaStaGroupKeySetting(pAd);
				break;

			case CMDTHREAD_SET_PAIRWISE_KEY:
				WpaStaPairwiseKeySetting(pAd);
				break;

			case CMDTHREAD_SET_PSM_BIT:
				{
					u16 *pPsm = (u16 *) pData;
					MlmeSetPsmBit(pAd, *pPsm);
				}
				break;
			case CMDTHREAD_FORCE_WAKE_UP:
				AsicForceWakeup(pAd, TRUE);
				break;

			default:
				DBGPRINT(RT_DEBUG_ERROR,
					 ("--> Control Thread !! ERROR !! Unknown(cmdqelmt->command=0x%x) !! \n",
					  cmdqelmt->command));
				break;
			}
		}

		if (cmdqelmt->CmdFromNdis == TRUE) {
			if (cmdqelmt->buffer != NULL)
				os_free_mem(pAd, cmdqelmt->buffer);
			os_free_mem(pAd, cmdqelmt);
		} else {
			if ((cmdqelmt->buffer != NULL)
			    && (cmdqelmt->bufferlength != 0))
				os_free_mem(pAd, cmdqelmt->buffer);
			os_free_mem(pAd, cmdqelmt);
		}
	}			/* end of while */
}

#endif /* RTMP_MAC_USB // */
