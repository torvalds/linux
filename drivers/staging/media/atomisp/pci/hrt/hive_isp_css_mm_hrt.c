/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
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

#include "atomisp_internal.h"

#include "hive_isp_css_mm_hrt.h"
#include "hmm/hmm.h"

#define __page_align(size)	(((size) + (PAGE_SIZE - 1)) & (~(PAGE_SIZE - 1)))

ia_css_ptr hrt_isp_css_mm_alloc(size_t bytes)
{
	return hmm_alloc(bytes, HMM_BO_PRIVATE, 0, NULL, false);
}

ia_css_ptr hrt_isp_css_mm_alloc_user_ptr(size_t bytes,
	const void __user *userptr,
	unsigned int num_pages,
	bool cached)
{
	if (num_pages < ((__page_align(bytes)) >> PAGE_SHIFT))
		dev_err(atomisp_dev,
			"user space memory size is less than the expected size..\n");
	else if (num_pages > ((__page_align(bytes))
				>> PAGE_SHIFT))
		dev_err(atomisp_dev,
			"user space memory size is large than the expected size..\n");

	return hmm_alloc(bytes, HMM_BO_USER, 0,
			    userptr, cached);
}

ia_css_ptr hrt_isp_css_mm_alloc_cached(size_t bytes)
{
	return hmm_alloc(bytes, HMM_BO_PRIVATE, 0, NULL,
				 HMM_CACHED);
}

ia_css_ptr hrt_isp_css_mm_calloc(size_t bytes)
{
	ia_css_ptr ptr = hrt_isp_css_mm_alloc(bytes);

	if (ptr)
		hmm_set(ptr, 0, bytes);
	return ptr;
}

ia_css_ptr hrt_isp_css_mm_calloc_cached(size_t bytes)
{
	ia_css_ptr ptr = hrt_isp_css_mm_alloc_cached(bytes);

	if (ptr)
		hmm_set(ptr, 0, bytes);
	return ptr;
}
