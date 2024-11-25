/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * SPI controller driver for the nordic52832 SoCs
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>

#pragma once

#define KXR_SPI_XCHG_SIZE				190
#define KXR_SPI_XCHG_TAG				0xA8

#pragma pack(1)

enum kxr_spi_xchg_cmd {
		getMasterNordicVersionRequest = 1,
		setVibStateRequest,
		bondJoyStickRequest,
		disconnectJoyStickRequest,
		getJoyStickBondStateRequest,
		hostEnterDfuStateRequest,
		getLeftJoyStickProductNameRequest,
		getRightJoyStickProductNameRequest,
		getLeftJoyStickFwVersionRequest,
		getRightJoyStickFwVersionRequest,
		setPowerStateRequest,
		setBleMacAddr,//12
		getBleMacAddr,
		setBleAdvMode,//14
		invalidRequest,
};


union kxr_spi_xchg_header {
	struct {
		u32 key		:7;
		u32 ack		:1;
		u32 value	:24;
	};

	struct {
		u8 key_ack;
		u8 args[3];
	};
};

struct kxr_spi_xchg_node {
	u32 time;
	u8 buff[26];
};

struct kxr_spi_xchg_req {
	u8 type;
	union kxr_spi_xchg_header header;
};

struct kxr_spi_xchg_rsp {
	union {
		u32 header_value;
		union kxr_spi_xchg_header header;
	};

	u8 node_size;
	u8 node_count;
	u32 time;
	struct kxr_spi_xchg_node nodes[6];
};

#pragma pack()

struct kxr_spi_xchg {
	union {
		struct kxr_spi_xchg_req req;
		u8 tx_buff[KXR_SPI_XCHG_SIZE];
	};

	union {
		struct kxr_spi_xchg_rsp rsp;
		u8 rx_buff[KXR_SPI_XCHG_SIZE];
	};

	union kxr_spi_xchg_header header;
	u8 req_times;
};

void kxr_spi_xchg_clear(struct kxr_spi_xchg *xchg);

