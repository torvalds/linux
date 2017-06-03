/*
 * Support for Intel Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */
#ifdef CONFIG_COMPAT
#include <linux/compat.h>

#include <linux/videodev2.h>

#include "atomisp_internal.h"
#include "atomisp_compat.h"
#include "atomisp_compat_ioctl32.h"

static int get_atomisp_histogram32(struct atomisp_histogram *kp,
					struct atomisp_histogram32 __user *up)
{
	compat_uptr_t tmp;

	if (!access_ok(VERIFY_READ, up, sizeof(struct atomisp_histogram32)) ||
		get_user(kp->num_elements, &up->num_elements) ||
		get_user(tmp, &up->data))
			return -EFAULT;

	kp->data = compat_ptr(tmp);
	return 0;
}

static int put_atomisp_histogram32(struct atomisp_histogram *kp,
					struct atomisp_histogram32 __user *up)
{
	compat_uptr_t tmp = (compat_uptr_t)((uintptr_t)kp->data);

	if (!access_ok(VERIFY_WRITE, up, sizeof(struct atomisp_histogram32)) ||
		put_user(kp->num_elements, &up->num_elements) ||
		put_user(tmp, &up->data))
			return -EFAULT;

	return 0;
}

static inline int get_v4l2_pix_format(struct v4l2_pix_format *kp,
					struct v4l2_pix_format __user *up)
{
	if (copy_from_user(kp, up, sizeof(struct v4l2_pix_format)))
		return -EFAULT;
	return 0;
}

static inline int put_v4l2_pix_format(struct v4l2_pix_format *kp,
					struct v4l2_pix_format __user *up)
{
	if (copy_to_user(up, kp, sizeof(struct v4l2_pix_format)))
		return -EFAULT;
	return 0;
}

static int get_v4l2_framebuffer32(struct v4l2_framebuffer *kp,
					struct v4l2_framebuffer32 __user *up)
{
	compat_uptr_t tmp;

	if (!access_ok(VERIFY_READ, up, sizeof(struct v4l2_framebuffer32)) ||
		get_user(tmp, &up->base) ||
		get_user(kp->capability, &up->capability) ||
		get_user(kp->flags, &up->flags))
			return -EFAULT;

	kp->base = compat_ptr(tmp);
	get_v4l2_pix_format((struct v4l2_pix_format *)&kp->fmt, &up->fmt);
	return 0;
}

static int get_atomisp_dis_statistics32(struct atomisp_dis_statistics *kp,
				struct atomisp_dis_statistics32 __user *up)
{
	compat_uptr_t hor_prod_odd_real;
	compat_uptr_t hor_prod_odd_imag;
	compat_uptr_t hor_prod_even_real;
	compat_uptr_t hor_prod_even_imag;
	compat_uptr_t ver_prod_odd_real;
	compat_uptr_t ver_prod_odd_imag;
	compat_uptr_t ver_prod_even_real;
	compat_uptr_t ver_prod_even_imag;

	if (!access_ok(VERIFY_READ, up,
			sizeof(struct atomisp_dis_statistics32)) ||
		copy_from_user(kp, up, sizeof(struct atomisp_dvs_grid_info)) ||
		get_user(hor_prod_odd_real,
				&up->dvs2_stat.hor_prod.odd_real) ||
		get_user(hor_prod_odd_imag,
				&up->dvs2_stat.hor_prod.odd_imag) ||
		get_user(hor_prod_even_real,
				&up->dvs2_stat.hor_prod.even_real) ||
		get_user(hor_prod_even_imag,
				&up->dvs2_stat.hor_prod.even_imag) ||
		get_user(ver_prod_odd_real,
				&up->dvs2_stat.ver_prod.odd_real) ||
		get_user(ver_prod_odd_imag,
				&up->dvs2_stat.ver_prod.odd_imag) ||
		get_user(ver_prod_even_real,
				&up->dvs2_stat.ver_prod.even_real) ||
		get_user(ver_prod_even_imag,
				&up->dvs2_stat.ver_prod.even_imag) ||
		get_user(kp->exp_id, &up->exp_id))
			return -EFAULT;

	kp->dvs2_stat.hor_prod.odd_real = compat_ptr(hor_prod_odd_real);
	kp->dvs2_stat.hor_prod.odd_imag = compat_ptr(hor_prod_odd_imag);
	kp->dvs2_stat.hor_prod.even_real = compat_ptr(hor_prod_even_real);
	kp->dvs2_stat.hor_prod.even_imag = compat_ptr(hor_prod_even_imag);
	kp->dvs2_stat.ver_prod.odd_real = compat_ptr(ver_prod_odd_real);
	kp->dvs2_stat.ver_prod.odd_imag = compat_ptr(ver_prod_odd_imag);
	kp->dvs2_stat.ver_prod.even_real = compat_ptr(ver_prod_even_real);
	kp->dvs2_stat.ver_prod.even_imag = compat_ptr(ver_prod_even_imag);
	return 0;
}

static int put_atomisp_dis_statistics32(struct atomisp_dis_statistics *kp,
				struct atomisp_dis_statistics32 __user *up)
{
	compat_uptr_t hor_prod_odd_real =
		(compat_uptr_t)((uintptr_t)kp->dvs2_stat.hor_prod.odd_real);
	compat_uptr_t hor_prod_odd_imag =
		(compat_uptr_t)((uintptr_t)kp->dvs2_stat.hor_prod.odd_imag);
	compat_uptr_t hor_prod_even_real =
		(compat_uptr_t)((uintptr_t)kp->dvs2_stat.hor_prod.even_real);
	compat_uptr_t hor_prod_even_imag =
		(compat_uptr_t)((uintptr_t)kp->dvs2_stat.hor_prod.even_imag);
	compat_uptr_t ver_prod_odd_real =
		(compat_uptr_t)((uintptr_t)kp->dvs2_stat.ver_prod.odd_real);
	compat_uptr_t ver_prod_odd_imag =
		(compat_uptr_t)((uintptr_t)kp->dvs2_stat.ver_prod.odd_imag);
	compat_uptr_t ver_prod_even_real =
		(compat_uptr_t)((uintptr_t)kp->dvs2_stat.ver_prod.even_real);
	compat_uptr_t ver_prod_even_imag =
		(compat_uptr_t)((uintptr_t)kp->dvs2_stat.ver_prod.even_imag);

	if (!access_ok(VERIFY_WRITE, up,
			sizeof(struct atomisp_dis_statistics32)) ||
		copy_to_user(up, kp, sizeof(struct atomisp_dvs_grid_info)) ||
		put_user(hor_prod_odd_real,
				&up->dvs2_stat.hor_prod.odd_real) ||
		put_user(hor_prod_odd_imag,
				&up->dvs2_stat.hor_prod.odd_imag) ||
		put_user(hor_prod_even_real,
				&up->dvs2_stat.hor_prod.even_real) ||
		put_user(hor_prod_even_imag,
				&up->dvs2_stat.hor_prod.even_imag) ||
		put_user(ver_prod_odd_real,
				&up->dvs2_stat.ver_prod.odd_real) ||
		put_user(ver_prod_odd_imag,
				&up->dvs2_stat.ver_prod.odd_imag) ||
		put_user(ver_prod_even_real,
				&up->dvs2_stat.ver_prod.even_real) ||
		put_user(ver_prod_even_imag,
				&up->dvs2_stat.ver_prod.even_imag) ||
		put_user(kp->exp_id, &up->exp_id))
			return -EFAULT;

	return 0;
}

static int get_atomisp_dis_coefficients32(struct atomisp_dis_coefficients *kp,
				struct atomisp_dis_coefficients32 __user *up)
{
	compat_uptr_t hor_coefs_odd_real;
	compat_uptr_t hor_coefs_odd_imag;
	compat_uptr_t hor_coefs_even_real;
	compat_uptr_t hor_coefs_even_imag;
	compat_uptr_t ver_coefs_odd_real;
	compat_uptr_t ver_coefs_odd_imag;
	compat_uptr_t ver_coefs_even_real;
	compat_uptr_t ver_coefs_even_imag;

	if (!access_ok(VERIFY_READ, up,
			sizeof(struct atomisp_dis_coefficients32)) ||
		copy_from_user(kp, up, sizeof(struct atomisp_dvs_grid_info)) ||
		get_user(hor_coefs_odd_real, &up->hor_coefs.odd_real) ||
		get_user(hor_coefs_odd_imag, &up->hor_coefs.odd_imag) ||
		get_user(hor_coefs_even_real, &up->hor_coefs.even_real) ||
		get_user(hor_coefs_even_imag, &up->hor_coefs.even_imag) ||
		get_user(ver_coefs_odd_real, &up->ver_coefs.odd_real) ||
		get_user(ver_coefs_odd_imag, &up->ver_coefs.odd_imag) ||
		get_user(ver_coefs_even_real, &up->ver_coefs.even_real) ||
		get_user(ver_coefs_even_imag, &up->ver_coefs.even_imag))
			return -EFAULT;

	kp->hor_coefs.odd_real = compat_ptr(hor_coefs_odd_real);
	kp->hor_coefs.odd_imag = compat_ptr(hor_coefs_odd_imag);
	kp->hor_coefs.even_real = compat_ptr(hor_coefs_even_real);
	kp->hor_coefs.even_imag = compat_ptr(hor_coefs_even_imag);
	kp->ver_coefs.odd_real = compat_ptr(ver_coefs_odd_real);
	kp->ver_coefs.odd_imag = compat_ptr(ver_coefs_odd_imag);
	kp->ver_coefs.even_real = compat_ptr(ver_coefs_even_real);
	kp->ver_coefs.even_imag = compat_ptr(ver_coefs_even_imag);
	return 0;
}

static int get_atomisp_dvs_6axis_config32(struct atomisp_dvs_6axis_config *kp,
				struct atomisp_dvs_6axis_config32 __user *up)
{	compat_uptr_t xcoords_y;
	compat_uptr_t ycoords_y;
	compat_uptr_t xcoords_uv;
	compat_uptr_t ycoords_uv;

	if (!access_ok(VERIFY_READ, up,
			sizeof(struct atomisp_dvs_6axis_config32)) ||
		get_user(kp->exp_id, &up->exp_id) ||
		get_user(kp->width_y, &up->width_y) ||
		get_user(kp->height_y, &up->height_y) ||
		get_user(kp->width_uv, &up->width_uv) ||
		get_user(kp->height_uv, &up->height_uv) ||
		get_user(xcoords_y, &up->xcoords_y) ||
		get_user(ycoords_y, &up->ycoords_y) ||
		get_user(xcoords_uv, &up->xcoords_uv) ||
		get_user(ycoords_uv, &up->ycoords_uv))
			return -EFAULT;

	kp->xcoords_y = compat_ptr(xcoords_y);
	kp->ycoords_y = compat_ptr(ycoords_y);
	kp->xcoords_uv = compat_ptr(xcoords_uv);
	kp->ycoords_uv = compat_ptr(ycoords_uv);
	return 0;
}

static int get_atomisp_3a_statistics32(struct atomisp_3a_statistics *kp,
				struct atomisp_3a_statistics32 __user *up)
{
	compat_uptr_t data;
	compat_uptr_t rgby_data;

	if (!access_ok(VERIFY_READ, up,
			sizeof(struct atomisp_3a_statistics32)) ||
		copy_from_user(kp, up, sizeof(struct atomisp_grid_info)) ||
		get_user(rgby_data, &up->rgby_data) ||
		get_user(data, &up->data) ||
		get_user(kp->exp_id, &up->exp_id) ||
		get_user(kp->isp_config_id, &up->isp_config_id))
			return -EFAULT;

	kp->data = compat_ptr(data);
	kp->rgby_data = compat_ptr(rgby_data);

	return 0;
}

static int put_atomisp_3a_statistics32(struct atomisp_3a_statistics *kp,
				struct atomisp_3a_statistics32 __user *up)
{
	compat_uptr_t data = (compat_uptr_t)((uintptr_t)kp->data);
	compat_uptr_t rgby_data = (compat_uptr_t)((uintptr_t)kp->rgby_data);

	if (!access_ok(VERIFY_WRITE, up,
			sizeof(struct atomisp_3a_statistics32)) ||
		copy_to_user(up, kp, sizeof(struct atomisp_grid_info)) ||
		put_user(rgby_data, &up->rgby_data) ||
		put_user(data, &up->data) ||
		put_user(kp->exp_id, &up->exp_id) ||
		put_user(kp->isp_config_id, &up->isp_config_id))
			return -EFAULT;

	return 0;
}


static int get_atomisp_metadata_stat32(struct atomisp_metadata *kp,
				struct atomisp_metadata32 __user *up)
{
	compat_uptr_t data;
	compat_uptr_t effective_width;

	if (!access_ok(VERIFY_READ, up,
			sizeof(struct atomisp_metadata32)) ||
		get_user(data, &up->data) ||
		get_user(kp->width, &up->width) ||
		get_user(kp->height, &up->height) ||
		get_user(kp->stride, &up->stride) ||
		get_user(kp->exp_id, &up->exp_id) ||
		get_user(effective_width, &up->effective_width))
			return -EFAULT;

	kp->data = compat_ptr(data);
	kp->effective_width = compat_ptr(effective_width);
	return 0;
}


static int put_atomisp_metadata_stat32(struct atomisp_metadata *kp,
				struct atomisp_metadata32 __user *up)
{
	compat_uptr_t data = (compat_uptr_t)((uintptr_t)kp->data);
	compat_uptr_t effective_width =
		(compat_uptr_t)((uintptr_t)kp->effective_width);
	if (!access_ok(VERIFY_WRITE, up,
			sizeof(struct atomisp_metadata32)) ||
		put_user(data, &up->data) ||
		put_user(kp->width, &up->width) ||
		put_user(kp->height, &up->height) ||
		put_user(kp->stride, &up->stride) ||
		put_user(kp->exp_id, &up->exp_id) ||
		put_user(effective_width, &up->effective_width))
			return -EFAULT;

	return 0;
}

static int put_atomisp_metadata_by_type_stat32(
				struct atomisp_metadata_with_type *kp,
				struct atomisp_metadata_with_type32 __user *up)
{
	compat_uptr_t data = (compat_uptr_t)((uintptr_t)kp->data);
	compat_uptr_t effective_width =
		(compat_uptr_t)((uintptr_t)kp->effective_width);
	if (!access_ok(VERIFY_WRITE, up,
			sizeof(struct atomisp_metadata_with_type32)) ||
		put_user(data, &up->data) ||
		put_user(kp->width, &up->width) ||
		put_user(kp->height, &up->height) ||
		put_user(kp->stride, &up->stride) ||
		put_user(kp->exp_id, &up->exp_id) ||
		put_user(effective_width, &up->effective_width) ||
		put_user(kp->type, &up->type))
			return -EFAULT;

	return 0;
}

static int get_atomisp_metadata_by_type_stat32(
				struct atomisp_metadata_with_type *kp,
				struct atomisp_metadata_with_type32 __user *up)
{
	compat_uptr_t data;
	compat_uptr_t effective_width;

	if (!access_ok(VERIFY_READ, up,
			sizeof(struct atomisp_metadata_with_type32)) ||
		get_user(data, &up->data) ||
		get_user(kp->width, &up->width) ||
		get_user(kp->height, &up->height) ||
		get_user(kp->stride, &up->stride) ||
		get_user(kp->exp_id, &up->exp_id) ||
		get_user(effective_width, &up->effective_width) ||
		get_user(kp->type, &up->type))
			return -EFAULT;

	kp->data = compat_ptr(data);
	kp->effective_width = compat_ptr(effective_width);
	return 0;
}

static int get_atomisp_morph_table32(struct atomisp_morph_table *kp,
				struct atomisp_morph_table32 __user *up)
{
	unsigned int n = ATOMISP_MORPH_TABLE_NUM_PLANES;

	if (!access_ok(VERIFY_READ, up,
			sizeof(struct atomisp_morph_table32)) ||
		get_user(kp->enabled, &up->enabled) ||
		get_user(kp->width, &up->width) ||
		get_user(kp->height, &up->height))
			return -EFAULT;

	while (n-- > 0) {
		uintptr_t *coord_kp = (uintptr_t *)&kp->coordinates_x[n];

		if (get_user((*coord_kp), &up->coordinates_x[n]))
			return -EFAULT;

		coord_kp = (uintptr_t *)&kp->coordinates_y[n];
		if (get_user((*coord_kp), &up->coordinates_y[n]))
			return -EFAULT;
	}
	return 0;
}

static int put_atomisp_morph_table32(struct atomisp_morph_table *kp,
				struct atomisp_morph_table32 __user *up)
{
	unsigned int n = ATOMISP_MORPH_TABLE_NUM_PLANES;

	if (!access_ok(VERIFY_WRITE, up,
			sizeof(struct atomisp_morph_table32)) ||
		put_user(kp->enabled, &up->enabled) ||
		put_user(kp->width, &up->width) ||
		put_user(kp->height, &up->height))
			return -EFAULT;

	while (n-- > 0) {
		uintptr_t *coord_kp = (uintptr_t *)&kp->coordinates_x[n];

		if (put_user((*coord_kp), &up->coordinates_x[n]))
			return -EFAULT;

		coord_kp = (uintptr_t *)&kp->coordinates_y[n];
		if (put_user((*coord_kp), &up->coordinates_y[n]))
			return -EFAULT;
	}
	return 0;
}

static int get_atomisp_overlay32(struct atomisp_overlay *kp,
					struct atomisp_overlay32 __user *up)
{
	compat_uptr_t frame;
	if (!access_ok(VERIFY_READ, up, sizeof(struct atomisp_overlay32)) ||
		get_user(frame, &up->frame) ||
		get_user(kp->bg_y, &up->bg_y) ||
		get_user(kp->bg_u, &up->bg_u) ||
		get_user(kp->bg_v, &up->bg_v) ||
		get_user(kp->blend_input_perc_y, &up->blend_input_perc_y) ||
		get_user(kp->blend_input_perc_u, &up->blend_input_perc_u) ||
		get_user(kp->blend_input_perc_v, &up->blend_input_perc_v) ||
		get_user(kp->blend_overlay_perc_y,
				&up->blend_overlay_perc_y) ||
		get_user(kp->blend_overlay_perc_u,
				&up->blend_overlay_perc_u) ||
		get_user(kp->blend_overlay_perc_v,
				&up->blend_overlay_perc_v) ||
		get_user(kp->blend_overlay_perc_u,
				&up->blend_overlay_perc_u) ||
		get_user(kp->overlay_start_x, &up->overlay_start_y))
			return -EFAULT;

	kp->frame = compat_ptr(frame);
	return 0;
}

static int put_atomisp_overlay32(struct atomisp_overlay *kp,
					struct atomisp_overlay32 __user *up)
{
	compat_uptr_t frame = (compat_uptr_t)((uintptr_t)kp->frame);

	if (!access_ok(VERIFY_WRITE, up, sizeof(struct atomisp_overlay32)) ||
		put_user(frame, &up->frame) ||
		put_user(kp->bg_y, &up->bg_y) ||
		put_user(kp->bg_u, &up->bg_u) ||
		put_user(kp->bg_v, &up->bg_v) ||
		put_user(kp->blend_input_perc_y, &up->blend_input_perc_y) ||
		put_user(kp->blend_input_perc_u, &up->blend_input_perc_u) ||
		put_user(kp->blend_input_perc_v, &up->blend_input_perc_v) ||
		put_user(kp->blend_overlay_perc_y,
				&up->blend_overlay_perc_y) ||
		put_user(kp->blend_overlay_perc_u,
				&up->blend_overlay_perc_u) ||
		put_user(kp->blend_overlay_perc_v,
				&up->blend_overlay_perc_v) ||
		put_user(kp->blend_overlay_perc_u,
				&up->blend_overlay_perc_u) ||
		put_user(kp->overlay_start_x, &up->overlay_start_y))
			return -EFAULT;

	return 0;
}

static int get_atomisp_calibration_group32(
				struct atomisp_calibration_group *kp,
				struct atomisp_calibration_group32 __user *up)
{
	compat_uptr_t calb_grp_values;

	if (!access_ok(VERIFY_READ, up,
			sizeof(struct atomisp_calibration_group32)) ||
		get_user(kp->size, &up->size) ||
		get_user(kp->type, &up->type) ||
		get_user(calb_grp_values, &up->calb_grp_values))
			return -EFAULT;

	kp->calb_grp_values = compat_ptr(calb_grp_values);
	return 0;
}

static int put_atomisp_calibration_group32(
				struct atomisp_calibration_group *kp,
				struct atomisp_calibration_group32 __user *up)
{
	compat_uptr_t calb_grp_values =
			(compat_uptr_t)((uintptr_t)kp->calb_grp_values);

	if (!access_ok(VERIFY_WRITE, up,
			sizeof(struct atomisp_calibration_group32)) ||
		put_user(kp->size, &up->size) ||
		put_user(kp->type, &up->type) ||
		put_user(calb_grp_values, &up->calb_grp_values))
			return -EFAULT;

	return 0;
}

static int get_atomisp_acc_fw_load32(struct atomisp_acc_fw_load *kp,
				struct atomisp_acc_fw_load32 __user *up)
{
	compat_uptr_t data;

	if (!access_ok(VERIFY_READ, up,
			sizeof(struct atomisp_acc_fw_load32)) ||
		get_user(kp->size, &up->size) ||
		get_user(kp->fw_handle, &up->fw_handle) ||
		get_user(data, &up->data))
			return -EFAULT;

	kp->data = compat_ptr(data);
	return 0;
}

static int put_atomisp_acc_fw_load32(struct atomisp_acc_fw_load *kp,
				struct atomisp_acc_fw_load32 __user *up)
{
	compat_uptr_t data = (compat_uptr_t)((uintptr_t)kp->data);

	if (!access_ok(VERIFY_WRITE, up,
			sizeof(struct atomisp_acc_fw_load32)) ||
		put_user(kp->size, &up->size) ||
		put_user(kp->fw_handle, &up->fw_handle) ||
		put_user(data, &up->data))
			return -EFAULT;

	return 0;
}

static int get_atomisp_acc_fw_arg32(struct atomisp_acc_fw_arg *kp,
					struct atomisp_acc_fw_arg32 __user *up)
{
	compat_uptr_t value;

	if (!access_ok(VERIFY_READ, up, sizeof(struct atomisp_acc_fw_arg32)) ||
		get_user(kp->fw_handle, &up->fw_handle) ||
		get_user(kp->index, &up->index) ||
		get_user(value, &up->value) ||
		get_user(kp->size, &up->size))
			return -EFAULT;

	kp->value = compat_ptr(value);
	return 0;
}

static int put_atomisp_acc_fw_arg32(struct atomisp_acc_fw_arg *kp,
					struct atomisp_acc_fw_arg32 __user *up)
{
	compat_uptr_t value = (compat_uptr_t)((uintptr_t)kp->value);

	if (!access_ok(VERIFY_WRITE, up, sizeof(struct atomisp_acc_fw_arg32)) ||
		put_user(kp->fw_handle, &up->fw_handle) ||
		put_user(kp->index, &up->index) ||
		put_user(value, &up->value) ||
		put_user(kp->size, &up->size))
			return -EFAULT;

	return 0;
}

static int get_v4l2_private_int_data32(struct v4l2_private_int_data *kp,
					struct v4l2_private_int_data32 __user *up)
{
	compat_uptr_t data;

	if (!access_ok(VERIFY_READ, up,
			sizeof(struct v4l2_private_int_data32)) ||
		get_user(kp->size, &up->size) ||
		get_user(data, &up->data) ||
		get_user(kp->reserved[0], &up->reserved[0]) ||
		get_user(kp->reserved[1], &up->reserved[1]))
			return -EFAULT;

	kp->data = compat_ptr(data);
	return 0;
}

static int put_v4l2_private_int_data32(struct v4l2_private_int_data *kp,
				struct v4l2_private_int_data32 __user *up)
{
	compat_uptr_t data = (compat_uptr_t)((uintptr_t)kp->data);

	if (!access_ok(VERIFY_WRITE, up,
			sizeof(struct v4l2_private_int_data32)) ||
		put_user(kp->size, &up->size) ||
		put_user(data, &up->data) ||
		put_user(kp->reserved[0], &up->reserved[0]) ||
		put_user(kp->reserved[1], &up->reserved[1]))
			return -EFAULT;

	return 0;
}

static int get_atomisp_shading_table32(struct atomisp_shading_table *kp,
				struct atomisp_shading_table32 __user *up)
{
	unsigned int n = ATOMISP_NUM_SC_COLORS;

	if (!access_ok(VERIFY_READ, up,
			sizeof(struct atomisp_shading_table32)) ||
		get_user(kp->enable, &up->enable) ||
		get_user(kp->sensor_width, &up->sensor_width) ||
		get_user(kp->sensor_height, &up->sensor_height) ||
		get_user(kp->width, &up->width) ||
		get_user(kp->height, &up->height) ||
		get_user(kp->fraction_bits, &up->fraction_bits))
			return -EFAULT;

	while (n-- > 0) {
		uintptr_t *data_p = (uintptr_t *)&kp->data[n];

		if (get_user((*data_p), &up->data[n]))
			return -EFAULT;
	}
	return 0;
}

static int get_atomisp_acc_map32(struct atomisp_acc_map *kp,
					struct atomisp_acc_map32 __user *up)
{
	compat_uptr_t user_ptr;

	if (!access_ok(VERIFY_READ, up, sizeof(struct atomisp_acc_map32)) ||
		get_user(kp->flags, &up->flags) ||
		get_user(kp->length, &up->length) ||
		get_user(user_ptr, &up->user_ptr) ||
		get_user(kp->css_ptr, &up->css_ptr) ||
		get_user(kp->reserved[0], &up->reserved[0]) ||
		get_user(kp->reserved[1], &up->reserved[1]) ||
		get_user(kp->reserved[2], &up->reserved[2]) ||
		get_user(kp->reserved[3], &up->reserved[3]))
			return -EFAULT;

	kp->user_ptr = compat_ptr(user_ptr);
	return 0;
}

static int put_atomisp_acc_map32(struct atomisp_acc_map *kp,
					struct atomisp_acc_map32 __user *up)
{
	compat_uptr_t user_ptr = (compat_uptr_t)((uintptr_t)kp->user_ptr);

	if (!access_ok(VERIFY_WRITE, up, sizeof(struct atomisp_acc_map32)) ||
		put_user(kp->flags, &up->flags) ||
		put_user(kp->length, &up->length) ||
		put_user(user_ptr, &up->user_ptr) ||
		put_user(kp->css_ptr, &up->css_ptr) ||
		put_user(kp->reserved[0], &up->reserved[0]) ||
		put_user(kp->reserved[1], &up->reserved[1]) ||
		put_user(kp->reserved[2], &up->reserved[2]) ||
		put_user(kp->reserved[3], &up->reserved[3]))
			return -EFAULT;

	return 0;
}

static int get_atomisp_acc_s_mapped_arg32(struct atomisp_acc_s_mapped_arg *kp,
				struct atomisp_acc_s_mapped_arg32 __user *up)
{
	if (!access_ok(VERIFY_READ, up,
			sizeof(struct atomisp_acc_s_mapped_arg32)) ||
		get_user(kp->fw_handle, &up->fw_handle) ||
		get_user(kp->memory, &up->memory) ||
		get_user(kp->length, &up->length) ||
		get_user(kp->css_ptr, &up->css_ptr))
			return -EFAULT;

	return 0;
}

static int put_atomisp_acc_s_mapped_arg32(struct atomisp_acc_s_mapped_arg *kp,
				struct atomisp_acc_s_mapped_arg32 __user *up)
{
	if (!access_ok(VERIFY_WRITE, up,
			sizeof(struct atomisp_acc_s_mapped_arg32)) ||
		put_user(kp->fw_handle, &up->fw_handle) ||
		put_user(kp->memory, &up->memory) ||
		put_user(kp->length, &up->length) ||
		put_user(kp->css_ptr, &up->css_ptr))
			return -EFAULT;

	return 0;
}

static int get_atomisp_parameters32(struct atomisp_parameters *kp,
					struct atomisp_parameters32 __user *up)
{
	int n = offsetof(struct atomisp_parameters32, output_frame) /
				sizeof(compat_uptr_t);
	unsigned int size, offset = 0;
	void  __user *user_ptr;
#ifdef ISP2401
	unsigned int stp, mtp, dcp, dscp = 0;

#endif
	if (!access_ok(VERIFY_READ, up, sizeof(struct atomisp_parameters32)))
			return -EFAULT;

	while (n >= 0) {
		compat_uptr_t *src = (compat_uptr_t *)up + n;
		uintptr_t *dst = (uintptr_t *)kp + n;

		if (get_user((*dst), src))
			return -EFAULT;
		n--;
	}
	if (get_user(kp->isp_config_id, &up->isp_config_id) ||
#ifndef ISP2401
	    get_user(kp->per_frame_setting, &up->per_frame_setting))
#else
	    get_user(kp->per_frame_setting, &up->per_frame_setting) ||
	    get_user(stp, &up->shading_table) ||
	    get_user(mtp, &up->morph_table) ||
	    get_user(dcp, &up->dvs2_coefs) ||
	    get_user(dscp, &up->dvs_6axis_config))
#endif
		return -EFAULT;

	{
		union {
			struct atomisp_shading_table shading_table;
			struct atomisp_morph_table   morph_table;
			struct atomisp_dis_coefficients dvs2_coefs;
			struct atomisp_dvs_6axis_config dvs_6axis_config;
		} karg;

		size = sizeof(struct atomisp_shading_table) +
				sizeof(struct atomisp_morph_table) +
				sizeof(struct atomisp_dis_coefficients) +
				sizeof(struct atomisp_dvs_6axis_config);
		user_ptr = compat_alloc_user_space(size);

		/* handle shading table */
#ifndef ISP2401
		if (up->shading_table != 0) {
#else
		if (stp != 0) {
#endif
			if (get_atomisp_shading_table32(&karg.shading_table,
				(struct atomisp_shading_table32 __user *)
#ifndef ISP2401
						(uintptr_t)up->shading_table))
#else
						(uintptr_t)stp))
#endif
				return -EFAULT;

			kp->shading_table = user_ptr + offset;
			offset = sizeof(struct atomisp_shading_table);
			if (!kp->shading_table)
				return -EFAULT;

			if (copy_to_user(kp->shading_table,
					 &karg.shading_table,
					 sizeof(struct atomisp_shading_table)))
				return -EFAULT;
		}

		/* handle morph table */
#ifndef ISP2401
		if (up->morph_table != 0) {
#else
		if (mtp != 0) {
#endif
			if (get_atomisp_morph_table32(&karg.morph_table,
					(struct atomisp_morph_table32 __user *)
#ifndef ISP2401
						(uintptr_t)up->morph_table))
#else
						(uintptr_t)mtp))
#endif
				return -EFAULT;

			kp->morph_table = user_ptr + offset;
			offset += sizeof(struct atomisp_morph_table);
			if (!kp->morph_table)
				return -EFAULT;

			if (copy_to_user(kp->morph_table, &karg.morph_table,
					   sizeof(struct atomisp_morph_table)))
				return -EFAULT;
		}

		/* handle dvs2 coefficients */
#ifndef ISP2401
		if (up->dvs2_coefs != 0) {
#else
		if (dcp != 0) {
#endif
			if (get_atomisp_dis_coefficients32(&karg.dvs2_coefs,
				(struct atomisp_dis_coefficients32 __user *)
#ifndef ISP2401
						(uintptr_t)up->dvs2_coefs))
#else
						(uintptr_t)dcp))
#endif
				return -EFAULT;

			kp->dvs2_coefs = user_ptr + offset;
			offset += sizeof(struct atomisp_dis_coefficients);
			if (!kp->dvs2_coefs)
				return -EFAULT;

			if (copy_to_user(kp->dvs2_coefs, &karg.dvs2_coefs,
				sizeof(struct atomisp_dis_coefficients)))
				return -EFAULT;
		}
		/* handle dvs 6axis configuration */
#ifndef ISP2401
		if (up->dvs_6axis_config != 0) {
#else
		if (dscp != 0) {
#endif
			if (get_atomisp_dvs_6axis_config32(&karg.dvs_6axis_config,
				(struct atomisp_dvs_6axis_config32 __user *)
#ifndef ISP2401
						(uintptr_t)up->dvs_6axis_config))
#else
						(uintptr_t)dscp))
#endif
				return -EFAULT;

			kp->dvs_6axis_config = user_ptr + offset;
			offset += sizeof(struct atomisp_dvs_6axis_config);
			if (!kp->dvs_6axis_config)
				return -EFAULT;

			if (copy_to_user(kp->dvs_6axis_config, &karg.dvs_6axis_config,
				sizeof(struct atomisp_dvs_6axis_config)))
				return -EFAULT;
		}
	}
	return 0;
}

static int get_atomisp_acc_fw_load_to_pipe32(
			struct atomisp_acc_fw_load_to_pipe *kp,
			struct atomisp_acc_fw_load_to_pipe32 __user *up)
{
	compat_uptr_t data;
	if (!access_ok(VERIFY_READ, up,
			sizeof(struct atomisp_acc_fw_load_to_pipe32)) ||
		get_user(kp->flags, &up->flags) ||
		get_user(kp->fw_handle, &up->fw_handle) ||
		get_user(kp->size, &up->size) ||
		get_user(kp->type, &up->type) ||
		get_user(kp->reserved[0], &up->reserved[0]) ||
		get_user(kp->reserved[1], &up->reserved[1]) ||
		get_user(kp->reserved[2], &up->reserved[2]) ||
		get_user(data, &up->data))
			return -EFAULT;

	kp->data = compat_ptr(data);
	return 0;
}

static int put_atomisp_acc_fw_load_to_pipe32(
			struct atomisp_acc_fw_load_to_pipe *kp,
			struct atomisp_acc_fw_load_to_pipe32 __user *up)
{
	compat_uptr_t data = (compat_uptr_t)((uintptr_t)kp->data);
	if (!access_ok(VERIFY_WRITE, up,
			sizeof(struct atomisp_acc_fw_load_to_pipe32)) ||
		put_user(kp->flags, &up->flags) ||
		put_user(kp->fw_handle, &up->fw_handle) ||
		put_user(kp->size, &up->size) ||
		put_user(kp->type, &up->type) ||
		put_user(kp->reserved[0], &up->reserved[0]) ||
		put_user(kp->reserved[1], &up->reserved[1]) ||
		put_user(kp->reserved[2], &up->reserved[2]) ||
		put_user(data, &up->data))
			return -EFAULT;

	return 0;
}

static int get_atomisp_sensor_ae_bracketing_lut(
			struct atomisp_sensor_ae_bracketing_lut *kp,
			struct atomisp_sensor_ae_bracketing_lut32 __user *up)
{
	compat_uptr_t lut;
	if (!access_ok(VERIFY_READ, up,
			sizeof(struct atomisp_sensor_ae_bracketing_lut32)) ||
		get_user(kp->lut_size, &up->lut_size) ||
		get_user(lut, &up->lut))
			return -EFAULT;

	kp->lut = compat_ptr(lut);
	return 0;
}

static long native_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = -ENOIOCTLCMD;

	if (file->f_op->unlocked_ioctl)
		ret = file->f_op->unlocked_ioctl(file, cmd, arg);

	return ret;
}

long atomisp_do_compat_ioctl(struct file *file,
			    unsigned int cmd, unsigned long arg)
{
	union {
		struct atomisp_histogram his;
		struct atomisp_dis_statistics dis_s;
		struct atomisp_dis_coefficients dis_c;
		struct atomisp_dvs_6axis_config dvs_c;
		struct atomisp_3a_statistics s3a_s;
		struct atomisp_morph_table mor_t;
		struct v4l2_framebuffer v4l2_buf;
		struct atomisp_overlay overlay;
		struct atomisp_calibration_group cal_grp;
		struct atomisp_acc_fw_load acc_fw_load;
		struct atomisp_acc_fw_arg acc_fw_arg;
		struct v4l2_private_int_data v4l2_pri_data;
		struct atomisp_shading_table shd_tbl;
		struct atomisp_acc_map acc_map;
		struct atomisp_acc_s_mapped_arg acc_map_arg;
		struct atomisp_parameters param;
		struct atomisp_acc_fw_load_to_pipe acc_fw_to_pipe;
		struct atomisp_metadata md;
		struct atomisp_metadata_with_type md_with_type;
		struct atomisp_sensor_ae_bracketing_lut lut;
	} karg;
	mm_segment_t old_fs;
	void __user *up = compat_ptr(arg);
	long err = -ENOIOCTLCMD;

	/* First, convert the command. */
	switch (cmd) {
	case ATOMISP_IOC_G_HISTOGRAM32:
		cmd = ATOMISP_IOC_G_HISTOGRAM;
		break;
	case ATOMISP_IOC_S_HISTOGRAM32:
		cmd = ATOMISP_IOC_S_HISTOGRAM;
		break;
	case ATOMISP_IOC_G_DIS_STAT32:
		cmd = ATOMISP_IOC_G_DIS_STAT;
		break;
	case ATOMISP_IOC_S_DIS_COEFS32:
		cmd = ATOMISP_IOC_S_DIS_COEFS;
		break;
	case ATOMISP_IOC_S_DIS_VECTOR32:
		cmd = ATOMISP_IOC_S_DIS_VECTOR;
		break;
	case ATOMISP_IOC_G_3A_STAT32:
		cmd = ATOMISP_IOC_G_3A_STAT;
		break;
	case ATOMISP_IOC_G_ISP_GDC_TAB32:
		cmd = ATOMISP_IOC_G_ISP_GDC_TAB;
		break;
	case ATOMISP_IOC_S_ISP_GDC_TAB32:
		cmd = ATOMISP_IOC_S_ISP_GDC_TAB;
		break;
	case ATOMISP_IOC_S_ISP_FPN_TABLE32:
		cmd = ATOMISP_IOC_S_ISP_FPN_TABLE;
		break;
	case ATOMISP_IOC_G_ISP_OVERLAY32:
		cmd = ATOMISP_IOC_G_ISP_OVERLAY;
		break;
	case ATOMISP_IOC_S_ISP_OVERLAY32:
		cmd = ATOMISP_IOC_S_ISP_OVERLAY;
		break;
	case ATOMISP_IOC_G_SENSOR_CALIBRATION_GROUP32:
		cmd = ATOMISP_IOC_G_SENSOR_CALIBRATION_GROUP;
		break;
	case ATOMISP_IOC_ACC_LOAD32:
		cmd = ATOMISP_IOC_ACC_LOAD;
		break;
	case ATOMISP_IOC_ACC_S_ARG32:
		cmd = ATOMISP_IOC_ACC_S_ARG;
		break;
	case ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA32:
		cmd = ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA;
		break;
	case ATOMISP_IOC_S_ISP_SHD_TAB32:
		cmd = ATOMISP_IOC_S_ISP_SHD_TAB;
		break;
	case ATOMISP_IOC_ACC_DESTAB32:
		cmd = ATOMISP_IOC_ACC_DESTAB;
		break;
	case ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA32:
		cmd = ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA;
		break;
	case ATOMISP_IOC_ACC_MAP32:
		cmd = ATOMISP_IOC_ACC_MAP;
		break;
	case ATOMISP_IOC_ACC_UNMAP32:
		cmd = ATOMISP_IOC_ACC_UNMAP;
		break;
	case ATOMISP_IOC_ACC_S_MAPPED_ARG32:
		cmd = ATOMISP_IOC_ACC_S_MAPPED_ARG;
		break;
	case ATOMISP_IOC_S_PARAMETERS32:
		cmd = ATOMISP_IOC_S_PARAMETERS;
		break;
	case ATOMISP_IOC_ACC_LOAD_TO_PIPE32:
		cmd = ATOMISP_IOC_ACC_LOAD_TO_PIPE;
		break;
	case ATOMISP_IOC_G_METADATA32:
		cmd = ATOMISP_IOC_G_METADATA;
		break;
	case ATOMISP_IOC_G_METADATA_BY_TYPE32:
		cmd = ATOMISP_IOC_G_METADATA_BY_TYPE;
		break;
	case ATOMISP_IOC_S_SENSOR_AE_BRACKETING_LUT32:
		cmd = ATOMISP_IOC_S_SENSOR_AE_BRACKETING_LUT;
		break;
	}

	switch (cmd) {
	case ATOMISP_IOC_G_HISTOGRAM:
	case ATOMISP_IOC_S_HISTOGRAM:
		err = get_atomisp_histogram32(&karg.his, up);
		break;
	case ATOMISP_IOC_G_DIS_STAT:
		err = get_atomisp_dis_statistics32(&karg.dis_s, up);
		break;
	case ATOMISP_IOC_S_DIS_COEFS:
		err = get_atomisp_dis_coefficients32(&karg.dis_c, up);
		break;
	case ATOMISP_IOC_S_DIS_VECTOR:
		err = get_atomisp_dvs_6axis_config32(&karg.dvs_c, up);
		break;
	case ATOMISP_IOC_G_3A_STAT:
		err = get_atomisp_3a_statistics32(&karg.s3a_s, up);
		break;
	case ATOMISP_IOC_G_ISP_GDC_TAB:
	case ATOMISP_IOC_S_ISP_GDC_TAB:
		err = get_atomisp_morph_table32(&karg.mor_t, up);
		break;
	case ATOMISP_IOC_S_ISP_FPN_TABLE:
		err = get_v4l2_framebuffer32(&karg.v4l2_buf, up);
		break;
	case ATOMISP_IOC_G_ISP_OVERLAY:
	case ATOMISP_IOC_S_ISP_OVERLAY:
		err = get_atomisp_overlay32(&karg.overlay, up);
		break;
	case ATOMISP_IOC_G_SENSOR_CALIBRATION_GROUP:
		err = get_atomisp_calibration_group32(&karg.cal_grp, up);
		break;
	case ATOMISP_IOC_ACC_LOAD:
		err = get_atomisp_acc_fw_load32(&karg.acc_fw_load, up);
		break;
	case ATOMISP_IOC_ACC_S_ARG:
	case ATOMISP_IOC_ACC_DESTAB:
		err = get_atomisp_acc_fw_arg32(&karg.acc_fw_arg, up);
		break;
	case ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA:
	case ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA:
		err = get_v4l2_private_int_data32(&karg.v4l2_pri_data, up);
		break;
	case ATOMISP_IOC_S_ISP_SHD_TAB:
		err = get_atomisp_shading_table32(&karg.shd_tbl, up);
		break;
	case ATOMISP_IOC_ACC_MAP:
	case ATOMISP_IOC_ACC_UNMAP:
		err = get_atomisp_acc_map32(&karg.acc_map, up);
		break;
	case ATOMISP_IOC_ACC_S_MAPPED_ARG:
		err = get_atomisp_acc_s_mapped_arg32(&karg.acc_map_arg, up);
		break;
	case ATOMISP_IOC_S_PARAMETERS:
		err = get_atomisp_parameters32(&karg.param, up);
		break;
	case ATOMISP_IOC_ACC_LOAD_TO_PIPE:
		err = get_atomisp_acc_fw_load_to_pipe32(&karg.acc_fw_to_pipe,
							up);
		break;
	case ATOMISP_IOC_G_METADATA:
		err = get_atomisp_metadata_stat32(&karg.md, up);
		break;
	case ATOMISP_IOC_G_METADATA_BY_TYPE:
		err = get_atomisp_metadata_by_type_stat32(&karg.md_with_type,
							up);
		break;
	case ATOMISP_IOC_S_SENSOR_AE_BRACKETING_LUT:
		err = get_atomisp_sensor_ae_bracketing_lut(&karg.lut, up);
		break;
	}
	if (err)
		return err;

	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = native_ioctl(file, cmd, (unsigned long)&karg);
	set_fs(old_fs);
	if (err)
		return err;

	switch (cmd) {
	case ATOMISP_IOC_G_HISTOGRAM:
		err = put_atomisp_histogram32(&karg.his, up);
		break;
	case ATOMISP_IOC_G_DIS_STAT:
		err = put_atomisp_dis_statistics32(&karg.dis_s, up);
		break;
	case ATOMISP_IOC_G_3A_STAT:
		err = put_atomisp_3a_statistics32(&karg.s3a_s, up);
		break;
	case ATOMISP_IOC_G_ISP_GDC_TAB:
		err = put_atomisp_morph_table32(&karg.mor_t, up);
		break;
	case ATOMISP_IOC_G_ISP_OVERLAY:
		err = put_atomisp_overlay32(&karg.overlay, up);
		break;
	case ATOMISP_IOC_G_SENSOR_CALIBRATION_GROUP:
		err = put_atomisp_calibration_group32(&karg.cal_grp, up);
		break;
	case ATOMISP_IOC_ACC_LOAD:
		err = put_atomisp_acc_fw_load32(&karg.acc_fw_load, up);
		break;
	case ATOMISP_IOC_ACC_S_ARG:
	case ATOMISP_IOC_ACC_DESTAB:
		err = put_atomisp_acc_fw_arg32(&karg.acc_fw_arg, up);
		break;
	case ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA:
	case ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA:
		err = put_v4l2_private_int_data32(&karg.v4l2_pri_data, up);
		break;
	case ATOMISP_IOC_ACC_MAP:
	case ATOMISP_IOC_ACC_UNMAP:
		err = put_atomisp_acc_map32(&karg.acc_map, up);
		break;
	case ATOMISP_IOC_ACC_S_MAPPED_ARG:
		err = put_atomisp_acc_s_mapped_arg32(&karg.acc_map_arg, up);
		break;
	case ATOMISP_IOC_ACC_LOAD_TO_PIPE:
		err = put_atomisp_acc_fw_load_to_pipe32(&karg.acc_fw_to_pipe,
							up);
		break;
	case ATOMISP_IOC_G_METADATA:
		err = put_atomisp_metadata_stat32(&karg.md, up);
		break;
	case ATOMISP_IOC_G_METADATA_BY_TYPE:
		err = put_atomisp_metadata_by_type_stat32(&karg.md_with_type,
							up);
		break;
	}

	return err;
}

long atomisp_compat_ioctl32(struct file *file,
			    unsigned int cmd, unsigned long arg)
{

	struct video_device *vdev = video_devdata(file);
	struct atomisp_device *isp = video_get_drvdata(vdev);
	long ret = -ENOIOCTLCMD;

	if (!file->f_op->unlocked_ioctl)
		return ret;

	switch (cmd) {
	case ATOMISP_IOC_G_XNR:
	case ATOMISP_IOC_S_XNR:
	case ATOMISP_IOC_G_NR:
	case ATOMISP_IOC_S_NR:
	case ATOMISP_IOC_G_TNR:
	case ATOMISP_IOC_S_TNR:
	case ATOMISP_IOC_G_BLACK_LEVEL_COMP:
	case ATOMISP_IOC_S_BLACK_LEVEL_COMP:
	case ATOMISP_IOC_G_EE:
	case ATOMISP_IOC_S_EE:
	case ATOMISP_IOC_S_DIS_VECTOR:
	case ATOMISP_IOC_G_ISP_PARM:
	case ATOMISP_IOC_S_ISP_PARM:
	case ATOMISP_IOC_G_ISP_GAMMA:
	case ATOMISP_IOC_S_ISP_GAMMA:
	case ATOMISP_IOC_ISP_MAKERNOTE:
	case ATOMISP_IOC_G_ISP_MACC:
	case ATOMISP_IOC_S_ISP_MACC:
	case ATOMISP_IOC_G_ISP_BAD_PIXEL_DETECTION:
	case ATOMISP_IOC_S_ISP_BAD_PIXEL_DETECTION:
	case ATOMISP_IOC_G_ISP_FALSE_COLOR_CORRECTION:
	case ATOMISP_IOC_S_ISP_FALSE_COLOR_CORRECTION:
	case ATOMISP_IOC_G_ISP_CTC:
	case ATOMISP_IOC_S_ISP_CTC:
	case ATOMISP_IOC_G_ISP_WHITE_BALANCE:
	case ATOMISP_IOC_S_ISP_WHITE_BALANCE:
	case ATOMISP_IOC_CAMERA_BRIDGE:
	case ATOMISP_IOC_G_SENSOR_MODE_DATA:
	case ATOMISP_IOC_S_EXPOSURE:
	case ATOMISP_IOC_G_3A_CONFIG:
	case ATOMISP_IOC_S_3A_CONFIG:
	case ATOMISP_IOC_ACC_UNLOAD:
	case ATOMISP_IOC_ACC_START:
	case ATOMISP_IOC_ACC_WAIT:
	case ATOMISP_IOC_ACC_ABORT:
	case ATOMISP_IOC_G_ISP_GAMMA_CORRECTION:
	case ATOMISP_IOC_S_ISP_GAMMA_CORRECTION:
	case ATOMISP_IOC_S_CONT_CAPTURE_CONFIG:
	case ATOMISP_IOC_G_DVS2_BQ_RESOLUTIONS:
	case ATOMISP_IOC_EXT_ISP_CTRL:
	case ATOMISP_IOC_EXP_ID_UNLOCK:
	case ATOMISP_IOC_EXP_ID_CAPTURE:
	case ATOMISP_IOC_S_ENABLE_DZ_CAPT_PIPE:
	case ATOMISP_IOC_G_FORMATS_CONFIG:
	case ATOMISP_IOC_S_FORMATS_CONFIG:
	case ATOMISP_IOC_S_EXPOSURE_WINDOW:
	case ATOMISP_IOC_S_ACC_STATE:
	case ATOMISP_IOC_G_ACC_STATE:
	case ATOMISP_IOC_INJECT_A_FAKE_EVENT:
	case ATOMISP_IOC_G_SENSOR_AE_BRACKETING_INFO:
	case ATOMISP_IOC_S_SENSOR_AE_BRACKETING_MODE:
	case ATOMISP_IOC_G_SENSOR_AE_BRACKETING_MODE:
	case ATOMISP_IOC_G_INVALID_FRAME_NUM:
	case ATOMISP_IOC_S_ARRAY_RESOLUTION:
#ifdef ISP2401
	case ATOMISP_IOC_S_SENSOR_RUNMODE:
	case ATOMISP_IOC_G_UPDATE_EXPOSURE:
#endif
		ret = native_ioctl(file, cmd, arg);
		break;

	case ATOMISP_IOC_G_HISTOGRAM32:
	case ATOMISP_IOC_S_HISTOGRAM32:
	case ATOMISP_IOC_G_DIS_STAT32:
	case ATOMISP_IOC_S_DIS_COEFS32:
	case ATOMISP_IOC_S_DIS_VECTOR32:
	case ATOMISP_IOC_G_3A_STAT32:
	case ATOMISP_IOC_G_ISP_GDC_TAB32:
	case ATOMISP_IOC_S_ISP_GDC_TAB32:
	case ATOMISP_IOC_S_ISP_FPN_TABLE32:
	case ATOMISP_IOC_G_ISP_OVERLAY32:
	case ATOMISP_IOC_S_ISP_OVERLAY32:
	case ATOMISP_IOC_G_SENSOR_CALIBRATION_GROUP32:
	case ATOMISP_IOC_ACC_LOAD32:
	case ATOMISP_IOC_ACC_S_ARG32:
	case ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA32:
	case ATOMISP_IOC_S_ISP_SHD_TAB32:
	case ATOMISP_IOC_ACC_DESTAB32:
	case ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA32:
	case ATOMISP_IOC_ACC_MAP32:
	case ATOMISP_IOC_ACC_UNMAP32:
	case ATOMISP_IOC_ACC_S_MAPPED_ARG32:
	case ATOMISP_IOC_S_PARAMETERS32:
	case ATOMISP_IOC_ACC_LOAD_TO_PIPE32:
	case ATOMISP_IOC_G_METADATA32:
	case ATOMISP_IOC_G_METADATA_BY_TYPE32:
	case ATOMISP_IOC_S_SENSOR_AE_BRACKETING_LUT32:
		ret = atomisp_do_compat_ioctl(file, cmd, arg);
		break;

	default:
		dev_warn(isp->dev,
			"%s: unknown ioctl '%c', dir=%d, #%d (0x%08x)\n",
			__func__, _IOC_TYPE(cmd), _IOC_DIR(cmd), _IOC_NR(cmd),
			cmd);
		break;
	}
	return ret;
}
#endif /* CONFIG_COMPAT */
