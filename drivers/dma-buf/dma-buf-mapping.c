// SPDX-License-Identifier: GPL-2.0-only
/*
 * DMA BUF Mapping Helpers
 *
 */
#include <linux/dma-buf-mapping.h>
#include <linux/dma-resv.h>

static struct scatterlist *fill_sg_entry(struct scatterlist *sgl, size_t length,
					 dma_addr_t addr)
{
	unsigned int len, nents;
	int i;

	nents = DIV_ROUND_UP(length, UINT_MAX);
	for (i = 0; i < nents; i++) {
		len = min_t(size_t, length, UINT_MAX);
		length -= len;
		/*
		 * DMABUF abuses scatterlist to create a scatterlist
		 * that does not have any CPU list, only the DMA list.
		 * Always set the page related values to NULL to ensure
		 * importers can't use it. The phys_addr based DMA API
		 * does not require the CPU list for mapping or unmapping.
		 */
		sg_set_page(sgl, NULL, 0, 0);
		sg_dma_address(sgl) = addr + (dma_addr_t)i * UINT_MAX;
		sg_dma_len(sgl) = len;
		sgl = sg_next(sgl);
	}

	return sgl;
}

static unsigned int calc_sg_nents(struct dma_iova_state *state,
				  struct phys_vec *phys_vec, size_t nr_ranges,
				  size_t size)
{
	unsigned int nents = 0;
	size_t i;

	if (!state || !dma_use_iova(state)) {
		for (i = 0; i < nr_ranges; i++)
			nents += DIV_ROUND_UP(phys_vec[i].len, UINT_MAX);
	} else {
		/*
		 * In IOVA case, there is only one SG entry which spans
		 * for whole IOVA address space, but we need to make sure
		 * that it fits sg->length, maybe we need more.
		 */
		nents = DIV_ROUND_UP(size, UINT_MAX);
	}

	return nents;
}

/**
 * struct dma_buf_dma - holds DMA mapping information
 * @sgt:    Scatter-gather table
 * @state:  DMA IOVA state relevant in IOMMU-based DMA
 * @size:   Total size of DMA transfer
 */
struct dma_buf_dma {
	struct sg_table sgt;
	struct dma_iova_state *state;
	size_t size;
};

/**
 * dma_buf_phys_vec_to_sgt - Returns the scatterlist table of the attachment
 * from arrays of physical vectors. This funciton is intended for MMIO memory
 * only.
 * @attach:	[in]	attachment whose scatterlist is to be returned
 * @provider:	[in]	p2pdma provider
 * @phys_vec:	[in]	array of physical vectors
 * @nr_ranges:	[in]	number of entries in phys_vec array
 * @size:	[in]	total size of phys_vec
 * @dir:	[in]	direction of DMA transfer
 *
 * Returns sg_table containing the scatterlist to be returned; returns ERR_PTR
 * on error. May return -EINTR if it is interrupted by a signal.
 *
 * On success, the DMA addresses and lengths in the returned scatterlist are
 * PAGE_SIZE aligned.
 *
 * A mapping must be unmapped by using dma_buf_free_sgt().
 *
 * NOTE: This function is intended for exporters. If direct traffic routing is
 * mandatory exporter should call routing pci_p2pdma_map_type() before calling
 * this function.
 */
struct sg_table *dma_buf_phys_vec_to_sgt(struct dma_buf_attachment *attach,
					 struct p2pdma_provider *provider,
					 struct phys_vec *phys_vec,
					 size_t nr_ranges, size_t size,
					 enum dma_data_direction dir)
{
	unsigned int nents, mapped_len = 0;
	struct dma_buf_dma *dma;
	struct scatterlist *sgl;
	dma_addr_t addr;
	size_t i;
	int ret;

	dma_resv_assert_held(attach->dmabuf->resv);

	if (WARN_ON(!attach || !attach->dmabuf || !provider))
		/* This function is supposed to work on MMIO memory only */
		return ERR_PTR(-EINVAL);

	dma = kzalloc(sizeof(*dma), GFP_KERNEL);
	if (!dma)
		return ERR_PTR(-ENOMEM);

	switch (pci_p2pdma_map_type(provider, attach->dev)) {
	case PCI_P2PDMA_MAP_BUS_ADDR:
		/*
		 * There is no need in IOVA at all for this flow.
		 */
		break;
	case PCI_P2PDMA_MAP_THRU_HOST_BRIDGE:
		dma->state = kzalloc(sizeof(*dma->state), GFP_KERNEL);
		if (!dma->state) {
			ret = -ENOMEM;
			goto err_free_dma;
		}

		dma_iova_try_alloc(attach->dev, dma->state, 0, size);
		break;
	default:
		ret = -EINVAL;
		goto err_free_dma;
	}

	nents = calc_sg_nents(dma->state, phys_vec, nr_ranges, size);
	ret = sg_alloc_table(&dma->sgt, nents, GFP_KERNEL | __GFP_ZERO);
	if (ret)
		goto err_free_state;

	sgl = dma->sgt.sgl;

	for (i = 0; i < nr_ranges; i++) {
		if (!dma->state) {
			addr = pci_p2pdma_bus_addr_map(provider,
						       phys_vec[i].paddr);
		} else if (dma_use_iova(dma->state)) {
			ret = dma_iova_link(attach->dev, dma->state,
					    phys_vec[i].paddr, 0,
					    phys_vec[i].len, dir,
					    DMA_ATTR_MMIO);
			if (ret)
				goto err_unmap_dma;

			mapped_len += phys_vec[i].len;
		} else {
			addr = dma_map_phys(attach->dev, phys_vec[i].paddr,
					    phys_vec[i].len, dir,
					    DMA_ATTR_MMIO);
			ret = dma_mapping_error(attach->dev, addr);
			if (ret)
				goto err_unmap_dma;
		}

		if (!dma->state || !dma_use_iova(dma->state))
			sgl = fill_sg_entry(sgl, phys_vec[i].len, addr);
	}

	if (dma->state && dma_use_iova(dma->state)) {
		WARN_ON_ONCE(mapped_len != size);
		ret = dma_iova_sync(attach->dev, dma->state, 0, mapped_len);
		if (ret)
			goto err_unmap_dma;

		sgl = fill_sg_entry(sgl, mapped_len, dma->state->addr);
	}

	dma->size = size;

	/*
	 * No CPU list included — set orig_nents = 0 so others can detect
	 * this via SG table (use nents only).
	 */
	dma->sgt.orig_nents = 0;


	/*
	 * SGL must be NULL to indicate that SGL is the last one
	 * and we allocated correct number of entries in sg_alloc_table()
	 */
	WARN_ON_ONCE(sgl);
	return &dma->sgt;

err_unmap_dma:
	if (!i || !dma->state) {
		; /* Do nothing */
	} else if (dma_use_iova(dma->state)) {
		dma_iova_destroy(attach->dev, dma->state, mapped_len, dir,
				 DMA_ATTR_MMIO);
	} else {
		for_each_sgtable_dma_sg(&dma->sgt, sgl, i)
			dma_unmap_phys(attach->dev, sg_dma_address(sgl),
				       sg_dma_len(sgl), dir, DMA_ATTR_MMIO);
	}
	sg_free_table(&dma->sgt);
err_free_state:
	kfree(dma->state);
err_free_dma:
	kfree(dma);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL_NS_GPL(dma_buf_phys_vec_to_sgt, "DMA_BUF");

/**
 * dma_buf_free_sgt- unmaps the buffer
 * @attach:	[in]	attachment to unmap buffer from
 * @sgt:	[in]	scatterlist info of the buffer to unmap
 * @dir:	[in]	direction of DMA transfer
 *
 * This unmaps a DMA mapping for @attached obtained
 * by dma_buf_phys_vec_to_sgt().
 */
void dma_buf_free_sgt(struct dma_buf_attachment *attach, struct sg_table *sgt,
		      enum dma_data_direction dir)
{
	struct dma_buf_dma *dma = container_of(sgt, struct dma_buf_dma, sgt);
	int i;

	dma_resv_assert_held(attach->dmabuf->resv);

	if (!dma->state) {
		; /* Do nothing */
	} else if (dma_use_iova(dma->state)) {
		dma_iova_destroy(attach->dev, dma->state, dma->size, dir,
				 DMA_ATTR_MMIO);
	} else {
		struct scatterlist *sgl;

		for_each_sgtable_dma_sg(sgt, sgl, i)
			dma_unmap_phys(attach->dev, sg_dma_address(sgl),
				       sg_dma_len(sgl), dir, DMA_ATTR_MMIO);
	}

	sg_free_table(sgt);
	kfree(dma->state);
	kfree(dma);

}
EXPORT_SYMBOL_NS_GPL(dma_buf_free_sgt, "DMA_BUF");
