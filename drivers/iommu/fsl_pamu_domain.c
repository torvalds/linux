// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 * Author: Varun Sethi <varun.sethi@freescale.com>
 */

#define pr_fmt(fmt)    "fsl-pamu-domain: %s: " fmt, __func__

#include "fsl_pamu_domain.h"

#include <sysdev/fsl_pci.h>

/*
 * Global spinlock that needs to be held while
 * configuring PAMU.
 */
static DEFINE_SPINLOCK(iommu_lock);

static struct kmem_cache *fsl_pamu_domain_cache;
static struct kmem_cache *iommu_devinfo_cache;
static DEFINE_SPINLOCK(device_domain_lock);

struct iommu_device pamu_iommu;	/* IOMMU core code handle */

static struct fsl_dma_domain *to_fsl_dma_domain(struct iommu_domain *dom)
{
	return container_of(dom, struct fsl_dma_domain, iommu_domain);
}

static int __init iommu_init_mempool(void)
{
	fsl_pamu_domain_cache = kmem_cache_create("fsl_pamu_domain",
						  sizeof(struct fsl_dma_domain),
						  0,
						  SLAB_HWCACHE_ALIGN,
						  NULL);
	if (!fsl_pamu_domain_cache) {
		pr_debug("Couldn't create fsl iommu_domain cache\n");
		return -ENOMEM;
	}

	iommu_devinfo_cache = kmem_cache_create("iommu_devinfo",
						sizeof(struct device_domain_info),
						0,
						SLAB_HWCACHE_ALIGN,
						NULL);
	if (!iommu_devinfo_cache) {
		pr_debug("Couldn't create devinfo cache\n");
		kmem_cache_destroy(fsl_pamu_domain_cache);
		return -ENOMEM;
	}

	return 0;
}

static phys_addr_t get_phys_addr(struct fsl_dma_domain *dma_domain, dma_addr_t iova)
{
	u32 win_cnt = dma_domain->win_cnt;
	struct dma_window *win_ptr = &dma_domain->win_arr[0];
	struct iommu_domain_geometry *geom;

	geom = &dma_domain->iommu_domain.geometry;

	if (!win_cnt || !dma_domain->geom_size) {
		pr_debug("Number of windows/geometry not configured for the domain\n");
		return 0;
	}

	if (win_cnt > 1) {
		u64 subwin_size;
		dma_addr_t subwin_iova;
		u32 wnd;

		subwin_size = dma_domain->geom_size >> ilog2(win_cnt);
		subwin_iova = iova & ~(subwin_size - 1);
		wnd = (subwin_iova - geom->aperture_start) >> ilog2(subwin_size);
		win_ptr = &dma_domain->win_arr[wnd];
	}

	if (win_ptr->valid)
		return win_ptr->paddr + (iova & (win_ptr->size - 1));

	return 0;
}

static int map_subwins(int liodn, struct fsl_dma_domain *dma_domain)
{
	struct dma_window *sub_win_ptr = &dma_domain->win_arr[0];
	int i, ret;
	unsigned long rpn, flags;

	for (i = 0; i < dma_domain->win_cnt; i++) {
		if (sub_win_ptr[i].valid) {
			rpn = sub_win_ptr[i].paddr >> PAMU_PAGE_SHIFT;
			spin_lock_irqsave(&iommu_lock, flags);
			ret = pamu_config_spaace(liodn, dma_domain->win_cnt, i,
						 sub_win_ptr[i].size,
						 ~(u32)0,
						 rpn,
						 dma_domain->snoop_id,
						 dma_domain->stash_id,
						 (i > 0) ? 1 : 0,
						 sub_win_ptr[i].prot);
			spin_unlock_irqrestore(&iommu_lock, flags);
			if (ret) {
				pr_debug("SPAACE configuration failed for liodn %d\n",
					 liodn);
				return ret;
			}
		}
	}

	return ret;
}

static int map_win(int liodn, struct fsl_dma_domain *dma_domain)
{
	int ret;
	struct dma_window *wnd = &dma_domain->win_arr[0];
	phys_addr_t wnd_addr = dma_domain->iommu_domain.geometry.aperture_start;
	unsigned long flags;

	spin_lock_irqsave(&iommu_lock, flags);
	ret = pamu_config_ppaace(liodn, wnd_addr,
				 wnd->size,
				 ~(u32)0,
				 wnd->paddr >> PAMU_PAGE_SHIFT,
				 dma_domain->snoop_id, dma_domain->stash_id,
				 0, wnd->prot);
	spin_unlock_irqrestore(&iommu_lock, flags);
	if (ret)
		pr_debug("PAACE configuration failed for liodn %d\n", liodn);

	return ret;
}

/* Map the DMA window corresponding to the LIODN */
static int map_liodn(int liodn, struct fsl_dma_domain *dma_domain)
{
	if (dma_domain->win_cnt > 1)
		return map_subwins(liodn, dma_domain);
	else
		return map_win(liodn, dma_domain);
}

/* Update window/subwindow mapping for the LIODN */
static int update_liodn(int liodn, struct fsl_dma_domain *dma_domain, u32 wnd_nr)
{
	int ret;
	struct dma_window *wnd = &dma_domain->win_arr[wnd_nr];
	unsigned long flags;

	spin_lock_irqsave(&iommu_lock, flags);
	if (dma_domain->win_cnt > 1) {
		ret = pamu_config_spaace(liodn, dma_domain->win_cnt, wnd_nr,
					 wnd->size,
					 ~(u32)0,
					 wnd->paddr >> PAMU_PAGE_SHIFT,
					 dma_domain->snoop_id,
					 dma_domain->stash_id,
					 (wnd_nr > 0) ? 1 : 0,
					 wnd->prot);
		if (ret)
			pr_debug("Subwindow reconfiguration failed for liodn %d\n",
				 liodn);
	} else {
		phys_addr_t wnd_addr;

		wnd_addr = dma_domain->iommu_domain.geometry.aperture_start;

		ret = pamu_config_ppaace(liodn, wnd_addr,
					 wnd->size,
					 ~(u32)0,
					 wnd->paddr >> PAMU_PAGE_SHIFT,
					 dma_domain->snoop_id, dma_domain->stash_id,
					 0, wnd->prot);
		if (ret)
			pr_debug("Window reconfiguration failed for liodn %d\n",
				 liodn);
	}

	spin_unlock_irqrestore(&iommu_lock, flags);

	return ret;
}

static int update_liodn_stash(int liodn, struct fsl_dma_domain *dma_domain,
			      u32 val)
{
	int ret = 0, i;
	unsigned long flags;

	spin_lock_irqsave(&iommu_lock, flags);
	if (!dma_domain->win_arr) {
		pr_debug("Windows not configured, stash destination update failed for liodn %d\n",
			 liodn);
		spin_unlock_irqrestore(&iommu_lock, flags);
		return -EINVAL;
	}

	for (i = 0; i < dma_domain->win_cnt; i++) {
		ret = pamu_update_paace_stash(liodn, i, val);
		if (ret) {
			pr_debug("Failed to update SPAACE %d field for liodn %d\n ",
				 i, liodn);
			spin_unlock_irqrestore(&iommu_lock, flags);
			return ret;
		}
	}

	spin_unlock_irqrestore(&iommu_lock, flags);

	return ret;
}

/* Set the geometry parameters for a LIODN */
static int pamu_set_liodn(int liodn, struct device *dev,
			  struct fsl_dma_domain *dma_domain,
			  struct iommu_domain_geometry *geom_attr,
			  u32 win_cnt)
{
	phys_addr_t window_addr, window_size;
	phys_addr_t subwin_size;
	int ret = 0, i;
	u32 omi_index = ~(u32)0;
	unsigned long flags;

	/*
	 * Configure the omi_index at the geometry setup time.
	 * This is a static value which depends on the type of
	 * device and would not change thereafter.
	 */
	get_ome_index(&omi_index, dev);

	window_addr = geom_attr->aperture_start;
	window_size = dma_domain->geom_size;

	spin_lock_irqsave(&iommu_lock, flags);
	ret = pamu_disable_liodn(liodn);
	if (!ret)
		ret = pamu_config_ppaace(liodn, window_addr, window_size, omi_index,
					 0, dma_domain->snoop_id,
					 dma_domain->stash_id, win_cnt, 0);
	spin_unlock_irqrestore(&iommu_lock, flags);
	if (ret) {
		pr_debug("PAACE configuration failed for liodn %d, win_cnt =%d\n",
			 liodn, win_cnt);
		return ret;
	}

	if (win_cnt > 1) {
		subwin_size = window_size >> ilog2(win_cnt);
		for (i = 0; i < win_cnt; i++) {
			spin_lock_irqsave(&iommu_lock, flags);
			ret = pamu_disable_spaace(liodn, i);
			if (!ret)
				ret = pamu_config_spaace(liodn, win_cnt, i,
							 subwin_size, omi_index,
							 0, dma_domain->snoop_id,
							 dma_domain->stash_id,
							 0, 0);
			spin_unlock_irqrestore(&iommu_lock, flags);
			if (ret) {
				pr_debug("SPAACE configuration failed for liodn %d\n",
					 liodn);
				return ret;
			}
		}
	}

	return ret;
}

static int check_size(u64 size, dma_addr_t iova)
{
	/*
	 * Size must be a power of two and at least be equal
	 * to PAMU page size.
	 */
	if ((size & (size - 1)) || size < PAMU_PAGE_SIZE) {
		pr_debug("Size too small or not a power of two\n");
		return -EINVAL;
	}

	/* iova must be page size aligned */
	if (iova & (size - 1)) {
		pr_debug("Address is not aligned with window size\n");
		return -EINVAL;
	}

	return 0;
}

static struct fsl_dma_domain *iommu_alloc_dma_domain(void)
{
	struct fsl_dma_domain *domain;

	domain = kmem_cache_zalloc(fsl_pamu_domain_cache, GFP_KERNEL);
	if (!domain)
		return NULL;

	domain->stash_id = ~(u32)0;
	domain->snoop_id = ~(u32)0;
	domain->win_cnt = pamu_get_max_subwin_cnt();
	domain->geom_size = 0;

	INIT_LIST_HEAD(&domain->devices);

	spin_lock_init(&domain->domain_lock);

	return domain;
}

static void remove_device_ref(struct device_domain_info *info, u32 win_cnt)
{
	unsigned long flags;

	list_del(&info->link);
	spin_lock_irqsave(&iommu_lock, flags);
	if (win_cnt > 1)
		pamu_free_subwins(info->liodn);
	pamu_disable_liodn(info->liodn);
	spin_unlock_irqrestore(&iommu_lock, flags);
	spin_lock_irqsave(&device_domain_lock, flags);
	dev_iommu_priv_set(info->dev, NULL);
	kmem_cache_free(iommu_devinfo_cache, info);
	spin_unlock_irqrestore(&device_domain_lock, flags);
}

static void detach_device(struct device *dev, struct fsl_dma_domain *dma_domain)
{
	struct device_domain_info *info, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&dma_domain->domain_lock, flags);
	/* Remove the device from the domain device list */
	list_for_each_entry_safe(info, tmp, &dma_domain->devices, link) {
		if (!dev || (info->dev == dev))
			remove_device_ref(info, dma_domain->win_cnt);
	}
	spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
}

static void attach_device(struct fsl_dma_domain *dma_domain, int liodn, struct device *dev)
{
	struct device_domain_info *info, *old_domain_info;
	unsigned long flags;

	spin_lock_irqsave(&device_domain_lock, flags);
	/*
	 * Check here if the device is already attached to domain or not.
	 * If the device is already attached to a domain detach it.
	 */
	old_domain_info = dev_iommu_priv_get(dev);
	if (old_domain_info && old_domain_info->domain != dma_domain) {
		spin_unlock_irqrestore(&device_domain_lock, flags);
		detach_device(dev, old_domain_info->domain);
		spin_lock_irqsave(&device_domain_lock, flags);
	}

	info = kmem_cache_zalloc(iommu_devinfo_cache, GFP_ATOMIC);

	info->dev = dev;
	info->liodn = liodn;
	info->domain = dma_domain;

	list_add(&info->link, &dma_domain->devices);
	/*
	 * In case of devices with multiple LIODNs just store
	 * the info for the first LIODN as all
	 * LIODNs share the same domain
	 */
	if (!dev_iommu_priv_get(dev))
		dev_iommu_priv_set(dev, info);
	spin_unlock_irqrestore(&device_domain_lock, flags);
}

static phys_addr_t fsl_pamu_iova_to_phys(struct iommu_domain *domain,
					 dma_addr_t iova)
{
	struct fsl_dma_domain *dma_domain = to_fsl_dma_domain(domain);

	if (iova < domain->geometry.aperture_start ||
	    iova > domain->geometry.aperture_end)
		return 0;

	return get_phys_addr(dma_domain, iova);
}

static bool fsl_pamu_capable(enum iommu_cap cap)
{
	return cap == IOMMU_CAP_CACHE_COHERENCY;
}

static void fsl_pamu_domain_free(struct iommu_domain *domain)
{
	struct fsl_dma_domain *dma_domain = to_fsl_dma_domain(domain);

	/* remove all the devices from the device list */
	detach_device(NULL, dma_domain);

	dma_domain->enabled = 0;
	dma_domain->mapped = 0;

	kmem_cache_free(fsl_pamu_domain_cache, dma_domain);
}

static struct iommu_domain *fsl_pamu_domain_alloc(unsigned type)
{
	struct fsl_dma_domain *dma_domain;

	if (type != IOMMU_DOMAIN_UNMANAGED)
		return NULL;

	dma_domain = iommu_alloc_dma_domain();
	if (!dma_domain) {
		pr_debug("dma_domain allocation failed\n");
		return NULL;
	}
	/* defaul geometry 64 GB i.e. maximum system address */
	dma_domain->iommu_domain. geometry.aperture_start = 0;
	dma_domain->iommu_domain.geometry.aperture_end = (1ULL << 36) - 1;
	dma_domain->iommu_domain.geometry.force_aperture = true;

	return &dma_domain->iommu_domain;
}

/* Configure geometry settings for all LIODNs associated with domain */
static int pamu_set_domain_geometry(struct fsl_dma_domain *dma_domain,
				    struct iommu_domain_geometry *geom_attr,
				    u32 win_cnt)
{
	struct device_domain_info *info;
	int ret = 0;

	list_for_each_entry(info, &dma_domain->devices, link) {
		ret = pamu_set_liodn(info->liodn, info->dev, dma_domain,
				     geom_attr, win_cnt);
		if (ret)
			break;
	}

	return ret;
}

/* Update stash destination for all LIODNs associated with the domain */
static int update_domain_stash(struct fsl_dma_domain *dma_domain, u32 val)
{
	struct device_domain_info *info;
	int ret = 0;

	list_for_each_entry(info, &dma_domain->devices, link) {
		ret = update_liodn_stash(info->liodn, dma_domain, val);
		if (ret)
			break;
	}

	return ret;
}

/* Update domain mappings for all LIODNs associated with the domain */
static int update_domain_mapping(struct fsl_dma_domain *dma_domain, u32 wnd_nr)
{
	struct device_domain_info *info;
	int ret = 0;

	list_for_each_entry(info, &dma_domain->devices, link) {
		ret = update_liodn(info->liodn, dma_domain, wnd_nr);
		if (ret)
			break;
	}
	return ret;
}


static int fsl_pamu_window_enable(struct iommu_domain *domain, u32 wnd_nr,
				  phys_addr_t paddr, u64 size, int prot)
{
	struct fsl_dma_domain *dma_domain = to_fsl_dma_domain(domain);
	struct dma_window *wnd;
	int pamu_prot = 0;
	int ret;
	unsigned long flags;
	u64 win_size;

	if (prot & IOMMU_READ)
		pamu_prot |= PAACE_AP_PERMS_QUERY;
	if (prot & IOMMU_WRITE)
		pamu_prot |= PAACE_AP_PERMS_UPDATE;

	spin_lock_irqsave(&dma_domain->domain_lock, flags);
	if (!dma_domain->win_arr) {
		pr_debug("Number of windows not configured\n");
		spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
		return -ENODEV;
	}

	if (wnd_nr >= dma_domain->win_cnt) {
		pr_debug("Invalid window index\n");
		spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
		return -EINVAL;
	}

	win_size = dma_domain->geom_size >> ilog2(dma_domain->win_cnt);
	if (size > win_size) {
		pr_debug("Invalid window size\n");
		spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
		return -EINVAL;
	}

	if (dma_domain->win_cnt == 1) {
		if (dma_domain->enabled) {
			pr_debug("Disable the window before updating the mapping\n");
			spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
			return -EBUSY;
		}

		ret = check_size(size, domain->geometry.aperture_start);
		if (ret) {
			pr_debug("Aperture start not aligned to the size\n");
			spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
			return -EINVAL;
		}
	}

	wnd = &dma_domain->win_arr[wnd_nr];
	if (!wnd->valid) {
		wnd->paddr = paddr;
		wnd->size = size;
		wnd->prot = pamu_prot;

		ret = update_domain_mapping(dma_domain, wnd_nr);
		if (!ret) {
			wnd->valid = 1;
			dma_domain->mapped++;
		}
	} else {
		pr_debug("Disable the window before updating the mapping\n");
		ret = -EBUSY;
	}

	spin_unlock_irqrestore(&dma_domain->domain_lock, flags);

	return ret;
}

/*
 * Attach the LIODN to the DMA domain and configure the geometry
 * and window mappings.
 */
static int handle_attach_device(struct fsl_dma_domain *dma_domain,
				struct device *dev, const u32 *liodn,
				int num)
{
	unsigned long flags;
	struct iommu_domain *domain = &dma_domain->iommu_domain;
	int ret = 0;
	int i;

	spin_lock_irqsave(&dma_domain->domain_lock, flags);
	for (i = 0; i < num; i++) {
		/* Ensure that LIODN value is valid */
		if (liodn[i] >= PAACE_NUMBER_ENTRIES) {
			pr_debug("Invalid liodn %d, attach device failed for %pOF\n",
				 liodn[i], dev->of_node);
			ret = -EINVAL;
			break;
		}

		attach_device(dma_domain, liodn[i], dev);
		/*
		 * Check if geometry has already been configured
		 * for the domain. If yes, set the geometry for
		 * the LIODN.
		 */
		if (dma_domain->win_arr) {
			u32 win_cnt = dma_domain->win_cnt > 1 ? dma_domain->win_cnt : 0;

			ret = pamu_set_liodn(liodn[i], dev, dma_domain,
					     &domain->geometry, win_cnt);
			if (ret)
				break;
			if (dma_domain->mapped) {
				/*
				 * Create window/subwindow mapping for
				 * the LIODN.
				 */
				ret = map_liodn(liodn[i], dma_domain);
				if (ret)
					break;
			}
		}
	}
	spin_unlock_irqrestore(&dma_domain->domain_lock, flags);

	return ret;
}

static int fsl_pamu_attach_device(struct iommu_domain *domain,
				  struct device *dev)
{
	struct fsl_dma_domain *dma_domain = to_fsl_dma_domain(domain);
	const u32 *liodn;
	u32 liodn_cnt;
	int len, ret = 0;
	struct pci_dev *pdev = NULL;
	struct pci_controller *pci_ctl;

	/*
	 * Use LIODN of the PCI controller while attaching a
	 * PCI device.
	 */
	if (dev_is_pci(dev)) {
		pdev = to_pci_dev(dev);
		pci_ctl = pci_bus_to_host(pdev->bus);
		/*
		 * make dev point to pci controller device
		 * so we can get the LIODN programmed by
		 * u-boot.
		 */
		dev = pci_ctl->parent;
	}

	liodn = of_get_property(dev->of_node, "fsl,liodn", &len);
	if (liodn) {
		liodn_cnt = len / sizeof(u32);
		ret = handle_attach_device(dma_domain, dev, liodn, liodn_cnt);
	} else {
		pr_debug("missing fsl,liodn property at %pOF\n", dev->of_node);
		ret = -EINVAL;
	}

	return ret;
}

static void fsl_pamu_detach_device(struct iommu_domain *domain,
				   struct device *dev)
{
	struct fsl_dma_domain *dma_domain = to_fsl_dma_domain(domain);
	const u32 *prop;
	int len;
	struct pci_dev *pdev = NULL;
	struct pci_controller *pci_ctl;

	/*
	 * Use LIODN of the PCI controller while detaching a
	 * PCI device.
	 */
	if (dev_is_pci(dev)) {
		pdev = to_pci_dev(dev);
		pci_ctl = pci_bus_to_host(pdev->bus);
		/*
		 * make dev point to pci controller device
		 * so we can get the LIODN programmed by
		 * u-boot.
		 */
		dev = pci_ctl->parent;
	}

	prop = of_get_property(dev->of_node, "fsl,liodn", &len);
	if (prop)
		detach_device(dev, dma_domain);
	else
		pr_debug("missing fsl,liodn property at %pOF\n", dev->of_node);
}

static  int configure_domain_geometry(struct iommu_domain *domain, void *data)
{
	struct iommu_domain_geometry *geom_attr = data;
	struct fsl_dma_domain *dma_domain = to_fsl_dma_domain(domain);
	dma_addr_t geom_size;
	unsigned long flags;

	geom_size = geom_attr->aperture_end - geom_attr->aperture_start + 1;
	/*
	 * Sanity check the geometry size. Also, we do not support
	 * DMA outside of the geometry.
	 */
	if (check_size(geom_size, geom_attr->aperture_start) ||
	    !geom_attr->force_aperture) {
		pr_debug("Invalid PAMU geometry attributes\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&dma_domain->domain_lock, flags);
	if (dma_domain->enabled) {
		pr_debug("Can't set geometry attributes as domain is active\n");
		spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
		return  -EBUSY;
	}

	/* Copy the domain geometry information */
	memcpy(&domain->geometry, geom_attr,
	       sizeof(struct iommu_domain_geometry));
	dma_domain->geom_size = geom_size;

	spin_unlock_irqrestore(&dma_domain->domain_lock, flags);

	return 0;
}

/* Set the domain stash attribute */
static int configure_domain_stash(struct fsl_dma_domain *dma_domain, void *data)
{
	struct pamu_stash_attribute *stash_attr = data;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&dma_domain->domain_lock, flags);

	memcpy(&dma_domain->dma_stash, stash_attr,
	       sizeof(struct pamu_stash_attribute));

	dma_domain->stash_id = get_stash_id(stash_attr->cache,
					    stash_attr->cpu);
	if (dma_domain->stash_id == ~(u32)0) {
		pr_debug("Invalid stash attributes\n");
		spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
		return -EINVAL;
	}

	ret = update_domain_stash(dma_domain, dma_domain->stash_id);

	spin_unlock_irqrestore(&dma_domain->domain_lock, flags);

	return ret;
}

/* Configure domain dma state i.e. enable/disable DMA */
static int configure_domain_dma_state(struct fsl_dma_domain *dma_domain, bool enable)
{
	struct device_domain_info *info;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&dma_domain->domain_lock, flags);

	if (enable && !dma_domain->mapped) {
		pr_debug("Can't enable DMA domain without valid mapping\n");
		spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
		return -ENODEV;
	}

	dma_domain->enabled = enable;
	list_for_each_entry(info, &dma_domain->devices, link) {
		ret = (enable) ? pamu_enable_liodn(info->liodn) :
			pamu_disable_liodn(info->liodn);
		if (ret)
			pr_debug("Unable to set dma state for liodn %d",
				 info->liodn);
	}
	spin_unlock_irqrestore(&dma_domain->domain_lock, flags);

	return 0;
}

static int fsl_pamu_set_windows(struct iommu_domain *domain, u32 w_count)
{
	struct fsl_dma_domain *dma_domain = to_fsl_dma_domain(domain);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&dma_domain->domain_lock, flags);
	/* Ensure domain is inactive i.e. DMA should be disabled for the domain */
	if (dma_domain->enabled) {
		pr_debug("Can't set geometry attributes as domain is active\n");
		spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
		return  -EBUSY;
	}

	/* Ensure that the geometry has been set for the domain */
	if (!dma_domain->geom_size) {
		pr_debug("Please configure geometry before setting the number of windows\n");
		spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
		return -EINVAL;
	}

	/*
	 * Ensure we have valid window count i.e. it should be less than
	 * maximum permissible limit and should be a power of two.
	 */
	if (w_count > pamu_get_max_subwin_cnt() || !is_power_of_2(w_count)) {
		pr_debug("Invalid window count\n");
		spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
		return -EINVAL;
	}

	ret = pamu_set_domain_geometry(dma_domain, &domain->geometry,
				       w_count > 1 ? w_count : 0);
	if (!ret) {
		kfree(dma_domain->win_arr);
		dma_domain->win_arr = kcalloc(w_count,
					      sizeof(*dma_domain->win_arr),
					      GFP_ATOMIC);
		if (!dma_domain->win_arr) {
			spin_unlock_irqrestore(&dma_domain->domain_lock, flags);
			return -ENOMEM;
		}
		dma_domain->win_cnt = w_count;
	}
	spin_unlock_irqrestore(&dma_domain->domain_lock, flags);

	return ret;
}

static int fsl_pamu_set_domain_attr(struct iommu_domain *domain,
				    enum iommu_attr attr_type, void *data)
{
	struct fsl_dma_domain *dma_domain = to_fsl_dma_domain(domain);
	int ret = 0;

	switch (attr_type) {
	case DOMAIN_ATTR_GEOMETRY:
		ret = configure_domain_geometry(domain, data);
		break;
	case DOMAIN_ATTR_FSL_PAMU_STASH:
		ret = configure_domain_stash(dma_domain, data);
		break;
	case DOMAIN_ATTR_FSL_PAMU_ENABLE:
		ret = configure_domain_dma_state(dma_domain, *(int *)data);
		break;
	case DOMAIN_ATTR_WINDOWS:
		ret = fsl_pamu_set_windows(domain, *(u32 *)data);
		break;
	default:
		pr_debug("Unsupported attribute type\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static struct iommu_group *get_device_iommu_group(struct device *dev)
{
	struct iommu_group *group;

	group = iommu_group_get(dev);
	if (!group)
		group = iommu_group_alloc();

	return group;
}

static  bool check_pci_ctl_endpt_part(struct pci_controller *pci_ctl)
{
	u32 version;

	/* Check the PCI controller version number by readding BRR1 register */
	version = in_be32(pci_ctl->cfg_addr + (PCI_FSL_BRR1 >> 2));
	version &= PCI_FSL_BRR1_VER;
	/* If PCI controller version is >= 0x204 we can partition endpoints */
	return version >= 0x204;
}

/* Get iommu group information from peer devices or devices on the parent bus */
static struct iommu_group *get_shared_pci_device_group(struct pci_dev *pdev)
{
	struct pci_dev *tmp;
	struct iommu_group *group;
	struct pci_bus *bus = pdev->bus;

	/*
	 * Traverese the pci bus device list to get
	 * the shared iommu group.
	 */
	while (bus) {
		list_for_each_entry(tmp, &bus->devices, bus_list) {
			if (tmp == pdev)
				continue;
			group = iommu_group_get(&tmp->dev);
			if (group)
				return group;
		}

		bus = bus->parent;
	}

	return NULL;
}

static struct iommu_group *get_pci_device_group(struct pci_dev *pdev)
{
	struct pci_controller *pci_ctl;
	bool pci_endpt_partitioning;
	struct iommu_group *group = NULL;

	pci_ctl = pci_bus_to_host(pdev->bus);
	pci_endpt_partitioning = check_pci_ctl_endpt_part(pci_ctl);
	/* We can partition PCIe devices so assign device group to the device */
	if (pci_endpt_partitioning) {
		group = pci_device_group(&pdev->dev);

		/*
		 * PCIe controller is not a paritionable entity
		 * free the controller device iommu_group.
		 */
		if (pci_ctl->parent->iommu_group)
			iommu_group_remove_device(pci_ctl->parent);
	} else {
		/*
		 * All devices connected to the controller will share the
		 * PCI controllers device group. If this is the first
		 * device to be probed for the pci controller, copy the
		 * device group information from the PCI controller device
		 * node and remove the PCI controller iommu group.
		 * For subsequent devices, the iommu group information can
		 * be obtained from sibling devices (i.e. from the bus_devices
		 * link list).
		 */
		if (pci_ctl->parent->iommu_group) {
			group = get_device_iommu_group(pci_ctl->parent);
			iommu_group_remove_device(pci_ctl->parent);
		} else {
			group = get_shared_pci_device_group(pdev);
		}
	}

	if (!group)
		group = ERR_PTR(-ENODEV);

	return group;
}

static struct iommu_group *fsl_pamu_device_group(struct device *dev)
{
	struct iommu_group *group = ERR_PTR(-ENODEV);
	int len;

	/*
	 * For platform devices we allocate a separate group for
	 * each of the devices.
	 */
	if (dev_is_pci(dev))
		group = get_pci_device_group(to_pci_dev(dev));
	else if (of_get_property(dev->of_node, "fsl,liodn", &len))
		group = get_device_iommu_group(dev);

	return group;
}

static struct iommu_device *fsl_pamu_probe_device(struct device *dev)
{
	return &pamu_iommu;
}

static void fsl_pamu_release_device(struct device *dev)
{
}

static const struct iommu_ops fsl_pamu_ops = {
	.capable	= fsl_pamu_capable,
	.domain_alloc	= fsl_pamu_domain_alloc,
	.domain_free    = fsl_pamu_domain_free,
	.attach_dev	= fsl_pamu_attach_device,
	.detach_dev	= fsl_pamu_detach_device,
	.domain_window_enable = fsl_pamu_window_enable,
	.iova_to_phys	= fsl_pamu_iova_to_phys,
	.domain_set_attr = fsl_pamu_set_domain_attr,
	.probe_device	= fsl_pamu_probe_device,
	.release_device	= fsl_pamu_release_device,
	.device_group   = fsl_pamu_device_group,
};

int __init pamu_domain_init(void)
{
	int ret = 0;

	ret = iommu_init_mempool();
	if (ret)
		return ret;

	ret = iommu_device_sysfs_add(&pamu_iommu, NULL, NULL, "iommu0");
	if (ret)
		return ret;

	iommu_device_set_ops(&pamu_iommu, &fsl_pamu_ops);

	ret = iommu_device_register(&pamu_iommu);
	if (ret) {
		iommu_device_sysfs_remove(&pamu_iommu);
		pr_err("Can't register iommu device\n");
		return ret;
	}

	bus_set_iommu(&platform_bus_type, &fsl_pamu_ops);
	bus_set_iommu(&pci_bus_type, &fsl_pamu_ops);

	return ret;
}
