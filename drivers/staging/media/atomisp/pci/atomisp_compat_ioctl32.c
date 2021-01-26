// SPDX-License-Identifier: GPL-2.0
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
 *
 */
#ifdef CONFIG_COMPAT
#include <linux/compat.h>

#include <linux/videodev2.h>

#include "atomisp_internal.h"
#include "atomisp_compat.h"
#include "atomisp_ioctl.h"
#include "atomisp_compat_ioctl32.h"

/* Macros borrowed from v4l2-compat-ioctl32.c */

#define get_user_cast(__x, __ptr)					\
({									\
	get_user(__x, (typeof(*__ptr) __user *)(__ptr));		\
})

#define put_user_force(__x, __ptr)					\
({									\
	put_user((typeof(*__x) __force *)(__x), __ptr);			\
})

/* Use the same argument order as copy_in_user */
#define assign_in_user(to, from)					\
({									\
	typeof(*from) __assign_tmp;					\
									\
	get_user_cast(__assign_tmp, from) || put_user(__assign_tmp, to);\
})

static int get_atomisp_histogram32(struct atomisp_histogram __user *kp,
				   struct atomisp_histogram32 __user *up)
{
	compat_uptr_t tmp;

	if (!access_ok(up, sizeof(struct atomisp_histogram32)) ||
	    assign_in_user(&kp->num_elements, &up->num_elements) ||
	    get_user(tmp, &up->data) ||
	    put_user(compat_ptr(tmp), &kp->data))
		return -EFAULT;

	return 0;
}

static int put_atomisp_histogram32(struct atomisp_histogram __user *kp,
				   struct atomisp_histogram32 __user *up)
{
	void __user *tmp;

	if (!access_ok(up, sizeof(struct atomisp_histogram32)) ||
	    assign_in_user(&up->num_elements, &kp->num_elements) ||
	    get_user(tmp, &kp->data) ||
	    put_user(ptr_to_compat(tmp), &up->data))
		return -EFAULT;

	return 0;
}

static int get_v4l2_framebuffer32(struct v4l2_framebuffer __user *kp,
				  struct v4l2_framebuffer32 __user *up)
{
	compat_uptr_t tmp;

	if (!access_ok(up, sizeof(struct v4l2_framebuffer32)) ||
	    get_user(tmp, &up->base) ||
	    put_user_force(compat_ptr(tmp), &kp->base) ||
	    assign_in_user(&kp->capability, &up->capability) ||
	    assign_in_user(&kp->flags, &up->flags) ||
	    copy_in_user(&kp->fmt, &up->fmt, sizeof(kp->fmt)))
		return -EFAULT;

	return 0;
}

static int get_atomisp_dis_statistics32(struct atomisp_dis_statistics __user *kp,
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

	if (!access_ok(up, sizeof(struct atomisp_dis_statistics32)) ||
	    copy_in_user(kp, up, sizeof(struct atomisp_dvs_grid_info)) ||
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
	    assign_in_user(&kp->exp_id, &up->exp_id) ||
	    put_user(compat_ptr(hor_prod_odd_real),
		     &kp->dvs2_stat.hor_prod.odd_real) ||
	    put_user(compat_ptr(hor_prod_odd_imag),
		     &kp->dvs2_stat.hor_prod.odd_imag) ||
	    put_user(compat_ptr(hor_prod_even_real),
		     &kp->dvs2_stat.hor_prod.even_real) ||
	    put_user(compat_ptr(hor_prod_even_imag),
		     &kp->dvs2_stat.hor_prod.even_imag) ||
	    put_user(compat_ptr(ver_prod_odd_real),
		     &kp->dvs2_stat.ver_prod.odd_real) ||
	    put_user(compat_ptr(ver_prod_odd_imag),
		     &kp->dvs2_stat.ver_prod.odd_imag) ||
	    put_user(compat_ptr(ver_prod_even_real),
		     &kp->dvs2_stat.ver_prod.even_real) ||
	    put_user(compat_ptr(ver_prod_even_imag),
		     &kp->dvs2_stat.ver_prod.even_imag))
		return -EFAULT;

	return 0;
}

static int put_atomisp_dis_statistics32(struct atomisp_dis_statistics __user *kp,
					struct atomisp_dis_statistics32 __user *up)
{
	void __user *hor_prod_odd_real;
	void __user *hor_prod_odd_imag;
	void __user *hor_prod_even_real;
	void __user *hor_prod_even_imag;
	void __user *ver_prod_odd_real;
	void __user *ver_prod_odd_imag;
	void __user *ver_prod_even_real;
	void __user *ver_prod_even_imag;

	if (!!access_ok(up, sizeof(struct atomisp_dis_statistics32)) ||
	    copy_in_user(up, kp, sizeof(struct atomisp_dvs_grid_info)) ||
	    get_user(hor_prod_odd_real,
		     &kp->dvs2_stat.hor_prod.odd_real) ||
	    get_user(hor_prod_odd_imag,
		     &kp->dvs2_stat.hor_prod.odd_imag) ||
	    get_user(hor_prod_even_real,
		     &kp->dvs2_stat.hor_prod.even_real) ||
	    get_user(hor_prod_even_imag,
		     &kp->dvs2_stat.hor_prod.even_imag) ||
	    get_user(ver_prod_odd_real,
		     &kp->dvs2_stat.ver_prod.odd_real) ||
	    get_user(ver_prod_odd_imag,
		     &kp->dvs2_stat.ver_prod.odd_imag) ||
	    get_user(ver_prod_even_real,
		     &kp->dvs2_stat.ver_prod.even_real) ||
	    get_user(ver_prod_even_imag,
		     &kp->dvs2_stat.ver_prod.even_imag) ||
	    put_user(ptr_to_compat(hor_prod_odd_real),
		     &up->dvs2_stat.hor_prod.odd_real) ||
	    put_user(ptr_to_compat(hor_prod_odd_imag),
		     &up->dvs2_stat.hor_prod.odd_imag) ||
	    put_user(ptr_to_compat(hor_prod_even_real),
		     &up->dvs2_stat.hor_prod.even_real) ||
	    put_user(ptr_to_compat(hor_prod_even_imag),
		     &up->dvs2_stat.hor_prod.even_imag) ||
	    put_user(ptr_to_compat(ver_prod_odd_real),
		     &up->dvs2_stat.ver_prod.odd_real) ||
	    put_user(ptr_to_compat(ver_prod_odd_imag),
		     &up->dvs2_stat.ver_prod.odd_imag) ||
	    put_user(ptr_to_compat(ver_prod_even_real),
		     &up->dvs2_stat.ver_prod.even_real) ||
	    put_user(ptr_to_compat(ver_prod_even_imag),
		     &up->dvs2_stat.ver_prod.even_imag) ||
	    assign_in_user(&up->exp_id, &kp->exp_id))
		return -EFAULT;

	return 0;
}

static int get_atomisp_dis_coefficients32(struct atomisp_dis_coefficients __user *kp,
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

	if (!access_ok(up, sizeof(struct atomisp_dis_coefficients32)) ||
	    copy_in_user(kp, up, sizeof(struct atomisp_dvs_grid_info)) ||
	    get_user(hor_coefs_odd_real, &up->hor_coefs.odd_real) ||
	    get_user(hor_coefs_odd_imag, &up->hor_coefs.odd_imag) ||
	    get_user(hor_coefs_even_real, &up->hor_coefs.even_real) ||
	    get_user(hor_coefs_even_imag, &up->hor_coefs.even_imag) ||
	    get_user(ver_coefs_odd_real, &up->ver_coefs.odd_real) ||
	    get_user(ver_coefs_odd_imag, &up->ver_coefs.odd_imag) ||
	    get_user(ver_coefs_even_real, &up->ver_coefs.even_real) ||
	    get_user(ver_coefs_even_imag, &up->ver_coefs.even_imag) ||
	    put_user(compat_ptr(hor_coefs_odd_real),
		     &kp->hor_coefs.odd_real) ||
	    put_user(compat_ptr(hor_coefs_odd_imag),
		     &kp->hor_coefs.odd_imag) ||
	    put_user(compat_ptr(hor_coefs_even_real),
		     &kp->hor_coefs.even_real) ||
	    put_user(compat_ptr(hor_coefs_even_imag),
		     &kp->hor_coefs.even_imag) ||
	    put_user(compat_ptr(ver_coefs_odd_real),
		     &kp->ver_coefs.odd_real) ||
	    put_user(compat_ptr(ver_coefs_odd_imag),
		     &kp->ver_coefs.odd_imag) ||
	    put_user(compat_ptr(ver_coefs_even_real),
		     &kp->ver_coefs.even_real) ||
	    put_user(compat_ptr(ver_coefs_even_imag),
		     &kp->ver_coefs.even_imag))
		return -EFAULT;

	return 0;
}

static int get_atomisp_dvs_6axis_config32(struct atomisp_dvs_6axis_config __user *kp,
					  struct atomisp_dvs_6axis_config32 __user *up)
{
	compat_uptr_t xcoords_y;
	compat_uptr_t ycoords_y;
	compat_uptr_t xcoords_uv;
	compat_uptr_t ycoords_uv;

	if (!access_ok(up, sizeof(struct atomisp_dvs_6axis_config32)) ||
	    assign_in_user(&kp->exp_id, &up->exp_id) ||
	    assign_in_user(&kp->width_y, &up->width_y) ||
	    assign_in_user(&kp->height_y, &up->height_y) ||
	    assign_in_user(&kp->width_uv, &up->width_uv) ||
	    assign_in_user(&kp->height_uv, &up->height_uv) ||
	    get_user(xcoords_y, &up->xcoords_y) ||
	    get_user(ycoords_y, &up->ycoords_y) ||
	    get_user(xcoords_uv, &up->xcoords_uv) ||
	    get_user(ycoords_uv, &up->ycoords_uv) ||
	    put_user_force(compat_ptr(xcoords_y), &kp->xcoords_y) ||
	    put_user_force(compat_ptr(ycoords_y), &kp->ycoords_y) ||
	    put_user_force(compat_ptr(xcoords_uv), &kp->xcoords_uv) ||
	    put_user_force(compat_ptr(ycoords_uv), &kp->ycoords_uv))
		return -EFAULT;

	return 0;
}

static int get_atomisp_3a_statistics32(struct atomisp_3a_statistics __user *kp,
				       struct atomisp_3a_statistics32 __user *up)
{
	compat_uptr_t data;
	compat_uptr_t rgby_data;

	if (!access_ok(up, sizeof(struct atomisp_3a_statistics32)) ||
	    copy_in_user(kp, up, sizeof(struct atomisp_grid_info)) ||
	    get_user(rgby_data, &up->rgby_data) ||
	    put_user(compat_ptr(rgby_data), &kp->rgby_data) ||
	    get_user(data, &up->data) ||
	    put_user(compat_ptr(data), &kp->data) ||
	    assign_in_user(&kp->exp_id, &up->exp_id) ||
	    assign_in_user(&kp->isp_config_id, &up->isp_config_id))
		return -EFAULT;

	return 0;
}

static int put_atomisp_3a_statistics32(struct atomisp_3a_statistics __user *kp,
				       struct atomisp_3a_statistics32 __user *up)
{
	void __user *data;
	void __user *rgby_data;

	if (!access_ok(up, sizeof(struct atomisp_3a_statistics32)) ||
	    copy_in_user(up, kp, sizeof(struct atomisp_grid_info)) ||
	    get_user(rgby_data, &kp->rgby_data) ||
	    put_user(ptr_to_compat(rgby_data), &up->rgby_data) ||
	    get_user(data, &kp->data) ||
	    put_user(ptr_to_compat(data), &up->data) ||
	    assign_in_user(&up->exp_id, &kp->exp_id) ||
	    assign_in_user(&up->isp_config_id, &kp->isp_config_id))
		return -EFAULT;

	return 0;
}

static int get_atomisp_metadata_stat32(struct atomisp_metadata __user *kp,
				       struct atomisp_metadata32 __user *up)
{
	compat_uptr_t data;
	compat_uptr_t effective_width;

	if (!access_ok(up, sizeof(struct atomisp_metadata32)) ||
	    get_user(data, &up->data) ||
	    put_user(compat_ptr(data), &kp->data) ||
	    assign_in_user(&kp->width, &up->width) ||
	    assign_in_user(&kp->height, &up->height) ||
	    assign_in_user(&kp->stride, &up->stride) ||
	    assign_in_user(&kp->exp_id, &up->exp_id) ||
	    get_user(effective_width, &up->effective_width) ||
	    put_user_force(compat_ptr(effective_width), &kp->effective_width))
		return -EFAULT;

	return 0;
}

static int put_atomisp_metadata_stat32(struct atomisp_metadata __user *kp,
				struct atomisp_metadata32 __user *up)
{
	void __user *data;
	void *effective_width;

	if (!access_ok(up, sizeof(struct atomisp_metadata32)) ||
	    get_user(data, &kp->data) ||
	    put_user(ptr_to_compat(data), &up->data) ||
	    assign_in_user(&up->width, &kp->width) ||
	    assign_in_user(&up->height, &kp->height) ||
	    assign_in_user(&up->stride, &kp->stride) ||
	    assign_in_user(&up->exp_id, &kp->exp_id) ||
	    get_user(effective_width, &kp->effective_width) ||
	    put_user(ptr_to_compat((void __user *)effective_width),
				   &up->effective_width))
		return -EFAULT;

	return 0;
}

static int
put_atomisp_metadata_by_type_stat32(struct atomisp_metadata_with_type __user *kp,
				    struct atomisp_metadata_with_type32 __user *up)
{
	void __user *data;
	u32 *effective_width;

	if (!access_ok(up, sizeof(struct atomisp_metadata_with_type32)) ||
	    get_user(data, &kp->data) ||
	    put_user(ptr_to_compat(data), &up->data) ||
	    assign_in_user(&up->width, &kp->width) ||
	    assign_in_user(&up->height, &kp->height) ||
	    assign_in_user(&up->stride, &kp->stride) ||
	    assign_in_user(&up->exp_id, &kp->exp_id) ||
	    get_user(effective_width, &kp->effective_width) ||
	    put_user(ptr_to_compat((void __user *)effective_width),
		     &up->effective_width) ||
	    assign_in_user(&up->type, &kp->type))
		return -EFAULT;

	return 0;
}

static int
get_atomisp_metadata_by_type_stat32(struct atomisp_metadata_with_type __user *kp,
				    struct atomisp_metadata_with_type32 __user *up)
{
	compat_uptr_t data;
	compat_uptr_t effective_width;

	if (!access_ok(up, sizeof(struct atomisp_metadata_with_type32)) ||
	    get_user(data, &up->data) ||
	    put_user(compat_ptr(data), &kp->data) ||
	    assign_in_user(&kp->width, &up->width) ||
	    assign_in_user(&kp->height, &up->height) ||
	    assign_in_user(&kp->stride, &up->stride) ||
	    assign_in_user(&kp->exp_id, &up->exp_id) ||
	    get_user(effective_width, &up->effective_width) ||
	    put_user_force(compat_ptr(effective_width), &kp->effective_width) ||
	    assign_in_user(&kp->type, &up->type))
		return -EFAULT;

	return 0;
}

static int
get_atomisp_morph_table32(struct atomisp_morph_table __user *kp,
			  struct atomisp_morph_table32 __user *up)
{
	unsigned int n = ATOMISP_MORPH_TABLE_NUM_PLANES;

	if (!access_ok(up, sizeof(struct atomisp_morph_table32)) ||
		assign_in_user(&kp->enabled, &up->enabled) ||
		assign_in_user(&kp->width, &up->width) ||
		assign_in_user(&kp->height, &up->height))
			return -EFAULT;

	while (n-- > 0) {
		compat_uptr_t coord_kp;

		if (get_user(coord_kp, &up->coordinates_x[n]) ||
		    put_user(compat_ptr(coord_kp), &kp->coordinates_x[n]) ||
		    get_user(coord_kp, &up->coordinates_y[n]) ||
		    put_user(compat_ptr(coord_kp), &kp->coordinates_y[n]))
			return -EFAULT;
	}
	return 0;
}

static int put_atomisp_morph_table32(struct atomisp_morph_table __user *kp,
				     struct atomisp_morph_table32 __user *up)
{
	unsigned int n = ATOMISP_MORPH_TABLE_NUM_PLANES;

	if (!access_ok(up, sizeof(struct atomisp_morph_table32)) ||
		assign_in_user(&up->enabled, &kp->enabled) ||
		assign_in_user(&up->width, &kp->width) ||
		assign_in_user(&up->height, &kp->height))
			return -EFAULT;

	while (n-- > 0) {
		void __user *coord_kp;

		if (get_user(coord_kp, &kp->coordinates_x[n]) ||
		    put_user(ptr_to_compat(coord_kp), &up->coordinates_x[n]) ||
		    get_user(coord_kp, &kp->coordinates_y[n]) ||
		    put_user(ptr_to_compat(coord_kp), &up->coordinates_y[n]))
			return -EFAULT;
	}
	return 0;
}

static int get_atomisp_overlay32(struct atomisp_overlay __user *kp,
				 struct atomisp_overlay32 __user *up)
{
	compat_uptr_t frame;

	if (!access_ok(up, sizeof(struct atomisp_overlay32)) ||
	    get_user(frame, &up->frame) ||
	    put_user_force(compat_ptr(frame), &kp->frame) ||
	    assign_in_user(&kp->bg_y, &up->bg_y) ||
	    assign_in_user(&kp->bg_u, &up->bg_u) ||
	    assign_in_user(&kp->bg_v, &up->bg_v) ||
	    assign_in_user(&kp->blend_input_perc_y,
			   &up->blend_input_perc_y) ||
	    assign_in_user(&kp->blend_input_perc_u,
			   &up->blend_input_perc_u) ||
	    assign_in_user(&kp->blend_input_perc_v,
			   &up->blend_input_perc_v) ||
	    assign_in_user(&kp->blend_overlay_perc_y,
			   &up->blend_overlay_perc_y) ||
	    assign_in_user(&kp->blend_overlay_perc_u,
			   &up->blend_overlay_perc_u) ||
	    assign_in_user(&kp->blend_overlay_perc_v,
			   &up->blend_overlay_perc_v) ||
	    assign_in_user(&kp->overlay_start_x, &up->overlay_start_x) ||
	    assign_in_user(&kp->overlay_start_y, &up->overlay_start_y))
		return -EFAULT;

	return 0;
}

static int put_atomisp_overlay32(struct atomisp_overlay __user *kp,
				 struct atomisp_overlay32 __user *up)
{
	void *frame;

	if (!access_ok(up, sizeof(struct atomisp_overlay32)) ||
	    get_user(frame, &kp->frame) ||
	    put_user(ptr_to_compat((void __user *)frame), &up->frame) ||
	    assign_in_user(&up->bg_y, &kp->bg_y) ||
	    assign_in_user(&up->bg_u, &kp->bg_u) ||
	    assign_in_user(&up->bg_v, &kp->bg_v) ||
	    assign_in_user(&up->blend_input_perc_y,
			   &kp->blend_input_perc_y) ||
	    assign_in_user(&up->blend_input_perc_u,
			   &kp->blend_input_perc_u) ||
	    assign_in_user(&up->blend_input_perc_v,
			   &kp->blend_input_perc_v) ||
	    assign_in_user(&up->blend_overlay_perc_y,
			   &kp->blend_overlay_perc_y) ||
	    assign_in_user(&up->blend_overlay_perc_u,
			   &kp->blend_overlay_perc_u) ||
	    assign_in_user(&up->blend_overlay_perc_v,
			   &kp->blend_overlay_perc_v) ||
	    assign_in_user(&up->overlay_start_x, &kp->overlay_start_x) ||
	    assign_in_user(&up->overlay_start_y, &kp->overlay_start_y))
		return -EFAULT;

	return 0;
}

static int
get_atomisp_calibration_group32(struct atomisp_calibration_group __user *kp,
				struct atomisp_calibration_group32 __user *up)
{
	compat_uptr_t calb_grp_values;

	if (!access_ok(up, sizeof(struct atomisp_calibration_group32)) ||
	    assign_in_user(&kp->size, &up->size) ||
	    assign_in_user(&kp->type, &up->type) ||
	    get_user(calb_grp_values, &up->calb_grp_values) ||
	    put_user_force(compat_ptr(calb_grp_values), &kp->calb_grp_values))
		return -EFAULT;

	return 0;
}

static int
put_atomisp_calibration_group32(struct atomisp_calibration_group __user *kp,
				struct atomisp_calibration_group32 __user *up)
{
	void *calb_grp_values;

	if (!access_ok(up, sizeof(struct atomisp_calibration_group32)) ||
	    assign_in_user(&up->size, &kp->size) ||
	    assign_in_user(&up->type, &kp->type) ||
	    get_user(calb_grp_values, &kp->calb_grp_values) ||
	    put_user(ptr_to_compat((void __user *)calb_grp_values),
		     &up->calb_grp_values))
		return -EFAULT;

	return 0;
}

static int get_atomisp_acc_fw_load32(struct atomisp_acc_fw_load __user *kp,
				     struct atomisp_acc_fw_load32 __user *up)
{
	compat_uptr_t data;

	if (!access_ok(up, sizeof(struct atomisp_acc_fw_load32)) ||
	    assign_in_user(&kp->size, &up->size) ||
	    assign_in_user(&kp->fw_handle, &up->fw_handle) ||
	    get_user_cast(data, &up->data) ||
	    put_user(compat_ptr(data), &kp->data))
		return -EFAULT;

	return 0;
}

static int put_atomisp_acc_fw_load32(struct atomisp_acc_fw_load __user *kp,
				     struct atomisp_acc_fw_load32 __user *up)
{
	void __user *data;

	if (!access_ok(up, sizeof(struct atomisp_acc_fw_load32)) ||
	    assign_in_user(&up->size, &kp->size) ||
	    assign_in_user(&up->fw_handle, &kp->fw_handle) ||
	    get_user(data, &kp->data) ||
	    put_user(ptr_to_compat(data), &up->data))
		return -EFAULT;

	return 0;
}

static int get_atomisp_acc_fw_arg32(struct atomisp_acc_fw_arg __user *kp,
				    struct atomisp_acc_fw_arg32 __user *up)
{
	compat_uptr_t value;

	if (!access_ok(up, sizeof(struct atomisp_acc_fw_arg32)) ||
	    assign_in_user(&kp->fw_handle, &up->fw_handle) ||
	    assign_in_user(&kp->index, &up->index) ||
	    get_user(value, &up->value) ||
	    put_user(compat_ptr(value), &kp->value) ||
	    assign_in_user(&kp->size, &up->size))
		return -EFAULT;

	return 0;
}

static int put_atomisp_acc_fw_arg32(struct atomisp_acc_fw_arg __user *kp,
				    struct atomisp_acc_fw_arg32 __user *up)
{
	void __user *value;

	if (!access_ok(up, sizeof(struct atomisp_acc_fw_arg32)) ||
	    assign_in_user(&up->fw_handle, &kp->fw_handle) ||
	    assign_in_user(&up->index, &kp->index) ||
	    get_user(value, &kp->value) ||
	    put_user(ptr_to_compat(value), &up->value) ||
	    assign_in_user(&up->size, &kp->size))
		return -EFAULT;

	return 0;
}

static int get_v4l2_private_int_data32(struct v4l2_private_int_data __user *kp,
				       struct v4l2_private_int_data32 __user *up)
{
	compat_uptr_t data;

	if (!access_ok(up, sizeof(struct v4l2_private_int_data32)) ||
	    assign_in_user(&kp->size, &up->size) ||
	    get_user(data, &up->data) ||
	    put_user(compat_ptr(data), &kp->data) ||
	    assign_in_user(&kp->reserved[0], &up->reserved[0]) ||
	    assign_in_user(&kp->reserved[1], &up->reserved[1]))
		return -EFAULT;

	return 0;
}

static int put_v4l2_private_int_data32(struct v4l2_private_int_data __user *kp,
				       struct v4l2_private_int_data32 __user *up)
{
	void __user *data;

	if (!access_ok(up, sizeof(struct v4l2_private_int_data32)) ||
	    assign_in_user(&up->size, &kp->size) ||
	    get_user(data, &kp->data) ||
	    put_user(ptr_to_compat(data), &up->data) ||
	    assign_in_user(&up->reserved[0], &kp->reserved[0]) ||
	    assign_in_user(&up->reserved[1], &kp->reserved[1]))
		return -EFAULT;

	return 0;
}

static int get_atomisp_shading_table32(struct atomisp_shading_table __user *kp,
				       struct atomisp_shading_table32 __user *up)
{
	unsigned int n = ATOMISP_NUM_SC_COLORS;

	if (!access_ok(up, sizeof(struct atomisp_shading_table32)) ||
	    assign_in_user(&kp->enable, &up->enable) ||
	    assign_in_user(&kp->sensor_width, &up->sensor_width) ||
	    assign_in_user(&kp->sensor_height, &up->sensor_height) ||
	    assign_in_user(&kp->width, &up->width) ||
	    assign_in_user(&kp->height, &up->height) ||
	    assign_in_user(&kp->fraction_bits, &up->fraction_bits))
		return -EFAULT;

	while (n-- > 0) {
		compat_uptr_t tmp;

		if (get_user(tmp, &up->data[n]) ||
		    put_user_force(compat_ptr(tmp), &kp->data[n]))
			return -EFAULT;
	}
	return 0;
}

static int get_atomisp_acc_map32(struct atomisp_acc_map __user *kp,
				 struct atomisp_acc_map32 __user *up)
{
	compat_uptr_t user_ptr;

	if (!access_ok(up, sizeof(struct atomisp_acc_map32)) ||
	    assign_in_user(&kp->flags, &up->flags) ||
	    assign_in_user(&kp->length, &up->length) ||
	    get_user(user_ptr, &up->user_ptr) ||
	    put_user(compat_ptr(user_ptr), &kp->user_ptr) ||
	    assign_in_user(&kp->css_ptr, &up->css_ptr) ||
	    assign_in_user(&kp->reserved[0], &up->reserved[0]) ||
	    assign_in_user(&kp->reserved[1], &up->reserved[1]) ||
	    assign_in_user(&kp->reserved[2], &up->reserved[2]) ||
	    assign_in_user(&kp->reserved[3], &up->reserved[3]))
		return -EFAULT;

	return 0;
}

static int put_atomisp_acc_map32(struct atomisp_acc_map __user *kp,
				 struct atomisp_acc_map32 __user *up)
{
	void __user *user_ptr;

	if (!access_ok(up, sizeof(struct atomisp_acc_map32)) ||
	    assign_in_user(&up->flags, &kp->flags) ||
	    assign_in_user(&up->length, &kp->length) ||
	    get_user(user_ptr, &kp->user_ptr) ||
	    put_user(ptr_to_compat(user_ptr), &up->user_ptr) ||
	    assign_in_user(&up->css_ptr, &kp->css_ptr) ||
	    assign_in_user(&up->reserved[0], &kp->reserved[0]) ||
	    assign_in_user(&up->reserved[1], &kp->reserved[1]) ||
	    assign_in_user(&up->reserved[2], &kp->reserved[2]) ||
	    assign_in_user(&up->reserved[3], &kp->reserved[3]))
		return -EFAULT;

	return 0;
}

static int
get_atomisp_acc_s_mapped_arg32(struct atomisp_acc_s_mapped_arg __user *kp,
			       struct atomisp_acc_s_mapped_arg32 __user *up)
{
	if (!access_ok(up, sizeof(struct atomisp_acc_s_mapped_arg32)) ||
	    assign_in_user(&kp->fw_handle, &up->fw_handle) ||
	    assign_in_user(&kp->memory, &up->memory) ||
	    assign_in_user(&kp->length, &up->length) ||
	    assign_in_user(&kp->css_ptr, &up->css_ptr))
		return -EFAULT;

	return 0;
}

static int
put_atomisp_acc_s_mapped_arg32(struct atomisp_acc_s_mapped_arg __user *kp,
			       struct atomisp_acc_s_mapped_arg32 __user *up)
{
	if (!access_ok(up, sizeof(struct atomisp_acc_s_mapped_arg32)) ||
	    assign_in_user(&up->fw_handle, &kp->fw_handle) ||
	    assign_in_user(&up->memory, &kp->memory) ||
	    assign_in_user(&up->length, &kp->length) ||
	    assign_in_user(&up->css_ptr, &kp->css_ptr))
		return -EFAULT;

	return 0;
}

static int get_atomisp_parameters32(struct atomisp_parameters __user *kp,
				    struct atomisp_parameters32 __user *up)
{
	int n = offsetof(struct atomisp_parameters32, output_frame) /
		sizeof(compat_uptr_t);
	compat_uptr_t stp, mtp, dcp, dscp;
	struct {
		struct atomisp_shading_table shading_table;
		struct atomisp_morph_table morph_table;
		struct atomisp_dis_coefficients dvs2_coefs;
		struct atomisp_dvs_6axis_config dvs_6axis_config;
	} __user *karg = (void __user *)(kp + 1);

	if (!access_ok(up, sizeof(struct atomisp_parameters32)))
		return -EFAULT;

	while (n >= 0) {
		compat_uptr_t __user *src = (compat_uptr_t __user *)up + n;
		void * __user *dst = (void * __user *)kp + n;
		compat_uptr_t tmp;

		if (get_user_cast(tmp, src) || put_user_force(compat_ptr(tmp), dst))
			return -EFAULT;
		n--;
	}

	if (assign_in_user(&kp->isp_config_id, &up->isp_config_id) ||
	    assign_in_user(&kp->per_frame_setting, &up->per_frame_setting) ||
	    get_user(stp, &up->shading_table) ||
	    get_user(mtp, &up->morph_table) ||
	    get_user(dcp, &up->dvs2_coefs) ||
	    get_user(dscp, &up->dvs_6axis_config))
		return -EFAULT;

	/* handle shading table */
	if (stp && (get_atomisp_shading_table32(&karg->shading_table,
						compat_ptr(stp)) ||
		    put_user_force(&karg->shading_table, &kp->shading_table)))
		return -EFAULT;

	/* handle morph table */
	if (mtp && (get_atomisp_morph_table32(&karg->morph_table,
					      compat_ptr(mtp)) ||
		    put_user_force(&karg->morph_table, &kp->morph_table)))
		return -EFAULT;

	/* handle dvs2 coefficients */
	if (dcp && (get_atomisp_dis_coefficients32(&karg->dvs2_coefs,
						   compat_ptr(dcp)) ||
		    put_user_force(&karg->dvs2_coefs, &kp->dvs2_coefs)))
		return -EFAULT;

	/* handle dvs 6axis configuration */
	if (dscp &&
	    (get_atomisp_dvs_6axis_config32(&karg->dvs_6axis_config,
					    compat_ptr(dscp)) ||
	     put_user_force(&karg->dvs_6axis_config, &kp->dvs_6axis_config)))
		return -EFAULT;

	return 0;
}

static int
get_atomisp_acc_fw_load_to_pipe32(struct atomisp_acc_fw_load_to_pipe __user *kp,
				  struct atomisp_acc_fw_load_to_pipe32 __user *up)
{
	compat_uptr_t data;

	if (!access_ok(up, sizeof(struct atomisp_acc_fw_load_to_pipe32)) ||
	    assign_in_user(&kp->flags, &up->flags) ||
	    assign_in_user(&kp->fw_handle, &up->fw_handle) ||
	    assign_in_user(&kp->size, &up->size) ||
	    assign_in_user(&kp->type, &up->type) ||
	    assign_in_user(&kp->reserved[0], &up->reserved[0]) ||
	    assign_in_user(&kp->reserved[1], &up->reserved[1]) ||
	    assign_in_user(&kp->reserved[2], &up->reserved[2]) ||
	    get_user(data, &up->data) ||
	    put_user(compat_ptr(data), &kp->data))
		return -EFAULT;

	return 0;
}

static int
put_atomisp_acc_fw_load_to_pipe32(struct atomisp_acc_fw_load_to_pipe __user *kp,
				  struct atomisp_acc_fw_load_to_pipe32 __user *up)
{
	void __user *data;

	if (!access_ok(up, sizeof(struct atomisp_acc_fw_load_to_pipe32)) ||
	    assign_in_user(&up->flags, &kp->flags) ||
	    assign_in_user(&up->fw_handle, &kp->fw_handle) ||
	    assign_in_user(&up->size, &kp->size) ||
	    assign_in_user(&up->type, &kp->type) ||
	    assign_in_user(&up->reserved[0], &kp->reserved[0]) ||
	    assign_in_user(&up->reserved[1], &kp->reserved[1]) ||
	    assign_in_user(&up->reserved[2], &kp->reserved[2]) ||
	    get_user(data, &kp->data) ||
	    put_user(ptr_to_compat(data), &up->data))
		return -EFAULT;

	return 0;
}

static int
get_atomisp_sensor_ae_bracketing_lut(struct atomisp_sensor_ae_bracketing_lut __user *kp,
				     struct atomisp_sensor_ae_bracketing_lut32 __user *up)
{
	compat_uptr_t lut;

	if (!access_ok(up, sizeof(struct atomisp_sensor_ae_bracketing_lut32)) ||
	    assign_in_user(&kp->lut_size, &up->lut_size) ||
	    get_user(lut, &up->lut) ||
	    put_user_force(compat_ptr(lut), &kp->lut))
		return -EFAULT;

	return 0;
}

static long native_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = -ENOIOCTLCMD;

	if (file->f_op->unlocked_ioctl)
		ret = file->f_op->unlocked_ioctl(file, cmd, arg);

	return ret;
}

static long atomisp_do_compat_ioctl(struct file *file,
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
	} __user *karg;
	void __user *up = compat_ptr(arg);
	long err = -ENOIOCTLCMD;

	karg = compat_alloc_user_space(
		sizeof(*karg) + (cmd == ATOMISP_IOC_S_PARAMETERS32 ?
				 sizeof(struct atomisp_shading_table) +
				 sizeof(struct atomisp_morph_table) +
				 sizeof(struct atomisp_dis_coefficients) +
				 sizeof(struct atomisp_dvs_6axis_config) : 0));
	if (!karg)
		return -ENOMEM;

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
		err = get_atomisp_histogram32(&karg->his, up);
		break;
	case ATOMISP_IOC_G_DIS_STAT:
		err = get_atomisp_dis_statistics32(&karg->dis_s, up);
		break;
	case ATOMISP_IOC_S_DIS_COEFS:
		err = get_atomisp_dis_coefficients32(&karg->dis_c, up);
		break;
	case ATOMISP_IOC_S_DIS_VECTOR:
		err = get_atomisp_dvs_6axis_config32(&karg->dvs_c, up);
		break;
	case ATOMISP_IOC_G_3A_STAT:
		err = get_atomisp_3a_statistics32(&karg->s3a_s, up);
		break;
	case ATOMISP_IOC_G_ISP_GDC_TAB:
	case ATOMISP_IOC_S_ISP_GDC_TAB:
		err = get_atomisp_morph_table32(&karg->mor_t, up);
		break;
	case ATOMISP_IOC_S_ISP_FPN_TABLE:
		err = get_v4l2_framebuffer32(&karg->v4l2_buf, up);
		break;
	case ATOMISP_IOC_G_ISP_OVERLAY:
	case ATOMISP_IOC_S_ISP_OVERLAY:
		err = get_atomisp_overlay32(&karg->overlay, up);
		break;
	case ATOMISP_IOC_G_SENSOR_CALIBRATION_GROUP:
		err = get_atomisp_calibration_group32(&karg->cal_grp, up);
		break;
	case ATOMISP_IOC_ACC_LOAD:
		err = get_atomisp_acc_fw_load32(&karg->acc_fw_load, up);
		break;
	case ATOMISP_IOC_ACC_S_ARG:
	case ATOMISP_IOC_ACC_DESTAB:
		err = get_atomisp_acc_fw_arg32(&karg->acc_fw_arg, up);
		break;
	case ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA:
	case ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA:
		err = get_v4l2_private_int_data32(&karg->v4l2_pri_data, up);
		break;
	case ATOMISP_IOC_S_ISP_SHD_TAB:
		err = get_atomisp_shading_table32(&karg->shd_tbl, up);
		break;
	case ATOMISP_IOC_ACC_MAP:
	case ATOMISP_IOC_ACC_UNMAP:
		err = get_atomisp_acc_map32(&karg->acc_map, up);
		break;
	case ATOMISP_IOC_ACC_S_MAPPED_ARG:
		err = get_atomisp_acc_s_mapped_arg32(&karg->acc_map_arg, up);
		break;
	case ATOMISP_IOC_S_PARAMETERS:
		err = get_atomisp_parameters32(&karg->param, up);
		break;
	case ATOMISP_IOC_ACC_LOAD_TO_PIPE:
		err = get_atomisp_acc_fw_load_to_pipe32(&karg->acc_fw_to_pipe,
							up);
		break;
	case ATOMISP_IOC_G_METADATA:
		err = get_atomisp_metadata_stat32(&karg->md, up);
		break;
	case ATOMISP_IOC_G_METADATA_BY_TYPE:
		err = get_atomisp_metadata_by_type_stat32(&karg->md_with_type,
							  up);
		break;
	case ATOMISP_IOC_S_SENSOR_AE_BRACKETING_LUT:
		err = get_atomisp_sensor_ae_bracketing_lut(&karg->lut, up);
		break;
	}
	if (err)
		return err;

	err = native_ioctl(file, cmd, (unsigned long)karg);
	if (err)
		return err;

	switch (cmd) {
	case ATOMISP_IOC_G_HISTOGRAM:
		err = put_atomisp_histogram32(&karg->his, up);
		break;
	case ATOMISP_IOC_G_DIS_STAT:
		err = put_atomisp_dis_statistics32(&karg->dis_s, up);
		break;
	case ATOMISP_IOC_G_3A_STAT:
		err = put_atomisp_3a_statistics32(&karg->s3a_s, up);
		break;
	case ATOMISP_IOC_G_ISP_GDC_TAB:
		err = put_atomisp_morph_table32(&karg->mor_t, up);
		break;
	case ATOMISP_IOC_G_ISP_OVERLAY:
		err = put_atomisp_overlay32(&karg->overlay, up);
		break;
	case ATOMISP_IOC_G_SENSOR_CALIBRATION_GROUP:
		err = put_atomisp_calibration_group32(&karg->cal_grp, up);
		break;
	case ATOMISP_IOC_ACC_LOAD:
		err = put_atomisp_acc_fw_load32(&karg->acc_fw_load, up);
		break;
	case ATOMISP_IOC_ACC_S_ARG:
	case ATOMISP_IOC_ACC_DESTAB:
		err = put_atomisp_acc_fw_arg32(&karg->acc_fw_arg, up);
		break;
	case ATOMISP_IOC_G_SENSOR_PRIV_INT_DATA:
	case ATOMISP_IOC_G_MOTOR_PRIV_INT_DATA:
		err = put_v4l2_private_int_data32(&karg->v4l2_pri_data, up);
		break;
	case ATOMISP_IOC_ACC_MAP:
	case ATOMISP_IOC_ACC_UNMAP:
		err = put_atomisp_acc_map32(&karg->acc_map, up);
		break;
	case ATOMISP_IOC_ACC_S_MAPPED_ARG:
		err = put_atomisp_acc_s_mapped_arg32(&karg->acc_map_arg, up);
		break;
	case ATOMISP_IOC_ACC_LOAD_TO_PIPE:
		err = put_atomisp_acc_fw_load_to_pipe32(&karg->acc_fw_to_pipe,
							up);
		break;
	case ATOMISP_IOC_G_METADATA:
		err = put_atomisp_metadata_stat32(&karg->md, up);
		break;
	case ATOMISP_IOC_G_METADATA_BY_TYPE:
		err = put_atomisp_metadata_by_type_stat32(&karg->md_with_type,
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
	case ATOMISP_IOC_S_SENSOR_RUNMODE:
	case ATOMISP_IOC_G_UPDATE_EXPOSURE:
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
