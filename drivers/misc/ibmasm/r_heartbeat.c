// SPDX-License-Identifier: GPL-2.0-or-later

/*
 *
 * Copyright (C) IBM Corporation, 2004
 *
 * Author: Max Asböck <amax@us.ibm.com>
 */

#include <linux/sched/signal.h>
#include "ibmasm.h"
#include "dot_command.h"

/*
 * Reverse Heartbeat, i.e. heartbeats sent from the driver to the
 * service processor.
 * These heartbeats are initiated by user level programs.
 */

/* the reverse heartbeat dot command */
#pragma pack(1)
static struct {
	struct dot_command_header	header;
	unsigned char			command[3];
} rhb_dot_cmd = {
	.header = {
		.type =		sp_read,
		.command_size = 3,
		.data_size =	0,
		.status =	0
	},
	.command = { 4, 3, 6 }
};
#pragma pack()

void ibmasm_init_reverse_heartbeat(struct service_processor *sp, struct reverse_heartbeat *rhb)
{
	init_waitqueue_head(&rhb->wait);
	rhb->stopped = 0;
}

/**
 * start_reverse_heartbeat
 * Loop forever, sending a reverse heartbeat dot command to the service
 * processor, then sleeping. The loop comes to an end if the service
 * processor fails to respond 3 times or we were interrupted.
 */
int ibmasm_start_reverse_heartbeat(struct service_processor *sp, struct reverse_heartbeat *rhb)
{
	struct command *cmd;
	int times_failed = 0;
	int result = 1;

	cmd = ibmasm_new_command(sp, sizeof rhb_dot_cmd);
	if (!cmd)
		return -ENOMEM;

	while (times_failed < 3) {
		memcpy(cmd->buffer, (void *)&rhb_dot_cmd, sizeof rhb_dot_cmd);
		cmd->status = IBMASM_CMD_PENDING;
		ibmasm_exec_command(sp, cmd);
		ibmasm_wait_for_response(cmd, IBMASM_CMD_TIMEOUT_NORMAL);

		if (cmd->status != IBMASM_CMD_COMPLETE)
			times_failed++;

		wait_event_interruptible_timeout(rhb->wait,
			rhb->stopped,
			REVERSE_HEARTBEAT_TIMEOUT * HZ);

		if (signal_pending(current) || rhb->stopped) {
			result = -EINTR;
			break;
		}
	}
	command_put(cmd);
	rhb->stopped = 0;

	return result;
}

void ibmasm_stop_reverse_heartbeat(struct reverse_heartbeat *rhb)
{
	rhb->stopped = 1;
	wake_up_interruptible(&rhb->wait);
}
