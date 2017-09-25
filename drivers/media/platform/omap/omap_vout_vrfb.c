/*
 * omap_vout_vrfb.c
 *
 * Copyright (C) 2010 Texas Instruments.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/videodev2.h>
#include <linux/slab.h>

#include <media/videobuf-dma-contig.h>
#include <media/v4l2-device.h>

#include <video/omapvrfb.h>

#include "omap_voutdef.h"
#include "omap_voutlib.h"
#include "omap_vout_vrfb.h"

#define OMAP_DMA_NO_DEVICE	0

/*
 * Function for allocating video buffers
 */
static int omap_vout_allocate_vrfb_buffers(struct omap_vout_device *vout,
		unsigned int *count, int startindex)
{
	int i, j;

	for (i = 0; i < *count; i++) {
		if (!vout->smsshado_virt_addr[i]) {
			vout->smsshado_virt_addr[i] =
				omap_vout_alloc_buffer(vout->smsshado_size,
						&vout->smsshado_phy_addr[i]);
		}
		if (!vout->smsshado_virt_addr[i] && startindex != -1) {
			if (V4L2_MEMORY_MMAP == vout->memory && i >= startindex)
				break;
		}
		if (!vout->smsshado_virt_addr[i]) {
			for (j = 0; j < i; j++) {
				omap_vout_free_buffer(
						vout->smsshado_virt_addr[j],
						vout->smsshado_size);
				vout->smsshado_virt_addr[j] = 0;
				vout->smsshado_phy_addr[j] = 0;
			}
			*count = 0;
			return -ENOMEM;
		}
		memset((void *) vout->smsshado_virt_addr[i], 0,
				vout->smsshado_size);
	}
	return 0;
}

/*
 * Wakes up the application once the DMA transfer to VRFB space is completed.
 */
static void omap_vout_vrfb_dma_tx_callback(void *data)
{
	struct vid_vrfb_dma *t = (struct vid_vrfb_dma *) data;

	t->tx_status = 1;
	wake_up_interruptible(&t->wait);
}

/*
 * Free VRFB buffers
 */
void omap_vout_free_vrfb_buffers(struct omap_vout_device *vout)
{
	int j;

	for (j = 0; j < VRFB_NUM_BUFS; j++) {
		if (vout->smsshado_virt_addr[j]) {
			omap_vout_free_buffer(vout->smsshado_virt_addr[j],
					      vout->smsshado_size);
			vout->smsshado_virt_addr[j] = 0;
			vout->smsshado_phy_addr[j] = 0;
		}
	}
}

int omap_vout_setup_vrfb_bufs(struct platform_device *pdev, int vid_num,
			      bool static_vrfb_allocation)
{
	int ret = 0, i, j;
	struct omap_vout_device *vout;
	struct video_device *vfd;
	dma_cap_mask_t mask;
	int image_width, image_height;
	int vrfb_num_bufs = VRFB_NUM_BUFS;
	struct v4l2_device *v4l2_dev = platform_get_drvdata(pdev);
	struct omap2video_device *vid_dev =
		container_of(v4l2_dev, struct omap2video_device, v4l2_dev);

	vout = vid_dev->vouts[vid_num];
	vfd = vout->vfd;

	for (i = 0; i < VRFB_NUM_BUFS; i++) {
		if (omap_vrfb_request_ctx(&vout->vrfb_context[i])) {
			dev_info(&pdev->dev, ": VRFB allocation failed\n");
			for (j = 0; j < i; j++)
				omap_vrfb_release_ctx(&vout->vrfb_context[j]);
			ret = -ENOMEM;
			goto free_buffers;
		}
	}

	/* Calculate VRFB memory size */
	/* allocate for worst case size */
	image_width = VID_MAX_WIDTH / TILE_SIZE;
	if (VID_MAX_WIDTH % TILE_SIZE)
		image_width++;

	image_width = image_width * TILE_SIZE;
	image_height = VID_MAX_HEIGHT / TILE_SIZE;

	if (VID_MAX_HEIGHT % TILE_SIZE)
		image_height++;

	image_height = image_height * TILE_SIZE;
	vout->smsshado_size = PAGE_ALIGN(image_width * image_height * 2 * 2);

	/*
	 * Request and Initialize DMA, for DMA based VRFB transfer
	 */
	dma_cap_zero(mask);
	dma_cap_set(DMA_INTERLEAVE, mask);
	vout->vrfb_dma_tx.chan = dma_request_chan_by_mask(&mask);
	if (IS_ERR(vout->vrfb_dma_tx.chan)) {
		vout->vrfb_dma_tx.req_status = DMA_CHAN_NOT_ALLOTED;
	} else {
		size_t xt_size = sizeof(struct dma_interleaved_template) +
				 sizeof(struct data_chunk);

		vout->vrfb_dma_tx.xt = kzalloc(xt_size, GFP_KERNEL);
		if (!vout->vrfb_dma_tx.xt) {
			dma_release_channel(vout->vrfb_dma_tx.chan);
			vout->vrfb_dma_tx.req_status = DMA_CHAN_NOT_ALLOTED;
		}
	}

	if (vout->vrfb_dma_tx.req_status == DMA_CHAN_NOT_ALLOTED)
		dev_info(&pdev->dev,
			 ": failed to allocate DMA Channel for video%d\n",
			 vfd->minor);

	init_waitqueue_head(&vout->vrfb_dma_tx.wait);

	/* statically allocated the VRFB buffer is done through
	   commands line aruments */
	if (static_vrfb_allocation) {
		if (omap_vout_allocate_vrfb_buffers(vout, &vrfb_num_bufs, -1)) {
			ret =  -ENOMEM;
			goto release_vrfb_ctx;
		}
		vout->vrfb_static_allocation = true;
	}
	return 0;

release_vrfb_ctx:
	for (j = 0; j < VRFB_NUM_BUFS; j++)
		omap_vrfb_release_ctx(&vout->vrfb_context[j]);
free_buffers:
	omap_vout_free_buffers(vout);

	return ret;
}

/*
 * Release the VRFB context once the module exits
 */
void omap_vout_release_vrfb(struct omap_vout_device *vout)
{
	int i;

	for (i = 0; i < VRFB_NUM_BUFS; i++)
		omap_vrfb_release_ctx(&vout->vrfb_context[i]);

	if (vout->vrfb_dma_tx.req_status == DMA_CHAN_ALLOTED) {
		vout->vrfb_dma_tx.req_status = DMA_CHAN_NOT_ALLOTED;
		kfree(vout->vrfb_dma_tx.xt);
		dmaengine_terminate_sync(vout->vrfb_dma_tx.chan);
		dma_release_channel(vout->vrfb_dma_tx.chan);
	}
}

/*
 * Allocate the buffers for the VRFB space.  Data is copied from V4L2
 * buffers to the VRFB buffers using the DMA engine.
 */
int omap_vout_vrfb_buffer_setup(struct omap_vout_device *vout,
			  unsigned int *count, unsigned int startindex)
{
	int i;
	bool yuv_mode;

	if (!is_rotation_enabled(vout))
		return 0;

	/* If rotation is enabled, allocate memory for VRFB space also */
	*count = *count > VRFB_NUM_BUFS ? VRFB_NUM_BUFS : *count;

	/* Allocate the VRFB buffers only if the buffers are not
	 * allocated during init time.
	 */
	if (!vout->vrfb_static_allocation)
		if (omap_vout_allocate_vrfb_buffers(vout, count, startindex))
			return -ENOMEM;

	if (vout->dss_mode == OMAP_DSS_COLOR_YUV2 ||
			vout->dss_mode == OMAP_DSS_COLOR_UYVY)
		yuv_mode = true;
	else
		yuv_mode = false;

	for (i = 0; i < *count; i++)
		omap_vrfb_setup(&vout->vrfb_context[i],
				vout->smsshado_phy_addr[i], vout->pix.width,
				vout->pix.height, vout->bpp, yuv_mode);

	return 0;
}

int omap_vout_prepare_vrfb(struct omap_vout_device *vout,
			   struct videobuf_buffer *vb)
{
	struct dma_async_tx_descriptor *tx;
	enum dma_ctrl_flags flags = DMA_PREP_INTERRUPT | DMA_CTRL_ACK;
	struct dma_chan *chan = vout->vrfb_dma_tx.chan;
	struct dma_device *dmadev = chan->device;
	struct dma_interleaved_template *xt = vout->vrfb_dma_tx.xt;
	dma_cookie_t cookie;
	enum dma_status status;
	enum dss_rotation rotation;
	size_t dst_icg;
	u32 pixsize;

	if (!is_rotation_enabled(vout))
		return 0;

	/* If rotation is enabled, copy input buffer into VRFB
	 * memory space using DMA. We are copying input buffer
	 * into VRFB memory space of desired angle and DSS will
	 * read image VRFB memory for 0 degree angle
	 */

	pixsize = vout->bpp * vout->vrfb_bpp;
	dst_icg = ((MAX_PIXELS_PER_LINE * pixsize) -
		  (vout->pix.width * vout->bpp)) + 1;

	xt->src_start = vout->buf_phy_addr[vb->i];
	xt->dst_start = vout->vrfb_context[vb->i].paddr[0];

	xt->numf = vout->pix.height;
	xt->frame_size = 1;
	xt->sgl[0].size = vout->pix.width * vout->bpp;
	xt->sgl[0].icg = dst_icg;

	xt->dir = DMA_MEM_TO_MEM;
	xt->src_sgl = false;
	xt->src_inc = true;
	xt->dst_sgl = true;
	xt->dst_inc = true;

	tx = dmadev->device_prep_interleaved_dma(chan, xt, flags);
	if (tx == NULL) {
		pr_err("%s: DMA interleaved prep error\n", __func__);
		return -EINVAL;
	}

	tx->callback = omap_vout_vrfb_dma_tx_callback;
	tx->callback_param = &vout->vrfb_dma_tx;

	cookie = dmaengine_submit(tx);
	if (dma_submit_error(cookie)) {
		pr_err("%s: dmaengine_submit failed (%d)\n", __func__, cookie);
		return -EINVAL;
	}

	vout->vrfb_dma_tx.tx_status = 0;
	dma_async_issue_pending(chan);

	wait_event_interruptible_timeout(vout->vrfb_dma_tx.wait,
					 vout->vrfb_dma_tx.tx_status == 1,
					 VRFB_TX_TIMEOUT);

	status = dma_async_is_tx_complete(chan, cookie, NULL, NULL);

	if (vout->vrfb_dma_tx.tx_status == 0) {
		pr_err("%s: Timeout while waiting for DMA\n", __func__);
		dmaengine_terminate_sync(chan);
		return -EINVAL;
	} else if (status != DMA_COMPLETE) {
		pr_err("%s: DMA completion %s status\n", __func__,
		       status == DMA_ERROR ? "error" : "busy");
		dmaengine_terminate_sync(chan);
		return -EINVAL;
	}

	/* Store buffers physical address into an array. Addresses
	 * from this array will be used to configure DSS */
	rotation = calc_rotation(vout);
	vout->queued_buf_addr[vb->i] = (u8 *)
		vout->vrfb_context[vb->i].paddr[rotation];
	return 0;
}

/*
 * Calculate the buffer offsets from which the streaming should
 * start. This offset calculation is mainly required because of
 * the VRFB 32 pixels alignment with rotation.
 */
void omap_vout_calculate_vrfb_offset(struct omap_vout_device *vout)
{
	enum dss_rotation rotation;
	bool mirroring = vout->mirror;
	struct v4l2_rect *crop = &vout->crop;
	struct v4l2_pix_format *pix = &vout->pix;
	int *cropped_offset = &vout->cropped_offset;
	int vr_ps = 1, ps = 2, temp_ps = 2;
	int offset = 0, ctop = 0, cleft = 0, line_length = 0;

	rotation = calc_rotation(vout);

	if (V4L2_PIX_FMT_YUYV == pix->pixelformat ||
			V4L2_PIX_FMT_UYVY == pix->pixelformat) {
		if (is_rotation_enabled(vout)) {
			/*
			 * ps    - Actual pixel size for YUYV/UYVY for
			 *         VRFB/Mirroring is 4 bytes
			 * vr_ps - Virtually pixel size for YUYV/UYVY is
			 *         2 bytes
			 */
			ps = 4;
			vr_ps = 2;
		} else {
			ps = 2;	/* otherwise the pixel size is 2 byte */
		}
	} else if (V4L2_PIX_FMT_RGB32 == pix->pixelformat) {
		ps = 4;
	} else if (V4L2_PIX_FMT_RGB24 == pix->pixelformat) {
		ps = 3;
	}
	vout->ps = ps;
	vout->vr_ps = vr_ps;

	if (is_rotation_enabled(vout)) {
		line_length = MAX_PIXELS_PER_LINE;
		ctop = (pix->height - crop->height) - crop->top;
		cleft = (pix->width - crop->width) - crop->left;
	} else {
		line_length = pix->width;
	}
	vout->line_length = line_length;
	switch (rotation) {
	case dss_rotation_90_degree:
		offset = vout->vrfb_context[0].yoffset *
			vout->vrfb_context[0].bytespp;
		temp_ps = ps / vr_ps;
		if (!mirroring) {
			*cropped_offset = offset + line_length *
				temp_ps * cleft + crop->top * temp_ps;
		} else {
			*cropped_offset = offset + line_length * temp_ps *
				cleft + crop->top * temp_ps + (line_length *
				((crop->width / (vr_ps)) - 1) * ps);
		}
		break;
	case dss_rotation_180_degree:
		offset = ((MAX_PIXELS_PER_LINE * vout->vrfb_context[0].yoffset *
			vout->vrfb_context[0].bytespp) +
			(vout->vrfb_context[0].xoffset *
			vout->vrfb_context[0].bytespp));
		if (!mirroring) {
			*cropped_offset = offset + (line_length * ps * ctop) +
				(cleft / vr_ps) * ps;

		} else {
			*cropped_offset = offset + (line_length * ps * ctop) +
				(cleft / vr_ps) * ps + (line_length *
				(crop->height - 1) * ps);
		}
		break;
	case dss_rotation_270_degree:
		offset = MAX_PIXELS_PER_LINE * vout->vrfb_context[0].xoffset *
			vout->vrfb_context[0].bytespp;
		temp_ps = ps / vr_ps;
		if (!mirroring) {
			*cropped_offset = offset + line_length *
			    temp_ps * crop->left + ctop * ps;
		} else {
			*cropped_offset = offset + line_length *
				temp_ps * crop->left + ctop * ps +
				(line_length * ((crop->width / vr_ps) - 1) *
				 ps);
		}
		break;
	case dss_rotation_0_degree:
		if (!mirroring) {
			*cropped_offset = (line_length * ps) *
				crop->top + (crop->left / vr_ps) * ps;
		} else {
			*cropped_offset = (line_length * ps) *
				crop->top + (crop->left / vr_ps) * ps +
				(line_length * (crop->height - 1) * ps);
		}
		break;
	default:
		*cropped_offset = (line_length * ps * crop->top) /
			vr_ps + (crop->left * ps) / vr_ps +
			((crop->width / vr_ps) - 1) * ps;
		break;
	}
}
