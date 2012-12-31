/*
 * Samsung Exynos5 SoC series FIMC-IS driver
 *
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 * Contact: Jiyoung Shin<idon.shin@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef FIMC_IS_MISC_H
#define FIMC_IS_MISC_H

 int enable_mipi(void);
int start_fimc_lite(struct flite_frame *f_frame);
int stop_fimc_lite(void);
int start_mipi_csi(struct flite_frame *f_frame);
int stop_mipi_csi(void);

#endif/*FIMC_IS_MISC_H*/