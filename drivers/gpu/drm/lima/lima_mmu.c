// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/device.h>

#include "lima_device.h"
#include "lima_mmu.h"
#include "lima_vm.h"
#include "lima_regs.h"

#define mmu_write(reg, data) writel(data, ip->iomem + reg)
#define mmu_read(reg) readl(ip->iomem + reg)

#define lima_mmu_send_command(cmd, addr, val, cond)	     \
({							     \
	int __ret;					     \
							     \
	mmu_write(LIMA_MMU_COMMAND, cmd);		     \
	__ret = readl_poll_timeout(ip->iomem + (addr), val,  \
				  cond, 0, 100);	     \
	if (__ret)					     \
		dev_err(dev->dev,			     \
			"mmu command %x timeout\n", cmd);    \
	__ret;						     \
})

static irqreturn_t lima_mmu_irq_handler(int irq, void *data)
{
	struct lima_ip *ip = data;
	struct lima_device *dev = ip->dev;
	u32 status = mmu_read(LIMA_MMU_INT_STATUS);
	struct lima_sched_pipe *pipe;

	/* for shared irq case */
	if (!status)
		return IRQ_NONE;

	if (status & LIMA_MMU_INT_PAGE_FAULT) {
		u32 fault = mmu_read(LIMA_MMU_PAGE_FAULT_ADDR);

		dev_err(dev->dev, "mmu page fault at 0x%x from bus id %d of type %s on %s\n",
			fault, LIMA_MMU_STATUS_BUS_ID(status),
			status & LIMA_MMU_STATUS_PAGE_FAULT_IS_WRITE ? "write" : "read",
			lima_ip_name(ip));
	}

	if (status & LIMA_MMU_INT_READ_BUS_ERROR)
		dev_err(dev->dev, "mmu %s irq bus error\n", lima_ip_name(ip));

	/* mask all interrupts before resume */
	mmu_write(LIMA_MMU_INT_MASK, 0);
	mmu_write(LIMA_MMU_INT_CLEAR, status);

	pipe = dev->pipe + (ip->id == lima_ip_gpmmu ? lima_pipe_gp : lima_pipe_pp);
	lima_sched_pipe_mmu_error(pipe);

	return IRQ_HANDLED;
}

static int lima_mmu_hw_init(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	int err;
	u32 v;

	mmu_write(LIMA_MMU_COMMAND, LIMA_MMU_COMMAND_HARD_RESET);
	err = lima_mmu_send_command(LIMA_MMU_COMMAND_HARD_RESET,
				    LIMA_MMU_DTE_ADDR, v, v == 0);
	if (err)
		return err;

	mmu_write(LIMA_MMU_INT_MASK,
		  LIMA_MMU_INT_PAGE_FAULT | LIMA_MMU_INT_READ_BUS_ERROR);
	mmu_write(LIMA_MMU_DTE_ADDR, dev->empty_vm->pd.dma);
	return lima_mmu_send_command(LIMA_MMU_COMMAND_ENABLE_PAGING,
				     LIMA_MMU_STATUS, v,
				     v & LIMA_MMU_STATUS_PAGING_ENABLED);
}

int lima_mmu_resume(struct lima_ip *ip)
{
	if (ip->id == lima_ip_ppmmu_bcast)
		return 0;

	return lima_mmu_hw_init(ip);
}

void lima_mmu_suspend(struct lima_ip *ip)
{

}

int lima_mmu_init(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	int err;

	if (ip->id == lima_ip_ppmmu_bcast)
		return 0;

	mmu_write(LIMA_MMU_DTE_ADDR, 0xCAFEBABE);
	if (mmu_read(LIMA_MMU_DTE_ADDR) != 0xCAFEB000) {
		dev_err(dev->dev, "mmu %s dte write test fail\n", lima_ip_name(ip));
		return -EIO;
	}

	err = devm_request_irq(dev->dev, ip->irq, lima_mmu_irq_handler,
			       IRQF_SHARED, lima_ip_name(ip), ip);
	if (err) {
		dev_err(dev->dev, "mmu %s fail to request irq\n", lima_ip_name(ip));
		return err;
	}

	return lima_mmu_hw_init(ip);
}

void lima_mmu_fini(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;

	if (ip->id == lima_ip_ppmmu_bcast)
		return;

	devm_free_irq(dev->dev, ip->irq, ip);
}

void lima_mmu_flush_tlb(struct lima_ip *ip)
{
	mmu_write(LIMA_MMU_COMMAND, LIMA_MMU_COMMAND_ZAP_CACHE);
}

void lima_mmu_switch_vm(struct lima_ip *ip, struct lima_vm *vm)
{
	struct lima_device *dev = ip->dev;
	u32 v;

	lima_mmu_send_command(LIMA_MMU_COMMAND_ENABLE_STALL,
			      LIMA_MMU_STATUS, v,
			      v & LIMA_MMU_STATUS_STALL_ACTIVE);

	mmu_write(LIMA_MMU_DTE_ADDR, vm->pd.dma);

	/* flush the TLB */
	mmu_write(LIMA_MMU_COMMAND, LIMA_MMU_COMMAND_ZAP_CACHE);

	lima_mmu_send_command(LIMA_MMU_COMMAND_DISABLE_STALL,
			      LIMA_MMU_STATUS, v,
			      !(v & LIMA_MMU_STATUS_STALL_ACTIVE));
}

void lima_mmu_page_fault_resume(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	u32 status = mmu_read(LIMA_MMU_STATUS);
	u32 v;

	if (status & LIMA_MMU_STATUS_PAGE_FAULT_ACTIVE) {
		dev_info(dev->dev, "mmu resume\n");

		mmu_write(LIMA_MMU_INT_MASK, 0);
		mmu_write(LIMA_MMU_DTE_ADDR, 0xCAFEBABE);
		lima_mmu_send_command(LIMA_MMU_COMMAND_HARD_RESET,
				      LIMA_MMU_DTE_ADDR, v, v == 0);
		mmu_write(LIMA_MMU_INT_MASK, LIMA_MMU_INT_PAGE_FAULT | LIMA_MMU_INT_READ_BUS_ERROR);
		mmu_write(LIMA_MMU_DTE_ADDR, dev->empty_vm->pd.dma);
		lima_mmu_send_command(LIMA_MMU_COMMAND_ENABLE_PAGING,
				      LIMA_MMU_STATUS, v,
				      v & LIMA_MMU_STATUS_PAGING_ENABLED);
	}
}
