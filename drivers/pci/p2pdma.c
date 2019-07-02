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

struct pci_p2pdma {
	struct percpu_ref devmap_ref;
	struct completion devmap_ref_done;
	struct gen_pool *pool;
	bool p2pmem_published;
};

static ssize_t size_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	size_t size = 0;

	if (pdev->p2pdma->pool)
		size = gen_pool_size(pdev->p2pdma->pool);

	return snprintf(buf, PAGE_SIZE, "%zd\n", size);
}
static DEVICE_ATTR_RO(size);

static ssize_t available_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	size_t avail = 0;

	if (pdev->p2pdma->pool)
		avail = gen_pool_avail(pdev->p2pdma->pool);

	return snprintf(buf, PAGE_SIZE, "%zd\n", avail);
}
static DEVICE_ATTR_RO(available);

static ssize_t published_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);

	return snprintf(buf, PAGE_SIZE, "%d\n",
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

static void pci_p2pdma_percpu_release(struct percpu_ref *ref)
{
	struct pci_p2pdma *p2p =
		container_of(ref, struct pci_p2pdma, devmap_ref);

	complete_all(&p2p->devmap_ref_done);
}

static void pci_p2pdma_percpu_kill(struct percpu_ref *ref)
{
	/*
	 * pci_p2pdma_add_resource() may be called multiple times
	 * by a driver and may register the percpu_kill devm action multiple
	 * times. We only want the first action to actually kill the
	 * percpu_ref.
	 */
	if (percpu_ref_is_dying(ref))
		return;

	percpu_ref_kill(ref);
}

static void pci_p2pdma_release(void *data)
{
	struct pci_dev *pdev = data;

	if (!pdev->p2pdma)
		return;

	wait_for_completion(&pdev->p2pdma->devmap_ref_done);
	percpu_ref_exit(&pdev->p2pdma->devmap_ref);

	gen_pool_destroy(pdev->p2pdma->pool);
	sysfs_remove_group(&pdev->dev.kobj, &p2pmem_group);
	pdev->p2pdma = NULL;
}

static int pci_p2pdma_setup(struct pci_dev *pdev)
{
	int error = -ENOMEM;
	struct pci_p2pdma *p2p;

	p2p = devm_kzalloc(&pdev->dev, sizeof(*p2p), GFP_KERNEL);
	if (!p2p)
		return -ENOMEM;

	p2p->pool = gen_pool_create(PAGE_SHIFT, dev_to_node(&pdev->dev));
	if (!p2p->pool)
		goto out;

	init_completion(&p2p->devmap_ref_done);
	error = percpu_ref_init(&p2p->devmap_ref,
			pci_p2pdma_percpu_release, 0, GFP_KERNEL);
	if (error)
		goto out_pool_destroy;

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

	pgmap = devm_kzalloc(&pdev->dev, sizeof(*pgmap), GFP_KERNEL);
	if (!pgmap)
		return -ENOMEM;

	pgmap->res.start = pci_resource_start(pdev, bar) + offset;
	pgmap->res.end = pgmap->res.start + size - 1;
	pgmap->res.flags = pci_resource_flags(pdev, bar);
	pgmap->ref = &pdev->p2pdma->devmap_ref;
	pgmap->type = MEMORY_DEVICE_PCI_P2PDMA;
	pgmap->pci_p2pdma_bus_offset = pci_bus_address(pdev, bar) -
		pci_resource_start(pdev, bar);
	pgmap->kill = pci_p2pdma_percpu_kill;

	addr = devm_memremap_pages(&pdev->dev, pgmap);
	if (IS_ERR(addr)) {
		error = PTR_ERR(addr);
		goto pgmap_free;
	}

	error = gen_pool_add_virt(pdev->p2pdma->pool, (unsigned long)addr,
			pci_bus_address(pdev, bar) + offset,
			resource_size(&pgmap->res), dev_to_node(&pdev->dev));
	if (error)
		goto pgmap_free;

	pci_info(pdev, "added peer-to-peer DMA memory %pR\n",
		 &pgmap->res);

	return 0;

pgmap_free:
	devm_kfree(&pdev->dev, pgmap);
	return error;
}
EXPORT_SYMBOL_GPL(pci_p2pdma_add_resource);

/*
 * Note this function returns the parent PCI device with a
 * reference taken. It is the caller's responsibily to drop
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

	pos = pci_find_ext_capability(pdev, PCI_EXT_CAP_ID_ACS);
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

/*
 * If we can't find a common upstream bridge take a look at the root
 * complex and compare it to a whitelist of known good hardware.
 */
static bool root_complex_whitelist(struct pci_dev *dev)
{
	struct pci_host_bridge *host = pci_find_host_bridge(dev->bus);
	struct pci_dev *root = pci_get_slot(host->bus, PCI_DEVFN(0, 0));
	unsigned short vendor, device;

	if (!root)
		return false;

	vendor = root->vendor;
	device = root->device;
	pci_dev_put(root);

	/* AMD ZEN host bridges can do peer to peer */
	if (vendor == PCI_VENDOR_ID_AMD && device == 0x1450)
		return true;

	return false;
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
 * Any two devices that don't have a common upstream bridge will return -1.
 * In this way devices on separate PCIe root ports will be rejected, which
 * is what we want for peer-to-peer seeing each PCIe root port defines a
 * separate hierarchy domain and there's no way to determine whether the root
 * complex supports forwarding between them.
 *
 * In the case where two devices are connected to different PCIe switches,
 * this function will still return a positive distance as long as both
 * switches eventually have a common upstream bridge. Note this covers
 * the case of using multiple PCIe switches to achieve a desired level of
 * fan-out from a root port. The exact distance will be a function of the
 * number of switches between Device A and Device B.
 *
 * If a bridge which has any ACS redirection bits set is in the path
 * then this functions will return -2. This is so we reject any
 * cases where the TLPs are forwarded up into the root complex.
 * In this case, a list of all infringing bridge addresses will be
 * populated in acs_list (assuming it's non-null) for printk purposes.
 */
static int upstream_bridge_distance(struct pci_dev *provider,
				    struct pci_dev *client,
				    struct seq_buf *acs_list)
{
	struct pci_dev *a = provider, *b = client, *bb;
	int dist_a = 0;
	int dist_b = 0;
	int acs_cnt = 0;

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

	/*
	 * Allow the connection if both devices are on a whitelisted root
	 * complex, but add an arbitary large value to the distance.
	 */
	if (root_complex_whitelist(provider) &&
	    root_complex_whitelist(client))
		return 0x1000 + dist_a + dist_b;

	return -1;

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

	if (acs_cnt)
		return -2;

	return dist_a + dist_b;
}

static int upstream_bridge_distance_warn(struct pci_dev *provider,
					 struct pci_dev *client)
{
	struct seq_buf acs_list;
	int ret;

	seq_buf_init(&acs_list, kmalloc(PAGE_SIZE, GFP_KERNEL), PAGE_SIZE);
	if (!acs_list.buffer)
		return -ENOMEM;

	ret = upstream_bridge_distance(provider, client, &acs_list);
	if (ret == -2) {
		pci_warn(client, "cannot be used for peer-to-peer DMA as ACS redirect is set between the client and provider (%s)\n",
			 pci_name(provider));
		/* Drop final semicolon */
		acs_list.buffer[acs_list.len-1] = 0;
		pci_warn(client, "to disable ACS redirect for this path, add the kernel parameter: pci=disable_acs_redir=%s\n",
			 acs_list.buffer);

	} else if (ret < 0) {
		pci_warn(client, "cannot be used for peer-to-peer DMA as the client and provider (%s) do not share an upstream bridge\n",
			 pci_name(provider));
	}

	kfree(acs_list.buffer);

	return ret;
}

/**
 * pci_p2pdma_distance_many - Determive the cumulative distance between
 *	a p2pdma provider and the clients in use.
 * @provider: p2pdma provider to check against the client list
 * @clients: array of devices to check (NULL-terminated)
 * @num_clients: number of clients in the array
 * @verbose: if true, print warnings for devices when we return -1
 *
 * Returns -1 if any of the clients are not compatible (behind the same
 * root port as the provider), otherwise returns a positive number where
 * a lower number is the preferable choice. (If there's one client
 * that's the same as the provider it will return 0, which is best choice).
 *
 * For now, "compatible" means the provider and the clients are all behind
 * the same PCI root port. This cuts out cases that may work but is safest
 * for the user. Future work can expand this to white-list root complexes that
 * can safely forward between each ports.
 */
int pci_p2pdma_distance_many(struct pci_dev *provider, struct device **clients,
			     int num_clients, bool verbose)
{
	bool not_supported = false;
	struct pci_dev *pci_client;
	int distance = 0;
	int i, ret;

	if (num_clients == 0)
		return -1;

	for (i = 0; i < num_clients; i++) {
		if (IS_ENABLED(CONFIG_DMA_VIRT_OPS) &&
		    clients[i]->dma_ops == &dma_virt_ops) {
			if (verbose)
				dev_warn(clients[i],
					 "cannot be used for peer-to-peer DMA because the driver makes use of dma_virt_ops\n");
			return -1;
		}

		pci_client = find_parent_pci_dev(clients[i]);
		if (!pci_client) {
			if (verbose)
				dev_warn(clients[i],
					 "cannot be used for peer-to-peer DMA as it is not a PCI device\n");
			return -1;
		}

		if (verbose)
			ret = upstream_bridge_distance_warn(provider,
							    pci_client);
		else
			ret = upstream_bridge_distance(provider, pci_client,
						       NULL);

		pci_dev_put(pci_client);

		if (ret < 0)
			not_supported = true;

		if (not_supported && !verbose)
			break;

		distance += ret;
	}

	if (not_supported)
		return -1;

	return distance;
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
 * pci_p2pmem_find - find a peer-to-peer DMA memory device compatible with
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
 * pci_alloc_p2p_mem - allocate peer-to-peer DMA memory
 * @pdev: the device to allocate memory from
 * @size: number of bytes to allocate
 *
 * Returns the allocated memory or NULL on error.
 */
void *pci_alloc_p2pmem(struct pci_dev *pdev, size_t size)
{
	void *ret;

	if (unlikely(!pdev->p2pdma))
		return NULL;

	if (unlikely(!percpu_ref_tryget_live(&pdev->p2pdma->devmap_ref)))
		return NULL;

	ret = (void *)gen_pool_alloc(pdev->p2pdma->pool, size);

	if (unlikely(!ret))
		percpu_ref_put(&pdev->p2pdma->devmap_ref);

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
	gen_pool_free(pdev->p2pdma->pool, (uintptr_t)addr, size);
	percpu_ref_put(&pdev->p2pdma->devmap_ref);
}
EXPORT_SYMBOL_GPL(pci_free_p2pmem);

/**
 * pci_virt_to_bus - return the PCI bus address for a given virtual
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

	sg = kzalloc(sizeof(*sg), GFP_KERNEL);
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

/**
 * pci_p2pdma_map_sg - map a PCI peer-to-peer scatterlist for DMA
 * @dev: device doing the DMA request
 * @sg: scatter list to map
 * @nents: elements in the scatterlist
 * @dir: DMA direction
 *
 * Scatterlists mapped with this function should not be unmapped in any way.
 *
 * Returns the number of SG entries mapped or 0 on error.
 */
int pci_p2pdma_map_sg(struct device *dev, struct scatterlist *sg, int nents,
		      enum dma_data_direction dir)
{
	struct dev_pagemap *pgmap;
	struct scatterlist *s;
	phys_addr_t paddr;
	int i;

	/*
	 * p2pdma mappings are not compatible with devices that use
	 * dma_virt_ops. If the upper layers do the right thing
	 * this should never happen because it will be prevented
	 * by the check in pci_p2pdma_distance_many()
	 */
	if (WARN_ON_ONCE(IS_ENABLED(CONFIG_DMA_VIRT_OPS) &&
			 dev->dma_ops == &dma_virt_ops))
		return 0;

	for_each_sg(sg, s, nents, i) {
		pgmap = sg_page(s)->pgmap;
		paddr = sg_phys(s);

		s->dma_address = paddr - pgmap->pci_p2pdma_bus_offset;
		sg_dma_len(s) = s->length;
	}

	return nents;
}
EXPORT_SYMBOL_GPL(pci_p2pdma_map_sg);

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
