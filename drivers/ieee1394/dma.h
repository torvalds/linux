/*
 * DMA region bookkeeping routines
 *
 * Copyright (C) 2002 Maas Digital LLC
 *
 * This code is licensed under the GPL.  See the file COPYING in the root
 * directory of the kernel sources for details.
 */

#ifndef IEEE1394_DMA_H
#define IEEE1394_DMA_H

#include <linux/pci.h>
#include <asm/scatterlist.h>

/* struct dma_prog_region

   a small, physically-contiguous DMA buffer with random-access,
   synchronous usage characteristics
*/

struct dma_prog_region {
	unsigned char    *kvirt;     /* kernel virtual address */
	struct pci_dev   *dev;       /* PCI device */
	unsigned int      n_pages;   /* # of kernel pages */
	dma_addr_t        bus_addr;  /* base bus address */
};

/* clear out all fields but do not allocate any memory */
void dma_prog_region_init(struct dma_prog_region *prog);
int  dma_prog_region_alloc(struct dma_prog_region *prog, unsigned long n_bytes, struct pci_dev *dev);
void dma_prog_region_free(struct dma_prog_region *prog);

static inline dma_addr_t dma_prog_region_offset_to_bus(struct dma_prog_region *prog, unsigned long offset)
{
	return prog->bus_addr + offset;
}

/* struct dma_region

   a large, non-physically-contiguous DMA buffer with streaming,
   asynchronous usage characteristics
*/

struct dma_region {
	unsigned char      *kvirt;       /* kernel virtual address */
	struct pci_dev     *dev;         /* PCI device */
	unsigned int        n_pages;     /* # of kernel pages */
	unsigned int        n_dma_pages; /* # of IOMMU pages */
	struct scatterlist *sglist;      /* IOMMU mapping */
	int                 direction;   /* PCI_DMA_TODEVICE, etc */
};

/* clear out all fields but do not allocate anything */
void dma_region_init(struct dma_region *dma);

/* allocate the buffer and map it to the IOMMU */
int  dma_region_alloc(struct dma_region *dma, unsigned long n_bytes, struct pci_dev *dev, int direction);

/* unmap and free the buffer */
void dma_region_free(struct dma_region *dma);

/* sync the CPU's view of the buffer */
void dma_region_sync_for_cpu(struct dma_region *dma, unsigned long offset, unsigned long len);
/* sync the IO bus' view of the buffer */
void dma_region_sync_for_device(struct dma_region *dma, unsigned long offset, unsigned long len);

/* map the buffer into a user space process */
int  dma_region_mmap(struct dma_region *dma, struct file *file, struct vm_area_struct *vma);

/* macro to index into a DMA region (or dma_prog_region) */
#define dma_region_i(_dma, _type, _index) ( ((_type*) ((_dma)->kvirt)) + (_index) )

/* return the DMA bus address of the byte with the given offset
   relative to the beginning of the dma_region */
dma_addr_t dma_region_offset_to_bus(struct dma_region *dma, unsigned long offset);

#endif /* IEEE1394_DMA_H */
