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
		dma_unmap_single(dev, bl->bufers[i].addr,
				 bl->bufers[i].len, bl_dma_dir);

	dma_unmap_single(dev, blp, sz, DMA_TO_DEVICE);

	if (!buf->sgl_src_valid)
		kfree(bl);

	if (blp != blpout) {
		/* If out of place operation dma unmap only data */
		int bufless = blout->num_bufs - blout->num_mapped_bufs;

		for (i = bufless; i < blout->num_bufs; i++) {
			dma_unmap_single(dev, blout->bufers[i].addr,
					 blout->bufers[i].len,
					 DMA_FROM_DEVICE);
		}
		dma_unmap_single(dev, blpout, sz_out, DMA_TO_DEVICE);

		if (!buf->sgl_dst_valid)
			kfree(blout);
	}
}

int qat_bl_sgl_to_bufl(struct adf_accel_dev *accel_dev,
		       struct scatterlist *sgl,
		       struct scatterlist *sglout,
		       struct qat_request_buffs *buf,
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
	size_t sz_out, sz = struct_size(bufl, bufers, n);
	int node = dev_to_node(&GET_DEV(accel_dev));
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
		bufl = &buf->sgl_src.sgl_hdr;
		memset(bufl, 0, sizeof(struct qat_alg_buf_list));
		buf->sgl_src_valid = true;
	}

	bufl_dma_dir = sgl != sglout ? DMA_TO_DEVICE : DMA_BIDIRECTIONAL;

	for_each_sg(sgl, sg, n, i)
		bufl->bufers[i].addr = DMA_MAPPING_ERROR;

	for_each_sg(sgl, sg, n, i) {
		int y = sg_nctr;

		if (!sg->length)
			continue;

		bufl->bufers[y].addr = dma_map_single(dev, sg_virt(sg),
						      sg->length,
						      bufl_dma_dir);
		bufl->bufers[y].len = sg->length;
		if (unlikely(dma_mapping_error(dev, bufl->bufers[y].addr)))
			goto err_in;
		sg_nctr++;
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
		struct qat_alg_buf *bufers;

		n = sg_nents(sglout);
		sz_out = struct_size(buflout, bufers, n);
		sg_nctr = 0;

		if (n > QAT_MAX_BUFF_DESC) {
			buflout = kzalloc_node(sz_out, flags, node);
			if (unlikely(!buflout))
				goto err_in;
		} else {
			buflout = &buf->sgl_dst.sgl_hdr;
			memset(buflout, 0, sizeof(struct qat_alg_buf_list));
			buf->sgl_dst_valid = true;
		}

		bufers = buflout->bufers;
		for_each_sg(sglout, sg, n, i)
			bufers[i].addr = DMA_MAPPING_ERROR;

		for_each_sg(sglout, sg, n, i) {
			int y = sg_nctr;

			if (!sg->length)
				continue;

			bufers[y].addr = dma_map_single(dev, sg_virt(sg),
							sg->length,
							DMA_FROM_DEVICE);
			if (unlikely(dma_mapping_error(dev, bufers[y].addr)))
				goto err_out;
			bufers[y].len = sg->length;
			sg_nctr++;
		}
		buflout->num_bufs = sg_nctr;
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
	for (i = 0; i < n; i++)
		if (!dma_mapping_error(dev, buflout->bufers[i].addr))
			dma_unmap_single(dev, buflout->bufers[i].addr,
					 buflout->bufers[i].len,
					 DMA_FROM_DEVICE);

	if (!buf->sgl_dst_valid)
		kfree(buflout);

err_in:
	if (!dma_mapping_error(dev, blp))
		dma_unmap_single(dev, blp, sz, DMA_TO_DEVICE);

	n = sg_nents(sgl);
	for (i = 0; i < n; i++)
		if (!dma_mapping_error(dev, bufl->bufers[i].addr))
			dma_unmap_single(dev, bufl->bufers[i].addr,
					 bufl->bufers[i].len,
					 bufl_dma_dir);

	if (!buf->sgl_src_valid)
		kfree(bufl);

	dev_err(dev, "Failed to map buf for dma\n");
	return -ENOMEM;
}
