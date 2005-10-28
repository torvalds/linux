/* 
 *    Interfaces to retrieve and set PDC Stable options (firmware)
 *
 *    Copyright (C) 2005 Thibaut VARENE <varenet@parisc-linux.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *
 *    DEV NOTE: the PDC Procedures reference states that:
 *    "A minimum of 96 bytes of Stable Storage is required. Providing more than
 *    96 bytes of Stable Storage is optional [...]. Failure to provide the
 *    optional locations from 96 to 192 results in the loss of certain
 *    functionality during boot."
 *
 *    Since locations between 96 and 192 are the various paths, most (if not
 *    all) PA-RISC machines should have them. Anyway, for safety reasons, the
 *    following code can deal with only 96 bytes of Stable Storage, and all
 *    sizes between 96 and 192 bytes (provided they are multiple of struct
 *    device_path size, eg: 128, 160 and 192) to provide full information.
 *    The code makes no use of data above 192 bytes. One last word: there's one
 *    path we can always count on: the primary path.
 */

#undef PDCS_DEBUG
#ifdef PDCS_DEBUG
#define DPRINTK(fmt, args...)	printk(KERN_DEBUG fmt, ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>		/* for capable() */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/errno.h>

#include <asm/pdc.h>
#include <asm/page.h>
#include <asm/uaccess.h>
#include <asm/hardware.h>

#define PDCS_VERSION	"0.09"

#define PDCS_ADDR_PPRI	0x00
#define PDCS_ADDR_OSID	0x40
#define PDCS_ADDR_FSIZ	0x5C
#define PDCS_ADDR_PCON	0x60
#define PDCS_ADDR_PALT	0x80
#define PDCS_ADDR_PKBD	0xA0

MODULE_AUTHOR("Thibaut VARENE <varenet@parisc-linux.org>");
MODULE_DESCRIPTION("sysfs interface to HP PDC Stable Storage data");
MODULE_LICENSE("GPL");
MODULE_VERSION(PDCS_VERSION);

static unsigned long pdcs_size = 0;

/* This struct defines what we need to deal with a parisc pdc path entry */
struct pdcspath_entry {
	short ready;			/* entry record is valid if != 0 */
	unsigned long addr;		/* entry address in stable storage */
	char *name;			/* entry name */
	struct device_path devpath;	/* device path in parisc representation */
	struct device *dev;		/* corresponding device */
	struct kobject kobj;
};

struct pdcspath_attribute {
	struct attribute attr;
	ssize_t (*show)(struct pdcspath_entry *entry, char *buf);
	ssize_t (*store)(struct pdcspath_entry *entry, const char *buf, size_t count);
};

#define PDCSPATH_ENTRY(_addr, _name) \
struct pdcspath_entry pdcspath_entry_##_name = { \
	.ready = 0, \
	.addr = _addr, \
	.name = __stringify(_name), \
};

#define PDCS_ATTR(_name, _mode, _show, _store) \
struct subsys_attribute pdcs_attr_##_name = { \
	.attr = {.name = __stringify(_name), .mode = _mode, .owner = THIS_MODULE}, \
	.show = _show, \
	.store = _store, \
};

#define PATHS_ATTR(_name, _mode, _show, _store) \
struct pdcspath_attribute paths_attr_##_name = { \
	.attr = {.name = __stringify(_name), .mode = _mode, .owner = THIS_MODULE}, \
	.show = _show, \
	.store = _store, \
};

#define to_pdcspath_attribute(_attr) container_of(_attr, struct pdcspath_attribute, attr)
#define to_pdcspath_entry(obj)  container_of(obj, struct pdcspath_entry, kobj)

/**
 * pdcspath_fetch - This function populates the path entry structs.
 * @entry: A pointer to an allocated pdcspath_entry.
 * 
 * The general idea is that you don't read from the Stable Storage every time
 * you access the files provided by the facilites. We store a copy of the
 * content of the stable storage WRT various paths in these structs. We read
 * these structs when reading the files, and we will write to these structs when
 * writing to the files, and only then write them back to the Stable Storage.
 */
static int
pdcspath_fetch(struct pdcspath_entry *entry)
{
	struct device_path *devpath;

	if (!entry)
		return -EINVAL;

	devpath = &entry->devpath;
	
	DPRINTK("%s: fetch: 0x%p, 0x%p, addr: 0x%lx\n", __func__,
			entry, devpath, entry->addr);

	/* addr, devpath and count must be word aligned */
	if (pdc_stable_read(entry->addr, devpath, sizeof(*devpath)) != PDC_OK)
		return -EIO;
		
	/* Find the matching device.
	   NOTE: hardware_path overlays with device_path, so the nice cast can
	   be used */
	entry->dev = hwpath_to_device((struct hardware_path *)devpath);

	entry->ready = 1;
	
	DPRINTK("%s: device: 0x%p\n", __func__, entry->dev);
	
	return 0;
}

/**
 * pdcspath_store - This function writes a path to stable storage.
 * @entry: A pointer to an allocated pdcspath_entry.
 * 
 * It can be used in two ways: either by passing it a preset devpath struct
 * containing an already computed hardware path, or by passing it a device
 * pointer, from which it'll find out the corresponding hardware path.
 * For now we do not handle the case where there's an error in writing to the
 * Stable Storage area, so you'd better not mess up the data :P
 */
static int
pdcspath_store(struct pdcspath_entry *entry)
{
	struct device_path *devpath;

	if (!entry)
		return -EINVAL;

	devpath = &entry->devpath;
	
	/* We expect the caller to set the ready flag to 0 if the hardware
	   path struct provided is invalid, so that we know we have to fill it.
	   First case, we don't have a preset hwpath... */
	if (!entry->ready) {
		/* ...but we have a device, map it */
		if (entry->dev)
			device_to_hwpath(entry->dev, (struct hardware_path *)devpath);
		else
			return -EINVAL;
	}
	/* else, we expect the provided hwpath to be valid. */
	
	DPRINTK("%s: store: 0x%p, 0x%p, addr: 0x%lx\n", __func__,
			entry, devpath, entry->addr);

	/* addr, devpath and count must be word aligned */
	if (pdc_stable_write(entry->addr, devpath, sizeof(*devpath)) != PDC_OK) {
		printk(KERN_ERR "%s: an error occured when writing to PDC.\n"
				"It is likely that the Stable Storage data has been corrupted.\n"
				"Please check it carefully upon next reboot.\n", __func__);
		return -EIO;
	}
		
	entry->ready = 1;
	
	DPRINTK("%s: device: 0x%p\n", __func__, entry->dev);
	
	return 0;
}

/**
 * pdcspath_hwpath_read - This function handles hardware path pretty printing.
 * @entry: An allocated and populated pdscpath_entry struct.
 * @buf: The output buffer to write to.
 * 
 * We will call this function to format the output of the hwpath attribute file.
 */
static ssize_t
pdcspath_hwpath_read(struct pdcspath_entry *entry, char *buf)
{
	char *out = buf;
	struct device_path *devpath;
	unsigned short i;

	if (!entry || !buf)
		return -EINVAL;

	devpath = &entry->devpath;

	if (!entry->ready)
		return -ENODATA;
	
	for (i = 0; i < 6; i++) {
		if (devpath->bc[i] >= 128)
			continue;
		out += sprintf(out, "%u/", (unsigned char)devpath->bc[i]);
	}
	out += sprintf(out, "%u\n", (unsigned char)devpath->mod);
	
	return out - buf;
}

/**
 * pdcspath_hwpath_write - This function handles hardware path modifying.
 * @entry: An allocated and populated pdscpath_entry struct.
 * @buf: The input buffer to read from.
 * @count: The number of bytes to be read.
 * 
 * We will call this function to change the current hardware path.
 * Hardware paths are to be given '/'-delimited, without brackets.
 * We take care to make sure that the provided path actually maps to an existing
 * device, BUT nothing would prevent some foolish user to set the path to some
 * PCI bridge or even a CPU...
 * A better work around would be to make sure we are at the end of a device tree
 * for instance, but it would be IMHO beyond the simple scope of that driver.
 * The aim is to provide a facility. Data correctness is left to userland.
 */
static ssize_t
pdcspath_hwpath_write(struct pdcspath_entry *entry, const char *buf, size_t count)
{
	struct hardware_path hwpath;
	unsigned short i;
	char in[count+1], *temp;
	struct device *dev;

	if (!entry || !buf || !count)
		return -EINVAL;

	/* We'll use a local copy of buf */
	memset(in, 0, count+1);
	strncpy(in, buf, count);
	
	/* Let's clean up the target. 0xff is a blank pattern */
	memset(&hwpath, 0xff, sizeof(hwpath));
	
	/* First, pick the mod field (the last one of the input string) */
	if (!(temp = strrchr(in, '/')))
		return -EINVAL;
			
	hwpath.mod = simple_strtoul(temp+1, NULL, 10);
	in[temp-in] = '\0';	/* truncate the remaining string. just precaution */
	DPRINTK("%s: mod: %d\n", __func__, hwpath.mod);
	
	/* Then, loop for each delimiter, making sure we don't have too many.
	   we write the bc fields in a down-top way. No matter what, we stop
	   before writing the last field. If there are too many fields anyway,
	   then the user is a moron and it'll be caught up later when we'll
	   check the consistency of the given hwpath. */
	for (i=5; ((temp = strrchr(in, '/'))) && (temp-in > 0) && (likely(i)); i--) {
		hwpath.bc[i] = simple_strtoul(temp+1, NULL, 10);
		in[temp-in] = '\0';
		DPRINTK("%s: bc[%d]: %d\n", __func__, i, hwpath.bc[i]);
	}
	
	/* Store the final field */		
	hwpath.bc[i] = simple_strtoul(in, NULL, 10);
	DPRINTK("%s: bc[%d]: %d\n", __func__, i, hwpath.bc[i]);
	
	/* Now we check that the user isn't trying to lure us */
	if (!(dev = hwpath_to_device((struct hardware_path *)&hwpath))) {
		printk(KERN_WARNING "%s: attempt to set invalid \"%s\" "
			"hardware path: %s\n", __func__, entry->name, buf);
		return -EINVAL;
	}
	
	/* So far so good, let's get in deep */
	entry->ready = 0;
	entry->dev = dev;
	
	/* Now, dive in. Write back to the hardware */
	WARN_ON(pdcspath_store(entry));	/* this warn should *NEVER* happen */
	
	/* Update the symlink to the real device */
	sysfs_remove_link(&entry->kobj, "device");
	sysfs_create_link(&entry->kobj, &entry->dev->kobj, "device");
	
	printk(KERN_INFO "PDC Stable Storage: changed \"%s\" path to \"%s\"\n",
		entry->name, buf);
	
	return count;
}

/**
 * pdcspath_layer_read - Extended layer (eg. SCSI ids) pretty printing.
 * @entry: An allocated and populated pdscpath_entry struct.
 * @buf: The output buffer to write to.
 * 
 * We will call this function to format the output of the layer attribute file.
 */
static ssize_t
pdcspath_layer_read(struct pdcspath_entry *entry, char *buf)
{
	char *out = buf;
	struct device_path *devpath;
	unsigned short i;

	if (!entry || !buf)
		return -EINVAL;
	
	devpath = &entry->devpath;

	if (!entry->ready)
		return -ENODATA;
	
	for (i = 0; devpath->layers[i] && (likely(i < 6)); i++)
		out += sprintf(out, "%u ", devpath->layers[i]);

	out += sprintf(out, "\n");
	
	return out - buf;
}

/**
 * pdcspath_layer_write - This function handles extended layer modifying.
 * @entry: An allocated and populated pdscpath_entry struct.
 * @buf: The input buffer to read from.
 * @count: The number of bytes to be read.
 * 
 * We will call this function to change the current layer value.
 * Layers are to be given '.'-delimited, without brackets.
 * XXX beware we are far less checky WRT input data provided than for hwpath.
 * Potential harm can be done, since there's no way to check the validity of
 * the layer fields.
 */
static ssize_t
pdcspath_layer_write(struct pdcspath_entry *entry, const char *buf, size_t count)
{
	unsigned int layers[6]; /* device-specific info (ctlr#, unit#, ...) */
	unsigned short i;
	char in[count+1], *temp;

	if (!entry || !buf || !count)
		return -EINVAL;

	/* We'll use a local copy of buf */
	memset(in, 0, count+1);
	strncpy(in, buf, count);
	
	/* Let's clean up the target. 0 is a blank pattern */
	memset(&layers, 0, sizeof(layers));
	
	/* First, pick the first layer */
	if (unlikely(!isdigit(*in)))
		return -EINVAL;
	layers[0] = simple_strtoul(in, NULL, 10);
	DPRINTK("%s: layer[0]: %d\n", __func__, layers[0]);
	
	temp = in;
	for (i=1; ((temp = strchr(temp, '.'))) && (likely(i<6)); i++) {
		if (unlikely(!isdigit(*(++temp))))
			return -EINVAL;
		layers[i] = simple_strtoul(temp, NULL, 10);
		DPRINTK("%s: layer[%d]: %d\n", __func__, i, layers[i]);
	}
		
	/* So far so good, let's get in deep */
	
	/* First, overwrite the current layers with the new ones, not touching
	   the hardware path. */
	memcpy(&entry->devpath.layers, &layers, sizeof(layers));
	
	/* Now, dive in. Write back to the hardware */
	WARN_ON(pdcspath_store(entry));	/* this warn should *NEVER* happen */
	
	printk(KERN_INFO "PDC Stable Storage: changed \"%s\" layers to \"%s\"\n",
		entry->name, buf);
	
	return count;
}

/**
 * pdcspath_attr_show - Generic read function call wrapper.
 * @kobj: The kobject to get info from.
 * @attr: The attribute looked upon.
 * @buf: The output buffer.
 */
static ssize_t
pdcspath_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct pdcspath_entry *entry = to_pdcspath_entry(kobj);
	struct pdcspath_attribute *pdcs_attr = to_pdcspath_attribute(attr);
	ssize_t ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (pdcs_attr->show)
		ret = pdcs_attr->show(entry, buf);

	return ret;
}

/**
 * pdcspath_attr_store - Generic write function call wrapper.
 * @kobj: The kobject to write info to.
 * @attr: The attribute to be modified.
 * @buf: The input buffer.
 * @count: The size of the buffer.
 */
static ssize_t
pdcspath_attr_store(struct kobject *kobj, struct attribute *attr,
			const char *buf, size_t count)
{
	struct pdcspath_entry *entry = to_pdcspath_entry(kobj);
	struct pdcspath_attribute *pdcs_attr = to_pdcspath_attribute(attr);
	ssize_t ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (pdcs_attr->store)
		ret = pdcs_attr->store(entry, buf, count);

	return ret;
}

static struct sysfs_ops pdcspath_attr_ops = {
	.show = pdcspath_attr_show,
	.store = pdcspath_attr_store,
};

/* These are the two attributes of any PDC path. */
static PATHS_ATTR(hwpath, 0600, pdcspath_hwpath_read, pdcspath_hwpath_write);
static PATHS_ATTR(layer, 0600, pdcspath_layer_read, pdcspath_layer_write);

static struct attribute *paths_subsys_attrs[] = {
	&paths_attr_hwpath.attr,
	&paths_attr_layer.attr,
	NULL,
};

/* Specific kobject type for our PDC paths */
static struct kobj_type ktype_pdcspath = {
	.sysfs_ops = &pdcspath_attr_ops,
	.default_attrs = paths_subsys_attrs,
};

/* We hard define the 4 types of path we expect to find */
static PDCSPATH_ENTRY(PDCS_ADDR_PPRI, primary);
static PDCSPATH_ENTRY(PDCS_ADDR_PCON, console);
static PDCSPATH_ENTRY(PDCS_ADDR_PALT, alternative);
static PDCSPATH_ENTRY(PDCS_ADDR_PKBD, keyboard);

/* An array containing all PDC paths we will deal with */
static struct pdcspath_entry *pdcspath_entries[] = {
	&pdcspath_entry_primary,
	&pdcspath_entry_alternative,
	&pdcspath_entry_console,
	&pdcspath_entry_keyboard,
	NULL,
};

/**
 * pdcs_info_read - Pretty printing of the remaining useful data.
 * @entry: An allocated and populated subsytem struct. We don't use it tho.
 * @buf: The output buffer to write to.
 * 
 * We will call this function to format the output of the 'info' attribute file.
 * Please refer to PDC Procedures documentation, section PDC_STABLE to get a
 * better insight of what we're doing here.
 */
static ssize_t
pdcs_info_read(struct subsystem *entry, char *buf)
{
	char *out = buf;
	__u32 result;
	struct device_path devpath;
	char *tmpstr = NULL;
	
	if (!entry || !buf)
		return -EINVAL;
		
	/* show the size of the stable storage */
	out += sprintf(out, "Stable Storage size: %ld bytes\n", pdcs_size);

	/* deal with flags */
	if (pdc_stable_read(PDCS_ADDR_PPRI, &devpath, sizeof(devpath)) != PDC_OK)
		return -EIO;
	
	out += sprintf(out, "Autoboot: %s\n", (devpath.flags & PF_AUTOBOOT) ? "On" : "Off");
	out += sprintf(out, "Autosearch: %s\n", (devpath.flags & PF_AUTOSEARCH) ? "On" : "Off");
	out += sprintf(out, "Timer: %u s\n", (devpath.flags & PF_TIMER) ? (1 << (devpath.flags & PF_TIMER)) : 0);

	/* get OSID */
	if (pdc_stable_read(PDCS_ADDR_OSID, &result, sizeof(result)) != PDC_OK)
		return -EIO;

	/* the actual result is 16 bits away */
	switch (result >> 16) {
		case 0x0000:	tmpstr = "No OS-dependent data"; break;
		case 0x0001:	tmpstr = "HP-UX dependent data"; break;
		case 0x0002:	tmpstr = "MPE-iX dependent data"; break;
		case 0x0003:	tmpstr = "OSF dependent data"; break;
		case 0x0004:	tmpstr = "HP-RT dependent data"; break;
		case 0x0005:	tmpstr = "Novell Netware dependent data"; break;
		default:	tmpstr = "Unknown"; break;
	}
	out += sprintf(out, "OS ID: %s (0x%.4x)\n", tmpstr, (result >> 16));

	/* get fast-size */
	if (pdc_stable_read(PDCS_ADDR_FSIZ, &result, sizeof(result)) != PDC_OK)
		return -EIO;

	out += sprintf(out, "Memory tested: ");
	if ((result & 0x0F) < 0x0E)
		out += sprintf(out, "%d kB", (1<<(result & 0x0F))*256);
	else
		out += sprintf(out, "All");
	out += sprintf(out, "\n");
	
	return out - buf;
}

/**
 * pdcs_info_write - This function handles boot flag modifying.
 * @entry: An allocated and populated subsytem struct. We don't use it tho.
 * @buf: The input buffer to read from.
 * @count: The number of bytes to be read.
 * 
 * We will call this function to change the current boot flags.
 * We expect a precise syntax:
 *	\"n n\" (n == 0 or 1) to toggle respectively AutoBoot and AutoSearch
 *
 * As of now there is no incentive on my side to provide more "knobs" to that
 * interface, since modifying the rest of the data is pretty meaningless when
 * the machine is running and for the expected use of that facility, such as
 * PALO setting up the boot disk when installing a Linux distribution...
 */
static ssize_t
pdcs_info_write(struct subsystem *entry, const char *buf, size_t count)
{
	struct pdcspath_entry *pathentry;
	unsigned char flags;
	char in[count+1], *temp;
	char c;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (!entry || !buf || !count)
		return -EINVAL;

	/* We'll use a local copy of buf */
	memset(in, 0, count+1);
	strncpy(in, buf, count);

	/* Current flags are stored in primary boot path entry */
	pathentry = &pdcspath_entry_primary;
	
	/* Be nice to the existing flag record */
	flags = pathentry->devpath.flags;
	
	DPRINTK("%s: flags before: 0x%X\n", __func__, flags);
			
	temp = in;
	
	while (*temp && isspace(*temp))
		temp++;
	
	c = *temp++ - '0';
	if ((c != 0) && (c != 1))
		goto parse_error;
	if (c == 0)
		flags &= ~PF_AUTOBOOT;
	else
		flags |= PF_AUTOBOOT;
	
	if (*temp++ != ' ')
		goto parse_error;
	
	c = *temp++ - '0';
	if ((c != 0) && (c != 1))
		goto parse_error;
	if (c == 0)
		flags &= ~PF_AUTOSEARCH;
	else
		flags |= PF_AUTOSEARCH;
	
	DPRINTK("%s: flags after: 0x%X\n", __func__, flags);
		
	/* So far so good, let's get in deep */
	
	/* Change the path entry flags first */
	pathentry->devpath.flags = flags;
		
	/* Now, dive in. Write back to the hardware */
	WARN_ON(pdcspath_store(pathentry));	/* this warn should *NEVER* happen */
	
	printk(KERN_INFO "PDC Stable Storage: changed flags to \"%s\"\n", buf);
	
	return count;

parse_error:
	printk(KERN_WARNING "%s: Parse error: expect \"n n\" (n == 0 or 1) for AB and AS\n", __func__);
	return -EINVAL;
}

/* The last attribute (the 'root' one actually) with all remaining data. */
static PDCS_ATTR(info, 0600, pdcs_info_read, pdcs_info_write);

static struct subsys_attribute *pdcs_subsys_attrs[] = {
	&pdcs_attr_info,
	NULL,	/* maybe more in the future? */
};

static decl_subsys(paths, &ktype_pdcspath, NULL);
static decl_subsys(pdc, NULL, NULL);

/**
 * pdcs_register_pathentries - Prepares path entries kobjects for sysfs usage.
 * 
 * It creates kobjects corresponding to each path entry with nice sysfs
 * links to the real device. This is where the magic takes place: when
 * registering the subsystem attributes during module init, each kobject hereby
 * created will show in the sysfs tree as a folder containing files as defined
 * by path_subsys_attr[].
 */
static inline int __init
pdcs_register_pathentries(void)
{
	unsigned short i;
	struct pdcspath_entry *entry;
	
	for (i = 0; (entry = pdcspath_entries[i]); i++) {
		if (pdcspath_fetch(entry) < 0)
			continue;

		kobject_set_name(&entry->kobj, "%s", entry->name);
		kobj_set_kset_s(entry, paths_subsys);
		kobject_register(&entry->kobj);

		if (!entry->dev)
			continue;

		/* Add a nice symlink to the real device */
		sysfs_create_link(&entry->kobj, &entry->dev->kobj, "device");
	}
	
	return 0;
}

/**
 * pdcs_unregister_pathentries - Routine called when unregistering the module.
 */
static inline void __exit
pdcs_unregister_pathentries(void)
{
	unsigned short i;
	struct pdcspath_entry *entry;
	
	for (i = 0; (entry = pdcspath_entries[i]); i++)
		if (entry->ready)
			kobject_unregister(&entry->kobj);	
}

/*
 * For now we register the pdc subsystem with the firmware subsystem
 * and the paths subsystem with the pdc subsystem
 */
static int __init
pdc_stable_init(void)
{
	struct subsys_attribute *attr;
	int i, rc = 0, error = 0;

	/* find the size of the stable storage */
	if (pdc_stable_get_size(&pdcs_size) != PDC_OK) 
		return -ENODEV;

	printk(KERN_INFO "PDC Stable Storage facility v%s\n", PDCS_VERSION);

	/* For now we'll register the pdc subsys within this driver */
	if ((rc = firmware_register(&pdc_subsys)))
		return rc;

	/* Don't forget the info entry */
	for (i = 0; (attr = pdcs_subsys_attrs[i]) && !error; i++)
		if (attr->show)
			error = subsys_create_file(&pdc_subsys, attr);
	
	/* register the paths subsys as a subsystem of pdc subsys */
	kset_set_kset_s(&paths_subsys, pdc_subsys);
	subsystem_register(&paths_subsys);

	/* now we create all "files" for the paths subsys */
	pdcs_register_pathentries();
	
	return 0;
}

static void __exit
pdc_stable_exit(void)
{
	pdcs_unregister_pathentries();
	subsystem_unregister(&paths_subsys);

	firmware_unregister(&pdc_subsys);
}


module_init(pdc_stable_init);
module_exit(pdc_stable_exit);
