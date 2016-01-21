
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
		return -EINVAL;
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
		struct message *tail_msg = mq->msg_list;

		while (tail_msg->next)
			tail_msg = tail_msg->next;

		tail_msg->next = new_msg;
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
int wilc_mq_recv(struct message_queue *mq,
		 void *recv_buf, u32 recv_buf_size,
		 u32 *pu32ReceivedLength)
{
	struct message *pstrMessage;
	unsigned long flags;

	if ((!mq) || (recv_buf_size == 0)
	    || (!recv_buf) || (!pu32ReceivedLength)) {
		PRINT_ER("mq or recv_buf is null\n");
		return -EINVAL;
	}

	if (mq->exiting) {
		PRINT_ER("mq fail\n");
		return -EFAULT;
	}

	spin_lock_irqsave(&mq->lock, flags);
	mq->recv_count++;
	spin_unlock_irqrestore(&mq->lock, flags);

	down(&mq->sem);
	spin_lock_irqsave(&mq->lock, flags);

	pstrMessage = mq->msg_list;
	if (!pstrMessage) {
		spin_unlock_irqrestore(&mq->lock, flags);
		PRINT_ER("pstrMessage is null\n");
		return -EFAULT;
	}
	/* check buffer size */
	if (recv_buf_size < pstrMessage->len) {
		spin_unlock_irqrestore(&mq->lock, flags);
		up(&mq->sem);
		PRINT_ER("recv_buf_size overflow\n");
		return -EOVERFLOW;
	}

	/* consume the message */
	mq->recv_count--;
	memcpy(recv_buf, pstrMessage->buf, pstrMessage->len);
	*pu32ReceivedLength = pstrMessage->len;

	mq->msg_list = pstrMessage->next;

	kfree(pstrMessage->buf);
	kfree(pstrMessage);

	spin_unlock_irqrestore(&mq->lock, flags);

	return 0;
}
