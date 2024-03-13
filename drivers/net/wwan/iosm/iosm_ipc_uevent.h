/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-21 Intel Corporation.
 */

#ifndef IOSM_IPC_UEVENT_H
#define IOSM_IPC_UEVENT_H

/* Baseband event strings */
#define UEVENT_MDM_NOT_READY "MDM_NOT_READY"
#define UEVENT_ROM_READY "ROM_READY"
#define UEVENT_MDM_READY "MDM_READY"
#define UEVENT_CRASH "CRASH"
#define UEVENT_CD_READY "CD_READY"
#define UEVENT_CD_READY_LINK_DOWN "CD_READY_LINK_DOWN"
#define UEVENT_MDM_TIMEOUT "MDM_TIMEOUT"

/* Maximum length of user events */
#define MAX_UEVENT_LEN 64

/**
 * struct ipc_uevent_info - Uevent information structure.
 * @dev:	Pointer to device structure
 * @uevent:	Uevent information
 * @work:	Uevent work struct
 */
struct ipc_uevent_info {
	struct device *dev;
	char uevent[MAX_UEVENT_LEN];
	struct work_struct work;
};

/**
 * ipc_uevent_send - Send modem event to user space.
 * @dev:	Generic device pointer
 * @uevent:	Uevent information
 *
 */
void ipc_uevent_send(struct device *dev, char *uevent);

#endif
