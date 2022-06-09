// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * it66353 HDMI 3 in 1 out driver.
 *
 * Author: Kenneth.Hung@ite.com.tw
 * 	   Wangqiang Guo <kay.guo@rock-chips.com>
 * Version: IT66353_SAMPLE_1.08
 *
 */
#ifndef _IT66353_DRV_H_
#define _IT66353_DRV_H_

#include "platform.h"
#include "it66353.h"

#define RX_PORT_0	0
#define RX_PORT_1	1
#define RX_PORT_2	2
#define RX_PORT_3	3

#define TRUE		1
#define FALSE		0

#define DDCWAITTIME	5
#define DDCWAITNUM	10

#define RX_PORT_COUNT	4

// for it66353_rx_term_power_down
#define CH0_OFF		(0x10)
#define CH1_OFF		(0x20)
#define CH2_OFF		(0x40)
#define CLK_OFF		(0x80)
#define ALLCH_OFF	(0xF0)
#define ALLCH_ON	(0x00)

/* ===================================================
 * config:
 * ===================================================
 *
 * RCLKFreqSel => 0: 20MHz, 1: 10MHz, 2: 5MHz, 3: 2.5MHz
 */
#define RCLKFreqSel 0

typedef enum {
	RX_TOGGLE_HPD,
	RX_PORT_CHANGE,
	TX_OUTPUT,
	TX_OUTPUT_PREPARE,
	RX_CHECK_EQ,
	SETUP_AFE,
	RX_WAIT_CLOCK,
	RX_HPD,
	TX_GOT_HPD,
	TX_WAIT_HPD,
	TX_UNPLUG,
	RX_UNPLUG,
	IDLE,
} _SYS_FSM_STATE;

enum {
	HDMI_MODE_AUTO,
	HDMI_MODE_14,
	HDMI_MODE_20,
};

enum {
	EQ_MODE_H14,
	EQ_MODE_H20,
};


typedef enum {
	DEV_DEVICE_LOOP,
	DEV_DEVICE_INIT,
	DEV_WAIT_DEVICE_READY,
	DEV_FW_VAR_INIT,
	DEV_WAIT_RESET,
} _DEV_FSM_STATE;

typedef enum {
	AEQ_OFF,
	AEQ_START,
	AEQ_CHECK_SAREQ_RESULT,
	AEQ_APPLY_SAREQ,
	AEQ_DONE,
	AEQ_FAIL,
	AEQ_MAX,
} _AEQ_FSM_STATE;

typedef enum {
	EQRES_UNKNOWN,
	EQRES_BUSY,
	EQRES_SAREQ_DONE,
	EQRES_SAREQ_FAIL,
	EQRES_SAREQ_TIMEOUT,
	EQRES_H14EQ_DONE,
	EQRES_H14EQ_FAIL,
	EQRES_H14EQ_TIMEOUT,
	EQRES_DONE,
} _EQ_RESULT_TYPE;

typedef enum {
	SysAEQ_OFF,
	SysAEQ_RUN,
	SysAEQ_DONE,
} _SYS_AEQ_TYPE;

enum {
	EDID_SRC_EXT_SINK,
	EDID_SRC_INTERNAL,
};

enum {
	TERM_LOW,
	TERM_HIGH,
	TERM_FOLLOW_TX,
	TERM_FOLLOW_HPD,
};

#define EDID_PORT_0	0x01
#define EDID_PORT_1	0x02
#define EDID_PORT_2	0x04
#define EDID_PORT_3	0x08
#define EDID_PORT_ALL (EDID_PORT_0 | EDID_PORT_1 | EDID_PORT_2 | EDID_PORT_3)

/*
 * for it66353_get_port_info0()
 */
#define PI_5V		(BIT(0))
#define PI_HDMI_MODE	(BIT(1))
#define PI_CLK_DET	(BIT(2))
#define PI_CLK_VALID	(BIT(3))
#define PI_CLK_STABLE	(BIT(4))
#define PI_PLL_LOCK	(BIT(5))
// #define PI_XX		(BIT(6))
#define PI_SYM_LOCK	(BIT(7))

/*
 * for it66353_get_port_info1()
 */
#define PI_PLL_HS1G	0x01
// #define PI_PLL_HS1G (BIT0)

typedef struct {
	// TxSwap
	u8 EnTxPNSwap;
	u8 EnTxChSwap;
	u8 EnTxVCLKInv;
	u8 EnTxOutD1t;

	u8 EnRxDDCBypass;
	u8 EnRxPWR5VBypass;
	u8 EnRxHPDBypass;

	u8 EnCEC;

	u8 EnableAutoEQ;
	u8 ParseEDIDFromSink;
	u8 NonActivePortReplyHPD;
	u8 DisableEdidRam;
	u8 TryFixedEQFirst;
	u8 TurnOffTx5VWhenSwitchPort;
	u8 FixIncorrectHdmiEnc;

} IT6635_DEVICE_OPTION_INT;

typedef struct {
	u8 tag1;
	u8 EnRxDDCBypass;
	u8 EnRxPWR5VBypass;
	u8 EnRxHPDBypass;
	u8 TryFixedEQFirst;
	u8 EnableAutoEQ;
	u8 NonActivePortReplyHPD;
	u8 DisableEdidRam;
	u8 DefaultEQ[3];
	u8 FixIncorrectHdmiEnc;
	u8 HPDOutputInverse;
	u8 HPDTogglePeriod;
	u8 TxOEAlignment;
	u8 str_size;

} IT6635_RX_OPTIONS;

typedef struct {
	u8 tag1;
	// TxSwap
	u8 EnTxPNSwap;
	u8 EnTxChSwap;
	u8 EnTxVCLKInv;
	u8 EnTxOutD1t;
	u8 CopyEDIDFromSink;
	u8 ParsePhysicalAddr;
	u8 TurnOffTx5VWhenSwitchPort;
	u8 str_size;

} IT6635_TX_OPTIONS;

typedef struct {
	u8 tag1;
	u8 SwAddr;
	u8 RxAddr;
	u8 CecAddr;
	u8 EdidAddr;
	u8 ForceRxOn;
	u8 RxAutoPowerDown;
	u8 DoTxPowerDown;
	u8 TxPowerDownWhileWaitingClock;
	u8 str_size;

} IT6635_DEV_OPTION;

typedef struct {
	IT6635_RX_OPTIONS *active_rx_opt;
	IT6635_RX_OPTIONS *rx_opt[4];
	IT6635_TX_OPTIONS *tx_opt;
	IT6635_DEV_OPTION *dev_opt;

} IT6635_DEV_OPTION_INTERNAL;

typedef struct {
	struct {
		u8 Rev;
		u32 RCLK;
		u8 RxHPDFlag[4];

		u8 VSDBOffset;  // 0xFF;

		u8 PhyAdr[4];
		u8 EdidChkSum[2];

		_SYS_FSM_STATE state_sys_fsm;
		u8 state_dev_init;
		u8 state_dev;
		u8 fsm_return;
		u8 Rx_active_port;
		u8 Rx_new_port;
		u8 Tx_current_5v;
		u32 vclk;
		u32 vclk_prev;

		u16 RxCEDErr[3];
		u8 RxCEDErrValid;
		u16 RxCEDErrRec[3][3];

		u8 count_unlock;
		u8 count_symlock;
		u8 count_symlock_lost;
		u8 count_symlock_fail;
		u8 count_symlock_unstable;
		u8 count_fsm_err;
		u8 count_eq_check;
		u8 count_try_force_hdmi_mode;
		u8 count_auto_eq_fail;
		u8 count_wait_clock;
		u8 clock_ratio;
		u8 h2_scramble;
		u8 edid_ready;
		u8 prev_hpd_state;

		u8 try_fixed_EQ;
		u8 current_hdmi_mode;
		u8 current_txoe;
		u8 check_for_hpd_toggle;
		u8 sdi_stable_count;
		u8 check_for_sdi;
		u8 force_hpd_state;
		// u8 txoe_alignment;
		u8 hpd_toggle_timeout;
		u8 spmon;

		__tick tick_set_afe;
		__tick tick_hdcp;
		// u8 en_count_hdcp;
		u8 *default_edid[4];

		// tx
		u8 hpd_wait_count;
		u8 is_hdmi20_sink;
		u8 rx_deskew_err;
	} vars;

	struct {
		_SYS_AEQ_TYPE sys_aEQ;
		u8 AutoEQ_state;
		u8 AutoEQ_WaitTime;
		u8 AutoEQ_Result;
		u8 DFE_Valid;
		u8 RS_Valid;
		u16 RS_ValidMap[3];
		u8 EqHDMIMode;
		u8 ManuEQ_state;
		u8 DFE[14][3][3]; // [RS_value][channel012][NumABC]  -> 0x34B...0x353
		u8 CalcRS[3];

		u8 EQ_flag_14;
		u8 EQ_flag_20;
		u8 txoe_ready14;
		u8 txoe_ready20;
		u8 stored_RS_14[3];
		u8 stored_RS_20[3];
		u8 current_eq_mode;

		// u8 FixedRsIndex[4];

		u8 meq_cur_idx;

		u8 meq_adj_idx[3];
		u32 ced_err_avg[3];
		u32 ced_err_avg_prev[3];
		u8  ced_acc_count;
		u8  manu_eq_fine_tune_count[3];
		u8  manu_eq_fine_tune_best_rs[3];

	} EQ;

	// u8 edid_buf[128];

	IT6635_DEV_OPTION_INTERNAL opts;

} IT6635_DEVICE_DATA;

extern IT6635_DEVICE_DATA it66353_gdev;
extern const u8 it66353_rs_value[];
extern IT6635_RX_OPTIONS it66353_s_RxOpts;
extern IT6635_TX_OPTIONS it66353_s_TxOpts;
extern IT6635_DEV_OPTION it66353_s_DevOpts;
extern u8 it66353_s_default_edid_port0[];


#ifdef __cplusplus
extern "C" {
#endif

// --------------------------------
extern u8 it66353_h2swwr(u8 offset, u8 wdata);
extern u8 it66353_h2swrd(u8 offset);
extern u8 it66353_h2swset(u8 offset, u8 mask, u8 wdata);
extern void it66353_h2swbrd(u8 offset, u8 length, u8 *rddata);
extern void it66353_h2swbwr(u8 offset, u8 length, u8 *rddata);

extern u8 it66353_h2rxwr(u8 offset, u8 wdata);
extern u8 it66353_h2rxrd(u8 offset);
extern u8 it66353_h2rxset(u8 offset, u8 mask, u8 dwata);
extern void it66353_h2rxbrd(u8 offset, u8 length, u8 *rddata);
extern void it66353_h2rxbwr(u8 offset, u8 length, u8 *rddata);

extern u8 it66353_cecwr(u8 offset, u8 wdata);
extern u8 it66353_cecrd(u8 offset);
extern u8 it66353_cecset(u8 offset, u8 mask, u8 wdata);
extern void it66353_cecbrd(u8 offset, u8 length, u8 *rddata);
extern void it66353_cecbwr(u8 offset, u8 length, u8 *rddata);

extern u8 it66353_h2rxedidwr(u8 offset, u8 *wrdata, u8 length);

extern void it66353_chgrxbank(u8 bankno);
extern void it66353_chgswbank(u8 bankno);

extern void it66353_rx_update_ced_err_from_hw(void);
extern void it66353_rx_get_ced_err(void);
extern void it66353_rx_clear_ced_err(void);
extern u8 it66353_rx_monitor_ced_err(void);
extern void it66353_rx_DFE_enable(u8 enable);
extern void it66353_rx_set_rs_3ch(u8 *rs_value);
extern void it66353_rx_set_rs(u8 ch, u8 rs_value);

extern u8 it66353_rx_is_all_ch_symlock(void);
extern u8 it66353_rx_is_ch_symlock(u8 ch);
extern u8 it66353_rx_is_clock_stable(void);

extern void it66353_rx_ovwr_hdmi_clk(u8 port, u8 ratio);
extern void it66353_rx_ovwr_h20_scrb(u8 port, u8 scrb);

extern void it66353_rx_auto_power_down_enable(u8 port, u8 enable);
extern void it66353_rx_term_power_down(u8 port, u8 channel);
extern void it66353_rx_handle_output_err(void);

extern void it66353_sw_enable_timer0(void);
extern void it66353_sw_disable_timer0(void);
extern u8 it66353_sw_get_timer0_interrupt(void);

extern void it66353_sw_clear_hdcp_status(void);
// --------------------------------
extern void it66353_txoe(u8 enable);
extern void it66353_auto_detect_hdmi_encoding(void);
extern void it66353_fix_incorrect_hdmi_encoding(void);

extern u8 it66353_get_port_info1(u8 port, u8 info);
extern u8 it66353_get_port_info0(u8 port, u8 info);

extern void it66353_init_rclk(void);
extern void it66353_enable_tx_port(u8 enable);
// --------------
extern void it66353_sys_state(u8 new_state);
extern void it66353_rx_reset(void);
extern void it66353_rx_caof_init(u8 port);

extern void it66353_eq_save_h20(void);
extern void it66353_eq_load_h20(void);
extern void it66353_eq_save_h14(void);
extern void it66353_eq_load_h14(void);
extern void it66353_eq_load_previous(void);
extern void it66353_eq_load_default(void);

extern void it66353_eq_reset_state(void);
extern void it66353_eq_set_state(u8 state);
extern u8 it66353_eq_get_state(void);
extern void it66353_eq_reset_txoe_ready(void);
extern void it66353_eq_set_txoe_ready(u8 ready);
extern u8 it66353_eq_get_txoe_ready(void);

extern void it66353_aeq_set_DFE2(u8 EQ0, u8 EQ1, u8 EQ2);
extern u8 it66353_rx_is_hdmi20(void);
extern void it66353_aeq_diable_eq_trigger(void);
extern u8 it66353_aeq_check_sareq_result(void);

#if DEBUG_FSM_CHANGE
#define it66353_fsm_chg(new_state)	__it66353_fsm_chg(new_state, __LINE__)
#define it66353_fsm_chg_delayed(new_state)	__it66353_fsm_chg2(new_state, __LINE__)
#else
extern void it66353_fsm_chg(u8 new_state);
extern void it66353_fsm_chg_delayed(u8 new_state);
#endif

extern void __it66353_fsm_chg(u8 new_state, int caller);
extern void __it66353_fsm_chg2(u8 new_state, int caller);
// void it66353_vars_init(void);
extern bool it66353_device_init(void);
extern bool it66353_device_init2(void);

extern bool it66353_read_edid(u8 block, u8 offset, int length, u8 *edid_buffer);
extern bool it66353_write_one_block_edid(u8 block, u8 *edid_buffer);
extern bool it66353_setup_edid_ram(u8 flag);

extern void it66353_force_hdmi20(void);

#ifdef __cplusplus
}
#endif

extern void it66353_rx_skew_adj(u8 ch);
#define _rx_edid_address_enable(port)\
		{it66353_h2swset(0x55 + port, 0x24, 0x20); }
#define _rx_edid_address_disable(port)\
		{it66353_h2swset(0x55 + port, 0x24, 0x04); }
#define _rx_edid_ram_enable(port)\
		{if (it66353_gdev.opts.rx_opt[port]->EnRxDDCBypass == 0) { it66353_h2swset(0x55 + port, 0x01, 0x00); }}
#define _rx_edid_ram_disable(port)\
		{ it66353_h2swset(0x55 + port, 0x01, 0x01); }
#define _rx_edid_set_chksum(port, sum)\
		{ it66353_h2swwr(0xe1 + port * 2, sum);  }
#define _rx_edid_set_cec_phyaddr(port, phyAB, phyCD)\
		{ it66353_h2swwr(0xd9 + port*2, phyAB); it66353_h2swwr(0xda + port*2, phyCD);  }

#endif