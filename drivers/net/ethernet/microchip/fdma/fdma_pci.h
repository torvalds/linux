/* SPDX-License-Identifier: GPL-2.0+ */

#ifndef _FDMA_PCI_H_
#define _FDMA_PCI_H_

#include <linux/align.h>
#include <linux/bits.h>
#include <linux/mutex.h>
#include <linux/types.h>

#define FDMA_PCI_ATU_REGION_MAX 6

/* Outbound regions are 64KB granular (datasheet section 3.24.7.4.1), so both
 * the region base and the mapped size must be aligned to this.
 */
#define FDMA_PCI_ATU_REGION_ALIGN BIT(16)

#define FDMA_PCI_DB_ALIGN 128
#define FDMA_PCI_DB_SIZE(mtu) ALIGN(mtu, FDMA_PCI_DB_ALIGN)

struct fdma_pci_atu;

struct fdma_pci_atu_region {
	struct fdma_pci_atu *atu;
	u64 base_addr; /* Base addr of the OB window */
	u64 limit_addr; /* End addr of the active mapping (base_addr + size - 1) */
	u64 target_addr; /* Host DMA address this region maps to */
	int idx;
	bool in_use;
};

struct fdma_pci_atu {
	void __iomem *addr;
	struct mutex lock; /* Protects region alloc/free and ATU register access */
	struct fdma_pci_atu_region regions[FDMA_PCI_ATU_REGION_MAX];
};

/* Initialize ATU, dividing OB space into regions. */
void fdma_pci_atu_init(struct fdma_pci_atu *atu, void __iomem *addr);

/* Unmap an ATU region, clearing its translation and disabling it. */
void fdma_pci_atu_region_unmap(struct fdma_pci_atu_region *region);

/* Map a host DMA address into a free ATU region. target_addr and size must be
 * FDMA_PCI_ATU_REGION_ALIGN aligned; a misaligned target_addr returns -EINVAL.
 */
struct fdma_pci_atu_region *fdma_pci_atu_region_map(struct fdma_pci_atu *atu,
						    u64 target_addr,
						    int size);

/* Translate a host DMA address to the OB address space. Reads the region
 * unlocked, so the caller must quiesce DMA and the descriptor paths before
 * unmapping the region.
 */
u64 fdma_pci_atu_translate_addr(struct fdma_pci_atu_region *region, u64 addr);

#endif
