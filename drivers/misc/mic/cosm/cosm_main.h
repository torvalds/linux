/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel MIC Coprocessor State Management (COSM) Driver
 *
 */
#ifndef _COSM_COSM_H_
#define _COSM_COSM_H_

#include <linux/scif.h>
#include "../bus/cosm_bus.h"

#define COSM_HEARTBEAT_SEND_SEC 30
#define SCIF_COSM_LISTEN_PORT  201

/**
 * enum COSM msg id's
 * @COSM_MSG_SHUTDOWN: host->card trigger shutdown
 * @COSM_MSG_SYNC_TIME: host->card send host time to card to sync time
 * @COSM_MSG_HEARTBEAT: card->host heartbeat
 * @COSM_MSG_SHUTDOWN_STATUS: card->host with shutdown status as payload
 */
enum cosm_msg_id {
	COSM_MSG_SHUTDOWN,
	COSM_MSG_SYNC_TIME,
	COSM_MSG_HEARTBEAT,
	COSM_MSG_SHUTDOWN_STATUS,
};

struct cosm_msg {
	u64 id;
	union {
		u64 shutdown_status;
		struct {
			u64 tv_sec;
			u64 tv_nsec;
		} timespec;
	};
};

extern const char * const cosm_state_string[];
extern const char * const cosm_shutdown_status_string[];

void cosm_sysfs_init(struct cosm_device *cdev);
int cosm_start(struct cosm_device *cdev);
void cosm_stop(struct cosm_device *cdev, bool force);
int cosm_reset(struct cosm_device *cdev);
int cosm_shutdown(struct cosm_device *cdev);
void cosm_set_state(struct cosm_device *cdev, u8 state);
void cosm_set_shutdown_status(struct cosm_device *cdev, u8 status);
void cosm_init_debugfs(void);
void cosm_exit_debugfs(void);
void cosm_create_debug_dir(struct cosm_device *cdev);
void cosm_delete_debug_dir(struct cosm_device *cdev);
int cosm_scif_init(void);
void cosm_scif_exit(void);
void cosm_scif_work(struct work_struct *work);

#endif
