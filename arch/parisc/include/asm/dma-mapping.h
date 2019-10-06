/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PARISC_DMA_MAPPING_H
#define _PARISC_DMA_MAPPING_H

/*
** We need to support 4 different coherent dma models with one binary:
**
**     I/O MMU        consistent method           dma_sync behavior
**  =============   ======================       =======================
**  a) PA-7x00LC    uncachable host memory          flush/purge
**  b) U2/Uturn      cachable host memory              NOP
**  c) Ike/Astro     cachable host memory              NOP
**  d) EPIC/SAGA     memory on EPIC/SAGA         flush/reset DMA channel
**
** PA-7[13]00LC processors have a GSC bus interface and no I/O MMU.
**
** Systems (eg PCX-T workstations) that don't fall into the above
** categories will need to modify the needed drivers to perform
** flush/purge and allocate "regular" cacheable pages for everything.
*/

extern const struct dma_map_ops *hppa_dma_ops;

static inline const struct dma_map_ops *get_arch_dma_ops(struct bus_type *bus)
{
	return hppa_dma_ops;
}

#endif
