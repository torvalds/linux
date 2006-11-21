/*
 * edac_mc kernel module
 * (C) 2005, 2006 Linux Networx (http://lnxi.com)
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written by Thayne Harbaugh
 * Based on work by Dan Hollis <goemon at anime dot net> and others.
 *	http://www.anime.net/~goemon/linux-ecc/
 *
 * Modified by Dave Peterson and Doug Thompson
 *
 */

#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/sysctl.h>
#include <linux/highmem.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/sysdev.h>
#include <linux/ctype.h>
#include <linux/kthread.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/edac.h>
#include "edac_mc.h"

#define EDAC_MC_VERSION "Ver: 2.0.1 " __DATE__


#ifdef CONFIG_EDAC_DEBUG
/* Values of 0 to 4 will generate output */
int edac_debug_level = 1;
EXPORT_SYMBOL_GPL(edac_debug_level);
#endif

/* EDAC Controls, setable by module parameter, and sysfs */
static int log_ue = 1;
static int log_ce = 1;
static int panic_on_ue;
static int poll_msec = 1000;

/* lock to memory controller's control array */
static DECLARE_MUTEX(mem_ctls_mutex);
static struct list_head mc_devices = LIST_HEAD_INIT(mc_devices);

static struct task_struct *edac_thread;

#ifdef CONFIG_PCI
static int check_pci_parity = 0;	/* default YES check PCI parity */
static int panic_on_pci_parity;		/* default no panic on PCI Parity */
static atomic_t pci_parity_count = ATOMIC_INIT(0);

static struct kobject edac_pci_kobj; /* /sys/devices/system/edac/pci */
static struct completion edac_pci_kobj_complete;
#endif	/* CONFIG_PCI */

/*  START sysfs data and methods */


static const char *mem_types[] = {
	[MEM_EMPTY] = "Empty",
	[MEM_RESERVED] = "Reserved",
	[MEM_UNKNOWN] = "Unknown",
	[MEM_FPM] = "FPM",
	[MEM_EDO] = "EDO",
	[MEM_BEDO] = "BEDO",
	[MEM_SDR] = "Unbuffered-SDR",
	[MEM_RDR] = "Registered-SDR",
	[MEM_DDR] = "Unbuffered-DDR",
	[MEM_RDDR] = "Registered-DDR",
	[MEM_RMBS] = "RMBS"
};

static const char *dev_types[] = {
	[DEV_UNKNOWN] = "Unknown",
	[DEV_X1] = "x1",
	[DEV_X2] = "x2",
	[DEV_X4] = "x4",
	[DEV_X8] = "x8",
	[DEV_X16] = "x16",
	[DEV_X32] = "x32",
	[DEV_X64] = "x64"
};

static const char *edac_caps[] = {
	[EDAC_UNKNOWN] = "Unknown",
	[EDAC_NONE] = "None",
	[EDAC_RESERVED] = "Reserved",
	[EDAC_PARITY] = "PARITY",
	[EDAC_EC] = "EC",
	[EDAC_SECDED] = "SECDED",
	[EDAC_S2ECD2ED] = "S2ECD2ED",
	[EDAC_S4ECD4ED] = "S4ECD4ED",
	[EDAC_S8ECD8ED] = "S8ECD8ED",
	[EDAC_S16ECD16ED] = "S16ECD16ED"
};

/* sysfs object: /sys/devices/system/edac */
static struct sysdev_class edac_class = {
	set_kset_name("edac"),
};

/* sysfs object:
 *	/sys/devices/system/edac/mc
 */
static struct kobject edac_memctrl_kobj;

/* We use these to wait for the reference counts on edac_memctrl_kobj and
 * edac_pci_kobj to reach 0.
 */
static struct completion edac_memctrl_kobj_complete;

/*
 * /sys/devices/system/edac/mc;
 *	data structures and methods
 */
static ssize_t memctrl_int_show(void *ptr, char *buffer)
{
	int *value = (int*) ptr;
	return sprintf(buffer, "%u\n", *value);
}

static ssize_t memctrl_int_store(void *ptr, const char *buffer, size_t count)
{
	int *value = (int*) ptr;

	if (isdigit(*buffer))
		*value = simple_strtoul(buffer, NULL, 0);

	return count;
}

struct memctrl_dev_attribute {
	struct attribute attr;
	void *value;
	ssize_t (*show)(void *,char *);
	ssize_t (*store)(void *, const char *, size_t);
};

/* Set of show/store abstract level functions for memory control object */
static ssize_t memctrl_dev_show(struct kobject *kobj,
		struct attribute *attr, char *buffer)
{
	struct memctrl_dev_attribute *memctrl_dev;
	memctrl_dev = (struct memctrl_dev_attribute*)attr;

	if (memctrl_dev->show)
		return memctrl_dev->show(memctrl_dev->value, buffer);

	return -EIO;
}

static ssize_t memctrl_dev_store(struct kobject *kobj, struct attribute *attr,
		const char *buffer, size_t count)
{
	struct memctrl_dev_attribute *memctrl_dev;
	memctrl_dev = (struct memctrl_dev_attribute*)attr;

	if (memctrl_dev->store)
		return memctrl_dev->store(memctrl_dev->value, buffer, count);

	return -EIO;
}

static struct sysfs_ops memctrlfs_ops = {
	.show   = memctrl_dev_show,
	.store  = memctrl_dev_store
};

#define MEMCTRL_ATTR(_name,_mode,_show,_store)			\
struct memctrl_dev_attribute attr_##_name = {			\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.value  = &_name,					\
	.show   = _show,					\
	.store  = _store,					\
};

#define MEMCTRL_STRING_ATTR(_name,_data,_mode,_show,_store)	\
struct memctrl_dev_attribute attr_##_name = {			\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.value  = _data,					\
	.show   = _show,					\
	.store  = _store,					\
};

/* csrow<id> control files */
MEMCTRL_ATTR(panic_on_ue,S_IRUGO|S_IWUSR,memctrl_int_show,memctrl_int_store);
MEMCTRL_ATTR(log_ue,S_IRUGO|S_IWUSR,memctrl_int_show,memctrl_int_store);
MEMCTRL_ATTR(log_ce,S_IRUGO|S_IWUSR,memctrl_int_show,memctrl_int_store);
MEMCTRL_ATTR(poll_msec,S_IRUGO|S_IWUSR,memctrl_int_show,memctrl_int_store);

/* Base Attributes of the memory ECC object */
static struct memctrl_dev_attribute *memctrl_attr[] = {
	&attr_panic_on_ue,
	&attr_log_ue,
	&attr_log_ce,
	&attr_poll_msec,
	NULL,
};

/* Main MC kobject release() function */
static void edac_memctrl_master_release(struct kobject *kobj)
{
	debugf1("%s()\n", __func__);
	complete(&edac_memctrl_kobj_complete);
}

static struct kobj_type ktype_memctrl = {
	.release = edac_memctrl_master_release,
	.sysfs_ops = &memctrlfs_ops,
	.default_attrs = (struct attribute **) memctrl_attr,
};

/* Initialize the main sysfs entries for edac:
 *   /sys/devices/system/edac
 *
 * and children
 *
 * Return:  0 SUCCESS
 *         !0 FAILURE
 */
static int edac_sysfs_memctrl_setup(void)
{
	int err = 0;

	debugf1("%s()\n", __func__);

	/* create the /sys/devices/system/edac directory */
	err = sysdev_class_register(&edac_class);

	if (err) {
		debugf1("%s() error=%d\n", __func__, err);
		return err;
	}

	/* Init the MC's kobject */
	memset(&edac_memctrl_kobj, 0, sizeof (edac_memctrl_kobj));
	edac_memctrl_kobj.parent = &edac_class.kset.kobj;
	edac_memctrl_kobj.ktype = &ktype_memctrl;

	/* generate sysfs "..../edac/mc"   */
	err = kobject_set_name(&edac_memctrl_kobj,"mc");

	if (err)
		goto fail;

	/* FIXME: maybe new sysdev_create_subdir() */
	err = kobject_register(&edac_memctrl_kobj);

	if (err) {
		debugf1("Failed to register '.../edac/mc'\n");
		goto fail;
	}

	debugf1("Registered '.../edac/mc' kobject\n");

	return 0;

fail:
	sysdev_class_unregister(&edac_class);
	return err;
}

/*
 * MC teardown:
 *	the '..../edac/mc' kobject followed by '..../edac' itself
 */
static void edac_sysfs_memctrl_teardown(void)
{
	debugf0("MC: " __FILE__ ": %s()\n", __func__);

	/* Unregister the MC's kobject and wait for reference count to reach
	 * 0.
	 */
	init_completion(&edac_memctrl_kobj_complete);
	kobject_unregister(&edac_memctrl_kobj);
	wait_for_completion(&edac_memctrl_kobj_complete);

	/* Unregister the 'edac' object */
	sysdev_class_unregister(&edac_class);
}

#ifdef CONFIG_PCI
static ssize_t edac_pci_int_show(void *ptr, char *buffer)
{
	int *value = ptr;
	return sprintf(buffer,"%d\n",*value);
}

static ssize_t edac_pci_int_store(void *ptr, const char *buffer, size_t count)
{
	int *value = ptr;

	if (isdigit(*buffer))
		*value = simple_strtoul(buffer,NULL,0);

	return count;
}

struct edac_pci_dev_attribute {
	struct attribute attr;
	void *value;
	ssize_t (*show)(void *,char *);
	ssize_t (*store)(void *, const char *,size_t);
};

/* Set of show/store abstract level functions for PCI Parity object */
static ssize_t edac_pci_dev_show(struct kobject *kobj, struct attribute *attr,
		char *buffer)
{
	struct edac_pci_dev_attribute *edac_pci_dev;
	edac_pci_dev= (struct edac_pci_dev_attribute*)attr;

	if (edac_pci_dev->show)
		return edac_pci_dev->show(edac_pci_dev->value, buffer);
	return -EIO;
}

static ssize_t edac_pci_dev_store(struct kobject *kobj,
		struct attribute *attr, const char *buffer, size_t count)
{
	struct edac_pci_dev_attribute *edac_pci_dev;
	edac_pci_dev= (struct edac_pci_dev_attribute*)attr;

	if (edac_pci_dev->show)
		return edac_pci_dev->store(edac_pci_dev->value, buffer, count);
	return -EIO;
}

static struct sysfs_ops edac_pci_sysfs_ops = {
	.show   = edac_pci_dev_show,
	.store  = edac_pci_dev_store
};

#define EDAC_PCI_ATTR(_name,_mode,_show,_store)			\
struct edac_pci_dev_attribute edac_pci_attr_##_name = {		\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.value  = &_name,					\
	.show   = _show,					\
	.store  = _store,					\
};

#define EDAC_PCI_STRING_ATTR(_name,_data,_mode,_show,_store)	\
struct edac_pci_dev_attribute edac_pci_attr_##_name = {		\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.value  = _data,					\
	.show   = _show,					\
	.store  = _store,					\
};

/* PCI Parity control files */
EDAC_PCI_ATTR(check_pci_parity, S_IRUGO|S_IWUSR, edac_pci_int_show,
	edac_pci_int_store);
EDAC_PCI_ATTR(panic_on_pci_parity, S_IRUGO|S_IWUSR, edac_pci_int_show,
	edac_pci_int_store);
EDAC_PCI_ATTR(pci_parity_count, S_IRUGO, edac_pci_int_show, NULL);

/* Base Attributes of the memory ECC object */
static struct edac_pci_dev_attribute *edac_pci_attr[] = {
	&edac_pci_attr_check_pci_parity,
	&edac_pci_attr_panic_on_pci_parity,
	&edac_pci_attr_pci_parity_count,
	NULL,
};

/* No memory to release */
static void edac_pci_release(struct kobject *kobj)
{
	debugf1("%s()\n", __func__);
	complete(&edac_pci_kobj_complete);
}

static struct kobj_type ktype_edac_pci = {
	.release = edac_pci_release,
	.sysfs_ops = &edac_pci_sysfs_ops,
	.default_attrs = (struct attribute **) edac_pci_attr,
};

/**
 * edac_sysfs_pci_setup()
 *
 */
static int edac_sysfs_pci_setup(void)
{
	int err;

	debugf1("%s()\n", __func__);

	memset(&edac_pci_kobj, 0, sizeof(edac_pci_kobj));
	edac_pci_kobj.parent = &edac_class.kset.kobj;
	edac_pci_kobj.ktype = &ktype_edac_pci;
	err = kobject_set_name(&edac_pci_kobj, "pci");

	if (!err) {
		/* Instanstiate the csrow object */
		/* FIXME: maybe new sysdev_create_subdir() */
		err = kobject_register(&edac_pci_kobj);

		if (err)
			debugf1("Failed to register '.../edac/pci'\n");
		else
			debugf1("Registered '.../edac/pci' kobject\n");
	}

	return err;
}

static void edac_sysfs_pci_teardown(void)
{
	debugf0("%s()\n", __func__);
	init_completion(&edac_pci_kobj_complete);
	kobject_unregister(&edac_pci_kobj);
	wait_for_completion(&edac_pci_kobj_complete);
}


static u16 get_pci_parity_status(struct pci_dev *dev, int secondary)
{
	int where;
	u16 status;

	where = secondary ? PCI_SEC_STATUS : PCI_STATUS;
	pci_read_config_word(dev, where, &status);

	/* If we get back 0xFFFF then we must suspect that the card has been
	 * pulled but the Linux PCI layer has not yet finished cleaning up.
	 * We don't want to report on such devices
	 */

	if (status == 0xFFFF) {
		u32 sanity;

		pci_read_config_dword(dev, 0, &sanity);

		if (sanity == 0xFFFFFFFF)
			return 0;
	}

	status &= PCI_STATUS_DETECTED_PARITY | PCI_STATUS_SIG_SYSTEM_ERROR |
		PCI_STATUS_PARITY;

	if (status)
		/* reset only the bits we are interested in */
		pci_write_config_word(dev, where, status);

	return status;
}

typedef void (*pci_parity_check_fn_t) (struct pci_dev *dev);

/* Clear any PCI parity errors logged by this device. */
static void edac_pci_dev_parity_clear(struct pci_dev *dev)
{
	u8 header_type;

	get_pci_parity_status(dev, 0);

	/* read the device TYPE, looking for bridges */
	pci_read_config_byte(dev, PCI_HEADER_TYPE, &header_type);

	if ((header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE)
		get_pci_parity_status(dev, 1);
}

/*
 *  PCI Parity polling
 *
 */
static void edac_pci_dev_parity_test(struct pci_dev *dev)
{
	u16 status;
	u8  header_type;

	/* read the STATUS register on this device
	 */
	status = get_pci_parity_status(dev, 0);

	debugf2("PCI STATUS= 0x%04x %s\n", status, dev->dev.bus_id );

	/* check the status reg for errors */
	if (status) {
		if (status & (PCI_STATUS_SIG_SYSTEM_ERROR))
			edac_printk(KERN_CRIT, EDAC_PCI,
				"Signaled System Error on %s\n",
				pci_name(dev));

		if (status & (PCI_STATUS_PARITY)) {
			edac_printk(KERN_CRIT, EDAC_PCI,
				"Master Data Parity Error on %s\n",
				pci_name(dev));

			atomic_inc(&pci_parity_count);
		}

		if (status & (PCI_STATUS_DETECTED_PARITY)) {
			edac_printk(KERN_CRIT, EDAC_PCI,
				"Detected Parity Error on %s\n",
				pci_name(dev));

			atomic_inc(&pci_parity_count);
		}
	}

	/* read the device TYPE, looking for bridges */
	pci_read_config_byte(dev, PCI_HEADER_TYPE, &header_type);

	debugf2("PCI HEADER TYPE= 0x%02x %s\n", header_type, dev->dev.bus_id );

	if ((header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {
		/* On bridges, need to examine secondary status register  */
		status = get_pci_parity_status(dev, 1);

		debugf2("PCI SEC_STATUS= 0x%04x %s\n",
				status, dev->dev.bus_id );

		/* check the secondary status reg for errors */
		if (status) {
			if (status & (PCI_STATUS_SIG_SYSTEM_ERROR))
				edac_printk(KERN_CRIT, EDAC_PCI, "Bridge "
					"Signaled System Error on %s\n",
					pci_name(dev));

			if (status & (PCI_STATUS_PARITY)) {
				edac_printk(KERN_CRIT, EDAC_PCI, "Bridge "
					"Master Data Parity Error on "
					"%s\n", pci_name(dev));

				atomic_inc(&pci_parity_count);
			}

			if (status & (PCI_STATUS_DETECTED_PARITY)) {
				edac_printk(KERN_CRIT, EDAC_PCI, "Bridge "
					"Detected Parity Error on %s\n",
					pci_name(dev));

				atomic_inc(&pci_parity_count);
			}
		}
	}
}

/*
 * pci_dev parity list iterator
 *	Scan the PCI device list for one iteration, looking for SERRORs
 *	Master Parity ERRORS or Parity ERRORs on primary or secondary devices
 */
static inline void edac_pci_dev_parity_iterator(pci_parity_check_fn_t fn)
{
	struct pci_dev *dev = NULL;

	/* request for kernel access to the next PCI device, if any,
	 * and while we are looking at it have its reference count
	 * bumped until we are done with it
	 */
	while((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		fn(dev);
	}
}

static void do_pci_parity_check(void)
{
	unsigned long flags;
	int before_count;

	debugf3("%s()\n", __func__);

	if (!check_pci_parity)
		return;

	before_count = atomic_read(&pci_parity_count);

	/* scan all PCI devices looking for a Parity Error on devices and
	 * bridges
	 */
	local_irq_save(flags);
	edac_pci_dev_parity_iterator(edac_pci_dev_parity_test);
	local_irq_restore(flags);

	/* Only if operator has selected panic on PCI Error */
	if (panic_on_pci_parity) {
		/* If the count is different 'after' from 'before' */
		if (before_count != atomic_read(&pci_parity_count))
			panic("EDAC: PCI Parity Error");
	}
}

static inline void clear_pci_parity_errors(void)
{
	/* Clear any PCI bus parity errors that devices initially have logged
	 * in their registers.
	 */
	edac_pci_dev_parity_iterator(edac_pci_dev_parity_clear);
}

#else	/* CONFIG_PCI */

/* pre-process these away */
#define	do_pci_parity_check()
#define	clear_pci_parity_errors()
#define	edac_sysfs_pci_teardown()
#define	edac_sysfs_pci_setup()	(0)

#endif	/* CONFIG_PCI */

/* EDAC sysfs CSROW data structures and methods
 */

/* Set of more default csrow<id> attribute show/store functions */
static ssize_t csrow_ue_count_show(struct csrow_info *csrow, char *data, int private)
{
	return sprintf(data,"%u\n", csrow->ue_count);
}

static ssize_t csrow_ce_count_show(struct csrow_info *csrow, char *data, int private)
{
	return sprintf(data,"%u\n", csrow->ce_count);
}

static ssize_t csrow_size_show(struct csrow_info *csrow, char *data, int private)
{
	return sprintf(data,"%u\n", PAGES_TO_MiB(csrow->nr_pages));
}

static ssize_t csrow_mem_type_show(struct csrow_info *csrow, char *data, int private)
{
	return sprintf(data,"%s\n", mem_types[csrow->mtype]);
}

static ssize_t csrow_dev_type_show(struct csrow_info *csrow, char *data, int private)
{
	return sprintf(data,"%s\n", dev_types[csrow->dtype]);
}

static ssize_t csrow_edac_mode_show(struct csrow_info *csrow, char *data, int private)
{
	return sprintf(data,"%s\n", edac_caps[csrow->edac_mode]);
}

/* show/store functions for DIMM Label attributes */
static ssize_t channel_dimm_label_show(struct csrow_info *csrow,
		char *data, int channel)
{
	return snprintf(data, EDAC_MC_LABEL_LEN,"%s",
			csrow->channels[channel].label);
}

static ssize_t channel_dimm_label_store(struct csrow_info *csrow,
				const char *data,
				size_t count,
				int channel)
{
	ssize_t max_size = 0;

	max_size = min((ssize_t)count,(ssize_t)EDAC_MC_LABEL_LEN-1);
	strncpy(csrow->channels[channel].label, data, max_size);
	csrow->channels[channel].label[max_size] = '\0';

	return max_size;
}

/* show function for dynamic chX_ce_count attribute */
static ssize_t channel_ce_count_show(struct csrow_info *csrow,
				char *data,
				int channel)
{
	return sprintf(data, "%u\n", csrow->channels[channel].ce_count);
}

/* csrow specific attribute structure */
struct csrowdev_attribute {
	struct attribute attr;
	ssize_t (*show)(struct csrow_info *,char *,int);
	ssize_t (*store)(struct csrow_info *, const char *,size_t,int);
	int    private;
};

#define to_csrow(k) container_of(k, struct csrow_info, kobj)
#define to_csrowdev_attr(a) container_of(a, struct csrowdev_attribute, attr)

/* Set of show/store higher level functions for default csrow attributes */
static ssize_t csrowdev_show(struct kobject *kobj,
			struct attribute *attr,
			char *buffer)
{
	struct csrow_info *csrow = to_csrow(kobj);
	struct csrowdev_attribute *csrowdev_attr = to_csrowdev_attr(attr);

	if (csrowdev_attr->show)
		return csrowdev_attr->show(csrow,
					buffer,
					csrowdev_attr->private);
	return -EIO;
}

static ssize_t csrowdev_store(struct kobject *kobj, struct attribute *attr,
		const char *buffer, size_t count)
{
	struct csrow_info *csrow = to_csrow(kobj);
	struct csrowdev_attribute * csrowdev_attr = to_csrowdev_attr(attr);

	if (csrowdev_attr->store)
		return csrowdev_attr->store(csrow,
					buffer,
					count,
					csrowdev_attr->private);
	return -EIO;
}

static struct sysfs_ops csrowfs_ops = {
	.show   = csrowdev_show,
	.store  = csrowdev_store
};

#define CSROWDEV_ATTR(_name,_mode,_show,_store,_private)	\
struct csrowdev_attribute attr_##_name = {			\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show   = _show,					\
	.store  = _store,					\
	.private = _private,					\
};

/* default cwrow<id>/attribute files */
CSROWDEV_ATTR(size_mb,S_IRUGO,csrow_size_show,NULL,0);
CSROWDEV_ATTR(dev_type,S_IRUGO,csrow_dev_type_show,NULL,0);
CSROWDEV_ATTR(mem_type,S_IRUGO,csrow_mem_type_show,NULL,0);
CSROWDEV_ATTR(edac_mode,S_IRUGO,csrow_edac_mode_show,NULL,0);
CSROWDEV_ATTR(ue_count,S_IRUGO,csrow_ue_count_show,NULL,0);
CSROWDEV_ATTR(ce_count,S_IRUGO,csrow_ce_count_show,NULL,0);

/* default attributes of the CSROW<id> object */
static struct csrowdev_attribute *default_csrow_attr[] = {
	&attr_dev_type,
	&attr_mem_type,
	&attr_edac_mode,
	&attr_size_mb,
	&attr_ue_count,
	&attr_ce_count,
	NULL,
};


/* possible dynamic channel DIMM Label attribute files */
CSROWDEV_ATTR(ch0_dimm_label,S_IRUGO|S_IWUSR,
		channel_dimm_label_show,
		channel_dimm_label_store,
		0 );
CSROWDEV_ATTR(ch1_dimm_label,S_IRUGO|S_IWUSR,
		channel_dimm_label_show,
		channel_dimm_label_store,
		1 );
CSROWDEV_ATTR(ch2_dimm_label,S_IRUGO|S_IWUSR,
		channel_dimm_label_show,
		channel_dimm_label_store,
		2 );
CSROWDEV_ATTR(ch3_dimm_label,S_IRUGO|S_IWUSR,
		channel_dimm_label_show,
		channel_dimm_label_store,
		3 );
CSROWDEV_ATTR(ch4_dimm_label,S_IRUGO|S_IWUSR,
		channel_dimm_label_show,
		channel_dimm_label_store,
		4 );
CSROWDEV_ATTR(ch5_dimm_label,S_IRUGO|S_IWUSR,
		channel_dimm_label_show,
		channel_dimm_label_store,
		5 );

/* Total possible dynamic DIMM Label attribute file table */
static struct csrowdev_attribute *dynamic_csrow_dimm_attr[] = {
		&attr_ch0_dimm_label,
		&attr_ch1_dimm_label,
		&attr_ch2_dimm_label,
		&attr_ch3_dimm_label,
		&attr_ch4_dimm_label,
		&attr_ch5_dimm_label
};

/* possible dynamic channel ce_count attribute files */
CSROWDEV_ATTR(ch0_ce_count,S_IRUGO|S_IWUSR,
		channel_ce_count_show,
		NULL,
		0 );
CSROWDEV_ATTR(ch1_ce_count,S_IRUGO|S_IWUSR,
		channel_ce_count_show,
		NULL,
		1 );
CSROWDEV_ATTR(ch2_ce_count,S_IRUGO|S_IWUSR,
		channel_ce_count_show,
		NULL,
		2 );
CSROWDEV_ATTR(ch3_ce_count,S_IRUGO|S_IWUSR,
		channel_ce_count_show,
		NULL,
		3 );
CSROWDEV_ATTR(ch4_ce_count,S_IRUGO|S_IWUSR,
		channel_ce_count_show,
		NULL,
		4 );
CSROWDEV_ATTR(ch5_ce_count,S_IRUGO|S_IWUSR,
		channel_ce_count_show,
		NULL,
		5 );

/* Total possible dynamic ce_count attribute file table */
static struct csrowdev_attribute *dynamic_csrow_ce_count_attr[] = {
		&attr_ch0_ce_count,
		&attr_ch1_ce_count,
		&attr_ch2_ce_count,
		&attr_ch3_ce_count,
		&attr_ch4_ce_count,
		&attr_ch5_ce_count
};


#define EDAC_NR_CHANNELS	6

/* Create dynamic CHANNEL files, indexed by 'chan',  under specifed CSROW */
static int edac_create_channel_files(struct kobject *kobj, int chan)
{
	int err=-ENODEV;

	if (chan >= EDAC_NR_CHANNELS)
		return err;

	/* create the DIMM label attribute file */
	err = sysfs_create_file(kobj,
			(struct attribute *) dynamic_csrow_dimm_attr[chan]);

	if (!err) {
		/* create the CE Count attribute file */
		err = sysfs_create_file(kobj,
			(struct attribute *) dynamic_csrow_ce_count_attr[chan]);
	} else {
		debugf1("%s()  dimm labels and ce_count files created", __func__);
	}

	return err;
}

/* No memory to release for this kobj */
static void edac_csrow_instance_release(struct kobject *kobj)
{
	struct csrow_info *cs;

	cs = container_of(kobj, struct csrow_info, kobj);
	complete(&cs->kobj_complete);
}

/* the kobj_type instance for a CSROW */
static struct kobj_type ktype_csrow = {
	.release = edac_csrow_instance_release,
	.sysfs_ops = &csrowfs_ops,
	.default_attrs = (struct attribute **) default_csrow_attr,
};

/* Create a CSROW object under specifed edac_mc_device */
static int edac_create_csrow_object(
		struct kobject *edac_mci_kobj,
		struct csrow_info *csrow,
		int index)
{
	int err = 0;
	int chan;

	memset(&csrow->kobj, 0, sizeof(csrow->kobj));

	/* generate ..../edac/mc/mc<id>/csrow<index>   */

	csrow->kobj.parent = edac_mci_kobj;
	csrow->kobj.ktype = &ktype_csrow;

	/* name this instance of csrow<id> */
	err = kobject_set_name(&csrow->kobj,"csrow%d",index);
	if (err)
		goto error_exit;

	/* Instanstiate the csrow object */
	err = kobject_register(&csrow->kobj);
	if (!err) {
		/* Create the dyanmic attribute files on this csrow,
		 * namely, the DIMM labels and the channel ce_count
		 */
		for (chan = 0; chan < csrow->nr_channels; chan++) {
			err = edac_create_channel_files(&csrow->kobj,chan);
			if (err)
				break;
		}
	}

error_exit:
	return err;
}

/* default sysfs methods and data structures for the main MCI kobject */

static ssize_t mci_reset_counters_store(struct mem_ctl_info *mci,
		const char *data, size_t count)
{
	int row, chan;

	mci->ue_noinfo_count = 0;
	mci->ce_noinfo_count = 0;
	mci->ue_count = 0;
	mci->ce_count = 0;

	for (row = 0; row < mci->nr_csrows; row++) {
		struct csrow_info *ri = &mci->csrows[row];

		ri->ue_count = 0;
		ri->ce_count = 0;

		for (chan = 0; chan < ri->nr_channels; chan++)
			ri->channels[chan].ce_count = 0;
	}

	mci->start_time = jiffies;
	return count;
}

/* default attribute files for the MCI object */
static ssize_t mci_ue_count_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data,"%d\n", mci->ue_count);
}

static ssize_t mci_ce_count_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data,"%d\n", mci->ce_count);
}

static ssize_t mci_ce_noinfo_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data,"%d\n", mci->ce_noinfo_count);
}

static ssize_t mci_ue_noinfo_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data,"%d\n", mci->ue_noinfo_count);
}

static ssize_t mci_seconds_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data,"%ld\n", (jiffies - mci->start_time) / HZ);
}

static ssize_t mci_ctl_name_show(struct mem_ctl_info *mci, char *data)
{
	return sprintf(data,"%s\n", mci->ctl_name);
}

static ssize_t mci_size_mb_show(struct mem_ctl_info *mci, char *data)
{
	int total_pages, csrow_idx;

	for (total_pages = csrow_idx = 0; csrow_idx < mci->nr_csrows;
			csrow_idx++) {
		struct csrow_info *csrow = &mci->csrows[csrow_idx];

		if (!csrow->nr_pages)
			continue;

		total_pages += csrow->nr_pages;
	}

	return sprintf(data,"%u\n", PAGES_TO_MiB(total_pages));
}

struct mcidev_attribute {
	struct attribute attr;
	ssize_t (*show)(struct mem_ctl_info *,char *);
	ssize_t (*store)(struct mem_ctl_info *, const char *,size_t);
};

#define to_mci(k) container_of(k, struct mem_ctl_info, edac_mci_kobj)
#define to_mcidev_attr(a) container_of(a, struct mcidev_attribute, attr)

/* MCI show/store functions for top most object */
static ssize_t mcidev_show(struct kobject *kobj, struct attribute *attr,
		char *buffer)
{
	struct mem_ctl_info *mem_ctl_info = to_mci(kobj);
	struct mcidev_attribute * mcidev_attr = to_mcidev_attr(attr);

	if (mcidev_attr->show)
		return mcidev_attr->show(mem_ctl_info, buffer);

	return -EIO;
}

static ssize_t mcidev_store(struct kobject *kobj, struct attribute *attr,
		const char *buffer, size_t count)
{
	struct mem_ctl_info *mem_ctl_info = to_mci(kobj);
	struct mcidev_attribute * mcidev_attr = to_mcidev_attr(attr);

	if (mcidev_attr->store)
		return mcidev_attr->store(mem_ctl_info, buffer, count);

	return -EIO;
}

static struct sysfs_ops mci_ops = {
	.show = mcidev_show,
	.store = mcidev_store
};

#define MCIDEV_ATTR(_name,_mode,_show,_store)			\
struct mcidev_attribute mci_attr_##_name = {			\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show   = _show,					\
	.store  = _store,					\
};

/* default Control file */
MCIDEV_ATTR(reset_counters,S_IWUSR,NULL,mci_reset_counters_store);

/* default Attribute files */
MCIDEV_ATTR(mc_name,S_IRUGO,mci_ctl_name_show,NULL);
MCIDEV_ATTR(size_mb,S_IRUGO,mci_size_mb_show,NULL);
MCIDEV_ATTR(seconds_since_reset,S_IRUGO,mci_seconds_show,NULL);
MCIDEV_ATTR(ue_noinfo_count,S_IRUGO,mci_ue_noinfo_show,NULL);
MCIDEV_ATTR(ce_noinfo_count,S_IRUGO,mci_ce_noinfo_show,NULL);
MCIDEV_ATTR(ue_count,S_IRUGO,mci_ue_count_show,NULL);
MCIDEV_ATTR(ce_count,S_IRUGO,mci_ce_count_show,NULL);

static struct mcidev_attribute *mci_attr[] = {
	&mci_attr_reset_counters,
	&mci_attr_mc_name,
	&mci_attr_size_mb,
	&mci_attr_seconds_since_reset,
	&mci_attr_ue_noinfo_count,
	&mci_attr_ce_noinfo_count,
	&mci_attr_ue_count,
	&mci_attr_ce_count,
	NULL
};

/*
 * Release of a MC controlling instance
 */
static void edac_mci_instance_release(struct kobject *kobj)
{
	struct mem_ctl_info *mci;

	mci = to_mci(kobj);
	debugf0("%s() idx=%d\n", __func__, mci->mc_idx);
	complete(&mci->kobj_complete);
}

static struct kobj_type ktype_mci = {
	.release = edac_mci_instance_release,
	.sysfs_ops = &mci_ops,
	.default_attrs = (struct attribute **) mci_attr,
};


#define EDAC_DEVICE_SYMLINK	"device"

/*
 * Create a new Memory Controller kobject instance,
 *	mc<id> under the 'mc' directory
 *
 * Return:
 *	0	Success
 *	!0	Failure
 */
static int edac_create_sysfs_mci_device(struct mem_ctl_info *mci)
{
	int i;
	int err;
	struct csrow_info *csrow;
	struct kobject *edac_mci_kobj=&mci->edac_mci_kobj;

	debugf0("%s() idx=%d\n", __func__, mci->mc_idx);
	memset(edac_mci_kobj, 0, sizeof(*edac_mci_kobj));

	/* set the name of the mc<id> object */
	err = kobject_set_name(edac_mci_kobj,"mc%d",mci->mc_idx);
	if (err)
		return err;

	/* link to our parent the '..../edac/mc' object */
	edac_mci_kobj->parent = &edac_memctrl_kobj;
	edac_mci_kobj->ktype = &ktype_mci;

	/* register the mc<id> kobject */
	err = kobject_register(edac_mci_kobj);
	if (err)
		return err;

	/* create a symlink for the device */
	err = sysfs_create_link(edac_mci_kobj, &mci->dev->kobj,
				EDAC_DEVICE_SYMLINK);
	if (err)
		goto fail0;

	/* Make directories for each CSROW object
	 * under the mc<id> kobject
	 */
	for (i = 0; i < mci->nr_csrows; i++) {
		csrow = &mci->csrows[i];

		/* Only expose populated CSROWs */
		if (csrow->nr_pages > 0) {
			err = edac_create_csrow_object(edac_mci_kobj,csrow,i);
			if (err)
				goto fail1;
		}
	}

	return 0;

	/* CSROW error: backout what has already been registered,  */
fail1:
	for ( i--; i >= 0; i--) {
		if (csrow->nr_pages > 0) {
			init_completion(&csrow->kobj_complete);
			kobject_unregister(&mci->csrows[i].kobj);
			wait_for_completion(&csrow->kobj_complete);
		}
	}

fail0:
	init_completion(&mci->kobj_complete);
	kobject_unregister(edac_mci_kobj);
	wait_for_completion(&mci->kobj_complete);
	return err;
}

/*
 * remove a Memory Controller instance
 */
static void edac_remove_sysfs_mci_device(struct mem_ctl_info *mci)
{
	int i;

	debugf0("%s()\n", __func__);

	/* remove all csrow kobjects */
	for (i = 0; i < mci->nr_csrows; i++) {
		if (mci->csrows[i].nr_pages > 0) {
			init_completion(&mci->csrows[i].kobj_complete);
			kobject_unregister(&mci->csrows[i].kobj);
			wait_for_completion(&mci->csrows[i].kobj_complete);
		}
	}

	sysfs_remove_link(&mci->edac_mci_kobj, EDAC_DEVICE_SYMLINK);
	init_completion(&mci->kobj_complete);
	kobject_unregister(&mci->edac_mci_kobj);
	wait_for_completion(&mci->kobj_complete);
}

/* END OF sysfs data and methods */

#ifdef CONFIG_EDAC_DEBUG

void edac_mc_dump_channel(struct channel_info *chan)
{
	debugf4("\tchannel = %p\n", chan);
	debugf4("\tchannel->chan_idx = %d\n", chan->chan_idx);
	debugf4("\tchannel->ce_count = %d\n", chan->ce_count);
	debugf4("\tchannel->label = '%s'\n", chan->label);
	debugf4("\tchannel->csrow = %p\n\n", chan->csrow);
}
EXPORT_SYMBOL_GPL(edac_mc_dump_channel);

void edac_mc_dump_csrow(struct csrow_info *csrow)
{
	debugf4("\tcsrow = %p\n", csrow);
	debugf4("\tcsrow->csrow_idx = %d\n", csrow->csrow_idx);
	debugf4("\tcsrow->first_page = 0x%lx\n",
		csrow->first_page);
	debugf4("\tcsrow->last_page = 0x%lx\n", csrow->last_page);
	debugf4("\tcsrow->page_mask = 0x%lx\n", csrow->page_mask);
	debugf4("\tcsrow->nr_pages = 0x%x\n", csrow->nr_pages);
	debugf4("\tcsrow->nr_channels = %d\n",
		csrow->nr_channels);
	debugf4("\tcsrow->channels = %p\n", csrow->channels);
	debugf4("\tcsrow->mci = %p\n\n", csrow->mci);
}
EXPORT_SYMBOL_GPL(edac_mc_dump_csrow);

void edac_mc_dump_mci(struct mem_ctl_info *mci)
{
	debugf3("\tmci = %p\n", mci);
	debugf3("\tmci->mtype_cap = %lx\n", mci->mtype_cap);
	debugf3("\tmci->edac_ctl_cap = %lx\n", mci->edac_ctl_cap);
	debugf3("\tmci->edac_cap = %lx\n", mci->edac_cap);
	debugf4("\tmci->edac_check = %p\n", mci->edac_check);
	debugf3("\tmci->nr_csrows = %d, csrows = %p\n",
		mci->nr_csrows, mci->csrows);
	debugf3("\tdev = %p\n", mci->dev);
	debugf3("\tmod_name:ctl_name = %s:%s\n",
		mci->mod_name, mci->ctl_name);
	debugf3("\tpvt_info = %p\n\n", mci->pvt_info);
}
EXPORT_SYMBOL_GPL(edac_mc_dump_mci);

#endif  /* CONFIG_EDAC_DEBUG */

/* 'ptr' points to a possibly unaligned item X such that sizeof(X) is 'size'.
 * Adjust 'ptr' so that its alignment is at least as stringent as what the
 * compiler would provide for X and return the aligned result.
 *
 * If 'size' is a constant, the compiler will optimize this whole function
 * down to either a no-op or the addition of a constant to the value of 'ptr'.
 */
static inline char * align_ptr(void *ptr, unsigned size)
{
	unsigned align, r;

	/* Here we assume that the alignment of a "long long" is the most
	 * stringent alignment that the compiler will ever provide by default.
	 * As far as I know, this is a reasonable assumption.
	 */
	if (size > sizeof(long))
		align = sizeof(long long);
	else if (size > sizeof(int))
		align = sizeof(long);
	else if (size > sizeof(short))
		align = sizeof(int);
	else if (size > sizeof(char))
		align = sizeof(short);
	else
		return (char *) ptr;

	r = size % align;

	if (r == 0)
		return (char *) ptr;

	return (char *) (((unsigned long) ptr) + align - r);
}

/**
 * edac_mc_alloc: Allocate a struct mem_ctl_info structure
 * @size_pvt:	size of private storage needed
 * @nr_csrows:	Number of CWROWS needed for this MC
 * @nr_chans:	Number of channels for the MC
 *
 * Everything is kmalloc'ed as one big chunk - more efficient.
 * Only can be used if all structures have the same lifetime - otherwise
 * you have to allocate and initialize your own structures.
 *
 * Use edac_mc_free() to free mc structures allocated by this function.
 *
 * Returns:
 *	NULL allocation failed
 *	struct mem_ctl_info pointer
 */
struct mem_ctl_info *edac_mc_alloc(unsigned sz_pvt, unsigned nr_csrows,
		unsigned nr_chans)
{
	struct mem_ctl_info *mci;
	struct csrow_info *csi, *csrow;
	struct channel_info *chi, *chp, *chan;
	void *pvt;
	unsigned size;
	int row, chn;

	/* Figure out the offsets of the various items from the start of an mc
	 * structure.  We want the alignment of each item to be at least as
	 * stringent as what the compiler would provide if we could simply
	 * hardcode everything into a single struct.
	 */
	mci = (struct mem_ctl_info *) 0;
	csi = (struct csrow_info *)align_ptr(&mci[1], sizeof(*csi));
	chi = (struct channel_info *)
			align_ptr(&csi[nr_csrows], sizeof(*chi));
	pvt = align_ptr(&chi[nr_chans * nr_csrows], sz_pvt);
	size = ((unsigned long) pvt) + sz_pvt;

	if ((mci = kmalloc(size, GFP_KERNEL)) == NULL)
		return NULL;

	/* Adjust pointers so they point within the memory we just allocated
	 * rather than an imaginary chunk of memory located at address 0.
	 */
	csi = (struct csrow_info *) (((char *) mci) + ((unsigned long) csi));
	chi = (struct channel_info *) (((char *) mci) + ((unsigned long) chi));
	pvt = sz_pvt ? (((char *) mci) + ((unsigned long) pvt)) : NULL;

	memset(mci, 0, size);  /* clear all fields */
	mci->csrows = csi;
	mci->pvt_info = pvt;
	mci->nr_csrows = nr_csrows;

	for (row = 0; row < nr_csrows; row++) {
		csrow = &csi[row];
		csrow->csrow_idx = row;
		csrow->mci = mci;
		csrow->nr_channels = nr_chans;
		chp = &chi[row * nr_chans];
		csrow->channels = chp;

		for (chn = 0; chn < nr_chans; chn++) {
			chan = &chp[chn];
			chan->chan_idx = chn;
			chan->csrow = csrow;
		}
	}

	return mci;
}
EXPORT_SYMBOL_GPL(edac_mc_alloc);

/**
 * edac_mc_free:  Free a previously allocated 'mci' structure
 * @mci: pointer to a struct mem_ctl_info structure
 */
void edac_mc_free(struct mem_ctl_info *mci)
{
	kfree(mci);
}
EXPORT_SYMBOL_GPL(edac_mc_free);

static struct mem_ctl_info *find_mci_by_dev(struct device *dev)
{
	struct mem_ctl_info *mci;
	struct list_head *item;

	debugf3("%s()\n", __func__);

	list_for_each(item, &mc_devices) {
		mci = list_entry(item, struct mem_ctl_info, link);

		if (mci->dev == dev)
			return mci;
	}

	return NULL;
}

/* Return 0 on success, 1 on failure.
 * Before calling this function, caller must
 * assign a unique value to mci->mc_idx.
 */
static int add_mc_to_global_list (struct mem_ctl_info *mci)
{
	struct list_head *item, *insert_before;
	struct mem_ctl_info *p;

	insert_before = &mc_devices;

	if (unlikely((p = find_mci_by_dev(mci->dev)) != NULL))
		goto fail0;

	list_for_each(item, &mc_devices) {
		p = list_entry(item, struct mem_ctl_info, link);

		if (p->mc_idx >= mci->mc_idx) {
			if (unlikely(p->mc_idx == mci->mc_idx))
				goto fail1;

			insert_before = item;
			break;
		}
	}

	list_add_tail_rcu(&mci->link, insert_before);
	return 0;

fail0:
	edac_printk(KERN_WARNING, EDAC_MC,
		    "%s (%s) %s %s already assigned %d\n", p->dev->bus_id,
		    dev_name(p->dev), p->mod_name, p->ctl_name, p->mc_idx);
	return 1;

fail1:
	edac_printk(KERN_WARNING, EDAC_MC,
		    "bug in low-level driver: attempt to assign\n"
		    "    duplicate mc_idx %d in %s()\n", p->mc_idx, __func__);
	return 1;
}

static void complete_mc_list_del(struct rcu_head *head)
{
	struct mem_ctl_info *mci;

	mci = container_of(head, struct mem_ctl_info, rcu);
	INIT_LIST_HEAD(&mci->link);
	complete(&mci->complete);
}

static void del_mc_from_global_list(struct mem_ctl_info *mci)
{
	list_del_rcu(&mci->link);
	init_completion(&mci->complete);
	call_rcu(&mci->rcu, complete_mc_list_del);
	wait_for_completion(&mci->complete);
}

/**
 * edac_mc_add_mc: Insert the 'mci' structure into the mci global list and
 *                 create sysfs entries associated with mci structure
 * @mci: pointer to the mci structure to be added to the list
 * @mc_idx: A unique numeric identifier to be assigned to the 'mci' structure.
 *
 * Return:
 *	0	Success
 *	!0	Failure
 */

/* FIXME - should a warning be printed if no error detection? correction? */
int edac_mc_add_mc(struct mem_ctl_info *mci, int mc_idx)
{
	debugf0("%s()\n", __func__);
	mci->mc_idx = mc_idx;
#ifdef CONFIG_EDAC_DEBUG
	if (edac_debug_level >= 3)
		edac_mc_dump_mci(mci);

	if (edac_debug_level >= 4) {
		int i;

		for (i = 0; i < mci->nr_csrows; i++) {
			int j;

			edac_mc_dump_csrow(&mci->csrows[i]);
			for (j = 0; j < mci->csrows[i].nr_channels; j++)
				edac_mc_dump_channel(
					&mci->csrows[i].channels[j]);
		}
	}
#endif
	down(&mem_ctls_mutex);

	if (add_mc_to_global_list(mci))
		goto fail0;

	/* set load time so that error rate can be tracked */
	mci->start_time = jiffies;

        if (edac_create_sysfs_mci_device(mci)) {
                edac_mc_printk(mci, KERN_WARNING,
			"failed to create sysfs device\n");
                goto fail1;
        }

	/* Report action taken */
	edac_mc_printk(mci, KERN_INFO, "Giving out device to %s %s: DEV %s\n",
		mci->mod_name, mci->ctl_name, dev_name(mci->dev));

	up(&mem_ctls_mutex);
	return 0;

fail1:
	del_mc_from_global_list(mci);

fail0:
	up(&mem_ctls_mutex);
	return 1;
}
EXPORT_SYMBOL_GPL(edac_mc_add_mc);

/**
 * edac_mc_del_mc: Remove sysfs entries for specified mci structure and
 *                 remove mci structure from global list
 * @pdev: Pointer to 'struct device' representing mci structure to remove.
 *
 * Return pointer to removed mci structure, or NULL if device not found.
 */
struct mem_ctl_info * edac_mc_del_mc(struct device *dev)
{
	struct mem_ctl_info *mci;

	debugf0("MC: %s()\n", __func__);
	down(&mem_ctls_mutex);

	if ((mci = find_mci_by_dev(dev)) == NULL) {
		up(&mem_ctls_mutex);
		return NULL;
	}

	edac_remove_sysfs_mci_device(mci);
	del_mc_from_global_list(mci);
	up(&mem_ctls_mutex);
	edac_printk(KERN_INFO, EDAC_MC,
		"Removed device %d for %s %s: DEV %s\n", mci->mc_idx,
		mci->mod_name, mci->ctl_name, dev_name(mci->dev));
	return mci;
}
EXPORT_SYMBOL_GPL(edac_mc_del_mc);

void edac_mc_scrub_block(unsigned long page, unsigned long offset, u32 size)
{
	struct page *pg;
	void *virt_addr;
	unsigned long flags = 0;

	debugf3("%s()\n", __func__);

	/* ECC error page was not in our memory. Ignore it. */
	if(!pfn_valid(page))
		return;

	/* Find the actual page structure then map it and fix */
	pg = pfn_to_page(page);

	if (PageHighMem(pg))
		local_irq_save(flags);

	virt_addr = kmap_atomic(pg, KM_BOUNCE_READ);

	/* Perform architecture specific atomic scrub operation */
	atomic_scrub(virt_addr + offset, size);

	/* Unmap and complete */
	kunmap_atomic(virt_addr, KM_BOUNCE_READ);

	if (PageHighMem(pg))
		local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(edac_mc_scrub_block);

/* FIXME - should return -1 */
int edac_mc_find_csrow_by_page(struct mem_ctl_info *mci, unsigned long page)
{
	struct csrow_info *csrows = mci->csrows;
	int row, i;

	debugf1("MC%d: %s(): 0x%lx\n", mci->mc_idx, __func__, page);
	row = -1;

	for (i = 0; i < mci->nr_csrows; i++) {
		struct csrow_info *csrow = &csrows[i];

		if (csrow->nr_pages == 0)
			continue;

		debugf3("MC%d: %s(): first(0x%lx) page(0x%lx) last(0x%lx) "
			"mask(0x%lx)\n", mci->mc_idx, __func__,
			csrow->first_page, page, csrow->last_page,
			csrow->page_mask);

		if ((page >= csrow->first_page) &&
		    (page <= csrow->last_page) &&
		    ((page & csrow->page_mask) ==
		     (csrow->first_page & csrow->page_mask))) {
			row = i;
			break;
		}
	}

	if (row == -1)
		edac_mc_printk(mci, KERN_ERR,
			"could not look up page error address %lx\n",
			(unsigned long) page);

	return row;
}
EXPORT_SYMBOL_GPL(edac_mc_find_csrow_by_page);

/* FIXME - setable log (warning/emerg) levels */
/* FIXME - integrate with evlog: http://evlog.sourceforge.net/ */
void edac_mc_handle_ce(struct mem_ctl_info *mci,
		unsigned long page_frame_number, unsigned long offset_in_page,
		unsigned long syndrome, int row, int channel, const char *msg)
{
	unsigned long remapped_page;

	debugf3("MC%d: %s()\n", mci->mc_idx, __func__);

	/* FIXME - maybe make panic on INTERNAL ERROR an option */
	if (row >= mci->nr_csrows || row < 0) {
		/* something is wrong */
		edac_mc_printk(mci, KERN_ERR,
			"INTERNAL ERROR: row out of range "
			"(%d >= %d)\n", row, mci->nr_csrows);
		edac_mc_handle_ce_no_info(mci, "INTERNAL ERROR");
		return;
	}

	if (channel >= mci->csrows[row].nr_channels || channel < 0) {
		/* something is wrong */
		edac_mc_printk(mci, KERN_ERR,
			"INTERNAL ERROR: channel out of range "
			"(%d >= %d)\n", channel,
			mci->csrows[row].nr_channels);
		edac_mc_handle_ce_no_info(mci, "INTERNAL ERROR");
		return;
	}

	if (log_ce)
		/* FIXME - put in DIMM location */
		edac_mc_printk(mci, KERN_WARNING,
			"CE page 0x%lx, offset 0x%lx, grain %d, syndrome "
			"0x%lx, row %d, channel %d, label \"%s\": %s\n",
			page_frame_number, offset_in_page,
			mci->csrows[row].grain, syndrome, row, channel,
			mci->csrows[row].channels[channel].label, msg);

	mci->ce_count++;
	mci->csrows[row].ce_count++;
	mci->csrows[row].channels[channel].ce_count++;

	if (mci->scrub_mode & SCRUB_SW_SRC) {
		/*
		 * Some MC's can remap memory so that it is still available
		 * at a different address when PCI devices map into memory.
		 * MC's that can't do this lose the memory where PCI devices
		 * are mapped.  This mapping is MC dependant and so we call
		 * back into the MC driver for it to map the MC page to
		 * a physical (CPU) page which can then be mapped to a virtual
		 * page - which can then be scrubbed.
		 */
		remapped_page = mci->ctl_page_to_phys ?
		    mci->ctl_page_to_phys(mci, page_frame_number) :
		    page_frame_number;

		edac_mc_scrub_block(remapped_page, offset_in_page,
					mci->csrows[row].grain);
	}
}
EXPORT_SYMBOL_GPL(edac_mc_handle_ce);

void edac_mc_handle_ce_no_info(struct mem_ctl_info *mci, const char *msg)
{
	if (log_ce)
		edac_mc_printk(mci, KERN_WARNING,
			"CE - no information available: %s\n", msg);

	mci->ce_noinfo_count++;
	mci->ce_count++;
}
EXPORT_SYMBOL_GPL(edac_mc_handle_ce_no_info);

void edac_mc_handle_ue(struct mem_ctl_info *mci,
		unsigned long page_frame_number, unsigned long offset_in_page,
		int row, const char *msg)
{
	int len = EDAC_MC_LABEL_LEN * 4;
	char labels[len + 1];
	char *pos = labels;
	int chan;
	int chars;

	debugf3("MC%d: %s()\n", mci->mc_idx, __func__);

	/* FIXME - maybe make panic on INTERNAL ERROR an option */
	if (row >= mci->nr_csrows || row < 0) {
		/* something is wrong */
		edac_mc_printk(mci, KERN_ERR,
			"INTERNAL ERROR: row out of range "
			"(%d >= %d)\n", row, mci->nr_csrows);
		edac_mc_handle_ue_no_info(mci, "INTERNAL ERROR");
		return;
	}

	chars = snprintf(pos, len + 1, "%s",
			mci->csrows[row].channels[0].label);
	len -= chars;
	pos += chars;

	for (chan = 1; (chan < mci->csrows[row].nr_channels) && (len > 0);
	     chan++) {
		chars = snprintf(pos, len + 1, ":%s",
				mci->csrows[row].channels[chan].label);
		len -= chars;
		pos += chars;
	}

	if (log_ue)
		edac_mc_printk(mci, KERN_EMERG,
			"UE page 0x%lx, offset 0x%lx, grain %d, row %d, "
			"labels \"%s\": %s\n", page_frame_number,
			offset_in_page, mci->csrows[row].grain, row, labels,
			msg);

	if (panic_on_ue)
		panic("EDAC MC%d: UE page 0x%lx, offset 0x%lx, grain %d, "
			"row %d, labels \"%s\": %s\n", mci->mc_idx,
			page_frame_number, offset_in_page,
			mci->csrows[row].grain, row, labels, msg);

	mci->ue_count++;
	mci->csrows[row].ue_count++;
}
EXPORT_SYMBOL_GPL(edac_mc_handle_ue);

void edac_mc_handle_ue_no_info(struct mem_ctl_info *mci, const char *msg)
{
	if (panic_on_ue)
		panic("EDAC MC%d: Uncorrected Error", mci->mc_idx);

	if (log_ue)
		edac_mc_printk(mci, KERN_WARNING,
			"UE - no information available: %s\n", msg);
	mci->ue_noinfo_count++;
	mci->ue_count++;
}
EXPORT_SYMBOL_GPL(edac_mc_handle_ue_no_info);


/*
 * Iterate over all MC instances and check for ECC, et al, errors
 */
static inline void check_mc_devices(void)
{
	struct list_head *item;
	struct mem_ctl_info *mci;

	debugf3("%s()\n", __func__);
	down(&mem_ctls_mutex);

	list_for_each(item, &mc_devices) {
		mci = list_entry(item, struct mem_ctl_info, link);

		if (mci->edac_check != NULL)
			mci->edac_check(mci);
	}

	up(&mem_ctls_mutex);
}

/*
 * Check MC status every poll_msec.
 * Check PCI status every poll_msec as well.
 *
 * This where the work gets done for edac.
 *
 * SMP safe, doesn't use NMI, and auto-rate-limits.
 */
static void do_edac_check(void)
{
	debugf3("%s()\n", __func__);
	check_mc_devices();
	do_pci_parity_check();
}

static int edac_kernel_thread(void *arg)
{
	while (!kthread_should_stop()) {
		do_edac_check();

		/* goto sleep for the interval */
		schedule_timeout_interruptible((HZ * poll_msec) / 1000);
		try_to_freeze();
	}

	return 0;
}

/*
 * edac_mc_init
 *      module initialization entry point
 */
static int __init edac_mc_init(void)
{
	edac_printk(KERN_INFO, EDAC_MC, EDAC_MC_VERSION "\n");

	/*
	 * Harvest and clear any boot/initialization PCI parity errors
	 *
	 * FIXME: This only clears errors logged by devices present at time of
	 * 	module initialization.  We should also do an initial clear
	 *	of each newly hotplugged device.
	 */
	clear_pci_parity_errors();

	/* Create the MC sysfs entries */
	if (edac_sysfs_memctrl_setup()) {
		edac_printk(KERN_ERR, EDAC_MC,
			"Error initializing sysfs code\n");
		return -ENODEV;
	}

	/* Create the PCI parity sysfs entries */
	if (edac_sysfs_pci_setup()) {
		edac_sysfs_memctrl_teardown();
		edac_printk(KERN_ERR, EDAC_MC,
			"EDAC PCI: Error initializing sysfs code\n");
		return -ENODEV;
	}

	/* create our kernel thread */
	edac_thread = kthread_run(edac_kernel_thread, NULL, "kedac");

	if (IS_ERR(edac_thread)) {
		/* remove the sysfs entries */
		edac_sysfs_memctrl_teardown();
		edac_sysfs_pci_teardown();
		return PTR_ERR(edac_thread);
	}

	return 0;
}

/*
 * edac_mc_exit()
 *      module exit/termination functioni
 */
static void __exit edac_mc_exit(void)
{
	debugf0("%s()\n", __func__);
	kthread_stop(edac_thread);

        /* tear down the sysfs device */
	edac_sysfs_memctrl_teardown();
	edac_sysfs_pci_teardown();
}

module_init(edac_mc_init);
module_exit(edac_mc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Linux Networx (http://lnxi.com) Thayne Harbaugh et al\n"
	"Based on work by Dan Hollis et al");
MODULE_DESCRIPTION("Core library routines for MC reporting");

module_param(panic_on_ue, int, 0644);
MODULE_PARM_DESC(panic_on_ue, "Panic on uncorrected error: 0=off 1=on");
#ifdef CONFIG_PCI
module_param(check_pci_parity, int, 0644);
MODULE_PARM_DESC(check_pci_parity, "Check for PCI bus parity errors: 0=off 1=on");
module_param(panic_on_pci_parity, int, 0644);
MODULE_PARM_DESC(panic_on_pci_parity, "Panic on PCI Bus Parity error: 0=off 1=on");
#endif
module_param(log_ue, int, 0644);
MODULE_PARM_DESC(log_ue, "Log uncorrectable error to console: 0=off 1=on");
module_param(log_ce, int, 0644);
MODULE_PARM_DESC(log_ce, "Log correctable error to console: 0=off 1=on");
module_param(poll_msec, int, 0644);
MODULE_PARM_DESC(poll_msec, "Polling period in milliseconds");
#ifdef CONFIG_EDAC_DEBUG
module_param(edac_debug_level, int, 0644);
MODULE_PARM_DESC(edac_debug_level, "Debug level");
#endif
