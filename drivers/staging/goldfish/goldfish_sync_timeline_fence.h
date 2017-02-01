#include <linux/sync_file.h>
#include <linux/fence.h>

/**
 * struct sync_pt - sync_pt object
 * @base: base fence object
 * @child_list: sync timeline child's list
 * @active_list: sync timeline active child's list
 */
struct sync_pt {
	struct fence base;
	struct list_head child_list;
	struct list_head active_list;
};

/**
 * goldfish_sync_timeline_create_internal() - creates a sync object
 * @name:	goldfish_sync_timeline name
 *
 * Creates a new goldfish_sync_timeline.
 * Returns the goldfish_sync_timeline object or NULL in case of error.
 */
struct goldfish_sync_timeline
*goldfish_sync_timeline_create_internal(const char *name);

/**
 * goldfish_sync_pt_create_internal() - creates a sync pt
 * @parent:	fence's parent goldfish_sync_timeline
 * @size:	size to allocate for this pt
 * @inc:	value of the fence
 *
 * Creates a new sync_pt as a child of @parent.  @size bytes will be
 * allocated allowing for implementation specific data to be kept after
 * the generic sync_timeline struct. Returns the sync_pt object or
 * NULL in case of error.
 */
struct sync_pt
*goldfish_sync_pt_create_internal(struct goldfish_sync_timeline *obj,
									int size, unsigned int value);

/**
 * goldfish_sync_timeline_signal_internal() -
 * signal a status change on a sync_timeline
 * @obj:	goldfish_sync_timeline to signal
 * @inc:	num to increment on timeline->value
 *
 * A sync implementation should call this any time one of it's fences
 * has signaled or has an error condition.
 */
void goldfish_sync_timeline_signal_internal(struct goldfish_sync_timeline *obj,
											unsigned int inc);

/**
 * goldfish_sync_timeline_put_internal() - dec refcount of a sync_timeline
 * and clean up memory if it was the last ref.
 * @obj:	goldfish_sync_timeline to decref
 */
void goldfish_sync_timeline_put_internal(struct goldfish_sync_timeline *obj);
