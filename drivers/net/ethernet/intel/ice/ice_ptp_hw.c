// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021, Intel Corporation. */

#include <linux/delay.h>
#include "ice_common.h"
#include "ice_ptp_hw.h"
#include "ice_ptp_consts.h"
#include "ice_cgu_regs.h"

static struct dpll_pin_frequency ice_cgu_pin_freq_common[] = {
	DPLL_PIN_FREQUENCY_1PPS,
	DPLL_PIN_FREQUENCY_10MHZ,
};

static struct dpll_pin_frequency ice_cgu_pin_freq_1_hz[] = {
	DPLL_PIN_FREQUENCY_1PPS,
};

static struct dpll_pin_frequency ice_cgu_pin_freq_10_mhz[] = {
	DPLL_PIN_FREQUENCY_10MHZ,
};

static const struct ice_cgu_pin_desc ice_e810t_sfp_cgu_inputs[] = {
	{ "CVL-SDP22",	  ZL_REF0P, DPLL_PIN_TYPE_INT_OSCILLATOR,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "CVL-SDP20",	  ZL_REF0N, DPLL_PIN_TYPE_INT_OSCILLATOR,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "C827_0-RCLKA", ZL_REF1P, DPLL_PIN_TYPE_MUX, 0, },
	{ "C827_0-RCLKB", ZL_REF1N, DPLL_PIN_TYPE_MUX, 0, },
	{ "SMA1",	  ZL_REF3P, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "SMA2/U.FL2",	  ZL_REF3N, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "GNSS-1PPS",	  ZL_REF4P, DPLL_PIN_TYPE_GNSS,
		ARRAY_SIZE(ice_cgu_pin_freq_1_hz), ice_cgu_pin_freq_1_hz },
	{ "OCXO",	  ZL_REF4N, DPLL_PIN_TYPE_INT_OSCILLATOR, 0, },
};

static const struct ice_cgu_pin_desc ice_e810t_qsfp_cgu_inputs[] = {
	{ "CVL-SDP22",	  ZL_REF0P, DPLL_PIN_TYPE_INT_OSCILLATOR,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "CVL-SDP20",	  ZL_REF0N, DPLL_PIN_TYPE_INT_OSCILLATOR,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "C827_0-RCLKA", ZL_REF1P, DPLL_PIN_TYPE_MUX, },
	{ "C827_0-RCLKB", ZL_REF1N, DPLL_PIN_TYPE_MUX, },
	{ "C827_1-RCLKA", ZL_REF2P, DPLL_PIN_TYPE_MUX, },
	{ "C827_1-RCLKB", ZL_REF2N, DPLL_PIN_TYPE_MUX, },
	{ "SMA1",	  ZL_REF3P, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "SMA2/U.FL2",	  ZL_REF3N, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "GNSS-1PPS",	  ZL_REF4P, DPLL_PIN_TYPE_GNSS,
		ARRAY_SIZE(ice_cgu_pin_freq_1_hz), ice_cgu_pin_freq_1_hz },
	{ "OCXO",	  ZL_REF4N, DPLL_PIN_TYPE_INT_OSCILLATOR, },
};

static const struct ice_cgu_pin_desc ice_e810t_sfp_cgu_outputs[] = {
	{ "REF-SMA1",	    ZL_OUT0, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "REF-SMA2/U.FL2", ZL_OUT1, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "PHY-CLK",	    ZL_OUT2, DPLL_PIN_TYPE_SYNCE_ETH_PORT, },
	{ "MAC-CLK",	    ZL_OUT3, DPLL_PIN_TYPE_SYNCE_ETH_PORT, },
	{ "CVL-SDP21",	    ZL_OUT4, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_1_hz), ice_cgu_pin_freq_1_hz },
	{ "CVL-SDP23",	    ZL_OUT5, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_1_hz), ice_cgu_pin_freq_1_hz },
};

static const struct ice_cgu_pin_desc ice_e810t_qsfp_cgu_outputs[] = {
	{ "REF-SMA1",	    ZL_OUT0, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "REF-SMA2/U.FL2", ZL_OUT1, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "PHY-CLK",	    ZL_OUT2, DPLL_PIN_TYPE_SYNCE_ETH_PORT, 0 },
	{ "PHY2-CLK",	    ZL_OUT3, DPLL_PIN_TYPE_SYNCE_ETH_PORT, 0 },
	{ "MAC-CLK",	    ZL_OUT4, DPLL_PIN_TYPE_SYNCE_ETH_PORT, 0 },
	{ "CVL-SDP21",	    ZL_OUT5, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_1_hz), ice_cgu_pin_freq_1_hz },
	{ "CVL-SDP23",	    ZL_OUT6, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_1_hz), ice_cgu_pin_freq_1_hz },
};

static const struct ice_cgu_pin_desc ice_e823_si_cgu_inputs[] = {
	{ "NONE",	  SI_REF0P, 0, 0 },
	{ "NONE",	  SI_REF0N, 0, 0 },
	{ "SYNCE0_DP",	  SI_REF1P, DPLL_PIN_TYPE_MUX, 0 },
	{ "SYNCE0_DN",	  SI_REF1N, DPLL_PIN_TYPE_MUX, 0 },
	{ "EXT_CLK_SYNC", SI_REF2P, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "NONE",	  SI_REF2N, 0, 0 },
	{ "EXT_PPS_OUT",  SI_REF3,  DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "INT_PPS_OUT",  SI_REF4,  DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
};

static const struct ice_cgu_pin_desc ice_e823_si_cgu_outputs[] = {
	{ "1588-TIME_SYNC", SI_OUT0, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "PHY-CLK",	    SI_OUT1, DPLL_PIN_TYPE_SYNCE_ETH_PORT, 0 },
	{ "10MHZ-SMA2",	    SI_OUT2, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_10_mhz), ice_cgu_pin_freq_10_mhz },
	{ "PPS-SMA1",	    SI_OUT3, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
};

static const struct ice_cgu_pin_desc ice_e823_zl_cgu_inputs[] = {
	{ "NONE",	  ZL_REF0P, 0, 0 },
	{ "INT_PPS_OUT",  ZL_REF0N, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_1_hz), ice_cgu_pin_freq_1_hz },
	{ "SYNCE0_DP",	  ZL_REF1P, DPLL_PIN_TYPE_MUX, 0 },
	{ "SYNCE0_DN",	  ZL_REF1N, DPLL_PIN_TYPE_MUX, 0 },
	{ "NONE",	  ZL_REF2P, 0, 0 },
	{ "NONE",	  ZL_REF2N, 0, 0 },
	{ "EXT_CLK_SYNC", ZL_REF3P, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "NONE",	  ZL_REF3N, 0, 0 },
	{ "EXT_PPS_OUT",  ZL_REF4P, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_1_hz), ice_cgu_pin_freq_1_hz },
	{ "OCXO",	  ZL_REF4N, DPLL_PIN_TYPE_INT_OSCILLATOR, 0 },
};

static const struct ice_cgu_pin_desc ice_e823_zl_cgu_outputs[] = {
	{ "PPS-SMA1",	   ZL_OUT0, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_1_hz), ice_cgu_pin_freq_1_hz },
	{ "10MHZ-SMA2",	   ZL_OUT1, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_10_mhz), ice_cgu_pin_freq_10_mhz },
	{ "PHY-CLK",	   ZL_OUT2, DPLL_PIN_TYPE_SYNCE_ETH_PORT, 0 },
	{ "1588-TIME_REF", ZL_OUT3, DPLL_PIN_TYPE_SYNCE_ETH_PORT, 0 },
	{ "CPK-TIME_SYNC", ZL_OUT4, DPLL_PIN_TYPE_EXT,
		ARRAY_SIZE(ice_cgu_pin_freq_common), ice_cgu_pin_freq_common },
	{ "NONE",	   ZL_OUT5, 0, 0 },
};

/* Low level functions for interacting with and managing the device clock used
 * for the Precision Time Protocol.
 *
 * The ice hardware represents the current time using three registers:
 *
 *    GLTSYN_TIME_H     GLTSYN_TIME_L     GLTSYN_TIME_R
 *  +---------------+ +---------------+ +---------------+
 *  |    32 bits    | |    32 bits    | |    32 bits    |
 *  +---------------+ +---------------+ +---------------+
 *
 * The registers are incremented every clock tick using a 40bit increment
 * value defined over two registers:
 *
 *                     GLTSYN_INCVAL_H   GLTSYN_INCVAL_L
 *                    +---------------+ +---------------+
 *                    |    8 bit s    | |    32 bits    |
 *                    +---------------+ +---------------+
 *
 * The increment value is added to the GLSTYN_TIME_R and GLSTYN_TIME_L
 * registers every clock source tick. Depending on the specific device
 * configuration, the clock source frequency could be one of a number of
 * values.
 *
 * For E810 devices, the increment frequency is 812.5 MHz
 *
 * For E822 devices the clock can be derived from different sources, and the
 * increment has an effective frequency of one of the following:
 * - 823.4375 MHz
 * - 783.36 MHz
 * - 796.875 MHz
 * - 816 MHz
 * - 830.078125 MHz
 * - 783.36 MHz
 *
 * The hardware captures timestamps in the PHY for incoming packets, and for
 * outgoing packets on request. To support this, the PHY maintains a timer
 * that matches the lower 64 bits of the global source timer.
 *
 * In order to ensure that the PHY timers and the source timer are equivalent,
 * shadow registers are used to prepare the desired initial values. A special
 * sync command is issued to trigger copying from the shadow registers into
 * the appropriate source and PHY registers simultaneously.
 *
 * The driver supports devices which have different PHYs with subtly different
 * mechanisms to program and control the timers. We divide the devices into
 * families named after the first major device, E810 and similar devices, and
 * E822 and similar devices.
 *
 * - E822 based devices have additional support for fine grained Vernier
 *   calibration which requires significant setup
 * - The layout of timestamp data in the PHY register blocks is different
 * - The way timer synchronization commands are issued is different.
 *
 * To support this, very low level functions have an e810 or e822 suffix
 * indicating what type of device they work on. Higher level abstractions for
 * tasks that can be done on both devices do not have the suffix and will
 * correctly look up the appropriate low level function when running.
 *
 * Functions which only make sense on a single device family may not have
 * a suitable generic implementation
 */

/**
 * ice_get_ptp_src_clock_index - determine source clock index
 * @hw: pointer to HW struct
 *
 * Determine the source clock index currently in use, based on device
 * capabilities reported during initialization.
 */
u8 ice_get_ptp_src_clock_index(struct ice_hw *hw)
{
	return hw->func_caps.ts_func_info.tmr_index_assoc;
}

/**
 * ice_ptp_read_src_incval - Read source timer increment value
 * @hw: pointer to HW struct
 *
 * Read the increment value of the source timer and return it.
 */
static u64 ice_ptp_read_src_incval(struct ice_hw *hw)
{
	u32 lo, hi;
	u8 tmr_idx;

	tmr_idx = ice_get_ptp_src_clock_index(hw);

	lo = rd32(hw, GLTSYN_INCVAL_L(tmr_idx));
	hi = rd32(hw, GLTSYN_INCVAL_H(tmr_idx));

	return ((u64)(hi & INCVAL_HIGH_M) << 32) | lo;
}

/**
 * ice_ptp_src_cmd - Prepare source timer for a timer command
 * @hw: pointer to HW structure
 * @cmd: Timer command
 *
 * Prepare the source timer for an upcoming timer sync command.
 */
void ice_ptp_src_cmd(struct ice_hw *hw, enum ice_ptp_tmr_cmd cmd)
{
	u32 cmd_val;
	u8 tmr_idx;

	tmr_idx = ice_get_ptp_src_clock_index(hw);
	cmd_val = tmr_idx << SEL_CPK_SRC;

	switch (cmd) {
	case ICE_PTP_INIT_TIME:
		cmd_val |= GLTSYN_CMD_INIT_TIME;
		break;
	case ICE_PTP_INIT_INCVAL:
		cmd_val |= GLTSYN_CMD_INIT_INCVAL;
		break;
	case ICE_PTP_ADJ_TIME:
		cmd_val |= GLTSYN_CMD_ADJ_TIME;
		break;
	case ICE_PTP_ADJ_TIME_AT_TIME:
		cmd_val |= GLTSYN_CMD_ADJ_INIT_TIME;
		break;
	case ICE_PTP_READ_TIME:
		cmd_val |= GLTSYN_CMD_READ_TIME;
		break;
	case ICE_PTP_NOP:
		break;
	}

	wr32(hw, GLTSYN_CMD, cmd_val);
}

/**
 * ice_ptp_exec_tmr_cmd - Execute all prepared timer commands
 * @hw: pointer to HW struct
 *
 * Write the SYNC_EXEC_CMD bit to the GLTSYN_CMD_SYNC register, and flush the
 * write immediately. This triggers the hardware to begin executing all of the
 * source and PHY timer commands synchronously.
 */
static void ice_ptp_exec_tmr_cmd(struct ice_hw *hw)
{
	wr32(hw, GLTSYN_CMD_SYNC, SYNC_EXEC_CMD);
	ice_flush(hw);
}

/* E822 family functions
 *
 * The following functions operate on the E822 family of devices.
 */

/**
 * ice_fill_phy_msg_e822 - Fill message data for a PHY register access
 * @msg: the PHY message buffer to fill in
 * @port: the port to access
 * @offset: the register offset
 */
static void
ice_fill_phy_msg_e822(struct ice_sbq_msg_input *msg, u8 port, u16 offset)
{
	int phy_port, phy, quadtype;

	phy_port = port % ICE_PORTS_PER_PHY_E822;
	phy = port / ICE_PORTS_PER_PHY_E822;
	quadtype = (port / ICE_PORTS_PER_QUAD) % ICE_QUADS_PER_PHY_E822;

	if (quadtype == 0) {
		msg->msg_addr_low = P_Q0_L(P_0_BASE + offset, phy_port);
		msg->msg_addr_high = P_Q0_H(P_0_BASE + offset, phy_port);
	} else {
		msg->msg_addr_low = P_Q1_L(P_4_BASE + offset, phy_port);
		msg->msg_addr_high = P_Q1_H(P_4_BASE + offset, phy_port);
	}

	if (phy == 0)
		msg->dest_dev = rmn_0;
	else if (phy == 1)
		msg->dest_dev = rmn_1;
	else
		msg->dest_dev = rmn_2;
}

/**
 * ice_is_64b_phy_reg_e822 - Check if this is a 64bit PHY register
 * @low_addr: the low address to check
 * @high_addr: on return, contains the high address of the 64bit register
 *
 * Checks if the provided low address is one of the known 64bit PHY values
 * represented as two 32bit registers. If it is, return the appropriate high
 * register offset to use.
 */
static bool ice_is_64b_phy_reg_e822(u16 low_addr, u16 *high_addr)
{
	switch (low_addr) {
	case P_REG_PAR_PCS_TX_OFFSET_L:
		*high_addr = P_REG_PAR_PCS_TX_OFFSET_U;
		return true;
	case P_REG_PAR_PCS_RX_OFFSET_L:
		*high_addr = P_REG_PAR_PCS_RX_OFFSET_U;
		return true;
	case P_REG_PAR_TX_TIME_L:
		*high_addr = P_REG_PAR_TX_TIME_U;
		return true;
	case P_REG_PAR_RX_TIME_L:
		*high_addr = P_REG_PAR_RX_TIME_U;
		return true;
	case P_REG_TOTAL_TX_OFFSET_L:
		*high_addr = P_REG_TOTAL_TX_OFFSET_U;
		return true;
	case P_REG_TOTAL_RX_OFFSET_L:
		*high_addr = P_REG_TOTAL_RX_OFFSET_U;
		return true;
	case P_REG_UIX66_10G_40G_L:
		*high_addr = P_REG_UIX66_10G_40G_U;
		return true;
	case P_REG_UIX66_25G_100G_L:
		*high_addr = P_REG_UIX66_25G_100G_U;
		return true;
	case P_REG_TX_CAPTURE_L:
		*high_addr = P_REG_TX_CAPTURE_U;
		return true;
	case P_REG_RX_CAPTURE_L:
		*high_addr = P_REG_RX_CAPTURE_U;
		return true;
	case P_REG_TX_TIMER_INC_PRE_L:
		*high_addr = P_REG_TX_TIMER_INC_PRE_U;
		return true;
	case P_REG_RX_TIMER_INC_PRE_L:
		*high_addr = P_REG_RX_TIMER_INC_PRE_U;
		return true;
	default:
		return false;
	}
}

/**
 * ice_is_40b_phy_reg_e822 - Check if this is a 40bit PHY register
 * @low_addr: the low address to check
 * @high_addr: on return, contains the high address of the 40bit value
 *
 * Checks if the provided low address is one of the known 40bit PHY values
 * split into two registers with the lower 8 bits in the low register and the
 * upper 32 bits in the high register. If it is, return the appropriate high
 * register offset to use.
 */
static bool ice_is_40b_phy_reg_e822(u16 low_addr, u16 *high_addr)
{
	switch (low_addr) {
	case P_REG_TIMETUS_L:
		*high_addr = P_REG_TIMETUS_U;
		return true;
	case P_REG_PAR_RX_TUS_L:
		*high_addr = P_REG_PAR_RX_TUS_U;
		return true;
	case P_REG_PAR_TX_TUS_L:
		*high_addr = P_REG_PAR_TX_TUS_U;
		return true;
	case P_REG_PCS_RX_TUS_L:
		*high_addr = P_REG_PCS_RX_TUS_U;
		return true;
	case P_REG_PCS_TX_TUS_L:
		*high_addr = P_REG_PCS_TX_TUS_U;
		return true;
	case P_REG_DESK_PAR_RX_TUS_L:
		*high_addr = P_REG_DESK_PAR_RX_TUS_U;
		return true;
	case P_REG_DESK_PAR_TX_TUS_L:
		*high_addr = P_REG_DESK_PAR_TX_TUS_U;
		return true;
	case P_REG_DESK_PCS_RX_TUS_L:
		*high_addr = P_REG_DESK_PCS_RX_TUS_U;
		return true;
	case P_REG_DESK_PCS_TX_TUS_L:
		*high_addr = P_REG_DESK_PCS_TX_TUS_U;
		return true;
	default:
		return false;
	}
}

/**
 * ice_read_phy_reg_e822 - Read a PHY register
 * @hw: pointer to the HW struct
 * @port: PHY port to read from
 * @offset: PHY register offset to read
 * @val: on return, the contents read from the PHY
 *
 * Read a PHY register for the given port over the device sideband queue.
 */
static int
ice_read_phy_reg_e822(struct ice_hw *hw, u8 port, u16 offset, u32 *val)
{
	struct ice_sbq_msg_input msg = {0};
	int err;

	ice_fill_phy_msg_e822(&msg, port, offset);
	msg.opcode = ice_sbq_msg_rd;

	err = ice_sbq_rw_reg(hw, &msg);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to send message to PHY, err %d\n",
			  err);
		return err;
	}

	*val = msg.data;

	return 0;
}

/**
 * ice_read_64b_phy_reg_e822 - Read a 64bit value from PHY registers
 * @hw: pointer to the HW struct
 * @port: PHY port to read from
 * @low_addr: offset of the lower register to read from
 * @val: on return, the contents of the 64bit value from the PHY registers
 *
 * Reads the two registers associated with a 64bit value and returns it in the
 * val pointer. The offset always specifies the lower register offset to use.
 * The high offset is looked up. This function only operates on registers
 * known to be two parts of a 64bit value.
 */
static int
ice_read_64b_phy_reg_e822(struct ice_hw *hw, u8 port, u16 low_addr, u64 *val)
{
	u32 low, high;
	u16 high_addr;
	int err;

	/* Only operate on registers known to be split into two 32bit
	 * registers.
	 */
	if (!ice_is_64b_phy_reg_e822(low_addr, &high_addr)) {
		ice_debug(hw, ICE_DBG_PTP, "Invalid 64b register addr 0x%08x\n",
			  low_addr);
		return -EINVAL;
	}

	err = ice_read_phy_reg_e822(hw, port, low_addr, &low);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read from low register 0x%08x\n, err %d",
			  low_addr, err);
		return err;
	}

	err = ice_read_phy_reg_e822(hw, port, high_addr, &high);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read from high register 0x%08x\n, err %d",
			  high_addr, err);
		return err;
	}

	*val = (u64)high << 32 | low;

	return 0;
}

/**
 * ice_write_phy_reg_e822 - Write a PHY register
 * @hw: pointer to the HW struct
 * @port: PHY port to write to
 * @offset: PHY register offset to write
 * @val: The value to write to the register
 *
 * Write a PHY register for the given port over the device sideband queue.
 */
static int
ice_write_phy_reg_e822(struct ice_hw *hw, u8 port, u16 offset, u32 val)
{
	struct ice_sbq_msg_input msg = {0};
	int err;

	ice_fill_phy_msg_e822(&msg, port, offset);
	msg.opcode = ice_sbq_msg_wr;
	msg.data = val;

	err = ice_sbq_rw_reg(hw, &msg);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to send message to PHY, err %d\n",
			  err);
		return err;
	}

	return 0;
}

/**
 * ice_write_40b_phy_reg_e822 - Write a 40b value to the PHY
 * @hw: pointer to the HW struct
 * @port: port to write to
 * @low_addr: offset of the low register
 * @val: 40b value to write
 *
 * Write the provided 40b value to the two associated registers by splitting
 * it up into two chunks, the lower 8 bits and the upper 32 bits.
 */
static int
ice_write_40b_phy_reg_e822(struct ice_hw *hw, u8 port, u16 low_addr, u64 val)
{
	u32 low, high;
	u16 high_addr;
	int err;

	/* Only operate on registers known to be split into a lower 8 bit
	 * register and an upper 32 bit register.
	 */
	if (!ice_is_40b_phy_reg_e822(low_addr, &high_addr)) {
		ice_debug(hw, ICE_DBG_PTP, "Invalid 40b register addr 0x%08x\n",
			  low_addr);
		return -EINVAL;
	}

	low = (u32)(val & P_REG_40B_LOW_M);
	high = (u32)(val >> P_REG_40B_HIGH_S);

	err = ice_write_phy_reg_e822(hw, port, low_addr, low);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write to low register 0x%08x\n, err %d",
			  low_addr, err);
		return err;
	}

	err = ice_write_phy_reg_e822(hw, port, high_addr, high);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write to high register 0x%08x\n, err %d",
			  high_addr, err);
		return err;
	}

	return 0;
}

/**
 * ice_write_64b_phy_reg_e822 - Write a 64bit value to PHY registers
 * @hw: pointer to the HW struct
 * @port: PHY port to read from
 * @low_addr: offset of the lower register to read from
 * @val: the contents of the 64bit value to write to PHY
 *
 * Write the 64bit value to the two associated 32bit PHY registers. The offset
 * is always specified as the lower register, and the high address is looked
 * up. This function only operates on registers known to be two parts of
 * a 64bit value.
 */
static int
ice_write_64b_phy_reg_e822(struct ice_hw *hw, u8 port, u16 low_addr, u64 val)
{
	u32 low, high;
	u16 high_addr;
	int err;

	/* Only operate on registers known to be split into two 32bit
	 * registers.
	 */
	if (!ice_is_64b_phy_reg_e822(low_addr, &high_addr)) {
		ice_debug(hw, ICE_DBG_PTP, "Invalid 64b register addr 0x%08x\n",
			  low_addr);
		return -EINVAL;
	}

	low = lower_32_bits(val);
	high = upper_32_bits(val);

	err = ice_write_phy_reg_e822(hw, port, low_addr, low);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write to low register 0x%08x\n, err %d",
			  low_addr, err);
		return err;
	}

	err = ice_write_phy_reg_e822(hw, port, high_addr, high);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write to high register 0x%08x\n, err %d",
			  high_addr, err);
		return err;
	}

	return 0;
}

/**
 * ice_fill_quad_msg_e822 - Fill message data for quad register access
 * @msg: the PHY message buffer to fill in
 * @quad: the quad to access
 * @offset: the register offset
 *
 * Fill a message buffer for accessing a register in a quad shared between
 * multiple PHYs.
 */
static int
ice_fill_quad_msg_e822(struct ice_sbq_msg_input *msg, u8 quad, u16 offset)
{
	u32 addr;

	if (quad >= ICE_MAX_QUAD)
		return -EINVAL;

	msg->dest_dev = rmn_0;

	if ((quad % ICE_QUADS_PER_PHY_E822) == 0)
		addr = Q_0_BASE + offset;
	else
		addr = Q_1_BASE + offset;

	msg->msg_addr_low = lower_16_bits(addr);
	msg->msg_addr_high = upper_16_bits(addr);

	return 0;
}

/**
 * ice_read_quad_reg_e822 - Read a PHY quad register
 * @hw: pointer to the HW struct
 * @quad: quad to read from
 * @offset: quad register offset to read
 * @val: on return, the contents read from the quad
 *
 * Read a quad register over the device sideband queue. Quad registers are
 * shared between multiple PHYs.
 */
int
ice_read_quad_reg_e822(struct ice_hw *hw, u8 quad, u16 offset, u32 *val)
{
	struct ice_sbq_msg_input msg = {0};
	int err;

	err = ice_fill_quad_msg_e822(&msg, quad, offset);
	if (err)
		return err;

	msg.opcode = ice_sbq_msg_rd;

	err = ice_sbq_rw_reg(hw, &msg);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to send message to PHY, err %d\n",
			  err);
		return err;
	}

	*val = msg.data;

	return 0;
}

/**
 * ice_write_quad_reg_e822 - Write a PHY quad register
 * @hw: pointer to the HW struct
 * @quad: quad to write to
 * @offset: quad register offset to write
 * @val: The value to write to the register
 *
 * Write a quad register over the device sideband queue. Quad registers are
 * shared between multiple PHYs.
 */
int
ice_write_quad_reg_e822(struct ice_hw *hw, u8 quad, u16 offset, u32 val)
{
	struct ice_sbq_msg_input msg = {0};
	int err;

	err = ice_fill_quad_msg_e822(&msg, quad, offset);
	if (err)
		return err;

	msg.opcode = ice_sbq_msg_wr;
	msg.data = val;

	err = ice_sbq_rw_reg(hw, &msg);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to send message to PHY, err %d\n",
			  err);
		return err;
	}

	return 0;
}

/**
 * ice_read_phy_tstamp_e822 - Read a PHY timestamp out of the quad block
 * @hw: pointer to the HW struct
 * @quad: the quad to read from
 * @idx: the timestamp index to read
 * @tstamp: on return, the 40bit timestamp value
 *
 * Read a 40bit timestamp value out of the two associated registers in the
 * quad memory block that is shared between the internal PHYs of the E822
 * family of devices.
 */
static int
ice_read_phy_tstamp_e822(struct ice_hw *hw, u8 quad, u8 idx, u64 *tstamp)
{
	u16 lo_addr, hi_addr;
	u32 lo, hi;
	int err;

	lo_addr = (u16)TS_L(Q_REG_TX_MEMORY_BANK_START, idx);
	hi_addr = (u16)TS_H(Q_REG_TX_MEMORY_BANK_START, idx);

	err = ice_read_quad_reg_e822(hw, quad, lo_addr, &lo);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read low PTP timestamp register, err %d\n",
			  err);
		return err;
	}

	err = ice_read_quad_reg_e822(hw, quad, hi_addr, &hi);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read high PTP timestamp register, err %d\n",
			  err);
		return err;
	}

	/* For E822 based internal PHYs, the timestamp is reported with the
	 * lower 8 bits in the low register, and the upper 32 bits in the high
	 * register.
	 */
	*tstamp = ((u64)hi) << TS_PHY_HIGH_S | ((u64)lo & TS_PHY_LOW_M);

	return 0;
}

/**
 * ice_clear_phy_tstamp_e822 - Clear a timestamp from the quad block
 * @hw: pointer to the HW struct
 * @quad: the quad to read from
 * @idx: the timestamp index to reset
 *
 * Read the timestamp out of the quad to clear its timestamp status bit from
 * the PHY quad block that is shared between the internal PHYs of the E822
 * devices.
 *
 * Note that unlike E810, software cannot directly write to the quad memory
 * bank registers. E822 relies on the ice_get_phy_tx_tstamp_ready() function
 * to determine which timestamps are valid. Reading a timestamp auto-clears
 * the valid bit.
 *
 * To directly clear the contents of the timestamp block entirely, discarding
 * all timestamp data at once, software should instead use
 * ice_ptp_reset_ts_memory_quad_e822().
 *
 * This function should only be called on an idx whose bit is set according to
 * ice_get_phy_tx_tstamp_ready().
 */
static int
ice_clear_phy_tstamp_e822(struct ice_hw *hw, u8 quad, u8 idx)
{
	u64 unused_tstamp;
	int err;

	err = ice_read_phy_tstamp_e822(hw, quad, idx, &unused_tstamp);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read the timestamp register for quad %u, idx %u, err %d\n",
			  quad, idx, err);
		return err;
	}

	return 0;
}

/**
 * ice_ptp_reset_ts_memory_quad_e822 - Clear all timestamps from the quad block
 * @hw: pointer to the HW struct
 * @quad: the quad to read from
 *
 * Clear all timestamps from the PHY quad block that is shared between the
 * internal PHYs on the E822 devices.
 */
void ice_ptp_reset_ts_memory_quad_e822(struct ice_hw *hw, u8 quad)
{
	ice_write_quad_reg_e822(hw, quad, Q_REG_TS_CTRL, Q_REG_TS_CTRL_M);
	ice_write_quad_reg_e822(hw, quad, Q_REG_TS_CTRL, ~(u32)Q_REG_TS_CTRL_M);
}

/**
 * ice_ptp_reset_ts_memory_e822 - Clear all timestamps from all quad blocks
 * @hw: pointer to the HW struct
 */
static void ice_ptp_reset_ts_memory_e822(struct ice_hw *hw)
{
	unsigned int quad;

	for (quad = 0; quad < ICE_MAX_QUAD; quad++)
		ice_ptp_reset_ts_memory_quad_e822(hw, quad);
}

/**
 * ice_read_cgu_reg_e822 - Read a CGU register
 * @hw: pointer to the HW struct
 * @addr: Register address to read
 * @val: storage for register value read
 *
 * Read the contents of a register of the Clock Generation Unit. Only
 * applicable to E822 devices.
 */
static int
ice_read_cgu_reg_e822(struct ice_hw *hw, u32 addr, u32 *val)
{
	struct ice_sbq_msg_input cgu_msg;
	int err;

	cgu_msg.opcode = ice_sbq_msg_rd;
	cgu_msg.dest_dev = cgu;
	cgu_msg.msg_addr_low = addr;
	cgu_msg.msg_addr_high = 0x0;

	err = ice_sbq_rw_reg(hw, &cgu_msg);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read CGU register 0x%04x, err %d\n",
			  addr, err);
		return err;
	}

	*val = cgu_msg.data;

	return err;
}

/**
 * ice_write_cgu_reg_e822 - Write a CGU register
 * @hw: pointer to the HW struct
 * @addr: Register address to write
 * @val: value to write into the register
 *
 * Write the specified value to a register of the Clock Generation Unit. Only
 * applicable to E822 devices.
 */
static int
ice_write_cgu_reg_e822(struct ice_hw *hw, u32 addr, u32 val)
{
	struct ice_sbq_msg_input cgu_msg;
	int err;

	cgu_msg.opcode = ice_sbq_msg_wr;
	cgu_msg.dest_dev = cgu;
	cgu_msg.msg_addr_low = addr;
	cgu_msg.msg_addr_high = 0x0;
	cgu_msg.data = val;

	err = ice_sbq_rw_reg(hw, &cgu_msg);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write CGU register 0x%04x, err %d\n",
			  addr, err);
		return err;
	}

	return err;
}

/**
 * ice_clk_freq_str - Convert time_ref_freq to string
 * @clk_freq: Clock frequency
 *
 * Convert the specified TIME_REF clock frequency to a string.
 */
static const char *ice_clk_freq_str(u8 clk_freq)
{
	switch ((enum ice_time_ref_freq)clk_freq) {
	case ICE_TIME_REF_FREQ_25_000:
		return "25 MHz";
	case ICE_TIME_REF_FREQ_122_880:
		return "122.88 MHz";
	case ICE_TIME_REF_FREQ_125_000:
		return "125 MHz";
	case ICE_TIME_REF_FREQ_153_600:
		return "153.6 MHz";
	case ICE_TIME_REF_FREQ_156_250:
		return "156.25 MHz";
	case ICE_TIME_REF_FREQ_245_760:
		return "245.76 MHz";
	default:
		return "Unknown";
	}
}

/**
 * ice_clk_src_str - Convert time_ref_src to string
 * @clk_src: Clock source
 *
 * Convert the specified clock source to its string name.
 */
static const char *ice_clk_src_str(u8 clk_src)
{
	switch ((enum ice_clk_src)clk_src) {
	case ICE_CLK_SRC_TCX0:
		return "TCX0";
	case ICE_CLK_SRC_TIME_REF:
		return "TIME_REF";
	default:
		return "Unknown";
	}
}

/**
 * ice_cfg_cgu_pll_e822 - Configure the Clock Generation Unit
 * @hw: pointer to the HW struct
 * @clk_freq: Clock frequency to program
 * @clk_src: Clock source to select (TIME_REF, or TCX0)
 *
 * Configure the Clock Generation Unit with the desired clock frequency and
 * time reference, enabling the PLL which drives the PTP hardware clock.
 */
static int
ice_cfg_cgu_pll_e822(struct ice_hw *hw, enum ice_time_ref_freq clk_freq,
		     enum ice_clk_src clk_src)
{
	union tspll_ro_bwm_lf bwm_lf;
	union nac_cgu_dword19 dw19;
	union nac_cgu_dword22 dw22;
	union nac_cgu_dword24 dw24;
	union nac_cgu_dword9 dw9;
	int err;

	if (clk_freq >= NUM_ICE_TIME_REF_FREQ) {
		dev_warn(ice_hw_to_dev(hw), "Invalid TIME_REF frequency %u\n",
			 clk_freq);
		return -EINVAL;
	}

	if (clk_src >= NUM_ICE_CLK_SRC) {
		dev_warn(ice_hw_to_dev(hw), "Invalid clock source %u\n",
			 clk_src);
		return -EINVAL;
	}

	if (clk_src == ICE_CLK_SRC_TCX0 &&
	    clk_freq != ICE_TIME_REF_FREQ_25_000) {
		dev_warn(ice_hw_to_dev(hw),
			 "TCX0 only supports 25 MHz frequency\n");
		return -EINVAL;
	}

	err = ice_read_cgu_reg_e822(hw, NAC_CGU_DWORD9, &dw9.val);
	if (err)
		return err;

	err = ice_read_cgu_reg_e822(hw, NAC_CGU_DWORD24, &dw24.val);
	if (err)
		return err;

	err = ice_read_cgu_reg_e822(hw, TSPLL_RO_BWM_LF, &bwm_lf.val);
	if (err)
		return err;

	/* Log the current clock configuration */
	ice_debug(hw, ICE_DBG_PTP, "Current CGU configuration -- %s, clk_src %s, clk_freq %s, PLL %s\n",
		  dw24.field.ts_pll_enable ? "enabled" : "disabled",
		  ice_clk_src_str(dw24.field.time_ref_sel),
		  ice_clk_freq_str(dw9.field.time_ref_freq_sel),
		  bwm_lf.field.plllock_true_lock_cri ? "locked" : "unlocked");

	/* Disable the PLL before changing the clock source or frequency */
	if (dw24.field.ts_pll_enable) {
		dw24.field.ts_pll_enable = 0;

		err = ice_write_cgu_reg_e822(hw, NAC_CGU_DWORD24, dw24.val);
		if (err)
			return err;
	}

	/* Set the frequency */
	dw9.field.time_ref_freq_sel = clk_freq;
	err = ice_write_cgu_reg_e822(hw, NAC_CGU_DWORD9, dw9.val);
	if (err)
		return err;

	/* Configure the TS PLL feedback divisor */
	err = ice_read_cgu_reg_e822(hw, NAC_CGU_DWORD19, &dw19.val);
	if (err)
		return err;

	dw19.field.tspll_fbdiv_intgr = e822_cgu_params[clk_freq].feedback_div;
	dw19.field.tspll_ndivratio = 1;

	err = ice_write_cgu_reg_e822(hw, NAC_CGU_DWORD19, dw19.val);
	if (err)
		return err;

	/* Configure the TS PLL post divisor */
	err = ice_read_cgu_reg_e822(hw, NAC_CGU_DWORD22, &dw22.val);
	if (err)
		return err;

	dw22.field.time1588clk_div = e822_cgu_params[clk_freq].post_pll_div;
	dw22.field.time1588clk_sel_div2 = 0;

	err = ice_write_cgu_reg_e822(hw, NAC_CGU_DWORD22, dw22.val);
	if (err)
		return err;

	/* Configure the TS PLL pre divisor and clock source */
	err = ice_read_cgu_reg_e822(hw, NAC_CGU_DWORD24, &dw24.val);
	if (err)
		return err;

	dw24.field.ref1588_ck_div = e822_cgu_params[clk_freq].refclk_pre_div;
	dw24.field.tspll_fbdiv_frac = e822_cgu_params[clk_freq].frac_n_div;
	dw24.field.time_ref_sel = clk_src;

	err = ice_write_cgu_reg_e822(hw, NAC_CGU_DWORD24, dw24.val);
	if (err)
		return err;

	/* Finally, enable the PLL */
	dw24.field.ts_pll_enable = 1;

	err = ice_write_cgu_reg_e822(hw, NAC_CGU_DWORD24, dw24.val);
	if (err)
		return err;

	/* Wait to verify if the PLL locks */
	usleep_range(1000, 5000);

	err = ice_read_cgu_reg_e822(hw, TSPLL_RO_BWM_LF, &bwm_lf.val);
	if (err)
		return err;

	if (!bwm_lf.field.plllock_true_lock_cri) {
		dev_warn(ice_hw_to_dev(hw), "CGU PLL failed to lock\n");
		return -EBUSY;
	}

	/* Log the current clock configuration */
	ice_debug(hw, ICE_DBG_PTP, "New CGU configuration -- %s, clk_src %s, clk_freq %s, PLL %s\n",
		  dw24.field.ts_pll_enable ? "enabled" : "disabled",
		  ice_clk_src_str(dw24.field.time_ref_sel),
		  ice_clk_freq_str(dw9.field.time_ref_freq_sel),
		  bwm_lf.field.plllock_true_lock_cri ? "locked" : "unlocked");

	return 0;
}

/**
 * ice_init_cgu_e822 - Initialize CGU with settings from firmware
 * @hw: pointer to the HW structure
 *
 * Initialize the Clock Generation Unit of the E822 device.
 */
static int ice_init_cgu_e822(struct ice_hw *hw)
{
	struct ice_ts_func_info *ts_info = &hw->func_caps.ts_func_info;
	union tspll_cntr_bist_settings cntr_bist;
	int err;

	err = ice_read_cgu_reg_e822(hw, TSPLL_CNTR_BIST_SETTINGS,
				    &cntr_bist.val);
	if (err)
		return err;

	/* Disable sticky lock detection so lock err reported is accurate */
	cntr_bist.field.i_plllock_sel_0 = 0;
	cntr_bist.field.i_plllock_sel_1 = 0;

	err = ice_write_cgu_reg_e822(hw, TSPLL_CNTR_BIST_SETTINGS,
				     cntr_bist.val);
	if (err)
		return err;

	/* Configure the CGU PLL using the parameters from the function
	 * capabilities.
	 */
	err = ice_cfg_cgu_pll_e822(hw, ts_info->time_ref,
				   (enum ice_clk_src)ts_info->clk_src);
	if (err)
		return err;

	return 0;
}

/**
 * ice_ptp_set_vernier_wl - Set the window length for vernier calibration
 * @hw: pointer to the HW struct
 *
 * Set the window length used for the vernier port calibration process.
 */
static int ice_ptp_set_vernier_wl(struct ice_hw *hw)
{
	u8 port;

	for (port = 0; port < ICE_NUM_EXTERNAL_PORTS; port++) {
		int err;

		err = ice_write_phy_reg_e822(hw, port, P_REG_WL,
					     PTP_VERNIER_WL);
		if (err) {
			ice_debug(hw, ICE_DBG_PTP, "Failed to set vernier window length for port %u, err %d\n",
				  port, err);
			return err;
		}
	}

	return 0;
}

/**
 * ice_ptp_init_phc_e822 - Perform E822 specific PHC initialization
 * @hw: pointer to HW struct
 *
 * Perform PHC initialization steps specific to E822 devices.
 */
static int ice_ptp_init_phc_e822(struct ice_hw *hw)
{
	int err;
	u32 regval;

	/* Enable reading switch and PHY registers over the sideband queue */
#define PF_SB_REM_DEV_CTL_SWITCH_READ BIT(1)
#define PF_SB_REM_DEV_CTL_PHY0 BIT(2)
	regval = rd32(hw, PF_SB_REM_DEV_CTL);
	regval |= (PF_SB_REM_DEV_CTL_SWITCH_READ |
		   PF_SB_REM_DEV_CTL_PHY0);
	wr32(hw, PF_SB_REM_DEV_CTL, regval);

	/* Initialize the Clock Generation Unit */
	err = ice_init_cgu_e822(hw);
	if (err)
		return err;

	/* Set window length for all the ports */
	return ice_ptp_set_vernier_wl(hw);
}

/**
 * ice_ptp_prep_phy_time_e822 - Prepare PHY port with initial time
 * @hw: pointer to the HW struct
 * @time: Time to initialize the PHY port clocks to
 *
 * Program the PHY port registers with a new initial time value. The port
 * clock will be initialized once the driver issues an ICE_PTP_INIT_TIME sync
 * command. The time value is the upper 32 bits of the PHY timer, usually in
 * units of nominal nanoseconds.
 */
static int
ice_ptp_prep_phy_time_e822(struct ice_hw *hw, u32 time)
{
	u64 phy_time;
	u8 port;
	int err;

	/* The time represents the upper 32 bits of the PHY timer, so we need
	 * to shift to account for this when programming.
	 */
	phy_time = (u64)time << 32;

	for (port = 0; port < ICE_NUM_EXTERNAL_PORTS; port++) {
		/* Tx case */
		err = ice_write_64b_phy_reg_e822(hw, port,
						 P_REG_TX_TIMER_INC_PRE_L,
						 phy_time);
		if (err)
			goto exit_err;

		/* Rx case */
		err = ice_write_64b_phy_reg_e822(hw, port,
						 P_REG_RX_TIMER_INC_PRE_L,
						 phy_time);
		if (err)
			goto exit_err;
	}

	return 0;

exit_err:
	ice_debug(hw, ICE_DBG_PTP, "Failed to write init time for port %u, err %d\n",
		  port, err);

	return err;
}

/**
 * ice_ptp_prep_port_adj_e822 - Prepare a single port for time adjust
 * @hw: pointer to HW struct
 * @port: Port number to be programmed
 * @time: time in cycles to adjust the port Tx and Rx clocks
 *
 * Program the port for an atomic adjustment by writing the Tx and Rx timer
 * registers. The atomic adjustment won't be completed until the driver issues
 * an ICE_PTP_ADJ_TIME command.
 *
 * Note that time is not in units of nanoseconds. It is in clock time
 * including the lower sub-nanosecond portion of the port timer.
 *
 * Negative adjustments are supported using 2s complement arithmetic.
 */
static int
ice_ptp_prep_port_adj_e822(struct ice_hw *hw, u8 port, s64 time)
{
	u32 l_time, u_time;
	int err;

	l_time = lower_32_bits(time);
	u_time = upper_32_bits(time);

	/* Tx case */
	err = ice_write_phy_reg_e822(hw, port, P_REG_TX_TIMER_INC_PRE_L,
				     l_time);
	if (err)
		goto exit_err;

	err = ice_write_phy_reg_e822(hw, port, P_REG_TX_TIMER_INC_PRE_U,
				     u_time);
	if (err)
		goto exit_err;

	/* Rx case */
	err = ice_write_phy_reg_e822(hw, port, P_REG_RX_TIMER_INC_PRE_L,
				     l_time);
	if (err)
		goto exit_err;

	err = ice_write_phy_reg_e822(hw, port, P_REG_RX_TIMER_INC_PRE_U,
				     u_time);
	if (err)
		goto exit_err;

	return 0;

exit_err:
	ice_debug(hw, ICE_DBG_PTP, "Failed to write time adjust for port %u, err %d\n",
		  port, err);
	return err;
}

/**
 * ice_ptp_prep_phy_adj_e822 - Prep PHY ports for a time adjustment
 * @hw: pointer to HW struct
 * @adj: adjustment in nanoseconds
 *
 * Prepare the PHY ports for an atomic time adjustment by programming the PHY
 * Tx and Rx port registers. The actual adjustment is completed by issuing an
 * ICE_PTP_ADJ_TIME or ICE_PTP_ADJ_TIME_AT_TIME sync command.
 */
static int
ice_ptp_prep_phy_adj_e822(struct ice_hw *hw, s32 adj)
{
	s64 cycles;
	u8 port;

	/* The port clock supports adjustment of the sub-nanosecond portion of
	 * the clock. We shift the provided adjustment in nanoseconds to
	 * calculate the appropriate adjustment to program into the PHY ports.
	 */
	if (adj > 0)
		cycles = (s64)adj << 32;
	else
		cycles = -(((s64)-adj) << 32);

	for (port = 0; port < ICE_NUM_EXTERNAL_PORTS; port++) {
		int err;

		err = ice_ptp_prep_port_adj_e822(hw, port, cycles);
		if (err)
			return err;
	}

	return 0;
}

/**
 * ice_ptp_prep_phy_incval_e822 - Prepare PHY ports for time adjustment
 * @hw: pointer to HW struct
 * @incval: new increment value to prepare
 *
 * Prepare each of the PHY ports for a new increment value by programming the
 * port's TIMETUS registers. The new increment value will be updated after
 * issuing an ICE_PTP_INIT_INCVAL command.
 */
static int
ice_ptp_prep_phy_incval_e822(struct ice_hw *hw, u64 incval)
{
	int err;
	u8 port;

	for (port = 0; port < ICE_NUM_EXTERNAL_PORTS; port++) {
		err = ice_write_40b_phy_reg_e822(hw, port, P_REG_TIMETUS_L,
						 incval);
		if (err)
			goto exit_err;
	}

	return 0;

exit_err:
	ice_debug(hw, ICE_DBG_PTP, "Failed to write incval for port %u, err %d\n",
		  port, err);

	return err;
}

/**
 * ice_ptp_read_port_capture - Read a port's local time capture
 * @hw: pointer to HW struct
 * @port: Port number to read
 * @tx_ts: on return, the Tx port time capture
 * @rx_ts: on return, the Rx port time capture
 *
 * Read the port's Tx and Rx local time capture values.
 *
 * Note this has no equivalent for the E810 devices.
 */
static int
ice_ptp_read_port_capture(struct ice_hw *hw, u8 port, u64 *tx_ts, u64 *rx_ts)
{
	int err;

	/* Tx case */
	err = ice_read_64b_phy_reg_e822(hw, port, P_REG_TX_CAPTURE_L, tx_ts);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read REG_TX_CAPTURE, err %d\n",
			  err);
		return err;
	}

	ice_debug(hw, ICE_DBG_PTP, "tx_init = 0x%016llx\n",
		  (unsigned long long)*tx_ts);

	/* Rx case */
	err = ice_read_64b_phy_reg_e822(hw, port, P_REG_RX_CAPTURE_L, rx_ts);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read RX_CAPTURE, err %d\n",
			  err);
		return err;
	}

	ice_debug(hw, ICE_DBG_PTP, "rx_init = 0x%016llx\n",
		  (unsigned long long)*rx_ts);

	return 0;
}

/**
 * ice_ptp_write_port_cmd_e822 - Prepare a single PHY port for a timer command
 * @hw: pointer to HW struct
 * @port: Port to which cmd has to be sent
 * @cmd: Command to be sent to the port
 *
 * Prepare the requested port for an upcoming timer sync command.
 *
 * Do not use this function directly. If you want to configure exactly one
 * port, use ice_ptp_one_port_cmd() instead.
 */
static int
ice_ptp_write_port_cmd_e822(struct ice_hw *hw, u8 port, enum ice_ptp_tmr_cmd cmd)
{
	u32 cmd_val, val;
	u8 tmr_idx;
	int err;

	tmr_idx = ice_get_ptp_src_clock_index(hw);
	cmd_val = tmr_idx << SEL_PHY_SRC;
	switch (cmd) {
	case ICE_PTP_INIT_TIME:
		cmd_val |= PHY_CMD_INIT_TIME;
		break;
	case ICE_PTP_INIT_INCVAL:
		cmd_val |= PHY_CMD_INIT_INCVAL;
		break;
	case ICE_PTP_ADJ_TIME:
		cmd_val |= PHY_CMD_ADJ_TIME;
		break;
	case ICE_PTP_READ_TIME:
		cmd_val |= PHY_CMD_READ_TIME;
		break;
	case ICE_PTP_ADJ_TIME_AT_TIME:
		cmd_val |= PHY_CMD_ADJ_TIME_AT_TIME;
		break;
	case ICE_PTP_NOP:
		break;
	}

	/* Tx case */
	/* Read, modify, write */
	err = ice_read_phy_reg_e822(hw, port, P_REG_TX_TMR_CMD, &val);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read TX_TMR_CMD, err %d\n",
			  err);
		return err;
	}

	/* Modify necessary bits only and perform write */
	val &= ~TS_CMD_MASK;
	val |= cmd_val;

	err = ice_write_phy_reg_e822(hw, port, P_REG_TX_TMR_CMD, val);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write back TX_TMR_CMD, err %d\n",
			  err);
		return err;
	}

	/* Rx case */
	/* Read, modify, write */
	err = ice_read_phy_reg_e822(hw, port, P_REG_RX_TMR_CMD, &val);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read RX_TMR_CMD, err %d\n",
			  err);
		return err;
	}

	/* Modify necessary bits only and perform write */
	val &= ~TS_CMD_MASK;
	val |= cmd_val;

	err = ice_write_phy_reg_e822(hw, port, P_REG_RX_TMR_CMD, val);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write back RX_TMR_CMD, err %d\n",
			  err);
		return err;
	}

	return 0;
}

/**
 * ice_ptp_one_port_cmd - Prepare one port for a timer command
 * @hw: pointer to the HW struct
 * @configured_port: the port to configure with configured_cmd
 * @configured_cmd: timer command to prepare on the configured_port
 *
 * Prepare the configured_port for the configured_cmd, and prepare all other
 * ports for ICE_PTP_NOP. This causes the configured_port to execute the
 * desired command while all other ports perform no operation.
 */
static int
ice_ptp_one_port_cmd(struct ice_hw *hw, u8 configured_port,
		     enum ice_ptp_tmr_cmd configured_cmd)
{
	u8 port;

	for (port = 0; port < ICE_NUM_EXTERNAL_PORTS; port++) {
		enum ice_ptp_tmr_cmd cmd;
		int err;

		if (port == configured_port)
			cmd = configured_cmd;
		else
			cmd = ICE_PTP_NOP;

		err = ice_ptp_write_port_cmd_e822(hw, port, cmd);
		if (err)
			return err;
	}

	return 0;
}

/**
 * ice_ptp_port_cmd_e822 - Prepare all ports for a timer command
 * @hw: pointer to the HW struct
 * @cmd: timer command to prepare
 *
 * Prepare all ports connected to this device for an upcoming timer sync
 * command.
 */
static int
ice_ptp_port_cmd_e822(struct ice_hw *hw, enum ice_ptp_tmr_cmd cmd)
{
	u8 port;

	for (port = 0; port < ICE_NUM_EXTERNAL_PORTS; port++) {
		int err;

		err = ice_ptp_write_port_cmd_e822(hw, port, cmd);
		if (err)
			return err;
	}

	return 0;
}

/* E822 Vernier calibration functions
 *
 * The following functions are used as part of the vernier calibration of
 * a port. This calibration increases the precision of the timestamps on the
 * port.
 */

/**
 * ice_phy_get_speed_and_fec_e822 - Get link speed and FEC based on serdes mode
 * @hw: pointer to HW struct
 * @port: the port to read from
 * @link_out: if non-NULL, holds link speed on success
 * @fec_out: if non-NULL, holds FEC algorithm on success
 *
 * Read the serdes data for the PHY port and extract the link speed and FEC
 * algorithm.
 */
static int
ice_phy_get_speed_and_fec_e822(struct ice_hw *hw, u8 port,
			       enum ice_ptp_link_spd *link_out,
			       enum ice_ptp_fec_mode *fec_out)
{
	enum ice_ptp_link_spd link;
	enum ice_ptp_fec_mode fec;
	u32 serdes;
	int err;

	err = ice_read_phy_reg_e822(hw, port, P_REG_LINK_SPEED, &serdes);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read serdes info\n");
		return err;
	}

	/* Determine the FEC algorithm */
	fec = (enum ice_ptp_fec_mode)P_REG_LINK_SPEED_FEC_MODE(serdes);

	serdes &= P_REG_LINK_SPEED_SERDES_M;

	/* Determine the link speed */
	if (fec == ICE_PTP_FEC_MODE_RS_FEC) {
		switch (serdes) {
		case ICE_PTP_SERDES_25G:
			link = ICE_PTP_LNK_SPD_25G_RS;
			break;
		case ICE_PTP_SERDES_50G:
			link = ICE_PTP_LNK_SPD_50G_RS;
			break;
		case ICE_PTP_SERDES_100G:
			link = ICE_PTP_LNK_SPD_100G_RS;
			break;
		default:
			return -EIO;
		}
	} else {
		switch (serdes) {
		case ICE_PTP_SERDES_1G:
			link = ICE_PTP_LNK_SPD_1G;
			break;
		case ICE_PTP_SERDES_10G:
			link = ICE_PTP_LNK_SPD_10G;
			break;
		case ICE_PTP_SERDES_25G:
			link = ICE_PTP_LNK_SPD_25G;
			break;
		case ICE_PTP_SERDES_40G:
			link = ICE_PTP_LNK_SPD_40G;
			break;
		case ICE_PTP_SERDES_50G:
			link = ICE_PTP_LNK_SPD_50G;
			break;
		default:
			return -EIO;
		}
	}

	if (link_out)
		*link_out = link;
	if (fec_out)
		*fec_out = fec;

	return 0;
}

/**
 * ice_phy_cfg_lane_e822 - Configure PHY quad for single/multi-lane timestamp
 * @hw: pointer to HW struct
 * @port: to configure the quad for
 */
static void ice_phy_cfg_lane_e822(struct ice_hw *hw, u8 port)
{
	enum ice_ptp_link_spd link_spd;
	int err;
	u32 val;
	u8 quad;

	err = ice_phy_get_speed_and_fec_e822(hw, port, &link_spd, NULL);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to get PHY link speed, err %d\n",
			  err);
		return;
	}

	quad = port / ICE_PORTS_PER_QUAD;

	err = ice_read_quad_reg_e822(hw, quad, Q_REG_TX_MEM_GBL_CFG, &val);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read TX_MEM_GLB_CFG, err %d\n",
			  err);
		return;
	}

	if (link_spd >= ICE_PTP_LNK_SPD_40G)
		val &= ~Q_REG_TX_MEM_GBL_CFG_LANE_TYPE_M;
	else
		val |= Q_REG_TX_MEM_GBL_CFG_LANE_TYPE_M;

	err = ice_write_quad_reg_e822(hw, quad, Q_REG_TX_MEM_GBL_CFG, val);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write back TX_MEM_GBL_CFG, err %d\n",
			  err);
		return;
	}
}

/**
 * ice_phy_cfg_uix_e822 - Configure Serdes UI to TU conversion for E822
 * @hw: pointer to the HW structure
 * @port: the port to configure
 *
 * Program the conversion ration of Serdes clock "unit intervals" (UIs) to PHC
 * hardware clock time units (TUs). That is, determine the number of TUs per
 * serdes unit interval, and program the UIX registers with this conversion.
 *
 * This conversion is used as part of the calibration process when determining
 * the additional error of a timestamp vs the real time of transmission or
 * receipt of the packet.
 *
 * Hardware uses the number of TUs per 66 UIs, written to the UIX registers
 * for the two main serdes clock rates, 10G/40G and 25G/100G serdes clocks.
 *
 * To calculate the conversion ratio, we use the following facts:
 *
 * a) the clock frequency in Hz (cycles per second)
 * b) the number of TUs per cycle (the increment value of the clock)
 * c) 1 second per 1 billion nanoseconds
 * d) the duration of 66 UIs in nanoseconds
 *
 * Given these facts, we can use the following table to work out what ratios
 * to multiply in order to get the number of TUs per 66 UIs:
 *
 * cycles |   1 second   | incval (TUs) | nanoseconds
 * -------+--------------+--------------+-------------
 * second | 1 billion ns |    cycle     |   66 UIs
 *
 * To perform the multiplication using integers without too much loss of
 * precision, we can take use the following equation:
 *
 * (freq * incval * 6600 LINE_UI ) / ( 100 * 1 billion)
 *
 * We scale up to using 6600 UI instead of 66 in order to avoid fractional
 * nanosecond UIs (66 UI at 10G/40G is 6.4 ns)
 *
 * The increment value has a maximum expected range of about 34 bits, while
 * the frequency value is about 29 bits. Multiplying these values shouldn't
 * overflow the 64 bits. However, we must then further multiply them again by
 * the Serdes unit interval duration. To avoid overflow here, we split the
 * overall divide by 1e11 into a divide by 256 (shift down by 8 bits) and
 * a divide by 390,625,000. This does lose some precision, but avoids
 * miscalculation due to arithmetic overflow.
 */
static int ice_phy_cfg_uix_e822(struct ice_hw *hw, u8 port)
{
	u64 cur_freq, clk_incval, tu_per_sec, uix;
	int err;

	cur_freq = ice_e822_pll_freq(ice_e822_time_ref(hw));
	clk_incval = ice_ptp_read_src_incval(hw);

	/* Calculate TUs per second divided by 256 */
	tu_per_sec = (cur_freq * clk_incval) >> 8;

#define LINE_UI_10G_40G 640 /* 6600 UIs is 640 nanoseconds at 10Gb/40Gb */
#define LINE_UI_25G_100G 256 /* 6600 UIs is 256 nanoseconds at 25Gb/100Gb */

	/* Program the 10Gb/40Gb conversion ratio */
	uix = div_u64(tu_per_sec * LINE_UI_10G_40G, 390625000);

	err = ice_write_64b_phy_reg_e822(hw, port, P_REG_UIX66_10G_40G_L,
					 uix);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write UIX66_10G_40G, err %d\n",
			  err);
		return err;
	}

	/* Program the 25Gb/100Gb conversion ratio */
	uix = div_u64(tu_per_sec * LINE_UI_25G_100G, 390625000);

	err = ice_write_64b_phy_reg_e822(hw, port, P_REG_UIX66_25G_100G_L,
					 uix);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write UIX66_25G_100G, err %d\n",
			  err);
		return err;
	}

	return 0;
}

/**
 * ice_phy_cfg_parpcs_e822 - Configure TUs per PAR/PCS clock cycle
 * @hw: pointer to the HW struct
 * @port: port to configure
 *
 * Configure the number of TUs for the PAR and PCS clocks used as part of the
 * timestamp calibration process. This depends on the link speed, as the PHY
 * uses different markers depending on the speed.
 *
 * 1Gb/10Gb/25Gb:
 * - Tx/Rx PAR/PCS markers
 *
 * 25Gb RS:
 * - Tx/Rx Reed Solomon gearbox PAR/PCS markers
 *
 * 40Gb/50Gb:
 * - Tx/Rx PAR/PCS markers
 * - Rx Deskew PAR/PCS markers
 *
 * 50G RS and 100GB RS:
 * - Tx/Rx Reed Solomon gearbox PAR/PCS markers
 * - Rx Deskew PAR/PCS markers
 * - Tx PAR/PCS markers
 *
 * To calculate the conversion, we use the PHC clock frequency (cycles per
 * second), the increment value (TUs per cycle), and the related PHY clock
 * frequency to calculate the TUs per unit of the PHY link clock. The
 * following table shows how the units convert:
 *
 * cycles |  TUs  | second
 * -------+-------+--------
 * second | cycle | cycles
 *
 * For each conversion register, look up the appropriate frequency from the
 * e822 PAR/PCS table and calculate the TUs per unit of that clock. Program
 * this to the appropriate register, preparing hardware to perform timestamp
 * calibration to calculate the total Tx or Rx offset to adjust the timestamp
 * in order to calibrate for the internal PHY delays.
 *
 * Note that the increment value ranges up to ~34 bits, and the clock
 * frequency is ~29 bits, so multiplying them together should fit within the
 * 64 bit arithmetic.
 */
static int ice_phy_cfg_parpcs_e822(struct ice_hw *hw, u8 port)
{
	u64 cur_freq, clk_incval, tu_per_sec, phy_tus;
	enum ice_ptp_link_spd link_spd;
	enum ice_ptp_fec_mode fec_mode;
	int err;

	err = ice_phy_get_speed_and_fec_e822(hw, port, &link_spd, &fec_mode);
	if (err)
		return err;

	cur_freq = ice_e822_pll_freq(ice_e822_time_ref(hw));
	clk_incval = ice_ptp_read_src_incval(hw);

	/* Calculate TUs per cycle of the PHC clock */
	tu_per_sec = cur_freq * clk_incval;

	/* For each PHY conversion register, look up the appropriate link
	 * speed frequency and determine the TUs per that clock's cycle time.
	 * Split this into a high and low value and then program the
	 * appropriate register. If that link speed does not use the
	 * associated register, write zeros to clear it instead.
	 */

	/* P_REG_PAR_TX_TUS */
	if (e822_vernier[link_spd].tx_par_clk)
		phy_tus = div_u64(tu_per_sec,
				  e822_vernier[link_spd].tx_par_clk);
	else
		phy_tus = 0;

	err = ice_write_40b_phy_reg_e822(hw, port, P_REG_PAR_TX_TUS_L,
					 phy_tus);
	if (err)
		return err;

	/* P_REG_PAR_RX_TUS */
	if (e822_vernier[link_spd].rx_par_clk)
		phy_tus = div_u64(tu_per_sec,
				  e822_vernier[link_spd].rx_par_clk);
	else
		phy_tus = 0;

	err = ice_write_40b_phy_reg_e822(hw, port, P_REG_PAR_RX_TUS_L,
					 phy_tus);
	if (err)
		return err;

	/* P_REG_PCS_TX_TUS */
	if (e822_vernier[link_spd].tx_pcs_clk)
		phy_tus = div_u64(tu_per_sec,
				  e822_vernier[link_spd].tx_pcs_clk);
	else
		phy_tus = 0;

	err = ice_write_40b_phy_reg_e822(hw, port, P_REG_PCS_TX_TUS_L,
					 phy_tus);
	if (err)
		return err;

	/* P_REG_PCS_RX_TUS */
	if (e822_vernier[link_spd].rx_pcs_clk)
		phy_tus = div_u64(tu_per_sec,
				  e822_vernier[link_spd].rx_pcs_clk);
	else
		phy_tus = 0;

	err = ice_write_40b_phy_reg_e822(hw, port, P_REG_PCS_RX_TUS_L,
					 phy_tus);
	if (err)
		return err;

	/* P_REG_DESK_PAR_TX_TUS */
	if (e822_vernier[link_spd].tx_desk_rsgb_par)
		phy_tus = div_u64(tu_per_sec,
				  e822_vernier[link_spd].tx_desk_rsgb_par);
	else
		phy_tus = 0;

	err = ice_write_40b_phy_reg_e822(hw, port, P_REG_DESK_PAR_TX_TUS_L,
					 phy_tus);
	if (err)
		return err;

	/* P_REG_DESK_PAR_RX_TUS */
	if (e822_vernier[link_spd].rx_desk_rsgb_par)
		phy_tus = div_u64(tu_per_sec,
				  e822_vernier[link_spd].rx_desk_rsgb_par);
	else
		phy_tus = 0;

	err = ice_write_40b_phy_reg_e822(hw, port, P_REG_DESK_PAR_RX_TUS_L,
					 phy_tus);
	if (err)
		return err;

	/* P_REG_DESK_PCS_TX_TUS */
	if (e822_vernier[link_spd].tx_desk_rsgb_pcs)
		phy_tus = div_u64(tu_per_sec,
				  e822_vernier[link_spd].tx_desk_rsgb_pcs);
	else
		phy_tus = 0;

	err = ice_write_40b_phy_reg_e822(hw, port, P_REG_DESK_PCS_TX_TUS_L,
					 phy_tus);
	if (err)
		return err;

	/* P_REG_DESK_PCS_RX_TUS */
	if (e822_vernier[link_spd].rx_desk_rsgb_pcs)
		phy_tus = div_u64(tu_per_sec,
				  e822_vernier[link_spd].rx_desk_rsgb_pcs);
	else
		phy_tus = 0;

	return ice_write_40b_phy_reg_e822(hw, port, P_REG_DESK_PCS_RX_TUS_L,
					  phy_tus);
}

/**
 * ice_calc_fixed_tx_offset_e822 - Calculated Fixed Tx offset for a port
 * @hw: pointer to the HW struct
 * @link_spd: the Link speed to calculate for
 *
 * Calculate the fixed offset due to known static latency data.
 */
static u64
ice_calc_fixed_tx_offset_e822(struct ice_hw *hw, enum ice_ptp_link_spd link_spd)
{
	u64 cur_freq, clk_incval, tu_per_sec, fixed_offset;

	cur_freq = ice_e822_pll_freq(ice_e822_time_ref(hw));
	clk_incval = ice_ptp_read_src_incval(hw);

	/* Calculate TUs per second */
	tu_per_sec = cur_freq * clk_incval;

	/* Calculate number of TUs to add for the fixed Tx latency. Since the
	 * latency measurement is in 1/100th of a nanosecond, we need to
	 * multiply by tu_per_sec and then divide by 1e11. This calculation
	 * overflows 64 bit integer arithmetic, so break it up into two
	 * divisions by 1e4 first then by 1e7.
	 */
	fixed_offset = div_u64(tu_per_sec, 10000);
	fixed_offset *= e822_vernier[link_spd].tx_fixed_delay;
	fixed_offset = div_u64(fixed_offset, 10000000);

	return fixed_offset;
}

/**
 * ice_phy_cfg_tx_offset_e822 - Configure total Tx timestamp offset
 * @hw: pointer to the HW struct
 * @port: the PHY port to configure
 *
 * Program the P_REG_TOTAL_TX_OFFSET register with the total number of TUs to
 * adjust Tx timestamps by. This is calculated by combining some known static
 * latency along with the Vernier offset computations done by hardware.
 *
 * This function will not return successfully until the Tx offset calculations
 * have been completed, which requires waiting until at least one packet has
 * been transmitted by the device. It is safe to call this function
 * periodically until calibration succeeds, as it will only program the offset
 * once.
 *
 * To avoid overflow, when calculating the offset based on the known static
 * latency values, we use measurements in 1/100th of a nanosecond, and divide
 * the TUs per second up front. This avoids overflow while allowing
 * calculation of the adjustment using integer arithmetic.
 *
 * Returns zero on success, -EBUSY if the hardware vernier offset
 * calibration has not completed, or another error code on failure.
 */
int ice_phy_cfg_tx_offset_e822(struct ice_hw *hw, u8 port)
{
	enum ice_ptp_link_spd link_spd;
	enum ice_ptp_fec_mode fec_mode;
	u64 total_offset, val;
	int err;
	u32 reg;

	/* Nothing to do if we've already programmed the offset */
	err = ice_read_phy_reg_e822(hw, port, P_REG_TX_OR, &reg);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read TX_OR for port %u, err %d\n",
			  port, err);
		return err;
	}

	if (reg)
		return 0;

	err = ice_read_phy_reg_e822(hw, port, P_REG_TX_OV_STATUS, &reg);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read TX_OV_STATUS for port %u, err %d\n",
			  port, err);
		return err;
	}

	if (!(reg & P_REG_TX_OV_STATUS_OV_M))
		return -EBUSY;

	err = ice_phy_get_speed_and_fec_e822(hw, port, &link_spd, &fec_mode);
	if (err)
		return err;

	total_offset = ice_calc_fixed_tx_offset_e822(hw, link_spd);

	/* Read the first Vernier offset from the PHY register and add it to
	 * the total offset.
	 */
	if (link_spd == ICE_PTP_LNK_SPD_1G ||
	    link_spd == ICE_PTP_LNK_SPD_10G ||
	    link_spd == ICE_PTP_LNK_SPD_25G ||
	    link_spd == ICE_PTP_LNK_SPD_25G_RS ||
	    link_spd == ICE_PTP_LNK_SPD_40G ||
	    link_spd == ICE_PTP_LNK_SPD_50G) {
		err = ice_read_64b_phy_reg_e822(hw, port,
						P_REG_PAR_PCS_TX_OFFSET_L,
						&val);
		if (err)
			return err;

		total_offset += val;
	}

	/* For Tx, we only need to use the second Vernier offset for
	 * multi-lane link speeds with RS-FEC. The lanes will always be
	 * aligned.
	 */
	if (link_spd == ICE_PTP_LNK_SPD_50G_RS ||
	    link_spd == ICE_PTP_LNK_SPD_100G_RS) {
		err = ice_read_64b_phy_reg_e822(hw, port,
						P_REG_PAR_TX_TIME_L,
						&val);
		if (err)
			return err;

		total_offset += val;
	}

	/* Now that the total offset has been calculated, program it to the
	 * PHY and indicate that the Tx offset is ready. After this,
	 * timestamps will be enabled.
	 */
	err = ice_write_64b_phy_reg_e822(hw, port, P_REG_TOTAL_TX_OFFSET_L,
					 total_offset);
	if (err)
		return err;

	err = ice_write_phy_reg_e822(hw, port, P_REG_TX_OR, 1);
	if (err)
		return err;

	dev_info(ice_hw_to_dev(hw), "Port=%d Tx vernier offset calibration complete\n",
		 port);

	return 0;
}

/**
 * ice_phy_calc_pmd_adj_e822 - Calculate PMD adjustment for Rx
 * @hw: pointer to the HW struct
 * @port: the PHY port to adjust for
 * @link_spd: the current link speed of the PHY
 * @fec_mode: the current FEC mode of the PHY
 * @pmd_adj: on return, the amount to adjust the Rx total offset by
 *
 * Calculates the adjustment to Rx timestamps due to PMD alignment in the PHY.
 * This varies by link speed and FEC mode. The value calculated accounts for
 * various delays caused when receiving a packet.
 */
static int
ice_phy_calc_pmd_adj_e822(struct ice_hw *hw, u8 port,
			  enum ice_ptp_link_spd link_spd,
			  enum ice_ptp_fec_mode fec_mode, u64 *pmd_adj)
{
	u64 cur_freq, clk_incval, tu_per_sec, mult, adj;
	u8 pmd_align;
	u32 val;
	int err;

	err = ice_read_phy_reg_e822(hw, port, P_REG_PMD_ALIGNMENT, &val);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read PMD alignment, err %d\n",
			  err);
		return err;
	}

	pmd_align = (u8)val;

	cur_freq = ice_e822_pll_freq(ice_e822_time_ref(hw));
	clk_incval = ice_ptp_read_src_incval(hw);

	/* Calculate TUs per second */
	tu_per_sec = cur_freq * clk_incval;

	/* The PMD alignment adjustment measurement depends on the link speed,
	 * and whether FEC is enabled. For each link speed, the alignment
	 * adjustment is calculated by dividing a value by the length of
	 * a Time Unit in nanoseconds.
	 *
	 * 1G: align == 4 ? 10 * 0.8 : (align + 6 % 10) * 0.8
	 * 10G: align == 65 ? 0 : (align * 0.1 * 32/33)
	 * 10G w/FEC: align * 0.1 * 32/33
	 * 25G: align == 65 ? 0 : (align * 0.4 * 32/33)
	 * 25G w/FEC: align * 0.4 * 32/33
	 * 40G: align == 65 ? 0 : (align * 0.1 * 32/33)
	 * 40G w/FEC: align * 0.1 * 32/33
	 * 50G: align == 65 ? 0 : (align * 0.4 * 32/33)
	 * 50G w/FEC: align * 0.8 * 32/33
	 *
	 * For RS-FEC, if align is < 17 then we must also add 1.6 * 32/33.
	 *
	 * To allow for calculating this value using integer arithmetic, we
	 * instead start with the number of TUs per second, (inverse of the
	 * length of a Time Unit in nanoseconds), multiply by a value based
	 * on the PMD alignment register, and then divide by the right value
	 * calculated based on the table above. To avoid integer overflow this
	 * division is broken up into a step of dividing by 125 first.
	 */
	if (link_spd == ICE_PTP_LNK_SPD_1G) {
		if (pmd_align == 4)
			mult = 10;
		else
			mult = (pmd_align + 6) % 10;
	} else if (link_spd == ICE_PTP_LNK_SPD_10G ||
		   link_spd == ICE_PTP_LNK_SPD_25G ||
		   link_spd == ICE_PTP_LNK_SPD_40G ||
		   link_spd == ICE_PTP_LNK_SPD_50G) {
		/* If Clause 74 FEC, always calculate PMD adjust */
		if (pmd_align != 65 || fec_mode == ICE_PTP_FEC_MODE_CLAUSE74)
			mult = pmd_align;
		else
			mult = 0;
	} else if (link_spd == ICE_PTP_LNK_SPD_25G_RS ||
		   link_spd == ICE_PTP_LNK_SPD_50G_RS ||
		   link_spd == ICE_PTP_LNK_SPD_100G_RS) {
		if (pmd_align < 17)
			mult = pmd_align + 40;
		else
			mult = pmd_align;
	} else {
		ice_debug(hw, ICE_DBG_PTP, "Unknown link speed %d, skipping PMD adjustment\n",
			  link_spd);
		mult = 0;
	}

	/* In some cases, there's no need to adjust for the PMD alignment */
	if (!mult) {
		*pmd_adj = 0;
		return 0;
	}

	/* Calculate the adjustment by multiplying TUs per second by the
	 * appropriate multiplier and divisor. To avoid overflow, we first
	 * divide by 125, and then handle remaining divisor based on the link
	 * speed pmd_adj_divisor value.
	 */
	adj = div_u64(tu_per_sec, 125);
	adj *= mult;
	adj = div_u64(adj, e822_vernier[link_spd].pmd_adj_divisor);

	/* Finally, for 25G-RS and 50G-RS, a further adjustment for the Rx
	 * cycle count is necessary.
	 */
	if (link_spd == ICE_PTP_LNK_SPD_25G_RS) {
		u64 cycle_adj;
		u8 rx_cycle;

		err = ice_read_phy_reg_e822(hw, port, P_REG_RX_40_TO_160_CNT,
					    &val);
		if (err) {
			ice_debug(hw, ICE_DBG_PTP, "Failed to read 25G-RS Rx cycle count, err %d\n",
				  err);
			return err;
		}

		rx_cycle = val & P_REG_RX_40_TO_160_CNT_RXCYC_M;
		if (rx_cycle) {
			mult = (4 - rx_cycle) * 40;

			cycle_adj = div_u64(tu_per_sec, 125);
			cycle_adj *= mult;
			cycle_adj = div_u64(cycle_adj, e822_vernier[link_spd].pmd_adj_divisor);

			adj += cycle_adj;
		}
	} else if (link_spd == ICE_PTP_LNK_SPD_50G_RS) {
		u64 cycle_adj;
		u8 rx_cycle;

		err = ice_read_phy_reg_e822(hw, port, P_REG_RX_80_TO_160_CNT,
					    &val);
		if (err) {
			ice_debug(hw, ICE_DBG_PTP, "Failed to read 50G-RS Rx cycle count, err %d\n",
				  err);
			return err;
		}

		rx_cycle = val & P_REG_RX_80_TO_160_CNT_RXCYC_M;
		if (rx_cycle) {
			mult = rx_cycle * 40;

			cycle_adj = div_u64(tu_per_sec, 125);
			cycle_adj *= mult;
			cycle_adj = div_u64(cycle_adj, e822_vernier[link_spd].pmd_adj_divisor);

			adj += cycle_adj;
		}
	}

	/* Return the calculated adjustment */
	*pmd_adj = adj;

	return 0;
}

/**
 * ice_calc_fixed_rx_offset_e822 - Calculated the fixed Rx offset for a port
 * @hw: pointer to HW struct
 * @link_spd: The Link speed to calculate for
 *
 * Determine the fixed Rx latency for a given link speed.
 */
static u64
ice_calc_fixed_rx_offset_e822(struct ice_hw *hw, enum ice_ptp_link_spd link_spd)
{
	u64 cur_freq, clk_incval, tu_per_sec, fixed_offset;

	cur_freq = ice_e822_pll_freq(ice_e822_time_ref(hw));
	clk_incval = ice_ptp_read_src_incval(hw);

	/* Calculate TUs per second */
	tu_per_sec = cur_freq * clk_incval;

	/* Calculate number of TUs to add for the fixed Rx latency. Since the
	 * latency measurement is in 1/100th of a nanosecond, we need to
	 * multiply by tu_per_sec and then divide by 1e11. This calculation
	 * overflows 64 bit integer arithmetic, so break it up into two
	 * divisions by 1e4 first then by 1e7.
	 */
	fixed_offset = div_u64(tu_per_sec, 10000);
	fixed_offset *= e822_vernier[link_spd].rx_fixed_delay;
	fixed_offset = div_u64(fixed_offset, 10000000);

	return fixed_offset;
}

/**
 * ice_phy_cfg_rx_offset_e822 - Configure total Rx timestamp offset
 * @hw: pointer to the HW struct
 * @port: the PHY port to configure
 *
 * Program the P_REG_TOTAL_RX_OFFSET register with the number of Time Units to
 * adjust Rx timestamps by. This combines calculations from the Vernier offset
 * measurements taken in hardware with some data about known fixed delay as
 * well as adjusting for multi-lane alignment delay.
 *
 * This function will not return successfully until the Rx offset calculations
 * have been completed, which requires waiting until at least one packet has
 * been received by the device. It is safe to call this function periodically
 * until calibration succeeds, as it will only program the offset once.
 *
 * This function must be called only after the offset registers are valid,
 * i.e. after the Vernier calibration wait has passed, to ensure that the PHY
 * has measured the offset.
 *
 * To avoid overflow, when calculating the offset based on the known static
 * latency values, we use measurements in 1/100th of a nanosecond, and divide
 * the TUs per second up front. This avoids overflow while allowing
 * calculation of the adjustment using integer arithmetic.
 *
 * Returns zero on success, -EBUSY if the hardware vernier offset
 * calibration has not completed, or another error code on failure.
 */
int ice_phy_cfg_rx_offset_e822(struct ice_hw *hw, u8 port)
{
	enum ice_ptp_link_spd link_spd;
	enum ice_ptp_fec_mode fec_mode;
	u64 total_offset, pmd, val;
	int err;
	u32 reg;

	/* Nothing to do if we've already programmed the offset */
	err = ice_read_phy_reg_e822(hw, port, P_REG_RX_OR, &reg);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read RX_OR for port %u, err %d\n",
			  port, err);
		return err;
	}

	if (reg)
		return 0;

	err = ice_read_phy_reg_e822(hw, port, P_REG_RX_OV_STATUS, &reg);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read RX_OV_STATUS for port %u, err %d\n",
			  port, err);
		return err;
	}

	if (!(reg & P_REG_RX_OV_STATUS_OV_M))
		return -EBUSY;

	err = ice_phy_get_speed_and_fec_e822(hw, port, &link_spd, &fec_mode);
	if (err)
		return err;

	total_offset = ice_calc_fixed_rx_offset_e822(hw, link_spd);

	/* Read the first Vernier offset from the PHY register and add it to
	 * the total offset.
	 */
	err = ice_read_64b_phy_reg_e822(hw, port,
					P_REG_PAR_PCS_RX_OFFSET_L,
					&val);
	if (err)
		return err;

	total_offset += val;

	/* For Rx, all multi-lane link speeds include a second Vernier
	 * calibration, because the lanes might not be aligned.
	 */
	if (link_spd == ICE_PTP_LNK_SPD_40G ||
	    link_spd == ICE_PTP_LNK_SPD_50G ||
	    link_spd == ICE_PTP_LNK_SPD_50G_RS ||
	    link_spd == ICE_PTP_LNK_SPD_100G_RS) {
		err = ice_read_64b_phy_reg_e822(hw, port,
						P_REG_PAR_RX_TIME_L,
						&val);
		if (err)
			return err;

		total_offset += val;
	}

	/* In addition, Rx must account for the PMD alignment */
	err = ice_phy_calc_pmd_adj_e822(hw, port, link_spd, fec_mode, &pmd);
	if (err)
		return err;

	/* For RS-FEC, this adjustment adds delay, but for other modes, it
	 * subtracts delay.
	 */
	if (fec_mode == ICE_PTP_FEC_MODE_RS_FEC)
		total_offset += pmd;
	else
		total_offset -= pmd;

	/* Now that the total offset has been calculated, program it to the
	 * PHY and indicate that the Rx offset is ready. After this,
	 * timestamps will be enabled.
	 */
	err = ice_write_64b_phy_reg_e822(hw, port, P_REG_TOTAL_RX_OFFSET_L,
					 total_offset);
	if (err)
		return err;

	err = ice_write_phy_reg_e822(hw, port, P_REG_RX_OR, 1);
	if (err)
		return err;

	dev_info(ice_hw_to_dev(hw), "Port=%d Rx vernier offset calibration complete\n",
		 port);

	return 0;
}

/**
 * ice_read_phy_and_phc_time_e822 - Simultaneously capture PHC and PHY time
 * @hw: pointer to the HW struct
 * @port: the PHY port to read
 * @phy_time: on return, the 64bit PHY timer value
 * @phc_time: on return, the lower 64bits of PHC time
 *
 * Issue a ICE_PTP_READ_TIME timer command to simultaneously capture the PHY
 * and PHC timer values.
 */
static int
ice_read_phy_and_phc_time_e822(struct ice_hw *hw, u8 port, u64 *phy_time,
			       u64 *phc_time)
{
	u64 tx_time, rx_time;
	u32 zo, lo;
	u8 tmr_idx;
	int err;

	tmr_idx = ice_get_ptp_src_clock_index(hw);

	/* Prepare the PHC timer for a ICE_PTP_READ_TIME capture command */
	ice_ptp_src_cmd(hw, ICE_PTP_READ_TIME);

	/* Prepare the PHY timer for a ICE_PTP_READ_TIME capture command */
	err = ice_ptp_one_port_cmd(hw, port, ICE_PTP_READ_TIME);
	if (err)
		return err;

	/* Issue the sync to start the ICE_PTP_READ_TIME capture */
	ice_ptp_exec_tmr_cmd(hw);

	/* Read the captured PHC time from the shadow time registers */
	zo = rd32(hw, GLTSYN_SHTIME_0(tmr_idx));
	lo = rd32(hw, GLTSYN_SHTIME_L(tmr_idx));
	*phc_time = (u64)lo << 32 | zo;

	/* Read the captured PHY time from the PHY shadow registers */
	err = ice_ptp_read_port_capture(hw, port, &tx_time, &rx_time);
	if (err)
		return err;

	/* If the PHY Tx and Rx timers don't match, log a warning message.
	 * Note that this should not happen in normal circumstances since the
	 * driver always programs them together.
	 */
	if (tx_time != rx_time)
		dev_warn(ice_hw_to_dev(hw),
			 "PHY port %u Tx and Rx timers do not match, tx_time 0x%016llX, rx_time 0x%016llX\n",
			 port, (unsigned long long)tx_time,
			 (unsigned long long)rx_time);

	*phy_time = tx_time;

	return 0;
}

/**
 * ice_sync_phy_timer_e822 - Synchronize the PHY timer with PHC timer
 * @hw: pointer to the HW struct
 * @port: the PHY port to synchronize
 *
 * Perform an adjustment to ensure that the PHY and PHC timers are in sync.
 * This is done by issuing a ICE_PTP_READ_TIME command which triggers a
 * simultaneous read of the PHY timer and PHC timer. Then we use the
 * difference to calculate an appropriate 2s complement addition to add
 * to the PHY timer in order to ensure it reads the same value as the
 * primary PHC timer.
 */
static int ice_sync_phy_timer_e822(struct ice_hw *hw, u8 port)
{
	u64 phc_time, phy_time, difference;
	int err;

	if (!ice_ptp_lock(hw)) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to acquire PTP semaphore\n");
		return -EBUSY;
	}

	err = ice_read_phy_and_phc_time_e822(hw, port, &phy_time, &phc_time);
	if (err)
		goto err_unlock;

	/* Calculate the amount required to add to the port time in order for
	 * it to match the PHC time.
	 *
	 * Note that the port adjustment is done using 2s complement
	 * arithmetic. This is convenient since it means that we can simply
	 * calculate the difference between the PHC time and the port time,
	 * and it will be interpreted correctly.
	 */
	difference = phc_time - phy_time;

	err = ice_ptp_prep_port_adj_e822(hw, port, (s64)difference);
	if (err)
		goto err_unlock;

	err = ice_ptp_one_port_cmd(hw, port, ICE_PTP_ADJ_TIME);
	if (err)
		goto err_unlock;

	/* Do not perform any action on the main timer */
	ice_ptp_src_cmd(hw, ICE_PTP_NOP);

	/* Issue the sync to activate the time adjustment */
	ice_ptp_exec_tmr_cmd(hw);

	/* Re-capture the timer values to flush the command registers and
	 * verify that the time was properly adjusted.
	 */
	err = ice_read_phy_and_phc_time_e822(hw, port, &phy_time, &phc_time);
	if (err)
		goto err_unlock;

	dev_info(ice_hw_to_dev(hw),
		 "Port %u PHY time synced to PHC: 0x%016llX, 0x%016llX\n",
		 port, (unsigned long long)phy_time,
		 (unsigned long long)phc_time);

	ice_ptp_unlock(hw);

	return 0;

err_unlock:
	ice_ptp_unlock(hw);
	return err;
}

/**
 * ice_stop_phy_timer_e822 - Stop the PHY clock timer
 * @hw: pointer to the HW struct
 * @port: the PHY port to stop
 * @soft_reset: if true, hold the SOFT_RESET bit of P_REG_PS
 *
 * Stop the clock of a PHY port. This must be done as part of the flow to
 * re-calibrate Tx and Rx timestamping offsets whenever the clock time is
 * initialized or when link speed changes.
 */
int
ice_stop_phy_timer_e822(struct ice_hw *hw, u8 port, bool soft_reset)
{
	int err;
	u32 val;

	err = ice_write_phy_reg_e822(hw, port, P_REG_TX_OR, 0);
	if (err)
		return err;

	err = ice_write_phy_reg_e822(hw, port, P_REG_RX_OR, 0);
	if (err)
		return err;

	err = ice_read_phy_reg_e822(hw, port, P_REG_PS, &val);
	if (err)
		return err;

	val &= ~P_REG_PS_START_M;
	err = ice_write_phy_reg_e822(hw, port, P_REG_PS, val);
	if (err)
		return err;

	val &= ~P_REG_PS_ENA_CLK_M;
	err = ice_write_phy_reg_e822(hw, port, P_REG_PS, val);
	if (err)
		return err;

	if (soft_reset) {
		val |= P_REG_PS_SFT_RESET_M;
		err = ice_write_phy_reg_e822(hw, port, P_REG_PS, val);
		if (err)
			return err;
	}

	ice_debug(hw, ICE_DBG_PTP, "Disabled clock on PHY port %u\n", port);

	return 0;
}

/**
 * ice_start_phy_timer_e822 - Start the PHY clock timer
 * @hw: pointer to the HW struct
 * @port: the PHY port to start
 *
 * Start the clock of a PHY port. This must be done as part of the flow to
 * re-calibrate Tx and Rx timestamping offsets whenever the clock time is
 * initialized or when link speed changes.
 *
 * Hardware will take Vernier measurements on Tx or Rx of packets.
 */
int ice_start_phy_timer_e822(struct ice_hw *hw, u8 port)
{
	u32 lo, hi, val;
	u64 incval;
	u8 tmr_idx;
	int err;

	tmr_idx = ice_get_ptp_src_clock_index(hw);

	err = ice_stop_phy_timer_e822(hw, port, false);
	if (err)
		return err;

	ice_phy_cfg_lane_e822(hw, port);

	err = ice_phy_cfg_uix_e822(hw, port);
	if (err)
		return err;

	err = ice_phy_cfg_parpcs_e822(hw, port);
	if (err)
		return err;

	lo = rd32(hw, GLTSYN_INCVAL_L(tmr_idx));
	hi = rd32(hw, GLTSYN_INCVAL_H(tmr_idx));
	incval = (u64)hi << 32 | lo;

	err = ice_write_40b_phy_reg_e822(hw, port, P_REG_TIMETUS_L, incval);
	if (err)
		return err;

	err = ice_ptp_one_port_cmd(hw, port, ICE_PTP_INIT_INCVAL);
	if (err)
		return err;

	/* Do not perform any action on the main timer */
	ice_ptp_src_cmd(hw, ICE_PTP_NOP);

	ice_ptp_exec_tmr_cmd(hw);

	err = ice_read_phy_reg_e822(hw, port, P_REG_PS, &val);
	if (err)
		return err;

	val |= P_REG_PS_SFT_RESET_M;
	err = ice_write_phy_reg_e822(hw, port, P_REG_PS, val);
	if (err)
		return err;

	val |= P_REG_PS_START_M;
	err = ice_write_phy_reg_e822(hw, port, P_REG_PS, val);
	if (err)
		return err;

	val &= ~P_REG_PS_SFT_RESET_M;
	err = ice_write_phy_reg_e822(hw, port, P_REG_PS, val);
	if (err)
		return err;

	err = ice_ptp_one_port_cmd(hw, port, ICE_PTP_INIT_INCVAL);
	if (err)
		return err;

	ice_ptp_exec_tmr_cmd(hw);

	val |= P_REG_PS_ENA_CLK_M;
	err = ice_write_phy_reg_e822(hw, port, P_REG_PS, val);
	if (err)
		return err;

	val |= P_REG_PS_LOAD_OFFSET_M;
	err = ice_write_phy_reg_e822(hw, port, P_REG_PS, val);
	if (err)
		return err;

	ice_ptp_exec_tmr_cmd(hw);

	err = ice_sync_phy_timer_e822(hw, port);
	if (err)
		return err;

	ice_debug(hw, ICE_DBG_PTP, "Enabled clock on PHY port %u\n", port);

	return 0;
}

/**
 * ice_get_phy_tx_tstamp_ready_e822 - Read Tx memory status register
 * @hw: pointer to the HW struct
 * @quad: the timestamp quad to read from
 * @tstamp_ready: contents of the Tx memory status register
 *
 * Read the Q_REG_TX_MEMORY_STATUS register indicating which timestamps in
 * the PHY are ready. A set bit means the corresponding timestamp is valid and
 * ready to be captured from the PHY timestamp block.
 */
static int
ice_get_phy_tx_tstamp_ready_e822(struct ice_hw *hw, u8 quad, u64 *tstamp_ready)
{
	u32 hi, lo;
	int err;

	err = ice_read_quad_reg_e822(hw, quad, Q_REG_TX_MEMORY_STATUS_U, &hi);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read TX_MEMORY_STATUS_U for quad %u, err %d\n",
			  quad, err);
		return err;
	}

	err = ice_read_quad_reg_e822(hw, quad, Q_REG_TX_MEMORY_STATUS_L, &lo);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read TX_MEMORY_STATUS_L for quad %u, err %d\n",
			  quad, err);
		return err;
	}

	*tstamp_ready = (u64)hi << 32 | (u64)lo;

	return 0;
}

/* E810 functions
 *
 * The following functions operate on the E810 series devices which use
 * a separate external PHY.
 */

/**
 * ice_read_phy_reg_e810 - Read register from external PHY on E810
 * @hw: pointer to the HW struct
 * @addr: the address to read from
 * @val: On return, the value read from the PHY
 *
 * Read a register from the external PHY on the E810 device.
 */
static int ice_read_phy_reg_e810(struct ice_hw *hw, u32 addr, u32 *val)
{
	struct ice_sbq_msg_input msg = {0};
	int err;

	msg.msg_addr_low = lower_16_bits(addr);
	msg.msg_addr_high = upper_16_bits(addr);
	msg.opcode = ice_sbq_msg_rd;
	msg.dest_dev = rmn_0;

	err = ice_sbq_rw_reg(hw, &msg);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to send message to PHY, err %d\n",
			  err);
		return err;
	}

	*val = msg.data;

	return 0;
}

/**
 * ice_write_phy_reg_e810 - Write register on external PHY on E810
 * @hw: pointer to the HW struct
 * @addr: the address to writem to
 * @val: the value to write to the PHY
 *
 * Write a value to a register of the external PHY on the E810 device.
 */
static int ice_write_phy_reg_e810(struct ice_hw *hw, u32 addr, u32 val)
{
	struct ice_sbq_msg_input msg = {0};
	int err;

	msg.msg_addr_low = lower_16_bits(addr);
	msg.msg_addr_high = upper_16_bits(addr);
	msg.opcode = ice_sbq_msg_wr;
	msg.dest_dev = rmn_0;
	msg.data = val;

	err = ice_sbq_rw_reg(hw, &msg);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to send message to PHY, err %d\n",
			  err);
		return err;
	}

	return 0;
}

/**
 * ice_read_phy_tstamp_ll_e810 - Read a PHY timestamp registers through the FW
 * @hw: pointer to the HW struct
 * @idx: the timestamp index to read
 * @hi: 8 bit timestamp high value
 * @lo: 32 bit timestamp low value
 *
 * Read a 8bit timestamp high value and 32 bit timestamp low value out of the
 * timestamp block of the external PHY on the E810 device using the low latency
 * timestamp read.
 */
static int
ice_read_phy_tstamp_ll_e810(struct ice_hw *hw, u8 idx, u8 *hi, u32 *lo)
{
	u32 val;
	u8 i;

	/* Write TS index to read to the PF register so the FW can read it */
	val = FIELD_PREP(TS_LL_READ_TS_IDX, idx) | TS_LL_READ_TS;
	wr32(hw, PF_SB_ATQBAL, val);

	/* Read the register repeatedly until the FW provides us the TS */
	for (i = TS_LL_READ_RETRIES; i > 0; i--) {
		val = rd32(hw, PF_SB_ATQBAL);

		/* When the bit is cleared, the TS is ready in the register */
		if (!(FIELD_GET(TS_LL_READ_TS, val))) {
			/* High 8 bit value of the TS is on the bits 16:23 */
			*hi = FIELD_GET(TS_LL_READ_TS_HIGH, val);

			/* Read the low 32 bit value and set the TS valid bit */
			*lo = rd32(hw, PF_SB_ATQBAH) | TS_VALID;
			return 0;
		}

		udelay(10);
	}

	/* FW failed to provide the TS in time */
	ice_debug(hw, ICE_DBG_PTP, "Failed to read PTP timestamp using low latency read\n");
	return -EINVAL;
}

/**
 * ice_read_phy_tstamp_sbq_e810 - Read a PHY timestamp registers through the sbq
 * @hw: pointer to the HW struct
 * @lport: the lport to read from
 * @idx: the timestamp index to read
 * @hi: 8 bit timestamp high value
 * @lo: 32 bit timestamp low value
 *
 * Read a 8bit timestamp high value and 32 bit timestamp low value out of the
 * timestamp block of the external PHY on the E810 device using sideband queue.
 */
static int
ice_read_phy_tstamp_sbq_e810(struct ice_hw *hw, u8 lport, u8 idx, u8 *hi,
			     u32 *lo)
{
	u32 hi_addr = TS_EXT(HIGH_TX_MEMORY_BANK_START, lport, idx);
	u32 lo_addr = TS_EXT(LOW_TX_MEMORY_BANK_START, lport, idx);
	u32 lo_val, hi_val;
	int err;

	err = ice_read_phy_reg_e810(hw, lo_addr, &lo_val);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read low PTP timestamp register, err %d\n",
			  err);
		return err;
	}

	err = ice_read_phy_reg_e810(hw, hi_addr, &hi_val);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read high PTP timestamp register, err %d\n",
			  err);
		return err;
	}

	*lo = lo_val;
	*hi = (u8)hi_val;

	return 0;
}

/**
 * ice_read_phy_tstamp_e810 - Read a PHY timestamp out of the external PHY
 * @hw: pointer to the HW struct
 * @lport: the lport to read from
 * @idx: the timestamp index to read
 * @tstamp: on return, the 40bit timestamp value
 *
 * Read a 40bit timestamp value out of the timestamp block of the external PHY
 * on the E810 device.
 */
static int
ice_read_phy_tstamp_e810(struct ice_hw *hw, u8 lport, u8 idx, u64 *tstamp)
{
	u32 lo = 0;
	u8 hi = 0;
	int err;

	if (hw->dev_caps.ts_dev_info.ts_ll_read)
		err = ice_read_phy_tstamp_ll_e810(hw, idx, &hi, &lo);
	else
		err = ice_read_phy_tstamp_sbq_e810(hw, lport, idx, &hi, &lo);

	if (err)
		return err;

	/* For E810 devices, the timestamp is reported with the lower 32 bits
	 * in the low register, and the upper 8 bits in the high register.
	 */
	*tstamp = ((u64)hi) << TS_HIGH_S | ((u64)lo & TS_LOW_M);

	return 0;
}

/**
 * ice_clear_phy_tstamp_e810 - Clear a timestamp from the external PHY
 * @hw: pointer to the HW struct
 * @lport: the lport to read from
 * @idx: the timestamp index to reset
 *
 * Read the timestamp and then forcibly overwrite its value to clear the valid
 * bit from the timestamp block of the external PHY on the E810 device.
 *
 * This function should only be called on an idx whose bit is set according to
 * ice_get_phy_tx_tstamp_ready().
 */
static int ice_clear_phy_tstamp_e810(struct ice_hw *hw, u8 lport, u8 idx)
{
	u32 lo_addr, hi_addr;
	u64 unused_tstamp;
	int err;

	err = ice_read_phy_tstamp_e810(hw, lport, idx, &unused_tstamp);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read the timestamp register for lport %u, idx %u, err %d\n",
			  lport, idx, err);
		return err;
	}

	lo_addr = TS_EXT(LOW_TX_MEMORY_BANK_START, lport, idx);
	hi_addr = TS_EXT(HIGH_TX_MEMORY_BANK_START, lport, idx);

	err = ice_write_phy_reg_e810(hw, lo_addr, 0);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to clear low PTP timestamp register for lport %u, idx %u, err %d\n",
			  lport, idx, err);
		return err;
	}

	err = ice_write_phy_reg_e810(hw, hi_addr, 0);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to clear high PTP timestamp register for lport %u, idx %u, err %d\n",
			  lport, idx, err);
		return err;
	}

	return 0;
}

/**
 * ice_ptp_init_phy_e810 - Enable PTP function on the external PHY
 * @hw: pointer to HW struct
 *
 * Enable the timesync PTP functionality for the external PHY connected to
 * this function.
 */
int ice_ptp_init_phy_e810(struct ice_hw *hw)
{
	u8 tmr_idx;
	int err;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;
	err = ice_write_phy_reg_e810(hw, ETH_GLTSYN_ENA(tmr_idx),
				     GLTSYN_ENA_TSYN_ENA_M);
	if (err)
		ice_debug(hw, ICE_DBG_PTP, "PTP failed in ena_phy_time_syn %d\n",
			  err);

	return err;
}

/**
 * ice_ptp_init_phc_e810 - Perform E810 specific PHC initialization
 * @hw: pointer to HW struct
 *
 * Perform E810-specific PTP hardware clock initialization steps.
 */
static int ice_ptp_init_phc_e810(struct ice_hw *hw)
{
	/* Ensure synchronization delay is zero */
	wr32(hw, GLTSYN_SYNC_DLAY, 0);

	/* Initialize the PHY */
	return ice_ptp_init_phy_e810(hw);
}

/**
 * ice_ptp_prep_phy_time_e810 - Prepare PHY port with initial time
 * @hw: Board private structure
 * @time: Time to initialize the PHY port clock to
 *
 * Program the PHY port ETH_GLTSYN_SHTIME registers in preparation setting the
 * initial clock time. The time will not actually be programmed until the
 * driver issues an ICE_PTP_INIT_TIME command.
 *
 * The time value is the upper 32 bits of the PHY timer, usually in units of
 * nominal nanoseconds.
 */
static int ice_ptp_prep_phy_time_e810(struct ice_hw *hw, u32 time)
{
	u8 tmr_idx;
	int err;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;
	err = ice_write_phy_reg_e810(hw, ETH_GLTSYN_SHTIME_0(tmr_idx), 0);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write SHTIME_0, err %d\n",
			  err);
		return err;
	}

	err = ice_write_phy_reg_e810(hw, ETH_GLTSYN_SHTIME_L(tmr_idx), time);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write SHTIME_L, err %d\n",
			  err);
		return err;
	}

	return 0;
}

/**
 * ice_ptp_prep_phy_adj_e810 - Prep PHY port for a time adjustment
 * @hw: pointer to HW struct
 * @adj: adjustment value to program
 *
 * Prepare the PHY port for an atomic adjustment by programming the PHY
 * ETH_GLTSYN_SHADJ_L and ETH_GLTSYN_SHADJ_H registers. The actual adjustment
 * is completed by issuing an ICE_PTP_ADJ_TIME sync command.
 *
 * The adjustment value only contains the portion used for the upper 32bits of
 * the PHY timer, usually in units of nominal nanoseconds. Negative
 * adjustments are supported using 2s complement arithmetic.
 */
static int ice_ptp_prep_phy_adj_e810(struct ice_hw *hw, s32 adj)
{
	u8 tmr_idx;
	int err;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;

	/* Adjustments are represented as signed 2's complement values in
	 * nanoseconds. Sub-nanosecond adjustment is not supported.
	 */
	err = ice_write_phy_reg_e810(hw, ETH_GLTSYN_SHADJ_L(tmr_idx), 0);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write adj to PHY SHADJ_L, err %d\n",
			  err);
		return err;
	}

	err = ice_write_phy_reg_e810(hw, ETH_GLTSYN_SHADJ_H(tmr_idx), adj);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write adj to PHY SHADJ_H, err %d\n",
			  err);
		return err;
	}

	return 0;
}

/**
 * ice_ptp_prep_phy_incval_e810 - Prep PHY port increment value change
 * @hw: pointer to HW struct
 * @incval: The new 40bit increment value to prepare
 *
 * Prepare the PHY port for a new increment value by programming the PHY
 * ETH_GLTSYN_SHADJ_L and ETH_GLTSYN_SHADJ_H registers. The actual change is
 * completed by issuing an ICE_PTP_INIT_INCVAL command.
 */
static int ice_ptp_prep_phy_incval_e810(struct ice_hw *hw, u64 incval)
{
	u32 high, low;
	u8 tmr_idx;
	int err;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;
	low = lower_32_bits(incval);
	high = upper_32_bits(incval);

	err = ice_write_phy_reg_e810(hw, ETH_GLTSYN_SHADJ_L(tmr_idx), low);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write incval to PHY SHADJ_L, err %d\n",
			  err);
		return err;
	}

	err = ice_write_phy_reg_e810(hw, ETH_GLTSYN_SHADJ_H(tmr_idx), high);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write incval PHY SHADJ_H, err %d\n",
			  err);
		return err;
	}

	return 0;
}

/**
 * ice_ptp_port_cmd_e810 - Prepare all external PHYs for a timer command
 * @hw: pointer to HW struct
 * @cmd: Command to be sent to the port
 *
 * Prepare the external PHYs connected to this device for a timer sync
 * command.
 */
static int ice_ptp_port_cmd_e810(struct ice_hw *hw, enum ice_ptp_tmr_cmd cmd)
{
	u32 cmd_val, val;
	int err;

	switch (cmd) {
	case ICE_PTP_INIT_TIME:
		cmd_val = GLTSYN_CMD_INIT_TIME;
		break;
	case ICE_PTP_INIT_INCVAL:
		cmd_val = GLTSYN_CMD_INIT_INCVAL;
		break;
	case ICE_PTP_ADJ_TIME:
		cmd_val = GLTSYN_CMD_ADJ_TIME;
		break;
	case ICE_PTP_READ_TIME:
		cmd_val = GLTSYN_CMD_READ_TIME;
		break;
	case ICE_PTP_ADJ_TIME_AT_TIME:
		cmd_val = GLTSYN_CMD_ADJ_INIT_TIME;
		break;
	case ICE_PTP_NOP:
		return 0;
	}

	/* Read, modify, write */
	err = ice_read_phy_reg_e810(hw, ETH_GLTSYN_CMD, &val);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read GLTSYN_CMD, err %d\n", err);
		return err;
	}

	/* Modify necessary bits only and perform write */
	val &= ~TS_CMD_MASK_E810;
	val |= cmd_val;

	err = ice_write_phy_reg_e810(hw, ETH_GLTSYN_CMD, val);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to write back GLTSYN_CMD, err %d\n", err);
		return err;
	}

	return 0;
}

/**
 * ice_get_phy_tx_tstamp_ready_e810 - Read Tx memory status register
 * @hw: pointer to the HW struct
 * @port: the PHY port to read
 * @tstamp_ready: contents of the Tx memory status register
 *
 * E810 devices do not use a Tx memory status register. Instead simply
 * indicate that all timestamps are currently ready.
 */
static int
ice_get_phy_tx_tstamp_ready_e810(struct ice_hw *hw, u8 port, u64 *tstamp_ready)
{
	*tstamp_ready = 0xFFFFFFFFFFFFFFFF;
	return 0;
}

/* E810T SMA functions
 *
 * The following functions operate specifically on E810T hardware and are used
 * to access the extended GPIOs available.
 */

/**
 * ice_get_pca9575_handle
 * @hw: pointer to the hw struct
 * @pca9575_handle: GPIO controller's handle
 *
 * Find and return the GPIO controller's handle in the netlist.
 * When found - the value will be cached in the hw structure and following calls
 * will return cached value
 */
static int
ice_get_pca9575_handle(struct ice_hw *hw, u16 *pca9575_handle)
{
	struct ice_aqc_get_link_topo *cmd;
	struct ice_aq_desc desc;
	int status;
	u8 idx;

	/* If handle was read previously return cached value */
	if (hw->io_expander_handle) {
		*pca9575_handle = hw->io_expander_handle;
		return 0;
	}

	/* If handle was not detected read it from the netlist */
	cmd = &desc.params.get_link_topo;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_link_topo);

	/* Set node type to GPIO controller */
	cmd->addr.topo_params.node_type_ctx =
		(ICE_AQC_LINK_TOPO_NODE_TYPE_M &
		 ICE_AQC_LINK_TOPO_NODE_TYPE_GPIO_CTRL);

#define SW_PCA9575_SFP_TOPO_IDX		2
#define SW_PCA9575_QSFP_TOPO_IDX	1

	/* Check if the SW IO expander controlling SMA exists in the netlist. */
	if (hw->device_id == ICE_DEV_ID_E810C_SFP)
		idx = SW_PCA9575_SFP_TOPO_IDX;
	else if (hw->device_id == ICE_DEV_ID_E810C_QSFP)
		idx = SW_PCA9575_QSFP_TOPO_IDX;
	else
		return -EOPNOTSUPP;

	cmd->addr.topo_params.index = idx;

	status = ice_aq_send_cmd(hw, &desc, NULL, 0, NULL);
	if (status)
		return -EOPNOTSUPP;

	/* Verify if we found the right IO expander type */
	if (desc.params.get_link_topo.node_part_num !=
		ICE_AQC_GET_LINK_TOPO_NODE_NR_PCA9575)
		return -EOPNOTSUPP;

	/* If present save the handle and return it */
	hw->io_expander_handle =
		le16_to_cpu(desc.params.get_link_topo.addr.handle);
	*pca9575_handle = hw->io_expander_handle;

	return 0;
}

/**
 * ice_read_sma_ctrl_e810t
 * @hw: pointer to the hw struct
 * @data: pointer to data to be read from the GPIO controller
 *
 * Read the SMA controller state. It is connected to pins 3-7 of Port 1 of the
 * PCA9575 expander, so only bits 3-7 in data are valid.
 */
int ice_read_sma_ctrl_e810t(struct ice_hw *hw, u8 *data)
{
	int status;
	u16 handle;
	u8 i;

	status = ice_get_pca9575_handle(hw, &handle);
	if (status)
		return status;

	*data = 0;

	for (i = ICE_SMA_MIN_BIT_E810T; i <= ICE_SMA_MAX_BIT_E810T; i++) {
		bool pin;

		status = ice_aq_get_gpio(hw, handle, i + ICE_PCA9575_P1_OFFSET,
					 &pin, NULL);
		if (status)
			break;
		*data |= (u8)(!pin) << i;
	}

	return status;
}

/**
 * ice_write_sma_ctrl_e810t
 * @hw: pointer to the hw struct
 * @data: data to be written to the GPIO controller
 *
 * Write the data to the SMA controller. It is connected to pins 3-7 of Port 1
 * of the PCA9575 expander, so only bits 3-7 in data are valid.
 */
int ice_write_sma_ctrl_e810t(struct ice_hw *hw, u8 data)
{
	int status;
	u16 handle;
	u8 i;

	status = ice_get_pca9575_handle(hw, &handle);
	if (status)
		return status;

	for (i = ICE_SMA_MIN_BIT_E810T; i <= ICE_SMA_MAX_BIT_E810T; i++) {
		bool pin;

		pin = !(data & (1 << i));
		status = ice_aq_set_gpio(hw, handle, i + ICE_PCA9575_P1_OFFSET,
					 pin, NULL);
		if (status)
			break;
	}

	return status;
}

/**
 * ice_read_pca9575_reg_e810t
 * @hw: pointer to the hw struct
 * @offset: GPIO controller register offset
 * @data: pointer to data to be read from the GPIO controller
 *
 * Read the register from the GPIO controller
 */
int ice_read_pca9575_reg_e810t(struct ice_hw *hw, u8 offset, u8 *data)
{
	struct ice_aqc_link_topo_addr link_topo;
	__le16 addr;
	u16 handle;
	int err;

	memset(&link_topo, 0, sizeof(link_topo));

	err = ice_get_pca9575_handle(hw, &handle);
	if (err)
		return err;

	link_topo.handle = cpu_to_le16(handle);
	link_topo.topo_params.node_type_ctx =
		FIELD_PREP(ICE_AQC_LINK_TOPO_NODE_CTX_M,
			   ICE_AQC_LINK_TOPO_NODE_CTX_PROVIDED);

	addr = cpu_to_le16((u16)offset);

	return ice_aq_read_i2c(hw, link_topo, 0, addr, 1, data, NULL);
}

/* Device agnostic functions
 *
 * The following functions implement shared behavior common to both E822 and
 * E810 devices, possibly calling a device specific implementation where
 * necessary.
 */

/**
 * ice_ptp_lock - Acquire PTP global semaphore register lock
 * @hw: pointer to the HW struct
 *
 * Acquire the global PTP hardware semaphore lock. Returns true if the lock
 * was acquired, false otherwise.
 *
 * The PFTSYN_SEM register sets the busy bit on read, returning the previous
 * value. If software sees the busy bit cleared, this means that this function
 * acquired the lock (and the busy bit is now set). If software sees the busy
 * bit set, it means that another function acquired the lock.
 *
 * Software must clear the busy bit with a write to release the lock for other
 * functions when done.
 */
bool ice_ptp_lock(struct ice_hw *hw)
{
	u32 hw_lock;
	int i;

#define MAX_TRIES 15

	for (i = 0; i < MAX_TRIES; i++) {
		hw_lock = rd32(hw, PFTSYN_SEM + (PFTSYN_SEM_BYTES * hw->pf_id));
		hw_lock = hw_lock & PFTSYN_SEM_BUSY_M;
		if (hw_lock) {
			/* Somebody is holding the lock */
			usleep_range(5000, 6000);
			continue;
		}

		break;
	}

	return !hw_lock;
}

/**
 * ice_ptp_unlock - Release PTP global semaphore register lock
 * @hw: pointer to the HW struct
 *
 * Release the global PTP hardware semaphore lock. This is done by writing to
 * the PFTSYN_SEM register.
 */
void ice_ptp_unlock(struct ice_hw *hw)
{
	wr32(hw, PFTSYN_SEM + (PFTSYN_SEM_BYTES * hw->pf_id), 0);
}

/**
 * ice_ptp_init_phy_model - Initialize hw->phy_model based on device type
 * @hw: pointer to the HW structure
 *
 * Determine the PHY model for the device, and initialize hw->phy_model
 * for use by other functions.
 */
void ice_ptp_init_phy_model(struct ice_hw *hw)
{
	if (ice_is_e810(hw))
		hw->phy_model = ICE_PHY_E810;
	else
		hw->phy_model = ICE_PHY_E822;
}

/**
 * ice_ptp_tmr_cmd - Prepare and trigger a timer sync command
 * @hw: pointer to HW struct
 * @cmd: the command to issue
 *
 * Prepare the source timer and PHY timers and then trigger the requested
 * command. This causes the shadow registers previously written in preparation
 * for the command to be synchronously applied to both the source and PHY
 * timers.
 */
static int ice_ptp_tmr_cmd(struct ice_hw *hw, enum ice_ptp_tmr_cmd cmd)
{
	int err;

	/* First, prepare the source timer */
	ice_ptp_src_cmd(hw, cmd);

	/* Next, prepare the ports */
	switch (hw->phy_model) {
	case ICE_PHY_E810:
		err = ice_ptp_port_cmd_e810(hw, cmd);
		break;
	case ICE_PHY_E822:
		err = ice_ptp_port_cmd_e822(hw, cmd);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to prepare PHY ports for timer command %u, err %d\n",
			  cmd, err);
		return err;
	}

	/* Write the sync command register to drive both source and PHY timer
	 * commands synchronously
	 */
	ice_ptp_exec_tmr_cmd(hw);

	return 0;
}

/**
 * ice_ptp_init_time - Initialize device time to provided value
 * @hw: pointer to HW struct
 * @time: 64bits of time (GLTSYN_TIME_L and GLTSYN_TIME_H)
 *
 * Initialize the device to the specified time provided. This requires a three
 * step process:
 *
 * 1) write the new init time to the source timer shadow registers
 * 2) write the new init time to the PHY timer shadow registers
 * 3) issue an init_time timer command to synchronously switch both the source
 *    and port timers to the new init time value at the next clock cycle.
 */
int ice_ptp_init_time(struct ice_hw *hw, u64 time)
{
	u8 tmr_idx;
	int err;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;

	/* Source timers */
	wr32(hw, GLTSYN_SHTIME_L(tmr_idx), lower_32_bits(time));
	wr32(hw, GLTSYN_SHTIME_H(tmr_idx), upper_32_bits(time));
	wr32(hw, GLTSYN_SHTIME_0(tmr_idx), 0);

	/* PHY timers */
	/* Fill Rx and Tx ports and send msg to PHY */
	switch (hw->phy_model) {
	case ICE_PHY_E810:
		err = ice_ptp_prep_phy_time_e810(hw, time & 0xFFFFFFFF);
		break;
	case ICE_PHY_E822:
		err = ice_ptp_prep_phy_time_e822(hw, time & 0xFFFFFFFF);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	if (err)
		return err;

	return ice_ptp_tmr_cmd(hw, ICE_PTP_INIT_TIME);
}

/**
 * ice_ptp_write_incval - Program PHC with new increment value
 * @hw: pointer to HW struct
 * @incval: Source timer increment value per clock cycle
 *
 * Program the PHC with a new increment value. This requires a three-step
 * process:
 *
 * 1) Write the increment value to the source timer shadow registers
 * 2) Write the increment value to the PHY timer shadow registers
 * 3) Issue an ICE_PTP_INIT_INCVAL timer command to synchronously switch both
 *    the source and port timers to the new increment value at the next clock
 *    cycle.
 */
int ice_ptp_write_incval(struct ice_hw *hw, u64 incval)
{
	u8 tmr_idx;
	int err;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;

	/* Shadow Adjust */
	wr32(hw, GLTSYN_SHADJ_L(tmr_idx), lower_32_bits(incval));
	wr32(hw, GLTSYN_SHADJ_H(tmr_idx), upper_32_bits(incval));

	switch (hw->phy_model) {
	case ICE_PHY_E810:
		err = ice_ptp_prep_phy_incval_e810(hw, incval);
		break;
	case ICE_PHY_E822:
		err = ice_ptp_prep_phy_incval_e822(hw, incval);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	if (err)
		return err;

	return ice_ptp_tmr_cmd(hw, ICE_PTP_INIT_INCVAL);
}

/**
 * ice_ptp_write_incval_locked - Program new incval while holding semaphore
 * @hw: pointer to HW struct
 * @incval: Source timer increment value per clock cycle
 *
 * Program a new PHC incval while holding the PTP semaphore.
 */
int ice_ptp_write_incval_locked(struct ice_hw *hw, u64 incval)
{
	int err;

	if (!ice_ptp_lock(hw))
		return -EBUSY;

	err = ice_ptp_write_incval(hw, incval);

	ice_ptp_unlock(hw);

	return err;
}

/**
 * ice_ptp_adj_clock - Adjust PHC clock time atomically
 * @hw: pointer to HW struct
 * @adj: Adjustment in nanoseconds
 *
 * Perform an atomic adjustment of the PHC time by the specified number of
 * nanoseconds. This requires a three-step process:
 *
 * 1) Write the adjustment to the source timer shadow registers
 * 2) Write the adjustment to the PHY timer shadow registers
 * 3) Issue an ICE_PTP_ADJ_TIME timer command to synchronously apply the
 *    adjustment to both the source and port timers at the next clock cycle.
 */
int ice_ptp_adj_clock(struct ice_hw *hw, s32 adj)
{
	u8 tmr_idx;
	int err;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;

	/* Write the desired clock adjustment into the GLTSYN_SHADJ register.
	 * For an ICE_PTP_ADJ_TIME command, this set of registers represents
	 * the value to add to the clock time. It supports subtraction by
	 * interpreting the value as a 2's complement integer.
	 */
	wr32(hw, GLTSYN_SHADJ_L(tmr_idx), 0);
	wr32(hw, GLTSYN_SHADJ_H(tmr_idx), adj);

	switch (hw->phy_model) {
	case ICE_PHY_E810:
		err = ice_ptp_prep_phy_adj_e810(hw, adj);
		break;
	case ICE_PHY_E822:
		err = ice_ptp_prep_phy_adj_e822(hw, adj);
		break;
	default:
		err = -EOPNOTSUPP;
	}

	if (err)
		return err;

	return ice_ptp_tmr_cmd(hw, ICE_PTP_ADJ_TIME);
}

/**
 * ice_read_phy_tstamp - Read a PHY timestamp from the timestamo block
 * @hw: pointer to the HW struct
 * @block: the block to read from
 * @idx: the timestamp index to read
 * @tstamp: on return, the 40bit timestamp value
 *
 * Read a 40bit timestamp value out of the timestamp block. For E822 devices,
 * the block is the quad to read from. For E810 devices, the block is the
 * logical port to read from.
 */
int ice_read_phy_tstamp(struct ice_hw *hw, u8 block, u8 idx, u64 *tstamp)
{
	switch (hw->phy_model) {
	case ICE_PHY_E810:
		return ice_read_phy_tstamp_e810(hw, block, idx, tstamp);
	case ICE_PHY_E822:
		return ice_read_phy_tstamp_e822(hw, block, idx, tstamp);
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * ice_clear_phy_tstamp - Clear a timestamp from the timestamp block
 * @hw: pointer to the HW struct
 * @block: the block to read from
 * @idx: the timestamp index to reset
 *
 * Clear a timestamp from the timestamp block, discarding its value without
 * returning it. This resets the memory status bit for the timestamp index
 * allowing it to be reused for another timestamp in the future.
 *
 * For E822 devices, the block number is the PHY quad to clear from. For E810
 * devices, the block number is the logical port to clear from.
 *
 * This function must only be called on a timestamp index whose valid bit is
 * set according to ice_get_phy_tx_tstamp_ready().
 */
int ice_clear_phy_tstamp(struct ice_hw *hw, u8 block, u8 idx)
{
	switch (hw->phy_model) {
	case ICE_PHY_E810:
		return ice_clear_phy_tstamp_e810(hw, block, idx);
	case ICE_PHY_E822:
		return ice_clear_phy_tstamp_e822(hw, block, idx);
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * ice_get_pf_c827_idx - find and return the C827 index for the current pf
 * @hw: pointer to the hw struct
 * @idx: index of the found C827 PHY
 * Return:
 * * 0 - success
 * * negative - failure
 */
int ice_get_pf_c827_idx(struct ice_hw *hw, u8 *idx)
{
	struct ice_aqc_get_link_topo cmd;
	u8 node_part_number;
	u16 node_handle;
	int status;
	u8 ctx;

	if (hw->mac_type != ICE_MAC_E810)
		return -ENODEV;

	if (hw->device_id != ICE_DEV_ID_E810C_QSFP) {
		*idx = C827_0;
		return 0;
	}

	memset(&cmd, 0, sizeof(cmd));

	ctx = ICE_AQC_LINK_TOPO_NODE_TYPE_PHY << ICE_AQC_LINK_TOPO_NODE_TYPE_S;
	ctx |= ICE_AQC_LINK_TOPO_NODE_CTX_PORT << ICE_AQC_LINK_TOPO_NODE_CTX_S;
	cmd.addr.topo_params.node_type_ctx = ctx;

	status = ice_aq_get_netlist_node(hw, &cmd, &node_part_number,
					 &node_handle);
	if (status || node_part_number != ICE_AQC_GET_LINK_TOPO_NODE_NR_C827)
		return -ENOENT;

	if (node_handle == E810C_QSFP_C827_0_HANDLE)
		*idx = C827_0;
	else if (node_handle == E810C_QSFP_C827_1_HANDLE)
		*idx = C827_1;
	else
		return -EIO;

	return 0;
}

/**
 * ice_ptp_reset_ts_memory - Reset timestamp memory for all blocks
 * @hw: pointer to the HW struct
 */
void ice_ptp_reset_ts_memory(struct ice_hw *hw)
{
	switch (hw->phy_model) {
	case ICE_PHY_E822:
		ice_ptp_reset_ts_memory_e822(hw);
		break;
	case ICE_PHY_E810:
	default:
		return;
	}
}

/**
 * ice_ptp_init_phc - Initialize PTP hardware clock
 * @hw: pointer to the HW struct
 *
 * Perform the steps required to initialize the PTP hardware clock.
 */
int ice_ptp_init_phc(struct ice_hw *hw)
{
	u8 src_idx = hw->func_caps.ts_func_info.tmr_index_owned;

	/* Enable source clocks */
	wr32(hw, GLTSYN_ENA(src_idx), GLTSYN_ENA_TSYN_ENA_M);

	/* Clear event err indications for auxiliary pins */
	(void)rd32(hw, GLTSYN_STAT(src_idx));

	switch (hw->phy_model) {
	case ICE_PHY_E810:
		return ice_ptp_init_phc_e810(hw);
	case ICE_PHY_E822:
		return ice_ptp_init_phc_e822(hw);
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * ice_get_phy_tx_tstamp_ready - Read PHY Tx memory status indication
 * @hw: pointer to the HW struct
 * @block: the timestamp block to check
 * @tstamp_ready: storage for the PHY Tx memory status information
 *
 * Check the PHY for Tx timestamp memory status. This reports a 64 bit value
 * which indicates which timestamps in the block may be captured. A set bit
 * means the timestamp can be read. An unset bit means the timestamp is not
 * ready and software should avoid reading the register.
 */
int ice_get_phy_tx_tstamp_ready(struct ice_hw *hw, u8 block, u64 *tstamp_ready)
{
	switch (hw->phy_model) {
	case ICE_PHY_E810:
		return ice_get_phy_tx_tstamp_ready_e810(hw, block,
							tstamp_ready);
	case ICE_PHY_E822:
		return ice_get_phy_tx_tstamp_ready_e822(hw, block,
							tstamp_ready);
		break;
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * ice_cgu_get_pin_desc_e823 - get pin description array
 * @hw: pointer to the hw struct
 * @input: if request is done against input or output pin
 * @size: number of inputs/outputs
 *
 * Return: pointer to pin description array associated to given hw.
 */
static const struct ice_cgu_pin_desc *
ice_cgu_get_pin_desc_e823(struct ice_hw *hw, bool input, int *size)
{
	static const struct ice_cgu_pin_desc *t;

	if (hw->cgu_part_number ==
	    ICE_AQC_GET_LINK_TOPO_NODE_NR_ZL30632_80032) {
		if (input) {
			t = ice_e823_zl_cgu_inputs;
			*size = ARRAY_SIZE(ice_e823_zl_cgu_inputs);
		} else {
			t = ice_e823_zl_cgu_outputs;
			*size = ARRAY_SIZE(ice_e823_zl_cgu_outputs);
		}
	} else if (hw->cgu_part_number ==
		   ICE_AQC_GET_LINK_TOPO_NODE_NR_SI5383_5384) {
		if (input) {
			t = ice_e823_si_cgu_inputs;
			*size = ARRAY_SIZE(ice_e823_si_cgu_inputs);
		} else {
			t = ice_e823_si_cgu_outputs;
			*size = ARRAY_SIZE(ice_e823_si_cgu_outputs);
		}
	} else {
		t = NULL;
		*size = 0;
	}

	return t;
}

/**
 * ice_cgu_get_pin_desc - get pin description array
 * @hw: pointer to the hw struct
 * @input: if request is done against input or output pins
 * @size: size of array returned by function
 *
 * Return: pointer to pin description array associated to given hw.
 */
static const struct ice_cgu_pin_desc *
ice_cgu_get_pin_desc(struct ice_hw *hw, bool input, int *size)
{
	const struct ice_cgu_pin_desc *t = NULL;

	switch (hw->device_id) {
	case ICE_DEV_ID_E810C_SFP:
		if (input) {
			t = ice_e810t_sfp_cgu_inputs;
			*size = ARRAY_SIZE(ice_e810t_sfp_cgu_inputs);
		} else {
			t = ice_e810t_sfp_cgu_outputs;
			*size = ARRAY_SIZE(ice_e810t_sfp_cgu_outputs);
		}
		break;
	case ICE_DEV_ID_E810C_QSFP:
		if (input) {
			t = ice_e810t_qsfp_cgu_inputs;
			*size = ARRAY_SIZE(ice_e810t_qsfp_cgu_inputs);
		} else {
			t = ice_e810t_qsfp_cgu_outputs;
			*size = ARRAY_SIZE(ice_e810t_qsfp_cgu_outputs);
		}
		break;
	case ICE_DEV_ID_E823L_10G_BASE_T:
	case ICE_DEV_ID_E823L_1GBE:
	case ICE_DEV_ID_E823L_BACKPLANE:
	case ICE_DEV_ID_E823L_QSFP:
	case ICE_DEV_ID_E823L_SFP:
	case ICE_DEV_ID_E823C_10G_BASE_T:
	case ICE_DEV_ID_E823C_BACKPLANE:
	case ICE_DEV_ID_E823C_QSFP:
	case ICE_DEV_ID_E823C_SFP:
	case ICE_DEV_ID_E823C_SGMII:
		t = ice_cgu_get_pin_desc_e823(hw, input, size);
		break;
	default:
		break;
	}

	return t;
}

/**
 * ice_cgu_get_pin_type - get pin's type
 * @hw: pointer to the hw struct
 * @pin: pin index
 * @input: if request is done against input or output pin
 *
 * Return: type of a pin.
 */
enum dpll_pin_type ice_cgu_get_pin_type(struct ice_hw *hw, u8 pin, bool input)
{
	const struct ice_cgu_pin_desc *t;
	int t_size;

	t = ice_cgu_get_pin_desc(hw, input, &t_size);

	if (!t)
		return 0;

	if (pin >= t_size)
		return 0;

	return t[pin].type;
}

/**
 * ice_cgu_get_pin_freq_supp - get pin's supported frequency
 * @hw: pointer to the hw struct
 * @pin: pin index
 * @input: if request is done against input or output pin
 * @num: output number of supported frequencies
 *
 * Get frequency supported number and array of supported frequencies.
 *
 * Return: array of supported frequencies for given pin.
 */
struct dpll_pin_frequency *
ice_cgu_get_pin_freq_supp(struct ice_hw *hw, u8 pin, bool input, u8 *num)
{
	const struct ice_cgu_pin_desc *t;
	int t_size;

	*num = 0;
	t = ice_cgu_get_pin_desc(hw, input, &t_size);
	if (!t)
		return NULL;
	if (pin >= t_size)
		return NULL;
	*num = t[pin].freq_supp_num;

	return t[pin].freq_supp;
}

/**
 * ice_cgu_get_pin_name - get pin's name
 * @hw: pointer to the hw struct
 * @pin: pin index
 * @input: if request is done against input or output pin
 *
 * Return:
 * * null terminated char array with name
 * * NULL in case of failure
 */
const char *ice_cgu_get_pin_name(struct ice_hw *hw, u8 pin, bool input)
{
	const struct ice_cgu_pin_desc *t;
	int t_size;

	t = ice_cgu_get_pin_desc(hw, input, &t_size);

	if (!t)
		return NULL;

	if (pin >= t_size)
		return NULL;

	return t[pin].name;
}

/**
 * ice_get_cgu_state - get the state of the DPLL
 * @hw: pointer to the hw struct
 * @dpll_idx: Index of internal DPLL unit
 * @last_dpll_state: last known state of DPLL
 * @pin: pointer to a buffer for returning currently active pin
 * @ref_state: reference clock state
 * @eec_mode: eec mode of the DPLL
 * @phase_offset: pointer to a buffer for returning phase offset
 * @dpll_state: state of the DPLL (output)
 *
 * This function will read the state of the DPLL(dpll_idx). Non-null
 * 'pin', 'ref_state', 'eec_mode' and 'phase_offset' parameters are used to
 * retrieve currently active pin, state, mode and phase_offset respectively.
 *
 * Return: state of the DPLL
 */
int ice_get_cgu_state(struct ice_hw *hw, u8 dpll_idx,
		      enum dpll_lock_status last_dpll_state, u8 *pin,
		      u8 *ref_state, u8 *eec_mode, s64 *phase_offset,
		      enum dpll_lock_status *dpll_state)
{
	u8 hw_ref_state, hw_dpll_state, hw_eec_mode, hw_config;
	s64 hw_phase_offset;
	int status;

	status = ice_aq_get_cgu_dpll_status(hw, dpll_idx, &hw_ref_state,
					    &hw_dpll_state, &hw_config,
					    &hw_phase_offset, &hw_eec_mode);
	if (status)
		return status;

	if (pin)
		/* current ref pin in dpll_state_refsel_status_X register */
		*pin = hw_config & ICE_AQC_GET_CGU_DPLL_CONFIG_CLK_REF_SEL;
	if (phase_offset)
		*phase_offset = hw_phase_offset;
	if (ref_state)
		*ref_state = hw_ref_state;
	if (eec_mode)
		*eec_mode = hw_eec_mode;
	if (!dpll_state)
		return 0;

	/* According to ZL DPLL documentation, once state reach LOCKED_HO_ACQ
	 * it would never return to FREERUN. This aligns to ITU-T G.781
	 * Recommendation. We cannot report HOLDOVER as HO memory is cleared
	 * while switching to another reference.
	 * Only for situations where previous state was either: "LOCKED without
	 * HO_ACQ" or "HOLDOVER" we actually back to FREERUN.
	 */
	if (hw_dpll_state & ICE_AQC_GET_CGU_DPLL_STATUS_STATE_LOCK) {
		if (hw_dpll_state & ICE_AQC_GET_CGU_DPLL_STATUS_STATE_HO_READY)
			*dpll_state = DPLL_LOCK_STATUS_LOCKED_HO_ACQ;
		else
			*dpll_state = DPLL_LOCK_STATUS_LOCKED;
	} else if (last_dpll_state == DPLL_LOCK_STATUS_LOCKED_HO_ACQ ||
		   last_dpll_state == DPLL_LOCK_STATUS_HOLDOVER) {
		*dpll_state = DPLL_LOCK_STATUS_HOLDOVER;
	} else {
		*dpll_state = DPLL_LOCK_STATUS_UNLOCKED;
	}

	return 0;
}

/**
 * ice_get_cgu_rclk_pin_info - get info on available recovered clock pins
 * @hw: pointer to the hw struct
 * @base_idx: returns index of first recovered clock pin on device
 * @pin_num: returns number of recovered clock pins available on device
 *
 * Based on hw provide caller info about recovery clock pins available on the
 * board.
 *
 * Return:
 * * 0 - success, information is valid
 * * negative - failure, information is not valid
 */
int ice_get_cgu_rclk_pin_info(struct ice_hw *hw, u8 *base_idx, u8 *pin_num)
{
	u8 phy_idx;
	int ret;

	switch (hw->device_id) {
	case ICE_DEV_ID_E810C_SFP:
	case ICE_DEV_ID_E810C_QSFP:

		ret = ice_get_pf_c827_idx(hw, &phy_idx);
		if (ret)
			return ret;
		*base_idx = E810T_CGU_INPUT_C827(phy_idx, ICE_RCLKA_PIN);
		*pin_num = ICE_E810_RCLK_PINS_NUM;
		ret = 0;
		break;
	case ICE_DEV_ID_E823L_10G_BASE_T:
	case ICE_DEV_ID_E823L_1GBE:
	case ICE_DEV_ID_E823L_BACKPLANE:
	case ICE_DEV_ID_E823L_QSFP:
	case ICE_DEV_ID_E823L_SFP:
	case ICE_DEV_ID_E823C_10G_BASE_T:
	case ICE_DEV_ID_E823C_BACKPLANE:
	case ICE_DEV_ID_E823C_QSFP:
	case ICE_DEV_ID_E823C_SFP:
	case ICE_DEV_ID_E823C_SGMII:
		*pin_num = ICE_E822_RCLK_PINS_NUM;
		ret = 0;
		if (hw->cgu_part_number ==
		    ICE_AQC_GET_LINK_TOPO_NODE_NR_ZL30632_80032)
			*base_idx = ZL_REF1P;
		else if (hw->cgu_part_number ==
			 ICE_AQC_GET_LINK_TOPO_NODE_NR_SI5383_5384)
			*base_idx = SI_REF1P;
		else
			ret = -ENODEV;

		break;
	default:
		ret = -ENODEV;
		break;
	}

	return ret;
}
