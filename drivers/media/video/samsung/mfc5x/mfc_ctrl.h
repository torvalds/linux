/*
 * linux/drivers/media/video/samsung/mfc5x/mfc_ctrl.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Control interface for Samsung MFC (Multi Function Codec - FIMV) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MFC_CTRL_H
#define __MFC_CTRL_H __FILE__

int mfc_load_firmware(const unsigned char *data, size_t size);
int mfc_start(struct mfc_dev *dev);
int mfc_sleep(struct mfc_dev *dev);
int mfc_wakeup(struct mfc_dev *dev);

#endif /* __MFC_CTRL_H */
