/*
 * sysfs.c - sysfs support
 *
 * (C) 2006-2007 Shaohua Li <shaohua.li@intel.com>
 *
 * This code is licenced under the GPL.
 */

#include <linux/kernel.h>
#include <linux/cpuidle.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/cpu.h>
#include <linux/completion.h>
#include <linux/capability.h>
#include <linux/device.h>
#include <linux/kobject.h>

#include "cpuidle.h"

static unsigned int sysfs_switch;
static int __init cpuidle_sysfs_setup(char *unused)
{
	sysfs_switch = 1;
	return 1;
}
__setup("cpuidle_sysfs_switch", cpuidle_sysfs_setup);

static ssize_t show_available_governors(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	ssize_t i = 0;
	struct cpuidle_governor *tmp;

	mutex_lock(&cpuidle_lock);
	list_for_each_entry(tmp, &cpuidle_governors, governor_list) {
		if (i >= (ssize_t) ((PAGE_SIZE/sizeof(char)) -
				    CPUIDLE_NAME_LEN - 2))
			goto out;
		i += scnprintf(&buf[i], CPUIDLE_NAME_LEN, "%s ", tmp->name);
	}

out:
	i+= sprintf(&buf[i], "\n");
	mutex_unlock(&cpuidle_lock);
	return i;
}

static ssize_t show_current_driver(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	ssize_t ret;
	struct cpuidle_driver *drv;

	spin_lock(&cpuidle_driver_lock);
	drv = cpuidle_get_driver();
	if (drv)
		ret = sprintf(buf, "%s\n", drv->name);
	else
		ret = sprintf(buf, "none\n");
	spin_unlock(&cpuidle_driver_lock);

	return ret;
}

static ssize_t show_current_governor(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	ssize_t ret;

	mutex_lock(&cpuidle_lock);
	if (cpuidle_curr_governor)
		ret = sprintf(buf, "%s\n", cpuidle_curr_governor->name);
	else
		ret = sprintf(buf, "none\n");
	mutex_unlock(&cpuidle_lock);

	return ret;
}

static ssize_t store_current_governor(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	char gov_name[CPUIDLE_NAME_LEN];
	int ret = -EINVAL;
	size_t len = count;
	struct cpuidle_governor *gov;

	if (!len || len >= sizeof(gov_name))
		return -EINVAL;

	memcpy(gov_name, buf, len);
	gov_name[len] = '\0';
	if (gov_name[len - 1] == '\n')
		gov_name[--len] = '\0';

	mutex_lock(&cpuidle_lock);

	list_for_each_entry(gov, &cpuidle_governors, governor_list) {
		if (strlen(gov->name) == len && !strcmp(gov->name, gov_name)) {
			ret = cpuidle_switch_governor(gov);
			break;
		}
	}

	mutex_unlock(&cpuidle_lock);

	if (ret)
		return ret;
	else
		return count;
}

static DEVICE_ATTR(current_driver, 0444, show_current_driver, NULL);
static DEVICE_ATTR(current_governor_ro, 0444, show_current_governor, NULL);

static struct attribute *cpuidle_default_attrs[] = {
	&dev_attr_current_driver.attr,
	&dev_attr_current_governor_ro.attr,
	NULL
};

static DEVICE_ATTR(available_governors, 0444, show_available_governors, NULL);
static DEVICE_ATTR(current_governor, 0644, show_current_governor,
		   store_current_governor);

static struct attribute *cpuidle_switch_attrs[] = {
	&dev_attr_available_governors.attr,
	&dev_attr_current_driver.attr,
	&dev_attr_current_governor.attr,
	NULL
};

static struct attribute_group cpuidle_attr_group = {
	.attrs = cpuidle_default_attrs,
	.name = "cpuidle",
};

/**
 * cpuidle_add_interface - add CPU global sysfs attributes
 */
int cpuidle_add_interface(struct device *dev)
{
	if (sysfs_switch)
		cpuidle_attr_group.attrs = cpuidle_switch_attrs;

	return sysfs_create_group(&dev->kobj, &cpuidle_attr_group);
}

/**
 * cpuidle_remove_interface - remove CPU global sysfs attributes
 */
void cpuidle_remove_interface(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &cpuidle_attr_group);
}

struct cpuidle_attr {
	struct attribute attr;
	ssize_t (*show)(struct cpuidle_device *, char *);
	ssize_t (*store)(struct cpuidle_device *, const char *, size_t count);
};

#define define_one_ro(_name, show) \
	static struct cpuidle_attr attr_##_name = __ATTR(_name, 0444, show, NULL)
#define define_one_rw(_name, show, store) \
	static struct cpuidle_attr attr_##_name = __ATTR(_name, 0644, show, store)

#define attr_to_cpuidleattr(a) container_of(a, struct cpuidle_attr, attr)

struct cpuidle_device_kobj {
	struct cpuidle_device *dev;
	struct completion kobj_unregister;
	struct kobject kobj;
};

static inline struct cpuidle_device *to_cpuidle_device(struct kobject *kobj)
{
	struct cpuidle_device_kobj *kdev =
		container_of(kobj, struct cpuidle_device_kobj, kobj);

	return kdev->dev;
}

static ssize_t cpuidle_show(struct kobject *kobj, struct attribute *attr,
			    char *buf)
{
	int ret = -EIO;
	struct cpuidle_device *dev = to_cpuidle_device(kobj);
	struct cpuidle_attr *cattr = attr_to_cpuidleattr(attr);

	if (cattr->show) {
		mutex_lock(&cpuidle_lock);
		ret = cattr->show(dev, buf);
		mutex_unlock(&cpuidle_lock);
	}
	return ret;
}

static ssize_t cpuidle_store(struct kobject *kobj, struct attribute *attr,
			     const char *buf, size_t count)
{
	int ret = -EIO;
	struct cpuidle_device *dev = to_cpuidle_device(kobj);
	struct cpuidle_attr *cattr = attr_to_cpuidleattr(attr);

	if (cattr->store) {
		mutex_lock(&cpuidle_lock);
		ret = cattr->store(dev, buf, count);
		mutex_unlock(&cpuidle_lock);
	}
	return ret;
}

static const struct sysfs_ops cpuidle_sysfs_ops = {
	.show = cpuidle_show,
	.store = cpuidle_store,
};

static void cpuidle_sysfs_release(struct kobject *kobj)
{
	struct cpuidle_device_kobj *kdev =
		container_of(kobj, struct cpuidle_device_kobj, kobj);

	complete(&kdev->kobj_unregister);
}

static struct kobj_type ktype_cpuidle = {
	.sysfs_ops = &cpuidle_sysfs_ops,
	.release = cpuidle_sysfs_release,
};

struct cpuidle_state_attr {
	struct attribute attr;
	ssize_t (*show)(struct cpuidle_state *, \
					struct cpuidle_state_usage *, char *);
	ssize_t (*store)(struct cpuidle_state *, \
			struct cpuidle_state_usage *, const char *, size_t);
};

#define define_one_state_ro(_name, show) \
static struct cpuidle_state_attr attr_##_name = __ATTR(_name, 0444, show, NULL)

#define define_one_state_rw(_name, show, store) \
static struct cpuidle_state_attr attr_##_name = __ATTR(_name, 0644, show, store)

#define define_show_state_function(_name) \
static ssize_t show_state_##_name(struct cpuidle_state *state, \
			 struct cpuidle_state_usage *state_usage, char *buf) \
{ \
	return sprintf(buf, "%u\n", state->_name);\
}

#define define_store_state_ull_function(_name) \
static ssize_t store_state_##_name(struct cpuidle_state *state, \
				   struct cpuidle_state_usage *state_usage, \
				   const char *buf, size_t size)	\
{ \
	unsigned long long value; \
	int err; \
	if (!capable(CAP_SYS_ADMIN)) \
		return -EPERM; \
	err = kstrtoull(buf, 0, &value); \
	if (err) \
		return err; \
	if (value) \
		state_usage->_name = 1; \
	else \
		state_usage->_name = 0; \
	return size; \
}

#define define_show_state_ull_function(_name) \
static ssize_t show_state_##_name(struct cpuidle_state *state, \
				  struct cpuidle_state_usage *state_usage, \
				  char *buf)				\
{ \
	return sprintf(buf, "%llu\n", state_usage->_name);\
}

#define define_show_state_str_function(_name) \
static ssize_t show_state_##_name(struct cpuidle_state *state, \
				  struct cpuidle_state_usage *state_usage, \
				  char *buf)				\
{ \
	if (state->_name[0] == '\0')\
		return sprintf(buf, "<null>\n");\
	return sprintf(buf, "%s\n", state->_name);\
}

define_show_state_function(exit_latency)
define_show_state_function(target_residency)
define_show_state_function(power_usage)
define_show_state_ull_function(usage)
define_show_state_ull_function(time)
define_show_state_str_function(name)
define_show_state_str_function(desc)
define_show_state_ull_function(disable)
define_store_state_ull_function(disable)

define_one_state_ro(name, show_state_name);
define_one_state_ro(desc, show_state_desc);
define_one_state_ro(latency, show_state_exit_latency);
define_one_state_ro(residency, show_state_target_residency);
define_one_state_ro(power, show_state_power_usage);
define_one_state_ro(usage, show_state_usage);
define_one_state_ro(time, show_state_time);
define_one_state_rw(disable, show_state_disable, store_state_disable);

static struct attribute *cpuidle_state_default_attrs[] = {
	&attr_name.attr,
	&attr_desc.attr,
	&attr_latency.attr,
	&attr_residency.attr,
	&attr_power.attr,
	&attr_usage.attr,
	&attr_time.attr,
	&attr_disable.attr,
	NULL
};

struct cpuidle_state_kobj {
	struct cpuidle_state *state;
	struct cpuidle_state_usage *state_usage;
	struct completion kobj_unregister;
	struct kobject kobj;
};

#define kobj_to_state_obj(k) container_of(k, struct cpuidle_state_kobj, kobj)
#define kobj_to_state(k) (kobj_to_state_obj(k)->state)
#define kobj_to_state_usage(k) (kobj_to_state_obj(k)->state_usage)
#define attr_to_stateattr(a) container_of(a, struct cpuidle_state_attr, attr)

static ssize_t cpuidle_state_show(struct kobject *kobj, struct attribute *attr,
				  char * buf)
{
	int ret = -EIO;
	struct cpuidle_state *state = kobj_to_state(kobj);
	struct cpuidle_state_usage *state_usage = kobj_to_state_usage(kobj);
	struct cpuidle_state_attr * cattr = attr_to_stateattr(attr);

	if (cattr->show)
		ret = cattr->show(state, state_usage, buf);

	return ret;
}

static ssize_t cpuidle_state_store(struct kobject *kobj, struct attribute *attr,
				   const char *buf, size_t size)
{
	int ret = -EIO;
	struct cpuidle_state *state = kobj_to_state(kobj);
	struct cpuidle_state_usage *state_usage = kobj_to_state_usage(kobj);
	struct cpuidle_state_attr *cattr = attr_to_stateattr(attr);

	if (cattr->store)
		ret = cattr->store(state, state_usage, buf, size);

	return ret;
}

static const struct sysfs_ops cpuidle_state_sysfs_ops = {
	.show = cpuidle_state_show,
	.store = cpuidle_state_store,
};

static void cpuidle_state_sysfs_release(struct kobject *kobj)
{
	struct cpuidle_state_kobj *state_obj = kobj_to_state_obj(kobj);

	complete(&state_obj->kobj_unregister);
}

static struct kobj_type ktype_state_cpuidle = {
	.sysfs_ops = &cpuidle_state_sysfs_ops,
	.default_attrs = cpuidle_state_default_attrs,
	.release = cpuidle_state_sysfs_release,
};

static inline void cpuidle_free_state_kobj(struct cpuidle_device *device, int i)
{
	kobject_put(&device->kobjs[i]->kobj);
	wait_for_completion(&device->kobjs[i]->kobj_unregister);
	kfree(device->kobjs[i]);
	device->kobjs[i] = NULL;
}

/**
 * cpuidle_add_state_sysfs - adds cpuidle states sysfs attributes
 * @device: the target device
 */
static int cpuidle_add_state_sysfs(struct cpuidle_device *device)
{
	int i, ret = -ENOMEM;
	struct cpuidle_state_kobj *kobj;
	struct cpuidle_device_kobj *kdev = device->kobj_dev;
	struct cpuidle_driver *drv = cpuidle_get_cpu_driver(device);

	/* state statistics */
	for (i = 0; i < drv->state_count; i++) {
		kobj = kzalloc(sizeof(struct cpuidle_state_kobj), GFP_KERNEL);
		if (!kobj)
			goto error_state;
		kobj->state = &drv->states[i];
		kobj->state_usage = &device->states_usage[i];
		init_completion(&kobj->kobj_unregister);

		ret = kobject_init_and_add(&kobj->kobj, &ktype_state_cpuidle,
					   &kdev->kobj, "state%d", i);
		if (ret) {
			kfree(kobj);
			goto error_state;
		}
		kobject_uevent(&kobj->kobj, KOBJ_ADD);
		device->kobjs[i] = kobj;
	}

	return 0;

error_state:
	for (i = i - 1; i >= 0; i--)
		cpuidle_free_state_kobj(device, i);
	return ret;
}

/**
 * cpuidle_remove_driver_sysfs - removes the cpuidle states sysfs attributes
 * @device: the target device
 */
static void cpuidle_remove_state_sysfs(struct cpuidle_device *device)
{
	struct cpuidle_driver *drv = cpuidle_get_cpu_driver(device);
	int i;

	for (i = 0; i < drv->state_count; i++)
		cpuidle_free_state_kobj(device, i);
}

#ifdef CONFIG_CPU_IDLE_MULTIPLE_DRIVERS
#define kobj_to_driver_kobj(k) container_of(k, struct cpuidle_driver_kobj, kobj)
#define attr_to_driver_attr(a) container_of(a, struct cpuidle_driver_attr, attr)

#define define_one_driver_ro(_name, show)                       \
	static struct cpuidle_driver_attr attr_driver_##_name = \
		__ATTR(_name, 0444, show, NULL)

struct cpuidle_driver_kobj {
	struct cpuidle_driver *drv;
	struct completion kobj_unregister;
	struct kobject kobj;
};

struct cpuidle_driver_attr {
	struct attribute attr;
	ssize_t (*show)(struct cpuidle_driver *, char *);
	ssize_t (*store)(struct cpuidle_driver *, const char *, size_t);
};

static ssize_t show_driver_name(struct cpuidle_driver *drv, char *buf)
{
	ssize_t ret;

	spin_lock(&cpuidle_driver_lock);
	ret = sprintf(buf, "%s\n", drv ? drv->name : "none");
	spin_unlock(&cpuidle_driver_lock);

	return ret;
}

static void cpuidle_driver_sysfs_release(struct kobject *kobj)
{
	struct cpuidle_driver_kobj *driver_kobj = kobj_to_driver_kobj(kobj);
	complete(&driver_kobj->kobj_unregister);
}

static ssize_t cpuidle_driver_show(struct kobject *kobj, struct attribute *attr,
				   char *buf)
{
	int ret = -EIO;
	struct cpuidle_driver_kobj *driver_kobj = kobj_to_driver_kobj(kobj);
	struct cpuidle_driver_attr *dattr = attr_to_driver_attr(attr);

	if (dattr->show)
		ret = dattr->show(driver_kobj->drv, buf);

	return ret;
}

static ssize_t cpuidle_driver_store(struct kobject *kobj, struct attribute *attr,
				    const char *buf, size_t size)
{
	int ret = -EIO;
	struct cpuidle_driver_kobj *driver_kobj = kobj_to_driver_kobj(kobj);
	struct cpuidle_driver_attr *dattr = attr_to_driver_attr(attr);

	if (dattr->store)
		ret = dattr->store(driver_kobj->drv, buf, size);

	return ret;
}

define_one_driver_ro(name, show_driver_name);

static const struct sysfs_ops cpuidle_driver_sysfs_ops = {
	.show = cpuidle_driver_show,
	.store = cpuidle_driver_store,
};

static struct attribute *cpuidle_driver_default_attrs[] = {
	&attr_driver_name.attr,
	NULL
};

static struct kobj_type ktype_driver_cpuidle = {
	.sysfs_ops = &cpuidle_driver_sysfs_ops,
	.default_attrs = cpuidle_driver_default_attrs,
	.release = cpuidle_driver_sysfs_release,
};

/**
 * cpuidle_add_driver_sysfs - adds the driver name sysfs attribute
 * @device: the target device
 */
static int cpuidle_add_driver_sysfs(struct cpuidle_device *dev)
{
	struct cpuidle_driver_kobj *kdrv;
	struct cpuidle_device_kobj *kdev = dev->kobj_dev;
	struct cpuidle_driver *drv = cpuidle_get_cpu_driver(dev);
	int ret;

	kdrv = kzalloc(sizeof(*kdrv), GFP_KERNEL);
	if (!kdrv)
		return -ENOMEM;

	kdrv->drv = drv;
	init_completion(&kdrv->kobj_unregister);

	ret = kobject_init_and_add(&kdrv->kobj, &ktype_driver_cpuidle,
				   &kdev->kobj, "driver");
	if (ret) {
		kfree(kdrv);
		return ret;
	}

	kobject_uevent(&kdrv->kobj, KOBJ_ADD);
	dev->kobj_driver = kdrv;

	return ret;
}

/**
 * cpuidle_remove_driver_sysfs - removes the driver name sysfs attribute
 * @device: the target device
 */
static void cpuidle_remove_driver_sysfs(struct cpuidle_device *dev)
{
	struct cpuidle_driver_kobj *kdrv = dev->kobj_driver;
	kobject_put(&kdrv->kobj);
	wait_for_completion(&kdrv->kobj_unregister);
	kfree(kdrv);
}
#else
static inline int cpuidle_add_driver_sysfs(struct cpuidle_device *dev)
{
	return 0;
}

static inline void cpuidle_remove_driver_sysfs(struct cpuidle_device *dev)
{
	;
}
#endif

/**
 * cpuidle_add_device_sysfs - adds device specific sysfs attributes
 * @device: the target device
 */
int cpuidle_add_device_sysfs(struct cpuidle_device *device)
{
	int ret;

	ret = cpuidle_add_state_sysfs(device);
	if (ret)
		return ret;

	ret = cpuidle_add_driver_sysfs(device);
	if (ret)
		cpuidle_remove_state_sysfs(device);
	return ret;
}

/**
 * cpuidle_remove_device_sysfs : removes device specific sysfs attributes
 * @device : the target device
 */
void cpuidle_remove_device_sysfs(struct cpuidle_device *device)
{
	cpuidle_remove_driver_sysfs(device);
	cpuidle_remove_state_sysfs(device);
}

/**
 * cpuidle_add_sysfs - creates a sysfs instance for the target device
 * @dev: the target device
 */
int cpuidle_add_sysfs(struct cpuidle_device *dev)
{
	struct cpuidle_device_kobj *kdev;
	struct device *cpu_dev = get_cpu_device((unsigned long)dev->cpu);
	int error;

	/*
	 * Return if cpu_device is not setup for this CPU.
	 *
	 * This could happen if the arch did not set up cpu_device
	 * since this CPU is not in cpu_present mask and the
	 * driver did not send a correct CPU mask during registration.
	 * Without this check we would end up passing bogus
	 * value for &cpu_dev->kobj in kobject_init_and_add()
	 */
	if (!cpu_dev)
		return -ENODEV;

	kdev = kzalloc(sizeof(*kdev), GFP_KERNEL);
	if (!kdev)
		return -ENOMEM;
	kdev->dev = dev;
	dev->kobj_dev = kdev;

	init_completion(&kdev->kobj_unregister);

	error = kobject_init_and_add(&kdev->kobj, &ktype_cpuidle, &cpu_dev->kobj,
				   "cpuidle");
	if (error) {
		kfree(kdev);
		return error;
	}

	kobject_uevent(&kdev->kobj, KOBJ_ADD);

	return 0;
}

/**
 * cpuidle_remove_sysfs - deletes a sysfs instance on the target device
 * @dev: the target device
 */
void cpuidle_remove_sysfs(struct cpuidle_device *dev)
{
	struct cpuidle_device_kobj *kdev = dev->kobj_dev;

	kobject_put(&kdev->kobj);
	wait_for_completion(&kdev->kobj_unregister);
	kfree(kdev);
}
