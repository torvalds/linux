/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 *
 */

#ifndef __MAXIM2C_LINK_H__
#define __MAXIM2C_LINK_H__

#include "maxim2c_i2c.h"

/* Link cable */
enum maxim2c_link_cable {
	MAXIM2C_CABLE_COAX = 0,
	MAXIM2C_CABLE_STP,
};

/* Link Type */
enum maxim2c_link_type {
	MAXIM2C_GMSL1 = 0,
	MAXIM2C_GMSL2,
};

/* Link Mode */
enum maxim2c_link_mode {
	MAXIM2C_GMSL_PIXEL = 0,
	MAXIM2C_GMSL_TUNNEL,
};

/* I2C Remote Control Port */
enum {
	MAXIM2C_I2C_PORT0 = 0,
	MAXIM2C_I2C_PORT1,
	MAXIM2C_I2C_PORT2,
	MAXIM2C_I2C_PORT_MAX,
};

/* Link SIO ID: 0 ~ 3 */
enum {
	MAXIM2C_LINK_ID_A = 0,
	MAXIM2C_LINK_ID_B,
	MAXIM2C_LINK_ID_MAX,
};

/* Link Bit Mask: bit0 ~ bit3 */
#define MAXIM2C_LINK_MASK_A		BIT(MAXIM2C_LINK_ID_A)
#define MAXIM2C_LINK_MASK_B		BIT(MAXIM2C_LINK_ID_B)

#define MAXIM2C_LINK_MASK_ALL		GENMASK(MAXIM2C_LINK_ID_B, MAXIM2C_LINK_ID_A)

/* Link Receiver Rate */
enum maxim2c_link_rx_rate {
	MAXIM2C_LINK_RX_RATE_3GBPS = 0,
	MAXIM2C_LINK_RX_RATE_6GBPS,
};

/* Link Transmitter Rate */
enum maxim2c_link_tx_rate {
	MAXIM2C_LINK_TX_RATE_187_5MPS = 0,
};

struct maxim2c_link_cfg {
	u8 link_enable;
	u8 link_type;
	u8 link_rx_rate;
	u8 link_tx_rate;

	struct maxim2c_i2c_init_seq link_init_seq;
};

typedef struct maxim2c_gmsl_link {
	u8 link_enable_mask;
	u8 link_type_mask;
	u8 link_locked_mask;
	u8 link_vdd_ldo1_en;

	struct maxim2c_link_cfg link_cfg[MAXIM2C_LINK_ID_MAX];
} maxim2c_gmsl_link_t;

#endif /* __MAXIM2C_LINK_H__ */
