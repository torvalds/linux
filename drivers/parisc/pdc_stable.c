/* 
 *    Interfaces to retrieve and set PDC Stable options (firmware)
 *
 *    Copyright (C) 2005-2006 Thibaut VARENE <varenet@parisc-linux.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License, version 2, as
 *    published by the Free Software Foundation.
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
 *    following code can deal with just 96 bytes of Stable Storage, and all
 *    sizes between 96 and 192 bytes (provided they are multiple of struct
 *    device_path size, eg: 128, 160 and 192) to provide full information.
 *    One last word: there's one path we can always count on: the primary path.
 *    Anything above 224 bytes is used for 'osdep2' OS-dependent storage area.
 *
 *    The first OS-dependent area should always be available. Obviously, this is
 *    not true for the other one. Also bear in mind that reading/writing from/to
 *    osdep2 is much more expensive than from/to osdep1.
 *    NOTE: We do not handle the 2 bytes OS-dep area at 0x5D, nor the first
 *    2 bytes of storage available right after OSID. That's a total of 4 bytes
 *    sacrificed: -ETOOLAZY :P
 *
 *    The current policy wrt file permissions is:
 *	- write: root only
 *	- read: (reading triggers PDC calls) ? root only : everyone
 *    The rationale is that PDC calls could hog (DoS) the machine.
 *
 *	TODO:
 *	- timer/fastsize write calls
 */

#undef PDCS_DEBUG
#ifdef PDCS_DEBUG
#define DPRINTK(fmt, args...)	printk(KERN_DEBUG fmt, ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/capability.h>
#include <linux/ctype.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/spinlock.h>

#include <asm/pdc.h>
#include <asm/page.h>
#include <linux/uaccess.h>
#include <asm/hardware.h>

#define PDCS_VERSION	"0.30"
#define PDCS_PREFIX	"PDC Stable Storage"

#define PDCS_ADDR_PPRI	0x00
#define PDCS_ADDR_OSID	0x40
#define PDCS_ADDR_OSD1	0x48
#define PDCS_ADDR_DIAG	0x58
#define PDCS_ADDR_FSIZ	0x5C
#define PDCS_ADDR_PCON	0x60
#define PDCS_ADDR_PALT	0x80
#define PDCS_ADDR_PKBD	0xA0
#define PDCS_ADDR_OSD2	0xE0

MODULE_AUTHOR("Thibaut VARENE <varenet@parisc-linux.org>");
MODULE_DESCRIPTION("sysfs interface to HP PDC Stable Storage data");
MODULE_LICENSE("GPL");
MODULE_VERSION(PDCS_VERSION);

/* holds Stable Storage size. Initialized once and for all, no lock needed */
static unsigned long pdcs_size __read_mostly;

/* holds OS ID. Initialized once and for all, hopefully to 0x0006 */
static u16 pdcs_osid __read_mostly;

/* This struct defines what we need to deal with a parisc pdc path entry */
struct pdcspath_entry {
	rwlock_t rw_lock;		/* to protect path entry access */
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
struct kobj_attribute pdcs_attr_##_name = { \
	.attr = {.name = __stringify(_name), .mode = _mode}, \
	.show = _show, \
	.store = _store, \
};

#define PATHS_ATTR(_name, _mode, _show, _store) \
struct pdcspath_attribute paths_attr_##_name = { \
	.attr = {.name = __stringify(_name), .mode = _mode}, \
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
 * you access the files provided by the facilities. We store a copy of the
 * content of the stable storage WRT various paths in these structs. We read
 * these structs when reading the files, and we will write to these structs when
 * writing to the files, and only then write them back to the Stable Storage.
 *
 * This function expects to be called with @entry->rw_lock write-hold.
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
 *
 * This function expects to be called with @entry->rw_lock write-hold.
 */
static void
pdcspath_store(struct pdcspath_entry *entry)
{
	struct device_path *devpath;

	BUG_ON(!entry);

	devpath = &entry->devpath;
	
	/* We expect the caller to set the ready flag to 0 if the hardware
	   path struct provided is invalid, so that we know we have to fill it.
	   First case, we don't have a preset hwpath... */
	if (!entry->ready) {
		/* ...but we have a device, map it */
		BUG_ON(!entry->dev);
		device_to_hwpath(entry->dev, (struct hardware_path *)devpath);
	}
	/* else, we expect the provided hwpath to be valid. */
	
	DPRINTK("%s: store: 0x%p, 0x%p, addr: 0x%lx\n", __func__,
			entry, devpath, entry->addr);

	/* addr, devpath and count must be word aligned */
	if (pdc_stable_write(entry->addr, devpath, sizeof(*devpath)) != PDC_OK)
		WARN(1, KERN_ERR "%s: an error occurred when writing to PDC.\n"
				"It is likely that the Stable Storage data has been corrupted.\n"
				"Please check it carefully upon next reboot.\n", __func__);
		
	/* kobject is already registered */
	entry->ready = 2;
	
	DPRINTK("%s: device: 0x%p\n", __func__, entry->dev);
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
	short i;

	if (!entry || !buf)
		return -EINVAL;

	read_lock(&entry->rw_lock);
	devpath = &entry->devpath;
	i = entry->ready;
	read_unlock(&entry->rw_lock);

	if (!i)	/* entry is not ready */
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
 * We make sure that the provided path actually maps to an existing
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
	char in[64], *temp;
	struct device *dev;
	int ret;

	if (!entry || !buf || !count)
		return -EINVAL;

	/* We'll use a local copy of buf */
	count = min_t(size_t, count, sizeof(in)-1);
	strncpy(in, buf, count);
	in[count] = '\0';
	
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
	write_lock(&entry->rw_lock);
	entry->ready = 0;
	entry->dev = dev;
	
	/* Now, dive in. Write back to the hardware */
	pdcspath_store(entry);
	
	/* Update the symlink to the real device */
	sysfs_remove_link(&entry->kobj, "device");
	ret = sysfs_create_link(&entry->kobj, &entry->dev->kobj, "device");
	WARN_ON(ret);

	write_unlock(&entry->rw_lock);
	
	printk(KERN_INFO PDCS_PREFIX ": changed \"%s\" path to \"%s\"\n",
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
	short i;

	if (!entry || !buf)
		return -EINVAL;
	
	read_lock(&entry->rw_lock);
	devpath = &entry->devpath;
	i = entry->ready;
	read_unlock(&entry->rw_lock);

	if (!i)	/* entry is not ready */
		return -ENODATA;
	
	for (i = 0; i < 6 && devpath->layers[i]; i++)
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
	char in[64], *temp;

	if (!entry || !buf || !count)
		return -EINVAL;

	/* We'll use a local copy of buf */
	count = min_t(size_t, count, sizeof(in)-1);
	strncpy(in, buf, count);
	in[count] = '\0';
	
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
	write_lock(&entry->rw_lock);
	
	/* First, overwrite the current layers with the new ones, not touching
	   the hardware path. */
	memcpy(&entry->devpath.layers, &layers, sizeof(layers));
	
	/* Now, dive in. Write back to the hardware */
	pdcspath_store(entry);
	write_unlock(&entry->rw_lock);
	
	printk(KERN_INFO PDCS_PREFIX ": changed \"%s\" layers to \"%s\"\n",
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

static const struct sysfs_ops pdcspath_attr_ops = {
	.show = pdcspath_attr_show,
	.store = pdcspath_attr_store,
};

/* These are the two attributes of any PDC path. */
static PATHS_ATTR(hwpath, 0644, pdcspath_hwpath_read, pdcspath_hwpath_write);
static PATHS_ATTR(layer, 0644, pdcspath_layer_read, pdcspath_layer_write);

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


/* For more insight of what's going on here, refer to PDC Procedures doc,
 * Section PDC_STABLE */

/**
 * pdcs_size_read - Stable Storage size output.
 * @buf: The output buffer to write to.
 */
static ssize_t pdcs_size_read(struct kobject *kobj,
			      struct kobj_attribute *attr,
			      char *buf)
{
	char *out = buf;

	if (!buf)
		return -EINVAL;

	/* show the size of the stable storage */
	out += sprintf(out, "%ld\n", pdcs_size);

	return out - buf;
}

/**
 * pdcs_auto_read - Stable Storage autoboot/search flag output.
 * @buf: The output buffer to write to.
 * @knob: The PF_AUTOBOOT or PF_AUTOSEARCH flag
 */
static ssize_t pdcs_auto_read(struct kobject *kobj,
			      struct kobj_attribute *attr,
			      char *buf, int knob)
{
	char *out = buf;
	struct pdcspath_entry *pathentry;

	if (!buf)
		return -EINVAL;

	/* Current flags are stored in primary boot path entry */
	pathentry = &pdcspath_entry_primary;

	read_lock(&pathentry->rw_lock);
	out += sprintf(out, "%s\n", (pathentry->devpath.flags & knob) ?
					"On" : "Off");
	read_unlock(&pathentry->rw_lock);

	return out - buf;
}

/**
 * pdcs_autoboot_read - Stable Storage autoboot flag output.
 * @buf: The output buffer to write to.
 */
static ssize_t pdcs_autoboot_read(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	return pdcs_auto_read(kobj, attr, buf, PF_AUTOBOOT);
}

/**
 * pdcs_autosearch_read - Stable Storage autoboot flag output.
 * @buf: The output buffer to write to.
 */
static ssize_t pdcs_autosearch_read(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	return pdcs_auto_read(kobj, attr, buf, PF_AUTOSEARCH);
}

/**
 * pdcs_timer_read - Stable Storage timer count output (in seconds).
 * @buf: The output buffer to write to.
 *
 * The value of the timer field correponds to a number of seconds in powers of 2.
 */
static ssize_t pdcs_timer_read(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	char *out = buf;
	struct pdcspath_entry *pathentry;

	if (!buf)
		return -EINVAL;

	/* Current flags are stored in primary boot path entry */
	pathentry = &pdcspath_entry_primary;

	/* print the timer value in seconds */
	read_lock(&pathentry->rw_lock);
	out += sprintf(out, "%u\n", (pathentry->devpath.flags & PF_TIMER) ?
				(1 << (pathentry->devpath.flags & PF_TIMER)) : 0);
	read_unlock(&pathentry->rw_lock);

	return out - buf;
}

/**
 * pdcs_osid_read - Stable Storage OS ID register output.
 * @buf: The output buffer to write to.
 */
static ssize_t pdcs_osid_read(struct kobject *kobj,
			      struct kobj_attribute *attr, char *buf)
{
	char *out = buf;

	if (!buf)
		return -EINVAL;

	out += sprintf(out, "%s dependent data (0x%.4x)\n",
		os_id_to_string(pdcs_osid), pdcs_osid);

	return out - buf;
}

/**
 * pdcs_osdep1_read - Stable Storage OS-Dependent data area 1 output.
 * @buf: The output buffer to write to.
 *
 * This can hold 16 bytes of OS-Dependent data.
 */
static ssize_t pdcs_osdep1_read(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	char *out = buf;
	u32 result[4];

	if (!buf)
		return -EINVAL;

	if (pdc_stable_read(PDCS_ADDR_OSD1, &result, sizeof(result)) != PDC_OK)
		return -EIO;

	out += sprintf(out, "0x%.8x\n", result[0]);
	out += sprintf(out, "0x%.8x\n", result[1]);
	out += sprintf(out, "0x%.8x\n", result[2]);
	out += sprintf(out, "0x%.8x\n", result[3]);

	return out - buf;
}

/**
 * pdcs_diagnostic_read - Stable Storage Diagnostic register output.
 * @buf: The output buffer to write to.
 *
 * I have NFC how to interpret the content of that register ;-).
 */
static ssize_t pdcs_diagnostic_read(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	char *out = buf;
	u32 result;

	if (!buf)
		return -EINVAL;

	/* get diagnostic */
	if (pdc_stable_read(PDCS_ADDR_DIAG, &result, sizeof(result)) != PDC_OK)
		return -EIO;

	out += sprintf(out, "0x%.4x\n", (result >> 16));

	return out - buf;
}

/**
 * pdcs_fastsize_read - Stable Storage FastSize register output.
 * @buf: The output buffer to write to.
 *
 * This register holds the amount of system RAM to be tested during boot sequence.
 */
static ssize_t pdcs_fastsize_read(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	char *out = buf;
	u32 result;

	if (!buf)
		return -EINVAL;

	/* get fast-size */
	if (pdc_stable_read(PDCS_ADDR_FSIZ, &result, sizeof(result)) != PDC_OK)
		return -EIO;

	if ((result & 0x0F) < 0x0E)
		out += sprintf(out, "%d kB", (1<<(result & 0x0F))*256);
	else
		out += sprintf(out, "All");
	out += sprintf(out, "\n");
	
	return out - buf;
}

/**
 * pdcs_osdep2_read - Stable Storage OS-Dependent data area 2 output.
 * @buf: The output buffer to write to.
 *
 * This can hold pdcs_size - 224 bytes of OS-Dependent data, when available.
 */
static ssize_t pdcs_osdep2_read(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	char *out = buf;
	unsigned long size;
	unsigned short i;
	u32 result;

	if (unlikely(pdcs_size <= 224))
		return -ENODATA;

	size = pdcs_size - 224;

	if (!buf)
		return -EINVAL;

	for (i=0; i<size; i+=4) {
		if (unlikely(pdc_stable_read(PDCS_ADDR_OSD2 + i, &result,
					sizeof(result)) != PDC_OK))
			return -EIO;
		out += sprintf(out, "0x%.8x\n", result);
	}

	return out - buf;
}

/**
 * pdcs_auto_write - This function handles autoboot/search flag modifying.
 * @buf: The input buffer to read from.
 * @count: The number of bytes to be read.
 * @knob: The PF_AUTOBOOT or PF_AUTOSEARCH flag
 * 
 * We will call this function to change the current autoboot flag.
 * We expect a precise syntax:
 *	\"n\" (n == 0 or 1) to toggle AutoBoot Off or On
 */
static ssize_t pdcs_auto_write(struct kobject *kobj,
			       struct kobj_attribute *attr, const char *buf,
			       size_t count, int knob)
{
	struct pdcspath_entry *pathentry;
	unsigned char flags;
	char in[8], *temp;
	char c;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (!buf || !count)
		return -EINVAL;

	/* We'll use a local copy of buf */
	count = min_t(size_t, count, sizeof(in)-1);
	strncpy(in, buf, count);
	in[count] = '\0';

	/* Current flags are stored in primary boot path entry */
	pathentry = &pdcspath_entry_primary;
	
	/* Be nice to the existing flag record */
	read_lock(&pathentry->rw_lock);
	flags = pathentry->devpath.flags;
	read_unlock(&pathentry->rw_lock);
	
	DPRINTK("%s: flags before: 0x%X\n", __func__, flags);

	temp = skip_spaces(in);

	c = *temp++ - '0';
	if ((c != 0) && (c != 1))
		goto parse_error;
	if (c == 0)
		flags &= ~knob;
	else
		flags |= knob;
	
	DPRINTK("%s: flags after: 0x%X\n", __func__, flags);
		
	/* So far so good, let's get in deep */
	write_lock(&pathentry->rw_lock);
	
	/* Change the path entry flags first */
	pathentry->devpath.flags = flags;
		
	/* Now, dive in. Write back to the hardware */
	pdcspath_store(pathentry);
	write_unlock(&pathentry->rw_lock);
	
	printk(KERN_INFO PDCS_PREFIX ": changed \"%s\" to \"%s\"\n",
		(knob & PF_AUTOBOOT) ? "autoboot" : "autosearch",
		(flags & knob) ? "On" : "Off");
	
	return count;

parse_error:
	printk(KERN_WARNING "%s: Parse error: expect \"n\" (n == 0 or 1)\n", __func__);
	return -EINVAL;
}

/**
 * pdcs_autoboot_write - This function handles autoboot flag modifying.
 * @buf: The input buffer to read from.
 * @count: The number of bytes to be read.
 *
 * We will call this function to change the current boot flags.
 * We expect a precise syntax:
 *	\"n\" (n == 0 or 1) to toggle AutoSearch Off or On
 */
static ssize_t pdcs_autoboot_write(struct kobject *kobj,
				   struct kobj_attribute *attr,
				   const char *buf, size_t count)
{
	return pdcs_auto_write(kobj, attr, buf, count, PF_AUTOBOOT);
}

/**
 * pdcs_autosearch_write - This function handles autosearch flag modifying.
 * @buf: The input buffer to read from.
 * @count: The number of bytes to be read.
 *
 * We will call this function to change the current boot flags.
 * We expect a precise syntax:
 *	\"n\" (n == 0 or 1) to toggle AutoSearch Off or On
 */
static ssize_t pdcs_autosearch_write(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t count)
{
	return pdcs_auto_write(kobj, attr, buf, count, PF_AUTOSEARCH);
}

/**
 * pdcs_osdep1_write - Stable Storage OS-Dependent data area 1 input.
 * @buf: The input buffer to read from.
 * @count: The number of bytes to be read.
 *
 * This can store 16 bytes of OS-Dependent data. We use a byte-by-byte
 * write approach. It's up to userspace to deal with it when constructing
 * its input buffer.
 */
static ssize_t pdcs_osdep1_write(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t count)
{
	u8 in[16];

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (!buf || !count)
		return -EINVAL;

	if (unlikely(pdcs_osid != OS_ID_LINUX))
		return -EPERM;

	if (count > 16)
		return -EMSGSIZE;

	/* We'll use a local copy of buf */
	memset(in, 0, 16);
	memcpy(in, buf, count);

	if (pdc_stable_write(PDCS_ADDR_OSD1, &in, sizeof(in)) != PDC_OK)
		return -EIO;

	return count;
}

/**
 * pdcs_osdep2_write - Stable Storage OS-Dependent data area 2 input.
 * @buf: The input buffer to read from.
 * @count: The number of bytes to be read.
 *
 * This can store pdcs_size - 224 bytes of OS-Dependent data. We use a
 * byte-by-byte write approach. It's up to userspace to deal with it when
 * constructing its input buffer.
 */
static ssize_t pdcs_osdep2_write(struct kobject *kobj,
				 struct kobj_attribute *attr,
				 const char *buf, size_t count)
{
	unsigned long size;
	unsigned short i;
	u8 in[4];

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (!buf || !count)
		return -EINVAL;

	if (unlikely(pdcs_size <= 224))
		return -ENOSYS;

	if (unlikely(pdcs_osid != OS_ID_LINUX))
		return -EPERM;

	size = pdcs_size - 224;

	if (count > size)
		return -EMSGSIZE;

	/* We'll use a local copy of buf */

	for (i=0; i<count; i+=4) {
		memset(in, 0, 4);
		memcpy(in, buf+i, (count-i < 4) ? count-i : 4);
		if (unlikely(pdc_stable_write(PDCS_ADDR_OSD2 + i, &in,
					sizeof(in)) != PDC_OK))
			return -EIO;
	}

	return count;
}

/* The remaining attributes. */
static PDCS_ATTR(size, 0444, pdcs_size_read, NULL);
static PDCS_ATTR(autoboot, 0644, pdcs_autoboot_read, pdcs_autoboot_write);
static PDCS_ATTR(autosearch, 0644, pdcs_autosearch_read, pdcs_autosearch_write);
static PDCS_ATTR(timer, 0444, pdcs_timer_read, NULL);
static PDCS_ATTR(osid, 0444, pdcs_osid_read, NULL);
static PDCS_ATTR(osdep1, 0600, pdcs_osdep1_read, pdcs_osdep1_write);
static PDCS_ATTR(diagnostic, 0400, pdcs_diagnostic_read, NULL);
static PDCS_ATTR(fastsize, 0400, pdcs_fastsize_read, NULL);
static PDCS_ATTR(osdep2, 0600, pdcs_osdep2_read, pdcs_osdep2_write);

static struct attribute *pdcs_subsys_attrs[] = {
	&pdcs_attr_size.attr,
	&pdcs_attr_autoboot.attr,
	&pdcs_attr_autosearch.attr,
	&pdcs_attr_timer.attr,
	&pdcs_attr_osid.attr,
	&pdcs_attr_osdep1.attr,
	&pdcs_attr_diagnostic.attr,
	&pdcs_attr_fastsize.attr,
	&pdcs_attr_osdep2.attr,
	NULL,
};

static struct attribute_group pdcs_attr_group = {
	.attrs = pdcs_subsys_attrs,
};

static struct kobject *stable_kobj;
static struct kset *paths_kset;

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
	int err;
	
	/* Initialize the entries rw_lock before anything else */
	for (i = 0; (entry = pdcspath_entries[i]); i++)
		rwlock_init(&entry->rw_lock);

	for (i = 0; (entry = pdcspath_entries[i]); i++) {
		write_lock(&entry->rw_lock);
		err = pdcspath_fetch(entry);
		write_unlock(&entry->rw_lock);

		if (err < 0)
			continue;

		entry->kobj.kset = paths_kset;
		err = kobject_init_and_add(&entry->kobj, &ktype_pdcspath, NULL,
					   "%s", entry->name);
		if (err)
			return err;

		/* kobject is now registered */
		write_lock(&entry->rw_lock);
		entry->ready = 2;
		
		/* Add a nice symlink to the real device */
		if (entry->dev) {
			err = sysfs_create_link(&entry->kobj, &entry->dev->kobj, "device");
			WARN_ON(err);
		}

		write_unlock(&entry->rw_lock);
		kobject_uevent(&entry->kobj, KOBJ_ADD);
	}
	
	return 0;
}

/**
 * pdcs_unregister_pathentries - Routine called when unregistering the module.
 */
static inline void
pdcs_unregister_pathentries(void)
{
	unsigned short i;
	struct pdcspath_entry *entry;
	
	for (i = 0; (entry = pdcspath_entries[i]); i++) {
		read_lock(&entry->rw_lock);
		if (entry->ready >= 2)
			kobject_put(&entry->kobj);
		read_unlock(&entry->rw_lock);
	}
}

/*
 * For now we register the stable subsystem with the firmware subsystem
 * and the paths subsystem with the stable subsystem
 */
static int __init
pdc_stable_init(void)
{
	int rc = 0, error = 0;
	u32 result;

	/* find the size of the stable storage */
	if (pdc_stable_get_size(&pdcs_size) != PDC_OK) 
		return -ENODEV;

	/* make sure we have enough data */
	if (pdcs_size < 96)
		return -ENODATA;

	printk(KERN_INFO PDCS_PREFIX " facility v%s\n", PDCS_VERSION);

	/* get OSID */
	if (pdc_stable_read(PDCS_ADDR_OSID, &result, sizeof(result)) != PDC_OK)
		return -EIO;

	/* the actual result is 16 bits away */
	pdcs_osid = (u16)(result >> 16);

	/* For now we'll register the directory at /sys/firmware/stable */
	stable_kobj = kobject_create_and_add("stable", firmware_kobj);
	if (!stable_kobj) {
		rc = -ENOMEM;
		goto fail_firmreg;
	}

	/* Don't forget the root entries */
	error = sysfs_create_group(stable_kobj, &pdcs_attr_group);

	/* register the paths kset as a child of the stable kset */
	paths_kset = kset_create_and_add("paths", NULL, stable_kobj);
	if (!paths_kset) {
		rc = -ENOMEM;
		goto fail_ksetreg;
	}

	/* now we create all "files" for the paths kset */
	if ((rc = pdcs_register_pathentries()))
		goto fail_pdcsreg;

	return rc;
	
fail_pdcsreg:
	pdcs_unregister_pathentries();
	kset_unregister(paths_kset);
	
fail_ksetreg:
	kobject_put(stable_kobj);
	
fail_firmreg:
	printk(KERN_INFO PDCS_PREFIX " bailing out\n");
	return rc;
}

static void __exit
pdc_stable_exit(void)
{
	pdcs_unregister_pathentries();
	kset_unregister(paths_kset);
	kobject_put(stable_kobj);
}


module_init(pdc_stable_init);
module_exit(pdc_stable_exit);
