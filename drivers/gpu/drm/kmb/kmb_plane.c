// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright © 2018-2020 Intel Corporation
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_plane_helper.h>

#include "kmb_drv.h"
#include "kmb_plane.h"
#include "kmb_regs.h"

const u32 layer_irqs[] = {
	LCD_INT_VL0,
	LCD_INT_VL1,
	LCD_INT_GL0,
	LCD_INT_GL1
};

/* Conversion (yuv->rgb) matrix from myriadx */
static const u32 csc_coef_lcd[] = {
	1024, 0, 1436,
	1024, -352, -731,
	1024, 1814, 0,
	-179, 125, -226
};

/* Graphics layer (layers 2 & 3) formats, only packed formats  are supported */
static const u32 kmb_formats_g[] = {
	DRM_FORMAT_RGB332,
	DRM_FORMAT_XRGB4444, DRM_FORMAT_XBGR4444,
	DRM_FORMAT_ARGB4444, DRM_FORMAT_ABGR4444,
	DRM_FORMAT_XRGB1555, DRM_FORMAT_XBGR1555,
	DRM_FORMAT_ARGB1555, DRM_FORMAT_ABGR1555,
	DRM_FORMAT_RGB565, DRM_FORMAT_BGR565,
	DRM_FORMAT_RGB888, DRM_FORMAT_BGR888,
	DRM_FORMAT_XRGB8888, DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888, DRM_FORMAT_ABGR8888,
};

/* Video layer ( 0 & 1) formats, packed and planar formats are supported */
static const u32 kmb_formats_v[] = {
	/* packed formats */
	DRM_FORMAT_RGB332,
	DRM_FORMAT_XRGB4444, DRM_FORMAT_XBGR4444,
	DRM_FORMAT_ARGB4444, DRM_FORMAT_ABGR4444,
	DRM_FORMAT_XRGB1555, DRM_FORMAT_XBGR1555,
	DRM_FORMAT_ARGB1555, DRM_FORMAT_ABGR1555,
	DRM_FORMAT_RGB565, DRM_FORMAT_BGR565,
	DRM_FORMAT_RGB888, DRM_FORMAT_BGR888,
	DRM_FORMAT_XRGB8888, DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ARGB8888, DRM_FORMAT_ABGR8888,
	/*planar formats */
	DRM_FORMAT_YUV420, DRM_FORMAT_YVU420,
	DRM_FORMAT_YUV422, DRM_FORMAT_YVU422,
	DRM_FORMAT_YUV444, DRM_FORMAT_YVU444,
	DRM_FORMAT_NV12, DRM_FORMAT_NV21,
};

static unsigned int check_pixel_format(struct drm_plane *plane, u32 format)
{
	int i;

	for (i = 0; i < plane->format_count; i++) {
		if (plane->format_types[i] == format)
			return 0;
	}
	return -EINVAL;
}

static int kmb_plane_atomic_check(struct drm_plane *plane,
				  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct drm_framebuffer *fb;
	int ret;
	struct drm_crtc_state *crtc_state;
	bool can_position;

	fb = new_plane_state->fb;
	if (!fb || !new_plane_state->crtc)
		return 0;

	ret = check_pixel_format(plane, fb->format->format);
	if (ret)
		return ret;

	if (new_plane_state->crtc_w > KMB_MAX_WIDTH || new_plane_state->crtc_h > KMB_MAX_HEIGHT)
		return -EINVAL;
	if (new_plane_state->crtc_w < KMB_MIN_WIDTH || new_plane_state->crtc_h < KMB_MIN_HEIGHT)
		return -EINVAL;
	can_position = (plane->type == DRM_PLANE_TYPE_OVERLAY);
	crtc_state =
		drm_atomic_get_existing_crtc_state(state,
						   new_plane_state->crtc);
	return drm_atomic_helper_check_plane_state(new_plane_state,
						   crtc_state,
						   DRM_PLANE_HELPER_NO_SCALING,
						   DRM_PLANE_HELPER_NO_SCALING,
						   can_position, true);
}

static void kmb_plane_atomic_disable(struct drm_plane *plane,
				     struct drm_atomic_state *state)
{
	struct kmb_plane *kmb_plane = to_kmb_plane(plane);
	int plane_id = kmb_plane->id;
	struct kmb_drm_private *kmb;

	kmb = to_kmb(plane->dev);

	if (WARN_ON(plane_id >= KMB_MAX_PLANES))
		return;

	switch (plane_id) {
	case LAYER_0:
		kmb->plane_status[plane_id].ctrl = LCD_CTRL_VL1_ENABLE;
		break;
	case LAYER_1:
		kmb->plane_status[plane_id].ctrl = LCD_CTRL_VL2_ENABLE;
		break;
	case LAYER_2:
		kmb->plane_status[plane_id].ctrl = LCD_CTRL_GL1_ENABLE;
		break;
	case LAYER_3:
		kmb->plane_status[plane_id].ctrl = LCD_CTRL_GL2_ENABLE;
		break;
	}

	kmb->plane_status[plane_id].disable = true;
}

static unsigned int get_pixel_format(u32 format)
{
	unsigned int val = 0;

	switch (format) {
		/* planar formats */
	case DRM_FORMAT_YUV444:
		val = LCD_LAYER_FORMAT_YCBCR444PLAN | LCD_LAYER_PLANAR_STORAGE;
		break;
	case DRM_FORMAT_YVU444:
		val = LCD_LAYER_FORMAT_YCBCR444PLAN | LCD_LAYER_PLANAR_STORAGE
		    | LCD_LAYER_CRCB_ORDER;
		break;
	case DRM_FORMAT_YUV422:
		val = LCD_LAYER_FORMAT_YCBCR422PLAN | LCD_LAYER_PLANAR_STORAGE;
		break;
	case DRM_FORMAT_YVU422:
		val = LCD_LAYER_FORMAT_YCBCR422PLAN | LCD_LAYER_PLANAR_STORAGE
		    | LCD_LAYER_CRCB_ORDER;
		break;
	case DRM_FORMAT_YUV420:
		val = LCD_LAYER_FORMAT_YCBCR420PLAN | LCD_LAYER_PLANAR_STORAGE;
		break;
	case DRM_FORMAT_YVU420:
		val = LCD_LAYER_FORMAT_YCBCR420PLAN | LCD_LAYER_PLANAR_STORAGE
		    | LCD_LAYER_CRCB_ORDER;
		break;
	case DRM_FORMAT_NV12:
		val = LCD_LAYER_FORMAT_NV12 | LCD_LAYER_PLANAR_STORAGE;
		break;
	case DRM_FORMAT_NV21:
		val = LCD_LAYER_FORMAT_NV12 | LCD_LAYER_PLANAR_STORAGE
		    | LCD_LAYER_CRCB_ORDER;
		break;
		/* packed formats */
		/* looks hw requires B & G to be swapped when RGB */
	case DRM_FORMAT_RGB332:
		val = LCD_LAYER_FORMAT_RGB332 | LCD_LAYER_BGR_ORDER;
		break;
	case DRM_FORMAT_XBGR4444:
		val = LCD_LAYER_FORMAT_RGBX4444;
		break;
	case DRM_FORMAT_ARGB4444:
		val = LCD_LAYER_FORMAT_RGBA4444 | LCD_LAYER_BGR_ORDER;
		break;
	case DRM_FORMAT_ABGR4444:
		val = LCD_LAYER_FORMAT_RGBA4444;
		break;
	case DRM_FORMAT_XRGB1555:
		val = LCD_LAYER_FORMAT_XRGB1555 | LCD_LAYER_BGR_ORDER;
		break;
	case DRM_FORMAT_XBGR1555:
		val = LCD_LAYER_FORMAT_XRGB1555;
		break;
	case DRM_FORMAT_ARGB1555:
		val = LCD_LAYER_FORMAT_RGBA1555 | LCD_LAYER_BGR_ORDER;
		break;
	case DRM_FORMAT_ABGR1555:
		val = LCD_LAYER_FORMAT_RGBA1555;
		break;
	case DRM_FORMAT_RGB565:
		val = LCD_LAYER_FORMAT_RGB565 | LCD_LAYER_BGR_ORDER;
		break;
	case DRM_FORMAT_BGR565:
		val = LCD_LAYER_FORMAT_RGB565;
		break;
	case DRM_FORMAT_RGB888:
		val = LCD_LAYER_FORMAT_RGB888 | LCD_LAYER_BGR_ORDER;
		break;
	case DRM_FORMAT_BGR888:
		val = LCD_LAYER_FORMAT_RGB888;
		break;
	case DRM_FORMAT_XRGB8888:
		val = LCD_LAYER_FORMAT_RGBX8888 | LCD_LAYER_BGR_ORDER;
		break;
	case DRM_FORMAT_XBGR8888:
		val = LCD_LAYER_FORMAT_RGBX8888;
		break;
	case DRM_FORMAT_ARGB8888:
		val = LCD_LAYER_FORMAT_RGBA8888 | LCD_LAYER_BGR_ORDER;
		break;
	case DRM_FORMAT_ABGR8888:
		val = LCD_LAYER_FORMAT_RGBA8888;
		break;
	}
	DRM_INFO_ONCE("%s : %d format=0x%x val=0x%x\n",
		      __func__, __LINE__, format, val);
	return val;
}

static unsigned int get_bits_per_pixel(const struct drm_format_info *format)
{
	u32 bpp = 0;
	unsigned int val = 0;

	if (format->num_planes > 1) {
		val = LCD_LAYER_8BPP;
		return val;
	}

	bpp += 8 * format->cpp[0];

	switch (bpp) {
	case 8:
		val = LCD_LAYER_8BPP;
		break;
	case 16:
		val = LCD_LAYER_16BPP;
		break;
	case 24:
		val = LCD_LAYER_24BPP;
		break;
	case 32:
		val = LCD_LAYER_32BPP;
		break;
	}

	DRM_DEBUG("bpp=%d val=0x%x\n", bpp, val);
	return val;
}

static void config_csc(struct kmb_drm_private *kmb, int plane_id)
{
	/* YUV to RGB conversion using the fixed matrix csc_coef_lcd */
	kmb_write_lcd(kmb, LCD_LAYERn_CSC_COEFF11(plane_id), csc_coef_lcd[0]);
	kmb_write_lcd(kmb, LCD_LAYERn_CSC_COEFF12(plane_id), csc_coef_lcd[1]);
	kmb_write_lcd(kmb, LCD_LAYERn_CSC_COEFF13(plane_id), csc_coef_lcd[2]);
	kmb_write_lcd(kmb, LCD_LAYERn_CSC_COEFF21(plane_id), csc_coef_lcd[3]);
	kmb_write_lcd(kmb, LCD_LAYERn_CSC_COEFF22(plane_id), csc_coef_lcd[4]);
	kmb_write_lcd(kmb, LCD_LAYERn_CSC_COEFF23(plane_id), csc_coef_lcd[5]);
	kmb_write_lcd(kmb, LCD_LAYERn_CSC_COEFF31(plane_id), csc_coef_lcd[6]);
	kmb_write_lcd(kmb, LCD_LAYERn_CSC_COEFF32(plane_id), csc_coef_lcd[7]);
	kmb_write_lcd(kmb, LCD_LAYERn_CSC_COEFF33(plane_id), csc_coef_lcd[8]);
	kmb_write_lcd(kmb, LCD_LAYERn_CSC_OFF1(plane_id), csc_coef_lcd[9]);
	kmb_write_lcd(kmb, LCD_LAYERn_CSC_OFF2(plane_id), csc_coef_lcd[10]);
	kmb_write_lcd(kmb, LCD_LAYERn_CSC_OFF3(plane_id), csc_coef_lcd[11]);
}

static void kmb_plane_atomic_update(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	struct drm_plane_state *old_plane_state = drm_atomic_get_old_plane_state(state,
										 plane);
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct drm_framebuffer *fb;
	struct kmb_drm_private *kmb;
	unsigned int width;
	unsigned int height;
	unsigned int dma_len;
	struct kmb_plane *kmb_plane;
	unsigned int dma_cfg;
	unsigned int ctrl = 0, val = 0, out_format = 0;
	unsigned int src_w, src_h, crtc_x, crtc_y;
	unsigned char plane_id;
	int num_planes;
	static dma_addr_t addr[MAX_SUB_PLANES];

	if (!plane || !new_plane_state || !old_plane_state)
		return;

	fb = new_plane_state->fb;
	if (!fb)
		return;
	num_planes = fb->format->num_planes;
	kmb_plane = to_kmb_plane(plane);
	plane_id = kmb_plane->id;

	kmb = to_kmb(plane->dev);

	spin_lock_irq(&kmb->irq_lock);
	if (kmb->kmb_under_flow || kmb->kmb_flush_done) {
		spin_unlock_irq(&kmb->irq_lock);
		drm_dbg(&kmb->drm, "plane_update:underflow!!!! returning");
		return;
	}
	spin_unlock_irq(&kmb->irq_lock);

	src_w = (new_plane_state->src_w >> 16);
	src_h = new_plane_state->src_h >> 16;
	crtc_x = new_plane_state->crtc_x;
	crtc_y = new_plane_state->crtc_y;

	drm_dbg(&kmb->drm,
		"src_w=%d src_h=%d, fb->format->format=0x%x fb->flags=0x%x\n",
		  src_w, src_h, fb->format->format, fb->flags);

	width = fb->width;
	height = fb->height;
	dma_len = (width * height * fb->format->cpp[0]);
	drm_dbg(&kmb->drm, "dma_len=%d ", dma_len);
	kmb_write_lcd(kmb, LCD_LAYERn_DMA_LEN(plane_id), dma_len);
	kmb_write_lcd(kmb, LCD_LAYERn_DMA_LEN_SHADOW(plane_id), dma_len);
	kmb_write_lcd(kmb, LCD_LAYERn_DMA_LINE_VSTRIDE(plane_id),
		      fb->pitches[0]);
	kmb_write_lcd(kmb, LCD_LAYERn_DMA_LINE_WIDTH(plane_id),
		      (width * fb->format->cpp[0]));

	addr[Y_PLANE] = drm_fb_cma_get_gem_addr(fb, new_plane_state, 0);
	kmb_write_lcd(kmb, LCD_LAYERn_DMA_START_ADDR(plane_id),
		      addr[Y_PLANE] + fb->offsets[0]);
	val = get_pixel_format(fb->format->format);
	val |= get_bits_per_pixel(fb->format);
	/* Program Cb/Cr for planar formats */
	if (num_planes > 1) {
		kmb_write_lcd(kmb, LCD_LAYERn_DMA_CB_LINE_VSTRIDE(plane_id),
			      width * fb->format->cpp[0]);
		kmb_write_lcd(kmb, LCD_LAYERn_DMA_CB_LINE_WIDTH(plane_id),
			      (width * fb->format->cpp[0]));

		addr[U_PLANE] = drm_fb_cma_get_gem_addr(fb, new_plane_state,
							U_PLANE);
		/* check if Cb/Cr is swapped*/
		if (num_planes == 3 && (val & LCD_LAYER_CRCB_ORDER))
			kmb_write_lcd(kmb,
				      LCD_LAYERn_DMA_START_CR_ADR(plane_id),
					addr[U_PLANE]);
		else
			kmb_write_lcd(kmb,
				      LCD_LAYERn_DMA_START_CB_ADR(plane_id),
					addr[U_PLANE]);

		if (num_planes == 3) {
			kmb_write_lcd(kmb,
				      LCD_LAYERn_DMA_CR_LINE_VSTRIDE(plane_id),
				      ((width) * fb->format->cpp[0]));

			kmb_write_lcd(kmb,
				      LCD_LAYERn_DMA_CR_LINE_WIDTH(plane_id),
				      ((width) * fb->format->cpp[0]));

			addr[V_PLANE] = drm_fb_cma_get_gem_addr(fb,
								new_plane_state,
								V_PLANE);

			/* check if Cb/Cr is swapped*/
			if (val & LCD_LAYER_CRCB_ORDER)
				kmb_write_lcd(kmb,
					      LCD_LAYERn_DMA_START_CB_ADR(plane_id),
					      addr[V_PLANE]);
			else
				kmb_write_lcd(kmb,
					      LCD_LAYERn_DMA_START_CR_ADR(plane_id),
					      addr[V_PLANE]);
		}
	}

	kmb_write_lcd(kmb, LCD_LAYERn_WIDTH(plane_id), src_w - 1);
	kmb_write_lcd(kmb, LCD_LAYERn_HEIGHT(plane_id), src_h - 1);
	kmb_write_lcd(kmb, LCD_LAYERn_COL_START(plane_id), crtc_x);
	kmb_write_lcd(kmb, LCD_LAYERn_ROW_START(plane_id), crtc_y);

	val |= LCD_LAYER_FIFO_100;

	if (val & LCD_LAYER_PLANAR_STORAGE) {
		val |= LCD_LAYER_CSC_EN;

		/* Enable CSC if input is planar and output is RGB */
		config_csc(kmb, plane_id);
	}

	kmb_write_lcd(kmb, LCD_LAYERn_CFG(plane_id), val);

	switch (plane_id) {
	case LAYER_0:
		ctrl = LCD_CTRL_VL1_ENABLE;
		break;
	case LAYER_1:
		ctrl = LCD_CTRL_VL2_ENABLE;
		break;
	case LAYER_2:
		ctrl = LCD_CTRL_GL1_ENABLE;
		break;
	case LAYER_3:
		ctrl = LCD_CTRL_GL2_ENABLE;
		break;
	}

	ctrl |= LCD_CTRL_PROGRESSIVE | LCD_CTRL_TIM_GEN_ENABLE
	    | LCD_CTRL_CONTINUOUS | LCD_CTRL_OUTPUT_ENABLED;

	/* LCD is connected to MIPI on kmb
	 * Therefore this bit is required for DSI Tx
	 */
	ctrl |= LCD_CTRL_VHSYNC_IDLE_LVL;

	kmb_set_bitmask_lcd(kmb, LCD_CONTROL, ctrl);

	/* Enable pipeline AXI read transactions for the DMA
	 * after setting graphics layers. This must be done
	 * in a separate write cycle.
	 */
	kmb_set_bitmask_lcd(kmb, LCD_CONTROL, LCD_CTRL_PIPELINE_DMA);

	/* FIXME no doc on how to set output format, these values are taken
	 * from the Myriadx tests
	 */
	out_format |= LCD_OUTF_FORMAT_RGB888;

	/* Leave RGB order,conversion mode and clip mode to default */
	/* do not interleave RGB channels for mipi Tx compatibility */
	out_format |= LCD_OUTF_MIPI_RGB_MODE;
	kmb_write_lcd(kmb, LCD_OUT_FORMAT_CFG, out_format);

	dma_cfg = LCD_DMA_LAYER_ENABLE | LCD_DMA_LAYER_VSTRIDE_EN |
	    LCD_DMA_LAYER_CONT_UPDATE | LCD_DMA_LAYER_AXI_BURST_16;

	/* Enable DMA */
	kmb_write_lcd(kmb, LCD_LAYERn_DMA_CFG(plane_id), dma_cfg);
	drm_dbg(&kmb->drm, "dma_cfg=0x%x LCD_DMA_CFG=0x%x\n", dma_cfg,
		kmb_read_lcd(kmb, LCD_LAYERn_DMA_CFG(plane_id)));

	kmb_set_bitmask_lcd(kmb, LCD_INT_CLEAR, LCD_INT_EOF |
			LCD_INT_DMA_ERR);
	kmb_set_bitmask_lcd(kmb, LCD_INT_ENABLE, LCD_INT_EOF |
			LCD_INT_DMA_ERR);
}

static const struct drm_plane_helper_funcs kmb_plane_helper_funcs = {
	.atomic_check = kmb_plane_atomic_check,
	.atomic_update = kmb_plane_atomic_update,
	.atomic_disable = kmb_plane_atomic_disable
};

void kmb_plane_destroy(struct drm_plane *plane)
{
	struct kmb_plane *kmb_plane = to_kmb_plane(plane);

	drm_plane_cleanup(plane);
	kfree(kmb_plane);
}

static const struct drm_plane_funcs kmb_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = kmb_plane_destroy,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

struct kmb_plane *kmb_plane_init(struct drm_device *drm)
{
	struct kmb_drm_private *kmb = to_kmb(drm);
	struct kmb_plane *plane = NULL;
	struct kmb_plane *primary = NULL;
	int i = 0;
	int ret = 0;
	enum drm_plane_type plane_type;
	const u32 *plane_formats;
	int num_plane_formats;

	for (i = 0; i < KMB_MAX_PLANES; i++) {
		plane = drmm_kzalloc(drm, sizeof(*plane), GFP_KERNEL);

		if (!plane) {
			drm_err(drm, "Failed to allocate plane\n");
			return ERR_PTR(-ENOMEM);
		}

		plane_type = (i == 0) ? DRM_PLANE_TYPE_PRIMARY :
		    DRM_PLANE_TYPE_OVERLAY;
		if (i < 2) {
			plane_formats = kmb_formats_v;
			num_plane_formats = ARRAY_SIZE(kmb_formats_v);
		} else {
			plane_formats = kmb_formats_g;
			num_plane_formats = ARRAY_SIZE(kmb_formats_g);
		}

		ret = drm_universal_plane_init(drm, &plane->base_plane,
					       POSSIBLE_CRTCS, &kmb_plane_funcs,
					       plane_formats, num_plane_formats,
					       NULL, plane_type, "plane %d", i);
		if (ret < 0) {
			drm_err(drm, "drm_universal_plane_init failed (ret=%d)",
				ret);
			goto cleanup;
		}
		drm_dbg(drm, "%s : %d i=%d type=%d",
			__func__, __LINE__,
			  i, plane_type);
		drm_plane_helper_add(&plane->base_plane,
				     &kmb_plane_helper_funcs);
		if (plane_type == DRM_PLANE_TYPE_PRIMARY) {
			primary = plane;
			kmb->plane = plane;
		}
		drm_dbg(drm, "%s : %d primary=%p\n", __func__, __LINE__,
			&primary->base_plane);
		plane->id = i;
	}

	/* Disable pipeline AXI read transactions for the DMA
	 * prior to setting graphics layers
	 */
	kmb_clr_bitmask_lcd(kmb, LCD_CONTROL, LCD_CTRL_PIPELINE_DMA);

	return primary;
cleanup:
	drmm_kfree(drm, plane);
	return ERR_PTR(ret);
}
