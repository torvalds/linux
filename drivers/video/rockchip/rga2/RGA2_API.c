/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/memory.h>
#include "RGA2_API.h"
#include "rga2.h"
//#include "rga_angle.h"

#define IS_YUV_420(format) \
     ((format == RK_FORMAT_YCbCr_420_P) | (format == RK_FORMAT_YCbCr_420_SP) | \
      (format == RK_FORMAT_YCrCb_420_P) | (format == RK_FORMAT_YCrCb_420_SP))

#define IS_YUV_422(format) \
     ((format == RK_FORMAT_YCbCr_422_P) | (format == RK_FORMAT_YCbCr_422_SP) | \
      (format == RK_FORMAT_YCrCb_422_P) | (format == RK_FORMAT_YCrCb_422_SP))

#define IS_YUV(format) \
     ((format == RK_FORMAT_YCbCr_420_P) | (format == RK_FORMAT_YCbCr_420_SP) | \
      (format == RK_FORMAT_YCrCb_420_P) | (format == RK_FORMAT_YCrCb_420_SP) | \
      (format == RK_FORMAT_YCbCr_422_P) | (format == RK_FORMAT_YCbCr_422_SP) | \
      (format == RK_FORMAT_YCrCb_422_P) | (format == RK_FORMAT_YCrCb_422_SP))



