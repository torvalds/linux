// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/*
 * mali_kbase_kinstr_jm.c
 * Kernel driver public interface to job manager atom tracing
 */

#include "mali_kbase_kinstr_jm.h"
#include <uapi/gpu/arm/bifrost/mali_kbase_kinstr_jm_reader.h>

#include "mali_kbase.h"
#include "mali_kbase_linux.h"

#include <backend/gpu/mali_kbase_jm_rb.h>

#include <asm/barrier.h>
#include <linux/anon_inodes.h>
#include <linux/circ_buf.h>
#include <linux/fs.h>
#include <linux/kref.h>
#include <linux/ktime.h>
#include <linux/log2.h>
#include <linux/mutex.h>
#include <linux/rculist_bl.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include <linux/wait.h>

/* Define static_assert().
 *
 * The macro was introduced in kernel 5.1. But older vendor kernels may define
 * it too.
 */
#if KERNEL_VERSION(5, 1, 0) <= LINUX_VERSION_CODE
#include <linux/build_bug.h>
#elif !defined(static_assert)
// Stringify the expression if no message is given.
#define static_assert(e, ...)  __static_assert(e, #__VA_ARGS__, #e)
#define __static_assert(e, msg, ...) _Static_assert(e, msg)
#endif

#if KERNEL_VERSION(4, 16, 0) >= LINUX_VERSION_CODE
typedef unsigned int __poll_t;
#endif

#ifndef ENOTSUP
#define ENOTSUP EOPNOTSUPP
#endif

/* The module printing prefix */
#define PR_ "mali_kbase_kinstr_jm: "

/* Allows us to perform ASM goto for the tracing
 * https://www.kernel.org/doc/Documentation/static-keys.txt
 */
DEFINE_STATIC_KEY_FALSE(basep_kinstr_jm_reader_static_key);

#define KBASE_KINSTR_JM_VERSION 2

/**
 * struct kbase_kinstr_jm - The context for the kernel job manager atom tracing
 * @readers: a bitlocked list of opened readers. Readers are attached to the
 *           private data of a file descriptor that the user opens with the
 *           KBASE_IOCTL_KINSTR_JM_FD IO control call.
 * @refcount: reference count for the context. Any reader will have a link
 *            back to the context so that they can remove themselves from the
 *            list.
 *
 * This is opaque outside this compilation unit
 */
struct kbase_kinstr_jm {
	struct hlist_bl_head readers;
	struct kref refcount;
};

/**
 * struct kbase_kinstr_jm_atom_state_change - Represents an atom changing to a
 *                                            new state
 * @timestamp: Raw monotonic nanoseconds of the state change
 * @state:     The state that the atom has moved to
 * @atom:      The atom number that has changed state
 * @flags:     Flags associated with the state change. See
 *             KBASE_KINSTR_JM_ATOM_STATE_FLAG_* defines.
 * @reserved:  Reserved for future use.
 * @data:      Extra data for the state change. Active member depends on state.
 * @data.start:      Extra data for the state change. Active member depends on
 *                   state.
 * @data.start.slot: Extra data for the state change. Active member depends on
 *                   state.
 * @data.padding:    Padding
 *
 * We can add new fields to the structure and old user code will gracefully
 * ignore the new fields.
 *
 * We can change the size of the structure and old user code will gracefully
 * skip over the new size via `struct kbase_kinstr_jm_fd_out->size`.
 *
 * If we remove fields, the version field in `struct
 * kbase_kinstr_jm_fd_out->version` will be incremented and old user code will
 * gracefully fail and tell the user that the kernel API is too new and has
 * backwards-incompatible changes. Note that one userspace can opt to handle
 * multiple kernel major versions of the structure.
 *
 * If we need to change the _meaning_ of one of the fields, i.e. the state
 * machine has had a incompatible change, we can keep the same members in the
 * structure and update the version as above. User code will no longer
 * recognise that it has the supported field and can gracefully explain to the
 * user that the kernel API is no longer supported.
 *
 * When making changes to this structure, make sure they are either:
 *  - additions to the end (for minor version bumps (i.e. only a size increase))
 *  such that the layout of existing fields doesn't change, or;
 *  - update the version reported to userspace so that it can fail explicitly.
 */
struct kbase_kinstr_jm_atom_state_change {
	u64 timestamp;
	s8 state; /* enum kbase_kinstr_jm_reader_atom_state */
	u8 atom;
	u8 flags;
	u8 reserved[1];
	/* Tagged union based on state. Ensure members are aligned correctly! */
	union {
		struct {
			u8 slot;
		} start;
		u8 padding[4];
	} data;
};
static_assert(
	((1 << 8 * sizeof(((struct kbase_kinstr_jm_atom_state_change *)0)->state)) - 1) >=
	KBASE_KINSTR_JM_READER_ATOM_STATE_COUNT);

#define KBASE_KINSTR_JM_ATOM_STATE_FLAG_OVERFLOW BIT(0)

/**
 * struct reader_changes - The circular buffer of kernel atom state changes
 * @data:      The allocated buffer. This is allocated when the user requests
 *             the reader file descriptor. It is released when the user calls
 *             close() on the fd. When accessing this, lock the producer spin
 *             lock to prevent races on the allocated memory. The consume lock
 *             does not need to be held because newly-inserted data will always
 *             be outside the currenly-read range.
 * @producer:  The producing spinlock which allows us to push changes into the
 *             buffer at the same time as a user read occurring. This needs to
 *             be locked when saving/restoring the IRQ because we can receive an
 *             interrupt from the GPU when an atom completes. The CPU could have
 *             a task preempted that is holding this lock.
 * @consumer:  The consuming mutex which locks around the user read().
 *             Must be held when updating the tail of the circular buffer.
 * @head:      The head of the circular buffer. Can be used with Linux @c CIRC_
 *             helpers. The producer should lock and update this with an SMP
 *             store when a new change lands. The consumer can read with an
 *             SMP load. This allows the producer to safely insert new changes
 *             into the circular buffer.
 * @tail:      The tail of the circular buffer. Can be used with Linux @c CIRC_
 *             helpers. The producer should do a READ_ONCE load and the consumer
 *             should SMP store.
 * @size:      The number of changes that are allowed in @c data. Can be used
 *             with Linux @c CIRC_ helpers. Will always be a power of two. The
 *             producer lock should be held when updating this and stored with
 *             an SMP release memory barrier. This means that the consumer can
 *             do an SMP load.
 * @threshold: The number of changes above which threads polling on the reader
 *             file descriptor will be woken up.
 */
struct reader_changes {
	struct kbase_kinstr_jm_atom_state_change *data;
	spinlock_t producer;
	struct mutex consumer;
	u32 head;
	u32 tail;
	u32 size;
	u32 threshold;
};

/**
 * reader_changes_is_valid_size() - Determines if requested changes buffer size
 *                                  is valid.
 * @size: The requested memory size
 *
 * We have a constraint that the underlying physical buffer must be a
 * power of two so that we can use the efficient circular buffer helpers that
 * the kernel provides. It also needs to be representable within a u32.
 *
 * Return:
 * * true  - the size is valid
 * * false - the size is invalid
 */
static inline bool reader_changes_is_valid_size(const size_t size)
{
	const size_t elem_size = sizeof(*((struct reader_changes *)0)->data);
	const size_t size_size = sizeof(((struct reader_changes *)0)->size);
	const size_t size_max = (1ull << (size_size * 8)) - 1;

	return is_power_of_2(size) && /* Is a power of two */
	       ((size / elem_size) <= size_max); /* Small enough */
}

/**
 * reader_changes_init() - Initializes the reader changes and allocates the
 *                         changes buffer
 * @changes: The context pointer, must point to a zero-inited allocated reader
 *           changes structure. We may support allocating the structure in the
 *           future.
 * @size: The requested changes buffer size
 *
 * Return:
 * (0, U16_MAX] - the number of data elements allocated
 * -EINVAL - a pointer was invalid
 * -ENOTSUP - we do not support allocation of the context
 * -ERANGE - the requested memory size was invalid
 * -ENOMEM - could not allocate the memory
 * -EADDRINUSE - the buffer memory was already allocated
 */
static int reader_changes_init(struct reader_changes *const changes,
			       const size_t size)
{
	BUILD_BUG_ON((PAGE_SIZE % sizeof(*changes->data)) != 0);

	if (!reader_changes_is_valid_size(size)) {
		pr_warn(PR_ "invalid size %zu\n", size);
		return -ERANGE;
	}

	changes->data = vmalloc(size);
	if (!changes->data)
		return -ENOMEM;

	spin_lock_init(&changes->producer);
	mutex_init(&changes->consumer);

	changes->size = size / sizeof(*changes->data);
	changes->threshold = min(((size_t)(changes->size)) / 4,
			     ((size_t)(PAGE_SIZE)) / sizeof(*changes->data));

	return changes->size;
}

/**
 * reader_changes_term() - Cleans up a reader changes structure
 * @changes: The context to clean up
 *
 * Releases the allocated state changes memory
 */
static void reader_changes_term(struct reader_changes *const changes)
{
	struct kbase_kinstr_jm_atom_state_change *data = NULL;
	unsigned long irq;

	/*
	 * Although changes->data is used on the consumer side, too, no active
	 * consumer is possible by the time we clean up the reader changes, so
	 * no need to take the consumer lock. However, we do need the producer
	 * lock because the list removal can race with list traversal.
	 */
	spin_lock_irqsave(&changes->producer, irq);
	swap(changes->data, data);
	spin_unlock_irqrestore(&changes->producer, irq);

	mutex_destroy(&changes->consumer);
	vfree(data);
}

/**
 * reader_changes_count_locked() - Retrieves the count of state changes from the
 * tail to the physical end of the buffer
 * @changes: The state changes context
 *
 * The consumer mutex must be held. Uses the CIRC_CNT_TO_END macro to
 * determine the count, so there may be more items. However, that's the maximum
 * number that can be read in one contiguous read.
 *
 * Return: the number of changes in the circular buffer until the end of the
 * allocation
 */
static u32 reader_changes_count_locked(struct reader_changes *const changes)
{
	u32 head;

	lockdep_assert_held_once(&changes->consumer);

	head = smp_load_acquire(&changes->head);

	return CIRC_CNT_TO_END(head, changes->tail, changes->size);
}

/**
 * reader_changes_count() - Retrieves the count of state changes from the
 * tail to the physical end of the buffer
 * @changes: The state changes context
 *
 * Return: the number of changes in the circular buffer until the end of the
 * allocation
 */
static u32 reader_changes_count(struct reader_changes *const changes)
{
	u32 ret;

	mutex_lock(&changes->consumer);
	ret = reader_changes_count_locked(changes);
	mutex_unlock(&changes->consumer);
	return ret;
}

/**
 * reader_changes_push() - Pushes a change into the reader circular buffer.
 * @changes:    The buffer to insert the change into
 * @change:     Kernel atom change to insert
 * @wait_queue: The queue to be kicked when changes should be read from
 *              userspace. Kicked when a threshold is reached or there is
 *              overflow.
 */
static void reader_changes_push(
	struct reader_changes *const changes,
	const struct kbase_kinstr_jm_atom_state_change *const change,
	wait_queue_head_t *const wait_queue)
{
	u32 head, tail, size, space;
	unsigned long irq;
	struct kbase_kinstr_jm_atom_state_change *data;

	spin_lock_irqsave(&changes->producer, irq);

	/* We may be called for a reader_changes that's awaiting cleanup. */
	data = changes->data;
	if (!data)
		goto unlock;

	size = changes->size;
	head = changes->head;
	tail = smp_load_acquire(&changes->tail);

	space = CIRC_SPACE(head, tail, size);
	if (space >= 1) {
		data[head] = *change;
		if (space == 1) {
			data[head].flags |=
				KBASE_KINSTR_JM_ATOM_STATE_FLAG_OVERFLOW;
			pr_warn(PR_ "overflow of circular buffer\n");
		}
		smp_store_release(&changes->head, (head + 1) & (size - 1));
	}

	/* Wake for either overflow or over-threshold cases. */
	if (CIRC_CNT(head + 1, tail, size) >= changes->threshold)
		wake_up_interruptible(wait_queue);

unlock:
	spin_unlock_irqrestore(&changes->producer, irq);
}

/**
 * struct reader - Allows the kernel state changes to be read by user space.
 * @node: The node in the @c readers locked list
 * @rcu_head: storage for the RCU callback to free this reader (see kfree_rcu)
 * @changes: The circular buffer of user changes
 * @wait_queue: A wait queue for poll
 * @context: a pointer to the parent context that created this reader. Can be
 *           used to remove the reader from the list of readers. Reference
 *           counted.
 *
 * The reader is a circular buffer in kernel space. State changes are pushed
 * into the buffer. The flow from user space is:
 *
 *   * Request file descriptor with KBASE_IOCTL_KINSTR_JM_FD. This will
 *     allocate the kernel side circular buffer with a size specified in the
 *     ioctl argument.
 *   * The user will then poll the file descriptor for data
 *   * Upon receiving POLLIN, perform a read() on the file descriptor to get
 *     the data out.
 *   * The buffer memory will be freed when the file descriptor is closed
 */
struct reader {
	struct hlist_bl_node node;
	struct rcu_head rcu_head;
	struct reader_changes changes;
	wait_queue_head_t wait_queue;
	struct kbase_kinstr_jm *context;
};

static struct kbase_kinstr_jm *
kbase_kinstr_jm_ref_get(struct kbase_kinstr_jm *const ctx);
static void kbase_kinstr_jm_ref_put(struct kbase_kinstr_jm *const ctx);
static int kbase_kinstr_jm_readers_add(struct kbase_kinstr_jm *const ctx,
					struct reader *const reader);
static void kbase_kinstr_jm_readers_del(struct kbase_kinstr_jm *const ctx,
					struct reader *const reader);

/**
 * reader_term() - Terminate a instrumentation job manager reader context.
 * @reader: Pointer to context to be terminated.
 */
static void reader_term(struct reader *const reader)
{
	if (!reader)
		return;

	kbase_kinstr_jm_readers_del(reader->context, reader);
	reader_changes_term(&reader->changes);
	kbase_kinstr_jm_ref_put(reader->context);

	kfree_rcu(reader, rcu_head);
}

/**
 * reader_init() - Initialise a instrumentation job manager reader context.
 * @out_reader:  Non-NULL pointer to where the pointer to the created context
 *               will be stored on success.
 * @ctx:         the pointer to the parent context. Reference count will be
 *               increased if initialization is successful
 * @num_changes: The number of changes to allocate a buffer for
 *
 * Return: 0 on success, else error code.
 */
static int reader_init(struct reader **const out_reader,
		       struct kbase_kinstr_jm *const ctx,
		       size_t const num_changes)
{
	struct reader *reader = NULL;
	const size_t change_size = sizeof(struct kbase_kinstr_jm_atom_state_change);
	int status;

	if (!out_reader || !ctx || !num_changes)
		return -EINVAL;

	reader = kzalloc(sizeof(*reader), GFP_KERNEL);
	if (!reader)
		return -ENOMEM;

	INIT_HLIST_BL_NODE(&reader->node);
	init_waitqueue_head(&reader->wait_queue);

	reader->context = kbase_kinstr_jm_ref_get(ctx);

	status = reader_changes_init(&reader->changes, num_changes * change_size);
	if (status < 0)
		goto fail;

	status = kbase_kinstr_jm_readers_add(ctx, reader);
	if (status < 0)
		goto fail;

	*out_reader = reader;

	return 0;

fail:
	kbase_kinstr_jm_ref_put(reader->context);
	kfree(reader);
	return status;
}

/**
 * reader_release() - Invoked when the reader file descriptor is released
 * @node: The inode that the file descriptor that the file corresponds to. In
 *        our case our reader file descriptor is backed by an anonymous node so
 *        not much is in this.
 * @file: the file data. Our reader context is held in the private data
 * Return: zero on success
 */
static int reader_release(struct inode *const node, struct file *const file)
{
	struct reader *const reader = file->private_data;

	reader_term(reader);
	file->private_data = NULL;

	return 0;
}

/**
 * reader_changes_copy_to_user() - Copy any changes from a changes structure to
 * the user-provided buffer.
 * @changes: The changes structure from which to copy.
 * @buffer: The user buffer to copy the data to.
 * @buffer_size: The number of bytes in the buffer.
 * Return: The number of bytes copied or negative errno on failure.
 */
static ssize_t reader_changes_copy_to_user(struct reader_changes *const changes,
					   char __user *buffer,
					   size_t buffer_size)
{
	ssize_t ret = 0;
	struct kbase_kinstr_jm_atom_state_change const *src_buf = READ_ONCE(
		changes->data);
	size_t const entry_size = sizeof(*src_buf);
	size_t changes_tail, changes_count, read_size;

	/* Needed for the quick buffer capacity calculation below.
	 * Note that we can't use is_power_of_2() since old compilers don't
	 * understand it's a constant expression.
	 */
#define is_power_of_two(x) ((x) && !((x) & ((x) - 1)))
	static_assert(is_power_of_two(
			sizeof(struct kbase_kinstr_jm_atom_state_change)));
#undef is_power_of_two

	lockdep_assert_held_once(&changes->consumer);

	/* Read continuously until either:
	 * - we've filled the output buffer, or
	 * - there are no changes when we check.
	 *
	 * If more changes arrive while we're copying to the user, we can copy
	 * those as well, space permitting.
	 */
	do {
		changes_tail = changes->tail;
		changes_count = reader_changes_count_locked(changes);
		read_size = min(changes_count * entry_size,
				buffer_size & ~(entry_size - 1));

		if (!read_size)
			break;

		if (copy_to_user(buffer, &(src_buf[changes_tail]), read_size))
			return -EFAULT;

		buffer += read_size;
		buffer_size -= read_size;
		ret += read_size;
		changes_tail = (changes_tail + read_size / entry_size) &
			(changes->size - 1);
		smp_store_release(&changes->tail, changes_tail);
	} while (read_size);

	return ret;
}

/**
 * reader_read() - Handles a read call on the reader file descriptor
 *
 * @filp: The file that the read was performed on
 * @buffer: The destination buffer
 * @buffer_size: The maximum number of bytes to read
 * @offset: The offset into the 'file' to read from.
 *
 * Note the destination buffer needs to be fully mapped in userspace or the read
 * will fault.
 *
 * Return:
 * * The number of bytes read or:
 * * -EBADF - the file descriptor did not have an attached reader
 * * -EFAULT - memory access fault
 * * -EAGAIN - if the file is set to nonblocking reads with O_NONBLOCK and there
 *             is no data available
 *
 * Note: The number of bytes read will always be a multiple of the size of an
 * entry.
 */
static ssize_t reader_read(struct file *const filp,
			   char __user *const buffer,
			   size_t const buffer_size,
			   loff_t *const offset)
{
	struct reader *const reader = filp->private_data;
	struct reader_changes *changes;
	ssize_t ret;

	if (!reader)
		return -EBADF;

	if (buffer_size < sizeof(struct kbase_kinstr_jm_atom_state_change))
		return -ENOBUFS;

#if KERNEL_VERSION(5, 0, 0) <= LINUX_VERSION_CODE
	if (!access_ok(buffer, buffer_size))
		return -EIO;
#else
	if (!access_ok(VERIFY_WRITE, buffer, buffer_size))
		return -EIO;
#endif

	changes = &reader->changes;

	mutex_lock(&changes->consumer);
	if (!reader_changes_count_locked(changes)) {
		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			goto exit;
		}

		if (wait_event_interruptible(
				reader->wait_queue,
				!!reader_changes_count_locked(changes))) {
			ret = -EINTR;
			goto exit;
		}
	}

	ret = reader_changes_copy_to_user(changes, buffer, buffer_size);

exit:
	mutex_unlock(&changes->consumer);
	return ret;
}

/**
 * reader_poll() - Handles a poll call on the reader file descriptor
 * @file: The file that the poll was performed on
 * @wait: The poll table
 *
 * The results of the poll will be unreliable if there is no mapped memory as
 * there is no circular buffer to push atom state changes into.
 *
 * Return:
 * * 0 - no data ready
 * * POLLIN - state changes have been buffered
 * * -EBADF - the file descriptor did not have an attached reader
 * * -EINVAL - the IO control arguments were invalid
 */
static __poll_t reader_poll(struct file *const file,
			    struct poll_table_struct *const wait)
{
	struct reader *reader;
	struct reader_changes *changes;

	if (unlikely(!file || !wait))
		return -EINVAL;

	reader = file->private_data;
	if (unlikely(!reader))
		return -EBADF;

	changes = &reader->changes;

	if (reader_changes_count(changes) >= changes->threshold)
		return POLLIN;

	poll_wait(file, &reader->wait_queue, wait);

	return (reader_changes_count(changes) > 0) ? POLLIN : 0;
}

/* The file operations virtual function table */
static const struct file_operations file_operations = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.read = reader_read,
	.poll = reader_poll,
	.release = reader_release
};

/* The maximum amount of readers that can be created on a context. */
static const size_t kbase_kinstr_jm_readers_max = 16;

/**
 * kbase_kinstr_jm_release() - Invoked when the reference count is dropped
 * @ref: the context reference count
 */
static void kbase_kinstr_jm_release(struct kref *const ref)
{
	struct kbase_kinstr_jm *const ctx =
		container_of(ref, struct kbase_kinstr_jm, refcount);

	kfree(ctx);
}

/**
 * kbase_kinstr_jm_ref_get() - Reference counts the instrumentation context
 * @ctx: the context to reference count
 * Return: the reference counted context
 */
static struct kbase_kinstr_jm *
kbase_kinstr_jm_ref_get(struct kbase_kinstr_jm *const ctx)
{
	if (likely(ctx))
		kref_get(&ctx->refcount);
	return ctx;
}

/**
 * kbase_kinstr_jm_ref_put() - Dereferences the instrumentation context
 * @ctx: the context to lower the reference count on
 */
static void kbase_kinstr_jm_ref_put(struct kbase_kinstr_jm *const ctx)
{
	if (likely(ctx))
		kref_put(&ctx->refcount, kbase_kinstr_jm_release);
}

/**
 * kbase_kinstr_jm_readers_add() - Adds a reader to the list of readers
 * @ctx: the instrumentation context
 * @reader: the reader to add
 *
 * Return:
 * 0 - success
 * -ENOMEM - too many readers already added.
 */
static int kbase_kinstr_jm_readers_add(struct kbase_kinstr_jm *const ctx,
					struct reader *const reader)
{
	struct hlist_bl_head *const readers = &ctx->readers;
	struct hlist_bl_node *node;
	struct reader *temp;
	size_t count = 0;

	hlist_bl_lock(readers);

	hlist_bl_for_each_entry_rcu(temp, node, readers, node)
		++count;

	if (kbase_kinstr_jm_readers_max < count) {
		hlist_bl_unlock(readers);
		return -ENOMEM;
	}

	hlist_bl_add_head_rcu(&reader->node, readers);

	hlist_bl_unlock(readers);

	static_branch_inc(&basep_kinstr_jm_reader_static_key);

	return 0;
}

/**
 * kbase_kinstr_jm_readers_del() - Deletes a reader from the list of readers
 * @ctx: the instrumentation context
 * @reader: the reader to delete
 */
static void kbase_kinstr_jm_readers_del(struct kbase_kinstr_jm *const ctx,
					struct reader *const reader)
{
	struct hlist_bl_head *const readers = &ctx->readers;

	hlist_bl_lock(readers);
	hlist_bl_del_rcu(&reader->node);
	hlist_bl_unlock(readers);

	static_branch_dec(&basep_kinstr_jm_reader_static_key);
}

int kbase_kinstr_jm_get_fd(struct kbase_kinstr_jm *const ctx,
			   union kbase_kinstr_jm_fd *jm_fd_arg)
{
	struct kbase_kinstr_jm_fd_in const *in;
	struct reader *reader;
	size_t const change_size = sizeof(struct
					  kbase_kinstr_jm_atom_state_change);
	int status;
	int fd;
	int i;

	if (!ctx || !jm_fd_arg)
		return -EINVAL;

	in = &jm_fd_arg->in;

	if (!is_power_of_2(in->count))
		return -EINVAL;

	for (i = 0; i < sizeof(in->padding); ++i)
		if (in->padding[i])
			return -EINVAL;

	status = reader_init(&reader, ctx, in->count);
	if (status < 0)
		return status;

	jm_fd_arg->out.version = KBASE_KINSTR_JM_VERSION;
	jm_fd_arg->out.size = change_size;
	memset(&jm_fd_arg->out.padding, 0, sizeof(jm_fd_arg->out.padding));

	fd = anon_inode_getfd("[mali_kinstr_jm]", &file_operations, reader,
			      O_CLOEXEC);
	if (fd < 0)
		reader_term(reader);

	return fd;
}

int kbase_kinstr_jm_init(struct kbase_kinstr_jm **const out_ctx)
{
	struct kbase_kinstr_jm *ctx = NULL;

	if (!out_ctx)
		return -EINVAL;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	INIT_HLIST_BL_HEAD(&ctx->readers);
	kref_init(&ctx->refcount);

	*out_ctx = ctx;

	return 0;
}

void kbase_kinstr_jm_term(struct kbase_kinstr_jm *const ctx)
{
	kbase_kinstr_jm_ref_put(ctx);
}

void kbasep_kinstr_jm_atom_state(
	struct kbase_jd_atom *const katom,
	const enum kbase_kinstr_jm_reader_atom_state state)
{
	struct kbase_context *const kctx = katom->kctx;
	struct kbase_kinstr_jm *const ctx = kctx->kinstr_jm;
	const u8 id = kbase_jd_atom_id(kctx, katom);
	struct kbase_kinstr_jm_atom_state_change change = {
		.timestamp = ktime_get_raw_ns(), .atom = id, .state = state
	};
	struct reader *reader;
	struct hlist_bl_node *node;

	WARN(KBASE_KINSTR_JM_READER_ATOM_STATE_COUNT < state || 0 > state,
	     PR_ "unsupported katom (%u) state (%i)", id, state);

	switch (state) {
	case KBASE_KINSTR_JM_READER_ATOM_STATE_START:
		change.data.start.slot = katom->slot_nr;
		break;
	default:
		break;
	}

	rcu_read_lock();
	hlist_bl_for_each_entry_rcu(reader, node, &ctx->readers, node)
		reader_changes_push(
			&reader->changes, &change, &reader->wait_queue);
	rcu_read_unlock();
}

KBASE_EXPORT_TEST_API(kbasep_kinstr_jm_atom_state);

void kbasep_kinstr_jm_atom_hw_submit(struct kbase_jd_atom *const katom)
{
	struct kbase_context *const kctx = katom->kctx;
	struct kbase_device *const kbdev = kctx->kbdev;
	const int slot = katom->slot_nr;
	struct kbase_jd_atom *const submitted = kbase_gpu_inspect(kbdev, slot, 0);

	BUILD_BUG_ON(SLOT_RB_SIZE != 2);

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (WARN_ON(slot < 0 || slot >= GPU_MAX_JOB_SLOTS))
		return;
	if (WARN_ON(!submitted))
		return;

	if (submitted == katom)
		kbase_kinstr_jm_atom_state_start(katom);
}

void kbasep_kinstr_jm_atom_hw_release(struct kbase_jd_atom *const katom)
{
	struct kbase_context *const kctx = katom->kctx;
	struct kbase_device *const kbdev = kctx->kbdev;
	const int slot = katom->slot_nr;
	struct kbase_jd_atom *const submitted = kbase_gpu_inspect(kbdev, slot, 0);
	struct kbase_jd_atom *const queued = kbase_gpu_inspect(kbdev, slot, 1);

	BUILD_BUG_ON(SLOT_RB_SIZE != 2);

	lockdep_assert_held(&kbdev->hwaccess_lock);

	if (WARN_ON(slot < 0 || slot >= GPU_MAX_JOB_SLOTS))
		return;
	if (WARN_ON(!submitted))
		return;
	if (WARN_ON((submitted != katom) && (queued != katom)))
		return;

	if (queued == katom)
		return;

	if (katom->gpu_rb_state == KBASE_ATOM_GPU_RB_SUBMITTED)
		kbase_kinstr_jm_atom_state_stop(katom);
	if (queued && queued->gpu_rb_state == KBASE_ATOM_GPU_RB_SUBMITTED)
		kbase_kinstr_jm_atom_state_start(queued);
}
