// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Peer 2 Peer DMA support.
 *
 * Copyright (c) 2016-2018, Logan Gunthorpe
 * Copyright (c) 2016-2017, Microsemi Corporation
 * Copyright (c) 2017, Christoph Hellwig
 * Copyright (c) 2018, Eideticom Inc.
 */

#define pr_fmt(fmt) "pci-p2pdma: " fmt
#include <linux/ctype.h>
#include <linux/pci-p2pdma.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/genalloc.h>
#include <linux/memremap.h>
#include <linux/percpu-refcount.h>
#include <linux/random.h>
#include <linux/seq_buf.h>
#include <linux/xarray.h>

enum pci_p2pdma_map_type {
	PCI_P2PDMA_MAP_UNKNOWN = 0,
	PCI_P2PDMA_MAP_NOT_SUPPORTED,
	PCI_P2PDMA_MAP_BUS_ADDR,
	PCI_P2PDMA_MAP_THRU_HOST_BRIDGE,
};

struct pci_p2pdma {
	struct gen_pool *pool;
	bool p2pmem_published;
	struct xarray map_types;
};

struct pci_p2pdma_pagemap {
	struct dev_pagemap pgmap;
	struct pci_dev *provider;
	u64 bus_offset;
};

static struct pci_p2pdma_pagemap *to_p2p_pgmap(struct dev_pagemap *pgmap)
{
	return container_of(pgmap, struct pci_p2pdma_pagemap, pgmap);
}

static ssize_t size_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	size_t size = 0;

	if (pdev->p2pdma->pool)
		size = gen_pool_size(pdev->p2pdma->pool);

	return scnprintf(buf, PAGE_SIZE, "%zd\n", size);
}
static DEVICE_ATTR_RO(size);

static ssize_t available_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	size_t avail = 0;

	if (pdev->p2pdma->pool)
		avail = gen_pool_avail(pdev->p2pdma->pool);

	return scnprintf(buf, PAGE_SIZE, "%zd\n", avail);
}
static DEVICE_ATTR_RO(available);

static ssize_t published_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n",
			 pdev->p2pdma->p2pmem_published);
}
static DEVICE_ATTR_RO(published);

static struct attribute *p2pmem_attrs[] = {
	&dev_attr_size.attr,
	&dev_attr_available.attr,
	&dev_attr_published.attr,
	NULL,
};

static const struct attribute_group p2pmem_group = {
	.attrs = p2pmem_attrs,
	.name = "p2pmem",
};

static void pci_p2pdma_release(void *data)
{
	struct pci_dev *pdev = data;
	struct pci_p2pdma *p2pdma = pdev->p2pdma;

	if (!p2pdma)
		return;

	/* Flush and disable pci_alloc_p2p_mem() */
	pdev->p2pdma = NULL;
	synchronize_rcu();

	gen_pool_destroy(p2pdma->pool);
	sysfs_remove_group(&pdev->dev.kobj, &p2pmem_group);
	xa_destroy(&p2pdma->map_types);
}

static int pci_p2pdma_setup(struct pci_dev *pdev)
{
	int error = -ENOMEM;
	struct pci_p2pdma *p2p;

	p2p = devm_kzalloc(&pdev->dev, sizeof(*p2p), GFP_KERNEL);
	if (!p2p)
		return -ENOMEM;

	xa_init(&p2p->map_types);

	p2p->pool = gen_pool_create(PAGE_SHIFT, dev_to_node(&pdev->dev));
	if (!p2p->pool)
		goto out;

	error = devm_add_action_or_reset(&pdev->dev, pci_p2pdma_release, pdev);
	if (error)
		goto out_pool_destroy;

	pdev->p2pdma = p2p;

	error = sysfs_create_group(&pdev->dev.kobj, &p2pmem_group);
	if (error)
		goto out_pool_destroy;

	return 0;

out_pool_destroy:
	pdev->p2pdma = NULL;
	gen_pool_destroy(p2p->pool);
out:
	devm_kfree(&pdev->dev, p2p);
	return error;
}

/**
 * pci_p2pdma_add_resource - add memory for use as p2p memory
 * @pdev: the device to add the memory to
 * @bar: PCI BAR to add
 * @size: size of the memory to add, may be zero to use the whole BAR
 * @offset: offset into the PCI BAR
 *
 * The memory will be given ZONE_DEVICE struct pages so that it may
 * be used with any DMA request.
 */
int pci_p2pdma_add_resource(struct pci_dev *pdev, int bar, size_t size,
			    u64 offset)
{
	struct pci_p2pdma_pagemap *p2p_pgmap;
	struct dev_pagemap *pgmap;
	void *addr;
	int error;

	if (!(pci_resource_flags(pdev, bar) & IORESOURCE_MEM))
		return -EINVAL;

	if (offset >= pci_resource_len(pdev, bar))
		return -EINVAL;

	if (!size)
		size = pci_resource_len(pdev, bar) - offset;

	if (size + offset > pci_resource_len(pdev, bar))
		return -EINVAL;

	if (!pdev->p2pdma) {
		error = pci_p2pdma_setup(pdev);
		if (error)
			return error;
	}

	p2p_pgmap = devm_kzalloc(&pdev->dev, sizeof(*p2p_pgmap), GFP_KERNEL);
	if (!p2p_pgmap)
		return -ENOMEM;

	pgmap = &p2p_pgmap->pgmap;
	pgmap->range.start = pci_resource_start(pdev, bar) + offset;
	pgmap->range.end = pgmap->range.start + size - 1;
	pgmap->nr_range = 1;
	pgmap->type = MEMORY_DEVICE_PCI_P2PDMA;

	p2p_pgmap->provider = pdev;
	p2p_pgmap->bus_offset = pci_bus_address(pdev, bar) -
		pci_resource_start(pdev, bar);

	addr = devm_memremap_pages(&pdev->dev, pgmap);
	if (IS_ERR(addr)) {
		error = PTR_ERR(addr);
		goto pgmap_free;
	}

	error = gen_pool_add_owner(pdev->p2pdma->pool, (unsigned long)addr,
			pci_bus_address(pdev, bar) + offset,
			range_len(&pgmap->range), dev_to_node(&pdev->dev),
			pgmap->ref);
	if (error)
		goto pages_free;

	pci_info(pdev, "added peer-to-peer DMA memory %#llx-%#llx\n",
		 pgmap->range.start, pgmap->range.end);

	return 0;

pages_free:
	devm_memunmap_pages(&pdev->dev, pgmap);
pgmap_free:
	devm_kfree(&pdev->dev, pgmap);
	return error;
}
EXPORT_SYMBOL_GPL(pci_p2pdma_add_resource);

/*
 * Note this function returns the parent PCI device with a
 * reference taken. It is the caller's responsibility to drop
 * the reference.
 */
static struct pci_dev *find_parent_pci_dev(struct device *dev)
{
	struct device *parent;

	dev = get_device(dev);

	while (dev) {
		if (dev_is_pci(dev))
			return to_pci_dev(dev);

		parent = get_device(dev->parent);
		put_device(dev);
		dev = parent;
	}

	return NULL;
}

/*
 * Check if a PCI bridge has its ACS redirection bits set to redirect P2P
 * TLPs upstream via ACS. Returns 1 if the packets will be redirected
 * upstream, 0 otherwise.
 */
static int pci_bridge_has_acs_redir(struct pci_dev *pdev)
{
	int pos;
	u16 ctrl;

	pos = pdev->acs_cap;
	if (!pos)
		return 0;

	pci_read_config_word(pdev, pos + PCI_ACS_CTRL, &ctrl);

	if (ctrl & (PCI_ACS_RR | PCI_ACS_CR | PCI_ACS_EC))
		return 1;

	return 0;
}

static void seq_buf_print_bus_devfn(struct seq_buf *buf, struct pci_dev *pdev)
{
	if (!buf)
		return;

	seq_buf_printf(buf, "%s;", pci_name(pdev));
}

static bool cpu_supports_p2pdma(void)
{
#ifdef CONFIG_X86
	struct cpuinfo_x86 *c = &cpu_data(0);

	/* Any AMD CPU whose family ID is Zen or newer supports p2pdma */
	if (c->x86_vendor == X86_VENDOR_AMD && c->x86 >= 0x17)
		return true;
#endif

	return false;
}

static const struct pci_p2pdma_whitelist_entry {
	unsigned short vendor;
	unsigned short device;
	enum {
		REQ_SAME_HOST_BRIDGE	= 1 << 0,
	} flags;
} pci_p2pdma_whitelist[] = {
	/* Intel Xeon E5/Core i7 */
	{PCI_VENDOR_ID_INTEL,	0x3c00, REQ_SAME_HOST_BRIDGE},
	{PCI_VENDOR_ID_INTEL,	0x3c01, REQ_SAME_HOST_BRIDGE},
	/* Intel Xeon E7 v3/Xeon E5 v3/Core i7 */
	{PCI_VENDOR_ID_INTEL,	0x2f00, REQ_SAME_HOST_BRIDGE},
	{PCI_VENDOR_ID_INTEL,	0x2f01, REQ_SAME_HOST_BRIDGE},
	/* Intel SkyLake-E */
	{PCI_VENDOR_ID_INTEL,	0x2030, 0},
	{PCI_VENDOR_ID_INTEL,	0x2031, 0},
	{PCI_VENDOR_ID_INTEL,	0x2032, 0},
	{PCI_VENDOR_ID_INTEL,	0x2033, 0},
	{PCI_VENDOR_ID_INTEL,	0x2020, 0},
	{}
};

static bool __host_bridge_whitelist(struct pci_host_bridge *host,
				    bool same_host_bridge)
{
	struct pci_dev *root = pci_get_slot(host->bus, PCI_DEVFN(0, 0));
	const struct pci_p2pdma_whitelist_entry *entry;
	unsigned short vendor, device;

	if (!root)
		return false;

	vendor = root->vendor;
	device = root->device;
	pci_dev_put(root);

	for (entry = pci_p2pdma_whitelist; entry->vendor; entry++) {
		if (vendor != entry->vendor || device != entry->device)
			continue;
		if (entry->flags & REQ_SAME_HOST_BRIDGE && !same_host_bridge)
			return false;

		return true;
	}

	return false;
}

/*
 * If we can't find a common upstream bridge take a look at the root
 * complex and compare it to a whitelist of known good hardware.
 */
static bool host_bridge_whitelist(struct pci_dev *a, struct pci_dev *b)
{
	struct pci_host_bridge *host_a = pci_find_host_bridge(a->bus);
	struct pci_host_bridge *host_b = pci_find_host_bridge(b->bus);

	if (host_a == host_b)
		return __host_bridge_whitelist(host_a, true);

	if (__host_bridge_whitelist(host_a, false) &&
	    __host_bridge_whitelist(host_b, false))
		return true;

	return false;
}

static enum pci_p2pdma_map_type
__upstream_bridge_distance(struct pci_dev *provider, struct pci_dev *client,
		int *dist, bool *acs_redirects, struct seq_buf *acs_list)
{
	struct pci_dev *a = provider, *b = client, *bb;
	int dist_a = 0;
	int dist_b = 0;
	int acs_cnt = 0;

	if (acs_redirects)
		*acs_redirects = false;

	/*
	 * Note, we don't need to take references to devices returned by
	 * pci_upstream_bridge() seeing we hold a reference to a child
	 * device which will already hold a reference to the upstream bridge.
	 */

	while (a) {
		dist_b = 0;

		if (pci_bridge_has_acs_redir(a)) {
			seq_buf_print_bus_devfn(acs_list, a);
			acs_cnt++;
		}

		bb = b;

		while (bb) {
			if (a == bb)
				goto check_b_path_acs;

			bb = pci_upstream_bridge(bb);
			dist_b++;
		}

		a = pci_upstream_bridge(a);
		dist_a++;
	}

	if (dist)
		*dist = dist_a + dist_b;

	return PCI_P2PDMA_MAP_THRU_HOST_BRIDGE;

check_b_path_acs:
	bb = b;

	while (bb) {
		if (a == bb)
			break;

		if (pci_bridge_has_acs_redir(bb)) {
			seq_buf_print_bus_devfn(acs_list, bb);
			acs_cnt++;
		}

		bb = pci_upstream_bridge(bb);
	}

	if (dist)
		*dist = dist_a + dist_b;

	if (acs_cnt) {
		if (acs_redirects)
			*acs_redirects = true;

		return PCI_P2PDMA_MAP_THRU_HOST_BRIDGE;
	}

	return PCI_P2PDMA_MAP_BUS_ADDR;
}

static unsigned long map_types_idx(struct pci_dev *client)
{
	return (pci_domain_nr(client->bus) << 16) |
		(client->bus->number << 8) | client->devfn;
}

/*
 * Find the distance through the nearest common upstream bridge between
 * two PCI devices.
 *
 * If the two devices are the same device then 0 will be returned.
 *
 * If there are two virtual functions of the same device behind the same
 * bridge port then 2 will be returned (one step down to the PCIe switch,
 * then one step back to the same device).
 *
 * In the case where two devices are connected to the same PCIe switch, the
 * value 4 will be returned. This corresponds to the following PCI tree:
 *
 *     -+  Root Port
 *      \+ Switch Upstream Port
 *       +-+ Switch Downstream Port
 *       + \- Device A
 *       \-+ Switch Downstream Port
 *         \- Device B
 *
 * The distance is 4 because we traverse from Device A through the downstream
 * port of the switch, to the common upstream port, back up to the second
 * downstream port and then to Device B.
 *
 * Any two devices that cannot communicate using p2pdma will return
 * PCI_P2PDMA_MAP_NOT_SUPPORTED.
 *
 * Any two devices that have a data path that goes through the host bridge
 * will consult a whitelist. If the host bridges are on the whitelist,
 * this function will return PCI_P2PDMA_MAP_THRU_HOST_BRIDGE.
 *
 * If either bridge is not on the whitelist this function returns
 * PCI_P2PDMA_MAP_NOT_SUPPORTED.
 *
 * If a bridge which has any ACS redirection bits set is in the path,
 * acs_redirects will be set to true. In this case, a list of all infringing
 * bridge addresses will be populated in acs_list (assuming it's non-null)
 * for printk purposes.
 */
static enum pci_p2pdma_map_type
upstream_bridge_distance(struct pci_dev *provider, struct pci_dev *client,
		int *dist, bool *acs_redirects, struct seq_buf *acs_list)
{
	enum pci_p2pdma_map_type map_type;

	map_type = __upstream_bridge_distance(provider, client, dist,
					      acs_redirects, acs_list);

	if (map_type == PCI_P2PDMA_MAP_THRU_HOST_BRIDGE) {
		if (!cpu_supports_p2pdma() &&
		    !host_bridge_whitelist(provider, client))
			map_type = PCI_P2PDMA_MAP_NOT_SUPPORTED;
	}

	if (provider->p2pdma)
		xa_store(&provider->p2pdma->map_types, map_types_idx(client),
			 xa_mk_value(map_type), GFP_KERNEL);

	return map_type;
}

static enum pci_p2pdma_map_type
upstream_bridge_distance_warn(struct pci_dev *provider, struct pci_dev *client,
			      int *dist)
{
	struct seq_buf acs_list;
	bool acs_redirects;
	int ret;

	seq_buf_init(&acs_list, kmalloc(PAGE_SIZE, GFP_KERNEL), PAGE_SIZE);
	if (!acs_list.buffer)
		return -ENOMEM;

	ret = upstream_bridge_distance(provider, client, dist, &acs_redirects,
				       &acs_list);
	if (acs_redirects) {
		pci_warn(client, "ACS redirect is set between the client and provider (%s)\n",
			 pci_name(provider));
		/* Drop final semicolon */
		acs_list.buffer[acs_list.len-1] = 0;
		pci_warn(client, "to disable ACS redirect for this path, add the kernel parameter: pci=disable_acs_redir=%s\n",
			 acs_list.buffer);
	}

	if (ret == PCI_P2PDMA_MAP_NOT_SUPPORTED) {
		pci_warn(client, "cannot be used for peer-to-peer DMA as the client and provider (%s) do not share an upstream bridge or whitelisted host bridge\n",
			 pci_name(provider));
	}

	kfree(acs_list.buffer);

	return ret;
}

/**
 * pci_p2pdma_distance_many - Determine the cumulative distance between
 *	a p2pdma provider and the clients in use.
 * @provider: p2pdma provider to check against the client list
 * @clients: array of devices to check (NULL-terminated)
 * @num_clients: number of clients in the array
 * @verbose: if true, print warnings for devices when we return -1
 *
 * Returns -1 if any of the clients are not compatible, otherwise returns a
 * positive number where a lower number is the preferable choice. (If there's
 * one client that's the same as the provider it will return 0, which is best
 * choice).
 *
 * "compatible" means the provider and the clients are either all behind
 * the same PCI root port or the host bridges connected to each of the devices
 * are listed in the 'pci_p2pdma_whitelist'.
 */
int pci_p2pdma_distance_many(struct pci_dev *provider, struct device **clients,
			     int num_clients, bool verbose)
{
	bool not_supported = false;
	struct pci_dev *pci_client;
	int total_dist = 0;
	int distance;
	int i, ret;

	if (num_clients == 0)
		return -1;

	for (i = 0; i < num_clients; i++) {
		pci_client = find_parent_pci_dev(clients[i]);
		if (!pci_client) {
			if (verbose)
				dev_warn(clients[i],
					 "cannot be used for peer-to-peer DMA as it is not a PCI device\n");
			return -1;
		}

		if (verbose)
			ret = upstream_bridge_distance_warn(provider,
					pci_client, &distance);
		else
			ret = upstream_bridge_distance(provider, pci_client,
						       &distance, NULL, NULL);

		pci_dev_put(pci_client);

		if (ret == PCI_P2PDMA_MAP_NOT_SUPPORTED)
			not_supported = true;

		if (not_supported && !verbose)
			break;

		total_dist += distance;
	}

	if (not_supported)
		return -1;

	return total_dist;
}
EXPORT_SYMBOL_GPL(pci_p2pdma_distance_many);

/**
 * pci_has_p2pmem - check if a given PCI device has published any p2pmem
 * @pdev: PCI device to check
 */
bool pci_has_p2pmem(struct pci_dev *pdev)
{
	return pdev->p2pdma && pdev->p2pdma->p2pmem_published;
}
EXPORT_SYMBOL_GPL(pci_has_p2pmem);

/**
 * pci_p2pmem_find_many - find a peer-to-peer DMA memory device compatible with
 *	the specified list of clients and shortest distance (as determined
 *	by pci_p2pmem_dma())
 * @clients: array of devices to check (NULL-terminated)
 * @num_clients: number of client devices in the list
 *
 * If multiple devices are behind the same switch, the one "closest" to the
 * client devices in use will be chosen first. (So if one of the providers is
 * the same as one of the clients, that provider will be used ahead of any
 * other providers that are unrelated). If multiple providers are an equal
 * distance away, one will be chosen at random.
 *
 * Returns a pointer to the PCI device with a reference taken (use pci_dev_put
 * to return the reference) or NULL if no compatible device is found. The
 * found provider will also be assigned to the client list.
 */
struct pci_dev *pci_p2pmem_find_many(struct device **clients, int num_clients)
{
	struct pci_dev *pdev = NULL;
	int distance;
	int closest_distance = INT_MAX;
	struct pci_dev **closest_pdevs;
	int dev_cnt = 0;
	const int max_devs = PAGE_SIZE / sizeof(*closest_pdevs);
	int i;

	closest_pdevs = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!closest_pdevs)
		return NULL;

	while ((pdev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, pdev))) {
		if (!pci_has_p2pmem(pdev))
			continue;

		distance = pci_p2pdma_distance_many(pdev, clients,
						    num_clients, false);
		if (distance < 0 || distance > closest_distance)
			continue;

		if (distance == closest_distance && dev_cnt >= max_devs)
			continue;

		if (distance < closest_distance) {
			for (i = 0; i < dev_cnt; i++)
				pci_dev_put(closest_pdevs[i]);

			dev_cnt = 0;
			closest_distance = distance;
		}

		closest_pdevs[dev_cnt++] = pci_dev_get(pdev);
	}

	if (dev_cnt)
		pdev = pci_dev_get(closest_pdevs[prandom_u32_max(dev_cnt)]);

	for (i = 0; i < dev_cnt; i++)
		pci_dev_put(closest_pdevs[i]);

	kfree(closest_pdevs);
	return pdev;
}
EXPORT_SYMBOL_GPL(pci_p2pmem_find_many);

/**
 * pci_alloc_p2pmem - allocate peer-to-peer DMA memory
 * @pdev: the device to allocate memory from
 * @size: number of bytes to allocate
 *
 * Returns the allocated memory or NULL on error.
 */
void *pci_alloc_p2pmem(struct pci_dev *pdev, size_t size)
{
	void *ret = NULL;
	struct percpu_ref *ref;

	/*
	 * Pairs with synchronize_rcu() in pci_p2pdma_release() to
	 * ensure pdev->p2pdma is non-NULL for the duration of the
	 * read-lock.
	 */
	rcu_read_lock();
	if (unlikely(!pdev->p2pdma))
		goto out;

	ret = (void *)gen_pool_alloc_owner(pdev->p2pdma->pool, size,
			(void **) &ref);
	if (!ret)
		goto out;

	if (unlikely(!percpu_ref_tryget_live(ref))) {
		gen_pool_free(pdev->p2pdma->pool, (unsigned long) ret, size);
		ret = NULL;
		goto out;
	}
out:
	rcu_read_unlock();
	return ret;
}
EXPORT_SYMBOL_GPL(pci_alloc_p2pmem);

/**
 * pci_free_p2pmem - free peer-to-peer DMA memory
 * @pdev: the device the memory was allocated from
 * @addr: address of the memory that was allocated
 * @size: number of bytes that were allocated
 */
void pci_free_p2pmem(struct pci_dev *pdev, void *addr, size_t size)
{
	struct percpu_ref *ref;

	gen_pool_free_owner(pdev->p2pdma->pool, (uintptr_t)addr, size,
			(void **) &ref);
	percpu_ref_put(ref);
}
EXPORT_SYMBOL_GPL(pci_free_p2pmem);

/**
 * pci_p2pmem_virt_to_bus - return the PCI bus address for a given virtual
 *	address obtained with pci_alloc_p2pmem()
 * @pdev: the device the memory was allocated from
 * @addr: address of the memory that was allocated
 */
pci_bus_addr_t pci_p2pmem_virt_to_bus(struct pci_dev *pdev, void *addr)
{
	if (!addr)
		return 0;
	if (!pdev->p2pdma)
		return 0;

	/*
	 * Note: when we added the memory to the pool we used the PCI
	 * bus address as the physical address. So gen_pool_virt_to_phys()
	 * actually returns the bus address despite the misleading name.
	 */
	return gen_pool_virt_to_phys(pdev->p2pdma->pool, (unsigned long)addr);
}
EXPORT_SYMBOL_GPL(pci_p2pmem_virt_to_bus);

/**
 * pci_p2pmem_alloc_sgl - allocate peer-to-peer DMA memory in a scatterlist
 * @pdev: the device to allocate memory from
 * @nents: the number of SG entries in the list
 * @length: number of bytes to allocate
 *
 * Return: %NULL on error or &struct scatterlist pointer and @nents on success
 */
struct scatterlist *pci_p2pmem_alloc_sgl(struct pci_dev *pdev,
					 unsigned int *nents, u32 length)
{
	struct scatterlist *sg;
	void *addr;

	sg = kmalloc(sizeof(*sg), GFP_KERNEL);
	if (!sg)
		return NULL;

	sg_init_table(sg, 1);

	addr = pci_alloc_p2pmem(pdev, length);
	if (!addr)
		goto out_free_sg;

	sg_set_buf(sg, addr, length);
	*nents = 1;
	return sg;

out_free_sg:
	kfree(sg);
	return NULL;
}
EXPORT_SYMBOL_GPL(pci_p2pmem_alloc_sgl);

/**
 * pci_p2pmem_free_sgl - free a scatterlist allocated by pci_p2pmem_alloc_sgl()
 * @pdev: the device to allocate memory from
 * @sgl: the allocated scatterlist
 */
void pci_p2pmem_free_sgl(struct pci_dev *pdev, struct scatterlist *sgl)
{
	struct scatterlist *sg;
	int count;

	for_each_sg(sgl, sg, INT_MAX, count) {
		if (!sg)
			break;

		pci_free_p2pmem(pdev, sg_virt(sg), sg->length);
	}
	kfree(sgl);
}
EXPORT_SYMBOL_GPL(pci_p2pmem_free_sgl);

/**
 * pci_p2pmem_publish - publish the peer-to-peer DMA memory for use by
 *	other devices with pci_p2pmem_find()
 * @pdev: the device with peer-to-peer DMA memory to publish
 * @publish: set to true to publish the memory, false to unpublish it
 *
 * Published memory can be used by other PCI device drivers for
 * peer-2-peer DMA operations. Non-published memory is reserved for
 * exclusive use of the device driver that registers the peer-to-peer
 * memory.
 */
void pci_p2pmem_publish(struct pci_dev *pdev, bool publish)
{
	if (pdev->p2pdma)
		pdev->p2pdma->p2pmem_published = publish;
}
EXPORT_SYMBOL_GPL(pci_p2pmem_publish);

static enum pci_p2pdma_map_type pci_p2pdma_map_type(struct pci_dev *provider,
						    struct pci_dev *client)
{
	if (!provider->p2pdma)
		return PCI_P2PDMA_MAP_NOT_SUPPORTED;

	return xa_to_value(xa_load(&provider->p2pdma->map_types,
				   map_types_idx(client)));
}

static int __pci_p2pdma_map_sg(struct pci_p2pdma_pagemap *p2p_pgmap,
		struct device *dev, struct scatterlist *sg, int nents)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		s->dma_address = sg_phys(s) - p2p_pgmap->bus_offset;
		sg_dma_len(s) = s->length;
	}

	return nents;
}

/**
 * pci_p2pdma_map_sg_attrs - map a PCI peer-to-peer scatterlist for DMA
 * @dev: device doing the DMA request
 * @sg: scatter list to map
 * @nents: elements in the scatterlist
 * @dir: DMA direction
 * @attrs: DMA attributes passed to dma_map_sg() (if called)
 *
 * Scatterlists mapped with this function should be unmapped using
 * pci_p2pdma_unmap_sg_attrs().
 *
 * Returns the number of SG entries mapped or 0 on error.
 */
int pci_p2pdma_map_sg_attrs(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	struct pci_p2pdma_pagemap *p2p_pgmap =
		to_p2p_pgmap(sg_page(sg)->pgmap);
	struct pci_dev *client;

	if (WARN_ON_ONCE(!dev_is_pci(dev)))
		return 0;

	client = to_pci_dev(dev);

	switch (pci_p2pdma_map_type(p2p_pgmap->provider, client)) {
	case PCI_P2PDMA_MAP_THRU_HOST_BRIDGE:
		return dma_map_sg_attrs(dev, sg, nents, dir, attrs);
	case PCI_P2PDMA_MAP_BUS_ADDR:
		return __pci_p2pdma_map_sg(p2p_pgmap, dev, sg, nents);
	default:
		WARN_ON_ONCE(1);
		return 0;
	}
}
EXPORT_SYMBOL_GPL(pci_p2pdma_map_sg_attrs);

/**
 * pci_p2pdma_unmap_sg_attrs - unmap a PCI peer-to-peer scatterlist that was
 *	mapped with pci_p2pdma_map_sg()
 * @dev: device doing the DMA request
 * @sg: scatter list to map
 * @nents: number of elements returned by pci_p2pdma_map_sg()
 * @dir: DMA direction
 * @attrs: DMA attributes passed to dma_unmap_sg() (if called)
 */
void pci_p2pdma_unmap_sg_attrs(struct device *dev, struct scatterlist *sg,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	struct pci_p2pdma_pagemap *p2p_pgmap =
		to_p2p_pgmap(sg_page(sg)->pgmap);
	enum pci_p2pdma_map_type map_type;
	struct pci_dev *client;

	if (WARN_ON_ONCE(!dev_is_pci(dev)))
		return;

	client = to_pci_dev(dev);

	map_type = pci_p2pdma_map_type(p2p_pgmap->provider, client);

	if (map_type == PCI_P2PDMA_MAP_THRU_HOST_BRIDGE)
		dma_unmap_sg_attrs(dev, sg, nents, dir, attrs);
}
EXPORT_SYMBOL_GPL(pci_p2pdma_unmap_sg_attrs);

/**
 * pci_p2pdma_enable_store - parse a configfs/sysfs attribute store
 *		to enable p2pdma
 * @page: contents of the value to be stored
 * @p2p_dev: returns the PCI device that was selected to be used
 *		(if one was specified in the stored value)
 * @use_p2pdma: returns whether to enable p2pdma or not
 *
 * Parses an attribute value to decide whether to enable p2pdma.
 * The value can select a PCI device (using its full BDF device
 * name) or a boolean (in any format strtobool() accepts). A false
 * value disables p2pdma, a true value expects the caller
 * to automatically find a compatible device and specifying a PCI device
 * expects the caller to use the specific provider.
 *
 * pci_p2pdma_enable_show() should be used as the show operation for
 * the attribute.
 *
 * Returns 0 on success
 */
int pci_p2pdma_enable_store(const char *page, struct pci_dev **p2p_dev,
			    bool *use_p2pdma)
{
	struct device *dev;

	dev = bus_find_device_by_name(&pci_bus_type, NULL, page);
	if (dev) {
		*use_p2pdma = true;
		*p2p_dev = to_pci_dev(dev);

		if (!pci_has_p2pmem(*p2p_dev)) {
			pci_err(*p2p_dev,
				"PCI device has no peer-to-peer memory: %s\n",
				page);
			pci_dev_put(*p2p_dev);
			return -ENODEV;
		}

		return 0;
	} else if ((page[0] == '0' || page[0] == '1') && !iscntrl(page[1])) {
		/*
		 * If the user enters a PCI device that  doesn't exist
		 * like "0000:01:00.1", we don't want strtobool to think
		 * it's a '0' when it's clearly not what the user wanted.
		 * So we require 0's and 1's to be exactly one character.
		 */
	} else if (!strtobool(page, use_p2pdma)) {
		return 0;
	}

	pr_err("No such PCI device: %.*s\n", (int)strcspn(page, "\n"), page);
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(pci_p2pdma_enable_store);

/**
 * pci_p2pdma_enable_show - show a configfs/sysfs attribute indicating
 *		whether p2pdma is enabled
 * @page: contents of the stored value
 * @p2p_dev: the selected p2p device (NULL if no device is selected)
 * @use_p2pdma: whether p2pdma has been enabled
 *
 * Attributes that use pci_p2pdma_enable_store() should use this function
 * to show the value of the attribute.
 *
 * Returns 0 on success
 */
ssize_t pci_p2pdma_enable_show(char *page, struct pci_dev *p2p_dev,
			       bool use_p2pdma)
{
	if (!use_p2pdma)
		return sprintf(page, "0\n");

	if (!p2p_dev)
		return sprintf(page, "1\n");

	return sprintf(page, "%s\n", pci_name(p2p_dev));
}
EXPORT_SYMBOL_GPL(pci_p2pdma_enable_show);
