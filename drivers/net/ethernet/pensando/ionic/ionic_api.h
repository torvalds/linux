/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2025, Advanced Micro Devices, Inc. */

#ifndef _IONIC_API_H_
#define _IONIC_API_H_

#include <linux/auxiliary_bus.h>
#include "ionic_if.h"
#include "ionic_regs.h"

/**
 * struct ionic_aux_dev - Auxiliary device information
 * @lif:        Logical interface
 * @idx:        Index identifier
 * @adev:       Auxiliary device
 */
struct ionic_aux_dev {
	struct ionic_lif *lif;
	int idx;
	struct auxiliary_device adev;
};

/**
 * struct ionic_admin_ctx - Admin command context
 * @work:       Work completion wait queue element
 * @cmd:        Admin command (64B) to be copied to the queue
 * @comp:       Admin completion (16B) copied from the queue
 */
struct ionic_admin_ctx {
	struct completion work;
	union ionic_adminq_cmd cmd;
	union ionic_adminq_comp comp;
};

/**
 * ionic_adminq_post_wait - Post an admin command and wait for response
 * @lif:        Logical interface
 * @ctx:        API admin command context
 *
 * Post the command to an admin queue in the ethernet driver.  If this command
 * succeeds, then the command has been posted, but that does not indicate a
 * completion.  If this command returns success, then the completion callback
 * will eventually be called.
 *
 * Return: zero or negative error status
 */
int ionic_adminq_post_wait(struct ionic_lif *lif, struct ionic_admin_ctx *ctx);

/**
 * ionic_error_to_errno - Transform ionic_if errors to os errno
 * @code:       Ionic error number
 *
 * Return:      Negative OS error number or zero
 */
int ionic_error_to_errno(enum ionic_status_code code);

#endif /* _IONIC_API_H_ */
