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

#define pr_fmt(fmt) "mvebu-coherency: " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <linux/smp.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mbus.h>
#include <linux/clk.h>
#include <linux/pci.h>
#include <asm/smp_plat.h>
#include <asm/cacheflush.h>
#include <asm/mach/map.h>
#include "armada-370-xp.h"
#include "coherency.h"
#include "mvebu-soc-id.h"

unsigned long coherency_phys_base;
void __iomem *coherency_base;
static void __iomem *coherency_cpu_base;

/* Coherency fabric registers */
#define COHERENCY_FABRIC_CFG_OFFSET		   0x4

#define IO_SYNC_BARRIER_CTL_OFFSET		   0x0

enum {
	COHERENCY_FABRIC_TYPE_NONE,
	COHERENCY_FABRIC_TYPE_ARMADA_370_XP,
	COHERENCY_FABRIC_TYPE_ARMADA_375,
	COHERENCY_FABRIC_TYPE_ARMADA_380,
};

static struct of_device_id of_coherency_table[] = {
	{.compatible = "marvell,coherency-fabric",
	 .data = (void *) COHERENCY_FABRIC_TYPE_ARMADA_370_XP },
	{.compatible = "marvell,armada-375-coherency-fabric",
	 .data = (void *) COHERENCY_FABRIC_TYPE_ARMADA_375 },
	{.compatible = "marvell,armada-380-coherency-fabric",
	 .data = (void *) COHERENCY_FABRIC_TYPE_ARMADA_380 },
	{ /* end of list */ },
};

/* Functions defined in coherency_ll.S */
int ll_enable_coherency(void);
void ll_add_cpu_to_smp_group(void);

int set_cpu_coherent(void)
{
	if (!coherency_base) {
		pr_warn("Can't make current CPU cache coherent.\n");
		pr_warn("Coherency fabric is not initialized\n");
		return 1;
	}

	ll_add_cpu_to_smp_group();
	return ll_enable_coherency();
}

/*
 * The below code implements the I/O coherency workaround on Armada
 * 375. This workaround consists in using the two channels of the
 * first XOR engine to trigger a XOR transaction that serves as the
 * I/O coherency barrier.
 */

static void __iomem *xor_base, *xor_high_base;
static dma_addr_t coherency_wa_buf_phys[CONFIG_NR_CPUS];
static void *coherency_wa_buf[CONFIG_NR_CPUS];
static bool coherency_wa_enabled;

#define XOR_CONFIG(chan)            (0x10 + (chan * 4))
#define XOR_ACTIVATION(chan)        (0x20 + (chan * 4))
#define WINDOW_BAR_ENABLE(chan)     (0x240 + ((chan) << 2))
#define WINDOW_BASE(w)              (0x250 + ((w) << 2))
#define WINDOW_SIZE(w)              (0x270 + ((w) << 2))
#define WINDOW_REMAP_HIGH(w)        (0x290 + ((w) << 2))
#define WINDOW_OVERRIDE_CTRL(chan)  (0x2A0 + ((chan) << 2))
#define XOR_DEST_POINTER(chan)      (0x2B0 + (chan * 4))
#define XOR_BLOCK_SIZE(chan)        (0x2C0 + (chan * 4))
#define XOR_INIT_VALUE_LOW           0x2E0
#define XOR_INIT_VALUE_HIGH          0x2E4

static inline void mvebu_hwcc_armada375_sync_io_barrier_wa(void)
{
	int idx = smp_processor_id();

	/* Write '1' to the first word of the buffer */
	writel(0x1, coherency_wa_buf[idx]);

	/* Wait until the engine is idle */
	while ((readl(xor_base + XOR_ACTIVATION(idx)) >> 4) & 0x3)
		;

	dmb();

	/* Trigger channel */
	writel(0x1, xor_base + XOR_ACTIVATION(idx));

	/* Poll the data until it is cleared by the XOR transaction */
	while (readl(coherency_wa_buf[idx]))
		;
}

static void __init armada_375_coherency_init_wa(void)
{
	const struct mbus_dram_target_info *dram;
	struct device_node *xor_node;
	struct property *xor_status;
	struct clk *xor_clk;
	u32 win_enable = 0;
	int i;

	pr_warn("enabling coherency workaround for Armada 375 Z1, one XOR engine disabled\n");

	/*
	 * Since the workaround uses one XOR engine, we grab a
	 * reference to its Device Tree node first.
	 */
	xor_node = of_find_compatible_node(NULL, NULL, "marvell,orion-xor");
	BUG_ON(!xor_node);

	/*
	 * Then we mark it as disabled so that the real XOR driver
	 * will not use it.
	 */
	xor_status = kzalloc(sizeof(struct property), GFP_KERNEL);
	BUG_ON(!xor_status);

	xor_status->value = kstrdup("disabled", GFP_KERNEL);
	BUG_ON(!xor_status->value);

	xor_status->length = 8;
	xor_status->name = kstrdup("status", GFP_KERNEL);
	BUG_ON(!xor_status->name);

	of_update_property(xor_node, xor_status);

	/*
	 * And we remap the registers, get the clock, and do the
	 * initial configuration of the XOR engine.
	 */
	xor_base = of_iomap(xor_node, 0);
	xor_high_base = of_iomap(xor_node, 1);

	xor_clk = of_clk_get_by_name(xor_node, NULL);
	BUG_ON(!xor_clk);

	clk_prepare_enable(xor_clk);

	dram = mv_mbus_dram_info();

	for (i = 0; i < 8; i++) {
		writel(0, xor_base + WINDOW_BASE(i));
		writel(0, xor_base + WINDOW_SIZE(i));
		if (i < 4)
			writel(0, xor_base + WINDOW_REMAP_HIGH(i));
	}

	for (i = 0; i < dram->num_cs; i++) {
		const struct mbus_dram_window *cs = dram->cs + i;
		writel((cs->base & 0xffff0000) |
		       (cs->mbus_attr << 8) |
		       dram->mbus_dram_target_id, xor_base + WINDOW_BASE(i));
		writel((cs->size - 1) & 0xffff0000, xor_base + WINDOW_SIZE(i));

		win_enable |= (1 << i);
		win_enable |= 3 << (16 + (2 * i));
	}

	writel(win_enable, xor_base + WINDOW_BAR_ENABLE(0));
	writel(win_enable, xor_base + WINDOW_BAR_ENABLE(1));
	writel(0, xor_base + WINDOW_OVERRIDE_CTRL(0));
	writel(0, xor_base + WINDOW_OVERRIDE_CTRL(1));

	for (i = 0; i < CONFIG_NR_CPUS; i++) {
		coherency_wa_buf[i] = kzalloc(PAGE_SIZE, GFP_KERNEL);
		BUG_ON(!coherency_wa_buf[i]);

		/*
		 * We can't use the DMA mapping API, since we don't
		 * have a valid 'struct device' pointer
		 */
		coherency_wa_buf_phys[i] =
			virt_to_phys(coherency_wa_buf[i]);
		BUG_ON(!coherency_wa_buf_phys[i]);

		/*
		 * Configure the XOR engine for memset operation, with
		 * a 128 bytes block size
		 */
		writel(0x444, xor_base + XOR_CONFIG(i));
		writel(128, xor_base + XOR_BLOCK_SIZE(i));
		writel(coherency_wa_buf_phys[i],
		       xor_base + XOR_DEST_POINTER(i));
	}

	writel(0x0, xor_base + XOR_INIT_VALUE_LOW);
	writel(0x0, xor_base + XOR_INIT_VALUE_HIGH);

	coherency_wa_enabled = true;
}

static inline void mvebu_hwcc_sync_io_barrier(void)
{
	if (coherency_wa_enabled) {
		mvebu_hwcc_armada375_sync_io_barrier_wa();
		return;
	}

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

static int mvebu_hwcc_notifier(struct notifier_block *nb,
			       unsigned long event, void *__dev)
{
	struct device *dev = __dev;

	if (event != BUS_NOTIFY_ADD_DEVICE)
		return NOTIFY_DONE;
	set_dma_ops(dev, &mvebu_hwcc_dma_ops);

	return NOTIFY_OK;
}

static struct notifier_block mvebu_hwcc_nb = {
	.notifier_call = mvebu_hwcc_notifier,
};

static struct notifier_block mvebu_hwcc_pci_nb = {
	.notifier_call = mvebu_hwcc_notifier,
};

static void __init armada_370_coherency_init(struct device_node *np)
{
	struct resource res;

	of_address_to_resource(np, 0, &res);
	coherency_phys_base = res.start;
	/*
	 * Ensure secondary CPUs will see the updated value,
	 * which they read before they join the coherency
	 * fabric, and therefore before they are coherent with
	 * the boot CPU cache.
	 */
	sync_cache_w(&coherency_phys_base);
	coherency_base = of_iomap(np, 0);
	coherency_cpu_base = of_iomap(np, 1);
	set_cpu_coherent();
}

/*
 * This ioremap hook is used on Armada 375/38x to ensure that PCIe
 * memory areas are mapped as MT_UNCACHED instead of MT_DEVICE. This
 * is needed as a workaround for a deadlock issue between the PCIe
 * interface and the cache controller.
 */
static void __iomem *
armada_pcie_wa_ioremap_caller(phys_addr_t phys_addr, size_t size,
			      unsigned int mtype, void *caller)
{
	struct resource pcie_mem;

	mvebu_mbus_get_pcie_mem_aperture(&pcie_mem);

	if (pcie_mem.start <= phys_addr && (phys_addr + size) <= pcie_mem.end)
		mtype = MT_UNCACHED;

	return __arm_ioremap_caller(phys_addr, size, mtype, caller);
}

static void __init armada_375_380_coherency_init(struct device_node *np)
{
	struct device_node *cache_dn;

	coherency_cpu_base = of_iomap(np, 0);
	arch_ioremap_caller = armada_pcie_wa_ioremap_caller;

	/*
	 * Add the PL310 property "arm,io-coherent". This makes sure the
	 * outer sync operation is not used, which allows to
	 * workaround the system erratum that causes deadlocks when
	 * doing PCIe in an SMP situation on Armada 375 and Armada
	 * 38x.
	 */
	for_each_compatible_node(cache_dn, NULL, "arm,pl310-cache") {
		struct property *p;

		p = kzalloc(sizeof(*p), GFP_KERNEL);
		p->name = kstrdup("arm,io-coherent", GFP_KERNEL);
		of_add_property(cache_dn, p);
	}
}

static int coherency_type(void)
{
	struct device_node *np;
	const struct of_device_id *match;
	int type;

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
		return COHERENCY_FABRIC_TYPE_NONE;

	np = of_find_matching_node_and_match(NULL, of_coherency_table, &match);
	if (!np)
		return COHERENCY_FABRIC_TYPE_NONE;

	type = (int) match->data;

	of_node_put(np);

	return type;
}

int coherency_available(void)
{
	return coherency_type() != COHERENCY_FABRIC_TYPE_NONE;
}

int __init coherency_init(void)
{
	int type = coherency_type();
	struct device_node *np;

	np = of_find_matching_node(NULL, of_coherency_table);

	if (type == COHERENCY_FABRIC_TYPE_ARMADA_370_XP)
		armada_370_coherency_init(np);
	else if (type == COHERENCY_FABRIC_TYPE_ARMADA_375 ||
		 type == COHERENCY_FABRIC_TYPE_ARMADA_380)
		armada_375_380_coherency_init(np);

	of_node_put(np);

	return 0;
}

static int __init coherency_late_init(void)
{
	int type = coherency_type();

	if (type == COHERENCY_FABRIC_TYPE_NONE)
		return 0;

	if (type == COHERENCY_FABRIC_TYPE_ARMADA_375) {
		u32 dev, rev;

		if (mvebu_get_soc_id(&dev, &rev) == 0 &&
		    rev == ARMADA_375_Z1_REV)
			armada_375_coherency_init_wa();
	}

	bus_register_notifier(&platform_bus_type,
			      &mvebu_hwcc_nb);

	return 0;
}

postcore_initcall(coherency_late_init);

#if IS_ENABLED(CONFIG_PCI)
static int __init coherency_pci_init(void)
{
	if (coherency_available())
		bus_register_notifier(&pci_bus_type,
				       &mvebu_hwcc_pci_nb);
	return 0;
}

arch_initcall(coherency_pci_init);
#endif
