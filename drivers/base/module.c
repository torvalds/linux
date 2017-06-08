/*
 * module.c - module sysfs fun for drivers
 *
 * This file is released under the GPLv2
 *
 */
#include <linux/device.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "base.h"

static char *make_driver_name(struct device_driver *drv)
{
	char *driver_name;

	driver_name = kasprintf(GFP_KERNEL, "%s:%s", drv->bus->name, drv->name);
	if (!driver_name)
		return NULL;

	return driver_name;
}

static void module_create_drivers_dir(struct module_kobject *mk)
{
	static DEFINE_MUTEX(drivers_dir_mutex);

	mutex_lock(&drivers_dir_mutex);
	if (mk && !mk->drivers_dir)
		mk->drivers_dir = kobject_create_and_add("drivers", &mk->kobj);
	mutex_unlock(&drivers_dir_mutex);
}

void module_add_driver(struct module *mod, struct device_driver *drv)
{
	char *driver_name;
	int no_warn;
	struct module_kobject *mk = NULL;

	if (!drv)
		return;

	if (mod)
		mk = &mod->mkobj;
	else if (drv->mod_name) {
		struct kobject *mkobj;

		/* Lookup built-in module entry in /sys/modules */
		mkobj = kset_find_obj(module_kset, drv->mod_name);
		if (mkobj) {
			mk = container_of(mkobj, struct module_kobject, kobj);
			/* remember our module structure */
			drv->p->mkobj = mk;
			/* kset_find_obj took a reference */
			kobject_put(mkobj);
		}
	}

	if (!mk)
		return;

	/* Don't check return codes; these calls are idempotent */
	no_warn = sysfs_create_link(&drv->p->kobj, &mk->kobj, "module");
	driver_name = make_driver_name(drv);
	if (driver_name) {
		module_create_drivers_dir(mk);
		no_warn = sysfs_create_link(mk->drivers_dir, &drv->p->kobj,
					    driver_name);
		kfree(driver_name);
	}
}

void module_remove_driver(struct device_driver *drv)
{
	struct module_kobject *mk = NULL;
	char *driver_name;

	if (!drv)
		return;

	sysfs_remove_link(&drv->p->kobj, "module");

	if (drv->owner)
		mk = &drv->owner->mkobj;
	else if (drv->p->mkobj)
		mk = drv->p->mkobj;
	if (mk && mk->drivers_dir) {
		driver_name = make_driver_name(drv);
		if (driver_name) {
			sysfs_remove_link(mk->drivers_dir, driver_name);
			kfree(driver_name);
		}
	}
}
