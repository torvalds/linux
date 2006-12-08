/*
 * Copyright (C) 2003 Sistina Software
 *
 * This file is released under the LGPL.
 */

#ifndef DM_DIRTY_LOG
#define DM_DIRTY_LOG

#include "dm.h"

typedef sector_t region_t;

struct dirty_log_type;

struct dirty_log {
	struct dirty_log_type *type;
	void *context;
};

struct dirty_log_type {
	struct list_head list;
	const char *name;
	struct module *module;
	unsigned int use_count;

	int (*ctr)(struct dirty_log *log, struct dm_target *ti,
		   unsigned int argc, char **argv);
	void (*dtr)(struct dirty_log *log);

	/*
	 * There are times when we don't want the log to touch
	 * the disk.
	 */
	int (*suspend)(struct dirty_log *log);
	int (*resume)(struct dirty_log *log);

	/*
	 * Retrieves the smallest size of region that the log can
	 * deal with.
	 */
	uint32_t (*get_region_size)(struct dirty_log *log);

        /*
	 * A predicate to say whether a region is clean or not.
	 * May block.
	 */
	int (*is_clean)(struct dirty_log *log, region_t region);

	/*
	 *  Returns: 0, 1, -EWOULDBLOCK, < 0
	 *
	 * A predicate function to check the area given by
	 * [sector, sector + len) is in sync.
	 *
	 * If -EWOULDBLOCK is returned the state of the region is
	 * unknown, typically this will result in a read being
	 * passed to a daemon to deal with, since a daemon is
	 * allowed to block.
	 */
	int (*in_sync)(struct dirty_log *log, region_t region, int can_block);

	/*
	 * Flush the current log state (eg, to disk).  This
	 * function may block.
	 */
	int (*flush)(struct dirty_log *log);

	/*
	 * Mark an area as clean or dirty.  These functions may
	 * block, though for performance reasons blocking should
	 * be extremely rare (eg, allocating another chunk of
	 * memory for some reason).
	 */
	void (*mark_region)(struct dirty_log *log, region_t region);
	void (*clear_region)(struct dirty_log *log, region_t region);

	/*
	 * Returns: <0 (error), 0 (no region), 1 (region)
	 *
	 * The mirrord will need perform recovery on regions of
	 * the mirror that are in the NOSYNC state.  This
	 * function asks the log to tell the caller about the
	 * next region that this machine should recover.
	 *
	 * Do not confuse this function with 'in_sync()', one
	 * tells you if an area is synchronised, the other
	 * assigns recovery work.
	*/
	int (*get_resync_work)(struct dirty_log *log, region_t *region);

	/*
	 * This notifies the log that the resync status of a region
	 * has changed.  It also clears the region from the recovering
	 * list (if present).
	 */
	void (*set_region_sync)(struct dirty_log *log,
				region_t region, int in_sync);

        /*
	 * Returns the number of regions that are in sync.
         */
        region_t (*get_sync_count)(struct dirty_log *log);

	/*
	 * Support function for mirror status requests.
	 */
	int (*status)(struct dirty_log *log, status_type_t status_type,
		      char *result, unsigned int maxlen);
};

int dm_register_dirty_log_type(struct dirty_log_type *type);
int dm_unregister_dirty_log_type(struct dirty_log_type *type);


/*
 * Make sure you use these two functions, rather than calling
 * type->constructor/destructor() directly.
 */
struct dirty_log *dm_create_dirty_log(const char *type_name, struct dm_target *ti,
				      unsigned int argc, char **argv);
void dm_destroy_dirty_log(struct dirty_log *log);

/*
 * init/exit functions.
 */
int dm_dirty_log_init(void);
void dm_dirty_log_exit(void);

#endif
