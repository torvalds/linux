// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus operations
 *
 * Copyright 2015-2016 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#ifndef _GB_AUDIO_MANAGER_H_
#define _GB_AUDIO_MANAGER_H_

#include <linux/kobject.h>
#include <linux/list.h>

#define GB_AUDIO_MANAGER_NAME "gb_audio_manager"
#define GB_AUDIO_MANAGER_MODULE_NAME_LEN 64
#define GB_AUDIO_MANAGER_MODULE_NAME_LEN_SSCANF "63"

struct gb_audio_manager_module_descriptor {
	char name[GB_AUDIO_MANAGER_MODULE_NAME_LEN];
	int vid;
	int pid;
	int intf_id;
	unsigned int ip_devices;
	unsigned int op_devices;
};

struct gb_audio_manager_module {
	struct kobject kobj;
	struct list_head list;
	int id;
	struct gb_audio_manager_module_descriptor desc;
};

/*
 * Creates a new gb_audio_manager_module_descriptor, using the specified
 * descriptor.
 *
 * Returns a negative result on error, or the id of the newly created module.
 *
 */
int gb_audio_manager_add(struct gb_audio_manager_module_descriptor *desc);

/*
 * Removes a connected gb_audio_manager_module_descriptor for the specified ID.
 *
 * Returns zero on success, or a negative value on error.
 */
int gb_audio_manager_remove(int id);

/*
 * Removes all connected gb_audio_modules
 *
 * Returns zero on success, or a negative value on error.
 */
void gb_audio_manager_remove_all(void);

/*
 * Retrieves a gb_audio_manager_module_descriptor for the specified id.
 * Returns the gb_audio_manager_module_descriptor structure,
 * or NULL if there is no module with the specified ID.
 */
struct gb_audio_manager_module *gb_audio_manager_get_module(int id);

/*
 * Decreases the refcount of the module, obtained by the get function.
 * Modules are removed via gb_audio_manager_remove
 */
void gb_audio_manager_put_module(struct gb_audio_manager_module *module);

/*
 * Dumps the module for the specified id
 * Return 0 on success
 */
int gb_audio_manager_dump_module(int id);

/*
 * Dumps all connected modules
 */
void gb_audio_manager_dump_all(void);

#endif /* _GB_AUDIO_MANAGER_H_ */
