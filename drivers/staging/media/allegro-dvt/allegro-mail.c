// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Pengutronix, Michael Tretter <kernel@pengutronix.de>
 *
 * Helper functions for handling messages that are send via mailbox to the
 * Allegro VCU firmware.
 */

#include <linux/export.h>

#include "allegro-mail.h"

const char *msg_type_name(enum mcu_msg_type type)
{
	static char buf[9];

	switch (type) {
	case MCU_MSG_TYPE_INIT:
		return "INIT";
	case MCU_MSG_TYPE_CREATE_CHANNEL:
		return "CREATE_CHANNEL";
	case MCU_MSG_TYPE_DESTROY_CHANNEL:
		return "DESTROY_CHANNEL";
	case MCU_MSG_TYPE_ENCODE_FRAME:
		return "ENCODE_FRAME";
	case MCU_MSG_TYPE_PUT_STREAM_BUFFER:
		return "PUT_STREAM_BUFFER";
	case MCU_MSG_TYPE_PUSH_BUFFER_INTERMEDIATE:
		return "PUSH_BUFFER_INTERMEDIATE";
	case MCU_MSG_TYPE_PUSH_BUFFER_REFERENCE:
		return "PUSH_BUFFER_REFERENCE";
	default:
		snprintf(buf, sizeof(buf), "(0x%04x)", type);
		return buf;
	}
}
EXPORT_SYMBOL(msg_type_name);
