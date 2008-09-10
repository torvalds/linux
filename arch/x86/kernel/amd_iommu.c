/*
 * Copyright (C) 2007-2008 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 *         Leo Duran <leo.duran@amd.com>
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

#include <linux/pci.h>
#include <linux/gfp.h>
#include <linux/bitops.h>
#include <linux/scatterlist.h>
#include <linux/iommu-helper.h>
#include <asm/proto.h>
#include <asm/iommu.h>
#include <asm/amd_iommu_types.h>
#include <asm/amd_iommu.h>

#define CMD_SET_TYPE(cmd, t) ((cmd)->data[1] |= ((t) << 28))

#define EXIT_LOOP_COUNT 10000000

static DEFINE_RWLOCK(amd_iommu_devtable_lock);

/*
 * general struct to manage commands send to an IOMMU
 */
struct iommu_cmd {
	u32 data[4];
};

static int dma_ops_unity_map(struct dma_ops_domain *dma_dom,
			     struct unity_map_entry *e);

/* returns !0 if the IOMMU is caching non-present entries in its TLB */
static int iommu_has_npcache(struct amd_iommu *iommu)
{
	return iommu->cap & IOMMU_CAP_NPCACHE;
}

/****************************************************************************
 *
 * IOMMU command queuing functions
 *
 ****************************************************************************/

/*
 * Writes the command to the IOMMUs command buffer and informs the
 * hardware about the new command. Must be called with iommu->lock held.
 */
static int __iommu_queue_command(struct amd_iommu *iommu, struct iommu_cmd *cmd)
{
	u32 tail, head;
	u8 *target;

	tail = readl(iommu->mmio_base + MMIO_CMD_TAIL_OFFSET);
	target = iommu->cmd_buf + tail;
	memcpy_toio(target, cmd, sizeof(*cmd));
	tail = (tail + sizeof(*cmd)) % iommu->cmd_buf_size;
	head = readl(iommu->mmio_base + MMIO_CMD_HEAD_OFFSET);
	if (tail == head)
		return -ENOMEM;
	writel(tail, iommu->mmio_base + MMIO_CMD_TAIL_OFFSET);

	return 0;
}

/*
 * General queuing function for commands. Takes iommu->lock and calls
 * __iommu_queue_command().
 */
static int iommu_queue_command(struct amd_iommu *iommu, struct iommu_cmd *cmd)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&iommu->lock, flags);
	ret = __iommu_queue_command(iommu, cmd);
	spin_unlock_irqrestore(&iommu->lock, flags);

	return ret;
}

/*
 * This function is called whenever we need to ensure that the IOMMU has
 * completed execution of all commands we sent. It sends a
 * COMPLETION_WAIT command and waits for it to finish. The IOMMU informs
 * us about that by writing a value to a physical address we pass with
 * the command.
 */
static int iommu_completion_wait(struct amd_iommu *iommu)
{
	int ret, ready = 0;
	unsigned status = 0;
	struct iommu_cmd cmd;
	unsigned long i = 0;

	memset(&cmd, 0, sizeof(cmd));
	cmd.data[0] = CMD_COMPL_WAIT_INT_MASK;
	CMD_SET_TYPE(&cmd, CMD_COMPL_WAIT);

	iommu->need_sync = 0;

	ret = iommu_queue_command(iommu, &cmd);

	if (ret)
		return ret;

	while (!ready && (i < EXIT_LOOP_COUNT)) {
		++i;
		/* wait for the bit to become one */
		status = readl(iommu->mmio_base + MMIO_STATUS_OFFSET);
		ready = status & MMIO_STATUS_COM_WAIT_INT_MASK;
	}

	/* set bit back to zero */
	status &= ~MMIO_STATUS_COM_WAIT_INT_MASK;
	writel(status, iommu->mmio_base + MMIO_STATUS_OFFSET);

	if (unlikely((i == EXIT_LOOP_COUNT) && printk_ratelimit()))
		printk(KERN_WARNING "AMD IOMMU: Completion wait loop failed\n");

	return 0;
}

/*
 * Command send function for invalidating a device table entry
 */
static int iommu_queue_inv_dev_entry(struct amd_iommu *iommu, u16 devid)
{
	struct iommu_cmd cmd;

	BUG_ON(iommu == NULL);

	memset(&cmd, 0, sizeof(cmd));
	CMD_SET_TYPE(&cmd, CMD_INV_DEV_ENTRY);
	cmd.data[0] = devid;

	iommu->need_sync = 1;

	return iommu_queue_command(iommu, &cmd);
}

/*
 * Generic command send function for invalidaing TLB entries
 */
static int iommu_queue_inv_iommu_pages(struct amd_iommu *iommu,
		u64 address, u16 domid, int pde, int s)
{
	struct iommu_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	address &= PAGE_MASK;
	CMD_SET_TYPE(&cmd, CMD_INV_IOMMU_PAGES);
	cmd.data[1] |= domid;
	cmd.data[2] = lower_32_bits(address);
	cmd.data[3] = upper_32_bits(address);
	if (s) /* size bit - we flush more than one 4kb page */
		cmd.data[2] |= CMD_INV_IOMMU_PAGES_SIZE_MASK;
	if (pde) /* PDE bit - we wan't flush everything not only the PTEs */
		cmd.data[2] |= CMD_INV_IOMMU_PAGES_PDE_MASK;

	iommu->need_sync = 1;

	return iommu_queue_command(iommu, &cmd);
}

/*
 * TLB invalidation function which is called from the mapping functions.
 * It invalidates a single PTE if the range to flush is within a single
 * page. Otherwise it flushes the whole TLB of the IOMMU.
 */
static int iommu_flush_pages(struct amd_iommu *iommu, u16 domid,
		u64 address, size_t size)
{
	int s = 0;
	unsigned pages = iommu_num_pages(address, size);

	address &= PAGE_MASK;

	if (pages > 1) {
		/*
		 * If we have to flush more than one page, flush all
		 * TLB entries for this domain
		 */
		address = CMD_INV_IOMMU_ALL_PAGES_ADDRESS;
		s = 1;
	}

	iommu_queue_inv_iommu_pages(iommu, address, domid, 0, s);

	return 0;
}

/****************************************************************************
 *
 * The functions below are used the create the page table mappings for
 * unity mapped regions.
 *
 ****************************************************************************/

/*
 * Generic mapping functions. It maps a physical address into a DMA
 * address space. It allocates the page table pages if necessary.
 * In the future it can be extended to a generic mapping function
 * supporting all features of AMD IOMMU page tables like level skipping
 * and full 64 bit address spaces.
 */
static int iommu_map(struct protection_domain *dom,
		     unsigned long bus_addr,
		     unsigned long phys_addr,
		     int prot)
{
	u64 __pte, *pte, *page;

	bus_addr  = PAGE_ALIGN(bus_addr);
	phys_addr = PAGE_ALIGN(bus_addr);

	/* only support 512GB address spaces for now */
	if (bus_addr > IOMMU_MAP_SIZE_L3 || !(prot & IOMMU_PROT_MASK))
		return -EINVAL;

	pte = &dom->pt_root[IOMMU_PTE_L2_INDEX(bus_addr)];

	if (!IOMMU_PTE_PRESENT(*pte)) {
		page = (u64 *)get_zeroed_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
		*pte = IOMMU_L2_PDE(virt_to_phys(page));
	}

	pte = IOMMU_PTE_PAGE(*pte);
	pte = &pte[IOMMU_PTE_L1_INDEX(bus_addr)];

	if (!IOMMU_PTE_PRESENT(*pte)) {
		page = (u64 *)get_zeroed_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
		*pte = IOMMU_L1_PDE(virt_to_phys(page));
	}

	pte = IOMMU_PTE_PAGE(*pte);
	pte = &pte[IOMMU_PTE_L0_INDEX(bus_addr)];

	if (IOMMU_PTE_PRESENT(*pte))
		return -EBUSY;

	__pte = phys_addr | IOMMU_PTE_P;
	if (prot & IOMMU_PROT_IR)
		__pte |= IOMMU_PTE_IR;
	if (prot & IOMMU_PROT_IW)
		__pte |= IOMMU_PTE_IW;

	*pte = __pte;

	return 0;
}

/*
 * This function checks if a specific unity mapping entry is needed for
 * this specific IOMMU.
 */
static int iommu_for_unity_map(struct amd_iommu *iommu,
			       struct unity_map_entry *entry)
{
	u16 bdf, i;

	for (i = entry->devid_start; i <= entry->devid_end; ++i) {
		bdf = amd_iommu_alias_table[i];
		if (amd_iommu_rlookup_table[bdf] == iommu)
			return 1;
	}

	return 0;
}

/*
 * Init the unity mappings for a specific IOMMU in the system
 *
 * Basically iterates over all unity mapping entries and applies them to
 * the default domain DMA of that IOMMU if necessary.
 */
static int iommu_init_unity_mappings(struct amd_iommu *iommu)
{
	struct unity_map_entry *entry;
	int ret;

	list_for_each_entry(entry, &amd_iommu_unity_map, list) {
		if (!iommu_for_unity_map(iommu, entry))
			continue;
		ret = dma_ops_unity_map(iommu->default_dom, entry);
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * This function actually applies the mapping to the page table of the
 * dma_ops domain.
 */
static int dma_ops_unity_map(struct dma_ops_domain *dma_dom,
			     struct unity_map_entry *e)
{
	u64 addr;
	int ret;

	for (addr = e->address_start; addr < e->address_end;
	     addr += PAGE_SIZE) {
		ret = iommu_map(&dma_dom->domain, addr, addr, e->prot);
		if (ret)
			return ret;
		/*
		 * if unity mapping is in aperture range mark the page
		 * as allocated in the aperture
		 */
		if (addr < dma_dom->aperture_size)
			__set_bit(addr >> PAGE_SHIFT, dma_dom->bitmap);
	}

	return 0;
}

/*
 * Inits the unity mappings required for a specific device
 */
static int init_unity_mappings_for_device(struct dma_ops_domain *dma_dom,
					  u16 devid)
{
	struct unity_map_entry *e;
	int ret;

	list_for_each_entry(e, &amd_iommu_unity_map, list) {
		if (!(devid >= e->devid_start && devid <= e->devid_end))
			continue;
		ret = dma_ops_unity_map(dma_dom, e);
		if (ret)
			return ret;
	}

	return 0;
}

/****************************************************************************
 *
 * The next functions belong to the address allocator for the dma_ops
 * interface functions. They work like the allocators in the other IOMMU
 * drivers. Its basically a bitmap which marks the allocated pages in
 * the aperture. Maybe it could be enhanced in the future to a more
 * efficient allocator.
 *
 ****************************************************************************/
static unsigned long dma_mask_to_pages(unsigned long mask)
{
	return (mask >> PAGE_SHIFT) +
		(PAGE_ALIGN(mask & ~PAGE_MASK) >> PAGE_SHIFT);
}

/*
 * The address allocator core function.
 *
 * called with domain->lock held
 */
static unsigned long dma_ops_alloc_addresses(struct device *dev,
					     struct dma_ops_domain *dom,
					     unsigned int pages)
{
	unsigned long limit = dma_mask_to_pages(*dev->dma_mask);
	unsigned long address;
	unsigned long size = dom->aperture_size >> PAGE_SHIFT;
	unsigned long boundary_size;

	boundary_size = ALIGN(dma_get_seg_boundary(dev) + 1,
			PAGE_SIZE) >> PAGE_SHIFT;
	limit = limit < size ? limit : size;

	if (dom->next_bit >= limit)
		dom->next_bit = 0;

	address = iommu_area_alloc(dom->bitmap, limit, dom->next_bit, pages,
			0 , boundary_size, 0);
	if (address == -1)
		address = iommu_area_alloc(dom->bitmap, limit, 0, pages,
				0, boundary_size, 0);

	if (likely(address != -1)) {
		dom->next_bit = address + pages;
		address <<= PAGE_SHIFT;
	} else
		address = bad_dma_address;

	WARN_ON((address + (PAGE_SIZE*pages)) > dom->aperture_size);

	return address;
}

/*
 * The address free function.
 *
 * called with domain->lock held
 */
static void dma_ops_free_addresses(struct dma_ops_domain *dom,
				   unsigned long address,
				   unsigned int pages)
{
	address >>= PAGE_SHIFT;
	iommu_area_free(dom->bitmap, address, pages);
}

/****************************************************************************
 *
 * The next functions belong to the domain allocation. A domain is
 * allocated for every IOMMU as the default domain. If device isolation
 * is enabled, every device get its own domain. The most important thing
 * about domains is the page table mapping the DMA address space they
 * contain.
 *
 ****************************************************************************/

static u16 domain_id_alloc(void)
{
	unsigned long flags;
	int id;

	write_lock_irqsave(&amd_iommu_devtable_lock, flags);
	id = find_first_zero_bit(amd_iommu_pd_alloc_bitmap, MAX_DOMAIN_ID);
	BUG_ON(id == 0);
	if (id > 0 && id < MAX_DOMAIN_ID)
		__set_bit(id, amd_iommu_pd_alloc_bitmap);
	else
		id = 0;
	write_unlock_irqrestore(&amd_iommu_devtable_lock, flags);

	return id;
}

/*
 * Used to reserve address ranges in the aperture (e.g. for exclusion
 * ranges.
 */
static void dma_ops_reserve_addresses(struct dma_ops_domain *dom,
				      unsigned long start_page,
				      unsigned int pages)
{
	unsigned int last_page = dom->aperture_size >> PAGE_SHIFT;

	if (start_page + pages > last_page)
		pages = last_page - start_page;

	set_bit_string(dom->bitmap, start_page, pages);
}

static void dma_ops_free_pagetable(struct dma_ops_domain *dma_dom)
{
	int i, j;
	u64 *p1, *p2, *p3;

	p1 = dma_dom->domain.pt_root;

	if (!p1)
		return;

	for (i = 0; i < 512; ++i) {
		if (!IOMMU_PTE_PRESENT(p1[i]))
			continue;

		p2 = IOMMU_PTE_PAGE(p1[i]);
		for (j = 0; j < 512; ++i) {
			if (!IOMMU_PTE_PRESENT(p2[j]))
				continue;
			p3 = IOMMU_PTE_PAGE(p2[j]);
			free_page((unsigned long)p3);
		}

		free_page((unsigned long)p2);
	}

	free_page((unsigned long)p1);
}

/*
 * Free a domain, only used if something went wrong in the
 * allocation path and we need to free an already allocated page table
 */
static void dma_ops_domain_free(struct dma_ops_domain *dom)
{
	if (!dom)
		return;

	dma_ops_free_pagetable(dom);

	kfree(dom->pte_pages);

	kfree(dom->bitmap);

	kfree(dom);
}

/*
 * Allocates a new protection domain usable for the dma_ops functions.
 * It also intializes the page table and the address allocator data
 * structures required for the dma_ops interface
 */
static struct dma_ops_domain *dma_ops_domain_alloc(struct amd_iommu *iommu,
						   unsigned order)
{
	struct dma_ops_domain *dma_dom;
	unsigned i, num_pte_pages;
	u64 *l2_pde;
	u64 address;

	/*
	 * Currently the DMA aperture must be between 32 MB and 1GB in size
	 */
	if ((order < 25) || (order > 30))
		return NULL;

	dma_dom = kzalloc(sizeof(struct dma_ops_domain), GFP_KERNEL);
	if (!dma_dom)
		return NULL;

	spin_lock_init(&dma_dom->domain.lock);

	dma_dom->domain.id = domain_id_alloc();
	if (dma_dom->domain.id == 0)
		goto free_dma_dom;
	dma_dom->domain.mode = PAGE_MODE_3_LEVEL;
	dma_dom->domain.pt_root = (void *)get_zeroed_page(GFP_KERNEL);
	dma_dom->domain.priv = dma_dom;
	if (!dma_dom->domain.pt_root)
		goto free_dma_dom;
	dma_dom->aperture_size = (1ULL << order);
	dma_dom->bitmap = kzalloc(dma_dom->aperture_size / (PAGE_SIZE * 8),
				  GFP_KERNEL);
	if (!dma_dom->bitmap)
		goto free_dma_dom;
	/*
	 * mark the first page as allocated so we never return 0 as
	 * a valid dma-address. So we can use 0 as error value
	 */
	dma_dom->bitmap[0] = 1;
	dma_dom->next_bit = 0;

	/* Intialize the exclusion range if necessary */
	if (iommu->exclusion_start &&
	    iommu->exclusion_start < dma_dom->aperture_size) {
		unsigned long startpage = iommu->exclusion_start >> PAGE_SHIFT;
		int pages = iommu_num_pages(iommu->exclusion_start,
					    iommu->exclusion_length);
		dma_ops_reserve_addresses(dma_dom, startpage, pages);
	}

	/*
	 * At the last step, build the page tables so we don't need to
	 * allocate page table pages in the dma_ops mapping/unmapping
	 * path.
	 */
	num_pte_pages = dma_dom->aperture_size / (PAGE_SIZE * 512);
	dma_dom->pte_pages = kzalloc(num_pte_pages * sizeof(void *),
			GFP_KERNEL);
	if (!dma_dom->pte_pages)
		goto free_dma_dom;

	l2_pde = (u64 *)get_zeroed_page(GFP_KERNEL);
	if (l2_pde == NULL)
		goto free_dma_dom;

	dma_dom->domain.pt_root[0] = IOMMU_L2_PDE(virt_to_phys(l2_pde));

	for (i = 0; i < num_pte_pages; ++i) {
		dma_dom->pte_pages[i] = (u64 *)get_zeroed_page(GFP_KERNEL);
		if (!dma_dom->pte_pages[i])
			goto free_dma_dom;
		address = virt_to_phys(dma_dom->pte_pages[i]);
		l2_pde[i] = IOMMU_L1_PDE(address);
	}

	return dma_dom;

free_dma_dom:
	dma_ops_domain_free(dma_dom);

	return NULL;
}

/*
 * Find out the protection domain structure for a given PCI device. This
 * will give us the pointer to the page table root for example.
 */
static struct protection_domain *domain_for_device(u16 devid)
{
	struct protection_domain *dom;
	unsigned long flags;

	read_lock_irqsave(&amd_iommu_devtable_lock, flags);
	dom = amd_iommu_pd_table[devid];
	read_unlock_irqrestore(&amd_iommu_devtable_lock, flags);

	return dom;
}

/*
 * If a device is not yet associated with a domain, this function does
 * assigns it visible for the hardware
 */
static void set_device_domain(struct amd_iommu *iommu,
			      struct protection_domain *domain,
			      u16 devid)
{
	unsigned long flags;

	u64 pte_root = virt_to_phys(domain->pt_root);

	pte_root |= (domain->mode & 0x07) << 9;
	pte_root |= IOMMU_PTE_IR | IOMMU_PTE_IW | IOMMU_PTE_P | 2;

	write_lock_irqsave(&amd_iommu_devtable_lock, flags);
	amd_iommu_dev_table[devid].data[0] = pte_root;
	amd_iommu_dev_table[devid].data[1] = pte_root >> 32;
	amd_iommu_dev_table[devid].data[2] = domain->id;

	amd_iommu_pd_table[devid] = domain;
	write_unlock_irqrestore(&amd_iommu_devtable_lock, flags);

	iommu_queue_inv_dev_entry(iommu, devid);

	iommu->need_sync = 1;
}

/*****************************************************************************
 *
 * The next functions belong to the dma_ops mapping/unmapping code.
 *
 *****************************************************************************/

/*
 * In the dma_ops path we only have the struct device. This function
 * finds the corresponding IOMMU, the protection domain and the
 * requestor id for a given device.
 * If the device is not yet associated with a domain this is also done
 * in this function.
 */
static int get_device_resources(struct device *dev,
				struct amd_iommu **iommu,
				struct protection_domain **domain,
				u16 *bdf)
{
	struct dma_ops_domain *dma_dom;
	struct pci_dev *pcidev;
	u16 _bdf;

	BUG_ON(!dev || dev->bus != &pci_bus_type || !dev->dma_mask);

	pcidev = to_pci_dev(dev);
	_bdf = calc_devid(pcidev->bus->number, pcidev->devfn);

	/* device not translated by any IOMMU in the system? */
	if (_bdf > amd_iommu_last_bdf) {
		*iommu = NULL;
		*domain = NULL;
		*bdf = 0xffff;
		return 0;
	}

	*bdf = amd_iommu_alias_table[_bdf];

	*iommu = amd_iommu_rlookup_table[*bdf];
	if (*iommu == NULL)
		return 0;
	dma_dom = (*iommu)->default_dom;
	*domain = domain_for_device(*bdf);
	if (*domain == NULL) {
		*domain = &dma_dom->domain;
		set_device_domain(*iommu, *domain, *bdf);
		printk(KERN_INFO "AMD IOMMU: Using protection domain %d for "
				"device ", (*domain)->id);
		print_devid(_bdf, 1);
	}

	return 1;
}

/*
 * This is the generic map function. It maps one 4kb page at paddr to
 * the given address in the DMA address space for the domain.
 */
static dma_addr_t dma_ops_domain_map(struct amd_iommu *iommu,
				     struct dma_ops_domain *dom,
				     unsigned long address,
				     phys_addr_t paddr,
				     int direction)
{
	u64 *pte, __pte;

	WARN_ON(address > dom->aperture_size);

	paddr &= PAGE_MASK;

	pte  = dom->pte_pages[IOMMU_PTE_L1_INDEX(address)];
	pte += IOMMU_PTE_L0_INDEX(address);

	__pte = paddr | IOMMU_PTE_P | IOMMU_PTE_FC;

	if (direction == DMA_TO_DEVICE)
		__pte |= IOMMU_PTE_IR;
	else if (direction == DMA_FROM_DEVICE)
		__pte |= IOMMU_PTE_IW;
	else if (direction == DMA_BIDIRECTIONAL)
		__pte |= IOMMU_PTE_IR | IOMMU_PTE_IW;

	WARN_ON(*pte);

	*pte = __pte;

	return (dma_addr_t)address;
}

/*
 * The generic unmapping function for on page in the DMA address space.
 */
static void dma_ops_domain_unmap(struct amd_iommu *iommu,
				 struct dma_ops_domain *dom,
				 unsigned long address)
{
	u64 *pte;

	if (address >= dom->aperture_size)
		return;

	WARN_ON(address & 0xfffULL || address > dom->aperture_size);

	pte  = dom->pte_pages[IOMMU_PTE_L1_INDEX(address)];
	pte += IOMMU_PTE_L0_INDEX(address);

	WARN_ON(!*pte);

	*pte = 0ULL;
}

/*
 * This function contains common code for mapping of a physically
 * contiguous memory region into DMA address space. It is uses by all
 * mapping functions provided by this IOMMU driver.
 * Must be called with the domain lock held.
 */
static dma_addr_t __map_single(struct device *dev,
			       struct amd_iommu *iommu,
			       struct dma_ops_domain *dma_dom,
			       phys_addr_t paddr,
			       size_t size,
			       int dir)
{
	dma_addr_t offset = paddr & ~PAGE_MASK;
	dma_addr_t address, start;
	unsigned int pages;
	int i;

	pages = iommu_num_pages(paddr, size);
	paddr &= PAGE_MASK;

	address = dma_ops_alloc_addresses(dev, dma_dom, pages);
	if (unlikely(address == bad_dma_address))
		goto out;

	start = address;
	for (i = 0; i < pages; ++i) {
		dma_ops_domain_map(iommu, dma_dom, start, paddr, dir);
		paddr += PAGE_SIZE;
		start += PAGE_SIZE;
	}
	address += offset;

out:
	return address;
}

/*
 * Does the reverse of the __map_single function. Must be called with
 * the domain lock held too
 */
static void __unmap_single(struct amd_iommu *iommu,
			   struct dma_ops_domain *dma_dom,
			   dma_addr_t dma_addr,
			   size_t size,
			   int dir)
{
	dma_addr_t i, start;
	unsigned int pages;

	if ((dma_addr == 0) || (dma_addr + size > dma_dom->aperture_size))
		return;

	pages = iommu_num_pages(dma_addr, size);
	dma_addr &= PAGE_MASK;
	start = dma_addr;

	for (i = 0; i < pages; ++i) {
		dma_ops_domain_unmap(iommu, dma_dom, start);
		start += PAGE_SIZE;
	}

	dma_ops_free_addresses(dma_dom, dma_addr, pages);
}

/*
 * The exported map_single function for dma_ops.
 */
static dma_addr_t map_single(struct device *dev, phys_addr_t paddr,
			     size_t size, int dir)
{
	unsigned long flags;
	struct amd_iommu *iommu;
	struct protection_domain *domain;
	u16 devid;
	dma_addr_t addr;

	get_device_resources(dev, &iommu, &domain, &devid);

	if (iommu == NULL || domain == NULL)
		/* device not handled by any AMD IOMMU */
		return (dma_addr_t)paddr;

	spin_lock_irqsave(&domain->lock, flags);
	addr = __map_single(dev, iommu, domain->priv, paddr, size, dir);
	if (addr == bad_dma_address)
		goto out;

	if (iommu_has_npcache(iommu))
		iommu_flush_pages(iommu, domain->id, addr, size);

	if (iommu->need_sync)
		iommu_completion_wait(iommu);

out:
	spin_unlock_irqrestore(&domain->lock, flags);

	return addr;
}

/*
 * The exported unmap_single function for dma_ops.
 */
static void unmap_single(struct device *dev, dma_addr_t dma_addr,
			 size_t size, int dir)
{
	unsigned long flags;
	struct amd_iommu *iommu;
	struct protection_domain *domain;
	u16 devid;

	if (!get_device_resources(dev, &iommu, &domain, &devid))
		/* device not handled by any AMD IOMMU */
		return;

	spin_lock_irqsave(&domain->lock, flags);

	__unmap_single(iommu, domain->priv, dma_addr, size, dir);

	iommu_flush_pages(iommu, domain->id, dma_addr, size);

	if (iommu->need_sync)
		iommu_completion_wait(iommu);

	spin_unlock_irqrestore(&domain->lock, flags);
}

/*
 * This is a special map_sg function which is used if we should map a
 * device which is not handled by an AMD IOMMU in the system.
 */
static int map_sg_no_iommu(struct device *dev, struct scatterlist *sglist,
			   int nelems, int dir)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sglist, s, nelems, i) {
		s->dma_address = (dma_addr_t)sg_phys(s);
		s->dma_length  = s->length;
	}

	return nelems;
}

/*
 * The exported map_sg function for dma_ops (handles scatter-gather
 * lists).
 */
static int map_sg(struct device *dev, struct scatterlist *sglist,
		  int nelems, int dir)
{
	unsigned long flags;
	struct amd_iommu *iommu;
	struct protection_domain *domain;
	u16 devid;
	int i;
	struct scatterlist *s;
	phys_addr_t paddr;
	int mapped_elems = 0;

	get_device_resources(dev, &iommu, &domain, &devid);

	if (!iommu || !domain)
		return map_sg_no_iommu(dev, sglist, nelems, dir);

	spin_lock_irqsave(&domain->lock, flags);

	for_each_sg(sglist, s, nelems, i) {
		paddr = sg_phys(s);

		s->dma_address = __map_single(dev, iommu, domain->priv,
					      paddr, s->length, dir);

		if (s->dma_address) {
			s->dma_length = s->length;
			mapped_elems++;
		} else
			goto unmap;
		if (iommu_has_npcache(iommu))
			iommu_flush_pages(iommu, domain->id, s->dma_address,
					  s->dma_length);
	}

	if (iommu->need_sync)
		iommu_completion_wait(iommu);

out:
	spin_unlock_irqrestore(&domain->lock, flags);

	return mapped_elems;
unmap:
	for_each_sg(sglist, s, mapped_elems, i) {
		if (s->dma_address)
			__unmap_single(iommu, domain->priv, s->dma_address,
				       s->dma_length, dir);
		s->dma_address = s->dma_length = 0;
	}

	mapped_elems = 0;

	goto out;
}

/*
 * The exported map_sg function for dma_ops (handles scatter-gather
 * lists).
 */
static void unmap_sg(struct device *dev, struct scatterlist *sglist,
		     int nelems, int dir)
{
	unsigned long flags;
	struct amd_iommu *iommu;
	struct protection_domain *domain;
	struct scatterlist *s;
	u16 devid;
	int i;

	if (!get_device_resources(dev, &iommu, &domain, &devid))
		return;

	spin_lock_irqsave(&domain->lock, flags);

	for_each_sg(sglist, s, nelems, i) {
		__unmap_single(iommu, domain->priv, s->dma_address,
			       s->dma_length, dir);
		iommu_flush_pages(iommu, domain->id, s->dma_address,
				  s->dma_length);
		s->dma_address = s->dma_length = 0;
	}

	if (iommu->need_sync)
		iommu_completion_wait(iommu);

	spin_unlock_irqrestore(&domain->lock, flags);
}

/*
 * The exported alloc_coherent function for dma_ops.
 */
static void *alloc_coherent(struct device *dev, size_t size,
			    dma_addr_t *dma_addr, gfp_t flag)
{
	unsigned long flags;
	void *virt_addr;
	struct amd_iommu *iommu;
	struct protection_domain *domain;
	u16 devid;
	phys_addr_t paddr;

	virt_addr = (void *)__get_free_pages(flag, get_order(size));
	if (!virt_addr)
		return 0;

	memset(virt_addr, 0, size);
	paddr = virt_to_phys(virt_addr);

	get_device_resources(dev, &iommu, &domain, &devid);

	if (!iommu || !domain) {
		*dma_addr = (dma_addr_t)paddr;
		return virt_addr;
	}

	spin_lock_irqsave(&domain->lock, flags);

	*dma_addr = __map_single(dev, iommu, domain->priv, paddr,
				 size, DMA_BIDIRECTIONAL);

	if (*dma_addr == bad_dma_address) {
		free_pages((unsigned long)virt_addr, get_order(size));
		virt_addr = NULL;
		goto out;
	}

	if (iommu_has_npcache(iommu))
		iommu_flush_pages(iommu, domain->id, *dma_addr, size);

	if (iommu->need_sync)
		iommu_completion_wait(iommu);

out:
	spin_unlock_irqrestore(&domain->lock, flags);

	return virt_addr;
}

/*
 * The exported free_coherent function for dma_ops.
 */
static void free_coherent(struct device *dev, size_t size,
			  void *virt_addr, dma_addr_t dma_addr)
{
	unsigned long flags;
	struct amd_iommu *iommu;
	struct protection_domain *domain;
	u16 devid;

	get_device_resources(dev, &iommu, &domain, &devid);

	if (!iommu || !domain)
		goto free_mem;

	spin_lock_irqsave(&domain->lock, flags);

	__unmap_single(iommu, domain->priv, dma_addr, size, DMA_BIDIRECTIONAL);
	iommu_flush_pages(iommu, domain->id, dma_addr, size);

	if (iommu->need_sync)
		iommu_completion_wait(iommu);

	spin_unlock_irqrestore(&domain->lock, flags);

free_mem:
	free_pages((unsigned long)virt_addr, get_order(size));
}

/*
 * The function for pre-allocating protection domains.
 *
 * If the driver core informs the DMA layer if a driver grabs a device
 * we don't need to preallocate the protection domains anymore.
 * For now we have to.
 */
void prealloc_protection_domains(void)
{
	struct pci_dev *dev = NULL;
	struct dma_ops_domain *dma_dom;
	struct amd_iommu *iommu;
	int order = amd_iommu_aperture_order;
	u16 devid;

	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		devid = (dev->bus->number << 8) | dev->devfn;
		if (devid > amd_iommu_last_bdf)
			continue;
		devid = amd_iommu_alias_table[devid];
		if (domain_for_device(devid))
			continue;
		iommu = amd_iommu_rlookup_table[devid];
		if (!iommu)
			continue;
		dma_dom = dma_ops_domain_alloc(iommu, order);
		if (!dma_dom)
			continue;
		init_unity_mappings_for_device(dma_dom, devid);
		set_device_domain(iommu, &dma_dom->domain, devid);
		printk(KERN_INFO "AMD IOMMU: Allocated domain %d for device ",
		       dma_dom->domain.id);
		print_devid(devid, 1);
	}
}

static struct dma_mapping_ops amd_iommu_dma_ops = {
	.alloc_coherent = alloc_coherent,
	.free_coherent = free_coherent,
	.map_single = map_single,
	.unmap_single = unmap_single,
	.map_sg = map_sg,
	.unmap_sg = unmap_sg,
};

/*
 * The function which clues the AMD IOMMU driver into dma_ops.
 */
int __init amd_iommu_init_dma_ops(void)
{
	struct amd_iommu *iommu;
	int order = amd_iommu_aperture_order;
	int ret;

	/*
	 * first allocate a default protection domain for every IOMMU we
	 * found in the system. Devices not assigned to any other
	 * protection domain will be assigned to the default one.
	 */
	list_for_each_entry(iommu, &amd_iommu_list, list) {
		iommu->default_dom = dma_ops_domain_alloc(iommu, order);
		if (iommu->default_dom == NULL)
			return -ENOMEM;
		ret = iommu_init_unity_mappings(iommu);
		if (ret)
			goto free_domains;
	}

	/*
	 * If device isolation is enabled, pre-allocate the protection
	 * domains for each device.
	 */
	if (amd_iommu_isolate)
		prealloc_protection_domains();

	iommu_detected = 1;
	force_iommu = 1;
	bad_dma_address = 0;
#ifdef CONFIG_GART_IOMMU
	gart_iommu_aperture_disabled = 1;
	gart_iommu_aperture = 0;
#endif

	/* Make the driver finally visible to the drivers */
	dma_ops = &amd_iommu_dma_ops;

	return 0;

free_domains:

	list_for_each_entry(iommu, &amd_iommu_list, list) {
		if (iommu->default_dom)
			dma_ops_domain_free(iommu->default_dom);
	}

	return ret;
}
