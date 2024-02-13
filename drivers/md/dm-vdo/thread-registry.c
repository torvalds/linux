// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2023 Red Hat
 */

#include "thread-registry.h"

#include <asm/current.h>
#include <linux/rculist.h>

#include "permassert.h"

/*
 * We need to be careful when using other facilities that may use thread registry functions in
 * their normal operation. For example, we do not want to invoke the logger while holding a lock.
 */

void vdo_initialize_thread_registry(struct thread_registry *registry)
{
	INIT_LIST_HEAD(&registry->links);
	spin_lock_init(&registry->lock);
}

/* Register the current thread and associate it with a data pointer. */
void vdo_register_thread(struct thread_registry *registry,
			 struct registered_thread *new_thread, const void *pointer)
{
	struct registered_thread *thread;
	bool found_it = false;

	INIT_LIST_HEAD(&new_thread->links);
	new_thread->pointer = pointer;
	new_thread->task = current;

	spin_lock(&registry->lock);
	list_for_each_entry(thread, &registry->links, links) {
		if (thread->task == current) {
			/* There should be no existing entry. */
			list_del_rcu(&thread->links);
			found_it = true;
			break;
		}
	}
	list_add_tail_rcu(&new_thread->links, &registry->links);
	spin_unlock(&registry->lock);

	VDO_ASSERT_LOG_ONLY(!found_it, "new thread not already in registry");
	if (found_it) {
		/* Ensure no RCU iterators see it before re-initializing. */
		synchronize_rcu();
		INIT_LIST_HEAD(&thread->links);
	}
}

void vdo_unregister_thread(struct thread_registry *registry)
{
	struct registered_thread *thread;
	bool found_it = false;

	spin_lock(&registry->lock);
	list_for_each_entry(thread, &registry->links, links) {
		if (thread->task == current) {
			list_del_rcu(&thread->links);
			found_it = true;
			break;
		}
	}
	spin_unlock(&registry->lock);

	VDO_ASSERT_LOG_ONLY(found_it, "thread found in registry");
	if (found_it) {
		/* Ensure no RCU iterators see it before re-initializing. */
		synchronize_rcu();
		INIT_LIST_HEAD(&thread->links);
	}
}

const void *vdo_lookup_thread(struct thread_registry *registry)
{
	struct registered_thread *thread;
	const void *result = NULL;

	rcu_read_lock();
	list_for_each_entry_rcu(thread, &registry->links, links) {
		if (thread->task == current) {
			result = thread->pointer;
			break;
		}
	}
	rcu_read_unlock();

	return result;
}
