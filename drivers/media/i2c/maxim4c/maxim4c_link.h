/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 *
 */

#ifndef __MAXIM4C_LINK_H__
#define __MAXIM4C_LINK_H__

#include "maxim4c_i2c.h"

/* Link cable */
enum maxim4c_link_cable {
	MAXIM4C_CABLE_COAX = 0,
	MAXIM4C_CABLE_STP,
};

/* Link Type */
enum maxim4c_link_type {
	MAXIM4C_GMSL1 = 0,
	MAXIM4C_GMSL2,
};

/* Link Mode */
enum maxim4c_link_mode {
	MAXIM4C_GMSL_PIXEL = 0,
	MAXIM4C_GMSL_TUNNEL,
};

/* I2C Remote Control Port */
enum {
	MAXIM4C_I2C_PORT0 = 0,
	MAXIM4C_I2C_PORT1,
	MAXIM4C_I2C_PORT2,
	MAXIM4C_I2C_PORT_MAX,
};

/* Link SIO ID: 0 ~ 3 */
enum {
	MAXIM4C_LINK_ID_A = 0,
	MAXIM4C_LINK_ID_B,
	MAXIM4C_LINK_ID_C,
	MAXIM4C_LINK_ID_D,
	MAXIM4C_LINK_ID_MAX,
};

/* Link Bit Mask: bit0 ~ bit3 */
#define MAXIM4C_LINK_MASK_A		BIT(MAXIM4C_LINK_ID_A)
#define MAXIM4C_LINK_MASK_B		BIT(MAXIM4C_LINK_ID_B)
#define MAXIM4C_LINK_MASK_C		BIT(MAXIM4C_LINK_ID_C)
#define MAXIM4C_LINK_MASK_D		BIT(MAXIM4C_LINK_ID_D)

#define MAXIM4C_LINK_MASK_ALL		GENMASK(MAXIM4C_LINK_ID_D, MAXIM4C_LINK_ID_A)

/* Link Receiver Rate */
enum maxim4c_link_rx_rate {
	MAXIM4C_LINK_RX_RATE_3GBPS = 0,
	MAXIM4C_LINK_RX_RATE_6GBPS,
};

/* Link Transmitter Rate */
enum maxim4c_link_tx_rate {
	MAXIM4C_LINK_TX_RATE_187_5MPS = 0,
};

struct maxim4c_remote_info {
	const char *remote_name;
	const char *remote_compatible;
};

struct maxim4c_link_cfg {
	u8 link_enable;
	u8 link_type;
	u8 link_rx_rate;
	u8 link_tx_rate;

	struct maxim4c_remote_info remote_info;
	struct maxim4c_i2c_init_seq link_init_seq;
};

typedef struct maxim4c_gmsl_link {
	u8 link_enable_mask;
	u8 link_type_mask;
	u8 link_locked_mask;
	u8 link_vdd_ldo1_en;
	u8 link_vdd_ldo2_en;
	u8 i2c_ctrl_port;

	struct maxim4c_link_cfg link_cfg[MAXIM4C_LINK_ID_MAX];
} maxim4c_gmsl_link_t;

#endif /* __MAXIM4C_LINK_H__ */
