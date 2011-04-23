/*
 * Copyright (c) 2008, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 **/
#ifndef _PSB_SGX_H_
#define _PSB_SGX_H_

extern int psb_submit_video_cmdbuf(struct drm_device *dev,
			       struct ttm_buffer_object *cmd_buffer,
			       unsigned long cmd_offset,
			       unsigned long cmd_size,
			       struct ttm_fence_object *fence);

extern int drm_idle_check_interval;

#endif
