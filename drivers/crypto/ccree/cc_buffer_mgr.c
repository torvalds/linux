// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2012-2018 ARM Limited or its affiliates. */

#include <crypto/authenc.h>
#include <crypto/scatterwalk.h>
#include <linux/dmapool.h>
#include <linux/dma-mapping.h>

#include "cc_buffer_mgr.h"
#include "cc_lli_defs.h"

enum dma_buffer_type {
	DMA_NULL_TYPE = -1,
	DMA_SGL_TYPE = 1,
	DMA_BUFF_TYPE = 2,
};

struct buff_mgr_handle {
	struct dma_pool *mlli_buffs_pool;
};

union buffer_array_entry {
	struct scatterlist *sgl;
	dma_addr_t buffer_dma;
};

struct buffer_array {
	unsigned int num_of_buffers;
	union buffer_array_entry entry[MAX_NUM_OF_BUFFERS_IN_MLLI];
	unsigned int offset[MAX_NUM_OF_BUFFERS_IN_MLLI];
	int nents[MAX_NUM_OF_BUFFERS_IN_MLLI];
	int total_data_len[MAX_NUM_OF_BUFFERS_IN_MLLI];
	enum dma_buffer_type type[MAX_NUM_OF_BUFFERS_IN_MLLI];
	bool is_last[MAX_NUM_OF_BUFFERS_IN_MLLI];
	u32 *mlli_nents[MAX_NUM_OF_BUFFERS_IN_MLLI];
};

static inline char *cc_dma_buf_type(enum cc_req_dma_buf_type type)
{
	switch (type) {
	case CC_DMA_BUF_NULL:
		return "BUF_NULL";
	case CC_DMA_BUF_DLLI:
		return "BUF_DLLI";
	case CC_DMA_BUF_MLLI:
		return "BUF_MLLI";
	default:
		return "BUF_INVALID";
	}
}

/**
 * cc_get_sgl_nents() - Get scatterlist number of entries.
 *
 * @sg_list: SG list
 * @nbytes: [IN] Total SGL data bytes.
 * @lbytes: [OUT] Returns the amount of bytes at the last entry
 */
static unsigned int cc_get_sgl_nents(struct device *dev,
				     struct scatterlist *sg_list,
				     unsigned int nbytes, u32 *lbytes,
				     bool *is_chained)
{
	unsigned int nents = 0;

	while (nbytes && sg_list) {
		if (sg_list->length) {
			nents++;
			/* get the number of bytes in the last entry */
			*lbytes = nbytes;
			nbytes -= (sg_list->length > nbytes) ?
					nbytes : sg_list->length;
			sg_list = sg_next(sg_list);
		} else {
			sg_list = (struct scatterlist *)sg_page(sg_list);
			if (is_chained)
				*is_chained = true;
		}
	}
	dev_dbg(dev, "nents %d last bytes %d\n", nents, *lbytes);
	return nents;
}

/**
 * cc_zero_sgl() - Zero scatter scatter list data.
 *
 * @sgl:
 */
void cc_zero_sgl(struct scatterlist *sgl, u32 data_len)
{
	struct scatterlist *current_sg = sgl;
	int sg_index = 0;

	while (sg_index <= data_len) {
		if (!current_sg) {
			/* reached the end of the sgl --> just return back */
			return;
		}
		memset(sg_virt(current_sg), 0, current_sg->length);
		sg_index += current_sg->length;
		current_sg = sg_next(current_sg);
	}
}

/**
 * cc_copy_sg_portion() - Copy scatter list data,
 * from to_skip to end, to dest and vice versa
 *
 * @dest:
 * @sg:
 * @to_skip:
 * @end:
 * @direct:
 */
void cc_copy_sg_portion(struct device *dev, u8 *dest, struct scatterlist *sg,
			u32 to_skip, u32 end, enum cc_sg_cpy_direct direct)
{
	u32 nents, lbytes;

	nents = cc_get_sgl_nents(dev, sg, end, &lbytes, NULL);
	sg_copy_buffer(sg, nents, (void *)dest, (end - to_skip + 1), to_skip,
		       (direct == CC_SG_TO_BUF));
}

static int cc_render_buff_to_mlli(struct device *dev, dma_addr_t buff_dma,
				  u32 buff_size, u32 *curr_nents,
				  u32 **mlli_entry_pp)
{
	u32 *mlli_entry_p = *mlli_entry_pp;
	u32 new_nents;

	/* Verify there is no memory overflow*/
	new_nents = (*curr_nents + buff_size / CC_MAX_MLLI_ENTRY_SIZE + 1);
	if (new_nents > MAX_NUM_OF_TOTAL_MLLI_ENTRIES)
		return -ENOMEM;

	/*handle buffer longer than 64 kbytes */
	while (buff_size > CC_MAX_MLLI_ENTRY_SIZE) {
		cc_lli_set_addr(mlli_entry_p, buff_dma);
		cc_lli_set_size(mlli_entry_p, CC_MAX_MLLI_ENTRY_SIZE);
		dev_dbg(dev, "entry[%d]: single_buff=0x%08X size=%08X\n",
			*curr_nents, mlli_entry_p[LLI_WORD0_OFFSET],
			mlli_entry_p[LLI_WORD1_OFFSET]);
		buff_dma += CC_MAX_MLLI_ENTRY_SIZE;
		buff_size -= CC_MAX_MLLI_ENTRY_SIZE;
		mlli_entry_p = mlli_entry_p + 2;
		(*curr_nents)++;
	}
	/*Last entry */
	cc_lli_set_addr(mlli_entry_p, buff_dma);
	cc_lli_set_size(mlli_entry_p, buff_size);
	dev_dbg(dev, "entry[%d]: single_buff=0x%08X size=%08X\n",
		*curr_nents, mlli_entry_p[LLI_WORD0_OFFSET],
		mlli_entry_p[LLI_WORD1_OFFSET]);
	mlli_entry_p = mlli_entry_p + 2;
	*mlli_entry_pp = mlli_entry_p;
	(*curr_nents)++;
	return 0;
}

static int cc_render_sg_to_mlli(struct device *dev, struct scatterlist *sgl,
				u32 sgl_data_len, u32 sgl_offset,
				u32 *curr_nents, u32 **mlli_entry_pp)
{
	struct scatterlist *curr_sgl = sgl;
	u32 *mlli_entry_p = *mlli_entry_pp;
	s32 rc = 0;

	for ( ; (curr_sgl && sgl_data_len);
	      curr_sgl = sg_next(curr_sgl)) {
		u32 entry_data_len =
			(sgl_data_len > sg_dma_len(curr_sgl) - sgl_offset) ?
				sg_dma_len(curr_sgl) - sgl_offset :
				sgl_data_len;
		sgl_data_len -= entry_data_len;
		rc = cc_render_buff_to_mlli(dev, sg_dma_address(curr_sgl) +
					    sgl_offset, entry_data_len,
					    curr_nents, &mlli_entry_p);
		if (rc)
			return rc;

		sgl_offset = 0;
	}
	*mlli_entry_pp = mlli_entry_p;
	return 0;
}

static int cc_generate_mlli(struct device *dev, struct buffer_array *sg_data,
			    struct mlli_params *mlli_params, gfp_t flags)
{
	u32 *mlli_p;
	u32 total_nents = 0, prev_total_nents = 0;
	int rc = 0, i;

	dev_dbg(dev, "NUM of SG's = %d\n", sg_data->num_of_buffers);

	/* Allocate memory from the pointed pool */
	mlli_params->mlli_virt_addr =
		dma_pool_alloc(mlli_params->curr_pool, flags,
			       &mlli_params->mlli_dma_addr);
	if (!mlli_params->mlli_virt_addr) {
		dev_err(dev, "dma_pool_alloc() failed\n");
		rc = -ENOMEM;
		goto build_mlli_exit;
	}
	/* Point to start of MLLI */
	mlli_p = (u32 *)mlli_params->mlli_virt_addr;
	/* go over all SG's and link it to one MLLI table */
	for (i = 0; i < sg_data->num_of_buffers; i++) {
		union buffer_array_entry *entry = &sg_data->entry[i];
		u32 tot_len = sg_data->total_data_len[i];
		u32 offset = sg_data->offset[i];

		if (sg_data->type[i] == DMA_SGL_TYPE)
			rc = cc_render_sg_to_mlli(dev, entry->sgl, tot_len,
						  offset, &total_nents,
						  &mlli_p);
		else /*DMA_BUFF_TYPE*/
			rc = cc_render_buff_to_mlli(dev, entry->buffer_dma,
						    tot_len, &total_nents,
						    &mlli_p);
		if (rc)
			return rc;

		/* set last bit in the current table */
		if (sg_data->mlli_nents[i]) {
			/*Calculate the current MLLI table length for the
			 *length field in the descriptor
			 */
			*sg_data->mlli_nents[i] +=
				(total_nents - prev_total_nents);
			prev_total_nents = total_nents;
		}
	}

	/* Set MLLI size for the bypass operation */
	mlli_params->mlli_len = (total_nents * LLI_ENTRY_BYTE_SIZE);

	dev_dbg(dev, "MLLI params: virt_addr=%pK dma_addr=%pad mlli_len=0x%X\n",
		mlli_params->mlli_virt_addr, &mlli_params->mlli_dma_addr,
		mlli_params->mlli_len);

build_mlli_exit:
	return rc;
}

static void cc_add_sg_entry(struct device *dev, struct buffer_array *sgl_data,
			    unsigned int nents, struct scatterlist *sgl,
			    unsigned int data_len, unsigned int data_offset,
			    bool is_last_table, u32 *mlli_nents)
{
	unsigned int index = sgl_data->num_of_buffers;

	dev_dbg(dev, "index=%u nents=%u sgl=%pK data_len=0x%08X is_last=%d\n",
		index, nents, sgl, data_len, is_last_table);
	sgl_data->nents[index] = nents;
	sgl_data->entry[index].sgl = sgl;
	sgl_data->offset[index] = data_offset;
	sgl_data->total_data_len[index] = data_len;
	sgl_data->type[index] = DMA_SGL_TYPE;
	sgl_data->is_last[index] = is_last_table;
	sgl_data->mlli_nents[index] = mlli_nents;
	if (sgl_data->mlli_nents[index])
		*sgl_data->mlli_nents[index] = 0;
	sgl_data->num_of_buffers++;
}

static int cc_dma_map_sg(struct device *dev, struct scatterlist *sg, u32 nents,
			 enum dma_data_direction direction)
{
	u32 i, j;
	struct scatterlist *l_sg = sg;

	for (i = 0; i < nents; i++) {
		if (!l_sg)
			break;
		if (dma_map_sg(dev, l_sg, 1, direction) != 1) {
			dev_err(dev, "dma_map_page() sg buffer failed\n");
			goto err;
		}
		l_sg = sg_next(l_sg);
	}
	return nents;

err:
	/* Restore mapped parts */
	for (j = 0; j < i; j++) {
		if (!sg)
			break;
		dma_unmap_sg(dev, sg, 1, direction);
		sg = sg_next(sg);
	}
	return 0;
}

static int cc_map_sg(struct device *dev, struct scatterlist *sg,
		     unsigned int nbytes, int direction, u32 *nents,
		     u32 max_sg_nents, u32 *lbytes, u32 *mapped_nents)
{
	bool is_chained = false;

	if (sg_is_last(sg)) {
		/* One entry only case -set to DLLI */
		if (dma_map_sg(dev, sg, 1, direction) != 1) {
			dev_err(dev, "dma_map_sg() single buffer failed\n");
			return -ENOMEM;
		}
		dev_dbg(dev, "Mapped sg: dma_address=%pad page=%p addr=%pK offset=%u length=%u\n",
			&sg_dma_address(sg), sg_page(sg), sg_virt(sg),
			sg->offset, sg->length);
		*lbytes = nbytes;
		*nents = 1;
		*mapped_nents = 1;
	} else {  /*sg_is_last*/
		*nents = cc_get_sgl_nents(dev, sg, nbytes, lbytes,
					  &is_chained);
		if (*nents > max_sg_nents) {
			*nents = 0;
			dev_err(dev, "Too many fragments. current %d max %d\n",
				*nents, max_sg_nents);
			return -ENOMEM;
		}
		if (!is_chained) {
			/* In case of mmu the number of mapped nents might
			 * be changed from the original sgl nents
			 */
			*mapped_nents = dma_map_sg(dev, sg, *nents, direction);
			if (*mapped_nents == 0) {
				*nents = 0;
				dev_err(dev, "dma_map_sg() sg buffer failed\n");
				return -ENOMEM;
			}
		} else {
			/*In this case the driver maps entry by entry so it
			 * must have the same nents before and after map
			 */
			*mapped_nents = cc_dma_map_sg(dev, sg, *nents,
						      direction);
			if (*mapped_nents != *nents) {
				*nents = *mapped_nents;
				dev_err(dev, "dma_map_sg() sg buffer failed\n");
				return -ENOMEM;
			}
		}
	}

	return 0;
}

int cc_buffer_mgr_init(struct cc_drvdata *drvdata)
{
	struct buff_mgr_handle *buff_mgr_handle;
	struct device *dev = drvdata_to_dev(drvdata);

	buff_mgr_handle = kmalloc(sizeof(*buff_mgr_handle), GFP_KERNEL);
	if (!buff_mgr_handle)
		return -ENOMEM;

	drvdata->buff_mgr_handle = buff_mgr_handle;

	buff_mgr_handle->mlli_buffs_pool =
		dma_pool_create("dx_single_mlli_tables", dev,
				MAX_NUM_OF_TOTAL_MLLI_ENTRIES *
				LLI_ENTRY_BYTE_SIZE,
				MLLI_TABLE_MIN_ALIGNMENT, 0);

	if (!buff_mgr_handle->mlli_buffs_pool)
		goto error;

	return 0;

error:
	cc_buffer_mgr_fini(drvdata);
	return -ENOMEM;
}

int cc_buffer_mgr_fini(struct cc_drvdata *drvdata)
{
	struct buff_mgr_handle *buff_mgr_handle = drvdata->buff_mgr_handle;

	if (buff_mgr_handle) {
		dma_pool_destroy(buff_mgr_handle->mlli_buffs_pool);
		kfree(drvdata->buff_mgr_handle);
		drvdata->buff_mgr_handle = NULL;
	}
	return 0;
}
