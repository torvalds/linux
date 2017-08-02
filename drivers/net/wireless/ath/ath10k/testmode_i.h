/*
 * Copyright (c) 2014 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* "API" level of the ath10k testmode interface. Bump it after every
 * incompatible interface change.
 */
#define ATH10K_TESTMODE_VERSION_MAJOR 1

/* Bump this after every _compatible_ interface change, for example
 * addition of a new command or an attribute.
 */
#define ATH10K_TESTMODE_VERSION_MINOR 0

#define ATH10K_TM_DATA_MAX_LEN		5000

enum ath10k_tm_attr {
	__ATH10K_TM_ATTR_INVALID	= 0,
	ATH10K_TM_ATTR_CMD		= 1,
	ATH10K_TM_ATTR_DATA		= 2,
	ATH10K_TM_ATTR_WMI_CMDID	= 3,
	ATH10K_TM_ATTR_VERSION_MAJOR	= 4,
	ATH10K_TM_ATTR_VERSION_MINOR	= 5,
	ATH10K_TM_ATTR_WMI_OP_VERSION	= 6,

	/* keep last */
	__ATH10K_TM_ATTR_AFTER_LAST,
	ATH10K_TM_ATTR_MAX		= __ATH10K_TM_ATTR_AFTER_LAST - 1,
};

/* All ath10k testmode interface commands specified in
 * ATH10K_TM_ATTR_CMD
 */
enum ath10k_tm_cmd {
	/* Returns the supported ath10k testmode interface version in
	 * ATH10K_TM_ATTR_VERSION. Always guaranteed to work. User space
	 * uses this to verify it's using the correct version of the
	 * testmode interface
	 */
	ATH10K_TM_CMD_GET_VERSION = 0,

	/* Boots the UTF firmware, the netdev interface must be down at the
	 * time.
	 */
	ATH10K_TM_CMD_UTF_START = 1,

	/* Shuts down the UTF firmware and puts the driver back into OFF
	 * state.
	 */
	ATH10K_TM_CMD_UTF_STOP = 2,

	/* The command used to transmit a WMI command to the firmware and
	 * the event to receive WMI events from the firmware. Without
	 * struct wmi_cmd_hdr header, only the WMI payload. Command id is
	 * provided with ATH10K_TM_ATTR_WMI_CMDID and payload in
	 * ATH10K_TM_ATTR_DATA.
	 */
	ATH10K_TM_CMD_WMI = 3,
};
