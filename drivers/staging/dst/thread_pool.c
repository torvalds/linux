/*
 * 2007+ Copyright (c) Evgeniy Polyakov <zbr@ioremap.net>
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/dst.h>
#include <linux/kthread.h>
#include <linux/slab.h>

/*
 * Thread pool abstraction allows to schedule a work to be performed
 * on behalf of kernel thread. One does not operate with threads itself,
 * instead user provides setup and cleanup callbacks for thread pool itself,
 * and action and cleanup callbacks for each submitted work.
 *
 * Each worker has private data initialized at creation time and data,
 * provided by user at scheduling time.
 *
 * When action is being performed, thread can not be used by other users,
 * instead they will sleep until there is free thread to pick their work.
 */
struct thread_pool_worker
{
	struct list_head	worker_entry;

	struct task_struct	*thread;

	struct thread_pool	*pool;

	int			error;
	int			has_data;
	int			need_exit;
	unsigned int		id;

	wait_queue_head_t	wait;

	void			*private;
	void			*schedule_data;

	int			(* action)(void *private, void *schedule_data);
	void			(* cleanup)(void *private);
};

static void thread_pool_exit_worker(struct thread_pool_worker *w)
{
	kthread_stop(w->thread);

	w->cleanup(w->private);
	kfree(w);
}

/*
 * Called to mark thread as ready and allow users to schedule new work.
 */
static void thread_pool_worker_make_ready(struct thread_pool_worker *w)
{
	struct thread_pool *p = w->pool;

	mutex_lock(&p->thread_lock);

	if (!w->need_exit) {
		list_move_tail(&w->worker_entry, &p->ready_list);
		w->has_data = 0;
		mutex_unlock(&p->thread_lock);

		wake_up(&p->wait);
	} else {
		p->thread_num--;
		list_del(&w->worker_entry);
		mutex_unlock(&p->thread_lock);

		thread_pool_exit_worker(w);
	}
}

/*
 * Thread action loop: waits until there is new work.
 */
static int thread_pool_worker_func(void *data)
{
	struct thread_pool_worker *w = data;

	while (!kthread_should_stop()) {
		wait_event_interruptible(w->wait,
			kthread_should_stop() || w->has_data);

		if (kthread_should_stop())
			break;

		if (!w->has_data)
			continue;

		w->action(w->private, w->schedule_data);
		thread_pool_worker_make_ready(w);
	}

	return 0;
}

/*
 * Remove single worker without specifying which one.
 */
void thread_pool_del_worker(struct thread_pool *p)
{
	struct thread_pool_worker *w = NULL;

	while (!w && p->thread_num) {
		wait_event(p->wait, !list_empty(&p->ready_list) || !p->thread_num);

		dprintk("%s: locking list_empty: %d, thread_num: %d.\n",
				__func__, list_empty(&p->ready_list), p->thread_num);

		mutex_lock(&p->thread_lock);
		if (!list_empty(&p->ready_list)) {
			w = list_first_entry(&p->ready_list,
					struct thread_pool_worker,
					worker_entry);

			dprintk("%s: deleting w: %p, thread_num: %d, list: %p [%p.%p].\n",
					__func__, w, p->thread_num, &p->ready_list,
					p->ready_list.prev, p->ready_list.next);

			p->thread_num--;
			list_del(&w->worker_entry);
		}
		mutex_unlock(&p->thread_lock);
	}

	if (w)
		thread_pool_exit_worker(w);
	dprintk("%s: deleted w: %p, thread_num: %d.\n",
			__func__, w, p->thread_num);
}

/*
 * Remove a worker with given ID.
 */
void thread_pool_del_worker_id(struct thread_pool *p, unsigned int id)
{
	struct thread_pool_worker *w;
	int found = 0;

	mutex_lock(&p->thread_lock);
	list_for_each_entry(w, &p->ready_list, worker_entry) {
		if (w->id == id) {
			found = 1;
			p->thread_num--;
			list_del(&w->worker_entry);
			break;
		}
	}

	if (!found) {
		list_for_each_entry(w, &p->active_list, worker_entry) {
			if (w->id == id) {
				w->need_exit = 1;
				break;
			}
		}
	}
	mutex_unlock(&p->thread_lock);

	if (found)
		thread_pool_exit_worker(w);
}

/*
 * Add new worker thread with given parameters.
 * If initialization callback fails, return error.
 */
int thread_pool_add_worker(struct thread_pool *p,
		char *name,
		unsigned int id,
		void *(* init)(void *private),
		void (* cleanup)(void *private),
		void *private)
{
	struct thread_pool_worker *w;
	int err = -ENOMEM;

	w = kzalloc(sizeof(struct thread_pool_worker), GFP_KERNEL);
	if (!w)
		goto err_out_exit;

	w->pool = p;
	init_waitqueue_head(&w->wait);
	w->cleanup = cleanup;
	w->id = id;

	w->thread = kthread_run(thread_pool_worker_func, w, "%s", name);
	if (IS_ERR(w->thread)) {
		err = PTR_ERR(w->thread);
		goto err_out_free;
	}

	w->private = init(private);
	if (IS_ERR(w->private)) {
		err = PTR_ERR(w->private);
		goto err_out_stop_thread;
	}

	mutex_lock(&p->thread_lock);
	list_add_tail(&w->worker_entry, &p->ready_list);
	p->thread_num++;
	mutex_unlock(&p->thread_lock);

	return 0;

err_out_stop_thread:
	kthread_stop(w->thread);
err_out_free:
	kfree(w);
err_out_exit:
	return err;
}

/*
 * Destroy the whole pool.
 */
void thread_pool_destroy(struct thread_pool *p)
{
	while (p->thread_num) {
		dprintk("%s: num: %d.\n", __func__, p->thread_num);
		thread_pool_del_worker(p);
	}

	kfree(p);
}

/*
 * Create a pool with given number of threads.
 * They will have sequential IDs started from zero.
 */
struct thread_pool *thread_pool_create(int num, char *name,
		void *(* init)(void *private),
		void (* cleanup)(void *private),
		void *private)
{
	struct thread_pool_worker *w, *tmp;
	struct thread_pool *p;
	int err = -ENOMEM;
	int i;

	p = kzalloc(sizeof(struct thread_pool), GFP_KERNEL);
	if (!p)
		goto err_out_exit;

	init_waitqueue_head(&p->wait);
	mutex_init(&p->thread_lock);
	INIT_LIST_HEAD(&p->ready_list);
	INIT_LIST_HEAD(&p->active_list);
	p->thread_num = 0;

	for (i=0; i<num; ++i) {
		err = thread_pool_add_worker(p, name, i, init,
				cleanup, private);
		if (err)
			goto err_out_free_all;
	}

	return p;

err_out_free_all:
	list_for_each_entry_safe(w, tmp, &p->ready_list, worker_entry) {
		list_del(&w->worker_entry);
		thread_pool_exit_worker(w);
	}
	kfree(p);
err_out_exit:
	return ERR_PTR(err);
}

/*
 * Schedule execution of the action on a given thread,
 * provided ID pointer has to match previously stored
 * private data.
 */
int thread_pool_schedule_private(struct thread_pool *p,
		int (* setup)(void *private, void *data),
		int (* action)(void *private, void *data),
		void *data, long timeout, void *id)
{
	struct thread_pool_worker *w, *tmp, *worker = NULL;
	int err = 0;

	while (!worker && !err) {
		timeout = wait_event_interruptible_timeout(p->wait,
				!list_empty(&p->ready_list),
				timeout);

		if (!timeout) {
			err = -ETIMEDOUT;
			break;
		}

		worker = NULL;
		mutex_lock(&p->thread_lock);
		list_for_each_entry_safe(w, tmp, &p->ready_list, worker_entry) {
			if (id && id != w->private)
				continue;

			worker = w;

			list_move_tail(&w->worker_entry, &p->active_list);

			err = setup(w->private, data);
			if (!err) {
				w->schedule_data = data;
				w->action = action;
				w->has_data = 1;
				wake_up(&w->wait);
			} else {
				list_move_tail(&w->worker_entry, &p->ready_list);
			}

			break;
		}
		mutex_unlock(&p->thread_lock);
	}

	return err;
}

/*
 * Schedule execution on arbitrary thread from the pool.
 */
int thread_pool_schedule(struct thread_pool *p,
		int (* setup)(void *private, void *data),
		int (* action)(void *private, void *data),
		void *data, long timeout)
{
	return thread_pool_schedule_private(p, setup,
			action, data, timeout, NULL);
}
