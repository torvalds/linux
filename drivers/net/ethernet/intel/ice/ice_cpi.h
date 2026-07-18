/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2026 Intel Corporation */

#ifndef _ICE_CPI_H_
#define _ICE_CPI_H_

#include "ice_type.h"
#include "ice_ptp_hw.h"

#define CPI0_PHY1_CMD_DATA	0x7FD028
#define CPI0_LM1_CMD_DATA	0x7FD024
#define CPI_RETRIES_COUNT	10
#define CPI_RETRIES_CADENCE_MS	100

/* CPI PHY CMD DATA register (CPI0_PHY1_CMD_DATA) */
#define CPI_PHY_CMD_DATA_M	GENMASK(15, 0)
#define CPI_PHY_CMD_OPCODE_M	GENMASK(23, 16)
#define CPI_PHY_CMD_PORTLANE_M	GENMASK(26, 24)
#define CPI_PHY_CMD_RSVD_M	GENMASK(29, 27)
#define CPI_PHY_CMD_ERROR_M	BIT(30)
#define CPI_PHY_CMD_ACK_M	BIT(31)

/* CPI LM CMD DATA register (CPI0_LM1_CMD_DATA) */
#define CPI_LM_CMD_DATA_M	GENMASK(15, 0)
#define CPI_LM_CMD_OPCODE_M	GENMASK(23, 16)
#define CPI_LM_CMD_PORTLANE_M	GENMASK(26, 24)
#define CPI_LM_CMD_RSVD_M	GENMASK(28, 27)
#define CPI_LM_CMD_GET_SET_M	BIT(29)
#define CPI_LM_CMD_REQ_M	BIT(31)

#define CPI_OPCODE_PHY_CLK			0xF1
#define CPI_OPCODE_PHY_CLK_PHY_SEL_M		GENMASK(9, 6)
#define CPI_OPCODE_PHY_CLK_REF_CTRL_M		GENMASK(5, 4)
#define CPI_OPCODE_PHY_CLK_DISABLE		1
#define CPI_OPCODE_PHY_CLK_ENABLE		2
#define CPI_OPCODE_PHY_CLK_REF_SEL_M		GENMASK(3, 0)

#define CPI_LM_CMD_REQ		1

struct ice_cpi_cmd {
	u8 port;
	u8 opcode;
	u16 data;
	bool set;
};

struct ice_cpi_resp {
	u8 port;
	u8 opcode;
	u16 data;
};

int ice_cpi_exec(struct ice_hw *hw, u8 phy,
		 const struct ice_cpi_cmd *cmd,
		 struct ice_cpi_resp *resp);
int ice_cpi_ena_dis_clk_ref(struct ice_hw *hw, u8 phy,
			    enum ice_e825c_ref_clk clk, bool enable);
#endif /* _ICE_CPI_H_ */
