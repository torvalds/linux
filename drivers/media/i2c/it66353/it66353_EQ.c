// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co. Ltd.
 *
 * it66353 HDMI 3 in 1 out driver.
 *
 * Author: Kenneth.Hung@ite.com.tw
 *         Wangqiang Guo <kay.guo@rock-chips.com>
 * Version: IT66353_SAMPLE_1.08
 *
 */
#include "config.h"
#include "platform.h"
#include "debug.h"
#include "it66353_drv.h"
#include "it66353_EQ.h"
#include "it66353.h"
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/printk.h>

#define Channel_B 0
#define Channel_G 1
#define Channel_R 2

#define PR_ERR(...); 	{if (dbg_level >= 0) printk(__VA_ARGS__);}
#define PR_INFO(...);	{if (dbg_level >= 1) printk(__VA_ARGS__);}
#define PR_INFO2(...);	{if (dbg_level >= 2) printk(__VA_ARGS__);}
#define PR_AEQ(...);	{if (dbg_level >= 2) printk(__VA_ARGS__);}

const u8 it66353_rs_value[] =  {0x7F, 0x7E, 0x3F, 0x3E, 0x1F, 0x1E, 0x0F,
			0x0E, 0x07, 0x06, 0x03, 0x02, 0x01, 0x00};
static u8 rs_try_idx[] = {7, 6, 8, 5, 9, 4, 10, 3, 11, 2, 12, 1, 13, 0};
static int dbg_level;
#if 0
static u8 _rx_is_h14_tmds_over_1G(void)
{
	return it66353_get_port_info1(it66353_gdev.vars.Rx_active_port, PI_PLL_HS1G);
}
#endif

void it66353_rx_DFE_enable(u8 enable)
{
	it66353_chgrxbank(3);
	if (enable)
		it66353_h2rxset(0x22, 0x40, 0x40); // Enable DFE
	else
		it66353_h2rxset(0x22, 0x40, 0x00); // Disable DFE
	it66353_chgrxbank(0);
}

void it66353_rx_set_rs_3ch(u8 *rs_value)
{
	u8 rs[3];

	if (rs_value) {
		rs[0] = rs_value[0] | 0x80;
		rs[1] = rs_value[1] | 0x80;
		rs[2] = rs_value[2] | 0x80;
		it66353_chgrxbank(3);
		it66353_h2rxbwr(0x27, 3, rs);
		rs[0] = 0xCC;
		rs[1] = 0xCC;
		rs[2] = 0xCC;
		it66353_h2rxbrd(0x27, 3, rs);
		it66353_chgrxbank(0);
		// it66353_h2rxwr(0x0f, 0x00);
	}
}

void it66353_rx_set_rs(u8 ch, u8 rs_value)
{
	rs_value |= 0x80;
	it66353_chgrxbank(3);
	it66353_h2rxwr(0x27 + ch, rs_value);
	it66353_chgrxbank(0);

}

#if 0
static u8 _rx_get_rs(u8 ch)
{
	u8 rs_value;

	it66353_chgrxbank(3);
	rs_value = it66353_h2rxrd(0x27+ch);
	it66353_chgrxbank(0);

	return rs_value;
}
#endif

void it66353_rx_clear_ced_err(void)
{
	// read CED Error from SAREQ CEDError Counter
	it66353_h2rxset(0x3B, 0x08, 0x08);
	it66353_chgrxbank(3);
	it66353_h2rxset(0x55, 0x80, 0x00);
	it66353_h2rxwr(0xe9, 0x80);
	it66353_chgrxbank(0);
}

static void _rx_ced_err_recheck(void)
{
	u8 i;
	u8 tmp;

	it66353_h2rxwr(0xB9, 0xFF);
	it66353_h2rxwr(0xBE, 0xFF);
	msleep(10);

	for (i = 0 ; i < 3 ; i++) {
		if ((it66353_gdev.vars.RxCEDErrValid&(1 << i)) &&
		    (it66353_gdev.vars.RxCEDErr[i] > 0x100)) {
			if ((it66353_h2rxrd(0x19)&0x80)) {
				tmp = it66353_h2rxrd(0xB9);
				if (0 == (tmp & (0x03<<i)))
					it66353_gdev.vars.RxCEDErr[i] = 0;
			}
		}
	}
}

void it66353_rx_update_ced_err_from_hw(void)
{
	u8 symlock;

	if (it66353_rx_is_clock_stable()) {
		symlock = it66353_h2rxrd(0x14);

		if (0 == (symlock & 0x38)) {
			it66353_gdev.vars.RxCEDErrValid = 0;
		} else {
			// read CED Error from SAREQ CEDError Counter
			it66353_h2rxset(0x3B, 0x08, 0x08);
			it66353_chgrxbank(3);
			it66353_h2rxset(0x55, 0x80, 0x00);

			if (symlock & 0x08) {
				it66353_h2rxwr(0xe9, 0x00);
				it66353_gdev.vars.RxCEDErr[0] = it66353_h2rxrd(0xEB);
				// ced valid
				if (it66353_gdev.vars.RxCEDErr[0] & 0x80) {
					it66353_gdev.vars.RxCEDErr[0] = it66353_gdev.vars.RxCEDErr[0] & 0x7F;
					it66353_gdev.vars.RxCEDErr[0] = (it66353_gdev.vars.RxCEDErr[0] << 8) +
								it66353_h2rxrd(0xEA);
					it66353_gdev.vars.RxCEDErrValid = 1;
				} else {
					it66353_gdev.vars.RxCEDErrValid = 0;
				}
			} else {
				it66353_gdev.vars.RxCEDErrValid = 0;
			}

			if (symlock & 0x10) {
				it66353_h2rxwr(0xe9, 0x20);
				it66353_gdev.vars.RxCEDErr[1] = it66353_h2rxrd(0xEB);

				if (it66353_gdev.vars.RxCEDErr[1] & 0x80) {
					it66353_gdev.vars.RxCEDErr[1] = it66353_gdev.vars.RxCEDErr[1] & 0x7F;
					it66353_gdev.vars.RxCEDErr[1] = (it66353_gdev.vars.RxCEDErr[1]<<8) +
								it66353_h2rxrd(0xEA);
					it66353_gdev.vars.RxCEDErrValid |= 0x02;
				} else {
					it66353_gdev.vars.RxCEDErrValid &= ~0x02;
				}
			} else {
				it66353_gdev.vars.RxCEDErrValid &= ~0x02;
			}

			if (symlock & 0x20) {
				it66353_h2rxwr(0xe9, 0x40);
				it66353_gdev.vars.RxCEDErr[2] = it66353_h2rxrd(0xEB);

				if (it66353_gdev.vars.RxCEDErr[2] & 0x80) {
					it66353_gdev.vars.RxCEDErr[2] = it66353_gdev.vars.RxCEDErr[2] & 0x7F;
					it66353_gdev.vars.RxCEDErr[2] = (it66353_gdev.vars.RxCEDErr[2]<<8) +
								it66353_h2rxrd(0xEA);
					it66353_gdev.vars.RxCEDErrValid |= 0x04;
				} else {
					it66353_gdev.vars.RxCEDErrValid &= ~0x04;
				}
			} else {
				it66353_gdev.vars.RxCEDErrValid &= ~0x04;
			}

			it66353_h2rxwr(0xe9, 0x80);

			it66353_chgrxbank(0);

			_rx_ced_err_recheck();

		}
	} else {
		it66353_gdev.vars.RxCEDErrValid = 0;
	}
}

static void _rx_record_ced_err(void)
{
	u8 i;

	if (it66353_gdev.EQ.ced_acc_count == 0) {
		for (i = 0 ; i < 3 ; i++) {
			it66353_gdev.EQ.ced_err_avg[i] = it66353_gdev.vars.RxCEDErr[i];
			// it66353_gdev.EQ.ced_err_avg_prev[i] = 0x8888;
		}
	} else {
		for (i = 0 ; i < 3 ; i++) {
			it66353_gdev.EQ.ced_err_avg[i] = it66353_gdev.EQ.ced_err_avg[i] +
						 it66353_gdev.vars.RxCEDErr[i];
			it66353_gdev.EQ.ced_err_avg[i] >>= 1;
		}
	}
	it66353_gdev.EQ.ced_acc_count++;
}

u8 it66353_rx_monitor_ced_err(void)
{
	static u8 err_acc;

	if (0 == it66353_rx_is_all_ch_symlock()) {
		return 0;
	}

	_rx_record_ced_err();


	if (it66353_gdev.vars.RxCEDErrValid) {
		if ((it66353_gdev.vars.RxCEDErr[0] || it66353_gdev.vars.RxCEDErr[1] ||
		    it66353_gdev.vars.RxCEDErr[2])) {
			err_acc++;

			if (err_acc > 10) {
				err_acc = 0;
				if ((it66353_gdev.vars.RxCEDErr[0] > 0x08 ||
				     it66353_gdev.vars.RxCEDErr[1] > 0x08 ||
				     it66353_gdev.vars.RxCEDErr[2] > 0x08)) {
					// return 1, means too much CED
					return 1;
				}
			}
		} else {
			err_acc = 0;
		}
	} else {
		// no ced error value for reference
		// do nothing about CED error
	}

	return 0;
}

u8 it66353_rx_is_hdmi20(void)
{
#if 1
	return it66353_gdev.vars.clock_ratio;
#else
	if (it66353_h2swrd(0x61 + it66353_gdev.vars.Rx_active_port * 3) & 0x40) {
		return 1;
	}
	return 0;
#endif
}

#if EN_H14_SKEW
void it66353_rx_skew_adj(u8 ch)
{
	static u8 idx[3] = {0};

	it66353_chgrxbank(3);
	it66353_h2rxset(0x2D, 0x1 << ch, 0x01 << ch);
	switch (idx[ch]) {
	case 0:
		it66353_h2rxset(0x2C, 0x3 << ch, 0x02 << ch);
		idx[ch] = 1;
		break;
	case 1:
		it66353_h2rxset(0x2C, 0x3 << ch, 0x03 << ch);
		idx[ch] = 2;
		break;
	case 2:
		it66353_h2rxset(0x2C, 0x3 << ch, 0x00 << ch);
		idx[ch] = 0;
		break;
	default:
		idx[ch] = 0;
		break;
	}
	it66353_chgrxbank(0);

}
#endif

void it66353_eq_save_h20(void)
{
	it66353_chgrxbank(3);
	it66353_gdev.EQ.stored_RS_20[0] = it66353_h2rxrd(0x27);
	it66353_gdev.EQ.stored_RS_20[1] = it66353_h2rxrd(0x28);
	it66353_gdev.EQ.stored_RS_20[2] = it66353_h2rxrd(0x29);
	it66353_chgrxbank(0);
}

void it66353_eq_load_h20(void)
{
	PR_INFO("enter it66353_eq_load_h20\r\n");
	it66353_rx_set_rs_3ch(it66353_gdev.EQ.stored_RS_20);
	it66353_aeq_set_DFE2(it66353_gdev.EQ.stored_RS_20[0], it66353_gdev.EQ.stored_RS_20[1],
		      it66353_gdev.EQ.stored_RS_20[2]);
	it66353_rx_DFE_enable(1);
}

void it66353_eq_save_h14(void)
{
	it66353_chgrxbank(3);
	it66353_gdev.EQ.stored_RS_14[0] = it66353_h2rxrd(0x27);
	it66353_gdev.EQ.stored_RS_14[1] = it66353_h2rxrd(0x28);
	it66353_gdev.EQ.stored_RS_14[2] = it66353_h2rxrd(0x29);
	it66353_chgrxbank(0);
}

void it66353_eq_load_h14(void)
{
	PR_INFO("enter it66353_eq_load_h14\r\n");
	it66353_rx_set_rs_3ch(it66353_gdev.EQ.stored_RS_14);
}

void it66353_eq_load_previous(void)
{
	if (it66353_rx_is_hdmi20()) {
		if (it66353_gdev.EQ.current_eq_mode == 0) {
			it66353_gdev.EQ.current_eq_mode = 1;
			it66353_eq_load_h20();
		}
	} else {
		if (it66353_gdev.EQ.current_eq_mode == 1) {
			it66353_gdev.EQ.current_eq_mode = 0;
			it66353_eq_load_h14();
		}
	}
}

void it66353_eq_load_default(void)
{
	it66353_rx_set_rs_3ch(it66353_gdev.opts.active_rx_opt->DefaultEQ);

	if (it66353_rx_is_hdmi20()) {
		it66353_rx_DFE_enable(0);
		// PR_INFO(("_eq_load_def_20\r\n"));
	} else {
		// PR_INFO(("_eq_load_def_14\r\n"));
	}
}

static u8 _eq_find_rs_index(u8 EQ_Value)
{
	u8 i = 0;
	EQ_Value &= ~0x80;
	for (i = 0; i < sizeof(it66353_rs_value); i++) {
		if (it66353_rs_value[i] == EQ_Value)
			return i;
	}

	PR_ERR("EQ %02X not found !!\r\n", EQ_Value);
	return 4;
}

void it66353_eq_reset_txoe_ready(void)
{
	it66353_gdev.EQ.txoe_ready20 = 0;
	it66353_gdev.EQ.txoe_ready14 = 0;
}

void it66353_eq_set_txoe_ready(u8 ready)
{
	if (it66353_rx_is_hdmi20())
		it66353_gdev.EQ.txoe_ready20 = ready;
	else
		it66353_gdev.EQ.txoe_ready14 = ready;

	PR_INFO("set ready 14=%d 20=%d\r\n", it66353_gdev.EQ.txoe_ready14,
		it66353_gdev.EQ.txoe_ready20);
}

u8 it66353_eq_get_txoe_ready(void)
{
	PR_INFO("get ready 14=%d 20=%d\r\n", it66353_gdev.EQ.txoe_ready14,
		it66353_gdev.EQ.txoe_ready20);
	if (it66353_rx_is_hdmi20()) {
		return it66353_gdev.EQ.txoe_ready20;
	} else {
		return it66353_gdev.EQ.txoe_ready14;
	}
}

void it66353_eq_reset_state(void)
{
	it66353_gdev.EQ.EQ_flag_20 = SysAEQ_RUN;
	it66353_gdev.EQ.EQ_flag_14 = SysAEQ_RUN;
}

void it66353_eq_set_state(u8 state)
{
	if (it66353_rx_is_hdmi20()) {
		it66353_gdev.EQ.EQ_flag_20 = state;
	} else {
		it66353_gdev.EQ.EQ_flag_14 = state;
	}

	PR_INFO("set eq flag 14=%d 20=%d\r\n", it66353_gdev.EQ.EQ_flag_14,
		it66353_gdev.EQ.EQ_flag_20);
}

u8 it66353_eq_get_state(void)
{
	PR_INFO("get eq flag 14=%d 20=%d\r\n", it66353_gdev.EQ.EQ_flag_14,
		it66353_gdev.EQ.EQ_flag_20);

	if (it66353_rx_is_hdmi20()) {
		return it66353_gdev.EQ.EQ_flag_20;
	} else {
		return it66353_gdev.EQ.EQ_flag_14;
	}
}

static u8 _aeq_ready_to_start(void)
{
	return it66353_rx_is_clock_stable();
}

void it66353_aeq_diable_eq_trigger(void)
{
	it66353_chgrxbank(3);
	it66353_h2rxset(0x22, 0x04, 0x00);
	it66353_chgrxbank(0);
}

static void _aeq_reset(void)
{
	it66353_aeq_diable_eq_trigger();

	it66353_h2rxwr(0x07, 0xff);
	it66353_h2rxwr(0x23, 0xB0);
	msleep(10);
	it66353_h2rxwr(0x23, 0xA0);
}

static void _aeq_get_DFE2(u8 ch, u8 rs_idx)
{
	u8 i;
	u8 amp_a, amp_b, amp_c, amp_d;
	u8 tap1_sign, tap1_value;
	u8 tap2_sign, tap2_value;
	u8 tap3_sign, tap3_value;
	u8 dfe_a, dfe_b, dfe_c;
	u8 reg37o, reg37;
	u8 rec[4];

	it66353_chgrxbank(3);

	i = rs_idx;

	it66353_h2rxset(0x36, 0x0F, i);
	reg37o = it66353_h2rxrd(0x37) & (~0xC0);
	PR_AEQ("RS=%02x DEF:\r\n", 0x80|it66353_rs_value[i]);

	for (ch = 0 ; ch < 3 ; ch++) {
		// hdmirxset(0x37 ,0xC0 ,ch<<6);
		reg37 = reg37o | (ch << 6);
		it66353_h2rxwr(0x37, reg37);

#if 0
		amp_a = hdmirxrd(0x5d);
		amp_b = hdmirxrd(0x5e);
		amp_c = hdmirxrd(0x5f);
		amp_d = hdmirxrd(0x60);
#else
		it66353_h2rxbrd(0x5d, 4, rec);
		amp_a = rec[0];
		amp_b = rec[1];
		amp_c = rec[2];
		amp_d = rec[3];
#endif

		if (amp_a > amp_b) {
			tap1_sign = 1;
			tap1_value = (amp_a - amp_b) >> 1;
		} else {
			tap1_sign = 0;
			tap1_value = (amp_b - amp_a) >> 1;
		}

		if (amp_a > amp_c) {
			tap2_sign = 1;
			tap2_value = (amp_a - amp_c) >> 1;
		} else {
			tap2_sign = 0;
			tap2_value = (amp_c - amp_a) >> 1;
		}

		if (amp_a > amp_d) {
			tap3_sign = 1;
			tap3_value = (amp_a - amp_d) >> 1;
		} else {
			tap3_sign = 0;
			tap3_value = (amp_d - amp_a) >> 1;
		}

		if (tap1_value > 0x1F)
			tap1_value = 0x1F;
		if (tap2_value > 0x0F)
			tap2_value = 0x0F;
		if (tap3_value > 0x07)
			tap3_value = 0x07;

		dfe_a = (0x40) + (tap1_sign << 5) + tap1_value;
		dfe_b = (0x20) + (tap2_sign << 4) + tap2_value;
		dfe_c = (0x10) + (tap3_sign << 3) + tap3_value;

		it66353_gdev.EQ.DFE[i][ch][0] = dfe_a;
		it66353_gdev.EQ.DFE[i][ch][1] = dfe_b;
		it66353_gdev.EQ.DFE[i][ch][2] = dfe_c;

		PR_AEQ(" ch%d=%02X %02X %02X |", ch, dfe_a, dfe_b, dfe_c);
	}
	PR_AEQ("\r\n");

	it66353_chgrxbank(0);

}

void it66353_aeq_set_DFE2(u8 EQ0, u8 EQ1, u8 EQ2)
{
	u8 dfe[9];
	u8 i;

	i = _eq_find_rs_index(EQ0);
	_aeq_get_DFE2(0, i);
	dfe[0] = it66353_gdev.EQ.DFE[i][Channel_B][0];
	dfe[1] = it66353_gdev.EQ.DFE[i][Channel_B][1];
	dfe[2] = it66353_gdev.EQ.DFE[i][Channel_B][2];

	i = _eq_find_rs_index(EQ1);
	_aeq_get_DFE2(1, i);
	dfe[3] = it66353_gdev.EQ.DFE[i][Channel_G][0];
	dfe[4] = it66353_gdev.EQ.DFE[i][Channel_G][1];
	dfe[5] = it66353_gdev.EQ.DFE[i][Channel_G][2];

	i = _eq_find_rs_index(EQ2);
	_aeq_get_DFE2(2, i);
	dfe[6] = it66353_gdev.EQ.DFE[i][Channel_R][0];
	dfe[7] = it66353_gdev.EQ.DFE[i][Channel_R][1];
	dfe[8] = it66353_gdev.EQ.DFE[i][Channel_R][2];

	it66353_chgrxbank(3);
	it66353_h2rxbwr(0x4B, 9, dfe);
	// it66353_h2rxbrd(0x4B, 9, dfe); // for recheck only
	for (i = 0; i < 3; i++) {
		PR_AEQ("DFE:CH%d=%02X %02X %02X\r\n",
			i, dfe[0+i*3], dfe[1+i*3], dfe[2+i*3]);
	}
	it66353_chgrxbank(0);
}

static void _aeq_get_calc_rs(void)
{
	it66353_chgrxbank(3);
	it66353_h2rxbrd(0xD5, 3, it66353_gdev.EQ.CalcRS);
	it66353_chgrxbank(0);
	PR_AEQ("AEQ RS=%02X %02X %02X\r\n",
		it66353_gdev.EQ.CalcRS[0], it66353_gdev.EQ.CalcRS[1], it66353_gdev.EQ.CalcRS[2]);
}

#if 0
static void _aeq_trigger_EQ(void)
{
	_aeq_reset();

	it66353_chgrxbank(3);

	// if 1G~3G, need to force set CSEL to 110
	if (_rx_is_h14_tmds_over_1G()) {
		PR_AEQ("over 1G\r\n");
		it66353_h2rxwr(0x20, 0x36);
		it66353_h2rxwr(0x21, 0x0E);
	} else {
		PR_AEQ("under 1G\r\n");
		it66353_h2rxwr(0x20, 0x1B);
		it66353_h2rxwr(0x21, 0x03);
	}

	it66353_h2rxwr(0x26, 0x00);
	it66353_h2rxwr(0x22, 0x38); // [5:3] AUTOAMP , AUTOEQ , EQPWDB
	it66353_h2rxset(0x22, 0x04, 0x04); // Trigger EQ
	msleep(1);
	it66353_h2rxset(0x22, 0x04, 0x00); // Trigger EQ
	it66353_chgrxbank(0);

	it66353_gdev.EQ.AutoEQ_WaitTime = 0;
}
#endif

static void _aeq_trigger_SAREQ(void)
{
	// MUST disable EQTrg before EQRst
	u8 def_opt = 1;

	it66353_chgrxbank(3);
	//  init CSC value
	it66353_h2rxwr(0x20, 0x1B);
	it66353_h2rxwr(0x21, 0x03);
	it66353_h2rxset(0x20, 0x80, 0x00);	// disable CLKStb AutoEQTrg
	it66353_h2rxwr(0x22, 0x00);	// disable [4] ENDFE, set [2] EQTrg low
	it66353_chgrxbank(0);

	_aeq_reset();

	it66353_h2rxwr(0x3B, 0x03);	// Reg_CEDOPT[5:0]

	it66353_chgrxbank(3);

	it66353_h2rxwr(0x26, 0x00);
	// it66353_h2rxwr(0x27, 0x1F);
	// it66353_h2rxwr(0x28, 0x1F);
	// it66353_h2rxwr(0x29, 0x1F);
	it66353_h2rxset(0x27, 0x80, 0x00);
	it66353_h2rxset(0x28, 0x80, 0x00);
	it66353_h2rxset(0x29, 0x80, 0x00);

	it66353_h2rxset(0x2C, 0xC0, 0xC0);
	// it66353_h2rxset(0x2D, 0xF0, 0x20);
	it66353_h2rxset(0x2D, 0xF0, 0x20+(def_opt<<4)); // rsdfeopt=1
	it66353_h2rxset(0x2D, 0x07, 0x00);
	it66353_h2rxwr(0x30, 0x8C);

	it66353_h2rxwr(0x31, 0xB0);		// AMPTime[7:0]

	it66353_h2rxwr(0x32, 0x43);
	it66353_h2rxwr(0x33, 0x47);
	it66353_h2rxwr(0x34, 0x4B);
	it66353_h2rxwr(0x35, 0x53);

	it66353_h2rxset(0x36, 0xc0, 0x00);	// [7:6] AMPTime[9:8]

	it66353_h2rxwr(0x37, 0x0B);		// [7:6] RecChannel, [4]: RSOnestage,
						// [3] IgnoreOPT, [1:0] MacthNoSel

	it66353_h2rxwr(0x38, 0xF2);		// [7:4] MonTime
	/* [5] POLBOPT, [4] ADDPClrOPT, [3:0] CED Valid Threshold 0x0D */
	it66353_h2rxwr(0x39, 0x04);
	it66353_h2rxset(0x4A, 0x80, 0x00);
	it66353_h2rxset(0x4B, 0x80, 0x00);
	it66353_h2rxset(0x54, 0x80, 0x80);	// Reg_EN_PREEQ
	it66353_h2rxset(0x54, 0x38, 0x38);

	it66353_h2rxwr(0x55, 0x40);		// RSM Threshold

	// it66353_h2rxset(0x22, 0x04, 0x04);	// Trigger EQ
	it66353_h2rxset(0x22, 0x44, 0x04+(def_opt<<6));	// Trigger EQ
	it66353_chgrxbank(0);

	it66353_gdev.EQ.AutoEQ_WaitTime = 0;
}

static void _aeq_check_amp_and_rs_status(void)
{
	u8 i;
	u8 reg63, reg6D;
	u16 reg64, reg6E;
	u16 amp_timeout;

	it66353_gdev.EQ.DFE_Valid = 1;
	it66353_gdev.EQ.RS_Valid = 1;

	it66353_chgrxbank(3);
	for (i = 0; i < 3; i++) {
		it66353_h2rxset(0x37, 0xC0, i<<6);
		reg63 = it66353_h2rxrd(0x63);
		reg64 = it66353_h2rxrd(0x64)&0x3F;
		reg6D = it66353_h2rxrd(0x6D);
		reg6E = it66353_h2rxrd(0x6E);

		it66353_gdev.EQ.RS_ValidMap[i] = (reg6E<<8) + reg6D;
		amp_timeout = (reg64<<8) + reg63;

		PR_AEQ("CH %d, AMP_TimeOut = 0x%04X \r\n", i, amp_timeout);
		PR_AEQ("CH %d, RS_ValidMap = 0x%04X \r\n", i,
			it66353_gdev.EQ.RS_ValidMap[i]);

		if (amp_timeout == 0x3FFF) {
			it66353_gdev.EQ.DFE_Valid = 0;
		}

		reg63 = it66353_h2rxrd(0x61);
		reg64 = it66353_h2rxrd(0x62) & 0x3F;
		amp_timeout = (reg64 << 8) + reg63;

		PR_AEQ("CH %d, AMP_Done = 0x%04X \r\n", i, amp_timeout);

		if (it66353_gdev.EQ.RS_ValidMap[i] == 0x0000) {
			it66353_gdev.EQ.RS_Valid = 0;
		}
	}
	it66353_chgrxbank(0);
}

u8 it66353_aeq_check_sareq_result(void)
{
	u8 tmp;

	tmp = it66353_h2rxrd(0x07);
	if (tmp & (0x20 | 0x10)) {		// EQ fail | EQ Done
		it66353_aeq_diable_eq_trigger();
		_aeq_check_amp_and_rs_status();
		if (tmp & 0x10) {
			it66353_gdev.EQ.AutoEQ_Result = EQRES_SAREQ_DONE;
		} else {
			it66353_gdev.EQ.AutoEQ_Result = EQRES_SAREQ_FAIL;
		}

		return 1;
	}
	/*
	 * else if (tmp & BIT5) {		// EQ fail
	 *	it66353_aeq_diable_eq_trigger();
	 *	_aeq_check_amp_and_rs_status();
	 *	it66353_gdev.EQ.AutoEQ_Result = EQRES_SAREQ_FAIL;
	 * }
	 */
	else {
		it66353_gdev.EQ.AutoEQ_WaitTime++;
		msleep(20);
		if (it66353_gdev.EQ.AutoEQ_WaitTime > 30) {
			it66353_gdev.EQ.AutoEQ_Result = EQRES_SAREQ_TIMEOUT;
			return 1;
		}
	}

	return 0;
}

#if 0
static u8 _aeq_check_h14eq_result(void)
{
	u8 tmp;

	tmp = it66353_h2rxrd(0x07);
	if (tmp & 0x40) {		// EQ Done
		it66353_aeq_diable_eq_trigger();
		PR_AEQ("AEQ 14, DONE\r\n");
		return EQRES_H14EQ_DONE;
	} else if (tmp & 0x80) {	// EQ fail
		it66353_aeq_diable_eq_trigger();
		PR_ERR("AEQ 14, FAIL\r\n");
		return EQRES_H14EQ_FAIL;
	} else {
		msleep(25);
		it66353_gdev.EQ.AutoEQ_WaitTime++;
		if (it66353_gdev.EQ.AutoEQ_WaitTime > 100) {
			it66353_gdev.EQ.AutoEQ_Result = EQRES_SAREQ_TIMEOUT;
			PR_ERR("AEQ 14, TIMEOUT\r\n");
			return EQRES_H14EQ_TIMEOUT;
		} else {
			// return EQRES_BUSY;
		}

	}
	return EQRES_BUSY;
}
#endif

static u8 _meq_14(void)
{
	u8 rs_idx;
	static u8 ch_rs_idx[3];
	u8 ch;
	u8 ch_all_done;
	u8 ch_done[3];
	u8 res = EQRES_H14EQ_FAIL;

	ch_done[0] = 0;
	ch_done[1] = 0;
	ch_done[2] = 0;
	ch_all_done = 0;
	ch_rs_idx[0] = 0;
	ch_rs_idx[1] = 0;
	ch_rs_idx[2] = 0;

	it66353_rx_clear_ced_err();
	msleep(50);


	for (rs_idx = 0; rs_idx < 14; rs_idx++) {
		if (0 == _aeq_ready_to_start()) {
			PR_ERR("Cannot perform EQ adjust\r\n");
			break;
		}


		it66353_rx_update_ced_err_from_hw();

		for (ch = 0; ch < 3; ch++) {
			if (ch_done[ch]) {
				// break;
			}

			if (it66353_gdev.vars.RxCEDErrValid & (1 << ch)) {
				if (it66353_gdev.vars.RxCEDErr[ch] < 0x01) {
					ch_done[ch] = 1;
					ch_all_done++;
					PR_AEQ("RS good ch%d = %02X\r\n",
						ch, it66353_rs_value[ch_rs_idx[ch]]);
				} else {
					ch_done[ch] = 0;
					PR_AEQ("RS NG ch%d = %02X, err=%04X\r\n",
						ch, it66353_rs_value[ch_rs_idx[ch]],
						it66353_gdev.vars.RxCEDErr[ch]);
				}
			} else {
				ch_done[ch] = 0;
			}
		}

		if (ch_done[0] && ch_done[1] && ch_done[2]) {
			PR_AEQ("ch_all_done\r\n");
			res = EQRES_H14EQ_DONE;
			break;
		}

		it66353_rx_clear_ced_err();
		for (ch = 0; ch < 3; ch++) {
			if (0 == ch_done[ch]) {
				it66353_rx_set_rs(ch, it66353_rs_value[rs_try_idx[ch_rs_idx[ch]]]);
				ch_rs_idx[ch]++;
				if (ch_rs_idx[ch] >= 14) {
					ch_rs_idx[ch] = 0;
				}
			}
		}

		msleep(100);
	}

	return res;
}


bool it66353_auto_eq_adjust(void)
{
	u8 result;
	u8 ret = false;

	if (_aeq_ready_to_start()) {
		it66353_h2swset(0xD4, 0x03, 0x01);	  // Set TXOE off

		if (it66353_gdev.vars.count_auto_eq_fail > 13) {
			it66353_gdev.vars.count_auto_eq_fail = 0;
			it66353_rx_caof_init(it66353_gdev.vars.Rx_active_port);
		}

		if (it66353_rx_is_hdmi20()) {
			it66353_gdev.EQ.current_eq_mode = EQ_MODE_H20;
			it66353_gdev.EQ.RS_Valid = 0;
			it66353_gdev.EQ.DFE_Valid = 0;
			_aeq_trigger_SAREQ();

			while (it66353_aeq_check_sareq_result() == 0) {
				// auto EQ will wait about 400ms here
			}

			if (it66353_gdev.EQ.DFE_Valid && it66353_gdev.EQ.RS_Valid) {
				_aeq_get_calc_rs();
				it66353_rx_set_rs_3ch(it66353_gdev.EQ.CalcRS);
				it66353_aeq_set_DFE2(it66353_gdev.EQ.CalcRS[0],
					      it66353_gdev.EQ.CalcRS[1],
					      it66353_gdev.EQ.CalcRS[2]);
				it66353_rx_DFE_enable(1);

				it66353_eq_save_h20();

				ret = true;
			} else {
				it66353_chgrxbank(3);
				it66353_h2rxset(0x27, 0x80, 0x80);
				it66353_h2rxset(0x28, 0x80, 0x80);
				it66353_h2rxset(0x29, 0x80, 0x80);
				it66353_chgrxbank(0);
			}
		} else {
			it66353_gdev.EQ.current_eq_mode = EQ_MODE_H14;

#if 0
			_aeq_trigger_EQ();

			result = _aeq_check_h14eq_result();
			while (EQRES_BUSY == result) {
				result = _aeq_check_h14eq_result();
			}

			if (result == EQRES_H14EQ_DONE) {
				_aeq_get_calc_rs();
				it66353_rx_set_rs_3ch(it66353_gdev.EQ.CalcRS);
			} else {
				result = _meq_14();
			}
#else
			result = _meq_14();
#endif

			if (result == EQRES_H14EQ_DONE) {
				it66353_eq_save_h14();
				ret = true;
			}

		}

		it66353_h2swset(0xD4, 0x03, 0x00);	  // Set DRV_RST='1'

		if (ret) {
			it66353_gdev.vars.count_auto_eq_fail = 0;
		} else {
			it66353_gdev.vars.count_auto_eq_fail++;
			PR_ERR("EQ result NG %d\r\n", it66353_gdev.vars.count_auto_eq_fail);
		}
	} else {
		PR_ERR("EQ adjust not available\r\n");
	}

	return ret;
}

MODULE_LICENSE("GPL v2");
