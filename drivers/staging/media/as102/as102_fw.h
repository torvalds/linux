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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#define MAX_FW_PKT_SIZE	64

extern int dual_tuner;

#pragma pack(1)
struct as10x_raw_fw_pkt {
	unsigned char address[4];
	unsigned char data[MAX_FW_PKT_SIZE - 6];
};

struct as10x_fw_pkt_t {
	union {
		unsigned char request[2];
		unsigned char length[2];
	} u;
	struct as10x_raw_fw_pkt raw;
};
#pragma pack()

#ifdef __KERNEL__
int as102_fw_upload(struct as102_bus_adapter_t *bus_adap);
#endif

/* EOF - vim: set textwidth=80 ts=8 sw=8 sts=8 noet: */
