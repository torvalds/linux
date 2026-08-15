/* SPDX-License-Identifier: GPL-2.0-only */

/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */
/* Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. */

#ifndef __QAIC_SSR_H__
#define __QAIC_SSR_H__

struct drm_device;
struct qaic_device;

int qaic_ssr_register(void);
void qaic_ssr_unregister(void);
void qaic_clean_up_ssr(struct qaic_device *qdev);
int qaic_ssr_init(struct qaic_device *qdev, struct drm_device *drm);
#endif /* __QAIC_SSR_H__ */
