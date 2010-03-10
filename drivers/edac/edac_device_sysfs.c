/*
 * file for managing the edac_device class of devices for EDAC
 *
 * (C) 2007 SoftwareBitMaker (http://www.softwarebitmaker.com)
 *
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written Doug Thompson <norsk5@xmission.com>
 *
 */

#include <linux/ctype.h>
#include <linux/module.h>

#include "edac_core.h"
#include "edac_module.h"

#define EDAC_DEVICE_SYMLINK	"device"

#define to_edacdev(k) container_of(k, struct edac_device_ctl_info, kobj)
#define to_edacdev_attr(a) container_of(a, struct edacdev_attribute, attr)


/*
 * Set of edac_device_ctl_info attribute store/show functions
 */

/* 'log_ue' */
static ssize_t edac_device_ctl_log_ue_show(struct edac_device_ctl_info
					*ctl_info, char *data)
{
	return sprintf(data, "%u\n", ctl_info->log_ue);
}

static ssize_t edac_device_ctl_log_ue_store(struct edac_device_ctl_info
					*ctl_info, const char *data,
					size_t count)
{
	/* if parameter is zero, turn off flag, if non-zero turn on flag */
	ctl_info->log_ue = (simple_strtoul(data, NULL, 0) != 0);

	return count;
}

/* 'log_ce' */
static ssize_t edac_device_ctl_log_ce_show(struct edac_device_ctl_info
					*ctl_info, char *data)
{
	return sprintf(data, "%u\n", ctl_info->log_ce);
}

static ssize_t edac_device_ctl_log_ce_store(struct edac_device_ctl_info
					*ctl_info, const char *data,
					size_t count)
{
	/* if parameter is zero, turn off flag, if non-zero turn on flag */
	ctl_info->log_ce = (simple_strtoul(data, NULL, 0) != 0);

	return count;
}

/* 'panic_on_ue' */
static ssize_t edac_device_ctl_panic_on_ue_show(struct edac_device_ctl_info
						*ctl_info, char *data)
{
	return sprintf(data, "%u\n", ctl_info->panic_on_ue);
}

static ssize_t edac_device_ctl_panic_on_ue_store(struct edac_device_ctl_info
						 *ctl_info, const char *data,
						 size_t count)
{
	/* if parameter is zero, turn off flag, if non-zero turn on flag */
	ctl_info->panic_on_ue = (simple_strtoul(data, NULL, 0) != 0);

	return count;
}

/* 'poll_msec' show and store functions*/
static ssize_t edac_device_ctl_poll_msec_show(struct edac_device_ctl_info
					*ctl_info, char *data)
{
	return sprintf(data, "%u\n", ctl_info->poll_msec);
}

static ssize_t edac_device_ctl_poll_msec_store(struct edac_device_ctl_info
					*ctl_info, const char *data,
					size_t count)
{
	unsigned long value;

	/* get the value and enforce that it is non-zero, must be at least
	 * one millisecond for the delay period, between scans
	 * Then cancel last outstanding delay for the work request
	 * and set a new one.
	 */
	value = simple_strtoul(data, NULL, 0);
	edac_device_reset_delay_period(ctl_info, value);

	return count;
}

/* edac_device_ctl_info specific attribute structure */
struct ctl_info_attribute {
	struct attribute attr;
	ssize_t(*show) (struct edac_device_ctl_info *, char *);
	ssize_t(*store) (struct edac_device_ctl_info *, const char *, size_t);
};

#define to_ctl_info(k) container_of(k, struct edac_device_ctl_info, kobj)
#define to_ctl_info_attr(a) container_of(a,struct ctl_info_attribute,attr)

/* Function to 'show' fields from the edac_dev 'ctl_info' structure */
static ssize_t edac_dev_ctl_info_show(struct kobject *kobj,
				struct attribute *attr, char *buffer)
{
	struct edac_device_ctl_info *edac_dev = to_ctl_info(kobj);
	struct ctl_info_attribute *ctl_info_attr = to_ctl_info_attr(attr);

	if (ctl_info_attr->show)
		return ctl_info_attr->show(edac_dev, buffer);
	return -EIO;
}

/* Function to 'store' fields into the edac_dev 'ctl_info' structure */
static ssize_t edac_dev_ctl_info_store(struct kobject *kobj,
				struct attribute *attr,
				const char *buffer, size_t count)
{
	struct edac_device_ctl_info *edac_dev = to_ctl_info(kobj);
	struct ctl_info_attribute *ctl_info_attr = to_ctl_info_attr(attr);

	if (ctl_info_attr->store)
		return ctl_info_attr->store(edac_dev, buffer, count);
	return -EIO;
}

/* edac_dev file operations for an 'ctl_info' */
static const struct sysfs_ops device_ctl_info_ops = {
	.show = edac_dev_ctl_info_show,
	.store = edac_dev_ctl_info_store
};

#define CTL_INFO_ATTR(_name,_mode,_show,_store)        \
static struct ctl_info_attribute attr_ctl_info_##_name = {      \
	.attr = {.name = __stringify(_name), .mode = _mode },   \
	.show   = _show,                                        \
	.store  = _store,                                       \
};

/* Declare the various ctl_info attributes here and their respective ops */
CTL_INFO_ATTR(log_ue, S_IRUGO | S_IWUSR,
	edac_device_ctl_log_ue_show, edac_device_ctl_log_ue_store);
CTL_INFO_ATTR(log_ce, S_IRUGO | S_IWUSR,
	edac_device_ctl_log_ce_show, edac_device_ctl_log_ce_store);
CTL_INFO_ATTR(panic_on_ue, S_IRUGO | S_IWUSR,
	edac_device_ctl_panic_on_ue_show,
	edac_device_ctl_panic_on_ue_store);
CTL_INFO_ATTR(poll_msec, S_IRUGO | S_IWUSR,
	edac_device_ctl_poll_msec_show, edac_device_ctl_poll_msec_store);

/* Base Attributes of the EDAC_DEVICE ECC object */
static struct ctl_info_attribute *device_ctrl_attr[] = {
	&attr_ctl_info_panic_on_ue,
	&attr_ctl_info_log_ue,
	&attr_ctl_info_log_ce,
	&attr_ctl_info_poll_msec,
	NULL,
};

/*
 * edac_device_ctrl_master_release
 *
 *	called when the reference count for the 'main' kobj
 *	for a edac_device control struct reaches zero
 *
 *	Reference count model:
 *		One 'main' kobject for each control structure allocated.
 *		That main kobj is initially set to one AND
 *		the reference count for the EDAC 'core' module is
 *		bumped by one, thus added 'keep in memory' dependency.
 *
 *		Each new internal kobj (in instances and blocks) then
 *		bumps the 'main' kobject.
 *
 *		When they are released their release functions decrement
 *		the 'main' kobj.
 *
 *		When the main kobj reaches zero (0) then THIS function
 *		is called which then decrements the EDAC 'core' module.
 *		When the module reference count reaches zero then the
 *		module no longer has dependency on keeping the release
 *		function code in memory and module can be unloaded.
 *
 *		This will support several control objects as well, each
 *		with its own 'main' kobj.
 */
static void edac_device_ctrl_master_release(struct kobject *kobj)
{
	struct edac_device_ctl_info *edac_dev = to_edacdev(kobj);

	debugf4("%s() control index=%d\n", __func__, edac_dev->dev_idx);

	/* decrement the EDAC CORE module ref count */
	module_put(edac_dev->owner);

	/* free the control struct containing the 'main' kobj
	 * passed in to this routine
	 */
	kfree(edac_dev);
}

/* ktype for the main (master) kobject */
static struct kobj_type ktype_device_ctrl = {
	.release = edac_device_ctrl_master_release,
	.sysfs_ops = &device_ctl_info_ops,
	.default_attrs = (struct attribute **)device_ctrl_attr,
};

/*
 * edac_device_register_sysfs_main_kobj
 *
 *	perform the high level setup for the new edac_device instance
 *
 * Return:  0 SUCCESS
 *         !0 FAILURE
 */
int edac_device_register_sysfs_main_kobj(struct edac_device_ctl_info *edac_dev)
{
	struct sysdev_class *edac_class;
	int err;

	debugf1("%s()\n", __func__);

	/* get the /sys/devices/system/edac reference */
	edac_class = edac_get_edac_class();
	if (edac_class == NULL) {
		debugf1("%s() no edac_class error\n", __func__);
		err = -ENODEV;
		goto err_out;
	}

	/* Point to the 'edac_class' this instance 'reports' to */
	edac_dev->edac_class = edac_class;

	/* Init the devices's kobject */
	memset(&edac_dev->kobj, 0, sizeof(struct kobject));

	/* Record which module 'owns' this control structure
	 * and bump the ref count of the module
	 */
	edac_dev->owner = THIS_MODULE;

	if (!try_module_get(edac_dev->owner)) {
		err = -ENODEV;
		goto err_out;
	}

	/* register */
	err = kobject_init_and_add(&edac_dev->kobj, &ktype_device_ctrl,
				   &edac_class->kset.kobj,
				   "%s", edac_dev->name);
	if (err) {
		debugf1("%s()Failed to register '.../edac/%s'\n",
			__func__, edac_dev->name);
		goto err_kobj_reg;
	}
	kobject_uevent(&edac_dev->kobj, KOBJ_ADD);

	/* At this point, to 'free' the control struct,
	 * edac_device_unregister_sysfs_main_kobj() must be used
	 */

	debugf4("%s() Registered '.../edac/%s' kobject\n",
		__func__, edac_dev->name);

	return 0;

	/* Error exit stack */
err_kobj_reg:
	module_put(edac_dev->owner);

err_out:
	return err;
}

/*
 * edac_device_unregister_sysfs_main_kobj:
 *	the '..../edac/<name>' kobject
 */
void edac_device_unregister_sysfs_main_kobj(
					struct edac_device_ctl_info *edac_dev)
{
	debugf0("%s()\n", __func__);
	debugf4("%s() name of kobject is: %s\n",
		__func__, kobject_name(&edac_dev->kobj));

	/*
	 * Unregister the edac device's kobject and
	 * allow for reference count to reach 0 at which point
	 * the callback will be called to:
	 *   a) module_put() this module
	 *   b) 'kfree' the memory
	 */
	kobject_put(&edac_dev->kobj);
}

/* edac_dev -> instance information */

/*
 * Set of low-level instance attribute show functions
 */
static ssize_t instance_ue_count_show(struct edac_device_instance *instance,
				char *data)
{
	return sprintf(data, "%u\n", instance->counters.ue_count);
}

static ssize_t instance_ce_count_show(struct edac_device_instance *instance,
				char *data)
{
	return sprintf(data, "%u\n", instance->counters.ce_count);
}

#define to_instance(k) container_of(k, struct edac_device_instance, kobj)
#define to_instance_attr(a) container_of(a,struct instance_attribute,attr)

/* DEVICE instance kobject release() function */
static void edac_device_ctrl_instance_release(struct kobject *kobj)
{
	struct edac_device_instance *instance;

	debugf1("%s()\n", __func__);

	/* map from this kobj to the main control struct
	 * and then dec the main kobj count
	 */
	instance = to_instance(kobj);
	kobject_put(&instance->ctl->kobj);
}

/* instance specific attribute structure */
struct instance_attribute {
	struct attribute attr;
	ssize_t(*show) (struct edac_device_instance *, char *);
	ssize_t(*store) (struct edac_device_instance *, const char *, size_t);
};

/* Function to 'show' fields from the edac_dev 'instance' structure */
static ssize_t edac_dev_instance_show(struct kobject *kobj,
				struct attribute *attr, char *buffer)
{
	struct edac_device_instance *instance = to_instance(kobj);
	struct instance_attribute *instance_attr = to_instance_attr(attr);

	if (instance_attr->show)
		return instance_attr->show(instance, buffer);
	return -EIO;
}

/* Function to 'store' fields into the edac_dev 'instance' structure */
static ssize_t edac_dev_instance_store(struct kobject *kobj,
				struct attribute *attr,
				const char *buffer, size_t count)
{
	struct edac_device_instance *instance = to_instance(kobj);
	struct instance_attribute *instance_attr = to_instance_attr(attr);

	if (instance_attr->store)
		return instance_attr->store(instance, buffer, count);
	return -EIO;
}

/* edac_dev file operations for an 'instance' */
static const struct sysfs_ops device_instance_ops = {
	.show = edac_dev_instance_show,
	.store = edac_dev_instance_store
};

#define INSTANCE_ATTR(_name,_mode,_show,_store)        \
static struct instance_attribute attr_instance_##_name = {      \
	.attr = {.name = __stringify(_name), .mode = _mode },   \
	.show   = _show,                                        \
	.store  = _store,                                       \
};

/*
 * Define attributes visible for the edac_device instance object
 *	Each contains a pointer to a show and an optional set
 *	function pointer that does the low level output/input
 */
INSTANCE_ATTR(ce_count, S_IRUGO, instance_ce_count_show, NULL);
INSTANCE_ATTR(ue_count, S_IRUGO, instance_ue_count_show, NULL);

/* list of edac_dev 'instance' attributes */
static struct instance_attribute *device_instance_attr[] = {
	&attr_instance_ce_count,
	&attr_instance_ue_count,
	NULL,
};

/* The 'ktype' for each edac_dev 'instance' */
static struct kobj_type ktype_instance_ctrl = {
	.release = edac_device_ctrl_instance_release,
	.sysfs_ops = &device_instance_ops,
	.default_attrs = (struct attribute **)device_instance_attr,
};

/* edac_dev -> instance -> block information */

#define to_block(k) container_of(k, struct edac_device_block, kobj)
#define to_block_attr(a) \
	container_of(a, struct edac_dev_sysfs_block_attribute, attr)

/*
 * Set of low-level block attribute show functions
 */
static ssize_t block_ue_count_show(struct kobject *kobj,
					struct attribute *attr, char *data)
{
	struct edac_device_block *block = to_block(kobj);

	return sprintf(data, "%u\n", block->counters.ue_count);
}

static ssize_t block_ce_count_show(struct kobject *kobj,
					struct attribute *attr, char *data)
{
	struct edac_device_block *block = to_block(kobj);

	return sprintf(data, "%u\n", block->counters.ce_count);
}

/* DEVICE block kobject release() function */
static void edac_device_ctrl_block_release(struct kobject *kobj)
{
	struct edac_device_block *block;

	debugf1("%s()\n", __func__);

	/* get the container of the kobj */
	block = to_block(kobj);

	/* map from 'block kobj' to 'block->instance->controller->main_kobj'
	 * now 'release' the block kobject
	 */
	kobject_put(&block->instance->ctl->kobj);
}


/* Function to 'show' fields from the edac_dev 'block' structure */
static ssize_t edac_dev_block_show(struct kobject *kobj,
				struct attribute *attr, char *buffer)
{
	struct edac_dev_sysfs_block_attribute *block_attr =
						to_block_attr(attr);

	if (block_attr->show)
		return block_attr->show(kobj, attr, buffer);
	return -EIO;
}

/* Function to 'store' fields into the edac_dev 'block' structure */
static ssize_t edac_dev_block_store(struct kobject *kobj,
				struct attribute *attr,
				const char *buffer, size_t count)
{
	struct edac_dev_sysfs_block_attribute *block_attr;

	block_attr = to_block_attr(attr);

	if (block_attr->store)
		return block_attr->store(kobj, attr, buffer, count);
	return -EIO;
}

/* edac_dev file operations for a 'block' */
static const struct sysfs_ops device_block_ops = {
	.show = edac_dev_block_show,
	.store = edac_dev_block_store
};

#define BLOCK_ATTR(_name,_mode,_show,_store)        \
static struct edac_dev_sysfs_block_attribute attr_block_##_name = {	\
	.attr = {.name = __stringify(_name), .mode = _mode },   \
	.show   = _show,                                        \
	.store  = _store,                                       \
};

BLOCK_ATTR(ce_count, S_IRUGO, block_ce_count_show, NULL);
BLOCK_ATTR(ue_count, S_IRUGO, block_ue_count_show, NULL);

/* list of edac_dev 'block' attributes */
static struct edac_dev_sysfs_block_attribute *device_block_attr[] = {
	&attr_block_ce_count,
	&attr_block_ue_count,
	NULL,
};

/* The 'ktype' for each edac_dev 'block' */
static struct kobj_type ktype_block_ctrl = {
	.release = edac_device_ctrl_block_release,
	.sysfs_ops = &device_block_ops,
	.default_attrs = (struct attribute **)device_block_attr,
};

/* block ctor/dtor  code */

/*
 * edac_device_create_block
 */
static int edac_device_create_block(struct edac_device_ctl_info *edac_dev,
				struct edac_device_instance *instance,
				struct edac_device_block *block)
{
	int i;
	int err;
	struct edac_dev_sysfs_block_attribute *sysfs_attrib;
	struct kobject *main_kobj;

	debugf4("%s() Instance '%s' inst_p=%p  block '%s'  block_p=%p\n",
		__func__, instance->name, instance, block->name, block);
	debugf4("%s() block kobj=%p  block kobj->parent=%p\n",
		__func__, &block->kobj, &block->kobj.parent);

	/* init this block's kobject */
	memset(&block->kobj, 0, sizeof(struct kobject));

	/* bump the main kobject's reference count for this controller
	 * and this instance is dependant on the main
	 */
	main_kobj = kobject_get(&edac_dev->kobj);
	if (!main_kobj) {
		err = -ENODEV;
		goto err_out;
	}

	/* Add this block's kobject */
	err = kobject_init_and_add(&block->kobj, &ktype_block_ctrl,
				   &instance->kobj,
				   "%s", block->name);
	if (err) {
		debugf1("%s() Failed to register instance '%s'\n",
			__func__, block->name);
		kobject_put(main_kobj);
		err = -ENODEV;
		goto err_out;
	}

	/* If there are driver level block attributes, then added them
	 * to the block kobject
	 */
	sysfs_attrib = block->block_attributes;
	if (sysfs_attrib && block->nr_attribs) {
		for (i = 0; i < block->nr_attribs; i++, sysfs_attrib++) {

			debugf4("%s() creating block attrib='%s' "
				"attrib->%p to kobj=%p\n",
				__func__,
				sysfs_attrib->attr.name,
				sysfs_attrib, &block->kobj);

			/* Create each block_attribute file */
			err = sysfs_create_file(&block->kobj,
				&sysfs_attrib->attr);
			if (err)
				goto err_on_attrib;
		}
	}
	kobject_uevent(&block->kobj, KOBJ_ADD);

	return 0;

	/* Error unwind stack */
err_on_attrib:
	kobject_put(&block->kobj);

err_out:
	return err;
}

/*
 * edac_device_delete_block(edac_dev,block);
 */
static void edac_device_delete_block(struct edac_device_ctl_info *edac_dev,
				struct edac_device_block *block)
{
	struct edac_dev_sysfs_block_attribute *sysfs_attrib;
	int i;

	/* if this block has 'attributes' then we need to iterate over the list
	 * and 'remove' the attributes on this block
	 */
	sysfs_attrib = block->block_attributes;
	if (sysfs_attrib && block->nr_attribs) {
		for (i = 0; i < block->nr_attribs; i++, sysfs_attrib++) {

			/* remove each block_attrib file */
			sysfs_remove_file(&block->kobj,
				(struct attribute *) sysfs_attrib);
		}
	}

	/* unregister this block's kobject, SEE:
	 *	edac_device_ctrl_block_release() callback operation
	 */
	kobject_put(&block->kobj);
}

/* instance ctor/dtor code */

/*
 * edac_device_create_instance
 *	create just one instance of an edac_device 'instance'
 */
static int edac_device_create_instance(struct edac_device_ctl_info *edac_dev,
				int idx)
{
	int i, j;
	int err;
	struct edac_device_instance *instance;
	struct kobject *main_kobj;

	instance = &edac_dev->instances[idx];

	/* Init the instance's kobject */
	memset(&instance->kobj, 0, sizeof(struct kobject));

	instance->ctl = edac_dev;

	/* bump the main kobject's reference count for this controller
	 * and this instance is dependant on the main
	 */
	main_kobj = kobject_get(&edac_dev->kobj);
	if (!main_kobj) {
		err = -ENODEV;
		goto err_out;
	}

	/* Formally register this instance's kobject under the edac_device */
	err = kobject_init_and_add(&instance->kobj, &ktype_instance_ctrl,
				   &edac_dev->kobj, "%s", instance->name);
	if (err != 0) {
		debugf2("%s() Failed to register instance '%s'\n",
			__func__, instance->name);
		kobject_put(main_kobj);
		goto err_out;
	}

	debugf4("%s() now register '%d' blocks for instance %d\n",
		__func__, instance->nr_blocks, idx);

	/* register all blocks of this instance */
	for (i = 0; i < instance->nr_blocks; i++) {
		err = edac_device_create_block(edac_dev, instance,
						&instance->blocks[i]);
		if (err) {
			/* If any fail, remove all previous ones */
			for (j = 0; j < i; j++)
				edac_device_delete_block(edac_dev,
							&instance->blocks[j]);
			goto err_release_instance_kobj;
		}
	}
	kobject_uevent(&instance->kobj, KOBJ_ADD);

	debugf4("%s() Registered instance %d '%s' kobject\n",
		__func__, idx, instance->name);

	return 0;

	/* error unwind stack */
err_release_instance_kobj:
	kobject_put(&instance->kobj);

err_out:
	return err;
}

/*
 * edac_device_remove_instance
 *	remove an edac_device instance
 */
static void edac_device_delete_instance(struct edac_device_ctl_info *edac_dev,
					int idx)
{
	struct edac_device_instance *instance;
	int i;

	instance = &edac_dev->instances[idx];

	/* unregister all blocks in this instance */
	for (i = 0; i < instance->nr_blocks; i++)
		edac_device_delete_block(edac_dev, &instance->blocks[i]);

	/* unregister this instance's kobject, SEE:
	 *	edac_device_ctrl_instance_release() for callback operation
	 */
	kobject_put(&instance->kobj);
}

/*
 * edac_device_create_instances
 *	create the first level of 'instances' for this device
 *	(ie  'cache' might have 'cache0', 'cache1', 'cache2', etc
 */
static int edac_device_create_instances(struct edac_device_ctl_info *edac_dev)
{
	int i, j;
	int err;

	debugf0("%s()\n", __func__);

	/* iterate over creation of the instances */
	for (i = 0; i < edac_dev->nr_instances; i++) {
		err = edac_device_create_instance(edac_dev, i);
		if (err) {
			/* unwind previous instances on error */
			for (j = 0; j < i; j++)
				edac_device_delete_instance(edac_dev, j);
			return err;
		}
	}

	return 0;
}

/*
 * edac_device_delete_instances(edac_dev);
 *	unregister all the kobjects of the instances
 */
static void edac_device_delete_instances(struct edac_device_ctl_info *edac_dev)
{
	int i;

	/* iterate over creation of the instances */
	for (i = 0; i < edac_dev->nr_instances; i++)
		edac_device_delete_instance(edac_dev, i);
}

/* edac_dev sysfs ctor/dtor  code */

/*
 * edac_device_add_main_sysfs_attributes
 *	add some attributes to this instance's main kobject
 */
static int edac_device_add_main_sysfs_attributes(
			struct edac_device_ctl_info *edac_dev)
{
	struct edac_dev_sysfs_attribute *sysfs_attrib;
	int err = 0;

	sysfs_attrib = edac_dev->sysfs_attributes;
	if (sysfs_attrib) {
		/* iterate over the array and create an attribute for each
		 * entry in the list
		 */
		while (sysfs_attrib->attr.name != NULL) {
			err = sysfs_create_file(&edac_dev->kobj,
				(struct attribute*) sysfs_attrib);
			if (err)
				goto err_out;

			sysfs_attrib++;
		}
	}

err_out:
	return err;
}

/*
 * edac_device_remove_main_sysfs_attributes
 *	remove any attributes to this instance's main kobject
 */
static void edac_device_remove_main_sysfs_attributes(
			struct edac_device_ctl_info *edac_dev)
{
	struct edac_dev_sysfs_attribute *sysfs_attrib;

	/* if there are main attributes, defined, remove them. First,
	 * point to the start of the array and iterate over it
	 * removing each attribute listed from this device's instance's kobject
	 */
	sysfs_attrib = edac_dev->sysfs_attributes;
	if (sysfs_attrib) {
		while (sysfs_attrib->attr.name != NULL) {
			sysfs_remove_file(&edac_dev->kobj,
					(struct attribute *) sysfs_attrib);
			sysfs_attrib++;
		}
	}
}

/*
 * edac_device_create_sysfs() Constructor
 *
 * accept a created edac_device control structure
 * and 'export' it to sysfs. The 'main' kobj should already have been
 * created. 'instance' and 'block' kobjects should be registered
 * along with any 'block' attributes from the low driver. In addition,
 * the main attributes (if any) are connected to the main kobject of
 * the control structure.
 *
 * Return:
 *	0	Success
 *	!0	Failure
 */
int edac_device_create_sysfs(struct edac_device_ctl_info *edac_dev)
{
	int err;
	struct kobject *edac_kobj = &edac_dev->kobj;

	debugf0("%s() idx=%d\n", __func__, edac_dev->dev_idx);

	/*  go create any main attributes callers wants */
	err = edac_device_add_main_sysfs_attributes(edac_dev);
	if (err) {
		debugf0("%s() failed to add sysfs attribs\n", __func__);
		goto err_out;
	}

	/* create a symlink from the edac device
	 * to the platform 'device' being used for this
	 */
	err = sysfs_create_link(edac_kobj,
				&edac_dev->dev->kobj, EDAC_DEVICE_SYMLINK);
	if (err) {
		debugf0("%s() sysfs_create_link() returned err= %d\n",
			__func__, err);
		goto err_remove_main_attribs;
	}

	/* Create the first level instance directories
	 * In turn, the nested blocks beneath the instances will
	 * be registered as well
	 */
	err = edac_device_create_instances(edac_dev);
	if (err) {
		debugf0("%s() edac_device_create_instances() "
			"returned err= %d\n", __func__, err);
		goto err_remove_link;
	}


	debugf4("%s() create-instances done, idx=%d\n",
		__func__, edac_dev->dev_idx);

	return 0;

	/* Error unwind stack */
err_remove_link:
	/* remove the sym link */
	sysfs_remove_link(&edac_dev->kobj, EDAC_DEVICE_SYMLINK);

err_remove_main_attribs:
	edac_device_remove_main_sysfs_attributes(edac_dev);

err_out:
	return err;
}

/*
 * edac_device_remove_sysfs() destructor
 *
 * given an edac_device struct, tear down the kobject resources
 */
void edac_device_remove_sysfs(struct edac_device_ctl_info *edac_dev)
{
	debugf0("%s()\n", __func__);

	/* remove any main attributes for this device */
	edac_device_remove_main_sysfs_attributes(edac_dev);

	/* remove the device sym link */
	sysfs_remove_link(&edac_dev->kobj, EDAC_DEVICE_SYMLINK);

	/* walk the instance/block kobject tree, deconstructing it */
	edac_device_delete_instances(edac_dev);
}
