/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Abilis Systems Single DVB-T Receiver
 * Copyright (C) 2008 Pierrick Hascoet <pierrick.hascoet@abilis.com>
 */
#ifndef _AS10X_HANDLE_H
#define _AS10X_HANDLE_H
struct as10x_bus_adapter_t;
struct as102_dev_t;

#include "as10x_cmd.h"

/* values for "mode" field */
#define REGMODE8	8
#define REGMODE16	16
#define REGMODE32	32

struct as102_priv_ops_t {
	int (*upload_fw_pkt)(struct as10x_bus_adapter_t *bus_adap,
			      unsigned char *buf, int buflen, int swap32);

	int (*send_cmd)(struct as10x_bus_adapter_t *bus_adap,
			 unsigned char *buf, int buflen);

	int (*xfer_cmd)(struct as10x_bus_adapter_t *bus_adap,
			 unsigned char *send_buf, int send_buf_len,
			 unsigned char *recv_buf, int recv_buf_len);

	int (*start_stream)(struct as102_dev_t *dev);
	void (*stop_stream)(struct as102_dev_t *dev);

	int (*reset_target)(struct as10x_bus_adapter_t *bus_adap);

	int (*read_write)(struct as10x_bus_adapter_t *bus_adap, uint8_t mode,
			  uint32_t rd_addr, uint16_t rd_len,
			  uint32_t wr_addr, uint16_t wr_len);

	int (*as102_read_ep2)(struct as10x_bus_adapter_t *bus_adap,
			       unsigned char *recv_buf,
			       int recv_buf_len);
};
#endif
