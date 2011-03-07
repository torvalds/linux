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
	rtusb_bulk.c

	Abstract:

	Revision History:
	Who			When		What
	--------	----------	----------------------------------------------
	Name			Date		Modification logs
	Justin P. Mattock	11/07/2010	Fix some typos.

*/

#include "rt_config.h"

void dump_urb(struct urb *purb)
{
	printk(KERN_DEBUG "urb                  :0x%08lx\n", (unsigned long)purb);
	printk(KERN_DEBUG "\tdev                   :0x%08lx\n", (unsigned long)purb->dev);
	printk(KERN_DEBUG "\t\tdev->state          :0x%d\n", purb->dev->state);
	printk(KERN_DEBUG "\tpipe                  :0x%08x\n", purb->pipe);
	printk(KERN_DEBUG "\tstatus                :%d\n", purb->status);
	printk(KERN_DEBUG "\ttransfer_flags        :0x%08x\n", purb->transfer_flags);
	printk(KERN_DEBUG "\ttransfer_buffer       :0x%08lx\n",
	       (unsigned long)purb->transfer_buffer);
	printk(KERN_DEBUG "\ttransfer_buffer_length:%d\n", purb->transfer_buffer_length);
	printk(KERN_DEBUG "\tactual_length         :%d\n", purb->actual_length);
	printk(KERN_DEBUG "\tsetup_packet          :0x%08lx\n",
	       (unsigned long)purb->setup_packet);
	printk(KERN_DEBUG "\tstart_frame           :%d\n", purb->start_frame);
	printk(KERN_DEBUG "\tnumber_of_packets     :%d\n", purb->number_of_packets);
	printk(KERN_DEBUG "\tinterval              :%d\n", purb->interval);
	printk(KERN_DEBUG "\terror_count           :%d\n", purb->error_count);
	printk(KERN_DEBUG "\tcontext               :0x%08lx\n",
	       (unsigned long)purb->context);
	printk(KERN_DEBUG "\tcomplete              :0x%08lx\n\n",
	       (unsigned long)purb->complete);
}

/*
========================================================================
Routine Description:
    Create kernel threads & tasklets.

Arguments:
    *net_dev			Pointer to wireless net device interface

Return Value:
	NDIS_STATUS_SUCCESS
	NDIS_STATUS_FAILURE

Note:
========================================================================
*/
int RtmpMgmtTaskInit(struct rt_rtmp_adapter *pAd)
{
	struct rt_rtmp_os_task *pTask;
	int status;

	/*
	   Creat TimerQ Thread, We need init timerQ related structure before create the timer thread.
	 */
	RtmpTimerQInit(pAd);

	pTask = &pAd->timerTask;
	RtmpOSTaskInit(pTask, "RtmpTimerTask", pAd);
	status = RtmpOSTaskAttach(pTask, RtmpTimerQThread, pTask);
	if (status == NDIS_STATUS_FAILURE) {
		printk(KERN_WARNING "%s: unable to start RtmpTimerQThread\n",
		       RTMP_OS_NETDEV_GET_DEVNAME(pAd->net_dev));
		return NDIS_STATUS_FAILURE;
	}

	/* Creat MLME Thread */
	pTask = &pAd->mlmeTask;
	RtmpOSTaskInit(pTask, "RtmpMlmeTask", pAd);
	status = RtmpOSTaskAttach(pTask, MlmeThread, pTask);
	if (status == NDIS_STATUS_FAILURE) {
		printk(KERN_WARNING "%s: unable to start MlmeThread\n",
		       RTMP_OS_NETDEV_GET_DEVNAME(pAd->net_dev));
		return NDIS_STATUS_FAILURE;
	}

	/* Creat Command Thread */
	pTask = &pAd->cmdQTask;
	RtmpOSTaskInit(pTask, "RtmpCmdQTask", pAd);
	status = RtmpOSTaskAttach(pTask, RTUSBCmdThread, pTask);
	if (status == NDIS_STATUS_FAILURE) {
		printk(KERN_WARNING "%s: unable to start RTUSBCmdThread\n",
		       RTMP_OS_NETDEV_GET_DEVNAME(pAd->net_dev));
		return NDIS_STATUS_FAILURE;
	}

	return NDIS_STATUS_SUCCESS;
}

/*
========================================================================
Routine Description:
    Close kernel threads.

Arguments:
	*pAd				the raxx interface data pointer

Return Value:
    NONE

Note:
========================================================================
*/
void RtmpMgmtTaskExit(struct rt_rtmp_adapter *pAd)
{
	int ret;
	struct rt_rtmp_os_task *pTask;

	/* Sleep 50 milliseconds so pending io might finish normally */
	RTMPusecDelay(50000);

	/* We want to wait until all pending receives and sends to the */
	/* device object. We cancel any */
	/* irps. Wait until sends and receives have stopped. */
	RTUSBCancelPendingIRPs(pAd);

	/* We need clear timerQ related structure before exits of the timer thread. */
	RtmpTimerQExit(pAd);

	/* Terminate Mlme Thread */
	pTask = &pAd->mlmeTask;
	ret = RtmpOSTaskKill(pTask);
	if (ret == NDIS_STATUS_FAILURE) {
		DBGPRINT(RT_DEBUG_ERROR, ("%s: kill task(%s) failed!\n",
					  RTMP_OS_NETDEV_GET_DEVNAME(pAd->
								     net_dev),
					  pTask->taskName));
	}

	/* Terminate cmdQ thread */
	pTask = &pAd->cmdQTask;
#ifdef KTHREAD_SUPPORT
	if (pTask->kthread_task)
#else
	CHECK_PID_LEGALITY(pTask->taskPID)
#endif
	{
		mb();
		NdisAcquireSpinLock(&pAd->CmdQLock);
		pAd->CmdQ.CmdQState = RTMP_TASK_STAT_STOPED;
		NdisReleaseSpinLock(&pAd->CmdQLock);
		mb();
		/*RTUSBCMDUp(pAd); */
		ret = RtmpOSTaskKill(pTask);
		if (ret == NDIS_STATUS_FAILURE) {
			DBGPRINT(RT_DEBUG_ERROR, ("%s: kill task(%s) failed!\n",
						  RTMP_OS_NETDEV_GET_DEVNAME
						  (pAd->net_dev),
						  pTask->taskName));
		}
		pAd->CmdQ.CmdQState = RTMP_TASK_STAT_UNKNOWN;
	}

	/* Terminate timer thread */
	pTask = &pAd->timerTask;
	ret = RtmpOSTaskKill(pTask);
	if (ret == NDIS_STATUS_FAILURE) {
		DBGPRINT(RT_DEBUG_ERROR, ("%s: kill task(%s) failed!\n",
					  RTMP_OS_NETDEV_GET_DEVNAME(pAd->
								     net_dev),
					  pTask->taskName));
	}

}

static void rtusb_dataout_complete(unsigned long data)
{
	struct rt_rtmp_adapter *pAd;
	struct urb *pUrb;
	struct os_cookie *pObj;
	struct rt_ht_tx_context *pHTTXContext;
	u8 BulkOutPipeId;
	int Status;
	unsigned long IrqFlags;

	pUrb = (struct urb *)data;
	pHTTXContext = (struct rt_ht_tx_context *)pUrb->context;
	pAd = pHTTXContext->pAd;
	pObj = (struct os_cookie *)pAd->OS_Cookie;
	Status = pUrb->status;

	/* Store BulkOut PipeId */
	BulkOutPipeId = pHTTXContext->BulkOutPipeId;
	pAd->BulkOutDataOneSecCount++;

	/*DBGPRINT(RT_DEBUG_LOUD, ("Done-B(%d):I=0x%lx, CWPos=%ld, NBPos=%ld, ENBPos=%ld, bCopy=%d!\n", BulkOutPipeId, in_interrupt(), pHTTXContext->CurWritePosition, */
	/*              pHTTXContext->NextBulkOutPosition, pHTTXContext->ENextBulkOutPosition, pHTTXContext->bCopySavePad)); */

	RTMP_IRQ_LOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);
	pAd->BulkOutPending[BulkOutPipeId] = FALSE;
	pHTTXContext->IRPPending = FALSE;
	pAd->watchDogTxPendingCnt[BulkOutPipeId] = 0;

	if (Status == USB_ST_NOERROR) {
		pAd->BulkOutComplete++;

		RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);

		pAd->Counters8023.GoodTransmits++;
		/*RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags); */
		FREE_HTTX_RING(pAd, BulkOutPipeId, pHTTXContext);
		/*RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags); */

	} else {		/* STATUS_OTHER */
		u8 *pBuf;

		pAd->BulkOutCompleteOther++;

		pBuf =
		    &pHTTXContext->TransferBuffer->field.
		    WirelessPacket[pHTTXContext->NextBulkOutPosition];

		if (!RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
					  fRTMP_ADAPTER_HALT_IN_PROGRESS |
					  fRTMP_ADAPTER_NIC_NOT_EXIST |
					  fRTMP_ADAPTER_BULKOUT_RESET))) {
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET);
			pAd->bulkResetPipeid = BulkOutPipeId;
			pAd->bulkResetReq[BulkOutPipeId] = pAd->BulkOutReq;
		}
		RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[BulkOutPipeId], IrqFlags);

		DBGPRINT_RAW(RT_DEBUG_ERROR,
			     ("BulkOutDataPacket failed: ReasonCode=%d!\n",
			      Status));
		DBGPRINT_RAW(RT_DEBUG_ERROR,
			     ("\t>>BulkOut Req=0x%lx, Complete=0x%lx, Other=0x%lx\n",
			      pAd->BulkOutReq, pAd->BulkOutComplete,
			      pAd->BulkOutCompleteOther));
		DBGPRINT_RAW(RT_DEBUG_ERROR,
			     ("\t>>BulkOut Header:%x %x %x %x %x %x %x %x\n",
			      pBuf[0], pBuf[1], pBuf[2], pBuf[3], pBuf[4],
			      pBuf[5], pBuf[6], pBuf[7]));
		/*DBGPRINT_RAW(RT_DEBUG_ERROR, (">>BulkOutCompleteCancel=0x%x, BulkOutCompleteOther=0x%x\n", pAd->BulkOutCompleteCancel, pAd->BulkOutCompleteOther)); */

	}

	/* */
	/* bInUse = TRUE, means some process are filling TX data, after that must turn on bWaitingBulkOut */
	/* bWaitingBulkOut = TRUE, means the TX data are waiting for bulk out. */
	/* */
	/*RTMP_IRQ_LOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags); */
	if ((pHTTXContext->ENextBulkOutPosition !=
	     pHTTXContext->CurWritePosition)
	    && (pHTTXContext->ENextBulkOutPosition !=
		(pHTTXContext->CurWritePosition + 8))
	    && !RTUSB_TEST_BULK_FLAG(pAd,
				     (fRTUSB_BULK_OUT_DATA_FRAG <<
				      BulkOutPipeId))) {
		/* Indicate There is data available */
		RTUSB_SET_BULK_FLAG(pAd,
				    (fRTUSB_BULK_OUT_DATA_NORMAL <<
				     BulkOutPipeId));
	}
	/*RTMP_IRQ_UNLOCK(&pAd->TxContextQueueLock[BulkOutPipeId], IrqFlags); */

	/* Always call Bulk routine, even reset bulk. */
	/* The protection of rest bulk should be in BulkOut routine */
	RTUSBKickBulkOut(pAd);
}

static void rtusb_null_frame_done_tasklet(unsigned long data)
{
	struct rt_rtmp_adapter *pAd;
	struct rt_tx_context *pNullContext;
	struct urb *pUrb;
	int Status;
	unsigned long irqFlag;

	pUrb = (struct urb *)data;
	pNullContext = (struct rt_tx_context *)pUrb->context;
	pAd = pNullContext->pAd;
	Status = pUrb->status;

	/* Reset Null frame context flags */
	RTMP_IRQ_LOCK(&pAd->BulkOutLock[0], irqFlag);
	pNullContext->IRPPending = FALSE;
	pNullContext->InUse = FALSE;
	pAd->BulkOutPending[0] = FALSE;
	pAd->watchDogTxPendingCnt[0] = 0;

	if (Status == USB_ST_NOERROR) {
		RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], irqFlag);

		RTMPDeQueuePacket(pAd, FALSE, NUM_OF_TX_RING, MAX_TX_PROCESS);
	} else {		/* STATUS_OTHER */
		if ((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS)) &&
		    (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS)) &&
		    (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) &&
		    (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET))) {
			DBGPRINT_RAW(RT_DEBUG_ERROR,
				     ("Bulk Out Null Frame Failed, ReasonCode=%d!\n",
				      Status));
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET);
			pAd->bulkResetPipeid =
			    (MGMTPIPEIDX | BULKOUT_MGMT_RESET_FLAG);
			RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], irqFlag);
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT,
						NULL, 0);
		} else {
			RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], irqFlag);
		}
	}

	/* Always call Bulk routine, even reset bulk. */
	/* The protection of rest bulk should be in BulkOut routine */
	RTUSBKickBulkOut(pAd);
}

static void rtusb_rts_frame_done_tasklet(unsigned long data)
{
	struct rt_rtmp_adapter *pAd;
	struct rt_tx_context *pRTSContext;
	struct urb *pUrb;
	int Status;
	unsigned long irqFlag;

	pUrb = (struct urb *)data;
	pRTSContext = (struct rt_tx_context *)pUrb->context;
	pAd = pRTSContext->pAd;
	Status = pUrb->status;

	/* Reset RTS frame context flags */
	RTMP_IRQ_LOCK(&pAd->BulkOutLock[0], irqFlag);
	pRTSContext->IRPPending = FALSE;
	pRTSContext->InUse = FALSE;

	if (Status == USB_ST_NOERROR) {
		RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], irqFlag);
		RTMPDeQueuePacket(pAd, FALSE, NUM_OF_TX_RING, MAX_TX_PROCESS);
	} else {		/* STATUS_OTHER */
		if ((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS)) &&
		    (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS)) &&
		    (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) &&
		    (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET))) {
			DBGPRINT_RAW(RT_DEBUG_ERROR,
				     ("Bulk Out RTS Frame Failed\n"));
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET);
			pAd->bulkResetPipeid =
			    (MGMTPIPEIDX | BULKOUT_MGMT_RESET_FLAG);
			RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], irqFlag);
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT,
						NULL, 0);
		} else {
			RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[0], irqFlag);
		}
	}

	RTMP_SEM_LOCK(&pAd->BulkOutLock[pRTSContext->BulkOutPipeId]);
	pAd->BulkOutPending[pRTSContext->BulkOutPipeId] = FALSE;
	RTMP_SEM_UNLOCK(&pAd->BulkOutLock[pRTSContext->BulkOutPipeId]);

	/* Always call Bulk routine, even reset bulk. */
	/* The protection of rest bulk should be in BulkOut routine */
	RTUSBKickBulkOut(pAd);

}

static void rtusb_pspoll_frame_done_tasklet(unsigned long data)
{
	struct rt_rtmp_adapter *pAd;
	struct rt_tx_context *pPsPollContext;
	struct urb *pUrb;
	int Status;

	pUrb = (struct urb *)data;
	pPsPollContext = (struct rt_tx_context *)pUrb->context;
	pAd = pPsPollContext->pAd;
	Status = pUrb->status;

	/* Reset PsPoll context flags */
	pPsPollContext->IRPPending = FALSE;
	pPsPollContext->InUse = FALSE;
	pAd->watchDogTxPendingCnt[0] = 0;

	if (Status == USB_ST_NOERROR) {
		RTMPDeQueuePacket(pAd, FALSE, NUM_OF_TX_RING, MAX_TX_PROCESS);
	} else {		/* STATUS_OTHER */
		if ((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS)) &&
		    (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS)) &&
		    (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) &&
		    (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET))) {
			DBGPRINT_RAW(RT_DEBUG_ERROR,
				     ("Bulk Out PSPoll Failed\n"));
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET);
			pAd->bulkResetPipeid =
			    (MGMTPIPEIDX | BULKOUT_MGMT_RESET_FLAG);
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT,
						NULL, 0);
		}
	}

	RTMP_SEM_LOCK(&pAd->BulkOutLock[0]);
	pAd->BulkOutPending[0] = FALSE;
	RTMP_SEM_UNLOCK(&pAd->BulkOutLock[0]);

	/* Always call Bulk routine, even reset bulk. */
	/* The protection of rest bulk should be in BulkOut routine */
	RTUSBKickBulkOut(pAd);

}

/*
========================================================================
Routine Description:
    Handle received packets.

Arguments:
	data				- URB information pointer

Return Value:
    None

Note:
========================================================================
*/
static void rx_done_tasklet(unsigned long data)
{
	struct urb *pUrb;
	struct rt_rx_context *pRxContext;
	struct rt_rtmp_adapter *pAd;
	int Status;
	unsigned int IrqFlags;

	pUrb = (struct urb *)data;
	pRxContext = (struct rt_rx_context *)pUrb->context;
	pAd = pRxContext->pAd;
	Status = pUrb->status;

	RTMP_IRQ_LOCK(&pAd->BulkInLock, IrqFlags);
	pRxContext->InUse = FALSE;
	pRxContext->IRPPending = FALSE;
	pRxContext->BulkInOffset += pUrb->actual_length;
	/*NdisInterlockedDecrement(&pAd->PendingRx); */
	pAd->PendingRx--;

	if (Status == USB_ST_NOERROR) {
		pAd->BulkInComplete++;
		pAd->NextRxBulkInPosition = 0;
		if (pRxContext->BulkInOffset) { /* As jan's comment, it may bulk-in success but size is zero. */
			pRxContext->Readable = TRUE;
			INC_RING_INDEX(pAd->NextRxBulkInIndex, RX_RING_SIZE);
		}
		RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);
	} else {			/* STATUS_OTHER */
		pAd->BulkInCompleteFail++;
		/* Still read this packet although it may comtain wrong bytes. */
		pRxContext->Readable = FALSE;
		RTMP_IRQ_UNLOCK(&pAd->BulkInLock, IrqFlags);

		/* Parsing all packets. because after reset, the index will reset to all zero. */
		if ((!RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
					   fRTMP_ADAPTER_BULKIN_RESET |
					   fRTMP_ADAPTER_HALT_IN_PROGRESS |
					   fRTMP_ADAPTER_NIC_NOT_EXIST)))) {

			DBGPRINT_RAW(RT_DEBUG_ERROR,
				     ("Bulk In Failed. Status=%d, BIIdx=0x%x, BIRIdx=0x%x, actual_length= 0x%x\n",
				      Status, pAd->NextRxBulkInIndex,
				      pAd->NextRxBulkInReadIndex,
				      pRxContext->pUrb->actual_length));

			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKIN_RESET);
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_IN,
						NULL, 0);
		}
	}

	ASSERT((pRxContext->InUse == pRxContext->IRPPending));

	RTUSBBulkReceive(pAd);

	return;

}

static void rtusb_mgmt_dma_done_tasklet(unsigned long data)
{
	struct rt_rtmp_adapter *pAd;
	struct rt_tx_context *pMLMEContext;
	int index;
	void *pPacket;
	struct urb *pUrb;
	int Status;
	unsigned long IrqFlags;

	pUrb = (struct urb *)data;
	pMLMEContext = (struct rt_tx_context *)pUrb->context;
	pAd = pMLMEContext->pAd;
	Status = pUrb->status;
	index = pMLMEContext->SelfIdx;

	ASSERT((pAd->MgmtRing.TxDmaIdx == index));

	RTMP_IRQ_LOCK(&pAd->BulkOutLock[MGMTPIPEIDX], IrqFlags);

	if (Status != USB_ST_NOERROR) {
		/*Bulk-Out fail status handle */
		if ((!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_RESET_IN_PROGRESS)) &&
		    (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS)) &&
		    (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) &&
		    (!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET))) {
			DBGPRINT_RAW(RT_DEBUG_ERROR,
				     ("Bulk Out MLME Failed, Status=%d!\n",
				      Status));
			/* TODO: How to handle about the MLMEBulkOut failed issue. Need to resend the mgmt pkt? */
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET);
			pAd->bulkResetPipeid =
			    (MGMTPIPEIDX | BULKOUT_MGMT_RESET_FLAG);
		}
	}

	pAd->BulkOutPending[MGMTPIPEIDX] = FALSE;
	RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[MGMTPIPEIDX], IrqFlags);

	RTMP_IRQ_LOCK(&pAd->MLMEBulkOutLock, IrqFlags);
	/* Reset MLME context flags */
	pMLMEContext->IRPPending = FALSE;
	pMLMEContext->InUse = FALSE;
	pMLMEContext->bWaitingBulkOut = FALSE;
	pMLMEContext->BulkOutSize = 0;

	pPacket = pAd->MgmtRing.Cell[index].pNdisPacket;
	pAd->MgmtRing.Cell[index].pNdisPacket = NULL;

	/* Increase MgmtRing Index */
	INC_RING_INDEX(pAd->MgmtRing.TxDmaIdx, MGMT_RING_SIZE);
	pAd->MgmtRing.TxSwFreeIdx++;
	RTMP_IRQ_UNLOCK(&pAd->MLMEBulkOutLock, IrqFlags);

	/* No-matter success or fail, we free the mgmt packet. */
	if (pPacket)
		RTMPFreeNdisPacket(pAd, pPacket);

	if ((RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
				  fRTMP_ADAPTER_HALT_IN_PROGRESS |
				  fRTMP_ADAPTER_NIC_NOT_EXIST)))) {
		/* do nothing and return directly. */
	} else {
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET) && ((pAd->bulkResetPipeid & BULKOUT_MGMT_RESET_FLAG) == BULKOUT_MGMT_RESET_FLAG)) {	/* For Mgmt Bulk-Out failed, ignore it now. */
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT,
						NULL, 0);
		} else {

			/* Always call Bulk routine, even reset bulk. */
			/* The protection of rest bulk should be in BulkOut routine */
			if (pAd->MgmtRing.TxSwFreeIdx <
			    MGMT_RING_SIZE
			    /* pMLMEContext->bWaitingBulkOut == TRUE */) {
				RTUSB_SET_BULK_FLAG(pAd, fRTUSB_BULK_OUT_MLME);
			}
			RTUSBKickBulkOut(pAd);
		}
	}

}

static void rtusb_ac3_dma_done_tasklet(unsigned long data)
{
	struct rt_rtmp_adapter *pAd;
	struct rt_ht_tx_context *pHTTXContext;
	u8 BulkOutPipeId = 3;
	struct urb *pUrb;

	pUrb = (struct urb *)data;
	pHTTXContext = (struct rt_ht_tx_context *)pUrb->context;
	pAd = pHTTXContext->pAd;

	rtusb_dataout_complete((unsigned long)pUrb);

	if ((RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
				  fRTMP_ADAPTER_HALT_IN_PROGRESS |
				  fRTMP_ADAPTER_NIC_NOT_EXIST)))) {
		/* do nothing and return directly. */
	} else {
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET)) {
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT,
						NULL, 0);
		} else {
			pHTTXContext = &pAd->TxContext[BulkOutPipeId];
			if ((pAd->TxSwQueue[BulkOutPipeId].Number > 0) &&
			    /*((pHTTXContext->CurWritePosition > (pHTTXContext->NextBulkOutPosition + 0x6000)) || (pHTTXContext->NextBulkOutPosition > pHTTXContext->CurWritePosition + 0x6000)) && */
			    (pAd->DeQueueRunning[BulkOutPipeId] == FALSE) &&
			    (pHTTXContext->bCurWriting == FALSE)) {
				RTMPDeQueuePacket(pAd, FALSE, BulkOutPipeId,
						  MAX_TX_PROCESS);
			}

			RTUSB_SET_BULK_FLAG(pAd,
					    fRTUSB_BULK_OUT_DATA_NORMAL << 3);
			RTUSBKickBulkOut(pAd);
		}
	}

	return;
}

static void rtusb_ac2_dma_done_tasklet(unsigned long data)
{
	struct rt_rtmp_adapter *pAd;
	struct rt_ht_tx_context *pHTTXContext;
	u8 BulkOutPipeId = 2;
	struct urb *pUrb;

	pUrb = (struct urb *)data;
	pHTTXContext = (struct rt_ht_tx_context *)pUrb->context;
	pAd = pHTTXContext->pAd;

	rtusb_dataout_complete((unsigned long)pUrb);

	if ((RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
				  fRTMP_ADAPTER_HALT_IN_PROGRESS |
				  fRTMP_ADAPTER_NIC_NOT_EXIST)))) {
		/* do nothing and return directly. */
	} else {
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET)) {
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT,
						NULL, 0);
		} else {
			pHTTXContext = &pAd->TxContext[BulkOutPipeId];
			if ((pAd->TxSwQueue[BulkOutPipeId].Number > 0) &&
			    /*((pHTTXContext->CurWritePosition > (pHTTXContext->NextBulkOutPosition + 0x6000)) || (pHTTXContext->NextBulkOutPosition > pHTTXContext->CurWritePosition + 0x6000)) && */
			    (pAd->DeQueueRunning[BulkOutPipeId] == FALSE) &&
			    (pHTTXContext->bCurWriting == FALSE)) {
				RTMPDeQueuePacket(pAd, FALSE, BulkOutPipeId,
						  MAX_TX_PROCESS);
			}

			RTUSB_SET_BULK_FLAG(pAd,
					    fRTUSB_BULK_OUT_DATA_NORMAL << 2);
			RTUSBKickBulkOut(pAd);
		}
	}

	return;
}

static void rtusb_ac1_dma_done_tasklet(unsigned long data)
{
	struct rt_rtmp_adapter *pAd;
	struct rt_ht_tx_context *pHTTXContext;
	u8 BulkOutPipeId = 1;
	struct urb *pUrb;

	pUrb = (struct urb *)data;
	pHTTXContext = (struct rt_ht_tx_context *)pUrb->context;
	pAd = pHTTXContext->pAd;

	rtusb_dataout_complete((unsigned long)pUrb);

	if ((RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
				  fRTMP_ADAPTER_HALT_IN_PROGRESS |
				  fRTMP_ADAPTER_NIC_NOT_EXIST)))) {
		/* do nothing and return directly. */
	} else {
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET)) {
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT,
						NULL, 0);
		} else {
			pHTTXContext = &pAd->TxContext[BulkOutPipeId];
			if ((pAd->TxSwQueue[BulkOutPipeId].Number > 0) &&
			    /*((pHTTXContext->CurWritePosition > (pHTTXContext->NextBulkOutPosition + 0x6000)) || (pHTTXContext->NextBulkOutPosition > pHTTXContext->CurWritePosition + 0x6000)) && */
			    (pAd->DeQueueRunning[BulkOutPipeId] == FALSE) &&
			    (pHTTXContext->bCurWriting == FALSE)) {
				RTMPDeQueuePacket(pAd, FALSE, BulkOutPipeId,
						  MAX_TX_PROCESS);
			}

			RTUSB_SET_BULK_FLAG(pAd,
					    fRTUSB_BULK_OUT_DATA_NORMAL << 1);
			RTUSBKickBulkOut(pAd);
		}
	}
	return;

}

static void rtusb_ac0_dma_done_tasklet(unsigned long data)
{
	struct rt_rtmp_adapter *pAd;
	struct rt_ht_tx_context *pHTTXContext;
	u8 BulkOutPipeId = 0;
	struct urb *pUrb;

	pUrb = (struct urb *)data;
	pHTTXContext = (struct rt_ht_tx_context *)pUrb->context;
	pAd = pHTTXContext->pAd;

	rtusb_dataout_complete((unsigned long)pUrb);

	if ((RTMP_TEST_FLAG(pAd, (fRTMP_ADAPTER_RESET_IN_PROGRESS |
				  fRTMP_ADAPTER_HALT_IN_PROGRESS |
				  fRTMP_ADAPTER_NIC_NOT_EXIST)))) {
		/* do nothing and return directly. */
	} else {
		if (RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_BULKOUT_RESET)) {
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_OUT,
						NULL, 0);
		} else {
			pHTTXContext = &pAd->TxContext[BulkOutPipeId];
			if ((pAd->TxSwQueue[BulkOutPipeId].Number > 0) &&
			    /*  ((pHTTXContext->CurWritePosition > (pHTTXContext->NextBulkOutPosition + 0x6000)) || (pHTTXContext->NextBulkOutPosition > pHTTXContext->CurWritePosition + 0x6000)) && */
			    (pAd->DeQueueRunning[BulkOutPipeId] == FALSE) &&
			    (pHTTXContext->bCurWriting == FALSE)) {
				RTMPDeQueuePacket(pAd, FALSE, BulkOutPipeId,
						  MAX_TX_PROCESS);
			}

			RTUSB_SET_BULK_FLAG(pAd, fRTUSB_BULK_OUT_DATA_NORMAL);
			RTUSBKickBulkOut(pAd);
		}
	}

	return;

}

int RtmpNetTaskInit(struct rt_rtmp_adapter *pAd)
{
	struct os_cookie *pObj = (struct os_cookie *)pAd->OS_Cookie;

	/* Create receive tasklet */
	tasklet_init(&pObj->rx_done_task, rx_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->mgmt_dma_done_task, rtusb_mgmt_dma_done_tasklet,
		     (unsigned long)pAd);
	tasklet_init(&pObj->ac0_dma_done_task, rtusb_ac0_dma_done_tasklet,
		     (unsigned long)pAd);
	tasklet_init(&pObj->ac1_dma_done_task, rtusb_ac1_dma_done_tasklet,
		     (unsigned long)pAd);
	tasklet_init(&pObj->ac2_dma_done_task, rtusb_ac2_dma_done_tasklet,
		     (unsigned long)pAd);
	tasklet_init(&pObj->ac3_dma_done_task, rtusb_ac3_dma_done_tasklet,
		     (unsigned long)pAd);
	tasklet_init(&pObj->tbtt_task, tbtt_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->null_frame_complete_task,
		     rtusb_null_frame_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->rts_frame_complete_task,
		     rtusb_rts_frame_done_tasklet, (unsigned long)pAd);
	tasklet_init(&pObj->pspoll_frame_complete_task,
		     rtusb_pspoll_frame_done_tasklet, (unsigned long)pAd);

	return NDIS_STATUS_SUCCESS;
}

void RtmpNetTaskExit(struct rt_rtmp_adapter *pAd)
{
	struct os_cookie *pObj;

	pObj = (struct os_cookie *)pAd->OS_Cookie;

	tasklet_kill(&pObj->rx_done_task);
	tasklet_kill(&pObj->mgmt_dma_done_task);
	tasklet_kill(&pObj->ac0_dma_done_task);
	tasklet_kill(&pObj->ac1_dma_done_task);
	tasklet_kill(&pObj->ac2_dma_done_task);
	tasklet_kill(&pObj->ac3_dma_done_task);
	tasklet_kill(&pObj->tbtt_task);
	tasklet_kill(&pObj->null_frame_complete_task);
	tasklet_kill(&pObj->rts_frame_complete_task);
	tasklet_kill(&pObj->pspoll_frame_complete_task);
}
