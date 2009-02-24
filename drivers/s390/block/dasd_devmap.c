/*
 * File...........: linux/drivers/s390/block/dasd_devmap.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 *		    Horst Hummel <Horst.Hummel@de.ibm.com>
 *		    Carsten Otte <Cotte@de.ibm.com>
 *		    Martin Schwidefsky <schwidefsky@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999-2001
 *
 * Device mapping and dasd= parameter parsing functions. All devmap
 * functions may not be called from interrupt context. In particular
 * dasd_get_device is a no-no from interrupt context.
 *
 */

#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/module.h>

#include <asm/debug.h>
#include <asm/uaccess.h>
#include <asm/ipl.h>

/* This is ugly... */
#define PRINTK_HEADER "dasd_devmap:"
#define DASD_BUS_ID_SIZE 20

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
	struct dasd_uid uid;
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

/*
 * char *dasd[] is intended to hold the ranges supplied by the dasd= statement
 * it is named 'dasd' to directly be filled by insmod with the comma separated
 * strings when running as a module.
 */
static char *dasd[256];
module_param_array(dasd, charp, NULL, 0);

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
/*
 * The parameter parsing functions for builtin-drivers are called
 * before kmalloc works. Store the pointers to the parameters strings
 * into dasd[] for later processing.
 */
static int __init
dasd_call_setup(char *str)
{
	static int count = 0;

	if (count < 256)
		dasd[count++] = str;
	return 1;
}

__setup ("dasd=", dasd_call_setup);
#endif	/* #ifndef MODULE */

#define	DASD_IPLDEV	"ipldev"

/*
 * Read a device busid/devno from a string.
 */
static int
dasd_busid(char **str, int *id0, int *id1, int *devno)
{
	int val, old_style;

	/* Interpret ipldev busid */
	if (strncmp(DASD_IPLDEV, *str, strlen(DASD_IPLDEV)) == 0) {
		if (ipl_info.type != IPL_TYPE_CCW) {
			MESSAGE(KERN_ERR, "%s", "ipl device is not a ccw "
				"device");
			return -EINVAL;
		}
		*id0 = 0;
		*id1 = ipl_info.data.ccw.dev_id.ssid;
		*devno = ipl_info.data.ccw.dev_id.devno;
		*str += strlen(DASD_IPLDEV);

		return 0;
	}
	/* check for leading '0x' */
	old_style = 0;
	if ((*str)[0] == '0' && (*str)[1] == 'x') {
		*str += 2;
		old_style = 1;
	}
	if (!isxdigit((*str)[0]))	/* We require at least one hex digit */
		return -EINVAL;
	val = simple_strtoul(*str, str, 16);
	if (old_style || (*str)[0] != '.') {
		*id0 = *id1 = 0;
		if (val < 0 || val > 0xffff)
			return -EINVAL;
		*devno = val;
		return 0;
	}
	/* New style x.y.z busid */
	if (val < 0 || val > 0xff)
		return -EINVAL;
	*id0 = val;
	(*str)++;
	if (!isxdigit((*str)[0]))	/* We require at least one hex digit */
		return -EINVAL;
	val = simple_strtoul(*str, str, 16);
	if (val < 0 || val > 0xff || (*str)++[0] != '.')
		return -EINVAL;
	*id1 = val;
	if (!isxdigit((*str)[0]))	/* We require at least one hex digit */
		return -EINVAL;
	val = simple_strtoul(*str, str, 16);
	if (val < 0 || val > 0xffff)
		return -EINVAL;
	*devno = val;
	return 0;
}

/*
 * Read colon separated list of dasd features. Currently there is
 * only one: "ro" for read-only devices. The default feature set
 * is empty (value 0).
 */
static int
dasd_feature_list(char *str, char **endp)
{
	int features, len, rc;

	rc = 0;
	if (*str != '(') {
		*endp = str;
		return DASD_FEATURE_DEFAULT;
	}
	str++;
	features = 0;

	while (1) {
		for (len = 0;
		     str[len] && str[len] != ':' && str[len] != ')'; len++);
		if (len == 2 && !strncmp(str, "ro", 2))
			features |= DASD_FEATURE_READONLY;
		else if (len == 4 && !strncmp(str, "diag", 4))
			features |= DASD_FEATURE_USEDIAG;
		else if (len == 6 && !strncmp(str, "erplog", 6))
			features |= DASD_FEATURE_ERPLOG;
		else if (len == 8 && !strncmp(str, "failfast", 8))
			features |= DASD_FEATURE_FAILFAST;
		else {
			MESSAGE(KERN_WARNING,
				"unsupported feature: %*s, "
				"ignoring setting", len, str);
			rc = -EINVAL;
		}
		str += len;
		if (*str != ':')
			break;
		str++;
	}
	if (*str != ')') {
		MESSAGE(KERN_WARNING, "%s",
			"missing ')' in dasd parameter string\n");
		rc = -EINVAL;
	} else
		str++;
	*endp = str;
	if (rc != 0)
		return rc;
	return features;
}

/*
 * Try to match the first element on the comma separated parse string
 * with one of the known keywords. If a keyword is found, take the approprate
 * action and return a pointer to the residual string. If the first element
 * could not be matched to any keyword then return an error code.
 */
static char *
dasd_parse_keyword( char *parsestring ) {

	char *nextcomma, *residual_str;
	int length;

	nextcomma = strchr(parsestring,',');
	if (nextcomma) {
		length = nextcomma - parsestring;
		residual_str = nextcomma + 1;
	} else {
		length = strlen(parsestring);
		residual_str = parsestring + length;
        }
	if (strncmp("autodetect", parsestring, length) == 0) {
		dasd_autodetect = 1;
		MESSAGE (KERN_INFO, "%s",
			 "turning to autodetection mode");
                return residual_str;
        }
	if (strncmp("probeonly", parsestring, length) == 0) {
		dasd_probeonly = 1;
		MESSAGE(KERN_INFO, "%s",
			"turning to probeonly mode");
                return residual_str;
        }
	if (strncmp("nopav", parsestring, length) == 0) {
		if (MACHINE_IS_VM)
			MESSAGE(KERN_INFO, "%s", "'nopav' not supported on VM");
		else {
			dasd_nopav = 1;
			MESSAGE(KERN_INFO, "%s", "disable PAV mode");
		}
		return residual_str;
	}
	if (strncmp("fixedbuffers", parsestring, length) == 0) {
		if (dasd_page_cache)
			return residual_str;
		dasd_page_cache =
			kmem_cache_create("dasd_page_cache", PAGE_SIZE,
					  PAGE_SIZE, SLAB_CACHE_DMA,
					  NULL);
		if (!dasd_page_cache)
			MESSAGE(KERN_WARNING, "%s", "Failed to create slab, "
				"fixed buffer mode disabled.");
		else
			MESSAGE (KERN_INFO, "%s",
				 "turning on fixed buffer mode");
                return residual_str;
        }
	return ERR_PTR(-EINVAL);
}

/*
 * Try to interprete the first element on the comma separated parse string
 * as a device number or a range of devices. If the interpretation is
 * successfull, create the matching dasd_devmap entries and return a pointer
 * to the residual string.
 * If interpretation fails or in case of an error, return an error code.
 */
static char *
dasd_parse_range( char *parsestring ) {

	struct dasd_devmap *devmap;
	int from, from_id0, from_id1;
	int to, to_id0, to_id1;
	int features, rc;
	char bus_id[DASD_BUS_ID_SIZE+1], *str;

	str = parsestring;
	rc = dasd_busid(&str, &from_id0, &from_id1, &from);
	if (rc == 0) {
		to = from;
		to_id0 = from_id0;
		to_id1 = from_id1;
		if (*str == '-') {
			str++;
			rc = dasd_busid(&str, &to_id0, &to_id1, &to);
		}
	}
	if (rc == 0 &&
	    (from_id0 != to_id0 || from_id1 != to_id1 || from > to))
		rc = -EINVAL;
	if (rc) {
		MESSAGE(KERN_ERR, "Invalid device range %s", parsestring);
		return ERR_PTR(rc);
	}
	features = dasd_feature_list(str, &str);
	if (features < 0)
		return ERR_PTR(-EINVAL);
	/* each device in dasd= parameter should be set initially online */
	features |= DASD_FEATURE_INITIAL_ONLINE;
	while (from <= to) {
		sprintf(bus_id, "%01x.%01x.%04x",
			from_id0, from_id1, from++);
		devmap = dasd_add_busid(bus_id, features);
		if (IS_ERR(devmap))
			return (char *)devmap;
	}
	if (*str == ',')
		return str + 1;
	if (*str == '\0')
		return str;
	MESSAGE(KERN_WARNING,
		"junk at end of dasd parameter string: %s\n", str);
	return ERR_PTR(-EINVAL);
}

static char *
dasd_parse_next_element( char *parsestring ) {
	char * residual_str;
	residual_str = dasd_parse_keyword(parsestring);
	if (!IS_ERR(residual_str))
		return residual_str;
	residual_str = dasd_parse_range(parsestring);
	return residual_str;
}

/*
 * Parse parameters stored in dasd[]
 * The 'dasd=...' parameter allows to specify a comma separated list of
 * keywords and device ranges. When the dasd driver is build into the kernel,
 * the complete list will be stored as one element of the dasd[] array.
 * When the dasd driver is build as a module, then the list is broken into
 * it's elements and each dasd[] entry contains one element.
 */
int
dasd_parse(void)
{
	int rc, i;
	char *parsestring;

	rc = 0;
	for (i = 0; i < 256; i++) {
		if (dasd[i] == NULL)
			break;
		parsestring = dasd[i];
		/* loop over the comma separated list in the parsestring */
		while (*parsestring) {
			parsestring = dasd_parse_next_element(parsestring);
			if(IS_ERR(parsestring)) {
				rc = PTR_ERR(parsestring);
				break;
			}
		}
		if (rc) {
			DBF_EVENT(DBF_ALERT, "%s", "invalid range found");
			break;
		}
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

	new = (struct dasd_devmap *)
		kzalloc(sizeof(struct dasd_devmap), GFP_KERNEL);
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
	struct dasd_devmap *devmap;
	int val;
	char *endp;

	devmap = dasd_devmap_from_cdev(to_ccwdev(dev));
	if (IS_ERR(devmap))
		return PTR_ERR(devmap);

	val = simple_strtoul(buf, &endp, 0);
	if (((endp + 1) < (buf + count)) || (val > 1))
		return -EINVAL;

	spin_lock(&dasd_devmap_lock);
	if (val)
		devmap->features |= DASD_FEATURE_FAILFAST;
	else
		devmap->features &= ~DASD_FEATURE_FAILFAST;
	if (devmap->device)
		devmap->device->features = devmap->features;
	spin_unlock(&dasd_devmap_lock);
	return count;
}

static DEVICE_ATTR(failfast, 0644, dasd_ff_show, dasd_ff_store);

/*
 * readonly controls the readonly status of a dasd
 */
static ssize_t
dasd_ro_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dasd_devmap *devmap;
	int ro_flag;

	devmap = dasd_find_busid(dev_name(dev));
	if (!IS_ERR(devmap))
		ro_flag = (devmap->features & DASD_FEATURE_READONLY) != 0;
	else
		ro_flag = (DASD_FEATURE_DEFAULT & DASD_FEATURE_READONLY) != 0;
	return snprintf(buf, PAGE_SIZE, ro_flag ? "1\n" : "0\n");
}

static ssize_t
dasd_ro_store(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct dasd_devmap *devmap;
	int val;
	char *endp;

	devmap = dasd_devmap_from_cdev(to_ccwdev(dev));
	if (IS_ERR(devmap))
		return PTR_ERR(devmap);

	val = simple_strtoul(buf, &endp, 0);
	if (((endp + 1) < (buf + count)) || (val > 1))
		return -EINVAL;

	spin_lock(&dasd_devmap_lock);
	if (val)
		devmap->features |= DASD_FEATURE_READONLY;
	else
		devmap->features &= ~DASD_FEATURE_READONLY;
	if (devmap->device)
		devmap->device->features = devmap->features;
	if (devmap->device && devmap->device->block
	    && devmap->device->block->gdp)
		set_disk_ro(devmap->device->block->gdp, val);
	spin_unlock(&dasd_devmap_lock);
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
	struct dasd_devmap *devmap;
	int val;
	char *endp;

	devmap = dasd_devmap_from_cdev(to_ccwdev(dev));
	if (IS_ERR(devmap))
		return PTR_ERR(devmap);

	val = simple_strtoul(buf, &endp, 0);
	if (((endp + 1) < (buf + count)) || (val > 1))
		return -EINVAL;

	spin_lock(&dasd_devmap_lock);
	if (val)
		devmap->features |= DASD_FEATURE_ERPLOG;
	else
		devmap->features &= ~DASD_FEATURE_ERPLOG;
	if (devmap->device)
		devmap->device->features = devmap->features;
	spin_unlock(&dasd_devmap_lock);
	return count;
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
	ssize_t rc;
	int val;
	char *endp;

	devmap = dasd_devmap_from_cdev(to_ccwdev(dev));
	if (IS_ERR(devmap))
		return PTR_ERR(devmap);

	val = simple_strtoul(buf, &endp, 0);
	if (((endp + 1) < (buf + count)) || (val > 1))
		return -EINVAL;

	spin_lock(&dasd_devmap_lock);
	/* Changing diag discipline flag is only allowed in offline state. */
	rc = count;
	if (!devmap->device) {
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

static ssize_t
dasd_discipline_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct dasd_device *device;
	ssize_t len;

	device = dasd_device_from_cdev(to_ccwdev(dev));
	if (!IS_ERR(device) && device->discipline) {
		len = snprintf(buf, PAGE_SIZE, "%s\n",
			       device->discipline->name);
		dasd_put_device(device);
	} else
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

static ssize_t
dasd_alias_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dasd_devmap *devmap;
	int alias;

	devmap = dasd_find_busid(dev_name(dev));
	spin_lock(&dasd_devmap_lock);
	if (IS_ERR(devmap) || strlen(devmap->uid.vendor) == 0) {
		spin_unlock(&dasd_devmap_lock);
		return sprintf(buf, "0\n");
	}
	if (devmap->uid.type == UA_BASE_PAV_ALIAS ||
	    devmap->uid.type == UA_HYPER_PAV_ALIAS)
		alias = 1;
	else
		alias = 0;
	spin_unlock(&dasd_devmap_lock);
	return sprintf(buf, alias ? "1\n" : "0\n");
}

static DEVICE_ATTR(alias, 0444, dasd_alias_show, NULL);

static ssize_t
dasd_vendor_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dasd_devmap *devmap;
	char *vendor;

	devmap = dasd_find_busid(dev_name(dev));
	spin_lock(&dasd_devmap_lock);
	if (!IS_ERR(devmap) && strlen(devmap->uid.vendor) > 0)
		vendor = devmap->uid.vendor;
	else
		vendor = "";
	spin_unlock(&dasd_devmap_lock);

	return snprintf(buf, PAGE_SIZE, "%s\n", vendor);
}

static DEVICE_ATTR(vendor, 0444, dasd_vendor_show, NULL);

#define UID_STRLEN ( /* vendor */ 3 + 1 + /* serial    */ 14 + 1 +\
		     /* SSID   */ 4 + 1 + /* unit addr */ 2 + 1 +\
		     /* vduit */ 32 + 1)

static ssize_t
dasd_uid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dasd_devmap *devmap;
	char uid_string[UID_STRLEN];
	char ua_string[3];
	struct dasd_uid *uid;

	devmap = dasd_find_busid(dev_name(dev));
	spin_lock(&dasd_devmap_lock);
	if (IS_ERR(devmap) || strlen(devmap->uid.vendor) == 0) {
		spin_unlock(&dasd_devmap_lock);
		return sprintf(buf, "\n");
	}
	uid = &devmap->uid;
	switch (uid->type) {
	case UA_BASE_DEVICE:
		sprintf(ua_string, "%02x", uid->real_unit_addr);
		break;
	case UA_BASE_PAV_ALIAS:
		sprintf(ua_string, "%02x", uid->base_unit_addr);
		break;
	case UA_HYPER_PAV_ALIAS:
		sprintf(ua_string, "xx");
		break;
	default:
		/* should not happen, treat like base device */
		sprintf(ua_string, "%02x", uid->real_unit_addr);
		break;
	}
	if (strlen(uid->vduit) > 0)
		snprintf(uid_string, sizeof(uid_string),
			 "%s.%s.%04x.%s.%s",
			 uid->vendor, uid->serial,
			 uid->ssid, ua_string,
			 uid->vduit);
	else
		snprintf(uid_string, sizeof(uid_string),
			 "%s.%s.%04x.%s",
			 uid->vendor, uid->serial,
			 uid->ssid, ua_string);
	spin_unlock(&dasd_devmap_lock);
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
	struct dasd_devmap *devmap;
	int val, rc;
	char *endp;

	devmap = dasd_devmap_from_cdev(to_ccwdev(dev));
	if (IS_ERR(devmap))
		return PTR_ERR(devmap);
	if (!devmap->device)
		return -ENODEV;

	val = simple_strtoul(buf, &endp, 0);
	if (((endp + 1) < (buf + count)) || (val > 1))
		return -EINVAL;

	if (val) {
		rc = dasd_eer_enable(devmap->device);
		if (rc)
			return rc;
	} else
		dasd_eer_disable(devmap->device);
	return count;
}

static DEVICE_ATTR(eer_enabled, 0644, dasd_eer_show, dasd_eer_store);

static struct attribute * dasd_attrs[] = {
	&dev_attr_readonly.attr,
	&dev_attr_discipline.attr,
	&dev_attr_status.attr,
	&dev_attr_alias.attr,
	&dev_attr_vendor.attr,
	&dev_attr_uid.attr,
	&dev_attr_use_diag.attr,
	&dev_attr_eer_enabled.attr,
	&dev_attr_erplog.attr,
	&dev_attr_failfast.attr,
	NULL,
};

static struct attribute_group dasd_attr_group = {
	.attrs = dasd_attrs,
};

/*
 * Return copy of the device unique identifier.
 */
int
dasd_get_uid(struct ccw_device *cdev, struct dasd_uid *uid)
{
	struct dasd_devmap *devmap;

	devmap = dasd_find_busid(dev_name(&cdev->dev));
	if (IS_ERR(devmap))
		return PTR_ERR(devmap);
	spin_lock(&dasd_devmap_lock);
	*uid = devmap->uid;
	spin_unlock(&dasd_devmap_lock);
	return 0;
}

/*
 * Register the given device unique identifier into devmap struct.
 * In addition check if the related storage server subsystem ID is already
 * contained in the dasd_server_ssid_list. If subsystem ID is not contained,
 * create new entry.
 * Return 0 if server was already in serverlist,
 *	  1 if the server was added successful
 *	 <0 in case of error.
 */
int
dasd_set_uid(struct ccw_device *cdev, struct dasd_uid *uid)
{
	struct dasd_devmap *devmap;

	devmap = dasd_find_busid(dev_name(&cdev->dev));
	if (IS_ERR(devmap))
		return PTR_ERR(devmap);

	spin_lock(&dasd_devmap_lock);
	devmap->uid = *uid;
	spin_unlock(&dasd_devmap_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(dasd_set_uid);

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
 * Flag indicates wether to set (!=0) or the reset (=0) the feature.
 */
int
dasd_set_feature(struct ccw_device *cdev, int feature, int flag)
{
	struct dasd_devmap *devmap;

	devmap = dasd_find_busid(dev_name(&cdev->dev));
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
