// SPDX-License-Identifier: GPL-2.0
/*
 * uvc_configfs.h
 *
 * Configfs support for the uvc function.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
 */
#ifndef UVC_CONFIGFS_H
#define UVC_CONFIGFS_H

struct f_uvc_opts;

int uvcg_attach_configfs(struct f_uvc_opts *opts);

#endif /* UVC_CONFIGFS_H */
