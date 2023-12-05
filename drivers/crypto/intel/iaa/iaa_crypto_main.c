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

#include "idxd.h"
#include "iaa_crypto.h"

#ifdef pr_fmt
#undef pr_fmt
#endif

#define pr_fmt(fmt)			"idxd: " IDXD_SUBDRIVER_NAME ": " fmt

/* number of iaa instances probed */
static unsigned int nr_iaa;
static unsigned int nr_cpus;
static unsigned int nr_nodes;
static unsigned int nr_cpus_per_node;

/* Number of physical cpus sharing each iaa instance */
static unsigned int cpus_per_iaa;

/* Per-cpu lookup table for balanced wqs */
static struct wq_table_entry __percpu *wq_table;

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

static LIST_HEAD(iaa_devices);
static DEFINE_MUTEX(iaa_devices_lock);

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
	kfree(mode->header_table);

	kfree(mode);
}

/*
 * IAA Compression modes are defined by an ll_table, a d_table, and an
 * optional header_table.  These tables are typically generated and
 * captured using statistics collected from running actual
 * compress/decompress workloads.
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
 * @header_table: Optional header table
 * @header_table_size: Optional header table size in bytes
 * @gen_decomp_table_flags: Otional flags used to generate the decomp table
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
			     const u8 *header_table,
			     int header_table_size,
			     u16 gen_decomp_table_flags,
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
		mode->ll_table = kzalloc(ll_table_size, GFP_KERNEL);
		if (!mode->ll_table)
			goto free;
		memcpy(mode->ll_table, ll_table, ll_table_size);
		mode->ll_table_size = ll_table_size;
	}

	if (d_table) {
		mode->d_table = kzalloc(d_table_size, GFP_KERNEL);
		if (!mode->d_table)
			goto free;
		memcpy(mode->d_table, d_table, d_table_size);
		mode->d_table_size = d_table_size;
	}

	if (header_table) {
		mode->header_table = kzalloc(header_table_size, GFP_KERNEL);
		if (!mode->header_table)
			goto free;
		memcpy(mode->header_table, header_table, header_table_size);
		mode->header_table_size = header_table_size;
	}

	mode->gen_decomp_table_flags = gen_decomp_table_flags;

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

static void free_device_compression_mode(struct iaa_device *iaa_device,
					 struct iaa_device_compression_mode *device_mode)
{
	size_t size = sizeof(struct aecs_comp_table_record) + IAA_AECS_ALIGN;
	struct device *dev = &iaa_device->idxd->pdev->dev;

	kfree(device_mode->name);

	if (device_mode->aecs_comp_table)
		dma_free_coherent(dev, size, device_mode->aecs_comp_table,
				  device_mode->aecs_comp_table_dma_addr);
	if (device_mode->aecs_decomp_table)
		dma_free_coherent(dev, size, device_mode->aecs_decomp_table,
				  device_mode->aecs_decomp_table_dma_addr);

	kfree(device_mode);
}

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

	device_mode->aecs_decomp_table = dma_alloc_coherent(dev, size,
							    &device_mode->aecs_decomp_table_dma_addr, GFP_KERNEL);
	if (!device_mode->aecs_decomp_table)
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

		free_device_compression_mode(iaa_device, device_mode);
		iaa_device->compression_modes[i] = NULL;
		if (iaa_compression_modes[i]->free)
			iaa_compression_modes[i]->free(device_mode);
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

static void iaa_device_free(struct iaa_device *iaa_device)
{
	struct iaa_wq *iaa_wq, *next;

	list_for_each_entry_safe(iaa_wq, next, &iaa_device->wqs, list) {
		list_del(&iaa_wq->list);
		kfree(iaa_wq);
	}

	kfree(iaa_device);
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
	remove_device_compression_modes(iaa_device);

	list_del(&iaa_device->list);

	iaa_device_free(iaa_device);

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
			goto out;
		}

		ret = init_iaa_device(new_device, new_wq);
		if (ret) {
			del_iaa_wq(new_device, new_wq->wq);
			del_iaa_device(new_device);
			goto out;
		}
	}

	if (WARN_ON(nr_iaa == 0))
		return -EINVAL;

	cpus_per_iaa = (nr_nodes * nr_cpus_per_node) / nr_iaa;
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

	if (nr_iaa)
		cpus_per_iaa = (nr_nodes * nr_cpus_per_node) / nr_iaa;
	else
		cpus_per_iaa = 0;
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
	};

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

	for_each_online_node(node) {
		node_cpus = cpumask_of_node(node);

		for (cpu = 0; cpu < nr_cpus_per_node; cpu++) {
			int node_cpu = cpumask_nth(cpu, node_cpus);

			if ((cpu % cpus_per_iaa) == 0)
				iaa++;

			if (WARN_ON(wq_table_add_wqs(iaa, node_cpu))) {
				pr_debug("could not add any wqs for iaa %d to cpu %d!\n", iaa, cpu);
				return;
			}
		}
	}
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

	mutex_unlock(&iaa_devices_lock);
out:
	mutex_unlock(&wq->wq_lock);

	return ret;

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

	idxd_wq_quiesce(wq);

	mutex_lock(&wq->wq_lock);
	mutex_lock(&iaa_devices_lock);

	remove_iaa_wq(wq);

	idxd_drv_disable_wq(wq);
	rebalance_wq_table();

	if (nr_iaa == 0)
		free_wq_table();

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
};

static int __init iaa_crypto_init_module(void)
{
	int ret = 0;

	nr_cpus = num_online_cpus();
	nr_nodes = num_online_nodes();
	nr_cpus_per_node = nr_cpus / nr_nodes;

	ret = iaa_aecs_init_fixed();
	if (ret < 0) {
		pr_debug("IAA fixed compression mode init failed\n");
		goto out;
	}

	ret = idxd_driver_register(&iaa_crypto_driver);
	if (ret) {
		pr_debug("IAA wq sub-driver registration failed\n");
		goto err_driver_reg;
	}

	pr_debug("initialized\n");
out:
	return ret;

err_driver_reg:
	iaa_aecs_cleanup_fixed();

	goto out;
}

static void __exit iaa_crypto_cleanup_module(void)
{
	idxd_driver_unregister(&iaa_crypto_driver);
	iaa_aecs_cleanup_fixed();

	pr_debug("cleaned up\n");
}

MODULE_IMPORT_NS(IDXD);
MODULE_LICENSE("GPL");
MODULE_ALIAS_IDXD_DEVICE(0);
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("IAA Compression Accelerator Crypto Driver");

module_init(iaa_crypto_init_module);
module_exit(iaa_crypto_cleanup_module);
