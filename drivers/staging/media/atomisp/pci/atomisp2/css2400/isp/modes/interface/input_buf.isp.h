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

#ifndef _INPUT_BUF_ISP_H_
#define _INPUT_BUF_ISP_H_

/* Temporary include, since IA_CSS_BINARY_MODE_COPY is still needed */
#include "sh_css_defs.h"
#include "isp_const.h" /* MAX_VECTORS_PER_INPUT_LINE */

#define INPUT_BUF_HEIGHT	2 /* double buffer */
#define INPUT_BUF_LINES		2

#ifndef ENABLE_CONTINUOUS
#define ENABLE_CONTINUOUS 0
#endif

/* In continuous mode, the input buffer must be a fixed size for all binaries
 * and at a fixed address since it will be used by the SP. */
#define EXTRA_INPUT_VECTORS	2 /* For left padding */
#define MAX_VECTORS_PER_INPUT_LINE_CONT (CEIL_DIV(SH_CSS_MAX_SENSOR_WIDTH, ISP_NWAY) + EXTRA_INPUT_VECTORS)

/* The input buffer should be on a fixed address in vmem, for continuous capture */
#define INPUT_BUF_ADDR 0x0
#if (defined(__ISP) && (!defined(MODE) || MODE != IA_CSS_BINARY_MODE_COPY))

#if ENABLE_CONTINUOUS
typedef struct {
  tmemvectoru  raw[INPUT_BUF_HEIGHT][INPUT_BUF_LINES][MAX_VECTORS_PER_INPUT_LINE_CONT]; /* 2 bayer lines */
  /* Two more lines for SP raw copy efficiency */
#ifndef ENABLE_REDUCED_INPUT_BUFFER
  /* "Workaround" solution in the case that space needed vmem exceeds the size of the vmem. */
  /* Since in theory this buffer is not needed for IPU 2.2/2.3,  */
  /* the workaround solution will not be needed (and the whole buffer) after the code refactoring. */
  tmemvectoru _raw[INPUT_BUF_HEIGHT][INPUT_BUF_LINES][MAX_VECTORS_PER_INPUT_LINE_CONT]; /* 2 bayer lines */
#endif
} input_line_type;
#else /* ENABLE CONTINUOUS == 0 */
typedef struct {
  tmemvectoru  raw[INPUT_BUF_HEIGHT][INPUT_BUF_LINES][MAX_VECTORS_PER_INPUT_LINE]; /* 2 bayer lines */
} input_line_type;
#endif /* ENABLE_CONTINUOUS */

#endif /*MODE*/

#endif /* _INPUT_BUF_ISP_H_ */
