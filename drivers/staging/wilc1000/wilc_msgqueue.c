
#include "wilc_msgqueue.h"
#include <linux/spinlock.h>

/*!
 *  @author		syounan
 *  @date		1 Sep 2010
 *  @note		copied from FLO glue implementatuion
 *  @version		1.0
 */
WILC_ErrNo WILC_MsgQueueCreate(WILC_MsgQueueHandle *pHandle,
			       tstrWILC_MsgQueueAttrs *pstrAttrs)
{
	spin_lock_init(&pHandle->strCriticalSection);
	sema_init(&pHandle->hSem, 0);
	pHandle->pstrMessageList = NULL;
	pHandle->u32ReceiversCount = 0;
	pHandle->bExiting = false;
	return WILC_SUCCESS;
}

/*!
 *  @author		syounan
 *  @date		1 Sep 2010
 *  @note		copied from FLO glue implementatuion
 *  @version		1.0
 */
WILC_ErrNo WILC_MsgQueueDestroy(WILC_MsgQueueHandle *pHandle,
				tstrWILC_MsgQueueAttrs *pstrAttrs)
{

	pHandle->bExiting = true;

	/* Release any waiting receiver thread. */
	while (pHandle->u32ReceiversCount > 0) {
		up(&(pHandle->hSem));
		pHandle->u32ReceiversCount--;
	}

	while (pHandle->pstrMessageList != NULL) {
		Message *pstrMessge = pHandle->pstrMessageList->pstrNext;
		WILC_FREE(pHandle->pstrMessageList);
		pHandle->pstrMessageList = pstrMessge;
	}

	return WILC_SUCCESS;
}

/*!
 *  @author		syounan
 *  @date		1 Sep 2010
 *  @note		copied from FLO glue implementatuion
 *  @version		1.0
 */
WILC_ErrNo WILC_MsgQueueSend(WILC_MsgQueueHandle *pHandle,
			     const void *pvSendBuffer, u32 u32SendBufferSize,
			     tstrWILC_MsgQueueAttrs *pstrAttrs)
{
	WILC_ErrNo s32RetStatus = WILC_SUCCESS;
	unsigned long flags;
	Message *pstrMessage = NULL;

	if ((pHandle == NULL) || (u32SendBufferSize == 0) || (pvSendBuffer == NULL)) {
		WILC_ERRORREPORT(s32RetStatus, WILC_INVALID_ARGUMENT);
	}

	if (pHandle->bExiting == true) {
		WILC_ERRORREPORT(s32RetStatus, WILC_FAIL);
	}

	spin_lock_irqsave(&pHandle->strCriticalSection, flags);

	/* construct a new message */
	pstrMessage = WILC_NEW(Message, 1);
	WILC_NULLCHECK(s32RetStatus, pstrMessage);
	pstrMessage->u32Length = u32SendBufferSize;
	pstrMessage->pstrNext = NULL;
	pstrMessage->pvBuffer = WILC_MALLOC(u32SendBufferSize);
	WILC_NULLCHECK(s32RetStatus, pstrMessage->pvBuffer);
	WILC_memcpy(pstrMessage->pvBuffer, pvSendBuffer, u32SendBufferSize);


	/* add it to the message queue */
	if (pHandle->pstrMessageList == NULL) {
		pHandle->pstrMessageList  = pstrMessage;
	} else {
		Message *pstrTailMsg = pHandle->pstrMessageList;
		while (pstrTailMsg->pstrNext != NULL) {
			pstrTailMsg = pstrTailMsg->pstrNext;
		}
		pstrTailMsg->pstrNext = pstrMessage;
	}

	spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);

	up(&pHandle->hSem);

	WILC_CATCH(s32RetStatus)
	{
		/* error occured, free any allocations */
		if (pstrMessage != NULL) {
			if (pstrMessage->pvBuffer != NULL) {
				WILC_FREE(pstrMessage->pvBuffer);
			}
			WILC_FREE(pstrMessage);
		}
	}

	return s32RetStatus;
}



/*!
 *  @author		syounan
 *  @date		1 Sep 2010
 *  @note		copied from FLO glue implementatuion
 *  @version		1.0
 */
WILC_ErrNo WILC_MsgQueueRecv(WILC_MsgQueueHandle *pHandle,
			     void *pvRecvBuffer, u32 u32RecvBufferSize,
			     u32 *pu32ReceivedLength,
			     tstrWILC_MsgQueueAttrs *pstrAttrs)
{

	Message *pstrMessage;
	WILC_ErrNo s32RetStatus = WILC_SUCCESS;
	unsigned long flags;
	if ((pHandle == NULL) || (u32RecvBufferSize == 0)
	    || (pvRecvBuffer == NULL) || (pu32ReceivedLength == NULL)) {
		WILC_ERRORREPORT(s32RetStatus, WILC_INVALID_ARGUMENT);
	}

	if (pHandle->bExiting == true) {
		WILC_ERRORREPORT(s32RetStatus, WILC_FAIL);
	}

	spin_lock_irqsave(&pHandle->strCriticalSection, flags);
	pHandle->u32ReceiversCount++;
	spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);

	down(&(pHandle->hSem));

	if (s32RetStatus == WILC_TIMEOUT) {
		/* timed out, just exit without consumeing the message */
		spin_lock_irqsave(&pHandle->strCriticalSection, flags);
		pHandle->u32ReceiversCount--;
		spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);
	} else {
		/* other non-timeout scenarios */
		WILC_ERRORCHECK(s32RetStatus);

		if (pHandle->bExiting) {
			WILC_ERRORREPORT(s32RetStatus, WILC_FAIL);
		}

		spin_lock_irqsave(&pHandle->strCriticalSection, flags);

		pstrMessage = pHandle->pstrMessageList;
		if (pstrMessage == NULL) {
			spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);
			WILC_ERRORREPORT(s32RetStatus, WILC_FAIL);
		}
		/* check buffer size */
		if (u32RecvBufferSize < pstrMessage->u32Length)	{
			spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);
			up(&pHandle->hSem);
			WILC_ERRORREPORT(s32RetStatus, WILC_BUFFER_OVERFLOW);
		}

		/* consume the message */
		pHandle->u32ReceiversCount--;
		WILC_memcpy(pvRecvBuffer, pstrMessage->pvBuffer, pstrMessage->u32Length);
		*pu32ReceivedLength = pstrMessage->u32Length;

		pHandle->pstrMessageList = pstrMessage->pstrNext;

		WILC_FREE(pstrMessage->pvBuffer);
		WILC_FREE(pstrMessage);

		spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);

	}

	WILC_CATCH(s32RetStatus)
	{
	}

	return s32RetStatus;
}
