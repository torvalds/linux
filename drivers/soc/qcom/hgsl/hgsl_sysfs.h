/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 */
#ifndef __HGSL_SYSFS_H
#define __HGSL_SYSFS_H

#include <linux/device.h>
#include <linux/platform_device.h>

#ifdef CONFIG_SYSFS
int hgsl_sysfs_client_init(struct hgsl_priv *priv);
void hgsl_sysfs_client_release(struct hgsl_priv *priv);
int hgsl_sysfs_init(struct platform_device *pdev);
void hgsl_sysfs_release(struct platform_device *pdev);
#else
int hgsl_sysfs_client_init(struct hgsl_priv *priv)
{
	return 0;
}

void hgsl_sysfs_client_release(struct hgsl_priv *priv)
{
}
int hgsl_sysfs_init(struct platform_device *pdev)
{
	return 0;
}
void hgsl_sysfs_release(struct platform_device *pdev)
{
}
#endif /* CONFIG_SYSFS  */

#endif  /* __HGSL_SYSFS_H */

