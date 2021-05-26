// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2020 Google, Inc
 * Copyright (C) 2020 Palmer Dabbelt <palmerdabbelt@google.com>
 */

#include <linux/device-mapper.h>
#include <uapi/linux/dm-user.h>

#include <linux/bio.h>
#include <linux/init.h>
#include <linux/mempool.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/uio.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#define DM_MSG_PREFIX "user"

#define MAX_OUTSTANDING_MESSAGES 128

static unsigned int daemon_timeout_msec = 4000;
module_param_named(dm_user_daemon_timeout_msec, daemon_timeout_msec, uint,
		   0644);
MODULE_PARM_DESC(dm_user_daemon_timeout_msec,
		 "IO Timeout in msec if daemon does not process");

/*
 * dm-user uses four structures:
 *
 *  - "struct target", the outermost structure, corresponds to a single device
 *    mapper target.  This contains the set of outstanding BIOs that have been
 *    provided by DM and are not actively being processed by the user, along
 *    with a misc device that userspace can open to communicate with the
 *    kernel.  Each time userspaces opens the misc device a new channel is
 *    created.
 *  - "struct channel", which represents a single active communication channel
 *    with userspace.  Userspace may choose arbitrary read/write sizes to use
 *    when processing messages, channels form these into logical accesses.
 *    When userspace responds to a full message the channel completes the BIO
 *    and obtains a new message to process from the target.
 *  - "struct message", which wraps a BIO with the additional information
 *    required by the kernel to sort out what to do with BIOs when they return
 *    from userspace.
 *  - "struct dm_user_message", which is the exact message format that
 *    userspace sees.
 *
 * The hot path contains three distinct operations:
 *
 *  - user_map(), which is provided a BIO from device mapper that is queued
 *    into the target.  This allocates and enqueues a new message.
 *  - dev_read(), which dequeues a message, copies it to userspace.
 *  - dev_write(), which looks up a message (keyed by sequence number) and
 *    completes the corresponding BIO.
 *
 * Lock ordering (outer to inner)
 *
 * 1) miscdevice's global lock.  This is held around dev_open, so it has to be
 *    the outermost lock.
 * 2) target->lock
 * 3) channel->lock
 */

struct message {
	/*
	 * Messages themselves do not need a lock, they're protected by either
	 * the target or channel's lock, depending on which can reference them
	 * directly.
	 */
	struct dm_user_message msg;
	struct bio *bio;
	size_t posn_to_user;
	size_t total_to_user;
	size_t posn_from_user;
	size_t total_from_user;

	struct list_head from_user;
	struct list_head to_user;

	/*
	 * These are written back from the user.  They live in the same spot in
	 * the message, but we need to either keep the old values around or
	 * call a bunch more BIO helpers.  These are only valid after write has
	 * adopted the message.
	 */
	u64 return_type;
	u64 return_flags;

	struct delayed_work work;
	bool delayed;
	struct target *t;
};

struct target {
	/*
	 * A target has a single lock, which protects everything in the target
	 * (but does not protect the channels associated with a target).
	 */
	struct mutex lock;

	/*
	 * There is only one point at which anything blocks: userspace blocks
	 * reading a new message, which is woken up by device mapper providing
	 * a new BIO to process (or tearing down the target).  The
	 * corresponding write side doesn't block, instead we treat userspace's
	 * response containing a message that has yet to be mapped as an
	 * invalid operation.
	 */
	struct wait_queue_head wq;

	/*
	 * Messages are delivered to userspace in order, but may be returned
	 * out of order.  This allows userspace to schedule IO if it wants to.
	 */
	mempool_t message_pool;
	u64 next_seq_to_map;
	u64 next_seq_to_user;
	struct list_head to_user;

	/*
	 * There is a misc device per target.  The name is selected by
	 * userspace (via a DM create ioctl argument), and each ends up in
	 * /dev/dm-user/.  It looks like a better way to do this may be to have
	 * a filesystem to manage these, but this was more expedient.  The
	 * current mechanism is functional, but does result in an arbitrary
	 * number of dynamically created misc devices.
	 */
	struct miscdevice miscdev;

	/*
	 * Device mapper's target destructor triggers tearing this all down,
	 * but we can't actually free until every channel associated with this
	 * target has been destroyed.  Channels each have a reference to their
	 * target, and there is an additional single reference that corresponds
	 * to both DM and the misc device (both of which are destroyed by DM).
	 *
	 * In the common case userspace will be asleep waiting for a new
	 * message when device mapper decides to destroy the target, which
	 * means no new messages will appear.  The destroyed flag triggers a
	 * wakeup, which will end up removing the reference.
	 */
	struct kref references;
	int dm_destroyed;
	bool daemon_terminated;
};

struct channel {
	struct target *target;

	/*
	 * A channel has a single lock, which prevents multiple reads (or
	 * multiple writes) from conflicting with each other.
	 */
	struct mutex lock;

	struct message *cur_to_user;
	struct message *cur_from_user;
	ssize_t to_user_error;
	ssize_t from_user_error;

	/*
	 * Once a message has been forwarded to userspace on a channel it must
	 * be responded to on the same channel.  This allows us to error out
	 * the messages that have not yet been responded to by a channel when
	 * that channel closes, which makes handling errors more reasonable for
	 * fault-tolerant userspace daemons.  It also happens to make avoiding
	 * shared locks between user_map() and dev_read() a lot easier.
	 *
	 * This does preclude a multi-threaded work stealing userspace
	 * implementation (or at least, force a degree of head-of-line blocking
	 * on the response path).
	 */
	struct list_head from_user;

	/*
	 * Responses from userspace can arrive in arbitrarily small chunks.
	 * We need some place to buffer one up until we can find the
	 * corresponding kernel-side message to continue processing, so instead
	 * of allocating them we just keep one off to the side here.  This can
	 * only ever be pointer to by from_user_cur, and will never have a BIO.
	 */
	struct message scratch_message_from_user;
};

static void message_kill(struct message *m, mempool_t *pool)
{
	m->bio->bi_status = BLK_STS_IOERR;
	bio_endio(m->bio);
	bio_put(m->bio);
	mempool_free(m, pool);
}

static inline bool is_user_space_thread_present(struct target *t)
{
	lockdep_assert_held(&t->lock);
	return (kref_read(&t->references) > 1);
}

static void process_delayed_work(struct work_struct *work)
{
	struct delayed_work *del_work = to_delayed_work(work);
	struct message *msg = container_of(del_work, struct message, work);

	struct target *t = msg->t;

	mutex_lock(&t->lock);

	/*
	 * There is at least one thread to process the IO.
	 */
	if (is_user_space_thread_present(t)) {
		mutex_unlock(&t->lock);
		return;
	}

	/*
	 * Terminate the IO with an error
	 */
	list_del(&msg->to_user);
	pr_err("I/O error: sector %llu: no user-space daemon for %s target\n",
	       msg->bio->bi_iter.bi_sector,
	       t->miscdev.name);
	message_kill(msg, &t->message_pool);
	mutex_unlock(&t->lock);
}

static void enqueue_delayed_work(struct message *m, bool is_delay)
{
	unsigned long delay = 0;

	m->delayed = true;
	INIT_DELAYED_WORK(&m->work, process_delayed_work);

	/*
	 * Snapuserd daemon is the user-space process
	 * which processes IO request from dm-user
	 * when OTA is applied. Per the current design,
	 * when a dm-user target is created, daemon
	 * attaches to target and starts processing
	 * the IO's. Daemon is terminated only when
	 * dm-user target is destroyed.
	 *
	 * If for some reason, daemon crashes or terminates early,
	 * without destroying the dm-user target; then
	 * there is no mechanism to restart the daemon
	 * and start processing the IO's from the same target.
	 * Theoretically, it is possible but that infrastructure
	 * doesn't exist in the android ecosystem.
	 *
	 * Thus, when the daemon terminates, there is no way the IO's
	 * issued on that target will be processed. Hence,
	 * we set the delay to 0 and fail the IO's immediately.
	 *
	 * On the other hand, when a new dm-user target is created,
	 * we wait for the daemon to get attached for the first time.
	 * This primarily happens when init first stage spins up
	 * the daemon. At this point, since the snapshot device is mounted
	 * of a root filesystem, dm-user target may receive IO request
	 * even though daemon is not fully launched. We don't want
	 * to fail those IO requests immediately. Thus, we queue these
	 * requests with a timeout so that daemon is ready to process
	 * those IO requests. Again, if the daemon fails to launch within
	 * the timeout period, then IO's will be failed.
	 */
	if (is_delay)
		delay = msecs_to_jiffies(daemon_timeout_msec);

	queue_delayed_work(system_wq, &m->work, delay);
}

static inline struct target *target_from_target(struct dm_target *target)
{
	WARN_ON(target->private == NULL);
	return target->private;
}

static inline struct target *target_from_miscdev(struct miscdevice *miscdev)
{
	return container_of(miscdev, struct target, miscdev);
}

static inline struct channel *channel_from_file(struct file *file)
{
	WARN_ON(file->private_data == NULL);
	return file->private_data;
}

static inline struct target *target_from_channel(struct channel *c)
{
	WARN_ON(c->target == NULL);
	return c->target;
}

static inline size_t bio_size(struct bio *bio)
{
	struct bio_vec bvec;
	struct bvec_iter iter;
	size_t out = 0;

	bio_for_each_segment (bvec, bio, iter)
		out += bio_iter_len(bio, iter);
	return out;
}

static inline size_t bio_bytes_needed_to_user(struct bio *bio)
{
	switch (bio_op(bio)) {
	case REQ_OP_WRITE:
		return sizeof(struct dm_user_message) + bio_size(bio);
	case REQ_OP_READ:
	case REQ_OP_FLUSH:
	case REQ_OP_DISCARD:
	case REQ_OP_SECURE_ERASE:
	case REQ_OP_WRITE_SAME:
	case REQ_OP_WRITE_ZEROES:
		return sizeof(struct dm_user_message);

	/*
	 * These ops are not passed to userspace under the assumption that
	 * they're not going to be particularly useful in that context.
	 */
	default:
		return -EOPNOTSUPP;
	}
}

static inline size_t bio_bytes_needed_from_user(struct bio *bio)
{
	switch (bio_op(bio)) {
	case REQ_OP_READ:
		return sizeof(struct dm_user_message) + bio_size(bio);
	case REQ_OP_WRITE:
	case REQ_OP_FLUSH:
	case REQ_OP_DISCARD:
	case REQ_OP_SECURE_ERASE:
	case REQ_OP_WRITE_SAME:
	case REQ_OP_WRITE_ZEROES:
		return sizeof(struct dm_user_message);

	/*
	 * These ops are not passed to userspace under the assumption that
	 * they're not going to be particularly useful in that context.
	 */
	default:
		return -EOPNOTSUPP;
	}
}

static inline long bio_type_to_user_type(struct bio *bio)
{
	switch (bio_op(bio)) {
	case REQ_OP_READ:
		return DM_USER_REQ_MAP_READ;
	case REQ_OP_WRITE:
		return DM_USER_REQ_MAP_WRITE;
	case REQ_OP_FLUSH:
		return DM_USER_REQ_MAP_FLUSH;
	case REQ_OP_DISCARD:
		return DM_USER_REQ_MAP_DISCARD;
	case REQ_OP_SECURE_ERASE:
		return DM_USER_REQ_MAP_SECURE_ERASE;
	case REQ_OP_WRITE_SAME:
		return DM_USER_REQ_MAP_WRITE_SAME;
	case REQ_OP_WRITE_ZEROES:
		return DM_USER_REQ_MAP_WRITE_ZEROES;

	/*
	 * These ops are not passed to userspace under the assumption that
	 * they're not going to be particularly useful in that context.
	 */
	default:
		return -EOPNOTSUPP;
	}
}

static inline long bio_flags_to_user_flags(struct bio *bio)
{
	u64 out = 0;
	typeof(bio->bi_opf) opf = bio->bi_opf & ~REQ_OP_MASK;

	if (opf & REQ_FAILFAST_DEV) {
		opf &= ~REQ_FAILFAST_DEV;
		out |= DM_USER_REQ_MAP_FLAG_FAILFAST_DEV;
	}

	if (opf & REQ_FAILFAST_TRANSPORT) {
		opf &= ~REQ_FAILFAST_TRANSPORT;
		out |= DM_USER_REQ_MAP_FLAG_FAILFAST_TRANSPORT;
	}

	if (opf & REQ_FAILFAST_DRIVER) {
		opf &= ~REQ_FAILFAST_DRIVER;
		out |= DM_USER_REQ_MAP_FLAG_FAILFAST_DRIVER;
	}

	if (opf & REQ_SYNC) {
		opf &= ~REQ_SYNC;
		out |= DM_USER_REQ_MAP_FLAG_SYNC;
	}

	if (opf & REQ_META) {
		opf &= ~REQ_META;
		out |= DM_USER_REQ_MAP_FLAG_META;
	}

	if (opf & REQ_PRIO) {
		opf &= ~REQ_PRIO;
		out |= DM_USER_REQ_MAP_FLAG_PRIO;
	}

	if (opf & REQ_NOMERGE) {
		opf &= ~REQ_NOMERGE;
		out |= DM_USER_REQ_MAP_FLAG_NOMERGE;
	}

	if (opf & REQ_IDLE) {
		opf &= ~REQ_IDLE;
		out |= DM_USER_REQ_MAP_FLAG_IDLE;
	}

	if (opf & REQ_INTEGRITY) {
		opf &= ~REQ_INTEGRITY;
		out |= DM_USER_REQ_MAP_FLAG_INTEGRITY;
	}

	if (opf & REQ_FUA) {
		opf &= ~REQ_FUA;
		out |= DM_USER_REQ_MAP_FLAG_FUA;
	}

	if (opf & REQ_PREFLUSH) {
		opf &= ~REQ_PREFLUSH;
		out |= DM_USER_REQ_MAP_FLAG_PREFLUSH;
	}

	if (opf & REQ_RAHEAD) {
		opf &= ~REQ_RAHEAD;
		out |= DM_USER_REQ_MAP_FLAG_RAHEAD;
	}

	if (opf & REQ_BACKGROUND) {
		opf &= ~REQ_BACKGROUND;
		out |= DM_USER_REQ_MAP_FLAG_BACKGROUND;
	}

	if (opf & REQ_NOWAIT) {
		opf &= ~REQ_NOWAIT;
		out |= DM_USER_REQ_MAP_FLAG_NOWAIT;
	}

	if (opf & REQ_NOUNMAP) {
		opf &= ~REQ_NOUNMAP;
		out |= DM_USER_REQ_MAP_FLAG_NOUNMAP;
	}

	if (unlikely(opf)) {
		pr_warn("unsupported BIO type %x\n", opf);
		return -EOPNOTSUPP;
	}
	WARN_ON(out < 0);
	return out;
}

/*
 * Not quite what's in blk-map.c, but instead what I thought the functions in
 * blk-map did.  This one seems more generally useful and I think we could
 * write the blk-map version in terms of this one.  The differences are that
 * this has a return value that counts, and blk-map uses the BIO _all iters.
 * Neither  advance the BIO iter but don't advance the IOV iter, which is a bit
 * odd here.
 */
static ssize_t bio_copy_from_iter(struct bio *bio, struct iov_iter *iter)
{
	struct bio_vec bvec;
	struct bvec_iter biter;
	ssize_t out = 0;

	bio_for_each_segment (bvec, bio, biter) {
		ssize_t ret;

		ret = copy_page_from_iter(bvec.bv_page, bvec.bv_offset,
					  bvec.bv_len, iter);

		/*
		 * FIXME: I thought that IOV copies had a mechanism for
		 * terminating early, if for example a signal came in while
		 * sleeping waiting for a page to be mapped, but I don't see
		 * where that would happen.
		 */
		WARN_ON(ret < 0);
		out += ret;

		if (!iov_iter_count(iter))
			break;

		if (ret < bvec.bv_len)
			return ret;
	}

	return out;
}

static ssize_t bio_copy_to_iter(struct bio *bio, struct iov_iter *iter)
{
	struct bio_vec bvec;
	struct bvec_iter biter;
	ssize_t out = 0;

	bio_for_each_segment (bvec, bio, biter) {
		ssize_t ret;

		ret = copy_page_to_iter(bvec.bv_page, bvec.bv_offset,
					bvec.bv_len, iter);

		/* as above */
		WARN_ON(ret < 0);
		out += ret;

		if (!iov_iter_count(iter))
			break;

		if (ret < bvec.bv_len)
			return ret;
	}

	return out;
}

static ssize_t msg_copy_to_iov(struct message *msg, struct iov_iter *to)
{
	ssize_t copied = 0;

	if (!iov_iter_count(to))
		return 0;

	if (msg->posn_to_user < sizeof(msg->msg)) {
		copied = copy_to_iter((char *)(&msg->msg) + msg->posn_to_user,
				      sizeof(msg->msg) - msg->posn_to_user, to);
	} else {
		copied = bio_copy_to_iter(msg->bio, to);
		if (copied > 0)
			bio_advance(msg->bio, copied);
	}

	if (copied < 0)
		return copied;

	msg->posn_to_user += copied;
	return copied;
}

static ssize_t msg_copy_from_iov(struct message *msg, struct iov_iter *from)
{
	ssize_t copied = 0;

	if (!iov_iter_count(from))
		return 0;

	if (msg->posn_from_user < sizeof(msg->msg)) {
		copied = copy_from_iter(
			(char *)(&msg->msg) + msg->posn_from_user,
			sizeof(msg->msg) - msg->posn_from_user, from);
	} else {
		copied = bio_copy_from_iter(msg->bio, from);
		if (copied > 0)
			bio_advance(msg->bio, copied);
	}

	if (copied < 0)
		return copied;

	msg->posn_from_user += copied;
	return copied;
}

static struct message *msg_get_map(struct target *t)
{
	struct message *m;

	lockdep_assert_held(&t->lock);

	m = mempool_alloc(&t->message_pool, GFP_NOIO);
	m->msg.seq = t->next_seq_to_map++;
	INIT_LIST_HEAD(&m->to_user);
	INIT_LIST_HEAD(&m->from_user);
	return m;
}

static struct message *msg_get_to_user(struct target *t)
{
	struct message *m;

	lockdep_assert_held(&t->lock);

	if (list_empty(&t->to_user))
		return NULL;

	m = list_first_entry(&t->to_user, struct message, to_user);

	list_del(&m->to_user);

	/*
	 * If the IO was queued to workqueue since there
	 * was no daemon to service the IO, then we
	 * will have to cancel the delayed work as the
	 * IO will be processed by this user-space thread.
	 *
	 * If the delayed work was already picked up for
	 * processing, then wait for it to complete. Note
	 * that the IO will not be terminated by the work
	 * queue thread.
	 */
	if (unlikely(m->delayed)) {
		mutex_unlock(&t->lock);
		cancel_delayed_work_sync(&m->work);
		mutex_lock(&t->lock);
	}
	return m;
}

static struct message *msg_get_from_user(struct channel *c, u64 seq)
{
	struct message *m;
	struct list_head *cur, *tmp;

	lockdep_assert_held(&c->lock);

	list_for_each_safe (cur, tmp, &c->from_user) {
		m = list_entry(cur, struct message, from_user);
		if (m->msg.seq == seq) {
			list_del(&m->from_user);
			return m;
		}
	}

	return NULL;
}

/*
 * Returns 0 when there is no work left to do.  This must be callable without
 * holding the target lock, as it is part of the waitqueue's check expression.
 * When called without the lock it may spuriously indicate there is remaining
 * work, but when called with the lock it must be accurate.
 */
static int target_poll(struct target *t)
{
	return !list_empty(&t->to_user) || t->dm_destroyed;
}

static void target_release(struct kref *ref)
{
	struct target *t = container_of(ref, struct target, references);
	struct list_head *cur, *tmp;

	/*
	 * There may be outstanding BIOs that have not yet been given to
	 * userspace.  At this point there's nothing we can do about them, as
	 * there are and will never be any channels.
	 */
	list_for_each_safe (cur, tmp, &t->to_user) {
		struct message *m = list_entry(cur, struct message, to_user);

		if (unlikely(m->delayed)) {
			bool ret;

			mutex_unlock(&t->lock);
			ret = cancel_delayed_work_sync(&m->work);
			mutex_lock(&t->lock);
			if (!ret)
				continue;
		}
		message_kill(m, &t->message_pool);
	}

	mempool_exit(&t->message_pool);
	mutex_unlock(&t->lock);
	mutex_destroy(&t->lock);
	kfree(t);
}

static void target_put(struct target *t)
{
	/*
	 * This both releases a reference to the target and the lock.  We leave
	 * it up to the caller to hold the lock, as they probably needed it for
	 * something else.
	 */
	lockdep_assert_held(&t->lock);

	if (!kref_put(&t->references, target_release)) {
		/*
		 * User-space thread is getting terminated.
		 * We need to scan the list for all those
		 * pending IO's which were not processed yet
		 * and put them back to work-queue for delayed
		 * processing.
		 */
		if (!is_user_space_thread_present(t)) {
			struct list_head *cur, *tmp;

			list_for_each_safe(cur, tmp, &t->to_user) {
				struct message *m = list_entry(cur,
							       struct message,
							       to_user);
				if (!m->delayed)
					enqueue_delayed_work(m, false);
			}
			/*
			 * Daemon attached to this target is terminated.
			 */
			t->daemon_terminated = true;
		}
		mutex_unlock(&t->lock);
	}
}

static struct channel *channel_alloc(struct target *t)
{
	struct channel *c;

	lockdep_assert_held(&t->lock);

	c = kzalloc(sizeof(*c), GFP_KERNEL);
	if (c == NULL)
		return NULL;

	kref_get(&t->references);
	c->target = t;
	c->cur_from_user = &c->scratch_message_from_user;
	mutex_init(&c->lock);
	INIT_LIST_HEAD(&c->from_user);
	return c;
}

static void channel_free(struct channel *c)
{
	struct list_head *cur, *tmp;

	lockdep_assert_held(&c->lock);

	/*
	 * There may be outstanding BIOs that have been given to userspace but
	 * have not yet been completed.  The channel has been shut down so
	 * there's no way to process the rest of those messages, so we just go
	 * ahead and error out the BIOs.  Hopefully whatever's on the other end
	 * can handle the errors.  One could imagine splitting the BIOs and
	 * completing as much as we got, but that seems like overkill here.
	 *
	 * Our only other options would be to let the BIO hang around (which
	 * seems way worse) or to resubmit it to userspace in the hope there's
	 * another channel.  I don't really like the idea of submitting a
	 * message twice.
	 */
	if (c->cur_to_user != NULL)
		message_kill(c->cur_to_user, &c->target->message_pool);
	if (c->cur_from_user != &c->scratch_message_from_user)
		message_kill(c->cur_from_user, &c->target->message_pool);
	list_for_each_safe (cur, tmp, &c->from_user)
		message_kill(list_entry(cur, struct message, from_user),
			     &c->target->message_pool);

	mutex_lock(&c->target->lock);
	target_put(c->target);
	mutex_unlock(&c->lock);
	mutex_destroy(&c->lock);
	kfree(c);
}

static int dev_open(struct inode *inode, struct file *file)
{
	struct channel *c;
	struct target *t;

	/*
	 * This is called by miscdev, which sets private_data to point to the
	 * struct miscdevice that was opened.  The rest of our file operations
	 * want to refer to the channel that's been opened, so we swap that
	 * pointer out with a fresh channel.
	 *
	 * This is called with the miscdev lock held, which is also held while
	 * registering/unregistering the miscdev.  The miscdev must be
	 * registered for this to get called, which means there must be an
	 * outstanding reference to the target, which means it cannot be freed
	 * out from under us despite us not holding a reference yet.
	 */
	t = container_of(file->private_data, struct target, miscdev);
	mutex_lock(&t->lock);
	file->private_data = c = channel_alloc(t);

	if (c == NULL) {
		mutex_unlock(&t->lock);
		return -ENOMEM;
	}

	mutex_unlock(&t->lock);
	return 0;
}

static ssize_t dev_read(struct kiocb *iocb, struct iov_iter *to)
{
	struct channel *c = channel_from_file(iocb->ki_filp);
	ssize_t total_processed = 0;
	ssize_t processed;

	mutex_lock(&c->lock);

	if (unlikely(c->to_user_error)) {
		total_processed = c->to_user_error;
		goto cleanup_unlock;
	}

	if (c->cur_to_user == NULL) {
		struct target *t = target_from_channel(c);

		mutex_lock(&t->lock);

		while (!target_poll(t)) {
			int e;

			mutex_unlock(&t->lock);
			mutex_unlock(&c->lock);
			e = wait_event_interruptible(t->wq, target_poll(t));
			mutex_lock(&c->lock);
			mutex_lock(&t->lock);

			if (unlikely(e != 0)) {
				/*
				 * We haven't processed any bytes in either the
				 * BIO or the IOV, so we can just terminate
				 * right now.  Elsewhere in the kernel handles
				 * restarting the syscall when appropriate.
				 */
				total_processed = e;
				mutex_unlock(&t->lock);
				goto cleanup_unlock;
			}
		}

		if (unlikely(t->dm_destroyed)) {
			/*
			 * DM has destroyed this target, so just lock
			 * the user out.  There's really nothing else
			 * we can do here.  Note that we don't actually
			 * tear any thing down until userspace has
			 * closed the FD, as there may still be
			 * outstanding BIOs.
			 *
			 * This is kind of a wacky error code to
			 * return.  My goal was really just to try and
			 * find something that wasn't likely to be
			 * returned by anything else in the miscdev
			 * path.  The message "block device required"
			 * seems like a somewhat reasonable thing to
			 * say when the target has disappeared out from
			 * under us, but "not block" isn't sensible.
			 */
			c->to_user_error = total_processed = -ENOTBLK;
			mutex_unlock(&t->lock);
			goto cleanup_unlock;
		}

		/*
		 * Ensures that accesses to the message data are not ordered
		 * before the remote accesses that produce that message data.
		 *
		 * This pairs with the barrier in user_map(), via the
		 * conditional within the while loop above. Also see the lack
		 * of barrier in user_dtr(), which is why this can be after the
		 * destroyed check.
		 */
		smp_rmb();

		c->cur_to_user = msg_get_to_user(t);
		WARN_ON(c->cur_to_user == NULL);
		mutex_unlock(&t->lock);
	}

	processed = msg_copy_to_iov(c->cur_to_user, to);
	total_processed += processed;

	WARN_ON(c->cur_to_user->posn_to_user > c->cur_to_user->total_to_user);
	if (c->cur_to_user->posn_to_user == c->cur_to_user->total_to_user) {
		struct message *m = c->cur_to_user;

		c->cur_to_user = NULL;
		list_add_tail(&m->from_user, &c->from_user);
	}

cleanup_unlock:
	mutex_unlock(&c->lock);
	return total_processed;
}

static ssize_t dev_write(struct kiocb *iocb, struct iov_iter *from)
{
	struct channel *c = channel_from_file(iocb->ki_filp);
	ssize_t total_processed = 0;
	ssize_t processed;

	mutex_lock(&c->lock);

	if (unlikely(c->from_user_error)) {
		total_processed = c->from_user_error;
		goto cleanup_unlock;
	}

	/*
	 * cur_from_user can never be NULL.  If there's no real message it must
	 * point to the scratch space.
	 */
	WARN_ON(c->cur_from_user == NULL);
	if (c->cur_from_user->posn_from_user < sizeof(struct dm_user_message)) {
		struct message *msg, *old;

		processed = msg_copy_from_iov(c->cur_from_user, from);
		if (processed <= 0) {
			pr_warn("msg_copy_from_iov() returned %zu\n",
				processed);
			c->from_user_error = -EINVAL;
			goto cleanup_unlock;
		}
		total_processed += processed;

		/*
		 * In the unlikely event the user has provided us a very short
		 * write, not even big enough to fill a message, just succeed.
		 * We'll eventually build up enough bytes to do something.
		 */
		if (unlikely(c->cur_from_user->posn_from_user <
			     sizeof(struct dm_user_message)))
			goto cleanup_unlock;

		old = c->cur_from_user;
		mutex_lock(&c->target->lock);
		msg = msg_get_from_user(c, c->cur_from_user->msg.seq);
		if (msg == NULL) {
			pr_info("user provided an invalid messag seq of %llx\n",
				old->msg.seq);
			mutex_unlock(&c->target->lock);
			c->from_user_error = -EINVAL;
			goto cleanup_unlock;
		}
		mutex_unlock(&c->target->lock);

		WARN_ON(old->posn_from_user != sizeof(struct dm_user_message));
		msg->posn_from_user = sizeof(struct dm_user_message);
		msg->return_type = old->msg.type;
		msg->return_flags = old->msg.flags;
		WARN_ON(msg->posn_from_user > msg->total_from_user);
		c->cur_from_user = msg;
		WARN_ON(old != &c->scratch_message_from_user);
	}

	/*
	 * Userspace can signal an error for single requests by overwriting the
	 * seq field.
	 */
	switch (c->cur_from_user->return_type) {
	case DM_USER_RESP_SUCCESS:
		c->cur_from_user->bio->bi_status = BLK_STS_OK;
		break;
	case DM_USER_RESP_ERROR:
	case DM_USER_RESP_UNSUPPORTED:
	default:
		c->cur_from_user->bio->bi_status = BLK_STS_IOERR;
		goto finish_bio;
	}

	/*
	 * The op was a success as far as userspace is concerned, so process
	 * whatever data may come along with it.  The user may provide the BIO
	 * data in multiple chunks, in which case we don't need to finish the
	 * BIO.
	 */
	processed = msg_copy_from_iov(c->cur_from_user, from);
	total_processed += processed;

	if (c->cur_from_user->posn_from_user <
	    c->cur_from_user->total_from_user)
		goto cleanup_unlock;

finish_bio:
	/*
	 * When we set up this message the BIO's size matched the
	 * message size, if that's not still the case then something
	 * has gone off the rails.
	 */
	WARN_ON(bio_size(c->cur_from_user->bio) != 0);
	bio_endio(c->cur_from_user->bio);
	bio_put(c->cur_from_user->bio);

	/*
	 * We don't actually need to take the target lock here, as all
	 * we're doing is freeing the message and mempools have their
	 * own lock.  Each channel has its ows scratch message.
	 */
	WARN_ON(c->cur_from_user == &c->scratch_message_from_user);
	mempool_free(c->cur_from_user, &c->target->message_pool);
	c->scratch_message_from_user.posn_from_user = 0;
	c->cur_from_user = &c->scratch_message_from_user;

cleanup_unlock:
	mutex_unlock(&c->lock);
	return total_processed;
}

static int dev_release(struct inode *inode, struct file *file)
{
	struct channel *c;

	c = channel_from_file(file);
	mutex_lock(&c->lock);
	channel_free(c);

	return 0;
}

static const struct file_operations file_operations = {
	.owner = THIS_MODULE,
	.open = dev_open,
	.llseek = no_llseek,
	.read_iter = dev_read,
	.write_iter = dev_write,
	.release = dev_release,
};

static int user_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct target *t;
	int r;

	if (argc != 3) {
		ti->error = "Invalid argument count";
		r = -EINVAL;
		goto cleanup_none;
	}

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (t == NULL) {
		r = -ENOMEM;
		goto cleanup_none;
	}
	ti->private = t;

	/* Enable more BIO types. */
	ti->num_discard_bios = 1;
	ti->discards_supported = true;
	ti->num_flush_bios = 1;
	ti->flush_supported = true;

	/*
	 * We begin with a single reference to the target, which is miscdev's
	 * reference.  This ensures that the target won't be freed
	 * until after the miscdev has been unregistered and all extant
	 * channels have been closed.
	 */
	kref_init(&t->references);

	t->daemon_terminated = false;
	mutex_init(&t->lock);
	init_waitqueue_head(&t->wq);
	INIT_LIST_HEAD(&t->to_user);
	mempool_init_kmalloc_pool(&t->message_pool, MAX_OUTSTANDING_MESSAGES,
				  sizeof(struct message));

	t->miscdev.minor = MISC_DYNAMIC_MINOR;
	t->miscdev.fops = &file_operations;
	t->miscdev.name = kasprintf(GFP_KERNEL, "dm-user/%s", argv[2]);
	if (t->miscdev.name == NULL) {
		r = -ENOMEM;
		goto cleanup_message_pool;
	}

	/*
	 * Once the miscdev is registered it can be opened and therefor
	 * concurrent references to the channel can happen.  Holding the target
	 * lock during misc_register() could deadlock.  If registration
	 * succeeds then we will not access the target again so we just stick a
	 * barrier here, which pairs with taking the target lock everywhere
	 * else the target is accessed.
	 *
	 * I forgot where we ended up on the RCpc/RCsc locks.  IIU RCsc locks
	 * would mean that we could take the target lock earlier and release it
	 * here instead of the memory barrier.  I'm not sure that's any better,
	 * though, and this isn't on a hot path so it probably doesn't matter
	 * either way.
	 */
	smp_mb();

	r = misc_register(&t->miscdev);
	if (r) {
		DMERR("Unable to register miscdev %s for dm-user",
		      t->miscdev.name);
		r = -ENOMEM;
		goto cleanup_misc_name;
	}

	return 0;

cleanup_misc_name:
	kfree(t->miscdev.name);
cleanup_message_pool:
	mempool_exit(&t->message_pool);
	kfree(t);
cleanup_none:
	return r;
}

static void user_dtr(struct dm_target *ti)
{
	struct target *t = target_from_target(ti);

	/*
	 * Removes the miscdev.  This must be called without the target lock
	 * held to avoid a possible deadlock because our open implementation is
	 * called holding the miscdev lock and must later take the target lock.
	 *
	 * There is no race here because only DM can register/unregister the
	 * miscdev, and DM ensures that doesn't happen twice.  The internal
	 * miscdev lock is sufficient to ensure there are no races between
	 * deregistering the miscdev and open.
	 */
	misc_deregister(&t->miscdev);

	/*
	 * We are now free to take the target's lock and drop our reference to
	 * the target.  There are almost certainly tasks sleeping in read on at
	 * least one of the channels associated with this target, this
	 * explicitly wakes them up and terminates the read.
	 */
	mutex_lock(&t->lock);
	/*
	 * No barrier here, as wait/wake ensures that the flag visibility is
	 * correct WRT the wake/sleep state of the target tasks.
	 */
	t->dm_destroyed = true;
	wake_up_all(&t->wq);
	target_put(t);
}

/*
 * Consumes a BIO from device mapper, queueing it up for userspace.
 */
static int user_map(struct dm_target *ti, struct bio *bio)
{
	struct target *t;
	struct message *entry;

	t = target_from_target(ti);
	/*
	 * FIXME
	 *
	 * This seems like a bad idea.  Specifically, here we're
	 * directly on the IO path when we take the target lock, which may also
	 * be taken from a user context.  The user context doesn't actively
	 * trigger anything that may sleep while holding the lock, but this
	 * still seems like a bad idea.
	 *
	 * The obvious way to fix this would be to use a proper queue, which
	 * would result in no shared locks between the direct IO path and user
	 * tasks.  I had a version that did this, but the head-of-line blocking
	 * from the circular buffer resulted in us needing a fairly large
	 * allocation in order to avoid situations in which the queue fills up
	 * and everything goes off the rails.
	 *
	 * I could jump through a some hoops to avoid a shared lock while still
	 * allowing for a large queue, but I'm not actually sure that allowing
	 * for very large queues is the right thing to do here.  Intuitively it
	 * seems better to keep the queues small in here (essentially sized to
	 * the user latency for performance reasons only) and rely on returning
	 * DM_MAPIO_REQUEUE regularly, as that would give the rest of the
	 * kernel more information.
	 *
	 * I'll spend some time trying to figure out what's going on with
	 * DM_MAPIO_REQUEUE, but if someone has a better idea of how to fix
	 * this I'm all ears.
	 */
	mutex_lock(&t->lock);

	/*
	 * FIXME
	 *
	 * The assumption here is that there's no benefit to returning
	 * DM_MAPIO_KILL as opposed to just erroring out the BIO, but I'm not
	 * sure that's actually true -- for example, I could imagine users
	 * expecting that submitted BIOs are unlikely to fail and therefor
	 * relying on submission failure to indicate an unsupported type.
	 *
	 * There's two ways I can think of to fix this:
	 *   - Add DM arguments that are parsed during the constructor that
	 *     allow various dm_target flags to be set that indicate the op
	 *     types supported by this target.  This may make sense for things
	 *     like discard, where DM can already transform the BIOs to a form
	 *     that's likely to be supported.
	 *   - Some sort of pre-filter that allows userspace to hook in here
	 *     and kill BIOs before marking them as submitted.  My guess would
	 *     be that a userspace round trip is a bad idea here, but a BPF
	 *     call seems resonable.
	 *
	 * My guess is that we'd likely want to do both.  The first one is easy
	 * and gives DM the proper info, so it seems better.  The BPF call
	 * seems overly complex for just this, but one could imagine wanting to
	 * sometimes return _MAPPED and a BPF filter would be the way to do
	 * that.
	 *
	 * For example, in Android we have an in-kernel DM device called
	 * "dm-bow" that takes advange of some portion of the space that has
	 * been discarded on a device to provide opportunistic block-level
	 * backups.  While one could imagine just implementing this entirely in
	 * userspace, that would come with an appreciable performance penalty.
	 * Instead one could keep a BPF program that forwards most accesses
	 * directly to the backing block device while informing a userspace
	 * daemon of any discarded space and on writes to blocks that are to be
	 * backed up.
	 */
	if (unlikely((bio_type_to_user_type(bio) < 0) ||
		     (bio_flags_to_user_flags(bio) < 0))) {
		mutex_unlock(&t->lock);
		return DM_MAPIO_KILL;
	}

	entry = msg_get_map(t);
	if (unlikely(entry == NULL)) {
		mutex_unlock(&t->lock);
		return DM_MAPIO_REQUEUE;
	}

	bio_get(bio);
	entry->msg.type = bio_type_to_user_type(bio);
	entry->msg.flags = bio_flags_to_user_flags(bio);
	entry->msg.sector = bio->bi_iter.bi_sector;
	entry->msg.len = bio_size(bio);
	entry->bio = bio;
	entry->posn_to_user = 0;
	entry->total_to_user = bio_bytes_needed_to_user(bio);
	entry->posn_from_user = 0;
	entry->total_from_user = bio_bytes_needed_from_user(bio);
	entry->delayed = false;
	entry->t = t;
	/* Pairs with the barrier in dev_read() */
	smp_wmb();
	list_add_tail(&entry->to_user, &t->to_user);

	/*
	 * If there is no daemon to process the IO's,
	 * queue these messages into a workqueue with
	 * a timeout.
	 */
	if (!is_user_space_thread_present(t))
		enqueue_delayed_work(entry, !t->daemon_terminated);

	wake_up_interruptible(&t->wq);
	mutex_unlock(&t->lock);
	return DM_MAPIO_SUBMITTED;
}

static struct target_type user_target = {
	.name = "user",
	.version = { 1, 0, 0 },
	.module = THIS_MODULE,
	.ctr = user_ctr,
	.dtr = user_dtr,
	.map = user_map,
};

static int __init dm_user_init(void)
{
	int r;

	r = dm_register_target(&user_target);
	if (r) {
		DMERR("register failed %d", r);
		goto error;
	}

	return 0;

error:
	return r;
}

static void __exit dm_user_exit(void)
{
	dm_unregister_target(&user_target);
}

module_init(dm_user_init);
module_exit(dm_user_exit);
MODULE_AUTHOR("Palmer Dabbelt <palmerdabbelt@google.com>");
MODULE_DESCRIPTION(DM_NAME " target returning blocks from userspace");
MODULE_LICENSE("GPL");
