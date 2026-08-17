/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _XE_SYSCTRL_MAILBOX_H_
#define _XE_SYSCTRL_MAILBOX_H_

#include <linux/bitfield.h>
#include <linux/types.h>

#include "abi/xe_sysctrl_abi.h"

struct xe_sysctrl;
struct xe_sysctrl_mailbox_command;

#define XE_SYSCTRL_APP_HDR_GROUP_ID(hdr) \
	FIELD_GET(APP_HDR_GROUP_ID_MASK, (hdr)->data)

#define XE_SYSCTRL_APP_HDR_COMMAND(hdr) \
	FIELD_GET(APP_HDR_COMMAND_MASK, (hdr)->data)

#define XE_SYSCTRL_APP_HDR_VERSION(hdr) \
	FIELD_GET(APP_HDR_VERSION_MASK, (hdr)->data)

void xe_sysctrl_mailbox_init(struct xe_sysctrl *sc);
int xe_sysctrl_send_command(struct xe_sysctrl *sc,
			    struct xe_sysctrl_mailbox_command *cmd,
			    size_t *rdata_len);

#endif
