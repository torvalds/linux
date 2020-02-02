// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2012-2019 ARM Limited (or its affiliates). */

#include <crypto/internal/aead.h>
#include <crypto/authenc.h>
#include <crypto/scatterwalk.h>
#include <linux/dmapool.h>
#include <linux/dma-mapping.h>

#include "cc_buffer_mgr.h"
#include "cc_lli_defs.h"
#include "cc_cipher.h"
#include "cc_hash.h"
#include "cc_aead.h"

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
 * cc_copy_mac() - Copy MAC to temporary location
 *
 * @dev: device object
 * @req: aead request object
 * @dir: [IN] copy from/to sgl
 */
static void cc_copy_mac(struct device *dev, struct aead_request *req,
			enum cc_sg_cpy_direct dir)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	u32 skip = areq_ctx->assoclen + req->cryptlen;

	if (areq_ctx->is_gcm4543)
		skip += crypto_aead_ivsize(tfm);

	cc_copy_sg_portion(dev, areq_ctx->backup_mac, req->src,
			   (skip - areq_ctx->req_authsize), skip, dir);
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
				     unsigned int nbytes, u32 *lbytes)
{
	unsigned int nents = 0;

	*lbytes = 0;

	while (nbytes && sg_list) {
		nents++;
		/* get the number of bytes in the last entry */
		*lbytes = nbytes;
		nbytes -= (sg_list->length > nbytes) ?
				nbytes : sg_list->length;
		sg_list = sg_next(sg_list);
	}

	dev_dbg(dev, "nents %d last bytes %d\n", nents, *lbytes);
	return nents;
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
	u32 nents;

	nents = sg_nents_for_len(sg, end);
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
	if (new_nents > MAX_NUM_OF_TOTAL_MLLI_ENTRIES) {
		dev_err(dev, "Too many mlli entries. current %d max %d\n",
			new_nents, MAX_NUM_OF_TOTAL_MLLI_ENTRIES);
		return -ENOMEM;
	}

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

static void cc_add_buffer_entry(struct device *dev,
				struct buffer_array *sgl_data,
				dma_addr_t buffer_dma, unsigned int buffer_len,
				bool is_last_entry, u32 *mlli_nents)
{
	unsigned int index = sgl_data->num_of_buffers;

	dev_dbg(dev, "index=%u single_buff=%pad buffer_len=0x%08X is_last=%d\n",
		index, &buffer_dma, buffer_len, is_last_entry);
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

static int cc_map_sg(struct device *dev, struct scatterlist *sg,
		     unsigned int nbytes, int direction, u32 *nents,
		     u32 max_sg_nents, u32 *lbytes, u32 *mapped_nents)
{
	int ret = 0;

	*nents = cc_get_sgl_nents(dev, sg, nbytes, lbytes);
	if (*nents > max_sg_nents) {
		*nents = 0;
		dev_err(dev, "Too many fragments. current %d max %d\n",
			*nents, max_sg_nents);
		return -ENOMEM;
	}

	ret = dma_map_sg(dev, sg, *nents, direction);
	if (dma_mapping_error(dev, ret)) {
		*nents = 0;
		dev_err(dev, "dma_map_sg() sg buffer failed %d\n", ret);
		return -ENOMEM;
	}

	*mapped_nents = ret;

	return 0;
}

static int
cc_set_aead_conf_buf(struct device *dev, struct aead_req_ctx *areq_ctx,
		     u8 *config_data, struct buffer_array *sg_data,
		     unsigned int assoclen)
{
	dev_dbg(dev, " handle additional data config set to DLLI\n");
	/* create sg for the current buffer */
	sg_init_one(&areq_ctx->ccm_adata_sg, config_data,
		    AES_BLOCK_SIZE + areq_ctx->ccm_hdr_size);
	if (dma_map_sg(dev, &areq_ctx->ccm_adata_sg, 1, DMA_TO_DEVICE) != 1) {
		dev_err(dev, "dma_map_sg() config buffer failed\n");
		return -ENOMEM;
	}
	dev_dbg(dev, "Mapped curr_buff: dma_address=%pad page=%p addr=%pK offset=%u length=%u\n",
		&sg_dma_address(&areq_ctx->ccm_adata_sg),
		sg_page(&areq_ctx->ccm_adata_sg),
		sg_virt(&areq_ctx->ccm_adata_sg),
		areq_ctx->ccm_adata_sg.offset, areq_ctx->ccm_adata_sg.length);
	/* prepare for case of MLLI */
	if (assoclen > 0) {
		cc_add_sg_entry(dev, sg_data, 1, &areq_ctx->ccm_adata_sg,
				(AES_BLOCK_SIZE + areq_ctx->ccm_hdr_size),
				0, false, NULL);
	}
	return 0;
}

static int cc_set_hash_buf(struct device *dev, struct ahash_req_ctx *areq_ctx,
			   u8 *curr_buff, u32 curr_buff_cnt,
			   struct buffer_array *sg_data)
{
	dev_dbg(dev, " handle curr buff %x set to   DLLI\n", curr_buff_cnt);
	/* create sg for the current buffer */
	sg_init_one(areq_ctx->buff_sg, curr_buff, curr_buff_cnt);
	if (dma_map_sg(dev, areq_ctx->buff_sg, 1, DMA_TO_DEVICE) != 1) {
		dev_err(dev, "dma_map_sg() src buffer failed\n");
		return -ENOMEM;
	}
	dev_dbg(dev, "Mapped curr_buff: dma_address=%pad page=%p addr=%pK offset=%u length=%u\n",
		&sg_dma_address(areq_ctx->buff_sg), sg_page(areq_ctx->buff_sg),
		sg_virt(areq_ctx->buff_sg), areq_ctx->buff_sg->offset,
		areq_ctx->buff_sg->length);
	areq_ctx->data_dma_buf_type = CC_DMA_BUF_DLLI;
	areq_ctx->curr_sg = areq_ctx->buff_sg;
	areq_ctx->in_nents = 0;
	/* prepare for case of MLLI */
	cc_add_sg_entry(dev, sg_data, 1, areq_ctx->buff_sg, curr_buff_cnt, 0,
			false, NULL);
	return 0;
}

void cc_unmap_cipher_request(struct device *dev, void *ctx,
				unsigned int ivsize, struct scatterlist *src,
				struct scatterlist *dst)
{
	struct cipher_req_ctx *req_ctx = (struct cipher_req_ctx *)ctx;

	if (req_ctx->gen_ctx.iv_dma_addr) {
		dev_dbg(dev, "Unmapped iv: iv_dma_addr=%pad iv_size=%u\n",
			&req_ctx->gen_ctx.iv_dma_addr, ivsize);
		dma_unmap_single(dev, req_ctx->gen_ctx.iv_dma_addr,
				 ivsize, DMA_BIDIRECTIONAL);
	}
	/* Release pool */
	if (req_ctx->dma_buf_type == CC_DMA_BUF_MLLI &&
	    req_ctx->mlli_params.mlli_virt_addr) {
		dma_pool_free(req_ctx->mlli_params.curr_pool,
			      req_ctx->mlli_params.mlli_virt_addr,
			      req_ctx->mlli_params.mlli_dma_addr);
	}

	dma_unmap_sg(dev, src, req_ctx->in_nents, DMA_BIDIRECTIONAL);
	dev_dbg(dev, "Unmapped req->src=%pK\n", sg_virt(src));

	if (src != dst) {
		dma_unmap_sg(dev, dst, req_ctx->out_nents, DMA_BIDIRECTIONAL);
		dev_dbg(dev, "Unmapped req->dst=%pK\n", sg_virt(dst));
	}
}

int cc_map_cipher_request(struct cc_drvdata *drvdata, void *ctx,
			  unsigned int ivsize, unsigned int nbytes,
			  void *info, struct scatterlist *src,
			  struct scatterlist *dst, gfp_t flags)
{
	struct cipher_req_ctx *req_ctx = (struct cipher_req_ctx *)ctx;
	struct mlli_params *mlli_params = &req_ctx->mlli_params;
	struct buff_mgr_handle *buff_mgr = drvdata->buff_mgr_handle;
	struct device *dev = drvdata_to_dev(drvdata);
	struct buffer_array sg_data;
	u32 dummy = 0;
	int rc = 0;
	u32 mapped_nents = 0;

	req_ctx->dma_buf_type = CC_DMA_BUF_DLLI;
	mlli_params->curr_pool = NULL;
	sg_data.num_of_buffers = 0;

	/* Map IV buffer */
	if (ivsize) {
		dump_byte_array("iv", (u8 *)info, ivsize);
		req_ctx->gen_ctx.iv_dma_addr =
			dma_map_single(dev, (void *)info,
				       ivsize, DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev, req_ctx->gen_ctx.iv_dma_addr)) {
			dev_err(dev, "Mapping iv %u B at va=%pK for DMA failed\n",
				ivsize, info);
			return -ENOMEM;
		}
		dev_dbg(dev, "Mapped iv %u B at va=%pK to dma=%pad\n",
			ivsize, info, &req_ctx->gen_ctx.iv_dma_addr);
	} else {
		req_ctx->gen_ctx.iv_dma_addr = 0;
	}

	/* Map the src SGL */
	rc = cc_map_sg(dev, src, nbytes, DMA_BIDIRECTIONAL, &req_ctx->in_nents,
		       LLI_MAX_NUM_OF_DATA_ENTRIES, &dummy, &mapped_nents);
	if (rc)
		goto cipher_exit;
	if (mapped_nents > 1)
		req_ctx->dma_buf_type = CC_DMA_BUF_MLLI;

	if (src == dst) {
		/* Handle inplace operation */
		if (req_ctx->dma_buf_type == CC_DMA_BUF_MLLI) {
			req_ctx->out_nents = 0;
			cc_add_sg_entry(dev, &sg_data, req_ctx->in_nents, src,
					nbytes, 0, true,
					&req_ctx->in_mlli_nents);
		}
	} else {
		/* Map the dst sg */
		rc = cc_map_sg(dev, dst, nbytes, DMA_BIDIRECTIONAL,
			       &req_ctx->out_nents, LLI_MAX_NUM_OF_DATA_ENTRIES,
			       &dummy, &mapped_nents);
		if (rc)
			goto cipher_exit;
		if (mapped_nents > 1)
			req_ctx->dma_buf_type = CC_DMA_BUF_MLLI;

		if (req_ctx->dma_buf_type == CC_DMA_BUF_MLLI) {
			cc_add_sg_entry(dev, &sg_data, req_ctx->in_nents, src,
					nbytes, 0, true,
					&req_ctx->in_mlli_nents);
			cc_add_sg_entry(dev, &sg_data, req_ctx->out_nents, dst,
					nbytes, 0, true,
					&req_ctx->out_mlli_nents);
		}
	}

	if (req_ctx->dma_buf_type == CC_DMA_BUF_MLLI) {
		mlli_params->curr_pool = buff_mgr->mlli_buffs_pool;
		rc = cc_generate_mlli(dev, &sg_data, mlli_params, flags);
		if (rc)
			goto cipher_exit;
	}

	dev_dbg(dev, "areq_ctx->dma_buf_type = %s\n",
		cc_dma_buf_type(req_ctx->dma_buf_type));

	return 0;

cipher_exit:
	cc_unmap_cipher_request(dev, req_ctx, ivsize, src, dst);
	return rc;
}

void cc_unmap_aead_request(struct device *dev, struct aead_request *req)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	unsigned int hw_iv_size = areq_ctx->hw_iv_size;
	struct cc_drvdata *drvdata = dev_get_drvdata(dev);

	if (areq_ctx->mac_buf_dma_addr) {
		dma_unmap_single(dev, areq_ctx->mac_buf_dma_addr,
				 MAX_MAC_SIZE, DMA_BIDIRECTIONAL);
	}

	if (areq_ctx->cipher_mode == DRV_CIPHER_GCTR) {
		if (areq_ctx->hkey_dma_addr) {
			dma_unmap_single(dev, areq_ctx->hkey_dma_addr,
					 AES_BLOCK_SIZE, DMA_BIDIRECTIONAL);
		}

		if (areq_ctx->gcm_block_len_dma_addr) {
			dma_unmap_single(dev, areq_ctx->gcm_block_len_dma_addr,
					 AES_BLOCK_SIZE, DMA_TO_DEVICE);
		}

		if (areq_ctx->gcm_iv_inc1_dma_addr) {
			dma_unmap_single(dev, areq_ctx->gcm_iv_inc1_dma_addr,
					 AES_BLOCK_SIZE, DMA_TO_DEVICE);
		}

		if (areq_ctx->gcm_iv_inc2_dma_addr) {
			dma_unmap_single(dev, areq_ctx->gcm_iv_inc2_dma_addr,
					 AES_BLOCK_SIZE, DMA_TO_DEVICE);
		}
	}

	if (areq_ctx->ccm_hdr_size != ccm_header_size_null) {
		if (areq_ctx->ccm_iv0_dma_addr) {
			dma_unmap_single(dev, areq_ctx->ccm_iv0_dma_addr,
					 AES_BLOCK_SIZE, DMA_TO_DEVICE);
		}

		dma_unmap_sg(dev, &areq_ctx->ccm_adata_sg, 1, DMA_TO_DEVICE);
	}
	if (areq_ctx->gen_ctx.iv_dma_addr) {
		dma_unmap_single(dev, areq_ctx->gen_ctx.iv_dma_addr,
				 hw_iv_size, DMA_BIDIRECTIONAL);
		kzfree(areq_ctx->gen_ctx.iv);
	}

	/* Release pool */
	if ((areq_ctx->assoc_buff_type == CC_DMA_BUF_MLLI ||
	     areq_ctx->data_buff_type == CC_DMA_BUF_MLLI) &&
	    (areq_ctx->mlli_params.mlli_virt_addr)) {
		dev_dbg(dev, "free MLLI buffer: dma=%pad virt=%pK\n",
			&areq_ctx->mlli_params.mlli_dma_addr,
			areq_ctx->mlli_params.mlli_virt_addr);
		dma_pool_free(areq_ctx->mlli_params.curr_pool,
			      areq_ctx->mlli_params.mlli_virt_addr,
			      areq_ctx->mlli_params.mlli_dma_addr);
	}

	dev_dbg(dev, "Unmapping src sgl: req->src=%pK areq_ctx->src.nents=%u areq_ctx->assoc.nents=%u assoclen:%u cryptlen=%u\n",
		sg_virt(req->src), areq_ctx->src.nents, areq_ctx->assoc.nents,
		areq_ctx->assoclen, req->cryptlen);

	dma_unmap_sg(dev, req->src, areq_ctx->src.mapped_nents,
		     DMA_BIDIRECTIONAL);
	if (req->src != req->dst) {
		dev_dbg(dev, "Unmapping dst sgl: req->dst=%pK\n",
			sg_virt(req->dst));
		dma_unmap_sg(dev, req->dst, areq_ctx->dst.mapped_nents,
			     DMA_BIDIRECTIONAL);
	}
	if (drvdata->coherent &&
	    areq_ctx->gen_ctx.op_type == DRV_CRYPTO_DIRECTION_DECRYPT &&
	    req->src == req->dst) {
		/* copy back mac from temporary location to deal with possible
		 * data memory overriding that caused by cache coherence
		 * problem.
		 */
		cc_copy_mac(dev, req, CC_SG_FROM_BUF);
	}
}

static bool cc_is_icv_frag(unsigned int sgl_nents, unsigned int authsize,
			   u32 last_entry_data_size)
{
	return ((sgl_nents > 1) && (last_entry_data_size < authsize));
}

static int cc_aead_chain_iv(struct cc_drvdata *drvdata,
			    struct aead_request *req,
			    struct buffer_array *sg_data,
			    bool is_last, bool do_chain)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	unsigned int hw_iv_size = areq_ctx->hw_iv_size;
	struct device *dev = drvdata_to_dev(drvdata);
	gfp_t flags = cc_gfp_flags(&req->base);
	int rc = 0;

	if (!req->iv) {
		areq_ctx->gen_ctx.iv_dma_addr = 0;
		areq_ctx->gen_ctx.iv = NULL;
		goto chain_iv_exit;
	}

	areq_ctx->gen_ctx.iv = kmemdup(req->iv, hw_iv_size, flags);
	if (!areq_ctx->gen_ctx.iv)
		return -ENOMEM;

	areq_ctx->gen_ctx.iv_dma_addr =
		dma_map_single(dev, areq_ctx->gen_ctx.iv, hw_iv_size,
			       DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, areq_ctx->gen_ctx.iv_dma_addr)) {
		dev_err(dev, "Mapping iv %u B at va=%pK for DMA failed\n",
			hw_iv_size, req->iv);
		kzfree(areq_ctx->gen_ctx.iv);
		areq_ctx->gen_ctx.iv = NULL;
		rc = -ENOMEM;
		goto chain_iv_exit;
	}

	dev_dbg(dev, "Mapped iv %u B at va=%pK to dma=%pad\n",
		hw_iv_size, req->iv, &areq_ctx->gen_ctx.iv_dma_addr);
	// TODO: what about CTR?? ask Ron
	if (do_chain && areq_ctx->plaintext_authenticate_only) {
		struct crypto_aead *tfm = crypto_aead_reqtfm(req);
		unsigned int iv_size_to_authenc = crypto_aead_ivsize(tfm);
		unsigned int iv_ofs = GCM_BLOCK_RFC4_IV_OFFSET;
		/* Chain to given list */
		cc_add_buffer_entry(dev, sg_data,
				    (areq_ctx->gen_ctx.iv_dma_addr + iv_ofs),
				    iv_size_to_authenc, is_last,
				    &areq_ctx->assoc.mlli_nents);
		areq_ctx->assoc_buff_type = CC_DMA_BUF_MLLI;
	}

chain_iv_exit:
	return rc;
}

static int cc_aead_chain_assoc(struct cc_drvdata *drvdata,
			       struct aead_request *req,
			       struct buffer_array *sg_data,
			       bool is_last, bool do_chain)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	int rc = 0;
	int mapped_nents = 0;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	unsigned int size_of_assoc = areq_ctx->assoclen;
	struct device *dev = drvdata_to_dev(drvdata);

	if (areq_ctx->is_gcm4543)
		size_of_assoc += crypto_aead_ivsize(tfm);

	if (!sg_data) {
		rc = -EINVAL;
		goto chain_assoc_exit;
	}

	if (areq_ctx->assoclen == 0) {
		areq_ctx->assoc_buff_type = CC_DMA_BUF_NULL;
		areq_ctx->assoc.nents = 0;
		areq_ctx->assoc.mlli_nents = 0;
		dev_dbg(dev, "Chain assoc of length 0: buff_type=%s nents=%u\n",
			cc_dma_buf_type(areq_ctx->assoc_buff_type),
			areq_ctx->assoc.nents);
		goto chain_assoc_exit;
	}

	mapped_nents = sg_nents_for_len(req->src, size_of_assoc);
	if (mapped_nents < 0)
		return mapped_nents;

	if (mapped_nents > LLI_MAX_NUM_OF_ASSOC_DATA_ENTRIES) {
		dev_err(dev, "Too many fragments. current %d max %d\n",
			mapped_nents, LLI_MAX_NUM_OF_ASSOC_DATA_ENTRIES);
		return -ENOMEM;
	}
	areq_ctx->assoc.nents = mapped_nents;

	/* in CCM case we have additional entry for
	 * ccm header configurations
	 */
	if (areq_ctx->ccm_hdr_size != ccm_header_size_null) {
		if ((mapped_nents + 1) > LLI_MAX_NUM_OF_ASSOC_DATA_ENTRIES) {
			dev_err(dev, "CCM case.Too many fragments. Current %d max %d\n",
				(areq_ctx->assoc.nents + 1),
				LLI_MAX_NUM_OF_ASSOC_DATA_ENTRIES);
			rc = -ENOMEM;
			goto chain_assoc_exit;
		}
	}

	if (mapped_nents == 1 && areq_ctx->ccm_hdr_size == ccm_header_size_null)
		areq_ctx->assoc_buff_type = CC_DMA_BUF_DLLI;
	else
		areq_ctx->assoc_buff_type = CC_DMA_BUF_MLLI;

	if (do_chain || areq_ctx->assoc_buff_type == CC_DMA_BUF_MLLI) {
		dev_dbg(dev, "Chain assoc: buff_type=%s nents=%u\n",
			cc_dma_buf_type(areq_ctx->assoc_buff_type),
			areq_ctx->assoc.nents);
		cc_add_sg_entry(dev, sg_data, areq_ctx->assoc.nents, req->src,
				areq_ctx->assoclen, 0, is_last,
				&areq_ctx->assoc.mlli_nents);
		areq_ctx->assoc_buff_type = CC_DMA_BUF_MLLI;
	}

chain_assoc_exit:
	return rc;
}

static void cc_prepare_aead_data_dlli(struct aead_request *req,
				      u32 *src_last_bytes, u32 *dst_last_bytes)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	enum drv_crypto_direction direct = areq_ctx->gen_ctx.op_type;
	unsigned int authsize = areq_ctx->req_authsize;
	struct scatterlist *sg;
	ssize_t offset;

	areq_ctx->is_icv_fragmented = false;

	if ((req->src == req->dst) || direct == DRV_CRYPTO_DIRECTION_DECRYPT) {
		sg = areq_ctx->src_sgl;
		offset = *src_last_bytes - authsize;
	} else {
		sg = areq_ctx->dst_sgl;
		offset = *dst_last_bytes - authsize;
	}

	areq_ctx->icv_dma_addr = sg_dma_address(sg) + offset;
	areq_ctx->icv_virt_addr = sg_virt(sg) + offset;
}

static void cc_prepare_aead_data_mlli(struct cc_drvdata *drvdata,
				      struct aead_request *req,
				      struct buffer_array *sg_data,
				      u32 *src_last_bytes, u32 *dst_last_bytes,
				      bool is_last_table)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	enum drv_crypto_direction direct = areq_ctx->gen_ctx.op_type;
	unsigned int authsize = areq_ctx->req_authsize;
	struct device *dev = drvdata_to_dev(drvdata);
	struct scatterlist *sg;

	if (req->src == req->dst) {
		/*INPLACE*/
		cc_add_sg_entry(dev, sg_data, areq_ctx->src.nents,
				areq_ctx->src_sgl, areq_ctx->cryptlen,
				areq_ctx->src_offset, is_last_table,
				&areq_ctx->src.mlli_nents);

		areq_ctx->is_icv_fragmented =
			cc_is_icv_frag(areq_ctx->src.nents, authsize,
				       *src_last_bytes);

		if (areq_ctx->is_icv_fragmented) {
			/* Backup happens only when ICV is fragmented, ICV
			 * verification is made by CPU compare in order to
			 * simplify MAC verification upon request completion
			 */
			if (direct == DRV_CRYPTO_DIRECTION_DECRYPT) {
				/* In coherent platforms (e.g. ACP)
				 * already copying ICV for any
				 * INPLACE-DECRYPT operation, hence
				 * we must neglect this code.
				 */
				if (!drvdata->coherent)
					cc_copy_mac(dev, req, CC_SG_TO_BUF);

				areq_ctx->icv_virt_addr = areq_ctx->backup_mac;
			} else {
				areq_ctx->icv_virt_addr = areq_ctx->mac_buf;
				areq_ctx->icv_dma_addr =
					areq_ctx->mac_buf_dma_addr;
			}
		} else { /* Contig. ICV */
			sg = &areq_ctx->src_sgl[areq_ctx->src.nents - 1];
			/*Should hanlde if the sg is not contig.*/
			areq_ctx->icv_dma_addr = sg_dma_address(sg) +
				(*src_last_bytes - authsize);
			areq_ctx->icv_virt_addr = sg_virt(sg) +
				(*src_last_bytes - authsize);
		}

	} else if (direct == DRV_CRYPTO_DIRECTION_DECRYPT) {
		/*NON-INPLACE and DECRYPT*/
		cc_add_sg_entry(dev, sg_data, areq_ctx->src.nents,
				areq_ctx->src_sgl, areq_ctx->cryptlen,
				areq_ctx->src_offset, is_last_table,
				&areq_ctx->src.mlli_nents);
		cc_add_sg_entry(dev, sg_data, areq_ctx->dst.nents,
				areq_ctx->dst_sgl, areq_ctx->cryptlen,
				areq_ctx->dst_offset, is_last_table,
				&areq_ctx->dst.mlli_nents);

		areq_ctx->is_icv_fragmented =
			cc_is_icv_frag(areq_ctx->src.nents, authsize,
				       *src_last_bytes);
		/* Backup happens only when ICV is fragmented, ICV

		 * verification is made by CPU compare in order to simplify
		 * MAC verification upon request completion
		 */
		if (areq_ctx->is_icv_fragmented) {
			cc_copy_mac(dev, req, CC_SG_TO_BUF);
			areq_ctx->icv_virt_addr = areq_ctx->backup_mac;

		} else { /* Contig. ICV */
			sg = &areq_ctx->src_sgl[areq_ctx->src.nents - 1];
			/*Should hanlde if the sg is not contig.*/
			areq_ctx->icv_dma_addr = sg_dma_address(sg) +
				(*src_last_bytes - authsize);
			areq_ctx->icv_virt_addr = sg_virt(sg) +
				(*src_last_bytes - authsize);
		}

	} else {
		/*NON-INPLACE and ENCRYPT*/
		cc_add_sg_entry(dev, sg_data, areq_ctx->dst.nents,
				areq_ctx->dst_sgl, areq_ctx->cryptlen,
				areq_ctx->dst_offset, is_last_table,
				&areq_ctx->dst.mlli_nents);
		cc_add_sg_entry(dev, sg_data, areq_ctx->src.nents,
				areq_ctx->src_sgl, areq_ctx->cryptlen,
				areq_ctx->src_offset, is_last_table,
				&areq_ctx->src.mlli_nents);

		areq_ctx->is_icv_fragmented =
			cc_is_icv_frag(areq_ctx->dst.nents, authsize,
				       *dst_last_bytes);

		if (!areq_ctx->is_icv_fragmented) {
			sg = &areq_ctx->dst_sgl[areq_ctx->dst.nents - 1];
			/* Contig. ICV */
			areq_ctx->icv_dma_addr = sg_dma_address(sg) +
				(*dst_last_bytes - authsize);
			areq_ctx->icv_virt_addr = sg_virt(sg) +
				(*dst_last_bytes - authsize);
		} else {
			areq_ctx->icv_dma_addr = areq_ctx->mac_buf_dma_addr;
			areq_ctx->icv_virt_addr = areq_ctx->mac_buf;
		}
	}
}

static int cc_aead_chain_data(struct cc_drvdata *drvdata,
			      struct aead_request *req,
			      struct buffer_array *sg_data,
			      bool is_last_table, bool do_chain)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	struct device *dev = drvdata_to_dev(drvdata);
	enum drv_crypto_direction direct = areq_ctx->gen_ctx.op_type;
	unsigned int authsize = areq_ctx->req_authsize;
	unsigned int src_last_bytes = 0, dst_last_bytes = 0;
	int rc = 0;
	u32 src_mapped_nents = 0, dst_mapped_nents = 0;
	u32 offset = 0;
	/* non-inplace mode */
	unsigned int size_for_map = areq_ctx->assoclen + req->cryptlen;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	u32 sg_index = 0;
	bool is_gcm4543 = areq_ctx->is_gcm4543;
	u32 size_to_skip = areq_ctx->assoclen;
	struct scatterlist *sgl;

	if (is_gcm4543)
		size_to_skip += crypto_aead_ivsize(tfm);

	offset = size_to_skip;

	if (!sg_data)
		return -EINVAL;

	areq_ctx->src_sgl = req->src;
	areq_ctx->dst_sgl = req->dst;

	if (is_gcm4543)
		size_for_map += crypto_aead_ivsize(tfm);

	size_for_map += (direct == DRV_CRYPTO_DIRECTION_ENCRYPT) ?
			authsize : 0;
	src_mapped_nents = cc_get_sgl_nents(dev, req->src, size_for_map,
					    &src_last_bytes);
	sg_index = areq_ctx->src_sgl->length;
	//check where the data starts
	while (src_mapped_nents && (sg_index <= size_to_skip)) {
		src_mapped_nents--;
		offset -= areq_ctx->src_sgl->length;
		sgl = sg_next(areq_ctx->src_sgl);
		if (!sgl)
			break;
		areq_ctx->src_sgl = sgl;
		sg_index += areq_ctx->src_sgl->length;
	}
	if (src_mapped_nents > LLI_MAX_NUM_OF_DATA_ENTRIES) {
		dev_err(dev, "Too many fragments. current %d max %d\n",
			src_mapped_nents, LLI_MAX_NUM_OF_DATA_ENTRIES);
		return -ENOMEM;
	}

	areq_ctx->src.nents = src_mapped_nents;

	areq_ctx->src_offset = offset;

	if (req->src != req->dst) {
		size_for_map = areq_ctx->assoclen + req->cryptlen;

		if (direct == DRV_CRYPTO_DIRECTION_ENCRYPT)
			size_for_map += authsize;
		else
			size_for_map -= authsize;

		if (is_gcm4543)
			size_for_map += crypto_aead_ivsize(tfm);

		rc = cc_map_sg(dev, req->dst, size_for_map, DMA_BIDIRECTIONAL,
			       &areq_ctx->dst.mapped_nents,
			       LLI_MAX_NUM_OF_DATA_ENTRIES, &dst_last_bytes,
			       &dst_mapped_nents);
		if (rc)
			goto chain_data_exit;
	}

	dst_mapped_nents = cc_get_sgl_nents(dev, req->dst, size_for_map,
					    &dst_last_bytes);
	sg_index = areq_ctx->dst_sgl->length;
	offset = size_to_skip;

	//check where the data starts
	while (dst_mapped_nents && sg_index <= size_to_skip) {
		dst_mapped_nents--;
		offset -= areq_ctx->dst_sgl->length;
		sgl = sg_next(areq_ctx->dst_sgl);
		if (!sgl)
			break;
		areq_ctx->dst_sgl = sgl;
		sg_index += areq_ctx->dst_sgl->length;
	}
	if (dst_mapped_nents > LLI_MAX_NUM_OF_DATA_ENTRIES) {
		dev_err(dev, "Too many fragments. current %d max %d\n",
			dst_mapped_nents, LLI_MAX_NUM_OF_DATA_ENTRIES);
		return -ENOMEM;
	}
	areq_ctx->dst.nents = dst_mapped_nents;
	areq_ctx->dst_offset = offset;
	if (src_mapped_nents > 1 ||
	    dst_mapped_nents  > 1 ||
	    do_chain) {
		areq_ctx->data_buff_type = CC_DMA_BUF_MLLI;
		cc_prepare_aead_data_mlli(drvdata, req, sg_data,
					  &src_last_bytes, &dst_last_bytes,
					  is_last_table);
	} else {
		areq_ctx->data_buff_type = CC_DMA_BUF_DLLI;
		cc_prepare_aead_data_dlli(req, &src_last_bytes,
					  &dst_last_bytes);
	}

chain_data_exit:
	return rc;
}

static void cc_update_aead_mlli_nents(struct cc_drvdata *drvdata,
				      struct aead_request *req)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	u32 curr_mlli_size = 0;

	if (areq_ctx->assoc_buff_type == CC_DMA_BUF_MLLI) {
		areq_ctx->assoc.sram_addr = drvdata->mlli_sram_addr;
		curr_mlli_size = areq_ctx->assoc.mlli_nents *
						LLI_ENTRY_BYTE_SIZE;
	}

	if (areq_ctx->data_buff_type == CC_DMA_BUF_MLLI) {
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

int cc_map_aead_request(struct cc_drvdata *drvdata, struct aead_request *req)
{
	struct aead_req_ctx *areq_ctx = aead_request_ctx(req);
	struct mlli_params *mlli_params = &areq_ctx->mlli_params;
	struct device *dev = drvdata_to_dev(drvdata);
	struct buffer_array sg_data;
	unsigned int authsize = areq_ctx->req_authsize;
	struct buff_mgr_handle *buff_mgr = drvdata->buff_mgr_handle;
	int rc = 0;
	struct crypto_aead *tfm = crypto_aead_reqtfm(req);
	bool is_gcm4543 = areq_ctx->is_gcm4543;
	dma_addr_t dma_addr;
	u32 mapped_nents = 0;
	u32 dummy = 0; /*used for the assoc data fragments */
	u32 size_to_map = 0;
	gfp_t flags = cc_gfp_flags(&req->base);

	mlli_params->curr_pool = NULL;
	sg_data.num_of_buffers = 0;

	/* copy mac to a temporary location to deal with possible
	 * data memory overriding that caused by cache coherence problem.
	 */
	if (drvdata->coherent &&
	    areq_ctx->gen_ctx.op_type == DRV_CRYPTO_DIRECTION_DECRYPT &&
	    req->src == req->dst)
		cc_copy_mac(dev, req, CC_SG_TO_BUF);

	/* cacluate the size for cipher remove ICV in decrypt*/
	areq_ctx->cryptlen = (areq_ctx->gen_ctx.op_type ==
				 DRV_CRYPTO_DIRECTION_ENCRYPT) ?
				req->cryptlen :
				(req->cryptlen - authsize);

	dma_addr = dma_map_single(dev, areq_ctx->mac_buf, MAX_MAC_SIZE,
				  DMA_BIDIRECTIONAL);
	if (dma_mapping_error(dev, dma_addr)) {
		dev_err(dev, "Mapping mac_buf %u B at va=%pK for DMA failed\n",
			MAX_MAC_SIZE, areq_ctx->mac_buf);
		rc = -ENOMEM;
		goto aead_map_failure;
	}
	areq_ctx->mac_buf_dma_addr = dma_addr;

	if (areq_ctx->ccm_hdr_size != ccm_header_size_null) {
		void *addr = areq_ctx->ccm_config + CCM_CTR_COUNT_0_OFFSET;

		dma_addr = dma_map_single(dev, addr, AES_BLOCK_SIZE,
					  DMA_TO_DEVICE);

		if (dma_mapping_error(dev, dma_addr)) {
			dev_err(dev, "Mapping mac_buf %u B at va=%pK for DMA failed\n",
				AES_BLOCK_SIZE, addr);
			areq_ctx->ccm_iv0_dma_addr = 0;
			rc = -ENOMEM;
			goto aead_map_failure;
		}
		areq_ctx->ccm_iv0_dma_addr = dma_addr;

		rc = cc_set_aead_conf_buf(dev, areq_ctx, areq_ctx->ccm_config,
					  &sg_data, areq_ctx->assoclen);
		if (rc)
			goto aead_map_failure;
	}

	if (areq_ctx->cipher_mode == DRV_CIPHER_GCTR) {
		dma_addr = dma_map_single(dev, areq_ctx->hkey, AES_BLOCK_SIZE,
					  DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev, dma_addr)) {
			dev_err(dev, "Mapping hkey %u B at va=%pK for DMA failed\n",
				AES_BLOCK_SIZE, areq_ctx->hkey);
			rc = -ENOMEM;
			goto aead_map_failure;
		}
		areq_ctx->hkey_dma_addr = dma_addr;

		dma_addr = dma_map_single(dev, &areq_ctx->gcm_len_block,
					  AES_BLOCK_SIZE, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, dma_addr)) {
			dev_err(dev, "Mapping gcm_len_block %u B at va=%pK for DMA failed\n",
				AES_BLOCK_SIZE, &areq_ctx->gcm_len_block);
			rc = -ENOMEM;
			goto aead_map_failure;
		}
		areq_ctx->gcm_block_len_dma_addr = dma_addr;

		dma_addr = dma_map_single(dev, areq_ctx->gcm_iv_inc1,
					  AES_BLOCK_SIZE, DMA_TO_DEVICE);

		if (dma_mapping_error(dev, dma_addr)) {
			dev_err(dev, "Mapping gcm_iv_inc1 %u B at va=%pK for DMA failed\n",
				AES_BLOCK_SIZE, (areq_ctx->gcm_iv_inc1));
			areq_ctx->gcm_iv_inc1_dma_addr = 0;
			rc = -ENOMEM;
			goto aead_map_failure;
		}
		areq_ctx->gcm_iv_inc1_dma_addr = dma_addr;

		dma_addr = dma_map_single(dev, areq_ctx->gcm_iv_inc2,
					  AES_BLOCK_SIZE, DMA_TO_DEVICE);

		if (dma_mapping_error(dev, dma_addr)) {
			dev_err(dev, "Mapping gcm_iv_inc2 %u B at va=%pK for DMA failed\n",
				AES_BLOCK_SIZE, (areq_ctx->gcm_iv_inc2));
			areq_ctx->gcm_iv_inc2_dma_addr = 0;
			rc = -ENOMEM;
			goto aead_map_failure;
		}
		areq_ctx->gcm_iv_inc2_dma_addr = dma_addr;
	}

	size_to_map = req->cryptlen + areq_ctx->assoclen;
	/* If we do in-place encryption, we also need the auth tag */
	if ((areq_ctx->gen_ctx.op_type == DRV_CRYPTO_DIRECTION_ENCRYPT) &&
	   (req->src == req->dst)) {
		size_to_map += authsize;
	}
	if (is_gcm4543)
		size_to_map += crypto_aead_ivsize(tfm);
	rc = cc_map_sg(dev, req->src, size_to_map, DMA_BIDIRECTIONAL,
		       &areq_ctx->src.mapped_nents,
		       (LLI_MAX_NUM_OF_ASSOC_DATA_ENTRIES +
			LLI_MAX_NUM_OF_DATA_ENTRIES),
		       &dummy, &mapped_nents);
	if (rc)
		goto aead_map_failure;

	if (areq_ctx->is_single_pass) {
		/*
		 * Create MLLI table for:
		 *   (1) Assoc. data
		 *   (2) Src/Dst SGLs
		 *   Note: IV is contg. buffer (not an SGL)
		 */
		rc = cc_aead_chain_assoc(drvdata, req, &sg_data, true, false);
		if (rc)
			goto aead_map_failure;
		rc = cc_aead_chain_iv(drvdata, req, &sg_data, true, false);
		if (rc)
			goto aead_map_failure;
		rc = cc_aead_chain_data(drvdata, req, &sg_data, true, false);
		if (rc)
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
		rc = cc_aead_chain_assoc(drvdata, req, &sg_data, false, true);
		if (rc)
			goto aead_map_failure;
		rc = cc_aead_chain_iv(drvdata, req, &sg_data, false, true);
		if (rc)
			goto aead_map_failure;
		rc = cc_aead_chain_data(drvdata, req, &sg_data, true, true);
		if (rc)
			goto aead_map_failure;
	}

	/* Mlli support -start building the MLLI according to the above
	 * results
	 */
	if (areq_ctx->assoc_buff_type == CC_DMA_BUF_MLLI ||
	    areq_ctx->data_buff_type == CC_DMA_BUF_MLLI) {
		mlli_params->curr_pool = buff_mgr->mlli_buffs_pool;
		rc = cc_generate_mlli(dev, &sg_data, mlli_params, flags);
		if (rc)
			goto aead_map_failure;

		cc_update_aead_mlli_nents(drvdata, req);
		dev_dbg(dev, "assoc params mn %d\n",
			areq_ctx->assoc.mlli_nents);
		dev_dbg(dev, "src params mn %d\n", areq_ctx->src.mlli_nents);
		dev_dbg(dev, "dst params mn %d\n", areq_ctx->dst.mlli_nents);
	}
	return 0;

aead_map_failure:
	cc_unmap_aead_request(dev, req);
	return rc;
}

int cc_map_hash_request_final(struct cc_drvdata *drvdata, void *ctx,
			      struct scatterlist *src, unsigned int nbytes,
			      bool do_update, gfp_t flags)
{
	struct ahash_req_ctx *areq_ctx = (struct ahash_req_ctx *)ctx;
	struct device *dev = drvdata_to_dev(drvdata);
	u8 *curr_buff = cc_hash_buf(areq_ctx);
	u32 *curr_buff_cnt = cc_hash_buf_cnt(areq_ctx);
	struct mlli_params *mlli_params = &areq_ctx->mlli_params;
	struct buffer_array sg_data;
	struct buff_mgr_handle *buff_mgr = drvdata->buff_mgr_handle;
	int rc = 0;
	u32 dummy = 0;
	u32 mapped_nents = 0;

	dev_dbg(dev, "final params : curr_buff=%pK curr_buff_cnt=0x%X nbytes = 0x%X src=%pK curr_index=%u\n",
		curr_buff, *curr_buff_cnt, nbytes, src, areq_ctx->buff_index);
	/* Init the type of the dma buffer */
	areq_ctx->data_dma_buf_type = CC_DMA_BUF_NULL;
	mlli_params->curr_pool = NULL;
	sg_data.num_of_buffers = 0;
	areq_ctx->in_nents = 0;

	if (nbytes == 0 && *curr_buff_cnt == 0) {
		/* nothing to do */
		return 0;
	}

	/*TODO: copy data in case that buffer is enough for operation */
	/* map the previous buffer */
	if (*curr_buff_cnt) {
		rc = cc_set_hash_buf(dev, areq_ctx, curr_buff, *curr_buff_cnt,
				     &sg_data);
		if (rc)
			return rc;
	}

	if (src && nbytes > 0 && do_update) {
		rc = cc_map_sg(dev, src, nbytes, DMA_TO_DEVICE,
			       &areq_ctx->in_nents, LLI_MAX_NUM_OF_DATA_ENTRIES,
			       &dummy, &mapped_nents);
		if (rc)
			goto unmap_curr_buff;
		if (src && mapped_nents == 1 &&
		    areq_ctx->data_dma_buf_type == CC_DMA_BUF_NULL) {
			memcpy(areq_ctx->buff_sg, src,
			       sizeof(struct scatterlist));
			areq_ctx->buff_sg->length = nbytes;
			areq_ctx->curr_sg = areq_ctx->buff_sg;
			areq_ctx->data_dma_buf_type = CC_DMA_BUF_DLLI;
		} else {
			areq_ctx->data_dma_buf_type = CC_DMA_BUF_MLLI;
		}
	}

	/*build mlli */
	if (areq_ctx->data_dma_buf_type == CC_DMA_BUF_MLLI) {
		mlli_params->curr_pool = buff_mgr->mlli_buffs_pool;
		/* add the src data to the sg_data */
		cc_add_sg_entry(dev, &sg_data, areq_ctx->in_nents, src, nbytes,
				0, true, &areq_ctx->mlli_nents);
		rc = cc_generate_mlli(dev, &sg_data, mlli_params, flags);
		if (rc)
			goto fail_unmap_din;
	}
	/* change the buffer index for the unmap function */
	areq_ctx->buff_index = (areq_ctx->buff_index ^ 1);
	dev_dbg(dev, "areq_ctx->data_dma_buf_type = %s\n",
		cc_dma_buf_type(areq_ctx->data_dma_buf_type));
	return 0;

fail_unmap_din:
	dma_unmap_sg(dev, src, areq_ctx->in_nents, DMA_TO_DEVICE);

unmap_curr_buff:
	if (*curr_buff_cnt)
		dma_unmap_sg(dev, areq_ctx->buff_sg, 1, DMA_TO_DEVICE);

	return rc;
}

int cc_map_hash_request_update(struct cc_drvdata *drvdata, void *ctx,
			       struct scatterlist *src, unsigned int nbytes,
			       unsigned int block_size, gfp_t flags)
{
	struct ahash_req_ctx *areq_ctx = (struct ahash_req_ctx *)ctx;
	struct device *dev = drvdata_to_dev(drvdata);
	u8 *curr_buff = cc_hash_buf(areq_ctx);
	u32 *curr_buff_cnt = cc_hash_buf_cnt(areq_ctx);
	u8 *next_buff = cc_next_buf(areq_ctx);
	u32 *next_buff_cnt = cc_next_buf_cnt(areq_ctx);
	struct mlli_params *mlli_params = &areq_ctx->mlli_params;
	unsigned int update_data_len;
	u32 total_in_len = nbytes + *curr_buff_cnt;
	struct buffer_array sg_data;
	struct buff_mgr_handle *buff_mgr = drvdata->buff_mgr_handle;
	unsigned int swap_index = 0;
	int rc = 0;
	u32 dummy = 0;
	u32 mapped_nents = 0;

	dev_dbg(dev, " update params : curr_buff=%pK curr_buff_cnt=0x%X nbytes=0x%X src=%pK curr_index=%u\n",
		curr_buff, *curr_buff_cnt, nbytes, src, areq_ctx->buff_index);
	/* Init the type of the dma buffer */
	areq_ctx->data_dma_buf_type = CC_DMA_BUF_NULL;
	mlli_params->curr_pool = NULL;
	areq_ctx->curr_sg = NULL;
	sg_data.num_of_buffers = 0;
	areq_ctx->in_nents = 0;

	if (total_in_len < block_size) {
		dev_dbg(dev, " less than one block: curr_buff=%pK *curr_buff_cnt=0x%X copy_to=%pK\n",
			curr_buff, *curr_buff_cnt, &curr_buff[*curr_buff_cnt]);
		areq_ctx->in_nents = sg_nents_for_len(src, nbytes);
		sg_copy_to_buffer(src, areq_ctx->in_nents,
				  &curr_buff[*curr_buff_cnt], nbytes);
		*curr_buff_cnt += nbytes;
		return 1;
	}

	/* Calculate the residue size*/
	*next_buff_cnt = total_in_len & (block_size - 1);
	/* update data len */
	update_data_len = total_in_len - *next_buff_cnt;

	dev_dbg(dev, " temp length : *next_buff_cnt=0x%X update_data_len=0x%X\n",
		*next_buff_cnt, update_data_len);

	/* Copy the new residue to next buffer */
	if (*next_buff_cnt) {
		dev_dbg(dev, " handle residue: next buff %pK skip data %u residue %u\n",
			next_buff, (update_data_len - *curr_buff_cnt),
			*next_buff_cnt);
		cc_copy_sg_portion(dev, next_buff, src,
				   (update_data_len - *curr_buff_cnt),
				   nbytes, CC_SG_TO_BUF);
		/* change the buffer index for next operation */
		swap_index = 1;
	}

	if (*curr_buff_cnt) {
		rc = cc_set_hash_buf(dev, areq_ctx, curr_buff, *curr_buff_cnt,
				     &sg_data);
		if (rc)
			return rc;
		/* change the buffer index for next operation */
		swap_index = 1;
	}

	if (update_data_len > *curr_buff_cnt) {
		rc = cc_map_sg(dev, src, (update_data_len - *curr_buff_cnt),
			       DMA_TO_DEVICE, &areq_ctx->in_nents,
			       LLI_MAX_NUM_OF_DATA_ENTRIES, &dummy,
			       &mapped_nents);
		if (rc)
			goto unmap_curr_buff;
		if (mapped_nents == 1 &&
		    areq_ctx->data_dma_buf_type == CC_DMA_BUF_NULL) {
			/* only one entry in the SG and no previous data */
			memcpy(areq_ctx->buff_sg, src,
			       sizeof(struct scatterlist));
			areq_ctx->buff_sg->length = update_data_len;
			areq_ctx->data_dma_buf_type = CC_DMA_BUF_DLLI;
			areq_ctx->curr_sg = areq_ctx->buff_sg;
		} else {
			areq_ctx->data_dma_buf_type = CC_DMA_BUF_MLLI;
		}
	}

	if (areq_ctx->data_dma_buf_type == CC_DMA_BUF_MLLI) {
		mlli_params->curr_pool = buff_mgr->mlli_buffs_pool;
		/* add the src data to the sg_data */
		cc_add_sg_entry(dev, &sg_data, areq_ctx->in_nents, src,
				(update_data_len - *curr_buff_cnt), 0, true,
				&areq_ctx->mlli_nents);
		rc = cc_generate_mlli(dev, &sg_data, mlli_params, flags);
		if (rc)
			goto fail_unmap_din;
	}
	areq_ctx->buff_index = (areq_ctx->buff_index ^ swap_index);

	return 0;

fail_unmap_din:
	dma_unmap_sg(dev, src, areq_ctx->in_nents, DMA_TO_DEVICE);

unmap_curr_buff:
	if (*curr_buff_cnt)
		dma_unmap_sg(dev, areq_ctx->buff_sg, 1, DMA_TO_DEVICE);

	return rc;
}

void cc_unmap_hash_request(struct device *dev, void *ctx,
			   struct scatterlist *src, bool do_revert)
{
	struct ahash_req_ctx *areq_ctx = (struct ahash_req_ctx *)ctx;
	u32 *prev_len = cc_next_buf_cnt(areq_ctx);

	/*In case a pool was set, a table was
	 *allocated and should be released
	 */
	if (areq_ctx->mlli_params.curr_pool) {
		dev_dbg(dev, "free MLLI buffer: dma=%pad virt=%pK\n",
			&areq_ctx->mlli_params.mlli_dma_addr,
			areq_ctx->mlli_params.mlli_virt_addr);
		dma_pool_free(areq_ctx->mlli_params.curr_pool,
			      areq_ctx->mlli_params.mlli_virt_addr,
			      areq_ctx->mlli_params.mlli_dma_addr);
	}

	if (src && areq_ctx->in_nents) {
		dev_dbg(dev, "Unmapped sg src: virt=%pK dma=%pad len=0x%X\n",
			sg_virt(src), &sg_dma_address(src), sg_dma_len(src));
		dma_unmap_sg(dev, src,
			     areq_ctx->in_nents, DMA_TO_DEVICE);
	}

	if (*prev_len) {
		dev_dbg(dev, "Unmapped buffer: areq_ctx->buff_sg=%pK dma=%pad len 0x%X\n",
			sg_virt(areq_ctx->buff_sg),
			&sg_dma_address(areq_ctx->buff_sg),
			sg_dma_len(areq_ctx->buff_sg));
		dma_unmap_sg(dev, areq_ctx->buff_sg, 1, DMA_TO_DEVICE);
		if (!do_revert) {
			/* clean the previous data length for update
			 * operation
			 */
			*prev_len = 0;
		} else {
			areq_ctx->buff_index ^= 1;
		}
	}
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
