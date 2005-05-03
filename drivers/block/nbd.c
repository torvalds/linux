/*
 * Network block device - make block devices work over TCP
 *
 * Note that you can not swap over this thing, yet. Seems to work but
 * deadlocks sometimes - you can not swap over TCP in general.
 * 
 * Copyright 1997-2000 Pavel Machek <pavel@ucw.cz>
 * Parts copyright 2001 Steven Whitehouse <steve@chygwyn.com>
 *
 * (part of code stolen from loop.c)
 *
 * 97-3-25 compiled 0-th version, not yet tested it 
 *   (it did not work, BTW) (later that day) HEY! it works!
 *   (bit later) hmm, not that much... 2:00am next day:
 *   yes, it works, but it gives something like 50kB/sec
 * 97-4-01 complete rewrite to make it possible for many requests at 
 *   once to be processed
 * 97-4-11 Making protocol independent of endianity etc.
 * 97-9-13 Cosmetic changes
 * 98-5-13 Attempt to make 64-bit-clean on 64-bit machines
 * 99-1-11 Attempt to make 64-bit-clean on 32-bit machines <ankry@mif.pg.gda.pl>
 * 01-2-27 Fix to store proper blockcount for kernel (calculated using
 *   BLOCK_SIZE_BITS, not device blocksize) <aga@permonline.ru>
 * 01-3-11 Make nbd work with new Linux block layer code. It now supports
 *   plugging like all the other block devices. Also added in MSG_MORE to
 *   reduce number of partial TCP segments sent. <steve@chygwyn.com>
 * 01-12-6 Fix deadlock condition by making queue locks independent of
 *   the transmit lock. <steve@chygwyn.com>
 * 02-10-11 Allow hung xmit to be aborted via SIGKILL & various fixes.
 *   <Paul.Clements@SteelEye.com> <James.Bottomley@SteelEye.com>
 * 03-06-22 Make nbd work with new linux 2.5 block layer design. This fixes
 *   memory corruption from module removal and possible memory corruption
 *   from sending/receiving disk data. <ldl@aros.net>
 * 03-06-23 Cosmetic changes. <ldl@aros.net>
 * 03-06-23 Enhance diagnostics support. <ldl@aros.net>
 * 03-06-24 Remove unneeded blksize_bits field from nbd_device struct.
 *   <ldl@aros.net>
 * 03-06-24 Cleanup PARANOIA usage & code. <ldl@aros.net>
 * 04-02-19 Remove PARANOIA, plus various cleanups (Paul Clements)
 * possible FIXME: make set_sock / set_blksize / set_size / do_it one syscall
 * why not: would need access_ok and friends, would share yet another
 *          structure with userland
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
#include <net/sock.h>

#include <linux/devfs_fs_kernel.h>

#include <asm/uaccess.h>
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
static unsigned int debugflags;
static unsigned int nbds_max = 16;
#endif /* NDEBUG */

static struct nbd_device nbd_dev[MAX_NBD];

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
	int uptodate = (req->errors == 0) ? 1 : 0;
	request_queue_t *q = req->q;
	unsigned long flags;

	dprintk(DBG_BLKDEV, "%s: request %p: %s\n", req->rq_disk->disk_name,
			req, uptodate? "done": "failed");

	spin_lock_irqsave(q->queue_lock, flags);
	if (!end_that_request_first(req, uptodate, req->nr_sectors)) {
		end_that_request_last(req);
	}
	spin_unlock_irqrestore(q->queue_lock, flags);
}

/*
 *  Send or receive packet.
 */
static int sock_xmit(struct socket *sock, int send, void *buf, int size,
		int msg_flags)
{
	int result;
	struct msghdr msg;
	struct kvec iov;
	unsigned long flags;
	sigset_t oldset;

	/* Allow interception of SIGKILL only
	 * Don't allow other signals to interrupt the transmission */
	spin_lock_irqsave(&current->sighand->siglock, flags);
	oldset = current->blocked;
	sigfillset(&current->blocked);
	sigdelsetmask(&current->blocked, sigmask(SIGKILL));
	recalc_sigpending();
	spin_unlock_irqrestore(&current->sighand->siglock, flags);

	do {
		sock->sk->sk_allocation = GFP_NOIO;
		iov.iov_base = buf;
		iov.iov_len = size;
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_namelen = 0;
		msg.msg_flags = msg_flags | MSG_NOSIGNAL;

		if (send)
			result = kernel_sendmsg(sock, &msg, &iov, 1, size);
		else
			result = kernel_recvmsg(sock, &msg, &iov, 1, size, 0);

		if (signal_pending(current)) {
			siginfo_t info;
			spin_lock_irqsave(&current->sighand->siglock, flags);
			printk(KERN_WARNING "nbd (pid %d: %s) got signal %d\n",
				current->pid, current->comm, 
				dequeue_signal(current, &current->blocked, &info));
			spin_unlock_irqrestore(&current->sighand->siglock, flags);
			result = -EINTR;
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

	spin_lock_irqsave(&current->sighand->siglock, flags);
	current->blocked = oldset;
	recalc_sigpending();
	spin_unlock_irqrestore(&current->sighand->siglock, flags);

	return result;
}

static inline int sock_send_bvec(struct socket *sock, struct bio_vec *bvec,
		int flags)
{
	int result;
	void *kaddr = kmap(bvec->bv_page);
	result = sock_xmit(sock, 1, kaddr + bvec->bv_offset, bvec->bv_len,
			flags);
	kunmap(bvec->bv_page);
	return result;
}

static int nbd_send_req(struct nbd_device *lo, struct request *req)
{
	int result, i, flags;
	struct nbd_request request;
	unsigned long size = req->nr_sectors << 9;
	struct socket *sock = lo->sock;

	request.magic = htonl(NBD_REQUEST_MAGIC);
	request.type = htonl(nbd_cmd(req));
	request.from = cpu_to_be64((u64) req->sector << 9);
	request.len = htonl(size);
	memcpy(request.handle, &req, sizeof(req));

	down(&lo->tx_lock);

	if (!sock || !lo->sock) {
		printk(KERN_ERR "%s: Attempted send on closed socket\n",
				lo->disk->disk_name);
		goto error_out;
	}

	dprintk(DBG_TX, "%s: request %p: sending control (%s@%llu,%luB)\n",
			lo->disk->disk_name, req,
			nbdcmd_to_ascii(nbd_cmd(req)),
			(unsigned long long)req->sector << 9,
			req->nr_sectors << 9);
	result = sock_xmit(sock, 1, &request, sizeof(request),
			(nbd_cmd(req) == NBD_CMD_WRITE)? MSG_MORE: 0);
	if (result <= 0) {
		printk(KERN_ERR "%s: Send control failed (result %d)\n",
				lo->disk->disk_name, result);
		goto error_out;
	}

	if (nbd_cmd(req) == NBD_CMD_WRITE) {
		struct bio *bio;
		/*
		 * we are really probing at internals to determine
		 * whether to set MSG_MORE or not...
		 */
		rq_for_each_bio(bio, req) {
			struct bio_vec *bvec;
			bio_for_each_segment(bvec, bio, i) {
				flags = 0;
				if ((i < (bio->bi_vcnt - 1)) || bio->bi_next)
					flags = MSG_MORE;
				dprintk(DBG_TX, "%s: request %p: sending %d bytes data\n",
						lo->disk->disk_name, req,
						bvec->bv_len);
				result = sock_send_bvec(sock, bvec, flags);
				if (result <= 0) {
					printk(KERN_ERR "%s: Send data failed (result %d)\n",
							lo->disk->disk_name,
							result);
					goto error_out;
				}
			}
		}
	}
	up(&lo->tx_lock);
	return 0;

error_out:
	up(&lo->tx_lock);
	return 1;
}

static struct request *nbd_find_request(struct nbd_device *lo, char *handle)
{
	struct request *req;
	struct list_head *tmp;
	struct request *xreq;

	memcpy(&xreq, handle, sizeof(xreq));

	spin_lock(&lo->queue_lock);
	list_for_each(tmp, &lo->queue_head) {
		req = list_entry(tmp, struct request, queuelist);
		if (req != xreq)
			continue;
		list_del_init(&req->queuelist);
		spin_unlock(&lo->queue_lock);
		return req;
	}
	spin_unlock(&lo->queue_lock);
	return NULL;
}

static inline int sock_recv_bvec(struct socket *sock, struct bio_vec *bvec)
{
	int result;
	void *kaddr = kmap(bvec->bv_page);
	result = sock_xmit(sock, 0, kaddr + bvec->bv_offset, bvec->bv_len,
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
	struct socket *sock = lo->sock;

	reply.magic = 0;
	result = sock_xmit(sock, 0, &reply, sizeof(reply), MSG_WAITALL);
	if (result <= 0) {
		printk(KERN_ERR "%s: Receive control failed (result %d)\n",
				lo->disk->disk_name, result);
		goto harderror;
	}
	req = nbd_find_request(lo, reply.handle);
	if (req == NULL) {
		printk(KERN_ERR "%s: Unexpected reply (%p)\n",
				lo->disk->disk_name, reply.handle);
		result = -EBADR;
		goto harderror;
	}

	if (ntohl(reply.magic) != NBD_REPLY_MAGIC) {
		printk(KERN_ERR "%s: Wrong magic (0x%lx)\n",
				lo->disk->disk_name,
				(unsigned long)ntohl(reply.magic));
		result = -EPROTO;
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
		int i;
		struct bio *bio;
		rq_for_each_bio(bio, req) {
			struct bio_vec *bvec;
			bio_for_each_segment(bvec, bio, i) {
				result = sock_recv_bvec(sock, bvec);
				if (result <= 0) {
					printk(KERN_ERR "%s: Receive data failed (result %d)\n",
							lo->disk->disk_name,
							result);
					goto harderror;
				}
				dprintk(DBG_RX, "%s: request %p: got %d bytes data\n",
					lo->disk->disk_name, req, bvec->bv_len);
			}
		}
	}
	return req;
harderror:
	lo->harderror = result;
	return NULL;
}

static void nbd_do_it(struct nbd_device *lo)
{
	struct request *req;

	BUG_ON(lo->magic != LO_MAGIC);

	while ((req = nbd_read_stat(lo)) != NULL)
		nbd_end_request(req);
	return;
}

static void nbd_clear_que(struct nbd_device *lo)
{
	struct request *req;

	BUG_ON(lo->magic != LO_MAGIC);

	do {
		req = NULL;
		spin_lock(&lo->queue_lock);
		if (!list_empty(&lo->queue_head)) {
			req = list_entry(lo->queue_head.next, struct request, queuelist);
			list_del_init(&req->queuelist);
		}
		spin_unlock(&lo->queue_lock);
		if (req) {
			req->errors++;
			nbd_end_request(req);
		}
	} while (req);
}

/*
 * We always wait for result of write, for now. It would be nice to make it optional
 * in future
 * if ((req->cmd == WRITE) && (lo->flags & NBD_WRITE_NOCHK)) 
 *   { printk( "Warning: Ignoring result!\n"); nbd_end_request( req ); }
 */

static void do_nbd_request(request_queue_t * q)
{
	struct request *req;
	
	while ((req = elv_next_request(q)) != NULL) {
		struct nbd_device *lo;

		blkdev_dequeue_request(req);
		dprintk(DBG_BLKDEV, "%s: request %p: dequeued (flags=%lx)\n",
				req->rq_disk->disk_name, req, req->flags);

		if (!(req->flags & REQ_CMD))
			goto error_out;

		lo = req->rq_disk->private_data;

		BUG_ON(lo->magic != LO_MAGIC);

		if (!lo->file) {
			printk(KERN_ERR "%s: Request when not-ready\n",
					lo->disk->disk_name);
			goto error_out;
		}
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
		spin_unlock_irq(q->queue_lock);

		spin_lock(&lo->queue_lock);

		if (!lo->file) {
			spin_unlock(&lo->queue_lock);
			printk(KERN_ERR "%s: failed between accept and semaphore, file lost\n",
					lo->disk->disk_name);
			req->errors++;
			nbd_end_request(req);
			spin_lock_irq(q->queue_lock);
			continue;
		}

		list_add(&req->queuelist, &lo->queue_head);
		spin_unlock(&lo->queue_lock);

		if (nbd_send_req(lo, req) != 0) {
			printk(KERN_ERR "%s: Request send failed\n",
					lo->disk->disk_name);
			if (nbd_find_request(lo, (char *)&req) != NULL) {
				/* we still own req */
				req->errors++;
				nbd_end_request(req);
			} else /* we're racing with nbd_clear_que */
				printk(KERN_DEBUG "nbd: can't find req\n");
		}

		spin_lock_irq(q->queue_lock);
		continue;

error_out:
		req->errors++;
		spin_unlock(q->queue_lock);
		nbd_end_request(req);
		spin_lock(q->queue_lock);
	}
	return;
}

static int nbd_ioctl(struct inode *inode, struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	struct nbd_device *lo = inode->i_bdev->bd_disk->private_data;
	int error;
	struct request sreq ;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	BUG_ON(lo->magic != LO_MAGIC);

	/* Anyone capable of this syscall can do *real bad* things */
	dprintk(DBG_IOCTL, "%s: nbd_ioctl cmd=%s(0x%x) arg=%lu\n",
			lo->disk->disk_name, ioctl_cmd_to_ascii(cmd), cmd, arg);

	switch (cmd) {
	case NBD_DISCONNECT:
	        printk(KERN_INFO "%s: NBD_DISCONNECT\n", lo->disk->disk_name);
		sreq.flags = REQ_SPECIAL;
		nbd_cmd(&sreq) = NBD_CMD_DISC;
		/*
		 * Set these to sane values in case server implementation
		 * fails to check the request type first and also to keep
		 * debugging output cleaner.
		 */
		sreq.sector = 0;
		sreq.nr_sectors = 0;
                if (!lo->sock)
			return -EINVAL;
                nbd_send_req(lo, &sreq);
                return 0;
 
	case NBD_CLEAR_SOCK:
		error = 0;
		down(&lo->tx_lock);
		lo->sock = NULL;
		up(&lo->tx_lock);
		spin_lock(&lo->queue_lock);
		file = lo->file;
		lo->file = NULL;
		spin_unlock(&lo->queue_lock);
		nbd_clear_que(lo);
		spin_lock(&lo->queue_lock);
		if (!list_empty(&lo->queue_head)) {
			printk(KERN_ERR "nbd: disconnect: some requests are in progress -> please try again.\n");
			error = -EBUSY;
		}
		spin_unlock(&lo->queue_lock);
		if (file)
			fput(file);
		return error;
	case NBD_SET_SOCK:
		if (lo->file)
			return -EBUSY;
		error = -EINVAL;
		file = fget(arg);
		if (file) {
			inode = file->f_dentry->d_inode;
			if (S_ISSOCK(inode->i_mode)) {
				lo->file = file;
				lo->sock = SOCKET_I(inode);
				error = 0;
			} else {
				fput(file);
			}
		}
		return error;
	case NBD_SET_BLKSIZE:
		lo->blksize = arg;
		lo->bytesize &= ~(lo->blksize-1);
		inode->i_bdev->bd_inode->i_size = lo->bytesize;
		set_blocksize(inode->i_bdev, lo->blksize);
		set_capacity(lo->disk, lo->bytesize >> 9);
		return 0;
	case NBD_SET_SIZE:
		lo->bytesize = arg & ~(lo->blksize-1);
		inode->i_bdev->bd_inode->i_size = lo->bytesize;
		set_blocksize(inode->i_bdev, lo->blksize);
		set_capacity(lo->disk, lo->bytesize >> 9);
		return 0;
	case NBD_SET_SIZE_BLOCKS:
		lo->bytesize = ((u64) arg) * lo->blksize;
		inode->i_bdev->bd_inode->i_size = lo->bytesize;
		set_blocksize(inode->i_bdev, lo->blksize);
		set_capacity(lo->disk, lo->bytesize >> 9);
		return 0;
	case NBD_DO_IT:
		if (!lo->file)
			return -EINVAL;
		nbd_do_it(lo);
		/* on return tidy up in case we have a signal */
		/* Forcibly shutdown the socket causing all listeners
		 * to error
		 *
		 * FIXME: This code is duplicated from sys_shutdown, but
		 * there should be a more generic interface rather than
		 * calling socket ops directly here */
		down(&lo->tx_lock);
		if (lo->sock) {
			printk(KERN_WARNING "%s: shutting down socket\n",
				lo->disk->disk_name);
			lo->sock->ops->shutdown(lo->sock,
				SEND_SHUTDOWN|RCV_SHUTDOWN);
			lo->sock = NULL;
		}
		up(&lo->tx_lock);
		spin_lock(&lo->queue_lock);
		file = lo->file;
		lo->file = NULL;
		spin_unlock(&lo->queue_lock);
		nbd_clear_que(lo);
		printk(KERN_WARNING "%s: queue cleared\n", lo->disk->disk_name);
		if (file)
			fput(file);
		return lo->harderror;
	case NBD_CLEAR_QUE:
		down(&lo->tx_lock);
		if (lo->sock) {
			up(&lo->tx_lock);
			return 0; /* probably should be error, but that would
				   * break "nbd-client -d", so just return 0 */
		}
		up(&lo->tx_lock);
		nbd_clear_que(lo);
		return 0;
	case NBD_PRINT_DEBUG:
		printk(KERN_INFO "%s: next = %p, prev = %p, head = %p\n",
			inode->i_bdev->bd_disk->disk_name,
			lo->queue_head.next, lo->queue_head.prev,
			&lo->queue_head);
		return 0;
	}
	return -EINVAL;
}

static struct block_device_operations nbd_fops =
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

	if (sizeof(struct nbd_request) != 28) {
		printk(KERN_CRIT "nbd: sizeof nbd_request needs to be 28 in order to work!\n" );
		return -EIO;
	}

	if (nbds_max > MAX_NBD) {
		printk(KERN_CRIT "nbd: cannot allocate more than %u nbds; %u requested.\n", MAX_NBD,
				nbds_max);
		return -EINVAL;
	}

	for (i = 0; i < nbds_max; i++) {
		struct gendisk *disk = alloc_disk(1);
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
	}

	if (register_blkdev(NBD_MAJOR, "nbd")) {
		err = -EIO;
		goto out;
	}

	printk(KERN_INFO "nbd: registered device at major %d\n", NBD_MAJOR);
	dprintk(DBG_INIT, "nbd: debugflags=0x%x\n", debugflags);

	devfs_mk_dir("nbd");
	for (i = 0; i < nbds_max; i++) {
		struct gendisk *disk = nbd_dev[i].disk;
		nbd_dev[i].file = NULL;
		nbd_dev[i].magic = LO_MAGIC;
		nbd_dev[i].flags = 0;
		spin_lock_init(&nbd_dev[i].queue_lock);
		INIT_LIST_HEAD(&nbd_dev[i].queue_head);
		init_MUTEX(&nbd_dev[i].tx_lock);
		nbd_dev[i].blksize = 1024;
		nbd_dev[i].bytesize = 0x7ffffc00ULL << 10; /* 2TB */
		disk->major = NBD_MAJOR;
		disk->first_minor = i;
		disk->fops = &nbd_fops;
		disk->private_data = &nbd_dev[i];
		disk->flags |= GENHD_FL_SUPPRESS_PARTITION_INFO;
		sprintf(disk->disk_name, "nbd%d", i);
		sprintf(disk->devfs_name, "nbd/%d", i);
		set_capacity(disk, 0x7ffffc00ULL << 1); /* 2 TB */
		add_disk(disk);
	}

	return 0;
out:
	while (i--) {
		blk_cleanup_queue(nbd_dev[i].disk->queue);
		put_disk(nbd_dev[i].disk);
	}
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
	devfs_remove("nbd");
	unregister_blkdev(NBD_MAJOR, "nbd");
	printk(KERN_INFO "nbd: unregistered device at major %d\n", NBD_MAJOR);
}

module_init(nbd_init);
module_exit(nbd_cleanup);

MODULE_DESCRIPTION("Network Block Device");
MODULE_LICENSE("GPL");

module_param(nbds_max, int, 0444);
MODULE_PARM_DESC(nbds_max, "How many network block devices to initialize.");
#ifndef NDEBUG
module_param(debugflags, int, 0644);
MODULE_PARM_DESC(debugflags, "flags for controlling debug output");
#endif
