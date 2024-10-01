/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * linux/drivers/media/platform/samsung/s5p-mfc/s5p_mfc_enc.h
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 */

#ifndef S5P_MFC_ENC_H_
#define S5P_MFC_ENC_H_

const struct s5p_mfc_codec_ops *get_enc_codec_ops(void);
const struct vb2_ops *get_enc_queue_ops(void);
const struct v4l2_ioctl_ops *get_enc_v4l2_ioctl_ops(void);
int s5p_mfc_enc_ctrls_setup(struct s5p_mfc_ctx *ctx);
void s5p_mfc_enc_ctrls_delete(struct s5p_mfc_ctx *ctx);
void s5p_mfc_enc_init(struct s5p_mfc_ctx *ctx);

#endif /* S5P_MFC_ENC_H_  */
