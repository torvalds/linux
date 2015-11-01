/*
 * Network block device - make block devices work over TCP
 *
 * Note that you can not swap over this thing, yet. Seems to work but
 * deadlocks sometimes - you can not swap over TCP in general.
 * 
 * Copyright 1997-2000, 2008 Pavel Machek <pavel@ucw.cz>
 * Parts copyright 2001 Steven Whitehouse <steve@chygwyn.com>
 *
 * This file is released under GPLv2 or later.
 *
 * (part of code stolen from loop.c)
 */

#include <linux/major.h>

#include <linux/blkdev.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/bio.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/ioctl.h>
#include <linux/mutex.h>
#include <linux/compiler.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <net/sock.h>
#include <linux/net.h>
#include <linux/kthread.h>
#include <linux/types.h>
#include <linux/debugfs.h>

#include <asm/uaccess.h>
#include <asm/types.h>

#include <linux/nbd.h>

struct nbd_device {
	u32 flags;
	struct socket * sock;	/* If == NULL, device is not ready, yet	*/
	int magic;

	spinlock_t queue_lock;
	struct list_head queue_head;	/* Requests waiting result */
	struct request *active_req;
	wait_queue_head_t active_wq;
	struct list_head waiting_queue;	/* Requests to be sent */
	wait_queue_head_t waiting_wq;

	struct mutex tx_lock;
	struct gendisk *disk;
	int blksize;
	loff_t bytesize;
	int xmit_timeout;
	bool disconnect; /* a disconnect has been requested by user */

	struct timer_list timeout_timer;
	spinlock_t tasks_lock;
	struct task_struct *task_recv;
	struct task_struct *task_send;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *dbg_dir;
#endif
};

#if IS_ENABLED(CONFIG_DEBUG_FS)
static struct dentry *nbd_dbg_dir;
#endif

#define nbd_name(nbd) ((nbd)->disk->disk_name)

#define NBD_MAGIC 0x68797548

static unsigned int nbds_max = 16;
static struct nbd_device *nbd_dev;
static int max_part;

/*
 * Use just one lock (or at most 1 per NIC). Two arguments for this:
 * 1. Each NIC is essentially a synchronization point for all servers
 *    accessed through that NIC so there's no need to have more locks
 *    than NICs anyway.
 * 2. More locks lead to more "Dirty cache line bouncing" which will slow
 *    down each lock to the point where they're actually slower than just
 *    a single lock.
 * Thanks go to Jens Axboe and Al Viro for their LKML emails explaining this!
 */
static DEFINE_SPINLOCK(nbd_lock);

static inline struct device *nbd_to_dev(struct nbd_device *nbd)
{
	return disk_to_dev(nbd->disk);
}

static const char *nbdcmd_to_ascii(int cmd)
{
	switch (cmd) {
	case  NBD_CMD_READ: return "read";
	case NBD_CMD_WRITE: return "write";
	case  NBD_CMD_DISC: return "disconnect";
	case NBD_CMD_FLUSH: return "flush";
	case  NBD_CMD_TRIM: return "trim/discard";
	}
	return "invalid";
}

static void nbd_end_request(struct nbd_device *nbd, struct request *req)
{
	int error = req->errors ? -EIO : 0;
	struct request_queue *q = req->q;
	unsigned long flags;

	dev_dbg(nbd_to_dev(nbd), "request %p: %s\n", req,
		error ? "failed" : "done");

	spin_lock_irqsave(q->queue_lock, flags);
	__blk_end_request_all(req, error);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

/*
 * Forcibly shutdown the socket causing all listeners to error
 */
static void sock_shutdown(struct nbd_device *nbd)
{
	if (!nbd->sock)
		return;

	dev_warn(disk_to_dev(nbd->disk), "shutting down socket\n");
	kernel_sock_shutdown(nbd->sock, SHUT_RDWR);
	nbd->sock = NULL;
	del_timer_sync(&nbd->timeout_timer);
}

static void nbd_xmit_timeout(unsigned long arg)
{
	struct nbd_device *nbd = (struct nbd_device *)arg;
	unsigned long flags;

	if (list_empty(&nbd->queue_head))
		return;

	nbd->disconnect = true;

	spin_lock_irqsave(&nbd->tasks_lock, flags);

	if (nbd->task_recv)
		force_sig(SIGKILL, nbd->task_recv);

	if (nbd->task_send)
		force_sig(SIGKILL, nbd->task_send);

	spin_unlock_irqrestore(&nbd->tasks_lock, flags);

	dev_err(nbd_to_dev(nbd), "Connection timed out, killed receiver and sender, shutting down connection\n");
}

/*
 *  Send or receive packet.
 */
static int sock_xmit(struct nbd_device *nbd, int send, void *buf, int size,
		int msg_flags)
{
	struct socket *sock = nbd->sock;
	int result;
	struct msghdr msg;
	struct kvec iov;
	sigset_t blocked, oldset;
	unsigned long pflags = current->flags;

	if (unlikely(!sock)) {
		dev_err(disk_to_dev(nbd->disk),
			"Attempted %s on closed socket in sock_xmit\n",
			(send ? "send" : "recv"));
		return -EINVAL;
	}

	/* Allow interception of SIGKILL only
	 * Don't allow other signals to interrupt the transmission */
	siginitsetinv(&blocked, sigmask(SIGKILL));
	sigprocmask(SIG_SETMASK, &blocked, &oldset);

	current->flags |= PF_MEMALLOC;
	do {
		sock->sk->sk_allocation = GFP_NOIO | __GFP_MEMALLOC;
		iov.iov_base = buf;
		iov.iov_len = size;
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags = msg_flags | MSG_NOSIGNAL;

		if (send)
			result = kernel_sendmsg(sock, &msg, &iov, 1, size);
		else
			result = kernel_recvmsg(sock, &msg, &iov, 1, size,
						msg.msg_flags);

		if (result <= 0) {
			if (result == 0)
				result = -EPIPE; /* short read */
			break;
		}
		size -= result;
		buf += result;
	} while (size > 0);

	sigprocmask(SIG_SETMASK, &oldset, NULL);
	tsk_restore_flags(current, pflags, PF_MEMALLOC);

	if (!send && nbd->xmit_timeout)
		mod_timer(&nbd->timeout_timer, jiffies + nbd->xmit_timeout);

	return result;
}

static inline int sock_send_bvec(struct nbd_device *nbd, struct bio_vec *bvec,
		int flags)
{
	int result;
	void *kaddr = kmap(bvec->bv_page);
	result = sock_xmit(nbd, 1, kaddr + bvec->bv_offset,
			   bvec->bv_len, flags);
	kunmap(bvec->bv_page);
	return result;
}

/* always call with the tx_lock held */
static int nbd_send_req(struct nbd_device *nbd, struct request *req)
{
	int result, flags;
	struct nbd_request request;
	unsigned long size = blk_rq_bytes(req);
	u32 type;

	if (req->cmd_type == REQ_TYPE_DRV_PRIV)
		type = NBD_CMD_DISC;
	else if (req->cmd_flags & REQ_DISCARD)
		type = NBD_CMD_TRIM;
	else if (req->cmd_flags & REQ_FLUSH)
		type = NBD_CMD_FLUSH;
	else if (rq_data_dir(req) == WRITE)
		type = NBD_CMD_WRITE;
	else
		type = NBD_CMD_READ;

	memset(&request, 0, sizeof(request));
	request.magic = htonl(NBD_REQUEST_MAGIC);
	request.type = htonl(type);
	if (type != NBD_CMD_FLUSH && type != NBD_CMD_DISC) {
		request.from = cpu_to_be64((u64)blk_rq_pos(req) << 9);
		request.len = htonl(size);
	}
	memcpy(request.handle, &req, sizeof(req));

	dev_dbg(nbd_to_dev(nbd), "request %p: sending control (%s@%llu,%uB)\n",
		req, nbdcmd_to_ascii(type),
		(unsigned long long)blk_rq_pos(req) << 9, blk_rq_bytes(req));
	result = sock_xmit(nbd, 1, &request, sizeof(request),
			(type == NBD_CMD_WRITE) ? MSG_MORE : 0);
	if (result <= 0) {
		dev_err(disk_to_dev(nbd->disk),
			"Send control failed (result %d)\n", result);
		return -EIO;
	}

	if (type == NBD_CMD_WRITE) {
		struct req_iterator iter;
		struct bio_vec bvec;
		/*
		 * we are really probing at internals to determine
		 * whether to set MSG_MORE or not...
		 */
		rq_for_each_segment(bvec, req, iter) {
			flags = 0;
			if (!rq_iter_last(bvec, iter))
				flags = MSG_MORE;
			dev_dbg(nbd_to_dev(nbd), "request %p: sending %d bytes data\n",
				req, bvec.bv_len);
			result = sock_send_bvec(nbd, &bvec, flags);
			if (result <= 0) {
				dev_err(disk_to_dev(nbd->disk),
					"Send data failed (result %d)\n",
					result);
				return -EIO;
			}
		}
	}
	return 0;
}

static struct request *nbd_find_request(struct nbd_device *nbd,
					struct request *xreq)
{
	struct request *req, *tmp;
	int err;

	err = wait_event_interruptible(nbd->active_wq, nbd->active_req != xreq);
	if (unlikely(err))
		return ERR_PTR(err);

	spin_lock(&nbd->queue_lock);
	list_for_each_entry_safe(req, tmp, &nbd->queue_head, queuelist) {
		if (req != xreq)
			continue;
		list_del_init(&req->queuelist);
		spin_unlock(&nbd->queue_lock);
		return req;
	}
	spin_unlock(&nbd->queue_lock);

	return ERR_PTR(-ENOENT);
}

static inline int sock_recv_bvec(struct nbd_device *nbd, struct bio_vec *bvec)
{
	int result;
	void *kaddr = kmap(bvec->bv_page);
	result = sock_xmit(nbd, 0, kaddr + bvec->bv_offset, bvec->bv_len,
			MSG_WAITALL);
	kunmap(bvec->bv_page);
	return result;
}

/* NULL returned = something went wrong, inform userspace */
static struct request *nbd_read_stat(struct nbd_device *nbd)
{
	int result;
	struct nbd_reply reply;
	struct request *req;

	reply.magic = 0;
	result = sock_xmit(nbd, 0, &reply, sizeof(reply), MSG_WAITALL);
	if (result <= 0) {
		dev_err(disk_to_dev(nbd->disk),
			"Receive control failed (result %d)\n", result);
		return ERR_PTR(result);
	}

	if (ntohl(reply.magic) != NBD_REPLY_MAGIC) {
		dev_err(disk_to_dev(nbd->disk), "Wrong magic (0x%lx)\n",
				(unsigned long)ntohl(reply.magic));
		return ERR_PTR(-EPROTO);
	}

	req = nbd_find_request(nbd, *(struct request **)reply.handle);
	if (IS_ERR(req)) {
		result = PTR_ERR(req);
		if (result != -ENOENT)
			return ERR_PTR(result);

		dev_err(disk_to_dev(nbd->disk), "Unexpected reply (%p)\n",
			reply.handle);
		return ERR_PTR(-EBADR);
	}

	if (ntohl(reply.error)) {
		dev_err(disk_to_dev(nbd->disk), "Other side returned error (%d)\n",
			ntohl(reply.error));
		req->errors++;
		return req;
	}

	dev_dbg(nbd_to_dev(nbd), "request %p: got reply\n", req);
	if (rq_data_dir(req) != WRITE) {
		struct req_iterator iter;
		struct bio_vec bvec;

		rq_for_each_segment(bvec, req, iter) {
			result = sock_recv_bvec(nbd, &bvec);
			if (result <= 0) {
				dev_err(disk_to_dev(nbd->disk), "Receive data failed (result %d)\n",
					result);
				req->errors++;
				return req;
			}
			dev_dbg(nbd_to_dev(nbd), "request %p: got %d bytes data\n",
				req, bvec.bv_len);
		}
	}
	return req;
}

static ssize_t pid_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);
	struct nbd_device *nbd = (struct nbd_device *)disk->private_data;

	return sprintf(buf, "%d\n", task_pid_nr(nbd->task_recv));
}

static struct device_attribute pid_attr = {
	.attr = { .name = "pid", .mode = S_IRUGO},
	.show = pid_show,
};

static int nbd_thread_recv(struct nbd_device *nbd)
{
	struct request *req;
	int ret;
	unsigned long flags;

	BUG_ON(nbd->magic != NBD_MAGIC);

	sk_set_memalloc(nbd->sock->sk);

	spin_lock_irqsave(&nbd->tasks_lock, flags);
	nbd->task_recv = current;
	spin_unlock_irqrestore(&nbd->tasks_lock, flags);

	ret = device_create_file(disk_to_dev(nbd->disk), &pid_attr);
	if (ret) {
		dev_err(disk_to_dev(nbd->disk), "device_create_file failed!\n");

		spin_lock_irqsave(&nbd->tasks_lock, flags);
		nbd->task_recv = NULL;
		spin_unlock_irqrestore(&nbd->tasks_lock, flags);

		return ret;
	}

	while (1) {
		req = nbd_read_stat(nbd);
		if (IS_ERR(req)) {
			ret = PTR_ERR(req);
			break;
		}

		nbd_end_request(nbd, req);
	}

	device_remove_file(disk_to_dev(nbd->disk), &pid_attr);

	spin_lock_irqsave(&nbd->tasks_lock, flags);
	nbd->task_recv = NULL;
	spin_unlock_irqrestore(&nbd->tasks_lock, flags);

	if (signal_pending(current)) {
		siginfo_t info;

		ret = dequeue_signal_lock(current, &current->blocked, &info);
		dev_warn(nbd_to_dev(nbd), "pid %d, %s, got signal %d\n",
			 task_pid_nr(current), current->comm, ret);
		mutex_lock(&nbd->tx_lock);
		sock_shutdown(nbd);
		mutex_unlock(&nbd->tx_lock);
		ret = -ETIMEDOUT;
	}

	return ret;
}

static void nbd_clear_que(struct nbd_device *nbd)
{
	struct request *req;

	BUG_ON(nbd->magic != NBD_MAGIC);

	/*
	 * Because we have set nbd->sock to NULL under the tx_lock, all
	 * modifications to the list must have completed by now.  For
	 * the same reason, the active_req must be NULL.
	 *
	 * As a consequence, we don't need to take the spin lock while
	 * purging the list here.
	 */
	BUG_ON(nbd->sock);
	BUG_ON(nbd->active_req);

	while (!list_empty(&nbd->queue_head)) {
		req = list_entry(nbd->queue_head.next, struct request,
				 queuelist);
		list_del_init(&req->queuelist);
		req->errors++;
		nbd_end_request(nbd, req);
	}

	while (!list_empty(&nbd->waiting_queue)) {
		req = list_entry(nbd->waiting_queue.next, struct request,
				 queuelist);
		list_del_init(&req->queuelist);
		req->errors++;
		nbd_end_request(nbd, req);
	}
	dev_dbg(disk_to_dev(nbd->disk), "queue cleared\n");
}


static void nbd_handle_req(struct nbd_device *nbd, struct request *req)
{
	if (req->cmd_type != REQ_TYPE_FS)
		goto error_out;

	if (rq_data_dir(req) == WRITE &&
	    (nbd->flags & NBD_FLAG_READ_ONLY)) {
		dev_err(disk_to_dev(nbd->disk),
			"Write on read-only\n");
		goto error_out;
	}

	req->errors = 0;

	mutex_lock(&nbd->tx_lock);
	if (unlikely(!nbd->sock)) {
		mutex_unlock(&nbd->tx_lock);
		dev_err(disk_to_dev(nbd->disk),
			"Attempted send on closed socket\n");
		goto error_out;
	}

	nbd->active_req = req;

	if (nbd->xmit_timeout && list_empty_careful(&nbd->queue_head))
		mod_timer(&nbd->timeout_timer, jiffies + nbd->xmit_timeout);

	if (nbd_send_req(nbd, req) != 0) {
		dev_err(disk_to_dev(nbd->disk), "Request send failed\n");
		req->errors++;
		nbd_end_request(nbd, req);
	} else {
		spin_lock(&nbd->queue_lock);
		list_add_tail(&req->queuelist, &nbd->queue_head);
		spin_unlock(&nbd->queue_lock);
	}

	nbd->active_req = NULL;
	mutex_unlock(&nbd->tx_lock);
	wake_up_all(&nbd->active_wq);

	return;

error_out:
	req->errors++;
	nbd_end_request(nbd, req);
}

static int nbd_thread_send(void *data)
{
	struct nbd_device *nbd = data;
	struct request *req;
	unsigned long flags;

	spin_lock_irqsave(&nbd->tasks_lock, flags);
	nbd->task_send = current;
	spin_unlock_irqrestore(&nbd->tasks_lock, flags);

	set_user_nice(current, MIN_NICE);
	while (!kthread_should_stop() || !list_empty(&nbd->waiting_queue)) {
		/* wait for something to do */
		wait_event_interruptible(nbd->waiting_wq,
					 kthread_should_stop() ||
					 !list_empty(&nbd->waiting_queue));

		if (signal_pending(current)) {
			siginfo_t info;
			int ret;

			ret = dequeue_signal_lock(current, &current->blocked,
						  &info);
			dev_warn(nbd_to_dev(nbd), "pid %d, %s, got signal %d\n",
				 task_pid_nr(current), current->comm, ret);
			mutex_lock(&nbd->tx_lock);
			sock_shutdown(nbd);
			mutex_unlock(&nbd->tx_lock);
			break;
		}

		/* extract request */
		if (list_empty(&nbd->waiting_queue))
			continue;

		spin_lock_irq(&nbd->queue_lock);
		req = list_entry(nbd->waiting_queue.next, struct request,
				 queuelist);
		list_del_init(&req->queuelist);
		spin_unlock_irq(&nbd->queue_lock);

		/* handle request */
		nbd_handle_req(nbd, req);
	}

	spin_lock_irqsave(&nbd->tasks_lock, flags);
	nbd->task_send = NULL;
	spin_unlock_irqrestore(&nbd->tasks_lock, flags);

	/* Clear maybe pending signals */
	if (signal_pending(current)) {
		siginfo_t info;
		dequeue_signal_lock(current, &current->blocked, &info);
	}

	return 0;
}

/*
 * We always wait for result of write, for now. It would be nice to make it optional
 * in future
 * if ((rq_data_dir(req) == WRITE) && (nbd->flags & NBD_WRITE_NOCHK))
 *   { printk( "Warning: Ignoring result!\n"); nbd_end_request( req ); }
 */

static void nbd_request_handler(struct request_queue *q)
		__releases(q->queue_lock) __acquires(q->queue_lock)
{
	struct request *req;
	
	while ((req = blk_fetch_request(q)) != NULL) {
		struct nbd_device *nbd;

		spin_unlock_irq(q->queue_lock);

		nbd = req->rq_disk->private_data;

		BUG_ON(nbd->magic != NBD_MAGIC);

		dev_dbg(nbd_to_dev(nbd), "request %p: dequeued (flags=%x)\n",
			req, req->cmd_type);

		if (unlikely(!nbd->sock)) {
			dev_err(disk_to_dev(nbd->disk),
				"Attempted send on closed socket\n");
			req->errors++;
			nbd_end_request(nbd, req);
			spin_lock_irq(q->queue_lock);
			continue;
		}

		spin_lock_irq(&nbd->queue_lock);
		list_add_tail(&req->queuelist, &nbd->waiting_queue);
		spin_unlock_irq(&nbd->queue_lock);

		wake_up(&nbd->waiting_wq);

		spin_lock_irq(q->queue_lock);
	}
}

static int nbd_dev_dbg_init(struct nbd_device *nbd);
static void nbd_dev_dbg_close(struct nbd_device *nbd);

/* Must be called with tx_lock held */

static int __nbd_ioctl(struct block_device *bdev, struct nbd_device *nbd,
		       unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case NBD_DISCONNECT: {
		struct request sreq;

		dev_info(disk_to_dev(nbd->disk), "NBD_DISCONNECT\n");
		if (!nbd->sock)
			return -EINVAL;

		mutex_unlock(&nbd->tx_lock);
		fsync_bdev(bdev);
		mutex_lock(&nbd->tx_lock);
		blk_rq_init(NULL, &sreq);
		sreq.cmd_type = REQ_TYPE_DRV_PRIV;

		/* Check again after getting mutex back.  */
		if (!nbd->sock)
			return -EINVAL;

		nbd->disconnect = true;

		nbd_send_req(nbd, &sreq);
		return 0;
	}
 
	case NBD_CLEAR_SOCK: {
		struct socket *sock = nbd->sock;
		nbd->sock = NULL;
		nbd_clear_que(nbd);
		BUG_ON(!list_empty(&nbd->queue_head));
		BUG_ON(!list_empty(&nbd->waiting_queue));
		kill_bdev(bdev);
		if (sock)
			sockfd_put(sock);
		return 0;
	}

	case NBD_SET_SOCK: {
		struct socket *sock;
		int err;
		if (nbd->sock)
			return -EBUSY;
		sock = sockfd_lookup(arg, &err);
		if (sock) {
			nbd->sock = sock;
			if (max_part > 0)
				bdev->bd_invalidated = 1;
			nbd->disconnect = false; /* we're connected now */
			return 0;
		}
		return -EINVAL;
	}

	case NBD_SET_BLKSIZE:
		nbd->blksize = arg;
		nbd->bytesize &= ~(nbd->blksize-1);
		bdev->bd_inode->i_size = nbd->bytesize;
		set_blocksize(bdev, nbd->blksize);
		set_capacity(nbd->disk, nbd->bytesize >> 9);
		return 0;

	case NBD_SET_SIZE:
		nbd->bytesize = arg & ~(nbd->blksize-1);
		bdev->bd_inode->i_size = nbd->bytesize;
		set_blocksize(bdev, nbd->blksize);
		set_capacity(nbd->disk, nbd->bytesize >> 9);
		return 0;

	case NBD_SET_TIMEOUT:
		nbd->xmit_timeout = arg * HZ;
		if (arg)
			mod_timer(&nbd->timeout_timer,
				  jiffies + nbd->xmit_timeout);
		else
			del_timer_sync(&nbd->timeout_timer);

		return 0;

	case NBD_SET_FLAGS:
		nbd->flags = arg;
		return 0;

	case NBD_SET_SIZE_BLOCKS:
		nbd->bytesize = ((u64) arg) * nbd->blksize;
		bdev->bd_inode->i_size = nbd->bytesize;
		set_blocksize(bdev, nbd->blksize);
		set_capacity(nbd->disk, nbd->bytesize >> 9);
		return 0;

	case NBD_DO_IT: {
		struct task_struct *thread;
		struct socket *sock;
		int error;

		if (nbd->task_recv)
			return -EBUSY;
		if (!nbd->sock)
			return -EINVAL;

		mutex_unlock(&nbd->tx_lock);

		if (nbd->flags & NBD_FLAG_READ_ONLY)
			set_device_ro(bdev, true);
		if (nbd->flags & NBD_FLAG_SEND_TRIM)
			queue_flag_set_unlocked(QUEUE_FLAG_DISCARD,
				nbd->disk->queue);
		if (nbd->flags & NBD_FLAG_SEND_FLUSH)
			blk_queue_flush(nbd->disk->queue, REQ_FLUSH);
		else
			blk_queue_flush(nbd->disk->queue, 0);

		thread = kthread_run(nbd_thread_send, nbd, "%s",
				     nbd_name(nbd));
		if (IS_ERR(thread)) {
			mutex_lock(&nbd->tx_lock);
			return PTR_ERR(thread);
		}

		nbd_dev_dbg_init(nbd);
		error = nbd_thread_recv(nbd);
		nbd_dev_dbg_close(nbd);
		kthread_stop(thread);

		mutex_lock(&nbd->tx_lock);

		sock_shutdown(nbd);
		sock = nbd->sock;
		nbd->sock = NULL;
		nbd_clear_que(nbd);
		kill_bdev(bdev);
		queue_flag_clear_unlocked(QUEUE_FLAG_DISCARD, nbd->disk->queue);
		set_device_ro(bdev, false);
		if (sock)
			sockfd_put(sock);
		nbd->flags = 0;
		nbd->bytesize = 0;
		bdev->bd_inode->i_size = 0;
		set_capacity(nbd->disk, 0);
		if (max_part > 0)
			blkdev_reread_part(bdev);
		if (nbd->disconnect) /* user requested, ignore socket errors */
			return 0;
		return error;
	}

	case NBD_CLEAR_QUE:
		/*
		 * This is for compatibility only.  The queue is always cleared
		 * by NBD_DO_IT or NBD_CLEAR_SOCK.
		 */
		return 0;

	case NBD_PRINT_DEBUG:
		dev_info(disk_to_dev(nbd->disk),
			"next = %p, prev = %p, head = %p\n",
			nbd->queue_head.next, nbd->queue_head.prev,
			&nbd->queue_head);
		return 0;
	}
	return -ENOTTY;
}

static int nbd_ioctl(struct block_device *bdev, fmode_t mode,
		     unsigned int cmd, unsigned long arg)
{
	struct nbd_device *nbd = bdev->bd_disk->private_data;
	int error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	BUG_ON(nbd->magic != NBD_MAGIC);

	mutex_lock(&nbd->tx_lock);
	error = __nbd_ioctl(bdev, nbd, cmd, arg);
	mutex_unlock(&nbd->tx_lock);

	return error;
}

static const struct block_device_operations nbd_fops =
{
	.owner =	THIS_MODULE,
	.ioctl =	nbd_ioctl,
};

#if IS_ENABLED(CONFIG_DEBUG_FS)

static int nbd_dbg_tasks_show(struct seq_file *s, void *unused)
{
	struct nbd_device *nbd = s->private;

	if (nbd->task_recv)
		seq_printf(s, "recv: %d\n", task_pid_nr(nbd->task_recv));
	if (nbd->task_send)
		seq_printf(s, "send: %d\n", task_pid_nr(nbd->task_send));

	return 0;
}

static int nbd_dbg_tasks_open(struct inode *inode, struct file *file)
{
	return single_open(file, nbd_dbg_tasks_show, inode->i_private);
}

static const struct file_operations nbd_dbg_tasks_ops = {
	.open = nbd_dbg_tasks_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int nbd_dbg_flags_show(struct seq_file *s, void *unused)
{
	struct nbd_device *nbd = s->private;
	u32 flags = nbd->flags;

	seq_printf(s, "Hex: 0x%08x\n\n", flags);

	seq_puts(s, "Known flags:\n");

	if (flags & NBD_FLAG_HAS_FLAGS)
		seq_puts(s, "NBD_FLAG_HAS_FLAGS\n");
	if (flags & NBD_FLAG_READ_ONLY)
		seq_puts(s, "NBD_FLAG_READ_ONLY\n");
	if (flags & NBD_FLAG_SEND_FLUSH)
		seq_puts(s, "NBD_FLAG_SEND_FLUSH\n");
	if (flags & NBD_FLAG_SEND_TRIM)
		seq_puts(s, "NBD_FLAG_SEND_TRIM\n");

	return 0;
}

static int nbd_dbg_flags_open(struct inode *inode, struct file *file)
{
	return single_open(file, nbd_dbg_flags_show, inode->i_private);
}

static const struct file_operations nbd_dbg_flags_ops = {
	.open = nbd_dbg_flags_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int nbd_dev_dbg_init(struct nbd_device *nbd)
{
	struct dentry *dir;
	struct dentry *f;

	dir = debugfs_create_dir(nbd_name(nbd), nbd_dbg_dir);
	if (IS_ERR_OR_NULL(dir)) {
		dev_err(nbd_to_dev(nbd), "Failed to create debugfs dir for '%s' (%ld)\n",
			nbd_name(nbd), PTR_ERR(dir));
		return PTR_ERR(dir);
	}
	nbd->dbg_dir = dir;

	f = debugfs_create_file("tasks", 0444, dir, nbd, &nbd_dbg_tasks_ops);
	if (IS_ERR_OR_NULL(f)) {
		dev_err(nbd_to_dev(nbd), "Failed to create debugfs file 'tasks', %ld\n",
			PTR_ERR(f));
		return PTR_ERR(f);
	}

	f = debugfs_create_u64("size_bytes", 0444, dir, &nbd->bytesize);
	if (IS_ERR_OR_NULL(f)) {
		dev_err(nbd_to_dev(nbd), "Failed to create debugfs file 'size_bytes', %ld\n",
			PTR_ERR(f));
		return PTR_ERR(f);
	}

	f = debugfs_create_u32("timeout", 0444, dir, &nbd->xmit_timeout);
	if (IS_ERR_OR_NULL(f)) {
		dev_err(nbd_to_dev(nbd), "Failed to create debugfs file 'timeout', %ld\n",
			PTR_ERR(f));
		return PTR_ERR(f);
	}

	f = debugfs_create_u32("blocksize", 0444, dir, &nbd->blksize);
	if (IS_ERR_OR_NULL(f)) {
		dev_err(nbd_to_dev(nbd), "Failed to create debugfs file 'blocksize', %ld\n",
			PTR_ERR(f));
		return PTR_ERR(f);
	}

	f = debugfs_create_file("flags", 0444, dir, &nbd, &nbd_dbg_flags_ops);
	if (IS_ERR_OR_NULL(f)) {
		dev_err(nbd_to_dev(nbd), "Failed to create debugfs file 'flags', %ld\n",
			PTR_ERR(f));
		return PTR_ERR(f);
	}

	return 0;
}

static void nbd_dev_dbg_close(struct nbd_device *nbd)
{
	debugfs_remove_recursive(nbd->dbg_dir);
}

static int nbd_dbg_init(void)
{
	struct dentry *dbg_dir;

	dbg_dir = debugfs_create_dir("nbd", NULL);
	if (IS_ERR(dbg_dir))
		return PTR_ERR(dbg_dir);

	nbd_dbg_dir = dbg_dir;

	return 0;
}

static void nbd_dbg_close(void)
{
	debugfs_remove_recursive(nbd_dbg_dir);
}

#else  /* IS_ENABLED(CONFIG_DEBUG_FS) */

static int nbd_dev_dbg_init(struct nbd_device *nbd)
{
	return 0;
}

static void nbd_dev_dbg_close(struct nbd_device *nbd)
{
}

static int nbd_dbg_init(void)
{
	return 0;
}

static void nbd_dbg_close(void)
{
}

#endif

/*
 * And here should be modules and kernel interface 
 *  (Just smiley confuses emacs :-)
 */

static int __init nbd_init(void)
{
	int err = -ENOMEM;
	int i;
	int part_shift;

	BUILD_BUG_ON(sizeof(struct nbd_request) != 28);

	if (max_part < 0) {
		printk(KERN_ERR "nbd: max_part must be >= 0\n");
		return -EINVAL;
	}

	part_shift = 0;
	if (max_part > 0) {
		part_shift = fls(max_part);

		/*
		 * Adjust max_part according to part_shift as it is exported
		 * to user space so that user can know the max number of
		 * partition kernel should be able to manage.
		 *
		 * Note that -1 is required because partition 0 is reserved
		 * for the whole disk.
		 */
		max_part = (1UL << part_shift) - 1;
	}

	if ((1UL << part_shift) > DISK_MAX_PARTS)
		return -EINVAL;

	if (nbds_max > 1UL << (MINORBITS - part_shift))
		return -EINVAL;

	nbd_dev = kcalloc(nbds_max, sizeof(*nbd_dev), GFP_KERNEL);
	if (!nbd_dev)
		return -ENOMEM;

	for (i = 0; i < nbds_max; i++) {
		struct gendisk *disk = alloc_disk(1 << part_shift);
		if (!disk)
			goto out;
		nbd_dev[i].disk = disk;
		/*
		 * The new linux 2.5 block layer implementation requires
		 * every gendisk to have its very own request_queue struct.
		 * These structs are big so we dynamically allocate them.
		 */
		disk->queue = blk_init_queue(nbd_request_handler, &nbd_lock);
		if (!disk->queue) {
			put_disk(disk);
			goto out;
		}
		/*
		 * Tell the block layer that we are not a rotational device
		 */
		queue_flag_set_unlocked(QUEUE_FLAG_NONROT, disk->queue);
		queue_flag_clear_unlocked(QUEUE_FLAG_ADD_RANDOM, disk->queue);
		disk->queue->limits.discard_granularity = 512;
		blk_queue_max_discard_sectors(disk->queue, UINT_MAX);
		disk->queue->limits.discard_zeroes_data = 0;
		blk_queue_max_hw_sectors(disk->queue, 65536);
		disk->queue->limits.max_sectors = 256;
	}

	if (register_blkdev(NBD_MAJOR, "nbd")) {
		err = -EIO;
		goto out;
	}

	printk(KERN_INFO "nbd: registered device at major %d\n", NBD_MAJOR);

	nbd_dbg_init();

	for (i = 0; i < nbds_max; i++) {
		struct gendisk *disk = nbd_dev[i].disk;
		nbd_dev[i].magic = NBD_MAGIC;
		INIT_LIST_HEAD(&nbd_dev[i].waiting_queue);
		spin_lock_init(&nbd_dev[i].queue_lock);
		spin_lock_init(&nbd_dev[i].tasks_lock);
		INIT_LIST_HEAD(&nbd_dev[i].queue_head);
		mutex_init(&nbd_dev[i].tx_lock);
		init_timer(&nbd_dev[i].timeout_timer);
		nbd_dev[i].timeout_timer.function = nbd_xmit_timeout;
		nbd_dev[i].timeout_timer.data = (unsigned long)&nbd_dev[i];
		init_waitqueue_head(&nbd_dev[i].active_wq);
		init_waitqueue_head(&nbd_dev[i].waiting_wq);
		nbd_dev[i].blksize = 1024;
		nbd_dev[i].bytesize = 0;
		disk->major = NBD_MAJOR;
		disk->first_minor = i << part_shift;
		disk->fops = &nbd_fops;
		disk->private_data = &nbd_dev[i];
		sprintf(disk->disk_name, "nbd%d", i);
		set_capacity(disk, 0);
		add_disk(disk);
	}

	return 0;
out:
	while (i--) {
		blk_cleanup_queue(nbd_dev[i].disk->queue);
		put_disk(nbd_dev[i].disk);
	}
	kfree(nbd_dev);
	return err;
}

static void __exit nbd_cleanup(void)
{
	int i;

	nbd_dbg_close();

	for (i = 0; i < nbds_max; i++) {
		struct gendisk *disk = nbd_dev[i].disk;
		nbd_dev[i].magic = 0;
		if (disk) {
			del_gendisk(disk);
			blk_cleanup_queue(disk->queue);
			put_disk(disk);
		}
	}
	unregister_blkdev(NBD_MAJOR, "nbd");
	kfree(nbd_dev);
	printk(KERN_INFO "nbd: unregistered device at major %d\n", NBD_MAJOR);
}

module_init(nbd_init);
module_exit(nbd_cleanup);

MODULE_DESCRIPTION("Network Block Device");
MODULE_LICENSE("GPL");

module_param(nbds_max, int, 0444);
MODULE_PARM_DESC(nbds_max, "number of network block devices to initialize (default: 16)");
module_param(max_part, int, 0444);
MODULE_PARM_DESC(max_part, "number of partitions per device (default: 0)");
