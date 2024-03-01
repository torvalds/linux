// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "thread-utils.h"

#include <asm/current.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "errors.h"
#include "logger.h"
#include "memory-alloc.h"

static struct hlist_head thread_list;
static struct mutex thread_mutex;

struct thread {
	void (*thread_function)(void *thread_data);
	void *thread_data;
	struct hlist_node thread_links;
	struct task_struct *thread_task;
	struct completion thread_done;
};

void vdo_initialize_threads_mutex(void)
{
	mutex_init(&thread_mutex);
}

static int thread_starter(void *arg)
{
	struct registered_thread allocating_thread;
	struct thread *thread = arg;

	thread->thread_task = current;
	mutex_lock(&thread_mutex);
	hlist_add_head(&thread->thread_links, &thread_list);
	mutex_unlock(&thread_mutex);
	vdo_register_allocating_thread(&allocating_thread, NULL);
	thread->thread_function(thread->thread_data);
	vdo_unregister_allocating_thread();
	complete(&thread->thread_done);
	return 0;
}

int vdo_create_thread(void (*thread_function)(void *), void *thread_data,
		      const char *name, struct thread **new_thread)
{
	char *name_colon = strchr(name, ':');
	char *my_name_colon = strchr(current->comm, ':');
	struct task_struct *task;
	struct thread *thread;
	int result;

	result = vdo_allocate(1, struct thread, __func__, &thread);
	if (result != VDO_SUCCESS) {
		vdo_log_warning("Error allocating memory for %s", name);
		return result;
	}

	thread->thread_function = thread_function;
	thread->thread_data = thread_data;
	init_completion(&thread->thread_done);
	/*
	 * Start the thread, with an appropriate thread name.
	 *
	 * If the name supplied contains a colon character, use that name. This causes uds module
	 * threads to have names like "uds:callbackW" and the main test runner thread to be named
	 * "zub:runtest".
	 *
	 * Otherwise if the current thread has a name containing a colon character, prefix the name
	 * supplied with the name of the current thread up to (and including) the colon character.
	 * Thus when the "kvdo0:dedupeQ" thread opens an index session, all the threads associated
	 * with that index will have names like "kvdo0:foo".
	 *
	 * Otherwise just use the name supplied. This should be a rare occurrence.
	 */
	if ((name_colon == NULL) && (my_name_colon != NULL)) {
		task = kthread_run(thread_starter, thread, "%.*s:%s",
				   (int) (my_name_colon - current->comm), current->comm,
				   name);
	} else {
		task = kthread_run(thread_starter, thread, "%s", name);
	}

	if (IS_ERR(task)) {
		vdo_free(thread);
		return PTR_ERR(task);
	}

	*new_thread = thread;
	return VDO_SUCCESS;
}

void vdo_join_threads(struct thread *thread)
{
	while (wait_for_completion_interruptible(&thread->thread_done))
		fsleep(1000);

	mutex_lock(&thread_mutex);
	hlist_del(&thread->thread_links);
	mutex_unlock(&thread_mutex);
	vdo_free(thread);
}
