/*
 * Copyright (C) 2007-2008 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/bug.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/iommu.h>

static struct iommu_ops *iommu_ops;

void register_iommu(struct iommu_ops *ops)
{
	if (iommu_ops)
		BUG();

	iommu_ops = ops;
}

bool iommu_found(void)
{
	return iommu_ops != NULL;
}
EXPORT_SYMBOL_GPL(iommu_found);

struct iommu_domain *iommu_domain_alloc(void)
{
	struct iommu_domain *domain;
	int ret;

	domain = kmalloc(sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return NULL;

	ret = iommu_ops->domain_init(domain);
	if (ret)
		goto out_free;

	return domain;

out_free:
	kfree(domain);

	return NULL;
}
EXPORT_SYMBOL_GPL(iommu_domain_alloc);

void iommu_domain_free(struct iommu_domain *domain)
{
	iommu_ops->domain_destroy(domain);
	kfree(domain);
}
EXPORT_SYMBOL_GPL(iommu_domain_free);

int iommu_attach_device(struct iommu_domain *domain, struct device *dev)
{
	return iommu_ops->attach_dev(domain, dev);
}
EXPORT_SYMBOL_GPL(iommu_attach_device);

void iommu_detach_device(struct iommu_domain *domain, struct device *dev)
{
	iommu_ops->detach_dev(domain, dev);
}
EXPORT_SYMBOL_GPL(iommu_detach_device);

int iommu_map_range(struct iommu_domain *domain, unsigned long iova,
		    phys_addr_t paddr, size_t size, int prot)
{
	return iommu_ops->map(domain, iova, paddr, size, prot);
}
EXPORT_SYMBOL_GPL(iommu_map_range);

void iommu_unmap_range(struct iommu_domain *domain, unsigned long iova,
		      size_t size)
{
	iommu_ops->unmap(domain, iova, size);
}
EXPORT_SYMBOL_GPL(iommu_unmap_range);

phys_addr_t iommu_iova_to_phys(struct iommu_domain *domain,
			       unsigned long iova)
{
	return iommu_ops->iova_to_phys(domain, iova);
}
EXPORT_SYMBOL_GPL(iommu_iova_to_phys);

int iommu_domain_has_cap(struct iommu_domain *domain,
			 unsigned long cap)
{
	return iommu_ops->domain_has_cap(domain, cap);
}
EXPORT_SYMBOL_GPL(iommu_domain_has_cap);
