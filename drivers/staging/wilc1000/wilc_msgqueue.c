
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
int wilc_mq_create(struct message_queue *mq)
{
	spin_lock_init(&mq->lock);
	sema_init(&mq->sem, 0);
	mq->msg_list = NULL;
	mq->recv_count = 0;
	mq->exiting = false;
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
	while (pHandle->recv_count > 0) {
		up(&pHandle->sem);
		pHandle->recv_count--;
	}

	while (pHandle->msg_list) {
		struct message *pstrMessge = pHandle->msg_list->next;

		kfree(pHandle->msg_list);
		pHandle->msg_list = pstrMessge;
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
	if (!pHandle->msg_list) {
		pHandle->msg_list  = pstrMessage;
	} else {
		struct message *pstrTailMsg = pHandle->msg_list;

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
	pHandle->recv_count++;
	spin_unlock_irqrestore(&pHandle->lock, flags);

	down(&pHandle->sem);
	spin_lock_irqsave(&pHandle->lock, flags);

	pstrMessage = pHandle->msg_list;
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
	pHandle->recv_count--;
	memcpy(pvRecvBuffer, pstrMessage->buf, pstrMessage->len);
	*pu32ReceivedLength = pstrMessage->len;

	pHandle->msg_list = pstrMessage->next;

	kfree(pstrMessage->buf);
	kfree(pstrMessage);

	spin_unlock_irqrestore(&pHandle->lock, flags);

	return 0;
}
