/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010-2015, Intel Corporation.
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

#ifndef _hive_isp_css_ddr_hrt_modified_h_
#define _hive_isp_css_ddr_hrt_modified_h_

#include <hmm_64/hmm.h>

/* This function reads an image from DDR and stores it in the img_buf array
   that has been allocated by the caller.
   The specifics of how the pixels are stored into DDR by the DMA are taken
   into account (bits padded to a width of 256, depending on the number of
   elements per ddr word).
   The DMA specific parameters give to this function (elems_per_xword and sign_extend)
   should correspond to those given to the DMA engine.
   The address is a virtual address which will be translated to a physical address before
   data is loaded from or stored to that address.

   The return value is 0 in case of success and 1 in case of failure.
 */
unsigned int
hrt_isp_css_read_image_from_ddr(
    unsigned short *img_buf,
    unsigned int width,
    unsigned int height,
    unsigned int elems_per_xword,
    unsigned int sign_extend,
    hmm_ptr virt_addr);

/* This function writes an image to DDR, keeping the same aspects into account as the read_image function
   above. */
unsigned int
hrt_isp_css_write_image_to_ddr(
    const unsigned short *img_buf,
    unsigned int width,
    unsigned int height,
    unsigned int elems_per_xword,
    unsigned int sign_extend,
    hmm_ptr virt_addr);

/* return the size in bytes of an image (frame or plane). */
unsigned int
hrt_isp_css_sizeof_image_in_ddr(
    unsigned int width,
    unsigned int height,
    unsigned int bits_per_element);

unsigned int
hrt_isp_css_stride_of_image_in_ddr(
    unsigned int width,
    unsigned int bits_per_element);

hmm_ptr
hrt_isp_css_alloc_image_in_ddr(
    unsigned int width,
    unsigned int height,
    unsigned int elems_per_xword);

hmm_ptr
hrt_isp_css_calloc_image_in_ddr(
    unsigned int width,
    unsigned int height,
    unsigned int elems_per_xword);

#ifndef HIVE_ISP_NO_GDC
#include "gdc_v2_defs.h"

hmm_ptr
hrt_isp_css_alloc_gdc_lut_in_ddr(void);

void
hrt_isp_css_write_gdc_lut_to_ddr(
    short values[4][HRT_GDC_N],
    hmm_ptr virt_addr);
#endif

#ifdef _HIVE_ISP_CSS_FPGA_SYSTEM
hmm_ptr
hrt_isp_css_alloc_image_for_display(
    unsigned int width,
    unsigned int height,
    unsigned int elems_per_xword);

hmm_ptr
hrt_isp_css_calloc_image_for_display(
    unsigned int width,
    unsigned int height,
    unsigned int elems_per_xword);
#endif

/* New set of functions, these do not require the elems_per_xword, but use bits_per_element instead,
   this way the user does not need to know about the width of a DDR word. */
unsigned int
hrt_isp_css_read_unsigned(
    unsigned short *target,
    unsigned int width,
    unsigned int height,
    unsigned int source_bits_per_element,
    hmm_ptr source);

unsigned int
hrt_isp_css_read_signed(
    short *target,
    unsigned int width,
    unsigned int height,
    unsigned int source_bits_per_element,
    hmm_ptr source);

unsigned int 
hrt_isp_css_write_unsigned(
    const unsigned short *source,
    unsigned int width,
    unsigned int height,
    unsigned int target_bits_per_element,
    hmm_ptr target);

unsigned int 
hrt_isp_css_write_signed(
    const short *source,
    unsigned int width,
    unsigned int height,
    unsigned int target_bits_per_element,
    hmm_ptr target);

hmm_ptr
hrt_isp_css_alloc(
    unsigned int width,
    unsigned int height,
    unsigned int bits_per_element);

hmm_ptr
hrt_isp_css_calloc(
    unsigned int width,
    unsigned int height,
    unsigned int bits_per_element);

#endif /* _hive_isp_css_ddr_hrt_modified_h_ */
