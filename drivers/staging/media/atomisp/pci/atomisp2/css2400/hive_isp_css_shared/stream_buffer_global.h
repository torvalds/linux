/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __STREAM_BUFFER_GLOBAL_H_INCLUDED__
#define __STREAM_BUFFER_GLOBAL_H_INCLUDED__

typedef struct stream_buffer_s stream_buffer_t;
struct stream_buffer_s {
	unsigned	base;
	unsigned	limit;
	unsigned	top;
};

#endif /* __STREAM_BUFFER_GLOBAL_H_INCLUDED__ */

