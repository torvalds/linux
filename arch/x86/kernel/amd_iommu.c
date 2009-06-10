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
#include <linux/debugfs.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/iommu-helper.h>
#include <linux/iommu.h>
#include <asm/proto.h>
#include <asm/iommu.h>
#include <asm/gart.h>
#include <asm/amd_iommu_types.h>
#include <asm/amd_iommu.h>

#define CMD_SET_TYPE(cmd, t) ((cmd)->data[1] |= ((t) << 28))

#define EXIT_LOOP_COUNT 10000000

static DEFINE_RWLOCK(amd_iommu_devtable_lock);

/* A list of preallocated protection domains */
static LIST_HEAD(iommu_pd_list);
static DEFINE_SPINLOCK(iommu_pd_list_lock);

#ifdef CONFIG_IOMMU_API
static struct iommu_ops amd_iommu_ops;
#endif

/*
 * general struct to manage commands send to an IOMMU
 */
struct iommu_cmd {
	u32 data[4];
};

static int dma_ops_unity_map(struct dma_ops_domain *dma_dom,
			     struct unity_map_entry *e);
static struct dma_ops_domain *find_protection_domain(u16 devid);


#ifdef CONFIG_AMD_IOMMU_STATS

/*
 * Initialization code for statistics collection
 */

DECLARE_STATS_COUNTER(compl_wait);
DECLARE_STATS_COUNTER(cnt_map_single);
DECLARE_STATS_COUNTER(cnt_unmap_single);
DECLARE_STATS_COUNTER(cnt_map_sg);
DECLARE_STATS_COUNTER(cnt_unmap_sg);
DECLARE_STATS_COUNTER(cnt_alloc_coherent);
DECLARE_STATS_COUNTER(cnt_free_coherent);
DECLARE_STATS_COUNTER(cross_page);
DECLARE_STATS_COUNTER(domain_flush_single);
DECLARE_STATS_COUNTER(domain_flush_all);
DECLARE_STATS_COUNTER(alloced_io_mem);
DECLARE_STATS_COUNTER(total_map_requests);

static struct dentry *stats_dir;
static struct dentry *de_isolate;
static struct dentry *de_fflush;

static void amd_iommu_stats_add(struct __iommu_counter *cnt)
{
	if (stats_dir == NULL)
		return;

	cnt->dent = debugfs_create_u64(cnt->name, 0444, stats_dir,
				       &cnt->value);
}

static void amd_iommu_stats_init(void)
{
	stats_dir = debugfs_create_dir("amd-iommu", NULL);
	if (stats_dir == NULL)
		return;

	de_isolate = debugfs_create_bool("isolation", 0444, stats_dir,
					 (u32 *)&amd_iommu_isolate);

	de_fflush  = debugfs_create_bool("fullflush", 0444, stats_dir,
					 (u32 *)&amd_iommu_unmap_flush);

	amd_iommu_stats_add(&compl_wait);
	amd_iommu_stats_add(&cnt_map_single);
	amd_iommu_stats_add(&cnt_unmap_single);
	amd_iommu_stats_add(&cnt_map_sg);
	amd_iommu_stats_add(&cnt_unmap_sg);
	amd_iommu_stats_add(&cnt_alloc_coherent);
	amd_iommu_stats_add(&cnt_free_coherent);
	amd_iommu_stats_add(&cross_page);
	amd_iommu_stats_add(&domain_flush_single);
	amd_iommu_stats_add(&domain_flush_all);
	amd_iommu_stats_add(&alloced_io_mem);
	amd_iommu_stats_add(&total_map_requests);
}

#endif

/* returns !0 if the IOMMU is caching non-present entries in its TLB */
static int iommu_has_npcache(struct amd_iommu *iommu)
{
	return iommu->cap & (1UL << IOMMU_CAP_NPCACHE);
}

/****************************************************************************
 *
 * Interrupt handling functions
 *
 ****************************************************************************/

static void iommu_print_event(void *__evt)
{
	u32 *event = __evt;
	int type  = (event[1] >> EVENT_TYPE_SHIFT)  & EVENT_TYPE_MASK;
	int devid = (event[0] >> EVENT_DEVID_SHIFT) & EVENT_DEVID_MASK;
	int domid = (event[1] >> EVENT_DOMID_SHIFT) & EVENT_DOMID_MASK;
	int flags = (event[1] >> EVENT_FLAGS_SHIFT) & EVENT_FLAGS_MASK;
	u64 address = (u64)(((u64)event[3]) << 32) | event[2];

	printk(KERN_ERR "AMD IOMMU: Event logged [");

	switch (type) {
	case EVENT_TYPE_ILL_DEV:
		printk("ILLEGAL_DEV_TABLE_ENTRY device=%02x:%02x.%x "
		       "address=0x%016llx flags=0x%04x]\n",
		       PCI_BUS(devid), PCI_SLOT(devid), PCI_FUNC(devid),
		       address, flags);
		break;
	case EVENT_TYPE_IO_FAULT:
		printk("IO_PAGE_FAULT device=%02x:%02x.%x "
		       "domain=0x%04x address=0x%016llx flags=0x%04x]\n",
		       PCI_BUS(devid), PCI_SLOT(devid), PCI_FUNC(devid),
		       domid, address, flags);
		break;
	case EVENT_TYPE_DEV_TAB_ERR:
		printk("DEV_TAB_HARDWARE_ERROR device=%02x:%02x.%x "
		       "address=0x%016llx flags=0x%04x]\n",
		       PCI_BUS(devid), PCI_SLOT(devid), PCI_FUNC(devid),
		       address, flags);
		break;
	case EVENT_TYPE_PAGE_TAB_ERR:
		printk("PAGE_TAB_HARDWARE_ERROR device=%02x:%02x.%x "
		       "domain=0x%04x address=0x%016llx flags=0x%04x]\n",
		       PCI_BUS(devid), PCI_SLOT(devid), PCI_FUNC(devid),
		       domid, address, flags);
		break;
	case EVENT_TYPE_ILL_CMD:
		printk("ILLEGAL_COMMAND_ERROR address=0x%016llx]\n", address);
		break;
	case EVENT_TYPE_CMD_HARD_ERR:
		printk("COMMAND_HARDWARE_ERROR address=0x%016llx "
		       "flags=0x%04x]\n", address, flags);
		break;
	case EVENT_TYPE_IOTLB_INV_TO:
		printk("IOTLB_INV_TIMEOUT device=%02x:%02x.%x "
		       "address=0x%016llx]\n",
		       PCI_BUS(devid), PCI_SLOT(devid), PCI_FUNC(devid),
		       address);
		break;
	case EVENT_TYPE_INV_DEV_REQ:
		printk("INVALID_DEVICE_REQUEST device=%02x:%02x.%x "
		       "address=0x%016llx flags=0x%04x]\n",
		       PCI_BUS(devid), PCI_SLOT(devid), PCI_FUNC(devid),
		       address, flags);
		break;
	default:
		printk(KERN_ERR "UNKNOWN type=0x%02x]\n", type);
	}
}

static void iommu_poll_events(struct amd_iommu *iommu)
{
	u32 head, tail;
	unsigned long flags;

	spin_lock_irqsave(&iommu->lock, flags);

	head = readl(iommu->mmio_base + MMIO_EVT_HEAD_OFFSET);
	tail = readl(iommu->mmio_base + MMIO_EVT_TAIL_OFFSET);

	while (head != tail) {
		iommu_print_event(iommu->evt_buf + head);
		head = (head + EVENT_ENTRY_SIZE) % iommu->evt_buf_size;
	}

	writel(head, iommu->mmio_base + MMIO_EVT_HEAD_OFFSET);

	spin_unlock_irqrestore(&iommu->lock, flags);
}

irqreturn_t amd_iommu_int_handler(int irq, void *data)
{
	struct amd_iommu *iommu;

	list_for_each_entry(iommu, &amd_iommu_list, list)
		iommu_poll_events(iommu);

	return IRQ_HANDLED;
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
	if (!ret)
		iommu->need_sync = true;
	spin_unlock_irqrestore(&iommu->lock, flags);

	return ret;
}

/*
 * This function waits until an IOMMU has completed a completion
 * wait command
 */
static void __iommu_wait_for_completion(struct amd_iommu *iommu)
{
	int ready = 0;
	unsigned status = 0;
	unsigned long i = 0;

	INC_STATS_COUNTER(compl_wait);

	while (!ready && (i < EXIT_LOOP_COUNT)) {
		++i;
		/* wait for the bit to become one */
		status = readl(iommu->mmio_base + MMIO_STATUS_OFFSET);
		ready = status & MMIO_STATUS_COM_WAIT_INT_MASK;
	}

	/* set bit back to zero */
	status &= ~MMIO_STATUS_COM_WAIT_INT_MASK;
	writel(status, iommu->mmio_base + MMIO_STATUS_OFFSET);

	if (unlikely(i == EXIT_LOOP_COUNT))
		panic("AMD IOMMU: Completion wait loop failed\n");
}

/*
 * This function queues a completion wait command into the command
 * buffer of an IOMMU
 */
static int __iommu_completion_wait(struct amd_iommu *iommu)
{
	struct iommu_cmd cmd;

	 memset(&cmd, 0, sizeof(cmd));
	 cmd.data[0] = CMD_COMPL_WAIT_INT_MASK;
	 CMD_SET_TYPE(&cmd, CMD_COMPL_WAIT);

	 return __iommu_queue_command(iommu, &cmd);
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
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&iommu->lock, flags);

	if (!iommu->need_sync)
		goto out;

	ret = __iommu_completion_wait(iommu);

	iommu->need_sync = false;

	if (ret)
		goto out;

	__iommu_wait_for_completion(iommu);

out:
	spin_unlock_irqrestore(&iommu->lock, flags);

	return 0;
}

/*
 * Command send function for invalidating a device table entry
 */
static int iommu_queue_inv_dev_entry(struct amd_iommu *iommu, u16 devid)
{
	struct iommu_cmd cmd;
	int ret;

	BUG_ON(iommu == NULL);

	memset(&cmd, 0, sizeof(cmd));
	CMD_SET_TYPE(&cmd, CMD_INV_DEV_ENTRY);
	cmd.data[0] = devid;

	ret = iommu_queue_command(iommu, &cmd);

	return ret;
}

static void __iommu_build_inv_iommu_pages(struct iommu_cmd *cmd, u64 address,
					  u16 domid, int pde, int s)
{
	memset(cmd, 0, sizeof(*cmd));
	address &= PAGE_MASK;
	CMD_SET_TYPE(cmd, CMD_INV_IOMMU_PAGES);
	cmd->data[1] |= domid;
	cmd->data[2] = lower_32_bits(address);
	cmd->data[3] = upper_32_bits(address);
	if (s) /* size bit - we flush more than one 4kb page */
		cmd->data[2] |= CMD_INV_IOMMU_PAGES_SIZE_MASK;
	if (pde) /* PDE bit - we wan't flush everything not only the PTEs */
		cmd->data[2] |= CMD_INV_IOMMU_PAGES_PDE_MASK;
}

/*
 * Generic command send function for invalidaing TLB entries
 */
static int iommu_queue_inv_iommu_pages(struct amd_iommu *iommu,
		u64 address, u16 domid, int pde, int s)
{
	struct iommu_cmd cmd;
	int ret;

	__iommu_build_inv_iommu_pages(&cmd, address, domid, pde, s);

	ret = iommu_queue_command(iommu, &cmd);

	return ret;
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
	unsigned pages = iommu_num_pages(address, size, PAGE_SIZE);

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

/* Flush the whole IO/TLB for a given protection domain */
static void iommu_flush_tlb(struct amd_iommu *iommu, u16 domid)
{
	u64 address = CMD_INV_IOMMU_ALL_PAGES_ADDRESS;

	INC_STATS_COUNTER(domain_flush_single);

	iommu_queue_inv_iommu_pages(iommu, address, domid, 0, 1);
}

/*
 * This function is used to flush the IO/TLB for a given protection domain
 * on every IOMMU in the system
 */
static void iommu_flush_domain(u16 domid)
{
	unsigned long flags;
	struct amd_iommu *iommu;
	struct iommu_cmd cmd;

	INC_STATS_COUNTER(domain_flush_all);

	__iommu_build_inv_iommu_pages(&cmd, CMD_INV_IOMMU_ALL_PAGES_ADDRESS,
				      domid, 1, 1);

	list_for_each_entry(iommu, &amd_iommu_list, list) {
		spin_lock_irqsave(&iommu->lock, flags);
		__iommu_queue_command(iommu, &cmd);
		__iommu_completion_wait(iommu);
		__iommu_wait_for_completion(iommu);
		spin_unlock_irqrestore(&iommu->lock, flags);
	}
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
static int iommu_map_page(struct protection_domain *dom,
			  unsigned long bus_addr,
			  unsigned long phys_addr,
			  int prot)
{
	u64 __pte, *pte, *page;

	bus_addr  = PAGE_ALIGN(bus_addr);
	phys_addr = PAGE_ALIGN(phys_addr);

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

static void iommu_unmap_page(struct protection_domain *dom,
			     unsigned long bus_addr)
{
	u64 *pte;

	pte = &dom->pt_root[IOMMU_PTE_L2_INDEX(bus_addr)];

	if (!IOMMU_PTE_PRESENT(*pte))
		return;

	pte = IOMMU_PTE_PAGE(*pte);
	pte = &pte[IOMMU_PTE_L1_INDEX(bus_addr)];

	if (!IOMMU_PTE_PRESENT(*pte))
		return;

	pte = IOMMU_PTE_PAGE(*pte);
	pte = &pte[IOMMU_PTE_L1_INDEX(bus_addr)];

	*pte = 0;
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
		ret = iommu_map_page(&dma_dom->domain, addr, addr, e->prot);
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

/*
 * The address allocator core function.
 *
 * called with domain->lock held
 */
static unsigned long dma_ops_alloc_addresses(struct device *dev,
					     struct dma_ops_domain *dom,
					     unsigned int pages,
					     unsigned long align_mask,
					     u64 dma_mask)
{
	unsigned long limit;
	unsigned long address;
	unsigned long boundary_size;

	boundary_size = ALIGN(dma_get_seg_boundary(dev) + 1,
			PAGE_SIZE) >> PAGE_SHIFT;
	limit = iommu_device_max_index(dom->aperture_size >> PAGE_SHIFT, 0,
				       dma_mask >> PAGE_SHIFT);

	if (dom->next_bit >= limit) {
		dom->next_bit = 0;
		dom->need_flush = true;
	}

	address = iommu_area_alloc(dom->bitmap, limit, dom->next_bit, pages,
				   0 , boundary_size, align_mask);
	if (address == -1) {
		address = iommu_area_alloc(dom->bitmap, limit, 0, pages,
				0, boundary_size, align_mask);
		dom->need_flush = true;
	}

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

	if (address >= dom->next_bit)
		dom->need_flush = true;
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

static void domain_id_free(int id)
{
	unsigned long flags;

	write_lock_irqsave(&amd_iommu_devtable_lock, flags);
	if (id > 0 && id < MAX_DOMAIN_ID)
		__clear_bit(id, amd_iommu_pd_alloc_bitmap);
	write_unlock_irqrestore(&amd_iommu_devtable_lock, flags);
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

	iommu_area_reserve(dom->bitmap, start_page, pages);
}

static void free_pagetable(struct protection_domain *domain)
{
	int i, j;
	u64 *p1, *p2, *p3;

	p1 = domain->pt_root;

	if (!p1)
		return;

	for (i = 0; i < 512; ++i) {
		if (!IOMMU_PTE_PRESENT(p1[i]))
			continue;

		p2 = IOMMU_PTE_PAGE(p1[i]);
		for (j = 0; j < 512; ++j) {
			if (!IOMMU_PTE_PRESENT(p2[j]))
				continue;
			p3 = IOMMU_PTE_PAGE(p2[j]);
			free_page((unsigned long)p3);
		}

		free_page((unsigned long)p2);
	}

	free_page((unsigned long)p1);

	domain->pt_root = NULL;
}

/*
 * Free a domain, only used if something went wrong in the
 * allocation path and we need to free an already allocated page table
 */
static void dma_ops_domain_free(struct dma_ops_domain *dom)
{
	if (!dom)
		return;

	free_pagetable(&dom->domain);

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
	dma_dom->domain.flags = PD_DMA_OPS_MASK;
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

	dma_dom->need_flush = false;
	dma_dom->target_dev = 0xffff;

	/* Intialize the exclusion range if necessary */
	if (iommu->exclusion_start &&
	    iommu->exclusion_start < dma_dom->aperture_size) {
		unsigned long startpage = iommu->exclusion_start >> PAGE_SHIFT;
		int pages = iommu_num_pages(iommu->exclusion_start,
					    iommu->exclusion_length,
					    PAGE_SIZE);
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
 * little helper function to check whether a given protection domain is a
 * dma_ops domain
 */
static bool dma_ops_domain(struct protection_domain *domain)
{
	return domain->flags & PD_DMA_OPS_MASK;
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
static void attach_device(struct amd_iommu *iommu,
			  struct protection_domain *domain,
			  u16 devid)
{
	unsigned long flags;
	u64 pte_root = virt_to_phys(domain->pt_root);

	domain->dev_cnt += 1;

	pte_root |= (domain->mode & DEV_ENTRY_MODE_MASK)
		    << DEV_ENTRY_MODE_SHIFT;
	pte_root |= IOMMU_PTE_IR | IOMMU_PTE_IW | IOMMU_PTE_P | IOMMU_PTE_TV;

	write_lock_irqsave(&amd_iommu_devtable_lock, flags);
	amd_iommu_dev_table[devid].data[0] = lower_32_bits(pte_root);
	amd_iommu_dev_table[devid].data[1] = upper_32_bits(pte_root);
	amd_iommu_dev_table[devid].data[2] = domain->id;

	amd_iommu_pd_table[devid] = domain;
	write_unlock_irqrestore(&amd_iommu_devtable_lock, flags);

	iommu_queue_inv_dev_entry(iommu, devid);
}

/*
 * Removes a device from a protection domain (unlocked)
 */
static void __detach_device(struct protection_domain *domain, u16 devid)
{

	/* lock domain */
	spin_lock(&domain->lock);

	/* remove domain from the lookup table */
	amd_iommu_pd_table[devid] = NULL;

	/* remove entry from the device table seen by the hardware */
	amd_iommu_dev_table[devid].data[0] = IOMMU_PTE_P | IOMMU_PTE_TV;
	amd_iommu_dev_table[devid].data[1] = 0;
	amd_iommu_dev_table[devid].data[2] = 0;

	/* decrease reference counter */
	domain->dev_cnt -= 1;

	/* ready */
	spin_unlock(&domain->lock);
}

/*
 * Removes a device from a protection domain (with devtable_lock held)
 */
static void detach_device(struct protection_domain *domain, u16 devid)
{
	unsigned long flags;

	/* lock device table */
	write_lock_irqsave(&amd_iommu_devtable_lock, flags);
	__detach_device(domain, devid);
	write_unlock_irqrestore(&amd_iommu_devtable_lock, flags);
}

static int device_change_notifier(struct notifier_block *nb,
				  unsigned long action, void *data)
{
	struct device *dev = data;
	struct pci_dev *pdev = to_pci_dev(dev);
	u16 devid = calc_devid(pdev->bus->number, pdev->devfn);
	struct protection_domain *domain;
	struct dma_ops_domain *dma_domain;
	struct amd_iommu *iommu;
	int order = amd_iommu_aperture_order;
	unsigned long flags;

	if (devid > amd_iommu_last_bdf)
		goto out;

	devid = amd_iommu_alias_table[devid];

	iommu = amd_iommu_rlookup_table[devid];
	if (iommu == NULL)
		goto out;

	domain = domain_for_device(devid);

	if (domain && !dma_ops_domain(domain))
		WARN_ONCE(1, "AMD IOMMU WARNING: device %s already bound "
			  "to a non-dma-ops domain\n", dev_name(dev));

	switch (action) {
	case BUS_NOTIFY_BOUND_DRIVER:
		if (domain)
			goto out;
		dma_domain = find_protection_domain(devid);
		if (!dma_domain)
			dma_domain = iommu->default_dom;
		attach_device(iommu, &dma_domain->domain, devid);
		printk(KERN_INFO "AMD IOMMU: Using protection domain %d for "
		       "device %s\n", dma_domain->domain.id, dev_name(dev));
		break;
	case BUS_NOTIFY_UNBIND_DRIVER:
		if (!domain)
			goto out;
		detach_device(domain, devid);
		break;
	case BUS_NOTIFY_ADD_DEVICE:
		/* allocate a protection domain if a device is added */
		dma_domain = find_protection_domain(devid);
		if (dma_domain)
			goto out;
		dma_domain = dma_ops_domain_alloc(iommu, order);
		if (!dma_domain)
			goto out;
		dma_domain->target_dev = devid;

		spin_lock_irqsave(&iommu_pd_list_lock, flags);
		list_add_tail(&dma_domain->list, &iommu_pd_list);
		spin_unlock_irqrestore(&iommu_pd_list_lock, flags);

		break;
	default:
		goto out;
	}

	iommu_queue_inv_dev_entry(iommu, devid);
	iommu_completion_wait(iommu);

out:
	return 0;
}

struct notifier_block device_nb = {
	.notifier_call = device_change_notifier,
};

/*****************************************************************************
 *
 * The next functions belong to the dma_ops mapping/unmapping code.
 *
 *****************************************************************************/

/*
 * This function checks if the driver got a valid device from the caller to
 * avoid dereferencing invalid pointers.
 */
static bool check_device(struct device *dev)
{
	if (!dev || !dev->dma_mask)
		return false;

	return true;
}

/*
 * In this function the list of preallocated protection domains is traversed to
 * find the domain for a specific device
 */
static struct dma_ops_domain *find_protection_domain(u16 devid)
{
	struct dma_ops_domain *entry, *ret = NULL;
	unsigned long flags;

	if (list_empty(&iommu_pd_list))
		return NULL;

	spin_lock_irqsave(&iommu_pd_list_lock, flags);

	list_for_each_entry(entry, &iommu_pd_list, list) {
		if (entry->target_dev == devid) {
			ret = entry;
			break;
		}
	}

	spin_unlock_irqrestore(&iommu_pd_list_lock, flags);

	return ret;
}

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

	*iommu = NULL;
	*domain = NULL;
	*bdf = 0xffff;

	if (dev->bus != &pci_bus_type)
		return 0;

	pcidev = to_pci_dev(dev);
	_bdf = calc_devid(pcidev->bus->number, pcidev->devfn);

	/* device not translated by any IOMMU in the system? */
	if (_bdf > amd_iommu_last_bdf)
		return 0;

	*bdf = amd_iommu_alias_table[_bdf];

	*iommu = amd_iommu_rlookup_table[*bdf];
	if (*iommu == NULL)
		return 0;
	*domain = domain_for_device(*bdf);
	if (*domain == NULL) {
		dma_dom = find_protection_domain(*bdf);
		if (!dma_dom)
			dma_dom = (*iommu)->default_dom;
		*domain = &dma_dom->domain;
		attach_device(*iommu, *domain, *bdf);
		printk(KERN_INFO "AMD IOMMU: Using protection domain %d for "
				"device %s\n", (*domain)->id, dev_name(dev));
	}

	if (domain_for_device(_bdf) == NULL)
		attach_device(*iommu, *domain, _bdf);

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

	WARN_ON(address & ~PAGE_MASK || address >= dom->aperture_size);

	pte  = dom->pte_pages[IOMMU_PTE_L1_INDEX(address)];
	pte += IOMMU_PTE_L0_INDEX(address);

	WARN_ON(!*pte);

	*pte = 0ULL;
}

/*
 * This function contains common code for mapping of a physically
 * contiguous memory region into DMA address space. It is used by all
 * mapping functions provided with this IOMMU driver.
 * Must be called with the domain lock held.
 */
static dma_addr_t __map_single(struct device *dev,
			       struct amd_iommu *iommu,
			       struct dma_ops_domain *dma_dom,
			       phys_addr_t paddr,
			       size_t size,
			       int dir,
			       bool align,
			       u64 dma_mask)
{
	dma_addr_t offset = paddr & ~PAGE_MASK;
	dma_addr_t address, start;
	unsigned int pages;
	unsigned long align_mask = 0;
	int i;

	pages = iommu_num_pages(paddr, size, PAGE_SIZE);
	paddr &= PAGE_MASK;

	INC_STATS_COUNTER(total_map_requests);

	if (pages > 1)
		INC_STATS_COUNTER(cross_page);

	if (align)
		align_mask = (1UL << get_order(size)) - 1;

	address = dma_ops_alloc_addresses(dev, dma_dom, pages, align_mask,
					  dma_mask);
	if (unlikely(address == bad_dma_address))
		goto out;

	start = address;
	for (i = 0; i < pages; ++i) {
		dma_ops_domain_map(iommu, dma_dom, start, paddr, dir);
		paddr += PAGE_SIZE;
		start += PAGE_SIZE;
	}
	address += offset;

	ADD_STATS_COUNTER(alloced_io_mem, size);

	if (unlikely(dma_dom->need_flush && !amd_iommu_unmap_flush)) {
		iommu_flush_tlb(iommu, dma_dom->domain.id);
		dma_dom->need_flush = false;
	} else if (unlikely(iommu_has_npcache(iommu)))
		iommu_flush_pages(iommu, dma_dom->domain.id, address, size);

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

	if ((dma_addr == bad_dma_address) ||
	    (dma_addr + size > dma_dom->aperture_size))
		return;

	pages = iommu_num_pages(dma_addr, size, PAGE_SIZE);
	dma_addr &= PAGE_MASK;
	start = dma_addr;

	for (i = 0; i < pages; ++i) {
		dma_ops_domain_unmap(iommu, dma_dom, start);
		start += PAGE_SIZE;
	}

	SUB_STATS_COUNTER(alloced_io_mem, size);

	dma_ops_free_addresses(dma_dom, dma_addr, pages);

	if (amd_iommu_unmap_flush || dma_dom->need_flush) {
		iommu_flush_pages(iommu, dma_dom->domain.id, dma_addr, size);
		dma_dom->need_flush = false;
	}
}

/*
 * The exported map_single function for dma_ops.
 */
static dma_addr_t map_page(struct device *dev, struct page *page,
			   unsigned long offset, size_t size,
			   enum dma_data_direction dir,
			   struct dma_attrs *attrs)
{
	unsigned long flags;
	struct amd_iommu *iommu;
	struct protection_domain *domain;
	u16 devid;
	dma_addr_t addr;
	u64 dma_mask;
	phys_addr_t paddr = page_to_phys(page) + offset;

	INC_STATS_COUNTER(cnt_map_single);

	if (!check_device(dev))
		return bad_dma_address;

	dma_mask = *dev->dma_mask;

	get_device_resources(dev, &iommu, &domain, &devid);

	if (iommu == NULL || domain == NULL)
		/* device not handled by any AMD IOMMU */
		return (dma_addr_t)paddr;

	if (!dma_ops_domain(domain))
		return bad_dma_address;

	spin_lock_irqsave(&domain->lock, flags);
	addr = __map_single(dev, iommu, domain->priv, paddr, size, dir, false,
			    dma_mask);
	if (addr == bad_dma_address)
		goto out;

	iommu_completion_wait(iommu);

out:
	spin_unlock_irqrestore(&domain->lock, flags);

	return addr;
}

/*
 * The exported unmap_single function for dma_ops.
 */
static void unmap_page(struct device *dev, dma_addr_t dma_addr, size_t size,
		       enum dma_data_direction dir, struct dma_attrs *attrs)
{
	unsigned long flags;
	struct amd_iommu *iommu;
	struct protection_domain *domain;
	u16 devid;

	INC_STATS_COUNTER(cnt_unmap_single);

	if (!check_device(dev) ||
	    !get_device_resources(dev, &iommu, &domain, &devid))
		/* device not handled by any AMD IOMMU */
		return;

	if (!dma_ops_domain(domain))
		return;

	spin_lock_irqsave(&domain->lock, flags);

	__unmap_single(iommu, domain->priv, dma_addr, size, dir);

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
		  int nelems, enum dma_data_direction dir,
		  struct dma_attrs *attrs)
{
	unsigned long flags;
	struct amd_iommu *iommu;
	struct protection_domain *domain;
	u16 devid;
	int i;
	struct scatterlist *s;
	phys_addr_t paddr;
	int mapped_elems = 0;
	u64 dma_mask;

	INC_STATS_COUNTER(cnt_map_sg);

	if (!check_device(dev))
		return 0;

	dma_mask = *dev->dma_mask;

	get_device_resources(dev, &iommu, &domain, &devid);

	if (!iommu || !domain)
		return map_sg_no_iommu(dev, sglist, nelems, dir);

	if (!dma_ops_domain(domain))
		return 0;

	spin_lock_irqsave(&domain->lock, flags);

	for_each_sg(sglist, s, nelems, i) {
		paddr = sg_phys(s);

		s->dma_address = __map_single(dev, iommu, domain->priv,
					      paddr, s->length, dir, false,
					      dma_mask);

		if (s->dma_address) {
			s->dma_length = s->length;
			mapped_elems++;
		} else
			goto unmap;
	}

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
		     int nelems, enum dma_data_direction dir,
		     struct dma_attrs *attrs)
{
	unsigned long flags;
	struct amd_iommu *iommu;
	struct protection_domain *domain;
	struct scatterlist *s;
	u16 devid;
	int i;

	INC_STATS_COUNTER(cnt_unmap_sg);

	if (!check_device(dev) ||
	    !get_device_resources(dev, &iommu, &domain, &devid))
		return;

	if (!dma_ops_domain(domain))
		return;

	spin_lock_irqsave(&domain->lock, flags);

	for_each_sg(sglist, s, nelems, i) {
		__unmap_single(iommu, domain->priv, s->dma_address,
			       s->dma_length, dir);
		s->dma_address = s->dma_length = 0;
	}

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
	u64 dma_mask = dev->coherent_dma_mask;

	INC_STATS_COUNTER(cnt_alloc_coherent);

	if (!check_device(dev))
		return NULL;

	if (!get_device_resources(dev, &iommu, &domain, &devid))
		flag &= ~(__GFP_DMA | __GFP_HIGHMEM | __GFP_DMA32);

	flag |= __GFP_ZERO;
	virt_addr = (void *)__get_free_pages(flag, get_order(size));
	if (!virt_addr)
		return 0;

	paddr = virt_to_phys(virt_addr);

	if (!iommu || !domain) {
		*dma_addr = (dma_addr_t)paddr;
		return virt_addr;
	}

	if (!dma_ops_domain(domain))
		goto out_free;

	if (!dma_mask)
		dma_mask = *dev->dma_mask;

	spin_lock_irqsave(&domain->lock, flags);

	*dma_addr = __map_single(dev, iommu, domain->priv, paddr,
				 size, DMA_BIDIRECTIONAL, true, dma_mask);

	if (*dma_addr == bad_dma_address)
		goto out_free;

	iommu_completion_wait(iommu);

	spin_unlock_irqrestore(&domain->lock, flags);

	return virt_addr;

out_free:

	free_pages((unsigned long)virt_addr, get_order(size));

	return NULL;
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

	INC_STATS_COUNTER(cnt_free_coherent);

	if (!check_device(dev))
		return;

	get_device_resources(dev, &iommu, &domain, &devid);

	if (!iommu || !domain)
		goto free_mem;

	if (!dma_ops_domain(domain))
		goto free_mem;

	spin_lock_irqsave(&domain->lock, flags);

	__unmap_single(iommu, domain->priv, dma_addr, size, DMA_BIDIRECTIONAL);

	iommu_completion_wait(iommu);

	spin_unlock_irqrestore(&domain->lock, flags);

free_mem:
	free_pages((unsigned long)virt_addr, get_order(size));
}

/*
 * This function is called by the DMA layer to find out if we can handle a
 * particular device. It is part of the dma_ops.
 */
static int amd_iommu_dma_supported(struct device *dev, u64 mask)
{
	u16 bdf;
	struct pci_dev *pcidev;

	/* No device or no PCI device */
	if (!dev || dev->bus != &pci_bus_type)
		return 0;

	pcidev = to_pci_dev(dev);

	bdf = calc_devid(pcidev->bus->number, pcidev->devfn);

	/* Out of our scope? */
	if (bdf > amd_iommu_last_bdf)
		return 0;

	return 1;
}

/*
 * The function for pre-allocating protection domains.
 *
 * If the driver core informs the DMA layer if a driver grabs a device
 * we don't need to preallocate the protection domains anymore.
 * For now we have to.
 */
static void prealloc_protection_domains(void)
{
	struct pci_dev *dev = NULL;
	struct dma_ops_domain *dma_dom;
	struct amd_iommu *iommu;
	int order = amd_iommu_aperture_order;
	u16 devid;

	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		devid = calc_devid(dev->bus->number, dev->devfn);
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
		dma_dom->target_dev = devid;

		list_add_tail(&dma_dom->list, &iommu_pd_list);
	}
}

static struct dma_map_ops amd_iommu_dma_ops = {
	.alloc_coherent = alloc_coherent,
	.free_coherent = free_coherent,
	.map_page = map_page,
	.unmap_page = unmap_page,
	.map_sg = map_sg,
	.unmap_sg = unmap_sg,
	.dma_supported = amd_iommu_dma_supported,
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
		iommu->default_dom->domain.flags |= PD_DEFAULT_MASK;
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

	register_iommu(&amd_iommu_ops);

	bus_register_notifier(&pci_bus_type, &device_nb);

	amd_iommu_stats_init();

	return 0;

free_domains:

	list_for_each_entry(iommu, &amd_iommu_list, list) {
		if (iommu->default_dom)
			dma_ops_domain_free(iommu->default_dom);
	}

	return ret;
}

/*****************************************************************************
 *
 * The following functions belong to the exported interface of AMD IOMMU
 *
 * This interface allows access to lower level functions of the IOMMU
 * like protection domain handling and assignement of devices to domains
 * which is not possible with the dma_ops interface.
 *
 *****************************************************************************/

static void cleanup_domain(struct protection_domain *domain)
{
	unsigned long flags;
	u16 devid;

	write_lock_irqsave(&amd_iommu_devtable_lock, flags);

	for (devid = 0; devid <= amd_iommu_last_bdf; ++devid)
		if (amd_iommu_pd_table[devid] == domain)
			__detach_device(domain, devid);

	write_unlock_irqrestore(&amd_iommu_devtable_lock, flags);
}

static int amd_iommu_domain_init(struct iommu_domain *dom)
{
	struct protection_domain *domain;

	domain = kzalloc(sizeof(*domain), GFP_KERNEL);
	if (!domain)
		return -ENOMEM;

	spin_lock_init(&domain->lock);
	domain->mode = PAGE_MODE_3_LEVEL;
	domain->id = domain_id_alloc();
	if (!domain->id)
		goto out_free;
	domain->pt_root = (void *)get_zeroed_page(GFP_KERNEL);
	if (!domain->pt_root)
		goto out_free;

	dom->priv = domain;

	return 0;

out_free:
	kfree(domain);

	return -ENOMEM;
}

static void amd_iommu_domain_destroy(struct iommu_domain *dom)
{
	struct protection_domain *domain = dom->priv;

	if (!domain)
		return;

	if (domain->dev_cnt > 0)
		cleanup_domain(domain);

	BUG_ON(domain->dev_cnt != 0);

	free_pagetable(domain);

	domain_id_free(domain->id);

	kfree(domain);

	dom->priv = NULL;
}

static void amd_iommu_detach_device(struct iommu_domain *dom,
				    struct device *dev)
{
	struct protection_domain *domain = dom->priv;
	struct amd_iommu *iommu;
	struct pci_dev *pdev;
	u16 devid;

	if (dev->bus != &pci_bus_type)
		return;

	pdev = to_pci_dev(dev);

	devid = calc_devid(pdev->bus->number, pdev->devfn);

	if (devid > 0)
		detach_device(domain, devid);

	iommu = amd_iommu_rlookup_table[devid];
	if (!iommu)
		return;

	iommu_queue_inv_dev_entry(iommu, devid);
	iommu_completion_wait(iommu);
}

static int amd_iommu_attach_device(struct iommu_domain *dom,
				   struct device *dev)
{
	struct protection_domain *domain = dom->priv;
	struct protection_domain *old_domain;
	struct amd_iommu *iommu;
	struct pci_dev *pdev;
	u16 devid;

	if (dev->bus != &pci_bus_type)
		return -EINVAL;

	pdev = to_pci_dev(dev);

	devid = calc_devid(pdev->bus->number, pdev->devfn);

	if (devid >= amd_iommu_last_bdf ||
			devid != amd_iommu_alias_table[devid])
		return -EINVAL;

	iommu = amd_iommu_rlookup_table[devid];
	if (!iommu)
		return -EINVAL;

	old_domain = domain_for_device(devid);
	if (old_domain)
		return -EBUSY;

	attach_device(iommu, domain, devid);

	iommu_completion_wait(iommu);

	return 0;
}

static int amd_iommu_map_range(struct iommu_domain *dom,
			       unsigned long iova, phys_addr_t paddr,
			       size_t size, int iommu_prot)
{
	struct protection_domain *domain = dom->priv;
	unsigned long i,  npages = iommu_num_pages(paddr, size, PAGE_SIZE);
	int prot = 0;
	int ret;

	if (iommu_prot & IOMMU_READ)
		prot |= IOMMU_PROT_IR;
	if (iommu_prot & IOMMU_WRITE)
		prot |= IOMMU_PROT_IW;

	iova  &= PAGE_MASK;
	paddr &= PAGE_MASK;

	for (i = 0; i < npages; ++i) {
		ret = iommu_map_page(domain, iova, paddr, prot);
		if (ret)
			return ret;

		iova  += PAGE_SIZE;
		paddr += PAGE_SIZE;
	}

	return 0;
}

static void amd_iommu_unmap_range(struct iommu_domain *dom,
				  unsigned long iova, size_t size)
{

	struct protection_domain *domain = dom->priv;
	unsigned long i,  npages = iommu_num_pages(iova, size, PAGE_SIZE);

	iova  &= PAGE_MASK;

	for (i = 0; i < npages; ++i) {
		iommu_unmap_page(domain, iova);
		iova  += PAGE_SIZE;
	}

	iommu_flush_domain(domain->id);
}

static phys_addr_t amd_iommu_iova_to_phys(struct iommu_domain *dom,
					  unsigned long iova)
{
	struct protection_domain *domain = dom->priv;
	unsigned long offset = iova & ~PAGE_MASK;
	phys_addr_t paddr;
	u64 *pte;

	pte = &domain->pt_root[IOMMU_PTE_L2_INDEX(iova)];

	if (!IOMMU_PTE_PRESENT(*pte))
		return 0;

	pte = IOMMU_PTE_PAGE(*pte);
	pte = &pte[IOMMU_PTE_L1_INDEX(iova)];

	if (!IOMMU_PTE_PRESENT(*pte))
		return 0;

	pte = IOMMU_PTE_PAGE(*pte);
	pte = &pte[IOMMU_PTE_L0_INDEX(iova)];

	if (!IOMMU_PTE_PRESENT(*pte))
		return 0;

	paddr  = *pte & IOMMU_PAGE_MASK;
	paddr |= offset;

	return paddr;
}

static int amd_iommu_domain_has_cap(struct iommu_domain *domain,
				    unsigned long cap)
{
	return 0;
}

static struct iommu_ops amd_iommu_ops = {
	.domain_init = amd_iommu_domain_init,
	.domain_destroy = amd_iommu_domain_destroy,
	.attach_dev = amd_iommu_attach_device,
	.detach_dev = amd_iommu_detach_device,
	.map = amd_iommu_map_range,
	.unmap = amd_iommu_unmap_range,
	.iova_to_phys = amd_iommu_iova_to_phys,
	.domain_has_cap = amd_iommu_domain_has_cap,
};

