/*
 * Abilis Systems Single DVB-T Receiver
 * Copyright (C) 2008 Pierrick Hascoet <pierrick.hascoet@abilis.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
