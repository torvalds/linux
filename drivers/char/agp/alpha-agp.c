#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include <linux/mm.h>
#include <linux/slab.h>

#include <asm/machvec.h>
#include <asm/agp_backend.h>
#include "../../../arch/alpha/kernel/pci_impl.h"

#include "agp.h"

static int alpha_core_agp_vm_fault(struct vm_area_struct *vma,
					struct vm_fault *vmf)
{
	alpha_agp_info *agp = agp_bridge->dev_private_data;
	dma_addr_t dma_addr;
	unsigned long pa;
	struct page *page;

	dma_addr = (unsigned long)vmf->virtual_address - vma->vm_start
						+ agp->aperture.bus_base;
	pa = agp->ops->translate(agp, dma_addr);

	if (pa == (unsigned long)-EINVAL)
		return VM_FAULT_SIGBUS;	/* no translation */

	/*
	 * Get the page, inc the use count, and return it
	 */
	page = virt_to_page(__va(pa));
	get_page(page);
	vmf->page = page;
	return 0;
}

static struct aper_size_info_fixed alpha_core_agp_sizes[] =
{
	{ 0, 0, 0 }, /* filled in by alpha_core_agp_setup */
};

struct vm_operations_struct alpha_core_agp_vm_ops = {
	.fault = alpha_core_agp_vm_fault,
};


static int alpha_core_agp_fetch_size(void)
{
	return alpha_core_agp_sizes[0].size;
}

static int alpha_core_agp_configure(void)
{
	alpha_agp_info *agp = agp_bridge->dev_private_data;
	agp_bridge->gart_bus_addr = agp->aperture.bus_base;
	return 0;
}

static void alpha_core_agp_cleanup(void)
{
	alpha_agp_info *agp = agp_bridge->dev_private_data;

	agp->ops->cleanup(agp);
}

static void alpha_core_agp_tlbflush(struct agp_memory *mem)
{
	alpha_agp_info *agp = agp_bridge->dev_private_data;
	alpha_mv.mv_pci_tbi(agp->hose, 0, -1);
}

static void alpha_core_agp_enable(struct agp_bridge_data *bridge, u32 mode)
{
	alpha_agp_info *agp = bridge->dev_private_data;

	agp->mode.lw = agp_collect_device_status(bridge, mode,
					agp->capability.lw);

	agp->mode.bits.enable = 1;
	agp->ops->configure(agp);

	agp_device_command(agp->mode.lw, false);
}

static int alpha_core_agp_insert_memory(struct agp_memory *mem, off_t pg_start,
					int type)
{
	alpha_agp_info *agp = agp_bridge->dev_private_data;
	int num_entries, status;
	void *temp;

	if (type >= AGP_USER_TYPES || mem->type >= AGP_USER_TYPES)
		return -EINVAL;

	temp = agp_bridge->current_size;
	num_entries = A_SIZE_FIX(temp)->num_entries;
	if ((pg_start + mem->page_count) > num_entries)
		return -EINVAL;

	status = agp->ops->bind(agp, pg_start, mem);
	mb();
	alpha_core_agp_tlbflush(mem);

	return status;
}

static int alpha_core_agp_remove_memory(struct agp_memory *mem, off_t pg_start,
					int type)
{
	alpha_agp_info *agp = agp_bridge->dev_private_data;
	int status;

	status = agp->ops->unbind(agp, pg_start, mem);
	alpha_core_agp_tlbflush(mem);
	return status;
}

static int alpha_core_agp_create_free_gatt_table(struct agp_bridge_data *a)
{
	return 0;
}

struct agp_bridge_driver alpha_core_agp_driver = {
	.owner			= THIS_MODULE,
	.aperture_sizes		= alpha_core_agp_sizes,
	.num_aperture_sizes	= 1,
	.size_type		= FIXED_APER_SIZE,
	.cant_use_aperture	= true,
	.masks			= NULL,

	.fetch_size		= alpha_core_agp_fetch_size,
	.configure		= alpha_core_agp_configure,
	.agp_enable		= alpha_core_agp_enable,
	.cleanup		= alpha_core_agp_cleanup,
	.tlb_flush		= alpha_core_agp_tlbflush,
	.mask_memory		= agp_generic_mask_memory,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= alpha_core_agp_create_free_gatt_table,
	.free_gatt_table	= alpha_core_agp_create_free_gatt_table,
	.insert_memory		= alpha_core_agp_insert_memory,
	.remove_memory		= alpha_core_agp_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_alloc_pages	= agp_generic_alloc_pages,
	.agp_destroy_page	= agp_generic_destroy_page,
	.agp_destroy_pages	= agp_generic_destroy_pages,
	.agp_type_to_mask_type  = agp_generic_type_to_mask_type,
};

struct agp_bridge_data *alpha_bridge;

int __init
alpha_core_agp_setup(void)
{
	alpha_agp_info *agp = alpha_mv.agp_info();
	struct pci_dev *pdev;	/* faked */
	struct aper_size_info_fixed *aper_size;

	if (!agp)
		return -ENODEV;
	if (agp->ops->setup(agp))
		return -ENODEV;

	/*
	 * Build the aperture size descriptor
	 */
	aper_size = alpha_core_agp_sizes;
	aper_size->size = agp->aperture.size / (1024 * 1024);
	aper_size->num_entries = agp->aperture.size / PAGE_SIZE;
	aper_size->page_order = __ffs(aper_size->num_entries / 1024);

	/*
	 * Build a fake pci_dev struct
	 */
	pdev = alloc_pci_dev();
	if (!pdev)
		return -ENOMEM;
	pdev->vendor = 0xffff;
	pdev->device = 0xffff;
	pdev->sysdata = agp->hose;

	alpha_bridge = agp_alloc_bridge();
	if (!alpha_bridge)
		goto fail;

	alpha_bridge->driver = &alpha_core_agp_driver;
	alpha_bridge->vm_ops = &alpha_core_agp_vm_ops;
	alpha_bridge->current_size = aper_size; /* only 1 size */
	alpha_bridge->dev_private_data = agp;
	alpha_bridge->dev = pdev;
	alpha_bridge->mode = agp->capability.lw;

	printk(KERN_INFO PFX "Detected AGP on hose %d\n", agp->hose->index);
	return agp_add_bridge(alpha_bridge);

 fail:
	kfree(pdev);
	return -ENOMEM;
}

static int __init agp_alpha_core_init(void)
{
	if (agp_off)
		return -EINVAL;
	if (alpha_mv.agp_info)
		return alpha_core_agp_setup();
	return -ENODEV;
}

static void __exit agp_alpha_core_cleanup(void)
{
	agp_remove_bridge(alpha_bridge);
	agp_put_bridge(alpha_bridge);
}

module_init(agp_alpha_core_init);
module_exit(agp_alpha_core_cleanup);

MODULE_AUTHOR("Jeff Wiedemeier <Jeff.Wiedemeier@hp.com>");
MODULE_LICENSE("GPL and additional rights");
