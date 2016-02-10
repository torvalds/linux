/*
 * Coherency fabric (Aurora) support for Armada 370 and XP platforms.
 *
 * Copyright (C) 2012 Marvell
 *
 * Yehuda Yitschak <yehuday@marvell.com>
 * Gregory Clement <gregory.clement@free-electrons.com>
 * Thomas Petazzoni <thomas.petazzoni@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * The Armada 370 and Armada XP SOCs have a coherency fabric which is
 * responsible for ensuring hardware coherency between all CPUs and between
 * CPUs and I/O masters. This file initializes the coherency fabric and
 * supplies basic routines for configuring and controlling hardware coherency
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <asm/smp_plat.h>
#include "armada-370-xp.h"

/*
 * Some functions in this file are called very early during SMP
 * initialization. At that time the device tree framework is not yet
 * ready, and it is not possible to get the register address to
 * ioremap it. That's why the pointer below is given with an initial
 * value matching its virtual mapping
 */
static void __iomem *coherency_base = ARMADA_370_XP_REGS_VIRT_BASE + 0x20200;
static void __iomem *coherency_cpu_base;

/* Coherency fabric registers */
#define COHERENCY_FABRIC_CFG_OFFSET		   0x4

#define IO_SYNC_BARRIER_CTL_OFFSET		   0x0

static struct of_device_id of_coherency_table[] = {
	{.compatible = "marvell,coherency-fabric"},
	{ /* end of list */ },
};

#ifdef CONFIG_SMP
int coherency_get_cpu_count(void)
{
	int reg, cnt;

	reg = readl(coherency_base + COHERENCY_FABRIC_CFG_OFFSET);
	cnt = (reg & 0xF) + 1;

	return cnt;
}
#endif

/* Function defined in coherency_ll.S */
int ll_set_cpu_coherent(void __iomem *base_addr, unsigned int hw_cpu_id);

int set_cpu_coherent(unsigned int hw_cpu_id, int smp_group_id)
{
	if (!coherency_base) {
		pr_warn("Can't make CPU %d cache coherent.\n", hw_cpu_id);
		pr_warn("Coherency fabric is not initialized\n");
		return 1;
	}

	return ll_set_cpu_coherent(coherency_base, hw_cpu_id);
}

static inline void mvebu_hwcc_sync_io_barrier(void)
{
	writel(0x1, coherency_cpu_base + IO_SYNC_BARRIER_CTL_OFFSET);
	while (readl(coherency_cpu_base + IO_SYNC_BARRIER_CTL_OFFSET) & 0x1);
}

static dma_addr_t mvebu_hwcc_dma_map_page(struct device *dev, struct page *page,
				  unsigned long offset, size_t size,
				  enum dma_data_direction dir,
				  struct dma_attrs *attrs)
{
	if (dir != DMA_TO_DEVICE)
		mvebu_hwcc_sync_io_barrier();
	return pfn_to_dma(dev, page_to_pfn(page)) + offset;
}


static void mvebu_hwcc_dma_unmap_page(struct device *dev, dma_addr_t dma_handle,
			      size_t size, enum dma_data_direction dir,
			      struct dma_attrs *attrs)
{
	if (dir != DMA_TO_DEVICE)
		mvebu_hwcc_sync_io_barrier();
}

static void mvebu_hwcc_dma_sync(struct device *dev, dma_addr_t dma_handle,
			size_t size, enum dma_data_direction dir)
{
	if (dir != DMA_TO_DEVICE)
		mvebu_hwcc_sync_io_barrier();
}

static struct dma_map_ops mvebu_hwcc_dma_ops = {
	.alloc			= arm_dma_alloc,
	.free			= arm_dma_free,
	.mmap			= arm_dma_mmap,
	.map_page		= mvebu_hwcc_dma_map_page,
	.unmap_page		= mvebu_hwcc_dma_unmap_page,
	.get_sgtable		= arm_dma_get_sgtable,
	.map_sg			= arm_dma_map_sg,
	.unmap_sg		= arm_dma_unmap_sg,
	.sync_single_for_cpu	= mvebu_hwcc_dma_sync,
	.sync_single_for_device	= mvebu_hwcc_dma_sync,
	.sync_sg_for_cpu	= arm_dma_sync_sg_for_cpu,
	.sync_sg_for_device	= arm_dma_sync_sg_for_device,
	.set_dma_mask		= arm_dma_set_mask,
};

static int mvebu_hwcc_platform_notifier(struct notifier_block *nb,
				       unsigned long event, void *__dev)
{
	struct device *dev = __dev;

	if (event != BUS_NOTIFY_ADD_DEVICE)
		return NOTIFY_DONE;
	set_dma_ops(dev, &mvebu_hwcc_dma_ops);

	return NOTIFY_OK;
}

static struct notifier_block mvebu_hwcc_platform_nb = {
	.notifier_call = mvebu_hwcc_platform_notifier,
};

/*
 * Keep track of whether we have IO hardware coherency enabled or not.
 * On Armada 370's we will not be using it for example. We need to make
 * that available [through coherency_available()] so the mbus controller
 * doesn't enable the IO coherency bit in the attribute bits of the
 * chip selects.
 */
static int coherency_enabled;

int coherency_available(void)
{
	return coherency_enabled;
}

int __init coherency_init(void)
{
	struct device_node *np;

	/*
	 * The coherency fabric is needed:
	 * - For coherency between processors on Armada XP, so only
	 *   when SMP is enabled.
	 * - For coherency between the processor and I/O devices, but
	 *   this coherency requires many pre-requisites (write
	 *   allocate cache policy, shareable pages, SMP bit set) that
	 *   are only meant in SMP situations.
	 *
	 * Note that this means that on Armada 370, there is currently
	 * no way to use hardware I/O coherency, because even when
	 * CONFIG_SMP is enabled, is_smp() returns false due to the
	 * Armada 370 being a single-core processor. To lift this
	 * limitation, we would have to find a way to make the cache
	 * policy set to write-allocate (on all Armada SoCs), and to
	 * set the shareable attribute in page tables (on all Armada
	 * SoCs except the Armada 370). Unfortunately, such decisions
	 * are taken very early in the kernel boot process, at a point
	 * where we don't know yet on which SoC we are running.
	 */
	if (!is_smp())
		return 0;

	np = of_find_matching_node(NULL, of_coherency_table);
	if (np) {
		pr_info("Initializing Coherency fabric\n");
		coherency_base = of_iomap(np, 0);
		coherency_cpu_base = of_iomap(np, 1);
		set_cpu_coherent(cpu_logical_map(smp_processor_id()), 0);
		coherency_enabled = 1;
		bus_register_notifier(&platform_bus_type,
					&mvebu_hwcc_platform_nb);
	}

	return 0;
}
