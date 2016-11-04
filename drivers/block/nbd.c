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
#include <linux/blk-mq.h>

#include <asm/uaccess.h>
#include <asm/types.h>

#include <linux/nbd.h>

#define NBD_TIMEDOUT			0
#define NBD_DISCONNECT_REQUESTED	1

struct nbd_device {
	u32 flags;
	unsigned long runtime_flags;
	struct socket * sock;	/* If == NULL, device is not ready, yet	*/
	int magic;

	struct blk_mq_tag_set tag_set;

	struct mutex tx_lock;
	struct gendisk *disk;
	int blksize;
	loff_t bytesize;

	/* protects initialization and shutdown of the socket */
	spinlock_t sock_lock;
	struct task_struct *task_recv;
	struct task_struct *task_send;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *dbg_dir;
#endif
};

struct nbd_cmd {
	struct nbd_device *nbd;
	struct list_head list;
};

#if IS_ENABLED(CONFIG_DEBUG_FS)
static struct dentry *nbd_dbg_dir;
#endif

#define nbd_name(nbd) ((nbd)->disk->disk_name)

#define NBD_MAGIC 0x68797548

static unsigned int nbds_max = 16;
static struct nbd_device *nbd_dev;
static int max_part;

static inline struct device *nbd_to_dev(struct nbd_device *nbd)
{
	return disk_to_dev(nbd->disk);
}

static bool nbd_is_connected(struct nbd_device *nbd)
{
	return !!nbd->task_recv;
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

static int nbd_size_clear(struct nbd_device *nbd, struct block_device *bdev)
{
	bdev->bd_inode->i_size = 0;
	set_capacity(nbd->disk, 0);
	kobject_uevent(&nbd_to_dev(nbd)->kobj, KOBJ_CHANGE);

	return 0;
}

static void nbd_size_update(struct nbd_device *nbd, struct block_device *bdev)
{
	if (!nbd_is_connected(nbd))
		return;

	bdev->bd_inode->i_size = nbd->bytesize;
	set_capacity(nbd->disk, nbd->bytesize >> 9);
	kobject_uevent(&nbd_to_dev(nbd)->kobj, KOBJ_CHANGE);
}

static int nbd_size_set(struct nbd_device *nbd, struct block_device *bdev,
			int blocksize, int nr_blocks)
{
	int ret;

	ret = set_blocksize(bdev, blocksize);
	if (ret)
		return ret;

	nbd->blksize = blocksize;
	nbd->bytesize = (loff_t)blocksize * (loff_t)nr_blocks;

	nbd_size_update(nbd, bdev);

	return 0;
}

static void nbd_end_request(struct nbd_cmd *cmd)
{
	struct nbd_device *nbd = cmd->nbd;
	struct request *req = blk_mq_rq_from_pdu(cmd);
	int error = req->errors ? -EIO : 0;

	dev_dbg(nbd_to_dev(nbd), "request %p: %s\n", cmd,
		error ? "failed" : "done");

	blk_mq_complete_request(req, error);
}

/*
 * Forcibly shutdown the socket causing all listeners to error
 */
static void sock_shutdown(struct nbd_device *nbd)
{
	struct socket *sock;

	spin_lock(&nbd->sock_lock);

	if (!nbd->sock) {
		spin_unlock_irq(&nbd->sock_lock);
		return;
	}

	sock = nbd->sock;
	dev_warn(disk_to_dev(nbd->disk), "shutting down socket\n");
	nbd->sock = NULL;
	spin_unlock(&nbd->sock_lock);

	kernel_sock_shutdown(sock, SHUT_RDWR);
	sockfd_put(sock);
}

static enum blk_eh_timer_return nbd_xmit_timeout(struct request *req,
						 bool reserved)
{
	struct nbd_cmd *cmd = blk_mq_rq_to_pdu(req);
	struct nbd_device *nbd = cmd->nbd;
	struct socket *sock = NULL;

	spin_lock(&nbd->sock_lock);

	set_bit(NBD_TIMEDOUT, &nbd->runtime_flags);

	if (nbd->sock) {
		sock = nbd->sock;
		get_file(sock->file);
	}

	spin_unlock(&nbd->sock_lock);
	if (sock) {
		kernel_sock_shutdown(sock, SHUT_RDWR);
		sockfd_put(sock);
	}

	req->errors++;
	dev_err(nbd_to_dev(nbd), "Connection timed out, shutting down connection\n");
	return BLK_EH_HANDLED;
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
	unsigned long pflags = current->flags;

	if (unlikely(!sock)) {
		dev_err(disk_to_dev(nbd->disk),
			"Attempted %s on closed socket in sock_xmit\n",
			(send ? "send" : "recv"));
		return -EINVAL;
	}

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
static int nbd_send_cmd(struct nbd_device *nbd, struct nbd_cmd *cmd)
{
	struct request *req = blk_mq_rq_from_pdu(cmd);
	int result, flags;
	struct nbd_request request;
	unsigned long size = blk_rq_bytes(req);
	u32 type;

	if (req->cmd_type == REQ_TYPE_DRV_PRIV)
		type = NBD_CMD_DISC;
	else if (req_op(req) == REQ_OP_DISCARD)
		type = NBD_CMD_TRIM;
	else if (req_op(req) == REQ_OP_FLUSH)
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
	memcpy(request.handle, &req->tag, sizeof(req->tag));

	dev_dbg(nbd_to_dev(nbd), "request %p: sending control (%s@%llu,%uB)\n",
		cmd, nbdcmd_to_ascii(type),
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
				cmd, bvec.bv_len);
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
static struct nbd_cmd *nbd_read_stat(struct nbd_device *nbd)
{
	int result;
	struct nbd_reply reply;
	struct nbd_cmd *cmd;
	struct request *req = NULL;
	u16 hwq;
	int tag;

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

	memcpy(&tag, reply.handle, sizeof(int));

	hwq = blk_mq_unique_tag_to_hwq(tag);
	if (hwq < nbd->tag_set.nr_hw_queues)
		req = blk_mq_tag_to_rq(nbd->tag_set.tags[hwq],
				       blk_mq_unique_tag_to_tag(tag));
	if (!req || !blk_mq_request_started(req)) {
		dev_err(disk_to_dev(nbd->disk), "Unexpected reply (%d) %p\n",
			tag, req);
		return ERR_PTR(-ENOENT);
	}
	cmd = blk_mq_rq_to_pdu(req);

	if (ntohl(reply.error)) {
		dev_err(disk_to_dev(nbd->disk), "Other side returned error (%d)\n",
			ntohl(reply.error));
		req->errors++;
		return cmd;
	}

	dev_dbg(nbd_to_dev(nbd), "request %p: got reply\n", cmd);
	if (rq_data_dir(req) != WRITE) {
		struct req_iterator iter;
		struct bio_vec bvec;

		rq_for_each_segment(bvec, req, iter) {
			result = sock_recv_bvec(nbd, &bvec);
			if (result <= 0) {
				dev_err(disk_to_dev(nbd->disk), "Receive data failed (result %d)\n",
					result);
				req->errors++;
				return cmd;
			}
			dev_dbg(nbd_to_dev(nbd), "request %p: got %d bytes data\n",
				cmd, bvec.bv_len);
		}
	}
	return cmd;
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

static int nbd_thread_recv(struct nbd_device *nbd, struct block_device *bdev)
{
	struct nbd_cmd *cmd;
	int ret;

	BUG_ON(nbd->magic != NBD_MAGIC);

	sk_set_memalloc(nbd->sock->sk);

	ret = device_create_file(disk_to_dev(nbd->disk), &pid_attr);
	if (ret) {
		dev_err(disk_to_dev(nbd->disk), "device_create_file failed!\n");
		return ret;
	}

	nbd_size_update(nbd, bdev);

	while (1) {
		cmd = nbd_read_stat(nbd);
		if (IS_ERR(cmd)) {
			ret = PTR_ERR(cmd);
			break;
		}

		nbd_end_request(cmd);
	}

	nbd_size_clear(nbd, bdev);

	device_remove_file(disk_to_dev(nbd->disk), &pid_attr);
	return ret;
}

static void nbd_clear_req(struct request *req, void *data, bool reserved)
{
	struct nbd_cmd *cmd;

	if (!blk_mq_request_started(req))
		return;
	cmd = blk_mq_rq_to_pdu(req);
	req->errors++;
	nbd_end_request(cmd);
}

static void nbd_clear_que(struct nbd_device *nbd)
{
	BUG_ON(nbd->magic != NBD_MAGIC);

	/*
	 * Because we have set nbd->sock to NULL under the tx_lock, all
	 * modifications to the list must have completed by now.
	 */
	BUG_ON(nbd->sock);

	blk_mq_tagset_busy_iter(&nbd->tag_set, nbd_clear_req, NULL);
	dev_dbg(disk_to_dev(nbd->disk), "queue cleared\n");
}


static void nbd_handle_cmd(struct nbd_cmd *cmd)
{
	struct request *req = blk_mq_rq_from_pdu(cmd);
	struct nbd_device *nbd = cmd->nbd;

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
	nbd->task_send = current;
	if (unlikely(!nbd->sock)) {
		mutex_unlock(&nbd->tx_lock);
		dev_err(disk_to_dev(nbd->disk),
			"Attempted send on closed socket\n");
		goto error_out;
	}

	if (nbd_send_cmd(nbd, cmd) != 0) {
		dev_err(disk_to_dev(nbd->disk), "Request send failed\n");
		req->errors++;
		nbd_end_request(cmd);
	}

	nbd->task_send = NULL;
	mutex_unlock(&nbd->tx_lock);

	return;

error_out:
	req->errors++;
	nbd_end_request(cmd);
}

static int nbd_queue_rq(struct blk_mq_hw_ctx *hctx,
			const struct blk_mq_queue_data *bd)
{
	struct nbd_cmd *cmd = blk_mq_rq_to_pdu(bd->rq);

	blk_mq_start_request(bd->rq);
	nbd_handle_cmd(cmd);
	return BLK_MQ_RQ_QUEUE_OK;
}

static int nbd_set_socket(struct nbd_device *nbd, struct socket *sock)
{
	int ret = 0;

	spin_lock_irq(&nbd->sock_lock);

	if (nbd->sock) {
		ret = -EBUSY;
		goto out;
	}

	nbd->sock = sock;

out:
	spin_unlock_irq(&nbd->sock_lock);

	return ret;
}

/* Reset all properties of an NBD device */
static void nbd_reset(struct nbd_device *nbd)
{
	nbd->runtime_flags = 0;
	nbd->blksize = 1024;
	nbd->bytesize = 0;
	set_capacity(nbd->disk, 0);
	nbd->flags = 0;
	nbd->tag_set.timeout = 0;
	queue_flag_clear_unlocked(QUEUE_FLAG_DISCARD, nbd->disk->queue);
}

static void nbd_bdev_reset(struct block_device *bdev)
{
	set_device_ro(bdev, false);
	bdev->bd_inode->i_size = 0;
	if (max_part > 0) {
		blkdev_reread_part(bdev);
		bdev->bd_invalidated = 1;
	}
}

static void nbd_parse_flags(struct nbd_device *nbd, struct block_device *bdev)
{
	if (nbd->flags & NBD_FLAG_READ_ONLY)
		set_device_ro(bdev, true);
	if (nbd->flags & NBD_FLAG_SEND_TRIM)
		queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, nbd->disk->queue);
	if (nbd->flags & NBD_FLAG_SEND_FLUSH)
		blk_queue_write_cache(nbd->disk->queue, true, false);
	else
		blk_queue_write_cache(nbd->disk->queue, false, false);
}

static int nbd_dev_dbg_init(struct nbd_device *nbd);
static void nbd_dev_dbg_close(struct nbd_device *nbd);

/* Must be called with tx_lock held */

static int __nbd_ioctl(struct block_device *bdev, struct nbd_device *nbd,
		       unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case NBD_DISCONNECT: {
		struct request *sreq;

		dev_info(disk_to_dev(nbd->disk), "NBD_DISCONNECT\n");
		if (!nbd->sock)
			return -EINVAL;

		sreq = blk_mq_alloc_request(bdev_get_queue(bdev), WRITE, 0);
		if (!sreq)
			return -ENOMEM;

		mutex_unlock(&nbd->tx_lock);
		fsync_bdev(bdev);
		mutex_lock(&nbd->tx_lock);
		sreq->cmd_type = REQ_TYPE_DRV_PRIV;

		/* Check again after getting mutex back.  */
		if (!nbd->sock) {
			blk_mq_free_request(sreq);
			return -EINVAL;
		}

		set_bit(NBD_DISCONNECT_REQUESTED, &nbd->runtime_flags);

		nbd_send_cmd(nbd, blk_mq_rq_to_pdu(sreq));
		blk_mq_free_request(sreq);
		return 0;
	}
 
	case NBD_CLEAR_SOCK:
		sock_shutdown(nbd);
		nbd_clear_que(nbd);
		kill_bdev(bdev);
		return 0;

	case NBD_SET_SOCK: {
		int err;
		struct socket *sock = sockfd_lookup(arg, &err);

		if (!sock)
			return err;

		err = nbd_set_socket(nbd, sock);
		if (!err && max_part)
			bdev->bd_invalidated = 1;

		return err;
	}

	case NBD_SET_BLKSIZE: {
		loff_t bsize = div_s64(nbd->bytesize, arg);

		return nbd_size_set(nbd, bdev, arg, bsize);
	}

	case NBD_SET_SIZE:
		return nbd_size_set(nbd, bdev, nbd->blksize,
				    arg / nbd->blksize);

	case NBD_SET_SIZE_BLOCKS:
		return nbd_size_set(nbd, bdev, nbd->blksize, arg);

	case NBD_SET_TIMEOUT:
		nbd->tag_set.timeout = arg * HZ;
		return 0;

	case NBD_SET_FLAGS:
		nbd->flags = arg;
		return 0;

	case NBD_DO_IT: {
		int error;

		if (nbd->task_recv)
			return -EBUSY;
		if (!nbd->sock)
			return -EINVAL;

		/* We have to claim the device under the lock */
		nbd->task_recv = current;
		mutex_unlock(&nbd->tx_lock);

		nbd_parse_flags(nbd, bdev);

		nbd_dev_dbg_init(nbd);
		error = nbd_thread_recv(nbd, bdev);
		nbd_dev_dbg_close(nbd);

		mutex_lock(&nbd->tx_lock);
		nbd->task_recv = NULL;

		sock_shutdown(nbd);
		nbd_clear_que(nbd);
		kill_bdev(bdev);
		nbd_bdev_reset(bdev);

		/* user requested, ignore socket errors */
		if (test_bit(NBD_DISCONNECT_REQUESTED, &nbd->runtime_flags))
			error = 0;
		if (test_bit(NBD_TIMEDOUT, &nbd->runtime_flags))
			error = -ETIMEDOUT;

		nbd_reset(nbd);

		return error;
	}

	case NBD_CLEAR_QUE:
		/*
		 * This is for compatibility only.  The queue is always cleared
		 * by NBD_DO_IT or NBD_CLEAR_SOCK.
		 */
		return 0;

	case NBD_PRINT_DEBUG:
		/*
		 * For compatibility only, we no longer keep a list of
		 * outstanding requests.
		 */
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
	.compat_ioctl =	nbd_ioctl,
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

	if (!nbd_dbg_dir)
		return -EIO;

	dir = debugfs_create_dir(nbd_name(nbd), nbd_dbg_dir);
	if (!dir) {
		dev_err(nbd_to_dev(nbd), "Failed to create debugfs dir for '%s'\n",
			nbd_name(nbd));
		return -EIO;
	}
	nbd->dbg_dir = dir;

	debugfs_create_file("tasks", 0444, dir, nbd, &nbd_dbg_tasks_ops);
	debugfs_create_u64("size_bytes", 0444, dir, &nbd->bytesize);
	debugfs_create_u32("timeout", 0444, dir, &nbd->tag_set.timeout);
	debugfs_create_u32("blocksize", 0444, dir, &nbd->blksize);
	debugfs_create_file("flags", 0444, dir, nbd, &nbd_dbg_flags_ops);

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
	if (!dbg_dir)
		return -EIO;

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

static int nbd_init_request(void *data, struct request *rq,
			    unsigned int hctx_idx, unsigned int request_idx,
			    unsigned int numa_node)
{
	struct nbd_cmd *cmd = blk_mq_rq_to_pdu(rq);

	cmd->nbd = data;
	INIT_LIST_HEAD(&cmd->list);
	return 0;
}

static struct blk_mq_ops nbd_mq_ops = {
	.queue_rq	= nbd_queue_rq,
	.init_request	= nbd_init_request,
	.timeout	= nbd_xmit_timeout,
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

		nbd_dev[i].tag_set.ops = &nbd_mq_ops;
		nbd_dev[i].tag_set.nr_hw_queues = 1;
		nbd_dev[i].tag_set.queue_depth = 128;
		nbd_dev[i].tag_set.numa_node = NUMA_NO_NODE;
		nbd_dev[i].tag_set.cmd_size = sizeof(struct nbd_cmd);
		nbd_dev[i].tag_set.flags = BLK_MQ_F_SHOULD_MERGE |
			BLK_MQ_F_SG_MERGE | BLK_MQ_F_BLOCKING;
		nbd_dev[i].tag_set.driver_data = &nbd_dev[i];

		err = blk_mq_alloc_tag_set(&nbd_dev[i].tag_set);
		if (err) {
			put_disk(disk);
			goto out;
		}

		/*
		 * The new linux 2.5 block layer implementation requires
		 * every gendisk to have its very own request_queue struct.
		 * These structs are big so we dynamically allocate them.
		 */
		disk->queue = blk_mq_init_queue(&nbd_dev[i].tag_set);
		if (!disk->queue) {
			blk_mq_free_tag_set(&nbd_dev[i].tag_set);
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
		spin_lock_init(&nbd_dev[i].sock_lock);
		mutex_init(&nbd_dev[i].tx_lock);
		disk->major = NBD_MAJOR;
		disk->first_minor = i << part_shift;
		disk->fops = &nbd_fops;
		disk->private_data = &nbd_dev[i];
		sprintf(disk->disk_name, "nbd%d", i);
		nbd_reset(&nbd_dev[i]);
		add_disk(disk);
	}

	return 0;
out:
	while (i--) {
		blk_mq_free_tag_set(&nbd_dev[i].tag_set);
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
			blk_mq_free_tag_set(&nbd_dev[i].tag_set);
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
