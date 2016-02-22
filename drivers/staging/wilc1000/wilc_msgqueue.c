
#include "wilc_msgqueue.h"
#include <linux/spinlock.h>
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
	INIT_LIST_HEAD(&mq->msg_list);
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
	struct message *msg;

	mq->exiting = true;

	/* Release any waiting receiver thread. */
	while (mq->recv_count > 0) {
		up(&mq->sem);
		mq->recv_count--;
	}

	while (!list_empty(&mq->msg_list)) {
		msg = list_first_entry(&mq->msg_list, struct message, list);
		list_del(&msg->list);
		kfree(msg->buf);
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

	if (!mq || (send_buf_size == 0) || !send_buf)
		return -EINVAL;

	if (mq->exiting)
		return -EFAULT;

	/* construct a new message */
	new_msg = kmalloc(sizeof(*new_msg), GFP_ATOMIC);
	if (!new_msg)
		return -ENOMEM;

	new_msg->len = send_buf_size;
	INIT_LIST_HEAD(&new_msg->list);
	new_msg->buf = kmemdup(send_buf, send_buf_size, GFP_ATOMIC);
	if (!new_msg->buf) {
		kfree(new_msg);
		return -ENOMEM;
	}

	spin_lock_irqsave(&mq->lock, flags);

	/* add it to the message queue */
	list_add_tail(&new_msg->list, &mq->msg_list);

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
		 void *recv_buf, u32 recv_buf_size, u32 *recv_len)
{
	struct message *msg;
	unsigned long flags;

	if (!mq || (recv_buf_size == 0) || !recv_buf || !recv_len)
		return -EINVAL;

	if (mq->exiting)
		return -EFAULT;

	spin_lock_irqsave(&mq->lock, flags);
	mq->recv_count++;
	spin_unlock_irqrestore(&mq->lock, flags);

	down(&mq->sem);
	spin_lock_irqsave(&mq->lock, flags);

	if (list_empty(&mq->msg_list)) {
		spin_unlock_irqrestore(&mq->lock, flags);
		up(&mq->sem);
		return -EFAULT;
	}
	/* check buffer size */
	msg = list_first_entry(&mq->msg_list, struct message, list);
	if (recv_buf_size < msg->len) {
		spin_unlock_irqrestore(&mq->lock, flags);
		up(&mq->sem);
		return -EOVERFLOW;
	}

	/* consume the message */
	mq->recv_count--;
	memcpy(recv_buf, msg->buf, msg->len);
	*recv_len = msg->len;

	list_del(&msg->list);

	kfree(msg->buf);
	kfree(msg);

	spin_unlock_irqrestore(&mq->lock, flags);

	return 0;
}
