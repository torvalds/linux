// SPDX-License-Identifier: GPL-2.0+

#include "fdma_api.h"

#include <linux/bits.h>
#include <linux/etherdevice.h>
#include <linux/types.h>

/* Add a DB to a DCB, providing a callback for getting the DB dataptr. */
static int __fdma_db_add(struct fdma *fdma, int dcb_idx, int db_idx, u64 status,
			 int (*cb)(struct fdma *fdma, int dcb_idx,
				   int db_idx, u64 *dataptr))
{
	struct fdma_db *db = fdma_db_get(fdma, dcb_idx, db_idx);
	u64 dataptr;
	int err;

	db->status = cpu_to_le64(status);

	err = cb(fdma, dcb_idx, db_idx, &dataptr);
	if (unlikely(err))
		return err;

	db->dataptr = cpu_to_le64(dataptr);

	return 0;
}

/* Add a DB to a DCB, using the callback set in the fdma_ops struct. */
int fdma_db_add(struct fdma *fdma, int dcb_idx, int db_idx, u64 status)
{
	return __fdma_db_add(fdma,
			     dcb_idx,
			     db_idx,
			     status,
			     fdma->ops.dataptr_cb);
}

/* Add a DCB with callbacks for getting the DB dataptr and the DCB nextptr. */
int __fdma_dcb_add(struct fdma *fdma, int dcb_idx, u64 info, u64 status,
		   int (*dcb_cb)(struct fdma *fdma, int dcb_idx, u64 *nextptr),
		   int (*db_cb)(struct fdma *fdma, int dcb_idx, int db_idx,
				u64 *dataptr))
{
	struct fdma_dcb *dcb = fdma_dcb_get(fdma, dcb_idx);
	u64 nextptr;
	int i, err;

	for (i = 0; i < fdma->n_dbs; i++) {
		err = __fdma_db_add(fdma, dcb_idx, i, status, db_cb);
		if (unlikely(err))
			return err;
	}

	err = dcb_cb(fdma, dcb_idx, &nextptr);
	if (unlikely(err))
		return err;

	fdma->last_dcb->nextptr = cpu_to_le64(nextptr);

	fdma->last_dcb = dcb;

	dcb->nextptr = cpu_to_le64(FDMA_DCB_INVALID_DATA);
	dcb->info = cpu_to_le64(info);

	return 0;
}
EXPORT_SYMBOL_GPL(__fdma_dcb_add);

/* Add a DCB, using the preset callbacks in the fdma_ops struct. */
int fdma_dcb_add(struct fdma *fdma, int dcb_idx, u64 info, u64 status)
{
	return __fdma_dcb_add(fdma,
			      dcb_idx,
			      info, status,
			      fdma->ops.nextptr_cb,
			      fdma->ops.dataptr_cb);
}
EXPORT_SYMBOL_GPL(fdma_dcb_add);

/* Initialize the DCB's and DB's. */
int fdma_dcbs_init(struct fdma *fdma, u64 info, u64 status)
{
	int i, err;

	fdma->last_dcb = fdma->dcbs;
	fdma->db_index = 0;
	fdma->dcb_index = 0;

	for (i = 0; i < fdma->n_dcbs; i++) {
		err = fdma_dcb_add(fdma, i, info, status);
		if (err)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(fdma_dcbs_init);

/* Allocate coherent DMA memory for FDMA. */
int fdma_alloc_coherent(struct device *dev, struct fdma *fdma)
{
	fdma->dcbs = dma_alloc_coherent(dev,
					fdma->size,
					&fdma->dma,
					GFP_KERNEL);
	if (!fdma->dcbs)
		return -ENOMEM;

	return 0;
}
EXPORT_SYMBOL_GPL(fdma_alloc_coherent);

/* Allocate physical memory for FDMA. */
int fdma_alloc_phys(struct fdma *fdma)
{
	fdma->dcbs = kzalloc(fdma->size, GFP_KERNEL);
	if (!fdma->dcbs)
		return -ENOMEM;

	fdma->dma = virt_to_phys(fdma->dcbs);

	return 0;
}
EXPORT_SYMBOL_GPL(fdma_alloc_phys);

/* Free coherent DMA memory. */
void fdma_free_coherent(struct device *dev, struct fdma *fdma)
{
	dma_free_coherent(dev, fdma->size, fdma->dcbs, fdma->dma);
}
EXPORT_SYMBOL_GPL(fdma_free_coherent);

/* Free virtual memory. */
void fdma_free_phys(struct fdma *fdma)
{
	kfree(fdma->dcbs);
}
EXPORT_SYMBOL_GPL(fdma_free_phys);

#if IS_ENABLED(CONFIG_MCHP_LAN966X_PCI)
/* Allocate coherent DMA memory and map it in the ATU. */
int fdma_alloc_coherent_and_map(struct device *dev, struct fdma *fdma,
				struct fdma_pci_atu *atu)
{
	struct fdma_pci_atu_region *region;
	int err;

	if (WARN_ON(fdma->atu_region))
		return -EBUSY;

	/* The ATU cannot express a limit finer than the region granularity, so
	 * the hardware widens the programmed limit to that boundary. Pad the
	 * allocation to match, or the outbound window would extend past the
	 * memory we own.
	 */
	fdma->size = ALIGN(fdma->size, FDMA_PCI_ATU_REGION_ALIGN);

	err = fdma_alloc_coherent(dev, fdma);
	if (err)
		return err;

	region = fdma_pci_atu_region_map(atu, fdma->dma, fdma->size);
	if (IS_ERR(region)) {
		fdma_free_coherent(dev, fdma);
		return PTR_ERR(region);
	}

	fdma->atu_region = region;

	return 0;
}
EXPORT_SYMBOL_GPL(fdma_alloc_coherent_and_map);

/* Free coherent DMA memory and unmap the memory in the ATU. */
void fdma_free_coherent_and_unmap(struct device *dev, struct fdma *fdma)
{
	fdma_pci_atu_region_unmap(fdma->atu_region);
	fdma->atu_region = NULL;
	fdma_free_coherent(dev, fdma);
}
EXPORT_SYMBOL_GPL(fdma_free_coherent_and_unmap);
#endif

/* Get the size of the FDMA memory */
u32 fdma_get_size(struct fdma *fdma)
{
	return ALIGN(sizeof(struct fdma_dcb) * fdma->n_dcbs, PAGE_SIZE);
}
EXPORT_SYMBOL_GPL(fdma_get_size);

/* Get the size of the FDMA memory. This function is only applicable if the
 * dataptr addresses and DCB's are in contiguous memory.
 */
u32 fdma_get_size_contiguous(struct fdma *fdma)
{
	return ALIGN(fdma->n_dcbs * sizeof(struct fdma_dcb) +
		     fdma->n_dcbs * fdma->n_dbs * fdma->db_size,
		     PAGE_SIZE);
}
EXPORT_SYMBOL_GPL(fdma_get_size_contiguous);
