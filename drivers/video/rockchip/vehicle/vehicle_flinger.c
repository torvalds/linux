// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/video/rockchip/video/vehicle_flinger.c
 *
 * Copyright (C) 2022 Rockchip Electronics Co., Ltd.
 * Authors:
 *	Jianwei Fan <jianwei.fan@rock-chips.com>
 *
 */

#include <linux/atomic.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kthread.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <asm/div64.h>
#include <linux/uaccess.h>
#include <linux/linux_logo.h>
#include <linux/dma-mapping.h>
#include <linux/regulator/consumer.h>
#include <linux/of_address.h>
#include <linux/memblock.h>
#include <linux/kthread.h>
#include <linux/fdtable.h>
#include <linux/miscdevice.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_gpio.h>
#include <video/of_display_timing.h>
#include <video/display_timing.h>
#endif

#include "vehicle_flinger.h"
#include "../../../gpu/drm/rockchip/rockchip_drm_direct_show.h"
#include "../drivers/video/rockchip/rga3/include/rga_drv.h"

static int vehicle_dump_cif;
static int vehicle_dump_rga;
static int vehicle_dump_vop;

enum force_value {
	FORCE_WIDTH = 1920,
	FORCE_HEIGHT = 1080,
	FORCE_STRIDE = 1920,
	FORCE_XOFFSET = 0,
	FORCE_YOFFSET = 0,
	FORCE_FORMAT = HAL_PIXEL_FORMAT_YCrCb_NV12,
	FORCE_ROTATION = RGA_TRANSFORM_ROT_0,
};

enum {
	NUM_SOURCE_BUFFERS = 5, /*5 src buffer for cif*/
	NUM_TARGET_BUFFERS = 3, /*3 dst buffer rga*/
};

enum buffer_state {
	UNKNOWN = 0,
	FREE,
	DEQUEUE,
	QUEUE,
	ACQUIRE,
	DISPLAY,
};

struct rect {
	size_t x;
	size_t y;
	size_t w;
	size_t h;
	size_t s;
	size_t f;
};

struct graphic_buffer {
	struct list_head list;
	uint32_t handle;
	struct rockchip_drm_direct_show_buffer *drm_buffer;
	int fd;
	struct sync_fence *rel_fence;
	struct rect src;
	struct rect dst;
	enum buffer_state state;
	unsigned long phy_addr;
	void *vir_addr;
	int rotation;
	int offset;
	int len;
	int width;
	int height;
	int stride;
	int format;
	struct work_struct render_work;
	ktime_t timestamp;
};

struct queue_buffer {
	struct list_head list;
	struct graphic_buffer *buffer;
};

struct flinger {
	struct device *dev;
	struct ion_client *ion_client;
	struct work_struct init_work;
	struct work_struct render_work;
	struct workqueue_struct *render_workqueue;
	struct mutex source_buffer_lock;/*src buffer lock*/
	struct mutex target_buffer_lock;/*dst buffer lock*/
	struct graphic_buffer source_buffer[NUM_SOURCE_BUFFERS];
	struct graphic_buffer target_buffer[NUM_TARGET_BUFFERS];
	struct mutex queue_buffer_lock;
	struct list_head queue_buffer_list;
	wait_queue_head_t worker_wait;
	atomic_t worker_cond_atomic;
	atomic_t worker_running_atomic;
	int source_index;
	int target_index;
	struct vehicle_cfg v_cfg;
	int cvbs_field_count;
	struct graphic_buffer *last_src_buffer;
	/*debug*/
	int debug_cif_count;
	int debug_vop_count;
	bool running;
	struct drm_device *drm_dev;
	struct drm_crtc *crtc;
	struct drm_plane *plane;
	const char *crtc_name;
	const char *plane_name;
};

static struct flinger *flinger;

static int rk_flinger_queue_work(struct flinger *flinger,
				 struct graphic_buffer *src_buffer);

static int rk_flinger_alloc_bpp(int format)
{
	int width = 4;

	switch (format) {
	case HAL_PIXEL_FORMAT_RGB_565:
		width = 2;
		break;
	case HAL_PIXEL_FORMAT_RGB_888:
		width =  3;
		break;
	case HAL_PIXEL_FORMAT_RGBA_8888:
		width =  4;
		break;
	case HAL_PIXEL_FORMAT_RGBX_8888:
		width =  4;
		break;
	case HAL_PIXEL_FORMAT_BGRA_8888:
		width =  4;
		break;
	case HAL_PIXEL_FORMAT_YCrCb_NV12:
		width =  2;
		break;
	default:
		VEHICLE_INFO("%s: unsupported format: 0x%x\n", __func__, format);
		break;
	}

	return width;
}

static int rk_flinger_HAL_format_to_DRM(int format)
{
	int drm_format = 0;

	switch (format) {
	case HAL_PIXEL_FORMAT_RGBX_8888:
		drm_format =  DRM_FORMAT_XRGB8888;
		break;
	case HAL_PIXEL_FORMAT_YCrCb_NV12:
		drm_format =  DRM_FORMAT_NV12;
		break;
	case HAL_PIXEL_FORMAT_RGB_888:
		drm_format =  DRM_FORMAT_RGB888;
		break;
	case HAL_PIXEL_FORMAT_RGB_565:
		drm_format =  DRM_FORMAT_RGB565;
		break;
	default:
		VEHICLE_INFO("%s: unsupported format: 0x%x\n", __func__, format);
		break;
	}

	return drm_format;
}

static int rk_flinger_alloc_buffer(struct flinger *flg,
				   struct graphic_buffer *buffer,
				   int w, int h,
				   int s, int f)
{
	unsigned long phy_addr;
	size_t len;
	int bpp;
	int ret = 0;
	struct rockchip_drm_direct_show_buffer *create_buffer;

	VEHICLE_DG("------------alloc buffer start---------\n");
	if (!flg)
		return -ENODEV;

	if (!buffer)
		return -EINVAL;

	bpp = rk_flinger_alloc_bpp(f);
	len = s * h * bpp;

	create_buffer = kmalloc(sizeof(struct rockchip_drm_direct_show_buffer), GFP_KERNEL);
	if (!create_buffer)
		return -ENOMEM;
	create_buffer->width = w;
	create_buffer->height = h;
	create_buffer->pixel_format = rk_flinger_HAL_format_to_DRM(f);
	create_buffer->flag = ROCKCHIP_BO_CONTIG;

	ret = rockchip_drm_direct_show_alloc_buffer(flg->drm_dev, create_buffer);
	if (ret)
		VEHICLE_DGERR("error: failed to alloc drm buffer\n");

	VEHICLE_DG("-----creat buffer over-----\n");
	buffer->vir_addr = create_buffer->vir_addr[0];
	buffer->handle = create_buffer->dmabuf_fd;
	phy_addr = create_buffer->phy_addr[0];
	buffer->fd = create_buffer->dmabuf_fd;
	buffer->drm_buffer = create_buffer;

	buffer->rel_fence = NULL;
	buffer->phy_addr = phy_addr;
	buffer->rotation = 0;
	buffer->width = w;
	buffer->height = h;
	buffer->stride = s;
	buffer->format = f;
	buffer->len = len;

	return ret;
}

static int rk_flinger_free_buffer(struct flinger *flinger,
			   struct graphic_buffer *buffer)
{
	if (!flinger)
		return -ENODEV;

	if (!buffer)
		return -EINVAL;

	if (buffer->drm_buffer)
		rockchip_drm_direct_show_free_buffer(flinger->drm_dev,
							buffer->drm_buffer);

	return 0;
}

static int rk_flinger_create_worker(struct flinger *flinger)
{
	struct workqueue_struct *wq = NULL;

	wq = create_singlethread_workqueue("flinger-render");
	if (!wq) {
		VEHICLE_DGERR("wzqtest Failed to create flinger workqueue\n");
		return -ENODEV;
	}
	flinger->render_workqueue = wq;

	return 0;
}

static int rk_flinger_destroy_worker(struct flinger *flinger)
{
	if (!flinger)
		return -ENODEV;

	if (flinger->render_workqueue)
		destroy_workqueue(flinger->render_workqueue);

	return 0;
}

static int vehicle_flinger_parse_dt(struct flinger *flinger)
{
	struct device *dev = flinger->dev;

	if (of_property_read_string(dev->of_node, "vehicle,crtc_name", &flinger->crtc_name)) {
		dev_info(dev, "%s: get crtc_name failed, use default!\n", __func__);
		flinger->crtc_name = "video_port3";
	} else {
		dev_info(dev, "%s: get crtc name from dts, crtc-name = %s\n",
							__func__, flinger->crtc_name);
	}

	if (of_property_read_string(dev->of_node, "vehicle,plane_name", &flinger->plane_name)) {
		dev_info(dev, "%s: get crtc_name failed, use default!\n", __func__);
		flinger->plane_name = "Esmart3-win0";
	} else {
		dev_info(dev, "%s: get crtc name from dts, crtc-name = %s\n",
							__func__, flinger->plane_name);
	}

	return 0;
}

int vehicle_flinger_init(struct device *dev, struct vehicle_cfg *v_cfg)
{
	struct graphic_buffer *buffer;
	struct flinger *flg = NULL;
	int i, ret, w, h, s, f;
	static bool inited;

	if (inited)
		return 0;

	// if (FORCE_ROTATION == RGA_TRANSFORM_ROT_270 || FORCE_ROTATION == RGA_TRANSFORM_ROT_90) {
	if (v_cfg->rotate_mirror == 0x01  || v_cfg->rotate_mirror == 0x04) {
		w = FORCE_WIDTH;
		h = ALIGN(FORCE_HEIGHT, 64);
		s = ALIGN(FORCE_HEIGHT, 64);
		f = FORCE_FORMAT;
	} else {
		w = ALIGN(FORCE_WIDTH, 64);
		h = FORCE_HEIGHT;
		s = ALIGN(FORCE_STRIDE, 64);
		f = FORCE_FORMAT;
	}

	flg = kzalloc(sizeof(*flg), GFP_KERNEL);
	if (!flg) {
		VEHICLE_DGERR("flinger is NULL\n");
		return -ENOMEM;
	}

	if (!flg->drm_dev)
		flg->drm_dev = rockchip_drm_get_dev();
	if (!flg->drm_dev) {
		VEHICLE_DGERR("------drm device is not ready!!!-----\n");
		kfree(flg);
		return -ENODEV;
	}

	mutex_init(&flg->queue_buffer_lock);
	mutex_init(&flg->source_buffer_lock);
	mutex_init(&flg->target_buffer_lock);
	INIT_LIST_HEAD(&flg->queue_buffer_list);
	init_waitqueue_head(&flg->worker_wait);
	atomic_set(&flg->worker_cond_atomic, 0);
	atomic_set(&flg->worker_running_atomic, 1);

	for (i = 0; i < NUM_SOURCE_BUFFERS; i++) {
		flg->source_buffer[i].handle = 0;
		flg->source_buffer[i].phy_addr = 0;
		flg->source_buffer[i].fd = -1;
	}
	for (i = 0; i < NUM_TARGET_BUFFERS; i++) {
		flg->target_buffer[i].phy_addr = 0;
		flg->target_buffer[i].handle = 0;
		flg->target_buffer[i].fd = -1;
	}

	for (i = 0; i < NUM_SOURCE_BUFFERS; i++) {
		buffer = &(flg->source_buffer[i]);
		ret = rk_flinger_alloc_buffer(flg, buffer, w, h, s, f);
		if (ret) {
			VEHICLE_DGERR("rk_flinger alloc src buffer failed(%d)\n",
					ret);
			goto free_dst_alloc;
		}
		buffer->state = FREE;
	}
	for (i = 0; i < NUM_TARGET_BUFFERS; i++) {
		buffer = &(flg->target_buffer[i]);
		// f = HAL_PIXEL_FORMAT_RGBX_8888;
		// if (FORCE_ROTATION == RGA_TRANSFORM_ROT_270 ||
		//	FORCE_ROTATION == RGA_TRANSFORM_ROT_90)
		if (v_cfg->rotate_mirror == 0x01  || v_cfg->rotate_mirror == 0x04)
			ret = rk_flinger_alloc_buffer(flg, buffer, h, w, s, f);
		else
			ret = rk_flinger_alloc_buffer(flg, buffer, w, h, s, f);
		// ret = rk_flinger_alloc_buffer(flg, buffer, w, h, s, f);
		if (ret) {
			VEHICLE_DGERR("rk_flinger alloc dst buffer failed\n");
			goto free_src_alloc;
		}
		buffer->state = FREE;
	}

	ret = rk_flinger_create_worker(flg);
	if (ret) {
		VEHICLE_DGERR("rk_flinger create worker failed\n");
		goto free_dst_alloc;
	}
	flinger = flg;

	memcpy(&flg->v_cfg, v_cfg, sizeof(struct vehicle_cfg));
	rk_flinger_queue_work(flg, NULL);
	flg->dev = dev;

	ret = vehicle_flinger_parse_dt(flg);
	if (ret) {
		VEHICLE_DGERR("vehicle flinger parse dts failed\n");
		goto free_dst_alloc;
	}

	VEHICLE_INFO("vehicle flinger init ok\n");
	inited = true;

	return 0;
free_dst_alloc:
	for (i = 0; i < NUM_TARGET_BUFFERS; i++)
		rk_flinger_free_buffer(flg, &(flg->target_buffer[i]));

free_src_alloc:
	for (i = 0; i < NUM_SOURCE_BUFFERS; i++)
		rk_flinger_free_buffer(flg, &(flg->source_buffer[i]));

	return -EINVAL;
}
__maybe_unused int vehicle_flinger_deinit(void)
{
	struct flinger *flg = flinger;
	int i;

	if (!flg)
		return -ENODEV;

	atomic_set(&flg->worker_running_atomic, 0);
	atomic_inc(&flg->worker_cond_atomic);
	wake_up(&flg->worker_wait);
	flush_work(&flg->render_work);
	flush_workqueue(flg->render_workqueue);
	rk_flinger_destroy_worker(flg);

	flinger = NULL;
	for (i = 0; i < NUM_SOURCE_BUFFERS; i++)
		rk_flinger_free_buffer(flg, &flg->source_buffer[i]);

	for (i = 0; i < NUM_TARGET_BUFFERS; i++)
		rk_flinger_free_buffer(flg, &flg->target_buffer[i]);

	kfree(flg);

	return 0;
}

static int rk_flinger_format_hal_to_rga(int format)
{
	int rga_format = -1;

	switch (format) {
	case HAL_PIXEL_FORMAT_RGB_565:
		rga_format =  RGA_FORMAT_RGB_565;
		break;
	case HAL_PIXEL_FORMAT_RGB_888:
		rga_format =  RGA_FORMAT_RGB_888;
		break;
	case HAL_PIXEL_FORMAT_RGBA_8888:
		rga_format =  RGA_FORMAT_RGBA_8888;
		break;
	case HAL_PIXEL_FORMAT_RGBX_8888:
		rga_format =  RGA_FORMAT_RGBX_8888;
		break;
	case HAL_PIXEL_FORMAT_BGRA_8888:
		rga_format =  RGA_FORMAT_BGRA_8888;
		break;
	case HAL_PIXEL_FORMAT_YCrCb_NV12:
		rga_format =  RGA_FORMAT_YCrCb_420_SP;
		break;
	case HAL_PIXEL_FORMAT_YCbCr_422_SP:
		rga_format =  RGA_FORMAT_YCbCr_422_SP;
		break;
	default:
		break;
	}

	return rga_format;
}

static int rk_flinger_set_rect(struct rect *rect, int x, size_t y,
			       int w, int h, int s, int f)
{
	if (!rect)
		return -EINVAL;

	rect->x = x;
	rect->y = y;
	rect->w = w;
	rect->h = h;
	rect->s = s;
	rect->f = f;

	return 0;
}

static int
rk_flinger_set_buffer_rotation(struct graphic_buffer *buffer, int r)
{
	if (!buffer)
		return -EINVAL;

	buffer->rotation = r;

	return buffer->rotation;
}

static int
rk_flinger_cacultae_dst_rect_by_rotation(struct graphic_buffer *buffer)
{
	struct rect *src_rect, *dst_rect;

	if (!buffer)
		return -EINVAL;

	src_rect = &buffer->src;
	dst_rect = &buffer->dst;

	switch (buffer->rotation) {
	case RGA_TRANSFORM_ROT_90:
	case RGA_TRANSFORM_ROT_270:
		dst_rect->x = src_rect->x;
		dst_rect->y = src_rect->y;
		dst_rect->h = src_rect->w;
		dst_rect->w = src_rect->h;
		dst_rect->s = src_rect->h;
		break;
	case RGA_TRANSFORM_ROT_0:
	case RGA_TRANSFORM_ROT_180:
	case RGA_TRANSFORM_FLIP_H:
	case RGA_TRANSFORM_FLIP_V:
	default:
		dst_rect->x = src_rect->x;
		dst_rect->y = src_rect->y;
		dst_rect->w = src_rect->w;
		dst_rect->h = src_rect->h;
		dst_rect->s = src_rect->s;
		break;
	}

	return 0;
}

static int rk_flinger_fill_buffer_rects(struct graphic_buffer *buffer,
					struct rect *src_rect,
					struct rect *dst_rect)
{
	if (!buffer)
		return -EINVAL;

	if (src_rect)
		memcpy(&buffer->src, src_rect, sizeof(struct rect));
	if (dst_rect)
		memcpy(&buffer->dst, dst_rect, sizeof(struct rect));

	return 0;
}

static int rk_flinger_iep_deinterlace(struct flinger *flinger,
				      struct graphic_buffer *src_buffer,
				      struct graphic_buffer *dst_buffer)
{
	struct rga_req rga_request;
	int ret;

	memset(&rga_request, 0, sizeof(rga_request));

	if (!src_buffer || !dst_buffer)
		return -EINVAL;

	rga_request.rotate_mode = 0;
	rga_request.sina = 0;
	rga_request.cosa = 0;

	rga_request.src.act_w = src_buffer->src.w;
	rga_request.src.act_h = src_buffer->src.h;
	rga_request.src.x_offset = 0;
	rga_request.src.y_offset = 0;
	rga_request.src.vir_w = src_buffer->src.w;
	rga_request.src.vir_h = src_buffer->src.h;
	rga_request.src.yrgb_addr = src_buffer->fd;
	rga_request.src.uv_addr = 0;
	rga_request.src.v_addr = 0;
	rga_request.src.format = RGA_FORMAT_YCrCb_420_SP;
	if (src_buffer->rotation == RGA_TRANSFORM_ROT_0 ||
		src_buffer->rotation == RGA_TRANSFORM_ROT_180) {
		rga_request.dst.act_w = src_buffer->src.w;
		rga_request.dst.act_h = src_buffer->src.h / 2;
		rga_request.dst.vir_w = src_buffer->src.w;
		rga_request.dst.vir_h = src_buffer->src.h / 2;
	} else {
		rga_request.dst.act_w = src_buffer->src.w / 2;
		rga_request.dst.act_h = src_buffer->src.h;
		rga_request.dst.vir_w = src_buffer->src.w / 2;
		rga_request.dst.vir_h = src_buffer->src.h;
	}
	rga_request.dst.x_offset = 0;
	rga_request.dst.y_offset = 0;

	rga_request.dst.yrgb_addr = dst_buffer->fd;
	rga_request.dst.uv_addr = 0;
	rga_request.dst.v_addr = 0;
	rga_request.dst.format = RGA_FORMAT_YCrCb_420_SP;

	rga_request.scale_mode = 1;

	rga_request.mmu_info.mmu_en = 1;
	rga_request.mmu_info.mmu_flag = ((2 & 0x3) << 4) |
			 1 | (1 << 31 | 1 << 8 | 1 << 10);

	rga_request.src.rd_mode = RGA_RASTER_MODE;
	rga_request.dst.rd_mode = RGA_RASTER_MODE;

	ret = rga_kernel_commit(&rga_request);
	if (ret)
		VEHICLE_DGERR("RGA_BLIT_SYNC failed(%d)\n", ret);

	dst_buffer->width = src_buffer->width;
	dst_buffer->height = src_buffer->height;
	dst_buffer->src.f = src_buffer->src.f;

	if (src_buffer->rotation == RGA_TRANSFORM_ROT_0 ||
		src_buffer->rotation == RGA_TRANSFORM_ROT_180) {
		dst_buffer->src.w = src_buffer->src.w;
		dst_buffer->src.h = src_buffer->src.h / 2;
	} else {
		dst_buffer->src.w = src_buffer->src.w / 2;
		dst_buffer->src.h = src_buffer->src.h;
	}
	dst_buffer->src.x = 0;
	dst_buffer->src.y = 0;

	src_buffer->state = FREE;

	return 0;
}

static int rk_flinger_rga_scaler(struct flinger *flinger,
				 struct graphic_buffer *src_buffer,
				 struct graphic_buffer *dst_buffer)
{
	struct rga_req rga_request;
	int ret;

	memset(&rga_request, 0, sizeof(rga_request));

	if (!src_buffer || !dst_buffer)
		return -EINVAL;

	rga_request.rotate_mode = 0;
	rga_request.sina = 0;
	rga_request.cosa = 0;

	rga_request.yuv2rgb_mode = 0x0 << 0; // yuvtoyuv config 0
	/* yuv to rgb color space transform if need  */
	//rga_request.yuv2rgb_mode = 0x1 << 0; // limit range
	//rga_request.yuv2rgb_mode = 0x2 << 0; // full range

	rga_request.src.act_w = src_buffer->src.w;
	rga_request.src.act_h = src_buffer->src.h;
	rga_request.src.x_offset = 0;
	rga_request.src.y_offset = 0;
	rga_request.src.vir_w = src_buffer->src.w;
	rga_request.src.vir_h = src_buffer->src.h;
	rga_request.src.yrgb_addr = src_buffer->fd;
	rga_request.src.uv_addr = 0;
	rga_request.src.v_addr = 0;
	rga_request.src.format = RGA_FORMAT_YCrCb_420_SP;

	rga_request.dst.act_w = dst_buffer->width;
	rga_request.dst.act_h = dst_buffer->height;
	rga_request.dst.x_offset = 0;
	rga_request.dst.y_offset = 0;
	rga_request.dst.vir_w = dst_buffer->width;
	rga_request.dst.vir_h = dst_buffer->height;
	rga_request.dst.yrgb_addr = dst_buffer->fd;
	rga_request.dst.uv_addr = 0;
	rga_request.dst.v_addr = 0;
	rga_request.dst.format =  RGA_FORMAT_YCrCb_420_SP;

	rga_request.scale_mode = 1;

	rga_request.mmu_info.mmu_en = 1;
	rga_request.mmu_info.mmu_flag = ((2 & 0x3) << 4) |
		   1 | (1 << 31 | 1 << 8 | 1 << 10);

	rga_request.src.rd_mode = RGA_RASTER_MODE;
	rga_request.dst.rd_mode = RGA_RASTER_MODE;

	ret = rga_kernel_commit(&rga_request);
	if (ret)
		VEHICLE_DGERR("RGA_BLIT_SYNC failed(%d)\n", ret);

	dst_buffer->src.f = dst_buffer->format;
	dst_buffer->src.w = dst_buffer->width;
	dst_buffer->src.h = dst_buffer->height;
	dst_buffer->src.x = 0;
	dst_buffer->src.y = 0;
	/* save rga in buffer */
	if (vehicle_dump_rga) {
		struct file *filep = NULL;
		loff_t pos = 0;
		static bool file_ready;
		static int frame_count;

		VEHICLE_DG("@%s src->vir_addr[0](%d) addr[100](%d)\n",
				__func__, ((char *)(src_buffer->vir_addr))[0],
					((char *)(src_buffer->vir_addr))[100]);
		if (!file_ready) {
			int frame_len = src_buffer->src.w * src_buffer->src.h * 3 / 2;
			char path[128] = {0};
			mm_segment_t fs;

			VEHICLE_DG("save vop frame(%d) frame_len(%d)\n",
							frame_count++, frame_len);
			sprintf(path, "/data/rga_scaler_in_%zu_%zu.yuv",
					src_buffer->src.w, src_buffer->src.h);
			filep = filp_open(path, O_CREAT | O_RDWR, 0666);
			if (IS_ERR(filep)) {
				VEHICLE_DGERR(" %s filp_open failed!\n", path);
				file_ready = false;
			} else {
				fs = get_fs();
				set_fs(KERNEL_DS);
				vfs_write(filep,
					(unsigned char __user *)(src_buffer->vir_addr),
					frame_len, &pos);
				filp_close(filep, NULL);
				set_fs(fs);
				VEHICLE_INFO(" %s file saved ok!\n", path);
				file_ready = true;
			}
		}
	}
	/* save rga out buffer */
	if (vehicle_dump_rga) {
		struct file *filep = NULL;
		loff_t pos = 0;
		static bool file_ready;
		static int frame_count;

		VEHICLE_DG("@%s dst->vir_addr[0](%d) addr[100](%d)\n",
				__func__, ((char *)(dst_buffer->vir_addr))[0],
					((char *)(dst_buffer->vir_addr))[100]);
		if (!file_ready) {
			/* NV12 */
			int frame_len = dst_buffer->src.w * dst_buffer->src.h * 3 / 2;
			char path[128] = {0};
			mm_segment_t fs;

			VEHICLE_DG("save vop frame(%d) frame_len(%d)\n",
							frame_count++, frame_len);
			sprintf(path, "/data/rga_scaler_out_%zu_%zu.yuv",
					dst_buffer->src.w, dst_buffer->src.h);
			filep = filp_open(path, O_CREAT | O_RDWR, 0666);
			if (IS_ERR(filep)) {
				VEHICLE_DGERR(" %s filp_open failed!\n", path);
				file_ready = false;
			} else {
				fs = get_fs();
				set_fs(KERNEL_DS);
				vfs_write(filep,
					(unsigned char __user *)(dst_buffer->vir_addr),
					frame_len, &pos);
				filp_close(filep, NULL);
				set_fs(fs);
				VEHICLE_INFO(" %s file saved ok!\n", path);
				file_ready = true;
			}
		}
	}

	src_buffer->state = FREE;

	return 0;
}

static int rk_flinger_rga_blit(struct flinger *flinger,
			       struct graphic_buffer *src_buffer,
			       struct graphic_buffer *dst_buffer)
{
	struct rga_req rga_request;
	int sx, sy, sw, sh, ss, sf;
	int dx, dy, dw, dh, ds, df;
	int orientation;
	int ret;
	int src_fd, dst_fd;

	if (!src_buffer || !dst_buffer)
		return -EINVAL;

	src_fd = src_buffer->fd;
	dst_fd = dst_buffer->fd;

	memset(&rga_request, 0, sizeof(rga_request));

	orientation = src_buffer->rotation;
	dst_buffer->rotation = src_buffer->rotation;

	sx = src_buffer->src.x;
	sy = src_buffer->src.y;
	sw = src_buffer->src.w;
	ss = src_buffer->src.s;
	sh = src_buffer->src.h;
	sf = rk_flinger_format_hal_to_rga(src_buffer->src.f);
	VEHICLE_DG("%s src: sx:%d, sy:%d, sw:%d, ss:%d, sh:%d\n",
				__func__, sx, sy, sw, ss, sh);
	dx = src_buffer->dst.x;
	dy = src_buffer->dst.y;
	dw = src_buffer->dst.w;
	ds = src_buffer->dst.s;
	dh = src_buffer->dst.h;
	df = rk_flinger_format_hal_to_rga(src_buffer->dst.f);
	VEHICLE_DG("%s dst: dx:%d, dy:%d, dw:%d, ds:%d, dh:%d\n",
				__func__, dx, dy, dw, ds, dh);
	if (src_buffer->offset) {
		sh += src_buffer->offset / src_buffer->len * sh;
		sx = src_buffer->offset / src_buffer->len * sh;
		src_fd = 0;
	}
	VEHICLE_DG("%s src: sx:%d, sy:%d, sw:%d, ss:%d, sh:%d\n",
				__func__, sx, sy, sw, ss, sh);
	switch (orientation) {
	case RGA_TRANSFORM_ROT_0:
		rga_request.rotate_mode = 0;
		rga_request.sina = 0;
		rga_request.cosa = 0;
		rga_request.dst.vir_w = ALIGN(ds, 64);
		rga_request.dst.vir_h = dh;
		rga_request.dst.act_w = dw;
		rga_request.dst.act_h = dh;
		rga_request.dst.x_offset = 0;
		rga_request.dst.y_offset = 0;
		break;
	case RGA_TRANSFORM_FLIP_H:/*x mirror*/
		rga_request.rotate_mode = 2;
		rga_request.dst.vir_w = ALIGN(ds, 64);
		rga_request.dst.vir_h = dh;
		rga_request.dst.act_w = dw;
		rga_request.dst.act_h = dh;
		rga_request.dst.x_offset = 0;
		rga_request.dst.y_offset = 0;
		break;
	case RGA_TRANSFORM_FLIP_V:/*y mirror*/
		rga_request.rotate_mode = 3;
		rga_request.dst.vir_w = ALIGN(ds, 64);
		rga_request.dst.vir_h = dh;
		rga_request.dst.act_w = dw;
		rga_request.dst.act_h = dh;
		rga_request.dst.x_offset = 0;
		rga_request.dst.y_offset = 0;
		break;
	case RGA_TRANSFORM_ROT_90:
		rga_request.rotate_mode = 1;
		rga_request.sina = 65536;
		rga_request.cosa = 0;
		rga_request.dst.vir_w = ALIGN(ds, 64);
		rga_request.dst.vir_h = dh;
		rga_request.dst.act_w = dh;
		rga_request.dst.act_h = dw;
		rga_request.dst.x_offset = 0;
		rga_request.dst.y_offset = 0;
		break;
	case RGA_TRANSFORM_ROT_180:
		rga_request.rotate_mode = 1;
		rga_request.sina = 0;
		rga_request.cosa = -65536;
		rga_request.dst.vir_w = ALIGN(ds, 64);
		rga_request.dst.vir_h = dh;
		rga_request.dst.act_w = dw;
		rga_request.dst.act_h = dh;
		rga_request.dst.x_offset = 0;
		rga_request.dst.y_offset = 0;
		break;
	case RGA_TRANSFORM_ROT_270:
		rga_request.rotate_mode = 1;
		rga_request.sina = -65536;
		rga_request.cosa = 0;
		rga_request.dst.vir_w = ALIGN(ds, 64);
		rga_request.dst.vir_h = dh;
		rga_request.dst.act_w = dh;
		rga_request.dst.act_h = dw;
		rga_request.dst.x_offset = 0;
		rga_request.dst.y_offset = 0;
		break;
	default:
		rga_request.rotate_mode = 0;
		rga_request.sina = 0;
		rga_request.cosa = 0;
		rga_request.dst.vir_w = ALIGN(ds, 64);
		rga_request.dst.vir_h = dh;
		rga_request.dst.act_w = dw;
		rga_request.dst.act_h = dh;
		rga_request.dst.x_offset = 0;
		rga_request.dst.y_offset = 0;
		break;
	}

	rga_request.src.yrgb_addr = src_fd;
	rga_request.src.uv_addr = 0;
	rga_request.src.v_addr = 0;

	rga_request.dst.yrgb_addr = dst_fd;
	rga_request.dst.uv_addr = 0;
	rga_request.dst.v_addr = 0;

	rga_request.src.vir_w = ss;
	rga_request.src.vir_h = sh;
	rga_request.src.format = sf;
	rga_request.src.act_w = sw;
	rga_request.src.act_h = sh;
	rga_request.src.x_offset = 0;
	rga_request.src.y_offset = 0;

	rga_request.dst.format = df;

	rga_request.clip.xmin = 0;
	rga_request.clip.xmax = dw - 1;
	rga_request.clip.ymin = 0;
	rga_request.clip.ymax = dh - 1;
	rga_request.scale_mode = 0;

	rga_request.yuv2rgb_mode = 0x0 << 0; // yuvtoyuv config 0
	/* yuv to rgb color space transform if need  */
	//rga_request.yuv2rgb_mode = 0x1 << 0; // limit range
	//rga_request.yuv2rgb_mode = 0x2 << 0; // full range

	rga_request.mmu_info.mmu_en = 1;
	rga_request.mmu_info.mmu_flag = ((2 & 0x3) << 4) |
		 1 | (1 << 31 | 1 << 8 | 1 << 10);

	rga_request.src.rd_mode = RGA_RASTER_MODE;
	rga_request.dst.rd_mode = RGA_RASTER_MODE;

	VEHICLE_DG("%s src_buffer->src.f(%zu) src_buffer->dst.f(%zu)",
				__func__, src_buffer->src.f, src_buffer->dst.f);
	ret = rga_kernel_commit(&rga_request);
	if (ret)
		VEHICLE_DGERR("RGA_BLIT_SYNC failed(%d)\n", ret);

	return 0;
}

static int rk_flinger_rga_render(struct flinger *flinger,
				 struct graphic_buffer *src_buffer,
				 struct graphic_buffer *dst_buffer)
{
	if (!flinger || !src_buffer || !dst_buffer)
		return -EINVAL;

	if (dst_buffer && dst_buffer->rel_fence)
		dst_buffer->rel_fence = NULL;

	rk_flinger_rga_blit(flinger, src_buffer, dst_buffer);
	rk_flinger_fill_buffer_rects(dst_buffer, &src_buffer->dst,
				     &src_buffer->dst);
	dst_buffer->src.f = src_buffer->dst.f;

	/* save rga out buffer */
	if (vehicle_dump_rga) {
		struct file *filep = NULL;
		loff_t pos = 0;
		static bool file_ready;
		static int frame_count;

		VEHICLE_DG("@%s dst->vir_addr[0](%d) addr[100](%d)\n",
				__func__, ((char *)(dst_buffer->vir_addr))[0],
					((char *)(dst_buffer->vir_addr))[100]);
		if (!file_ready) {
			int frame_len = dst_buffer->src.w * dst_buffer->src.h * 3 / 2;//NV12
			char path[128] = {0};
			mm_segment_t fs;

			VEHICLE_DG("save vop frame(%d) frame_len(%d)\n",
							frame_count++, frame_len);
			sprintf(path, "/data/rga_render_%zu_%zu.yuv",
					dst_buffer->src.w, dst_buffer->src.h);
			filep = filp_open(path, O_CREAT | O_RDWR, 0666);
			if (IS_ERR(filep)) {
				VEHICLE_DGERR(" %s filp_open failed!\n", path);
				file_ready = false;
			} else {
				fs = get_fs();
				set_fs(KERNEL_DS);
				vfs_write(filep,
					(unsigned char __user *)(dst_buffer->vir_addr),
					frame_len, &pos);
				filp_close(filep, NULL);
				set_fs(fs);
				VEHICLE_INFO(" %s file saved ok!\n", path);
				file_ready = true;
			}
		}
	}

	return 0;
}

static void rk_drm_vehicle_commit(struct flinger *flinger, struct graphic_buffer *buffer)
{
	struct rockchip_drm_direct_show_commit_info commit_info;
	int hdisplay = flinger->crtc->state->adjusted_mode.hdisplay;
	int vdisplay = flinger->crtc->state->adjusted_mode.vdisplay;

	commit_info.crtc = flinger->crtc;
	commit_info.plane = flinger->plane;

	commit_info.src_x = 0;
	commit_info.src_y = 0;
	commit_info.src_w = buffer->src.w;
	commit_info.src_h = buffer->src.h;
	// commit_info.src_w = buffer->drm_buffer->width;
	// commit_info.src_h = buffer->drm_buffer->height;

	/*center display*/
	// commit_info.dst_x = (hdisplay - BUFFER_WIDTH) / 2;
	// commit_info.dst_y = (vdisplay - BUFFER_HEIGHT) / 2;
	// commit_info.dst_w = commit_info.src_w;
	// commit_info.dst_h = commit_info.src_h;

	/*full screen display */
	commit_info.dst_x = 0;
	commit_info.dst_y = 0;
	commit_info.dst_w = hdisplay;
	commit_info.dst_h = vdisplay;

	commit_info.top_zpos  = true;

	commit_info.buffer = buffer->drm_buffer;

	if (vehicle_dump_vop) {
		struct file *filep = NULL;
		loff_t pos = 0;
		static bool file_ready;
		static int frame_count;

		if (!file_ready) {
			int frame_len = buffer->drm_buffer->width *
					buffer->drm_buffer->height * 3 / 2;//NV12
			char path[128] = {0};
			mm_segment_t fs;

			VEHICLE_DG("save vop frame(%d) frame_len(%d)\n",
							frame_count++, frame_len);
			sprintf(path, "/data/vop_commit_%d_%d.yuv",
						buffer->drm_buffer->width,
						buffer->drm_buffer->height);
			filep = filp_open(path, O_CREAT | O_RDWR, 0666);
			if (IS_ERR(filep)) {
				VEHICLE_DGERR(" %s filp_open failed!\n", path);
				file_ready = false;
			} else {
				fs = get_fs();
				set_fs(KERNEL_DS);
				vfs_write(filep,
					(unsigned char __user *)(buffer->drm_buffer->vir_addr[0]),
					frame_len, &pos);
				filp_close(filep, NULL);
				set_fs(fs);
				VEHICLE_INFO(" %s file saved ok!\n", path);
				file_ready = true;
			}
		}
	}
	rockchip_drm_direct_show_commit(flinger->drm_dev, &commit_info);
}

static int rk_flinger_vop_show(struct flinger *flinger,
			       struct graphic_buffer *buffer)
{
	if (!flinger || !buffer)
		return -EINVAL;

	VEHICLE_DG("flinger vop show buffer wxh(%zux%zu)\n",
					buffer->src.w, buffer->src.h);
	if (!flinger->running)
		return 0;

	/* get crtc and plane */
	flinger->crtc = rockchip_drm_direct_show_get_crtc(flinger->drm_dev, flinger->crtc_name);
	if (flinger->crtc == NULL) {
		VEHICLE_DGERR("error: failed to get crtc\n");
		return -EINVAL;
	}

	flinger->plane = rockchip_drm_direct_show_get_plane(flinger->drm_dev, flinger->plane_name);
	if (flinger->plane == NULL) {
		VEHICLE_DGERR("error: failed to get plane\n");
		return -EINVAL;
	}

	rk_drm_vehicle_commit(flinger, buffer);

	flinger->debug_vop_count++;
	/* save vop show buffer */
	if (vehicle_dump_vop) {
		struct file *filep = NULL;
		loff_t pos = 0;
		static bool file_ready;
		static int frame_count;

		VEHICLE_DG("@%s buffer->vir_addr[0](%d) addr[100](%d)\n",
				__func__, ((char *)(buffer->vir_addr))[0],
					((char *)(buffer->vir_addr))[100]);
		if (!file_ready) {
			int frame_len = buffer->src.w * buffer->src.h * 3 / 2;//NV12
			char path[128] = {0};
			mm_segment_t fs;

			VEHICLE_DG("save vop frame(%d) frame_len(%d)\n",
							frame_count++, frame_len);
			sprintf(path, "/data/vop_show_%zu_%zu.yuv",
						buffer->src.w, buffer->src.h);
			filep = filp_open(path, O_CREAT | O_RDWR, 0666);
			if (IS_ERR(filep)) {
				VEHICLE_DGERR(" %s filp_open failed!\n", path);
				file_ready = false;
			} else {
				fs = get_fs();
				set_fs(KERNEL_DS);
				vfs_write(filep,
					(unsigned char __user *)(buffer->vir_addr),
					frame_len, &pos);
				filp_close(filep, NULL);
				set_fs(fs);
				VEHICLE_INFO(" %s file saved ok!\n", path);
				file_ready = true;
			}
		}
	}

	return 0;
}

static void rk_flinger_first_done(struct work_struct *work)
{
	struct graphic_buffer *buffer;
	struct flinger *flg = flinger;
	int i;
	struct flinger *flg_test =
		 container_of(work, struct flinger, init_work);
	struct vehicle_cfg *v_cfg = &flg_test->v_cfg;

	if (!flg)
		return;

	for (i = 0; i < NUM_SOURCE_BUFFERS; i++) {
		if (flg->source_buffer[i].state == FREE) {
			buffer = &(flg->source_buffer[i]);
			rk_flinger_set_rect(&buffer->src,
					    FORCE_XOFFSET, FORCE_YOFFSET,
					    v_cfg->width, v_cfg->height,
					    v_cfg->width, FORCE_FORMAT);
			rk_flinger_set_buffer_rotation(buffer, FORCE_ROTATION);
			rk_flinger_cacultae_dst_rect_by_rotation(buffer);
			buffer->dst.f = buffer->src.f;
		}
	}
}

static void rk_flinger_render_show(struct work_struct *work)
{
	struct graphic_buffer *src_buffer, *dst_buffer, *iep_buffer, *buffer;
	/* struct queue_buffer *cur = NULL, *next = NULL; */
	struct flinger *flg = flinger;
	int i, found = 0;
	static int count = -1;
	static int last_src_index = -1;
	bool cvbs_flag = true;
	struct flinger *flg_test =
			container_of(work, struct flinger, render_work);
	struct vehicle_cfg *v_cfg = &flg_test->v_cfg;

	src_buffer = NULL;
	dst_buffer = NULL;
	flg->source_index = 0;

	do {
try_again:
		wait_event_interruptible_timeout(flg->worker_wait,
						 atomic_read(&flg->worker_cond_atomic),
						 msecs_to_jiffies(1000000));
		VEHICLE_DG("wake up enter, v_cfg.w*h(%dx%d)\n",
				v_cfg->width, v_cfg->height);

		if (atomic_read(&flg->worker_running_atomic) == 0) {
			VEHICLE_INFO("%s loop exit\n", __func__);
			break;
		}
		if (atomic_read(&flg->worker_cond_atomic) <= 0) {
			/*printk("waiting 'worker_cond_atomic' timed out.");*/
			goto try_again;
		}
		atomic_dec(&flg->worker_cond_atomic);

		/*  1. find src buffer */
		src_buffer = NULL;
		found = last_src_index + 1;
		for (i = 1; i < NUM_SOURCE_BUFFERS; i++, found++) {
			found = found % NUM_SOURCE_BUFFERS;
			if (flg->source_buffer[found].state == QUEUE) {
				src_buffer = &flg->source_buffer[found];
				last_src_index = found;
				break;
			}
		}

		if (!src_buffer || !src_buffer->fd) {
			usleep_range(3000, 3100);
			VEHICLE_DGERR("[%s:%d] error, no buffer\n", __func__, __LINE__);
			goto try_again;
		}

		count++;
		src_buffer->state = ACQUIRE;
		/* save rkcif buffer */
		if (vehicle_dump_cif) {
			// struct file *filep = NULL;
			struct file *filep;
			loff_t pos = 0;
			static bool file_ready;
			static int frame_count;

			VEHICLE_DG("src_buffer->vir_addr[0](%d) addr[100](%d)\n",
						((char *)(src_buffer->vir_addr))[0],
						((char *)(src_buffer->vir_addr))[100]);

			if (!file_ready) {
				//nv12 frame_len=w*h*3/2
				int frame_len = src_buffer->src.w * src_buffer->src.h * 3 / 2;
				char path[128] = {0};
				mm_segment_t fs;

				VEHICLE_DG("save vop frame(%d) frame_len(%d)\n",
								frame_count++, frame_len);
				sprintf(path, "/data/cif_out_%zu_%zu.yuv",
							src_buffer->src.w, src_buffer->src.h);
				filep = filp_open(path, O_RDWR | O_CREAT, 0666);
				if (IS_ERR(filep)) {
					VEHICLE_DGERR(" %s filp_open failed!\n", path);
					file_ready = false;
				} else {
					fs = get_fs();
					set_fs(KERNEL_DS);
					vfs_write(filep, src_buffer->vir_addr, frame_len, &pos);
					filp_close(filep, NULL);
					set_fs(fs);
					VEHICLE_INFO(" %s file saved ok!\n", path);
					file_ready = true;
				}
			}
		}

		/*  2. find dst buffer */
		dst_buffer = NULL;
		iep_buffer = NULL;
		/*get iep, rga, vop buffer*/
		if (1) { //rotation by rga
			if (flg->v_cfg.input_format == CIF_INPUT_FORMAT_PAL ||
			    flg->v_cfg.input_format == CIF_INPUT_FORMAT_NTSC) {
				iep_buffer = &(flg->target_buffer
					       [NUM_TARGET_BUFFERS - 1]);
				iep_buffer->state = ACQUIRE;
				cvbs_flag = true;
			} else {
				cvbs_flag = false;
			}
			dst_buffer = &(flg->target_buffer
				       [count % (NUM_TARGET_BUFFERS - 1)]);
			dst_buffer->state = ACQUIRE;
		} else if (flg->v_cfg.input_format == CIF_INPUT_FORMAT_PAL ||
			   flg->v_cfg.input_format == CIF_INPUT_FORMAT_NTSC) {
			iep_buffer = &(flg->target_buffer
				       [count % NUM_TARGET_BUFFERS]);
			iep_buffer->state = ACQUIRE;
		}
		if (!iep_buffer || !iep_buffer->fd) {
			if (iep_buffer)
				iep_buffer->state = FREE;
		}

		/* 3 do deinterlace & rotation & display*/
		if (!cvbs_flag) {
			// YPbPr
			VEHICLE_DG("it is ypbpr signal\n");
			iep_buffer = &(flg->target_buffer[NUM_TARGET_BUFFERS - 1]);
			iep_buffer->state = ACQUIRE;
			//scaler by rga to force widthxheight display
			rk_flinger_rga_render(flg, src_buffer, iep_buffer);
			src_buffer->state = FREE;
			rk_flinger_rga_scaler(flg, iep_buffer, dst_buffer);
			iep_buffer->state = FREE;
			rk_flinger_vop_show(flg, dst_buffer);
			for (i = 0; i < NUM_TARGET_BUFFERS; i++) {
				buffer = &(flinger->target_buffer[i]);
				if (buffer->state == DISPLAY)
					buffer->state = FREE;
			}

			dst_buffer->state = DISPLAY;
		} else {
			// cvbs
			VEHICLE_DG("it is a cvbs signal\n");
			rk_flinger_rga_render(flg, src_buffer, dst_buffer);
			src_buffer->state = FREE;
			rk_flinger_iep_deinterlace(flg, dst_buffer, iep_buffer);
			dst_buffer->state = FREE;
			rk_flinger_rga_scaler(flg, iep_buffer, dst_buffer);
			rk_flinger_vop_show(flg, dst_buffer);
			iep_buffer->state = FREE;

			for (i = 0; i < NUM_TARGET_BUFFERS; i++) {
				buffer = &(flinger->target_buffer[i]);
				if (buffer->state == DISPLAY)
					buffer->state = FREE;
			}
			dst_buffer->state = DISPLAY;
		}
	} while (1);
}

static int rk_flinger_queue_work(struct flinger *flinger,
				 struct graphic_buffer *src_buffer)
{
	if (!flinger)
		return -ENODEV;

	if (!src_buffer) {
		if (flinger->render_workqueue) {
			INIT_WORK(&flinger->init_work, rk_flinger_first_done);
			queue_work(flinger->render_workqueue,
				   &flinger->init_work);
		}
	}

	if (flinger->render_workqueue) {
		INIT_WORK(&flinger->render_work, rk_flinger_render_show);
		queue_work(flinger->render_workqueue, &flinger->render_work);
	}

	return 0;
}

static struct graphic_buffer *
rk_flinger_lookup_buffer_by_phy_addr(unsigned long phy_addr)
{
	struct graphic_buffer *buffer = NULL;
	struct flinger *flg = flinger;
	int i;

	VEHICLE_DG("%s:phy_addr=%lx\n", __func__, phy_addr);
	for (i = 1; i < NUM_SOURCE_BUFFERS; i++) {
		if (flg->source_buffer[i].state == DEQUEUE) {
			buffer = &(flg->source_buffer[i]);
			if (buffer && (buffer->offset +
			    buffer->phy_addr == phy_addr)) {
				buffer->state = QUEUE;
				break;
			}
		}
	}
	if (i < NUM_SOURCE_BUFFERS)
		return buffer;
	else
		return NULL;
}

static bool vehicle_rotation_param_check(struct vehicle_cfg *v_cfg)
{
	switch (v_cfg->rotate_mirror) {
	case RGA_TRANSFORM_ROT_90:
	case RGA_TRANSFORM_ROT_270:
	case RGA_TRANSFORM_ROT_0:
	case RGA_TRANSFORM_ROT_180:
	case RGA_TRANSFORM_FLIP_H:
	case RGA_TRANSFORM_FLIP_V:
		return true;
	default:
		VEHICLE_INFO("invalid rotate-mirror param %d\n",
					v_cfg->rotate_mirror);
		v_cfg->rotate_mirror = 0;
		return false;
	}
}
int vehicle_flinger_reverse_open(struct vehicle_cfg *v_cfg,
				bool android_is_ready)
{
	int i;
	int width;
	int height;
	struct flinger *flg = flinger;
	struct graphic_buffer *buffer;
	int hal_format;

	width = v_cfg->width;
	height = v_cfg->height;

	if (!flinger)
		return -ENODEV;

	vehicle_rotation_param_check(v_cfg);

	if (v_cfg->output_format == CIF_OUTPUT_FORMAT_422)
		hal_format = HAL_PIXEL_FORMAT_YCbCr_422_SP;
	else
		hal_format = HAL_PIXEL_FORMAT_YCrCb_NV12;

	/*  1. reinit buffer format */
	for (i = 0; i < NUM_SOURCE_BUFFERS; i++) {
		buffer = &(flg->source_buffer[i]);
		rk_flinger_set_rect(&buffer->src,
				    0, 0, width,
				    height, width, hal_format);
		rk_flinger_set_buffer_rotation(buffer, v_cfg->rotate_mirror);
		rk_flinger_cacultae_dst_rect_by_rotation(buffer);
		buffer->dst.f = buffer->src.f;
		buffer->state = FREE;
	}

	for (i = 0; i < NUM_TARGET_BUFFERS; i++) {
		buffer = &(flg->target_buffer[i]);
		buffer->state = FREE;
	}

	/*2. fill buffer info*/
	for (i = 0; i < NUM_SOURCE_BUFFERS && i < MAX_BUF_NUM; i++) {
		v_cfg->buf_phy_addr[i] = flinger->source_buffer[i].phy_addr;
		VEHICLE_DG("buf_phy_addr=%x, i=%d", v_cfg->buf_phy_addr[i], i);
	}

	v_cfg->buf_num = NUM_SOURCE_BUFFERS;

	flg->cvbs_field_count = 0;
	memcpy(&flg->v_cfg, v_cfg, sizeof(struct vehicle_cfg));
	flg->running = true;

	return 0;
}

int vehicle_flinger_reverse_close(bool android_is_ready)
{
	struct flinger *flg = flinger;

	flg->running = false;
	if (flg->drm_dev && flg->plane)
		rockchip_drm_direct_show_disable_plane(flg->drm_dev, flg->plane);
	VEHICLE_DG("%s(%d) done\n", __func__, __LINE__);

	return 0;
}

unsigned long vehicle_flinger_request_cif_buffer(void)
{
	struct graphic_buffer *src_buffer = NULL;
	struct flinger *flg = flinger;
	static int last_src_index = -1;
	int found;
	int i;

	src_buffer = NULL;
	for (i = 1; i < NUM_SOURCE_BUFFERS; i++) {
		found = (last_src_index + i) % NUM_SOURCE_BUFFERS;
		VEHICLE_DG("%s,flg->source_buffer[%d].state(%d)",
			__func__, found, flg->source_buffer[found].state);
		if (flg->source_buffer[found].state == FREE) {
			src_buffer = &flg->source_buffer[found];
			last_src_index = found;
			src_buffer->state = DEQUEUE;
			break;
		}
	}

	if (i < NUM_SOURCE_BUFFERS)
		return src_buffer->phy_addr;
	else
		return 0;
}

void vehicle_flinger_commit_cif_buffer(u32 buf_phy_addr)
{
	struct graphic_buffer *buffer = NULL;
	struct flinger *flg = flinger;

	if (!flg)
		return;

	buffer = rk_flinger_lookup_buffer_by_phy_addr(buf_phy_addr);
	if (buffer) {
		buffer->timestamp = ktime_get();
		atomic_inc(&flg->worker_cond_atomic);
		flg->debug_cif_count++;
		wake_up(&flg->worker_wait);
	} else {
		VEHICLE_DGERR("%x, no free buffer\n", buf_phy_addr);
	}
}
