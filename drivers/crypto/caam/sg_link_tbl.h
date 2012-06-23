/*
 * CAAM/SEC 4.x functions for using scatterlists in caam driver
 *
 * Copyright 2008-2011 Freescale Semiconductor, Inc.
 *
 */

struct link_tbl_entry;

/*
 * convert single dma address to h/w link table format
 */
static inline void sg_to_link_tbl_one(struct link_tbl_entry *link_tbl_ptr,
				      dma_addr_t dma, u32 len, u32 offset)
{
	link_tbl_ptr->ptr = dma;
	link_tbl_ptr->len = len;
	link_tbl_ptr->reserved = 0;
	link_tbl_ptr->buf_pool_id = 0;
	link_tbl_ptr->offset = offset;
#ifdef DEBUG
	print_hex_dump(KERN_ERR, "link_tbl_ptr@: ",
		       DUMP_PREFIX_ADDRESS, 16, 4, link_tbl_ptr,
		       sizeof(struct link_tbl_entry), 1);
#endif
}

/*
 * convert scatterlist to h/w link table format
 * but does not have final bit; instead, returns last entry
 */
static inline struct link_tbl_entry *
sg_to_link_tbl(struct scatterlist *sg, int sg_count,
	       struct link_tbl_entry *link_tbl_ptr, u32 offset)
{
	while (sg_count) {
		sg_to_link_tbl_one(link_tbl_ptr, sg_dma_address(sg),
				   sg_dma_len(sg), offset);
		link_tbl_ptr++;
		sg = sg_next(sg);
		sg_count--;
	}
	return link_tbl_ptr - 1;
}

/*
 * convert scatterlist to h/w link table format
 * scatterlist must have been previously dma mapped
 */
static inline void sg_to_link_tbl_last(struct scatterlist *sg, int sg_count,
				       struct link_tbl_entry *link_tbl_ptr,
				       u32 offset)
{
	link_tbl_ptr = sg_to_link_tbl(sg, sg_count, link_tbl_ptr, offset);
	link_tbl_ptr->len |= LINK_TBL_LEN_FIN;
}

/* count number of elements in scatterlist */
static inline int __sg_count(struct scatterlist *sg_list, int nbytes)
{
	struct scatterlist *sg = sg_list;
	int sg_nents = 0;

	while (nbytes > 0) {
		sg_nents++;
		nbytes -= sg->length;
		if (!sg_is_last(sg) && (sg + 1)->length == 0)
			BUG(); /* Not support chaining */
		sg = scatterwalk_sg_next(sg);
	}

	return sg_nents;
}

/* derive number of elements in scatterlist, but return 0 for 1 */
static inline int sg_count(struct scatterlist *sg_list, int nbytes)
{
	int sg_nents = __sg_count(sg_list, nbytes);

	if (likely(sg_nents == 1))
		return 0;

	return sg_nents;
}
