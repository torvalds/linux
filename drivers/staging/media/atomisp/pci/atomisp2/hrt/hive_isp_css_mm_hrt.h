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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef _hive_isp_css_mm_hrt_h_
#define _hive_isp_css_mm_hrt_h_

#ifndef HRT_KERNEL
/* size_t is defined already in kernel mode */
#include <stdlib.h>
#endif
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

void hrt_isp_css_mm_init(void);
void hrt_isp_css_mm_set_user_ptr(void *userptr,
				unsigned int num_pages, enum hrt_userptr_type);

int hrt_isp_css_mm_set(ia_css_ptr virt_addr, int c, size_t bytes);

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

/* Free memory, given a virtual address */
void hrt_isp_css_mm_free(ia_css_ptr virt_addr);

/* Store data to a virtual address */
int hrt_isp_css_mm_load(ia_css_ptr virt_addr, void *data, size_t bytes);

/* Load data from a virtual address */
int hrt_isp_css_mm_store(ia_css_ptr virt_addr, const void *data, size_t bytes);

int hrt_isp_css_mm_load_int(ia_css_ptr virt_addr, int *data);
int hrt_isp_css_mm_load_short(ia_css_ptr virt_addr, short *data);
int hrt_isp_css_mm_load_char(ia_css_ptr virt_addr, char *data);

int hrt_isp_css_mm_store_char(ia_css_ptr virt_addr, char data);
int hrt_isp_css_mm_store_short(ia_css_ptr virt_addr, short data);
int hrt_isp_css_mm_store_int(ia_css_ptr virt_addr, int data);

/* translate a virtual to a physical address, used to program
   the display driver on  the FPGA system */
phys_addr_t hrt_isp_css_virt_to_phys(ia_css_ptr virt_addr);

ia_css_ptr hrt_isp_css_mm_alloc_contiguous(size_t bytes);
ia_css_ptr hrt_isp_css_mm_calloc_contiguous(size_t bytes);

void hrt_isp_css_mm_clear(void);
#endif /* _hive_isp_css_mm_hrt_h_ */
