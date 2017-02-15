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

#include <linux/uaccess.h>
#include <asm/types.h>

#include <linux/nbd.h>

static DEFINE_IDR(nbd_index_idr);
static DEFINE_MUTEX(nbd_index_mutex);

struct nbd_sock {
	struct socket *sock;
	struct mutex tx_lock;
};

#define NBD_TIMEDOUT			0
#define NBD_DISCONNECT_REQUESTED	1
#define NBD_DISCONNECTED		2
#define NBD_RUNNING			3

struct nbd_device {
	u32 flags;
	unsigned long runtime_flags;
	struct nbd_sock **socks;
	int magic;

	struct blk_mq_tag_set tag_set;

	struct mutex config_lock;
	struct gendisk *disk;
	int num_connections;
	atomic_t recv_threads;
	wait_queue_head_t recv_wq;
	loff_t blksize;
	loff_t bytesize;

	struct task_struct *task_recv;
	struct task_struct *task_setup;

#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *dbg_dir;
#endif
};

struct nbd_cmd {
	struct nbd_device *nbd;
	struct completion send_complete;
};

#if IS_ENABLED(CONFIG_DEBUG_FS)
static struct dentry *nbd_dbg_dir;
#endif

#define nbd_name(nbd) ((nbd)->disk->disk_name)

#define NBD_MAGIC 0x68797548

static unsigned int nbds_max = 16;
static int max_part;
static struct workqueue_struct *recv_workqueue;
static int part_shift;

static int nbd_dev_dbg_init(struct nbd_device *nbd);
static void nbd_dev_dbg_close(struct nbd_device *nbd);


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
	bd_set_size(bdev, 0);
	set_capacity(nbd->disk, 0);
	kobject_uevent(&nbd_to_dev(nbd)->kobj, KOBJ_CHANGE);

	return 0;
}

static void nbd_size_update(struct nbd_device *nbd, struct block_device *bdev)
{
	blk_queue_logical_block_size(nbd->disk->queue, nbd->blksize);
	blk_queue_physical_block_size(nbd->disk->queue, nbd->blksize);
	bd_set_size(bdev, nbd->bytesize);
	set_capacity(nbd->disk, nbd->bytesize >> 9);
	kobject_uevent(&nbd_to_dev(nbd)->kobj, KOBJ_CHANGE);
}

static void nbd_size_set(struct nbd_device *nbd, struct block_device *bdev,
			loff_t blocksize, loff_t nr_blocks)
{
	nbd->blksize = blocksize;
	nbd->bytesize = blocksize * nr_blocks;
	if (nbd_is_connected(nbd))
		nbd_size_update(nbd, bdev);
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
	int i;

	if (nbd->num_connections == 0)
		return;
	if (test_and_set_bit(NBD_DISCONNECTED, &nbd->runtime_flags))
		return;

	for (i = 0; i < nbd->num_connections; i++) {
		struct nbd_sock *nsock = nbd->socks[i];
		mutex_lock(&nsock->tx_lock);
		kernel_sock_shutdown(nsock->sock, SHUT_RDWR);
		mutex_unlock(&nsock->tx_lock);
	}
	dev_warn(disk_to_dev(nbd->disk), "shutting down sockets\n");
}

static enum blk_eh_timer_return nbd_xmit_timeout(struct request *req,
						 bool reserved)
{
	struct nbd_cmd *cmd = blk_mq_rq_to_pdu(req);
	struct nbd_device *nbd = cmd->nbd;

	dev_err(nbd_to_dev(nbd), "Connection timed out, shutting down connection\n");
	set_bit(NBD_TIMEDOUT, &nbd->runtime_flags);
	req->errors++;

	mutex_lock(&nbd->config_lock);
	sock_shutdown(nbd);
	mutex_unlock(&nbd->config_lock);
	return BLK_EH_HANDLED;
}

/*
 *  Send or receive packet.
 */
static int sock_xmit(struct nbd_device *nbd, int index, int send, void *buf,
		     int size, int msg_flags)
{
	struct socket *sock = nbd->socks[index]->sock;
	int result;
	struct msghdr msg;
	struct kvec iov;
	unsigned long pflags = current->flags;

	if (unlikely(!sock)) {
		dev_err_ratelimited(disk_to_dev(nbd->disk),
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

static inline int sock_send_bvec(struct nbd_device *nbd, int index,
				 struct bio_vec *bvec, int flags)
{
	int result;
	void *kaddr = kmap(bvec->bv_page);
	result = sock_xmit(nbd, index, 1, kaddr + bvec->bv_offset,
			   bvec->bv_len, flags);
	kunmap(bvec->bv_page);
	return result;
}

/* always call with the tx_lock held */
static int nbd_send_cmd(struct nbd_device *nbd, struct nbd_cmd *cmd, int index)
{
	struct request *req = blk_mq_rq_from_pdu(cmd);
	int result;
	struct nbd_request request;
	unsigned long size = blk_rq_bytes(req);
	struct bio *bio;
	u32 type;
	u32 tag = blk_mq_unique_tag(req);

	switch (req_op(req)) {
	case REQ_OP_DISCARD:
		type = NBD_CMD_TRIM;
		break;
	case REQ_OP_FLUSH:
		type = NBD_CMD_FLUSH;
		break;
	case REQ_OP_WRITE:
		type = NBD_CMD_WRITE;
		break;
	case REQ_OP_READ:
		type = NBD_CMD_READ;
		break;
	default:
		return -EIO;
	}

	if (rq_data_dir(req) == WRITE &&
	    (nbd->flags & NBD_FLAG_READ_ONLY)) {
		dev_err_ratelimited(disk_to_dev(nbd->disk),
				    "Write on read-only\n");
		return -EIO;
	}

	memset(&request, 0, sizeof(request));
	request.magic = htonl(NBD_REQUEST_MAGIC);
	request.type = htonl(type);
	if (type != NBD_CMD_FLUSH) {
		request.from = cpu_to_be64((u64)blk_rq_pos(req) << 9);
		request.len = htonl(size);
	}
	memcpy(request.handle, &tag, sizeof(tag));

	dev_dbg(nbd_to_dev(nbd), "request %p: sending control (%s@%llu,%uB)\n",
		cmd, nbdcmd_to_ascii(type),
		(unsigned long long)blk_rq_pos(req) << 9, blk_rq_bytes(req));
	result = sock_xmit(nbd, index, 1, &request, sizeof(request),
			(type == NBD_CMD_WRITE) ? MSG_MORE : 0);
	if (result <= 0) {
		dev_err_ratelimited(disk_to_dev(nbd->disk),
			"Send control failed (result %d)\n", result);
		return -EIO;
	}

	if (type != NBD_CMD_WRITE)
		return 0;

	bio = req->bio;
	while (bio) {
		struct bio *next = bio->bi_next;
		struct bvec_iter iter;
		struct bio_vec bvec;

		bio_for_each_segment(bvec, bio, iter) {
			bool is_last = !next && bio_iter_last(bvec, iter);
			int flags = is_last ? 0 : MSG_MORE;

			dev_dbg(nbd_to_dev(nbd), "request %p: sending %d bytes data\n",
				cmd, bvec.bv_len);
			result = sock_send_bvec(nbd, index, &bvec, flags);
			if (result <= 0) {
				dev_err(disk_to_dev(nbd->disk),
					"Send data failed (result %d)\n",
					result);
				return -EIO;
			}
			/*
			 * The completion might already have come in,
			 * so break for the last one instead of letting
			 * the iterator do it. This prevents use-after-free
			 * of the bio.
			 */
			if (is_last)
				break;
		}
		bio = next;
	}
	return 0;
}

static inline int sock_recv_bvec(struct nbd_device *nbd, int index,
				 struct bio_vec *bvec)
{
	int result;
	void *kaddr = kmap(bvec->bv_page);
	result = sock_xmit(nbd, index, 0, kaddr + bvec->bv_offset,
			   bvec->bv_len, MSG_WAITALL);
	kunmap(bvec->bv_page);
	return result;
}

/* NULL returned = something went wrong, inform userspace */
static struct nbd_cmd *nbd_read_stat(struct nbd_device *nbd, int index)
{
	int result;
	struct nbd_reply reply;
	struct nbd_cmd *cmd;
	struct request *req = NULL;
	u16 hwq;
	u32 tag;

	reply.magic = 0;
	result = sock_xmit(nbd, index, 0, &reply, sizeof(reply), MSG_WAITALL);
	if (result <= 0) {
		if (!test_bit(NBD_DISCONNECTED, &nbd->runtime_flags) &&
		    !test_bit(NBD_DISCONNECT_REQUESTED, &nbd->runtime_flags))
			dev_err(disk_to_dev(nbd->disk),
				"Receive control failed (result %d)\n", result);
		return ERR_PTR(result);
	}

	if (ntohl(reply.magic) != NBD_REPLY_MAGIC) {
		dev_err(disk_to_dev(nbd->disk), "Wrong magic (0x%lx)\n",
				(unsigned long)ntohl(reply.magic));
		return ERR_PTR(-EPROTO);
	}

	memcpy(&tag, reply.handle, sizeof(u32));

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
			result = sock_recv_bvec(nbd, index, &bvec);
			if (result <= 0) {
				dev_err(disk_to_dev(nbd->disk), "Receive data failed (result %d)\n",
					result);
				req->errors++;
				return cmd;
			}
			dev_dbg(nbd_to_dev(nbd), "request %p: got %d bytes data\n",
				cmd, bvec.bv_len);
		}
	} else {
		/* See the comment in nbd_queue_rq. */
		wait_for_completion(&cmd->send_complete);
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

struct recv_thread_args {
	struct work_struct work;
	struct nbd_device *nbd;
	int index;
};

static void recv_work(struct work_struct *work)
{
	struct recv_thread_args *args = container_of(work,
						     struct recv_thread_args,
						     work);
	struct nbd_device *nbd = args->nbd;
	struct nbd_cmd *cmd;
	int ret = 0;

	BUG_ON(nbd->magic != NBD_MAGIC);
	while (1) {
		cmd = nbd_read_stat(nbd, args->index);
		if (IS_ERR(cmd)) {
			ret = PTR_ERR(cmd);
			break;
		}

		nbd_end_request(cmd);
	}

	/*
	 * We got an error, shut everybody down if this wasn't the result of a
	 * disconnect request.
	 */
	if (ret && !test_bit(NBD_DISCONNECT_REQUESTED, &nbd->runtime_flags))
		sock_shutdown(nbd);
	atomic_dec(&nbd->recv_threads);
	wake_up(&nbd->recv_wq);
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

	blk_mq_tagset_busy_iter(&nbd->tag_set, nbd_clear_req, NULL);
	dev_dbg(disk_to_dev(nbd->disk), "queue cleared\n");
}


static void nbd_handle_cmd(struct nbd_cmd *cmd, int index)
{
	struct request *req = blk_mq_rq_from_pdu(cmd);
	struct nbd_device *nbd = cmd->nbd;
	struct nbd_sock *nsock;

	if (index >= nbd->num_connections) {
		dev_err_ratelimited(disk_to_dev(nbd->disk),
				    "Attempted send on invalid socket\n");
		goto error_out;
	}

	if (test_bit(NBD_DISCONNECTED, &nbd->runtime_flags)) {
		dev_err_ratelimited(disk_to_dev(nbd->disk),
				    "Attempted send on closed socket\n");
		goto error_out;
	}

	req->errors = 0;

	nsock = nbd->socks[index];
	mutex_lock(&nsock->tx_lock);
	if (unlikely(!nsock->sock)) {
		mutex_unlock(&nsock->tx_lock);
		dev_err_ratelimited(disk_to_dev(nbd->disk),
				    "Attempted send on closed socket\n");
		goto error_out;
	}

	if (nbd_send_cmd(nbd, cmd, index) != 0) {
		dev_err_ratelimited(disk_to_dev(nbd->disk),
				    "Request send failed\n");
		req->errors++;
		nbd_end_request(cmd);
	}

	mutex_unlock(&nsock->tx_lock);

	return;

error_out:
	req->errors++;
	nbd_end_request(cmd);
}

static int nbd_queue_rq(struct blk_mq_hw_ctx *hctx,
			const struct blk_mq_queue_data *bd)
{
	struct nbd_cmd *cmd = blk_mq_rq_to_pdu(bd->rq);

	/*
	 * Since we look at the bio's to send the request over the network we
	 * need to make sure the completion work doesn't mark this request done
	 * before we are done doing our send.  This keeps us from dereferencing
	 * freed data if we have particularly fast completions (ie we get the
	 * completion before we exit sock_xmit on the last bvec) or in the case
	 * that the server is misbehaving (or there was an error) before we're
	 * done sending everything over the wire.
	 */
	init_completion(&cmd->send_complete);
	blk_mq_start_request(bd->rq);
	nbd_handle_cmd(cmd, hctx->queue_num);
	complete(&cmd->send_complete);

	return BLK_MQ_RQ_QUEUE_OK;
}

static int nbd_add_socket(struct nbd_device *nbd, struct block_device *bdev,
			  unsigned long arg)
{
	struct socket *sock;
	struct nbd_sock **socks;
	struct nbd_sock *nsock;
	int err;

	sock = sockfd_lookup(arg, &err);
	if (!sock)
		return err;

	if (!nbd->task_setup)
		nbd->task_setup = current;
	if (nbd->task_setup != current) {
		dev_err(disk_to_dev(nbd->disk),
			"Device being setup by another task");
		return -EINVAL;
	}

	socks = krealloc(nbd->socks, (nbd->num_connections + 1) *
			 sizeof(struct nbd_sock *), GFP_KERNEL);
	if (!socks)
		return -ENOMEM;
	nsock = kzalloc(sizeof(struct nbd_sock), GFP_KERNEL);
	if (!nsock)
		return -ENOMEM;

	nbd->socks = socks;

	mutex_init(&nsock->tx_lock);
	nsock->sock = sock;
	socks[nbd->num_connections++] = nsock;

	if (max_part)
		bdev->bd_invalidated = 1;
	return 0;
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

static void send_disconnects(struct nbd_device *nbd)
{
	struct nbd_request request = {};
	int i, ret;

	request.magic = htonl(NBD_REQUEST_MAGIC);
	request.type = htonl(NBD_CMD_DISC);

	for (i = 0; i < nbd->num_connections; i++) {
		ret = sock_xmit(nbd, i, 1, &request, sizeof(request), 0);
		if (ret <= 0)
			dev_err(disk_to_dev(nbd->disk),
				"Send disconnect failed %d\n", ret);
	}
}

static int nbd_disconnect(struct nbd_device *nbd, struct block_device *bdev)
{
	dev_info(disk_to_dev(nbd->disk), "NBD_DISCONNECT\n");
	if (!nbd->socks)
		return -EINVAL;

	mutex_unlock(&nbd->config_lock);
	fsync_bdev(bdev);
	mutex_lock(&nbd->config_lock);

	/* Check again after getting mutex back.  */
	if (!nbd->socks)
		return -EINVAL;

	if (!test_and_set_bit(NBD_DISCONNECT_REQUESTED,
			      &nbd->runtime_flags))
		send_disconnects(nbd);
	return 0;
}

static int nbd_clear_sock(struct nbd_device *nbd, struct block_device *bdev)
{
	sock_shutdown(nbd);
	nbd_clear_que(nbd);
	kill_bdev(bdev);
	nbd_bdev_reset(bdev);
	/*
	 * We want to give the run thread a chance to wait for everybody
	 * to clean up and then do it's own cleanup.
	 */
	if (!test_bit(NBD_RUNNING, &nbd->runtime_flags) &&
	    nbd->num_connections) {
		int i;

		for (i = 0; i < nbd->num_connections; i++)
			kfree(nbd->socks[i]);
		kfree(nbd->socks);
		nbd->socks = NULL;
		nbd->num_connections = 0;
	}
	nbd->task_setup = NULL;

	return 0;
}

static int nbd_start_device(struct nbd_device *nbd, struct block_device *bdev)
{
	struct recv_thread_args *args;
	int num_connections = nbd->num_connections;
	int error = 0, i;

	if (nbd->task_recv)
		return -EBUSY;
	if (!nbd->socks)
		return -EINVAL;
	if (num_connections > 1 &&
	    !(nbd->flags & NBD_FLAG_CAN_MULTI_CONN)) {
		dev_err(disk_to_dev(nbd->disk), "server does not support multiple connections per device.\n");
		error = -EINVAL;
		goto out_err;
	}

	set_bit(NBD_RUNNING, &nbd->runtime_flags);
	blk_mq_update_nr_hw_queues(&nbd->tag_set, nbd->num_connections);
	args = kcalloc(num_connections, sizeof(*args), GFP_KERNEL);
	if (!args) {
		error = -ENOMEM;
		goto out_err;
	}
	nbd->task_recv = current;
	mutex_unlock(&nbd->config_lock);

	nbd_parse_flags(nbd, bdev);

	error = device_create_file(disk_to_dev(nbd->disk), &pid_attr);
	if (error) {
		dev_err(disk_to_dev(nbd->disk), "device_create_file failed!\n");
		goto out_recv;
	}

	nbd_size_update(nbd, bdev);

	nbd_dev_dbg_init(nbd);
	for (i = 0; i < num_connections; i++) {
		sk_set_memalloc(nbd->socks[i]->sock->sk);
		atomic_inc(&nbd->recv_threads);
		INIT_WORK(&args[i].work, recv_work);
		args[i].nbd = nbd;
		args[i].index = i;
		queue_work(recv_workqueue, &args[i].work);
	}
	wait_event_interruptible(nbd->recv_wq,
				 atomic_read(&nbd->recv_threads) == 0);
	for (i = 0; i < num_connections; i++)
		flush_work(&args[i].work);
	nbd_dev_dbg_close(nbd);
	nbd_size_clear(nbd, bdev);
	device_remove_file(disk_to_dev(nbd->disk), &pid_attr);
out_recv:
	mutex_lock(&nbd->config_lock);
	nbd->task_recv = NULL;
out_err:
	clear_bit(NBD_RUNNING, &nbd->runtime_flags);
	nbd_clear_sock(nbd, bdev);

	/* user requested, ignore socket errors */
	if (test_bit(NBD_DISCONNECT_REQUESTED, &nbd->runtime_flags))
		error = 0;
	if (test_bit(NBD_TIMEDOUT, &nbd->runtime_flags))
		error = -ETIMEDOUT;

	nbd_reset(nbd);
	return error;
}

/* Must be called with config_lock held */
static int __nbd_ioctl(struct block_device *bdev, struct nbd_device *nbd,
		       unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case NBD_DISCONNECT:
		return nbd_disconnect(nbd, bdev);
	case NBD_CLEAR_SOCK:
		return nbd_clear_sock(nbd, bdev);
	case NBD_SET_SOCK:
		return nbd_add_socket(nbd, bdev, arg);
	case NBD_SET_BLKSIZE:
		nbd_size_set(nbd, bdev, arg,
			     div_s64(nbd->bytesize, arg));
		return 0;
	case NBD_SET_SIZE:
		nbd_size_set(nbd, bdev, nbd->blksize,
			     div_s64(arg, nbd->blksize));
		return 0;
	case NBD_SET_SIZE_BLOCKS:
		nbd_size_set(nbd, bdev, nbd->blksize, arg);
		return 0;
	case NBD_SET_TIMEOUT:
		nbd->tag_set.timeout = arg * HZ;
		return 0;

	case NBD_SET_FLAGS:
		nbd->flags = arg;
		return 0;
	case NBD_DO_IT:
		return nbd_start_device(nbd, bdev);
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

	mutex_lock(&nbd->config_lock);
	error = __nbd_ioctl(bdev, nbd, cmd, arg);
	mutex_unlock(&nbd->config_lock);

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
	debugfs_create_u64("blocksize", 0444, dir, &nbd->blksize);
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
	return 0;
}

static struct blk_mq_ops nbd_mq_ops = {
	.queue_rq	= nbd_queue_rq,
	.init_request	= nbd_init_request,
	.timeout	= nbd_xmit_timeout,
};

static void nbd_dev_remove(struct nbd_device *nbd)
{
	struct gendisk *disk = nbd->disk;
	nbd->magic = 0;
	if (disk) {
		del_gendisk(disk);
		blk_cleanup_queue(disk->queue);
		blk_mq_free_tag_set(&nbd->tag_set);
		put_disk(disk);
	}
	kfree(nbd);
}

static int nbd_dev_add(int index)
{
	struct nbd_device *nbd;
	struct gendisk *disk;
	struct request_queue *q;
	int err = -ENOMEM;

	nbd = kzalloc(sizeof(struct nbd_device), GFP_KERNEL);
	if (!nbd)
		goto out;

	disk = alloc_disk(1 << part_shift);
	if (!disk)
		goto out_free_nbd;

	if (index >= 0) {
		err = idr_alloc(&nbd_index_idr, nbd, index, index + 1,
				GFP_KERNEL);
		if (err == -ENOSPC)
			err = -EEXIST;
	} else {
		err = idr_alloc(&nbd_index_idr, nbd, 0, 0, GFP_KERNEL);
		if (err >= 0)
			index = err;
	}
	if (err < 0)
		goto out_free_disk;

	nbd->disk = disk;
	nbd->tag_set.ops = &nbd_mq_ops;
	nbd->tag_set.nr_hw_queues = 1;
	nbd->tag_set.queue_depth = 128;
	nbd->tag_set.numa_node = NUMA_NO_NODE;
	nbd->tag_set.cmd_size = sizeof(struct nbd_cmd);
	nbd->tag_set.flags = BLK_MQ_F_SHOULD_MERGE |
		BLK_MQ_F_SG_MERGE | BLK_MQ_F_BLOCKING;
	nbd->tag_set.driver_data = nbd;

	err = blk_mq_alloc_tag_set(&nbd->tag_set);
	if (err)
		goto out_free_idr;

	q = blk_mq_init_queue(&nbd->tag_set);
	if (IS_ERR(q)) {
		err = PTR_ERR(q);
		goto out_free_tags;
	}
	disk->queue = q;

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

	nbd->magic = NBD_MAGIC;
	mutex_init(&nbd->config_lock);
	disk->major = NBD_MAJOR;
	disk->first_minor = index << part_shift;
	disk->fops = &nbd_fops;
	disk->private_data = nbd;
	sprintf(disk->disk_name, "nbd%d", index);
	init_waitqueue_head(&nbd->recv_wq);
	nbd_reset(nbd);
	add_disk(disk);
	return index;

out_free_tags:
	blk_mq_free_tag_set(&nbd->tag_set);
out_free_idr:
	idr_remove(&nbd_index_idr, index);
out_free_disk:
	put_disk(disk);
out_free_nbd:
	kfree(nbd);
out:
	return err;
}

/*
 * And here should be modules and kernel interface 
 *  (Just smiley confuses emacs :-)
 */

static int __init nbd_init(void)
{
	int i;

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
	recv_workqueue = alloc_workqueue("knbd-recv",
					 WQ_MEM_RECLAIM | WQ_HIGHPRI, 0);
	if (!recv_workqueue)
		return -ENOMEM;

	if (register_blkdev(NBD_MAJOR, "nbd")) {
		destroy_workqueue(recv_workqueue);
		return -EIO;
	}

	nbd_dbg_init();

	mutex_lock(&nbd_index_mutex);
	for (i = 0; i < nbds_max; i++)
		nbd_dev_add(i);
	mutex_unlock(&nbd_index_mutex);
	return 0;
}

static int nbd_exit_cb(int id, void *ptr, void *data)
{
	struct nbd_device *nbd = ptr;
	nbd_dev_remove(nbd);
	return 0;
}

static void __exit nbd_cleanup(void)
{
	nbd_dbg_close();

	idr_for_each(&nbd_index_idr, &nbd_exit_cb, NULL);
	idr_destroy(&nbd_index_idr);
	destroy_workqueue(recv_workqueue);
	unregister_blkdev(NBD_MAJOR, "nbd");
}

module_init(nbd_init);
module_exit(nbd_cleanup);

MODULE_DESCRIPTION("Network Block Device");
MODULE_LICENSE("GPL");

module_param(nbds_max, int, 0444);
MODULE_PARM_DESC(nbds_max, "number of network block devices to initialize (default: 16)");
module_param(max_part, int, 0444);
MODULE_PARM_DESC(max_part, "number of partitions per device (default: 0)");
