/*
 * Arche Platform driver to enable Unipro link.
 *
 * Copyright 2015-2016 Google Inc.
 * Copyright 2015-2016 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __ARCHE_PLATFORM_H
#define __ARCHE_PLATFORM_H

int arche_apb_ctrl_probe(struct platform_device *pdev);
int arche_apb_ctrl_remove(struct platform_device *pdev);
extern const struct dev_pm_ops arche_apb_ctrl_pm_ops;

#endif	/* __ARCHE_PLATFORM_H */
