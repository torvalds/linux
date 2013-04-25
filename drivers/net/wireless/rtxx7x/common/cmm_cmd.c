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




/*
	========================================================================
	
	Routine Description:

	Arguments:

	Return Value:
	
	IRQL = 
	
	Note:
	
	========================================================================
*/
VOID	RTInitializeCmdQ(
	IN	PCmdQ	cmdq)
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
VOID	RTThreadDequeueCmd(
	IN	PCmdQ		cmdq,
	OUT	PCmdQElmt	*pcmdqelmt)
{
	*pcmdqelmt = cmdq->head;
	
	if (*pcmdqelmt != NULL)
	{
		cmdq->head = cmdq->head->next;
		cmdq->size--;
		if (cmdq->size == 0)
			cmdq->tail = NULL;
	}
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
NDIS_STATUS RTEnqueueInternalCmd(
	IN PRTMP_ADAPTER	pAd,
	IN NDIS_OID			Oid,
	IN PVOID			pInformationBuffer,
	IN UINT32			InformationBufferLength)	
{
	NDIS_STATUS	status;
	PCmdQElmt	cmdqelmt = NULL;
	

	status = os_alloc_mem(pAd, (PUCHAR *)&cmdqelmt, sizeof(CmdQElmt));
	if ((status != NDIS_STATUS_SUCCESS) || (cmdqelmt == NULL))
		return (NDIS_STATUS_RESOURCES);
	NdisZeroMemory(cmdqelmt, sizeof(CmdQElmt));

	if(InformationBufferLength > 0)
	{
		status = os_alloc_mem(pAd, (PUCHAR *)&cmdqelmt->buffer, InformationBufferLength);
		if ((status != NDIS_STATUS_SUCCESS) || (cmdqelmt->buffer == NULL))
		{
			os_free_mem(pAd, cmdqelmt);
			return (NDIS_STATUS_RESOURCES);
		}
		else
		{
			NdisMoveMemory(cmdqelmt->buffer, pInformationBuffer, InformationBufferLength);
			cmdqelmt->bufferlength = InformationBufferLength;
		}
	}
	else
	{
		cmdqelmt->buffer = NULL;
		cmdqelmt->bufferlength = 0;
	}

	cmdqelmt->command = Oid;
	cmdqelmt->CmdFromNdis = FALSE;

	if (cmdqelmt != NULL)
	{
		NdisAcquireSpinLock(&pAd->CmdQLock);
		if (pAd->CmdQ.CmdQState & RTMP_TASK_CAN_DO_INSERT)
		{
			EnqueueCmd((&pAd->CmdQ), cmdqelmt);
			status = NDIS_STATUS_SUCCESS;
		}
		else
		{
			status = NDIS_STATUS_FAILURE;
		}
		NdisReleaseSpinLock(&pAd->CmdQLock);

		if (status == NDIS_STATUS_FAILURE)
		{
			if (cmdqelmt->buffer)
				os_free_mem(pAd, cmdqelmt->buffer);
			os_free_mem(pAd, cmdqelmt);
		}
		else
			RTCMDUp(&pAd->cmdQTask);
	}
	return(NDIS_STATUS_SUCCESS);
}
