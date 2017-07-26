/*
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * Copyright IBM Corp. 1999,2001
 *
 * Device mapping and dasd= parameter parsing functions. All devmap
 * functions may not be called from interrupt context. In particular
 * dasd_get_device is a no-no from interrupt context.
 *
 */

#define KMSG_COMPONENT "dasd"

#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <asm/debug.h>
#include <linux/uaccess.h>
#include <asm/ipl.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_devmap:"
#define DASD_BUS_ID_SIZE 20
#define DASD_MAX_PARAMS 256

#include "dasd_int.h"

struct kmem_cache *dasd_page_cache;
EXPORT_SYMBOL_GPL(dasd_page_cache);

/*
 * dasd_devmap_t is used to store the features and the relation
 * between device number and device index. To find a dasd_devmap_t
 * that corresponds to a device number of a device index each
 * dasd_devmap_t is added to two linked lists, one to search by
 * the device number and one to search by the device index. As
 * soon as big minor numbers are available the device index list
 * can be removed since the device number will then be identical
 * to the device index.
 */
struct dasd_devmap {
	struct list_head list;
	char bus_id[DASD_BUS_ID_SIZE];
        unsigned int devindex;
        unsigned short features;
	struct dasd_device *device;
};

/*
 * Parameter parsing functions for dasd= parameter. The syntax is:
 *   <devno>		: (0x)?[0-9a-fA-F]+
 *   <busid>		: [0-0a-f]\.[0-9a-f]\.(0x)?[0-9a-fA-F]+
 *   <feature>		: ro
 *   <feature_list>	: \(<feature>(:<feature>)*\)
 *   <devno-range>	: <devno>(-<devno>)?<feature_list>?
 *   <busid-range>	: <busid>(-<busid>)?<feature_list>?
 *   <devices>		: <devno-range>|<busid-range>
 *   <dasd_module>	: dasd_diag_mod|dasd_eckd_mod|dasd_fba_mod
 *
 *   <dasd>		: autodetect|probeonly|<devices>(,<devices>)*
 */

int dasd_probeonly =  0;	/* is true, when probeonly mode is active */
int dasd_autodetect = 0;	/* is true, when autodetection is active */
int dasd_nopav = 0;		/* is true, when PAV is disabled */
EXPORT_SYMBOL_GPL(dasd_nopav);
int dasd_nofcx;			/* disable High Performance Ficon */
EXPORT_SYMBOL_GPL(dasd_nofcx);

/*
 * char *dasd[] is intended to hold the ranges supplied by the dasd= statement
 * it is named 'dasd' to directly be filled by insmod with the comma separated
 * strings when running as a module.
 */
static char *dasd[DASD_MAX_PARAMS];
module_param_array(dasd, charp, NULL, S_IRUGO);

/*
 * Single spinlock to protect devmap and servermap structures and lists.
 */
static DEFINE_SPINLOCK(dasd_devmap_lock);

/*
 * Hash lists for devmap structures.
 */
static struct list_head dasd_hashlists[256];
int dasd_max_devindex;

static struct dasd_devmap *dasd_add_busid(const char *, int);

static inline int
dasd_hash_busid(const char *bus_id)
{
	int hash, i;

	hash = 0;
	for (i = 0; (i < DASD_BUS_ID_SIZE) && *bus_id; i++, bus_id++)
		hash += *bus_id;
	return hash & 0xff;
}

#ifndef MODULE
static int __init dasd_call_setup(char *opt)
{
	static int i __initdata;
	char *tmp;

	while (i < DASD_MAX_PARAMS) {
		tmp = strsep(&opt, ",");
		if (!tmp)
			break;

		dasd[i++] = tmp;
	}

	return 1;
}

__setup ("dasd=", dasd_call_setup);
#endif	/* #ifndef MODULE */

#define	DASD_IPLDEV	"ipldev"

/*
 * Read a device busid/devno from a string.
 */
static int __init dasd_busid(char *str, int *id0, int *id1, int *devno)
{
	unsigned int val;
	char *tok;

	/* Interpret ipldev busid */
	if (strncmp(DASD_IPLDEV, str, strlen(DASD_IPLDEV)) == 0) {
		if (ipl_info.type != IPL_TYPE_CCW) {
			pr_err("The IPL device is not a CCW device\n");
			return -EINVAL;
		}
		*id0 = 0;
		*id1 = ipl_info.data.ccw.dev_id.ssid;
		*devno = ipl_info.data.ccw.dev_id.devno;

		return 0;
	}

	/* Old style 0xXXXX or XXXX */
	if (!kstrtouint(str, 16, &val)) {
		*id0 = *id1 = 0;
		if (val > 0xffff)
			return -EINVAL;
		*devno = val;
		return 0;
	}

	/* New style x.y.z busid */
	tok = strsep(&str, ".");
	if (kstrtouint(tok, 16, &val) || val > 0xff)
		return -EINVAL;
	*id0 = val;

	tok = strsep(&str, ".");
	if (kstrtouint(tok, 16, &val) || val > 0xff)
		return -EINVAL;
	*id1 = val;

	tok = strsep(&str, ".");
	if (kstrtouint(tok, 16, &val) || val > 0xffff)
		return -EINVAL;
	*devno = val;

	return 0;
}

/*
 * Read colon separated list of dasd features.
 */
static int __init dasd_feature_list(char *str)
{
	int features, len, rc;

	features = 0;
	rc = 0;

	if (!str)
		return DASD_FEATURE_DEFAULT;

	while (1) {
		for (len = 0;
		     str[len] && str[len] != ':' && str[len] != ')'; len++);
		if (len == 2 && !strncmp(str, "ro", 2))
			features |= DASD_FEATURE_READONLY;
		else if (len == 4 && !strncmp(str, "diag", 4))
			features |= DASD_FEATURE_USEDIAG;
		else if (len == 3 && !strncmp(str, "raw", 3))
			features |= DASD_FEATURE_USERAW;
		else if (len == 6 && !strncmp(str, "erplog", 6))
			features |= DASD_FEATURE_ERPLOG;
		else if (len == 8 && !strncmp(str, "failfast", 8))
			features |= DASD_FEATURE_FAILFAST;
		else {
			pr_warn("%*s is not a supported device option\n",
				len, str);
			rc = -EINVAL;
		}
		str += len;
		if (*str != ':')
			break;
		str++;
	}

	return rc ? : features;
}

/*
 * Try to match the first element on the comma separated parse string
 * with one of the known keywords. If a keyword is found, take the approprate
 * action and return a pointer to the residual string. If the first element
 * could not be matched to any keyword then return an error code.
 */
static int __init dasd_parse_keyword(char *keyword)
{
	int length = strlen(keyword);

	if (strncmp("autodetect", keyword, length) == 0) {
		dasd_autodetect = 1;
		pr_info("The autodetection mode has been activated\n");
		return 0;
        }
	if (strncmp("probeonly", keyword, length) == 0) {
		dasd_probeonly = 1;
		pr_info("The probeonly mode has been activated\n");
		return 0;
        }
	if (strncmp("nopav", keyword, length) == 0) {
		if (MACHINE_IS_VM)
			pr_info("'nopav' is not supported on z/VM\n");
		else {
			dasd_nopav = 1;
			pr_info("PAV support has be deactivated\n");
		}
		return 0;
	}
	if (strncmp("nofcx", keyword, length) == 0) {
		dasd_nofcx = 1;
		pr_info("High Performance FICON support has been "
			"deactivated\n");
		return 0;
	}
	if (strncmp("fixedbuffers", keyword, length) == 0) {
		if (dasd_page_cache)
			return 0;
		dasd_page_cache =
			kmem_cache_create("dasd_page_cache", PAGE_SIZE,
					  PAGE_SIZE, SLAB_CACHE_DMA,
					  NULL);
		if (!dasd_page_cache)
			DBF_EVENT(DBF_WARNING, "%s", "Failed to create slab, "
				"fixed buffer mode disabled.");
		else
			DBF_EVENT(DBF_INFO, "%s",
				 "turning on fixed buffer mode");
		return 0;
	}

	return -EINVAL;
}

/*
 * Split a string of a device range into its pieces and return the from, to, and
 * feature parts separately.
 * e.g.:
 * 0.0.1234-0.0.5678(ro:erplog) -> from: 0.0.1234 to: 0.0.5678 features: ro:erplog
 * 0.0.8765(raw) -> from: 0.0.8765 to: null features: raw
 * 0x4321 -> from: 0x4321 to: null features: null
 */
static int __init dasd_evaluate_range_param(char *range, char **from_str,
					    char **to_str, char **features_str)
{
	int rc = 0;

	/* Do we have a range or a single device? */
	if (strchr(range, '-')) {
		*from_str = strsep(&range, "-");
		*to_str = strsep(&range, "(");
		*features_str = strsep(&range, ")");
	} else {
		*from_str = strsep(&range, "(");
		*features_str = strsep(&range, ")");
	}

	if (*features_str && !range) {
		pr_warn("A closing parenthesis ')' is missing in the dasd= parameter\n");
		rc = -EINVAL;
	}

	return rc;
}

/*
 * Try to interprete the range string as a device number or a range of devices.
 * If the interpretation is successful, create the matching dasd_devmap entries.
 * If interpretation fails or in case of an error, return an error code.
 */
static int __init dasd_parse_range(const char *range)
{
	struct dasd_devmap *devmap;
	int from, from_id0, from_id1;
	int to, to_id0, to_id1;
	int features;
	char bus_id[DASD_BUS_ID_SIZE + 1];
	char *features_str = NULL;
	char *from_str = NULL;
	char *to_str = NULL;
	int rc = 0;
	char *tmp;

	tmp = kstrdup(range, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if (dasd_evaluate_range_param(tmp, &from_str, &to_str, &features_str)) {
		rc = -EINVAL;
		goto out;
	}

	if (dasd_busid(from_str, &from_id0, &from_id1, &from)) {
		rc = -EINVAL;
		goto out;
	}

	to = from;
	to_id0 = from_id0;
	to_id1 = from_id1;
	if (to_str) {
		if (dasd_busid(to_str, &to_id0, &to_id1, &to)) {
			rc = -EINVAL;
			goto out;
		}
		if (from_id0 != to_id0 || from_id1 != to_id1 || from > to) {
			pr_err("%s is not a valid device range\n", range);
			rc = -EINVAL;
			goto out;
		}
	}

	features = dasd_feature_list(features_str);
	if (features < 0) {
		rc = -EINVAL;
		goto out;
	}
	/* each device in dasd= parameter should be set initially online */
	features |= DASD_FEATURE_INITIAL_ONLINE;
	while (from <= to) {
		sprintf(bus_id, "%01x.%01x.%04x", from_id0, from_id1, from++);
		devmap = dasd_add_busid(bus_id, features);
		if (IS_ERR(devmap)) {
			rc = PTR_ERR(devmap);
			goto out;
		}
	}

out:
	kfree(tmp);

	return rc;
}

/*
 * Parse parameters stored in dasd[]
 * The 'dasd=...' parameter allows to specify a comma separated list of
 * keywords and device ranges. The parameters in that list will be stored as
 * separate elementes in dasd[].
 */
int __init dasd_parse(void)
{
	int rc, i;
	char *cur;

	rc = 0;
	for (i = 0; i < DASD_MAX_PARAMS; i++) {
		cur = dasd[i];
		if (!cur)
			break;
		if (*cur == '\0')
			continue;

		rc = dasd_parse_keyword(cur);
		if (rc)
			rc = dasd_parse_range(cur);

		if (rc)
			break;
	}

	return rc;
}

/*
 * Add a devmap for the device specified by busid. It is possible that
 * the devmap already exists (dasd= parameter). The order of the devices
 * added through this function will define the kdevs for the individual
 * devices.
 */
static struct dasd_devmap *
dasd_add_busid(const char *bus_id, int features)
{
	struct dasd_devmap *devmap, *new, *tmp;
	int hash;

	new = kzalloc(sizeof(struct dasd_devmap), GFP_KERNEL);
	if (!new)
		return ERR_PTR(-ENOMEM);
	spin_lock(&dasd_devmap_lock);
	devmap = NULL;
	hash = dasd_hash_busid(bus_id);
	list_for_each_entry(tmp, &dasd_hashlists[hash], list)
		if (strncmp(tmp->bus_id, bus_id, DASD_BUS_ID_SIZE) == 0) {
			devmap = tmp;
			break;
		}
	if (!devmap) {
		/* This bus_id is new. */
		new->devindex = dasd_max_devindex++;
		strncpy(new->bus_id, bus_id, DASD_BUS_ID_SIZE);
		new->features = features;
		new->device = NULL;
		list_add(&new->list, &dasd_hashlists[hash]);
		devmap = new;
		new = NULL;
	}
	spin_unlock(&dasd_devmap_lock);
	kfree(new);
	return devmap;
}

/*
 * Find devmap for device with given bus_id.
 */
static struct dasd_devmap *
dasd_find_busid(const char *bus_id)
{
	struct dasd_devmap *devmap, *tmp;
	int hash;

	spin_lock(&dasd_devmap_lock);
	devmap = ERR_PTR(-ENODEV);
	hash = dasd_hash_busid(bus_id);
	list_for_each_entry(tmp, &dasd_hashlists[hash], list) {
		if (strncmp(tmp->bus_id, bus_id, DASD_BUS_ID_SIZE) == 0) {
			devmap = tmp;
			break;
		}
	}
	spin_unlock(&dasd_devmap_lock);
	return devmap;
}

/*
 * Check if busid has been added to the list of dasd ranges.
 */
int
dasd_busid_known(const char *bus_id)
{
	return IS_ERR(dasd_find_busid(bus_id)) ? -ENOENT : 0;
}

/*
 * Forget all about the device numbers added so far.
 * This may only be called at module unload or system shutdown.
 */
static void
dasd_forget_ranges(void)
{
	struct dasd_devmap *devmap, *n;
	int i;

	spin_lock(&dasd_devmap_lock);
	for (i = 0; i < 256; i++) {
		list_for_each_entry_safe(devmap, n, &dasd_hashlists[i], list) {
			BUG_ON(devmap->device != NULL);
			list_del(&devmap->list);
			kfree(devmap);
		}
	}
	spin_unlock(&dasd_devmap_lock);
}

/*
 * Find the device struct by its device index.
 */
struct dasd_device *
dasd_device_from_devindex(int devindex)
{
	struct dasd_devmap *devmap, *tmp;
	struct dasd_device *device;
	int i;

	spin_lock(&dasd_devmap_lock);
	devmap = NULL;
	for (i = 0; (i < 256) && !devmap; i++)
		list_for_each_entry(tmp, &dasd_hashlists[i], list)
			if (tmp->devindex == devindex) {
				/* Found the devmap for the device. */
				devmap = tmp;
				break;
			}
	if (devmap && devmap->device) {
		device = devmap->device;
		dasd_get_device(device);
	} else
		device = ERR_PTR(-ENODEV);
	spin_unlock(&dasd_devmap_lock);
	return device;
}

/*
 * Return devmap for cdev. If no devmap exists yet, create one and
 * connect it to the cdev.
 */
static struct dasd_devmap *
dasd_devmap_from_cdev(struct ccw_device *cdev)
{
	struct dasd_devmap *devmap;

	devmap = dasd_find_busid(dev_name(&cdev->dev));
	if (IS_ERR(devmap))
		devmap = dasd_add_busid(dev_name(&cdev->dev),
					DASD_FEATURE_DEFAULT);
	return devmap;
}

/*
 * Create a dasd device structure for cdev.
 */
struct dasd_device *
dasd_create_device(struct ccw_device *cdev)
{
	struct dasd_devmap *devmap;
	struct dasd_device *device;
	unsigned long flags;
	int rc;

	devmap = dasd_devmap_from_cdev(cdev);
	if (IS_ERR(devmap))
		return (void *) devmap;

	device = dasd_alloc_device();
	if (IS_ERR(device))
		return device;
	atomic_set(&device->ref_count, 3);

	spin_lock(&dasd_devmap_lock);
	if (!devmap->device) {
		devmap->device = device;
		device->devindex = devmap->devindex;
		device->features = devmap->features;
		get_device(&cdev->dev);
		device->cdev = cdev;
		rc = 0;
	} else
		/* Someone else was faster. */
		rc = -EBUSY;
	spin_unlock(&dasd_devmap_lock);

	if (rc) {
		dasd_free_device(device);
		return ERR_PTR(rc);
	}

	spin_lock_irqsave(get_ccwdev_lock(cdev), flags);
	dev_set_drvdata(&cdev->dev, device);
	spin_unlock_irqrestore(get_ccwdev_lock(cdev), flags);

	return device;
}

/*
 * Wait queue for dasd_delete_device waits.
 */
static DECLARE_WAIT_QUEUE_HEAD(dasd_delete_wq);

/*
 * Remove a dasd device structure. The passed referenced
 * is destroyed.
 */
void
dasd_delete_device(struct dasd_device *device)
{
	struct ccw_device *cdev;
	struct dasd_devmap *devmap;
	unsigned long flags;

	/* First remove device pointer from devmap. */
	devmap = dasd_find_busid(dev_name(&device->cdev->dev));
	BUG_ON(IS_ERR(devmap));
	spin_lock(&dasd_devmap_lock);
	if (devmap->device != device) {
		spin_unlock(&dasd_devmap_lock);
		dasd_put_device(device);
		return;
	}
	devmap->device = NULL;
	spin_unlock(&dasd_devmap_lock);

	/* Disconnect dasd_device structure from ccw_device structure. */
	spin_lock_irqsave(get_ccwdev_lock(device->cdev), flags);
	dev_set_drvdata(&device->cdev->dev, NULL);
	spin_unlock_irqrestore(get_ccwdev_lock(device->cdev), flags);

	/*
	 * Drop ref_count by 3, one for the devmap reference, one for
	 * the cdev reference and one for the passed reference.
	 */
	atomic_sub(3, &device->ref_count);

	/* Wait for reference counter to drop to zero. */
	wait_event(dasd_delete_wq, atomic_read(&device->ref_count) == 0);

	dasd_generic_free_discipline(device);
	/* Disconnect dasd_device structure from ccw_device structure. */
	cdev = device->cdev;
	device->cdev = NULL;

	/* Put ccw_device structure. */
	put_device(&cdev->dev);

	/* Now the device structure can be freed. */
	dasd_free_device(device);
}

/*
 * Reference counter dropped to zero. Wake up waiter
 * in dasd_delete_device.
 */
void
dasd_put_device_wake(struct dasd_device *device)
{
	wake_up(&dasd_delete_wq);
}
EXPORT_SYMBOL_GPL(dasd_put_device_wake);

/*
 * Return dasd_device structure associated with cdev.
 * This function needs to be called with the ccw device
 * lock held. It can be used from interrupt context.
 */
struct dasd_device *
dasd_device_from_cdev_locked(struct ccw_device *cdev)
{
	struct dasd_device *device = dev_get_drvdata(&cdev->dev);

	if (!device)
		return ERR_PTR(-ENODEV);
	dasd_get_device(device);
	return device;
}

/*
 * Return dasd_device structure associated with cdev.
 */
struct dasd_device *
dasd_device_from_cdev(struct ccw_device *cdev)
{
	struct dasd_device *device;
	unsigned long flags;

	spin_lock_irqsave(get_ccwdev_lock(cdev), flags);
	device = dasd_device_from_cdev_locked(cdev);
	spin_unlock_irqrestore(get_ccwdev_lock(cdev), flags);
	return device;
}

void dasd_add_link_to_gendisk(struct gendisk *gdp, struct dasd_device *device)
{
	struct dasd_devmap *devmap;

	devmap = dasd_find_busid(dev_name(&device->cdev->dev));
	if (IS_ERR(devmap))
		return;
	spin_lock(&dasd_devmap_lock);
	gdp->private_data = devmap;
	spin_unlock(&dasd_devmap_lock);
}

struct dasd_device *dasd_device_from_gendisk(struct gendisk *gdp)
{
	struct dasd_device *device;
	struct dasd_devmap *devmap;

	if (!gdp->private_data)
		return NULL;
	device = NULL;
	spin_lock(&dasd_devmap_lock);
	devmap = gdp->private_data;
	if (devmap && devmap->device) {
		device = devmap->device;
		dasd_get_device(device);
	}
	spin_unlock(&dasd_devmap_lock);
	return device;
}

/*
 * SECTION: files in sysfs
 */

/*
 * failfast controls the behaviour, if no path is available
 */
static ssize_t dasd_ff_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct dasd_devmap *devmap;
	int ff_flag;

	devmap = dasd_find_busid(dev_name(dev));
	if (!IS_ERR(devmap))
		ff_flag = (devmap->features & DASD_FEATURE_FAILFAST) != 0;
	else
		ff_flag = (DASD_FEATURE_DEFAULT & DASD_FEATURE_FAILFAST) != 0;
	return snprintf(buf, PAGE_SIZE, ff_flag ? "1\n" : "0\n");
}

static ssize_t dasd_ff_store(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	unsigned int val;
	int rc;

	if (kstrtouint(buf, 0, &val) || val > 1)
		return -EINVAL;

	rc = dasd_set_feature(to_ccwdev(dev), DASD_FEATURE_FAILFAST, val);

	return rc ? : count;
}

static DEVICE_ATTR(failfast, 0644, dasd_ff_show, dasd_ff_store);

/*
 * readonly controls the readonly status of a dasd
 */
static ssize_t
dasd_ro_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dasd_devmap *devmap;
	struct dasd_device *device;
	int ro_flag = 0;

	devmap = dasd_find_busid(dev_name(dev));
	if (IS_ERR(devmap))
		goto out;

	ro_flag = !!(devmap->features & DASD_FEATURE_READONLY);

	spin_lock(&dasd_devmap_lock);
	device = devmap->device;
	if (device)
		ro_flag |= test_bit(DASD_FLAG_DEVICE_RO, &device->flags);
	spin_unlock(&dasd_devmap_lock);

out:
	return snprintf(buf, PAGE_SIZE, ro_flag ? "1\n" : "0\n");
}

static ssize_t
dasd_ro_store(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct dasd_device *device;
	unsigned long flags;
	unsigned int val;
	int rc;

	if (kstrtouint(buf, 0, &val) || val > 1)
		return -EINVAL;

	rc = dasd_set_feature(cdev, DASD_FEATURE_READONLY, val);
	if (rc)
		return rc;

	device = dasd_device_from_cdev(cdev);
	if (IS_ERR(device))
		return count;

	spin_lock_irqsave(get_ccwdev_lock(cdev), flags);
	val = val || test_bit(DASD_FLAG_DEVICE_RO, &device->flags);

	if (!device->block || !device->block->gdp ||
	    test_bit(DASD_FLAG_OFFLINE, &device->flags)) {
		spin_unlock_irqrestore(get_ccwdev_lock(cdev), flags);
		goto out;
	}
	/* Increase open_count to avoid losing the block device */
	atomic_inc(&device->block->open_count);
	spin_unlock_irqrestore(get_ccwdev_lock(cdev), flags);

	set_disk_ro(device->block->gdp, val);
	atomic_dec(&device->block->open_count);

out:
	dasd_put_device(device);

	return count;
}

static DEVICE_ATTR(readonly, 0644, dasd_ro_show, dasd_ro_store);
/*
 * erplog controls the logging of ERP related data
 * (e.g. failing channel programs).
 */
static ssize_t
dasd_erplog_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dasd_devmap *devmap;
	int erplog;

	devmap = dasd_find_busid(dev_name(dev));
	if (!IS_ERR(devmap))
		erplog = (devmap->features & DASD_FEATURE_ERPLOG) != 0;
	else
		erplog = (DASD_FEATURE_DEFAULT & DASD_FEATURE_ERPLOG) != 0;
	return snprintf(buf, PAGE_SIZE, erplog ? "1\n" : "0\n");
}

static ssize_t
dasd_erplog_store(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	unsigned int val;
	int rc;

	if (kstrtouint(buf, 0, &val) || val > 1)
		return -EINVAL;

	rc = dasd_set_feature(to_ccwdev(dev), DASD_FEATURE_ERPLOG, val);

	return rc ? : count;
}

static DEVICE_ATTR(erplog, 0644, dasd_erplog_show, dasd_erplog_store);

/*
 * use_diag controls whether the driver should use diag rather than ssch
 * to talk to the device
 */
static ssize_t
dasd_use_diag_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dasd_devmap *devmap;
	int use_diag;

	devmap = dasd_find_busid(dev_name(dev));
	if (!IS_ERR(devmap))
		use_diag = (devmap->features & DASD_FEATURE_USEDIAG) != 0;
	else
		use_diag = (DASD_FEATURE_DEFAULT & DASD_FEATURE_USEDIAG) != 0;
	return sprintf(buf, use_diag ? "1\n" : "0\n");
}

static ssize_t
dasd_use_diag_store(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	struct dasd_devmap *devmap;
	unsigned int val;
	ssize_t rc;

	devmap = dasd_devmap_from_cdev(to_ccwdev(dev));
	if (IS_ERR(devmap))
		return PTR_ERR(devmap);

	if (kstrtouint(buf, 0, &val) || val > 1)
		return -EINVAL;

	spin_lock(&dasd_devmap_lock);
	/* Changing diag discipline flag is only allowed in offline state. */
	rc = count;
	if (!devmap->device && !(devmap->features & DASD_FEATURE_USERAW)) {
		if (val)
			devmap->features |= DASD_FEATURE_USEDIAG;
		else
			devmap->features &= ~DASD_FEATURE_USEDIAG;
	} else
		rc = -EPERM;
	spin_unlock(&dasd_devmap_lock);
	return rc;
}

static DEVICE_ATTR(use_diag, 0644, dasd_use_diag_show, dasd_use_diag_store);

/*
 * use_raw controls whether the driver should give access to raw eckd data or
 * operate in standard mode
 */
static ssize_t
dasd_use_raw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dasd_devmap *devmap;
	int use_raw;

	devmap = dasd_find_busid(dev_name(dev));
	if (!IS_ERR(devmap))
		use_raw = (devmap->features & DASD_FEATURE_USERAW) != 0;
	else
		use_raw = (DASD_FEATURE_DEFAULT & DASD_FEATURE_USERAW) != 0;
	return sprintf(buf, use_raw ? "1\n" : "0\n");
}

static ssize_t
dasd_use_raw_store(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	struct dasd_devmap *devmap;
	ssize_t rc;
	unsigned long val;

	devmap = dasd_devmap_from_cdev(to_ccwdev(dev));
	if (IS_ERR(devmap))
		return PTR_ERR(devmap);

	if ((kstrtoul(buf, 10, &val) != 0) || val > 1)
		return -EINVAL;

	spin_lock(&dasd_devmap_lock);
	/* Changing diag discipline flag is only allowed in offline state. */
	rc = count;
	if (!devmap->device && !(devmap->features & DASD_FEATURE_USEDIAG)) {
		if (val)
			devmap->features |= DASD_FEATURE_USERAW;
		else
			devmap->features &= ~DASD_FEATURE_USERAW;
	} else
		rc = -EPERM;
	spin_unlock(&dasd_devmap_lock);
	return rc;
}

static DEVICE_ATTR(raw_track_access, 0644, dasd_use_raw_show,
		   dasd_use_raw_store);

static ssize_t
dasd_safe_offline_store(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct dasd_device *device;
	unsigned long flags;
	int rc;

	spin_lock_irqsave(get_ccwdev_lock(cdev), flags);
	device = dasd_device_from_cdev_locked(cdev);
	if (IS_ERR(device)) {
		rc = PTR_ERR(device);
		spin_unlock_irqrestore(get_ccwdev_lock(cdev), flags);
		goto out;
	}

	if (test_bit(DASD_FLAG_OFFLINE, &device->flags) ||
	    test_bit(DASD_FLAG_SAFE_OFFLINE_RUNNING, &device->flags)) {
		/* Already doing offline processing */
		dasd_put_device(device);
		spin_unlock_irqrestore(get_ccwdev_lock(cdev), flags);
		rc = -EBUSY;
		goto out;
	}

	set_bit(DASD_FLAG_SAFE_OFFLINE, &device->flags);
	dasd_put_device(device);
	spin_unlock_irqrestore(get_ccwdev_lock(cdev), flags);

	rc = ccw_device_set_offline(cdev);

out:
	return rc ? rc : count;
}

static DEVICE_ATTR(safe_offline, 0200, NULL, dasd_safe_offline_store);

static ssize_t
dasd_access_show(struct device *dev, struct device_attribute *attr,
		 char *buf)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	struct dasd_device *device;
	int count;

	device = dasd_device_from_cdev(cdev);
	if (IS_ERR(device))
		return PTR_ERR(device);

	if (!device->discipline)
		count = -ENODEV;
	else if (!device->discipline->host_access_count)
		count = -EOPNOTSUPP;
	else
		count = device->discipline->host_access_count(device);

	dasd_put_device(device);
	if (count < 0)
		return count;

	return sprintf(buf, "%d\n", count);
}

static DEVICE_ATTR(host_access_count, 0444, dasd_access_show, NULL);

static ssize_t
dasd_discipline_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct dasd_device *device;
	ssize_t len;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		goto out;
	else if (!device->discipline) {
		dasd_put_device(device);
		goto out;
	} else {
		len = snprintf(buf, PAGE_SIZE, "%s\n",
			       device->discipline->name);
		dasd_put_device(device);
		return len;
	}
out:
	len = snprintf(buf, PAGE_SIZE, "none\n");
	return len;
}

static DEVICE_ATTR(discipline, 0444, dasd_discipline_show, NULL);

static ssize_t
dasd_device_status_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct dasd_device *device;
	ssize_t len;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (!IS_ERR(device)) {
		switch (device->state) {
		case DASD_STATE_NEW:
			len = snprintf(buf, PAGE_SIZE, "new\n");
			break;
		case DASD_STATE_KNOWN:
			len = snprintf(buf, PAGE_SIZE, "detected\n");
			break;
		case DASD_STATE_BASIC:
			len = snprintf(buf, PAGE_SIZE, "basic\n");
			break;
		case DASD_STATE_UNFMT:
			len = snprintf(buf, PAGE_SIZE, "unformatted\n");
			break;
		case DASD_STATE_READY:
			len = snprintf(buf, PAGE_SIZE, "ready\n");
			break;
		case DASD_STATE_ONLINE:
			len = snprintf(buf, PAGE_SIZE, "online\n");
			break;
		default:
			len = snprintf(buf, PAGE_SIZE, "no stat\n");
			break;
		}
		dasd_put_device(device);
	} else
		len = snprintf(buf, PAGE_SIZE, "unknown\n");
	return len;
}

static DEVICE_ATTR(status, 0444, dasd_device_status_show, NULL);

static ssize_t dasd_alias_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct dasd_device *device;
	struct dasd_uid uid;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return sprintf(buf, "0\n");

	if (device->discipline && device->discipline->get_uid &&
	    !device->discipline->get_uid(device, &uid)) {
		if (uid.type == UA_BASE_PAV_ALIAS ||
		    uid.type == UA_HYPER_PAV_ALIAS) {
			dasd_put_device(device);
			return sprintf(buf, "1\n");
		}
	}
	dasd_put_device(device);

	return sprintf(buf, "0\n");
}

static DEVICE_ATTR(alias, 0444, dasd_alias_show, NULL);

static ssize_t dasd_vendor_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct dasd_device *device;
	struct dasd_uid uid;
	char *vendor;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	vendor = "";
	if (IS_ERR(device))
		return snprintf(buf, PAGE_SIZE, "%s\n", vendor);

	if (device->discipline && device->discipline->get_uid &&
	    !device->discipline->get_uid(device, &uid))
			vendor = uid.vendor;

	dasd_put_device(device);

	return snprintf(buf, PAGE_SIZE, "%s\n", vendor);
}

static DEVICE_ATTR(vendor, 0444, dasd_vendor_show, NULL);

#define UID_STRLEN ( /* vendor */ 3 + 1 + /* serial    */ 14 + 1 +\
		     /* SSID   */ 4 + 1 + /* unit addr */ 2 + 1 +\
		     /* vduit */ 32 + 1)

static ssize_t
dasd_uid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dasd_device *device;
	struct dasd_uid uid;
	char uid_string[UID_STRLEN];
	char ua_string[3];

	device = dasd_device_from_cdev(to_ccwdev(dev));
	uid_string[0] = 0;
	if (IS_ERR(device))
		return snprintf(buf, PAGE_SIZE, "%s\n", uid_string);

	if (device->discipline && device->discipline->get_uid &&
	    !device->discipline->get_uid(device, &uid)) {
		switch (uid.type) {
		case UA_BASE_DEVICE:
			snprintf(ua_string, sizeof(ua_string), "%02x",
				 uid.real_unit_addr);
			break;
		case UA_BASE_PAV_ALIAS:
			snprintf(ua_string, sizeof(ua_string), "%02x",
				 uid.base_unit_addr);
			break;
		case UA_HYPER_PAV_ALIAS:
			snprintf(ua_string, sizeof(ua_string), "xx");
			break;
		default:
			/* should not happen, treat like base device */
			snprintf(ua_string, sizeof(ua_string), "%02x",
				 uid.real_unit_addr);
			break;
		}

		if (strlen(uid.vduit) > 0)
			snprintf(uid_string, sizeof(uid_string),
				 "%s.%s.%04x.%s.%s",
				 uid.vendor, uid.serial, uid.ssid, ua_string,
				 uid.vduit);
		else
			snprintf(uid_string, sizeof(uid_string),
				 "%s.%s.%04x.%s",
				 uid.vendor, uid.serial, uid.ssid, ua_string);
	}
	dasd_put_device(device);

	return snprintf(buf, PAGE_SIZE, "%s\n", uid_string);
}
static DEVICE_ATTR(uid, 0444, dasd_uid_show, NULL);

/*
 * extended error-reporting
 */
static ssize_t
dasd_eer_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dasd_devmap *devmap;
	int eer_flag;

	devmap = dasd_find_busid(dev_name(dev));
	if (!IS_ERR(devmap) && devmap->device)
		eer_flag = dasd_eer_enabled(devmap->device);
	else
		eer_flag = 0;
	return snprintf(buf, PAGE_SIZE, eer_flag ? "1\n" : "0\n");
}

static ssize_t
dasd_eer_store(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct dasd_device *device;
	unsigned int val;
	int rc = 0;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return PTR_ERR(device);

	if (kstrtouint(buf, 0, &val) || val > 1)
		return -EINVAL;

	if (val)
		rc = dasd_eer_enable(device);
	else
		dasd_eer_disable(device);

	dasd_put_device(device);

	return rc ? : count;
}

static DEVICE_ATTR(eer_enabled, 0644, dasd_eer_show, dasd_eer_store);

/*
 * expiration time for default requests
 */
static ssize_t
dasd_expires_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dasd_device *device;
	int len;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return -ENODEV;
	len = snprintf(buf, PAGE_SIZE, "%lu\n", device->default_expires);
	dasd_put_device(device);
	return len;
}

static ssize_t
dasd_expires_store(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct dasd_device *device;
	unsigned long val;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return -ENODEV;

	if ((kstrtoul(buf, 10, &val) != 0) ||
	    (val > DASD_EXPIRES_MAX) || val == 0) {
		dasd_put_device(device);
		return -EINVAL;
	}

	if (val)
		device->default_expires = val;

	dasd_put_device(device);
	return count;
}

static DEVICE_ATTR(expires, 0644, dasd_expires_show, dasd_expires_store);

static ssize_t
dasd_retries_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dasd_device *device;
	int len;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return -ENODEV;
	len = snprintf(buf, PAGE_SIZE, "%lu\n", device->default_retries);
	dasd_put_device(device);
	return len;
}

static ssize_t
dasd_retries_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct dasd_device *device;
	unsigned long val;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return -ENODEV;

	if ((kstrtoul(buf, 10, &val) != 0) ||
	    (val > DASD_RETRIES_MAX)) {
		dasd_put_device(device);
		return -EINVAL;
	}

	if (val)
		device->default_retries = val;

	dasd_put_device(device);
	return count;
}

static DEVICE_ATTR(retries, 0644, dasd_retries_show, dasd_retries_store);

static ssize_t
dasd_timeout_show(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	struct dasd_device *device;
	int len;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return -ENODEV;
	len = snprintf(buf, PAGE_SIZE, "%lu\n", device->blk_timeout);
	dasd_put_device(device);
	return len;
}

static ssize_t
dasd_timeout_store(struct device *dev, struct device_attribute *attr,
		   const char *buf, size_t count)
{
	struct dasd_device *device;
	struct request_queue *q;
	unsigned long val, flags;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device) || !device->block)
		return -ENODEV;

	if ((kstrtoul(buf, 10, &val) != 0) ||
	    val > UINT_MAX / HZ) {
		dasd_put_device(device);
		return -EINVAL;
	}
	q = device->block->request_queue;
	if (!q) {
		dasd_put_device(device);
		return -ENODEV;
	}
	spin_lock_irqsave(&device->block->request_queue_lock, flags);
	if (!val)
		blk_queue_rq_timed_out(q, NULL);
	else
		blk_queue_rq_timed_out(q, dasd_times_out);

	device->blk_timeout = val;

	blk_queue_rq_timeout(q, device->blk_timeout * HZ);
	spin_unlock_irqrestore(&device->block->request_queue_lock, flags);

	dasd_put_device(device);
	return count;
}

static DEVICE_ATTR(timeout, 0644,
		   dasd_timeout_show, dasd_timeout_store);


static ssize_t
dasd_path_reset_store(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct dasd_device *device;
	unsigned int val;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return -ENODEV;

	if ((kstrtouint(buf, 16, &val) != 0) || val > 0xff)
		val = 0;

	if (device->discipline && device->discipline->reset_path)
		device->discipline->reset_path(device, (__u8) val);

	dasd_put_device(device);
	return count;
}

static DEVICE_ATTR(path_reset, 0200, NULL, dasd_path_reset_store);

static ssize_t dasd_hpf_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct dasd_device *device;
	int hpf;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return -ENODEV;
	if (!device->discipline || !device->discipline->hpf_enabled) {
		dasd_put_device(device);
		return snprintf(buf, PAGE_SIZE, "%d\n", dasd_nofcx);
	}
	hpf = device->discipline->hpf_enabled(device);
	dasd_put_device(device);
	return snprintf(buf, PAGE_SIZE, "%d\n", hpf);
}

static DEVICE_ATTR(hpf, 0444, dasd_hpf_show, NULL);

static ssize_t dasd_reservation_policy_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct dasd_devmap *devmap;
	int rc = 0;

	devmap = dasd_find_busid(dev_name(dev));
	if (IS_ERR(devmap)) {
		rc = snprintf(buf, PAGE_SIZE, "ignore\n");
	} else {
		spin_lock(&dasd_devmap_lock);
		if (devmap->features & DASD_FEATURE_FAILONSLCK)
			rc = snprintf(buf, PAGE_SIZE, "fail\n");
		else
			rc = snprintf(buf, PAGE_SIZE, "ignore\n");
		spin_unlock(&dasd_devmap_lock);
	}
	return rc;
}

static ssize_t dasd_reservation_policy_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct ccw_device *cdev = to_ccwdev(dev);
	int rc;

	if (sysfs_streq("ignore", buf))
		rc = dasd_set_feature(cdev, DASD_FEATURE_FAILONSLCK, 0);
	else if (sysfs_streq("fail", buf))
		rc = dasd_set_feature(cdev, DASD_FEATURE_FAILONSLCK, 1);
	else
		rc = -EINVAL;

	return rc ? : count;
}

static DEVICE_ATTR(reservation_policy, 0644,
		   dasd_reservation_policy_show, dasd_reservation_policy_store);

static ssize_t dasd_reservation_state_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct dasd_device *device;
	int rc = 0;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return snprintf(buf, PAGE_SIZE, "none\n");

	if (test_bit(DASD_FLAG_IS_RESERVED, &device->flags))
		rc = snprintf(buf, PAGE_SIZE, "reserved\n");
	else if (test_bit(DASD_FLAG_LOCK_STOLEN, &device->flags))
		rc = snprintf(buf, PAGE_SIZE, "lost\n");
	else
		rc = snprintf(buf, PAGE_SIZE, "none\n");
	dasd_put_device(device);
	return rc;
}

static ssize_t dasd_reservation_state_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct dasd_device *device;
	int rc = 0;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return -ENODEV;
	if (sysfs_streq("reset", buf))
		clear_bit(DASD_FLAG_LOCK_STOLEN, &device->flags);
	else
		rc = -EINVAL;
	dasd_put_device(device);

	if (rc)
		return rc;
	else
		return count;
}

static DEVICE_ATTR(last_known_reservation_state, 0644,
		   dasd_reservation_state_show, dasd_reservation_state_store);

static ssize_t dasd_pm_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct dasd_device *device;
	u8 opm, nppm, cablepm, cuirpm, hpfpm, ifccpm;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return sprintf(buf, "0\n");

	opm = dasd_path_get_opm(device);
	nppm = dasd_path_get_nppm(device);
	cablepm = dasd_path_get_cablepm(device);
	cuirpm = dasd_path_get_cuirpm(device);
	hpfpm = dasd_path_get_hpfpm(device);
	ifccpm = dasd_path_get_ifccpm(device);
	dasd_put_device(device);

	return sprintf(buf, "%02x %02x %02x %02x %02x %02x\n", opm, nppm,
		       cablepm, cuirpm, hpfpm, ifccpm);
}

static DEVICE_ATTR(path_masks, 0444, dasd_pm_show, NULL);

/*
 * threshold value for IFCC/CCC errors
 */
static ssize_t
dasd_path_threshold_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct dasd_device *device;
	int len;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return -ENODEV;
	len = snprintf(buf, PAGE_SIZE, "%lu\n", device->path_thrhld);
	dasd_put_device(device);
	return len;
}

static ssize_t
dasd_path_threshold_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct dasd_device *device;
	unsigned long flags;
	unsigned long val;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return -ENODEV;

	if (kstrtoul(buf, 10, &val) != 0 || val > DASD_THRHLD_MAX) {
		dasd_put_device(device);
		return -EINVAL;
	}
	spin_lock_irqsave(get_ccwdev_lock(to_ccwdev(dev)), flags);
	device->path_thrhld = val;
	spin_unlock_irqrestore(get_ccwdev_lock(to_ccwdev(dev)), flags);
	dasd_put_device(device);
	return count;
}

static DEVICE_ATTR(path_threshold, 0644, dasd_path_threshold_show,
		   dasd_path_threshold_store);
/*
 * interval for IFCC/CCC checks
 * meaning time with no IFCC/CCC error before the error counter
 * gets reset
 */
static ssize_t
dasd_path_interval_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct dasd_device *device;
	int len;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return -ENODEV;
	len = snprintf(buf, PAGE_SIZE, "%lu\n", device->path_interval);
	dasd_put_device(device);
	return len;
}

static ssize_t
dasd_path_interval_store(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct dasd_device *device;
	unsigned long flags;
	unsigned long val;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (IS_ERR(device))
		return -ENODEV;

	if ((kstrtoul(buf, 10, &val) != 0) ||
	    (val > DASD_INTERVAL_MAX) || val == 0) {
		dasd_put_device(device);
		return -EINVAL;
	}
	spin_lock_irqsave(get_ccwdev_lock(to_ccwdev(dev)), flags);
	if (val)
		device->path_interval = val;
	spin_unlock_irqrestore(get_ccwdev_lock(to_ccwdev(dev)), flags);
	dasd_put_device(device);
	return count;
}

static DEVICE_ATTR(path_interval, 0644, dasd_path_interval_show,
		   dasd_path_interval_store);


static struct attribute * dasd_attrs[] = {
	&dev_attr_readonly.attr,
	&dev_attr_discipline.attr,
	&dev_attr_status.attr,
	&dev_attr_alias.attr,
	&dev_attr_vendor.attr,
	&dev_attr_uid.attr,
	&dev_attr_use_diag.attr,
	&dev_attr_raw_track_access.attr,
	&dev_attr_eer_enabled.attr,
	&dev_attr_erplog.attr,
	&dev_attr_failfast.attr,
	&dev_attr_expires.attr,
	&dev_attr_retries.attr,
	&dev_attr_timeout.attr,
	&dev_attr_reservation_policy.attr,
	&dev_attr_last_known_reservation_state.attr,
	&dev_attr_safe_offline.attr,
	&dev_attr_host_access_count.attr,
	&dev_attr_path_masks.attr,
	&dev_attr_path_threshold.attr,
	&dev_attr_path_interval.attr,
	&dev_attr_path_reset.attr,
	&dev_attr_hpf.attr,
	NULL,
};

static struct attribute_group dasd_attr_group = {
	.attrs = dasd_attrs,
};

/*
 * Return value of the specified feature.
 */
int
dasd_get_feature(struct ccw_device *cdev, int feature)
{
	struct dasd_devmap *devmap;

	devmap = dasd_find_busid(dev_name(&cdev->dev));
	if (IS_ERR(devmap))
		return PTR_ERR(devmap);

	return ((devmap->features & feature) != 0);
}

/*
 * Set / reset given feature.
 * Flag indicates whether to set (!=0) or the reset (=0) the feature.
 */
int
dasd_set_feature(struct ccw_device *cdev, int feature, int flag)
{
	struct dasd_devmap *devmap;

	devmap = dasd_devmap_from_cdev(cdev);
	if (IS_ERR(devmap))
		return PTR_ERR(devmap);

	spin_lock(&dasd_devmap_lock);
	if (flag)
		devmap->features |= feature;
	else
		devmap->features &= ~feature;
	if (devmap->device)
		devmap->device->features = devmap->features;
	spin_unlock(&dasd_devmap_lock);
	return 0;
}


int
dasd_add_sysfs_files(struct ccw_device *cdev)
{
	return sysfs_create_group(&cdev->dev.kobj, &dasd_attr_group);
}

void
dasd_remove_sysfs_files(struct ccw_device *cdev)
{
	sysfs_remove_group(&cdev->dev.kobj, &dasd_attr_group);
}


int
dasd_devmap_init(void)
{
	int i;

	/* Initialize devmap structures. */
	dasd_max_devindex = 0;
	for (i = 0; i < 256; i++)
		INIT_LIST_HEAD(&dasd_hashlists[i]);
	return 0;
}

void
dasd_devmap_exit(void)
{
	dasd_forget_ranges();
}
