/*
 * linux/drivers/media/video/exynos/mfc/s5p_mfc_cmd.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __S5P_MFC_CMD_H
#define __S5P_MFC_CMD_H __FILE__

#define MAX_H2R_ARG		4

struct s5p_mfc_cmd_args {
	unsigned int	arg[MAX_H2R_ARG];
};

int s5p_mfc_cmd_host2risc(int cmd, struct s5p_mfc_cmd_args *args);
int s5p_mfc_sys_init_cmd(struct s5p_mfc_dev *dev);
int s5p_mfc_sleep_cmd(struct s5p_mfc_dev *dev);
int s5p_mfc_wakeup_cmd(struct s5p_mfc_dev *dev);
int s5p_mfc_open_inst_cmd(struct s5p_mfc_ctx *ctx);
int s5p_mfc_close_inst_cmd(struct s5p_mfc_ctx *ctx);

#endif /* __S5P_MFC_CMD_H */
