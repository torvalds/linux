/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Intel Speed Select Interface: Drivers Internal defines
 * Copyright (c) 2019, Intel Corporation.
 * All rights reserved.
 *
 * Author: Srinivas Pandruvada <srinivas.pandruvada@linux.intel.com>
 */

#ifndef __ISST_IF_COMMON_H
#define __ISST_IF_COMMON_H

/*
 * Validate maximum commands in a single request.
 * This is enough to handle command to every core in one ioctl, or all
 * possible message id to one CPU. Limit is also helpful for resonse time
 * per IOCTL request, as PUNIT may take different times to process each
 * request and may hold for long for too many commands.
 */
#define ISST_IF_CMD_LIMIT	64

#define ISST_IF_API_VERSION	0x01
#define ISST_IF_DRIVER_VERSION	0x01

#define ISST_IF_DEV_MBOX	0
#define ISST_IF_DEV_MMIO	1
#define ISST_IF_DEV_MAX		2

/**
 * struct isst_if_cmd_cb - Used to register a IOCTL handler
 * @registered:	Used by the common code to store registry. Caller don't
 *		to touch this field
 * @cmd_size:	The command size of the individual command in IOCTL
 * @offset:	Offset to the first valid member in command structure.
 *		This will be the offset of the start of the command
 *		after command count field
 * @cmd_callback: Callback function to handle IOCTL. The callback has the
 *		command pointer with data for command. There is a pointer
 *		called write_only, which when set, will not copy the
 *		response to user ioctl buffer. The "resume" argument
 *		can be used to avoid storing the command for replay
 *		during system resume
 *
 * This structure is used to register an handler for IOCTL. To avoid
 * code duplication common code handles all the IOCTL command read/write
 * including handling multiple command in single IOCTL. The caller just
 * need to execute a command via the registered callback.
 */
struct isst_if_cmd_cb {
	int registered;
	int cmd_size;
	int offset;
	struct module *owner;
	long (*cmd_callback)(u8 *ptr, int *write_only, int resume);
};

/* Internal interface functions */
int isst_if_cdev_register(int type, struct isst_if_cmd_cb *cb);
void isst_if_cdev_unregister(int type);
struct pci_dev *isst_if_get_pci_dev(int cpu, int bus, int dev, int fn);
#endif
