// SPDX-License-Identifier: GPL-2.0+

#include <linux/align.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "fdma_pci.h"

/* When the switch operates as a PCIe endpoint, the FDMA engine needs to
 * DMA to/from host memory. The FDMA writes to addresses within the endpoint's
 * internal Outbound (OB) address space, and the PCIe ATU translates these to
 * DMA addresses on the PCIe bus, targeting host memory.
 *
 * The ATU supports up to six outbound regions. This implementation divides
 * the OB address space into six equally sized chunks.
 *
 * +-------------+------------+------------+-----+------------+
 * | Index       | Region 0   | Region 1   | ... | Region 5   |
 * +-------------+------------+------------+-----+------------+
 * | Base addr   | 0x10000000 | 0x12aa0000 | ... | 0x1d520000 |
 * | Limit addr  | 0x12a9ffff | 0x1553ffff | ... | 0x1ffbffff |
 * | Target addr | host dma   | host dma   | ... | host dma   |
 * +-------------+------------+------------+-----+------------+
 *
 * Base addr is the start address of the region within the OB address space.
 * Limit addr is each region's own upper bound. The value actually
 * programmed is set per-mapping as base_addr + mapped size - 1, and is
 * usually smaller than this.
 * Target addr is the host DMA address that the base addr translates to.
 */

#define FDMA_PCI_ATU_OB_START        0x10000000
#define FDMA_PCI_ATU_OB_END          0x1fffffff

#define FDMA_PCI_ATU_ADDR            0x300000
#define FDMA_PCI_ATU_IDX_SIZE        0x200
#define FDMA_PCI_ATU_ENA_REG         0x4
#define FDMA_PCI_ATU_ENA_BIT         BIT(31)
#define FDMA_PCI_ATU_LWR_BASE_ADDR   0x8
#define FDMA_PCI_ATU_UPP_BASE_ADDR   0xc
#define FDMA_PCI_ATU_LIMIT_ADDR      0x10
#define FDMA_PCI_ATU_LWR_TARGET_ADDR 0x14
#define FDMA_PCI_ATU_UPP_TARGET_ADDR 0x18

static u32 fdma_pci_atu_region_size(void)
{
	return round_down((FDMA_PCI_ATU_OB_END - FDMA_PCI_ATU_OB_START) /
			  FDMA_PCI_ATU_REGION_MAX, FDMA_PCI_ATU_REGION_ALIGN);
}

static void __iomem *fdma_pci_atu_addr_get(void __iomem *addr, int offset,
					   int idx)
{
	return addr + FDMA_PCI_ATU_ADDR + FDMA_PCI_ATU_IDX_SIZE * idx + offset;
}

static void fdma_pci_atu_region_enable(struct fdma_pci_atu_region *region)
{
	writel(FDMA_PCI_ATU_ENA_BIT,
	       fdma_pci_atu_addr_get(region->atu->addr, FDMA_PCI_ATU_ENA_REG,
				     region->idx));
}

static void fdma_pci_atu_region_disable(struct fdma_pci_atu_region *region)
{
	writel(0, fdma_pci_atu_addr_get(region->atu->addr, FDMA_PCI_ATU_ENA_REG,
					region->idx));
}

/* Configure the address translation in the ATU. */
static void
fdma_pci_atu_configure_translation(struct fdma_pci_atu_region *region)
{
	struct fdma_pci_atu *atu = region->atu;
	int idx = region->idx;

	writel(lower_32_bits(region->base_addr),
	       fdma_pci_atu_addr_get(atu->addr,
				     FDMA_PCI_ATU_LWR_BASE_ADDR, idx));

	writel(upper_32_bits(region->base_addr),
	       fdma_pci_atu_addr_get(atu->addr,
				     FDMA_PCI_ATU_UPP_BASE_ADDR, idx));

	/* The OB address space lies entirely below 4GB, so the limit always
	 * fits the lower limit register and the upper one is left alone.
	 */
	writel(region->limit_addr,
	       fdma_pci_atu_addr_get(atu->addr, FDMA_PCI_ATU_LIMIT_ADDR, idx));

	writel(lower_32_bits(region->target_addr),
	       fdma_pci_atu_addr_get(atu->addr,
				     FDMA_PCI_ATU_LWR_TARGET_ADDR, idx));

	writel(upper_32_bits(region->target_addr),
	       fdma_pci_atu_addr_get(atu->addr,
				     FDMA_PCI_ATU_UPP_TARGET_ADDR, idx));
}

/* Find an unused ATU region. */
static struct fdma_pci_atu_region *
fdma_pci_atu_region_get_free(struct fdma_pci_atu *atu)
{
	struct fdma_pci_atu_region *regions = atu->regions;

	for (int i = 0; i < FDMA_PCI_ATU_REGION_MAX; i++) {
		if (regions[i].in_use)
			continue;

		return &regions[i];
	}

	return ERR_PTR(-ENOSPC);
}

/* Unmap an ATU region, clearing its translation and disabling it. */
void fdma_pci_atu_region_unmap(struct fdma_pci_atu_region *region)
{
	if (IS_ERR_OR_NULL(region))
		return;

	mutex_lock(&region->atu->lock);

	region->target_addr = 0;
	region->in_use = false;

	fdma_pci_atu_region_disable(region);
	fdma_pci_atu_configure_translation(region);

	mutex_unlock(&region->atu->lock);
}
EXPORT_SYMBOL_GPL(fdma_pci_atu_region_unmap);

/* Map a host DMA address into a free outbound region. */
struct fdma_pci_atu_region *
fdma_pci_atu_region_map(struct fdma_pci_atu *atu, u64 target_addr, int size)
{
	struct fdma_pci_atu_region *region;

	if (!atu)
		return ERR_PTR(-EINVAL);

	if (size <= 0)
		return ERR_PTR(-EINVAL);

	if (size > fdma_pci_atu_region_size())
		return ERR_PTR(-ERANGE);

	/* The ATU region base is only ever aligned to FDMA_PCI_ATU_REGION_ALIGN;
	 * require the same alignment of the host target address, since the ATU
	 * translates addr - target_addr + base_addr and any misalignment here
	 * would shift every translated address by the same amount.
	 */
	if (!IS_ALIGNED(target_addr, FDMA_PCI_ATU_REGION_ALIGN))
		return ERR_PTR(-EINVAL);

	mutex_lock(&atu->lock);

	region = fdma_pci_atu_region_get_free(atu);
	if (IS_ERR(region)) {
		mutex_unlock(&atu->lock);
		return region;
	}

	region->target_addr = target_addr;
	region->limit_addr = region->base_addr + size - 1;
	region->in_use = true;

	fdma_pci_atu_configure_translation(region);
	fdma_pci_atu_region_enable(region);

	mutex_unlock(&atu->lock);

	return region;
}
EXPORT_SYMBOL_GPL(fdma_pci_atu_region_map);

/* Translate a host DMA address to the corresponding OB address. */
u64 fdma_pci_atu_translate_addr(struct fdma_pci_atu_region *region, u64 addr)
{
	return region->base_addr + (addr - region->target_addr);
}
EXPORT_SYMBOL_GPL(fdma_pci_atu_translate_addr);

/* Initialize ATU, dividing the OB space into equally sized regions. */
void fdma_pci_atu_init(struct fdma_pci_atu *atu, void __iomem *addr)
{
	struct fdma_pci_atu_region *regions = atu->regions;
	u32 region_size = fdma_pci_atu_region_size();

	atu->addr = addr;
	mutex_init(&atu->lock);

	for (int i = 0; i < FDMA_PCI_ATU_REGION_MAX; i++) {
		regions[i].base_addr =
			FDMA_PCI_ATU_OB_START + (i * region_size);
		regions[i].limit_addr = 0;
		regions[i].target_addr = 0;
		regions[i].idx = i;
		regions[i].atu = atu;
		regions[i].in_use = false;

		fdma_pci_atu_region_disable(&regions[i]);
	}
}
EXPORT_SYMBOL_GPL(fdma_pci_atu_init);
