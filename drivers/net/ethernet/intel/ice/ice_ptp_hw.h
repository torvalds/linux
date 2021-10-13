/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021, Intel Corporation. */

#ifndef _ICE_PTP_HW_H_
#define _ICE_PTP_HW_H_

enum ice_ptp_tmr_cmd {
	INIT_TIME,
	INIT_INCVAL,
	ADJ_TIME,
	ADJ_TIME_AT_TIME,
	READ_TIME
};

/* Increment value to generate nanoseconds in the GLTSYN_TIME_L register for
 * the E810 devices. Based off of a PLL with an 812.5 MHz frequency.
 */
#define ICE_PTP_NOMINAL_INCVAL_E810 0x13b13b13bULL

/* Device agnostic functions */
u8 ice_get_ptp_src_clock_index(struct ice_hw *hw);
bool ice_ptp_lock(struct ice_hw *hw);
void ice_ptp_unlock(struct ice_hw *hw);
int ice_ptp_init_time(struct ice_hw *hw, u64 time);
int ice_ptp_write_incval(struct ice_hw *hw, u64 incval);
int ice_ptp_write_incval_locked(struct ice_hw *hw, u64 incval);
int ice_ptp_adj_clock(struct ice_hw *hw, s32 adj);
int ice_read_phy_tstamp(struct ice_hw *hw, u8 block, u8 idx, u64 *tstamp);
int ice_clear_phy_tstamp(struct ice_hw *hw, u8 block, u8 idx);
int ice_ptp_init_phc(struct ice_hw *hw);

/* E810 family functions */
int ice_ptp_init_phy_e810(struct ice_hw *hw);
int ice_read_sma_ctrl_e810t(struct ice_hw *hw, u8 *data);
int ice_write_sma_ctrl_e810t(struct ice_hw *hw, u8 data);
bool ice_is_pca9575_present(struct ice_hw *hw);

#define PFTSYN_SEM_BYTES	4

/* PHY timer commands */
#define SEL_CPK_SRC	8

/* Time Sync command Definitions */
#define GLTSYN_CMD_INIT_TIME		BIT(0)
#define GLTSYN_CMD_INIT_INCVAL		BIT(1)
#define GLTSYN_CMD_ADJ_TIME		BIT(2)
#define GLTSYN_CMD_ADJ_INIT_TIME	(BIT(2) | BIT(3))
#define GLTSYN_CMD_READ_TIME		BIT(7)

#define TS_CMD_MASK_E810		0xFF
#define SYNC_EXEC_CMD			0x3

/* E810 timesync enable register */
#define ETH_GLTSYN_ENA(_i)		(0x03000348 + ((_i) * 4))

/* E810 shadow init time registers */
#define ETH_GLTSYN_SHTIME_0(i)		(0x03000368 + ((i) * 32))
#define ETH_GLTSYN_SHTIME_L(i)		(0x0300036C + ((i) * 32))

/* E810 shadow time adjust registers */
#define ETH_GLTSYN_SHADJ_L(_i)		(0x03000378 + ((_i) * 32))
#define ETH_GLTSYN_SHADJ_H(_i)		(0x0300037C + ((_i) * 32))

/* E810 timer command register */
#define ETH_GLTSYN_CMD			0x03000344

/* Source timer incval macros */
#define INCVAL_HIGH_M			0xFF

/* Timestamp block macros */
#define TS_LOW_M			0xFFFFFFFF
#define TS_HIGH_S			32

#define BYTES_PER_IDX_ADDR_L_U		8

/* External PHY timestamp address */
#define TS_EXT(a, port, idx) ((a) + (0x1000 * (port)) +			\
				 ((idx) * BYTES_PER_IDX_ADDR_L_U))

#define LOW_TX_MEMORY_BANK_START	0x03090000
#define HIGH_TX_MEMORY_BANK_START	0x03090004

/* E810T SMA controller pin control */
#define ICE_SMA1_DIR_EN_E810T		BIT(4)
#define ICE_SMA1_TX_EN_E810T		BIT(5)
#define ICE_SMA2_UFL2_RX_DIS_E810T	BIT(3)
#define ICE_SMA2_DIR_EN_E810T		BIT(6)
#define ICE_SMA2_TX_EN_E810T		BIT(7)

#define ICE_SMA1_MASK_E810T	(ICE_SMA1_DIR_EN_E810T | \
				 ICE_SMA1_TX_EN_E810T)
#define ICE_SMA2_MASK_E810T	(ICE_SMA2_UFL2_RX_DIS_E810T | \
				 ICE_SMA2_DIR_EN_E810T | \
				 ICE_SMA2_TX_EN_E810T)
#define ICE_ALL_SMA_MASK_E810T	(ICE_SMA1_MASK_E810T | \
				 ICE_SMA2_MASK_E810T)

#define ICE_SMA_MIN_BIT_E810T	3
#define ICE_SMA_MAX_BIT_E810T	7
#define ICE_PCA9575_P1_OFFSET	8

#endif /* _ICE_PTP_HW_H_ */
