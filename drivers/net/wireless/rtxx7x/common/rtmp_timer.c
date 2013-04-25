/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
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
 *************************************************************************/


#include "rt_config.h"


BUILD_TIMER_FUNCTION(MlmePeriodicExec);
/*BUILD_TIMER_FUNCTION(MlmeRssiReportExec);*/
BUILD_TIMER_FUNCTION(AsicRxAntEvalTimeout);
BUILD_TIMER_FUNCTION(APSDPeriodicExec);
BUILD_TIMER_FUNCTION(EnqueueStartForPSKExec);
#ifdef CONFIG_STA_SUPPORT
#endif /* CONFIG_STA_SUPPORT */
#ifdef RTMP_MAC_USB
BUILD_TIMER_FUNCTION(BeaconUpdateExec);
#endif /* RTMP_MAC_USB */


#ifdef CONFIG_STA_SUPPORT
BUILD_TIMER_FUNCTION(BeaconTimeout);
BUILD_TIMER_FUNCTION(ScanTimeout);
BUILD_TIMER_FUNCTION(AuthTimeout);
BUILD_TIMER_FUNCTION(AssocTimeout);
BUILD_TIMER_FUNCTION(ReassocTimeout);
BUILD_TIMER_FUNCTION(DisassocTimeout);
BUILD_TIMER_FUNCTION(LinkDownExec);
BUILD_TIMER_FUNCTION(StaQuickResponeForRateUpExec);
BUILD_TIMER_FUNCTION(WpaDisassocApAndBlockAssoc);
#ifdef PCIE_PS_SUPPORT
BUILD_TIMER_FUNCTION(PsPollWakeExec);
BUILD_TIMER_FUNCTION(RadioOnExec);
#endif /* PCIE_PS_SUPPORT */
#ifdef QOS_DLS_SUPPORT
BUILD_TIMER_FUNCTION(DlsTimeoutAction);
#endif /* QOS_DLS_SUPPORT */



#ifdef RTMP_MAC_USB
BUILD_TIMER_FUNCTION(RtmpUsbStaAsicForceWakeupTimeout);
#endif /* RTMP_MAC_USB */


#endif /* CONFIG_STA_SUPPORT */






#ifdef RTMP_TIMER_TASK_SUPPORT
static void RtmpTimerQHandle(RTMP_ADAPTER *pAd)
{
/*#ifndef KTHREAD_SUPPORT*/
	int status;
/*#endif*/
	RALINK_TIMER_STRUCT	*pTimer;
	RTMP_TIMER_TASK_ENTRY	*pEntry;
	unsigned long	irqFlag;
	RTMP_OS_TASK *pTask;


	pTask = &pAd->timerTask;
	while(!RTMP_OS_TASK_IS_KILLED(pTask))
	{
		pTimer = NULL;

		if (RtmpOSTaskWait(pAd, pTask, &status) == FALSE)
		{
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS);
			break;
		}

		if (pAd->TimerQ.status == RTMP_TASK_STAT_STOPED)
			break;
		
		/* event happened.*/
		while(pAd->TimerQ.pQHead)
		{
			RTMP_INT_LOCK(&pAd->TimerQLock, irqFlag);
			pEntry = pAd->TimerQ.pQHead;
			if (pEntry)
			{
				pTimer = pEntry->pRaTimer;

				/* update pQHead*/
				pAd->TimerQ.pQHead = pEntry->pNext;
				if (pEntry == pAd->TimerQ.pQTail)
					pAd->TimerQ.pQTail = NULL;
			
				/* return this queue entry to timerQFreeList.*/
				pEntry->pNext = pAd->TimerQ.pQPollFreeList;
				pAd->TimerQ.pQPollFreeList = pEntry;
			}
			RTMP_INT_UNLOCK(&pAd->TimerQLock, irqFlag);

			if (pTimer)
			{
				if ((pTimer->handle != NULL) && (!pAd->PM_FlgSuspend))
					pTimer->handle(NULL, (PVOID) pTimer->cookie, NULL, pTimer);
				if ((pTimer->Repeat) && (pTimer->State == FALSE))
					RTMP_OS_Add_Timer(&pTimer->TimerObj, pTimer->TimerValue);
			}
		}
		
/*#ifndef KTHREAD_SUPPORT*/
		if (status != 0)
		{
			pAd->TimerQ.status = RTMP_TASK_STAT_STOPED;
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS);
			break;
		}
/*#endif*/
	}
}


INT RtmpTimerQThread(
	IN ULONG Context)
{
	RTMP_OS_TASK	*pTask;
	PRTMP_ADAPTER	pAd = NULL;


	pTask = (RTMP_OS_TASK *)Context;
	pAd = (PRTMP_ADAPTER)RTMP_OS_TASK_DATA_GET(pTask);

	if (pAd == NULL)
	{
		DBGPRINT(RT_DEBUG_ERROR,( "%s:: pAd is NULL!\n",__FUNCTION__));
		return 0;
	}	

	RtmpOSTaskCustomize(pTask);

	RtmpTimerQHandle(pAd);

	DBGPRINT(RT_DEBUG_TRACE,( "<---%s\n",__FUNCTION__));
	/* notify the exit routine that we're actually exiting now 
	 *
	 * complete()/wait_for_completion() is similar to up()/down(),
	 * except that complete() is safe in the case where the structure
	 * is getting deleted in a parallel mode of execution (i.e. just
	 * after the down() -- that's necessary for the thread-shutdown
	 * case.
	 *
	 * complete_and_exit() goes even further than this -- it is safe in
	 * the case that the thread of the caller is going away (not just
	 * the structure) -- this is necessary for the module-remove case.
	 * This is important in preemption kernels, which transfer the flow
	 * of execution immediately upon a complete().
	 */
	RtmpOSTaskNotifyToExit(pTask);
	
	return 0;

}


RTMP_TIMER_TASK_ENTRY *RtmpTimerQInsert(
	IN RTMP_ADAPTER *pAd, 
	IN RALINK_TIMER_STRUCT *pTimer)
{
	RTMP_TIMER_TASK_ENTRY *pQNode = NULL, *pQTail;
	unsigned long irqFlags;
	RTMP_OS_TASK	*pTask = &pAd->timerTask;

	RTMP_INT_LOCK(&pAd->TimerQLock, irqFlags);
	if (pAd->TimerQ.status & RTMP_TASK_CAN_DO_INSERT)
	{
		if(pAd->TimerQ.pQPollFreeList)
		{
			pQNode = pAd->TimerQ.pQPollFreeList;
			pAd->TimerQ.pQPollFreeList = pQNode->pNext;

			pQNode->pRaTimer = pTimer;
			pQNode->pNext = NULL;

			pQTail = pAd->TimerQ.pQTail;
			if (pAd->TimerQ.pQTail != NULL)
				pQTail->pNext = pQNode;
			pAd->TimerQ.pQTail = pQNode;
			if (pAd->TimerQ.pQHead == NULL)
				pAd->TimerQ.pQHead = pQNode;
		}
	}
	RTMP_INT_UNLOCK(&pAd->TimerQLock, irqFlags);

	if (pQNode)
	{
		RTMP_OS_TASK_WAKE_UP(pTask);
	}

	return pQNode;
}


BOOLEAN RtmpTimerQRemove(
	IN RTMP_ADAPTER *pAd, 
	IN RALINK_TIMER_STRUCT *pTimer)
{
	RTMP_TIMER_TASK_ENTRY *pNode, *pPrev = NULL;
	unsigned long irqFlags;

	RTMP_INT_LOCK(&pAd->TimerQLock, irqFlags);
	if (pAd->TimerQ.status >= RTMP_TASK_STAT_INITED)
	{
		pNode = pAd->TimerQ.pQHead;
		while (pNode)
		{
			if (pNode->pRaTimer == pTimer)
				break;
			pPrev = pNode;
			pNode = pNode->pNext;
		}

		/* Now move it to freeList queue.*/
		if (pNode)
		{	
			if (pNode == pAd->TimerQ.pQHead)
				pAd->TimerQ.pQHead = pNode->pNext;
			if (pNode == pAd->TimerQ.pQTail)
				pAd->TimerQ.pQTail = pPrev;
			if (pPrev != NULL)
				pPrev->pNext = pNode->pNext;
			
			/* return this queue entry to timerQFreeList.*/
			pNode->pNext = pAd->TimerQ.pQPollFreeList;
			pAd->TimerQ.pQPollFreeList = pNode;
		}
	}
	RTMP_INT_UNLOCK(&pAd->TimerQLock, irqFlags);
			
	return TRUE;
}


void RtmpTimerQExit(RTMP_ADAPTER *pAd)
{
	RTMP_TIMER_TASK_ENTRY *pTimerQ;
	unsigned long irqFlags;
	
	RTMP_INT_LOCK(&pAd->TimerQLock, irqFlags);
	while (pAd->TimerQ.pQHead)
	{
		pTimerQ = pAd->TimerQ.pQHead;
		pAd->TimerQ.pQHead = pTimerQ->pNext;
		/* remove the timeQ*/
	}
	pAd->TimerQ.pQPollFreeList = NULL;
	os_free_mem(pAd, pAd->TimerQ.pTimerQPoll);
	pAd->TimerQ.pQTail = NULL;
	pAd->TimerQ.pQHead = NULL;
/*#ifndef KTHREAD_SUPPORT*/
	pAd->TimerQ.status = RTMP_TASK_STAT_STOPED;
/*#endif*/
	RTMP_INT_UNLOCK(&pAd->TimerQLock, irqFlags);
	NdisFreeSpinLock(&pAd->TimerQLock);
}


void RtmpTimerQInit(RTMP_ADAPTER *pAd)
{
	int 	i;
	RTMP_TIMER_TASK_ENTRY *pQNode, *pEntry;
	unsigned long irqFlags;
	
	NdisAllocateSpinLock(pAd, &pAd->TimerQLock);
	
	NdisZeroMemory(&pAd->TimerQ, sizeof(pAd->TimerQ));

	os_alloc_mem(pAd, &pAd->TimerQ.pTimerQPoll, sizeof(RTMP_TIMER_TASK_ENTRY) * TIMER_QUEUE_SIZE_MAX);
	if (pAd->TimerQ.pTimerQPoll)
	{
		pEntry = NULL;
		pQNode = (RTMP_TIMER_TASK_ENTRY *)pAd->TimerQ.pTimerQPoll;
		NdisZeroMemory(pAd->TimerQ.pTimerQPoll, sizeof(RTMP_TIMER_TASK_ENTRY) * TIMER_QUEUE_SIZE_MAX);

		RTMP_INT_LOCK(&pAd->TimerQLock, irqFlags);
		for (i = 0 ;i <TIMER_QUEUE_SIZE_MAX; i++)
		{
			pQNode->pNext = pEntry;
			pEntry = pQNode;
			pQNode++;
		}
		pAd->TimerQ.pQPollFreeList = pEntry;
		pAd->TimerQ.pQHead = NULL;
		pAd->TimerQ.pQTail = NULL;
		pAd->TimerQ.status = RTMP_TASK_STAT_INITED;
		RTMP_INT_UNLOCK(&pAd->TimerQLock, irqFlags);
	}
}
#endif /* RTMP_TIMER_TASK_SUPPORT */

