/* SPDX-License-Identifier: GPL-2.0 */
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

*/

#ifndef __YUV444_IO_HOST_H
#define __YUV444_IO_HOST_H

#include "ia_css_yuv444_io_param.h"
#include "ia_css_yuv444_io_types.h"
#include "ia_css_binary.h"
#include "sh_css_internal.h"

int ia_css_yuv444_io_config(const struct ia_css_binary     *binary,
			    const struct sh_css_binary_args *args);

#endif /*__YUV44_IO_HOST_H */
