/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2021, Intel Corporation. */

#ifndef _ICE_PTP_HW_H_
#define _ICE_PTP_HW_H_
#include <linux/dpll.h>

enum ice_ptp_tmr_cmd {
	ICE_PTP_INIT_TIME,
	ICE_PTP_INIT_INCVAL,
	ICE_PTP_ADJ_TIME,
	ICE_PTP_ADJ_TIME_AT_TIME,
	ICE_PTP_READ_TIME,
	ICE_PTP_NOP,
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

enum eth56g_res_type {
	ETH56G_PHY_REG_PTP,
	ETH56G_PHY_MEM_PTP,
	ETH56G_PHY_REG_XPCS,
	ETH56G_PHY_REG_MAC,
	ETH56G_PHY_REG_GPCS,
	NUM_ETH56G_PHY_RES
};

enum ice_eth56g_link_spd {
	ICE_ETH56G_LNK_SPD_1G,
	ICE_ETH56G_LNK_SPD_2_5G,
	ICE_ETH56G_LNK_SPD_10G,
	ICE_ETH56G_LNK_SPD_25G,
	ICE_ETH56G_LNK_SPD_40G,
	ICE_ETH56G_LNK_SPD_50G,
	ICE_ETH56G_LNK_SPD_50G2,
	ICE_ETH56G_LNK_SPD_100G,
	ICE_ETH56G_LNK_SPD_100G2,
	NUM_ICE_ETH56G_LNK_SPD /* Must be last */
};

/**
 * struct ice_phy_reg_info_eth56g - ETH56G PHY register parameters
 * @base: base address for each PHY block
 * @step: step between PHY lanes
 *
 * Characteristic information for the various PHY register parameters in the
 * ETH56G devices
 */
struct ice_phy_reg_info_eth56g {
	u32 base[NUM_ETH56G_PHY_RES];
	u32 step;
};

/**
 * struct ice_time_ref_info_e82x
 * @pll_freq: Frequency of PLL that drives timer ticks in Hz
 * @nominal_incval: increment to generate nanoseconds in GLTSYN_TIME_L
 *
 * Characteristic information for the various TIME_REF sources possible in the
 * E822 devices
 */
struct ice_time_ref_info_e82x {
	u64 pll_freq;
	u64 nominal_incval;
};

/**
 * struct ice_vernier_info_e82x
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
struct ice_vernier_info_e82x {
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

#define ICE_ETH56G_MAC_CFG_RX_OFFSET_INT	GENMASK(19, 9)
#define ICE_ETH56G_MAC_CFG_RX_OFFSET_FRAC	GENMASK(8, 0)
#define ICE_ETH56G_MAC_CFG_FRAC_W		9
/**
 * struct ice_eth56g_mac_reg_cfg - MAC config values for specific PTP registers
 * @tx_mode: Tx timestamp compensation mode
 * @tx_mk_dly: Tx timestamp marker start strobe delay
 * @tx_cw_dly: Tx timestamp codeword start strobe delay
 * @rx_mode: Rx timestamp compensation mode
 * @rx_mk_dly: Rx timestamp marker start strobe delay
 * @rx_cw_dly: Rx timestamp codeword start strobe delay
 * @blks_per_clk: number of blocks transferred per clock cycle
 * @blktime: block time, fixed point
 * @mktime: marker time, fixed point
 * @tx_offset: total Tx offset, fixed point
 * @rx_offset: total Rx offset, contains value for bitslip/deskew, fixed point
 *
 * All fixed point registers except Rx offset are 23 bit unsigned ints with
 * a 9 bit fractional.
 * Rx offset is 11 bit unsigned int with a 9 bit fractional.
 */
struct ice_eth56g_mac_reg_cfg {
	struct {
		u8 def;
		u8 rs;
	} tx_mode;
	u8 tx_mk_dly;
	struct {
		u8 def;
		u8 onestep;
	} tx_cw_dly;
	struct {
		u8 def;
		u8 rs;
	} rx_mode;
	struct {
		u8 def;
		u8 rs;
	} rx_mk_dly;
	struct {
		u8 def;
		u8 rs;
	} rx_cw_dly;
	u8 blks_per_clk;
	u16 blktime;
	u16 mktime;
	struct {
		u32 serdes;
		u32 no_fec;
		u32 fc;
		u32 rs;
		u32 sfd;
		u32 onestep;
	} tx_offset;
	struct {
		u32 serdes;
		u32 no_fec;
		u32 fc;
		u32 rs;
		u32 sfd;
		u32 bs_ds;
	} rx_offset;
};

extern
const struct ice_eth56g_mac_reg_cfg eth56g_mac_cfg[NUM_ICE_ETH56G_LNK_SPD];

/**
 * struct ice_cgu_pll_params_e82x - E82X CGU parameters
 * @refclk_pre_div: Reference clock pre-divisor
 * @feedback_div: Feedback divisor
 * @frac_n_div: Fractional divisor
 * @post_pll_div: Post PLL divisor
 *
 * Clock Generation Unit parameters used to program the PLL based on the
 * selected TIME_REF frequency.
 */
struct ice_cgu_pll_params_e82x {
	u32 refclk_pre_div;
	u32 feedback_div;
	u32 frac_n_div;
	u32 post_pll_div;
};

#define E810C_QSFP_C827_0_HANDLE	2
#define E810C_QSFP_C827_1_HANDLE	3
enum ice_e810_c827_idx {
	C827_0,
	C827_1
};

enum ice_phy_rclk_pins {
	ICE_RCLKA_PIN = 0,		/* SCL pin */
	ICE_RCLKB_PIN,			/* SDA pin */
};

#define ICE_E810_RCLK_PINS_NUM		(ICE_RCLKB_PIN + 1)
#define ICE_E82X_RCLK_PINS_NUM		(ICE_RCLKA_PIN + 1)
#define E810T_CGU_INPUT_C827(_phy, _pin) ((_phy) * ICE_E810_RCLK_PINS_NUM + \
					  (_pin) + ZL_REF1P)

enum ice_zl_cgu_in_pins {
	ZL_REF0P = 0,
	ZL_REF0N,
	ZL_REF1P,
	ZL_REF1N,
	ZL_REF2P,
	ZL_REF2N,
	ZL_REF3P,
	ZL_REF3N,
	ZL_REF4P,
	ZL_REF4N,
	NUM_ZL_CGU_INPUT_PINS
};

enum ice_zl_cgu_out_pins {
	ZL_OUT0 = 0,
	ZL_OUT1,
	ZL_OUT2,
	ZL_OUT3,
	ZL_OUT4,
	ZL_OUT5,
	ZL_OUT6,
	NUM_ZL_CGU_OUTPUT_PINS
};

enum ice_si_cgu_in_pins {
	SI_REF0P = 0,
	SI_REF0N,
	SI_REF1P,
	SI_REF1N,
	SI_REF2P,
	SI_REF2N,
	SI_REF3,
	SI_REF4,
	NUM_SI_CGU_INPUT_PINS
};

enum ice_si_cgu_out_pins {
	SI_OUT0 = 0,
	SI_OUT1,
	SI_OUT2,
	SI_OUT3,
	SI_OUT4,
	NUM_SI_CGU_OUTPUT_PINS
};

struct ice_cgu_pin_desc {
	char *name;
	u8 index;
	enum dpll_pin_type type;
	u32 freq_supp_num;
	struct dpll_pin_frequency *freq_supp;
};

extern const struct
ice_cgu_pll_params_e82x e822_cgu_params[NUM_ICE_TIME_REF_FREQ];

/**
 * struct ice_cgu_pll_params_e825c - E825C CGU parameters
 * @tspll_ck_refclkfreq: tspll_ck_refclkfreq selection
 * @tspll_ndivratio: ndiv ratio that goes directly to the pll
 * @tspll_fbdiv_intgr: TS PLL integer feedback divide
 * @tspll_fbdiv_frac:  TS PLL fractional feedback divide
 * @ref1588_ck_div: clock divider for tspll ref
 *
 * Clock Generation Unit parameters used to program the PLL based on the
 * selected TIME_REF/TCXO frequency.
 */
struct ice_cgu_pll_params_e825c {
	u32 tspll_ck_refclkfreq;
	u32 tspll_ndivratio;
	u32 tspll_fbdiv_intgr;
	u32 tspll_fbdiv_frac;
	u32 ref1588_ck_div;
};

extern const struct
ice_cgu_pll_params_e825c e825c_cgu_params[NUM_ICE_TIME_REF_FREQ];

#define E810C_QSFP_C827_0_HANDLE 2
#define E810C_QSFP_C827_1_HANDLE 3

/* Table of constants related to possible ETH56G PHY resources */
extern const struct ice_phy_reg_info_eth56g eth56g_phy_res[NUM_ETH56G_PHY_RES];

/* Table of constants related to possible TIME_REF sources */
extern const struct ice_time_ref_info_e82x e82x_time_ref[NUM_ICE_TIME_REF_FREQ];

/* Table of constants for Vernier calibration on E822 */
extern const struct ice_vernier_info_e82x e822_vernier[NUM_ICE_PTP_LNK_SPD];

/* Increment value to generate nanoseconds in the GLTSYN_TIME_L register for
 * the E810 devices. Based off of a PLL with an 812.5 MHz frequency.
 */
#define ICE_E810_PLL_FREQ		812500000
#define ICE_PTP_NOMINAL_INCVAL_E810	0x13b13b13bULL

/* Device agnostic functions */
u8 ice_get_ptp_src_clock_index(struct ice_hw *hw);
int ice_cgu_cfg_pps_out(struct ice_hw *hw, bool enable);
bool ice_ptp_lock(struct ice_hw *hw);
void ice_ptp_unlock(struct ice_hw *hw);
void ice_ptp_src_cmd(struct ice_hw *hw, enum ice_ptp_tmr_cmd cmd);
int ice_ptp_init_time(struct ice_hw *hw, u64 time);
int ice_ptp_write_incval(struct ice_hw *hw, u64 incval);
int ice_ptp_write_incval_locked(struct ice_hw *hw, u64 incval);
int ice_ptp_adj_clock(struct ice_hw *hw, s32 adj);
int ice_ptp_clear_phy_offset_ready_e82x(struct ice_hw *hw);
int ice_read_phy_tstamp(struct ice_hw *hw, u8 block, u8 idx, u64 *tstamp);
int ice_clear_phy_tstamp(struct ice_hw *hw, u8 block, u8 idx);
void ice_ptp_reset_ts_memory(struct ice_hw *hw);
int ice_ptp_init_phc(struct ice_hw *hw);
void ice_ptp_init_hw(struct ice_hw *hw);
int ice_get_phy_tx_tstamp_ready(struct ice_hw *hw, u8 block, u64 *tstamp_ready);
int ice_ptp_one_port_cmd(struct ice_hw *hw, u8 configured_port,
			 enum ice_ptp_tmr_cmd configured_cmd);

/* E822 family functions */
int ice_read_quad_reg_e82x(struct ice_hw *hw, u8 quad, u16 offset, u32 *val);
int ice_write_quad_reg_e82x(struct ice_hw *hw, u8 quad, u16 offset, u32 val);
void ice_ptp_reset_ts_memory_quad_e82x(struct ice_hw *hw, u8 quad);

/**
 * ice_e82x_time_ref - Get the current TIME_REF from capabilities
 * @hw: pointer to the HW structure
 *
 * Returns the current TIME_REF from the capabilities structure.
 */
static inline enum ice_time_ref_freq ice_e82x_time_ref(const struct ice_hw *hw)
{
	return hw->func_caps.ts_func_info.time_ref;
}

/**
 * ice_set_e82x_time_ref - Set new TIME_REF
 * @hw: pointer to the HW structure
 * @time_ref: new TIME_REF to set
 *
 * Update the TIME_REF in the capabilities structure in response to some
 * change, such as an update to the CGU registers.
 */
static inline void
ice_set_e82x_time_ref(struct ice_hw *hw, enum ice_time_ref_freq time_ref)
{
	hw->func_caps.ts_func_info.time_ref = time_ref;
}

static inline u64 ice_e82x_pll_freq(enum ice_time_ref_freq time_ref)
{
	return e82x_time_ref[time_ref].pll_freq;
}

static inline u64 ice_e82x_nominal_incval(enum ice_time_ref_freq time_ref)
{
	return e82x_time_ref[time_ref].nominal_incval;
}

/* E822 Vernier calibration functions */
int ice_stop_phy_timer_e82x(struct ice_hw *hw, u8 port, bool soft_reset);
int ice_start_phy_timer_e82x(struct ice_hw *hw, u8 port);
int ice_phy_cfg_tx_offset_e82x(struct ice_hw *hw, u8 port);
int ice_phy_cfg_rx_offset_e82x(struct ice_hw *hw, u8 port);
int ice_phy_cfg_intr_e82x(struct ice_hw *hw, u8 quad, bool ena, u8 threshold);

/* E810 family functions */
int ice_read_sma_ctrl(struct ice_hw *hw, u8 *data);
int ice_write_sma_ctrl(struct ice_hw *hw, u8 data);
int ice_read_pca9575_reg(struct ice_hw *hw, u8 offset, u8 *data);
int ice_ptp_read_sdp_ac(struct ice_hw *hw, __le16 *entries, uint *num_entries);
int ice_cgu_get_num_pins(struct ice_hw *hw, bool input);
enum dpll_pin_type ice_cgu_get_pin_type(struct ice_hw *hw, u8 pin, bool input);
struct dpll_pin_frequency *
ice_cgu_get_pin_freq_supp(struct ice_hw *hw, u8 pin, bool input, u8 *num);
const char *ice_cgu_get_pin_name(struct ice_hw *hw, u8 pin, bool input);
int ice_get_cgu_state(struct ice_hw *hw, u8 dpll_idx,
		      enum dpll_lock_status last_dpll_state, u8 *pin,
		      u8 *ref_state, u8 *eec_mode, s64 *phase_offset,
		      enum dpll_lock_status *dpll_state);
int ice_get_cgu_rclk_pin_info(struct ice_hw *hw, u8 *base_idx, u8 *pin_num);
int ice_cgu_get_output_pin_state_caps(struct ice_hw *hw, u8 pin_id,
				      unsigned long *caps);

/* ETH56G family functions */
int ice_ptp_read_tx_hwtstamp_status_eth56g(struct ice_hw *hw, u32 *ts_status);
int ice_stop_phy_timer_eth56g(struct ice_hw *hw, u8 port, bool soft_reset);
int ice_start_phy_timer_eth56g(struct ice_hw *hw, u8 port);
int ice_phy_cfg_intr_eth56g(struct ice_hw *hw, u8 port, bool ena, u8 threshold);
int ice_phy_cfg_ptp_1step_eth56g(struct ice_hw *hw, u8 port);

#define ICE_ETH56G_NOMINAL_INCVAL	0x140000000ULL
#define ICE_ETH56G_NOMINAL_PCS_REF_TUS	0x100000000ULL
#define ICE_ETH56G_NOMINAL_PCS_REF_INC	0x300000000ULL
#define ICE_ETH56G_NOMINAL_THRESH4	0x7777
#define ICE_ETH56G_NOMINAL_TX_THRESH	0x6

/**
 * ice_get_base_incval - Get base clock increment value
 * @hw: pointer to the HW struct
 *
 * Return: base clock increment value for supported PHYs, 0 otherwise
 */
static inline u64 ice_get_base_incval(struct ice_hw *hw)
{
	switch (hw->ptp.phy_model) {
	case ICE_PHY_ETH56G:
		return ICE_ETH56G_NOMINAL_INCVAL;
	case ICE_PHY_E810:
		return ICE_PTP_NOMINAL_INCVAL_E810;
	case ICE_PHY_E82X:
		return ice_e82x_nominal_incval(ice_e82x_time_ref(hw));
	default:
		return 0;
	}
}

static inline bool ice_is_dual(struct ice_hw *hw)
{
	return !!(hw->dev_caps.nac_topo.mode & ICE_NAC_TOPO_DUAL_M);
}

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
#define TS_CMD_RX_TYPE			ICE_M(0x18, 0x4)

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
#define Q_REG_TX_MEM_GBL_CFG_TX_TYPE_M	ICE_M(0xFF, 1)
#define Q_REG_TX_MEM_GBL_CFG_INTR_THR_M ICE_M(0x3F, 9)
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

#define P_REG_40B_LOW_M			GENMASK(7, 0)
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
#define E810_ETH_GLTSYN_CMD		0x03000344

/* Source timer incval macros */
#define INCVAL_HIGH_M			0xFF

/* Timestamp block macros */
#define TS_VALID			BIT(0)
#define TS_LOW_M			0xFFFFFFFF
#define TS_HIGH_M			0xFF
#define TS_HIGH_S			32

#define TS_PHY_LOW_M			GENMASK(7, 0)
#define TS_PHY_HIGH_M			GENMASK_ULL(39, 8)

#define BYTES_PER_IDX_ADDR_L_U		8
#define BYTES_PER_IDX_ADDR_L		4

/* Tx timestamp low latency read definitions */
#define REG_LL_PROXY_H_TIMEOUT_US	2000
#define REG_LL_PROXY_H_PHY_TMR_CMD_M	GENMASK(7, 6)
#define REG_LL_PROXY_H_PHY_TMR_CMD_ADJ	0x1
#define REG_LL_PROXY_H_PHY_TMR_CMD_FREQ	0x2
#define REG_LL_PROXY_H_TS_HIGH		GENMASK(23, 16)
#define REG_LL_PROXY_H_PHY_TMR_IDX_M	BIT(24)
#define REG_LL_PROXY_H_TS_IDX		GENMASK(29, 24)
#define REG_LL_PROXY_H_TS_INTR_ENA	BIT(30)
#define REG_LL_PROXY_H_EXEC		BIT(31)

#define REG_LL_PROXY_L			PF_SB_ATQBAH
#define REG_LL_PROXY_H			PF_SB_ATQBAL

/* Internal PHY timestamp address */
#define TS_L(a, idx) ((a) + ((idx) * BYTES_PER_IDX_ADDR_L_U))
#define TS_H(a, idx) ((a) + ((idx) * BYTES_PER_IDX_ADDR_L_U +		\
			     BYTES_PER_IDX_ADDR_L))

/* External PHY timestamp address */
#define TS_EXT(a, port, idx) ((a) + (0x1000 * (port)) +			\
				 ((idx) * BYTES_PER_IDX_ADDR_L_U))

#define LOW_TX_MEMORY_BANK_START	0x03090000
#define HIGH_TX_MEMORY_BANK_START	0x03090004

/* SMA controller pin control */
#define ICE_SMA1_DIR_EN		BIT(4)
#define ICE_SMA1_TX_EN		BIT(5)
#define ICE_SMA2_UFL2_RX_DIS	BIT(3)
#define ICE_SMA2_DIR_EN		BIT(6)
#define ICE_SMA2_TX_EN		BIT(7)

#define ICE_SMA1_MASK		(ICE_SMA1_DIR_EN | ICE_SMA1_TX_EN)
#define ICE_SMA2_MASK		(ICE_SMA2_UFL2_RX_DIS | ICE_SMA2_DIR_EN | \
				 ICE_SMA2_TX_EN)
#define ICE_ALL_SMA_MASK	(ICE_SMA1_MASK | ICE_SMA2_MASK)

#define ICE_SMA_MIN_BIT		3
#define ICE_SMA_MAX_BIT		7
#define ICE_PCA9575_P1_OFFSET	8

/* PCA9575 IO controller registers */
#define ICE_PCA9575_P0_IN	0x0

/*  PCA9575 IO controller pin control */
#define ICE_P0_GNSS_PRSNT_N	BIT(4)

/* ETH56G PHY register addresses */
/* Timestamp PHY incval registers */
#define PHY_REG_TIMETUS_L		0x8
#define PHY_REG_TIMETUS_U		0xC

/* Timestamp PCS registers */
#define PHY_PCS_REF_TUS_L		0x18
#define PHY_PCS_REF_TUS_U		0x1C

/* Timestamp PCS ref incval registers */
#define PHY_PCS_REF_INC_L		0x20
#define PHY_PCS_REF_INC_U		0x24

/* Timestamp init registers */
#define PHY_REG_RX_TIMER_INC_PRE_L	0x64
#define PHY_REG_RX_TIMER_INC_PRE_U	0x68
#define PHY_REG_TX_TIMER_INC_PRE_L	0x44
#define PHY_REG_TX_TIMER_INC_PRE_U	0x48

/* Timestamp match and adjust target registers */
#define PHY_REG_RX_TIMER_CNT_ADJ_L	0x6C
#define PHY_REG_RX_TIMER_CNT_ADJ_U	0x70
#define PHY_REG_TX_TIMER_CNT_ADJ_L	0x4C
#define PHY_REG_TX_TIMER_CNT_ADJ_U	0x50

/* Timestamp command registers */
#define PHY_REG_TX_TMR_CMD		0x40
#define PHY_REG_RX_TMR_CMD		0x60

/* Phy offset ready registers */
#define PHY_REG_TX_OFFSET_READY		0x54
#define PHY_REG_RX_OFFSET_READY		0x74

/* Phy total offset registers */
#define PHY_REG_TOTAL_TX_OFFSET_L	0x38
#define PHY_REG_TOTAL_TX_OFFSET_U	0x3C
#define PHY_REG_TOTAL_RX_OFFSET_L	0x58
#define PHY_REG_TOTAL_RX_OFFSET_U	0x5C

/* Timestamp capture registers */
#define PHY_REG_TX_CAPTURE_L		0x78
#define PHY_REG_TX_CAPTURE_U		0x7C
#define PHY_REG_RX_CAPTURE_L		0x8C
#define PHY_REG_RX_CAPTURE_U		0x90

/* Memory status registers */
#define PHY_REG_TX_MEMORY_STATUS_L	0x80
#define PHY_REG_TX_MEMORY_STATUS_U	0x84

/* Interrupt config register */
#define PHY_REG_TS_INT_CONFIG		0x88

/* XIF mode config register */
#define PHY_MAC_XIF_MODE		0x24
#define PHY_MAC_XIF_1STEP_ENA_M		ICE_M(0x1, 5)
#define PHY_MAC_XIF_TS_BIN_MODE_M	ICE_M(0x1, 11)
#define PHY_MAC_XIF_TS_SFD_ENA_M	ICE_M(0x1, 20)
#define PHY_MAC_XIF_GMII_TS_SEL_M	ICE_M(0x1, 21)

/* GPCS config register */
#define PHY_GPCS_CONFIG_REG0		0x268
#define PHY_GPCS_CONFIG_REG0_TX_THR_M	ICE_M(0xF, 24)
#define PHY_GPCS_BITSLIP		0x5C

#define PHY_TS_INT_CONFIG_THRESHOLD_M	ICE_M(0x3F, 0)
#define PHY_TS_INT_CONFIG_ENA_M		BIT(6)

/* 1-step PTP config */
#define PHY_PTP_1STEP_CONFIG		0x270
#define PHY_PTP_1STEP_T1S_UP64_M	ICE_M(0xF, 4)
#define PHY_PTP_1STEP_T1S_DELTA_M	ICE_M(0xF, 8)
#define PHY_PTP_1STEP_PEER_DELAY(_port)	(0x274 + 4 * (_port))
#define PHY_PTP_1STEP_PD_ADD_PD_M	ICE_M(0x1, 0)
#define PHY_PTP_1STEP_PD_DELAY_M	ICE_M(0x3fffffff, 1)
#define PHY_PTP_1STEP_PD_DLY_V_M	ICE_M(0x1, 31)

/* Macros to derive offsets for TimeStampLow and TimeStampHigh */
#define PHY_TSTAMP_L(x) (((x) * 8) + 0)
#define PHY_TSTAMP_U(x) (((x) * 8) + 4)

#define PHY_REG_REVISION		0x85000

#define PHY_REG_DESKEW_0		0x94
#define PHY_REG_DESKEW_0_RLEVEL		GENMASK(6, 0)
#define PHY_REG_DESKEW_0_RLEVEL_FRAC	GENMASK(9, 7)
#define PHY_REG_DESKEW_0_RLEVEL_FRAC_W	3
#define PHY_REG_DESKEW_0_VALID		GENMASK(10, 10)

#define PHY_REG_GPCS_BITSLIP		0x5C
#define PHY_REG_SD_BIT_SLIP(_port_offset)	(0x29C + 4 * (_port_offset))
#define PHY_REVISION_ETH56G		0x10200
#define PHY_VENDOR_TXLANE_THRESH	0x2000C

#define PHY_MAC_TSU_CONFIG		0x40
#define PHY_MAC_TSU_CFG_RX_MODE_M	ICE_M(0x7, 0)
#define PHY_MAC_TSU_CFG_RX_MII_CW_DLY_M	ICE_M(0x7, 4)
#define PHY_MAC_TSU_CFG_RX_MII_MK_DLY_M	ICE_M(0x7, 8)
#define PHY_MAC_TSU_CFG_TX_MODE_M	ICE_M(0x7, 12)
#define PHY_MAC_TSU_CFG_TX_MII_CW_DLY_M	ICE_M(0x1F, 16)
#define PHY_MAC_TSU_CFG_TX_MII_MK_DLY_M	ICE_M(0x1F, 21)
#define PHY_MAC_TSU_CFG_BLKS_PER_CLK_M	ICE_M(0x1, 28)
#define PHY_MAC_RX_MODULO		0x44
#define PHY_MAC_RX_OFFSET		0x48
#define PHY_MAC_RX_OFFSET_M		ICE_M(0xFFFFFF, 0)
#define PHY_MAC_TX_MODULO		0x4C
#define PHY_MAC_BLOCKTIME		0x50
#define PHY_MAC_MARKERTIME		0x54
#define PHY_MAC_TX_OFFSET		0x58

#define PHY_PTP_INT_STATUS		0x7FD140

#endif /* _ICE_PTP_HW_H_ */
