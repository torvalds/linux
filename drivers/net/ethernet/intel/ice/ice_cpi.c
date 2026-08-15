// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2026 Intel Corporation */

#include "ice_type.h"
#include "ice_common.h"
#include "ice_ptp_hw.h"
#include "ice.h"
#include "ice_cpi.h"

/**
 * ice_cpi_get_dest_dev - get destination PHY for given phy index
 * @hw: pointer to the HW struct
 * @phy: phy index of port the CPI action is taken on
 *
 * Return: sideband queue destination PHY device.
 */
static enum ice_sbq_dev_id ice_cpi_get_dest_dev(struct ice_hw *hw, u8 phy)
{
	u8 curr_phy = hw->lane_num / hw->ptp.ports_per_phy;

	/* In the driver, lanes 4..7 are in fact 0..3 on a second PHY.
	 * On a single complex E825C, PHY 0 is always destination device phy_0
	 * and PHY 1 is phy_0_peer.
	 * On dual complex E825C, device phy_0 points to PHY on a current
	 * complex and phy_0_peer to PHY on a different complex.
	 */
	if ((!ice_is_dual(hw) && phy) ||
	    (ice_is_dual(hw) && phy != curr_phy))
		return ice_sbq_dev_phy_0_peer;
	else
		return ice_sbq_dev_phy_0;
}

/**
 * ice_cpi_write_phy - Write a CPI port register
 * @hw: pointer to the HW struct
 * @phy: phy index of port the CPI action is taken on
 * @addr: PHY register address
 * @val: Value to write
 *
 * Return:
 * * 0 on success
 * * other error codes when failed to write to PHY
 */
static int ice_cpi_write_phy(struct ice_hw *hw, u8 phy, u32 addr, u32 val)
{
	struct ice_sbq_msg_input msg = {
		.dest_dev = ice_cpi_get_dest_dev(hw, phy),
		.opcode = ice_sbq_msg_wr_np,
		.msg_addr_low = lower_16_bits(addr),
		.msg_addr_high = upper_16_bits(addr),
		.data = val
	};
	int err;

	err = ice_sbq_rw_reg(hw, &msg, LIBIE_AQ_FLAG_RD);
	if (err)
		ice_debug(hw, ICE_DBG_PTP,
			  "Failed to write CPI msg to phy %d, err: %d\n",
			  phy, err);

	return err;
}

/**
 * ice_cpi_read_phy - Read a CPI port register
 * @hw: pointer to the HW struct
 * @phy: phy index of port the CPI action is taken on
 * @addr: PHY register address
 * @val: storage for register value
 *
 * Return:
 * * 0 on success
 * * other error codes when failed to read from PHY
 */
static int ice_cpi_read_phy(struct ice_hw *hw, u8 phy, u32 addr, u32 *val)
{
	struct ice_sbq_msg_input msg = {
		.dest_dev = ice_cpi_get_dest_dev(hw, phy),
		.opcode = ice_sbq_msg_rd,
		.msg_addr_low = lower_16_bits(addr),
		.msg_addr_high = upper_16_bits(addr)
	};
	int err;

	err = ice_sbq_rw_reg(hw, &msg, LIBIE_AQ_FLAG_RD);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP,
			  "Failed to read CPI msg from phy %d, err: %d\n",
			  phy, err);
		return err;
	}

	*val = msg.data;

	return 0;
}

/**
 * ice_cpi_wait_req0_ack0 - waits for CPI interface to be available
 * @hw: pointer to the HW struct
 * @phy: phy index of port the CPI action is taken on
 *
 * This function checks if CPI interface is ready to use by CPI client.
 * It's done by assuring LM.CMD.REQ and PHY.CMD.ACK bit in CPI
 * interface registers to be 0.
 *
 * Return: 0 on success, negative on error
 */
static int ice_cpi_wait_req0_ack0(struct ice_hw *hw, int phy)
{
	u32 phy_val;
	u32 lm_val;

	for (int i = 0; i < CPI_RETRIES_COUNT; i++) {
		int err;

		/* check if another CPI Client is also accessing CPI */
		err = ice_cpi_read_phy(hw, phy, CPI0_LM1_CMD_DATA, &lm_val);
		if (err)
			return err;
		if (FIELD_GET(CPI_LM_CMD_REQ_M, lm_val))
			goto retry;

		/* check if PHY.ACK is deasserted */
		err = ice_cpi_read_phy(hw, phy, CPI0_PHY1_CMD_DATA, &phy_val);
		if (err)
			return err;
		if (!FIELD_GET(CPI_PHY_CMD_ACK_M, phy_val))
			/* req0 and ack0 at this point - ready to go */
			return 0;

retry:
		msleep(CPI_RETRIES_CADENCE_MS);
	}

	return -ETIMEDOUT;
}

/**
 * ice_cpi_wait_ack - Waits for the PHY.ACK bit to be asserted/deasserted
 * @hw: pointer to the HW struct
 * @phy: phy index of port the CPI action is taken on
 * @asserted: desired state of PHY.ACK bit
 * @data: pointer to the user data where PHY.data is stored
 *
 * This function checks if PHY.ACK bit is asserted or deasserted, depending
 * on the phase of CPI handshake. If 'asserted' state is required, PHY command
 * data is stored in the 'data' storage.
 *
 * Return: 0 on success, negative on error
 */
static int ice_cpi_wait_ack(struct ice_hw *hw, u8 phy, bool asserted,
			    u32 *data)
{
	u32 phy_val;

	for (int i = 0; i < CPI_RETRIES_COUNT; i++) {
		int err;

		err = ice_cpi_read_phy(hw, phy, CPI0_PHY1_CMD_DATA, &phy_val);
		if (err)
			return err;
		if (asserted && FIELD_GET(CPI_PHY_CMD_ERROR_M, phy_val))
			return -EFAULT;
		if (asserted && FIELD_GET(CPI_PHY_CMD_ACK_M, phy_val)) {
			if (data)
				*data = phy_val;
			return 0;
		}
		if (!asserted && !FIELD_GET(CPI_PHY_CMD_ACK_M, phy_val))
			return 0;

		msleep(CPI_RETRIES_CADENCE_MS);
	}

	return -ETIMEDOUT;
}

#define ice_cpi_wait_ack0(hw, port) \
	ice_cpi_wait_ack(hw, port, false, NULL)

#define ice_cpi_wait_ack1(hw, port, data) \
	ice_cpi_wait_ack(hw, port, true, data)

/**
 * ice_cpi_req0 - deasserts LM.REQ bit
 * @hw: pointer to the HW struct
 * @phy: phy index of port the CPI action is taken on
 * @data: the command data
 *
 * Return: 0 on success, negative on CPI write error
 */
static int ice_cpi_req0(struct ice_hw *hw, u8 phy, u32 data)
{
	data &= ~CPI_LM_CMD_REQ_M;

	return ice_cpi_write_phy(hw, phy, CPI0_LM1_CMD_DATA, data);
}

/**
 * ice_cpi_exec_cmd - writes command data to CPI interface
 * @hw: pointer to the HW struct
 * @phy: phy index of port the CPI action is taken on
 * @data: the command data
 *
 * Return: 0 on success, otherwise negative on error
 */
static int ice_cpi_exec_cmd(struct ice_hw *hw, int phy, u32 data)
{
	return ice_cpi_write_phy(hw, phy, CPI0_LM1_CMD_DATA, data);
}

/**
 * ice_cpi_phy_lock - get per-PHY lock for CPI transaction serialization
 * @hw: pointer to the HW struct
 * @phy: PHY index
 *
 * Return: pointer to PHY mutex, or %NULL when context is unavailable.
 */
static struct mutex *ice_cpi_phy_lock(struct ice_hw *hw, u8 phy)
{
	struct ice_pf *pf = hw->back;

	if (!pf || !pf->adapter || phy >= ICE_E825_MAX_PHYS)
		return NULL;

	return &pf->adapter->cpi_phy_lock[phy];
}

/**
 * ice_cpi_exec - executes CPI command
 * @hw: pointer to the HW struct
 * @phy: phy index of port the CPI action is taken on
 * @cmd: pointer to the command struct to execute
 * @resp: pointer to user allocated CPI response struct
 *
 * This function executes CPI request with respect to CPI handshake
 * mechanism.
 *
 * Return: 0 on success, otherwise negative on error
 */
int ice_cpi_exec(struct ice_hw *hw, u8 phy,
		 const struct ice_cpi_cmd *cmd,
		 struct ice_cpi_resp *resp)
{
	struct mutex *cpi_lock; /* serializes CPI transactions per PHY */
	u32 phy_cmd, lm_cmd = 0;
	int err, err1 = 0;

	if (!cmd || !resp)
		return -EINVAL;

	cpi_lock = ice_cpi_phy_lock(hw, phy);
	if (!cpi_lock)
		return -EINVAL;

	mutex_lock(cpi_lock);

	lm_cmd =
		FIELD_PREP(CPI_LM_CMD_REQ_M, CPI_LM_CMD_REQ) |
		FIELD_PREP(CPI_LM_CMD_GET_SET_M, cmd->set) |
		FIELD_PREP(CPI_LM_CMD_OPCODE_M, cmd->opcode) |
		FIELD_PREP(CPI_LM_CMD_PORTLANE_M, cmd->port) |
		FIELD_PREP(CPI_LM_CMD_DATA_M, cmd->data);

	/* 1. Try to acquire the bus, PHY ACK should be low before we begin */
	err = ice_cpi_wait_req0_ack0(hw, phy);
	if (err)
		goto cpi_exec_exit;

	/* 2. We start the CPI request */
	err = ice_cpi_exec_cmd(hw, phy, lm_cmd);
	if (err)
		goto cpi_deassert;

	/*
	 * 3. Wait for CPI confirmation, PHY ACK should be asserted and opcode
	 *    echoed in the response
	 */
	err = ice_cpi_wait_ack1(hw, phy, &phy_cmd);
	if (err)
		goto cpi_deassert;

	if (FIELD_GET(CPI_LM_CMD_OPCODE_M, lm_cmd) !=
	    FIELD_GET(CPI_PHY_CMD_OPCODE_M, phy_cmd)) {
		err = -EFAULT;
		goto cpi_deassert;
	}

	resp->opcode = FIELD_GET(CPI_PHY_CMD_OPCODE_M, phy_cmd);
	resp->data = FIELD_GET(CPI_PHY_CMD_DATA_M, phy_cmd);
	resp->port = FIELD_GET(CPI_PHY_CMD_PORTLANE_M, phy_cmd);

cpi_deassert:
	/* 4. We deassert REQ */
	err1 = ice_cpi_req0(hw, phy, lm_cmd);
	if (err1)
		goto cpi_exec_exit;

	/* 5. PHY ACK should be deasserted in response */
	err1 = ice_cpi_wait_ack0(hw, phy);

cpi_exec_exit:
	if (!err)
		err = err1;

	mutex_unlock(cpi_lock);

	return err;
}

/**
 * ice_cpi_set_cmd - execute CPI SET command
 * @hw: pointer to the HW struct
 * @opcode: CPI command opcode
 * @phy: phy index CPI command is applied for
 * @port_lane: ephy index CPI command is applied for
 * @data: CPI opcode context specific data
 *
 * Return: 0 on success, negative error code on failure.
 */
static int ice_cpi_set_cmd(struct ice_hw *hw, u8 opcode, u8 phy, u8 port_lane,
			   u16 data)
{
	struct ice_cpi_resp cpi_resp = {0};
	struct ice_cpi_cmd cpi_cmd = {
		.opcode = opcode,
		.set = true,
		.port = port_lane,
		.data = data,
	};

	return ice_cpi_exec(hw, phy, &cpi_cmd, &cpi_resp);
}

/**
 * ice_cpi_ena_dis_clk_ref - enables/disables Tx reference clock on port
 * @hw: pointer to the HW struct
 * @phy: phy index of port for which Tx reference clock is enabled/disabled
 * @clk: Tx reference clock to enable or disable
 * @enable: bool value to enable or disable Tx reference clock
 *
 * This function executes CPI request to enable or disable specific
 * Tx reference clock on given PHY.
 *
 * Return: 0 on success, negative error code on failure.
 */
int ice_cpi_ena_dis_clk_ref(struct ice_hw *hw, u8 phy,
			    enum ice_e825c_ref_clk clk, bool enable)
{
	u16 val;

	val = FIELD_PREP(CPI_OPCODE_PHY_CLK_PHY_SEL_M, phy) |
	      FIELD_PREP(CPI_OPCODE_PHY_CLK_REF_CTRL_M,
			 enable ? CPI_OPCODE_PHY_CLK_ENABLE :
			 CPI_OPCODE_PHY_CLK_DISABLE) |
	      FIELD_PREP(CPI_OPCODE_PHY_CLK_REF_SEL_M, clk);

	return ice_cpi_set_cmd(hw, CPI_OPCODE_PHY_CLK, phy, 0, val);
}

