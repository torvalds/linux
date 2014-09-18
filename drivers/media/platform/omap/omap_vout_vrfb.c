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

#include <media/videobuf-dma-contig.h>
#include <media/v4l2-device.h>

#include <linux/omap-dma.h>
#include <video/omapvrfb.h>

#include "omap_voutdef.h"
#include "omap_voutlib.h"

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
static void omap_vout_vrfb_dma_tx_callback(int lch, u16 ch_status, void *data)
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
		omap_vout_free_buffer(vout->smsshado_virt_addr[j],
				vout->smsshado_size);
		vout->smsshado_virt_addr[j] = 0;
		vout->smsshado_phy_addr[j] = 0;
	}
}

int omap_vout_setup_vrfb_bufs(struct platform_device *pdev, int vid_num,
			      bool static_vrfb_allocation)
{
	int ret = 0, i, j;
	struct omap_vout_device *vout;
	struct video_device *vfd;
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
	vout->vrfb_dma_tx.dev_id = OMAP_DMA_NO_DEVICE;
	vout->vrfb_dma_tx.dma_ch = -1;
	vout->vrfb_dma_tx.req_status = DMA_CHAN_ALLOTED;
	ret = omap_request_dma(vout->vrfb_dma_tx.dev_id, "VRFB DMA TX",
			omap_vout_vrfb_dma_tx_callback,
			(void *) &vout->vrfb_dma_tx, &vout->vrfb_dma_tx.dma_ch);
	if (ret < 0) {
		vout->vrfb_dma_tx.req_status = DMA_CHAN_NOT_ALLOTED;
		dev_info(&pdev->dev, ": failed to allocate DMA Channel for"
				" video%d\n", vfd->minor);
	}
	init_waitqueue_head(&vout->vrfb_dma_tx.wait);

	/* statically allocated the VRFB buffer is done through
	   commands line aruments */
	if (static_vrfb_allocation) {
		if (omap_vout_allocate_vrfb_buffers(vout, &vrfb_num_bufs, -1)) {
			ret =  -ENOMEM;
			goto release_vrfb_ctx;
		}
		vout->vrfb_static_allocation = 1;
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
		omap_free_dma(vout->vrfb_dma_tx.dma_ch);
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
	dma_addr_t dmabuf;
	struct vid_vrfb_dma *tx;
	enum dss_rotation rotation;
	u32 dest_frame_index = 0, src_element_index = 0;
	u32 dest_element_index = 0, src_frame_index = 0;
	u32 elem_count = 0, frame_count = 0, pixsize = 2;

	if (!is_rotation_enabled(vout))
		return 0;

	dmabuf = vout->buf_phy_addr[vb->i];
	/* If rotation is enabled, copy input buffer into VRFB
	 * memory space using DMA. We are copying input buffer
	 * into VRFB memory space of desired angle and DSS will
	 * read image VRFB memory for 0 degree angle
	 */
	pixsize = vout->bpp * vout->vrfb_bpp;
	/*
	 * DMA transfer in double index mode
	 */

	/* Frame index */
	dest_frame_index = ((MAX_PIXELS_PER_LINE * pixsize) -
			(vout->pix.width * vout->bpp)) + 1;

	/* Source and destination parameters */
	src_element_index = 0;
	src_frame_index = 0;
	dest_element_index = 1;
	/* Number of elements per frame */
	elem_count = vout->pix.width * vout->bpp;
	frame_count = vout->pix.height;
	tx = &vout->vrfb_dma_tx;
	tx->tx_status = 0;
	omap_set_dma_transfer_params(tx->dma_ch, OMAP_DMA_DATA_TYPE_S32,
			(elem_count / 4), frame_count, OMAP_DMA_SYNC_ELEMENT,
			tx->dev_id, 0x0);
	/* src_port required only for OMAP1 */
	omap_set_dma_src_params(tx->dma_ch, 0, OMAP_DMA_AMODE_POST_INC,
			dmabuf, src_element_index, src_frame_index);
	/*set dma source burst mode for VRFB */
	omap_set_dma_src_burst_mode(tx->dma_ch, OMAP_DMA_DATA_BURST_16);
	rotation = calc_rotation(vout);

	/* dest_port required only for OMAP1 */
	omap_set_dma_dest_params(tx->dma_ch, 0, OMAP_DMA_AMODE_DOUBLE_IDX,
			vout->vrfb_context[vb->i].paddr[0], dest_element_index,
			dest_frame_index);
	/*set dma dest burst mode for VRFB */
	omap_set_dma_dest_burst_mode(tx->dma_ch, OMAP_DMA_DATA_BURST_16);
	omap_dma_set_global_params(DMA_DEFAULT_ARB_RATE, 0x20, 0);

	omap_start_dma(tx->dma_ch);
	wait_event_interruptible_timeout(tx->wait, tx->tx_status == 1,
					 VRFB_TX_TIMEOUT);

	if (tx->tx_status == 0) {
		omap_stop_dma(tx->dma_ch);
		return -EINVAL;
	}
	/* Store buffers physical address into an array. Addresses
	 * from this array will be used to configure DSS */
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
		if (mirroring == 0) {
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
		if (mirroring == 0) {
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
		if (mirroring == 0) {
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
		if (mirroring == 0) {
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
