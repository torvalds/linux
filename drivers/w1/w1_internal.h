/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2004 Evgeniy Polyakov <zbr@ioremap.net>
 */

#ifndef __W1_H
#define __W1_H

#include <linux/w1.h>

#include <linux/completion.h>
#include <linux/mutex.h>

#define W1_SLAVE_ACTIVE		0
#define W1_SLAVE_DETACH		1

/**
 * struct w1_async_cmd - execute callback from the w1_process kthread
 * @async_entry: link entry
 * @cb: callback function, must list_del and destroy this list before
 * returning
 *
 * When inserted into the w1_master async_list, w1_process will execute
 * the callback.  Embed this into the structure with the command details.
 */
struct w1_async_cmd {
	struct list_head	async_entry;
	void (*cb)(struct w1_master *dev, struct w1_async_cmd *async_cmd);
};

int w1_create_master_attributes(struct w1_master *master);
void w1_destroy_master_attributes(struct w1_master *master);
void w1_search(struct w1_master *dev, u8 search_type,
	       w1_slave_found_callback cb);
void w1_search_devices(struct w1_master *dev, u8 search_type,
		       w1_slave_found_callback cb);
/* call w1_unref_slave to release the reference counts w1_search_slave added */
struct w1_slave *w1_search_slave(struct w1_reg_num *id);
/*
 * decrements the reference on sl->master and sl, and cleans up if zero
 * returns the reference count after it has been decremented
 */
int w1_unref_slave(struct w1_slave *sl);
void w1_slave_found(struct w1_master *dev, u64 rn);
void w1_search_process_cb(struct w1_master *dev, u8 search_type,
			  w1_slave_found_callback cb);
struct w1_slave *w1_slave_search_device(struct w1_master *dev,
					struct w1_reg_num *rn);
struct w1_master *w1_search_master_id(u32 id);

/* Disconnect and reconnect devices in the given family.  Used for finding
 * unclaimed devices after a family has been registered or releasing devices
 * after a family has been unregistered.  Set attach to 1 when a new family
 * has just been registered, to 0 when it has been unregistered.
 */
void w1_reconnect_slaves(struct w1_family *f, int attach);
int w1_attach_slave_device(struct w1_master *dev, struct w1_reg_num *rn);
/* 0 success, otherwise EBUSY */
int w1_slave_detach(struct w1_slave *sl);

void __w1_remove_master_device(struct w1_master *dev);

void w1_family_put(struct w1_family *f);
void __w1_family_get(struct w1_family *f);
struct w1_family *w1_family_registered(u8 fid);

extern struct device_driver w1_master_driver;
extern struct device w1_master_device;
extern int w1_max_slave_count;
extern int w1_max_slave_ttl;
extern struct list_head w1_masters;
extern struct mutex w1_mlock;
extern spinlock_t w1_flock;

int w1_process_callbacks(struct w1_master *dev);
int w1_process(void *data);

#endif /* __W1_H */
