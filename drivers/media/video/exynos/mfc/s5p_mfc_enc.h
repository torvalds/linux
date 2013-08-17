/*
 * linux/drivers/media/video/exynos/mfc/s5p_mfc_enc.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __S5P_MFC_ENC_H_
#define __S5P_MFC_ENC_H_ __FILE__

const struct v4l2_ioctl_ops *get_enc_v4l2_ioctl_ops(void);
int s5p_mfc_init_enc_ctx(struct s5p_mfc_ctx *ctx);
int s5p_mfc_enc_ctx_ready(struct s5p_mfc_ctx *ctx);

#endif /* __S5P_MFC_ENC_H_ */
