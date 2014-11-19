#include <linux/cpu.h>
#include <linux/dma-mapping.h>
#include <linux/gfp.h>
#include <linux/highmem.h>

#include <xen/features.h>

static DEFINE_PER_CPU(unsigned long, xen_mm32_scratch_virt);
static DEFINE_PER_CPU(pte_t *, xen_mm32_scratch_ptep);

static int alloc_xen_mm32_scratch_page(int cpu)
{
	struct page *page;
	unsigned long virt;
	pmd_t *pmdp;
	pte_t *ptep;

	if (per_cpu(xen_mm32_scratch_ptep, cpu) != NULL)
		return 0;

	page = alloc_page(GFP_KERNEL);
	if (page == NULL) {
		pr_warn("Failed to allocate xen_mm32_scratch_page for cpu %d\n", cpu);
		return -ENOMEM;
	}

	virt = (unsigned long)__va(page_to_phys(page));
	pmdp = pmd_offset(pud_offset(pgd_offset_k(virt), virt), virt);
	ptep = pte_offset_kernel(pmdp, virt);

	per_cpu(xen_mm32_scratch_virt, cpu) = virt;
	per_cpu(xen_mm32_scratch_ptep, cpu) = ptep;

	return 0;
}

static int xen_mm32_cpu_notify(struct notifier_block *self,
				    unsigned long action, void *hcpu)
{
	int cpu = (long)hcpu;
	switch (action) {
	case CPU_UP_PREPARE:
		if (alloc_xen_mm32_scratch_page(cpu))
			return NOTIFY_BAD;
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block xen_mm32_cpu_notifier = {
	.notifier_call	= xen_mm32_cpu_notify,
};

static void* xen_mm32_remap_page(dma_addr_t handle)
{
	unsigned long virt = get_cpu_var(xen_mm32_scratch_virt);
	pte_t *ptep = __get_cpu_var(xen_mm32_scratch_ptep);

	*ptep = pfn_pte(handle >> PAGE_SHIFT, PAGE_KERNEL);
	local_flush_tlb_kernel_page(virt);

	return (void*)virt;
}

static void xen_mm32_unmap(void *vaddr)
{
	put_cpu_var(xen_mm32_scratch_virt);
}


/* functions called by SWIOTLB */

static void dma_cache_maint(dma_addr_t handle, unsigned long offset,
	size_t size, enum dma_data_direction dir,
	void (*op)(const void *, size_t, int))
{
	unsigned long pfn;
	size_t left = size;

	pfn = (handle >> PAGE_SHIFT) + offset / PAGE_SIZE;
	offset %= PAGE_SIZE;

	do {
		size_t len = left;
		void *vaddr;
	
		if (!pfn_valid(pfn))
		{
			/* Cannot map the page, we don't know its physical address.
			 * Return and hope for the best */
			if (!xen_feature(XENFEAT_grant_map_identity))
				return;
			vaddr = xen_mm32_remap_page(handle) + offset;
			op(vaddr, len, dir);
			xen_mm32_unmap(vaddr - offset);
		} else {
			struct page *page = pfn_to_page(pfn);

			if (PageHighMem(page)) {
				if (len + offset > PAGE_SIZE)
					len = PAGE_SIZE - offset;

				if (cache_is_vipt_nonaliasing()) {
					vaddr = kmap_atomic(page);
					op(vaddr + offset, len, dir);
					kunmap_atomic(vaddr);
				} else {
					vaddr = kmap_high_get(page);
					if (vaddr) {
						op(vaddr + offset, len, dir);
						kunmap_high(page);
					}
				}
			} else {
				vaddr = page_address(page) + offset;
				op(vaddr, len, dir);
			}
		}

		offset = 0;
		pfn++;
		left -= len;
	} while (left);
}

static void __xen_dma_page_dev_to_cpu(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
	/* Cannot use __dma_page_dev_to_cpu because we don't have a
	 * struct page for handle */

	if (dir != DMA_TO_DEVICE)
		outer_inv_range(handle, handle + size);

	dma_cache_maint(handle & PAGE_MASK, handle & ~PAGE_MASK, size, dir, dmac_unmap_area);
}

static void __xen_dma_page_cpu_to_dev(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{

	dma_cache_maint(handle & PAGE_MASK, handle & ~PAGE_MASK, size, dir, dmac_map_area);

	if (dir == DMA_FROM_DEVICE) {
		outer_inv_range(handle, handle + size);
	} else {
		outer_clean_range(handle, handle + size);
	}
}

void xen_dma_unmap_page(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir,
		struct dma_attrs *attrs)

{
	if (!__generic_dma_ops(hwdev)->unmap_page)
		return;
	if (dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
		return;

	__xen_dma_page_dev_to_cpu(hwdev, handle, size, dir);
}

void xen_dma_sync_single_for_cpu(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	if (!__generic_dma_ops(hwdev)->sync_single_for_cpu)
		return;
	__xen_dma_page_dev_to_cpu(hwdev, handle, size, dir);
}

void xen_dma_sync_single_for_device(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	if (!__generic_dma_ops(hwdev)->sync_single_for_device)
		return;
	__xen_dma_page_cpu_to_dev(hwdev, handle, size, dir);
}

int __init xen_mm32_init(void)
{
	int cpu;

	if (!xen_initial_domain())
		return 0;

	register_cpu_notifier(&xen_mm32_cpu_notifier);
	get_online_cpus();
	for_each_online_cpu(cpu) {
		if (alloc_xen_mm32_scratch_page(cpu)) {
			put_online_cpus();
			unregister_cpu_notifier(&xen_mm32_cpu_notifier);
			return -ENOMEM;
		}
	}
	put_online_cpus();

	return 0;
}
arch_initcall(xen_mm32_init);
