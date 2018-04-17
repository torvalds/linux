/*
 * Support for Medfield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
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

#ifndef _hive_isp_css_mm_hrt_h_
#define _hive_isp_css_mm_hrt_h_

#include <hmm/hmm.h>
#include <hrt/hive_isp_css_custom_host_hrt.h>

#define HRT_BUF_FLAG_CACHED (1 << 0)

enum hrt_userptr_type {
	HRT_USR_PTR = 0,
#ifdef CONFIG_ION
	HRT_USR_ION,
#endif
};

struct hrt_userbuffer_attr {
	enum hrt_userptr_type	type;
	unsigned int		pgnr;
};

void hrt_isp_css_mm_set_user_ptr(void *userptr,
				unsigned int num_pages, enum hrt_userptr_type);

/* Allocate memory, returns a virtual address */
ia_css_ptr hrt_isp_css_mm_alloc(size_t bytes);
ia_css_ptr hrt_isp_css_mm_alloc_user_ptr(size_t bytes, void *userptr,
				    unsigned int num_pages,
				    enum hrt_userptr_type,
				    bool cached);
ia_css_ptr hrt_isp_css_mm_alloc_cached(size_t bytes);

/* allocate memory and initialize with zeros,
   returns a virtual address */
ia_css_ptr hrt_isp_css_mm_calloc(size_t bytes);
ia_css_ptr hrt_isp_css_mm_calloc_cached(size_t bytes);

#endif /* _hive_isp_css_mm_hrt_h_ */
