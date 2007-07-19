/*
 * (C) 2005, 2006 Linux Networx (http://lnxi.com)
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written Doug Thompson <norsk5@xmission.com>
 *
 */
#include <linux/module.h>
#include <linux/sysdev.h>
#include <linux/ctype.h>

#include "edac_core.h"
#include "edac_module.h"

#ifdef CONFIG_PCI

#define EDAC_PCI_SYMLINK	"device"

static int check_pci_errors = 0;	/* default YES check PCI parity */
static int edac_pci_panic_on_pe = 0;	/* default no panic on PCI Parity */
static int edac_pci_log_pe = 1;	/* log PCI parity errors */
static int edac_pci_log_npe = 1;	/* log PCI non-parity error errors */
static atomic_t pci_parity_count = ATOMIC_INIT(0);
static atomic_t pci_nonparity_count = ATOMIC_INIT(0);
static int edac_pci_poll_msec = 1000;

static struct kobject edac_pci_kobj;	/* /sys/devices/system/edac/pci */
static struct completion edac_pci_kobj_complete;
static atomic_t edac_pci_sysfs_refcount = ATOMIC_INIT(0);

int edac_pci_get_check_errors(void)
{
	return check_pci_errors;
}

int edac_pci_get_log_pe(void)
{
	return edac_pci_log_pe;
}

int edac_pci_get_log_npe(void)
{
	return edac_pci_log_npe;
}

int edac_pci_get_panic_on_pe(void)
{
	return edac_pci_panic_on_pe;
}

int edac_pci_get_poll_msec(void)
{
	return edac_pci_poll_msec;
}

/**************************** EDAC PCI sysfs instance *******************/
static ssize_t instance_pe_count_show(struct edac_pci_ctl_info *pci, char *data)
{
	return sprintf(data, "%u\n", atomic_read(&pci->counters.pe_count));
}

static ssize_t instance_npe_count_show(struct edac_pci_ctl_info *pci,
				       char *data)
{
	return sprintf(data, "%u\n", atomic_read(&pci->counters.npe_count));
}

#define to_instance(k) container_of(k, struct edac_pci_ctl_info, kobj)
#define to_instance_attr(a) container_of(a, struct instance_attribute, attr)

/* DEVICE instance kobject release() function */
static void edac_pci_instance_release(struct kobject *kobj)
{
	struct edac_pci_ctl_info *pci;

	debugf1("%s()\n", __func__);

	pci = to_instance(kobj);
	complete(&pci->kobj_complete);
}

/* instance specific attribute structure */
struct instance_attribute {
	struct attribute attr;
	 ssize_t(*show) (struct edac_pci_ctl_info *, char *);
	 ssize_t(*store) (struct edac_pci_ctl_info *, const char *, size_t);
};

/* Function to 'show' fields from the edac_pci 'instance' structure */
static ssize_t edac_pci_instance_show(struct kobject *kobj,
				      struct attribute *attr, char *buffer)
{
	struct edac_pci_ctl_info *pci = to_instance(kobj);
	struct instance_attribute *instance_attr = to_instance_attr(attr);

	if (instance_attr->show)
		return instance_attr->show(pci, buffer);
	return -EIO;
}

/* Function to 'store' fields into the edac_pci 'instance' structure */
static ssize_t edac_pci_instance_store(struct kobject *kobj,
				       struct attribute *attr,
				       const char *buffer, size_t count)
{
	struct edac_pci_ctl_info *pci = to_instance(kobj);
	struct instance_attribute *instance_attr = to_instance_attr(attr);

	if (instance_attr->store)
		return instance_attr->store(pci, buffer, count);
	return -EIO;
}

static struct sysfs_ops pci_instance_ops = {
	.show = edac_pci_instance_show,
	.store = edac_pci_instance_store
};

#define INSTANCE_ATTR(_name, _mode, _show, _store)	\
static struct instance_attribute attr_instance_##_name = {	\
	.attr	= {.name = __stringify(_name), .mode = _mode },	\
	.show	= _show,					\
	.store	= _store,					\
};

INSTANCE_ATTR(pe_count, S_IRUGO, instance_pe_count_show, NULL);
INSTANCE_ATTR(npe_count, S_IRUGO, instance_npe_count_show, NULL);

/* pci instance attributes */
static struct instance_attribute *pci_instance_attr[] = {
	&attr_instance_pe_count,
	&attr_instance_npe_count,
	NULL
};

/* the ktype for pci instance */
static struct kobj_type ktype_pci_instance = {
	.release = edac_pci_instance_release,
	.sysfs_ops = &pci_instance_ops,
	.default_attrs = (struct attribute **)pci_instance_attr,
};

static int edac_pci_create_instance_kobj(struct edac_pci_ctl_info *pci, int idx)
{
	int err;

	pci->kobj.parent = &edac_pci_kobj;
	pci->kobj.ktype = &ktype_pci_instance;

	err = kobject_set_name(&pci->kobj, "pci%d", idx);
	if (err)
		return err;

	err = kobject_register(&pci->kobj);
	if (err != 0) {
		debugf2("%s() failed to register instance pci%d\n",
			__func__, idx);
		return err;
	}

	debugf1("%s() Register instance 'pci%d' kobject\n", __func__, idx);

	return 0;
}

static void
edac_pci_delete_instance_kobj(struct edac_pci_ctl_info *pci, int idx)
{
	init_completion(&pci->kobj_complete);
	kobject_unregister(&pci->kobj);
	wait_for_completion(&pci->kobj_complete);
}

/***************************** EDAC PCI sysfs root **********************/
#define to_edacpci(k) container_of(k, struct edac_pci_ctl_info, kobj)
#define to_edacpci_attr(a) container_of(a, struct edac_pci_attr, attr)

static ssize_t edac_pci_int_show(void *ptr, char *buffer)
{
	int *value = ptr;
	return sprintf(buffer, "%d\n", *value);
}

static ssize_t edac_pci_int_store(void *ptr, const char *buffer, size_t count)
{
	int *value = ptr;

	if (isdigit(*buffer))
		*value = simple_strtoul(buffer, NULL, 0);

	return count;
}

struct edac_pci_dev_attribute {
	struct attribute attr;
	void *value;
	 ssize_t(*show) (void *, char *);
	 ssize_t(*store) (void *, const char *, size_t);
};

/* Set of show/store abstract level functions for PCI Parity object */
static ssize_t edac_pci_dev_show(struct kobject *kobj, struct attribute *attr,
				 char *buffer)
{
	struct edac_pci_dev_attribute *edac_pci_dev;
	edac_pci_dev = (struct edac_pci_dev_attribute *)attr;

	if (edac_pci_dev->show)
		return edac_pci_dev->show(edac_pci_dev->value, buffer);
	return -EIO;
}

static ssize_t edac_pci_dev_store(struct kobject *kobj,
				  struct attribute *attr, const char *buffer,
				  size_t count)
{
	struct edac_pci_dev_attribute *edac_pci_dev;
	edac_pci_dev = (struct edac_pci_dev_attribute *)attr;

	if (edac_pci_dev->show)
		return edac_pci_dev->store(edac_pci_dev->value, buffer, count);
	return -EIO;
}

static struct sysfs_ops edac_pci_sysfs_ops = {
	.show = edac_pci_dev_show,
	.store = edac_pci_dev_store
};

#define EDAC_PCI_ATTR(_name,_mode,_show,_store)			\
static struct edac_pci_dev_attribute edac_pci_attr_##_name = {		\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.value  = &_name,					\
	.show   = _show,					\
	.store  = _store,					\
};

#define EDAC_PCI_STRING_ATTR(_name,_data,_mode,_show,_store)	\
static struct edac_pci_dev_attribute edac_pci_attr_##_name = {		\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.value  = _data,					\
	.show   = _show,					\
	.store  = _store,					\
};

/* PCI Parity control files */
EDAC_PCI_ATTR(check_pci_errors, S_IRUGO | S_IWUSR, edac_pci_int_show,
	      edac_pci_int_store);
EDAC_PCI_ATTR(edac_pci_log_pe, S_IRUGO | S_IWUSR, edac_pci_int_show,
	      edac_pci_int_store);
EDAC_PCI_ATTR(edac_pci_log_npe, S_IRUGO | S_IWUSR, edac_pci_int_show,
	      edac_pci_int_store);
EDAC_PCI_ATTR(edac_pci_panic_on_pe, S_IRUGO | S_IWUSR, edac_pci_int_show,
	      edac_pci_int_store);
EDAC_PCI_ATTR(pci_parity_count, S_IRUGO, edac_pci_int_show, NULL);
EDAC_PCI_ATTR(pci_nonparity_count, S_IRUGO, edac_pci_int_show, NULL);

/* Base Attributes of the memory ECC object */
static struct edac_pci_dev_attribute *edac_pci_attr[] = {
	&edac_pci_attr_check_pci_errors,
	&edac_pci_attr_edac_pci_log_pe,
	&edac_pci_attr_edac_pci_log_npe,
	&edac_pci_attr_edac_pci_panic_on_pe,
	&edac_pci_attr_pci_parity_count,
	&edac_pci_attr_pci_nonparity_count,
	NULL,
};

/* No memory to release */
static void edac_pci_release(struct kobject *kobj)
{
	struct edac_pci_ctl_info *pci;

	pci = to_edacpci(kobj);

	debugf1("%s()\n", __func__);
	complete(&pci->kobj_complete);
}

static struct kobj_type ktype_edac_pci = {
	.release = edac_pci_release,
	.sysfs_ops = &edac_pci_sysfs_ops,
	.default_attrs = (struct attribute **)edac_pci_attr,
};

/**
 * edac_sysfs_pci_setup()
 *
 *	setup the sysfs for EDAC PCI attributes
 *	assumes edac_class has already been initialized
 */
int edac_pci_register_main_kobj(void)
{
	int err;
	struct sysdev_class *edac_class;

	debugf1("%s()\n", __func__);

	edac_class = edac_get_edac_class();
	if (edac_class == NULL) {
		debugf1("%s() no edac_class\n", __func__);
		return -ENODEV;
	}

	edac_pci_kobj.ktype = &ktype_edac_pci;

	edac_pci_kobj.parent = &edac_class->kset.kobj;

	err = kobject_set_name(&edac_pci_kobj, "pci");
	if (err)
		return err;

	/* Instanstiate the pci object */
	/* FIXME: maybe new sysdev_create_subdir() */
	err = kobject_register(&edac_pci_kobj);

	if (err) {
		debugf1("Failed to register '.../edac/pci'\n");
		return err;
	}

	debugf1("Registered '.../edac/pci' kobject\n");

	return 0;
}

/*
 * edac_pci_unregister_main_kobj()
 *
 *	perform the sysfs teardown for the PCI attributes
 */
void edac_pci_unregister_main_kobj(void)
{
	debugf0("%s()\n", __func__);
	init_completion(&edac_pci_kobj_complete);
	kobject_unregister(&edac_pci_kobj);
	wait_for_completion(&edac_pci_kobj_complete);
}

int edac_pci_create_sysfs(struct edac_pci_ctl_info *pci)
{
	int err;
	struct kobject *edac_kobj = &pci->kobj;

	if (atomic_inc_return(&edac_pci_sysfs_refcount) == 1) {
		err = edac_pci_register_main_kobj();
		if (err) {
			atomic_dec(&edac_pci_sysfs_refcount);
			return err;
		}
	}

	err = edac_pci_create_instance_kobj(pci, pci->pci_idx);
	if (err) {
		if (atomic_dec_return(&edac_pci_sysfs_refcount) == 0)
			edac_pci_unregister_main_kobj();
	}

	debugf0("%s() idx=%d\n", __func__, pci->pci_idx);

	err = sysfs_create_link(edac_kobj, &pci->dev->kobj, EDAC_PCI_SYMLINK);
	if (err) {
		debugf0("%s() sysfs_create_link() returned err= %d\n",
			__func__, err);
		return err;
	}

	return 0;
}

void edac_pci_remove_sysfs(struct edac_pci_ctl_info *pci)
{
	debugf0("%s()\n", __func__);

	edac_pci_delete_instance_kobj(pci, pci->pci_idx);

	sysfs_remove_link(&pci->kobj, EDAC_PCI_SYMLINK);

	if (atomic_dec_return(&edac_pci_sysfs_refcount) == 0)
		edac_pci_unregister_main_kobj();
}

/************************ PCI error handling *************************/
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

typedef void (*pci_parity_check_fn_t) (struct pci_dev * dev);

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
	u8 header_type;

	/* read the STATUS register on this device
	 */
	status = get_pci_parity_status(dev, 0);

	debugf2("PCI STATUS= 0x%04x %s\n", status, dev->dev.bus_id);

	/* check the status reg for errors */
	if (status) {
		if (status & (PCI_STATUS_SIG_SYSTEM_ERROR)) {
			edac_printk(KERN_CRIT, EDAC_PCI,
				    "Signaled System Error on %s\n",
				    pci_name(dev));
			atomic_inc(&pci_nonparity_count);
		}

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

	debugf2("PCI HEADER TYPE= 0x%02x %s\n", header_type, dev->dev.bus_id);

	if ((header_type & 0x7F) == PCI_HEADER_TYPE_BRIDGE) {
		/* On bridges, need to examine secondary status register  */
		status = get_pci_parity_status(dev, 1);

		debugf2("PCI SEC_STATUS= 0x%04x %s\n", status, dev->dev.bus_id);

		/* check the secondary status reg for errors */
		if (status) {
			if (status & (PCI_STATUS_SIG_SYSTEM_ERROR)) {
				edac_printk(KERN_CRIT, EDAC_PCI, "Bridge "
					    "Signaled System Error on %s\n",
					    pci_name(dev));
				atomic_inc(&pci_nonparity_count);
			}

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
	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		fn(dev);
	}
}

/*
 * edac_pci_do_parity_check
 *
 *	performs the actual PCI parity check operation
 */
void edac_pci_do_parity_check(void)
{
	unsigned long flags;
	int before_count;

	debugf3("%s()\n", __func__);

	if (!check_pci_errors)
		return;

	before_count = atomic_read(&pci_parity_count);

	/* scan all PCI devices looking for a Parity Error on devices and
	 * bridges
	 */
	local_irq_save(flags);
	edac_pci_dev_parity_iterator(edac_pci_dev_parity_test);
	local_irq_restore(flags);

	/* Only if operator has selected panic on PCI Error */
	if (edac_pci_get_panic_on_pe()) {
		/* If the count is different 'after' from 'before' */
		if (before_count != atomic_read(&pci_parity_count))
			panic("EDAC: PCI Parity Error");
	}
}

void edac_pci_clear_parity_errors(void)
{
	/* Clear any PCI bus parity errors that devices initially have logged
	 * in their registers.
	 */
	edac_pci_dev_parity_iterator(edac_pci_dev_parity_clear);
}
void edac_pci_handle_pe(struct edac_pci_ctl_info *pci, const char *msg)
{

	/* global PE counter incremented by edac_pci_do_parity_check() */
	atomic_inc(&pci->counters.pe_count);

	if (edac_pci_get_log_pe())
		edac_pci_printk(pci, KERN_WARNING,
				"Parity Error ctl: %s %d: %s\n",
				pci->ctl_name, pci->pci_idx, msg);

	/*
	 * poke all PCI devices and see which one is the troublemaker
	 * panic() is called if set
	 */
	edac_pci_do_parity_check();
}

EXPORT_SYMBOL_GPL(edac_pci_handle_pe);

void edac_pci_handle_npe(struct edac_pci_ctl_info *pci, const char *msg)
{

	/* global NPE counter incremented by edac_pci_do_parity_check() */
	atomic_inc(&pci->counters.npe_count);

	if (edac_pci_get_log_npe())
		edac_pci_printk(pci, KERN_WARNING,
				"Non-Parity Error ctl: %s %d: %s\n",
				pci->ctl_name, pci->pci_idx, msg);

	/*
	 * poke all PCI devices and see which one is the troublemaker
	 * panic() is called if set
	 */
	edac_pci_do_parity_check();
}

EXPORT_SYMBOL_GPL(edac_pci_handle_npe);

/*
 * Define the PCI parameter to the module
 */
module_param(check_pci_errors, int, 0644);
MODULE_PARM_DESC(check_pci_errors,
		 "Check for PCI bus parity errors: 0=off 1=on");
module_param(edac_pci_panic_on_pe, int, 0644);
MODULE_PARM_DESC(edac_pci_panic_on_pe,
		 "Panic on PCI Bus Parity error: 0=off 1=on");

#endif				/* CONFIG_PCI */
