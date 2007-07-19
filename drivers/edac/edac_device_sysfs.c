/*
 * file for managing the edac_device class of devices for EDAC
 *
 * (C) 2007 SoftwareBitMaker(http://www.softwarebitmaker.com)
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written Doug Thompson <norsk5@xmission.com>
 *
 */

#include <linux/ctype.h>

#include "edac_core.h"
#include "edac_module.h"

#define EDAC_DEVICE_SYMLINK	"device"

#define to_edacdev(k) container_of(k, struct edac_device_ctl_info, kobj)
#define to_edacdev_attr(a) container_of(a, struct edacdev_attribute, attr)

/************************** edac_device sysfs code and data **************/

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
static struct sysfs_ops device_ctl_info_ops = {
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

/* Main DEVICE kobject release() function */
static void edac_device_ctrl_master_release(struct kobject *kobj)
{
	struct edac_device_ctl_info *edac_dev;

	edac_dev = to_edacdev(kobj);

	debugf1("%s()\n", __func__);
	complete(&edac_dev->kobj_complete);
}

static struct kobj_type ktype_device_ctrl = {
	.release = edac_device_ctrl_master_release,
	.sysfs_ops = &device_ctl_info_ops,
	.default_attrs = (struct attribute **)device_ctrl_attr,
};

/**************** edac_device main kobj ctor/dtor code *********************/

/*
 * edac_device_register_main_kobj
 *
 *	perform the high level setup for the new edac_device instance
 *
 * Return:  0 SUCCESS
 *         !0 FAILURE
 */
static int edac_device_register_main_kobj(struct edac_device_ctl_info *edac_dev)
{
	int err = 0;
	struct sysdev_class *edac_class;

	debugf1("%s()\n", __func__);

	/* get the /sys/devices/system/edac reference */
	edac_class = edac_get_edac_class();
	if (edac_class == NULL) {
		debugf1("%s() no edac_class error=%d\n", __func__, err);
		return err;
	}

	/* Point to the 'edac_class' this instance 'reports' to */
	edac_dev->edac_class = edac_class;

	/* Init the devices's kobject */
	memset(&edac_dev->kobj, 0, sizeof(struct kobject));
	edac_dev->kobj.ktype = &ktype_device_ctrl;

	/* set this new device under the edac_class kobject */
	edac_dev->kobj.parent = &edac_class->kset.kobj;

	/* generate sysfs "..../edac/<name>"   */
	debugf1("%s() set name of kobject to: %s\n", __func__, edac_dev->name);
	err = kobject_set_name(&edac_dev->kobj, "%s", edac_dev->name);
	if (err)
		return err;
	err = kobject_register(&edac_dev->kobj);
	if (err) {
		debugf1("%s()Failed to register '.../edac/%s'\n",
			__func__, edac_dev->name);
		return err;
	}

	debugf1("%s() Registered '.../edac/%s' kobject\n",
		__func__, edac_dev->name);

	return 0;
}

/*
 * edac_device_unregister_main_kobj:
 *	the '..../edac/<name>' kobject
 */
static void edac_device_unregister_main_kobj(struct edac_device_ctl_info
					*edac_dev)
{
	debugf0("%s()\n", __func__);
	debugf1("%s() name of kobject is: %s\n",
		__func__, kobject_name(&edac_dev->kobj));

	init_completion(&edac_dev->kobj_complete);

	/*
	 * Unregister the edac device's kobject and
	 * wait for reference count to reach 0.
	 */
	kobject_unregister(&edac_dev->kobj);
	wait_for_completion(&edac_dev->kobj_complete);
}

/*************** edac_dev -> instance information ***********/

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

	instance = to_instance(kobj);
	complete(&instance->kobj_complete);
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
static struct sysfs_ops device_instance_ops = {
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

/*************** edac_dev -> instance -> block information *********/

/*
 * Set of low-level block attribute show functions
 */
static ssize_t block_ue_count_show(struct edac_device_block *block, char *data)
{
	return sprintf(data, "%u\n", block->counters.ue_count);
}

static ssize_t block_ce_count_show(struct edac_device_block *block, char *data)
{
	return sprintf(data, "%u\n", block->counters.ce_count);
}

#define to_block(k) container_of(k, struct edac_device_block, kobj)
#define to_block_attr(a) container_of(a,struct block_attribute,attr)

/* DEVICE block kobject release() function */
static void edac_device_ctrl_block_release(struct kobject *kobj)
{
	struct edac_device_block *block;

	debugf1("%s()\n", __func__);

	block = to_block(kobj);
	complete(&block->kobj_complete);
}

/* block specific attribute structure */
struct block_attribute {
	struct attribute attr;
	 ssize_t(*show) (struct edac_device_block *, char *);
	 ssize_t(*store) (struct edac_device_block *, const char *, size_t);
};

/* Function to 'show' fields from the edac_dev 'block' structure */
static ssize_t edac_dev_block_show(struct kobject *kobj,
				struct attribute *attr, char *buffer)
{
	struct edac_device_block *block = to_block(kobj);
	struct block_attribute *block_attr = to_block_attr(attr);

	if (block_attr->show)
		return block_attr->show(block, buffer);
	return -EIO;
}

/* Function to 'store' fields into the edac_dev 'block' structure */
static ssize_t edac_dev_block_store(struct kobject *kobj,
				struct attribute *attr,
				const char *buffer, size_t count)
{
	struct edac_device_block *block = to_block(kobj);
	struct block_attribute *block_attr = to_block_attr(attr);

	if (block_attr->store)
		return block_attr->store(block, buffer, count);
	return -EIO;
}

/* edac_dev file operations for a 'block' */
static struct sysfs_ops device_block_ops = {
	.show = edac_dev_block_show,
	.store = edac_dev_block_store
};

#define BLOCK_ATTR(_name,_mode,_show,_store)        \
static struct block_attribute attr_block_##_name = {                       \
	.attr = {.name = __stringify(_name), .mode = _mode },   \
	.show   = _show,                                        \
	.store  = _store,                                       \
};

BLOCK_ATTR(ce_count, S_IRUGO, block_ce_count_show, NULL);
BLOCK_ATTR(ue_count, S_IRUGO, block_ue_count_show, NULL);

/* list of edac_dev 'block' attributes */
static struct block_attribute *device_block_attr[] = {
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

/************** block ctor/dtor  code ************/

/*
 * edac_device_create_block
 */
static int edac_device_create_block(struct edac_device_ctl_info *edac_dev,
				struct edac_device_instance *instance,
				int idx)
{
	int i;
	int err;
	struct edac_device_block *block;
	struct edac_dev_sysfs_block_attribute *sysfs_attrib;

	block = &instance->blocks[idx];

	debugf1("%s() Instance '%s' block[%d] '%s'\n",
		__func__, instance->name, idx, block->name);

	/* init this block's kobject */
	memset(&block->kobj, 0, sizeof(struct kobject));
	block->kobj.parent = &instance->kobj;
	block->kobj.ktype = &ktype_block_ctrl;

	err = kobject_set_name(&block->kobj, "%s", block->name);
	if (err)
		return err;

	err = kobject_register(&block->kobj);
	if (err) {
		debugf1("%s()Failed to register instance '%s'\n",
			__func__, block->name);
		return err;
	}

	/* If there are driver level block attributes, then added them
	 * to the block kobject
	 */
	sysfs_attrib = block->block_attributes;
	if (sysfs_attrib != NULL) {
		for (i = 0; i < block->nr_attribs; i++) {
			err = sysfs_create_file(&block->kobj,
				(struct attribute *) &sysfs_attrib[i]);
			if (err)
				goto err_on_attrib;

			sysfs_attrib++;
		}
	}

	return 0;

err_on_attrib:
	kobject_unregister(&block->kobj);

	return err;
}

/*
 * edac_device_delete_block(edac_dev,j);
 */
static void edac_device_delete_block(struct edac_device_ctl_info *edac_dev,
				struct edac_device_instance *instance,
				int idx)
{
	struct edac_device_block *block;

	block = &instance->blocks[idx];

	/* unregister this block's kobject */
	init_completion(&block->kobj_complete);
	kobject_unregister(&block->kobj);
	wait_for_completion(&block->kobj_complete);
}

/************** instance ctor/dtor  code ************/

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

	instance = &edac_dev->instances[idx];

	/* Init the instance's kobject */
	memset(&instance->kobj, 0, sizeof(struct kobject));

	/* set this new device under the edac_device main kobject */
	instance->kobj.parent = &edac_dev->kobj;
	instance->kobj.ktype = &ktype_instance_ctrl;

	err = kobject_set_name(&instance->kobj, "%s", instance->name);
	if (err)
		return err;

	err = kobject_register(&instance->kobj);
	if (err != 0) {
		debugf2("%s() Failed to register instance '%s'\n",
			__func__, instance->name);
		return err;
	}

	debugf1("%s() now register '%d' blocks for instance %d\n",
		__func__, instance->nr_blocks, idx);

	/* register all blocks of this instance */
	for (i = 0; i < instance->nr_blocks; i++) {
		err = edac_device_create_block(edac_dev, instance, i);
		if (err) {
			for (j = 0; j < i; j++)
				edac_device_delete_block(edac_dev, instance, j);
			return err;
		}
	}

	debugf1("%s() Registered instance %d '%s' kobject\n",
		__func__, idx, instance->name);

	return 0;
}

/*
 * edac_device_remove_instance
 *	remove an edac_device instance
 */
static void edac_device_delete_instance(struct edac_device_ctl_info *edac_dev,
					int idx)
{
	int i;
	struct edac_device_instance *instance;

	instance = &edac_dev->instances[idx];

	/* unregister all blocks in this instance */
	for (i = 0; i < instance->nr_blocks; i++)
		edac_device_delete_block(edac_dev, instance, i);

	/* unregister this instance's kobject */
	init_completion(&instance->kobj_complete);
	kobject_unregister(&instance->kobj);
	wait_for_completion(&instance->kobj_complete);
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

/******************* edac_dev sysfs ctor/dtor  code *************/

/*
 * edac_device_add_sysfs_attributes
 *	add some attributes to this instance's main kobject
 */
static int edac_device_add_sysfs_attributes(
			struct edac_device_ctl_info *edac_dev)
{
	int err;
	struct edac_dev_sysfs_attribute *sysfs_attrib;

	/* point to the start of the array and iterate over it
	 * adding each attribute listed to this mci instance's kobject
	 */
	sysfs_attrib = edac_dev->sysfs_attributes;

	while (sysfs_attrib->attr.name != NULL) {
		err = sysfs_create_file(&edac_dev->kobj,
				(struct attribute*) sysfs_attrib);
		if (err)
			return err;

		sysfs_attrib++;
	}

	return 0;
}

/*
 * edac_device_create_sysfs() Constructor
 *
 * Create a new edac_device kobject instance,
 *
 * Return:
 *	0	Success
 *	!0	Failure
 */
int edac_device_create_sysfs(struct edac_device_ctl_info *edac_dev)
{
	int err;
	struct kobject *edac_kobj = &edac_dev->kobj;

	/* register this instance's main kobj with the edac class kobj */
	err = edac_device_register_main_kobj(edac_dev);
	if (err)
		return err;

	debugf0("%s() idx=%d\n", __func__, edac_dev->dev_idx);

	/* If the low level driver requests some sysfs entries
	 * then go create them here
	 */
	if (edac_dev->sysfs_attributes != NULL) {
		err = edac_device_add_sysfs_attributes(edac_dev);
		if (err) {
			debugf0("%s() failed to add sysfs attribs\n",
				__func__);
			goto err_unreg_object;
		}
	}

	/* create a symlink from the edac device
	 * to the platform 'device' being used for this
	 */
	err = sysfs_create_link(edac_kobj,
				&edac_dev->dev->kobj, EDAC_DEVICE_SYMLINK);
	if (err) {
		debugf0("%s() sysfs_create_link() returned err= %d\n",
			__func__, err);
		goto err_unreg_object;
	}

	debugf0("%s() calling create-instances, idx=%d\n",
		__func__, edac_dev->dev_idx);

	/* Create the first level instance directories */
	err = edac_device_create_instances(edac_dev);
	if (err)
		goto err_remove_link;

	return 0;

	/* Error unwind stack */
err_remove_link:
	/* remove the sym link */
	sysfs_remove_link(&edac_dev->kobj, EDAC_DEVICE_SYMLINK);

err_unreg_object:
	edac_device_unregister_main_kobj(edac_dev);

	return err;
}

/*
 * edac_device_remove_sysfs() destructor
 *
 * remove a edac_device instance
 */
void edac_device_remove_sysfs(struct edac_device_ctl_info *edac_dev)
{
	debugf0("%s()\n", __func__);

	edac_device_delete_instances(edac_dev);

	/* remove the sym link */
	sysfs_remove_link(&edac_dev->kobj, EDAC_DEVICE_SYMLINK);

	/* unregister the instance's main kobj */
	edac_device_unregister_main_kobj(edac_dev);
}
