
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
int wilc_mq_create(struct message_queue *pHandle)
{
	spin_lock_init(&pHandle->lock);
	sema_init(&pHandle->sem, 0);
	pHandle->pstrMessageList = NULL;
	pHandle->u32ReceiversCount = 0;
	pHandle->exiting = false;
	return 0;
}

/*!
 *  @author		syounan
 *  @date		1 Sep 2010
 *  @note		copied from FLO glue implementatuion
 *  @version		1.0
 */
int wilc_mq_destroy(struct message_queue *pHandle)
{
	pHandle->exiting = true;

	/* Release any waiting receiver thread. */
	while (pHandle->u32ReceiversCount > 0) {
		up(&pHandle->sem);
		pHandle->u32ReceiversCount--;
	}

	while (pHandle->pstrMessageList) {
		struct message *pstrMessge = pHandle->pstrMessageList->next;

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
int wilc_mq_send(struct message_queue *pHandle,
		 const void *pvSendBuffer, u32 u32SendBufferSize)
{
	unsigned long flags;
	struct message *pstrMessage = NULL;

	if ((!pHandle) || (u32SendBufferSize == 0) || (!pvSendBuffer)) {
		PRINT_ER("pHandle or pvSendBuffer is null\n");
		return -EFAULT;
	}

	if (pHandle->exiting) {
		PRINT_ER("pHandle fail\n");
		return -EFAULT;
	}

	/* construct a new message */
	pstrMessage = kmalloc(sizeof(struct message), GFP_ATOMIC);
	if (!pstrMessage)
		return -ENOMEM;

	pstrMessage->len = u32SendBufferSize;
	pstrMessage->next = NULL;
	pstrMessage->buf = kmemdup(pvSendBuffer, u32SendBufferSize,
				   GFP_ATOMIC);
	if (!pstrMessage->buf) {
		kfree(pstrMessage);
		return -ENOMEM;
	}

	spin_lock_irqsave(&pHandle->lock, flags);

	/* add it to the message queue */
	if (!pHandle->pstrMessageList) {
		pHandle->pstrMessageList  = pstrMessage;
	} else {
		struct message *pstrTailMsg = pHandle->pstrMessageList;

		while (pstrTailMsg->next)
			pstrTailMsg = pstrTailMsg->next;

		pstrTailMsg->next = pstrMessage;
	}

	spin_unlock_irqrestore(&pHandle->lock, flags);

	up(&pHandle->sem);

	return 0;
}

/*!
 *  @author		syounan
 *  @date		1 Sep 2010
 *  @note		copied from FLO glue implementatuion
 *  @version		1.0
 */
int wilc_mq_recv(struct message_queue *pHandle,
		 void *pvRecvBuffer, u32 u32RecvBufferSize,
		 u32 *pu32ReceivedLength)
{
	struct message *pstrMessage;
	unsigned long flags;

	if ((!pHandle) || (u32RecvBufferSize == 0)
	    || (!pvRecvBuffer) || (!pu32ReceivedLength)) {
		PRINT_ER("pHandle or pvRecvBuffer is null\n");
		return -EINVAL;
	}

	if (pHandle->exiting) {
		PRINT_ER("pHandle fail\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&pHandle->lock, flags);
	pHandle->u32ReceiversCount++;
	spin_unlock_irqrestore(&pHandle->lock, flags);

	down(&pHandle->sem);
	spin_lock_irqsave(&pHandle->lock, flags);

	pstrMessage = pHandle->pstrMessageList;
	if (!pstrMessage) {
		spin_unlock_irqrestore(&pHandle->lock, flags);
		PRINT_ER("pstrMessage is null\n");
		return -EFAULT;
	}
	/* check buffer size */
	if (u32RecvBufferSize < pstrMessage->len) {
		spin_unlock_irqrestore(&pHandle->lock, flags);
		up(&pHandle->sem);
		PRINT_ER("u32RecvBufferSize overflow\n");
		return -EOVERFLOW;
	}

	/* consume the message */
	pHandle->u32ReceiversCount--;
	memcpy(pvRecvBuffer, pstrMessage->buf, pstrMessage->len);
	*pu32ReceivedLength = pstrMessage->len;

	pHandle->pstrMessageList = pstrMessage->next;

	kfree(pstrMessage->buf);
	kfree(pstrMessage);

	spin_unlock_irqrestore(&pHandle->lock, flags);

	return 0;
}
