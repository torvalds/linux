/* SPDX-License-Identifier: GPL-2.0 */
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

*/

#ifndef __IA_CSS_COMMON_IO_TYPES
#define __IA_CSS_COMMON_IO_TYPES

#define MAX_IO_DMA_CHANNELS 3

struct ia_css_common_io_config {
	unsigned int base_address;
	unsigned int width;
	unsigned int height;
	unsigned int stride;
	unsigned int ddr_elems_per_word;
	unsigned int dma_channel[MAX_IO_DMA_CHANNELS];
};

#endif /* __IA_CSS_COMMON_IO_TYPES */
