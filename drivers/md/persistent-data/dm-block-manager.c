/*
 * Copyright (C) 2011 Red Hat, Inc.
 *
 * This file is released under the GPL.
 */
#include "dm-block-manager.h"
#include "dm-persistent-data-internal.h"

#include <linux/dm-bufio.h>
#include <linux/crc32c.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <linux/device-mapper.h>
#include <linux/stacktrace.h>
#include <linux/sched/task.h>

#define DM_MSG_PREFIX "block manager"

/*----------------------------------------------------------------*/

#ifdef CONFIG_DM_DEBUG_BLOCK_MANAGER_LOCKING

/*
 * This is a read/write semaphore with a couple of differences.
 *
 * i) There is a restriction on the number of concurrent read locks that
 * may be held at once.  This is just an implementation detail.
 *
 * ii) Recursive locking attempts are detected and return EINVAL.  A stack
 * trace is also emitted for the previous lock acquisition.
 *
 * iii) Priority is given to write locks.
 */
#define MAX_HOLDERS 4
#define MAX_STACK 10

struct stack_store {
	unsigned int	nr_entries;
	unsigned long	entries[MAX_STACK];
};

struct block_lock {
	spinlock_t lock;
	__s32 count;
	struct list_head waiters;
	struct task_struct *holders[MAX_HOLDERS];

#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
	struct stack_store traces[MAX_HOLDERS];
#endif
};

struct waiter {
	struct list_head list;
	struct task_struct *task;
	int wants_write;
};

static unsigned __find_holder(struct block_lock *lock,
			      struct task_struct *task)
{
	unsigned i;

	for (i = 0; i < MAX_HOLDERS; i++)
		if (lock->holders[i] == task)
			break;

	BUG_ON(i == MAX_HOLDERS);
	return i;
}

/* call this *after* you increment lock->count */
static void __add_holder(struct block_lock *lock, struct task_struct *task)
{
	unsigned h = __find_holder(lock, NULL);
#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
	struct stack_store *t;
#endif

	get_task_struct(task);
	lock->holders[h] = task;

#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
	t = lock->traces + h;
	t->nr_entries = stack_trace_save(t->entries, MAX_STACK, 2);
#endif
}

/* call this *before* you decrement lock->count */
static void __del_holder(struct block_lock *lock, struct task_struct *task)
{
	unsigned h = __find_holder(lock, task);
	lock->holders[h] = NULL;
	put_task_struct(task);
}

static int __check_holder(struct block_lock *lock)
{
	unsigned i;

	for (i = 0; i < MAX_HOLDERS; i++) {
		if (lock->holders[i] == current) {
			DMERR("recursive lock detected in metadata");
#ifdef CONFIG_DM_DEBUG_BLOCK_STACK_TRACING
			DMERR("previously held here:");
			stack_trace_print(lock->traces[i].entries,
					  lock->traces[i].nr_entries, 4);

			DMERR("subsequent acquisition attempted here:");
			dump_stack();
#endif
			return -EINVAL;
		}
	}

	return 0;
}

static void __wait(struct waiter *w)
{
	for (;;) {
		set_current_state(TASK_UNINTERRUPTIBLE);

		if (!w->task)
			break;

		schedule();
	}

	set_current_state(TASK_RUNNING);
}

static void __wake_waiter(struct waiter *w)
{
	struct task_struct *task;

	list_del(&w->list);
	task = w->task;
	smp_mb();
	w->task = NULL;
	wake_up_process(task);
}

/*
 * We either wake a few readers or a single writer.
 */
static void __wake_many(struct block_lock *lock)
{
	struct waiter *w, *tmp;

	BUG_ON(lock->count < 0);
	list_for_each_entry_safe(w, tmp, &lock->waiters, list) {
		if (lock->count >= MAX_HOLDERS)
			return;

		if (w->wants_write) {
			if (lock->count > 0)
				return; /* still read locked */

			lock->count = -1;
			__add_holder(lock, w->task);
			__wake_waiter(w);
			return;
		}

		lock->count++;
		__add_holder(lock, w->task);
		__wake_waiter(w);
	}
}

static void bl_init(struct block_lock *lock)
{
	int i;

	spin_lock_init(&lock->lock);
	lock->count = 0;
	INIT_LIST_HEAD(&lock->waiters);
	for (i = 0; i < MAX_HOLDERS; i++)
		lock->holders[i] = NULL;
}

static int __available_for_read(struct block_lock *lock)
{
	return lock->count >= 0 &&
		lock->count < MAX_HOLDERS &&
		list_empty(&lock->waiters);
}

static int bl_down_read(struct block_lock *lock)
{
	int r;
	struct waiter w;

	spin_lock(&lock->lock);
	r = __check_holder(lock);
	if (r) {
		spin_unlock(&lock->lock);
		return r;
	}

	if (__available_for_read(lock)) {
		lock->count++;
		__add_holder(lock, current);
		spin_unlock(&lock->lock);
		return 0;
	}

	get_task_struct(current);

	w.task = current;
	w.wants_write = 0;
	list_add_tail(&w.list, &lock->waiters);
	spin_unlock(&lock->lock);

	__wait(&w);
	put_task_struct(current);
	return 0;
}

static int bl_down_read_nonblock(struct block_lock *lock)
{
	int r;

	spin_lock(&lock->lock);
	r = __check_holder(lock);
	if (r)
		goto out;

	if (__available_for_read(lock)) {
		lock->count++;
		__add_holder(lock, current);
		r = 0;
	} else
		r = -EWOULDBLOCK;

out:
	spin_unlock(&lock->lock);
	return r;
}

static void bl_up_read(struct block_lock *lock)
{
	spin_lock(&lock->lock);
	BUG_ON(lock->count <= 0);
	__del_holder(lock, current);
	--lock->count;
	if (!list_empty(&lock->waiters))
		__wake_many(lock);
	spin_unlock(&lock->lock);
}

static int bl_down_write(struct block_lock *lock)
{
	int r;
	struct waiter w;

	spin_lock(&lock->lock);
	r = __check_holder(lock);
	if (r) {
		spin_unlock(&lock->lock);
		return r;
	}

	if (lock->count == 0 && list_empty(&lock->waiters)) {
		lock->count = -1;
		__add_holder(lock, current);
		spin_unlock(&lock->lock);
		return 0;
	}

	get_task_struct(current);
	w.task = current;
	w.wants_write = 1;

	/*
	 * Writers given priority. We know there's only one mutator in the
	 * system, so ignoring the ordering reversal.
	 */
	list_add(&w.list, &lock->waiters);
	spin_unlock(&lock->lock);

	__wait(&w);
	put_task_struct(current);

	return 0;
}

static void bl_up_write(struct block_lock *lock)
{
	spin_lock(&lock->lock);
	__del_holder(lock, current);
	lock->count = 0;
	if (!list_empty(&lock->waiters))
		__wake_many(lock);
	spin_unlock(&lock->lock);
}

static void report_recursive_bug(dm_block_t b, int r)
{
	if (r == -EINVAL)
		DMERR("recursive acquisition of block %llu requested.",
		      (unsigned long long) b);
}

#else  /* !CONFIG_DM_DEBUG_BLOCK_MANAGER_LOCKING */

#define bl_init(x) do { } while (0)
#define bl_down_read(x) 0
#define bl_down_read_nonblock(x) 0
#define bl_up_read(x) do { } while (0)
#define bl_down_write(x) 0
#define bl_up_write(x) do { } while (0)
#define report_recursive_bug(x, y) do { } while (0)

#endif /* CONFIG_DM_DEBUG_BLOCK_MANAGER_LOCKING */

/*----------------------------------------------------------------*/

/*
 * Block manager is currently implemented using dm-bufio.  struct
 * dm_block_manager and struct dm_block map directly onto a couple of
 * structs in the bufio interface.  I want to retain the freedom to move
 * away from bufio in the future.  So these structs are just cast within
 * this .c file, rather than making it through to the public interface.
 */
static struct dm_buffer *to_buffer(struct dm_block *b)
{
	return (struct dm_buffer *) b;
}

dm_block_t dm_block_location(struct dm_block *b)
{
	return dm_bufio_get_block_number(to_buffer(b));
}
EXPORT_SYMBOL_GPL(dm_block_location);

void *dm_block_data(struct dm_block *b)
{
	return dm_bufio_get_block_data(to_buffer(b));
}
EXPORT_SYMBOL_GPL(dm_block_data);

struct buffer_aux {
	struct dm_block_validator *validator;
	int write_locked;

#ifdef CONFIG_DM_DEBUG_BLOCK_MANAGER_LOCKING
	struct block_lock lock;
#endif
};

static void dm_block_manager_alloc_callback(struct dm_buffer *buf)
{
	struct buffer_aux *aux = dm_bufio_get_aux_data(buf);
	aux->validator = NULL;
	bl_init(&aux->lock);
}

static void dm_block_manager_write_callback(struct dm_buffer *buf)
{
	struct buffer_aux *aux = dm_bufio_get_aux_data(buf);
	if (aux->validator) {
		aux->validator->prepare_for_write(aux->validator, (struct dm_block *) buf,
			 dm_bufio_get_block_size(dm_bufio_get_client(buf)));
	}
}

/*----------------------------------------------------------------
 * Public interface
 *--------------------------------------------------------------*/
struct dm_block_manager {
	struct dm_bufio_client *bufio;
	bool read_only:1;
};

struct dm_block_manager *dm_block_manager_create(struct block_device *bdev,
						 unsigned block_size,
						 unsigned max_held_per_thread)
{
	int r;
	struct dm_block_manager *bm;

	bm = kmalloc(sizeof(*bm), GFP_KERNEL);
	if (!bm) {
		r = -ENOMEM;
		goto bad;
	}

	bm->bufio = dm_bufio_client_create(bdev, block_size, max_held_per_thread,
					   sizeof(struct buffer_aux),
					   dm_block_manager_alloc_callback,
					   dm_block_manager_write_callback);
	if (IS_ERR(bm->bufio)) {
		r = PTR_ERR(bm->bufio);
		kfree(bm);
		goto bad;
	}

	bm->read_only = false;

	return bm;

bad:
	return ERR_PTR(r);
}
EXPORT_SYMBOL_GPL(dm_block_manager_create);

void dm_block_manager_destroy(struct dm_block_manager *bm)
{
	dm_bufio_client_destroy(bm->bufio);
	kfree(bm);
}
EXPORT_SYMBOL_GPL(dm_block_manager_destroy);

unsigned dm_bm_block_size(struct dm_block_manager *bm)
{
	return dm_bufio_get_block_size(bm->bufio);
}
EXPORT_SYMBOL_GPL(dm_bm_block_size);

dm_block_t dm_bm_nr_blocks(struct dm_block_manager *bm)
{
	return dm_bufio_get_device_size(bm->bufio);
}

static int dm_bm_validate_buffer(struct dm_block_manager *bm,
				 struct dm_buffer *buf,
				 struct buffer_aux *aux,
				 struct dm_block_validator *v)
{
	if (unlikely(!aux->validator)) {
		int r;
		if (!v)
			return 0;
		r = v->check(v, (struct dm_block *) buf, dm_bufio_get_block_size(bm->bufio));
		if (unlikely(r)) {
			DMERR_LIMIT("%s validator check failed for block %llu", v->name,
				    (unsigned long long) dm_bufio_get_block_number(buf));
			return r;
		}
		aux->validator = v;
	} else {
		if (unlikely(aux->validator != v)) {
			DMERR_LIMIT("validator mismatch (old=%s vs new=%s) for block %llu",
				    aux->validator->name, v ? v->name : "NULL",
				    (unsigned long long) dm_bufio_get_block_number(buf));
			return -EINVAL;
		}
	}

	return 0;
}
int dm_bm_read_lock(struct dm_block_manager *bm, dm_block_t b,
		    struct dm_block_validator *v,
		    struct dm_block **result)
{
	struct buffer_aux *aux;
	void *p;
	int r;

	p = dm_bufio_read(bm->bufio, b, (struct dm_buffer **) result);
	if (IS_ERR(p))
		return PTR_ERR(p);

	aux = dm_bufio_get_aux_data(to_buffer(*result));
	r = bl_down_read(&aux->lock);
	if (unlikely(r)) {
		dm_bufio_release(to_buffer(*result));
		report_recursive_bug(b, r);
		return r;
	}

	aux->write_locked = 0;

	r = dm_bm_validate_buffer(bm, to_buffer(*result), aux, v);
	if (unlikely(r)) {
		bl_up_read(&aux->lock);
		dm_bufio_release(to_buffer(*result));
		return r;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dm_bm_read_lock);

int dm_bm_write_lock(struct dm_block_manager *bm,
		     dm_block_t b, struct dm_block_validator *v,
		     struct dm_block **result)
{
	struct buffer_aux *aux;
	void *p;
	int r;

	if (dm_bm_is_read_only(bm))
		return -EPERM;

	p = dm_bufio_read(bm->bufio, b, (struct dm_buffer **) result);
	if (IS_ERR(p))
		return PTR_ERR(p);

	aux = dm_bufio_get_aux_data(to_buffer(*result));
	r = bl_down_write(&aux->lock);
	if (r) {
		dm_bufio_release(to_buffer(*result));
		report_recursive_bug(b, r);
		return r;
	}

	aux->write_locked = 1;

	r = dm_bm_validate_buffer(bm, to_buffer(*result), aux, v);
	if (unlikely(r)) {
		bl_up_write(&aux->lock);
		dm_bufio_release(to_buffer(*result));
		return r;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(dm_bm_write_lock);

int dm_bm_read_try_lock(struct dm_block_manager *bm,
			dm_block_t b, struct dm_block_validator *v,
			struct dm_block **result)
{
	struct buffer_aux *aux;
	void *p;
	int r;

	p = dm_bufio_get(bm->bufio, b, (struct dm_buffer **) result);
	if (IS_ERR(p))
		return PTR_ERR(p);
	if (unlikely(!p))
		return -EWOULDBLOCK;

	aux = dm_bufio_get_aux_data(to_buffer(*result));
	r = bl_down_read_nonblock(&aux->lock);
	if (r < 0) {
		dm_bufio_release(to_buffer(*result));
		report_recursive_bug(b, r);
		return r;
	}
	aux->write_locked = 0;

	r = dm_bm_validate_buffer(bm, to_buffer(*result), aux, v);
	if (unlikely(r)) {
		bl_up_read(&aux->lock);
		dm_bufio_release(to_buffer(*result));
		return r;
	}

	return 0;
}

int dm_bm_write_lock_zero(struct dm_block_manager *bm,
			  dm_block_t b, struct dm_block_validator *v,
			  struct dm_block **result)
{
	int r;
	struct buffer_aux *aux;
	void *p;

	if (dm_bm_is_read_only(bm))
		return -EPERM;

	p = dm_bufio_new(bm->bufio, b, (struct dm_buffer **) result);
	if (IS_ERR(p))
		return PTR_ERR(p);

	memset(p, 0, dm_bm_block_size(bm));

	aux = dm_bufio_get_aux_data(to_buffer(*result));
	r = bl_down_write(&aux->lock);
	if (r) {
		dm_bufio_release(to_buffer(*result));
		return r;
	}

	aux->write_locked = 1;
	aux->validator = v;

	return 0;
}
EXPORT_SYMBOL_GPL(dm_bm_write_lock_zero);

void dm_bm_unlock(struct dm_block *b)
{
	struct buffer_aux *aux;
	aux = dm_bufio_get_aux_data(to_buffer(b));

	if (aux->write_locked) {
		dm_bufio_mark_buffer_dirty(to_buffer(b));
		bl_up_write(&aux->lock);
	} else
		bl_up_read(&aux->lock);

	dm_bufio_release(to_buffer(b));
}
EXPORT_SYMBOL_GPL(dm_bm_unlock);

int dm_bm_flush(struct dm_block_manager *bm)
{
	if (dm_bm_is_read_only(bm))
		return -EPERM;

	return dm_bufio_write_dirty_buffers(bm->bufio);
}
EXPORT_SYMBOL_GPL(dm_bm_flush);

void dm_bm_prefetch(struct dm_block_manager *bm, dm_block_t b)
{
	dm_bufio_prefetch(bm->bufio, b, 1);
}

bool dm_bm_is_read_only(struct dm_block_manager *bm)
{
	return (bm ? bm->read_only : true);
}
EXPORT_SYMBOL_GPL(dm_bm_is_read_only);

void dm_bm_set_read_only(struct dm_block_manager *bm)
{
	if (bm)
		bm->read_only = true;
}
EXPORT_SYMBOL_GPL(dm_bm_set_read_only);

void dm_bm_set_read_write(struct dm_block_manager *bm)
{
	if (bm)
		bm->read_only = false;
}
EXPORT_SYMBOL_GPL(dm_bm_set_read_write);

u32 dm_bm_checksum(const void *data, size_t len, u32 init_xor)
{
	return crc32c(~(u32) 0, data, len) ^ init_xor;
}
EXPORT_SYMBOL_GPL(dm_bm_checksum);

/*----------------------------------------------------------------*/

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joe Thornber <dm-devel@redhat.com>");
MODULE_DESCRIPTION("Immutable metadata library for dm");

/*----------------------------------------------------------------*/
