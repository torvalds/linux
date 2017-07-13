/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/crypto.h>
#include <linux/version.h>
#include <crypto/algapi.h>
#include <crypto/internal/aead.h>
#include <crypto/hash.h>
#include <crypto/authenc.h>
#include <crypto/scatterwalk.h>
#include <linux/dmapool.h>
#include <linux/dma-mapping.h>
#include <linux/crypto.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "ssi_buffer_mgr.h"
#include "cc_lli_defs.h"
#include "ssi_cipher.h"
#include "ssi_hash.h"
#include "ssi_aead.h"

#ifdef CC_DEBUG
#define GET_DMA_BUFFER_TYPE(buff_type) ( \
	((buff_type) == SSI_DMA_BUF_NULL) ? "BUF_NULL" : \
	((buff_type) == SSI_DMA_BUF_DLLI) ? "BUF_DLLI" : \
	((buff_type) == SSI_DMA_BUF_MLLI) ? "BUF_MLLI" : "BUF_INVALID")
#else
#define GET_DMA_BUFFER_TYPE(buff_type)
#endif

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

/**
 * ssi_buffer_mgr_get_sgl_nents() - Get scatterlist number of entries.
 *
 * @sg_list: SG list
 * @nbytes: [IN] Total SGL data bytes.
 * @lbytes: [OUT] Returns the amount of bytes at the last entry
 */
static unsigned int ssi_buffer_mgr_get_sgl_nents(
	struct scatterlist *sg_list, unsigned int nbytes, u32 *lbytes, bool *is_chained)
{
	unsigned int nents = 0;

	while (nbytes != 0) {
		if (sg_is_chain(sg_list)) {
			SSI_LOG_ERR("Unexpected chained entry "
				   "in sg (entry =0x%X)\n", nents);
			BUG();
		}
		if (sg_list->length != 0) {
			nents++;
			/* get the number of bytes in the last entry */
			*lbytes = nbytes;
			nbytes -= (sg_list->length > nbytes) ? nbytes : sg_list->length;
			sg_list = sg_next(sg_list);
		} else {
			sg_list = (struct scatterlist *)sg_page(sg_list);
			if (is_chained)
				*is_chained = true;
		}
	}
	SSI_LOG_DEBUG("nents %d last bytes %d\n", nents, *lbytes);
	return nents;
}

/**
 * ssi_buffer_mgr_zero_sgl() - Zero scatter scatter list data.
 *
 * @sgl:
 */
void ssi_buffer_mgr_zero_sgl(struct scatterlist *sgl, u32 data_len)
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
 * ssi_buffer_mgr_copy_scatterlist_portion() - Copy scatter list data,
 * from to_skip to end, to dest and vice versa
 *
 * @dest:
 * @sg:
 * @to_skip:
 * @end:
 * @direct:
 */
void ssi_buffer_mgr_copy_scatterlist_portion(
	u8 *dest, struct scatterlist *sg,
	u32 to_skip,  u32 end,
	enum ssi_sg_cpy_direct direct)
{
	u32 nents, lbytes;

	nents = ssi_buffer_mgr_get_sgl_nents(sg, end, &lbytes, NULL);
	sg_copy_buffer(sg, nents, (void *)dest, (end - to_skip + 1), to_skip,
		       (direct == SSI_SG_TO_BUF));
}

static inline int ssi_buffer_mgr_render_buff_to_mlli(
	dma_addr_t buff_dma, u32 buff_size, u32 *curr_nents,
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
		SSI_LOG_DEBUG("entry[%d]: single_buff=0x%08X size=%08X\n", *curr_nents,
			      mlli_entry_p[LLI_WORD0_OFFSET],
			      mlli_entry_p[LLI_WORD1_OFFSET]);
		buff_dma += CC_MAX_MLLI_ENTRY_SIZE;
		buff_size -= CC_MAX_MLLI_ENTRY_SIZE;
		mlli_entry_p = mlli_entry_p + 2;
		(*curr_nents)++;
	}
	/*Last entry */
	cc_lli_set_addr(mlli_entry_p, buff_dma);
	cc_lli_set_size(mlli_entry_p, buff_size);
	SSI_LOG_DEBUG("entry[%d]: single_buff=0x%08X size=%08X\n", *curr_nents,
		      mlli_entry_p[LLI_WORD0_OFFSET],
		      mlli_entry_p[LLI_WORD1_OFFSET]);
	mlli_entry_p = mlli_entry_p + 2;
	*mlli_entry_pp = mlli_entry_p;
	(*curr_nents)++;
	return 0;
}

static inline int ssi_buffer_mgr_render_scatterlist_to_mlli(
	struct scatterlist *sgl, u32 sgl_data_len, u32 sglOffset, u32 *curr_nents,
	u32 **mlli_entry_pp)
{
	struct scatterlist *curr_sgl = sgl;
	u32 *mlli_entry_p = *mlli_entry_pp;
	s32 rc = 0;

	for ( ; (curr_sgl) && (sgl_data_len != 0);
	      curr_sgl = sg_next(curr_sgl)) {
		u32 entry_data_len =
			(sgl_data_len > sg_dma_len(curr_sgl) - sglOffset) ?
				sg_dma_len(curr_sgl) - sglOffset : sgl_data_len;
		sgl_data_len -= entry_data_len;
		rc = ssi_buffer_mgr_render_buff_to_mlli(
			sg_dma_address(curr_sgl) + sglOffset, entry_data_len, curr_nents,
			&mlli_entry_p);
		if (rc != 0)
			return rc;

		sglOffset = 0;
	}
	*mlli_entry_pp = mlli_entry_p;
	return 0;
}

static int ssi_buffer_mgr_generate_mlli(
	struct device *dev,
	struct buffer_array *sg_data,
	struct mlli_params *mlli_params)
{
	u32 *mlli_p;
	u32 total_nents = 0, prev_total_nents = 0;
	int rc = 0, i;

	SSI_LOG_DEBUG("NUM of SG's = %d\n", sg_data->num_of_buffers);

	/* Allocate memory from the pointed pool */
	mlli_params->mlli_virt_addr = dma_pool_alloc(
			mlli_params->curr_pool, GFP_KERNEL,
			&mlli_params->mlli_dma_addr);
	if (unlikely(!mlli_params->mlli_virt_addr)) {
		SSI_LOG_ERR("dma_pool_alloc() failed\n");
		rc = -ENOMEM;
		goto build_mlli_exit;
	}
	/* Point to start of MLLI */
	mlli_p = (u32 *)mlli_params->mlli_virt_addr;
	/* go over all SG's and link it to one MLLI table */
	for (i = 0; i < sg_data->num_of_buffers; i++) {
		if (sg_data->type[i] == DMA_SGL_TYPE)
			rc = ssi_buffer_mgr_render_scatterlist_to_mlli(
				sg_data->entry[i].sgl,
				sg_data->total_data_len[i], sg_data->offset[i], &total_nents,
				&mlli_p);
		else /*DMA_BUFF_TYPE*/
			rc = ssi_buffer_mgr_render_buff_to_mlli(
				sg_data->entry[i].buffer_dma,
				sg_data->total_data_len[i], &total_nents,
				&mlli_p);
		if (rc != 0)
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

	SSI_LOG_DEBUG("MLLI params: "
		     "virt_addr=%pK dma_addr=0x%llX mlli_len=0x%X\n",
		   mlli_params->mlli_virt_addr,
		   (unsigned long long)mlli_params->mlli_dma_addr,
		   mlli_params->mlli_len);

build_mlli_exit:
	return rc;
}

static inline void ssi_buffer_mgr_add_buffer_entry(
	struct buffer_array *sgl_data,
	dma_addr_t buffer_dma, unsigned int buffer_len,
	bool is_last_entry, u32 *mlli_nents)
{
	unsigned int index = sgl_data->num_of_buffers;

	SSI_LOG_DEBUG("index=%u single_buff=0x%llX "
		     "buffer_len=0x%08X is_last=%d\n",
		     index, (unsigned long long)buffer_dma, buffer_len, is_last_entry);
	sgl_data->nents[index] = 1;
	sgl_data->entry[index].buffer_dma = buffer_dma;
	sgl_data->offset[index] = 0;
	sgl_data->total_data_len[index] = buffer_len;
	sgl_data->type[index] = DMA_BUFF_TYPE;
	sgl_data->is_last[index] = is_last_entry;
	sgl_data->mlli_nents[index] = mlli_nents;
	if (sgl_data->mlli_nents[index])
		*sgl_data->mlli_nents[index] = 0;
	sgl_data->num_of_buffers++;
}

static inline void ssi_buffer_mgr_add_scatterlist_entry(
	struct buffer_array *sgl_data,
	unsigned int nents,
	struct scatterlist *sgl,
	unsigned int data_len,
	unsigned int data_offset,
	bool is_last_table,
	u32 *mlli_nents)
{
	unsigned int index = sgl_data->num_of_buffers;

	SSI_LOG_DEBUG("index=%u nents=%u sgl=%pK data_len=0x%08X is_last=%d\n",
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

static int
ssi_buffer_mgr_dma_map_sg(struct device *dev, struct scatterlist *sg, u32 nents,
			  enum dma_data_direction direction)
{
	u32 i, j;
	struct scatterlist *l_sg = sg;

	for (i = 0; i < nents; i++) {
		if (!l_sg)
			break;
		if (unlikely(dma_map_sg(dev, l_sg, 1, direction) != 1)) {
			SSI_LOG_ERR("dma_map_page() sg buffer failed\n");
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

static int ssi_buffer_mgr_map_scatterlist(
	struct device *dev, struct scatterlist *sg,
	unsigned int nbytes, int direction,
	u32 *nents, u32 max_sg_nents,
	u32 *lbytes, u32 *mapped_nents)
{
	bool is_chained = false;

	if (sg_is_last(sg)) {
		/* One entry only case -set to DLLI */
		if (unlikely(dma_map_sg(dev, sg, 1, direction) != 1)) {
			SSI_LOG_ERR("dma_map_sg() single buffer failed\n");
			return -ENOMEM;
		}
		SSI_LOG_DEBUG("Mapped sg: dma_address=0x%llX "
			     "page=%p addr=%pK offset=%u "
			     "length=%u\n",
			     (unsigned long long)sg_dma_address(sg),
			     sg_page(sg),
			     sg_virt(sg),
			     sg->offset, sg->length);
		*lbytes = nbytes;
		*nents = 1;
		*mapped_nents = 1;
	} else {  /*sg_is_last*/
		*nents = ssi_buffer_mgr_get_sgl_nents(sg, nbytes, lbytes,
						     &is_chained);
		if (*nents > max_sg_nents) {
			*nents = 0;
			SSI_LOG_ERR("Too many fragments. current %d max %d\n",
				    *nents, max_sg_nents);
			return -ENOMEM;
		}
		if (!is_chained) {
			/* In case of mmu the number of mapped nents might
			 * be changed from the original sgl nents
			 */
			*mapped_nents = dma_map_sg(dev, sg, *nents, direction);
			if (unlikely(*mapped_nents == 0)) {
				*nents = 0;
				SSI_LOG_ERR("dma_map_sg() sg buffer failed\n");
				return -ENOMEM;
			}
		} else {
			/*In this case the driver maps entry by entry so it
			 * must have the same nents before and after map
			 */
			*mapped_nents = ssi_buffer_mgr_dma_map_sg(dev,
								 sg,
								 *nents,
								 direction);
			if (unlikely(*mapped_nents != *nents)) {
				*nents = *mapped_nents;
				SSI_LOG_ERR("dma_map_sg() sg buffer failed\n");
				return -ENOMEM;
			}
		}
	}

	return 0;
}

static inline int
ssi_aead_handle_config_buf(struct device *dev,
			   struct aead_req_ctx *areq_ctx,
			   u8 *config_data,
			   struct buffer_array *sg_data,
			   unsigned int assoclen)
{
	SSI_LOG_DEBUG(" handle additional data config set to   DLLI\n");
	/* create sg for the current buffer */
	sg_init_one(&areq_ctx->ccm_adata_sg, config_data, AES_BLOCK_SIZE + areq_ctx->ccm_hdr_size);
	if (unlikely(dma_map_sg(dev, &areq_ctx->ccm_adata_sg, 1,
				DMA_TO_DEVICE) != 1)) {
			SSI_LOG_ERR("dma_map_sg() "
			   "config buffer failed\n");
			return -ENOMEM;
	}
	SSI_LOG_DEBUG("Mapped curr_buff: dma_address=0x%llX "
		     "page=%p addr=%pK "
		     "offset=%u length=%u\n",
		     (unsigned long long)sg_dma_address(&areq_ctx->ccm_adata_sg),
		     sg_page(&areq_ctx->ccm_adata_sg),
		     sg_virt(&areq_ctx->ccm_adata_sg),
		     areq_ctx->ccm_adata_sg.offset,
		     areq_ctx->ccm_adata_sg.length);
	/* prepare for case of MLLI */
	if (assoclen > 0) {
		ssi_buffer_mgr_add_scatterlist_entry(sg_data, 1,
						     &areq_ctx->ccm_adata_sg,
						     (AES_BLOCK_SIZE + areq_ctx->ccm_hdr_size),
						     0, false, NULL);
	}
	return 0;
}

static inline int ssi_ahash_handle_curr_buf(struct device *dev,
					    struct ahash_req_ctx *areq_ctx,
					    u8 *curr_buff,
					    u32 curr_buff_cnt,
					    struct buffer_array *sg_data)
{
	SSI_LOG_DEBUG(" handle curr buff %x set to   DLLI\n", curr_buff_cnt);
	/* create sg for the current buffer */
	sg_init_one(areq_ctx->buff_sg, curr_buff, curr_buff_cnt);
	if (unlikely(dma_map_sg(dev, areq_ctx->buff_sg, 1,
				DMA_TO_DEVICE) != 1)) {
			SSI_LOG_ERR("dma_map_sg() "
			   "src buffer failed\n");
			return -ENOMEM;
	}
	SSI_LOG_DEBUG("Mapped curr_buff: dma_address=0x%llX "
		     "page=%p addr=%pK "
		     "offset=%u length=%u\n",
		     (unsigned long long)sg_dma_address(areq_ctx->buff_sg),
		     sg_page(areq_ctx->buff_sg),
		     sg_virt(areq_ctx->buff_sg),
		     areq_ctx->buff_sg->offset,
		     areq_ctx->buff_sg->length);
	areq_ctx->data_dma_buf_type = SSI_DMA_BUF_DLLI;
	areq_ctx->curr_sg = areq_ctx->buff_sg;
	areq_ctx->in_nents = 0;
	/* prepare for case of MLLI */
	ssi_buffer_mgr_add_scatterlist_entry(sg_data, 1, areq_ctx->buff_sg,
					     curr_buff_cnt, 0, false, NULL);
	return 0;
}

void ssi_buffer_mgr_unmap_blkcipher_request(
	struct device *dev,
	void *ctx,
	unsigned int ivsize,
	struct scatterlist *src,
	struct scatterlist *dst)
{
	struct blkcipher_req_ctx *req_ctx = (struct blkcipher_req_ctx *)ctx;

	if (likely(req_ctx->gen_ctx.iv_dma_addr != 0)) {
		SSI_LOG_DEBUG("Unmapped iv: iv_dma_addr=0x%llX iv_size=%u\n",
			      (unsigned long long)req_ctx->gen_ctx.iv_dma_addr,
			      ivsize);
		dma_unmap_single(dev, req_ctx->gen_ctx.iv_dma_addr,
				 ivsize,
				 req_ctx->is_giv ? DMA_BIDIRECTIONAL :
				 DMA_TO_DEVICE);
	}
	/* Release pool */
	if (req_ctx->dma_buf_type == SSI_DMA_BUF_MLLI) {
		dma_pool_free(req_ctx->mlli_params.curr_pool,
			      req_ctx->mlli_params.mlli_virt_addr,
			      req_ctx->mlli_params.mlli_dma_addr);
	}

	dma_unmap_sg(dev, src, req_ctx->in_nents, DMA_BIDIRECTIONAL);
	SSI_LOG_DEBUG("Unmapped req->src=%pK\n", sg_virt(src));

	if (src != dst) {
		dma_unmap_sg(dev, dst, req_ctx->out_nents, DMA_BIDIRECTIONAL);
		SSI_LOG_DEBUG("Unmapped req->dst=%pK\n", sg_virt(dst));
	}
}

int ssi_buffer_mgr_map_blkcipher_request(
	struct ssi_drvdata *drvdata,
	void *ctx,
	unsigned int ivsize,
	unsigned int nbytes,
	void *info,
	struct scatterlist *src,
	struct scatterlist *dst)
{
	struct blkcipher_req_ctx *req_ctx = (struct blkcipher_req_ctx *)ctx;
	struct mlli_params *mlli_params = &req_ctx->mlli_params;
	struct buff_mgr_handle *buff_mgr = drvdata->buff_mgr_handle;
	struct device *dev = &drvdata->plat_dev->dev;
	struct buffer_array sg_data;
	u32 dummy = 0;
	int rc = 0;
	u32 mapped_nents = 0;

	req_ctx->dma_buf_type = SSI_DMA_BUF_DLLI;
	mlli_params->curr_pool = NULL;
	sg_data.num_of_buffers = 0;

	/* Map IV buffer */
	if (likely(ivsize != 0)) {
		dump_byte_array("iv", (u8 *)info, ivsize);
		req_ctx->gen_ctx.iv_dma_addr =
			dma_map_single(dev, (void *)info,
				       ivsize,
				       req_ctx->is_giv ? DMA_BIDIRECTIONAL :
				       DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(dev,
					       req_ctx->gen_ctx.iv_dma_addr))) {
			SSI_LOG_ERR("Mapping iv %u B at va=%pK "
				   "for DMA failed\n", ivsize, info);
			return -ENOMEM;
		}
		SSI_LOG_DEBUG("Mapped iv %u B at va=%pK to dma=0x%llX\n",
			      ivsize, info,
			      (unsigned long long)req_ctx->gen_ctx.iv_dma_addr);
	} else {
		req_ctx->gen_ctx.iv_dma_addr = 0;
	}

	/* Map the src SGL */
	rc = ssi_buffer_mgr_map_scatterlist(dev, src,
					    nbytes, DMA_BIDIRECTIONAL,
					    &req_ctx->in_nents,
					    LLI_MAX_NUM_OF_DATA_ENTRIES, &dummy,
					    &mapped_nents);
	if (unlikely(rc != 0)) {
		rc = -ENOMEM;
		goto ablkcipher_exit;
	}
	if (mapped_nents > 1)
		req_ctx->dma_buf_type = SSI_DMA_BUF_MLLI;

	if (unlikely(src == dst)) {
		/* Handle inplace operation */
		if (unlikely(req_ctx->dma_buf_type == SSI_DMA_BUF_MLLI)) {
			req_ctx->out_nents = 0;
			ssi_buffer_mgr_add_scatterlist_entry(&sg_data,
							     req_ctx->in_nents,
							     src, nbytes, 0,
							     true,
							     &req_ctx->in_mlli_nents);
		}
	} else {
		/* Map the dst sg */
		if (unlikely(ssi_buffer_mgr_map_scatterlist(
			dev, dst, nbytes,
			DMA_BIDIRECTIONAL, &req_ctx->out_nents,
			LLI_MAX_NUM_OF_DATA_ENTRIES, &dummy,
			&mapped_nents))){
			rc = -ENOMEM;
			goto ablkcipher_exit;
		}
		if (mapped_nents > 1)
			req_ctx->dma_buf_type = SSI_DMA_BUF_MLLI;

		if (unlikely((req_ctx->dma_buf_type == SSI_DMA_BUF_MLLI))) {
			ssi_buffer_mgr_add_scatterlist_entry(&sg_data,
							     req_ctx->in_nents,
							     src, nbytes, 0,
							     true,
							     &req_ctx->in_mlli_nents);
			ssi_buffer_mgr_add_scatterlist_entry(&sg_data,
							     req_ctx->out_nents,
							     dst, nbytes, 0,
							     true,
							     &req_ctx->out_mlli_nents);
		}
	}

	if (unlikely(req_ctx->dma_buf_type == SSI_DMA_BUF_MLLI)) {
		mlli_params->curr_pool = buff_mgr->mlli_buffs_pool;
		rc = ssi_buffer_mgr_generate_mlli(dev, &sg_data, mlli_params);
		if (unlikely(rc != 0))
			goto ablkcipher_exit;
	}

	SSI_LOG_DEBUG("areq_ctx->dma_buf_type = %s\n",
		      GET_DMA_BUFFER_TYPE(req_ctx->dma_buf_type));

	return 0;

ablkcipher_exit:
	ssi_buffer_mgr_unmap_blkcipher_request(dev, req_ctx, ivsize, src, dst);
	return rc;
}

void ssi_buffer_mgr_unmap_aead_request(
	struct device *dev, struct aead_request *req)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	unsigned int hw_iv_size = areq_ctx->hw_iv_size;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	struct ssi_drvdata *drvdata = dev_get_drvdata(dev);
	u32 dummy;
	bool chained;
	u32 size_to_unmap = 0;

	if (areq_ctx->mac_buf_dma_addr != 0) {
		dma_unmap_single(dev, areq_ctx->mac_buf_dma_addr,
				 MAX_MAC_SIZE, DMA_BIDIRECTIONAL);
	}

#if SSI_CC_HAS_AES_GCM
	if (areq_ctx->cipher_mode == DRV_CIPHER_GCTR) {
		if (areq_ctx->hkey_dma_addr != 0) {
			dma_unmap_single(dev, areq_ctx->hkey_dma_addr,
					 AES_BLOCK_SIZE, DMA_BIDIRECTIONAL);
		}

		if (areq_ctx->gcm_block_len_dma_addr != 0) {
			dma_unmap_single(dev, areq_ctx->gcm_block_len_dma_addr,
					 AES_BLOCK_SIZE, DMA_TO_DEVICE);
		}

		if (areq_ctx->gcm_iv_inc1_dma_addr != 0) {
			dma_unmap_single(dev, areq_ctx->gcm_iv_inc1_dma_addr,
					 AES_BLOCK_SIZE, DMA_TO_DEVICE);
		}

		if (areq_ctx->gcm_iv_inc2_dma_addr != 0) {
			dma_unmap_single(dev, areq_ctx->gcm_iv_inc2_dma_addr,
					 AES_BLOCK_SIZE, DMA_TO_DEVICE);
		}
	}
#endif

	if (areq_ctx->ccm_hdr_size != ccm_header_size_null) {
		if (areq_ctx->ccm_iv0_dma_addr != 0) {
			dma_unmap_single(dev, areq_ctx->ccm_iv0_dma_addr,
					 AES_BLOCK_SIZE, DMA_TO_DEVICE);
		}

		dma_unmap_sg(dev, &areq_ctx->ccm_adata_sg, 1, DMA_TO_DEVICE);
	}
	if (areq_ctx->gen_ctx.iv_dma_addr != 0) {
		dma_unmap_single(dev, areq_ctx->gen_ctx.iv_dma_addr,
				 hw_iv_size, DMA_BIDIRECTIONAL);
	}

	/*In case a pool was set, a table was
	 *allocated and should be released
	 */
	if (areq_ctx->mlli_params.curr_pool) {
		SSI_LOG_DEBUG("free MLLI buffer: dma=0x%08llX virt=%pK\n",
			      (unsigned long long)areq_ctx->mlli_params.mlli_dma_addr,
			      areq_ctx->mlli_params.mlli_virt_addr);
		dma_pool_free(areq_ctx->mlli_params.curr_pool,
			      areq_ctx->mlli_params.mlli_virt_addr,
			      areq_ctx->mlli_params.mlli_dma_addr);
	}

	SSI_LOG_DEBUG("Unmapping src sgl: req->src=%pK areq_ctx->src.nents=%u areq_ctx->assoc.nents=%u assoclen:%u cryptlen=%u\n", sg_virt(req->src), areq_ctx->src.nents, areq_ctx->assoc.nents, req->assoclen, req->cryptlen);
	size_to_unmap = req->assoclen + req->cryptlen;
	if (areq_ctx->gen_ctx.op_type == DRV_CRYPTO_DIRECTION_ENCRYPT)
		size_to_unmap += areq_ctx->req_authsize;
	if (areq_ctx->is_gcm4543)
		size_to_unmap += crypto_aead_ivsize(tfm);

	dma_unmap_sg(dev, req->src, ssi_buffer_mgr_get_sgl_nents(req->src, size_to_unmap, &dummy, &chained), DMA_BIDIRECTIONAL);
	if (unlikely(req->src != req->dst)) {
		SSI_LOG_DEBUG("Unmapping dst sgl: req->dst=%pK\n",
			      sg_virt(req->dst));
		dma_unmap_sg(dev, req->dst,
			     ssi_buffer_mgr_get_sgl_nents(req->dst,
							  size_to_unmap,
							  &dummy,
							  &chained),
			     DMA_BIDIRECTIONAL);
	}
	if (drvdata->coherent &&
	    (areq_ctx->gen_ctx.op_type == DRV_CRYPTO_DIRECTION_DECRYPT) &&
	    likely(req->src == req->dst)) {
		u32 size_to_skip = req->assoclen;

		if (areq_ctx->is_gcm4543)
			size_to_skip += crypto_aead_ivsize(tfm);

		/* copy mac to a temporary location to deal with possible
		 * data memory overriding that caused by cache coherence problem.
		 */
		ssi_buffer_mgr_copy_scatterlist_portion(
			areq_ctx->backup_mac, req->src,
			size_to_skip + req->cryptlen - areq_ctx->req_authsize,
			size_to_skip + req->cryptlen, SSI_SG_FROM_BUF);
	}
}

static inline int ssi_buffer_mgr_get_aead_icv_nents(
	struct scatterlist *sgl,
	unsigned int sgl_nents,
	unsigned int authsize,
	u32 last_entry_data_size,
	bool *is_icv_fragmented)
{
	unsigned int icv_max_size = 0;
	unsigned int icv_required_size = authsize > last_entry_data_size ? (authsize - last_entry_data_size) : authsize;
	unsigned int nents;
	unsigned int i;

	if (sgl_nents < MAX_ICV_NENTS_SUPPORTED) {
		*is_icv_fragmented = false;
		return 0;
	}

	for (i = 0 ; i < (sgl_nents - MAX_ICV_NENTS_SUPPORTED) ; i++) {
		if (!sgl)
			break;
		sgl = sg_next(sgl);
	}

	if (sgl)
		icv_max_size = sgl->length;

	if (last_entry_data_size > authsize) {
		nents = 0; /* ICV attached to data in last entry (not fragmented!) */
		*is_icv_fragmented = false;
	} else if (last_entry_data_size == authsize) {
		nents = 1; /* ICV placed in whole last entry (not fragmented!) */
		*is_icv_fragmented = false;
	} else if (icv_max_size > icv_required_size) {
		nents = 1;
		*is_icv_fragmented = true;
	} else if (icv_max_size == icv_required_size) {
		nents = 2;
		*is_icv_fragmented = true;
	} else {
		SSI_LOG_ERR("Unsupported num. of ICV fragments (> %d)\n",
			    MAX_ICV_NENTS_SUPPORTED);
		nents = -1; /*unsupported*/
	}
	SSI_LOG_DEBUG("is_frag=%s icv_nents=%u\n",
		      (*is_icv_fragmented ? "true" : "false"), nents);

	return nents;
}

static inline int ssi_buffer_mgr_aead_chain_iv(
	struct ssi_drvdata *drvdata,
	struct aead_request *req,
	struct buffer_array *sg_data,
	bool is_last, bool do_chain)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	unsigned int hw_iv_size = areq_ctx->hw_iv_size;
	struct device *dev = &drvdata->plat_dev->dev;
	int rc = 0;

	if (unlikely(!req->iv)) {
		areq_ctx->gen_ctx.iv_dma_addr = 0;
		goto chain_iv_exit;
	}

	areq_ctx->gen_ctx.iv_dma_addr = dma_map_single(dev, req->iv,
		hw_iv_size, DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(dev, areq_ctx->gen_ctx.iv_dma_addr))) {
		SSI_LOG_ERR("Mapping iv %u B at va=%pK for DMA failed\n",
			    hw_iv_size, req->iv);
		rc = -ENOMEM;
		goto chain_iv_exit;
	}

	SSI_LOG_DEBUG("Mapped iv %u B at va=%pK to dma=0x%llX\n",
		      hw_iv_size, req->iv,
		      (unsigned long long)areq_ctx->gen_ctx.iv_dma_addr);
	if (do_chain && areq_ctx->plaintext_authenticate_only) {  // TODO: what about CTR?? ask Ron
		struct crypto_aead *tfm = crypto_aead_reqtfm(req);
		unsigned int iv_size_to_authenc = crypto_aead_ivsize(tfm);
		unsigned int iv_ofs = GCM_BLOCK_RFC4_IV_OFFSET;
		/* Chain to given list */
		ssi_buffer_mgr_add_buffer_entry(
			sg_data, areq_ctx->gen_ctx.iv_dma_addr + iv_ofs,
			iv_size_to_authenc, is_last,
			&areq_ctx->assoc.mlli_nents);
		areq_ctx->assoc_buff_type = SSI_DMA_BUF_MLLI;
	}

chain_iv_exit:
	return rc;
}

static inline int ssi_buffer_mgr_aead_chain_assoc(
	struct ssi_drvdata *drvdata,
	struct aead_request *req,
	struct buffer_array *sg_data,
	bool is_last, bool do_chain)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	int rc = 0;
	u32 mapped_nents = 0;
	struct scatterlist *current_sg = req->src;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	unsigned int sg_index = 0;
	u32 size_of_assoc = req->assoclen;

	if (areq_ctx->is_gcm4543)
		size_of_assoc += crypto_aead_ivsize(tfm);

	if (!sg_data) {
		rc = -EINVAL;
		goto chain_assoc_exit;
	}

	if (unlikely(req->assoclen == 0)) {
		areq_ctx->assoc_buff_type = SSI_DMA_BUF_NULL;
		areq_ctx->assoc.nents = 0;
		areq_ctx->assoc.mlli_nents = 0;
		SSI_LOG_DEBUG("Chain assoc of length 0: buff_type=%s nents=%u\n",
			      GET_DMA_BUFFER_TYPE(areq_ctx->assoc_buff_type),
			      areq_ctx->assoc.nents);
		goto chain_assoc_exit;
	}

	//iterate over the sgl to see how many entries are for associated data
	//it is assumed that if we reach here , the sgl is already mapped
	sg_index = current_sg->length;
	if (sg_index > size_of_assoc) { //the first entry in the scatter list contains all the associated data
		mapped_nents++;
	} else {
		while (sg_index <= size_of_assoc) {
			current_sg = sg_next(current_sg);
			//if have reached the end of the sgl, then this is unexpected
			if (!current_sg) {
				SSI_LOG_ERR("reached end of sg list. unexpected\n");
				BUG();
			}
			sg_index += current_sg->length;
			mapped_nents++;
		}
	}
	if (unlikely(mapped_nents > LLI_MAX_NUM_OF_ASSOC_DATA_ENTRIES)) {
		SSI_LOG_ERR("Too many fragments. current %d max %d\n",
			    mapped_nents, LLI_MAX_NUM_OF_ASSOC_DATA_ENTRIES);
		return -ENOMEM;
	}
	areq_ctx->assoc.nents = mapped_nents;

	/* in CCM case we have additional entry for
	 * ccm header configurations
	 */
	if (areq_ctx->ccm_hdr_size != ccm_header_size_null) {
		if (unlikely((mapped_nents + 1) >
			LLI_MAX_NUM_OF_ASSOC_DATA_ENTRIES)) {
			SSI_LOG_ERR("CCM case.Too many fragments. Current %d max %d\n",
				    (areq_ctx->assoc.nents + 1),
				    LLI_MAX_NUM_OF_ASSOC_DATA_ENTRIES);
			rc = -ENOMEM;
			goto chain_assoc_exit;
		}
	}

	if (likely(mapped_nents == 1) &&
	    (areq_ctx->ccm_hdr_size == ccm_header_size_null))
		areq_ctx->assoc_buff_type = SSI_DMA_BUF_DLLI;
	else
		areq_ctx->assoc_buff_type = SSI_DMA_BUF_MLLI;

	if (unlikely((do_chain) ||
		     (areq_ctx->assoc_buff_type == SSI_DMA_BUF_MLLI))) {
		SSI_LOG_DEBUG("Chain assoc: buff_type=%s nents=%u\n",
			      GET_DMA_BUFFER_TYPE(areq_ctx->assoc_buff_type),
			      areq_ctx->assoc.nents);
		ssi_buffer_mgr_add_scatterlist_entry(
			sg_data, areq_ctx->assoc.nents,
			req->src, req->assoclen, 0, is_last,
			&areq_ctx->assoc.mlli_nents);
		areq_ctx->assoc_buff_type = SSI_DMA_BUF_MLLI;
	}

chain_assoc_exit:
	return rc;
}

static inline void ssi_buffer_mgr_prepare_aead_data_dlli(
	struct aead_request *req,
	u32 *src_last_bytes, u32 *dst_last_bytes)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	enum drv_crypto_direction direct = areq_ctx->gen_ctx.op_type;
	unsigned int authsize = areq_ctx->req_authsize;

	areq_ctx->is_icv_fragmented = false;
	if (likely(req->src == req->dst)) {
		/*INPLACE*/
		areq_ctx->icv_dma_addr = sg_dma_address(
			areq_ctx->srcSgl) +
			(*src_last_bytes - authsize);
		areq_ctx->icv_virt_addr = sg_virt(
			areq_ctx->srcSgl) +
			(*src_last_bytes - authsize);
	} else if (direct == DRV_CRYPTO_DIRECTION_DECRYPT) {
		/*NON-INPLACE and DECRYPT*/
		areq_ctx->icv_dma_addr = sg_dma_address(
			areq_ctx->srcSgl) +
			(*src_last_bytes - authsize);
		areq_ctx->icv_virt_addr = sg_virt(
			areq_ctx->srcSgl) +
			(*src_last_bytes - authsize);
	} else {
		/*NON-INPLACE and ENCRYPT*/
		areq_ctx->icv_dma_addr = sg_dma_address(
			areq_ctx->dstSgl) +
			(*dst_last_bytes - authsize);
		areq_ctx->icv_virt_addr = sg_virt(
			areq_ctx->dstSgl) +
			(*dst_last_bytes - authsize);
	}
}

static inline int ssi_buffer_mgr_prepare_aead_data_mlli(
	struct ssi_drvdata *drvdata,
	struct aead_request *req,
	struct buffer_array *sg_data,
	u32 *src_last_bytes, u32 *dst_last_bytes,
	bool is_last_table)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	enum drv_crypto_direction direct = areq_ctx->gen_ctx.op_type;
	unsigned int authsize = areq_ctx->req_authsize;
	int rc = 0, icv_nents;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);

	if (likely(req->src == req->dst)) {
		/*INPLACE*/
		ssi_buffer_mgr_add_scatterlist_entry(sg_data,
						     areq_ctx->src.nents,
						     areq_ctx->srcSgl,
						     areq_ctx->cryptlen,
						     areq_ctx->srcOffset,
						     is_last_table,
						     &areq_ctx->src.mlli_nents);

		icv_nents = ssi_buffer_mgr_get_aead_icv_nents(areq_ctx->srcSgl,
							      areq_ctx->src.nents,
							      authsize,
							      *src_last_bytes,
							      &areq_ctx->is_icv_fragmented);
		if (unlikely(icv_nents < 0)) {
			rc = -ENOTSUPP;
			goto prepare_data_mlli_exit;
		}

		if (unlikely(areq_ctx->is_icv_fragmented)) {
			/* Backup happens only when ICV is fragmented, ICV
			 * verification is made by CPU compare in order to simplify
			 * MAC verification upon request completion
			 */
			if (direct == DRV_CRYPTO_DIRECTION_DECRYPT) {
				if (!drvdata->coherent) {
				/* In coherent platforms (e.g. ACP)
				 * already copying ICV for any
				 * INPLACE-DECRYPT operation, hence
				 * we must neglect this code.
				 */
					u32 skip = req->assoclen;

					if (areq_ctx->is_gcm4543)
						skip += crypto_aead_ivsize(tfm);

					ssi_buffer_mgr_copy_scatterlist_portion(
						areq_ctx->backup_mac, req->src,
						(skip + req->cryptlen -
						 areq_ctx->req_authsize),
						skip + req->cryptlen,
						SSI_SG_TO_BUF);
				}
				areq_ctx->icv_virt_addr = areq_ctx->backup_mac;
			} else {
				areq_ctx->icv_virt_addr = areq_ctx->mac_buf;
				areq_ctx->icv_dma_addr = areq_ctx->mac_buf_dma_addr;
			}
		} else { /* Contig. ICV */
			/*Should hanlde if the sg is not contig.*/
			areq_ctx->icv_dma_addr = sg_dma_address(
				&areq_ctx->srcSgl[areq_ctx->src.nents - 1]) +
				(*src_last_bytes - authsize);
			areq_ctx->icv_virt_addr = sg_virt(
				&areq_ctx->srcSgl[areq_ctx->src.nents - 1]) +
				(*src_last_bytes - authsize);
		}

	} else if (direct == DRV_CRYPTO_DIRECTION_DECRYPT) {
		/*NON-INPLACE and DECRYPT*/
		ssi_buffer_mgr_add_scatterlist_entry(sg_data,
						     areq_ctx->src.nents,
						     areq_ctx->srcSgl,
						     areq_ctx->cryptlen,
						     areq_ctx->srcOffset,
						     is_last_table,
						     &areq_ctx->src.mlli_nents);
		ssi_buffer_mgr_add_scatterlist_entry(sg_data,
						     areq_ctx->dst.nents,
						     areq_ctx->dstSgl,
						     areq_ctx->cryptlen,
						     areq_ctx->dstOffset,
						     is_last_table,
						     &areq_ctx->dst.mlli_nents);

		icv_nents = ssi_buffer_mgr_get_aead_icv_nents(areq_ctx->srcSgl,
							      areq_ctx->src.nents,
							      authsize,
							      *src_last_bytes,
							      &areq_ctx->is_icv_fragmented);
		if (unlikely(icv_nents < 0)) {
			rc = -ENOTSUPP;
			goto prepare_data_mlli_exit;
		}

		if (unlikely(areq_ctx->is_icv_fragmented)) {
			/* Backup happens only when ICV is fragmented, ICV
			 * verification is made by CPU compare in order to simplify
			 * MAC verification upon request completion
			 */
			  u32 size_to_skip = req->assoclen;

			  if (areq_ctx->is_gcm4543)
				  size_to_skip += crypto_aead_ivsize(tfm);

			  ssi_buffer_mgr_copy_scatterlist_portion(
				  areq_ctx->backup_mac, req->src,
				  size_to_skip + req->cryptlen - areq_ctx->req_authsize,
				  size_to_skip + req->cryptlen, SSI_SG_TO_BUF);
			areq_ctx->icv_virt_addr = areq_ctx->backup_mac;
		} else { /* Contig. ICV */
			/*Should hanlde if the sg is not contig.*/
			areq_ctx->icv_dma_addr = sg_dma_address(
				&areq_ctx->srcSgl[areq_ctx->src.nents - 1]) +
				(*src_last_bytes - authsize);
			areq_ctx->icv_virt_addr = sg_virt(
				&areq_ctx->srcSgl[areq_ctx->src.nents - 1]) +
				(*src_last_bytes - authsize);
		}

	} else {
		/*NON-INPLACE and ENCRYPT*/
		ssi_buffer_mgr_add_scatterlist_entry(sg_data,
						     areq_ctx->dst.nents,
						     areq_ctx->dstSgl,
						     areq_ctx->cryptlen,
						     areq_ctx->dstOffset,
						     is_last_table,
						     &areq_ctx->dst.mlli_nents);
		ssi_buffer_mgr_add_scatterlist_entry(sg_data,
						     areq_ctx->src.nents,
						     areq_ctx->srcSgl,
						     areq_ctx->cryptlen,
						     areq_ctx->srcOffset,
						     is_last_table,
						     &areq_ctx->src.mlli_nents);

		icv_nents = ssi_buffer_mgr_get_aead_icv_nents(areq_ctx->dstSgl,
							      areq_ctx->dst.nents,
							      authsize,
							      *dst_last_bytes,
			&areq_ctx->is_icv_fragmented);
		if (unlikely(icv_nents < 0)) {
			rc = -ENOTSUPP;
			goto prepare_data_mlli_exit;
		}

		if (likely(!areq_ctx->is_icv_fragmented)) {
			/* Contig. ICV */
			areq_ctx->icv_dma_addr = sg_dma_address(
				&areq_ctx->dstSgl[areq_ctx->dst.nents - 1]) +
				(*dst_last_bytes - authsize);
			areq_ctx->icv_virt_addr = sg_virt(
				&areq_ctx->dstSgl[areq_ctx->dst.nents - 1]) +
				(*dst_last_bytes - authsize);
		} else {
			areq_ctx->icv_dma_addr = areq_ctx->mac_buf_dma_addr;
			areq_ctx->icv_virt_addr = areq_ctx->mac_buf;
		}
	}

prepare_data_mlli_exit:
	return rc;
}

static inline int ssi_buffer_mgr_aead_chain_data(
	struct ssi_drvdata *drvdata,
	struct aead_request *req,
	struct buffer_array *sg_data,
	bool is_last_table, bool do_chain)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	struct device *dev = &drvdata->plat_dev->dev;
	enum drv_crypto_direction direct = areq_ctx->gen_ctx.op_type;
	unsigned int authsize = areq_ctx->req_authsize;
	int src_last_bytes = 0, dst_last_bytes = 0;
	int rc = 0;
	u32 src_mapped_nents = 0, dst_mapped_nents = 0;
	u32 offset = 0;
	unsigned int size_for_map = req->assoclen + req->cryptlen; /*non-inplace mode*/
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	u32 sg_index = 0;
	bool chained = false;
	bool is_gcm4543 = areq_ctx->is_gcm4543;
	u32 size_to_skip = req->assoclen;

	if (is_gcm4543)
		size_to_skip += crypto_aead_ivsize(tfm);

	offset = size_to_skip;

	if (!sg_data) {
		rc = -EINVAL;
		goto chain_data_exit;
	}
	areq_ctx->srcSgl = req->src;
	areq_ctx->dstSgl = req->dst;

	if (is_gcm4543)
		size_for_map += crypto_aead_ivsize(tfm);

	size_for_map += (direct == DRV_CRYPTO_DIRECTION_ENCRYPT) ? authsize : 0;
	src_mapped_nents = ssi_buffer_mgr_get_sgl_nents(req->src, size_for_map, &src_last_bytes, &chained);
	sg_index = areq_ctx->srcSgl->length;
	//check where the data starts
	while (sg_index <= size_to_skip) {
		offset -= areq_ctx->srcSgl->length;
		areq_ctx->srcSgl = sg_next(areq_ctx->srcSgl);
		//if have reached the end of the sgl, then this is unexpected
		if (!areq_ctx->srcSgl) {
			SSI_LOG_ERR("reached end of sg list. unexpected\n");
			BUG();
		}
		sg_index += areq_ctx->srcSgl->length;
		src_mapped_nents--;
	}
	if (unlikely(src_mapped_nents > LLI_MAX_NUM_OF_DATA_ENTRIES)) {
		SSI_LOG_ERR("Too many fragments. current %d max %d\n",
			    src_mapped_nents, LLI_MAX_NUM_OF_DATA_ENTRIES);
			return -ENOMEM;
	}

	areq_ctx->src.nents = src_mapped_nents;

	areq_ctx->srcOffset = offset;

	if (req->src != req->dst) {
		size_for_map = req->assoclen + req->cryptlen;
		size_for_map += (direct == DRV_CRYPTO_DIRECTION_ENCRYPT) ? authsize : 0;
		if (is_gcm4543)
			size_for_map += crypto_aead_ivsize(tfm);

		rc = ssi_buffer_mgr_map_scatterlist(dev, req->dst, size_for_map,
						    DMA_BIDIRECTIONAL,
						    &areq_ctx->dst.nents,
						    LLI_MAX_NUM_OF_DATA_ENTRIES,
						    &dst_last_bytes,
						    &dst_mapped_nents);
		if (unlikely(rc != 0)) {
			rc = -ENOMEM;
			goto chain_data_exit;
		}
	}

	dst_mapped_nents = ssi_buffer_mgr_get_sgl_nents(req->dst, size_for_map, &dst_last_bytes, &chained);
	sg_index = areq_ctx->dstSgl->length;
	offset = size_to_skip;

	//check where the data starts
	while (sg_index <= size_to_skip) {
		offset -= areq_ctx->dstSgl->length;
		areq_ctx->dstSgl = sg_next(areq_ctx->dstSgl);
		//if have reached the end of the sgl, then this is unexpected
		if (!areq_ctx->dstSgl) {
			SSI_LOG_ERR("reached end of sg list. unexpected\n");
			BUG();
		}
		sg_index += areq_ctx->dstSgl->length;
		dst_mapped_nents--;
	}
	if (unlikely(dst_mapped_nents > LLI_MAX_NUM_OF_DATA_ENTRIES)) {
		SSI_LOG_ERR("Too many fragments. current %d max %d\n",
			    dst_mapped_nents, LLI_MAX_NUM_OF_DATA_ENTRIES);
		return -ENOMEM;
	}
	areq_ctx->dst.nents = dst_mapped_nents;
	areq_ctx->dstOffset = offset;
	if ((src_mapped_nents > 1) ||
	    (dst_mapped_nents  > 1) ||
	    do_chain) {
		areq_ctx->data_buff_type = SSI_DMA_BUF_MLLI;
		rc = ssi_buffer_mgr_prepare_aead_data_mlli(drvdata, req,
							   sg_data,
							   &src_last_bytes,
							   &dst_last_bytes,
							   is_last_table);
	} else {
		areq_ctx->data_buff_type = SSI_DMA_BUF_DLLI;
		ssi_buffer_mgr_prepare_aead_data_dlli(
				req, &src_last_bytes, &dst_last_bytes);
	}

chain_data_exit:
	return rc;
}

static void ssi_buffer_mgr_update_aead_mlli_nents(struct ssi_drvdata *drvdata,
						  struct aead_request *req)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	u32 curr_mlli_size = 0;

	if (areq_ctx->assoc_buff_type == SSI_DMA_BUF_MLLI) {
		areq_ctx->assoc.sram_addr = drvdata->mlli_sram_addr;
		curr_mlli_size = areq_ctx->assoc.mlli_nents *
						LLI_ENTRY_BYTE_SIZE;
	}

	if (areq_ctx->data_buff_type == SSI_DMA_BUF_MLLI) {
		/*Inplace case dst nents equal to src nents*/
		if (req->src == req->dst) {
			areq_ctx->dst.mlli_nents = areq_ctx->src.mlli_nents;
			areq_ctx->src.sram_addr = drvdata->mlli_sram_addr +
								curr_mlli_size;
			areq_ctx->dst.sram_addr = areq_ctx->src.sram_addr;
			if (!areq_ctx->is_single_pass)
				areq_ctx->assoc.mlli_nents +=
					areq_ctx->src.mlli_nents;
		} else {
			if (areq_ctx->gen_ctx.op_type ==
					DRV_CRYPTO_DIRECTION_DECRYPT) {
				areq_ctx->src.sram_addr =
						drvdata->mlli_sram_addr +
								curr_mlli_size;
				areq_ctx->dst.sram_addr =
						areq_ctx->src.sram_addr +
						areq_ctx->src.mlli_nents *
						LLI_ENTRY_BYTE_SIZE;
				if (!areq_ctx->is_single_pass)
					areq_ctx->assoc.mlli_nents +=
						areq_ctx->src.mlli_nents;
			} else {
				areq_ctx->dst.sram_addr =
						drvdata->mlli_sram_addr +
								curr_mlli_size;
				areq_ctx->src.sram_addr =
						areq_ctx->dst.sram_addr +
						areq_ctx->dst.mlli_nents *
						LLI_ENTRY_BYTE_SIZE;
				if (!areq_ctx->is_single_pass)
					areq_ctx->assoc.mlli_nents +=
						areq_ctx->dst.mlli_nents;
			}
		}
	}
}

int ssi_buffer_mgr_map_aead_request(
	struct ssi_drvdata *drvdata, struct aead_request *req)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	struct mlli_params *mlli_params = &areq_ctx->mlli_params;
	struct device *dev = &drvdata->plat_dev->dev;
	struct buffer_array sg_data;
	unsigned int authsize = areq_ctx->req_authsize;
	struct buff_mgr_handle *buff_mgr = drvdata->buff_mgr_handle;
	int rc = 0;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	bool is_gcm4543 = areq_ctx->is_gcm4543;

	u32 mapped_nents = 0;
	u32 dummy = 0; /*used for the assoc data fragments */
	u32 size_to_map = 0;

	mlli_params->curr_pool = NULL;
	sg_data.num_of_buffers = 0;

	if (drvdata->coherent &&
	    (areq_ctx->gen_ctx.op_type == DRV_CRYPTO_DIRECTION_DECRYPT) &&
	    likely(req->src == req->dst)) {
		u32 size_to_skip = req->assoclen;

		if (is_gcm4543)
			size_to_skip += crypto_aead_ivsize(tfm);

		/* copy mac to a temporary location to deal with possible
		 * data memory overriding that caused by cache coherence problem.
		 */
		ssi_buffer_mgr_copy_scatterlist_portion(
			areq_ctx->backup_mac, req->src,
			size_to_skip + req->cryptlen - areq_ctx->req_authsize,
			size_to_skip + req->cryptlen, SSI_SG_TO_BUF);
	}

	/* cacluate the size for cipher remove ICV in decrypt*/
	areq_ctx->cryptlen = (areq_ctx->gen_ctx.op_type ==
				 DRV_CRYPTO_DIRECTION_ENCRYPT) ?
				req->cryptlen :
				(req->cryptlen - authsize);

	areq_ctx->mac_buf_dma_addr = dma_map_single(dev,
		areq_ctx->mac_buf, MAX_MAC_SIZE, DMA_BIDIRECTIONAL);
	if (unlikely(dma_mapping_error(dev, areq_ctx->mac_buf_dma_addr))) {
		SSI_LOG_ERR("Mapping mac_buf %u B at va=%pK for DMA failed\n",
			    MAX_MAC_SIZE, areq_ctx->mac_buf);
		rc = -ENOMEM;
		goto aead_map_failure;
	}

	if (areq_ctx->ccm_hdr_size != ccm_header_size_null) {
		areq_ctx->ccm_iv0_dma_addr = dma_map_single(dev,
			(areq_ctx->ccm_config + CCM_CTR_COUNT_0_OFFSET),
			AES_BLOCK_SIZE, DMA_TO_DEVICE);

		if (unlikely(dma_mapping_error(dev, areq_ctx->ccm_iv0_dma_addr))) {
			SSI_LOG_ERR("Mapping mac_buf %u B at va=%pK "
			"for DMA failed\n", AES_BLOCK_SIZE,
			(areq_ctx->ccm_config + CCM_CTR_COUNT_0_OFFSET));
			areq_ctx->ccm_iv0_dma_addr = 0;
			rc = -ENOMEM;
			goto aead_map_failure;
		}
		if (ssi_aead_handle_config_buf(dev, areq_ctx,
					       areq_ctx->ccm_config, &sg_data,
					       req->assoclen) != 0) {
			rc = -ENOMEM;
			goto aead_map_failure;
		}
	}

#if SSI_CC_HAS_AES_GCM
	if (areq_ctx->cipher_mode == DRV_CIPHER_GCTR) {
		areq_ctx->hkey_dma_addr = dma_map_single(dev,
			areq_ctx->hkey, AES_BLOCK_SIZE, DMA_BIDIRECTIONAL);
		if (unlikely(dma_mapping_error(dev, areq_ctx->hkey_dma_addr))) {
			SSI_LOG_ERR("Mapping hkey %u B at va=%pK for DMA failed\n",
				    AES_BLOCK_SIZE, areq_ctx->hkey);
			rc = -ENOMEM;
			goto aead_map_failure;
		}

		areq_ctx->gcm_block_len_dma_addr = dma_map_single(dev,
			&areq_ctx->gcm_len_block, AES_BLOCK_SIZE, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(dev, areq_ctx->gcm_block_len_dma_addr))) {
			SSI_LOG_ERR("Mapping gcm_len_block %u B at va=%pK for DMA failed\n",
				    AES_BLOCK_SIZE, &areq_ctx->gcm_len_block);
			rc = -ENOMEM;
			goto aead_map_failure;
		}

		areq_ctx->gcm_iv_inc1_dma_addr = dma_map_single(dev,
			areq_ctx->gcm_iv_inc1,
			AES_BLOCK_SIZE, DMA_TO_DEVICE);

		if (unlikely(dma_mapping_error(dev, areq_ctx->gcm_iv_inc1_dma_addr))) {
			SSI_LOG_ERR("Mapping gcm_iv_inc1 %u B at va=%pK "
			"for DMA failed\n", AES_BLOCK_SIZE,
			(areq_ctx->gcm_iv_inc1));
			areq_ctx->gcm_iv_inc1_dma_addr = 0;
			rc = -ENOMEM;
			goto aead_map_failure;
		}

		areq_ctx->gcm_iv_inc2_dma_addr = dma_map_single(dev,
			areq_ctx->gcm_iv_inc2,
			AES_BLOCK_SIZE, DMA_TO_DEVICE);

		if (unlikely(dma_mapping_error(dev, areq_ctx->gcm_iv_inc2_dma_addr))) {
			SSI_LOG_ERR("Mapping gcm_iv_inc2 %u B at va=%pK "
			"for DMA failed\n", AES_BLOCK_SIZE,
			(areq_ctx->gcm_iv_inc2));
			areq_ctx->gcm_iv_inc2_dma_addr = 0;
			rc = -ENOMEM;
			goto aead_map_failure;
		}
	}
#endif /*SSI_CC_HAS_AES_GCM*/

	size_to_map = req->cryptlen + req->assoclen;
	if (areq_ctx->gen_ctx.op_type == DRV_CRYPTO_DIRECTION_ENCRYPT)
		size_to_map += authsize;

	if (is_gcm4543)
		size_to_map += crypto_aead_ivsize(tfm);
	rc = ssi_buffer_mgr_map_scatterlist(dev, req->src,
					    size_to_map, DMA_BIDIRECTIONAL, &areq_ctx->src.nents,
					    LLI_MAX_NUM_OF_ASSOC_DATA_ENTRIES + LLI_MAX_NUM_OF_DATA_ENTRIES, &dummy, &mapped_nents);
	if (unlikely(rc != 0)) {
		rc = -ENOMEM;
		goto aead_map_failure;
	}

	if (likely(areq_ctx->is_single_pass)) {
		/*
		 * Create MLLI table for:
		 *   (1) Assoc. data
		 *   (2) Src/Dst SGLs
		 *   Note: IV is contg. buffer (not an SGL)
		 */
		rc = ssi_buffer_mgr_aead_chain_assoc(drvdata, req, &sg_data, true, false);
		if (unlikely(rc != 0))
			goto aead_map_failure;
		rc = ssi_buffer_mgr_aead_chain_iv(drvdata, req, &sg_data, true, false);
		if (unlikely(rc != 0))
			goto aead_map_failure;
		rc = ssi_buffer_mgr_aead_chain_data(drvdata, req, &sg_data, true, false);
		if (unlikely(rc != 0))
			goto aead_map_failure;
	} else { /* DOUBLE-PASS flow */
		/*
		 * Prepare MLLI table(s) in this order:
		 *
		 * If ENCRYPT/DECRYPT (inplace):
		 *   (1) MLLI table for assoc
		 *   (2) IV entry (chained right after end of assoc)
		 *   (3) MLLI for src/dst (inplace operation)
		 *
		 * If ENCRYPT (non-inplace)
		 *   (1) MLLI table for assoc
		 *   (2) IV entry (chained right after end of assoc)
		 *   (3) MLLI for dst
		 *   (4) MLLI for src
		 *
		 * If DECRYPT (non-inplace)
		 *   (1) MLLI table for assoc
		 *   (2) IV entry (chained right after end of assoc)
		 *   (3) MLLI for src
		 *   (4) MLLI for dst
		 */
		rc = ssi_buffer_mgr_aead_chain_assoc(drvdata, req, &sg_data, false, true);
		if (unlikely(rc != 0))
			goto aead_map_failure;
		rc = ssi_buffer_mgr_aead_chain_iv(drvdata, req, &sg_data, false, true);
		if (unlikely(rc != 0))
			goto aead_map_failure;
		rc = ssi_buffer_mgr_aead_chain_data(drvdata, req, &sg_data, true, true);
		if (unlikely(rc != 0))
			goto aead_map_failure;
	}

	/* Mlli support -start building the MLLI according to the above results */
	if (unlikely(
		(areq_ctx->assoc_buff_type == SSI_DMA_BUF_MLLI) ||
		(areq_ctx->data_buff_type == SSI_DMA_BUF_MLLI))) {
		mlli_params->curr_pool = buff_mgr->mlli_buffs_pool;
		rc = ssi_buffer_mgr_generate_mlli(dev, &sg_data, mlli_params);
		if (unlikely(rc != 0))
			goto aead_map_failure;

		ssi_buffer_mgr_update_aead_mlli_nents(drvdata, req);
		SSI_LOG_DEBUG("assoc params mn %d\n", areq_ctx->assoc.mlli_nents);
		SSI_LOG_DEBUG("src params mn %d\n", areq_ctx->src.mlli_nents);
		SSI_LOG_DEBUG("dst params mn %d\n", areq_ctx->dst.mlli_nents);
	}
	return 0;

aead_map_failure:
	ssi_buffer_mgr_unmap_aead_request(dev, req);
	return rc;
}

int ssi_buffer_mgr_map_hash_request_final(
	struct ssi_drvdata *drvdata, void *ctx, struct scatterlist *src, unsigned int nbytes, bool do_update)
{
	struct ahash_req_ctx *areq_ctx = (struct ahash_req_ctx *)ctx;
	struct device *dev = &drvdata->plat_dev->dev;
	u8 *curr_buff = areq_ctx->buff_index ? areq_ctx->buff1 :
			areq_ctx->buff0;
	u32 *curr_buff_cnt = areq_ctx->buff_index ? &areq_ctx->buff1_cnt :
			&areq_ctx->buff0_cnt;
	struct mlli_params *mlli_params = &areq_ctx->mlli_params;
	struct buffer_array sg_data;
	struct buff_mgr_handle *buff_mgr = drvdata->buff_mgr_handle;
	u32 dummy = 0;
	u32 mapped_nents = 0;

	SSI_LOG_DEBUG(" final params : curr_buff=%pK "
		     "curr_buff_cnt=0x%X nbytes = 0x%X "
		     "src=%pK curr_index=%u\n",
		     curr_buff, *curr_buff_cnt, nbytes,
		     src, areq_ctx->buff_index);
	/* Init the type of the dma buffer */
	areq_ctx->data_dma_buf_type = SSI_DMA_BUF_NULL;
	mlli_params->curr_pool = NULL;
	sg_data.num_of_buffers = 0;
	areq_ctx->in_nents = 0;

	if (unlikely(nbytes == 0 && *curr_buff_cnt == 0)) {
		/* nothing to do */
		return 0;
	}

	/*TODO: copy data in case that buffer is enough for operation */
	/* map the previous buffer */
	if (*curr_buff_cnt != 0) {
		if (ssi_ahash_handle_curr_buf(dev, areq_ctx, curr_buff,
					      *curr_buff_cnt, &sg_data) != 0) {
			return -ENOMEM;
		}
	}

	if (src && (nbytes > 0) && do_update) {
		if (unlikely(ssi_buffer_mgr_map_scatterlist(dev, src, nbytes,
							    DMA_TO_DEVICE,
							    &areq_ctx->in_nents,
							    LLI_MAX_NUM_OF_DATA_ENTRIES,
							    &dummy,
							    &mapped_nents))){
			goto unmap_curr_buff;
		}
		if (src && (mapped_nents == 1)
		     && (areq_ctx->data_dma_buf_type == SSI_DMA_BUF_NULL)) {
			memcpy(areq_ctx->buff_sg, src,
			       sizeof(struct scatterlist));
			areq_ctx->buff_sg->length = nbytes;
			areq_ctx->curr_sg = areq_ctx->buff_sg;
			areq_ctx->data_dma_buf_type = SSI_DMA_BUF_DLLI;
		} else {
			areq_ctx->data_dma_buf_type = SSI_DMA_BUF_MLLI;
		}
	}

	/*build mlli */
	if (unlikely(areq_ctx->data_dma_buf_type == SSI_DMA_BUF_MLLI)) {
		mlli_params->curr_pool = buff_mgr->mlli_buffs_pool;
		/* add the src data to the sg_data */
		ssi_buffer_mgr_add_scatterlist_entry(&sg_data,
						     areq_ctx->in_nents,
						     src, nbytes, 0, true,
						     &areq_ctx->mlli_nents);
		if (unlikely(ssi_buffer_mgr_generate_mlli(dev, &sg_data,
							  mlli_params) != 0)) {
			goto fail_unmap_din;
		}
	}
	/* change the buffer index for the unmap function */
	areq_ctx->buff_index = (areq_ctx->buff_index ^ 1);
	SSI_LOG_DEBUG("areq_ctx->data_dma_buf_type = %s\n",
		      GET_DMA_BUFFER_TYPE(areq_ctx->data_dma_buf_type));
	return 0;

fail_unmap_din:
	dma_unmap_sg(dev, src, areq_ctx->in_nents, DMA_TO_DEVICE);

unmap_curr_buff:
	if (*curr_buff_cnt != 0)
		dma_unmap_sg(dev, areq_ctx->buff_sg, 1, DMA_TO_DEVICE);

	return -ENOMEM;
}

int ssi_buffer_mgr_map_hash_request_update(
	struct ssi_drvdata *drvdata, void *ctx, struct scatterlist *src, unsigned int nbytes, unsigned int block_size)
{
	struct ahash_req_ctx *areq_ctx = (struct ahash_req_ctx *)ctx;
	struct device *dev = &drvdata->plat_dev->dev;
	u8 *curr_buff = areq_ctx->buff_index ? areq_ctx->buff1 :
			areq_ctx->buff0;
	u32 *curr_buff_cnt = areq_ctx->buff_index ? &areq_ctx->buff1_cnt :
			&areq_ctx->buff0_cnt;
	u8 *next_buff = areq_ctx->buff_index ? areq_ctx->buff0 :
			areq_ctx->buff1;
	u32 *next_buff_cnt = areq_ctx->buff_index ? &areq_ctx->buff0_cnt :
			&areq_ctx->buff1_cnt;
	struct mlli_params *mlli_params = &areq_ctx->mlli_params;
	unsigned int update_data_len;
	u32 total_in_len = nbytes + *curr_buff_cnt;
	struct buffer_array sg_data;
	struct buff_mgr_handle *buff_mgr = drvdata->buff_mgr_handle;
	unsigned int swap_index = 0;
	u32 dummy = 0;
	u32 mapped_nents = 0;

	SSI_LOG_DEBUG(" update params : curr_buff=%pK "
		     "curr_buff_cnt=0x%X nbytes=0x%X "
		     "src=%pK curr_index=%u\n",
		     curr_buff, *curr_buff_cnt, nbytes,
		     src, areq_ctx->buff_index);
	/* Init the type of the dma buffer */
	areq_ctx->data_dma_buf_type = SSI_DMA_BUF_NULL;
	mlli_params->curr_pool = NULL;
	areq_ctx->curr_sg = NULL;
	sg_data.num_of_buffers = 0;
	areq_ctx->in_nents = 0;

	if (unlikely(total_in_len < block_size)) {
		SSI_LOG_DEBUG(" less than one block: curr_buff=%pK "
			     "*curr_buff_cnt=0x%X copy_to=%pK\n",
			curr_buff, *curr_buff_cnt,
			&curr_buff[*curr_buff_cnt]);
		areq_ctx->in_nents =
			ssi_buffer_mgr_get_sgl_nents(src,
						     nbytes,
						     &dummy, NULL);
		sg_copy_to_buffer(src, areq_ctx->in_nents,
				  &curr_buff[*curr_buff_cnt], nbytes);
		*curr_buff_cnt += nbytes;
		return 1;
	}

	/* Calculate the residue size*/
	*next_buff_cnt = total_in_len & (block_size - 1);
	/* update data len */
	update_data_len = total_in_len - *next_buff_cnt;

	SSI_LOG_DEBUG(" temp length : *next_buff_cnt=0x%X "
		     "update_data_len=0x%X\n",
		*next_buff_cnt, update_data_len);

	/* Copy the new residue to next buffer */
	if (*next_buff_cnt != 0) {
		SSI_LOG_DEBUG(" handle residue: next buff %pK skip data %u"
			     " residue %u\n", next_buff,
			     (update_data_len - *curr_buff_cnt),
			     *next_buff_cnt);
		ssi_buffer_mgr_copy_scatterlist_portion(next_buff, src,
							(update_data_len - *curr_buff_cnt),
							nbytes, SSI_SG_TO_BUF);
		/* change the buffer index for next operation */
		swap_index = 1;
	}

	if (*curr_buff_cnt != 0) {
		if (ssi_ahash_handle_curr_buf(dev, areq_ctx, curr_buff,
					      *curr_buff_cnt, &sg_data) != 0) {
			return -ENOMEM;
		}
		/* change the buffer index for next operation */
		swap_index = 1;
	}

	if (update_data_len > *curr_buff_cnt) {
		if (unlikely(ssi_buffer_mgr_map_scatterlist(dev, src,
							    (update_data_len - *curr_buff_cnt),
							    DMA_TO_DEVICE,
							    &areq_ctx->in_nents,
							    LLI_MAX_NUM_OF_DATA_ENTRIES,
							    &dummy,
							    &mapped_nents))){
			goto unmap_curr_buff;
		}
		if ((mapped_nents == 1)
		     && (areq_ctx->data_dma_buf_type == SSI_DMA_BUF_NULL)) {
			/* only one entry in the SG and no previous data */
			memcpy(areq_ctx->buff_sg, src,
			       sizeof(struct scatterlist));
			areq_ctx->buff_sg->length = update_data_len;
			areq_ctx->data_dma_buf_type = SSI_DMA_BUF_DLLI;
			areq_ctx->curr_sg = areq_ctx->buff_sg;
		} else {
			areq_ctx->data_dma_buf_type = SSI_DMA_BUF_MLLI;
		}
	}

	if (unlikely(areq_ctx->data_dma_buf_type == SSI_DMA_BUF_MLLI)) {
		mlli_params->curr_pool = buff_mgr->mlli_buffs_pool;
		/* add the src data to the sg_data */
		ssi_buffer_mgr_add_scatterlist_entry(&sg_data,
						     areq_ctx->in_nents,
						     src,
						     (update_data_len - *curr_buff_cnt),
						     0,
						     true,
						     &areq_ctx->mlli_nents);
		if (unlikely(ssi_buffer_mgr_generate_mlli(dev, &sg_data,
							  mlli_params) != 0)) {
			goto fail_unmap_din;
		}
	}
	areq_ctx->buff_index = (areq_ctx->buff_index ^ swap_index);

	return 0;

fail_unmap_din:
	dma_unmap_sg(dev, src, areq_ctx->in_nents, DMA_TO_DEVICE);

unmap_curr_buff:
	if (*curr_buff_cnt != 0)
		dma_unmap_sg(dev, areq_ctx->buff_sg, 1, DMA_TO_DEVICE);

	return -ENOMEM;
}

void ssi_buffer_mgr_unmap_hash_request(
	struct device *dev, void *ctx, struct scatterlist *src, bool do_revert)
{
	struct ahash_req_ctx *areq_ctx = (struct ahash_req_ctx *)ctx;
	u32 *prev_len = areq_ctx->buff_index ?  &areq_ctx->buff0_cnt :
						&areq_ctx->buff1_cnt;

	/*In case a pool was set, a table was
	 *allocated and should be released
	 */
	if (areq_ctx->mlli_params.curr_pool) {
		SSI_LOG_DEBUG("free MLLI buffer: dma=0x%llX virt=%pK\n",
			      (unsigned long long)areq_ctx->mlli_params.mlli_dma_addr,
			      areq_ctx->mlli_params.mlli_virt_addr);
		dma_pool_free(areq_ctx->mlli_params.curr_pool,
			      areq_ctx->mlli_params.mlli_virt_addr,
			      areq_ctx->mlli_params.mlli_dma_addr);
	}

	if ((src) && likely(areq_ctx->in_nents != 0)) {
		SSI_LOG_DEBUG("Unmapped sg src: virt=%pK dma=0x%llX len=0x%X\n",
			      sg_virt(src),
			      (unsigned long long)sg_dma_address(src),
			      sg_dma_len(src));
		dma_unmap_sg(dev, src,
			     areq_ctx->in_nents, DMA_TO_DEVICE);
	}

	if (*prev_len != 0) {
		SSI_LOG_DEBUG("Unmapped buffer: areq_ctx->buff_sg=%pK"
			     " dma=0x%llX len 0x%X\n",
				sg_virt(areq_ctx->buff_sg),
				(unsigned long long)sg_dma_address(areq_ctx->buff_sg),
				sg_dma_len(areq_ctx->buff_sg));
		dma_unmap_sg(dev, areq_ctx->buff_sg, 1, DMA_TO_DEVICE);
		if (!do_revert) {
			/* clean the previous data length for update operation */
			*prev_len = 0;
		} else {
			areq_ctx->buff_index ^= 1;
		}
	}
}

int ssi_buffer_mgr_init(struct ssi_drvdata *drvdata)
{
	struct buff_mgr_handle *buff_mgr_handle;
	struct device *dev = &drvdata->plat_dev->dev;

	buff_mgr_handle = kmalloc(sizeof(*buff_mgr_handle), GFP_KERNEL);
	if (!buff_mgr_handle)
		return -ENOMEM;

	drvdata->buff_mgr_handle = buff_mgr_handle;

	buff_mgr_handle->mlli_buffs_pool = dma_pool_create(
				"dx_single_mlli_tables", dev,
				MAX_NUM_OF_TOTAL_MLLI_ENTRIES *
				LLI_ENTRY_BYTE_SIZE,
				MLLI_TABLE_MIN_ALIGNMENT, 0);

	if (unlikely(!buff_mgr_handle->mlli_buffs_pool))
		goto error;

	return 0;

error:
	ssi_buffer_mgr_fini(drvdata);
	return -ENOMEM;
}

int ssi_buffer_mgr_fini(struct ssi_drvdata *drvdata)
{
	struct buff_mgr_handle *buff_mgr_handle = drvdata->buff_mgr_handle;

	if (buff_mgr_handle) {
		dma_pool_destroy(buff_mgr_handle->mlli_buffs_pool);
		kfree(drvdata->buff_mgr_handle);
		drvdata->buff_mgr_handle = NULL;
	}
	return 0;
}
