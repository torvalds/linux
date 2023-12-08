// SPDX-License-Identifier: GPL-2.0
/*
 * Greybus operations
 *
 * Copyright 2015-2016 Google Inc.
 */

#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/idr.h>

#include "audio_manager.h"
#include "audio_manager_private.h"

static struct kset *manager_kset;

static LIST_HEAD(modules_list);
static DECLARE_RWSEM(modules_rwsem);
static DEFINE_IDA(module_id);

/* helpers */
static struct gb_audio_manager_module *gb_audio_manager_get_locked(int id)
{
	struct gb_audio_manager_module *module;

	if (id < 0)
		return NULL;

	list_for_each_entry(module, &modules_list, list) {
		if (module->id == id)
			return module;
	}

	return NULL;
}

/* public API */
int gb_audio_manager_add(struct gb_audio_manager_module_descriptor *desc)
{
	struct gb_audio_manager_module *module;
	int id;
	int err;

	id = ida_simple_get(&module_id, 0, 0, GFP_KERNEL);
	if (id < 0)
		return id;

	err = gb_audio_manager_module_create(&module, manager_kset,
					     id, desc);
	if (err) {
		ida_simple_remove(&module_id, id);
		return err;
	}

	/* Add it to the list */
	down_write(&modules_rwsem);
	list_add_tail(&module->list, &modules_list);
	up_write(&modules_rwsem);

	return module->id;
}
EXPORT_SYMBOL_GPL(gb_audio_manager_add);

int gb_audio_manager_remove(int id)
{
	struct gb_audio_manager_module *module;

	down_write(&modules_rwsem);

	module = gb_audio_manager_get_locked(id);
	if (!module) {
		up_write(&modules_rwsem);
		return -EINVAL;
	}
	list_del(&module->list);
	kobject_put(&module->kobj);
	up_write(&modules_rwsem);
	ida_simple_remove(&module_id, id);
	return 0;
}
EXPORT_SYMBOL_GPL(gb_audio_manager_remove);

void gb_audio_manager_remove_all(void)
{
	struct gb_audio_manager_module *module, *next;
	int is_empty;

	down_write(&modules_rwsem);

	list_for_each_entry_safe(module, next, &modules_list, list) {
		list_del(&module->list);
		ida_simple_remove(&module_id, module->id);
		kobject_put(&module->kobj);
	}

	is_empty = list_empty(&modules_list);

	up_write(&modules_rwsem);

	if (!is_empty)
		pr_warn("Not all nodes were deleted\n");
}
EXPORT_SYMBOL_GPL(gb_audio_manager_remove_all);

struct gb_audio_manager_module *gb_audio_manager_get_module(int id)
{
	struct gb_audio_manager_module *module;

	down_read(&modules_rwsem);
	module = gb_audio_manager_get_locked(id);
	kobject_get(&module->kobj);
	up_read(&modules_rwsem);
	return module;
}
EXPORT_SYMBOL_GPL(gb_audio_manager_get_module);

void gb_audio_manager_put_module(struct gb_audio_manager_module *module)
{
	kobject_put(&module->kobj);
}
EXPORT_SYMBOL_GPL(gb_audio_manager_put_module);

int gb_audio_manager_dump_module(int id)
{
	struct gb_audio_manager_module *module;

	down_read(&modules_rwsem);
	module = gb_audio_manager_get_locked(id);
	up_read(&modules_rwsem);

	if (!module)
		return -EINVAL;

	gb_audio_manager_module_dump(module);
	return 0;
}
EXPORT_SYMBOL_GPL(gb_audio_manager_dump_module);

void gb_audio_manager_dump_all(void)
{
	struct gb_audio_manager_module *module;
	int count = 0;

	down_read(&modules_rwsem);
	list_for_each_entry(module, &modules_list, list) {
		gb_audio_manager_module_dump(module);
		count++;
	}
	up_read(&modules_rwsem);

	pr_info("Number of connected modules: %d\n", count);
}
EXPORT_SYMBOL_GPL(gb_audio_manager_dump_all);

/*
 * module init/deinit
 */
static int __init manager_init(void)
{
	manager_kset = kset_create_and_add(GB_AUDIO_MANAGER_NAME, NULL,
					   kernel_kobj);
	if (!manager_kset)
		return -ENOMEM;

#ifdef GB_AUDIO_MANAGER_SYSFS
	gb_audio_manager_sysfs_init(&manager_kset->kobj);
#endif

	return 0;
}

static void __exit manager_exit(void)
{
	gb_audio_manager_remove_all();
	kset_unregister(manager_kset);
	ida_destroy(&module_id);
}

module_init(manager_init);
module_exit(manager_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Svetlin Ankov <ankov_svetlin@projectara.com>");
