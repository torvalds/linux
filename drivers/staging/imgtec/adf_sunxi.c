/* -*- mode: c; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* vi: set ts=8 sw=8 sts=8: */
/*************************************************************************/ /*!
@File           adf_sunxi.c
@Codingstyle    LinuxKernel
@Copyright      Copyright (c) Imagination Technologies Ltd. All Rights Reserved
@License        Dual MIT/GPLv2

The contents of this file are subject to the MIT license as set out below.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

Alternatively, the contents of this file may be used under the terms of
the GNU General Public License Version 2 ("GPL") in which case the provisions
of GPL are applicable instead of those above.

If you wish to allow use of your version of this file only under the terms of
GPL, and not to allow others to use your version of this file under the terms
of the MIT license, indicate your decision by deleting the provisions above
and replace them with the notice and other provisions required by GPL as set
out in the file called "GPL-COPYING" included in this distribution. If you do
not delete the provisions above, a recipient may use your version of this file
under the terms of either the MIT license or GPL.

This License is also included in this distribution in the file called
"MIT-COPYING".

EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/ /**************************************************************************/

#include <video/adf.h>
#include <video/adf_client.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>

#ifdef SUPPORT_ADF_SUNXI_FBDEV
#include <video/adf_fbdev.h>
#endif

#include PVR_ANDROID_ION_HEADER
#include PVR_ANDROID_SYNC_HEADER

#include <linux/sw_sync.h>

#include "adf_common.h"
#include "adf_sunxi.h"

#include "pvrmodule.h"

#define MAX_DISPLAYS 2
/* This is the maximum number of overlays per display
 * Any global limitation validation must be done in either adf_sunxi_attach()
 * or adf_sunxi_validate()
 */
#define NUM_OVERLAYS 4
#define NUM_BLENDER_PIPES 2
#define MAX_BUFFERS (MAX_DISPLAYS * NUM_OVERLAYS)

#define DEBUG_POST_DUMP_COUNT 4
#define VALIDATE_LOG_LINES 50
#define VALIDATE_LOG_LINE_SIZE 50

#ifdef ADF_VERBOSE_DEBUG
#define sunxi_dbg(x...) dev_dbg(x)
#else
#define sunxi_dbg(...)
#endif

struct sunxi_interface {
	struct adf_interface interface;
	enum adf_interface_type adf_type;
	const char *name;

	int num_supported_modes;
	struct drm_mode_modeinfo *supported_modes;

	bool connected;

	int display_id;
	disp_output_type disp_type;
};


struct sunxi_overlay {
	struct adf_overlay_engine overlay;
	/* <0 means not attached, otherwise offset in sunxi.interfaces */
	struct sunxi_interface *interface;
};

struct {
	struct adf_device device;
	struct device *dev;
	struct sunxi_interface interfaces[MAX_DISPLAYS];
	struct sunxi_overlay overlays[MAX_DISPLAYS][NUM_OVERLAYS];
	struct ion_client *ion_client;
	u32 ion_heap_id;
	atomic_t refcount;

	struct disp_composer_ops disp_ops;

	/* Used to dump the last config to debugfs file */
	struct setup_dispc_data last_config[DEBUG_POST_DUMP_COUNT];
	u32 last_config_id[DEBUG_POST_DUMP_COUNT];
	int last_config_pos;

	char validate_log[VALIDATE_LOG_LINES][VALIDATE_LOG_LINE_SIZE];
	int validate_log_position;

	struct dentry *debugfs_config_file;
	struct dentry *debugfs_val_log;

	atomic_t postcount;
	atomic_t callbackcount;

	wait_queue_head_t post_wait_queue;

#ifdef SUPPORT_ADF_SUNXI_FBDEV
	struct adf_fbdev fbdev;
#endif
} sunxi;

static const u32 sunxi_supported_formats[] = {
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_YVU420,
};
#define NUM_SUPPORTED_FORMATS ARRAY_SIZE(sunxi_supported_formats)

static void val_log(u32 post_id, const char *fmt, ...)
{
	va_list args;
	char *str;
	int offset;

	sunxi.validate_log_position = (sunxi.validate_log_position + 1)
		% VALIDATE_LOG_LINES;

	str = sunxi.validate_log[sunxi.validate_log_position];

	offset = snprintf(str, VALIDATE_LOG_LINE_SIZE, "id %u:",
		post_id);

	va_start(args, fmt);
	vsnprintf(str+offset, VALIDATE_LOG_LINE_SIZE-offset, fmt, args);
	va_end(args);
}

static bool
is_supported_format(u32 drm_format)
{
	int i;

	for (i = 0; i < NUM_SUPPORTED_FORMATS; i++) {
		if (sunxi_supported_formats[i] == drm_format)
			return true;
	}
	return false;
}

static disp_pixel_format
sunxi_format_to_disp(u32 format)
{
	switch (format) {
	case DRM_FORMAT_BGRA8888:
		return DISP_FORMAT_ARGB_8888;
	case DRM_FORMAT_ARGB8888:
		return DISP_FORMAT_BGRA_8888;
	case DRM_FORMAT_BGRX8888:
		return DISP_FORMAT_XRGB_8888;
	case DRM_FORMAT_XRGB8888:
		return DISP_FORMAT_BGRX_8888;
	case DRM_FORMAT_YVU420:
		return DISP_FORMAT_YUV420_P;
	default:
		BUG();
		return -1;
	}
}

static bool
sunxi_format_has_alpha(u32 format)
{
	switch (format) {
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_ARGB8888:
		return true;
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_XRGB8888:
	case DRM_FORMAT_YVU420:
		return false;
	default:
		BUG();
		return false;
	}
}

static u32
sunxi_format_bpp(u32 format)
{
	switch (format) {
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB8888:
		return 4;
	case DRM_FORMAT_YVU420:
		return 1;
	default:
		BUG();
		return 0;
	}
}

static bool
sunxi_format_uv_is_swapped(u32 format)
{
	switch (format) {
	case DRM_FORMAT_BGRA8888:
	case DRM_FORMAT_BGRX8888:
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB8888:
		return false;
	case DRM_FORMAT_YVU420:
		return true;
	default:
		BUG();
		return false;
	}
}

static bool
buffer_is_scaled(const struct adf_buffer_config_ext *ext_config_data)
{
	int srcWidth = ext_config_data->crop.x2 - ext_config_data->crop.x1;
	int srcHeight = ext_config_data->crop.y2 - ext_config_data->crop.y1;
	int dstWidth = ext_config_data->display.x2
		- ext_config_data->display.x1;
	int dstHeight = ext_config_data->display.y2
		- ext_config_data->display.y1;
	if (srcWidth != dstWidth ||
	    srcHeight != dstHeight)
		return true;
	else
		return false;
}

static int
sunxi_buffer_to_layer_info(const struct adf_buffer *buf,
	const struct adf_buffer_config_ext *ext_config_data,
	const struct adf_buffer_mapping *mappings, disp_layer_info *layer)
{
	int plane;

	if (buffer_is_scaled(ext_config_data))
		layer->mode = DISP_LAYER_WORK_MODE_SCALER;
	else
		layer->mode = DISP_LAYER_WORK_MODE_NORMAL;

	/* Pipe/z are set in the parent function */
	layer->pipe = 0;
	layer->zorder = 0;
	/*  0 = per-pixel alpha, 1 = global alpha */
	switch (ext_config_data->blend_type) {
	case ADF_BUFFER_BLENDING_NONE_EXT:
		layer->alpha_mode = 1;
		layer->alpha_value = 255;
		break;
	case ADF_BUFFER_BLENDING_PREMULT_EXT:
		if (sunxi_format_has_alpha(buf->format))
			layer->alpha_mode = 0;
		else
			layer->alpha_mode = 1;
		layer->alpha_value = ext_config_data->plane_alpha;
		layer->fb.pre_multiply = true;
		break;
	case ADF_BUFFER_BLENDING_COVERAGE_EXT:
		dev_err(sunxi.dev, "Coverage blending not implemented\n");
		return -1;
	default:
		dev_err(sunxi.dev, "Unknown blending type %d\n",
			ext_config_data->blend_type);
		return -1;

	}
	layer->ck_enable = false;
	layer->screen_win.x = ext_config_data->display.x1;
	layer->screen_win.y = ext_config_data->display.y1;
	layer->screen_win.width = ext_config_data->display.x2 -
		ext_config_data->display.x1;
	layer->screen_win.height = ext_config_data->display.y2 -
		ext_config_data->display.y1;

	if (mappings) {
		for (plane = 0; plane < buf->n_planes; plane++) {
			layer->fb.addr[plane] =
				sg_phys(mappings->sg_tables[plane]->sgl) +
				buf->offset[plane];
		}

		/* Fix up planar formats with VU plane ordering. For some
		 * reason this is not properly handled by the sunxi disp
		 * driver for sun9i.
		 */
		if (sunxi_format_uv_is_swapped(buf->format)) {
			unsigned int tmp = layer->fb.addr[1];

			layer->fb.addr[1] = layer->fb.addr[2];
			layer->fb.addr[2] = tmp;
		}
	}

	layer->fb.size.width = buf->pitch[0] / sunxi_format_bpp(buf->format);
	layer->fb.size.height = buf->h;
	layer->fb.format = sunxi_format_to_disp(buf->format);
	layer->fb.src_win.x = ext_config_data->crop.x1;
	layer->fb.src_win.y = ext_config_data->crop.y1;
	/*  fb.src_win.width/height is only used for scaled layers */
	layer->fb.src_win.width = ext_config_data->crop.x2 -
		ext_config_data->crop.x1;
	layer->fb.src_win.height = ext_config_data->crop.y2 -
		ext_config_data->crop.y1;

	return 0;
}

static int
adf_sunxi_open(struct adf_obj *obj, struct inode *inode, struct file *file)
{
	atomic_inc(&sunxi.refcount);
	return 0;
}

static void adf_sunxi_set_hotplug_state(struct sunxi_interface *intf,
	bool enable);

static void
adf_sunxi_release(struct adf_obj *obj, struct inode *inode, struct file *file)
{
	struct sync_fence *release_fence;
	int dpy;

	if (atomic_dec_return(&sunxi.refcount))
		return;

	/* NULL flip to push buffer off screen */
	release_fence = adf_device_post(obj->parent, NULL, 0, NULL, 0, NULL, 0);

	if (IS_ERR_OR_NULL(release_fence)) {
		dev_err(obj->parent->dev, "Failed to queue null flip command (err=%d)\n",
			(int)PTR_ERR(release_fence));
		return;
	}

	/* Disable any hotplug events */
	for (dpy = 0; dpy < MAX_DISPLAYS; dpy++) {
		if (sunxi.interfaces[dpy].disp_type == DISP_OUTPUT_TYPE_NONE)
			continue;
		if (sunxi.interfaces[dpy].disp_type == DISP_OUTPUT_TYPE_HDMI)
			adf_sunxi_set_hotplug_state(&sunxi.interfaces[dpy],
				false);
	}

	sync_fence_put(release_fence);
}

struct pipe_assignments {
	int pipe[MAX_BUFFERS];
};
static void adf_sunxi_state_free(struct adf_device *dev, void *driver_state)
{
	struct pipe_assignments *pipe_assignments =
		driver_state;

	kfree(pipe_assignments);
}

static int get_buf_display(struct adf_buffer *buf)
{
	int dpy, ovl;

	for (dpy = 0; dpy < MAX_DISPLAYS; dpy++) {
		for (ovl = 0; ovl < NUM_OVERLAYS; ovl++) {
			if (&sunxi.overlays[dpy][ovl].overlay ==
				buf->overlay_engine) {
				goto found_ovl;
			}
		}
	}
	return -1;
found_ovl:
	return dpy;
}

struct pipe_assignment_state {
	int current_pipe[MAX_DISPLAYS];
	int max_pipe[MAX_DISPLAYS];
	int current_pipe_layers[MAX_DISPLAYS];

	struct drm_clip_rect current_pipe_rects[MAX_DISPLAYS][NUM_OVERLAYS];
};

static int
assign_pipe(struct pipe_assignment_state *state, int dpy, bool blended,
	struct drm_clip_rect *display_rect)
{
	struct drm_clip_rect *current_pipe_rects =
		&state->current_pipe_rects[dpy][0];
	int rect;

	/* The sunxi display block appears to support a single blender
	* taking multiple input rects, so long as the blended
	* rects do not overlap
	*/
	if (blended) {
		for (rect = 0; rect < state->current_pipe_layers[dpy]; rect++) {
			const struct drm_clip_rect *layer_rect = &
				current_pipe_rects[rect];
			if (!adf_img_rects_intersect(layer_rect,
				display_rect)) {
				continue;
			}
			/* We need to assign a new pipe */
			state->current_pipe[dpy]++;
			state->current_pipe_layers[dpy] = 0;
			if (state->current_pipe[dpy] >=
				state->max_pipe[dpy]) {
				return -1;
			}
		}
	}
	current_pipe_rects[state->current_pipe_layers[dpy]] =
		*display_rect;
	state->current_pipe_layers[dpy]++;

	return state->current_pipe[dpy];
}

static int adf_sunxi_validate(struct adf_device *dev, struct adf_post *cfg,
	void **driver_state)
{
	int i, dpy, pipe;
	struct adf_post_ext *post_ext = cfg->custom_data;
	struct adf_buffer_config_ext *bufs_ext;
	size_t expected_custom_data_size;

	struct pipe_assignment_state pipe_state;
	struct pipe_assignments *pipe_assignments;
	bool scaler_in_use[MAX_DISPLAYS];
	int err = 0;
	u32 post_id;

	bool is_post = cfg->n_bufs && cfg->bufs[0].acquire_fence != NULL;

	if (cfg->n_bufs == 0) {
		val_log(0, "NULL flip\n");
		return 0;
	}

	if (!post_ext) {
		dev_err(dev->dev, "Invalid custom data pointer\n");
		return -EINVAL;
	}
	post_id = post_ext->post_id;

	expected_custom_data_size = sizeof(struct adf_post_ext)
		+ cfg->n_bufs * sizeof(struct adf_buffer_config_ext);
	if (cfg->custom_data_size != expected_custom_data_size) {
		dev_err(dev->dev, "Invalid custom data size - expected %u for %u buffers, got %u\n",
			expected_custom_data_size, cfg->n_bufs,
			cfg->custom_data_size);
		return -EINVAL;
	}

	bufs_ext = &post_ext->bufs_ext[0];

	/* Reset blend pipe state */
	for (dpy = 0; dpy < MAX_DISPLAYS; dpy++) {
		scaler_in_use[dpy] = false;
		pipe_state.current_pipe[dpy] = 0;
		pipe_state.current_pipe_layers[dpy] = 0;
	}

	/* NOTE: The current method of assigning pipes over multiple displays
	 * is unknown and needs experimentation/documentation to correct.
	 * The current assumption is that there are 2 blend sources (pipe 0
	 * and 1) on the internal display, and only 1 (pipe 0) on hdmi
	 */
	if (sunxi.interfaces[DISPLAY_HDMI].connected) {
		pipe_state.max_pipe[DISPLAY_HDMI] = 1;
		pipe_state.current_pipe[DISPLAY_HDMI] = 0;
		pipe_state.max_pipe[DISPLAY_INTERNAL] = NUM_BLENDER_PIPES;
		pipe_state.current_pipe[DISPLAY_INTERNAL] = 1;
	} else {
		pipe_state.max_pipe[DISPLAY_INTERNAL] = NUM_BLENDER_PIPES;
		pipe_state.current_pipe[DISPLAY_INTERNAL] = 0;
		pipe_state.max_pipe[DISPLAY_HDMI] = 0;
		pipe_state.current_pipe[DISPLAY_HDMI] = 0;
	}

	pipe_assignments =
		kzalloc(sizeof(*pipe_assignments), GFP_KERNEL);
	if (!pipe_assignments) {
		dev_err(dev->dev, "Failed to allocate pipe assignment state\n");
		err = -ENOMEM;
		goto err_free_assignments;
	}


	if (cfg->n_bufs > MAX_BUFFERS) {
		dev_err(dev->dev, "Trying to post %d buffers (max %d)\n",
			MAX_BUFFERS, NUM_OVERLAYS);
		err = -EINVAL;
		goto err_free_assignments;
	}

	for (i = 0; i < cfg->n_bufs; i++) {
		bool buffer_is_sane;
		struct adf_buffer *buf = &cfg->bufs[i];
		struct adf_buffer_config_ext *ebuf = &bufs_ext[i];

		dpy = get_buf_display(buf);
		if (dpy < 0) {
			dev_err(dev->dev, "Buffer %d has invalid assigned overlay\n",
				i);
			err = -EINVAL;
			goto err_free_assignments;
		}

		buffer_is_sane =
			adf_img_buffer_sanity_check(
				&sunxi.interfaces[dpy].interface,
				buf,
				ebuf);

		if (!buffer_is_sane) {
			dev_err(dev->dev, "Buffer %d failed sanity check\n",
				i);
			err = -EINVAL;
			goto err_free_assignments;
		}

		if (!is_supported_format(buf->format)) {
			/* This should be cleanly rejected when trying to assign
			 * an overlay engine
			 */
			dev_err(dev->dev, "Buffer %d has unrecognised format 0x%08x\n",
				i, buf->format);
			err = -EINVAL;
			goto err_free_assignments;
		}
		if (buffer_is_scaled(ebuf)) {
			/* The assumption is that there is a single scaled
			 * layer allowed per display, otherwise there may
			 * be a unbounded top end to the samples required per
			 * frame when testing validity a single layer at a time
			 */
			if (scaler_in_use[dpy]) {
				val_log(post_id, "Buffer %d is second scaled layer\n",
					i);
				err = -EINVAL;
				goto err_free_assignments;
			}
			scaler_in_use[dpy] = true;
			if (!sunxi.disp_ops.is_support_scaler_layer(dpy,
					ebuf->crop.x2 - ebuf->crop.x1,
					ebuf->crop.y2 - ebuf->crop.y1,
					ebuf->display.x2 - ebuf->display.x1,
					ebuf->display.y2 - ebuf->display.y1)) {
				val_log(post_id, "Buffer %d unsupported scaled layer\n",
					i);
				err = -EINVAL;
				goto err_free_assignments;
			}
		}
		if (ebuf->transform != ADF_BUFFER_TRANSFORM_NONE_EXT) {
			/* TODO: Sunxi transform support */
			val_log(post_id, "Transformed layers not supported at the minute\n");
			err = -EINVAL;
			goto err_free_assignments;
		}

		if (ebuf->blend_type != ADF_BUFFER_BLENDING_NONE_EXT &&
		    ebuf->plane_alpha != 255 &&
		    sunxi_format_has_alpha(buf->format)) {
			/* The sunxi display block appears to only support
			 * pixel /or/ global (plane) alpha, not both
			 */
			val_log(post_id, "Layer has both plane and pixel alpha\n");
			err = -EINVAL;
			goto err_free_assignments;
		}

		pipe = assign_pipe(&pipe_state, dpy,
			ebuf->blend_type != ADF_BUFFER_BLENDING_NONE_EXT,
			&ebuf->display);

		if (pipe < 0) {
			val_log(post_id, "Ran out of blend pipes\n");
			err = -EINVAL;
			goto err_free_assignments;
		}
		pipe_assignments->pipe[i] = pipe;
	}
	val_log(post_id, "Validate succeeded\n");

	*driver_state = pipe_assignments;

	return 0;
err_free_assignments:
	if (is_post)
		dev_err(dev->dev, "Failed validate for post\n");
	kfree(pipe_assignments);
	return err;
}

static void sunxi_retire_callback(void)
{
	atomic_inc(&sunxi.callbackcount);
	wake_up(&sunxi.post_wait_queue);
}

static bool sunxi_post_completed(u32 post_id)
{
	return (atomic_read(&sunxi.callbackcount) >= post_id);
}

static void adf_sunxi_post(struct adf_device *adf_dev, struct adf_post *cfg,
	void *driver_state)
{
	struct setup_dispc_data *disp_data;
	int err, buf;
	struct adf_post_ext *post_ext = cfg->custom_data;
	struct adf_buffer_config_ext *ext_config_data = NULL;
	int num_buffers[MAX_DISPLAYS];
	int dpy;
	struct pipe_assignments *pipe_assignments;
	u32 post_count, post_id;
	/* Allow a timeout of 4 frames before we force the frame off-screen */
	long timeout =
		msecs_to_jiffies((1000 / 60) * 4);

	if (cfg->n_bufs == 0) {
		val_log(0, "NULL flip\n");
		post_id = 0;
		post_ext = NULL;
	} else {
		BUG_ON(post_ext == NULL);
		post_id = post_ext->post_id;
		ext_config_data = &post_ext->bufs_ext[0];
		val_log(post_id, "Posting\n");
	}

	pipe_assignments = driver_state;
	if (!pipe_assignments && cfg->n_bufs != 0) {
		dev_err(adf_dev->dev, "Invalid driver state\n");
		return;
	}

	for (dpy = 0; dpy < MAX_DISPLAYS; dpy++)
		num_buffers[dpy] = 0;

	disp_data = kzalloc(sizeof(*disp_data), GFP_KERNEL);
	if (!disp_data) {
		dev_err(adf_dev->dev, "Failed to allocate post data");
		return;
	}

	for (buf = 0; buf < cfg->n_bufs; buf++) {

		dpy = get_buf_display(&cfg->bufs[buf]);
		if (dpy < 0) {
			dev_err(adf_dev->dev, "Invalid overlay %p assigned to layer %d",
				cfg->bufs[buf].overlay_engine, buf);
			goto err_free_data;
		}

		err = sunxi_buffer_to_layer_info(&cfg->bufs[buf],
			&ext_config_data[buf],
			&cfg->mappings[buf],
			&disp_data->layer_info[dpy][num_buffers[dpy]]);

		if (err) {
			dev_err(adf_dev->dev, "Failed to setup layer info (%d)\n",
				err);
			goto err_free_data;
		}
		disp_data->layer_info[dpy][num_buffers[dpy]].pipe =
			pipe_assignments->pipe[buf];
		disp_data->layer_info[dpy][num_buffers[dpy]].zorder = buf;
		num_buffers[dpy]++;
	}

	for (dpy = 0; dpy < MAX_DISPLAYS; dpy++) {
		sunxi_dbg(adf_dev->dev, "Dpy %u has %u layers\n", dpy,
			num_buffers[dpy]);
		disp_data->layer_num[dpy] = num_buffers[dpy];
	}

	disp_data->hConfigData = disp_data;

	sunxi.last_config_pos = (sunxi.last_config_pos + 1)
		% DEBUG_POST_DUMP_COUNT;

	sunxi.last_config[sunxi.last_config_pos] = *disp_data;
	sunxi.last_config_id[sunxi.last_config_pos] = post_id;

	err = sunxi.disp_ops.dispc_gralloc_queue(disp_data);
	if (err)
		dev_err(adf_dev->dev, "Failed to queue post (%d)\n", err);

	post_count = atomic_add_return(1, &sunxi.postcount);

	if (wait_event_timeout(sunxi.post_wait_queue,
		sunxi_post_completed(post_count-1), timeout) == 0) {
		dev_err(sunxi.dev, "Timeout waiting for post callback\n");

	}

err_free_data:
	kfree(disp_data);
	return;

}

static bool adf_sunxi_supports_event(struct adf_obj *obj,
	enum adf_event_type type)
{
	switch (obj->type) {
	case ADF_OBJ_INTERFACE: {
		struct adf_interface *intf =
			container_of(obj, struct adf_interface, base);
		struct sunxi_interface *sunxi_intf =
			container_of(intf, struct sunxi_interface, interface);
		switch (type) {
		case ADF_EVENT_VSYNC:
			return true;
		case ADF_EVENT_HOTPLUG:
			/* Only support hotplug on HDMI displays */
			return (sunxi_intf->disp_type == DISP_OUTPUT_TYPE_HDMI);
		default:
			return false;
		}
	}
	default:
		return false;
	}
	return false;
}

static struct
{
	u32 width, height, refresh;
	disp_tv_mode mode;
} hdmi_valid_modes[] = {
	/* List of modes in preference order */
	{ 1920,	1080,	60,	DISP_TV_MOD_1080P_60HZ},
	{ 1920,	1080,	50,	DISP_TV_MOD_1080P_50HZ},
	{ 1280,	720,	60,	DISP_TV_MOD_720P_60HZ},
	{ 1280,	720,	50,	DISP_TV_MOD_720P_50HZ},
	{ 1920,	1080,	25,	DISP_TV_MOD_1080P_25HZ},
	{ 1920,	1080,	30,	DISP_TV_MOD_1080P_30HZ},
	{ 640,  480,    30,     DISP_TV_MOD_480P},
};
#define NUM_HDMI_VALID_MODES \
	ARRAY_SIZE(hdmi_valid_modes)

static void setup_drm_mode(struct drm_mode_modeinfo *mode, int height,
	int width, int refresh)
{
	memset(mode, 0, sizeof(*mode));

	mode->vrefresh = refresh;
	mode->hdisplay = width;
	mode->vdisplay = height;

	adf_modeinfo_set_name(mode);
}

static void sunxi_disp_vsync_callback(void *user_data, u32 screen_id)
{
	adf_vsync_notify(&sunxi.interfaces[screen_id].interface, ktime_get());
}

static int sunxi_disp_hotplug_callback(void *user_data,
	disp_hotplug_state state)
{
	struct sunxi_interface *intf = user_data;
	int ret;
	int mode_count = 0;
	unsigned int idx;

	dev_dbg(sunxi.dev, "%s: called state = %u\n", __func__, state);

	/* Only HDMI displays can be hotplugged */
	BUG_ON(intf->disp_type != DISP_OUTPUT_TYPE_HDMI);

	kfree(intf->supported_modes);
	intf->supported_modes = NULL;
	intf->num_supported_modes = 0;
	switch (state) {
	default:
		dev_err(sunxi.dev, "%s: Invalid hotplug state\n", __func__);
		/* Fall-thru, treat as disconnect */
	case DISP_HOTPLUG_DISCONNECT:
		intf->connected = false;
		adf_hotplug_notify_disconnected(&intf->interface);
		dev_dbg(sunxi.dev, "%s: set disconnected\n", __func__);
		return 0;
	case DISP_HOTPLUG_CONNECT:
		intf->connected = true;
		break;
	}

	for (idx = 0; idx < NUM_HDMI_VALID_MODES; idx++) {
		ret = sunxi.disp_ops.hdmi_check_support_mode(intf->display_id,
			hdmi_valid_modes[idx].mode);
		if (ret == 1)
			mode_count++;
	}

	intf->num_supported_modes = mode_count;
	if (mode_count == 0) {
		dev_warn(sunxi.dev, "%s: No supported modes found for display id %d - forcing 720p\n",
			__func__, intf->display_id);
		intf->num_supported_modes = 1;
		intf->supported_modes = kzalloc(
			sizeof(*intf->supported_modes), GFP_KERNEL);
		if (!intf->supported_modes) {
			dev_err(sunxi.dev, "%s: Failed to allocate mode list\n",
				__func__);
			goto err_out;
		}
		/* Force the first mode in the supported list */
		setup_drm_mode(&intf->supported_modes[0],
			hdmi_valid_modes[0].height, hdmi_valid_modes[0].width,
			hdmi_valid_modes[0].refresh);
	} else {
		unsigned int supported_idx = 0;

		intf->num_supported_modes = mode_count;
		intf->supported_modes = kzalloc(
			mode_count * sizeof(*intf->supported_modes),
			GFP_KERNEL);
		if (!intf->supported_modes) {
			dev_err(sunxi.dev, "%s: Failed to allocate mode list\n",
				__func__);
			goto err_out;
		}
		for (idx = 0; idx < NUM_HDMI_VALID_MODES; idx++) {
			if (sunxi.disp_ops.hdmi_check_support_mode(
				intf->display_id,
				hdmi_valid_modes[idx].mode) != 1) {
				continue;
			}
			BUG_ON(supported_idx >= intf->num_supported_modes);
			setup_drm_mode(&intf->supported_modes[supported_idx],
				hdmi_valid_modes[idx].height,
				hdmi_valid_modes[idx].width,
				hdmi_valid_modes[idx].refresh);
			supported_idx++;
		}
		BUG_ON(supported_idx != intf->num_supported_modes);
	}
	adf_hotplug_notify_connected(&intf->interface, intf->supported_modes,
		intf->num_supported_modes);
	/* Default to first mode */
	ret = adf_interface_set_mode(&intf->interface,
		&intf->supported_modes[0]);
	if (ret) {
		dev_err(sunxi.dev, "%s: Failed hotplug modeset (%d)\n",
			__func__, ret);
		return ret;
	}
	dev_dbg(sunxi.dev, "%s: set connect\n", __func__);
	return 0;

err_out:
	intf->num_supported_modes = 0;
	kfree(intf->supported_modes);
	intf->supported_modes = NULL;
	return -1;
}

static void adf_sunxi_set_hotplug_state(struct sunxi_interface *intf,
	bool enabled)
{
	BUG_ON(intf->disp_type != DISP_OUTPUT_TYPE_HDMI);
	dev_dbg(sunxi.dev, "%s: hotplug set to %s\n", __func__,
		enabled ? "enabled" : "disabled");
	if (enabled) {
		sunxi.disp_ops.hotplug_enable(intf->display_id, true);
		sunxi.disp_ops.hotplug_callback(intf->display_id, intf,
			sunxi_disp_hotplug_callback);
		sunxi.disp_ops.hdmi_enable(intf->display_id);

	} else {
		sunxi.disp_ops.hdmi_disable(intf->display_id);
		sunxi.disp_ops.hotplug_enable(intf->display_id, false);
		sunxi.disp_ops.hotplug_callback(intf->display_id, NULL, NULL);
	}

}

static void adf_sunxi_set_event(struct adf_obj *obj, enum adf_event_type type,
	bool enabled)
{
	switch (obj->type) {
	case ADF_OBJ_INTERFACE: {
		struct adf_interface *intf =
			container_of(obj, struct adf_interface, base);
		struct sunxi_interface *sunxi_intf =
			container_of(intf, struct sunxi_interface, interface);
		switch (type) {
		case ADF_EVENT_VSYNC:
			sunxi.disp_ops.vsync_enable(sunxi_intf->display_id,
				enabled);
			break;
		case ADF_EVENT_HOTPLUG:
			adf_sunxi_set_hotplug_state(sunxi_intf, enabled);
			break;
		default:
			BUG();
		}
		break;
	}
	default:
		BUG();
	}
}


static disp_tv_mode
find_matching_disp_tv_mode_id(struct drm_mode_modeinfo *mode)
{
	unsigned int idx;

	for (idx = 0; idx < NUM_HDMI_VALID_MODES; idx++) {
		if (hdmi_valid_modes[idx].width == mode->hdisplay &&
		    hdmi_valid_modes[idx].height == mode->vdisplay &&
		    hdmi_valid_modes[idx].refresh == mode->vrefresh) {
			return hdmi_valid_modes[idx].mode;
		}
	}
	dev_err(sunxi.dev, "%s: No matching disp_tv_mode for %ux%u@%u\n",
		__func__, mode->hdisplay, mode->vdisplay, mode->vrefresh);
	return 0;
}

static int adf_sunxi_modeset(struct adf_interface *intf,
	struct drm_mode_modeinfo *mode)
{
	disp_tv_mode disp_mode;
	int err;
	struct sunxi_interface *sunxi_intf =
		container_of(intf, struct sunxi_interface, interface);

	dev_dbg(sunxi.dev, "%s: setting %d (type %d) to %ux%u@%u\n", __func__,
		sunxi_intf->display_id, sunxi_intf->disp_type, mode->hdisplay,
		mode->vdisplay, mode->vrefresh);

	if (sunxi_intf->disp_type != DISP_OUTPUT_TYPE_HDMI) {
		dev_dbg(sunxi.dev, "%s: Stub modeset for internal display\n",
			__func__);
		return 0;
	}

	disp_mode = find_matching_disp_tv_mode_id(mode);

	dev_dbg(sunxi.dev, "%s: HDMI modeset to mode %d\n", __func__,
		disp_mode);

	err = sunxi.disp_ops.hdmi_disable(sunxi_intf->display_id);
	if (err) {
		dev_err(sunxi.dev, "%s: Failed to disable display id %d for modeset\n",
			__func__, sunxi_intf->display_id);
		return -EFAULT;
	}

	err = sunxi.disp_ops.hdmi_set_mode(sunxi_intf->display_id, disp_mode);
	if (err) {
		dev_err(sunxi.dev, "%s: Failed to set mode %ux%u@%u (id %d) to display id %d\n",
			__func__, mode->hdisplay, mode->vdisplay,
			mode->vrefresh, disp_mode, sunxi_intf->display_id);
		return -EFAULT;
	}

	err = sunxi.disp_ops.hdmi_enable(sunxi_intf->display_id);
	if (err) {
		dev_err(sunxi.dev, "%s: Failed to enable display id %d after modeset\n",
			__func__, sunxi_intf->display_id);
		return -EFAULT;
	}
	return 0;
}

#ifdef SUPPORT_ADF_SUNXI_FBDEV

static int adf_sunxi_alloc_simple_buffer(struct adf_interface *intf, u16 w,
	u16 h, u32 format, struct dma_buf **dma_buf, u32 *offset, u32 *pitch)
{
	int err = 0;
	u32 bpp = sunxi_format_bpp(format);
	u32 size = h * w * bpp;
	struct ion_handle *hdl;
	struct adf_device *dev = intf->base.parent;

	if (bpp == 0) {
		dev_err(dev->dev, "%s: unknown format (0x%08x)\n",
			__func__, format);
		err = -EINVAL;
		goto err_out;
	}

	hdl = ion_alloc(sunxi.ion_client, size, 0,
		(1 << sunxi.ion_heap_id), 0);
	if (IS_ERR(hdl)) {
		err = PTR_ERR(hdl);
		dev_err(dev->dev, "%s: ion_alloc failed (%d)\n",
			__func__, err);
		goto err_out;
	}
	*dma_buf = ion_share_dma_buf(sunxi.ion_client, hdl);
	if (IS_ERR(*dma_buf)) {
		err = PTR_ERR(hdl);
		dev_err(dev->dev, "%s: ion_share_dma_buf failed (%d)\n",
			__func__, err);
		goto err_free_buffer;

	}
	*pitch = w * bpp;
	*offset = 0;
err_free_buffer:
	ion_free(sunxi.ion_client, hdl);
err_out:
	return err;
}

static int adf_sunxi_describe_simple_post(struct adf_interface *intf,
	struct adf_buffer *fb, void *data, size_t *size)
{
	*size = 0;
	return 0;
}

#endif /* SUPPORT_ADF_SUNXI_FBDEV */

static struct adf_device_ops adf_sunxi_device_ops = {
	.owner = THIS_MODULE,
	.base = {
		.open = adf_sunxi_open,
		.release = adf_sunxi_release,
		.ioctl = adf_img_ioctl,
	},
	.state_free = adf_sunxi_state_free,
	.validate = adf_sunxi_validate,
	.post = adf_sunxi_post,
};

static struct adf_interface_ops adf_sunxi_interface_ops = {
	.base = {
		.supports_event = adf_sunxi_supports_event,
		.set_event = adf_sunxi_set_event,
	},
	.modeset = adf_sunxi_modeset,
#ifdef SUPPORT_ADF_SUNXI_FBDEV
	.alloc_simple_buffer = adf_sunxi_alloc_simple_buffer,
	.describe_simple_post = adf_sunxi_describe_simple_post,
#endif
};

static struct adf_overlay_engine_ops adf_sunxi_overlay_ops = {
	.supported_formats = &sunxi_supported_formats[0],
	.n_supported_formats = NUM_SUPPORTED_FORMATS,
};

#ifdef SUPPORT_ADF_SUNXI_FBDEV

static struct fb_ops adf_sunxi_fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = adf_fbdev_open,
	.fb_release = adf_fbdev_release,
	.fb_check_var = adf_fbdev_check_var,
	.fb_set_par = adf_fbdev_set_par,
	.fb_blank = adf_fbdev_blank,
	.fb_pan_display = adf_fbdev_pan_display,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
	.fb_mmap = adf_fbdev_mmap,
};
#endif



static void sunxi_debugfs_print_window(struct seq_file *s, const char *prefix,
	const disp_window *win)
{
	if (win->x)
		seq_printf(s, "%sx\t=\t%u\n", prefix, win->x);
	if (win->y)
		seq_printf(s, "%sy\t=\t%u\n", prefix, win->y);
	seq_printf(s, "%sw\t=\t%u\n", prefix, win->width);
	seq_printf(s, "%sh\t=\t%u\n", prefix, win->height);

}

static void sunxi_debugfs_print_fb_info(struct seq_file *s, const char *prefix,
	const disp_fb_info *fb)
{
	int i;

	for (i = 0; i < 3; i++)
		if (fb->addr[i])
			seq_printf(s, "%saddr[%d]\t=\t0x%08x\n", prefix, i,
				fb->addr[i]);
	seq_printf(s, "%ssize.w\t=\t%u\n", prefix, fb->size.width);
	seq_printf(s, "%ssize.h\t=\t%u\n", prefix, fb->size.height);
	seq_printf(s, "%sformat\t=\t0x%x\n", prefix, fb->format);
	if (fb->cs_mode)
		seq_printf(s, "%scs_mode\t=\t0x%x\n", prefix, fb->cs_mode);
	if (fb->b_trd_src)
		seq_printf(s, "%sb_trd_src\t=\t0x%x\n", prefix, fb->b_trd_src);
	if (fb->trd_mode)
		seq_printf(s, "%strd_mode\t=\t0x%x\n", prefix, fb->trd_mode);
	for (i = 0; i < 3; i++)
		if (fb->trd_right_addr[i])
			seq_printf(s, "%strd_right_addr[%d]\t=\t0x%x\n", prefix,
				i, fb->trd_right_addr[i]);
	/* Default alpha mode is pre-multiply, so interesting values would
	 * not equal 0x1
	 */
	if (fb->pre_multiply != 0x1)
		seq_printf(s, "%spre_multiply\t=\t0x%x\n", prefix,
			fb->pre_multiply);

}

static void sunxi_debugfs_print_layer_info(struct seq_file *s, int layer_num,
	disp_layer_info *layer_info)
{
	int i;

	for (i = 0; i < layer_num; i++) {
		disp_layer_info *layer = &layer_info[i];

		seq_printf(s, "\tlayer[%d] = {\n", i);
		if (layer->mode)
			seq_printf(s, "\t\tmode\t=\t0x%x\n", layer->mode);
		seq_printf(s, "\t\tpipe\t=\t0x%x\n", layer->pipe);
		seq_printf(s, "\t\tzorder\t=\t0x%x\n", layer->zorder);
		if (layer->alpha_mode)
			seq_printf(s, "\t\talpha_mode\t=\t0x%x\n",
				layer->alpha_mode);
		/* The default alpha is 0xff, so interesting values would be
		 * when it does not equal 0xff
		 */
		if (layer->alpha_value != 0xff)
			seq_printf(s, "\t\talpha_value\t=\t0x%x\n",
				layer->alpha_value);
		if (layer->ck_enable)
			seq_printf(s, "\t\tck_enable\t=\t0x%x\n",
				layer->ck_enable);
		sunxi_debugfs_print_window(s, "\t\tscreen_win.",
			&layer->screen_win);
		sunxi_debugfs_print_fb_info(s, "\t\tfb.", &layer->fb);
		if (layer->b_trd_out)
			seq_printf(s, "\t\tb_trd_out\t=\t0x%x\n",
				layer->b_trd_out);
		if (layer->out_trd_mode)
			seq_printf(s, "\t\tout_trd_mode\t=\t0x%x\n",
			layer->out_trd_mode);
		seq_printf(s, "\t\tid\t=\t%u }\n", layer->id);
	}
}

static void sunxi_debugfs_print_config(struct seq_file *s, u32 post_id,
	struct setup_dispc_data *config)
{
	int dpy;

	seq_printf(s, "adf_sunxi post_id %u = {\n", post_id);
	for (dpy = 0; dpy < 3; dpy++) {
		seq_printf(s, "\tlayer_num[%d] = %u\n", dpy,
			config->layer_num[dpy]);
		sunxi_debugfs_print_layer_info(s,
			config->layer_num[dpy],
			&config->layer_info[dpy][0]);
	}
	seq_puts(s, "}\n");
}

static int sunxi_debugfs_show(struct seq_file *s, void *unused)
{
	/* FIXME: Should properly lock to reduce the risk of modification
	 * while printing?
	 */
	int post;

	for (post = 0; post < DEBUG_POST_DUMP_COUNT; post++) {
		/* Start at current buffer position +1 (oldest post in the
		 * log)
		 */
		int pos = (sunxi.last_config_pos + post + 1)
			% DEBUG_POST_DUMP_COUNT;
		sunxi_debugfs_print_config(s, sunxi.last_config_id[pos],
			&sunxi.last_config[pos]);
	}
	return 0;
}

static int sunxi_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, sunxi_debugfs_show, inode->i_private);
}

static const struct file_operations adf_sunxi_debugfs_fops = {
	.open = sunxi_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int sunxi_debugfs_val_show(struct seq_file *s, void *unused)
{
	int line;

	for (line = 0; line < VALIDATE_LOG_LINES; line++) {
		int pos = (sunxi.validate_log_position + line + 1)
			% VALIDATE_LOG_LINES;
		seq_puts(s, sunxi.validate_log[pos]);
	}
	return 0;
}

static int sunxi_debugfs_val_open(struct inode *inode, struct file *file)
{
	return single_open(file, sunxi_debugfs_val_show, inode->i_private);
}

static const struct file_operations adf_sunxi_debugfs_val_fops = {
	.open = sunxi_debugfs_val_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int adf_init_lcd_interface(struct sunxi_interface *interface)
{
	int height, width;
	int refresh = 60;
	int err;

	interface->connected = true;
	interface->name = "LCD";
	interface->adf_type = ADF_INTF_DSI;
	err = adf_interface_init(&interface->interface, &sunxi.device,
		interface->adf_type, interface->display_id,
		ADF_INTF_FLAG_PRIMARY, &adf_sunxi_interface_ops,
		interface->name);
	if (err) {
		dev_err(sunxi.dev, "%s: Failed to init adf interface %d (%d)\n",
			__func__, interface->display_id, err);
		goto err_out;
	}
	height = sunxi.disp_ops.get_screen_height(interface->display_id);
	if (height < 0) {
		dev_err(sunxi.dev, "%s: Failed to query display height (%d)\n",
			__func__, height);
		err = -EFAULT;
		goto err_out;
	}
	width = sunxi.disp_ops.get_screen_width(interface->display_id);
	if (width < 0) {
		dev_err(sunxi.dev, "%s: Failed to query display width (%d)\n",
			__func__, width);
		err = -EFAULT;
		goto err_out;
	}

	interface->supported_modes = kzalloc(sizeof(*interface->supported_modes),
		GFP_KERNEL);
	if (!interface->supported_modes) {
		dev_err(sunxi.dev, "%s: Failed to allocate mode struct\n",
			__func__);
		err = -ENOMEM;
		goto err_out;
	}
	interface->num_supported_modes = 1;
	setup_drm_mode(&interface->supported_modes[0], height, width, refresh);

	err = adf_hotplug_notify_connected(&interface->interface,
		interface->supported_modes, interface->num_supported_modes);
	if (err) {
		dev_err(sunxi.dev, "%s: Failed to notify connected (%d)\n",
			__func__, err);
		goto err_out;
	}
	/* We need to set initial mode */
	err = adf_interface_set_mode(&interface->interface,
		&interface->supported_modes[0]);
	if (err) {
		dev_err(sunxi.dev, "%s: Failed initial modeset (%d)\n",
			__func__, err);
		goto err_out;
	}
	err = sunxi.disp_ops.vsync_callback(NULL, sunxi_disp_vsync_callback);
	if (err) {
		dev_err(sunxi.dev, "%s: Failed to set vsync callback (%d)\n",
			__func__, err);
		goto err_out;
	}
	err = 0;
err_out:
	return err;
}

static int adf_init_hdmi_interface(struct sunxi_interface *interface)
{
	disp_hotplug_state hotplug_state;
	int err;

	interface->name = "HDMI";
	interface->adf_type = ADF_INTF_HDMI;
	hotplug_state = sunxi.disp_ops.hotplug_state(interface->display_id);

	err = adf_interface_init(&interface->interface, &sunxi.device,
		interface->adf_type, interface->display_id,
		ADF_INTF_FLAG_EXTERNAL, &adf_sunxi_interface_ops,
		interface->name);
	if (err) {
		dev_err(sunxi.dev, "%s: Failed to init adf interface %d (%d)\n",
			__func__, interface->display_id, err);
		goto err_out;
	}

	switch (hotplug_state) {
	case DISP_HOTPLUG_CONNECT:
		interface->connected = true;
		break;
	default:
		dev_err(sunxi.dev, "%s: Error querying hotplug state for display id %d\n",
			__func__, interface->display_id);
		hotplug_state = DISP_HOTPLUG_DISCONNECT;
		/* Fall-thru, act as if disconnected*/
	case DISP_HOTPLUG_DISCONNECT:
		interface->connected = false;
		break;
	}
	/* Call the hotplug function to setup modes */
	sunxi_disp_hotplug_callback(interface, hotplug_state);

	err = 0;
err_out:
	return err;
}


static void adf_init_interface(struct sunxi_interface *interface, int id)
{
	BUG_ON(!interface);
	memset(interface, 0, sizeof(*interface));
	interface->disp_type = sunxi.disp_ops.get_output_type(id);
	interface->display_id = id;
	dev_dbg(sunxi.dev, "%s: interface %d\n", __func__, id);

	switch (interface->disp_type) {
	default:
		dev_err(sunxi.dev, "%s: Unsupported interface type %d for display %d\n",
			__func__, interface->disp_type, id);
		interface->disp_type = DISP_OUTPUT_TYPE_NONE;
		/* Fall-thru */
	case DISP_OUTPUT_TYPE_NONE:
		dev_dbg(sunxi.dev, "%s: Skipping interface %d - type %d\n",
			__func__, id, interface->disp_type);
		interface->connected = false;
		return;
	case DISP_OUTPUT_TYPE_LCD:
		adf_init_lcd_interface(interface);
		break;
	case DISP_OUTPUT_TYPE_HDMI:
		adf_init_hdmi_interface(interface);
		break;
	}
}

static int adf_sunxi_probe(struct platform_device *pdev)
{
	int err = 0;
	int dpy = 0;
	int ovl = 0;

	memset(&sunxi.last_config, 0, sizeof(sunxi.last_config));

	atomic_set(&sunxi.postcount, 0);
	atomic_set(&sunxi.callbackcount, 0);
	init_waitqueue_head(&sunxi.post_wait_queue);

	sunxi.dev = &pdev->dev;

	err = adf_device_init(&sunxi.device, sunxi.dev,
		&adf_sunxi_device_ops, "sunxi_device");
	if (err) {
		dev_err(sunxi.dev, "Failed to init ADF device (%d)\n",
			err);
		goto err_out;
	}

	err = disp_get_composer_ops(&sunxi.disp_ops);
	if (err) {
		dev_err(sunxi.dev, "Failed to get composer ops (%d)\n",
			err);
		goto err_free_overlays;
	}
	/* Set the retire callback */
	err = sunxi.disp_ops.set_retire_callback(sunxi_retire_callback);
	if (err) {
		dev_err(sunxi.dev, "Failed to set retire callback (%d)\n",
			err);
		goto err_free_overlays;
	}
	/* The HDMI must be enabled to receive hotplug events, which in turn
	 * must already must have a valid mode set
	 */
	err = sunxi.disp_ops.hdmi_set_mode(1, DISP_TV_MOD_720P_60HZ);
	if (err) {
		dev_warn(sunxi.dev, "Failed to enable initial hdmi mode on dpy 1 (%d)\n",
			err);
		/* Not fatal */
	}
	dev_dbg(sunxi.dev, "%s: %d hdmi_enable\n", __func__, __LINE__);
	err = sunxi.disp_ops.hdmi_enable(1);
	if (err) {
		dev_warn(sunxi.dev, "Failed to enable hdmi on dpy 1 (%d)\n",
			err);
		/* Not fatal */
	}

	for (dpy = 0; dpy < MAX_DISPLAYS; dpy++)
		adf_init_interface(&sunxi.interfaces[dpy], dpy);

	for (dpy = 0; dpy < MAX_DISPLAYS; dpy++) {
		if (sunxi.interfaces[dpy].disp_type == DISP_OUTPUT_TYPE_NONE)
			continue;
		for (ovl = 0; ovl < NUM_OVERLAYS; ovl++) {
			err = adf_overlay_engine_init(
				&sunxi.overlays[dpy][ovl].overlay,
				&sunxi.device, &adf_sunxi_overlay_ops,
				"sunxi_overlay_%d-%d", dpy, ovl);
			if (err) {
				dev_err(sunxi.dev, "Failed to init overlay %d-%d (%d)\n",
					dpy, ovl, err);
				goto err_free_overlays;
			}
			err = adf_attachment_allow(&sunxi.device,
				&sunxi.overlays[dpy][ovl].overlay,
				&sunxi.interfaces[dpy].interface);

			if (err) {
				dev_err(sunxi.dev, "Failed to attach overlay %d-%d (%d)\n",
					dpy, ovl, err);
				goto err_free_overlays;
			}
		}
	}


	sunxi.ion_heap_id = ION_HEAP_TYPE_CARVEOUT;

	sunxi.ion_client = ion_client_create(idev, "adf_sunxi");

	if (IS_ERR(sunxi.ion_client)) {
		err = PTR_ERR(sunxi.ion_client);
		dev_err(sunxi.dev, "Failed to create ion client (%d)\n",
			err);
		goto err_free_overlays;
	}

#ifdef SUPPORT_ADF_SUNXI_FBDEV
	err = adf_fbdev_init(&sunxi.fbdev,
		&sunxi.interfaces[DISPLAY_INTERNAL].interface,
		&sunxi.overlays[DISPLAY_INTERNAL].overlay,
		sunxi.interfaces[DISPLAY_INTERNAL].width,
		sunxi.interfaces[DISPLAY_INTERNAL].height,
		DRM_FORMAT_BGRA8888,
		&adf_sunxi_fb_ops,
		"adf_sunxi_fb");
	if (err) {
		dev_err(sunxi.dev, "Failed to init ADF fbdev (%d)\n", err);
		goto err_free_ion_client;
	}
#endif

	sunxi.debugfs_config_file = debugfs_create_file("adf_debug", S_IRUGO,
		NULL, NULL, &adf_sunxi_debugfs_fops);

	sunxi.debugfs_val_log = debugfs_create_file("adf_val_log", S_IRUGO,
		NULL, NULL, &adf_sunxi_debugfs_val_fops);
	dev_err(sunxi.dev, "Successfully loaded adf_sunxi\n");

	return 0;
#ifdef SUPPORT_ADF_SUNXI_FBDEV
err_free_ion_client:
#endif
	ion_client_destroy(sunxi.ion_client);
err_free_overlays:
	for (; dpy > 0; dpy--) {
		if (sunxi.interfaces[dpy-1].disp_type == DISP_OUTPUT_TYPE_NONE)
			continue;
		for (; ovl > 0; ovl--) {
			adf_overlay_engine_destroy(
				&sunxi.overlays[dpy-1][ovl-1].overlay);
		}
	}
	dpy = MAX_DISPLAYS;
	for (; dpy > 0; dpy--) {
		if (sunxi.interfaces[dpy-1].disp_type == DISP_OUTPUT_TYPE_NONE)
			continue;
		if (sunxi.interfaces[dpy-1].disp_type == DISP_OUTPUT_TYPE_HDMI)
			adf_sunxi_set_hotplug_state(&sunxi.interfaces[dpy],
				false);
		adf_interface_destroy(&sunxi.interfaces[dpy-1].interface);
	}
	adf_device_destroy(&sunxi.device);
err_out:
	debugfs_remove(sunxi.debugfs_config_file);
	sunxi.debugfs_config_file = NULL;
	debugfs_remove(sunxi.debugfs_val_log);
	sunxi.debugfs_val_log = NULL;
	return err;
}

static int adf_sunxi_remove(struct platform_device *pdev)
{
	int dpy;
	int ovl;
#ifdef SUPPORT_ADF_SUNXI_FBDEV
	adf_fbdev_destroy(&sunxi.fbdev);
#endif
	debugfs_remove(sunxi.debugfs_config_file);
	sunxi.debugfs_config_file = NULL;
	debugfs_remove(sunxi.debugfs_val_log);
	sunxi.debugfs_val_log = NULL;
	ion_client_destroy(sunxi.ion_client);
	for (dpy = 0; dpy < MAX_DISPLAYS; dpy++) {
		if (sunxi.interfaces[dpy].disp_type == DISP_OUTPUT_TYPE_NONE)
			continue;
		for (ovl = 0; ovl < NUM_OVERLAYS; ovl++)
			adf_overlay_engine_destroy(
				&sunxi.overlays[dpy][ovl].overlay);
	}
	for (dpy = 0; dpy < MAX_DISPLAYS; dpy++) {
		if (sunxi.interfaces[dpy].disp_type == DISP_OUTPUT_TYPE_NONE)
			continue;
		if (sunxi.interfaces[dpy].disp_type == DISP_OUTPUT_TYPE_HDMI)
			adf_sunxi_set_hotplug_state(&sunxi.interfaces[dpy],
				false);
		adf_interface_destroy(&sunxi.interfaces[dpy].interface);
	}
	adf_device_destroy(&sunxi.device);
	return 0;
}

static void adf_sunxi_device_release(struct device *dev)
{
	/* NOOP */
}

static int adf_sunxi_device_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	return 0;
}
static int adf_sunxi_device_resume(struct platform_device *pdev)
{
	return 0;
}

struct platform_device adf_sunxi_platform_device = {
	.name = "adf_sunxi",
	.id = -1,
	.dev.release = adf_sunxi_device_release,
};

struct platform_driver adf_sunxi_platform_driver = {
	.driver.name = "adf_sunxi",
	.probe = adf_sunxi_probe,
	.remove = adf_sunxi_remove,
	.suspend = adf_sunxi_device_suspend,
	.resume = adf_sunxi_device_resume,
};

static int __init adf_sunxi_init(void)
{
	platform_device_register(&adf_sunxi_platform_device);
	platform_driver_register(&adf_sunxi_platform_driver);
	return 0;
}

static void __exit adf_sunxi_exit(void)
{
	platform_device_unregister(&adf_sunxi_platform_device);
	platform_driver_unregister(&adf_sunxi_platform_driver);
}

module_init(adf_sunxi_init);
module_exit(adf_sunxi_exit);
