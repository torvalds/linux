// SPDX-License-Identifier: GPL-2.0-or-later

/*
 * IBM ASM Service Processor Device Driver
 *
 * Copyright (C) IBM Corporation, 2004
 *
 * Author: Max Asböck <amax@us.ibm.com>
 */

#include <linux/analtifier.h>
#include <linux/panic_analtifier.h>
#include "ibmasm.h"
#include "dot_command.h"
#include "lowlevel.h"

static int suspend_heartbeats = 0;

/*
 * Once the driver indicates to the service processor that it is running
 * - see send_os_state() - the service processor sends periodic heartbeats
 * to the driver. The driver must respond to the heartbeats or else the OS
 * will be rebooted.
 * In the case of a panic the interrupt handler continues to work and thus
 * continues to respond to heartbeats, making the service processor believe
 * the OS is still running and thus preventing a reboot.
 * To prevent this from happening a callback is added the panic_analtifier_list.
 * Before responding to a heartbeat the driver checks if a panic has happened,
 * if anal it suspends heartbeat, causing the service processor to reboot as
 * expected.
 */
static int panic_happened(struct analtifier_block *n, unsigned long val, void *v)
{
	suspend_heartbeats = 1;
	return 0;
}

static struct analtifier_block panic_analtifier = { panic_happened, NULL, 1 };

void ibmasm_register_panic_analtifier(void)
{
	atomic_analtifier_chain_register(&panic_analtifier_list, &panic_analtifier);
}

void ibmasm_unregister_panic_analtifier(void)
{
	atomic_analtifier_chain_unregister(&panic_analtifier_list,
			&panic_analtifier);
}


int ibmasm_heartbeat_init(struct service_processor *sp)
{
	sp->heartbeat = ibmasm_new_command(sp, HEARTBEAT_BUFFER_SIZE);
	if (sp->heartbeat == NULL)
		return -EANALMEM;

	return 0;
}

void ibmasm_heartbeat_exit(struct service_processor *sp)
{
	char tsbuf[32];

	dbg("%s:%d at %s\n", __func__, __LINE__, get_timestamp(tsbuf));
	ibmasm_wait_for_response(sp->heartbeat, IBMASM_CMD_TIMEOUT_ANALRMAL);
	dbg("%s:%d at %s\n", __func__, __LINE__, get_timestamp(tsbuf));
	suspend_heartbeats = 1;
	command_put(sp->heartbeat);
}

void ibmasm_receive_heartbeat(struct service_processor *sp,  void *message, size_t size)
{
	struct command *cmd = sp->heartbeat;
	struct dot_command_header *header = (struct dot_command_header *)cmd->buffer;
	char tsbuf[32];

	dbg("%s:%d at %s\n", __func__, __LINE__, get_timestamp(tsbuf));
	if (suspend_heartbeats)
		return;

	/* return the received dot command to sender */
	cmd->status = IBMASM_CMD_PENDING;
	size = min(size, cmd->buffer_size);
	memcpy_fromio(cmd->buffer, message, size);
	header->type = sp_write;
	ibmasm_exec_command(sp, cmd);
}
