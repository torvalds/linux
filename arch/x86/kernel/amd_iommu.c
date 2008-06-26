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
#include <asm/gart.h>
#include <asm/amd_iommu_types.h>

#define CMD_SET_TYPE(cmd, t) ((cmd)->data[1] |= ((t) << 28))

#define to_pages(addr, size) \
	 (round_up(((addr) & ~PAGE_MASK) + (size), PAGE_SIZE) >> PAGE_SHIFT)

static DEFINE_RWLOCK(amd_iommu_devtable_lock);

struct command {
	u32 data[4];
};

static int dma_ops_unity_map(struct dma_ops_domain *dma_dom,
			     struct unity_map_entry *e);

static int __iommu_queue_command(struct amd_iommu *iommu, struct command *cmd)
{
	u32 tail, head;
	u8 *target;

	tail = readl(iommu->mmio_base + MMIO_CMD_TAIL_OFFSET);
	target = (iommu->cmd_buf + tail);
	memcpy_toio(target, cmd, sizeof(*cmd));
	tail = (tail + sizeof(*cmd)) % iommu->cmd_buf_size;
	head = readl(iommu->mmio_base + MMIO_CMD_HEAD_OFFSET);
	if (tail == head)
		return -ENOMEM;
	writel(tail, iommu->mmio_base + MMIO_CMD_TAIL_OFFSET);

	return 0;
}

static int iommu_queue_command(struct amd_iommu *iommu, struct command *cmd)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&iommu->lock, flags);
	ret = __iommu_queue_command(iommu, cmd);
	spin_unlock_irqrestore(&iommu->lock, flags);

	return ret;
}

static int iommu_completion_wait(struct amd_iommu *iommu)
{
	int ret;
	struct command cmd;
	volatile u64 ready = 0;
	unsigned long ready_phys = virt_to_phys(&ready);

	memset(&cmd, 0, sizeof(cmd));
	cmd.data[0] = LOW_U32(ready_phys) | CMD_COMPL_WAIT_STORE_MASK;
	cmd.data[1] = HIGH_U32(ready_phys);
	cmd.data[2] = 1; /* value written to 'ready' */
	CMD_SET_TYPE(&cmd, CMD_COMPL_WAIT);

	iommu->need_sync = 0;

	ret = iommu_queue_command(iommu, &cmd);

	if (ret)
		return ret;

	while (!ready)
		cpu_relax();

	return 0;
}

static int iommu_queue_inv_dev_entry(struct amd_iommu *iommu, u16 devid)
{
	struct command cmd;

	BUG_ON(iommu == NULL);

	memset(&cmd, 0, sizeof(cmd));
	CMD_SET_TYPE(&cmd, CMD_INV_DEV_ENTRY);
	cmd.data[0] = devid;

	iommu->need_sync = 1;

	return iommu_queue_command(iommu, &cmd);
}

static int iommu_queue_inv_iommu_pages(struct amd_iommu *iommu,
		u64 address, u16 domid, int pde, int s)
{
	struct command cmd;

	memset(&cmd, 0, sizeof(cmd));
	address &= PAGE_MASK;
	CMD_SET_TYPE(&cmd, CMD_INV_IOMMU_PAGES);
	cmd.data[1] |= domid;
	cmd.data[2] = LOW_U32(address);
	cmd.data[3] = HIGH_U32(address);
	if (s)
		cmd.data[2] |= CMD_INV_IOMMU_PAGES_SIZE_MASK;
	if (pde)
		cmd.data[2] |= CMD_INV_IOMMU_PAGES_PDE_MASK;

	iommu->need_sync = 1;

	return iommu_queue_command(iommu, &cmd);
}

static int iommu_flush_pages(struct amd_iommu *iommu, u16 domid,
		u64 address, size_t size)
{
	int i;
	unsigned pages = to_pages(address, size);

	address &= PAGE_MASK;

	for (i = 0; i < pages; ++i) {
		iommu_queue_inv_iommu_pages(iommu, address, domid, 0, 0);
		address += PAGE_SIZE;
	}

	return 0;
}

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

