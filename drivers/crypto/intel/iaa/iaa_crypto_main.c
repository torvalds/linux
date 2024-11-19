// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2021 Intel Corporation. All rights rsvd. */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/iommu.h>
#include <uapi/linux/idxd.h>
#include <linux/highmem.h>
#include <linux/sched/smt.h>
#include <crypto/internal/acompress.h>

#include "idxd.h"
#include "iaa_crypto.h"
#include "iaa_crypto_stats.h"

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt)			"idxd: " IDXD_SUBDRIVER_NAME ": " fmt

#define IAA_ALG_PRIORITY               300

/* number of iaa instances probed */
static unsigned int nr_iaa;
static unsigned int nr_cpus;
static unsigned int nr_nodes;
static unsigned int nr_cpus_per_node;

/* Number of physical cpus sharing each iaa instance */
static unsigned int cpus_per_iaa;

static struct crypto_comp *deflate_generic_tfm;

/* Per-cpu lookup table for balanced wqs */
static struct wq_table_entry __percpu *wq_table;

static struct idxd_wq *wq_table_next_wq(int cpu)
{
	struct wq_table_entry *entry = per_cpu_ptr(wq_table, cpu);

	if (++entry->cur_wq >= entry->n_wqs)
		entry->cur_wq = 0;

	if (!entry->wqs[entry->cur_wq])
		return NULL;

	pr_debug("%s: returning wq at idx %d (iaa wq %d.%d) from cpu %d\n", __func__,
		 entry->cur_wq, entry->wqs[entry->cur_wq]->idxd->id,
		 entry->wqs[entry->cur_wq]->id, cpu);

	return entry->wqs[entry->cur_wq];
}

static void wq_table_add(int cpu, struct idxd_wq *wq)
{
	struct wq_table_entry *entry = per_cpu_ptr(wq_table, cpu);

	if (WARN_ON(entry->n_wqs == entry->max_wqs))
		return;

	entry->wqs[entry->n_wqs++] = wq;

	pr_debug("%s: added iaa wq %d.%d to idx %d of cpu %d\n", __func__,
		 entry->wqs[entry->n_wqs - 1]->idxd->id,
		 entry->wqs[entry->n_wqs - 1]->id, entry->n_wqs - 1, cpu);
}

static void wq_table_free_entry(int cpu)
{
	struct wq_table_entry *entry = per_cpu_ptr(wq_table, cpu);

	kfree(entry->wqs);
	memset(entry, 0, sizeof(*entry));
}

static void wq_table_clear_entry(int cpu)
{
	struct wq_table_entry *entry = per_cpu_ptr(wq_table, cpu);

	entry->n_wqs = 0;
	entry->cur_wq = 0;
	memset(entry->wqs, 0, entry->max_wqs * sizeof(struct idxd_wq *));
}

LIST_HEAD(iaa_devices);
DEFINE_MUTEX(iaa_devices_lock);

/* If enabled, IAA hw crypto algos are registered, unavailable otherwise */
static bool iaa_crypto_enabled;
static bool iaa_crypto_registered;

/* Verify results of IAA compress or not */
static bool iaa_verify_compress = true;

static ssize_t verify_compress_show(struct device_driver *driver, char *buf)
{
	return sprintf(buf, "%d\n", iaa_verify_compress);
}

static ssize_t verify_compress_store(struct device_driver *driver,
				     const char *buf, size_t count)
{
	int ret = -EBUSY;

	mutex_lock(&iaa_devices_lock);

	if (iaa_crypto_enabled)
		goto out;

	ret = kstrtobool(buf, &iaa_verify_compress);
	if (ret)
		goto out;

	ret = count;
out:
	mutex_unlock(&iaa_devices_lock);

	return ret;
}
static DRIVER_ATTR_RW(verify_compress);

/*
 * The iaa crypto driver supports three 'sync' methods determining how
 * compressions and decompressions are performed:
 *
 * - sync:      the compression or decompression completes before
 *              returning.  This is the mode used by the async crypto
 *              interface when the sync mode is set to 'sync' and by
 *              the sync crypto interface regardless of setting.
 *
 * - async:     the compression or decompression is submitted and returns
 *              immediately.  Completion interrupts are not used so
 *              the caller is responsible for polling the descriptor
 *              for completion.  This mode is applicable to only the
 *              async crypto interface and is ignored for anything
 *              else.
 *
 * - async_irq: the compression or decompression is submitted and
 *              returns immediately.  Completion interrupts are
 *              enabled so the caller can wait for the completion and
 *              yield to other threads.  When the compression or
 *              decompression completes, the completion is signaled
 *              and the caller awakened.  This mode is applicable to
 *              only the async crypto interface and is ignored for
 *              anything else.
 *
 * These modes can be set using the iaa_crypto sync_mode driver
 * attribute.
 */

/* Use async mode */
static bool async_mode;
/* Use interrupts */
static bool use_irq;

/**
 * set_iaa_sync_mode - Set IAA sync mode
 * @name: The name of the sync mode
 *
 * Make the IAA sync mode named @name the current sync mode used by
 * compression/decompression.
 */

static int set_iaa_sync_mode(const char *name)
{
	int ret = 0;

	if (sysfs_streq(name, "sync")) {
		async_mode = false;
		use_irq = false;
	} else if (sysfs_streq(name, "async")) {
		async_mode = true;
		use_irq = false;
	} else if (sysfs_streq(name, "async_irq")) {
		async_mode = true;
		use_irq = true;
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static ssize_t sync_mode_show(struct device_driver *driver, char *buf)
{
	int ret = 0;

	if (!async_mode && !use_irq)
		ret = sprintf(buf, "%s\n", "sync");
	else if (async_mode && !use_irq)
		ret = sprintf(buf, "%s\n", "async");
	else if (async_mode && use_irq)
		ret = sprintf(buf, "%s\n", "async_irq");

	return ret;
}

static ssize_t sync_mode_store(struct device_driver *driver,
			       const char *buf, size_t count)
{
	int ret = -EBUSY;

	mutex_lock(&iaa_devices_lock);

	if (iaa_crypto_enabled)
		goto out;

	ret = set_iaa_sync_mode(buf);
	if (ret == 0)
		ret = count;
out:
	mutex_unlock(&iaa_devices_lock);

	return ret;
}
static DRIVER_ATTR_RW(sync_mode);

static struct iaa_compression_mode *iaa_compression_modes[IAA_COMP_MODES_MAX];

static int find_empty_iaa_compression_mode(void)
{
	int i = -EINVAL;

	for (i = 0; i < IAA_COMP_MODES_MAX; i++) {
		if (iaa_compression_modes[i])
			continue;
		break;
	}

	return i;
}

static struct iaa_compression_mode *find_iaa_compression_mode(const char *name, int *idx)
{
	struct iaa_compression_mode *mode;
	int i;

	for (i = 0; i < IAA_COMP_MODES_MAX; i++) {
		mode = iaa_compression_modes[i];
		if (!mode)
			continue;

		if (!strcmp(mode->name, name)) {
			*idx = i;
			return iaa_compression_modes[i];
		}
	}

	return NULL;
}

static void free_iaa_compression_mode(struct iaa_compression_mode *mode)
{
	kfree(mode->name);
	kfree(mode->ll_table);
	kfree(mode->d_table);

	kfree(mode);
}

/*
 * IAA Compression modes are defined by an ll_table and a d_table.
 * These tables are typically generated and captured using statistics
 * collected from running actual compress/decompress workloads.
 *
 * A module or other kernel code can add and remove compression modes
 * with a given name using the exported @add_iaa_compression_mode()
 * and @remove_iaa_compression_mode functions.
 *
 * When a new compression mode is added, the tables are saved in a
 * global compression mode list.  When IAA devices are added, a
 * per-IAA device dma mapping is created for each IAA device, for each
 * compression mode.  These are the tables used to do the actual
 * compression/deccompression and are unmapped if/when the devices are
 * removed.  Currently, compression modes must be added before any
 * device is added, and removed after all devices have been removed.
 */

/**
 * remove_iaa_compression_mode - Remove an IAA compression mode
 * @name: The name the compression mode will be known as
 *
 * Remove the IAA compression mode named @name.
 */
void remove_iaa_compression_mode(const char *name)
{
	struct iaa_compression_mode *mode;
	int idx;

	mutex_lock(&iaa_devices_lock);

	if (!list_empty(&iaa_devices))
		goto out;

	mode = find_iaa_compression_mode(name, &idx);
	if (mode) {
		free_iaa_compression_mode(mode);
		iaa_compression_modes[idx] = NULL;
	}
out:
	mutex_unlock(&iaa_devices_lock);
}
EXPORT_SYMBOL_GPL(remove_iaa_compression_mode);

/**
 * add_iaa_compression_mode - Add an IAA compression mode
 * @name: The name the compression mode will be known as
 * @ll_table: The ll table
 * @ll_table_size: The ll table size in bytes
 * @d_table: The d table
 * @d_table_size: The d table size in bytes
 * @init: Optional callback function to init the compression mode data
 * @free: Optional callback function to free the compression mode data
 *
 * Add a new IAA compression mode named @name.
 *
 * Returns 0 if successful, errcode otherwise.
 */
int add_iaa_compression_mode(const char *name,
			     const u32 *ll_table,
			     int ll_table_size,
			     const u32 *d_table,
			     int d_table_size,
			     iaa_dev_comp_init_fn_t init,
			     iaa_dev_comp_free_fn_t free)
{
	struct iaa_compression_mode *mode;
	int idx, ret = -ENOMEM;

	mutex_lock(&iaa_devices_lock);

	if (!list_empty(&iaa_devices)) {
		ret = -EBUSY;
		goto out;
	}

	mode = kzalloc(sizeof(*mode), GFP_KERNEL);
	if (!mode)
		goto out;

	mode->name = kstrdup(name, GFP_KERNEL);
	if (!mode->name)
		goto free;

	if (ll_table) {
		mode->ll_table = kmemdup(ll_table, ll_table_size, GFP_KERNEL);
		if (!mode->ll_table)
			goto free;
		mode->ll_table_size = ll_table_size;
	}

	if (d_table) {
		mode->d_table = kmemdup(d_table, d_table_size, GFP_KERNEL);
		if (!mode->d_table)
			goto free;
		mode->d_table_size = d_table_size;
	}

	mode->init = init;
	mode->free = free;

	idx = find_empty_iaa_compression_mode();
	if (idx < 0)
		goto free;

	pr_debug("IAA compression mode %s added at idx %d\n",
		 mode->name, idx);

	iaa_compression_modes[idx] = mode;

	ret = 0;
out:
	mutex_unlock(&iaa_devices_lock);

	return ret;
free:
	free_iaa_compression_mode(mode);
	goto out;
}
EXPORT_SYMBOL_GPL(add_iaa_compression_mode);

static struct iaa_device_compression_mode *
get_iaa_device_compression_mode(struct iaa_device *iaa_device, int idx)
{
	return iaa_device->compression_modes[idx];
}

static void free_device_compression_mode(struct iaa_device *iaa_device,
					 struct iaa_device_compression_mode *device_mode)
{
	size_t size = sizeof(struct aecs_comp_table_record) + IAA_AECS_ALIGN;
	struct device *dev = &iaa_device->idxd->pdev->dev;

	kfree(device_mode->name);

	if (device_mode->aecs_comp_table)
		dma_free_coherent(dev, size, device_mode->aecs_comp_table,
				  device_mode->aecs_comp_table_dma_addr);
	kfree(device_mode);
}

#define IDXD_OP_FLAG_AECS_RW_TGLS       0x400000
#define IAX_AECS_DEFAULT_FLAG (IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CC)
#define IAX_AECS_COMPRESS_FLAG	(IAX_AECS_DEFAULT_FLAG | IDXD_OP_FLAG_RD_SRC2_AECS)
#define IAX_AECS_DECOMPRESS_FLAG (IAX_AECS_DEFAULT_FLAG | IDXD_OP_FLAG_RD_SRC2_AECS)
#define IAX_AECS_GEN_FLAG (IAX_AECS_DEFAULT_FLAG | \
						IDXD_OP_FLAG_WR_SRC2_AECS_COMP | \
						IDXD_OP_FLAG_AECS_RW_TGLS)

static int check_completion(struct device *dev,
			    struct iax_completion_record *comp,
			    bool compress,
			    bool only_once);

static int init_device_compression_mode(struct iaa_device *iaa_device,
					struct iaa_compression_mode *mode,
					int idx, struct idxd_wq *wq)
{
	size_t size = sizeof(struct aecs_comp_table_record) + IAA_AECS_ALIGN;
	struct device *dev = &iaa_device->idxd->pdev->dev;
	struct iaa_device_compression_mode *device_mode;
	int ret = -ENOMEM;

	device_mode = kzalloc(sizeof(*device_mode), GFP_KERNEL);
	if (!device_mode)
		return -ENOMEM;

	device_mode->name = kstrdup(mode->name, GFP_KERNEL);
	if (!device_mode->name)
		goto free;

	device_mode->aecs_comp_table = dma_alloc_coherent(dev, size,
							  &device_mode->aecs_comp_table_dma_addr, GFP_KERNEL);
	if (!device_mode->aecs_comp_table)
		goto free;

	/* Add Huffman table to aecs */
	memset(device_mode->aecs_comp_table, 0, sizeof(*device_mode->aecs_comp_table));
	memcpy(device_mode->aecs_comp_table->ll_sym, mode->ll_table, mode->ll_table_size);
	memcpy(device_mode->aecs_comp_table->d_sym, mode->d_table, mode->d_table_size);

	if (mode->init) {
		ret = mode->init(device_mode);
		if (ret)
			goto free;
	}

	/* mode index should match iaa_compression_modes idx */
	iaa_device->compression_modes[idx] = device_mode;

	pr_debug("IAA %s compression mode initialized for iaa device %d\n",
		 mode->name, iaa_device->idxd->id);

	ret = 0;
out:
	return ret;
free:
	pr_debug("IAA %s compression mode initialization failed for iaa device %d\n",
		 mode->name, iaa_device->idxd->id);

	free_device_compression_mode(iaa_device, device_mode);
	goto out;
}

static int init_device_compression_modes(struct iaa_device *iaa_device,
					 struct idxd_wq *wq)
{
	struct iaa_compression_mode *mode;
	int i, ret = 0;

	for (i = 0; i < IAA_COMP_MODES_MAX; i++) {
		mode = iaa_compression_modes[i];
		if (!mode)
			continue;

		ret = init_device_compression_mode(iaa_device, mode, i, wq);
		if (ret)
			break;
	}

	return ret;
}

static void remove_device_compression_modes(struct iaa_device *iaa_device)
{
	struct iaa_device_compression_mode *device_mode;
	int i;

	for (i = 0; i < IAA_COMP_MODES_MAX; i++) {
		device_mode = iaa_device->compression_modes[i];
		if (!device_mode)
			continue;

		if (iaa_compression_modes[i]->free)
			iaa_compression_modes[i]->free(device_mode);
		free_device_compression_mode(iaa_device, device_mode);
		iaa_device->compression_modes[i] = NULL;
	}
}

static struct iaa_device *iaa_device_alloc(void)
{
	struct iaa_device *iaa_device;

	iaa_device = kzalloc(sizeof(*iaa_device), GFP_KERNEL);
	if (!iaa_device)
		return NULL;

	INIT_LIST_HEAD(&iaa_device->wqs);

	return iaa_device;
}

static bool iaa_has_wq(struct iaa_device *iaa_device, struct idxd_wq *wq)
{
	struct iaa_wq *iaa_wq;

	list_for_each_entry(iaa_wq, &iaa_device->wqs, list) {
		if (iaa_wq->wq == wq)
			return true;
	}

	return false;
}

static struct iaa_device *add_iaa_device(struct idxd_device *idxd)
{
	struct iaa_device *iaa_device;

	iaa_device = iaa_device_alloc();
	if (!iaa_device)
		return NULL;

	iaa_device->idxd = idxd;

	list_add_tail(&iaa_device->list, &iaa_devices);

	nr_iaa++;

	return iaa_device;
}

static int init_iaa_device(struct iaa_device *iaa_device, struct iaa_wq *iaa_wq)
{
	int ret = 0;

	ret = init_device_compression_modes(iaa_device, iaa_wq->wq);
	if (ret)
		return ret;

	return ret;
}

static void del_iaa_device(struct iaa_device *iaa_device)
{
	list_del(&iaa_device->list);

	nr_iaa--;
}

static int add_iaa_wq(struct iaa_device *iaa_device, struct idxd_wq *wq,
		      struct iaa_wq **new_wq)
{
	struct idxd_device *idxd = iaa_device->idxd;
	struct pci_dev *pdev = idxd->pdev;
	struct device *dev = &pdev->dev;
	struct iaa_wq *iaa_wq;

	iaa_wq = kzalloc(sizeof(*iaa_wq), GFP_KERNEL);
	if (!iaa_wq)
		return -ENOMEM;

	iaa_wq->wq = wq;
	iaa_wq->iaa_device = iaa_device;
	idxd_wq_set_private(wq, iaa_wq);

	list_add_tail(&iaa_wq->list, &iaa_device->wqs);

	iaa_device->n_wq++;

	if (new_wq)
		*new_wq = iaa_wq;

	dev_dbg(dev, "added wq %d to iaa device %d, n_wq %d\n",
		wq->id, iaa_device->idxd->id, iaa_device->n_wq);

	return 0;
}

static void del_iaa_wq(struct iaa_device *iaa_device, struct idxd_wq *wq)
{
	struct idxd_device *idxd = iaa_device->idxd;
	struct pci_dev *pdev = idxd->pdev;
	struct device *dev = &pdev->dev;
	struct iaa_wq *iaa_wq;

	list_for_each_entry(iaa_wq, &iaa_device->wqs, list) {
		if (iaa_wq->wq == wq) {
			list_del(&iaa_wq->list);
			iaa_device->n_wq--;

			dev_dbg(dev, "removed wq %d from iaa_device %d, n_wq %d, nr_iaa %d\n",
				wq->id, iaa_device->idxd->id,
				iaa_device->n_wq, nr_iaa);

			if (iaa_device->n_wq == 0)
				del_iaa_device(iaa_device);
			break;
		}
	}
}

static void clear_wq_table(void)
{
	int cpu;

	for (cpu = 0; cpu < nr_cpus; cpu++)
		wq_table_clear_entry(cpu);

	pr_debug("cleared wq table\n");
}

static void free_iaa_device(struct iaa_device *iaa_device)
{
	if (!iaa_device)
		return;

	remove_device_compression_modes(iaa_device);
	kfree(iaa_device);
}

static void __free_iaa_wq(struct iaa_wq *iaa_wq)
{
	struct iaa_device *iaa_device;

	if (!iaa_wq)
		return;

	iaa_device = iaa_wq->iaa_device;
	if (iaa_device->n_wq == 0)
		free_iaa_device(iaa_wq->iaa_device);
}

static void free_iaa_wq(struct iaa_wq *iaa_wq)
{
	struct idxd_wq *wq;

	__free_iaa_wq(iaa_wq);

	wq = iaa_wq->wq;

	kfree(iaa_wq);
	idxd_wq_set_private(wq, NULL);
}

static int iaa_wq_get(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct iaa_wq *iaa_wq;
	int ret = 0;

	spin_lock(&idxd->dev_lock);
	iaa_wq = idxd_wq_get_private(wq);
	if (iaa_wq && !iaa_wq->remove) {
		iaa_wq->ref++;
		idxd_wq_get(wq);
	} else {
		ret = -ENODEV;
	}
	spin_unlock(&idxd->dev_lock);

	return ret;
}

static int iaa_wq_put(struct idxd_wq *wq)
{
	struct idxd_device *idxd = wq->idxd;
	struct iaa_wq *iaa_wq;
	bool free = false;
	int ret = 0;

	spin_lock(&idxd->dev_lock);
	iaa_wq = idxd_wq_get_private(wq);
	if (iaa_wq) {
		iaa_wq->ref--;
		if (iaa_wq->ref == 0 && iaa_wq->remove) {
			idxd_wq_set_private(wq, NULL);
			free = true;
		}
		idxd_wq_put(wq);
	} else {
		ret = -ENODEV;
	}
	spin_unlock(&idxd->dev_lock);
	if (free) {
		__free_iaa_wq(iaa_wq);
		kfree(iaa_wq);
	}

	return ret;
}

static void free_wq_table(void)
{
	int cpu;

	for (cpu = 0; cpu < nr_cpus; cpu++)
		wq_table_free_entry(cpu);

	free_percpu(wq_table);

	pr_debug("freed wq table\n");
}

static int alloc_wq_table(int max_wqs)
{
	struct wq_table_entry *entry;
	int cpu;

	wq_table = alloc_percpu(struct wq_table_entry);
	if (!wq_table)
		return -ENOMEM;

	for (cpu = 0; cpu < nr_cpus; cpu++) {
		entry = per_cpu_ptr(wq_table, cpu);
		entry->wqs = kcalloc(max_wqs, sizeof(struct wq *), GFP_KERNEL);
		if (!entry->wqs) {
			free_wq_table();
			return -ENOMEM;
		}

		entry->max_wqs = max_wqs;
	}

	pr_debug("initialized wq table\n");

	return 0;
}

static int save_iaa_wq(struct idxd_wq *wq)
{
	struct iaa_device *iaa_device, *found = NULL;
	struct idxd_device *idxd;
	struct pci_dev *pdev;
	struct device *dev;
	int ret = 0;

	list_for_each_entry(iaa_device, &iaa_devices, list) {
		if (iaa_device->idxd == wq->idxd) {
			idxd = iaa_device->idxd;
			pdev = idxd->pdev;
			dev = &pdev->dev;
			/*
			 * Check to see that we don't already have this wq.
			 * Shouldn't happen but we don't control probing.
			 */
			if (iaa_has_wq(iaa_device, wq)) {
				dev_dbg(dev, "same wq probed multiple times for iaa_device %p\n",
					iaa_device);
				goto out;
			}

			found = iaa_device;

			ret = add_iaa_wq(iaa_device, wq, NULL);
			if (ret)
				goto out;

			break;
		}
	}

	if (!found) {
		struct iaa_device *new_device;
		struct iaa_wq *new_wq;

		new_device = add_iaa_device(wq->idxd);
		if (!new_device) {
			ret = -ENOMEM;
			goto out;
		}

		ret = add_iaa_wq(new_device, wq, &new_wq);
		if (ret) {
			del_iaa_device(new_device);
			free_iaa_device(new_device);
			goto out;
		}

		ret = init_iaa_device(new_device, new_wq);
		if (ret) {
			del_iaa_wq(new_device, new_wq->wq);
			del_iaa_device(new_device);
			free_iaa_wq(new_wq);
			goto out;
		}
	}

	if (WARN_ON(nr_iaa == 0))
		return -EINVAL;

	cpus_per_iaa = (nr_nodes * nr_cpus_per_node) / nr_iaa;
	if (!cpus_per_iaa)
		cpus_per_iaa = 1;
out:
	return 0;
}

static void remove_iaa_wq(struct idxd_wq *wq)
{
	struct iaa_device *iaa_device;

	list_for_each_entry(iaa_device, &iaa_devices, list) {
		if (iaa_has_wq(iaa_device, wq)) {
			del_iaa_wq(iaa_device, wq);
			break;
		}
	}

	if (nr_iaa) {
		cpus_per_iaa = (nr_nodes * nr_cpus_per_node) / nr_iaa;
		if (!cpus_per_iaa)
			cpus_per_iaa = 1;
	} else
		cpus_per_iaa = 1;
}

static int wq_table_add_wqs(int iaa, int cpu)
{
	struct iaa_device *iaa_device, *found_device = NULL;
	int ret = 0, cur_iaa = 0, n_wqs_added = 0;
	struct idxd_device *idxd;
	struct iaa_wq *iaa_wq;
	struct pci_dev *pdev;
	struct device *dev;

	list_for_each_entry(iaa_device, &iaa_devices, list) {
		idxd = iaa_device->idxd;
		pdev = idxd->pdev;
		dev = &pdev->dev;

		if (cur_iaa != iaa) {
			cur_iaa++;
			continue;
		}

		found_device = iaa_device;
		dev_dbg(dev, "getting wq from iaa_device %d, cur_iaa %d\n",
			found_device->idxd->id, cur_iaa);
		break;
	}

	if (!found_device) {
		found_device = list_first_entry_or_null(&iaa_devices,
							struct iaa_device, list);
		if (!found_device) {
			pr_debug("couldn't find any iaa devices with wqs!\n");
			ret = -EINVAL;
			goto out;
		}
		cur_iaa = 0;

		idxd = found_device->idxd;
		pdev = idxd->pdev;
		dev = &pdev->dev;
		dev_dbg(dev, "getting wq from only iaa_device %d, cur_iaa %d\n",
			found_device->idxd->id, cur_iaa);
	}

	list_for_each_entry(iaa_wq, &found_device->wqs, list) {
		wq_table_add(cpu, iaa_wq->wq);
		pr_debug("rebalance: added wq for cpu=%d: iaa wq %d.%d\n",
			 cpu, iaa_wq->wq->idxd->id, iaa_wq->wq->id);
		n_wqs_added++;
	}

	if (!n_wqs_added) {
		pr_debug("couldn't find any iaa wqs!\n");
		ret = -EINVAL;
		goto out;
	}
out:
	return ret;
}

/*
 * Rebalance the wq table so that given a cpu, it's easy to find the
 * closest IAA instance.  The idea is to try to choose the most
 * appropriate IAA instance for a caller and spread available
 * workqueues around to clients.
 */
static void rebalance_wq_table(void)
{
	const struct cpumask *node_cpus;
	int node, cpu, iaa = -1;

	if (nr_iaa == 0)
		return;

	pr_debug("rebalance: nr_nodes=%d, nr_cpus %d, nr_iaa %d, cpus_per_iaa %d\n",
		 nr_nodes, nr_cpus, nr_iaa, cpus_per_iaa);

	clear_wq_table();

	if (nr_iaa == 1) {
		for (cpu = 0; cpu < nr_cpus; cpu++) {
			if (WARN_ON(wq_table_add_wqs(0, cpu))) {
				pr_debug("could not add any wqs for iaa 0 to cpu %d!\n", cpu);
				return;
			}
		}

		return;
	}

	for_each_node_with_cpus(node) {
		node_cpus = cpumask_of_node(node);

		for (cpu = 0; cpu <  cpumask_weight(node_cpus); cpu++) {
			int node_cpu = cpumask_nth(cpu, node_cpus);

			if (WARN_ON(node_cpu >= nr_cpu_ids)) {
				pr_debug("node_cpu %d doesn't exist!\n", node_cpu);
				return;
			}

			if ((cpu % cpus_per_iaa) == 0)
				iaa++;

			if (WARN_ON(wq_table_add_wqs(iaa, node_cpu))) {
				pr_debug("could not add any wqs for iaa %d to cpu %d!\n", iaa, cpu);
				return;
			}
		}
	}
}

static inline int check_completion(struct device *dev,
				   struct iax_completion_record *comp,
				   bool compress,
				   bool only_once)
{
	char *op_str = compress ? "compress" : "decompress";
	int status_checks = 0;
	int ret = 0;

	while (!comp->status) {
		if (only_once)
			return -EAGAIN;
		cpu_relax();
		if (status_checks++ >= IAA_COMPLETION_TIMEOUT) {
			/* Something is wrong with the hw, disable it. */
			dev_err(dev, "%s completion timed out - "
				"assuming broken hw, iaa_crypto now DISABLED\n",
				op_str);
			iaa_crypto_enabled = false;
			ret = -ETIMEDOUT;
			goto out;
		}
	}

	if (comp->status != IAX_COMP_SUCCESS) {
		if (comp->status == IAA_ERROR_WATCHDOG_EXPIRED) {
			ret = -ETIMEDOUT;
			dev_dbg(dev, "%s timed out, size=0x%x\n",
				op_str, comp->output_size);
			update_completion_timeout_errs();
			goto out;
		}

		if (comp->status == IAA_ANALYTICS_ERROR &&
		    comp->error_code == IAA_ERROR_COMP_BUF_OVERFLOW && compress) {
			ret = -E2BIG;
			dev_dbg(dev, "compressed > uncompressed size,"
				" not compressing, size=0x%x\n",
				comp->output_size);
			update_completion_comp_buf_overflow_errs();
			goto out;
		}

		if (comp->status == IAA_ERROR_DECOMP_BUF_OVERFLOW) {
			ret = -EOVERFLOW;
			goto out;
		}

		ret = -EINVAL;
		dev_dbg(dev, "iaa %s status=0x%x, error=0x%x, size=0x%x\n",
			op_str, comp->status, comp->error_code, comp->output_size);
		print_hex_dump(KERN_INFO, "cmp-rec: ", DUMP_PREFIX_OFFSET, 8, 1, comp, 64, 0);
		update_completion_einval_errs();

		goto out;
	}
out:
	return ret;
}

static int deflate_generic_decompress(struct acomp_req *req)
{
	void *src, *dst;
	int ret;

	src = kmap_local_page(sg_page(req->src)) + req->src->offset;
	dst = kmap_local_page(sg_page(req->dst)) + req->dst->offset;

	ret = crypto_comp_decompress(deflate_generic_tfm,
				     src, req->slen, dst, &req->dlen);

	kunmap_local(src);
	kunmap_local(dst);

	update_total_sw_decomp_calls();

	return ret;
}

static int iaa_remap_for_verify(struct device *dev, struct iaa_wq *iaa_wq,
				struct acomp_req *req,
				dma_addr_t *src_addr, dma_addr_t *dst_addr);

static int iaa_compress_verify(struct crypto_tfm *tfm, struct acomp_req *req,
			       struct idxd_wq *wq,
			       dma_addr_t src_addr, unsigned int slen,
			       dma_addr_t dst_addr, unsigned int *dlen,
			       u32 compression_crc);

static void iaa_desc_complete(struct idxd_desc *idxd_desc,
			      enum idxd_complete_type comp_type,
			      bool free_desc, void *__ctx,
			      u32 *status)
{
	struct iaa_device_compression_mode *active_compression_mode;
	struct iaa_compression_ctx *compression_ctx;
	struct crypto_ctx *ctx = __ctx;
	struct iaa_device *iaa_device;
	struct idxd_device *idxd;
	struct iaa_wq *iaa_wq;
	struct pci_dev *pdev;
	struct device *dev;
	int ret, err = 0;

	compression_ctx = crypto_tfm_ctx(ctx->tfm);

	iaa_wq = idxd_wq_get_private(idxd_desc->wq);
	iaa_device = iaa_wq->iaa_device;
	idxd = iaa_device->idxd;
	pdev = idxd->pdev;
	dev = &pdev->dev;

	active_compression_mode = get_iaa_device_compression_mode(iaa_device,
								  compression_ctx->mode);
	dev_dbg(dev, "%s: compression mode %s,"
		" ctx->src_addr %llx, ctx->dst_addr %llx\n", __func__,
		active_compression_mode->name,
		ctx->src_addr, ctx->dst_addr);

	ret = check_completion(dev, idxd_desc->iax_completion,
			       ctx->compress, false);
	if (ret) {
		dev_dbg(dev, "%s: check_completion failed ret=%d\n", __func__, ret);
		if (!ctx->compress &&
		    idxd_desc->iax_completion->status == IAA_ANALYTICS_ERROR) {
			pr_warn("%s: falling back to deflate-generic decompress, "
				"analytics error code %x\n", __func__,
				idxd_desc->iax_completion->error_code);
			ret = deflate_generic_decompress(ctx->req);
			if (ret) {
				dev_dbg(dev, "%s: deflate-generic failed ret=%d\n",
					__func__, ret);
				err = -EIO;
				goto err;
			}
		} else {
			err = -EIO;
			goto err;
		}
	} else {
		ctx->req->dlen = idxd_desc->iax_completion->output_size;
	}

	/* Update stats */
	if (ctx->compress) {
		update_total_comp_bytes_out(ctx->req->dlen);
		update_wq_comp_bytes(iaa_wq->wq, ctx->req->dlen);
	} else {
		update_total_decomp_bytes_in(ctx->req->slen);
		update_wq_decomp_bytes(iaa_wq->wq, ctx->req->slen);
	}

	if (ctx->compress && compression_ctx->verify_compress) {
		dma_addr_t src_addr, dst_addr;
		u32 compression_crc;

		compression_crc = idxd_desc->iax_completion->crc;

		ret = iaa_remap_for_verify(dev, iaa_wq, ctx->req, &src_addr, &dst_addr);
		if (ret) {
			dev_dbg(dev, "%s: compress verify remap failed ret=%d\n", __func__, ret);
			err = -EIO;
			goto out;
		}

		ret = iaa_compress_verify(ctx->tfm, ctx->req, iaa_wq->wq, src_addr,
					  ctx->req->slen, dst_addr, &ctx->req->dlen,
					  compression_crc);
		if (ret) {
			dev_dbg(dev, "%s: compress verify failed ret=%d\n", __func__, ret);
			err = -EIO;
		}

		dma_unmap_sg(dev, ctx->req->dst, sg_nents(ctx->req->dst), DMA_TO_DEVICE);
		dma_unmap_sg(dev, ctx->req->src, sg_nents(ctx->req->src), DMA_FROM_DEVICE);

		goto out;
	}
err:
	dma_unmap_sg(dev, ctx->req->dst, sg_nents(ctx->req->dst), DMA_FROM_DEVICE);
	dma_unmap_sg(dev, ctx->req->src, sg_nents(ctx->req->src), DMA_TO_DEVICE);
out:
	if (ret != 0)
		dev_dbg(dev, "asynchronous compress failed ret=%d\n", ret);

	if (ctx->req->base.complete)
		acomp_request_complete(ctx->req, err);

	if (free_desc)
		idxd_free_desc(idxd_desc->wq, idxd_desc);
	iaa_wq_put(idxd_desc->wq);
}

static int iaa_compress(struct crypto_tfm *tfm,	struct acomp_req *req,
			struct idxd_wq *wq,
			dma_addr_t src_addr, unsigned int slen,
			dma_addr_t dst_addr, unsigned int *dlen,
			u32 *compression_crc,
			bool disable_async)
{
	struct iaa_device_compression_mode *active_compression_mode;
	struct iaa_compression_ctx *ctx = crypto_tfm_ctx(tfm);
	struct iaa_device *iaa_device;
	struct idxd_desc *idxd_desc;
	struct iax_hw_desc *desc;
	struct idxd_device *idxd;
	struct iaa_wq *iaa_wq;
	struct pci_dev *pdev;
	struct device *dev;
	int ret = 0;

	iaa_wq = idxd_wq_get_private(wq);
	iaa_device = iaa_wq->iaa_device;
	idxd = iaa_device->idxd;
	pdev = idxd->pdev;
	dev = &pdev->dev;

	active_compression_mode = get_iaa_device_compression_mode(iaa_device, ctx->mode);

	idxd_desc = idxd_alloc_desc(wq, IDXD_OP_BLOCK);
	if (IS_ERR(idxd_desc)) {
		dev_dbg(dev, "idxd descriptor allocation failed\n");
		dev_dbg(dev, "iaa compress failed: ret=%ld\n", PTR_ERR(idxd_desc));
		return PTR_ERR(idxd_desc);
	}
	desc = idxd_desc->iax_hw;

	desc->flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR |
		IDXD_OP_FLAG_RD_SRC2_AECS | IDXD_OP_FLAG_CC;
	desc->opcode = IAX_OPCODE_COMPRESS;
	desc->compr_flags = IAA_COMP_FLAGS;
	desc->priv = 0;

	desc->src1_addr = (u64)src_addr;
	desc->src1_size = slen;
	desc->dst_addr = (u64)dst_addr;
	desc->max_dst_size = *dlen;
	desc->src2_addr = active_compression_mode->aecs_comp_table_dma_addr;
	desc->src2_size = sizeof(struct aecs_comp_table_record);
	desc->completion_addr = idxd_desc->compl_dma;

	if (ctx->use_irq && !disable_async) {
		desc->flags |= IDXD_OP_FLAG_RCI;

		idxd_desc->crypto.req = req;
		idxd_desc->crypto.tfm = tfm;
		idxd_desc->crypto.src_addr = src_addr;
		idxd_desc->crypto.dst_addr = dst_addr;
		idxd_desc->crypto.compress = true;

		dev_dbg(dev, "%s use_async_irq: compression mode %s,"
			" src_addr %llx, dst_addr %llx\n", __func__,
			active_compression_mode->name,
			src_addr, dst_addr);
	} else if (ctx->async_mode && !disable_async)
		req->base.data = idxd_desc;

	dev_dbg(dev, "%s: compression mode %s,"
		" desc->src1_addr %llx, desc->src1_size %d,"
		" desc->dst_addr %llx, desc->max_dst_size %d,"
		" desc->src2_addr %llx, desc->src2_size %d\n", __func__,
		active_compression_mode->name,
		desc->src1_addr, desc->src1_size, desc->dst_addr,
		desc->max_dst_size, desc->src2_addr, desc->src2_size);

	ret = idxd_submit_desc(wq, idxd_desc);
	if (ret) {
		dev_dbg(dev, "submit_desc failed ret=%d\n", ret);
		goto err;
	}

	/* Update stats */
	update_total_comp_calls();
	update_wq_comp_calls(wq);

	if (ctx->async_mode && !disable_async) {
		ret = -EINPROGRESS;
		dev_dbg(dev, "%s: returning -EINPROGRESS\n", __func__);
		goto out;
	}

	ret = check_completion(dev, idxd_desc->iax_completion, true, false);
	if (ret) {
		dev_dbg(dev, "check_completion failed ret=%d\n", ret);
		goto err;
	}

	*dlen = idxd_desc->iax_completion->output_size;

	/* Update stats */
	update_total_comp_bytes_out(*dlen);
	update_wq_comp_bytes(wq, *dlen);

	*compression_crc = idxd_desc->iax_completion->crc;

	if (!ctx->async_mode || disable_async)
		idxd_free_desc(wq, idxd_desc);
out:
	return ret;
err:
	idxd_free_desc(wq, idxd_desc);
	dev_dbg(dev, "iaa compress failed: ret=%d\n", ret);

	goto out;
}

static int iaa_remap_for_verify(struct device *dev, struct iaa_wq *iaa_wq,
				struct acomp_req *req,
				dma_addr_t *src_addr, dma_addr_t *dst_addr)
{
	int ret = 0;
	int nr_sgs;

	dma_unmap_sg(dev, req->dst, sg_nents(req->dst), DMA_FROM_DEVICE);
	dma_unmap_sg(dev, req->src, sg_nents(req->src), DMA_TO_DEVICE);

	nr_sgs = dma_map_sg(dev, req->src, sg_nents(req->src), DMA_FROM_DEVICE);
	if (nr_sgs <= 0 || nr_sgs > 1) {
		dev_dbg(dev, "verify: couldn't map src sg for iaa device %d,"
			" wq %d: ret=%d\n", iaa_wq->iaa_device->idxd->id,
			iaa_wq->wq->id, ret);
		ret = -EIO;
		goto out;
	}
	*src_addr = sg_dma_address(req->src);
	dev_dbg(dev, "verify: dma_map_sg, src_addr %llx, nr_sgs %d, req->src %p,"
		" req->slen %d, sg_dma_len(sg) %d\n", *src_addr, nr_sgs,
		req->src, req->slen, sg_dma_len(req->src));

	nr_sgs = dma_map_sg(dev, req->dst, sg_nents(req->dst), DMA_TO_DEVICE);
	if (nr_sgs <= 0 || nr_sgs > 1) {
		dev_dbg(dev, "verify: couldn't map dst sg for iaa device %d,"
			" wq %d: ret=%d\n", iaa_wq->iaa_device->idxd->id,
			iaa_wq->wq->id, ret);
		ret = -EIO;
		dma_unmap_sg(dev, req->src, sg_nents(req->src), DMA_FROM_DEVICE);
		goto out;
	}
	*dst_addr = sg_dma_address(req->dst);
	dev_dbg(dev, "verify: dma_map_sg, dst_addr %llx, nr_sgs %d, req->dst %p,"
		" req->dlen %d, sg_dma_len(sg) %d\n", *dst_addr, nr_sgs,
		req->dst, req->dlen, sg_dma_len(req->dst));
out:
	return ret;
}

static int iaa_compress_verify(struct crypto_tfm *tfm, struct acomp_req *req,
			       struct idxd_wq *wq,
			       dma_addr_t src_addr, unsigned int slen,
			       dma_addr_t dst_addr, unsigned int *dlen,
			       u32 compression_crc)
{
	struct iaa_device_compression_mode *active_compression_mode;
	struct iaa_compression_ctx *ctx = crypto_tfm_ctx(tfm);
	struct iaa_device *iaa_device;
	struct idxd_desc *idxd_desc;
	struct iax_hw_desc *desc;
	struct idxd_device *idxd;
	struct iaa_wq *iaa_wq;
	struct pci_dev *pdev;
	struct device *dev;
	int ret = 0;

	iaa_wq = idxd_wq_get_private(wq);
	iaa_device = iaa_wq->iaa_device;
	idxd = iaa_device->idxd;
	pdev = idxd->pdev;
	dev = &pdev->dev;

	active_compression_mode = get_iaa_device_compression_mode(iaa_device, ctx->mode);

	idxd_desc = idxd_alloc_desc(wq, IDXD_OP_BLOCK);
	if (IS_ERR(idxd_desc)) {
		dev_dbg(dev, "idxd descriptor allocation failed\n");
		dev_dbg(dev, "iaa compress failed: ret=%ld\n",
			PTR_ERR(idxd_desc));
		return PTR_ERR(idxd_desc);
	}
	desc = idxd_desc->iax_hw;

	/* Verify (optional) - decompress and check crc, suppress dest write */

	desc->flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CC;
	desc->opcode = IAX_OPCODE_DECOMPRESS;
	desc->decompr_flags = IAA_DECOMP_FLAGS | IAA_DECOMP_SUPPRESS_OUTPUT;
	desc->priv = 0;

	desc->src1_addr = (u64)dst_addr;
	desc->src1_size = *dlen;
	desc->dst_addr = (u64)src_addr;
	desc->max_dst_size = slen;
	desc->completion_addr = idxd_desc->compl_dma;

	dev_dbg(dev, "(verify) compression mode %s,"
		" desc->src1_addr %llx, desc->src1_size %d,"
		" desc->dst_addr %llx, desc->max_dst_size %d,"
		" desc->src2_addr %llx, desc->src2_size %d\n",
		active_compression_mode->name,
		desc->src1_addr, desc->src1_size, desc->dst_addr,
		desc->max_dst_size, desc->src2_addr, desc->src2_size);

	ret = idxd_submit_desc(wq, idxd_desc);
	if (ret) {
		dev_dbg(dev, "submit_desc (verify) failed ret=%d\n", ret);
		goto err;
	}

	ret = check_completion(dev, idxd_desc->iax_completion, false, false);
	if (ret) {
		dev_dbg(dev, "(verify) check_completion failed ret=%d\n", ret);
		goto err;
	}

	if (compression_crc != idxd_desc->iax_completion->crc) {
		ret = -EINVAL;
		dev_dbg(dev, "(verify) iaa comp/decomp crc mismatch:"
			" comp=0x%x, decomp=0x%x\n", compression_crc,
			idxd_desc->iax_completion->crc);
		print_hex_dump(KERN_INFO, "cmp-rec: ", DUMP_PREFIX_OFFSET,
			       8, 1, idxd_desc->iax_completion, 64, 0);
		goto err;
	}

	idxd_free_desc(wq, idxd_desc);
out:
	return ret;
err:
	idxd_free_desc(wq, idxd_desc);
	dev_dbg(dev, "iaa compress failed: ret=%d\n", ret);

	goto out;
}

static int iaa_decompress(struct crypto_tfm *tfm, struct acomp_req *req,
			  struct idxd_wq *wq,
			  dma_addr_t src_addr, unsigned int slen,
			  dma_addr_t dst_addr, unsigned int *dlen,
			  bool disable_async)
{
	struct iaa_device_compression_mode *active_compression_mode;
	struct iaa_compression_ctx *ctx = crypto_tfm_ctx(tfm);
	struct iaa_device *iaa_device;
	struct idxd_desc *idxd_desc;
	struct iax_hw_desc *desc;
	struct idxd_device *idxd;
	struct iaa_wq *iaa_wq;
	struct pci_dev *pdev;
	struct device *dev;
	int ret = 0;

	iaa_wq = idxd_wq_get_private(wq);
	iaa_device = iaa_wq->iaa_device;
	idxd = iaa_device->idxd;
	pdev = idxd->pdev;
	dev = &pdev->dev;

	active_compression_mode = get_iaa_device_compression_mode(iaa_device, ctx->mode);

	idxd_desc = idxd_alloc_desc(wq, IDXD_OP_BLOCK);
	if (IS_ERR(idxd_desc)) {
		dev_dbg(dev, "idxd descriptor allocation failed\n");
		dev_dbg(dev, "iaa decompress failed: ret=%ld\n",
			PTR_ERR(idxd_desc));
		return PTR_ERR(idxd_desc);
	}
	desc = idxd_desc->iax_hw;

	desc->flags = IDXD_OP_FLAG_CRAV | IDXD_OP_FLAG_RCR | IDXD_OP_FLAG_CC;
	desc->opcode = IAX_OPCODE_DECOMPRESS;
	desc->max_dst_size = PAGE_SIZE;
	desc->decompr_flags = IAA_DECOMP_FLAGS;
	desc->priv = 0;

	desc->src1_addr = (u64)src_addr;
	desc->dst_addr = (u64)dst_addr;
	desc->max_dst_size = *dlen;
	desc->src1_size = slen;
	desc->completion_addr = idxd_desc->compl_dma;

	if (ctx->use_irq && !disable_async) {
		desc->flags |= IDXD_OP_FLAG_RCI;

		idxd_desc->crypto.req = req;
		idxd_desc->crypto.tfm = tfm;
		idxd_desc->crypto.src_addr = src_addr;
		idxd_desc->crypto.dst_addr = dst_addr;
		idxd_desc->crypto.compress = false;

		dev_dbg(dev, "%s: use_async_irq compression mode %s,"
			" src_addr %llx, dst_addr %llx\n", __func__,
			active_compression_mode->name,
			src_addr, dst_addr);
	} else if (ctx->async_mode && !disable_async)
		req->base.data = idxd_desc;

	dev_dbg(dev, "%s: decompression mode %s,"
		" desc->src1_addr %llx, desc->src1_size %d,"
		" desc->dst_addr %llx, desc->max_dst_size %d,"
		" desc->src2_addr %llx, desc->src2_size %d\n", __func__,
		active_compression_mode->name,
		desc->src1_addr, desc->src1_size, desc->dst_addr,
		desc->max_dst_size, desc->src2_addr, desc->src2_size);

	ret = idxd_submit_desc(wq, idxd_desc);
	if (ret) {
		dev_dbg(dev, "submit_desc failed ret=%d\n", ret);
		goto err;
	}

	/* Update stats */
	update_total_decomp_calls();
	update_wq_decomp_calls(wq);

	if (ctx->async_mode && !disable_async) {
		ret = -EINPROGRESS;
		dev_dbg(dev, "%s: returning -EINPROGRESS\n", __func__);
		goto out;
	}

	ret = check_completion(dev, idxd_desc->iax_completion, false, false);
	if (ret) {
		dev_dbg(dev, "%s: check_completion failed ret=%d\n", __func__, ret);
		if (idxd_desc->iax_completion->status == IAA_ANALYTICS_ERROR) {
			pr_warn("%s: falling back to deflate-generic decompress, "
				"analytics error code %x\n", __func__,
				idxd_desc->iax_completion->error_code);
			ret = deflate_generic_decompress(req);
			if (ret) {
				dev_dbg(dev, "%s: deflate-generic failed ret=%d\n",
					__func__, ret);
				goto err;
			}
		} else {
			goto err;
		}
	} else {
		req->dlen = idxd_desc->iax_completion->output_size;
	}

	*dlen = req->dlen;

	if (!ctx->async_mode || disable_async)
		idxd_free_desc(wq, idxd_desc);

	/* Update stats */
	update_total_decomp_bytes_in(slen);
	update_wq_decomp_bytes(wq, slen);
out:
	return ret;
err:
	idxd_free_desc(wq, idxd_desc);
	dev_dbg(dev, "iaa decompress failed: ret=%d\n", ret);

	goto out;
}

static int iaa_comp_acompress(struct acomp_req *req)
{
	struct iaa_compression_ctx *compression_ctx;
	struct crypto_tfm *tfm = req->base.tfm;
	dma_addr_t src_addr, dst_addr;
	bool disable_async = false;
	int nr_sgs, cpu, ret = 0;
	struct iaa_wq *iaa_wq;
	u32 compression_crc;
	struct idxd_wq *wq;
	struct device *dev;
	int order = -1;

	compression_ctx = crypto_tfm_ctx(tfm);

	if (!iaa_crypto_enabled) {
		pr_debug("iaa_crypto disabled, not compressing\n");
		return -ENODEV;
	}

	if (!req->src || !req->slen) {
		pr_debug("invalid src, not compressing\n");
		return -EINVAL;
	}

	cpu = get_cpu();
	wq = wq_table_next_wq(cpu);
	put_cpu();
	if (!wq) {
		pr_debug("no wq configured for cpu=%d\n", cpu);
		return -ENODEV;
	}

	ret = iaa_wq_get(wq);
	if (ret) {
		pr_debug("no wq available for cpu=%d\n", cpu);
		return -ENODEV;
	}

	iaa_wq = idxd_wq_get_private(wq);

	if (!req->dst) {
		gfp_t flags = req->flags & CRYPTO_TFM_REQ_MAY_SLEEP ? GFP_KERNEL : GFP_ATOMIC;

		/* incompressible data will always be < 2 * slen */
		req->dlen = 2 * req->slen;
		order = order_base_2(round_up(req->dlen, PAGE_SIZE) / PAGE_SIZE);
		req->dst = sgl_alloc_order(req->dlen, order, false, flags, NULL);
		if (!req->dst) {
			ret = -ENOMEM;
			order = -1;
			goto out;
		}
		disable_async = true;
	}

	dev = &wq->idxd->pdev->dev;

	nr_sgs = dma_map_sg(dev, req->src, sg_nents(req->src), DMA_TO_DEVICE);
	if (nr_sgs <= 0 || nr_sgs > 1) {
		dev_dbg(dev, "couldn't map src sg for iaa device %d,"
			" wq %d: ret=%d\n", iaa_wq->iaa_device->idxd->id,
			iaa_wq->wq->id, ret);
		ret = -EIO;
		goto out;
	}
	src_addr = sg_dma_address(req->src);
	dev_dbg(dev, "dma_map_sg, src_addr %llx, nr_sgs %d, req->src %p,"
		" req->slen %d, sg_dma_len(sg) %d\n", src_addr, nr_sgs,
		req->src, req->slen, sg_dma_len(req->src));

	nr_sgs = dma_map_sg(dev, req->dst, sg_nents(req->dst), DMA_FROM_DEVICE);
	if (nr_sgs <= 0 || nr_sgs > 1) {
		dev_dbg(dev, "couldn't map dst sg for iaa device %d,"
			" wq %d: ret=%d\n", iaa_wq->iaa_device->idxd->id,
			iaa_wq->wq->id, ret);
		ret = -EIO;
		goto err_map_dst;
	}
	dst_addr = sg_dma_address(req->dst);
	dev_dbg(dev, "dma_map_sg, dst_addr %llx, nr_sgs %d, req->dst %p,"
		" req->dlen %d, sg_dma_len(sg) %d\n", dst_addr, nr_sgs,
		req->dst, req->dlen, sg_dma_len(req->dst));

	ret = iaa_compress(tfm, req, wq, src_addr, req->slen, dst_addr,
			   &req->dlen, &compression_crc, disable_async);
	if (ret == -EINPROGRESS)
		return ret;

	if (!ret && compression_ctx->verify_compress) {
		ret = iaa_remap_for_verify(dev, iaa_wq, req, &src_addr, &dst_addr);
		if (ret) {
			dev_dbg(dev, "%s: compress verify remap failed ret=%d\n", __func__, ret);
			goto out;
		}

		ret = iaa_compress_verify(tfm, req, wq, src_addr, req->slen,
					  dst_addr, &req->dlen, compression_crc);
		if (ret)
			dev_dbg(dev, "asynchronous compress verification failed ret=%d\n", ret);

		dma_unmap_sg(dev, req->dst, sg_nents(req->dst), DMA_TO_DEVICE);
		dma_unmap_sg(dev, req->src, sg_nents(req->src), DMA_FROM_DEVICE);

		goto out;
	}

	if (ret)
		dev_dbg(dev, "asynchronous compress failed ret=%d\n", ret);

	dma_unmap_sg(dev, req->dst, sg_nents(req->dst), DMA_FROM_DEVICE);
err_map_dst:
	dma_unmap_sg(dev, req->src, sg_nents(req->src), DMA_TO_DEVICE);
out:
	iaa_wq_put(wq);

	if (order >= 0)
		sgl_free_order(req->dst, order);

	return ret;
}

static int iaa_comp_adecompress_alloc_dest(struct acomp_req *req)
{
	gfp_t flags = req->flags & CRYPTO_TFM_REQ_MAY_SLEEP ?
		GFP_KERNEL : GFP_ATOMIC;
	struct crypto_tfm *tfm = req->base.tfm;
	dma_addr_t src_addr, dst_addr;
	int nr_sgs, cpu, ret = 0;
	struct iaa_wq *iaa_wq;
	struct device *dev;
	struct idxd_wq *wq;
	int order = -1;

	cpu = get_cpu();
	wq = wq_table_next_wq(cpu);
	put_cpu();
	if (!wq) {
		pr_debug("no wq configured for cpu=%d\n", cpu);
		return -ENODEV;
	}

	ret = iaa_wq_get(wq);
	if (ret) {
		pr_debug("no wq available for cpu=%d\n", cpu);
		return -ENODEV;
	}

	iaa_wq = idxd_wq_get_private(wq);

	dev = &wq->idxd->pdev->dev;

	nr_sgs = dma_map_sg(dev, req->src, sg_nents(req->src), DMA_TO_DEVICE);
	if (nr_sgs <= 0 || nr_sgs > 1) {
		dev_dbg(dev, "couldn't map src sg for iaa device %d,"
			" wq %d: ret=%d\n", iaa_wq->iaa_device->idxd->id,
			iaa_wq->wq->id, ret);
		ret = -EIO;
		goto out;
	}
	src_addr = sg_dma_address(req->src);
	dev_dbg(dev, "dma_map_sg, src_addr %llx, nr_sgs %d, req->src %p,"
		" req->slen %d, sg_dma_len(sg) %d\n", src_addr, nr_sgs,
		req->src, req->slen, sg_dma_len(req->src));

	req->dlen = 4 * req->slen; /* start with ~avg comp rato */
alloc_dest:
	order = order_base_2(round_up(req->dlen, PAGE_SIZE) / PAGE_SIZE);
	req->dst = sgl_alloc_order(req->dlen, order, false, flags, NULL);
	if (!req->dst) {
		ret = -ENOMEM;
		order = -1;
		goto out;
	}

	nr_sgs = dma_map_sg(dev, req->dst, sg_nents(req->dst), DMA_FROM_DEVICE);
	if (nr_sgs <= 0 || nr_sgs > 1) {
		dev_dbg(dev, "couldn't map dst sg for iaa device %d,"
			" wq %d: ret=%d\n", iaa_wq->iaa_device->idxd->id,
			iaa_wq->wq->id, ret);
		ret = -EIO;
		goto err_map_dst;
	}

	dst_addr = sg_dma_address(req->dst);
	dev_dbg(dev, "dma_map_sg, dst_addr %llx, nr_sgs %d, req->dst %p,"
		" req->dlen %d, sg_dma_len(sg) %d\n", dst_addr, nr_sgs,
		req->dst, req->dlen, sg_dma_len(req->dst));
	ret = iaa_decompress(tfm, req, wq, src_addr, req->slen,
			     dst_addr, &req->dlen, true);
	if (ret == -EOVERFLOW) {
		dma_unmap_sg(dev, req->dst, sg_nents(req->dst), DMA_FROM_DEVICE);
		req->dlen *= 2;
		if (req->dlen > CRYPTO_ACOMP_DST_MAX)
			goto err_map_dst;
		goto alloc_dest;
	}

	if (ret != 0)
		dev_dbg(dev, "asynchronous decompress failed ret=%d\n", ret);

	dma_unmap_sg(dev, req->dst, sg_nents(req->dst), DMA_FROM_DEVICE);
err_map_dst:
	dma_unmap_sg(dev, req->src, sg_nents(req->src), DMA_TO_DEVICE);
out:
	iaa_wq_put(wq);

	if (order >= 0)
		sgl_free_order(req->dst, order);

	return ret;
}

static int iaa_comp_adecompress(struct acomp_req *req)
{
	struct crypto_tfm *tfm = req->base.tfm;
	dma_addr_t src_addr, dst_addr;
	int nr_sgs, cpu, ret = 0;
	struct iaa_wq *iaa_wq;
	struct device *dev;
	struct idxd_wq *wq;

	if (!iaa_crypto_enabled) {
		pr_debug("iaa_crypto disabled, not decompressing\n");
		return -ENODEV;
	}

	if (!req->src || !req->slen) {
		pr_debug("invalid src, not decompressing\n");
		return -EINVAL;
	}

	if (!req->dst)
		return iaa_comp_adecompress_alloc_dest(req);

	cpu = get_cpu();
	wq = wq_table_next_wq(cpu);
	put_cpu();
	if (!wq) {
		pr_debug("no wq configured for cpu=%d\n", cpu);
		return -ENODEV;
	}

	ret = iaa_wq_get(wq);
	if (ret) {
		pr_debug("no wq available for cpu=%d\n", cpu);
		return -ENODEV;
	}

	iaa_wq = idxd_wq_get_private(wq);

	dev = &wq->idxd->pdev->dev;

	nr_sgs = dma_map_sg(dev, req->src, sg_nents(req->src), DMA_TO_DEVICE);
	if (nr_sgs <= 0 || nr_sgs > 1) {
		dev_dbg(dev, "couldn't map src sg for iaa device %d,"
			" wq %d: ret=%d\n", iaa_wq->iaa_device->idxd->id,
			iaa_wq->wq->id, ret);
		ret = -EIO;
		goto out;
	}
	src_addr = sg_dma_address(req->src);
	dev_dbg(dev, "dma_map_sg, src_addr %llx, nr_sgs %d, req->src %p,"
		" req->slen %d, sg_dma_len(sg) %d\n", src_addr, nr_sgs,
		req->src, req->slen, sg_dma_len(req->src));

	nr_sgs = dma_map_sg(dev, req->dst, sg_nents(req->dst), DMA_FROM_DEVICE);
	if (nr_sgs <= 0 || nr_sgs > 1) {
		dev_dbg(dev, "couldn't map dst sg for iaa device %d,"
			" wq %d: ret=%d\n", iaa_wq->iaa_device->idxd->id,
			iaa_wq->wq->id, ret);
		ret = -EIO;
		goto err_map_dst;
	}
	dst_addr = sg_dma_address(req->dst);
	dev_dbg(dev, "dma_map_sg, dst_addr %llx, nr_sgs %d, req->dst %p,"
		" req->dlen %d, sg_dma_len(sg) %d\n", dst_addr, nr_sgs,
		req->dst, req->dlen, sg_dma_len(req->dst));

	ret = iaa_decompress(tfm, req, wq, src_addr, req->slen,
			     dst_addr, &req->dlen, false);
	if (ret == -EINPROGRESS)
		return ret;

	if (ret != 0)
		dev_dbg(dev, "asynchronous decompress failed ret=%d\n", ret);

	dma_unmap_sg(dev, req->dst, sg_nents(req->dst), DMA_FROM_DEVICE);
err_map_dst:
	dma_unmap_sg(dev, req->src, sg_nents(req->src), DMA_TO_DEVICE);
out:
	iaa_wq_put(wq);

	return ret;
}

static void compression_ctx_init(struct iaa_compression_ctx *ctx)
{
	ctx->verify_compress = iaa_verify_compress;
	ctx->async_mode = async_mode;
	ctx->use_irq = use_irq;
}

static int iaa_comp_init_fixed(struct crypto_acomp *acomp_tfm)
{
	struct crypto_tfm *tfm = crypto_acomp_tfm(acomp_tfm);
	struct iaa_compression_ctx *ctx = crypto_tfm_ctx(tfm);

	compression_ctx_init(ctx);

	ctx->mode = IAA_MODE_FIXED;

	return 0;
}

static void dst_free(struct scatterlist *sgl)
{
	/*
	 * Called for req->dst = NULL cases but we free elsewhere
	 * using sgl_free_order().
	 */
}

static struct acomp_alg iaa_acomp_fixed_deflate = {
	.init			= iaa_comp_init_fixed,
	.compress		= iaa_comp_acompress,
	.decompress		= iaa_comp_adecompress,
	.dst_free               = dst_free,
	.base			= {
		.cra_name		= "deflate",
		.cra_driver_name	= "deflate-iaa",
		.cra_flags		= CRYPTO_ALG_ASYNC,
		.cra_ctxsize		= sizeof(struct iaa_compression_ctx),
		.cra_module		= THIS_MODULE,
		.cra_priority		= IAA_ALG_PRIORITY,
	}
};

static int iaa_register_compression_device(void)
{
	int ret;

	ret = crypto_register_acomp(&iaa_acomp_fixed_deflate);
	if (ret) {
		pr_err("deflate algorithm acomp fixed registration failed (%d)\n", ret);
		goto out;
	}

	iaa_crypto_registered = true;
out:
	return ret;
}

static int iaa_unregister_compression_device(void)
{
	if (iaa_crypto_registered)
		crypto_unregister_acomp(&iaa_acomp_fixed_deflate);

	return 0;
}

static int iaa_crypto_probe(struct idxd_dev *idxd_dev)
{
	struct idxd_wq *wq = idxd_dev_to_wq(idxd_dev);
	struct idxd_device *idxd = wq->idxd;
	struct idxd_driver_data *data = idxd->data;
	struct device *dev = &idxd_dev->conf_dev;
	bool first_wq = false;
	int ret = 0;

	if (idxd->state != IDXD_DEV_ENABLED)
		return -ENXIO;

	if (data->type != IDXD_TYPE_IAX)
		return -ENODEV;

	mutex_lock(&wq->wq_lock);

	if (idxd_wq_get_private(wq)) {
		mutex_unlock(&wq->wq_lock);
		return -EBUSY;
	}

	if (!idxd_wq_driver_name_match(wq, dev)) {
		dev_dbg(dev, "wq %d.%d driver_name match failed: wq driver_name %s, dev driver name %s\n",
			idxd->id, wq->id, wq->driver_name, dev->driver->name);
		idxd->cmd_status = IDXD_SCMD_WQ_NO_DRV_NAME;
		ret = -ENODEV;
		goto err;
	}

	wq->type = IDXD_WQT_KERNEL;

	ret = idxd_drv_enable_wq(wq);
	if (ret < 0) {
		dev_dbg(dev, "enable wq %d.%d failed: %d\n",
			idxd->id, wq->id, ret);
		ret = -ENXIO;
		goto err;
	}

	mutex_lock(&iaa_devices_lock);

	if (list_empty(&iaa_devices)) {
		ret = alloc_wq_table(wq->idxd->max_wqs);
		if (ret)
			goto err_alloc;
		first_wq = true;
	}

	ret = save_iaa_wq(wq);
	if (ret)
		goto err_save;

	rebalance_wq_table();

	if (first_wq) {
		iaa_crypto_enabled = true;
		ret = iaa_register_compression_device();
		if (ret != 0) {
			iaa_crypto_enabled = false;
			dev_dbg(dev, "IAA compression device registration failed\n");
			goto err_register;
		}
		try_module_get(THIS_MODULE);

		pr_info("iaa_crypto now ENABLED\n");
	}

	mutex_unlock(&iaa_devices_lock);
out:
	mutex_unlock(&wq->wq_lock);

	return ret;

err_register:
	remove_iaa_wq(wq);
	free_iaa_wq(idxd_wq_get_private(wq));
err_save:
	if (first_wq)
		free_wq_table();
err_alloc:
	mutex_unlock(&iaa_devices_lock);
	idxd_drv_disable_wq(wq);
err:
	wq->type = IDXD_WQT_NONE;

	goto out;
}

static void iaa_crypto_remove(struct idxd_dev *idxd_dev)
{
	struct idxd_wq *wq = idxd_dev_to_wq(idxd_dev);
	struct idxd_device *idxd = wq->idxd;
	struct iaa_wq *iaa_wq;
	bool free = false;

	idxd_wq_quiesce(wq);

	mutex_lock(&wq->wq_lock);
	mutex_lock(&iaa_devices_lock);

	remove_iaa_wq(wq);

	spin_lock(&idxd->dev_lock);
	iaa_wq = idxd_wq_get_private(wq);
	if (!iaa_wq) {
		spin_unlock(&idxd->dev_lock);
		pr_err("%s: no iaa_wq available to remove\n", __func__);
		goto out;
	}

	if (iaa_wq->ref) {
		iaa_wq->remove = true;
	} else {
		wq = iaa_wq->wq;
		idxd_wq_set_private(wq, NULL);
		free = true;
	}
	spin_unlock(&idxd->dev_lock);
	if (free) {
		__free_iaa_wq(iaa_wq);
		kfree(iaa_wq);
	}

	idxd_drv_disable_wq(wq);
	rebalance_wq_table();

	if (nr_iaa == 0) {
		iaa_crypto_enabled = false;
		free_wq_table();
		module_put(THIS_MODULE);

		pr_info("iaa_crypto now DISABLED\n");
	}
out:
	mutex_unlock(&iaa_devices_lock);
	mutex_unlock(&wq->wq_lock);
}

static enum idxd_dev_type dev_types[] = {
	IDXD_DEV_WQ,
	IDXD_DEV_NONE,
};

static struct idxd_device_driver iaa_crypto_driver = {
	.probe = iaa_crypto_probe,
	.remove = iaa_crypto_remove,
	.name = IDXD_SUBDRIVER_NAME,
	.type = dev_types,
	.desc_complete = iaa_desc_complete,
};

static int __init iaa_crypto_init_module(void)
{
	int ret = 0;
	int node;

	nr_cpus = num_possible_cpus();
	for_each_node_with_cpus(node)
		nr_nodes++;
	if (!nr_nodes) {
		pr_err("IAA couldn't find any nodes with cpus\n");
		return -ENODEV;
	}
	nr_cpus_per_node = nr_cpus / nr_nodes;

	if (crypto_has_comp("deflate-generic", 0, 0))
		deflate_generic_tfm = crypto_alloc_comp("deflate-generic", 0, 0);

	if (IS_ERR_OR_NULL(deflate_generic_tfm)) {
		pr_err("IAA could not alloc %s tfm: errcode = %ld\n",
		       "deflate-generic", PTR_ERR(deflate_generic_tfm));
		return -ENOMEM;
	}

	ret = iaa_aecs_init_fixed();
	if (ret < 0) {
		pr_debug("IAA fixed compression mode init failed\n");
		goto err_aecs_init;
	}

	ret = idxd_driver_register(&iaa_crypto_driver);
	if (ret) {
		pr_debug("IAA wq sub-driver registration failed\n");
		goto err_driver_reg;
	}

	ret = driver_create_file(&iaa_crypto_driver.drv,
				 &driver_attr_verify_compress);
	if (ret) {
		pr_debug("IAA verify_compress attr creation failed\n");
		goto err_verify_attr_create;
	}

	ret = driver_create_file(&iaa_crypto_driver.drv,
				 &driver_attr_sync_mode);
	if (ret) {
		pr_debug("IAA sync mode attr creation failed\n");
		goto err_sync_attr_create;
	}

	if (iaa_crypto_debugfs_init())
		pr_warn("debugfs init failed, stats not available\n");

	pr_debug("initialized\n");
out:
	return ret;

err_sync_attr_create:
	driver_remove_file(&iaa_crypto_driver.drv,
			   &driver_attr_verify_compress);
err_verify_attr_create:
	idxd_driver_unregister(&iaa_crypto_driver);
err_driver_reg:
	iaa_aecs_cleanup_fixed();
err_aecs_init:
	crypto_free_comp(deflate_generic_tfm);

	goto out;
}

static void __exit iaa_crypto_cleanup_module(void)
{
	if (iaa_unregister_compression_device())
		pr_debug("IAA compression device unregister failed\n");

	iaa_crypto_debugfs_cleanup();
	driver_remove_file(&iaa_crypto_driver.drv,
			   &driver_attr_sync_mode);
	driver_remove_file(&iaa_crypto_driver.drv,
			   &driver_attr_verify_compress);
	idxd_driver_unregister(&iaa_crypto_driver);
	iaa_aecs_cleanup_fixed();
	crypto_free_comp(deflate_generic_tfm);

	pr_debug("cleaned up\n");
}

MODULE_IMPORT_NS(IDXD);
MODULE_LICENSE("GPL");
MODULE_ALIAS_IDXD_DEVICE(0);
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("IAA Compression Accelerator Crypto Driver");

module_init(iaa_crypto_init_module);
module_exit(iaa_crypto_cleanup_module);
