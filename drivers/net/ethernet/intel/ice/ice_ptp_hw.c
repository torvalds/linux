// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021, Intel Corporation. */

#include "ice_common.h"
#include "ice_ptp_hw.h"

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
 * The hardware captures timestamps in the PHY for incoming packets, and for
 * outgoing packets on request. To support this, the PHY maintains a timer
 * that matches the lower 64 bits of the global source timer.
 *
 * In order to ensure that the PHY timers and the source timer are equivalent,
 * shadow registers are used to prepare the desired initial values. A special
 * sync command is issued to trigger copying from the shadow registers into
 * the appropriate source and PHY registers simultaneously.
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
	u32 lo_addr, hi_addr, lo, hi;
	int err;

	lo_addr = TS_EXT(LOW_TX_MEMORY_BANK_START, lport, idx);
	hi_addr = TS_EXT(HIGH_TX_MEMORY_BANK_START, lport, idx);

	err = ice_read_phy_reg_e810(hw, lo_addr, &lo);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read low PTP timestamp register, err %d\n",
			  err);
		return err;
	}

	err = ice_read_phy_reg_e810(hw, hi_addr, &hi);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to read high PTP timestamp register, err %d\n",
			  err);
		return err;
	}

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
 * Clear a timestamp, resetting its valid bit, from the timestamp block of the
 * external PHY on the E810 device.
 */
static int ice_clear_phy_tstamp_e810(struct ice_hw *hw, u8 lport, u8 idx)
{
	u32 lo_addr, hi_addr;
	int err;

	lo_addr = TS_EXT(LOW_TX_MEMORY_BANK_START, lport, idx);
	hi_addr = TS_EXT(HIGH_TX_MEMORY_BANK_START, lport, idx);

	err = ice_write_phy_reg_e810(hw, lo_addr, 0);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to clear low PTP timestamp register, err %d\n",
			  err);
		return err;
	}

	err = ice_write_phy_reg_e810(hw, hi_addr, 0);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to clear high PTP timestamp register, err %d\n",
			  err);
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
 * driver issues an INIT_TIME command.
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
 * is completed by issuing an ADJ_TIME sync command.
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
 * completed by issuing an INIT_INCVAL command.
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
	case INIT_TIME:
		cmd_val = GLTSYN_CMD_INIT_TIME;
		break;
	case INIT_INCVAL:
		cmd_val = GLTSYN_CMD_INIT_INCVAL;
		break;
	case ADJ_TIME:
		cmd_val = GLTSYN_CMD_ADJ_TIME;
		break;
	case READ_TIME:
		cmd_val = GLTSYN_CMD_READ_TIME;
		break;
	case ADJ_TIME_AT_TIME:
		cmd_val = GLTSYN_CMD_ADJ_INIT_TIME;
		break;
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

/* Device agnostic functions
 *
 * The following functions implement useful behavior to hide the differences
 * between E810 and other devices. They call the device-specific
 * implementations where necessary.
 *
 * Currently, the driver only supports E810, but future work will enable
 * support for E822-based devices.
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

#define MAX_TRIES 5

	for (i = 0; i < MAX_TRIES; i++) {
		hw_lock = rd32(hw, PFTSYN_SEM + (PFTSYN_SEM_BYTES * hw->pf_id));
		hw_lock = hw_lock & PFTSYN_SEM_BUSY_M;
		if (!hw_lock)
			break;

		/* Somebody is holding the lock */
		usleep_range(10000, 20000);
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
 * ice_ptp_src_cmd - Prepare source timer for a timer command
 * @hw: pointer to HW structure
 * @cmd: Timer command
 *
 * Prepare the source timer for an upcoming timer sync command.
 */
static void ice_ptp_src_cmd(struct ice_hw *hw, enum ice_ptp_tmr_cmd cmd)
{
	u32 cmd_val;
	u8 tmr_idx;

	tmr_idx = ice_get_ptp_src_clock_index(hw);
	cmd_val = tmr_idx << SEL_CPK_SRC;

	switch (cmd) {
	case INIT_TIME:
		cmd_val |= GLTSYN_CMD_INIT_TIME;
		break;
	case INIT_INCVAL:
		cmd_val |= GLTSYN_CMD_INIT_INCVAL;
		break;
	case ADJ_TIME:
		cmd_val |= GLTSYN_CMD_ADJ_TIME;
		break;
	case ADJ_TIME_AT_TIME:
		cmd_val |= GLTSYN_CMD_ADJ_INIT_TIME;
		break;
	case READ_TIME:
		cmd_val |= GLTSYN_CMD_READ_TIME;
		break;
	}

	wr32(hw, GLTSYN_CMD, cmd_val);
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
	err = ice_ptp_port_cmd_e810(hw, cmd);
	if (err) {
		ice_debug(hw, ICE_DBG_PTP, "Failed to prepare PHY ports for timer command %u, err %d\n",
			  cmd, err);
		return err;
	}

	/* Write the sync command register to drive both source and PHY timer commands
	 * synchronously
	 */
	wr32(hw, GLTSYN_CMD_SYNC, SYNC_EXEC_CMD);

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
	err = ice_ptp_prep_phy_time_e810(hw, time & 0xFFFFFFFF);
	if (err)
		return err;

	return ice_ptp_tmr_cmd(hw, INIT_TIME);
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
 * 3) Issue an INIT_INCVAL timer command to synchronously switch both the
 *    source and port timers to the new increment value at the next clock
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

	err = ice_ptp_prep_phy_incval_e810(hw, incval);
	if (err)
		return err;

	return ice_ptp_tmr_cmd(hw, INIT_INCVAL);
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
 * 3) Issue an ADJ_TIME timer command to synchronously apply the adjustment to
 *    both the source and port timers at the next clock cycle.
 */
int ice_ptp_adj_clock(struct ice_hw *hw, s32 adj)
{
	u8 tmr_idx;
	int err;

	tmr_idx = hw->func_caps.ts_func_info.tmr_index_owned;

	/* Write the desired clock adjustment into the GLTSYN_SHADJ register.
	 * For an ADJ_TIME command, this set of registers represents the value
	 * to add to the clock time. It supports subtraction by interpreting
	 * the value as a 2's complement integer.
	 */
	wr32(hw, GLTSYN_SHADJ_L(tmr_idx), 0);
	wr32(hw, GLTSYN_SHADJ_H(tmr_idx), adj);

	err = ice_ptp_prep_phy_adj_e810(hw, adj);
	if (err)
		return err;

	return ice_ptp_tmr_cmd(hw, ADJ_TIME);
}

/**
 * ice_read_phy_tstamp - Read a PHY timestamp from the timestamo block
 * @hw: pointer to the HW struct
 * @block: the block to read from
 * @idx: the timestamp index to read
 * @tstamp: on return, the 40bit timestamp value
 *
 * Read a 40bit timestamp value out of the timestamp block.
 */
int ice_read_phy_tstamp(struct ice_hw *hw, u8 block, u8 idx, u64 *tstamp)
{
	return ice_read_phy_tstamp_e810(hw, block, idx, tstamp);
}

/**
 * ice_clear_phy_tstamp - Clear a timestamp from the timestamp block
 * @hw: pointer to the HW struct
 * @block: the block to read from
 * @idx: the timestamp index to reset
 *
 * Clear a timestamp, resetting its valid bit, from the timestamp block.
 */
int ice_clear_phy_tstamp(struct ice_hw *hw, u8 block, u8 idx)
{
	return ice_clear_phy_tstamp_e810(hw, block, idx);
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
 * ice_is_pca9575_present
 * @hw: pointer to the hw struct
 *
 * Check if the SW IO expander is present in the netlist
 */
bool ice_is_pca9575_present(struct ice_hw *hw)
{
	u16 handle = 0;
	int status;

	if (!ice_is_e810t(hw))
		return false;

	status = ice_get_pca9575_handle(hw, &handle);

	return !status && handle;
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

	return ice_ptp_init_phc_e810(hw);
}
