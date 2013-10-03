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

#include <asm/uaccess.h>
#include <asm/types.h>

#include <linux/nbd.h>

#define NBD_MAGIC 0x68797548

#ifdef NDEBUG
#define dprintk(flags, fmt...)
#else /* NDEBUG */
#define dprintk(flags, fmt...) do { \
	if (debugflags & (flags)) printk(KERN_DEBUG fmt); \
} while (0)
#define DBG_IOCTL       0x0004
#define DBG_INIT        0x0010
#define DBG_EXIT        0x0020
#define DBG_BLKDEV      0x0100
#define DBG_RX          0x0200
#define DBG_TX          0x0400
static unsigned int debugflags;
#endif /* NDEBUG */

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

#ifndef NDEBUG
static const char *ioctl_cmd_to_ascii(int cmd)
{
	switch (cmd) {
	case NBD_SET_SOCK: return "set-sock";
	case NBD_SET_BLKSIZE: return "set-blksize";
	case NBD_SET_SIZE: return "set-size";
	case NBD_SET_TIMEOUT: return "set-timeout";
	case NBD_SET_FLAGS: return "set-flags";
	case NBD_DO_IT: return "do-it";
	case NBD_CLEAR_SOCK: return "clear-sock";
	case NBD_CLEAR_QUE: return "clear-que";
	case NBD_PRINT_DEBUG: return "print-debug";
	case NBD_SET_SIZE_BLOCKS: return "set-size-blocks";
	case NBD_DISCONNECT: return "disconnect";
	case BLKROSET: return "set-read-only";
	case BLKFLSBUF: return "flush-buffer-cache";
	}
	return "unknown";
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
#endif /* NDEBUG */

static void nbd_end_request(struct request *req)
{
	int error = req->errors ? -EIO : 0;
	struct request_queue *q = req->q;
	unsigned long flags;

	dprintk(DBG_BLKDEV, "%s: request %p: %s\n", req->rq_disk->disk_name,
			req, error ? "failed" : "done");

	spin_lock_irqsave(q->queue_lock, flags);
	__blk_end_request_all(req, error);
	spin_unlock_irqrestore(q->queue_lock, flags);
}

static void sock_shutdown(struct nbd_device *nbd, int lock)
{
	/* Forcibly shutdown the socket causing all listeners
	 * to error
	 *
	 * FIXME: This code is duplicated from sys_shutdown, but
	 * there should be a more generic interface rather than
	 * calling socket ops directly here */
	if (lock)
		mutex_lock(&nbd->tx_lock);
	if (nbd->sock) {
		dev_warn(disk_to_dev(nbd->disk), "shutting down socket\n");
		kernel_sock_shutdown(nbd->sock, SHUT_RDWR);
		nbd->sock = NULL;
	}
	if (lock)
		mutex_unlock(&nbd->tx_lock);
}

static void nbd_xmit_timeout(unsigned long arg)
{
	struct task_struct *task = (struct task_struct *)arg;

	printk(KERN_WARNING "nbd: killing hung xmit (%s, pid: %d)\n",
		task->comm, task->pid);
	force_sig(SIGKILL, task);
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

		if (send) {
			struct timer_list ti;

			if (nbd->xmit_timeout) {
				init_timer(&ti);
				ti.function = nbd_xmit_timeout;
				ti.data = (unsigned long)current;
				ti.expires = jiffies + nbd->xmit_timeout;
				add_timer(&ti);
			}
			result = kernel_sendmsg(sock, &msg, &iov, 1, size);
			if (nbd->xmit_timeout)
				del_timer_sync(&ti);
		} else
			result = kernel_recvmsg(sock, &msg, &iov, 1, size,
						msg.msg_flags);

		if (signal_pending(current)) {
			siginfo_t info;
			printk(KERN_WARNING "nbd (pid %d: %s) got signal %d\n",
				task_pid_nr(current), current->comm,
				dequeue_signal_lock(current, &current->blocked, &info));
			result = -EINTR;
			sock_shutdown(nbd, !send);
			break;
		}

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

	request.magic = htonl(NBD_REQUEST_MAGIC);
	request.type = htonl(nbd_cmd(req));

	if (nbd_cmd(req) == NBD_CMD_FLUSH) {
		/* Other values are reserved for FLUSH requests.  */
		request.from = 0;
		request.len = 0;
	} else {
		request.from = cpu_to_be64((u64)blk_rq_pos(req) << 9);
		request.len = htonl(size);
	}
	memcpy(request.handle, &req, sizeof(req));

	dprintk(DBG_TX, "%s: request %p: sending control (%s@%llu,%uB)\n",
			nbd->disk->disk_name, req,
			nbdcmd_to_ascii(nbd_cmd(req)),
			(unsigned long long)blk_rq_pos(req) << 9,
			blk_rq_bytes(req));
	result = sock_xmit(nbd, 1, &request, sizeof(request),
			(nbd_cmd(req) == NBD_CMD_WRITE) ? MSG_MORE : 0);
	if (result <= 0) {
		dev_err(disk_to_dev(nbd->disk),
			"Send control failed (result %d)\n", result);
		goto error_out;
	}

	if (nbd_cmd(req) == NBD_CMD_WRITE) {
		struct req_iterator iter;
		struct bio_vec *bvec;
		/*
		 * we are really probing at internals to determine
		 * whether to set MSG_MORE or not...
		 */
		rq_for_each_segment(bvec, req, iter) {
			flags = 0;
			if (!rq_iter_last(req, iter))
				flags = MSG_MORE;
			dprintk(DBG_TX, "%s: request %p: sending %d bytes data\n",
					nbd->disk->disk_name, req, bvec->bv_len);
			result = sock_send_bvec(nbd, bvec, flags);
			if (result <= 0) {
				dev_err(disk_to_dev(nbd->disk),
					"Send data failed (result %d)\n",
					result);
				goto error_out;
			}
		}
	}
	return 0;

error_out:
	return -EIO;
}

static struct request *nbd_find_request(struct nbd_device *nbd,
					struct request *xreq)
{
	struct request *req, *tmp;
	int err;

	err = wait_event_interruptible(nbd->active_wq, nbd->active_req != xreq);
	if (unlikely(err))
		goto out;

	spin_lock(&nbd->queue_lock);
	list_for_each_entry_safe(req, tmp, &nbd->queue_head, queuelist) {
		if (req != xreq)
			continue;
		list_del_init(&req->queuelist);
		spin_unlock(&nbd->queue_lock);
		return req;
	}
	spin_unlock(&nbd->queue_lock);

	err = -ENOENT;

out:
	return ERR_PTR(err);
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
		goto harderror;
	}

	if (ntohl(reply.magic) != NBD_REPLY_MAGIC) {
		dev_err(disk_to_dev(nbd->disk), "Wrong magic (0x%lx)\n",
				(unsigned long)ntohl(reply.magic));
		result = -EPROTO;
		goto harderror;
	}

	req = nbd_find_request(nbd, *(struct request **)reply.handle);
	if (IS_ERR(req)) {
		result = PTR_ERR(req);
		if (result != -ENOENT)
			goto harderror;

		dev_err(disk_to_dev(nbd->disk), "Unexpected reply (%p)\n",
			reply.handle);
		result = -EBADR;
		goto harderror;
	}

	if (ntohl(reply.error)) {
		dev_err(disk_to_dev(nbd->disk), "Other side returned error (%d)\n",
			ntohl(reply.error));
		req->errors++;
		return req;
	}

	dprintk(DBG_RX, "%s: request %p: got reply\n",
			nbd->disk->disk_name, req);
	if (nbd_cmd(req) == NBD_CMD_READ) {
		struct req_iterator iter;
		struct bio_vec *bvec;

		rq_for_each_segment(bvec, req, iter) {
			result = sock_recv_bvec(nbd, bvec);
			if (result <= 0) {
				dev_err(disk_to_dev(nbd->disk), "Receive data failed (result %d)\n",
					result);
				req->errors++;
				return req;
			}
			dprintk(DBG_RX, "%s: request %p: got %d bytes data\n",
				nbd->disk->disk_name, req, bvec->bv_len);
		}
	}
	return req;
harderror:
	nbd->harderror = result;
	return NULL;
}

static ssize_t pid_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct gendisk *disk = dev_to_disk(dev);

	return sprintf(buf, "%ld\n",
		(long) ((struct nbd_device *)disk->private_data)->pid);
}

static struct device_attribute pid_attr = {
	.attr = { .name = "pid", .mode = S_IRUGO},
	.show = pid_show,
};

static int nbd_do_it(struct nbd_device *nbd)
{
	struct request *req;
	int ret;

	BUG_ON(nbd->magic != NBD_MAGIC);

	sk_set_memalloc(nbd->sock->sk);
	nbd->pid = task_pid_nr(current);
	ret = device_create_file(disk_to_dev(nbd->disk), &pid_attr);
	if (ret) {
		dev_err(disk_to_dev(nbd->disk), "device_create_file failed!\n");
		nbd->pid = 0;
		return ret;
	}

	while ((req = nbd_read_stat(nbd)) != NULL)
		nbd_end_request(req);

	device_remove_file(disk_to_dev(nbd->disk), &pid_attr);
	nbd->pid = 0;
	return 0;
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
		nbd_end_request(req);
	}

	while (!list_empty(&nbd->waiting_queue)) {
		req = list_entry(nbd->waiting_queue.next, struct request,
				 queuelist);
		list_del_init(&req->queuelist);
		req->errors++;
		nbd_end_request(req);
	}
}


static void nbd_handle_req(struct nbd_device *nbd, struct request *req)
{
	if (req->cmd_type != REQ_TYPE_FS)
		goto error_out;

	nbd_cmd(req) = NBD_CMD_READ;
	if (rq_data_dir(req) == WRITE) {
		if ((req->cmd_flags & REQ_DISCARD)) {
			WARN_ON(!(nbd->flags & NBD_FLAG_SEND_TRIM));
			nbd_cmd(req) = NBD_CMD_TRIM;
		} else
			nbd_cmd(req) = NBD_CMD_WRITE;
		if (nbd->flags & NBD_FLAG_READ_ONLY) {
			dev_err(disk_to_dev(nbd->disk),
				"Write on read-only\n");
			goto error_out;
		}
	}

	if (req->cmd_flags & REQ_FLUSH) {
		BUG_ON(unlikely(blk_rq_sectors(req)));
		nbd_cmd(req) = NBD_CMD_FLUSH;
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

	if (nbd_send_req(nbd, req) != 0) {
		dev_err(disk_to_dev(nbd->disk), "Request send failed\n");
		req->errors++;
		nbd_end_request(req);
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
	nbd_end_request(req);
}

static int nbd_thread(void *data)
{
	struct nbd_device *nbd = data;
	struct request *req;

	set_user_nice(current, -20);
	while (!kthread_should_stop() || !list_empty(&nbd->waiting_queue)) {
		/* wait for something to do */
		wait_event_interruptible(nbd->waiting_wq,
					 kthread_should_stop() ||
					 !list_empty(&nbd->waiting_queue));

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
	return 0;
}

/*
 * We always wait for result of write, for now. It would be nice to make it optional
 * in future
 * if ((rq_data_dir(req) == WRITE) && (nbd->flags & NBD_WRITE_NOCHK))
 *   { printk( "Warning: Ignoring result!\n"); nbd_end_request( req ); }
 */

static void do_nbd_request(struct request_queue *q)
		__releases(q->queue_lock) __acquires(q->queue_lock)
{
	struct request *req;
	
	while ((req = blk_fetch_request(q)) != NULL) {
		struct nbd_device *nbd;

		spin_unlock_irq(q->queue_lock);

		dprintk(DBG_BLKDEV, "%s: request %p: dequeued (flags=%x)\n",
				req->rq_disk->disk_name, req, req->cmd_type);

		nbd = req->rq_disk->private_data;

		BUG_ON(nbd->magic != NBD_MAGIC);

		if (unlikely(!nbd->sock)) {
			dev_err(disk_to_dev(nbd->disk),
				"Attempted send on closed socket\n");
			req->errors++;
			nbd_end_request(req);
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
		sreq.cmd_type = REQ_TYPE_SPECIAL;
		nbd_cmd(&sreq) = NBD_CMD_DISC;

		/* Check again after getting mutex back.  */
		if (!nbd->sock)
			return -EINVAL;

		nbd->disconnect = 1;

		nbd_send_req(nbd, &sreq);
		return 0;
	}
 
	case NBD_CLEAR_SOCK: {
		struct file *file;

		nbd->sock = NULL;
		file = nbd->file;
		nbd->file = NULL;
		nbd_clear_que(nbd);
		BUG_ON(!list_empty(&nbd->queue_head));
		BUG_ON(!list_empty(&nbd->waiting_queue));
		kill_bdev(bdev);
		if (file)
			fput(file);
		return 0;
	}

	case NBD_SET_SOCK: {
		struct file *file;
		if (nbd->file)
			return -EBUSY;
		file = fget(arg);
		if (file) {
			struct inode *inode = file_inode(file);
			if (S_ISSOCK(inode->i_mode)) {
				nbd->file = file;
				nbd->sock = SOCKET_I(inode);
				if (max_part > 0)
					bdev->bd_invalidated = 1;
				nbd->disconnect = 0; /* we're connected now */
				return 0;
			} else {
				fput(file);
			}
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
		struct file *file;
		int error;

		if (nbd->pid)
			return -EBUSY;
		if (!nbd->file)
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

		thread = kthread_create(nbd_thread, nbd, "%s",
					nbd->disk->disk_name);
		if (IS_ERR(thread)) {
			mutex_lock(&nbd->tx_lock);
			return PTR_ERR(thread);
		}
		wake_up_process(thread);
		error = nbd_do_it(nbd);
		kthread_stop(thread);

		mutex_lock(&nbd->tx_lock);
		if (error)
			return error;
		sock_shutdown(nbd, 0);
		file = nbd->file;
		nbd->file = NULL;
		nbd_clear_que(nbd);
		dev_warn(disk_to_dev(nbd->disk), "queue cleared\n");
		kill_bdev(bdev);
		queue_flag_clear_unlocked(QUEUE_FLAG_DISCARD, nbd->disk->queue);
		set_device_ro(bdev, false);
		if (file)
			fput(file);
		nbd->flags = 0;
		nbd->bytesize = 0;
		bdev->bd_inode->i_size = 0;
		set_capacity(nbd->disk, 0);
		if (max_part > 0)
			ioctl_by_bdev(bdev, BLKRRPART, 0);
		if (nbd->disconnect) /* user requested, ignore socket errors */
			return 0;
		return nbd->harderror;
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

	/* Anyone capable of this syscall can do *real bad* things */
	dprintk(DBG_IOCTL, "%s: nbd_ioctl cmd=%s(0x%x) arg=%lu\n",
		nbd->disk->disk_name, ioctl_cmd_to_ascii(cmd), cmd, arg);

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

	nbd_dev = kcalloc(nbds_max, sizeof(*nbd_dev), GFP_KERNEL);
	if (!nbd_dev)
		return -ENOMEM;

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
		disk->queue = blk_init_queue(do_nbd_request, &nbd_lock);
		if (!disk->queue) {
			put_disk(disk);
			goto out;
		}
		/*
		 * Tell the block layer that we are not a rotational device
		 */
		queue_flag_set_unlocked(QUEUE_FLAG_NONROT, disk->queue);
		disk->queue->limits.discard_granularity = 512;
		disk->queue->limits.max_discard_sectors = UINT_MAX;
		disk->queue->limits.discard_zeroes_data = 0;
		blk_queue_max_hw_sectors(disk->queue, 65536);
		disk->queue->limits.max_sectors = 256;
	}

	if (register_blkdev(NBD_MAJOR, "nbd")) {
		err = -EIO;
		goto out;
	}

	printk(KERN_INFO "nbd: registered device at major %d\n", NBD_MAJOR);
	dprintk(DBG_INIT, "nbd: debugflags=0x%x\n", debugflags);

	for (i = 0; i < nbds_max; i++) {
		struct gendisk *disk = nbd_dev[i].disk;
		nbd_dev[i].file = NULL;
		nbd_dev[i].magic = NBD_MAGIC;
		nbd_dev[i].flags = 0;
		INIT_LIST_HEAD(&nbd_dev[i].waiting_queue);
		spin_lock_init(&nbd_dev[i].queue_lock);
		INIT_LIST_HEAD(&nbd_dev[i].queue_head);
		mutex_init(&nbd_dev[i].tx_lock);
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
#ifndef NDEBUG
module_param(debugflags, int, 0644);
MODULE_PARM_DESC(debugflags, "flags for controlling debug output");
#endif
