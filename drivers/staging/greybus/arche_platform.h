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

#include "timesync.h"

enum arche_platform_state {
	ARCHE_PLATFORM_STATE_OFF,
	ARCHE_PLATFORM_STATE_ACTIVE,
	ARCHE_PLATFORM_STATE_STANDBY,
	ARCHE_PLATFORM_STATE_FW_FLASHING,
	ARCHE_PLATFORM_STATE_TIME_SYNC,
};

int arche_platform_change_state(enum arche_platform_state state,
				struct gb_timesync_svc *pdata);

extern int (*arche_platform_change_state_cb)(enum arche_platform_state state,
					     struct gb_timesync_svc *pdata);
int __init arche_apb_init(void);
void __exit arche_apb_exit(void);

/* Operational states for the APB device */
int apb_ctrl_coldboot(struct device *dev);
int apb_ctrl_fw_flashing(struct device *dev);
int apb_ctrl_standby_boot(struct device *dev);
void apb_ctrl_poweroff(struct device *dev);
void apb_bootret_assert(struct device *dev);
void apb_bootret_deassert(struct device *dev);

#endif	/* __ARCHE_PLATFORM_H */
