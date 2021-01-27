// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Zorro Liu <zorro.liu@rock-chips.com>
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/semaphore.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/wait.h>

#include "../ebc_dev.h"
#include "buf_manage.h"
#include "buf_list.h"

struct buf_info_s {
	int buf_total_num;
	unsigned long phy_mem_base;
	char *virt_mem_base;

	struct buf_list_s *buf_list; /* buffer list. */
	int use_buf_is_empty;

	struct buf_list_s *dsp_buf_list; /* dispplay buffer list. */
	int dsp_buf_list_status;
	struct ebc_buf_s *osd_buf;

	struct mutex dsp_lock;
};

static struct buf_info_s ebc_buf_info;
static DECLARE_WAIT_QUEUE_HEAD(ebc_buf_wq);

int ebc_buf_release(struct ebc_buf_s  *release_buf)
{
	struct ebc_buf_s *temp_buf = release_buf;

	if (temp_buf) {
		if (temp_buf->status == buf_osd) {
			kfree(temp_buf);
		} else {
			temp_buf->status = buf_idle;
			if (1 == ebc_buf_info.use_buf_is_empty) {
				ebc_buf_info.use_buf_is_empty = 0;
				wake_up_interruptible_sync(&ebc_buf_wq);
			}
		}
	}

	return BUF_SUCCESS;
}

int ebc_remove_from_dsp_buf_list(struct ebc_buf_s *remove_buf)
{
	mutex_lock(&ebc_buf_info.dsp_lock);
	if (ebc_buf_info.dsp_buf_list) {
		int pos;

		pos = buf_list_get_pos(ebc_buf_info.dsp_buf_list, (int *)remove_buf);
		buf_list_remove(ebc_buf_info.dsp_buf_list, pos);
	}
	mutex_unlock(&ebc_buf_info.dsp_lock);

	return BUF_SUCCESS;
}

int ebc_add_to_dsp_buf_list(struct ebc_buf_s *dsp_buf)
{
	struct ebc_buf_s *temp_buf;
	int temp_pos;
	int is_full_mode = 0;

	mutex_lock(&ebc_buf_info.dsp_lock);
	if (ebc_buf_info.dsp_buf_list) {
		switch (dsp_buf->buf_mode) {
		case EPD_DU:
		case EPD_SUSPEND:
		case EPD_RESUME:
		case EPD_POWER_OFF:
		case EPD_OVERLAY:
		case EPD_RESET:
			break;

		default:
			if (ebc_buf_info.dsp_buf_list->nb_elt > 1) {
				temp_pos = ebc_buf_info.dsp_buf_list->nb_elt;
				while (--temp_pos) {
					temp_buf = (struct ebc_buf_s *)buf_list_get(ebc_buf_info.dsp_buf_list, temp_pos);
					if ((temp_buf->buf_mode != EPD_FULL_GC16) &&
					    (temp_buf->buf_mode != EPD_FULL_GL16) &&
					    (temp_buf->buf_mode != EPD_FULL_GLR16) &&
					    (temp_buf->buf_mode != EPD_FULL_GLD16) &&
					    (temp_buf->buf_mode != EPD_FULL_GCC16) &&
					    (temp_buf->buf_mode != EPD_OVERLAY) &&
					    (temp_buf->buf_mode != EPD_DU) &&
					    (temp_buf->buf_mode != EPD_SUSPEND) &&
					    (temp_buf->buf_mode != EPD_RESUME) &&
					    (temp_buf->buf_mode != EPD_POWER_OFF)) {
						buf_list_remove(ebc_buf_info.dsp_buf_list, temp_pos);
						ebc_buf_release(temp_buf);
					} else if ((1 == is_full_mode) &&
						   (temp_buf->buf_mode != EPD_DU) &&
						   (temp_buf->buf_mode != EPD_OVERLAY) &&
						   (temp_buf->buf_mode != EPD_SUSPEND) &&
						   (temp_buf->buf_mode != EPD_RESUME) &&
						   (temp_buf->buf_mode != EPD_POWER_OFF)) {
						buf_list_remove(ebc_buf_info.dsp_buf_list, temp_pos);
						ebc_buf_release(temp_buf);
					} else {
						is_full_mode = 1;
					}
				}
			}
			break;
		}

		dsp_buf->status = buf_dsp;
		if (-1 == buf_list_add(ebc_buf_info.dsp_buf_list, (int *)dsp_buf, -1)) {
			mutex_unlock(&ebc_buf_info.dsp_lock);
			return BUF_ERROR;
		}
	}
	mutex_unlock(&ebc_buf_info.dsp_lock);

	return BUF_SUCCESS;
}

int ebc_get_dsp_list_enum_num(void)
{
	return ebc_buf_info.dsp_buf_list->nb_elt;
}

struct ebc_buf_s *ebc_find_buf_by_phy_addr(unsigned long phy_addr)
{
	struct ebc_buf_s *temp_buf;
	int temp_pos;

	if (ebc_buf_info.buf_list) {
		temp_pos = 0;
		while (temp_pos < ebc_buf_info.buf_list->nb_elt) {
			temp_buf = (struct ebc_buf_s *)buf_list_get(ebc_buf_info.buf_list, temp_pos++);
			if (temp_buf && (temp_buf->phy_addr == phy_addr))
				return temp_buf;
		}
	}

	return NULL;
}

struct ebc_buf_s *ebc_dsp_buf_get(void)
{
	struct ebc_buf_s *buf = NULL;

	mutex_lock(&ebc_buf_info.dsp_lock);
	if (ebc_buf_info.dsp_buf_list && (ebc_buf_info.dsp_buf_list->nb_elt > 0))
		buf = (struct ebc_buf_s *)buf_list_get(ebc_buf_info.dsp_buf_list, 0);
	mutex_unlock(&ebc_buf_info.dsp_lock);

	return buf;
}

struct ebc_buf_s *ebc_osd_buf_get(void)
{
	if (ebc_buf_info.osd_buf)
		return ebc_buf_info.osd_buf;
	return NULL;
}

struct ebc_buf_s *ebc_osd_buf_clone(void)
{
	struct ebc_buf_s *temp_buf;

	temp_buf = kzalloc(sizeof(*temp_buf), GFP_KERNEL);
	if (NULL == temp_buf)
		return NULL;

	temp_buf->virt_addr = ebc_buf_info.osd_buf->virt_addr;
	temp_buf->phy_addr = ebc_buf_info.osd_buf->phy_addr;
	temp_buf->status = buf_osd;

	return temp_buf;
}

struct ebc_buf_s *ebc_empty_buf_get(void)
{
	struct ebc_buf_s *temp_buf;
	int temp_pos;

	if (ebc_buf_info.buf_list) {
		temp_pos = 0;

		while (temp_pos < ebc_buf_info.buf_list->nb_elt) {
			temp_buf = (struct ebc_buf_s *)buf_list_get(ebc_buf_info.buf_list, temp_pos++);
			if (temp_buf) {
				if (temp_buf->status == buf_idle) {
					temp_buf->status = buf_user;
					memcpy(temp_buf->tid_name, current->comm, TASK_COMM_LEN); //store user thread name
					return temp_buf;
				}
				// one tid only can get one buf at one time
				else if ((temp_buf->status == buf_user) && (!strncmp(temp_buf->tid_name, current->comm, TASK_COMM_LEN - 7))) {
					return temp_buf;
				}
			}
		}
		ebc_buf_info.use_buf_is_empty = 1;

		wait_event_interruptible(ebc_buf_wq, ebc_buf_info.use_buf_is_empty != 1);

		return ebc_empty_buf_get();
	}

	return NULL;
}

unsigned long ebc_phy_buf_base_get(void)
{
	return ebc_buf_info.phy_mem_base;
}

char *ebc_virt_buf_base_get(void)
{
	return ebc_buf_info.virt_mem_base;
}

int ebc_buf_uninit(void)
{
	struct ebc_buf_s *temp_buf;
	int pos;

	ebc_buf_info.buf_total_num = 0;
	if (ebc_buf_info.buf_list) {
		pos = ebc_buf_info.buf_list->nb_elt - 1;
		while (pos >= 0) {
			temp_buf = (struct ebc_buf_s *)buf_list_get(ebc_buf_info.buf_list, pos);
			if (temp_buf)
				kfree(temp_buf);
			buf_list_remove(ebc_buf_info.buf_list, pos);
			pos--;
		}
	}

	return BUF_SUCCESS;
}

int ebc_buf_init(unsigned long phy_start, char *mem_start, int men_len, int dest_buf_len, int max_buf_num)
{
	int res;
	int use_len;
	char *temp_addr;
	struct ebc_buf_s *temp_buf;

	if (max_buf_num < 0)
		return BUF_ERROR;

	if (NULL == mem_start)
		return BUF_ERROR;

	mutex_init(&ebc_buf_info.dsp_lock);

	if (buf_list_init(&ebc_buf_info.buf_list, BUF_LIST_MAX_NUMBER))
		return BUF_ERROR;

	if (buf_list_init(&ebc_buf_info.dsp_buf_list, BUF_LIST_MAX_NUMBER)) {
		res = BUF_ERROR;
		goto buf_list_err;
	}

	ebc_buf_info.buf_total_num = 0;
	use_len = 0;

	temp_addr = mem_start;
	ebc_buf_info.virt_mem_base = mem_start;
	ebc_buf_info.phy_mem_base = phy_start;
	use_len += dest_buf_len;
	while (use_len <= men_len) {
		temp_buf = kzalloc(sizeof(*temp_buf), GFP_KERNEL);
		if (NULL == temp_buf) {
			res = BUF_ERROR;
			goto exit;
		}
		temp_buf->virt_addr = temp_addr;
		temp_buf->phy_addr = phy_start;
		temp_buf->len = dest_buf_len;
		temp_buf->status = buf_idle;

		if (-1 == buf_list_add(ebc_buf_info.buf_list, (int *)temp_buf, -1)) {
			res = BUF_ERROR;
			goto exit;
		}
		ebc_buf_info.use_buf_is_empty = 0;

		temp_addr += dest_buf_len;
		phy_start += dest_buf_len;
		use_len += dest_buf_len;

		if (ebc_buf_info.buf_list->nb_elt == max_buf_num)
			break;
	}

	ebc_buf_info.buf_total_num = ebc_buf_info.buf_list->nb_elt;
	if (use_len <= men_len) {
		temp_buf = kzalloc(sizeof(*temp_buf), GFP_KERNEL);
		if (NULL == temp_buf) {
			res = BUF_ERROR;
			goto exit;
		}
		temp_buf->virt_addr = temp_addr;
		temp_buf->phy_addr = phy_start;
		temp_buf->len = dest_buf_len;
		temp_buf->status = buf_osd;
		ebc_buf_info.osd_buf = temp_buf;
	}

	return BUF_SUCCESS;
exit:
	ebc_buf_uninit();
	buf_list_uninit(ebc_buf_info.dsp_buf_list);
buf_list_err:
	buf_list_uninit(ebc_buf_info.buf_list);

	return res;
}
