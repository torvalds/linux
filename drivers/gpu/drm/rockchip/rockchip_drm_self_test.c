// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 * Author: Sandy Huang <hjc@rock-chips.com>
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/workqueue.h>

#include <drm/drm_atomic_uapi.h>

#include "rockchip_drm_drv.h"
#include "rockchip_drm_direct_show.h"
#include "rockchip_drm_display_pattern.h"

#include "kernel_logo_img.h"

#define USE_BUFFER_NUM	2
#define BUFFER_WIDTH	652
#define BUFFER_HEIGHT	268
#define BUFFER_FORMAT	DRM_FORMAT_RGB565 /* DRM_FORMAT_RGB565/DRM_FORMAT_XRGB8888/DRM_FORMAT_NV12 */

struct rockchip_drm_self_test {
	struct drm_device *dev;
	struct work_struct commit_work;
	struct workqueue_struct *workqueue;

	struct drm_crtc *crtc;
	struct drm_plane *plane;

	struct rockchip_drm_direct_show_buffer *drm_buffer[USE_BUFFER_NUM];
};

static struct rockchip_drm_self_test rockchip_drm_st;

static void __maybe_unused
rockchip_drm_draw_white(struct rockchip_drm_direct_show_buffer *buffer)
{
	if (buffer && buffer->vir_addr[0])
		memset(buffer->vir_addr[0], 0xff, buffer->pitch[0] * buffer->height);
}

static void __maybe_unused
rockchip_drm_draw_gray128(struct rockchip_drm_direct_show_buffer *buffer)
{
	if (buffer && buffer->vir_addr[0])
		memset(buffer->vir_addr[0], 0x80, buffer->pitch[0] * buffer->height);
}

static void __maybe_unused
rockchip_drm_copy_bmp_file(struct rockchip_drm_direct_show_buffer *buffer)
{
	int i = 0;
	void *src, *dst;

	if (!buffer || !buffer->vir_addr[0]) {
		pr_info("%s[%d] buffer or buffer->vir_addr[0] is NULL\n", __func__, __LINE__);
		return;
	}

	src = (void *)bmp_file;
	dst = (void *)buffer->vir_addr[0];
	for (i = 0; i < buffer->height; i++) {
		memcpy(dst, src, buffer->pitch[0]);
		src += BUFFER_WIDTH * buffer->bpp >> 3;
		dst += buffer->pitch[0];
	}
}

static void __maybe_unused
rockchip_drm_draw_color_bar(struct rockchip_drm_direct_show_buffer *buffer)
{
	if (buffer && buffer->vir_addr[0])
		rockchip_drm_fill_color_bar(buffer->pixel_format,
					    buffer->vir_addr,
					    buffer->width,
					    buffer->height,
					    buffer->pitch[0]);
}

static int rockchip_drm_self_test_alloc_buffer(struct rockchip_drm_self_test *self_test)
{
	int ret = 0, i = 0;
	struct rockchip_drm_direct_show_buffer *buffer;

	for (i = 0; i < USE_BUFFER_NUM; i++) {
		buffer = kmalloc(sizeof(struct rockchip_drm_direct_show_buffer), GFP_KERNEL);
		if (!buffer)
			return -ENOMEM;
		buffer->width = BUFFER_WIDTH;
		buffer->height = BUFFER_HEIGHT;
		buffer->pixel_format = BUFFER_FORMAT;
		buffer->flag = ROCKCHIP_BO_CONTIG;
		ret = rockchip_drm_direct_show_alloc_buffer(self_test->dev, buffer);
		if (ret)
			pr_info("failed to alloc drm buffer\n");
		self_test->drm_buffer[i] = buffer;
	}

	return 0;
}

static int rockchip_drm_self_test_free_buffer(struct rockchip_drm_self_test *self_test)
{
	int i = 0;

	for (i = 0; i < USE_BUFFER_NUM; i++)
		rockchip_drm_direct_show_free_buffer(self_test->dev, self_test->drm_buffer[i]);

	return 0;
}

static void rockchip_drm_self_test_commit(struct work_struct *work)
{
	struct rockchip_drm_self_test *self_test =
			container_of(work, struct rockchip_drm_self_test, commit_work);
	struct rockchip_drm_direct_show_commit_info commit_info;
	int ret = 0;

	if (!self_test->dev)
		self_test->dev = rockchip_drm_get_dev();

	/* drm is unready */
	if (!self_test->dev) {
		pr_info("%s[%d], drm is unready\n", __func__, __LINE__);
		msleep(100);
		queue_work(self_test->workqueue, &self_test->commit_work);

		return;
	}

	/* alloc buffer */
	if (!self_test->drm_buffer[0]) {
		ret = rockchip_drm_self_test_alloc_buffer(self_test);
		if (ret)
			pr_info("error: drm self test alloc buffer error\n");
	}

	/* draw buffer */
	rockchip_drm_copy_bmp_file(self_test->drm_buffer[0]);
	/* rockchip_drm_draw_gray128(self_test->drm_buffer[1]); */
	rockchip_drm_draw_color_bar(self_test->drm_buffer[1]);

	/* get crtc and plane */
	self_test->crtc = rockchip_drm_direct_show_get_crtc(self_test->dev, NULL);
	if (self_test->crtc == NULL) {
		pr_info("error: failed to get crtc\n");
		goto free_buffer;
	}

	self_test->plane = rockchip_drm_direct_show_get_plane(self_test->dev, "Esmart0-win0");
	if (self_test->plane == NULL) {
		pr_info("error: failed to get plane\n");
		goto free_buffer;
	}

#if 1	/* for self test pattern */
	/* commit to display */
	do {
		u32 i = 0;

		commit_info.crtc = self_test->crtc;
		commit_info.plane = self_test->plane;

		commit_info.src_x = 0;
		commit_info.src_y = 0;
		commit_info.src_w = BUFFER_WIDTH;
		commit_info.src_h = BUFFER_HEIGHT;

		commit_info.dst_x = 0;
		commit_info.dst_y = 0;
		commit_info.dst_w = commit_info.src_w;
		commit_info.dst_h = commit_info.src_h;

		commit_info.top_zpos = true;

		for (i = 0; i < 1000; i++) {
			commit_info.buffer = self_test->drm_buffer[i % 2];/* two buffer ping pong */
			rockchip_drm_direct_show_commit(self_test->dev, &commit_info);
			mdelay(1000);
		}
		/* disable plane */
		rockchip_drm_direct_show_disable_plane(self_test->dev, commit_info.plane);
		/* free buffer */
		rockchip_drm_self_test_free_buffer(self_test);
	} while (0);
#else
	/* for kernel logo display */
	do {
		int hdisplay = self_test->crtc->state->adjusted_mode.hdisplay;
		int vdisplay = self_test->crtc->state->adjusted_mode.vdisplay;

		commit_info.crtc = self_test->crtc;
		commit_info.plane = self_test->plane;

		commit_info.src_x = 0;
		commit_info.src_y = 0;
		commit_info.src_w = self_test->drm_buffer[0]->width;
		commit_info.src_h = self_test->drm_buffer[0]->height;

		if (1) {/* center display */
			commit_info.dst_x = (hdisplay - BUFFER_WIDTH) / 2;
			commit_info.dst_y = (vdisplay - BUFFER_HEIGHT) / 2;
			commit_info.dst_w = commit_info.src_w;
			commit_info.dst_h = commit_info.src_h;

		} else {/* full screen display */
			commit_info.dst_x = 0;
			commit_info.dst_y = 0;
			commit_info.dst_w = hdisplay;
			commit_info.dst_h = vdisplay;
		}

		commit_info.buffer = self_test->drm_buffer[0];
		rockchip_drm_direct_show_commit(self_test->dev, &commit_info);
	} while (0);
#endif
	return;

free_buffer:
	/* free buffer */
	rockchip_drm_self_test_free_buffer(self_test);
}

static int rockchip_drm_self_test_create_worker(struct rockchip_drm_self_test *slef_test)
{
	struct workqueue_struct *wq = NULL;

	wq = create_singlethread_workqueue("rockchip_drm_self_test");
	if (!wq) {
		pr_info("Failed to create rockchip_drm_self_test workqueue\n");
		return -ENODEV;
	}
	slef_test->workqueue = wq;

	return 0;
}

static int __maybe_unused rockchip_drm_self_test_destory_worker(struct rockchip_drm_self_test *slef_test)
{
	if (!slef_test)
		return -ENODEV;

	if (slef_test->workqueue)
		destroy_workqueue(slef_test->workqueue);

	return 0;
}

static int rockchip_drm_self_test_main(void *arg)
{
	rockchip_drm_self_test_create_worker(&rockchip_drm_st);
	INIT_WORK(&rockchip_drm_st.commit_work, rockchip_drm_self_test_commit);
	queue_work(rockchip_drm_st.workqueue, &rockchip_drm_st.commit_work);

	return 0;
};

static int __init rockchip_drm_self_test(void)
{
	kthread_run(rockchip_drm_self_test_main, NULL, "rockchip drm self test");

	return 0;
}

subsys_initcall_sync(rockchip_drm_self_test);
