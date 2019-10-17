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
#include <linux/sched/mm.h>
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
#include <linux/nbd-netlink.h>
#include <net/genetlink.h>

static DEFINE_IDR(nbd_index_idr);
static DEFINE_MUTEX(nbd_index_mutex);
static int nbd_total_devices = 0;

struct nbd_sock {
	struct socket *sock;
	struct mutex tx_lock;
	struct request *pending;
	int sent;
	bool dead;
	int fallback_index;
	int cookie;
};

struct recv_thread_args {
	struct work_struct work;
	struct nbd_device *nbd;
	int index;
};

struct link_dead_args {
	struct work_struct work;
	int index;
};

#define NBD_TIMEDOUT			0
#define NBD_DISCONNECT_REQUESTED	1
#define NBD_DISCONNECTED		2
#define NBD_HAS_PID_FILE		3
#define NBD_HAS_CONFIG_REF		4
#define NBD_BOUND			5
#define NBD_DESTROY_ON_DISCONNECT	6
#define NBD_DISCONNECT_ON_CLOSE 	7

struct nbd_config {
	u32 flags;
	unsigned long runtime_flags;
	u64 dead_conn_timeout;

	struct nbd_sock **socks;
	int num_connections;
	atomic_t live_connections;
	wait_queue_head_t conn_wait;

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

	int index;
	refcount_t config_refs;
	refcount_t refs;
	struct nbd_config *config;
	struct mutex config_lock;
	struct gendisk *disk;
	struct workqueue_struct *recv_workq;

	struct list_head list;
	struct task_struct *task_recv;
	struct task_struct *task_setup;
};

#define NBD_CMD_REQUEUED	1

struct nbd_cmd {
	struct nbd_device *nbd;
	struct mutex lock;
	int index;
	int cookie;
	blk_status_t status;
	unsigned long flags;
	u32 cmd_cookie;
};

#if IS_ENABLED(CONFIG_DEBUG_FS)
static struct dentry *nbd_dbg_dir;
#endif

#define nbd_name(nbd) ((nbd)->disk->disk_name)

#define NBD_MAGIC 0x68797548

#define NBD_DEF_BLKSIZE 1024

static unsigned int nbds_max = 16;
static int max_part = 16;
static int part_shift;

static int nbd_dev_dbg_init(struct nbd_device *nbd);
static void nbd_dev_dbg_close(struct nbd_device *nbd);
static void nbd_config_put(struct nbd_device *nbd);
static void nbd_connect_reply(struct genl_info *info, int index);
static int nbd_genl_status(struct sk_buff *skb, struct genl_info *info);
static void nbd_dead_link_work(struct work_struct *work);
static void nbd_disconnect_and_put(struct nbd_device *nbd);

static inline struct device *nbd_to_dev(struct nbd_device *nbd)
{
	return disk_to_dev(nbd->disk);
}

static void nbd_requeue_cmd(struct nbd_cmd *cmd)
{
	struct request *req = blk_mq_rq_from_pdu(cmd);

	if (!test_and_set_bit(NBD_CMD_REQUEUED, &cmd->flags))
		blk_mq_requeue_request(req, true);
}

#define NBD_COOKIE_BITS 32

static u64 nbd_cmd_handle(struct nbd_cmd *cmd)
{
	struct request *req = blk_mq_rq_from_pdu(cmd);
	u32 tag = blk_mq_unique_tag(req);
	u64 cookie = cmd->cmd_cookie;

	return (cookie << NBD_COOKIE_BITS) | tag;
}

static u32 nbd_handle_to_tag(u64 handle)
{
	return (u32)handle;
}

static u32 nbd_handle_to_cookie(u64 handle)
{
	return (u32)(handle >> NBD_COOKIE_BITS);
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

static const struct device_attribute pid_attr = {
	.attr = { .name = "pid", .mode = 0444},
	.show = pid_show,
};

static void nbd_dev_remove(struct nbd_device *nbd)
{
	struct gendisk *disk = nbd->disk;
	struct request_queue *q;

	if (disk) {
		q = disk->queue;
		del_gendisk(disk);
		blk_cleanup_queue(q);
		blk_mq_free_tag_set(&nbd->tag_set);
		disk->private_data = NULL;
		put_disk(disk);
	}
	kfree(nbd);
}

static void nbd_put(struct nbd_device *nbd)
{
	if (refcount_dec_and_mutex_lock(&nbd->refs,
					&nbd_index_mutex)) {
		idr_remove(&nbd_index_idr, nbd->index);
		nbd_dev_remove(nbd);
		mutex_unlock(&nbd_index_mutex);
	}
}

static int nbd_disconnected(struct nbd_config *config)
{
	return test_bit(NBD_DISCONNECTED, &config->runtime_flags) ||
		test_bit(NBD_DISCONNECT_REQUESTED, &config->runtime_flags);
}

static void nbd_mark_nsock_dead(struct nbd_device *nbd, struct nbd_sock *nsock,
				int notify)
{
	if (!nsock->dead && notify && !nbd_disconnected(nbd->config)) {
		struct link_dead_args *args;
		args = kmalloc(sizeof(struct link_dead_args), GFP_NOIO);
		if (args) {
			INIT_WORK(&args->work, nbd_dead_link_work);
			args->index = nbd->index;
			queue_work(system_wq, &args->work);
		}
	}
	if (!nsock->dead) {
		kernel_sock_shutdown(nsock->sock, SHUT_RDWR);
		if (atomic_dec_return(&nbd->config->live_connections) == 0) {
			if (test_and_clear_bit(NBD_DISCONNECT_REQUESTED,
					       &nbd->config->runtime_flags)) {
				set_bit(NBD_DISCONNECTED,
					&nbd->config->runtime_flags);
				dev_info(nbd_to_dev(nbd),
					"Disconnected due to user request.\n");
			}
		}
	}
	nsock->dead = true;
	nsock->pending = NULL;
	nsock->sent = 0;
}

static void nbd_size_clear(struct nbd_device *nbd)
{
	if (nbd->config->bytesize) {
		set_capacity(nbd->disk, 0);
		kobject_uevent(&nbd_to_dev(nbd)->kobj, KOBJ_CHANGE);
	}
}

static void nbd_size_update(struct nbd_device *nbd)
{
	struct nbd_config *config = nbd->config;
	struct block_device *bdev = bdget_disk(nbd->disk, 0);

	if (config->flags & NBD_FLAG_SEND_TRIM) {
		nbd->disk->queue->limits.discard_granularity = config->blksize;
		nbd->disk->queue->limits.discard_alignment = config->blksize;
		blk_queue_max_discard_sectors(nbd->disk->queue, UINT_MAX);
	}
	blk_queue_logical_block_size(nbd->disk->queue, config->blksize);
	blk_queue_physical_block_size(nbd->disk->queue, config->blksize);
	set_capacity(nbd->disk, config->bytesize >> 9);
	if (bdev) {
		if (bdev->bd_disk) {
			bd_set_size(bdev, config->bytesize);
			set_blocksize(bdev, config->blksize);
		} else
			bdev->bd_invalidated = 1;
		bdput(bdev);
	}
	kobject_uevent(&nbd_to_dev(nbd)->kobj, KOBJ_CHANGE);
}

static void nbd_size_set(struct nbd_device *nbd, loff_t blocksize,
			 loff_t nr_blocks)
{
	struct nbd_config *config = nbd->config;
	config->blksize = blocksize;
	config->bytesize = blocksize * nr_blocks;
	if (nbd->task_recv != NULL)
		nbd_size_update(nbd);
}

static void nbd_complete_rq(struct request *req)
{
	struct nbd_cmd *cmd = blk_mq_rq_to_pdu(req);

	dev_dbg(nbd_to_dev(cmd->nbd), "request %p: %s\n", req,
		cmd->status ? "failed" : "done");

	blk_mq_end_request(req, cmd->status);
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
		nbd_mark_nsock_dead(nbd, nsock, 0);
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
		cmd->status = BLK_STS_TIMEOUT;
		goto done;
	}
	config = nbd->config;

	if (!mutex_trylock(&cmd->lock)) {
		nbd_config_put(nbd);
		return BLK_EH_RESET_TIMER;
	}

	if (config->num_connections > 1) {
		dev_err_ratelimited(nbd_to_dev(nbd),
				    "Connection timed out, retrying (%d/%d alive)\n",
				    atomic_read(&config->live_connections),
				    config->num_connections);
		/*
		 * Hooray we have more connections, requeue this IO, the submit
		 * path will put it on a real connection.
		 */
		if (config->socks && config->num_connections > 1) {
			if (cmd->index < config->num_connections) {
				struct nbd_sock *nsock =
					config->socks[cmd->index];
				mutex_lock(&nsock->tx_lock);
				/* We can have multiple outstanding requests, so
				 * we don't want to mark the nsock dead if we've
				 * already reconnected with a new socket, so
				 * only mark it dead if its the same socket we
				 * were sent out on.
				 */
				if (cmd->cookie == nsock->cookie)
					nbd_mark_nsock_dead(nbd, nsock, 1);
				mutex_unlock(&nsock->tx_lock);
			}
			mutex_unlock(&cmd->lock);
			nbd_requeue_cmd(cmd);
			nbd_config_put(nbd);
			return BLK_EH_DONE;
		}
	} else {
		dev_err_ratelimited(nbd_to_dev(nbd),
				    "Connection timed out\n");
	}
	set_bit(NBD_TIMEDOUT, &config->runtime_flags);
	cmd->status = BLK_STS_IOERR;
	mutex_unlock(&cmd->lock);
	sock_shutdown(nbd);
	nbd_config_put(nbd);
done:
	blk_mq_complete_request(req);
	return BLK_EH_DONE;
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
	unsigned int noreclaim_flag;

	if (unlikely(!sock)) {
		dev_err_ratelimited(disk_to_dev(nbd->disk),
			"Attempted %s on closed socket in sock_xmit\n",
			(send ? "send" : "recv"));
		return -EINVAL;
	}

	msg.msg_iter = *iter;

	noreclaim_flag = memalloc_noreclaim_save();
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

	memalloc_noreclaim_restore(noreclaim_flag);

	return result;
}

/*
 * Different settings for sk->sk_sndtimeo can result in different return values
 * if there is a signal pending when we enter sendmsg, because reasons?
 */
static inline int was_interrupted(int result)
{
	return result == -ERESTARTSYS || result == -EINTR;
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
	u64 handle;
	u32 type;
	u32 nbd_cmd_flags = 0;
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

	if (req->cmd_flags & REQ_FUA)
		nbd_cmd_flags |= NBD_CMD_FLAG_FUA;

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
	} else {
		cmd->cmd_cookie++;
	}
	cmd->index = index;
	cmd->cookie = nsock->cookie;
	request.type = htonl(type | nbd_cmd_flags);
	if (type != NBD_CMD_FLUSH) {
		request.from = cpu_to_be64((u64)blk_rq_pos(req) << 9);
		request.len = htonl(size);
	}
	handle = nbd_cmd_handle(cmd);
	memcpy(request.handle, &handle, sizeof(handle));

	dev_dbg(nbd_to_dev(nbd), "request %p: sending control (%s@%llu,%uB)\n",
		req, nbdcmd_to_ascii(type),
		(unsigned long long)blk_rq_pos(req) << 9, blk_rq_bytes(req));
	result = sock_xmit(nbd, index, 1, &from,
			(type == NBD_CMD_WRITE) ? MSG_MORE : 0, &sent);
	if (result <= 0) {
		if (was_interrupted(result)) {
			/* If we havne't sent anything we can just return BUSY,
			 * however if we have sent something we need to make
			 * sure we only allow this req to be sent until we are
			 * completely done.
			 */
			if (sent) {
				nsock->pending = req;
				nsock->sent = sent;
			}
			set_bit(NBD_CMD_REQUEUED, &cmd->flags);
			return BLK_STS_RESOURCE;
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
				req, bvec.bv_len);
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
				if (was_interrupted(result)) {
					/* We've already sent the header, we
					 * have no choice but to set pending and
					 * return BUSY.
					 */
					nsock->pending = req;
					nsock->sent = sent;
					set_bit(NBD_CMD_REQUEUED, &cmd->flags);
					return BLK_STS_RESOURCE;
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

/* NULL returned = something went wrong, inform userspace */
static struct nbd_cmd *nbd_read_stat(struct nbd_device *nbd, int index)
{
	struct nbd_config *config = nbd->config;
	int result;
	struct nbd_reply reply;
	struct nbd_cmd *cmd;
	struct request *req = NULL;
	u64 handle;
	u16 hwq;
	u32 tag;
	struct kvec iov = {.iov_base = &reply, .iov_len = sizeof(reply)};
	struct iov_iter to;
	int ret = 0;

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

	memcpy(&handle, reply.handle, sizeof(handle));
	tag = nbd_handle_to_tag(handle);
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

	mutex_lock(&cmd->lock);
	if (cmd->cmd_cookie != nbd_handle_to_cookie(handle)) {
		dev_err(disk_to_dev(nbd->disk), "Double reply on req %p, cmd_cookie %u, handle cookie %u\n",
			req, cmd->cmd_cookie, nbd_handle_to_cookie(handle));
		ret = -ENOENT;
		goto out;
	}
	if (test_bit(NBD_CMD_REQUEUED, &cmd->flags)) {
		dev_err(disk_to_dev(nbd->disk), "Raced with timeout on req %p\n",
			req);
		ret = -ENOENT;
		goto out;
	}
	if (ntohl(reply.error)) {
		dev_err(disk_to_dev(nbd->disk), "Other side returned error (%d)\n",
			ntohl(reply.error));
		cmd->status = BLK_STS_IOERR;
		goto out;
	}

	dev_dbg(nbd_to_dev(nbd), "request %p: got reply\n", req);
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
					cmd->status = BLK_STS_IOERR;
					goto out;
				}
				ret = -EIO;
				goto out;
			}
			dev_dbg(nbd_to_dev(nbd), "request %p: got %d bytes data\n",
				req, bvec.bv_len);
		}
	}
out:
	mutex_unlock(&cmd->lock);
	return ret ? ERR_PTR(ret) : cmd;
}

static void recv_work(struct work_struct *work)
{
	struct recv_thread_args *args = container_of(work,
						     struct recv_thread_args,
						     work);
	struct nbd_device *nbd = args->nbd;
	struct nbd_config *config = nbd->config;
	struct nbd_cmd *cmd;

	while (1) {
		cmd = nbd_read_stat(nbd, args->index);
		if (IS_ERR(cmd)) {
			struct nbd_sock *nsock = config->socks[args->index];

			mutex_lock(&nsock->tx_lock);
			nbd_mark_nsock_dead(nbd, nsock, 1);
			mutex_unlock(&nsock->tx_lock);
			break;
		}

		blk_mq_complete_request(blk_mq_rq_from_pdu(cmd));
	}
	atomic_dec(&config->recv_threads);
	wake_up(&config->recv_wq);
	nbd_config_put(nbd);
	kfree(args);
}

static void nbd_clear_req(struct request *req, void *data, bool reserved)
{
	struct nbd_cmd *cmd = blk_mq_rq_to_pdu(req);

	cmd->status = BLK_STS_IOERR;
	blk_mq_complete_request(req);
}

static void nbd_clear_que(struct nbd_device *nbd)
{
	blk_mq_quiesce_queue(nbd->disk->queue);
	blk_mq_tagset_busy_iter(&nbd->tag_set, nbd_clear_req, NULL);
	blk_mq_unquiesce_queue(nbd->disk->queue);
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

static int wait_for_reconnect(struct nbd_device *nbd)
{
	struct nbd_config *config = nbd->config;
	if (!config->dead_conn_timeout)
		return 0;
	if (test_bit(NBD_DISCONNECTED, &config->runtime_flags))
		return 0;
	return wait_event_timeout(config->conn_wait,
				  atomic_read(&config->live_connections) > 0,
				  config->dead_conn_timeout) > 0;
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
		blk_mq_start_request(req);
		return -EINVAL;
	}
	config = nbd->config;

	if (index >= config->num_connections) {
		dev_err_ratelimited(disk_to_dev(nbd->disk),
				    "Attempted send on invalid socket\n");
		nbd_config_put(nbd);
		blk_mq_start_request(req);
		return -EINVAL;
	}
	cmd->status = BLK_STS_OK;
again:
	nsock = config->socks[index];
	mutex_lock(&nsock->tx_lock);
	if (nsock->dead) {
		int old_index = index;
		index = find_fallback(nbd, index);
		mutex_unlock(&nsock->tx_lock);
		if (index < 0) {
			if (wait_for_reconnect(nbd)) {
				index = old_index;
				goto again;
			}
			/* All the sockets should already be down at this point,
			 * we just want to make sure that DISCONNECTED is set so
			 * any requests that come in that were queue'ed waiting
			 * for the reconnect timer don't trigger the timer again
			 * and instead just error out.
			 */
			sock_shutdown(nbd);
			nbd_config_put(nbd);
			blk_mq_start_request(req);
			return -EIO;
		}
		goto again;
	}

	/* Handle the case that we have a pending request that was partially
	 * transmitted that _has_ to be serviced first.  We need to call requeue
	 * here so that it gets put _after_ the request that is already on the
	 * dispatch list.
	 */
	blk_mq_start_request(req);
	if (unlikely(nsock->pending && nsock->pending != req)) {
		nbd_requeue_cmd(cmd);
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
				    "Request send failed, requeueing\n");
		nbd_mark_nsock_dead(nbd, nsock, 1);
		nbd_requeue_cmd(cmd);
		ret = 0;
	}
out:
	mutex_unlock(&nsock->tx_lock);
	nbd_config_put(nbd);
	return ret;
}

static blk_status_t nbd_queue_rq(struct blk_mq_hw_ctx *hctx,
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
	mutex_lock(&cmd->lock);
	clear_bit(NBD_CMD_REQUEUED, &cmd->flags);

	/* We can be called directly from the user space process, which means we
	 * could possibly have signals pending so our sendmsg will fail.  In
	 * this case we need to return that we are busy, otherwise error out as
	 * appropriate.
	 */
	ret = nbd_handle_cmd(cmd, hctx->queue_num);
	if (ret < 0)
		ret = BLK_STS_IOERR;
	else if (!ret)
		ret = BLK_STS_OK;
	mutex_unlock(&cmd->lock);

	return ret;
}

static struct socket *nbd_get_socket(struct nbd_device *nbd, unsigned long fd,
				     int *err)
{
	struct socket *sock;

	*err = 0;
	sock = sockfd_lookup(fd, err);
	if (!sock)
		return NULL;

	if (sock->ops->shutdown == sock_no_shutdown) {
		dev_err(disk_to_dev(nbd->disk), "Unsupported socket: shutdown callout must be supported.\n");
		*err = -EINVAL;
		return NULL;
	}

	return sock;
}

static int nbd_add_socket(struct nbd_device *nbd, unsigned long arg,
			  bool netlink)
{
	struct nbd_config *config = nbd->config;
	struct socket *sock;
	struct nbd_sock **socks;
	struct nbd_sock *nsock;
	int err;

	sock = nbd_get_socket(nbd, arg, &err);
	if (!sock)
		return err;

	if (!netlink && !nbd->task_setup &&
	    !test_bit(NBD_BOUND, &config->runtime_flags))
		nbd->task_setup = current;

	if (!netlink &&
	    (nbd->task_setup != current ||
	     test_bit(NBD_BOUND, &config->runtime_flags))) {
		dev_err(disk_to_dev(nbd->disk),
			"Device being setup by another task");
		sockfd_put(sock);
		return -EBUSY;
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
	nsock->cookie = 0;
	socks[config->num_connections++] = nsock;
	atomic_inc(&config->live_connections);

	return 0;
}

static int nbd_reconnect_socket(struct nbd_device *nbd, unsigned long arg)
{
	struct nbd_config *config = nbd->config;
	struct socket *sock, *old;
	struct recv_thread_args *args;
	int i;
	int err;

	sock = nbd_get_socket(nbd, arg, &err);
	if (!sock)
		return err;

	args = kzalloc(sizeof(*args), GFP_KERNEL);
	if (!args) {
		sockfd_put(sock);
		return -ENOMEM;
	}

	for (i = 0; i < config->num_connections; i++) {
		struct nbd_sock *nsock = config->socks[i];

		if (!nsock->dead)
			continue;

		mutex_lock(&nsock->tx_lock);
		if (!nsock->dead) {
			mutex_unlock(&nsock->tx_lock);
			continue;
		}
		sk_set_memalloc(sock->sk);
		if (nbd->tag_set.timeout)
			sock->sk->sk_sndtimeo = nbd->tag_set.timeout;
		atomic_inc(&config->recv_threads);
		refcount_inc(&nbd->config_refs);
		old = nsock->sock;
		nsock->fallback_index = -1;
		nsock->sock = sock;
		nsock->dead = false;
		INIT_WORK(&args->work, recv_work);
		args->index = i;
		args->nbd = nbd;
		nsock->cookie++;
		mutex_unlock(&nsock->tx_lock);
		sockfd_put(old);

		clear_bit(NBD_DISCONNECTED, &config->runtime_flags);

		/* We take the tx_mutex in an error path in the recv_work, so we
		 * need to queue_work outside of the tx_mutex.
		 */
		queue_work(nbd->recv_workq, &args->work);

		atomic_inc(&config->live_connections);
		wake_up(&config->conn_wait);
		return 0;
	}
	sockfd_put(sock);
	kfree(args);
	return -ENOSPC;
}

static void nbd_bdev_reset(struct block_device *bdev)
{
	if (bdev->bd_openers > 1)
		return;
	bd_set_size(bdev, 0);
}

static void nbd_parse_flags(struct nbd_device *nbd)
{
	struct nbd_config *config = nbd->config;
	if (config->flags & NBD_FLAG_READ_ONLY)
		set_disk_ro(nbd->disk, true);
	else
		set_disk_ro(nbd->disk, false);
	if (config->flags & NBD_FLAG_SEND_TRIM)
		blk_queue_flag_set(QUEUE_FLAG_DISCARD, nbd->disk->queue);
	if (config->flags & NBD_FLAG_SEND_FLUSH) {
		if (config->flags & NBD_FLAG_SEND_FUA)
			blk_queue_write_cache(nbd->disk->queue, true, true);
		else
			blk_queue_write_cache(nbd->disk->queue, true, false);
	}
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
		struct nbd_sock *nsock = config->socks[i];

		iov_iter_kvec(&from, WRITE | ITER_KVEC, &iov, 1, sizeof(request));
		mutex_lock(&nsock->tx_lock);
		ret = sock_xmit(nbd, i, 1, &from, 0, NULL);
		if (ret <= 0)
			dev_err(disk_to_dev(nbd->disk),
				"Send disconnect failed %d\n", ret);
		mutex_unlock(&nsock->tx_lock);
	}
}

static int nbd_disconnect(struct nbd_device *nbd)
{
	struct nbd_config *config = nbd->config;

	dev_info(disk_to_dev(nbd->disk), "NBD_DISCONNECT\n");
	set_bit(NBD_DISCONNECT_REQUESTED, &config->runtime_flags);
	send_disconnects(nbd);
	return 0;
}

static void nbd_clear_sock(struct nbd_device *nbd)
{
	sock_shutdown(nbd);
	nbd_clear_que(nbd);
	nbd->task_setup = NULL;
}

static void nbd_config_put(struct nbd_device *nbd)
{
	if (refcount_dec_and_mutex_lock(&nbd->config_refs,
					&nbd->config_lock)) {
		struct nbd_config *config = nbd->config;
		nbd_dev_dbg_close(nbd);
		nbd_size_clear(nbd);
		if (test_and_clear_bit(NBD_HAS_PID_FILE,
				       &config->runtime_flags))
			device_remove_file(disk_to_dev(nbd->disk), &pid_attr);
		nbd->task_recv = NULL;
		nbd_clear_sock(nbd);
		if (config->num_connections) {
			int i;
			for (i = 0; i < config->num_connections; i++) {
				sockfd_put(config->socks[i]->sock);
				kfree(config->socks[i]);
			}
			kfree(config->socks);
		}
		kfree(nbd->config);
		nbd->config = NULL;

		if (nbd->recv_workq)
			destroy_workqueue(nbd->recv_workq);
		nbd->recv_workq = NULL;

		nbd->tag_set.timeout = 0;
		nbd->disk->queue->limits.discard_granularity = 0;
		nbd->disk->queue->limits.discard_alignment = 0;
		blk_queue_max_discard_sectors(nbd->disk->queue, UINT_MAX);
		blk_queue_flag_clear(QUEUE_FLAG_DISCARD, nbd->disk->queue);

		mutex_unlock(&nbd->config_lock);
		nbd_put(nbd);
		module_put(THIS_MODULE);
	}
}

static int nbd_start_device(struct nbd_device *nbd)
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

	nbd->recv_workq = alloc_workqueue("knbd%d-recv",
					  WQ_MEM_RECLAIM | WQ_HIGHPRI |
					  WQ_UNBOUND, 0, nbd->index);
	if (!nbd->recv_workq) {
		dev_err(disk_to_dev(nbd->disk), "Could not allocate knbd recv work queue.\n");
		return -ENOMEM;
	}

	blk_mq_update_nr_hw_queues(&nbd->tag_set, config->num_connections);
	nbd->task_recv = current;

	nbd_parse_flags(nbd);

	error = device_create_file(disk_to_dev(nbd->disk), &pid_attr);
	if (error) {
		dev_err(disk_to_dev(nbd->disk), "device_create_file failed!\n");
		return error;
	}
	set_bit(NBD_HAS_PID_FILE, &config->runtime_flags);

	nbd_dev_dbg_init(nbd);
	for (i = 0; i < num_connections; i++) {
		struct recv_thread_args *args;

		args = kzalloc(sizeof(*args), GFP_KERNEL);
		if (!args) {
			sock_shutdown(nbd);
			return -ENOMEM;
		}
		sk_set_memalloc(config->socks[i]->sock->sk);
		if (nbd->tag_set.timeout)
			config->socks[i]->sock->sk->sk_sndtimeo =
				nbd->tag_set.timeout;
		atomic_inc(&config->recv_threads);
		refcount_inc(&nbd->config_refs);
		INIT_WORK(&args->work, recv_work);
		args->nbd = nbd;
		args->index = i;
		queue_work(nbd->recv_workq, &args->work);
	}
	nbd_size_update(nbd);
	return error;
}

static int nbd_start_device_ioctl(struct nbd_device *nbd, struct block_device *bdev)
{
	struct nbd_config *config = nbd->config;
	int ret;

	ret = nbd_start_device(nbd);
	if (ret)
		return ret;

	if (max_part)
		bdev->bd_invalidated = 1;
	mutex_unlock(&nbd->config_lock);
	ret = wait_event_interruptible(config->recv_wq,
					 atomic_read(&config->recv_threads) == 0);
	if (ret) {
		sock_shutdown(nbd);
		flush_workqueue(nbd->recv_workq);
	}
	mutex_lock(&nbd->config_lock);
	nbd_bdev_reset(bdev);
	/* user requested, ignore socket errors */
	if (test_bit(NBD_DISCONNECT_REQUESTED, &config->runtime_flags))
		ret = 0;
	if (test_bit(NBD_TIMEDOUT, &config->runtime_flags))
		ret = -ETIMEDOUT;
	return ret;
}

static void nbd_clear_sock_ioctl(struct nbd_device *nbd,
				 struct block_device *bdev)
{
	sock_shutdown(nbd);
	__invalidate_device(bdev, true);
	nbd_bdev_reset(bdev);
	if (test_and_clear_bit(NBD_HAS_CONFIG_REF,
			       &nbd->config->runtime_flags))
		nbd_config_put(nbd);
}

static bool nbd_is_valid_blksize(unsigned long blksize)
{
	if (!blksize || !is_power_of_2(blksize) || blksize < 512 ||
	    blksize > PAGE_SIZE)
		return false;
	return true;
}

/* Must be called with config_lock held */
static int __nbd_ioctl(struct block_device *bdev, struct nbd_device *nbd,
		       unsigned int cmd, unsigned long arg)
{
	struct nbd_config *config = nbd->config;

	switch (cmd) {
	case NBD_DISCONNECT:
		return nbd_disconnect(nbd);
	case NBD_CLEAR_SOCK:
		nbd_clear_sock_ioctl(nbd, bdev);
		return 0;
	case NBD_SET_SOCK:
		return nbd_add_socket(nbd, arg, false);
	case NBD_SET_BLKSIZE:
		if (!arg)
			arg = NBD_DEF_BLKSIZE;
		if (!nbd_is_valid_blksize(arg))
			return -EINVAL;
		nbd_size_set(nbd, arg,
			     div_s64(config->bytesize, arg));
		return 0;
	case NBD_SET_SIZE:
		nbd_size_set(nbd, config->blksize,
			     div_s64(arg, config->blksize));
		return 0;
	case NBD_SET_SIZE_BLOCKS:
		nbd_size_set(nbd, config->blksize, arg);
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
		return nbd_start_device_ioctl(nbd, bdev);
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
	struct nbd_config *config = nbd->config;
	int error = -EINVAL;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/* The block layer will pass back some non-nbd ioctls in case we have
	 * special handling for them, but we don't so just return an error.
	 */
	if (_IOC_TYPE(cmd) != 0xab)
		return -EINVAL;

	mutex_lock(&nbd->config_lock);

	/* Don't allow ioctl operations on a nbd device that was created with
	 * netlink, unless it's DISCONNECT or CLEAR_SOCK, which are fine.
	 */
	if (!test_bit(NBD_BOUND, &config->runtime_flags) ||
	    (cmd == NBD_DISCONNECT || cmd == NBD_CLEAR_SOCK))
		error = __nbd_ioctl(bdev, nbd, cmd, arg);
	else
		dev_err(nbd_to_dev(nbd), "Cannot use ioctl interface on a netlink controlled device.\n");
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
	init_waitqueue_head(&config->conn_wait);
	config->blksize = NBD_DEF_BLKSIZE;
	atomic_set(&config->live_connections, 0);
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
	if (!refcount_inc_not_zero(&nbd->refs)) {
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
		refcount_inc(&nbd->refs);
		mutex_unlock(&nbd->config_lock);
		bdev->bd_invalidated = 1;
	} else if (nbd_disconnected(nbd->config)) {
		bdev->bd_invalidated = 1;
	}
out:
	mutex_unlock(&nbd_index_mutex);
	return ret;
}

static void nbd_release(struct gendisk *disk, fmode_t mode)
{
	struct nbd_device *nbd = disk->private_data;
	struct block_device *bdev = bdget_disk(disk, 0);

	if (test_bit(NBD_DISCONNECT_ON_CLOSE, &nbd->config->runtime_flags) &&
			bdev->bd_openers == 0)
		nbd_disconnect_and_put(nbd);

	nbd_config_put(nbd);
	nbd_put(nbd);
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
	if (flags & NBD_FLAG_SEND_FUA)
		seq_puts(s, "NBD_FLAG_SEND_FUA\n");
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

static int nbd_init_request(struct blk_mq_tag_set *set, struct request *rq,
			    unsigned int hctx_idx, unsigned int numa_node)
{
	struct nbd_cmd *cmd = blk_mq_rq_to_pdu(rq);
	cmd->nbd = set->driver_data;
	cmd->flags = 0;
	mutex_init(&cmd->lock);
	return 0;
}

static const struct blk_mq_ops nbd_mq_ops = {
	.queue_rq	= nbd_queue_rq,
	.complete	= nbd_complete_rq,
	.init_request	= nbd_init_request,
	.timeout	= nbd_xmit_timeout,
};

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

	nbd->index = index;
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
	blk_queue_flag_set(QUEUE_FLAG_NONROT, disk->queue);
	blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, disk->queue);
	disk->queue->limits.discard_granularity = 0;
	disk->queue->limits.discard_alignment = 0;
	blk_queue_max_discard_sectors(disk->queue, 0);
	blk_queue_max_segment_size(disk->queue, UINT_MAX);
	blk_queue_max_segments(disk->queue, USHRT_MAX);
	blk_queue_max_hw_sectors(disk->queue, 65536);
	disk->queue->limits.max_sectors = 256;

	mutex_init(&nbd->config_lock);
	refcount_set(&nbd->config_refs, 0);
	refcount_set(&nbd->refs, 1);
	INIT_LIST_HEAD(&nbd->list);
	disk->major = NBD_MAJOR;
	disk->first_minor = index << part_shift;
	disk->fops = &nbd_fops;
	disk->private_data = nbd;
	sprintf(disk->disk_name, "nbd%d", index);
	add_disk(disk);
	nbd_total_devices++;
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

static int find_free_cb(int id, void *ptr, void *data)
{
	struct nbd_device *nbd = ptr;
	struct nbd_device **found = data;

	if (!refcount_read(&nbd->config_refs)) {
		*found = nbd;
		return 1;
	}
	return 0;
}

/* Netlink interface. */
static const struct nla_policy nbd_attr_policy[NBD_ATTR_MAX + 1] = {
	[NBD_ATTR_INDEX]		=	{ .type = NLA_U32 },
	[NBD_ATTR_SIZE_BYTES]		=	{ .type = NLA_U64 },
	[NBD_ATTR_BLOCK_SIZE_BYTES]	=	{ .type = NLA_U64 },
	[NBD_ATTR_TIMEOUT]		=	{ .type = NLA_U64 },
	[NBD_ATTR_SERVER_FLAGS]		=	{ .type = NLA_U64 },
	[NBD_ATTR_CLIENT_FLAGS]		=	{ .type = NLA_U64 },
	[NBD_ATTR_SOCKETS]		=	{ .type = NLA_NESTED},
	[NBD_ATTR_DEAD_CONN_TIMEOUT]	=	{ .type = NLA_U64 },
	[NBD_ATTR_DEVICE_LIST]		=	{ .type = NLA_NESTED},
};

static const struct nla_policy nbd_sock_policy[NBD_SOCK_MAX + 1] = {
	[NBD_SOCK_FD]			=	{ .type = NLA_U32 },
};

/* We don't use this right now since we don't parse the incoming list, but we
 * still want it here so userspace knows what to expect.
 */
static const struct nla_policy __attribute__((unused))
nbd_device_policy[NBD_DEVICE_ATTR_MAX + 1] = {
	[NBD_DEVICE_INDEX]		=	{ .type = NLA_U32 },
	[NBD_DEVICE_CONNECTED]		=	{ .type = NLA_U8 },
};

static int nbd_genl_connect(struct sk_buff *skb, struct genl_info *info)
{
	struct nbd_device *nbd = NULL;
	struct nbd_config *config;
	int index = -1;
	int ret;
	bool put_dev = false;

	if (!netlink_capable(skb, CAP_SYS_ADMIN))
		return -EPERM;

	if (info->attrs[NBD_ATTR_INDEX])
		index = nla_get_u32(info->attrs[NBD_ATTR_INDEX]);
	if (!info->attrs[NBD_ATTR_SOCKETS]) {
		printk(KERN_ERR "nbd: must specify at least one socket\n");
		return -EINVAL;
	}
	if (!info->attrs[NBD_ATTR_SIZE_BYTES]) {
		printk(KERN_ERR "nbd: must specify a size in bytes for the device\n");
		return -EINVAL;
	}
again:
	mutex_lock(&nbd_index_mutex);
	if (index == -1) {
		ret = idr_for_each(&nbd_index_idr, &find_free_cb, &nbd);
		if (ret == 0) {
			int new_index;
			new_index = nbd_dev_add(-1);
			if (new_index < 0) {
				mutex_unlock(&nbd_index_mutex);
				printk(KERN_ERR "nbd: failed to add new device\n");
				return new_index;
			}
			nbd = idr_find(&nbd_index_idr, new_index);
		}
	} else {
		nbd = idr_find(&nbd_index_idr, index);
		if (!nbd) {
			ret = nbd_dev_add(index);
			if (ret < 0) {
				mutex_unlock(&nbd_index_mutex);
				printk(KERN_ERR "nbd: failed to add new device\n");
				return ret;
			}
			nbd = idr_find(&nbd_index_idr, index);
		}
	}
	if (!nbd) {
		printk(KERN_ERR "nbd: couldn't find device at index %d\n",
		       index);
		mutex_unlock(&nbd_index_mutex);
		return -EINVAL;
	}
	if (!refcount_inc_not_zero(&nbd->refs)) {
		mutex_unlock(&nbd_index_mutex);
		if (index == -1)
			goto again;
		printk(KERN_ERR "nbd: device at index %d is going down\n",
		       index);
		return -EINVAL;
	}
	mutex_unlock(&nbd_index_mutex);

	mutex_lock(&nbd->config_lock);
	if (refcount_read(&nbd->config_refs)) {
		mutex_unlock(&nbd->config_lock);
		nbd_put(nbd);
		if (index == -1)
			goto again;
		printk(KERN_ERR "nbd: nbd%d already in use\n", index);
		return -EBUSY;
	}
	if (WARN_ON(nbd->config)) {
		mutex_unlock(&nbd->config_lock);
		nbd_put(nbd);
		return -EINVAL;
	}
	config = nbd->config = nbd_alloc_config();
	if (!nbd->config) {
		mutex_unlock(&nbd->config_lock);
		nbd_put(nbd);
		printk(KERN_ERR "nbd: couldn't allocate config\n");
		return -ENOMEM;
	}
	refcount_set(&nbd->config_refs, 1);
	set_bit(NBD_BOUND, &config->runtime_flags);

	if (info->attrs[NBD_ATTR_SIZE_BYTES]) {
		u64 bytes = nla_get_u64(info->attrs[NBD_ATTR_SIZE_BYTES]);
		nbd_size_set(nbd, config->blksize,
			     div64_u64(bytes, config->blksize));
	}
	if (info->attrs[NBD_ATTR_BLOCK_SIZE_BYTES]) {
		u64 bsize =
			nla_get_u64(info->attrs[NBD_ATTR_BLOCK_SIZE_BYTES]);
		if (!bsize)
			bsize = NBD_DEF_BLKSIZE;
		if (!nbd_is_valid_blksize(bsize)) {
			ret = -EINVAL;
			goto out;
		}
		nbd_size_set(nbd, bsize, div64_u64(config->bytesize, bsize));
	}
	if (info->attrs[NBD_ATTR_TIMEOUT]) {
		u64 timeout = nla_get_u64(info->attrs[NBD_ATTR_TIMEOUT]);
		nbd->tag_set.timeout = timeout * HZ;
		blk_queue_rq_timeout(nbd->disk->queue, timeout * HZ);
	}
	if (info->attrs[NBD_ATTR_DEAD_CONN_TIMEOUT]) {
		config->dead_conn_timeout =
			nla_get_u64(info->attrs[NBD_ATTR_DEAD_CONN_TIMEOUT]);
		config->dead_conn_timeout *= HZ;
	}
	if (info->attrs[NBD_ATTR_SERVER_FLAGS])
		config->flags =
			nla_get_u64(info->attrs[NBD_ATTR_SERVER_FLAGS]);
	if (info->attrs[NBD_ATTR_CLIENT_FLAGS]) {
		u64 flags = nla_get_u64(info->attrs[NBD_ATTR_CLIENT_FLAGS]);
		if (flags & NBD_CFLAG_DESTROY_ON_DISCONNECT) {
			set_bit(NBD_DESTROY_ON_DISCONNECT,
				&config->runtime_flags);
			put_dev = true;
		}
		if (flags & NBD_CFLAG_DISCONNECT_ON_CLOSE) {
			set_bit(NBD_DISCONNECT_ON_CLOSE,
				&config->runtime_flags);
		}
	}

	if (info->attrs[NBD_ATTR_SOCKETS]) {
		struct nlattr *attr;
		int rem, fd;

		nla_for_each_nested(attr, info->attrs[NBD_ATTR_SOCKETS],
				    rem) {
			struct nlattr *socks[NBD_SOCK_MAX+1];

			if (nla_type(attr) != NBD_SOCK_ITEM) {
				printk(KERN_ERR "nbd: socks must be embedded in a SOCK_ITEM attr\n");
				ret = -EINVAL;
				goto out;
			}
			ret = nla_parse_nested(socks, NBD_SOCK_MAX, attr,
					       nbd_sock_policy, info->extack);
			if (ret != 0) {
				printk(KERN_ERR "nbd: error processing sock list\n");
				ret = -EINVAL;
				goto out;
			}
			if (!socks[NBD_SOCK_FD])
				continue;
			fd = (int)nla_get_u32(socks[NBD_SOCK_FD]);
			ret = nbd_add_socket(nbd, fd, true);
			if (ret)
				goto out;
		}
	}
	ret = nbd_start_device(nbd);
out:
	mutex_unlock(&nbd->config_lock);
	if (!ret) {
		set_bit(NBD_HAS_CONFIG_REF, &config->runtime_flags);
		refcount_inc(&nbd->config_refs);
		nbd_connect_reply(info, nbd->index);
	}
	nbd_config_put(nbd);
	if (put_dev)
		nbd_put(nbd);
	return ret;
}

static void nbd_disconnect_and_put(struct nbd_device *nbd)
{
	mutex_lock(&nbd->config_lock);
	nbd_disconnect(nbd);
	nbd_clear_sock(nbd);
	mutex_unlock(&nbd->config_lock);
	/*
	 * Make sure recv thread has finished, so it does not drop the last
	 * config ref and try to destroy the workqueue from inside the work
	 * queue.
	 */
	flush_workqueue(nbd->recv_workq);
	if (test_and_clear_bit(NBD_HAS_CONFIG_REF,
			       &nbd->config->runtime_flags))
		nbd_config_put(nbd);
}

static int nbd_genl_disconnect(struct sk_buff *skb, struct genl_info *info)
{
	struct nbd_device *nbd;
	int index;

	if (!netlink_capable(skb, CAP_SYS_ADMIN))
		return -EPERM;

	if (!info->attrs[NBD_ATTR_INDEX]) {
		printk(KERN_ERR "nbd: must specify an index to disconnect\n");
		return -EINVAL;
	}
	index = nla_get_u32(info->attrs[NBD_ATTR_INDEX]);
	mutex_lock(&nbd_index_mutex);
	nbd = idr_find(&nbd_index_idr, index);
	if (!nbd) {
		mutex_unlock(&nbd_index_mutex);
		printk(KERN_ERR "nbd: couldn't find device at index %d\n",
		       index);
		return -EINVAL;
	}
	if (!refcount_inc_not_zero(&nbd->refs)) {
		mutex_unlock(&nbd_index_mutex);
		printk(KERN_ERR "nbd: device at index %d is going down\n",
		       index);
		return -EINVAL;
	}
	mutex_unlock(&nbd_index_mutex);
	if (!refcount_inc_not_zero(&nbd->config_refs)) {
		nbd_put(nbd);
		return 0;
	}
	nbd_disconnect_and_put(nbd);
	nbd_config_put(nbd);
	nbd_put(nbd);
	return 0;
}

static int nbd_genl_reconfigure(struct sk_buff *skb, struct genl_info *info)
{
	struct nbd_device *nbd = NULL;
	struct nbd_config *config;
	int index;
	int ret = 0;
	bool put_dev = false;

	if (!netlink_capable(skb, CAP_SYS_ADMIN))
		return -EPERM;

	if (!info->attrs[NBD_ATTR_INDEX]) {
		printk(KERN_ERR "nbd: must specify a device to reconfigure\n");
		return -EINVAL;
	}
	index = nla_get_u32(info->attrs[NBD_ATTR_INDEX]);
	mutex_lock(&nbd_index_mutex);
	nbd = idr_find(&nbd_index_idr, index);
	if (!nbd) {
		mutex_unlock(&nbd_index_mutex);
		printk(KERN_ERR "nbd: couldn't find a device at index %d\n",
		       index);
		return -EINVAL;
	}
	if (!refcount_inc_not_zero(&nbd->refs)) {
		mutex_unlock(&nbd_index_mutex);
		printk(KERN_ERR "nbd: device at index %d is going down\n",
		       index);
		return -EINVAL;
	}
	mutex_unlock(&nbd_index_mutex);

	if (!refcount_inc_not_zero(&nbd->config_refs)) {
		dev_err(nbd_to_dev(nbd),
			"not configured, cannot reconfigure\n");
		nbd_put(nbd);
		return -EINVAL;
	}

	mutex_lock(&nbd->config_lock);
	config = nbd->config;
	if (!test_bit(NBD_BOUND, &config->runtime_flags) ||
	    !nbd->task_recv) {
		dev_err(nbd_to_dev(nbd),
			"not configured, cannot reconfigure\n");
		ret = -EINVAL;
		goto out;
	}

	if (info->attrs[NBD_ATTR_TIMEOUT]) {
		u64 timeout = nla_get_u64(info->attrs[NBD_ATTR_TIMEOUT]);
		nbd->tag_set.timeout = timeout * HZ;
		blk_queue_rq_timeout(nbd->disk->queue, timeout * HZ);
	}
	if (info->attrs[NBD_ATTR_DEAD_CONN_TIMEOUT]) {
		config->dead_conn_timeout =
			nla_get_u64(info->attrs[NBD_ATTR_DEAD_CONN_TIMEOUT]);
		config->dead_conn_timeout *= HZ;
	}
	if (info->attrs[NBD_ATTR_CLIENT_FLAGS]) {
		u64 flags = nla_get_u64(info->attrs[NBD_ATTR_CLIENT_FLAGS]);
		if (flags & NBD_CFLAG_DESTROY_ON_DISCONNECT) {
			if (!test_and_set_bit(NBD_DESTROY_ON_DISCONNECT,
					      &config->runtime_flags))
				put_dev = true;
		} else {
			if (test_and_clear_bit(NBD_DESTROY_ON_DISCONNECT,
					       &config->runtime_flags))
				refcount_inc(&nbd->refs);
		}

		if (flags & NBD_CFLAG_DISCONNECT_ON_CLOSE) {
			set_bit(NBD_DISCONNECT_ON_CLOSE,
					&config->runtime_flags);
		} else {
			clear_bit(NBD_DISCONNECT_ON_CLOSE,
					&config->runtime_flags);
		}
	}

	if (info->attrs[NBD_ATTR_SOCKETS]) {
		struct nlattr *attr;
		int rem, fd;

		nla_for_each_nested(attr, info->attrs[NBD_ATTR_SOCKETS],
				    rem) {
			struct nlattr *socks[NBD_SOCK_MAX+1];

			if (nla_type(attr) != NBD_SOCK_ITEM) {
				printk(KERN_ERR "nbd: socks must be embedded in a SOCK_ITEM attr\n");
				ret = -EINVAL;
				goto out;
			}
			ret = nla_parse_nested(socks, NBD_SOCK_MAX, attr,
					       nbd_sock_policy, info->extack);
			if (ret != 0) {
				printk(KERN_ERR "nbd: error processing sock list\n");
				ret = -EINVAL;
				goto out;
			}
			if (!socks[NBD_SOCK_FD])
				continue;
			fd = (int)nla_get_u32(socks[NBD_SOCK_FD]);
			ret = nbd_reconnect_socket(nbd, fd);
			if (ret) {
				if (ret == -ENOSPC)
					ret = 0;
				goto out;
			}
			dev_info(nbd_to_dev(nbd), "reconnected socket\n");
		}
	}
out:
	mutex_unlock(&nbd->config_lock);
	nbd_config_put(nbd);
	nbd_put(nbd);
	if (put_dev)
		nbd_put(nbd);
	return ret;
}

static const struct genl_ops nbd_connect_genl_ops[] = {
	{
		.cmd	= NBD_CMD_CONNECT,
		.policy	= nbd_attr_policy,
		.doit	= nbd_genl_connect,
	},
	{
		.cmd	= NBD_CMD_DISCONNECT,
		.policy	= nbd_attr_policy,
		.doit	= nbd_genl_disconnect,
	},
	{
		.cmd	= NBD_CMD_RECONFIGURE,
		.policy	= nbd_attr_policy,
		.doit	= nbd_genl_reconfigure,
	},
	{
		.cmd	= NBD_CMD_STATUS,
		.policy	= nbd_attr_policy,
		.doit	= nbd_genl_status,
	},
};

static const struct genl_multicast_group nbd_mcast_grps[] = {
	{ .name = NBD_GENL_MCAST_GROUP_NAME, },
};

static struct genl_family nbd_genl_family __ro_after_init = {
	.hdrsize	= 0,
	.name		= NBD_GENL_FAMILY_NAME,
	.version	= NBD_GENL_VERSION,
	.module		= THIS_MODULE,
	.ops		= nbd_connect_genl_ops,
	.n_ops		= ARRAY_SIZE(nbd_connect_genl_ops),
	.maxattr	= NBD_ATTR_MAX,
	.mcgrps		= nbd_mcast_grps,
	.n_mcgrps	= ARRAY_SIZE(nbd_mcast_grps),
};

static int populate_nbd_status(struct nbd_device *nbd, struct sk_buff *reply)
{
	struct nlattr *dev_opt;
	u8 connected = 0;
	int ret;

	/* This is a little racey, but for status it's ok.  The
	 * reason we don't take a ref here is because we can't
	 * take a ref in the index == -1 case as we would need
	 * to put under the nbd_index_mutex, which could
	 * deadlock if we are configured to remove ourselves
	 * once we're disconnected.
	 */
	if (refcount_read(&nbd->config_refs))
		connected = 1;
	dev_opt = nla_nest_start(reply, NBD_DEVICE_ITEM);
	if (!dev_opt)
		return -EMSGSIZE;
	ret = nla_put_u32(reply, NBD_DEVICE_INDEX, nbd->index);
	if (ret)
		return -EMSGSIZE;
	ret = nla_put_u8(reply, NBD_DEVICE_CONNECTED,
			 connected);
	if (ret)
		return -EMSGSIZE;
	nla_nest_end(reply, dev_opt);
	return 0;
}

static int status_cb(int id, void *ptr, void *data)
{
	struct nbd_device *nbd = ptr;
	return populate_nbd_status(nbd, (struct sk_buff *)data);
}

static int nbd_genl_status(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *dev_list;
	struct sk_buff *reply;
	void *reply_head;
	size_t msg_size;
	int index = -1;
	int ret = -ENOMEM;

	if (info->attrs[NBD_ATTR_INDEX])
		index = nla_get_u32(info->attrs[NBD_ATTR_INDEX]);

	mutex_lock(&nbd_index_mutex);

	msg_size = nla_total_size(nla_attr_size(sizeof(u32)) +
				  nla_attr_size(sizeof(u8)));
	msg_size *= (index == -1) ? nbd_total_devices : 1;

	reply = genlmsg_new(msg_size, GFP_KERNEL);
	if (!reply)
		goto out;
	reply_head = genlmsg_put_reply(reply, info, &nbd_genl_family, 0,
				       NBD_CMD_STATUS);
	if (!reply_head) {
		nlmsg_free(reply);
		goto out;
	}

	dev_list = nla_nest_start(reply, NBD_ATTR_DEVICE_LIST);
	if (index == -1) {
		ret = idr_for_each(&nbd_index_idr, &status_cb, reply);
		if (ret) {
			nlmsg_free(reply);
			goto out;
		}
	} else {
		struct nbd_device *nbd;
		nbd = idr_find(&nbd_index_idr, index);
		if (nbd) {
			ret = populate_nbd_status(nbd, reply);
			if (ret) {
				nlmsg_free(reply);
				goto out;
			}
		}
	}
	nla_nest_end(reply, dev_list);
	genlmsg_end(reply, reply_head);
	genlmsg_reply(reply, info);
	ret = 0;
out:
	mutex_unlock(&nbd_index_mutex);
	return ret;
}

static void nbd_connect_reply(struct genl_info *info, int index)
{
	struct sk_buff *skb;
	void *msg_head;
	int ret;

	skb = genlmsg_new(nla_total_size(sizeof(u32)), GFP_KERNEL);
	if (!skb)
		return;
	msg_head = genlmsg_put_reply(skb, info, &nbd_genl_family, 0,
				     NBD_CMD_CONNECT);
	if (!msg_head) {
		nlmsg_free(skb);
		return;
	}
	ret = nla_put_u32(skb, NBD_ATTR_INDEX, index);
	if (ret) {
		nlmsg_free(skb);
		return;
	}
	genlmsg_end(skb, msg_head);
	genlmsg_reply(skb, info);
}

static void nbd_mcast_index(int index)
{
	struct sk_buff *skb;
	void *msg_head;
	int ret;

	skb = genlmsg_new(nla_total_size(sizeof(u32)), GFP_KERNEL);
	if (!skb)
		return;
	msg_head = genlmsg_put(skb, 0, 0, &nbd_genl_family, 0,
				     NBD_CMD_LINK_DEAD);
	if (!msg_head) {
		nlmsg_free(skb);
		return;
	}
	ret = nla_put_u32(skb, NBD_ATTR_INDEX, index);
	if (ret) {
		nlmsg_free(skb);
		return;
	}
	genlmsg_end(skb, msg_head);
	genlmsg_multicast(&nbd_genl_family, skb, 0, 0, GFP_KERNEL);
}

static void nbd_dead_link_work(struct work_struct *work)
{
	struct link_dead_args *args = container_of(work, struct link_dead_args,
						   work);
	nbd_mcast_index(args->index);
	kfree(args);
}

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

	if (register_blkdev(NBD_MAJOR, "nbd"))
		return -EIO;

	if (genl_register_family(&nbd_genl_family)) {
		unregister_blkdev(NBD_MAJOR, "nbd");
		return -EINVAL;
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
	struct list_head *list = (struct list_head *)data;
	struct nbd_device *nbd = ptr;

	list_add_tail(&nbd->list, list);
	return 0;
}

static void __exit nbd_cleanup(void)
{
	struct nbd_device *nbd;
	LIST_HEAD(del_list);

	nbd_dbg_close();

	mutex_lock(&nbd_index_mutex);
	idr_for_each(&nbd_index_idr, &nbd_exit_cb, &del_list);
	mutex_unlock(&nbd_index_mutex);

	while (!list_empty(&del_list)) {
		nbd = list_first_entry(&del_list, struct nbd_device, list);
		list_del_init(&nbd->list);
		if (refcount_read(&nbd->refs) != 1)
			printk(KERN_ERR "nbd: possibly leaking a device\n");
		nbd_put(nbd);
	}

	idr_destroy(&nbd_index_idr);
	genl_unregister_family(&nbd_genl_family);
	unregister_blkdev(NBD_MAJOR, "nbd");
}

module_init(nbd_init);
module_exit(nbd_cleanup);

MODULE_DESCRIPTION("Network Block Device");
MODULE_LICENSE("GPL");

module_param(nbds_max, int, 0444);
MODULE_PARM_DESC(nbds_max, "number of network block devices to initialize (default: 16)");
module_param(max_part, int, 0444);
MODULE_PARM_DESC(max_part, "number of partitions per device (default: 16)");
