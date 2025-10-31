// SPDX-License-Identifier: GPL-2.0
/*
 * Interface with platform TEE Security Manager (TSM) objects as defined by
 * PCIe r7.0 section 11 TEE Device Interface Security Protocol (TDISP)
 *
 * Copyright(c) 2024-2025 Intel Corporation. All rights reserved.
 */

#define dev_fmt(fmt) "PCI/TSM: " fmt

#include <linux/bitfield.h>
#include <linux/pci.h>
#include <linux/pci-doe.h>
#include <linux/pci-tsm.h>
#include <linux/sysfs.h>
#include <linux/tsm.h>
#include <linux/xarray.h>
#include "pci.h"

/*
 * Provide a read/write lock against the init / exit of pdev tsm
 * capabilities and arrival/departure of a TSM instance
 */
static DECLARE_RWSEM(pci_tsm_rwsem);

/*
 * Count of TSMs registered that support physical link operations vs device
 * security state management.
 */
static int pci_tsm_link_count;
static int pci_tsm_devsec_count;

static const struct pci_tsm_ops *to_pci_tsm_ops(struct pci_tsm *tsm)
{
	return tsm->tsm_dev->pci_ops;
}

static inline bool is_dsm(struct pci_dev *pdev)
{
	return pdev->tsm && pdev->tsm->dsm_dev == pdev;
}

static inline bool has_tee(struct pci_dev *pdev)
{
	return pdev->devcap & PCI_EXP_DEVCAP_TEE;
}

/* 'struct pci_tsm_pf0' wraps 'struct pci_tsm' when ->dsm_dev == ->pdev (self) */
static struct pci_tsm_pf0 *to_pci_tsm_pf0(struct pci_tsm *tsm)
{
	/*
	 * All "link" TSM contexts reference the device that hosts the DSM
	 * interface for a set of devices. Walk to the DSM device and cast its
	 * ->tsm context to a 'struct pci_tsm_pf0 *'.
	 */
	struct pci_dev *pf0 = tsm->dsm_dev;

	if (!is_pci_tsm_pf0(pf0) || !is_dsm(pf0)) {
		pci_WARN_ONCE(tsm->pdev, 1, "invalid context object\n");
		return NULL;
	}

	return container_of(pf0->tsm, struct pci_tsm_pf0, base_tsm);
}

static void tsm_remove(struct pci_tsm *tsm)
{
	struct pci_dev *pdev;

	if (!tsm)
		return;

	pdev = tsm->pdev;
	to_pci_tsm_ops(tsm)->remove(tsm);
	pdev->tsm = NULL;
}
DEFINE_FREE(tsm_remove, struct pci_tsm *, if (_T) tsm_remove(_T))

static void pci_tsm_walk_fns(struct pci_dev *pdev,
			     int (*cb)(struct pci_dev *pdev, void *data),
			     void *data)
{
	/* Walk subordinate physical functions */
	for (int i = 0; i < 8; i++) {
		struct pci_dev *pf __free(pci_dev_put) = pci_get_slot(
			pdev->bus, PCI_DEVFN(PCI_SLOT(pdev->devfn), i));

		if (!pf)
			continue;

		/* on entry function 0 has already run @cb */
		if (i > 0)
			cb(pf, data);

		/* walk virtual functions of each pf */
		for (int j = 0; j < pci_num_vf(pf); j++) {
			struct pci_dev *vf __free(pci_dev_put) =
				pci_get_domain_bus_and_slot(
					pci_domain_nr(pf->bus),
					pci_iov_virtfn_bus(pf, j),
					pci_iov_virtfn_devfn(pf, j));

			if (!vf)
				continue;

			cb(vf, data);
		}
	}

	/*
	 * Walk downstream devices, assumes that an upstream DSM is
	 * limited to downstream physical functions
	 */
	if (pci_pcie_type(pdev) == PCI_EXP_TYPE_UPSTREAM && is_dsm(pdev))
		pci_walk_bus(pdev->subordinate, cb, data);
}

static void pci_tsm_walk_fns_reverse(struct pci_dev *pdev,
				     int (*cb)(struct pci_dev *pdev,
					       void *data),
				     void *data)
{
	/* Reverse walk downstream devices */
	if (pci_pcie_type(pdev) == PCI_EXP_TYPE_UPSTREAM && is_dsm(pdev))
		pci_walk_bus_reverse(pdev->subordinate, cb, data);

	/* Reverse walk subordinate physical functions */
	for (int i = 7; i >= 0; i--) {
		struct pci_dev *pf __free(pci_dev_put) = pci_get_slot(
			pdev->bus, PCI_DEVFN(PCI_SLOT(pdev->devfn), i));

		if (!pf)
			continue;

		/* reverse walk virtual functions */
		for (int j = pci_num_vf(pf) - 1; j >= 0; j--) {
			struct pci_dev *vf __free(pci_dev_put) =
				pci_get_domain_bus_and_slot(
					pci_domain_nr(pf->bus),
					pci_iov_virtfn_bus(pf, j),
					pci_iov_virtfn_devfn(pf, j));

			if (!vf)
				continue;
			cb(vf, data);
		}

		/* on exit, caller will run @cb on function 0 */
		if (i > 0)
			cb(pf, data);
	}
}

static int probe_fn(struct pci_dev *pdev, void *dsm)
{
	struct pci_dev *dsm_dev = dsm;
	const struct pci_tsm_ops *ops = to_pci_tsm_ops(dsm_dev->tsm);

	pdev->tsm = ops->probe(dsm_dev->tsm->tsm_dev, pdev);
	pci_dbg(pdev, "setup TSM context: DSM: %s status: %s\n",
		pci_name(dsm_dev), pdev->tsm ? "success" : "failed");
	return 0;
}

static int pci_tsm_connect(struct pci_dev *pdev, struct tsm_dev *tsm_dev)
{
	int rc;
	struct pci_tsm_pf0 *tsm_pf0;
	const struct pci_tsm_ops *ops = tsm_dev->pci_ops;
	struct pci_tsm *pci_tsm __free(tsm_remove) = ops->probe(tsm_dev, pdev);

	/* connect() mutually exclusive with subfunction pci_tsm_init() */
	lockdep_assert_held_write(&pci_tsm_rwsem);

	if (!pci_tsm)
		return -ENXIO;

	pdev->tsm = pci_tsm;
	tsm_pf0 = to_pci_tsm_pf0(pdev->tsm);

	/* mutex_intr assumes connect() is always sysfs/user driven */
	ACQUIRE(mutex_intr, lock)(&tsm_pf0->lock);
	if ((rc = ACQUIRE_ERR(mutex_intr, &lock)))
		return rc;

	rc = ops->connect(pdev);
	if (rc)
		return rc;

	pdev->tsm = no_free_ptr(pci_tsm);

	/*
	 * Now that the DSM is established, probe() all the potential
	 * dependent functions. Failure to probe a function is not fatal
	 * to connect(), it just disables subsequent security operations
	 * for that function.
	 *
	 * Note this is done unconditionally, without regard to finding
	 * PCI_EXP_DEVCAP_TEE on the dependent function, for robustness. The DSM
	 * is the ultimate arbiter of security state relative to a given
	 * interface id, and if it says it can manage TDISP state of a function,
	 * let it.
	 */
	if (has_tee(pdev))
		pci_tsm_walk_fns(pdev, probe_fn, pdev);
	return 0;
}

static ssize_t connect_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct tsm_dev *tsm_dev;
	int rc;

	ACQUIRE(rwsem_read_intr, lock)(&pci_tsm_rwsem);
	if ((rc = ACQUIRE_ERR(rwsem_read_intr, &lock)))
		return rc;

	if (!pdev->tsm)
		return sysfs_emit(buf, "\n");

	tsm_dev = pdev->tsm->tsm_dev;
	return sysfs_emit(buf, "%s\n", dev_name(&tsm_dev->dev));
}

/* Is @tsm_dev managing physical link / session properties... */
static bool is_link_tsm(struct tsm_dev *tsm_dev)
{
	return tsm_dev && tsm_dev->pci_ops && tsm_dev->pci_ops->link_ops.probe;
}

/* ...or is @tsm_dev managing device security state ? */
static bool is_devsec_tsm(struct tsm_dev *tsm_dev)
{
	return tsm_dev && tsm_dev->pci_ops && tsm_dev->pci_ops->devsec_ops.lock;
}

static ssize_t connect_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	int rc, id;

	rc = sscanf(buf, "tsm%d\n", &id);
	if (rc != 1)
		return -EINVAL;

	ACQUIRE(rwsem_write_kill, lock)(&pci_tsm_rwsem);
	if ((rc = ACQUIRE_ERR(rwsem_write_kill, &lock)))
		return rc;

	if (pdev->tsm)
		return -EBUSY;

	struct tsm_dev *tsm_dev __free(put_tsm_dev) = find_tsm_dev(id);
	if (!is_link_tsm(tsm_dev))
		return -ENXIO;

	rc = pci_tsm_connect(pdev, tsm_dev);
	if (rc)
		return rc;
	return len;
}
static DEVICE_ATTR_RW(connect);

static int remove_fn(struct pci_dev *pdev, void *data)
{
	tsm_remove(pdev->tsm);
	return 0;
}

static void __pci_tsm_disconnect(struct pci_dev *pdev)
{
	struct pci_tsm_pf0 *tsm_pf0 = to_pci_tsm_pf0(pdev->tsm);
	const struct pci_tsm_ops *ops = to_pci_tsm_ops(pdev->tsm);

	/* disconnect() mutually exclusive with subfunction pci_tsm_init() */
	lockdep_assert_held_write(&pci_tsm_rwsem);

	/*
	 * disconnect() is uninterruptible as it may be called for device
	 * teardown
	 */
	guard(mutex)(&tsm_pf0->lock);
	pci_tsm_walk_fns_reverse(pdev, remove_fn, NULL);
	ops->disconnect(pdev);
}

static void pci_tsm_disconnect(struct pci_dev *pdev)
{
	__pci_tsm_disconnect(pdev);
	tsm_remove(pdev->tsm);
}

static ssize_t disconnect_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t len)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct tsm_dev *tsm_dev;
	int rc;

	ACQUIRE(rwsem_write_kill, lock)(&pci_tsm_rwsem);
	if ((rc = ACQUIRE_ERR(rwsem_write_kill, &lock)))
		return rc;

	if (!pdev->tsm)
		return -ENXIO;

	tsm_dev = pdev->tsm->tsm_dev;
	if (!sysfs_streq(buf, dev_name(&tsm_dev->dev)))
		return -EINVAL;

	pci_tsm_disconnect(pdev);
	return len;
}
static DEVICE_ATTR_WO(disconnect);

/* The 'authenticated' attribute is exclusive to the presence of a 'link' TSM */
static bool pci_tsm_link_group_visible(struct kobject *kobj)
{
	struct pci_dev *pdev = to_pci_dev(kobj_to_dev(kobj));

	return pci_tsm_link_count && is_pci_tsm_pf0(pdev);
}
DEFINE_SIMPLE_SYSFS_GROUP_VISIBLE(pci_tsm_link);

/*
 * 'link' and 'devsec' TSMs share the same 'tsm/' sysfs group, so the TSM type
 * specific attributes need individual visibility checks.
 */
static umode_t pci_tsm_attr_visible(struct kobject *kobj,
				    struct attribute *attr, int n)
{
	if (pci_tsm_link_group_visible(kobj)) {
		if (attr == &dev_attr_connect.attr ||
		    attr == &dev_attr_disconnect.attr)
			return attr->mode;
	}

	return 0;
}

static bool pci_tsm_group_visible(struct kobject *kobj)
{
	return pci_tsm_link_group_visible(kobj);
}
DEFINE_SYSFS_GROUP_VISIBLE(pci_tsm);

static struct attribute *pci_tsm_attrs[] = {
	&dev_attr_connect.attr,
	&dev_attr_disconnect.attr,
	NULL
};

const struct attribute_group pci_tsm_attr_group = {
	.name = "tsm",
	.attrs = pci_tsm_attrs,
	.is_visible = SYSFS_GROUP_VISIBLE(pci_tsm),
};

static ssize_t authenticated_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	/*
	 * When the SPDM session established via TSM the 'authenticated' state
	 * of the device is identical to the connect state.
	 */
	return connect_show(dev, attr, buf);
}
static DEVICE_ATTR_RO(authenticated);

static struct attribute *pci_tsm_auth_attrs[] = {
	&dev_attr_authenticated.attr,
	NULL
};

const struct attribute_group pci_tsm_auth_attr_group = {
	.attrs = pci_tsm_auth_attrs,
	.is_visible = SYSFS_GROUP_VISIBLE(pci_tsm_link),
};

/*
 * Retrieve physical function0 device whether it has TEE capability or not
 */
static struct pci_dev *pf0_dev_get(struct pci_dev *pdev)
{
	struct pci_dev *pf_dev = pci_physfn(pdev);

	if (PCI_FUNC(pf_dev->devfn) == 0)
		return pci_dev_get(pf_dev);

	return pci_get_slot(pf_dev->bus,
			    pf_dev->devfn - PCI_FUNC(pf_dev->devfn));
}

/*
 * Find the PCI Device instance that serves as the Device Security Manager (DSM)
 * for @pdev. Note that no additional reference is held for the resulting device
 * because that resulting object always has a registered lifetime
 * greater-than-or-equal to that of the @pdev argument. This is by virtue of
 * @pdev being a descendant of, or identical to, the returned DSM device.
 */
static struct pci_dev *find_dsm_dev(struct pci_dev *pdev)
{
	struct device *grandparent;
	struct pci_dev *uport;

	if (is_pci_tsm_pf0(pdev))
		return pdev;

	struct pci_dev *pf0 __free(pci_dev_put) = pf0_dev_get(pdev);
	if (!pf0)
		return NULL;

	if (is_dsm(pf0))
		return pf0;

	/*
	 * For cases where a switch may be hosting TDISP services on behalf of
	 * downstream devices, check the first upstream port relative to this
	 * endpoint.
	 */
	if (!pdev->dev.parent)
		return NULL;
	grandparent = pdev->dev.parent->parent;
	if (!grandparent)
		return NULL;
	if (!dev_is_pci(grandparent))
		return NULL;
	uport = to_pci_dev(grandparent);
	if (!pci_is_pcie(uport) ||
	    pci_pcie_type(uport) != PCI_EXP_TYPE_UPSTREAM)
		return NULL;

	if (is_dsm(uport))
		return uport;
	return NULL;
}

/**
 * pci_tsm_link_constructor() - base 'struct pci_tsm' initialization for link TSMs
 * @pdev: The PCI device
 * @tsm: context to initialize
 * @tsm_dev: Platform TEE Security Manager, initiator of security operations
 */
int pci_tsm_link_constructor(struct pci_dev *pdev, struct pci_tsm *tsm,
			     struct tsm_dev *tsm_dev)
{
	if (!is_link_tsm(tsm_dev))
		return -EINVAL;

	tsm->dsm_dev = find_dsm_dev(pdev);
	if (!tsm->dsm_dev) {
		pci_warn(pdev, "failed to find Device Security Manager\n");
		return -ENXIO;
	}
	tsm->pdev = pdev;
	tsm->tsm_dev = tsm_dev;

	return 0;
}
EXPORT_SYMBOL_GPL(pci_tsm_link_constructor);

/**
 * pci_tsm_pf0_constructor() - common 'struct pci_tsm_pf0' (DSM) initialization
 * @pdev: Physical Function 0 PCI device (as indicated by is_pci_tsm_pf0())
 * @tsm: context to initialize
 * @tsm_dev: Platform TEE Security Manager, initiator of security operations
 */
int pci_tsm_pf0_constructor(struct pci_dev *pdev, struct pci_tsm_pf0 *tsm,
			    struct tsm_dev *tsm_dev)
{
	mutex_init(&tsm->lock);
	tsm->doe_mb = pci_find_doe_mailbox(pdev, PCI_VENDOR_ID_PCI_SIG,
					   PCI_DOE_FEATURE_CMA);
	if (!tsm->doe_mb) {
		pci_warn(pdev, "TSM init failure, no CMA mailbox\n");
		return -ENODEV;
	}

	return pci_tsm_link_constructor(pdev, &tsm->base_tsm, tsm_dev);
}
EXPORT_SYMBOL_GPL(pci_tsm_pf0_constructor);

void pci_tsm_pf0_destructor(struct pci_tsm_pf0 *pf0_tsm)
{
	mutex_destroy(&pf0_tsm->lock);
}
EXPORT_SYMBOL_GPL(pci_tsm_pf0_destructor);

static void pf0_sysfs_enable(struct pci_dev *pdev)
{
	bool tee = has_tee(pdev);

	pci_dbg(pdev, "Device Security Manager detected (%s%s%s)\n",
		pdev->ide_cap ? "IDE" : "", pdev->ide_cap && tee ? " " : "",
		tee ? "TEE" : "");

	sysfs_update_group(&pdev->dev.kobj, &pci_tsm_auth_attr_group);
	sysfs_update_group(&pdev->dev.kobj, &pci_tsm_attr_group);
}

int pci_tsm_register(struct tsm_dev *tsm_dev)
{
	struct pci_dev *pdev = NULL;

	if (!tsm_dev)
		return -EINVAL;

	/* The TSM device must only implement one of link_ops or devsec_ops */
	if (!is_link_tsm(tsm_dev) && !is_devsec_tsm(tsm_dev))
		return -EINVAL;

	if (is_link_tsm(tsm_dev) && is_devsec_tsm(tsm_dev))
		return -EINVAL;

	guard(rwsem_write)(&pci_tsm_rwsem);

	/* On first enable, update sysfs groups */
	if (is_link_tsm(tsm_dev) && pci_tsm_link_count++ == 0) {
		for_each_pci_dev(pdev)
			if (is_pci_tsm_pf0(pdev))
				pf0_sysfs_enable(pdev);
	} else if (is_devsec_tsm(tsm_dev)) {
		pci_tsm_devsec_count++;
	}

	return 0;
}

static void pci_tsm_fn_exit(struct pci_dev *pdev)
{
	/* TODO: unbind the fn */
	tsm_remove(pdev->tsm);
}

/**
 * __pci_tsm_destroy() - destroy the TSM context for @pdev
 * @pdev: device to cleanup
 * @tsm_dev: the TSM device being removed, or NULL if @pdev is being removed.
 *
 * At device removal or TSM unregistration all established context
 * with the TSM is torn down. Additionally, if there are no more TSMs
 * registered, the PCI tsm/ sysfs attributes are hidden.
 */
static void __pci_tsm_destroy(struct pci_dev *pdev, struct tsm_dev *tsm_dev)
{
	struct pci_tsm *tsm = pdev->tsm;

	lockdep_assert_held_write(&pci_tsm_rwsem);

	/*
	 * First, handle the TSM removal case to shutdown @pdev sysfs, this is
	 * skipped if the device itself is being removed since sysfs goes away
	 * naturally at that point
	 */
	if (is_link_tsm(tsm_dev) && is_pci_tsm_pf0(pdev) && !pci_tsm_link_count) {
		sysfs_update_group(&pdev->dev.kobj, &pci_tsm_auth_attr_group);
		sysfs_update_group(&pdev->dev.kobj, &pci_tsm_attr_group);
	}

	/* Nothing else to do if this device never attached to the departing TSM */
	if (!tsm)
		return;

	/* Now lookup the tsm_dev to destroy TSM context */
	if (!tsm_dev)
		tsm_dev = tsm->tsm_dev;
	else if (tsm_dev != tsm->tsm_dev)
		return;

	if (is_link_tsm(tsm_dev) && is_pci_tsm_pf0(pdev))
		pci_tsm_disconnect(pdev);
	else
		pci_tsm_fn_exit(pdev);
}

void pci_tsm_destroy(struct pci_dev *pdev)
{
	guard(rwsem_write)(&pci_tsm_rwsem);
	__pci_tsm_destroy(pdev, NULL);
}

void pci_tsm_init(struct pci_dev *pdev)
{
	guard(rwsem_read)(&pci_tsm_rwsem);

	/*
	 * Subfunctions are either probed synchronous with connect() or later
	 * when either the SR-IOV configuration is changed, or, unlikely,
	 * connect() raced initial bus scanning.
	 */
	if (pdev->tsm)
		return;

	if (pci_tsm_link_count) {
		struct pci_dev *dsm = find_dsm_dev(pdev);

		if (!dsm)
			return;

		/*
		 * The only path to init a Device Security Manager capable
		 * device is via connect().
		 */
		if (!dsm->tsm)
			return;

		probe_fn(pdev, dsm);
	}
}

void pci_tsm_unregister(struct tsm_dev *tsm_dev)
{
	struct pci_dev *pdev = NULL;

	guard(rwsem_write)(&pci_tsm_rwsem);
	if (is_link_tsm(tsm_dev))
		pci_tsm_link_count--;
	if (is_devsec_tsm(tsm_dev))
		pci_tsm_devsec_count--;
	for_each_pci_dev_reverse(pdev)
		__pci_tsm_destroy(pdev, tsm_dev);
}

int pci_tsm_doe_transfer(struct pci_dev *pdev, u8 type, const void *req,
			 size_t req_sz, void *resp, size_t resp_sz)
{
	struct pci_tsm_pf0 *tsm;

	if (!pdev->tsm || !is_pci_tsm_pf0(pdev))
		return -ENXIO;

	tsm = to_pci_tsm_pf0(pdev->tsm);
	if (!tsm->doe_mb)
		return -ENXIO;

	return pci_doe(tsm->doe_mb, PCI_VENDOR_ID_PCI_SIG, type, req, req_sz,
		       resp, resp_sz);
}
EXPORT_SYMBOL_GPL(pci_tsm_doe_transfer);
