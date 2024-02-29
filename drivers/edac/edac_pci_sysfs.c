/*
 * (C) 2005, 2006 Linux Networx (http://lnxi.com)
 * This file may be distributed under the terms of the
 * GNU General Public License.
 *
 * Written Doug Thompson <norsk5@xmission.com>
 *
 */
#include <linux/module.h>
#include <linux/edac.h>
#include <linux/slab.h>
#include <linux/ctype.h>

#include "edac_pci.h"
#include "edac_module.h"

#define EDAC_PCI_SYMLINK	"device"

/* data variables exported via sysfs */
static int check_pci_errors;		/* default NO check PCI parity */
static int edac_pci_panic_on_pe;	/* default NO panic on PCI Parity */
static int edac_pci_log_pe = 1;		/* log PCI parity errors */
static int edac_pci_log_npe = 1;	/* log PCI non-parity error errors */
static int edac_pci_poll_msec = 1000;	/* one second workq period */

static atomic_t pci_parity_count = ATOMIC_INIT(0);
static atomic_t pci_nonparity_count = ATOMIC_INIT(0);

static struct kobject *edac_pci_top_main_kobj;
static atomic_t edac_pci_sysfs_refcount = ATOMIC_INIT(0);

/* getter functions for the data variables */
int edac_pci_get_check_errors(void)
{
	return check_pci_errors;
}

static int edac_pci_get_log_pe(void)
{
	return edac_pci_log_pe;
}

static int edac_pci_get_log_npe(void)
{
	return edac_pci_log_npe;
}

static int edac_pci_get_panic_on_pe(void)
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

	edac_dbg(0, "\n");

	/* Form pointer to containing struct, the pci control struct */
	pci = to_instance(kobj);

	/* decrement reference count on top main kobj */
	kobject_put(edac_pci_top_main_kobj);

	kfree(pci);	/* Free the control struct */
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

/* fs_ops table */
static const struct sysfs_ops pci_instance_ops = {
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
static struct attribute *pci_instance_attrs[] = {
	&attr_instance_pe_count.attr,
	&attr_instance_npe_count.attr,
	NULL
};
ATTRIBUTE_GROUPS(pci_instance);

/* the ktype for a pci instance */
static struct kobj_type ktype_pci_instance = {
	.release = edac_pci_instance_release,
	.sysfs_ops = &pci_instance_ops,
	.default_groups = pci_instance_groups,
};

/*
 * edac_pci_create_instance_kobj
 *
 *	construct one EDAC PCI instance's kobject for use
 */
static int edac_pci_create_instance_kobj(struct edac_pci_ctl_info *pci, int idx)
{
	struct kobject *main_kobj;
	int err;

	edac_dbg(0, "\n");

	/* First bump the ref count on the top main kobj, which will
	 * track the number of PCI instances we have, and thus nest
	 * properly on keeping the module loaded
	 */
	main_kobj = kobject_get(edac_pci_top_main_kobj);
	if (!main_kobj) {
		err = -ENODEV;
		goto error_out;
	}

	/* And now register this new kobject under the main kobj */
	err = kobject_init_and_add(&pci->kobj, &ktype_pci_instance,
				   edac_pci_top_main_kobj, "pci%d", idx);
	if (err != 0) {
		edac_dbg(2, "failed to register instance pci%d\n", idx);
		kobject_put(edac_pci_top_main_kobj);
		goto error_out;
	}

	kobject_uevent(&pci->kobj, KOBJ_ADD);
	edac_dbg(1, "Register instance 'pci%d' kobject\n", idx);

	return 0;

	/* Error unwind statck */
error_out:
	return err;
}

/*
 * edac_pci_unregister_sysfs_instance_kobj
 *
 *	unregister the kobj for the EDAC PCI instance
 */
static void edac_pci_unregister_sysfs_instance_kobj(
			struct edac_pci_ctl_info *pci)
{
	edac_dbg(0, "\n");

	/* Unregister the instance kobject and allow its release
	 * function release the main reference count and then
	 * kfree the memory
	 */
	kobject_put(&pci->kobj);
}

/***************************** EDAC PCI sysfs root **********************/
#define to_edacpci(k) container_of(k, struct edac_pci_ctl_info, kobj)
#define to_edacpci_attr(a) container_of(a, struct edac_pci_attr, attr)

/* simple show/store functions for attributes */
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

	if (edac_pci_dev->store)
		return edac_pci_dev->store(edac_pci_dev->value, buffer, count);
	return -EIO;
}

static const struct sysfs_ops edac_pci_sysfs_ops = {
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
static struct attribute *edac_pci_attrs[] = {
	&edac_pci_attr_check_pci_errors.attr,
	&edac_pci_attr_edac_pci_log_pe.attr,
	&edac_pci_attr_edac_pci_log_npe.attr,
	&edac_pci_attr_edac_pci_panic_on_pe.attr,
	&edac_pci_attr_pci_parity_count.attr,
	&edac_pci_attr_pci_nonparity_count.attr,
	NULL,
};
ATTRIBUTE_GROUPS(edac_pci);

/*
 * edac_pci_release_main_kobj
 *
 *	This release function is called when the reference count to the
 *	passed kobj goes to zero.
 *
 *	This kobj is the 'main' kobject that EDAC PCI instances
 *	link to, and thus provide for proper nesting counts
 */
static void edac_pci_release_main_kobj(struct kobject *kobj)
{
	edac_dbg(0, "here to module_put(THIS_MODULE)\n");

	kfree(kobj);

	/* last reference to top EDAC PCI kobject has been removed,
	 * NOW release our ref count on the core module
	 */
	module_put(THIS_MODULE);
}

/* ktype struct for the EDAC PCI main kobj */
static struct kobj_type ktype_edac_pci_main_kobj = {
	.release = edac_pci_release_main_kobj,
	.sysfs_ops = &edac_pci_sysfs_ops,
	.default_groups = edac_pci_groups,
};

/**
 * edac_pci_main_kobj_setup: Setup the sysfs for EDAC PCI attributes.
 */
static int edac_pci_main_kobj_setup(void)
{
	int err = -ENODEV;
	const struct bus_type *edac_subsys;
	struct device *dev_root;

	edac_dbg(0, "\n");

	/* check and count if we have already created the main kobject */
	if (atomic_inc_return(&edac_pci_sysfs_refcount) != 1)
		return 0;

	/* First time, so create the main kobject and its
	 * controls and attributes
	 */
	edac_subsys = edac_get_sysfs_subsys();

	/* Bump the reference count on this module to ensure the
	 * modules isn't unloaded until we deconstruct the top
	 * level main kobj for EDAC PCI
	 */
	if (!try_module_get(THIS_MODULE)) {
		edac_dbg(1, "try_module_get() failed\n");
		goto decrement_count_fail;
	}

	edac_pci_top_main_kobj = kzalloc(sizeof(struct kobject), GFP_KERNEL);
	if (!edac_pci_top_main_kobj) {
		edac_dbg(1, "Failed to allocate\n");
		err = -ENOMEM;
		goto kzalloc_fail;
	}

	/* Instanstiate the pci object */
	dev_root = bus_get_dev_root(edac_subsys);
	if (dev_root) {
		err = kobject_init_and_add(edac_pci_top_main_kobj,
					   &ktype_edac_pci_main_kobj,
					   &dev_root->kobj, "pci");
		put_device(dev_root);
	}
	if (err) {
		edac_dbg(1, "Failed to register '.../edac/pci'\n");
		goto kobject_init_and_add_fail;
	}

	/* At this point, to 'release' the top level kobject
	 * for EDAC PCI, then edac_pci_main_kobj_teardown()
	 * must be used, for resources to be cleaned up properly
	 */
	kobject_uevent(edac_pci_top_main_kobj, KOBJ_ADD);
	edac_dbg(1, "Registered '.../edac/pci' kobject\n");

	return 0;

	/* Error unwind statck */
kobject_init_and_add_fail:
	kobject_put(edac_pci_top_main_kobj);

kzalloc_fail:
	module_put(THIS_MODULE);

decrement_count_fail:
	/* if are on this error exit, nothing to tear down */
	atomic_dec(&edac_pci_sysfs_refcount);

	return err;
}

/*
 * edac_pci_main_kobj_teardown()
 *
 *	if no longer linked (needed) remove the top level EDAC PCI
 *	kobject with its controls and attributes
 */
static void edac_pci_main_kobj_teardown(void)
{
	edac_dbg(0, "\n");

	/* Decrement the count and only if no more controller instances
	 * are connected perform the unregisteration of the top level
	 * main kobj
	 */
	if (atomic_dec_return(&edac_pci_sysfs_refcount) == 0) {
		edac_dbg(0, "called kobject_put on main kobj\n");
		kobject_put(edac_pci_top_main_kobj);
	}
}

int edac_pci_create_sysfs(struct edac_pci_ctl_info *pci)
{
	int err;
	struct kobject *edac_kobj = &pci->kobj;

	edac_dbg(0, "idx=%d\n", pci->pci_idx);

	/* create the top main EDAC PCI kobject, IF needed */
	err = edac_pci_main_kobj_setup();
	if (err)
		return err;

	/* Create this instance's kobject under the MAIN kobject */
	err = edac_pci_create_instance_kobj(pci, pci->pci_idx);
	if (err)
		goto unregister_cleanup;

	err = sysfs_create_link(edac_kobj, &pci->dev->kobj, EDAC_PCI_SYMLINK);
	if (err) {
		edac_dbg(0, "sysfs_create_link() returned err= %d\n", err);
		goto symlink_fail;
	}

	return 0;

	/* Error unwind stack */
symlink_fail:
	edac_pci_unregister_sysfs_instance_kobj(pci);

unregister_cleanup:
	edac_pci_main_kobj_teardown();

	return err;
}

void edac_pci_remove_sysfs(struct edac_pci_ctl_info *pci)
{
	edac_dbg(0, "index=%d\n", pci->pci_idx);

	/* Remove the symlink */
	sysfs_remove_link(&pci->kobj, EDAC_PCI_SYMLINK);

	/* remove this PCI instance's sysfs entries */
	edac_pci_unregister_sysfs_instance_kobj(pci);

	/* Call the main unregister function, which will determine
	 * if this 'pci' is the last instance.
	 * If it is, the main kobject will be unregistered as a result
	 */
	edac_dbg(0, "calling edac_pci_main_kobj_teardown()\n");
	edac_pci_main_kobj_teardown();
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


/* Clear any PCI parity errors logged by this device. */
static void edac_pci_dev_parity_clear(struct pci_dev *dev)
{
	u8 header_type;

	get_pci_parity_status(dev, 0);

	/* read the device TYPE, looking for bridges */
	pci_read_config_byte(dev, PCI_HEADER_TYPE, &header_type);

	if ((header_type & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_BRIDGE)
		get_pci_parity_status(dev, 1);
}

/*
 *  PCI Parity polling
 *
 *	Function to retrieve the current parity status
 *	and decode it
 *
 */
static void edac_pci_dev_parity_test(struct pci_dev *dev)
{
	unsigned long flags;
	u16 status;
	u8 header_type;

	/* stop any interrupts until we can acquire the status */
	local_irq_save(flags);

	/* read the STATUS register on this device */
	status = get_pci_parity_status(dev, 0);

	/* read the device TYPE, looking for bridges */
	pci_read_config_byte(dev, PCI_HEADER_TYPE, &header_type);

	local_irq_restore(flags);

	edac_dbg(4, "PCI STATUS= 0x%04x %s\n", status, dev_name(&dev->dev));

	/* check the status reg for errors on boards NOT marked as broken
	 * if broken, we cannot trust any of the status bits
	 */
	if (status && !dev->broken_parity_status) {
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


	edac_dbg(4, "PCI HEADER TYPE= 0x%02x %s\n",
		 header_type, dev_name(&dev->dev));

	if ((header_type & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_BRIDGE) {
		/* On bridges, need to examine secondary status register  */
		status = get_pci_parity_status(dev, 1);

		edac_dbg(4, "PCI SEC_STATUS= 0x%04x %s\n",
			 status, dev_name(&dev->dev));

		/* check the secondary status reg for errors,
		 * on NOT broken boards
		 */
		if (status && !dev->broken_parity_status) {
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

/* reduce some complexity in definition of the iterator */
typedef void (*pci_parity_check_fn_t) (struct pci_dev *dev);

/*
 * pci_dev parity list iterator
 *
 *	Scan the PCI device list looking for SERRORs, Master Parity ERRORS or
 *	Parity ERRORs on primary or secondary devices.
 */
static inline void edac_pci_dev_parity_iterator(pci_parity_check_fn_t fn)
{
	struct pci_dev *dev = NULL;

	for_each_pci_dev(dev)
		fn(dev);
}

/*
 * edac_pci_do_parity_check
 *
 *	performs the actual PCI parity check operation
 */
void edac_pci_do_parity_check(void)
{
	int before_count;

	edac_dbg(3, "\n");

	/* if policy has PCI check off, leave now */
	if (!check_pci_errors)
		return;

	before_count = atomic_read(&pci_parity_count);

	/* scan all PCI devices looking for a Parity Error on devices and
	 * bridges.
	 * The iterator calls pci_get_device() which might sleep, thus
	 * we cannot disable interrupts in this scan.
	 */
	edac_pci_dev_parity_iterator(edac_pci_dev_parity_test);

	/* Only if operator has selected panic on PCI Error */
	if (edac_pci_get_panic_on_pe()) {
		/* If the count is different 'after' from 'before' */
		if (before_count != atomic_read(&pci_parity_count))
			panic("EDAC: PCI Parity Error");
	}
}

/*
 * edac_pci_clear_parity_errors
 *
 *	function to perform an iteration over the PCI devices
 *	and clearn their current status
 */
void edac_pci_clear_parity_errors(void)
{
	/* Clear any PCI bus parity errors that devices initially have logged
	 * in their registers.
	 */
	edac_pci_dev_parity_iterator(edac_pci_dev_parity_clear);
}

/*
 * edac_pci_handle_pe
 *
 *	Called to handle a PARITY ERROR event
 */
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


/*
 * edac_pci_handle_npe
 *
 *	Called to handle a NON-PARITY ERROR event
 */
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
