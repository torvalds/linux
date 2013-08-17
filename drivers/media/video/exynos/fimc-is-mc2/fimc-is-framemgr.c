/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 * exynos5 fimc-is video functions
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <mach/videonode.h>
#include <media/exynos_mc.h>
#include <linux/cma.h>
#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <linux/firmware.h>
#include <linux/dma-mapping.h>
#include <linux/scatterlist.h>
#include <linux/videodev2.h>
#include <linux/videodev2_exynos_camera.h>
#include <linux/videodev2_exynos_media.h>
#include <linux/v4l2-mediabus.h>
#include <linux/bug.h>

#include <mach/map.h>
#include <mach/regs-clock.h>

#include "fimc-is-core.h"
#include "fimc-is-param.h"
#include "fimc-is-cmd.h"
#include "fimc-is-regs.h"
#include "fimc-is-err.h"
#include "fimc-is-video.h"

#include "fimc-is-device-sensor.h"

int fimc_is_frame_s_free_shot(struct fimc_is_framemgr *this,
	struct fimc_is_frame *item)
{
	int ret = 0;

	if (item) {
		item->state = FIMC_IS_FRAME_STATE_FREE;

		list_add_tail(&item->list, &this->frame_free_head);
		this->frame_fre_cnt++;

#ifdef TRACE_FRAME
		fimc_is_frame_print_free_list(this);
#endif
	} else {
		ret = -EFAULT;
		err("item is null ptr\n");
	}

	return ret;
}


int fimc_is_frame_g_free_shot(struct fimc_is_framemgr *this,
	struct fimc_is_frame **item)
{
	int ret = 0;

	if (item) {
		if (this->frame_fre_cnt) {
			*item = container_of(this->frame_free_head.next,
				struct fimc_is_frame, list);
			list_del(&(*item)->list);
			this->frame_fre_cnt--;

			(*item)->state = FIMC_IS_FRAME_STATE_INVALID;
		} else {
			*item = NULL;
		}
	} else {
		ret = -EFAULT;
		err("item is null ptr\n");
	}

	return ret;
}

void fimc_is_frame_free_head(struct fimc_is_framemgr *this,
	struct fimc_is_frame **item)
{
	if (this->frame_fre_cnt)
		*item = container_of(this->frame_free_head.next,
			struct fimc_is_frame, list);
	else
		*item = NULL;
}

void fimc_is_frame_print_free_list(struct fimc_is_framemgr *this)
{
	struct list_head *temp;
	struct fimc_is_frame *shot;

	if (!(TRACE_ID & this->id))
		return;

	printk(KERN_CONT "[FRM] fre(%d, %d) :", this->id, this->frame_fre_cnt);

	list_for_each(temp, &this->frame_free_head) {
		shot = list_entry(temp, struct fimc_is_frame, list);
		printk(KERN_CONT "%d->", shot->index);
	}

	printk(KERN_CONT "X\n");
}

int fimc_is_frame_s_request_shot(struct fimc_is_framemgr *this,
	struct fimc_is_frame *item)
{
	int ret = 0;

	if (item) {
		list_add_tail(&item->list, &this->frame_request_head);
		this->frame_req_cnt++;

		item->state = FIMC_IS_FRAME_STATE_REQUEST;

#ifdef TRACE_FRAME
		fimc_is_frame_print_request_list(this);
#endif
	} else {
		ret = -EFAULT;
		err("item is null ptr\n");
	}

	return ret;
}

int fimc_is_frame_g_request_shot(struct fimc_is_framemgr *this,
	struct fimc_is_frame **item)
{
	int ret = 0;

	if (item) {
		if (this->frame_req_cnt) {
			*item = container_of(this->frame_request_head.next,
				struct fimc_is_frame, list);
			list_del(&(*item)->list);
			this->frame_req_cnt--;

			(*item)->state = FIMC_IS_FRAME_STATE_INVALID;
		} else {
			*item = NULL;
		}
	} else {
		ret = -EFAULT;
		err("item is null ptr\n");
	}

	return ret;
}

void fimc_is_frame_request_head(struct fimc_is_framemgr *this,
	struct fimc_is_frame **item)
{
	if (this->frame_req_cnt)
		*item = container_of(this->frame_request_head.next,
			struct fimc_is_frame, list);
	else
		*item = NULL;
}

void fimc_is_frame_print_request_list(struct fimc_is_framemgr *this)
{
	struct list_head *temp;
	struct fimc_is_frame *shot;

	if (!(TRACE_ID & this->id))
		return;

	printk(KERN_CONT "[FRM] req(%d, %d) :",
		this->id, this->frame_req_cnt);

	list_for_each(temp, &this->frame_request_head) {
		shot = list_entry(temp, struct fimc_is_frame, list);
		printk(KERN_CONT "%d->", shot->index);
	}

	printk(KERN_CONT "X\n");
}

int fimc_is_frame_s_process_shot(struct fimc_is_framemgr *this,
	struct fimc_is_frame *item)
{
	int ret = 0;

	if (item) {
		list_add_tail(&item->list, &this->frame_process_head);
		this->frame_pro_cnt++;

		item->state = FIMC_IS_FRAME_STATE_PROCESS;

#ifdef TRACE_FRAME
		fimc_is_frame_print_process_list(this);
#endif
	} else {
		ret = -EFAULT;
		err("item is null ptr\n");
	}

	return ret;
}

int fimc_is_frame_g_process_shot(struct fimc_is_framemgr *this,
	struct fimc_is_frame **item)
{
	int ret = 0;

	if (item) {
		if (this->frame_pro_cnt) {
			*item = container_of(this->frame_process_head.next,
				struct fimc_is_frame, list);
			list_del(&(*item)->list);
			this->frame_pro_cnt--;

			(*item)->state = FIMC_IS_FRAME_STATE_INVALID;
		} else {
			*item = NULL;
		}
	} else {
		ret = -EFAULT;
		err("item is null ptr\n");
	}

	return ret;
}

void fimc_is_frame_process_head(struct fimc_is_framemgr *this,
	struct fimc_is_frame **item)
{
	if (this->frame_pro_cnt)
		*item = container_of(this->frame_process_head.next,
			struct fimc_is_frame, list);
	else
		*item = NULL;
}

void fimc_is_frame_print_process_list(struct fimc_is_framemgr *this)
{
	struct list_head *temp;
	struct fimc_is_frame *shot;

	if (!(TRACE_ID & this->id))
		return;

	printk(KERN_CONT "[FRM] pro(%d, %d) :",
		this->id, this->frame_pro_cnt);

	list_for_each(temp, &this->frame_process_head) {
		shot = list_entry(temp, struct fimc_is_frame, list);
		printk(KERN_CONT "%d(%d)->", shot->index, shot->fcount);
	}

	printk(KERN_CONT "X\n");
}

int fimc_is_frame_s_complete_shot(struct fimc_is_framemgr *this,
	struct fimc_is_frame *item)
{
	int ret = 0;

	if (item) {
		list_add_tail(&item->list, &this->frame_complete_head);
		this->frame_com_cnt++;

		item->state = FIMC_IS_FRAME_STATE_COMPLETE;

#ifdef TRACE_FRAME
		fimc_is_frame_print_complete_list(this);
#endif
	} else {
		ret = -EFAULT;
		err("item is null ptr\n");
	}

	return ret;
}


int fimc_is_frame_g_complete_shot(struct fimc_is_framemgr *this,
	struct fimc_is_frame **item)
{
	int ret = 0;

	if (item) {
		if (this->frame_com_cnt) {
			*item = container_of(this->frame_complete_head.next,
				struct fimc_is_frame, list);
			list_del(&(*item)->list);
			this->frame_com_cnt--;

			(*item)->state = FIMC_IS_FRAME_STATE_INVALID;
		} else {
			*item = NULL;
		}
	} else {
		ret = -EFAULT;
		err("item is null ptr\n");
	}

	return ret;
}

void fimc_is_frame_complete_head(struct fimc_is_framemgr *this,
	struct fimc_is_frame **item)
{
	if (this->frame_com_cnt)
		*item = container_of(this->frame_complete_head.next,
			struct fimc_is_frame, list);
	else
		*item = NULL;
}

void fimc_is_frame_print_complete_list(struct fimc_is_framemgr *this)
{
	struct list_head *temp;
	struct fimc_is_frame *shot;

	if (!(TRACE_ID & this->id))
		return;

	printk(KERN_CONT "[FRM] com(%d, %d) :",
		this->id, this->frame_com_cnt);

	list_for_each(temp, &this->frame_complete_head) {
		shot = list_entry(temp, struct fimc_is_frame, list);
		printk(KERN_CONT "%d->", shot->index);
	}

	printk(KERN_CONT "X\n");
}

int fimc_is_frame_trans_fre_to_req(struct fimc_is_framemgr *this,
	struct fimc_is_frame *item)
{
	int ret = 0;

	if (!this->frame_fre_cnt) {
		err("shot free count is zero\n");
		ret = -EFAULT;
		goto exit;
	}

	list_del(&item->list);
	this->frame_fre_cnt--;

	fimc_is_frame_s_request_shot(this, item);

exit:
	return ret;
}

int fimc_is_frame_trans_req_to_pro(struct fimc_is_framemgr *this,
	struct fimc_is_frame *item)
{
	int ret = 0;

	if (!this->frame_req_cnt) {
		err("shot request count is zero\n");
		ret = -EFAULT;
		goto exit;
	}

	list_del(&item->list);
	this->frame_req_cnt--;

	fimc_is_frame_s_process_shot(this, item);

exit:
	return ret;
}

int fimc_is_frame_trans_req_to_com(struct fimc_is_framemgr *this,
	struct fimc_is_frame *item)
{
	int ret = 0;

	if (!this->frame_req_cnt) {
		err("shot request count is zero\n");
		ret = -EFAULT;
		goto exit;
	}

	list_del(&item->list);
	this->frame_req_cnt--;

	fimc_is_frame_s_complete_shot(this, item);

exit:
	return ret;
}

int fimc_is_frame_trans_req_to_fre(struct fimc_is_framemgr *this,
	struct fimc_is_frame *item)
{
	int ret = 0;

	if (!this->frame_req_cnt) {
		err("shot request count is zero\n");
		ret = -EFAULT;
		goto exit;
	}

	list_del(&item->list);
	this->frame_req_cnt--;

	fimc_is_frame_s_free_shot(this, item);

exit:
	return ret;
}

int fimc_is_frame_trans_pro_to_com(struct fimc_is_framemgr *this,
	struct fimc_is_frame *item)
{
	int ret = 0;

	if (!this->frame_pro_cnt) {
		err("shot process count is zero\n");
		ret = -EFAULT;
		goto exit;
	}

	list_del(&item->list);
	this->frame_pro_cnt--;

	fimc_is_frame_s_complete_shot(this, item);

exit:
	return ret;
}

int fimc_is_frame_trans_pro_to_fre(struct fimc_is_framemgr *this,
	struct fimc_is_frame *item)
{
	int ret = 0;

	if (!this->frame_pro_cnt) {
		err("shot process count is zero\n");
		ret = -EFAULT;
		goto exit;
	}

	list_del(&item->list);
	this->frame_pro_cnt--;

	fimc_is_frame_s_free_shot(this, item);

exit:
	return ret;
}

int fimc_is_frame_trans_fre_to_com(struct fimc_is_framemgr *this,
	struct fimc_is_frame *item)
{
	int ret = 0;

	if (!this->frame_fre_cnt) {
		err("shot free count is zero\n");
		ret = -EFAULT;
		goto exit;
	}

	list_del(&item->list);
	this->frame_fre_cnt--;

	fimc_is_frame_s_complete_shot(this, item);

exit:
	return ret;
}

int fimc_is_frame_trans_com_to_fre(struct fimc_is_framemgr *this,
	struct fimc_is_frame *item)
{
	int ret = 0;

	if (!this->frame_com_cnt) {
		err("shot complete count is zero\n");
		ret = -EFAULT;
		goto exit;
	}

	list_del(&item->list);
	this->frame_com_cnt--;

	fimc_is_frame_s_free_shot(this, item);

exit:
	return ret;
}

int fimc_is_frame_open(struct fimc_is_framemgr *this, u32 id, u32 buffers)
{
	int ret = 0;
	u32 i, j;

	spin_lock_init(&this->slock);

	INIT_LIST_HEAD(&this->frame_free_head);
	INIT_LIST_HEAD(&this->frame_request_head);
	INIT_LIST_HEAD(&this->frame_process_head);
	INIT_LIST_HEAD(&this->frame_complete_head);

	this->id = id;
	this->frame_cnt = buffers;
	this->frame_fre_cnt = 0;
	this->frame_req_cnt = 0;
	this->frame_pro_cnt = 0;
	this->frame_com_cnt = 0;

	for (i = 0; i < buffers; ++i) {
		this->frame[i].init = FRAME_UNI_MEM;
		this->frame[i].work_data1 = NULL;
		this->frame[i].work_data2 = NULL;
		this->frame[i].index = i;
		this->frame[i].fcount = 0;
		this->frame[i].rcount = 0;
		this->frame[i].req_flag = 0;
		this->frame[i].out_flag = 0;

		this->frame[i].vb = NULL;
		this->frame[i].shot = NULL;
		this->frame[i].shot_ext = NULL;
		this->frame[i].shot_size = 0;

		this->frame[i].stream = NULL;
		this->frame[i].stream_size = 0;

		this->frame[i].planes = 0;
		for (j = 0; j < FIMC_IS_MAX_PLANES; ++j) {
			this->frame[i].kvaddr_buffer[j] = 0;
			this->frame[i].dvaddr_buffer[j] = 0;
		}

		this->frame[i].kvaddr_shot = 0;
		this->frame[i].dvaddr_shot = 0;
		fimc_is_frame_s_free_shot(this, &this->frame[i]);
	}

	return ret;
}

int fimc_is_frame_close(struct fimc_is_framemgr *this)
{
	int ret = 0;
	u32 buffers;
	u32 i, j;

	buffers = this->frame_cnt;

	for (i = 0; i < buffers; ++i) {
		this->frame[i].init = FRAME_UNI_MEM;
		this->frame[i].index = i;
		this->frame[i].fcount = 0;
		this->frame[i].rcount = 0;
		this->frame[i].req_flag = 0;

		this->frame[i].vb = NULL;
		this->frame[i].shot = NULL;
		this->frame[i].shot_ext = NULL;
		this->frame[i].shot_size = 0;

		this->frame[i].stream = NULL;
		this->frame[i].stream_size = 0;

		this->frame[i].planes = 0;
		for (j = 0; j < FIMC_IS_MAX_PLANES; ++j) {
			this->frame[i].kvaddr_buffer[j] = 0;
			this->frame[i].dvaddr_buffer[j] = 0;
		}

		this->frame[i].kvaddr_shot = 0;
		this->frame[i].dvaddr_shot = 0;
	}

	return ret;
}

void fimc_is_frame_print_all(struct fimc_is_framemgr *this)
{
	fimc_is_frame_print_free_list(this);
	fimc_is_frame_print_request_list(this);
	fimc_is_frame_print_process_list(this);
	fimc_is_frame_print_complete_list(this);
}
