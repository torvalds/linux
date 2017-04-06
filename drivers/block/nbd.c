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
	struct request *pending;
	int sent;
	bool dead;
	int fallback_index;
};

struct recv_thread_args {
	struct work_struct work;
	struct nbd_device *nbd;
	int index;
};

#define NBD_TIMEDOUT			0
#define NBD_DISCONNECT_REQUESTED	1
#define NBD_DISCONNECTED		2
#define NBD_HAS_PID_FILE		3

struct nbd_config {
	u32 flags;
	unsigned long runtime_flags;

	struct nbd_sock **socks;
	int num_connections;

	atomic_t recv_threads;
	wait_queue_head_t recv_wq;
	loff_t blksize;
	loff_t bytesize;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	struct dentry *dbg_dir;
#endif
};

struct nbd_device {
	struct blk_mq_tag_set tag_set;

	refcount_t config_refs;
	struct nbd_config *config;
	struct mutex config_lock;
	struct gendisk *disk;

	struct task_struct *task_recv;
	struct task_struct *task_setup;
};

struct nbd_cmd {
	struct nbd_device *nbd;
	int index;
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
static void nbd_config_put(struct nbd_device *nbd);

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

static void nbd_mark_nsock_dead(struct nbd_sock *nsock)
{
	if (!nsock->dead)
		kernel_sock_shutdown(nsock->sock, SHUT_RDWR);
	nsock->dead = true;
	nsock->pending = NULL;
	nsock->sent = 0;
}

static int nbd_size_clear(struct nbd_device *nbd, struct block_device *bdev)
{
	if (nbd->config->bytesize) {
		if (bdev->bd_openers <= 1)
			bd_set_size(bdev, 0);
		set_capacity(nbd->disk, 0);
		kobject_uevent(&nbd_to_dev(nbd)->kobj, KOBJ_CHANGE);
	}

	return 0;
}

static void nbd_size_update(struct nbd_device *nbd, struct block_device *bdev)
{
	struct nbd_config *config = nbd->config;
	blk_queue_logical_block_size(nbd->disk->queue, config->blksize);
	blk_queue_physical_block_size(nbd->disk->queue, config->blksize);
	bd_set_size(bdev, config->bytesize);
	set_capacity(nbd->disk, config->bytesize >> 9);
	kobject_uevent(&nbd_to_dev(nbd)->kobj, KOBJ_CHANGE);
}

static void nbd_size_set(struct nbd_device *nbd, struct block_device *bdev,
			loff_t blocksize, loff_t nr_blocks)
{
	struct nbd_config *config = nbd->config;
	config->blksize = blocksize;
	config->bytesize = blocksize * nr_blocks;
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
	struct nbd_config *config = nbd->config;
	int i;

	if (config->num_connections == 0)
		return;
	if (test_and_set_bit(NBD_DISCONNECTED, &config->runtime_flags))
		return;

	for (i = 0; i < config->num_connections; i++) {
		struct nbd_sock *nsock = config->socks[i];
		mutex_lock(&nsock->tx_lock);
		kernel_sock_shutdown(nsock->sock, SHUT_RDWR);
		nbd_mark_nsock_dead(nsock);
		mutex_unlock(&nsock->tx_lock);
	}
	dev_warn(disk_to_dev(nbd->disk), "shutting down sockets\n");
}

static enum blk_eh_timer_return nbd_xmit_timeout(struct request *req,
						 bool reserved)
{
	struct nbd_cmd *cmd = blk_mq_rq_to_pdu(req);
	struct nbd_device *nbd = cmd->nbd;
	struct nbd_config *config;

	if (!refcount_inc_not_zero(&nbd->config_refs)) {
		req->errors = -EIO;
		return BLK_EH_HANDLED;
	}

	config = nbd->config;

	if (config->num_connections > 1) {
		dev_err_ratelimited(nbd_to_dev(nbd),
				    "Connection timed out, retrying\n");
		/*
		 * Hooray we have more connections, requeue this IO, the submit
		 * path will put it on a real connection.
		 */
		if (config->socks && config->num_connections > 1) {
			if (cmd->index < config->num_connections) {
				struct nbd_sock *nsock =
					config->socks[cmd->index];
				mutex_lock(&nsock->tx_lock);
				nbd_mark_nsock_dead(nsock);
				mutex_unlock(&nsock->tx_lock);
			}
			blk_mq_requeue_request(req, true);
			nbd_config_put(nbd);
			return BLK_EH_NOT_HANDLED;
		}
	} else {
		dev_err_ratelimited(nbd_to_dev(nbd),
				    "Connection timed out\n");
	}
	set_bit(NBD_TIMEDOUT, &config->runtime_flags);
	req->errors = -EIO;
	sock_shutdown(nbd);
	nbd_config_put(nbd);

	return BLK_EH_HANDLED;
}

/*
 *  Send or receive packet.
 */
static int sock_xmit(struct nbd_device *nbd, int index, int send,
		     struct iov_iter *iter, int msg_flags, int *sent)
{
	struct nbd_config *config = nbd->config;
	struct socket *sock = config->socks[index]->sock;
	int result;
	struct msghdr msg;
	unsigned long pflags = current->flags;

	if (unlikely(!sock)) {
		dev_err_ratelimited(disk_to_dev(nbd->disk),
			"Attempted %s on closed socket in sock_xmit\n",
			(send ? "send" : "recv"));
		return -EINVAL;
	}

	msg.msg_iter = *iter;

	current->flags |= PF_MEMALLOC;
	do {
		sock->sk->sk_allocation = GFP_NOIO | __GFP_MEMALLOC;
		msg.msg_name = NULL;
		msg.msg_namelen = 0;
		msg.msg_control = NULL;
		msg.msg_controllen = 0;
		msg.msg_flags = msg_flags | MSG_NOSIGNAL;

		if (send)
			result = sock_sendmsg(sock, &msg);
		else
			result = sock_recvmsg(sock, &msg, msg.msg_flags);

		if (result <= 0) {
			if (result == 0)
				result = -EPIPE; /* short read */
			break;
		}
		if (sent)
			*sent += result;
	} while (msg_data_left(&msg));

	tsk_restore_flags(current, pflags, PF_MEMALLOC);

	return result;
}

/* always call with the tx_lock held */
static int nbd_send_cmd(struct nbd_device *nbd, struct nbd_cmd *cmd, int index)
{
	struct request *req = blk_mq_rq_from_pdu(cmd);
	struct nbd_config *config = nbd->config;
	struct nbd_sock *nsock = config->socks[index];
	int result;
	struct nbd_request request = {.magic = htonl(NBD_REQUEST_MAGIC)};
	struct kvec iov = {.iov_base = &request, .iov_len = sizeof(request)};
	struct iov_iter from;
	unsigned long size = blk_rq_bytes(req);
	struct bio *bio;
	u32 type;
	u32 tag = blk_mq_unique_tag(req);
	int sent = nsock->sent, skip = 0;

	iov_iter_kvec(&from, WRITE | ITER_KVEC, &iov, 1, sizeof(request));

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
	    (config->flags & NBD_FLAG_READ_ONLY)) {
		dev_err_ratelimited(disk_to_dev(nbd->disk),
				    "Write on read-only\n");
		return -EIO;
	}

	/* We did a partial send previously, and we at least sent the whole
	 * request struct, so just go and send the rest of the pages in the
	 * request.
	 */
	if (sent) {
		if (sent >= sizeof(request)) {
			skip = sent - sizeof(request);
			goto send_pages;
		}
		iov_iter_advance(&from, sent);
	}
	cmd->index = index;
	request.type = htonl(type);
	if (type != NBD_CMD_FLUSH) {
		request.from = cpu_to_be64((u64)blk_rq_pos(req) << 9);
		request.len = htonl(size);
	}
	memcpy(request.handle, &tag, sizeof(tag));

	dev_dbg(nbd_to_dev(nbd), "request %p: sending control (%s@%llu,%uB)\n",
		cmd, nbdcmd_to_ascii(type),
		(unsigned long long)blk_rq_pos(req) << 9, blk_rq_bytes(req));
	result = sock_xmit(nbd, index, 1, &from,
			(type == NBD_CMD_WRITE) ? MSG_MORE : 0, &sent);
	if (result <= 0) {
		if (result == -ERESTARTSYS) {
			/* If we havne't sent anything we can just return BUSY,
			 * however if we have sent something we need to make
			 * sure we only allow this req to be sent until we are
			 * completely done.
			 */
			if (sent) {
				nsock->pending = req;
				nsock->sent = sent;
			}
			return BLK_MQ_RQ_QUEUE_BUSY;
		}
		dev_err_ratelimited(disk_to_dev(nbd->disk),
			"Send control failed (result %d)\n", result);
		return -EAGAIN;
	}
send_pages:
	if (type != NBD_CMD_WRITE)
		goto out;

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
			iov_iter_bvec(&from, ITER_BVEC | WRITE,
				      &bvec, 1, bvec.bv_len);
			if (skip) {
				if (skip >= iov_iter_count(&from)) {
					skip -= iov_iter_count(&from);
					continue;
				}
				iov_iter_advance(&from, skip);
				skip = 0;
			}
			result = sock_xmit(nbd, index, 1, &from, flags, &sent);
			if (result <= 0) {
				if (result == -ERESTARTSYS) {
					/* We've already sent the header, we
					 * have no choice but to set pending and
					 * return BUSY.
					 */
					nsock->pending = req;
					nsock->sent = sent;
					return BLK_MQ_RQ_QUEUE_BUSY;
				}
				dev_err(disk_to_dev(nbd->disk),
					"Send data failed (result %d)\n",
					result);
				return -EAGAIN;
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
out:
	nsock->pending = NULL;
	nsock->sent = 0;
	return 0;
}

static int nbd_disconnected(struct nbd_config *config)
{
	return test_bit(NBD_DISCONNECTED, &config->runtime_flags) ||
		test_bit(NBD_DISCONNECT_REQUESTED, &config->runtime_flags);
}

/* NULL returned = something went wrong, inform userspace */
static struct nbd_cmd *nbd_read_stat(struct nbd_device *nbd, int index)
{
	struct nbd_config *config = nbd->config;
	int result;
	struct nbd_reply reply;
	struct nbd_cmd *cmd;
	struct request *req = NULL;
	u16 hwq;
	u32 tag;
	struct kvec iov = {.iov_base = &reply, .iov_len = sizeof(reply)};
	struct iov_iter to;

	reply.magic = 0;
	iov_iter_kvec(&to, READ | ITER_KVEC, &iov, 1, sizeof(reply));
	result = sock_xmit(nbd, index, 0, &to, MSG_WAITALL, NULL);
	if (result <= 0) {
		if (!nbd_disconnected(config))
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
		req->errors = -EIO;
		return cmd;
	}

	dev_dbg(nbd_to_dev(nbd), "request %p: got reply\n", cmd);
	if (rq_data_dir(req) != WRITE) {
		struct req_iterator iter;
		struct bio_vec bvec;

		rq_for_each_segment(bvec, req, iter) {
			iov_iter_bvec(&to, ITER_BVEC | READ,
				      &bvec, 1, bvec.bv_len);
			result = sock_xmit(nbd, index, 0, &to, MSG_WAITALL, NULL);
			if (result <= 0) {
				dev_err(disk_to_dev(nbd->disk), "Receive data failed (result %d)\n",
					result);
				/*
				 * If we've disconnected or we only have 1
				 * connection then we need to make sure we
				 * complete this request, otherwise error out
				 * and let the timeout stuff handle resubmitting
				 * this request onto another connection.
				 */
				if (nbd_disconnected(config) ||
				    config->num_connections <= 1) {
					req->errors = -EIO;
					return cmd;
				}
				return ERR_PTR(-EIO);
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

static void recv_work(struct work_struct *work)
{
	struct recv_thread_args *args = container_of(work,
						     struct recv_thread_args,
						     work);
	struct nbd_device *nbd = args->nbd;
	struct nbd_config *config = nbd->config;
	struct nbd_cmd *cmd;
	int ret = 0;

	while (1) {
		cmd = nbd_read_stat(nbd, args->index);
		if (IS_ERR(cmd)) {
			struct nbd_sock *nsock = config->socks[args->index];

			mutex_lock(&nsock->tx_lock);
			nbd_mark_nsock_dead(nsock);
			mutex_unlock(&nsock->tx_lock);
			ret = PTR_ERR(cmd);
			break;
		}

		nbd_end_request(cmd);
	}
	atomic_dec(&config->recv_threads);
	wake_up(&config->recv_wq);
	nbd_config_put(nbd);
	kfree(args);
}

static void nbd_clear_req(struct request *req, void *data, bool reserved)
{
	struct nbd_cmd *cmd;

	if (!blk_mq_request_started(req))
		return;
	cmd = blk_mq_rq_to_pdu(req);
	req->errors = -EIO;
	nbd_end_request(cmd);
}

static void nbd_clear_que(struct nbd_device *nbd)
{
	blk_mq_tagset_busy_iter(&nbd->tag_set, nbd_clear_req, NULL);
	dev_dbg(disk_to_dev(nbd->disk), "queue cleared\n");
}

static int find_fallback(struct nbd_device *nbd, int index)
{
	struct nbd_config *config = nbd->config;
	int new_index = -1;
	struct nbd_sock *nsock = config->socks[index];
	int fallback = nsock->fallback_index;

	if (test_bit(NBD_DISCONNECTED, &config->runtime_flags))
		return new_index;

	if (config->num_connections <= 1) {
		dev_err_ratelimited(disk_to_dev(nbd->disk),
				    "Attempted send on invalid socket\n");
		return new_index;
	}

	if (fallback >= 0 && fallback < config->num_connections &&
	    !config->socks[fallback]->dead)
		return fallback;

	if (nsock->fallback_index < 0 ||
	    nsock->fallback_index >= config->num_connections ||
	    config->socks[nsock->fallback_index]->dead) {
		int i;
		for (i = 0; i < config->num_connections; i++) {
			if (i == index)
				continue;
			if (!config->socks[i]->dead) {
				new_index = i;
				break;
			}
		}
		nsock->fallback_index = new_index;
		if (new_index < 0) {
			dev_err_ratelimited(disk_to_dev(nbd->disk),
					    "Dead connection, failed to find a fallback\n");
			return new_index;
		}
	}
	new_index = nsock->fallback_index;
	return new_index;
}

static int nbd_handle_cmd(struct nbd_cmd *cmd, int index)
{
	struct request *req = blk_mq_rq_from_pdu(cmd);
	struct nbd_device *nbd = cmd->nbd;
	struct nbd_config *config;
	struct nbd_sock *nsock;
	int ret;

	if (!refcount_inc_not_zero(&nbd->config_refs)) {
		dev_err_ratelimited(disk_to_dev(nbd->disk),
				    "Socks array is empty\n");
		return -EINVAL;
	}
	config = nbd->config;

	if (index >= config->num_connections) {
		dev_err_ratelimited(disk_to_dev(nbd->disk),
				    "Attempted send on invalid socket\n");
		nbd_config_put(nbd);
		return -EINVAL;
	}
	req->errors = 0;
again:
	nsock = config->socks[index];
	mutex_lock(&nsock->tx_lock);
	if (nsock->dead) {
		index = find_fallback(nbd, index);
		if (index < 0) {
			ret = -EIO;
			goto out;
		}
		mutex_unlock(&nsock->tx_lock);
		goto again;
	}

	/* Handle the case that we have a pending request that was partially
	 * transmitted that _has_ to be serviced first.  We need to call requeue
	 * here so that it gets put _after_ the request that is already on the
	 * dispatch list.
	 */
	if (unlikely(nsock->pending && nsock->pending != req)) {
		blk_mq_requeue_request(req, true);
		ret = 0;
		goto out;
	}
	/*
	 * Some failures are related to the link going down, so anything that
	 * returns EAGAIN can be retried on a different socket.
	 */
	ret = nbd_send_cmd(nbd, cmd, index);
	if (ret == -EAGAIN) {
		dev_err_ratelimited(disk_to_dev(nbd->disk),
				    "Request send failed trying another connection\n");
		nbd_mark_nsock_dead(nsock);
		mutex_unlock(&nsock->tx_lock);
		goto again;
	}
out:
	mutex_unlock(&nsock->tx_lock);
	nbd_config_put(nbd);
	return ret;
}

static int nbd_queue_rq(struct blk_mq_hw_ctx *hctx,
			const struct blk_mq_queue_data *bd)
{
	struct nbd_cmd *cmd = blk_mq_rq_to_pdu(bd->rq);
	int ret;

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

	/* We can be called directly from the user space process, which means we
	 * could possibly have signals pending so our sendmsg will fail.  In
	 * this case we need to return that we are busy, otherwise error out as
	 * appropriate.
	 */
	ret = nbd_handle_cmd(cmd, hctx->queue_num);
	if (ret < 0)
		ret = BLK_MQ_RQ_QUEUE_ERROR;
	if (!ret)
		ret = BLK_MQ_RQ_QUEUE_OK;
	complete(&cmd->send_complete);

	return ret;
}

static int nbd_add_socket(struct nbd_device *nbd, struct block_device *bdev,
			  unsigned long arg)
{
	struct nbd_config *config = nbd->config;
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
		sockfd_put(sock);
		return -EINVAL;
	}

	socks = krealloc(config->socks, (config->num_connections + 1) *
			 sizeof(struct nbd_sock *), GFP_KERNEL);
	if (!socks) {
		sockfd_put(sock);
		return -ENOMEM;
	}
	nsock = kzalloc(sizeof(struct nbd_sock), GFP_KERNEL);
	if (!nsock) {
		sockfd_put(sock);
		return -ENOMEM;
	}

	config->socks = socks;

	nsock->fallback_index = -1;
	nsock->dead = false;
	mutex_init(&nsock->tx_lock);
	nsock->sock = sock;
	nsock->pending = NULL;
	nsock->sent = 0;
	socks[config->num_connections++] = nsock;

	if (max_part)
		bdev->bd_invalidated = 1;
	return 0;
}

/* Reset all properties of an NBD device */
static void nbd_reset(struct nbd_device *nbd)
{
	nbd->config = NULL;
	nbd->tag_set.timeout = 0;
	queue_flag_clear_unlocked(QUEUE_FLAG_DISCARD, nbd->disk->queue);
}

static void nbd_bdev_reset(struct block_device *bdev)
{
	if (bdev->bd_openers > 1)
		return;
	set_device_ro(bdev, false);
	bdev->bd_inode->i_size = 0;
	if (max_part > 0) {
		blkdev_reread_part(bdev);
		bdev->bd_invalidated = 1;
	}
}

static void nbd_parse_flags(struct nbd_device *nbd, struct block_device *bdev)
{
	struct nbd_config *config = nbd->config;
	if (config->flags & NBD_FLAG_READ_ONLY)
		set_device_ro(bdev, true);
	if (config->flags & NBD_FLAG_SEND_TRIM)
		queue_flag_set_unlocked(QUEUE_FLAG_DISCARD, nbd->disk->queue);
	if (config->flags & NBD_FLAG_SEND_FLUSH)
		blk_queue_write_cache(nbd->disk->queue, true, false);
	else
		blk_queue_write_cache(nbd->disk->queue, false, false);
}

static void send_disconnects(struct nbd_device *nbd)
{
	struct nbd_config *config = nbd->config;
	struct nbd_request request = {
		.magic = htonl(NBD_REQUEST_MAGIC),
		.type = htonl(NBD_CMD_DISC),
	};
	struct kvec iov = {.iov_base = &request, .iov_len = sizeof(request)};
	struct iov_iter from;
	int i, ret;

	for (i = 0; i < config->num_connections; i++) {
		iov_iter_kvec(&from, WRITE | ITER_KVEC, &iov, 1, sizeof(request));
		ret = sock_xmit(nbd, i, 1, &from, 0, NULL);
		if (ret <= 0)
			dev_err(disk_to_dev(nbd->disk),
				"Send disconnect failed %d\n", ret);
	}
}

static int nbd_disconnect(struct nbd_device *nbd, struct block_device *bdev)
{
	struct nbd_config *config = nbd->config;

	dev_info(disk_to_dev(nbd->disk), "NBD_DISCONNECT\n");
	mutex_unlock(&nbd->config_lock);
	fsync_bdev(bdev);
	mutex_lock(&nbd->config_lock);

	if (!test_and_set_bit(NBD_DISCONNECT_REQUESTED,
			      &config->runtime_flags))
		send_disconnects(nbd);
	return 0;
}

static int nbd_clear_sock(struct nbd_device *nbd, struct block_device *bdev)
{
	sock_shutdown(nbd);
	nbd_clear_que(nbd);

	__invalidate_device(bdev, true);
	nbd_bdev_reset(bdev);
	nbd->task_setup = NULL;
	return 0;
}

static void nbd_config_put(struct nbd_device *nbd)
{
	if (refcount_dec_and_mutex_lock(&nbd->config_refs,
					&nbd->config_lock)) {
		struct block_device *bdev;
		struct nbd_config *config = nbd->config;

		bdev = bdget_disk(nbd->disk, 0);
		if (!bdev) {
			mutex_unlock(&nbd->config_lock);
			return;
		}

		nbd_dev_dbg_close(nbd);
		nbd_size_clear(nbd, bdev);
		if (test_and_clear_bit(NBD_HAS_PID_FILE,
				       &config->runtime_flags))
			device_remove_file(disk_to_dev(nbd->disk), &pid_attr);
		nbd->task_recv = NULL;
		nbd_clear_sock(nbd, bdev);
		if (config->num_connections) {
			int i;
			for (i = 0; i < config->num_connections; i++) {
				sockfd_put(config->socks[i]->sock);
				kfree(config->socks[i]);
			}
			kfree(config->socks);
		}
		nbd_reset(nbd);
		mutex_unlock(&nbd->config_lock);
		bdput(bdev);
		module_put(THIS_MODULE);
	}
}

static int nbd_start_device(struct nbd_device *nbd, struct block_device *bdev)
{
	struct nbd_config *config = nbd->config;
	int num_connections = config->num_connections;
	int error = 0, i;

	if (nbd->task_recv)
		return -EBUSY;
	if (!config->socks)
		return -EINVAL;

	if (num_connections > 1 &&
	    !(config->flags & NBD_FLAG_CAN_MULTI_CONN)) {
		dev_err(disk_to_dev(nbd->disk), "server does not support multiple connections per device.\n");
		return -EINVAL;
	}

	blk_mq_update_nr_hw_queues(&nbd->tag_set, config->num_connections);
	nbd->task_recv = current;
	mutex_unlock(&nbd->config_lock);

	nbd_parse_flags(nbd, bdev);

	error = device_create_file(disk_to_dev(nbd->disk), &pid_attr);
	if (error) {
		dev_err(disk_to_dev(nbd->disk), "device_create_file failed!\n");
		return error;
	}
	set_bit(NBD_HAS_PID_FILE, &config->runtime_flags);

	nbd_size_update(nbd, bdev);

	nbd_dev_dbg_init(nbd);
	for (i = 0; i < num_connections; i++) {
		struct recv_thread_args *args;

		args = kzalloc(sizeof(*args), GFP_KERNEL);
		if (!args) {
			sock_shutdown(nbd);
			return -ENOMEM;
		}
		sk_set_memalloc(config->socks[i]->sock->sk);
		atomic_inc(&config->recv_threads);
		refcount_inc(&nbd->config_refs);
		INIT_WORK(&args->work, recv_work);
		args->nbd = nbd;
		args->index = i;
		queue_work(recv_workqueue, &args->work);
	}
	error = wait_event_interruptible(config->recv_wq,
					 atomic_read(&config->recv_threads) == 0);
	if (error)
		sock_shutdown(nbd);
	mutex_lock(&nbd->config_lock);

	/* user requested, ignore socket errors */
	if (test_bit(NBD_DISCONNECT_REQUESTED, &config->runtime_flags))
		error = 0;
	if (test_bit(NBD_TIMEDOUT, &config->runtime_flags))
		error = -ETIMEDOUT;
	return error;
}

/* Must be called with config_lock held */
static int __nbd_ioctl(struct block_device *bdev, struct nbd_device *nbd,
		       unsigned int cmd, unsigned long arg)
{
	struct nbd_config *config = nbd->config;

	switch (cmd) {
	case NBD_DISCONNECT:
		return nbd_disconnect(nbd, bdev);
	case NBD_CLEAR_SOCK:
		return nbd_clear_sock(nbd, bdev);
	case NBD_SET_SOCK:
		return nbd_add_socket(nbd, bdev, arg);
	case NBD_SET_BLKSIZE:
		nbd_size_set(nbd, bdev, arg,
			     div_s64(config->bytesize, arg));
		return 0;
	case NBD_SET_SIZE:
		nbd_size_set(nbd, bdev, config->blksize,
			     div_s64(arg, config->blksize));
		return 0;
	case NBD_SET_SIZE_BLOCKS:
		nbd_size_set(nbd, bdev, config->blksize, arg);
		return 0;
	case NBD_SET_TIMEOUT:
		if (arg) {
			nbd->tag_set.timeout = arg * HZ;
			blk_queue_rq_timeout(nbd->disk->queue, arg * HZ);
		}
		return 0;

	case NBD_SET_FLAGS:
		config->flags = arg;
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

	mutex_lock(&nbd->config_lock);
	error = __nbd_ioctl(bdev, nbd, cmd, arg);
	mutex_unlock(&nbd->config_lock);
	return error;
}

static struct nbd_config *nbd_alloc_config(void)
{
	struct nbd_config *config;

	config = kzalloc(sizeof(struct nbd_config), GFP_NOFS);
	if (!config)
		return NULL;
	atomic_set(&config->recv_threads, 0);
	init_waitqueue_head(&config->recv_wq);
	config->blksize = 1024;
	try_module_get(THIS_MODULE);
	return config;
}

static int nbd_open(struct block_device *bdev, fmode_t mode)
{
	struct nbd_device *nbd;
	int ret = 0;

	mutex_lock(&nbd_index_mutex);
	nbd = bdev->bd_disk->private_data;
	if (!nbd) {
		ret = -ENXIO;
		goto out;
	}
	if (!refcount_inc_not_zero(&nbd->config_refs)) {
		struct nbd_config *config;

		mutex_lock(&nbd->config_lock);
		if (refcount_inc_not_zero(&nbd->config_refs)) {
			mutex_unlock(&nbd->config_lock);
			goto out;
		}
		config = nbd->config = nbd_alloc_config();
		if (!config) {
			ret = -ENOMEM;
			mutex_unlock(&nbd->config_lock);
			goto out;
		}
		refcount_set(&nbd->config_refs, 1);
		mutex_unlock(&nbd->config_lock);
	}
out:
	mutex_unlock(&nbd_index_mutex);
	return ret;
}

static void nbd_release(struct gendisk *disk, fmode_t mode)
{
	struct nbd_device *nbd = disk->private_data;
	nbd_config_put(nbd);
}

static const struct block_device_operations nbd_fops =
{
	.owner =	THIS_MODULE,
	.open =		nbd_open,
	.release =	nbd_release,
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
	u32 flags = nbd->config->flags;

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
	struct nbd_config *config = nbd->config;

	if (!nbd_dbg_dir)
		return -EIO;

	dir = debugfs_create_dir(nbd_name(nbd), nbd_dbg_dir);
	if (!dir) {
		dev_err(nbd_to_dev(nbd), "Failed to create debugfs dir for '%s'\n",
			nbd_name(nbd));
		return -EIO;
	}
	config->dbg_dir = dir;

	debugfs_create_file("tasks", 0444, dir, nbd, &nbd_dbg_tasks_ops);
	debugfs_create_u64("size_bytes", 0444, dir, &config->bytesize);
	debugfs_create_u32("timeout", 0444, dir, &nbd->tag_set.timeout);
	debugfs_create_u64("blocksize", 0444, dir, &config->blksize);
	debugfs_create_file("flags", 0444, dir, nbd, &nbd_dbg_flags_ops);

	return 0;
}

static void nbd_dev_dbg_close(struct nbd_device *nbd)
{
	debugfs_remove_recursive(nbd->config->dbg_dir);
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

static const struct blk_mq_ops nbd_mq_ops = {
	.queue_rq	= nbd_queue_rq,
	.init_request	= nbd_init_request,
	.timeout	= nbd_xmit_timeout,
};

static void nbd_dev_remove(struct nbd_device *nbd)
{
	struct gendisk *disk = nbd->disk;
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
	blk_queue_max_hw_sectors(disk->queue, 65536);
	disk->queue->limits.max_sectors = 256;

	mutex_init(&nbd->config_lock);
	refcount_set(&nbd->config_refs, 0);
	disk->major = NBD_MAJOR;
	disk->first_minor = index << part_shift;
	disk->fops = &nbd_fops;
	disk->private_data = nbd;
	sprintf(disk->disk_name, "nbd%d", index);
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
