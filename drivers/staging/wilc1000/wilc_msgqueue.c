
#include "wilc_msgqueue.h"
#include <linux/spinlock.h>
#include "linux_wlan_common.h"
#include <linux/errno.h>
#include <linux/slab.h>

/*!
 *  @author		syounan
 *  @date		1 Sep 2010
 *  @note		copied from FLO glue implementatuion
 *  @version		1.0
 */
int wilc_mq_create(WILC_MsgQueueHandle *pHandle)
{
	spin_lock_init(&pHandle->strCriticalSection);
	sema_init(&pHandle->hSem, 0);
	pHandle->pstrMessageList = NULL;
	pHandle->u32ReceiversCount = 0;
	pHandle->bExiting = false;
	return 0;
}

/*!
 *  @author		syounan
 *  @date		1 Sep 2010
 *  @note		copied from FLO glue implementatuion
 *  @version		1.0
 */
int wilc_mq_destroy(WILC_MsgQueueHandle *pHandle)
{
	pHandle->bExiting = true;

	/* Release any waiting receiver thread. */
	while (pHandle->u32ReceiversCount > 0) {
		up(&pHandle->hSem);
		pHandle->u32ReceiversCount--;
	}

	while (pHandle->pstrMessageList) {
		Message *pstrMessge = pHandle->pstrMessageList->pstrNext;

		kfree(pHandle->pstrMessageList);
		pHandle->pstrMessageList = pstrMessge;
	}

	return 0;
}

/*!
 *  @author		syounan
 *  @date		1 Sep 2010
 *  @note		copied from FLO glue implementatuion
 *  @version		1.0
 */
int wilc_mq_send(WILC_MsgQueueHandle *pHandle,
			     const void *pvSendBuffer, u32 u32SendBufferSize)
{
	unsigned long flags;
	Message *pstrMessage = NULL;

	if ((!pHandle) || (u32SendBufferSize == 0) || (!pvSendBuffer)) {
		PRINT_ER("pHandle or pvSendBuffer is null\n");
		return -EFAULT;
	}

	if (pHandle->bExiting) {
		PRINT_ER("pHandle fail\n");
		return -EFAULT;
	}

	/* construct a new message */
	pstrMessage = kmalloc(sizeof(Message), GFP_ATOMIC);
	if (!pstrMessage)
		return -ENOMEM;

	pstrMessage->u32Length = u32SendBufferSize;
	pstrMessage->pstrNext = NULL;
	pstrMessage->pvBuffer = kmemdup(pvSendBuffer, u32SendBufferSize,
					GFP_ATOMIC);
	if (!pstrMessage->pvBuffer) {
		kfree(pstrMessage);
		return -ENOMEM;
	}

	spin_lock_irqsave(&pHandle->strCriticalSection, flags);

	/* add it to the message queue */
	if (!pHandle->pstrMessageList) {
		pHandle->pstrMessageList  = pstrMessage;
	} else {
		Message *pstrTailMsg = pHandle->pstrMessageList;

		while (pstrTailMsg->pstrNext)
			pstrTailMsg = pstrTailMsg->pstrNext;

		pstrTailMsg->pstrNext = pstrMessage;
	}

	spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);

	up(&pHandle->hSem);

	return 0;
}

/*!
 *  @author		syounan
 *  @date		1 Sep 2010
 *  @note		copied from FLO glue implementatuion
 *  @version		1.0
 */
int wilc_mq_recv(WILC_MsgQueueHandle *pHandle,
			     void *pvRecvBuffer, u32 u32RecvBufferSize,
			     u32 *pu32ReceivedLength)
{
	Message *pstrMessage;
	unsigned long flags;

	if ((!pHandle) || (u32RecvBufferSize == 0)
	    || (!pvRecvBuffer) || (!pu32ReceivedLength)) {
		PRINT_ER("pHandle or pvRecvBuffer is null\n");
		return -EINVAL;
	}

	if (pHandle->bExiting) {
		PRINT_ER("pHandle fail\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&pHandle->strCriticalSection, flags);
	pHandle->u32ReceiversCount++;
	spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);

	down(&pHandle->hSem);

	if (pHandle->bExiting) {
		PRINT_ER("pHandle fail\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&pHandle->strCriticalSection, flags);

	pstrMessage = pHandle->pstrMessageList;
	if (!pstrMessage) {
		spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);
		PRINT_ER("pstrMessage is null\n");
		return -EFAULT;
	}
	/* check buffer size */
	if (u32RecvBufferSize < pstrMessage->u32Length)	{
		spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);
		up(&pHandle->hSem);
		PRINT_ER("u32RecvBufferSize overflow\n");
		return -EOVERFLOW;
	}

	/* consume the message */
	pHandle->u32ReceiversCount--;
	memcpy(pvRecvBuffer, pstrMessage->pvBuffer, pstrMessage->u32Length);
	*pu32ReceivedLength = pstrMessage->u32Length;

	pHandle->pstrMessageList = pstrMessage->pstrNext;

	kfree(pstrMessage->pvBuffer);
	kfree(pstrMessage);

	spin_unlock_irqrestore(&pHandle->strCriticalSection, flags);

	return 0;
}
