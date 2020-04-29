/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Abilis Systems Single DVB-T Receiver
 * Copyright (C) 2008 Pierrick Hascoet <pierrick.hascoet@abilis.com>
 */
#define MAX_FW_PKT_SIZE	64

extern int dual_tuner;

struct as10x_raw_fw_pkt {
	unsigned char address[4];
	unsigned char data[MAX_FW_PKT_SIZE - 6];
} __packed;

struct as10x_fw_pkt_t {
	union {
		unsigned char request[2];
		unsigned char length[2];
	} __packed u;
	struct as10x_raw_fw_pkt raw;
} __packed;

#ifdef __KERNEL__
int as102_fw_upload(struct as10x_bus_adapter_t *bus_adap);
#endif
