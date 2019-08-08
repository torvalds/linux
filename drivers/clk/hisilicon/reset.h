/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2015 HiSilicon Technologies Co., Ltd.
 */

#ifndef	__HISI_RESET_H
#define	__HISI_RESET_H

struct device_node;
struct hisi_reset_controller;

#ifdef CONFIG_RESET_CONTROLLER
struct hisi_reset_controller *hisi_reset_init(struct platform_device *pdev);
void hisi_reset_exit(struct hisi_reset_controller *rstc);
#else
static inline
struct hisi_reset_controller *hisi_reset_init(struct platform_device *pdev)
{
	return 0;
}
static inline void hisi_reset_exit(struct hisi_reset_controller *rstc)
{}
#endif

#endif	/* __HISI_RESET_H */
