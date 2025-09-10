// SPDX-License-Identifier: GPL-2.0-only
/* Copyright(c) 2014 - 2022 Intel Corporation */
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/types.h>
#include "adf_accel_devices.h"
#include "qat_bl.h"
#include "qat_crypto.h"

void qat_bl_free_bufl(struct adf_accel_dev *accel_dev,
		      struct qat_request_buffs *buf)
{
	struct device *dev = &GET_DEV(accel_dev);
	struct qat_alg_buf_list *bl = buf->bl;
	struct qat_alg_buf_list *blout = buf->blout;
	dma_addr_t blp = buf->blp;
	dma_addr_t blpout = buf->bloutp;
	size_t sz = buf->sz;
	size_t sz_out = buf->sz_out;
	int bl_dma_dir;
	int i;

	bl_dma_dir = blp != blpout ? DMA_TO_DEVICE : DMA_BIDIRECTIONAL;

	for (i = 0; i < bl->num_bufs; i++)
		dma_unmap_single(dev, bl->buffers[i].addr,
				 bl->buffers[i].len, bl_dma_dir);

	dma_unmap_single(dev, blp, sz, DMA_TO_DEVICE);

	if (!buf->sgl_src_valid)
		kfree(bl);

	if (blp != blpout) {
		for (i = 0; i < blout->num_mapped_bufs; i++) {
			dma_unmap_single(dev, blout->buffers[i].addr,
					 blout->buffers[i].len,
					 DMA_BIDIRECTIONAL);
		}
		dma_unmap_single(dev, blpout, sz_out, DMA_TO_DEVICE);

		if (!buf->sgl_dst_valid)
			kfree(blout);
	}
}

static int __qat_bl_sgl_to_bufl(struct adf_accel_dev *accel_dev,
				struct scatterlist *sgl,
				struct scatterlist *sglout,
				struct qat_request_buffs *buf,
				dma_addr_t extra_dst_buff,
				size_t sz_extra_dst_buff,
				unsigned int sskip,
				unsigned int dskip,
				gfp_t flags)
{
	struct device *dev = &GET_DEV(accel_dev);
	int i, sg_nctr = 0;
	int n = sg_nents(sgl);
	struct qat_alg_buf_list *bufl;
	struct qat_alg_buf_list *buflout = NULL;
	dma_addr_t blp = DMA_MAPPING_ERROR;
	dma_addr_t bloutp = DMA_MAPPING_ERROR;
	struct scatterlist *sg;
	size_t sz_out, sz = struct_size(bufl, buffers, n);
	int node = dev_to_node(&GET_DEV(accel_dev));
	unsigned int left;
	int bufl_dma_dir;

	if (unlikely(!n))
		return -EINVAL;

	buf->sgl_src_valid = false;
	buf->sgl_dst_valid = false;

	if (n > QAT_MAX_BUFF_DESC) {
		bufl = kzalloc_node(sz, flags, node);
		if (unlikely(!bufl))
			return -ENOMEM;
	} else {
		bufl = container_of(&buf->sgl_src.sgl_hdr,
				    struct qat_alg_buf_list, hdr);
		memset(bufl, 0, sizeof(struct qat_alg_buf_list));
		buf->sgl_src_valid = true;
	}

	bufl_dma_dir = sgl != sglout ? DMA_TO_DEVICE : DMA_BIDIRECTIONAL;

	for (i = 0; i < n; i++)
		bufl->buffers[i].addr = DMA_MAPPING_ERROR;

	left = sskip;

	for_each_sg(sgl, sg, n, i) {
		int y = sg_nctr;

		if (!sg->length)
			continue;

		if (left >= sg->length) {
			left -= sg->length;
			continue;
		}
		bufl->buffers[y].addr = dma_map_single(dev, sg_virt(sg) + left,
						       sg->length - left,
						       bufl_dma_dir);
		bufl->buffers[y].len = sg->length;
		if (unlikely(dma_mapping_error(dev, bufl->buffers[y].addr)))
			goto err_in;
		sg_nctr++;
		if (left) {
			bufl->buffers[y].len -= left;
			left = 0;
		}
	}
	bufl->num_bufs = sg_nctr;
	blp = dma_map_single(dev, bufl, sz, DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(dev, blp)))
		goto err_in;
	buf->bl = bufl;
	buf->blp = blp;
	buf->sz = sz;
	/* Handle out of place operation */
	if (sgl != sglout) {
		struct qat_alg_buf *buffers;
		int extra_buff = extra_dst_buff ? 1 : 0;
		int n_sglout = sg_nents(sglout);

		n = n_sglout + extra_buff;
		sz_out = struct_size(buflout, buffers, n);
		left = dskip;

		sg_nctr = 0;

		if (n > QAT_MAX_BUFF_DESC) {
			buflout = kzalloc_node(sz_out, flags, node);
			if (unlikely(!buflout))
				goto err_in;
		} else {
			buflout = container_of(&buf->sgl_dst.sgl_hdr,
					       struct qat_alg_buf_list, hdr);
			memset(buflout, 0, sizeof(struct qat_alg_buf_list));
			buf->sgl_dst_valid = true;
		}

		buffers = buflout->buffers;
		for (i = 0; i < n; i++)
			buffers[i].addr = DMA_MAPPING_ERROR;

		for_each_sg(sglout, sg, n_sglout, i) {
			int y = sg_nctr;

			if (!sg->length)
				continue;

			if (left >= sg->length) {
				left -= sg->length;
				continue;
			}
			buffers[y].addr = dma_map_single(dev, sg_virt(sg) + left,
							 sg->length - left,
							 DMA_BIDIRECTIONAL);
			if (unlikely(dma_mapping_error(dev, buffers[y].addr)))
				goto err_out;
			buffers[y].len = sg->length;
			sg_nctr++;
			if (left) {
				buffers[y].len -= left;
				left = 0;
			}
		}
		if (extra_buff) {
			buffers[sg_nctr].addr = extra_dst_buff;
			buffers[sg_nctr].len = sz_extra_dst_buff;
		}

		buflout->num_bufs = sg_nctr;
		buflout->num_bufs += extra_buff;
		buflout->num_mapped_bufs = sg_nctr;
		bloutp = dma_map_single(dev, buflout, sz_out, DMA_TO_DEVICE);
		if (unlikely(dma_mapping_error(dev, bloutp)))
			goto err_out;
		buf->blout = buflout;
		buf->bloutp = bloutp;
		buf->sz_out = sz_out;
	} else {
		/* Otherwise set the src and dst to the same address */
		buf->bloutp = buf->blp;
		buf->sz_out = 0;
	}
	return 0;

err_out:
	if (!dma_mapping_error(dev, bloutp))
		dma_unmap_single(dev, bloutp, sz_out, DMA_TO_DEVICE);

	n = sg_nents(sglout);
	for (i = 0; i < n; i++) {
		if (buflout->buffers[i].addr == extra_dst_buff)
			break;
		if (!dma_mapping_error(dev, buflout->buffers[i].addr))
			dma_unmap_single(dev, buflout->buffers[i].addr,
					 buflout->buffers[i].len,
					 DMA_BIDIRECTIONAL);
	}

	if (!buf->sgl_dst_valid)
		kfree(buflout);

err_in:
	if (!dma_mapping_error(dev, blp))
		dma_unmap_single(dev, blp, sz, DMA_TO_DEVICE);

	n = sg_nents(sgl);
	for (i = 0; i < n; i++)
		if (!dma_mapping_error(dev, bufl->buffers[i].addr))
			dma_unmap_single(dev, bufl->buffers[i].addr,
					 bufl->buffers[i].len,
					 bufl_dma_dir);

	if (!buf->sgl_src_valid)
		kfree(bufl);

	dev_err(dev, "Failed to map buf for dma\n");
	return -ENOMEM;
}

int qat_bl_sgl_to_bufl(struct adf_accel_dev *accel_dev,
		       struct scatterlist *sgl,
		       struct scatterlist *sglout,
		       struct qat_request_buffs *buf,
		       struct qat_sgl_to_bufl_params *params,
		       gfp_t flags)
{
	dma_addr_t extra_dst_buff = 0;
	size_t sz_extra_dst_buff = 0;
	unsigned int sskip = 0;
	unsigned int dskip = 0;

	if (params) {
		extra_dst_buff = params->extra_dst_buff;
		sz_extra_dst_buff = params->sz_extra_dst_buff;
		sskip = params->sskip;
		dskip = params->dskip;
	}

	return __qat_bl_sgl_to_bufl(accel_dev, sgl, sglout, buf,
				    extra_dst_buff, sz_extra_dst_buff,
				    sskip, dskip, flags);
}
