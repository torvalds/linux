/**
 * iommu_fill_pdir - Insert coalesced scatter/gather chunks into the I/O Pdir.
 * @ioc: The I/O Controller.
 * @startsg: The scatter/gather list of coalesced chunks.
 * @nents: The number of entries in the scatter/gather list.
 * @hint: The DMA Hint.
 *
 * This function inserts the coalesced scatter/gather list chunks into the
 * I/O Controller's I/O Pdir.
 */ 
static inline unsigned int
iommu_fill_pdir(struct ioc *ioc, struct scatterlist *startsg, int nents, 
		unsigned long hint,
		void (*iommu_io_pdir_entry)(u64 *, space_t, unsigned long,
					    unsigned long))
{
	struct scatterlist *dma_sg = startsg;	/* pointer to current DMA */
	unsigned int n_mappings = 0;
	unsigned long dma_offset = 0, dma_len = 0;
	u64 *pdirp = NULL;

	/* Horrible hack.  For efficiency's sake, dma_sg starts one 
	 * entry below the true start (it is immediately incremented
	 * in the loop) */
	 dma_sg--;

	while (nents-- > 0) {
		unsigned long vaddr;
		long size;

		DBG_RUN_SG(" %d : %08lx/%05x %08lx/%05x\n", nents,
			   (unsigned long)sg_dma_address(startsg), cnt,
			   sg_virt_addr(startsg), startsg->length
		);


		/*
		** Look for the start of a new DMA stream
		*/
		
		if (sg_dma_address(startsg) & PIDE_FLAG) {
			u32 pide = sg_dma_address(startsg) & ~PIDE_FLAG;

			BUG_ON(pdirp && (dma_len != sg_dma_len(dma_sg)));

			dma_sg++;

			dma_len = sg_dma_len(startsg);
			sg_dma_len(startsg) = 0;
			dma_offset = (unsigned long) pide & ~IOVP_MASK;
			n_mappings++;
#if defined(ZX1_SUPPORT)
			/* Pluto IOMMU IO Virt Address is not zero based */
			sg_dma_address(dma_sg) = pide | ioc->ibase;
#else
			/* SBA, ccio, and dino are zero based.
			 * Trying to save a few CPU cycles for most users.
			 */
			sg_dma_address(dma_sg) = pide;
#endif
			pdirp = &(ioc->pdir_base[pide >> IOVP_SHIFT]);
			prefetchw(pdirp);
		}
		
		BUG_ON(pdirp == NULL);
		
		vaddr = sg_virt_addr(startsg);
		sg_dma_len(dma_sg) += startsg->length;
		size = startsg->length + dma_offset;
		dma_offset = 0;
#ifdef IOMMU_MAP_STATS
		ioc->msg_pages += startsg->length >> IOVP_SHIFT;
#endif
		do {
			iommu_io_pdir_entry(pdirp, KERNEL_SPACE, 
					    vaddr, hint);
			vaddr += IOVP_SIZE;
			size -= IOVP_SIZE;
			pdirp++;
		} while(unlikely(size > 0));
		startsg++;
	}
	return(n_mappings);
}


/*
** First pass is to walk the SG list and determine where the breaks are
** in the DMA stream. Allocates PDIR entries but does not fill them.
** Returns the number of DMA chunks.
**
** Doing the fill separate from the coalescing/allocation keeps the
** code simpler. Future enhancement could make one pass through
** the sglist do both.
*/

static inline unsigned int
iommu_coalesce_chunks(struct ioc *ioc, struct device *dev,
		      struct scatterlist *startsg, int nents,
		      int (*iommu_alloc_range)(struct ioc *, size_t))
{
	struct scatterlist *contig_sg;	   /* contig chunk head */
	unsigned long dma_offset, dma_len; /* start/len of DMA stream */
	unsigned int n_mappings = 0;
	unsigned int max_seg_size = dma_get_max_seg_size(dev);

	while (nents > 0) {

		/*
		** Prepare for first/next DMA stream
		*/
		contig_sg = startsg;
		dma_len = startsg->length;
		dma_offset = sg_virt_addr(startsg) & ~IOVP_MASK;

		/* PARANOID: clear entries */
		sg_dma_address(startsg) = 0;
		sg_dma_len(startsg) = 0;

		/*
		** This loop terminates one iteration "early" since
		** it's always looking one "ahead".
		*/
		while(--nents > 0) {
			unsigned long prevstartsg_end, startsg_end;

			prevstartsg_end = sg_virt_addr(startsg) +
				startsg->length;

			startsg++;
			startsg_end = sg_virt_addr(startsg) + 
				startsg->length;

			/* PARANOID: clear entries */
			sg_dma_address(startsg) = 0;
			sg_dma_len(startsg) = 0;

			/*
			** First make sure current dma stream won't
			** exceed DMA_CHUNK_SIZE if we coalesce the
			** next entry.
			*/   
			if(unlikely(ALIGN(dma_len + dma_offset + startsg->length,
					    IOVP_SIZE) > DMA_CHUNK_SIZE))
				break;

			if (startsg->length + dma_len > max_seg_size)
				break;

			/*
			** Next see if we can append the next chunk (i.e.
			** it must end on one page and begin on another
			*/
			if (unlikely(((prevstartsg_end | sg_virt_addr(startsg)) & ~PAGE_MASK) != 0))
				break;
			
			dma_len += startsg->length;
		}

		/*
		** End of DMA Stream
		** Terminate last VCONTIG block.
		** Allocate space for DMA stream.
		*/
		sg_dma_len(contig_sg) = dma_len;
		dma_len = ALIGN(dma_len + dma_offset, IOVP_SIZE);
		sg_dma_address(contig_sg) =
			PIDE_FLAG 
			| (iommu_alloc_range(ioc, dma_len) << IOVP_SHIFT)
			| dma_offset;
		n_mappings++;
	}

	return n_mappings;
}

