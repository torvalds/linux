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

enum ice_ptp_serdes {
	ICE_PTP_SERDES_1G,
	ICE_PTP_SERDES_10G,
	ICE_PTP_SERDES_25G,
	ICE_PTP_SERDES_40G,
	ICE_PTP_SERDES_50G,
	ICE_PTP_SERDES_100G
};

enum ice_ptp_link_spd {
	ICE_PTP_LNK_SPD_1G,
	ICE_PTP_LNK_SPD_10G,
	ICE_PTP_LNK_SPD_25G,
	ICE_PTP_LNK_SPD_25G_RS,
	ICE_PTP_LNK_SPD_40G,
	ICE_PTP_LNK_SPD_50G,
	ICE_PTP_LNK_SPD_50G_RS,
	ICE_PTP_LNK_SPD_100G_RS,
	NUM_ICE_PTP_LNK_SPD /* Must be last */
};

enum ice_ptp_fec_mode {
	ICE_PTP_FEC_MODE_NONE,
	ICE_PTP_FEC_MODE_CLAUSE74,
	ICE_PTP_FEC_MODE_RS_FEC
};

/**
 * struct ice_time_ref_info_e822
 * @pll_freq: Frequency of PLL that drives timer ticks in Hz
 * @nominal_incval: increment to generate nanoseconds in GLTSYN_TIME_L
 * @pps_delay: propagation delay of the PPS output signal
 *
 * Characteristic information for the various TIME_REF sources possible in the
 * E822 devices
 */
struct ice_time_ref_info_e822 {
	u64 pll_freq;
	u64 nominal_incval;
	u8 pps_delay;
};

/**
 * struct ice_vernier_info_e822
 * @tx_par_clk: Frequency used to calculate P_REG_PAR_TX_TUS
 * @rx_par_clk: Frequency used to calculate P_REG_PAR_RX_TUS
 * @tx_pcs_clk: Frequency used to calculate P_REG_PCS_TX_TUS
 * @rx_pcs_clk: Frequency used to calculate P_REG_PCS_RX_TUS
 * @tx_desk_rsgb_par: Frequency used to calculate P_REG_DESK_PAR_TX_TUS
 * @rx_desk_rsgb_par: Frequency used to calculate P_REG_DESK_PAR_RX_TUS
 * @tx_desk_rsgb_pcs: Frequency used to calculate P_REG_DESK_PCS_TX_TUS
 * @rx_desk_rsgb_pcs: Frequency used to calculate P_REG_DESK_PCS_RX_TUS
 * @tx_fixed_delay: Fixed Tx latency measured in 1/100th nanoseconds
 * @pmd_adj_divisor: Divisor used to calculate PDM alignment adjustment
 * @rx_fixed_delay: Fixed Rx latency measured in 1/100th nanoseconds
 *
 * Table of constants used during as part of the Vernier calibration of the Tx
 * and Rx timestamps. This includes frequency values used to compute TUs per
 * PAR/PCS clock cycle, and static delay values measured during hardware
 * design.
 *
 * Note that some values are not used for all link speeds, and the
 * P_REG_DESK_PAR* registers may represent different clock markers at
 * different link speeds, either the deskew marker for multi-lane link speeds
 * or the Reed Solomon gearbox marker for RS-FEC.
 */
struct ice_vernier_info_e822 {
	u32 tx_par_clk;
	u32 rx_par_clk;
	u32 tx_pcs_clk;
	u32 rx_pcs_clk;
	u32 tx_desk_rsgb_par;
	u32 rx_desk_rsgb_par;
	u32 tx_desk_rsgb_pcs;
	u32 rx_desk_rsgb_pcs;
	u32 tx_fixed_delay;
	u32 pmd_adj_divisor;
	u32 rx_fixed_delay;
};

/**
 * struct ice_cgu_pll_params_e822
 * @refclk_pre_div: Reference clock pre-divisor
 * @feedback_div: Feedback divisor
 * @frac_n_div: Fractional divisor
 * @post_pll_div: Post PLL divisor
 *
 * Clock Generation Unit parameters used to program the PLL based on the
 * selected TIME_REF frequency.
 */
struct ice_cgu_pll_params_e822 {
	u32 refclk_pre_div;
	u32 feedback_div;
	u32 frac_n_div;
	u32 post_pll_div;
};

extern const struct
ice_cgu_pll_params_e822 e822_cgu_params[NUM_ICE_TIME_REF_FREQ];

/* Table of constants related to possible TIME_REF sources */
extern const struct ice_time_ref_info_e822 e822_time_ref[NUM_ICE_TIME_REF_FREQ];

/* Table of constants for Vernier calibration on E822 */
extern const struct ice_vernier_info_e822 e822_vernier[NUM_ICE_PTP_LNK_SPD];

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

/* E822 family functions */
int ice_read_phy_reg_e822(struct ice_hw *hw, u8 port, u16 offset, u32 *val);
int ice_write_phy_reg_e822(struct ice_hw *hw, u8 port, u16 offset, u32 val);
int ice_read_quad_reg_e822(struct ice_hw *hw, u8 quad, u16 offset, u32 *val);
int ice_write_quad_reg_e822(struct ice_hw *hw, u8 quad, u16 offset, u32 val);
int ice_ptp_prep_port_adj_e822(struct ice_hw *hw, u8 port, s64 time);

/**
 * ice_e822_time_ref - Get the current TIME_REF from capabilities
 * @hw: pointer to the HW structure
 *
 * Returns the current TIME_REF from the capabilities structure.
 */
static inline enum ice_time_ref_freq ice_e822_time_ref(struct ice_hw *hw)
{
	return hw->func_caps.ts_func_info.time_ref;
}

/**
 * ice_set_e822_time_ref - Set new TIME_REF
 * @hw: pointer to the HW structure
 * @time_ref: new TIME_REF to set
 *
 * Update the TIME_REF in the capabilities structure in response to some
 * change, such as an update to the CGU registers.
 */
static inline void
ice_set_e822_time_ref(struct ice_hw *hw, enum ice_time_ref_freq time_ref)
{
	hw->func_caps.ts_func_info.time_ref = time_ref;
}

static inline u64 ice_e822_pll_freq(enum ice_time_ref_freq time_ref)
{
	return e822_time_ref[time_ref].pll_freq;
}

static inline u64 ice_e822_nominal_incval(enum ice_time_ref_freq time_ref)
{
	return e822_time_ref[time_ref].nominal_incval;
}

static inline u64 ice_e822_pps_delay(enum ice_time_ref_freq time_ref)
{
	return e822_time_ref[time_ref].pps_delay;
}

/* E822 Vernier calibration functions */
int ice_stop_phy_timer_e822(struct ice_hw *hw, u8 port, bool soft_reset);
int ice_start_phy_timer_e822(struct ice_hw *hw, u8 port, bool bypass);
int ice_phy_exit_bypass_e822(struct ice_hw *hw, u8 port);

/* E810 family functions */
int ice_ptp_init_phy_e810(struct ice_hw *hw);
int ice_read_sma_ctrl_e810t(struct ice_hw *hw, u8 *data);
int ice_write_sma_ctrl_e810t(struct ice_hw *hw, u8 data);
bool ice_is_pca9575_present(struct ice_hw *hw);

#define PFTSYN_SEM_BYTES	4

#define ICE_PTP_CLOCK_INDEX_0	0x00
#define ICE_PTP_CLOCK_INDEX_1	0x01

/* PHY timer commands */
#define SEL_CPK_SRC	8
#define SEL_PHY_SRC	3

/* Time Sync command Definitions */
#define GLTSYN_CMD_INIT_TIME		BIT(0)
#define GLTSYN_CMD_INIT_INCVAL		BIT(1)
#define GLTSYN_CMD_INIT_TIME_INCVAL	(BIT(0) | BIT(1))
#define GLTSYN_CMD_ADJ_TIME		BIT(2)
#define GLTSYN_CMD_ADJ_INIT_TIME	(BIT(2) | BIT(3))
#define GLTSYN_CMD_READ_TIME		BIT(7)

/* PHY port Time Sync command definitions */
#define PHY_CMD_INIT_TIME		BIT(0)
#define PHY_CMD_INIT_INCVAL		BIT(1)
#define PHY_CMD_ADJ_TIME		(BIT(0) | BIT(1))
#define PHY_CMD_ADJ_TIME_AT_TIME	(BIT(0) | BIT(2))
#define PHY_CMD_READ_TIME		(BIT(0) | BIT(1) | BIT(2))

#define TS_CMD_MASK_E810		0xFF
#define TS_CMD_MASK			0xF
#define SYNC_EXEC_CMD			0x3

/* Macros to derive port low and high addresses on both quads */
#define P_Q0_L(a, p) ((((a) + (0x2000 * (p)))) & 0xFFFF)
#define P_Q0_H(a, p) ((((a) + (0x2000 * (p)))) >> 16)
#define P_Q1_L(a, p) ((((a) - (0x2000 * ((p) - ICE_PORTS_PER_QUAD)))) & 0xFFFF)
#define P_Q1_H(a, p) ((((a) - (0x2000 * ((p) - ICE_PORTS_PER_QUAD)))) >> 16)

/* PHY QUAD register base addresses */
#define Q_0_BASE			0x94000
#define Q_1_BASE			0x114000

/* Timestamp memory reset registers */
#define Q_REG_TS_CTRL			0x618
#define Q_REG_TS_CTRL_S			0
#define Q_REG_TS_CTRL_M			BIT(0)

/* Timestamp availability status registers */
#define Q_REG_TX_MEMORY_STATUS_L	0xCF0
#define Q_REG_TX_MEMORY_STATUS_U	0xCF4

/* Tx FIFO status registers */
#define Q_REG_FIFO23_STATUS		0xCF8
#define Q_REG_FIFO01_STATUS		0xCFC
#define Q_REG_FIFO02_S			0
#define Q_REG_FIFO02_M			ICE_M(0x3FF, 0)
#define Q_REG_FIFO13_S			10
#define Q_REG_FIFO13_M			ICE_M(0x3FF, 10)

/* Interrupt control Config registers */
#define Q_REG_TX_MEM_GBL_CFG		0xC08
#define Q_REG_TX_MEM_GBL_CFG_LANE_TYPE_S	0
#define Q_REG_TX_MEM_GBL_CFG_LANE_TYPE_M	BIT(0)
#define Q_REG_TX_MEM_GBL_CFG_TX_TYPE_S	1
#define Q_REG_TX_MEM_GBL_CFG_TX_TYPE_M	ICE_M(0xFF, 1)
#define Q_REG_TX_MEM_GBL_CFG_INTR_THR_S	9
#define Q_REG_TX_MEM_GBL_CFG_INTR_THR_M ICE_M(0x3F, 9)
#define Q_REG_TX_MEM_GBL_CFG_INTR_ENA_S	15
#define Q_REG_TX_MEM_GBL_CFG_INTR_ENA_M	BIT(15)

/* Tx Timestamp data registers */
#define Q_REG_TX_MEMORY_BANK_START	0xA00

/* PHY port register base addresses */
#define P_0_BASE			0x80000
#define P_4_BASE			0x106000

/* Timestamp init registers */
#define P_REG_RX_TIMER_INC_PRE_L	0x46C
#define P_REG_RX_TIMER_INC_PRE_U	0x470
#define P_REG_TX_TIMER_INC_PRE_L	0x44C
#define P_REG_TX_TIMER_INC_PRE_U	0x450

/* Timestamp match and adjust target registers */
#define P_REG_RX_TIMER_CNT_ADJ_L	0x474
#define P_REG_RX_TIMER_CNT_ADJ_U	0x478
#define P_REG_TX_TIMER_CNT_ADJ_L	0x454
#define P_REG_TX_TIMER_CNT_ADJ_U	0x458

/* Timestamp capture registers */
#define P_REG_RX_CAPTURE_L		0x4D8
#define P_REG_RX_CAPTURE_U		0x4DC
#define P_REG_TX_CAPTURE_L		0x4B4
#define P_REG_TX_CAPTURE_U		0x4B8

/* Timestamp PHY incval registers */
#define P_REG_TIMETUS_L			0x410
#define P_REG_TIMETUS_U			0x414

#define P_REG_40B_LOW_M			0xFF
#define P_REG_40B_HIGH_S		8

/* PHY window length registers */
#define P_REG_WL			0x40C

#define PTP_VERNIER_WL			0x111ed

/* PHY start registers */
#define P_REG_PS			0x408
#define P_REG_PS_START_S		0
#define P_REG_PS_START_M		BIT(0)
#define P_REG_PS_BYPASS_MODE_S		1
#define P_REG_PS_BYPASS_MODE_M		BIT(1)
#define P_REG_PS_ENA_CLK_S		2
#define P_REG_PS_ENA_CLK_M		BIT(2)
#define P_REG_PS_LOAD_OFFSET_S		3
#define P_REG_PS_LOAD_OFFSET_M		BIT(3)
#define P_REG_PS_SFT_RESET_S		11
#define P_REG_PS_SFT_RESET_M		BIT(11)

/* PHY offset valid registers */
#define P_REG_TX_OV_STATUS		0x4D4
#define P_REG_TX_OV_STATUS_OV_S		0
#define P_REG_TX_OV_STATUS_OV_M		BIT(0)
#define P_REG_RX_OV_STATUS		0x4F8
#define P_REG_RX_OV_STATUS_OV_S		0
#define P_REG_RX_OV_STATUS_OV_M		BIT(0)

/* PHY offset ready registers */
#define P_REG_TX_OR			0x45C
#define P_REG_RX_OR			0x47C

/* PHY total offset registers */
#define P_REG_TOTAL_RX_OFFSET_L		0x460
#define P_REG_TOTAL_RX_OFFSET_U		0x464
#define P_REG_TOTAL_TX_OFFSET_L		0x440
#define P_REG_TOTAL_TX_OFFSET_U		0x444

/* Timestamp PAR/PCS registers */
#define P_REG_UIX66_10G_40G_L		0x480
#define P_REG_UIX66_10G_40G_U		0x484
#define P_REG_UIX66_25G_100G_L		0x488
#define P_REG_UIX66_25G_100G_U		0x48C
#define P_REG_DESK_PAR_RX_TUS_L		0x490
#define P_REG_DESK_PAR_RX_TUS_U		0x494
#define P_REG_DESK_PAR_TX_TUS_L		0x498
#define P_REG_DESK_PAR_TX_TUS_U		0x49C
#define P_REG_DESK_PCS_RX_TUS_L		0x4A0
#define P_REG_DESK_PCS_RX_TUS_U		0x4A4
#define P_REG_DESK_PCS_TX_TUS_L		0x4A8
#define P_REG_DESK_PCS_TX_TUS_U		0x4AC
#define P_REG_PAR_RX_TUS_L		0x420
#define P_REG_PAR_RX_TUS_U		0x424
#define P_REG_PAR_TX_TUS_L		0x428
#define P_REG_PAR_TX_TUS_U		0x42C
#define P_REG_PCS_RX_TUS_L		0x430
#define P_REG_PCS_RX_TUS_U		0x434
#define P_REG_PCS_TX_TUS_L		0x438
#define P_REG_PCS_TX_TUS_U		0x43C
#define P_REG_PAR_RX_TIME_L		0x4F0
#define P_REG_PAR_RX_TIME_U		0x4F4
#define P_REG_PAR_TX_TIME_L		0x4CC
#define P_REG_PAR_TX_TIME_U		0x4D0
#define P_REG_PAR_PCS_RX_OFFSET_L	0x4E8
#define P_REG_PAR_PCS_RX_OFFSET_U	0x4EC
#define P_REG_PAR_PCS_TX_OFFSET_L	0x4C4
#define P_REG_PAR_PCS_TX_OFFSET_U	0x4C8
#define P_REG_LINK_SPEED		0x4FC
#define P_REG_LINK_SPEED_SERDES_S	0
#define P_REG_LINK_SPEED_SERDES_M	ICE_M(0x7, 0)
#define P_REG_LINK_SPEED_FEC_MODE_S	3
#define P_REG_LINK_SPEED_FEC_MODE_M	ICE_M(0x3, 3)
#define P_REG_LINK_SPEED_FEC_MODE(reg)			\
	(((reg) & P_REG_LINK_SPEED_FEC_MODE_M) >>	\
	 P_REG_LINK_SPEED_FEC_MODE_S)

/* PHY timestamp related registers */
#define P_REG_PMD_ALIGNMENT		0x0FC
#define P_REG_RX_80_TO_160_CNT		0x6FC
#define P_REG_RX_80_TO_160_CNT_RXCYC_S	0
#define P_REG_RX_80_TO_160_CNT_RXCYC_M	BIT(0)
#define P_REG_RX_40_TO_160_CNT		0x8FC
#define P_REG_RX_40_TO_160_CNT_RXCYC_S	0
#define P_REG_RX_40_TO_160_CNT_RXCYC_M	ICE_M(0x3, 0)

/* Rx FIFO status registers */
#define P_REG_RX_OV_FS			0x4F8
#define P_REG_RX_OV_FS_FIFO_STATUS_S	2
#define P_REG_RX_OV_FS_FIFO_STATUS_M	ICE_M(0x3FF, 2)

/* Timestamp command registers */
#define P_REG_TX_TMR_CMD		0x448
#define P_REG_RX_TMR_CMD		0x468

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
#define TS_HIGH_M			0xFF
#define TS_HIGH_S			32

#define TS_PHY_LOW_M			0xFF
#define TS_PHY_HIGH_M			0xFFFFFFFF
#define TS_PHY_HIGH_S			8

#define BYTES_PER_IDX_ADDR_L_U		8
#define BYTES_PER_IDX_ADDR_L		4

/* Internal PHY timestamp address */
#define TS_L(a, idx) ((a) + ((idx) * BYTES_PER_IDX_ADDR_L_U))
#define TS_H(a, idx) ((a) + ((idx) * BYTES_PER_IDX_ADDR_L_U +		\
			     BYTES_PER_IDX_ADDR_L))

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
