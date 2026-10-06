/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * HID driver for Nintendo Switch 2 controllers
 *
 * Copyright (c) 2025 Valve Software
 *
 * This driver is based on the following work:
 *   https://gist.github.com/shinyquagsire23/66f006b46c56216acbaac6c1e2279b64
 *   https://github.com/ndeadly/switch2_controller_research
 */

#ifndef __HID_NINTENDO_H
#define __HID_NINTENDO_H

#include <linux/bits.h>

#define NS2_FLAG_OK	BIT(0)
#define NS2_FLAG_NACK	BIT(2)

enum switch2_cmd {
	NS2_CMD_NFC = 0x01,
	NS2_CMD_FLASH = 0x02,
	NS2_CMD_INIT = 0x03,
	NS2_CMD_GRIP = 0x08,
	NS2_CMD_LED = 0x09,
	NS2_CMD_VIBRATE = 0x0a,
	NS2_CMD_BATTERY = 0x0b,
	NS2_CMD_FEATSEL = 0x0c,
	NS2_CMD_FW_UPD = 0x0d,
	NS2_CMD_FW_INFO = 0x10,
	NS2_CMD_BT_PAIR = 0x15,
};

enum switch2_direction {
	NS2_DIR_IN = 0x00,
	NS2_DIR_OUT = 0x90,
};

enum switch2_transport {
	NS2_TRANS_USB = 0x00,
	NS2_TRANS_BT = 0x01,
};

struct switch2_cmd_header {
	uint8_t command;
	uint8_t flags;
	uint8_t transport;
	uint8_t subcommand;
	uint8_t unk1;
	uint8_t length;
	uint16_t unk2;
};
static_assert(sizeof(struct switch2_cmd_header) == 8);

struct device;
struct switch2_controller;
struct switch2_cfg_intf {
	struct switch2_controller *parent;
	struct device *dev;

	int (*send_command)(enum switch2_cmd command, uint8_t subcommand,
		const void *message, size_t length,
		struct switch2_cfg_intf *intf);
	void (*shutdown)(struct switch2_cfg_intf *intf);
};

int switch2_controller_attach_cfg(const char *phys, struct switch2_cfg_intf *cfg);
void switch2_controller_detach_cfg(struct switch2_controller *controller);

int switch2_receive_command(struct switch2_controller *controller,
	const uint8_t *message, size_t length);

#endif
