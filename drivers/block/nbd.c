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
#include <asm/system.h>
#include <asm/types.h>

#include <linux/nbd.h>

#define LO_MAGIC 0x68797548

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
static DEFINE_MUTEX(nbd_mutex);
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

static void sock_shutdown(struct nbd_device *lo, int lock)
{
	/* Forcibly shutdown the socket causing all listeners
	 * to error
	 *
	 * FIXME: This code is duplicated from sys_shutdown, but
	 * there should be a more generic interface rather than
	 * calling socket ops directly here */
	if (lock)
		mutex_lock(&lo->tx_lock);
	if (lo->sock) {
		printk(KERN_WARNING "%s: shutting down socket\n",
			lo->disk->disk_name);
		kernel_sock_shutdown(lo->sock, SHUT_RDWR);
		lo->sock = NULL;
	}
	if (lock)
		mutex_unlock(&lo->tx_lock);
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
static int sock_xmit(struct nbd_device *lo, int send, void *buf, int size,
		int msg_flags)
{
	struct socket *sock = lo->sock;
	int result;
	struct msghdr msg;
	struct kvec iov;
	sigset_t blocked, oldset;

	if (unlikely(!sock)) {
		printk(KERN_ERR "%s: Attempted %s on closed socket in sock_xmit\n",
		       lo->disk->disk_name, (send ? "send" : "recv"));
		return -EINVAL;
	}

	/* Allow interception of SIGKILL only
	 * Don't allow other signals to interrupt the transmission */
	siginitsetinv(&blocked, sigmask(SIGKILL));
	sigprocmask(SIG_SETMASK, &blocked, &oldset);

	do {
		sock->sk->sk_allocation = GFP_NOIO;
		iov.iov_base = buf;
		iov.iov_len = size;
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags = msg_flags | MSG_NOSIGNAL;

		if (send) {
			struct timer_list ti;

			if (lo->xmit_timeout) {
				init_timer(&ti);
				ti.function = nbd_xmit_timeout;
				ti.data = (unsigned long)current;
				ti.expires = jiffies + lo->xmit_timeout;
				add_timer(&ti);
			}
			result = kernel_sendmsg(sock, &msg, &iov, 1, size);
			if (lo->xmit_timeout)
				del_timer_sync(&ti);
		} else
			result = kernel_recvmsg(sock, &msg, &iov, 1, size, 0);

		if (signal_pending(current)) {
			siginfo_t info;
			printk(KERN_WARNING "nbd (pid %d: %s) got signal %d\n",
				task_pid_nr(current), current->comm,
				dequeue_signal_lock(current, &current->blocked, &info));
			result = -EINTR;
			sock_shutdown(lo, !send);
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

	return result;
}

static inline int sock_send_bvec(struct nbd_device *lo, struct bio_vec *bvec,
		int flags)
{
	int result;
	void *kaddr = kmap(bvec->bv_page);
	result = sock_xmit(lo, 1, kaddr + bvec->bv_offset, bvec->bv_len, flags);
	kunmap(bvec->bv_page);
	return result;
}

/* always call with the tx_lock held */
static int nbd_send_req(struct nbd_device *lo, struct request *req)
{
	int result, flags;
	struct nbd_request request;
	unsigned long size = blk_rq_bytes(req);

	request.magic = htonl(NBD_REQUEST_MAGIC);
	request.type = htonl(nbd_cmd(req));
	request.from = cpu_to_be64((u64)blk_rq_pos(req) << 9);
	request.len = htonl(size);
	memcpy(request.handle, &req, sizeof(req));

	dprintk(DBG_TX, "%s: request %p: sending control (%s@%llu,%uB)\n",
			lo->disk->disk_name, req,
			nbdcmd_to_ascii(nbd_cmd(req)),
			(unsigned long long)blk_rq_pos(req) << 9,
			blk_rq_bytes(req));
	result = sock_xmit(lo, 1, &request, sizeof(request),
			(nbd_cmd(req) == NBD_CMD_WRITE) ? MSG_MORE : 0);
	if (result <= 0) {
		printk(KERN_ERR "%s: Send control failed (result %d)\n",
				lo->disk->disk_name, result);
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
					lo->disk->disk_name, req, bvec->bv_len);
			result = sock_send_bvec(lo, bvec, flags);
			if (result <= 0) {
				printk(KERN_ERR "%s: Send data failed (result %d)\n",
						lo->disk->disk_name, result);
				goto error_out;
			}
		}
	}
	return 0;

error_out:
	return -EIO;
}

static struct request *nbd_find_request(struct nbd_device *lo,
					struct request *xreq)
{
	struct request *req, *tmp;
	int err;

	err = wait_event_interruptible(lo->active_wq, lo->active_req != xreq);
	if (unlikely(err))
		goto out;

	spin_lock(&lo->queue_lock);
	list_for_each_entry_safe(req, tmp, &lo->queue_head, queuelist) {
		if (req != xreq)
			continue;
		list_del_init(&req->queuelist);
		spin_unlock(&lo->queue_lock);
		return req;
	}
	spin_unlock(&lo->queue_lock);

	err = -ENOENT;

out:
	return ERR_PTR(err);
}

static inline int sock_recv_bvec(struct nbd_device *lo, struct bio_vec *bvec)
{
	int result;
	void *kaddr = kmap(bvec->bv_page);
	result = sock_xmit(lo, 0, kaddr + bvec->bv_offset, bvec->bv_len,
			MSG_WAITALL);
	kunmap(bvec->bv_page);
	return result;
}

/* NULL returned = something went wrong, inform userspace */
static struct request *nbd_read_stat(struct nbd_device *lo)
{
	int result;
	struct nbd_reply reply;
	struct request *req;

	reply.magic = 0;
	result = sock_xmit(lo, 0, &reply, sizeof(reply), MSG_WAITALL);
	if (result <= 0) {
		printk(KERN_ERR "%s: Receive control failed (result %d)\n",
				lo->disk->disk_name, result);
		goto harderror;
	}

	if (ntohl(reply.magic) != NBD_REPLY_MAGIC) {
		printk(KERN_ERR "%s: Wrong magic (0x%lx)\n",
				lo->disk->disk_name,
				(unsigned long)ntohl(reply.magic));
		result = -EPROTO;
		goto harderror;
	}

	req = nbd_find_request(lo, *(struct request **)reply.handle);
	if (IS_ERR(req)) {
		result = PTR_ERR(req);
		if (result != -ENOENT)
			goto harderror;

		printk(KERN_ERR "%s: Unexpected reply (%p)\n",
				lo->disk->disk_name, reply.handle);
		result = -EBADR;
		goto harderror;
	}

	if (ntohl(reply.error)) {
		printk(KERN_ERR "%s: Other side returned error (%d)\n",
				lo->disk->disk_name, ntohl(reply.error));
		req->errors++;
		return req;
	}

	dprintk(DBG_RX, "%s: request %p: got reply\n",
			lo->disk->disk_name, req);
	if (nbd_cmd(req) == NBD_CMD_READ) {
		struct req_iterator iter;
		struct bio_vec *bvec;

		rq_for_each_segment(bvec, req, iter) {
			result = sock_recv_bvec(lo, bvec);
			if (result <= 0) {
				printk(KERN_ERR "%s: Receive data failed (result %d)\n",
						lo->disk->disk_name, result);
				req->errors++;
				return req;
			}
			dprintk(DBG_RX, "%s: request %p: got %d bytes data\n",
				lo->disk->disk_name, req, bvec->bv_len);
		}
	}
	return req;
harderror:
	lo->harderror = result;
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

static int nbd_do_it(struct nbd_device *lo)
{
	struct request *req;
	int ret;

	BUG_ON(lo->magic != LO_MAGIC);

	lo->pid = current->pid;
	ret = sysfs_create_file(&disk_to_dev(lo->disk)->kobj, &pid_attr.attr);
	if (ret) {
		printk(KERN_ERR "nbd: sysfs_create_file failed!");
		lo->pid = 0;
		return ret;
	}

	while ((req = nbd_read_stat(lo)) != NULL)
		nbd_end_request(req);

	sysfs_remove_file(&disk_to_dev(lo->disk)->kobj, &pid_attr.attr);
	lo->pid = 0;
	return 0;
}

static void nbd_clear_que(struct nbd_device *lo)
{
	struct request *req;

	BUG_ON(lo->magic != LO_MAGIC);

	/*
	 * Because we have set lo->sock to NULL under the tx_lock, all
	 * modifications to the list must have completed by now.  For
	 * the same reason, the active_req must be NULL.
	 *
	 * As a consequence, we don't need to take the spin lock while
	 * purging the list here.
	 */
	BUG_ON(lo->sock);
	BUG_ON(lo->active_req);

	while (!list_empty(&lo->queue_head)) {
		req = list_entry(lo->queue_head.next, struct request,
				 queuelist);
		list_del_init(&req->queuelist);
		req->errors++;
		nbd_end_request(req);
	}
}


static void nbd_handle_req(struct nbd_device *lo, struct request *req)
{
	if (req->cmd_type != REQ_TYPE_FS)
		goto error_out;

	nbd_cmd(req) = NBD_CMD_READ;
	if (rq_data_dir(req) == WRITE) {
		nbd_cmd(req) = NBD_CMD_WRITE;
		if (lo->flags & NBD_READ_ONLY) {
			printk(KERN_ERR "%s: Write on read-only\n",
					lo->disk->disk_name);
			goto error_out;
		}
	}

	req->errors = 0;

	mutex_lock(&lo->tx_lock);
	if (unlikely(!lo->sock)) {
		mutex_unlock(&lo->tx_lock);
		printk(KERN_ERR "%s: Attempted send on closed socket\n",
		       lo->disk->disk_name);
		goto error_out;
	}

	lo->active_req = req;

	if (nbd_send_req(lo, req) != 0) {
		printk(KERN_ERR "%s: Request send failed\n",
				lo->disk->disk_name);
		req->errors++;
		nbd_end_request(req);
	} else {
		spin_lock(&lo->queue_lock);
		list_add(&req->queuelist, &lo->queue_head);
		spin_unlock(&lo->queue_lock);
	}

	lo->active_req = NULL;
	mutex_unlock(&lo->tx_lock);
	wake_up_all(&lo->active_wq);

	return;

error_out:
	req->errors++;
	nbd_end_request(req);
}

static int nbd_thread(void *data)
{
	struct nbd_device *lo = data;
	struct request *req;

	set_user_nice(current, -20);
	while (!kthread_should_stop() || !list_empty(&lo->waiting_queue)) {
		/* wait for something to do */
		wait_event_interruptible(lo->waiting_wq,
					 kthread_should_stop() ||
					 !list_empty(&lo->waiting_queue));

		/* extract request */
		if (list_empty(&lo->waiting_queue))
			continue;

		spin_lock_irq(&lo->queue_lock);
		req = list_entry(lo->waiting_queue.next, struct request,
				 queuelist);
		list_del_init(&req->queuelist);
		spin_unlock_irq(&lo->queue_lock);

		/* handle request */
		nbd_handle_req(lo, req);
	}
	return 0;
}

/*
 * We always wait for result of write, for now. It would be nice to make it optional
 * in future
 * if ((rq_data_dir(req) == WRITE) && (lo->flags & NBD_WRITE_NOCHK))
 *   { printk( "Warning: Ignoring result!\n"); nbd_end_request( req ); }
 */

static void do_nbd_request(struct request_queue *q)
{
	struct request *req;
	
	while ((req = blk_fetch_request(q)) != NULL) {
		struct nbd_device *lo;

		spin_unlock_irq(q->queue_lock);

		dprintk(DBG_BLKDEV, "%s: request %p: dequeued (flags=%x)\n",
				req->rq_disk->disk_name, req, req->cmd_type);

		lo = req->rq_disk->private_data;

		BUG_ON(lo->magic != LO_MAGIC);

		if (unlikely(!lo->sock)) {
			printk(KERN_ERR "%s: Attempted send on closed socket\n",
				lo->disk->disk_name);
			req->errors++;
			nbd_end_request(req);
			spin_lock_irq(q->queue_lock);
			continue;
		}

		spin_lock_irq(&lo->queue_lock);
		list_add_tail(&req->queuelist, &lo->waiting_queue);
		spin_unlock_irq(&lo->queue_lock);

		wake_up(&lo->waiting_wq);

		spin_lock_irq(q->queue_lock);
	}
}

/* Must be called with tx_lock held */

static int __nbd_ioctl(struct block_device *bdev, struct nbd_device *lo,
		       unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case NBD_DISCONNECT: {
		struct request sreq;

	        printk(KERN_INFO "%s: NBD_DISCONNECT\n", lo->disk->disk_name);

		blk_rq_init(NULL, &sreq);
		sreq.cmd_type = REQ_TYPE_SPECIAL;
		nbd_cmd(&sreq) = NBD_CMD_DISC;
		if (!lo->sock)
			return -EINVAL;
		nbd_send_req(lo, &sreq);
                return 0;
	}
 
	case NBD_CLEAR_SOCK: {
		struct file *file;

		lo->sock = NULL;
		file = lo->file;
		lo->file = NULL;
		nbd_clear_que(lo);
		BUG_ON(!list_empty(&lo->queue_head));
		if (file)
			fput(file);
		return 0;
	}

	case NBD_SET_SOCK: {
		struct file *file;
		if (lo->file)
			return -EBUSY;
		file = fget(arg);
		if (file) {
			struct inode *inode = file->f_path.dentry->d_inode;
			if (S_ISSOCK(inode->i_mode)) {
				lo->file = file;
				lo->sock = SOCKET_I(inode);
				if (max_part > 0)
					bdev->bd_invalidated = 1;
				return 0;
			} else {
				fput(file);
			}
		}
		return -EINVAL;
	}

	case NBD_SET_BLKSIZE:
		lo->blksize = arg;
		lo->bytesize &= ~(lo->blksize-1);
		bdev->bd_inode->i_size = lo->bytesize;
		set_blocksize(bdev, lo->blksize);
		set_capacity(lo->disk, lo->bytesize >> 9);
		return 0;

	case NBD_SET_SIZE:
		lo->bytesize = arg & ~(lo->blksize-1);
		bdev->bd_inode->i_size = lo->bytesize;
		set_blocksize(bdev, lo->blksize);
		set_capacity(lo->disk, lo->bytesize >> 9);
		return 0;

	case NBD_SET_TIMEOUT:
		lo->xmit_timeout = arg * HZ;
		return 0;

	case NBD_SET_SIZE_BLOCKS:
		lo->bytesize = ((u64) arg) * lo->blksize;
		bdev->bd_inode->i_size = lo->bytesize;
		set_blocksize(bdev, lo->blksize);
		set_capacity(lo->disk, lo->bytesize >> 9);
		return 0;

	case NBD_DO_IT: {
		struct task_struct *thread;
		struct file *file;
		int error;

		if (lo->pid)
			return -EBUSY;
		if (!lo->file)
			return -EINVAL;

		mutex_unlock(&lo->tx_lock);

		thread = kthread_create(nbd_thread, lo, lo->disk->disk_name);
		if (IS_ERR(thread)) {
			mutex_lock(&lo->tx_lock);
			return PTR_ERR(thread);
		}
		wake_up_process(thread);
		error = nbd_do_it(lo);
		kthread_stop(thread);

		mutex_lock(&lo->tx_lock);
		if (error)
			return error;
		sock_shutdown(lo, 0);
		file = lo->file;
		lo->file = NULL;
		nbd_clear_que(lo);
		printk(KERN_WARNING "%s: queue cleared\n", lo->disk->disk_name);
		if (file)
			fput(file);
		lo->bytesize = 0;
		bdev->bd_inode->i_size = 0;
		set_capacity(lo->disk, 0);
		if (max_part > 0)
			ioctl_by_bdev(bdev, BLKRRPART, 0);
		return lo->harderror;
	}

	case NBD_CLEAR_QUE:
		/*
		 * This is for compatibility only.  The queue is always cleared
		 * by NBD_DO_IT or NBD_CLEAR_SOCK.
		 */
		BUG_ON(!lo->sock && !list_empty(&lo->queue_head));
		return 0;

	case NBD_PRINT_DEBUG:
		printk(KERN_INFO "%s: next = %p, prev = %p, head = %p\n",
			bdev->bd_disk->disk_name,
			lo->queue_head.next, lo->queue_head.prev,
			&lo->queue_head);
		return 0;
	}
	return -ENOTTY;
}

static int nbd_ioctl(struct block_device *bdev, fmode_t mode,
		     unsigned int cmd, unsigned long arg)
{
	struct nbd_device *lo = bdev->bd_disk->private_data;
	int error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	BUG_ON(lo->magic != LO_MAGIC);

	/* Anyone capable of this syscall can do *real bad* things */
	dprintk(DBG_IOCTL, "%s: nbd_ioctl cmd=%s(0x%x) arg=%lu\n",
			lo->disk->disk_name, ioctl_cmd_to_ascii(cmd), cmd, arg);

	mutex_lock(&nbd_mutex);
	mutex_lock(&lo->tx_lock);
	error = __nbd_ioctl(bdev, lo, cmd, arg);
	mutex_unlock(&lo->tx_lock);
	mutex_unlock(&nbd_mutex);

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
		printk(KERN_CRIT "nbd: max_part must be >= 0\n");
		return -EINVAL;
	}

	nbd_dev = kcalloc(nbds_max, sizeof(*nbd_dev), GFP_KERNEL);
	if (!nbd_dev)
		return -ENOMEM;

	part_shift = 0;
	if (max_part > 0)
		part_shift = fls(max_part);

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
		nbd_dev[i].magic = LO_MAGIC;
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
