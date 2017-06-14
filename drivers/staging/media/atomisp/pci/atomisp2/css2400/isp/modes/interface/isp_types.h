#ifndef ISP2401
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
#else
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#endif

#ifndef _ISP_TYPES_H_
#define _ISP_TYPES_H_

/* Workaround: hivecc complains about "tag "sh_css_3a_output" already declared"
   without this extra decl. */
struct ia_css_3a_output;

#if defined(__ISP)
struct isp_uds_config {
	int      hive_dx;
	int      hive_dy;
	unsigned hive_woix;
	unsigned hive_bpp; /* gdc_bits_per_pixel */
	unsigned hive_bci;
};

struct s_isp_gdcac_config {
	unsigned nbx;
	unsigned nby;
};

/* output.hive.c request information */
typedef enum {
  output_y_channel,
  output_c_channel,
  OUTPUT_NUM_CHANNELS
} output_channel_type;

typedef struct s_output_dma_info {
  unsigned            cond;		/* Condition for transfer */
  output_channel_type channel_type;
  dma_channel         channel;
  unsigned            width_a;
  unsigned            width_b;
  unsigned            stride;
  unsigned            v_delta;	        /* Offset for v address to do cropping */
  char               *x_base;           /* X base address */
} output_dma_info_type;
#endif

/* Input stream formats, these correspond to the MIPI formats and the way
 * the CSS receiver sends these to the input formatter.
 * The bit depth of each pixel element is stored in the global variable
 * isp_bits_per_pixel.
 * NOTE: for rgb565, we set isp_bits_per_pixel to 565, for all other rgb
 * formats it's the actual depth (4, for 444, 8 for 888 etc).
 */
enum sh_stream_format {
	sh_stream_format_yuv420_legacy,
	sh_stream_format_yuv420,
	sh_stream_format_yuv422,
	sh_stream_format_rgb,
	sh_stream_format_raw,
	sh_stream_format_binary,	/* bytestream such as jpeg */
};

struct s_isp_frames {
	/* global variables that are written to by either the SP or the host,
	   every ISP binary needs these. */
	/* output frame */
	char *xmem_base_addr_y;
	char *xmem_base_addr_uv;
	char *xmem_base_addr_u;
	char *xmem_base_addr_v;
	/* 2nd output frame */
	char *xmem_base_addr_second_out_y;
	char *xmem_base_addr_second_out_u;
	char *xmem_base_addr_second_out_v;
	/* input yuv frame */
	char *xmem_base_addr_y_in;
	char *xmem_base_addr_u_in;
	char *xmem_base_addr_v_in;
	/* input raw frame */
	char *xmem_base_addr_raw;
	/* output raw frame */
	char *xmem_base_addr_raw_out;
	/* viewfinder output (vf_veceven) */
	char *xmem_base_addr_vfout_y;
	char *xmem_base_addr_vfout_u;
	char *xmem_base_addr_vfout_v;
	/* overlay frame (for vf_pp) */
	char *xmem_base_addr_overlay_y;
	char *xmem_base_addr_overlay_u;
	char *xmem_base_addr_overlay_v;
	/* pre-gdc output frame (gdc input) */
	char *xmem_base_addr_qplane_r;
	char *xmem_base_addr_qplane_ratb;
	char *xmem_base_addr_qplane_gr;
	char *xmem_base_addr_qplane_gb;
	char *xmem_base_addr_qplane_b;
	char *xmem_base_addr_qplane_batr;
	/* YUV as input, used by postisp binary */
	char *xmem_base_addr_yuv_16_y;
	char *xmem_base_addr_yuv_16_u;
	char *xmem_base_addr_yuv_16_v;
};

#endif /* _ISP_TYPES_H_ */
