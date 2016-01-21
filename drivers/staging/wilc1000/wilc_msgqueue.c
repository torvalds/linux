
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
int wilc_mq_destroy(struct message_queue *mq)
{
	mq->exiting = true;

	/* Release any waiting receiver thread. */
	while (mq->recv_count > 0) {
		up(&mq->sem);
		mq->recv_count--;
	}

	while (mq->msg_list) {
		struct message *msg = mq->msg_list->next;

		kfree(mq->msg_list);
		mq->msg_list = msg;
	}

	return 0;
}

/*!
 *  @author		syounan
 *  @date		1 Sep 2010
 *  @note		copied from FLO glue implementatuion
 *  @version		1.0
 */
int wilc_mq_send(struct message_queue *mq,
		 const void *send_buf, u32 send_buf_size)
{
	unsigned long flags;
	struct message *new_msg = NULL;

	if ((!mq) || (send_buf_size == 0) || (!send_buf)) {
		PRINT_ER("mq or send_buf is null\n");
		return -EFAULT;
	}

	if (mq->exiting) {
		PRINT_ER("mq fail\n");
		return -EFAULT;
	}

	/* construct a new message */
	new_msg = kmalloc(sizeof(struct message), GFP_ATOMIC);
	if (!new_msg)
		return -ENOMEM;

	new_msg->len = send_buf_size;
	new_msg->next = NULL;
	new_msg->buf = kmemdup(send_buf, send_buf_size, GFP_ATOMIC);
	if (!new_msg->buf) {
		kfree(new_msg);
		return -ENOMEM;
	}

	spin_lock_irqsave(&mq->lock, flags);

	/* add it to the message queue */
	if (!mq->msg_list) {
		mq->msg_list  = new_msg;
	} else {
		struct message *pstrTailMsg = mq->msg_list;

		while (pstrTailMsg->next)
			pstrTailMsg = pstrTailMsg->next;

		pstrTailMsg->next = new_msg;
	}

	spin_unlock_irqrestore(&mq->lock, flags);

	up(&mq->sem);

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
