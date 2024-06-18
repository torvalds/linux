/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_led_h__
#define __iwl_fw_api_led_h__

/**
 * struct iwl_led_cmd - LED switching command
 *
 * @status: LED status (on/off)
 */
struct iwl_led_cmd {
	__le32 status;
} __packed; /* LEDS_CMD_API_S_VER_2 */

#endif /* __iwl_fw_api_led_h__ */
