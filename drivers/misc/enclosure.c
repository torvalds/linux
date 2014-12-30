/*
 * Enclosure Services
 *
 * Copyright (C) 2008 James Bottomley <James.Bottomley@HansenPartnership.com>
 *
**-----------------------------------------------------------------------------
**
**  This program is free software; you can redistribute it and/or
**  modify it under the terms of the GNU General Public License
**  version 2 as published by the Free Software Foundation.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**-----------------------------------------------------------------------------
*/
#include <linux/device.h>
#include <linux/enclosure.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>

static LIST_HEAD(container_list);
static DEFINE_MUTEX(container_list_lock);
static struct class enclosure_class;

/**
 * enclosure_find - find an enclosure given a parent device
 * @dev:	the parent to match against
 * @start:	Optional enclosure device to start from (NULL if none)
 *
 * Looks through the list of registered enclosures to find all those
 * with @dev as a parent.  Returns NULL if no enclosure is
 * found. @start can be used as a starting point to obtain multiple
 * enclosures per parent (should begin with NULL and then be set to
 * each returned enclosure device). Obtains a reference to the
 * enclosure class device which must be released with device_put().
 * If @start is not NULL, a reference must be taken on it which is
 * released before returning (this allows a loop through all
 * enclosures to exit with only the reference on the enclosure of
 * interest held).  Note that the @dev may correspond to the actual
 * device housing the enclosure, in which case no iteration via @start
 * is required.
 */
struct enclosure_device *enclosure_find(struct device *dev,
					struct enclosure_device *start)
{
	struct enclosure_device *edev;

	mutex_lock(&container_list_lock);
	edev = list_prepare_entry(start, &container_list, node);
	if (start)
		put_device(&start->edev);

	list_for_each_entry_continue(edev, &container_list, node) {
		struct device *parent = edev->edev.parent;
		/* parent might not be immediate, so iterate up to
		 * the root of the tree if necessary */
		while (parent) {
			if (parent == dev) {
				get_device(&edev->edev);
				mutex_unlock(&container_list_lock);
				return edev;
			}
			parent = parent->parent;
		}
	}
	mutex_unlock(&container_list_lock);

	return NULL;
}
EXPORT_SYMBOL_GPL(enclosure_find);

/**
 * enclosure_for_each_device - calls a function for each enclosure
 * @fn:		the function to call
 * @data:	the data to pass to each call
 *
 * Loops over all the enclosures calling the function.
 *
 * Note, this function uses a mutex which will be held across calls to
 * @fn, so it must have non atomic context, and @fn may (although it
 * should not) sleep or otherwise cause the mutex to be held for
 * indefinite periods
 */
int enclosure_for_each_device(int (*fn)(struct enclosure_device *, void *),
			      void *data)
{
	int error = 0;
	struct enclosure_device *edev;

	mutex_lock(&container_list_lock);
	list_for_each_entry(edev, &container_list, node) {
		error = fn(edev, data);
		if (error)
			break;
	}
	mutex_unlock(&container_list_lock);

	return error;
}
EXPORT_SYMBOL_GPL(enclosure_for_each_device);

/**
 * enclosure_register - register device as an enclosure
 *
 * @dev:	device containing the enclosure
 * @components:	number of components in the enclosure
 *
 * This sets up the device for being an enclosure.  Note that @dev does
 * not have to be a dedicated enclosure device.  It may be some other type
 * of device that additionally responds to enclosure services
 */
struct enclosure_device *
enclosure_register(struct device *dev, const char *name, int components,
		   struct enclosure_component_callbacks *cb)
{
	struct enclosure_device *edev =
		kzalloc(sizeof(struct enclosure_device) +
			sizeof(struct enclosure_component)*components,
			GFP_KERNEL);
	int err, i;

	BUG_ON(!cb);

	if (!edev)
		return ERR_PTR(-ENOMEM);

	edev->components = components;

	edev->edev.class = &enclosure_class;
	edev->edev.parent = get_device(dev);
	edev->cb = cb;
	dev_set_name(&edev->edev, "%s", name);
	err = device_register(&edev->edev);
	if (err)
		goto err;

	for (i = 0; i < components; i++)
		edev->component[i].number = -1;

	mutex_lock(&container_list_lock);
	list_add_tail(&edev->node, &container_list);
	mutex_unlock(&container_list_lock);

	return edev;

 err:
	put_device(edev->edev.parent);
	kfree(edev);
	return ERR_PTR(err);
}
EXPORT_SYMBOL_GPL(enclosure_register);

static struct enclosure_component_callbacks enclosure_null_callbacks;

/**
 * enclosure_unregister - remove an enclosure
 *
 * @edev:	the registered enclosure to remove;
 */
void enclosure_unregister(struct enclosure_device *edev)
{
	int i;

	mutex_lock(&container_list_lock);
	list_del(&edev->node);
	mutex_unlock(&container_list_lock);

	for (i = 0; i < edev->components; i++)
		if (edev->component[i].number != -1)
			device_unregister(&edev->component[i].cdev);

	/* prevent any callbacks into service user */
	edev->cb = &enclosure_null_callbacks;
	device_unregister(&edev->edev);
}
EXPORT_SYMBOL_GPL(enclosure_unregister);

#define ENCLOSURE_NAME_SIZE	64
#define COMPONENT_NAME_SIZE	64

static void enclosure_link_name(struct enclosure_component *cdev, char *name)
{
	strcpy(name, "enclosure_device:");
	strcat(name, dev_name(&cdev->cdev));
}

static void enclosure_remove_links(struct enclosure_component *cdev)
{
	char name[ENCLOSURE_NAME_SIZE];

	/*
	 * In odd circumstances, like multipath devices, something else may
	 * already have removed the links, so check for this condition first.
	 */
	if (!cdev->dev->kobj.sd)
		return;

	enclosure_link_name(cdev, name);
	sysfs_remove_link(&cdev->dev->kobj, name);
	sysfs_remove_link(&cdev->cdev.kobj, "device");
}

static int enclosure_add_links(struct enclosure_component *cdev)
{
	int error;
	char name[ENCLOSURE_NAME_SIZE];

	error = sysfs_create_link(&cdev->cdev.kobj, &cdev->dev->kobj, "device");
	if (error)
		return error;

	enclosure_link_name(cdev, name);
	error = sysfs_create_link(&cdev->dev->kobj, &cdev->cdev.kobj, name);
	if (error)
		sysfs_remove_link(&cdev->cdev.kobj, "device");

	return error;
}

static void enclosure_release(struct device *cdev)
{
	struct enclosure_device *edev = to_enclosure_device(cdev);

	put_device(cdev->parent);
	kfree(edev);
}

static void enclosure_component_release(struct device *dev)
{
	struct enclosure_component *cdev = to_enclosure_component(dev);

	if (cdev->dev) {
		enclosure_remove_links(cdev);
		put_device(cdev->dev);
	}
	put_device(dev->parent);
}

static struct enclosure_component *
enclosure_component_find_by_name(struct enclosure_device *edev,
				const char *name)
{
	int i;
	const char *cname;
	struct enclosure_component *ecomp;

	if (!edev || !name || !name[0])
		return NULL;

	for (i = 0; i < edev->components; i++) {
		ecomp = &edev->component[i];
		cname = dev_name(&ecomp->cdev);
		if (ecomp->number != -1 &&
		    cname && cname[0] &&
		    !strcmp(cname, name))
			return ecomp;
	}

	return NULL;
}

static const struct attribute_group *enclosure_component_groups[];

/**
 * enclosure_component_alloc - prepare a new enclosure component
 * @edev:	the enclosure to add the component
 * @num:	the device number
 * @type:	the type of component being added
 * @name:	an optional name to appear in sysfs (leave NULL if none)
 *
 * The name is optional for enclosures that give their components a unique
 * name.  If not, leave the field NULL and a name will be assigned.
 *
 * Returns a pointer to the enclosure component or an error.
 */
struct enclosure_component *
enclosure_component_alloc(struct enclosure_device *edev,
			  unsigned int number,
			  enum enclosure_component_type type,
			  const char *name)
{
	struct enclosure_component *ecomp;
	struct device *cdev;
	int i;
	char newname[COMPONENT_NAME_SIZE];

	if (number >= edev->components)
		return ERR_PTR(-EINVAL);

	ecomp = &edev->component[number];

	if (ecomp->number != -1)
		return ERR_PTR(-EINVAL);

	ecomp->type = type;
	ecomp->number = number;
	cdev = &ecomp->cdev;
	cdev->parent = get_device(&edev->edev);

	if (name && name[0]) {
		/* Some hardware (e.g. enclosure in RX300 S6) has components
		 * with non unique names. Registering duplicates in sysfs
		 * will lead to warnings during bootup. So make the names
		 * unique by appending consecutive numbers -1, -2, ... */
		i = 1;
		snprintf(newname, COMPONENT_NAME_SIZE,
			 "%s", name);
		while (enclosure_component_find_by_name(edev, newname))
			snprintf(newname, COMPONENT_NAME_SIZE,
				 "%s-%i", name, i++);
		dev_set_name(cdev, "%s", newname);
	} else
		dev_set_name(cdev, "%u", number);

	cdev->release = enclosure_component_release;
	cdev->groups = enclosure_component_groups;

	return ecomp;
}
EXPORT_SYMBOL_GPL(enclosure_component_alloc);

/**
 * enclosure_component_register - publishes an initialized enclosure component
 * @ecomp:	component to add
 *
 * Returns 0 on successful registration, releases the component otherwise
 */
int enclosure_component_register(struct enclosure_component *ecomp)
{
	struct device *cdev;
	int err;

	cdev = &ecomp->cdev;
	err = device_register(cdev);
	if (err) {
		ecomp->number = -1;
		put_device(cdev);
		return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(enclosure_component_register);

/**
 * enclosure_add_device - add a device as being part of an enclosure
 * @edev:	the enclosure device being added to.
 * @num:	the number of the component
 * @dev:	the device being added
 *
 * Declares a real device to reside in slot (or identifier) @num of an
 * enclosure.  This will cause the relevant sysfs links to appear.
 * This function may also be used to change a device associated with
 * an enclosure without having to call enclosure_remove_device() in
 * between.
 *
 * Returns zero on success or an error.
 */
int enclosure_add_device(struct enclosure_device *edev, int component,
			 struct device *dev)
{
	struct enclosure_component *cdev;

	if (!edev || component >= edev->components)
		return -EINVAL;

	cdev = &edev->component[component];

	if (cdev->dev == dev)
		return -EEXIST;

	if (cdev->dev)
		enclosure_remove_links(cdev);

	put_device(cdev->dev);
	cdev->dev = get_device(dev);
	return enclosure_add_links(cdev);
}
EXPORT_SYMBOL_GPL(enclosure_add_device);

/**
 * enclosure_remove_device - remove a device from an enclosure
 * @edev:	the enclosure device
 * @num:	the number of the component to remove
 *
 * Returns zero on success or an error.
 *
 */
int enclosure_remove_device(struct enclosure_device *edev, struct device *dev)
{
	struct enclosure_component *cdev;
	int i;

	if (!edev || !dev)
		return -EINVAL;

	for (i = 0; i < edev->components; i++) {
		cdev = &edev->component[i];
		if (cdev->dev == dev) {
			enclosure_remove_links(cdev);
			device_del(&cdev->cdev);
			put_device(dev);
			cdev->dev = NULL;
			return device_add(&cdev->cdev);
		}
	}
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(enclosure_remove_device);

/*
 * sysfs pieces below
 */

static ssize_t components_show(struct device *cdev,
			       struct device_attribute *attr, char *buf)
{
	struct enclosure_device *edev = to_enclosure_device(cdev);

	return snprintf(buf, 40, "%d\n", edev->components);
}
static DEVICE_ATTR_RO(components);

static struct attribute *enclosure_class_attrs[] = {
	&dev_attr_components.attr,
	NULL,
};
ATTRIBUTE_GROUPS(enclosure_class);

static struct class enclosure_class = {
	.name			= "enclosure",
	.owner			= THIS_MODULE,
	.dev_release		= enclosure_release,
	.dev_groups		= enclosure_class_groups,
};

static const char *const enclosure_status [] = {
	[ENCLOSURE_STATUS_UNSUPPORTED] = "unsupported",
	[ENCLOSURE_STATUS_OK] = "OK",
	[ENCLOSURE_STATUS_CRITICAL] = "critical",
	[ENCLOSURE_STATUS_NON_CRITICAL] = "non-critical",
	[ENCLOSURE_STATUS_UNRECOVERABLE] = "unrecoverable",
	[ENCLOSURE_STATUS_NOT_INSTALLED] = "not installed",
	[ENCLOSURE_STATUS_UNKNOWN] = "unknown",
	[ENCLOSURE_STATUS_UNAVAILABLE] = "unavailable",
	[ENCLOSURE_STATUS_MAX] = NULL,
};

static const char *const enclosure_type [] = {
	[ENCLOSURE_COMPONENT_DEVICE] = "device",
	[ENCLOSURE_COMPONENT_ARRAY_DEVICE] = "array device",
};

static ssize_t get_component_fault(struct device *cdev,
				   struct device_attribute *attr, char *buf)
{
	struct enclosure_device *edev = to_enclosure_device(cdev->parent);
	struct enclosure_component *ecomp = to_enclosure_component(cdev);

	if (edev->cb->get_fault)
		edev->cb->get_fault(edev, ecomp);
	return snprintf(buf, 40, "%d\n", ecomp->fault);
}

static ssize_t set_component_fault(struct device *cdev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct enclosure_device *edev = to_enclosure_device(cdev->parent);
	struct enclosure_component *ecomp = to_enclosure_component(cdev);
	int val = simple_strtoul(buf, NULL, 0);

	if (edev->cb->set_fault)
		edev->cb->set_fault(edev, ecomp, val);
	return count;
}

static ssize_t get_component_status(struct device *cdev,
				    struct device_attribute *attr,char *buf)
{
	struct enclosure_device *edev = to_enclosure_device(cdev->parent);
	struct enclosure_component *ecomp = to_enclosure_component(cdev);

	if (edev->cb->get_status)
		edev->cb->get_status(edev, ecomp);
	return snprintf(buf, 40, "%s\n", enclosure_status[ecomp->status]);
}

static ssize_t set_component_status(struct device *cdev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct enclosure_device *edev = to_enclosure_device(cdev->parent);
	struct enclosure_component *ecomp = to_enclosure_component(cdev);
	int i;

	for (i = 0; enclosure_status[i]; i++) {
		if (strncmp(buf, enclosure_status[i],
			    strlen(enclosure_status[i])) == 0 &&
		    (buf[strlen(enclosure_status[i])] == '\n' ||
		     buf[strlen(enclosure_status[i])] == '\0'))
			break;
	}

	if (enclosure_status[i] && edev->cb->set_status) {
		edev->cb->set_status(edev, ecomp, i);
		return count;
	} else
		return -EINVAL;
}

static ssize_t get_component_active(struct device *cdev,
				    struct device_attribute *attr, char *buf)
{
	struct enclosure_device *edev = to_enclosure_device(cdev->parent);
	struct enclosure_component *ecomp = to_enclosure_component(cdev);

	if (edev->cb->get_active)
		edev->cb->get_active(edev, ecomp);
	return snprintf(buf, 40, "%d\n", ecomp->active);
}

static ssize_t set_component_active(struct device *cdev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct enclosure_device *edev = to_enclosure_device(cdev->parent);
	struct enclosure_component *ecomp = to_enclosure_component(cdev);
	int val = simple_strtoul(buf, NULL, 0);

	if (edev->cb->set_active)
		edev->cb->set_active(edev, ecomp, val);
	return count;
}

static ssize_t get_component_locate(struct device *cdev,
				    struct device_attribute *attr, char *buf)
{
	struct enclosure_device *edev = to_enclosure_device(cdev->parent);
	struct enclosure_component *ecomp = to_enclosure_component(cdev);

	if (edev->cb->get_locate)
		edev->cb->get_locate(edev, ecomp);
	return snprintf(buf, 40, "%d\n", ecomp->locate);
}

static ssize_t set_component_locate(struct device *cdev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct enclosure_device *edev = to_enclosure_device(cdev->parent);
	struct enclosure_component *ecomp = to_enclosure_component(cdev);
	int val = simple_strtoul(buf, NULL, 0);

	if (edev->cb->set_locate)
		edev->cb->set_locate(edev, ecomp, val);
	return count;
}

static ssize_t get_component_type(struct device *cdev,
				  struct device_attribute *attr, char *buf)
{
	struct enclosure_component *ecomp = to_enclosure_component(cdev);

	return snprintf(buf, 40, "%s\n", enclosure_type[ecomp->type]);
}


static DEVICE_ATTR(fault, S_IRUGO | S_IWUSR, get_component_fault,
		    set_component_fault);
static DEVICE_ATTR(status, S_IRUGO | S_IWUSR, get_component_status,
		   set_component_status);
static DEVICE_ATTR(active, S_IRUGO | S_IWUSR, get_component_active,
		   set_component_active);
static DEVICE_ATTR(locate, S_IRUGO | S_IWUSR, get_component_locate,
		   set_component_locate);
static DEVICE_ATTR(type, S_IRUGO, get_component_type, NULL);

static struct attribute *enclosure_component_attrs[] = {
	&dev_attr_fault.attr,
	&dev_attr_status.attr,
	&dev_attr_active.attr,
	&dev_attr_locate.attr,
	&dev_attr_type.attr,
	NULL
};
ATTRIBUTE_GROUPS(enclosure_component);

static int __init enclosure_init(void)
{
	int err;

	err = class_register(&enclosure_class);
	if (err)
		return err;

	return 0;
}

static void __exit enclosure_exit(void)
{
	class_unregister(&enclosure_class);
}

module_init(enclosure_init);
module_exit(enclosure_exit);

MODULE_AUTHOR("James Bottomley");
MODULE_DESCRIPTION("Enclosure Services");
MODULE_LICENSE("GPL v2");
