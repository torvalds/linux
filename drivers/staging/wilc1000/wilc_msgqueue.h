#ifndef __WILC_MSG_QUEUE_H__
#define __WILC_MSG_QUEUE_H__

#include <linux/semaphore.h>
#include <linux/list.h>

struct message {
	void *buf;
	u32 len;
	struct list_head list;
};

struct message_queue {
	struct semaphore sem;
	spinlock_t lock;
	bool exiting;
	u32 recv_count;
	struct list_head msg_list;
};

int wilc_mq_create(struct message_queue *mq);
int wilc_mq_send(struct message_queue *mq,
		 const void *send_buf, u32 send_buf_size);
int wilc_mq_recv(struct message_queue *mq,
		 void *recv_buf, u32 recv_buf_size, u32 *recv_len);
int wilc_mq_destroy(struct message_queue *mq);

#endif
