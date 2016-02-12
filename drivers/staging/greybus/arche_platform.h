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

enum arche_platform_state {
	ARCHE_PLATFORM_STATE_OFF,
	ARCHE_PLATFORM_STATE_ACTIVE,
	ARCHE_PLATFORM_STATE_STANDBY,
	ARCHE_PLATFORM_STATE_FW_FLASHING,
};

int arche_apb_ctrl_probe(struct platform_device *pdev);
int arche_apb_ctrl_remove(struct platform_device *pdev);
extern const struct dev_pm_ops arche_apb_ctrl_pm_ops;

#endif	/* __ARCHE_PLATFORM_H */
