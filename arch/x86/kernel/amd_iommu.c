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

