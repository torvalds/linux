/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */
#ifndef __HGSL_DEBUGFS_H
#define __HGSL_DEBUGFS_H

#include <linux/device.h>
#include <linux/platform_device.h>

#ifdef CONFIG_DEBUG_FS
int hgsl_debugfs_client_init(struct hgsl_priv *priv);
void hgsl_debugfs_client_release(struct hgsl_priv *priv);

void hgsl_debugfs_init(struct platform_device *pdev);
void hgsl_debugfs_release(struct platform_device *pdev);
#else
static int hgsl_debugfs_client_init(struct hgsl_priv *priv)
{
	return 0;
}

void hgsl_debugfs_client_release(struct hgsl_priv *priv)
{

}

void hgsl_debugfs_init(struct platform_device *pdev)
{

}
void hgsl_debugfs_release(struct platform_device *pdev)
{

}
#endif

#endif  /* __HGSL_DEBUGFS_H */

